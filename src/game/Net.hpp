#pragma once
// Online play, peer to peer over UDP (SFML Network), with no server of our own:
//  - each player asks a public STUN server what its address looks like from the internet and asks
//    the router to forward its port (UPnP), then shows that address as a short join code;
//  - the joiner types the host's code (and/or the host types the joiner's code), and both send
//    packets at each other until the routers let them through ("UDP hole punching");
//  - on top of UDP there is a reliable, ordered channel (handshake, game flow, pause) and an
//    unreliable one where only the newest packet counts (snapshots, paddle positions).
// The host runs the authoritative World and streams a snapshot after every fixed step;
// the client only sends where its own paddle is.
#include "game/World.hpp"

#include <SFML/Network.hpp>

#include <cstdint>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace game {

constexpr unsigned short DefaultPort = 27015;

enum class Msg : std::uint8_t {
    Hello = 1,   // client -> host: magic + protocol version
    Welcome,     // host -> client: magic + protocol version
    Flow,        // host -> client: game flow (state, level, scores, ...)
    Snapshot,    // host -> client: world state + events of one fixed step
    PaddleX,     // client -> host: the client's paddle position (arena units)
    PauseToggle, // client -> host: pause / resume request
};

/** An IPv4 address + UDP port. */
struct Endpoint {
    std::uint32_t ip = 0; // host byte order
    unsigned short port = 0;

    bool valid() const { return ip != 0 && port != 0; }
    sf::IpAddress address() const { return sf::IpAddress(ip); }
    std::string toString() const;
    bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Endpoint& o) const { return !(*this == o); }
};

/**
 * Join codes: an endpoint written with an unambiguous alphabet (no I/L/O/U) plus a checksum that
 * catches typos. "ABCD-EFGH" when the port is DefaultPort, "ABCD-EFGH-JKMN" otherwise.
 */
std::string encodeJoinCode(Endpoint e);
std::optional<Endpoint> decodeJoinCode(const std::string& text);

/** LAN, loopback, link-local or carrier-grade NAT address (not reachable from the internet)? */
bool isPrivateIp(std::uint32_t ip);

/**
 * One UDP socket that works out its own join code, connects to one peer, and carries packets.
 * Host: host() -> Listening -> Connected.  Joiner: open() -> Ready -> connect() -> Connecting -> Connected.
 */
class Net {
public:
    enum class Status { Idle, Ready, Listening, Connecting, Connected, Failed };

    Net() = default;
    ~Net() { close(); }
    Net(const Net&) = delete;
    Net& operator=(const Net&) = delete;

    /** Binds (to `port` if it is free, any port otherwise) and starts working out the join code. */
    bool host(unsigned short port, bool discover = true);
    bool open(unsigned short port, bool discover = true);
    /** Joiner: start knocking on a host, by join code or by address / name. */
    void connect(Endpoint code);
    void connect(const std::string& address, unsigned short port);
    /** Host: knock on a joiner as well, so a strict router on this end lets their packets in. */
    void punch(Endpoint code);
    void close();

    /** Reads the socket, handles handshakes / resends / timeouts. Call once per frame. */
    void poll();
    void send(sf::Packet packet, bool reliable = true);
    std::optional<sf::Packet> receive();

    Status status() const { return status_; }
    bool connected() const { return status_ == Status::Connected; }
    const std::string& error() const { return error_; }
    /** A problem worth telling the player that did not end the session (a connect attempt timed out). */
    std::string takeNotice();
    unsigned short localPort() const { return localPort_; }

    bool discovering() const { return discovering_; }
    const std::string& code() const { return code_; } // "" while discovering or when offline
    Endpoint publicEndpoint() const { return public_; }
    bool portMapped() const { return portMapped_; } // the router forwards our port (UPnP)
    bool strictNat() const { return strictNat_; }   // the router uses a new port for every destination
    bool punching() const { return !punchTargets_.empty(); }

    /** Testing: drop this fraction of incoming datagrams. */
    void setTestLoss(float fraction) { testLoss_ = fraction; }

    struct UpnpMapping {
        std::string host, path, service;
        unsigned short port = 80, externalPort = 0;
        std::uint32_t externalIp = 0;
    };

private:
    struct Target {
        Endpoint to;
        Endpoint want; // the code the joiner typed (lets a host ignore LAN broadcasts meant for another host)
    };
    struct Pending {
        std::uint32_t seq;
        std::vector<std::uint8_t> payload;
        float sentAt;
    };
    struct StunQuery {
        Endpoint server;
        std::uint8_t txid[12];
        std::optional<Endpoint> mapped;
    };
    struct Discovery; // background thread: DNS for the STUN servers + UPnP

    bool bindSocket(unsigned short port);
    void startDiscovery();
    void pollDiscovery();
    void finishDiscovery();
    void becomeConnected(Endpoint peer, std::uint32_t peerNonce);
    void fail(std::string why);
    void handleDatagram(const std::uint8_t* data, std::size_t size, Endpoint from);
    bool handleStun(const std::uint8_t* data, std::size_t size);
    sf::Packet header(std::uint8_t kind) const;
    void sendTo(const sf::Packet& p, Endpoint to);
    void sendData(bool reliable, std::uint32_t seq, const std::vector<std::uint8_t>& payload);
    float now() const { return clock_.getElapsedTime().asSeconds(); }

    sf::UdpSocket socket_;
    sf::Clock clock_;
    std::mt19937 rng_{std::random_device{}()};
    std::uint32_t nonce_ = 0; // tells our own broadcasts and stale sessions apart
    bool isHost_ = false;
    Status status_ = Status::Idle;
    std::string error_, notice_;
    unsigned short localPort_ = 0;
    std::vector<std::uint8_t> buffer_;
    float testLoss_ = 0;

    // join code
    std::shared_ptr<Discovery> discovery_;
    bool discovering_ = false;
    std::vector<StunQuery> stun_;
    int stunRounds_ = 0;
    float nextStun_ = 0;
    std::optional<UpnpMapping> mapping_;
    Endpoint public_;
    bool portMapped_ = false, strictNat_ = false;
    std::string code_;

    // knocking
    std::vector<Target> targets_;
    std::future<std::optional<Endpoint>> resolving_;
    std::vector<Endpoint> punchTargets_;
    float connectStart_ = 0, nextKnock_ = 0;

    // connected
    Endpoint peer_;
    std::uint32_t peerNonce_ = 0;
    float lastHeard_ = 0, nextPing_ = 0;
    std::uint32_t sendSeq_ = 0, recvSeq_ = 0;     // reliable: last sent / last delivered in order
    std::uint32_t unrelSend_ = 0, unrelRecv_ = 0; // unreliable: newest sent / received
    std::deque<Pending> unacked_;
    std::map<std::uint32_t, sf::Packet> early_; // reliable packets that overtook a lost one
    std::deque<sf::Packet> inbox_;
};

/** Game flow the host shares with the client. */
struct Flow {
    std::uint8_t state = 0;
    bool paused = false;
    std::int32_t levelIndex = 0;
    std::uint32_t levelSerial = 0; // bumps on every level (re)start
    std::uint32_t worldId = 0;     // bumps on every new round
    std::string setName;
    std::int32_t scores[2] = {0, 0};
    std::int32_t round = 1;
    std::int32_t serve = Human;
    std::int32_t roundLoser = -1;

    bool operator==(const Flow& o) const {
        return state == o.state && paused == o.paused && levelIndex == o.levelIndex && levelSerial == o.levelSerial &&
               worldId == o.worldId && setName == o.setName && scores[0] == o.scores[0] && scores[1] == o.scores[1] &&
               round == o.round && serve == o.serve && roundLoser == o.roundLoser;
    }
    bool operator!=(const Flow& o) const { return !(*this == o); }
};

sf::Packet& operator<<(sf::Packet& p, const Flow& f);
sf::Packet& operator>>(sf::Packet& p, Flow& f);

/** Everything the client needs to draw the world and its HUD. */
void writeSnapshot(sf::Packet& p, const World& w);
bool readSnapshot(sf::Packet& p, World& w);

/** "host", "host:port", "[v6]:port" -> address + port. */
bool parseAddress(const std::string& text, std::string& host, unsigned short& port);

} // namespace game
