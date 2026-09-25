#pragma once
// Online play: one host, one client, a single TCP connection (SFML Network).
// The host runs the authoritative World and streams a snapshot after every fixed step;
// the client only sends where its own paddle is.
#include "game/World.hpp"

#include <SFML/Network.hpp>

#include <cstdint>
#include <deque>
#include <future>
#include <optional>
#include <string>

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

/** Non-blocking packet connection. Either listens for one peer (host) or connects to one (client). */
class Net {
public:
    enum class Status { Idle, Listening, Connecting, Connected, Failed };

    ~Net() { close(); }

    bool host(unsigned short port);
    void connect(const std::string& address, unsigned short port);
    void close();

    /** Accepts / finishes connecting, flushes queued output. Call once per frame. */
    void poll();
    void send(sf::Packet packet);
    std::optional<sf::Packet> receive();

    Status status() const { return status_; }
    bool connected() const { return status_ == Status::Connected; }
    const std::string& error() const { return error_; }
    unsigned short port() const { return port_; }

private:
    void fail(std::string why);

    Status status_ = Status::Idle;
    std::string error_;
    unsigned short port_ = 0;
    sf::TcpListener listener_;
    sf::TcpSocket socket_;
    std::future<sf::Socket::Status> connecting_;
    std::deque<sf::Packet> outbox_;
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
