#include "game/Net.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace game {

namespace {

// ---- wire format: every datagram starts with magic, version, kind, sender nonce ----
constexpr std::uint32_t kWireMagic = 0x43534C55; // "CSLU"
constexpr std::uint8_t kWireVersion = 1;
enum Kind : std::uint8_t {
    Connect = 1, // joiner -> host, repeated until accepted: u8 viaBroadcast, u32 wantIp, u16 wantPort
    Accept,      // host -> joiner: u32 joiner nonce
    Punch,       // host -> joiner's code, repeated: opens the host's router towards the joiner. u8 fromHost
    Data,        // u8 reliable, u32 seq, u32 ack, payload
    Ping,        // u32 ack (keepalive)
    Bye,         // leaving
};

constexpr float kKnockInterval = 0.25f; // Connect / Punch resend
constexpr float kConnectTimeout = 45;   // long enough for the host to type the joiner's code
constexpr float kPingInterval = 0.1f;
constexpr float kResendAfter = 0.15f;
constexpr float kPeerTimeout = 8;
constexpr std::size_t kMaxUnacked = 2000; // ~30 s of reliable traffic the peer never confirmed

// ---- STUN (RFC 5389): "what address do my packets come from?" ----
struct StunServer {
    const char* host;
    unsigned short port;
};
const StunServer kStunServers[] = {{"stun.l.google.com", 19302}, {"stun1.l.google.com", 19302}, {"stun.cloudflare.com", 3478}};
constexpr std::uint32_t kStunCookie = 0x2112A442;
constexpr int kStunRounds = 8;
constexpr float kStunInterval = 0.3f;

std::uint16_t be16(const std::uint8_t* p) { return std::uint16_t(p[0] << 8 | p[1]); }
std::uint32_t be32(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; }

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

// ---- join codes ----
const char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"; // Crockford base32

std::uint64_t mix(std::uint64_t x) { // splitmix64
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

int codeDigit(char c) {
    c = char(std::toupper(static_cast<unsigned char>(c)));
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    const char* p = c ? std::strchr(kAlphabet, c) : nullptr;
    return p ? int(p - kAlphabet) : -1;
}

struct CodeLayout {
    int chars, dataBits, checkBits;
};
constexpr CodeLayout kShortCode{8, 32, 8};  // ip, port = DefaultPort
constexpr CodeLayout kLongCode{12, 48, 12}; // ip + port

std::uint64_t bits(int n) { return (std::uint64_t(1) << n) - 1; }
std::uint64_t codeCheck(std::uint64_t data, const CodeLayout& l) { return mix(data * 2 + (l.chars == 12)) & bits(l.checkBits); }
// Scrambles the data with its checksum so neighbouring addresses don't get look-alike codes.
std::uint64_t codeMask(std::uint64_t check, const CodeLayout& l) { return mix(check ^ 0xC0BE5ull) & bits(l.dataBits); }

// ---- UPnP: ask the router to forward our port ----
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

/** Text between <tag> and </tag>, searching from `from`. */
std::string xmlValue(const std::string& xml, const std::string& tag, std::size_t from = 0) {
    auto b = xml.find("<" + tag + ">", from);
    if (b == std::string::npos) return "";
    b += tag.size() + 2;
    auto e = xml.find("</" + tag + ">", b);
    return e == std::string::npos ? "" : trim(xml.substr(b, e - b));
}

bool splitUrl(const std::string& url, std::string& host, unsigned short& port, std::string& path) {
    if (lower(url.substr(0, 7)) != "http://") return false;
    std::string rest = url.substr(7);
    auto slash = rest.find('/');
    std::string hostPort = rest.substr(0, slash);
    path = slash == std::string::npos ? "/" : rest.substr(slash);
    auto colon = hostPort.find(':');
    host = hostPort.substr(0, colon);
    port = 80;
    if (colon != std::string::npos) {
        long v = std::strtol(hostPort.c_str() + colon + 1, nullptr, 10);
        if (v <= 0 || v > 65535) return false;
        port = static_cast<unsigned short>(v);
    }
    return !host.empty();
}

sf::Http::Request httpRequest(const std::string& host, unsigned short port, const std::string& path) {
    sf::Http::Request r(path);
    r.setHttpVersion(1, 1);
    r.setField("Host", host + ":" + std::to_string(port));
    r.setField("Connection", "close");
    return r;
}

/** SSDP multicast search for Internet Gateway Devices; returns their description URLs. */
std::vector<std::string> ssdpSearch() {
    sf::UdpSocket s;
    if (s.bind(sf::Socket::AnyPort) != sf::Socket::Status::Done) return {};
    s.setBlocking(false);
    const sf::IpAddress group(239, 255, 255, 250);
    const char* types[] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                           "urn:schemas-upnp-org:service:WANIPConnection:1"};
    std::vector<std::string> locations;
    std::vector<char> buf(4096);
    sf::Clock clock;
    float firstReply = -1;
    int rounds = 0;
    while (true) {
        const float t = clock.getElapsedTime().asSeconds();
        if (t > 2 || (firstReply >= 0 && t - firstReply > 0.3f)) break;
        if (rounds < 2 && t >= rounds * 0.6f) { // twice: multicast is easily lost
            for (const char* st : types) {
                std::string msg = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: ") +
                                  st + "\r\n\r\n";
                (void)s.send(msg.data(), msg.size(), group, 1900);
            }
            ++rounds;
        }
        std::size_t n = 0;
        std::optional<sf::IpAddress> from;
        unsigned short fromPort = 0;
        while (s.receive(buf.data(), buf.size(), n, from, fromPort) == sf::Socket::Status::Done) {
            std::string reply(buf.data(), n);
            auto at = lower(reply).find("\nlocation:");
            if (at == std::string::npos) continue;
            at += 10;
            std::string loc = trim(reply.substr(at, reply.find_first_of("\r\n", at) - at));
            if (!loc.empty() && std::find(locations.begin(), locations.end(), loc) == locations.end()) locations.push_back(loc);
            if (firstReply < 0) firstReply = t;
        }
        sf::sleep(sf::milliseconds(15));
    }
    return locations;
}

std::optional<std::string> soap(const Net::UpnpMapping& m, const std::string& action, const std::string& args, int* errorCode = nullptr) {
    sf::Http::Request r = httpRequest(m.host, m.port, m.path);
    r.setMethod(sf::Http::Request::Method::Post);
    r.setField("Content-Type", "text/xml; charset=\"utf-8\"");
    r.setField("SOAPAction", "\"" + m.service + "#" + action + "\"");
    r.setBody("<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
              "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" +
              action + " xmlns:u=\"" + m.service + "\">" + args + "</u:" + action + "></s:Body></s:Envelope>\r\n");
    sf::Http http(m.host, m.port);
    sf::Http::Response resp = http.sendRequest(r, sf::seconds(2));
    if (resp.getStatus() == sf::Http::Response::Status::Ok) return resp.getBody();
    if (errorCode) *errorCode = std::atoi(xmlValue(resp.getBody(), "errorCode").c_str());
    return std::nullopt;
}

std::string mappingArgs(unsigned short externalPort) {
    return "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(externalPort) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>";
}

/** AddPortMapping, preferring the same external port as ours. */
bool addMapping(Net::UpnpMapping& m, unsigned short internalPort, const std::string& client) {
    std::mt19937 rng{std::random_device{}()};
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto external = attempt == 0 ? internalPort : static_cast<unsigned short>(std::uniform_int_distribution<int>(30000, 60000)(rng));
        int error = 0;
        for (int lease : {3600, 0}) { // some routers only take permanent mappings (error 725)
            error = 0;
            std::string args = mappingArgs(external) + "<NewInternalPort>" + std::to_string(internalPort) + "</NewInternalPort><NewInternalClient>" +
                               client + "</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>Cube Slam</NewPortMappingDescription>"
                               "<NewLeaseDuration>" + std::to_string(lease) + "</NewLeaseDuration>";
            if (soap(m, "AddPortMapping", args, &error)) {
                m.externalPort = external;
                if (auto reply = soap(m, "GetExternalIPAddress", ""))
                    if (auto ip = sf::IpAddress::fromString(xmlValue(*reply, "NewExternalIPAddress")); ip && ip->isV4())
                        m.externalIp = ip->toInteger();
                return true;
            }
            if (error != 725) break;
        }
        if (error != 718) return false; // only "that port is taken" is worth another try
    }
    return false;
}

std::optional<Net::UpnpMapping> upnpMap(unsigned short internalPort, const std::atomic<bool>& cancelled) {
    auto local = sf::IpAddress::getLocalAddress();
    if (!local) return std::nullopt;
    const char* services[] = {"urn:schemas-upnp-org:service:WANIPConnection:2", "urn:schemas-upnp-org:service:WANIPConnection:1",
                              "urn:schemas-upnp-org:service:WANPPPConnection:1"};
    for (const std::string& location : ssdpSearch()) {
        if (cancelled) return std::nullopt;
        Net::UpnpMapping m;
        std::string path;
        if (!splitUrl(location, m.host, m.port, path)) continue;
        sf::Http http(m.host, m.port);
        sf::Http::Response resp = http.sendRequest(httpRequest(m.host, m.port, path), sf::seconds(2));
        if (resp.getStatus() != sf::Http::Response::Status::Ok) continue;
        const std::string& desc = resp.getBody();
        for (const char* service : services) {
            auto at = desc.find(service);
            if (at == std::string::npos) continue;
            auto c = desc.find("<controlURL>", at);
            if (c == std::string::npos || c > desc.find("</service>", at)) continue;
            std::string control = xmlValue(desc, "controlURL", at);
            if (control.empty()) continue;
            m.service = service;
            if (lower(control.substr(0, 7)) == "http://") {
                if (!splitUrl(control, m.host, m.port, m.path)) continue;
            } else {
                m.path = control[0] == '/' ? control : "/" + control;
            }
            if (addMapping(m, internalPort, local->toString())) return m;
            break;
        }
    }
    return std::nullopt;
}

void upnpUnmap(const Net::UpnpMapping& m) { (void)soap(m, "DeletePortMapping", mappingArgs(m.externalPort)); }

} // namespace

// ---------------------------------------------------------------- join codes

std::string Endpoint::toString() const { return address().toString() + ":" + std::to_string(port); }

bool isPrivateIp(std::uint32_t ip) {
    auto in = [ip](std::uint32_t net, int prefix) { return (ip >> (32 - prefix)) == (net >> (32 - prefix)); };
    return ip == 0 || in(0x0A000000, 8) || in(0xAC100000, 12) || in(0xC0A80000, 16) || in(0x64400000, 10) || in(0x7F000000, 8) ||
           in(0xA9FE0000, 16);
}

std::string encodeJoinCode(Endpoint e) {
    const CodeLayout& l = e.port == DefaultPort ? kShortCode : kLongCode;
    const std::uint64_t data = l.chars == 8 ? e.ip : std::uint64_t(e.ip) << 16 | e.port;
    const std::uint64_t check = codeCheck(data, l);
    const std::uint64_t value = (data ^ codeMask(check, l)) << l.checkBits | check;
    std::string s;
    for (int i = l.chars - 1; i >= 0; --i) {
        s += kAlphabet[(value >> (i * 5)) & 31];
        if (i % 4 == 0 && i) s += '-';
    }
    return s;
}

std::optional<Endpoint> decodeJoinCode(const std::string& text) {
    std::uint64_t value = 0;
    int chars = 0;
    for (char c : text) {
        if (c == '-' || c == ' ' || c == '\t') continue;
        int d = codeDigit(c);
        if (d < 0 || ++chars > kLongCode.chars) return std::nullopt;
        value = value << 5 | std::uint64_t(d);
    }
    const CodeLayout* l = chars == kShortCode.chars ? &kShortCode : chars == kLongCode.chars ? &kLongCode : nullptr;
    if (!l) return std::nullopt;
    const std::uint64_t check = value & bits(l->checkBits);
    const std::uint64_t data = (value >> l->checkBits) ^ codeMask(check, *l);
    if (codeCheck(data, *l) != check) return std::nullopt;
    Endpoint e = l == &kShortCode ? Endpoint{std::uint32_t(data), DefaultPort} : Endpoint{std::uint32_t(data >> 16), std::uint16_t(data)};
    if (!e.valid()) return std::nullopt;
    return e;
}

// ---------------------------------------------------------------- connection

struct Net::Discovery {
    std::mutex mutex;
    bool done = false;
    std::atomic<bool> cancelled{false};
    std::vector<Endpoint> stunServers;
    std::optional<UpnpMapping> mapping;
};

bool Net::bindSocket(unsigned short port) {
    close();
    error_.clear();
    notice_.clear();
    socket_.setBlocking(false);
    if (port == 0 || socket_.bind(port) != sf::Socket::Status::Done) {
        if (socket_.bind(sf::Socket::AnyPort) != sf::Socket::Status::Done) {
            fail("Could not open a network port");
            return false;
        }
    }
    localPort_ = socket_.getLocalPort();
    nonce_ = std::uint32_t(rng_()) | 1;
    buffer_.resize(sf::UdpSocket::MaxDatagramSize);
    clock_.restart();
    return true;
}

bool Net::host(unsigned short port, bool discover) {
    if (!bindSocket(port)) return false;
    isHost_ = true;
    status_ = Status::Listening;
    if (discover) startDiscovery();
    return true;
}

bool Net::open(unsigned short port, bool discover) {
    if (!bindSocket(port)) return false;
    isHost_ = false;
    status_ = Status::Ready;
    if (discover) startDiscovery();
    return true;
}

void Net::startDiscovery() {
    discovering_ = true;
    auto job = std::make_shared<Discovery>();
    discovery_ = job;
    const unsigned short port = localPort_;
    // DNS and UPnP block for up to a few seconds, so they run on their own thread with their own sockets.
    std::thread([job, port] {
        std::vector<Endpoint> servers;
        for (const StunServer& s : kStunServers) {
            if (job->cancelled) break;
            if (auto ips = sf::Dns::resolve(s.host))
                for (const sf::IpAddress& ip : *ips)
                    if (ip.isV4()) {
                        servers.push_back({ip.toInteger(), s.port});
                        break;
                    }
        }
        std::optional<UpnpMapping> mapping;
        if (!job->cancelled) mapping = upnpMap(port, job->cancelled);
        bool orphaned = false;
        {
            std::lock_guard<std::mutex> lock(job->mutex);
            orphaned = job->cancelled;
            if (!orphaned) {
                job->stunServers = std::move(servers);
                job->mapping = mapping;
            }
            job->done = true;
        }
        if (orphaned && mapping) upnpUnmap(*mapping); // the Net closed meanwhile; don't leave the port open
    }).detach();
}

void Net::pollDiscovery() {
    const float t = now();
    if (discovery_) {
        std::vector<Endpoint> servers;
        {
            std::lock_guard<std::mutex> lock(discovery_->mutex);
            if (!discovery_->done) return;
            servers = discovery_->stunServers;
            mapping_ = discovery_->mapping;
        }
        discovery_.reset();
        // Asked only now, after UPnP: the router may pick our outside port differently once a mapping exists.
        for (Endpoint s : servers) {
            StunQuery q{s, {}, std::nullopt};
            for (auto& b : q.txid) b = std::uint8_t(rng_());
            stun_.push_back(q);
        }
        stunRounds_ = 0;
        nextStun_ = t;
    }
    const int answered = int(std::count_if(stun_.begin(), stun_.end(), [](const StunQuery& q) { return q.mapped.has_value(); }));
    const bool enough = answered == int(stun_.size()) || (answered >= 2 && stunRounds_ >= 3);
    if (enough || (stunRounds_ >= kStunRounds && t >= nextStun_)) {
        finishDiscovery();
        return;
    }
    if (t < nextStun_) return;
    for (const StunQuery& q : stun_) {
        if (q.mapped) continue;
        std::uint8_t req[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42}; // binding request, no attributes
        std::memcpy(req + 8, q.txid, 12);
        (void)socket_.send(req, sizeof req, q.server.address(), q.server.port);
    }
    ++stunRounds_;
    nextStun_ = t + kStunInterval;
}

bool Net::handleStun(const std::uint8_t* d, std::size_t size) {
    if (size < 20 || d[0] != 0x01 || d[1] != 0x01 || be32(d + 4) != kStunCookie) return false; // not a binding success response
    auto q = std::find_if(stun_.begin(), stun_.end(), [&](const StunQuery& s) { return std::memcmp(s.txid, d + 8, 12) == 0; });
    if (q == stun_.end()) return true;
    const std::size_t end = std::min(size, std::size_t(20) + be16(d + 2));
    for (std::size_t at = 20; at + 4 <= end;) {
        const std::uint16_t type = be16(d + at), len = be16(d + at + 2);
        const std::uint8_t* v = d + at + 4;
        if (at + 4 + len > end) break;
        if ((type == 0x0020 || type == 0x0001) && len >= 8 && v[1] == 0x01) { // (XOR-)MAPPED-ADDRESS, IPv4
            std::uint16_t port = be16(v + 2);
            std::uint32_t ip = be32(v + 4);
            if (type == 0x0020) port ^= 0x2112, ip ^= kStunCookie;
            q->mapped = Endpoint{ip, port};
            if (type == 0x0020) break;
        }
        at += 4 + ((len + 3u) & ~3u);
    }
    return true;
}

void Net::finishDiscovery() {
    discovering_ = false;
    std::optional<Endpoint> seen;
    for (const StunQuery& q : stun_) {
        if (!q.mapped) continue;
        if (!seen) seen = q.mapped;
        else if (*q.mapped != *seen) strictNat_ = true; // a different outside port per destination: punching is unlikely to work
    }
    stun_.clear();
    if (mapping_) {
        if (seen && (mapping_->externalIp == 0 || mapping_->externalIp == seen->ip)) {
            public_ = {seen->ip, mapping_->externalPort};
            portMapped_ = true;
        } else if (!seen && !isPrivateIp(mapping_->externalIp)) {
            public_ = {mapping_->externalIp, mapping_->externalPort};
            portMapped_ = true;
        }
        // Otherwise that router sits behind another NAT (e.g. the ISP's), so its mapping doesn't help.
    }
    if (portMapped_) strictNat_ = false; // incoming packets get through either way
    else if (seen) public_ = *seen;
    code_ = public_.valid() ? encodeJoinCode(public_) : "";
}

void Net::connect(Endpoint code) {
    if (isHost_ || (status_ != Status::Ready && status_ != Status::Connecting)) return;
    targets_.push_back({code, code});
    if (status_ == Status::Ready) nextKnock_ = now();
    status_ = Status::Connecting;
    connectStart_ = now();
}

void Net::connect(const std::string& address, unsigned short port) {
    if (isHost_ || (status_ != Status::Ready && status_ != Status::Connecting)) return;
    if (resolving_.valid()) resolving_.wait();
    resolving_ = std::async(std::launch::async, [address, port]() -> std::optional<Endpoint> {
        if (auto ip = sf::IpAddress::fromString(address); ip && ip->isV4()) return Endpoint{ip->toInteger(), port};
        if (auto ips = sf::Dns::resolve(address))
            for (const sf::IpAddress& ip : *ips)
                if (ip.isV4()) return Endpoint{ip.toInteger(), port};
        return std::nullopt;
    });
    if (status_ == Status::Ready) nextKnock_ = now();
    status_ = Status::Connecting;
    connectStart_ = now();
}

void Net::punch(Endpoint code) {
    if (!isHost_ || status_ != Status::Listening || !code.valid()) return;
    if (std::find(punchTargets_.begin(), punchTargets_.end(), code) == punchTargets_.end()) punchTargets_.push_back(code);
    nextKnock_ = now();
}

void Net::close() {
    if (discovery_) {
        std::lock_guard<std::mutex> lock(discovery_->mutex);
        discovery_->cancelled = true;
    }
    discovery_.reset();
    if (status_ == Status::Connected)
        for (int i = 0; i < 3; ++i) sendTo(header(Bye), peer_); // UDP: say it a few times
    if (mapping_) upnpUnmap(*mapping_);
    mapping_.reset();
    if (resolving_.valid()) resolving_.wait();
    resolving_ = {};
    socket_.unbind();

    status_ = Status::Idle;
    isHost_ = false;
    localPort_ = 0;
    discovering_ = false;
    stun_.clear();
    public_ = {};
    portMapped_ = strictNat_ = false;
    code_.clear();
    targets_.clear();
    punchTargets_.clear();
    peer_ = {};
    peerNonce_ = 0;
    sendSeq_ = recvSeq_ = unrelSend_ = unrelRecv_ = 0;
    unacked_.clear();
    early_.clear();
    inbox_.clear();
}

void Net::fail(std::string why) {
    status_ = Status::Failed;
    error_ = std::move(why);
    unacked_.clear();
    early_.clear();
    inbox_.clear();
}

std::string Net::takeNotice() {
    std::string n = std::move(notice_);
    notice_.clear();
    return n;
}

sf::Packet Net::header(std::uint8_t kind) const {
    sf::Packet p;
    p << kWireMagic << kWireVersion << kind << nonce_;
    return p;
}

void Net::sendTo(const sf::Packet& p, Endpoint to) { (void)socket_.send(p.getData(), p.getDataSize(), to.address(), to.port); }

void Net::sendData(bool reliable, std::uint32_t seq, const std::vector<std::uint8_t>& payload) {
    sf::Packet p = header(Data);
    p << std::uint8_t(reliable) << seq << recvSeq_;
    p.append(payload.data(), payload.size());
    sendTo(p, peer_);
}

void Net::becomeConnected(Endpoint peer, std::uint32_t peerNonce) {
    status_ = Status::Connected;
    peer_ = peer;
    peerNonce_ = peerNonce;
    lastHeard_ = nextPing_ = now();
    targets_.clear();
    punchTargets_.clear();
}

void Net::poll() {
    if (status_ == Status::Idle || status_ == Status::Failed) return;

    for (int i = 0; i < 1024; ++i) {
        std::size_t n = 0;
        std::optional<sf::IpAddress> from;
        unsigned short fromPort = 0;
        const sf::Socket::Status st = socket_.receive(buffer_.data(), buffer_.size(), n, from, fromPort);
        if (st == sf::Socket::Status::NotReady) break;
        // Windows reports an ICMP "port unreachable" (a knock that hit a closed door) as a receive error: skip it.
        if (st != sf::Socket::Status::Done || !from || !from->isV4()) continue;
        if (testLoss_ > 0 && std::uniform_real_distribution<float>(0, 1)(rng_) < testLoss_) continue;
        handleDatagram(buffer_.data(), n, {from->toInteger(), fromPort});
        if (status_ == Status::Failed) return;
    }

    const float t = now();
    if (discovering_) pollDiscovery();

    if (status_ == Status::Listening && !punchTargets_.empty() && t >= nextKnock_) {
        sf::Packet p = header(Punch);
        p << std::uint8_t(1);
        for (Endpoint e : punchTargets_) sendTo(p, e);
        nextKnock_ = t + kKnockInterval;
    }

    if (status_ == Status::Connecting) {
        if (resolving_.valid() && resolving_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            if (std::optional<Endpoint> e = resolving_.get()) targets_.push_back({*e, {}});
            else if (targets_.empty()) {
                status_ = Status::Ready;
                notice_ = "Couldn't find that address";
            }
        }
        if (status_ == Status::Connecting && !resolving_.valid() && t - connectStart_ > kConnectTimeout) {
            status_ = Status::Ready;
            targets_.clear();
            notice_ = "No answer. Check the code, or ask the host to type in your code.";
        }
        if (status_ == Status::Connecting && t >= nextKnock_) {
            for (const Target& tg : targets_) {
                sf::Packet p = header(Connect);
                p << std::uint8_t(0) << tg.want.ip << tg.want.port;
                sendTo(p, tg.to);
                if (!tg.want.valid()) continue;
                // On the same network as the host? Many routers can't loop packets for their own public
                // address back inside, so also shout on the LAN (the host checks the code before answering).
                sf::Packet b = header(Connect);
                b << std::uint8_t(1) << tg.want.ip << tg.want.port;
                const std::uint32_t everyone = sf::IpAddress::Broadcast.toInteger();
                sendTo(b, {everyone, DefaultPort});
                if (tg.want.port != DefaultPort) sendTo(b, {everyone, tg.want.port});
            }
            nextKnock_ = t + kKnockInterval;
        }
    }

    if (status_ == Status::Connected) {
        if (t - lastHeard_ > kPeerTimeout) {
            fail("Connection lost");
            return;
        }
        for (Pending& pd : unacked_)
            if (t - pd.sentAt > kResendAfter) {
                sendData(true, pd.seq, pd.payload);
                pd.sentAt = t;
            }
        if (t >= nextPing_) {
            sf::Packet p = header(Ping);
            p << recvSeq_;
            sendTo(p, peer_);
            nextPing_ = t + kPingInterval;
        }
    }
}

void Net::handleDatagram(const std::uint8_t* data, std::size_t size, Endpoint from) {
    if (handleStun(data, size)) return;
    sf::Packet p;
    p.append(data, size);
    std::uint32_t magic = 0, nonce = 0;
    std::uint8_t version = 0, kind = 0;
    p >> magic >> version >> kind >> nonce;
    if (!p || magic != kWireMagic || version != kWireVersion || nonce == nonce_) return;
    auto fromPeer = [&] { return status_ == Status::Connected && from == peer_ && nonce == peerNonce_; };

    switch (kind) {
    case Connect: {
        std::uint8_t broadcast = 0;
        Endpoint want;
        p >> broadcast >> want.ip >> want.port;
        if (!p || !isHost_) return;
        if (status_ == Status::Listening) {
            if (broadcast && want != public_) return; // meant for another host on this network
            becomeConnected(from, nonce);
        }
        if (fromPeer()) { // (again, in case our first Accept got lost)
            sf::Packet a = header(Accept);
            a << nonce;
            sendTo(a, from);
        }
        return;
    }
    case Accept: {
        std::uint32_t forNonce = 0;
        p >> forNonce;
        if (p && !isHost_ && status_ == Status::Connecting && forNonce == nonce_) becomeConnected(from, nonce);
        return;
    }
    case Punch: {
        std::uint8_t fromHost = 0;
        p >> fromHost;
        if (!p || isHost_ || !fromHost || (status_ != Status::Ready && status_ != Status::Connecting)) return;
        // A host typed in our code: knock back at the address its packets come from.
        if (std::none_of(targets_.begin(), targets_.end(), [&](const Target& t) { return t.to == from; })) targets_.push_back({from, {}});
        if (status_ == Status::Ready) nextKnock_ = now();
        status_ = Status::Connecting;
        connectStart_ = now();
        return;
    }
    case Ping:
    case Data:
    case Bye: {
        if (!fromPeer()) return;
        lastHeard_ = now();
        if (kind == Bye) {
            fail("The other player left");
            return;
        }
        std::uint8_t reliable = 0;
        std::uint32_t seq = 0, ack = 0;
        if (kind == Ping) p >> ack;
        else p >> reliable >> seq >> ack;
        if (!p) return;
        while (!unacked_.empty() && unacked_.front().seq <= ack) unacked_.pop_front();
        if (kind == Ping) return;

        sf::Packet payload;
        payload.append(static_cast<const std::uint8_t*>(p.getData()) + p.getReadPosition(), p.getDataSize() - p.getReadPosition());
        if (!reliable) {
            if (seq > unrelRecv_) { // late ones are stale: a newer snapshot / paddle position already arrived
                unrelRecv_ = seq;
                inbox_.push_back(std::move(payload));
            }
        } else if (seq == recvSeq_ + 1) {
            inbox_.push_back(std::move(payload));
            ++recvSeq_;
            for (auto it = early_.find(recvSeq_ + 1); it != early_.end(); it = early_.find(recvSeq_ + 1)) {
                inbox_.push_back(std::move(it->second));
                early_.erase(it);
                ++recvSeq_;
            }
        } else if (seq > recvSeq_ + 1 && early_.size() < kMaxUnacked) {
            early_.emplace(seq, std::move(payload));
        }
        return;
    }
    default: return;
    }
}

void Net::send(sf::Packet packet, bool reliable) {
    if (status_ != Status::Connected) return;
    const auto* d = static_cast<const std::uint8_t*>(packet.getData());
    std::vector<std::uint8_t> payload(d, d + packet.getDataSize());
    if (!reliable) {
        sendData(false, ++unrelSend_, payload);
        return;
    }
    unacked_.push_back({++sendSeq_, std::move(payload), now()});
    sendData(true, unacked_.back().seq, unacked_.back().payload);
    if (unacked_.size() > kMaxUnacked) fail("Connection too slow");
}

std::optional<sf::Packet> Net::receive() {
    if (inbox_.empty()) return std::nullopt;
    sf::Packet p = std::move(inbox_.front());
    inbox_.pop_front();
    return p;
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
