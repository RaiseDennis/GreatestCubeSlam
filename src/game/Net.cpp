#include "game/Net.hpp"

#include <algorithm>
#include <cstdlib>

namespace game {

namespace {

constexpr std::size_t kMaxOutbox = 600; // ~10 s of snapshots: beyond that the peer is not keeping up

// ---- small serialization helpers ----
void put(sf::Packet& p, Vec2 v) { p << v.x << v.y; }
void get(sf::Packet& p, Vec2& v) { p >> v.x >> v.y; }

template <typename E>
void putEnum(sf::Packet& p, E e) { p << std::uint8_t(e); }
template <typename E>
void getEnum(sf::Packet& p, E& e) {
    std::uint8_t v = 0;
    p >> v;
    e = E(v);
}

void putSide(sf::Packet& p, int side) { p << std::int8_t(side); }
void getSide(sf::Packet& p, int& side) {
    std::int8_t v = -1;
    p >> v;
    side = v;
}

/** Reads a list length and sanity-checks it before anyone resizes a vector with it. */
bool getCount(sf::Packet& p, std::uint16_t& n, std::uint16_t max) {
    p >> n;
    return bool(p) && n <= max;
}

} // namespace

// ---------------------------------------------------------------- connection

bool Net::host(unsigned short port) {
    close();
    listener_.setBlocking(false);
    if (listener_.listen(port) != sf::Socket::Status::Done) {
        fail("Port " + std::to_string(port) + " is already in use");
        return false;
    }
    port_ = port;
    status_ = Status::Listening;
    return true;
}

void Net::connect(const std::string& address, unsigned short port) {
    close();
    port_ = port;
    status_ = Status::Connecting;
    connecting_ = std::async(std::launch::async, [this, address, port] {
        auto resolved = sf::Dns::resolve(address);
        if (!resolved || resolved->empty()) return sf::Socket::Status::Error;
        // The host listens on IPv4, so try those addresses first.
        std::stable_sort(resolved->begin(), resolved->end(), [](const sf::IpAddress& a, const sf::IpAddress& b) {
            return a.isV4() && !b.isV4();
        });
        sf::Socket::Status st = sf::Socket::Status::Error;
        for (const sf::IpAddress& ip : *resolved) {
            socket_.setBlocking(true);
            st = socket_.connect(ip, port, sf::seconds(4));
            if (st == sf::Socket::Status::Done) break;
        }
        return st;
    });
}

void Net::close() {
    if (connecting_.valid()) connecting_.wait();
    connecting_ = {};
    socket_.disconnect();
    listener_.close();
    outbox_.clear();
    status_ = Status::Idle;
}

void Net::fail(std::string why) {
    socket_.disconnect();
    listener_.close();
    outbox_.clear();
    status_ = Status::Failed;
    error_ = std::move(why);
}

void Net::poll() {
    if (status_ == Status::Listening) {
        if (listener_.accept(socket_) == sf::Socket::Status::Done) {
            listener_.close(); // one opponent only
            socket_.setBlocking(false);
            status_ = Status::Connected;
            error_.clear();
        }
    } else if (status_ == Status::Connecting) {
        if (connecting_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        sf::Socket::Status st = connecting_.get();
        if (st != sf::Socket::Status::Done) {
            fail("Could not connect");
            return;
        }
        socket_.setBlocking(false);
        status_ = Status::Connected;
        error_.clear();
    }

    while (status_ == Status::Connected && !outbox_.empty()) {
        // On Partial, SFML remembers how much of the packet went out; resend the same packet later.
        sf::Socket::Status st = socket_.send(outbox_.front());
        if (st == sf::Socket::Status::Done) outbox_.pop_front();
        else if (st == sf::Socket::Status::Partial || st == sf::Socket::Status::NotReady) break;
        else fail("Connection lost");
    }
    if (outbox_.size() > kMaxOutbox) fail("Connection too slow");
}

void Net::send(sf::Packet packet) {
    if (status_ != Status::Connected) return;
    outbox_.push_back(std::move(packet));
}

std::optional<sf::Packet> Net::receive() {
    if (status_ != Status::Connected) return std::nullopt;
    sf::Packet packet;
    switch (socket_.receive(packet)) {
    case sf::Socket::Status::Done: return packet;
    case sf::Socket::Status::NotReady:
    case sf::Socket::Status::Partial: return std::nullopt;
    default: fail("Opponent disconnected"); return std::nullopt;
    }
}

bool parseAddress(const std::string& text, std::string& host, unsigned short& port) {
    std::string s = text;
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == ' ' || c == '\t'; }), s.end());
    port = DefaultPort;
    std::string portText;
    if (!s.empty() && s[0] == '[') { // [ipv6]:port
        auto end = s.find(']');
        if (end == std::string::npos) return false;
        host = s.substr(1, end - 1);
        if (end + 1 < s.size() && s[end + 1] == ':') portText = s.substr(end + 2);
    } else if (std::count(s.begin(), s.end(), ':') == 1) { // host:port
        auto colon = s.find(':');
        host = s.substr(0, colon);
        portText = s.substr(colon + 1);
    } else {
        host = s; // plain host or bare IPv6
    }
    if (!portText.empty()) {
        long v = std::strtol(portText.c_str(), nullptr, 10);
        if (v <= 0 || v > 65535) return false;
        port = static_cast<unsigned short>(v);
    }
    return !host.empty();
}

// ---------------------------------------------------------------- protocol

sf::Packet& operator<<(sf::Packet& p, const Flow& f) {
    return p << f.state << f.paused << f.levelIndex << f.levelSerial << f.worldId << f.setName << f.scores[0] << f.scores[1]
             << f.round << f.serve << f.roundLoser;
}

sf::Packet& operator>>(sf::Packet& p, Flow& f) {
    return p >> f.state >> f.paused >> f.levelIndex >> f.levelSerial >> f.worldId >> f.setName >> f.scores[0] >> f.scores[1] >>
           f.round >> f.serve >> f.roundLoser;
}

void writeSnapshot(sf::Packet& p, const World& w) {
    p << std::int32_t(w.frame) << w.roundOver;
    putSide(p, w.roundLoser);
    put(p, w.roundOverPos);
    p << w.deathballTimer;

    p << std::uint16_t(w.pucks.size());
    for (auto& k : w.pucks) {
        put(p, k.pos);
        put(p, k.vel);
        putSide(p, k.lastHit);
        p << k.ghostTimer << k.ghostballTimer << std::int8_t(k.fireball) << k.timebombTimer;
    }
    for (auto& pd : w.paddles)
        p << pd.x << pd.prevX << pd.resizeTimer << pd.shrinkTimer << pd.fireball << pd.dizzyTimer << pd.laserTimer << pd.hitPulse;
    for (auto& pl : w.players) {
        p << std::uint16_t(pl.shields.size());
        for (int s : pl.shields) p << std::uint8_t(s);
        p << pl.bulletproofTimer << pl.mirroredTimer << pl.fogTimer;
    }
    p << std::uint16_t(w.shields.size());
    for (auto& s : w.shields) {
        p << s.x << s.y << s.w << s.h << std::int16_t(s.index) << s.flash;
        putSide(p, s.side);
    }
    p << std::uint16_t(w.obstacles.size());
    for (auto& o : w.obstacles) p << o.alive << o.flash;
    p << std::uint16_t(w.forces.size());
    for (auto& f : w.forces) p << f.active << f.toggleTimer;
    p << std::uint16_t(w.extras.size());
    for (auto& e : w.extras) {
        putEnum(p, e.def.type);
        put(p, e.pos);
        p << e.age << e.lifetime << e.alive << std::int32_t(e.id);
    }
    p << std::uint16_t(w.bullets.size());
    for (auto& b : w.bullets) {
        put(p, b.pos);
        put(p, b.vel);
        putSide(p, b.owner);
    }
    p << std::uint16_t(w.events.size());
    for (auto& e : w.events) {
        putEnum(p, e.type);
        put(p, e.pos);
        putSide(p, e.side);
        putEnum(p, e.extra);
    }
}

bool readSnapshot(sf::Packet& p, World& w) {
    std::int32_t frame = 0;
    std::uint16_t n = 0;
    p >> frame >> w.roundOver;
    w.frame = frame;
    getSide(p, w.roundLoser);
    get(p, w.roundOverPos);
    p >> w.deathballTimer;

    if (!getCount(p, n, 64)) return false;
    w.pucks.assign(n, Puck{});
    for (auto& k : w.pucks) {
        std::int8_t fire = 0;
        get(p, k.pos);
        get(p, k.vel);
        getSide(p, k.lastHit);
        p >> k.ghostTimer >> k.ghostballTimer >> fire >> k.timebombTimer;
        k.fireball = fire;
    }
    for (auto& pd : w.paddles)
        p >> pd.x >> pd.prevX >> pd.resizeTimer >> pd.shrinkTimer >> pd.fireball >> pd.dizzyTimer >> pd.laserTimer >> pd.hitPulse;
    for (auto& pl : w.players) {
        if (!getCount(p, n, 64)) return false;
        pl.shields.assign(n, 0);
        for (int& s : pl.shields) {
            std::uint8_t v = 0;
            p >> v;
            s = v;
        }
        p >> pl.bulletproofTimer >> pl.mirroredTimer >> pl.fogTimer;
    }
    if (!getCount(p, n, 128)) return false;
    w.shields.assign(n, Shield{});
    for (auto& s : w.shields) {
        std::int16_t index = 0;
        p >> s.x >> s.y >> s.w >> s.h >> index >> s.flash;
        s.index = index;
        getSide(p, s.side);
    }
    // Obstacles and forces come from the level layout; only their dynamic state is sent.
    if (!getCount(p, n, 256) || n != w.obstacles.size()) return false;
    for (auto& o : w.obstacles) p >> o.alive >> o.flash;
    if (!getCount(p, n, 64) || n != w.forces.size()) return false;
    for (auto& f : w.forces) p >> f.active >> f.toggleTimer;
    if (!getCount(p, n, 64)) return false;
    w.extras.assign(n, Extra{});
    for (auto& e : w.extras) {
        std::int32_t id = 0;
        getEnum(p, e.def.type);
        get(p, e.pos);
        p >> e.age >> e.lifetime >> e.alive >> id;
        e.id = id;
    }
    if (!getCount(p, n, 256)) return false;
    w.bullets.assign(n, Bullet{});
    for (auto& b : w.bullets) {
        get(p, b.pos);
        get(p, b.vel);
        getSide(p, b.owner);
    }
    if (!getCount(p, n, 1024)) return false;
    w.events.assign(n, Event{});
    for (auto& e : w.events) {
        getEnum(p, e.type);
        get(p, e.pos);
        getSide(p, e.side);
        getEnum(p, e.extra);
        if (e.type > EventType::RoundOver || e.extra > ExtraType::Random) return false;
    }
    for (auto& e : w.extras)
        if (e.def.type > ExtraType::Random) return false;
    return bool(p);
}

} // namespace game
