// Headless simulation of every level (both paddles automated) to catch physics/logic bugs,
// plus a two-player pass that checks the network snapshot format.
#include "game/AI.hpp"
#include "game/Net.hpp"
#include "game/World.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace game {

using namespace cfg;

/** Simple human stand-in: follows the puck most threatening to `side`, with a little error. */
HumanInput autoPilot(const World& w, float wobble, int side) {
    HumanInput in;
    const Puck* best = nullptr;
    for (auto& p : w.pucks)
        if (side == Human ? p.vel.y > 0 && (!best || p.pos.y > best->pos.y) : p.vel.y < 0 && (!best || p.pos.y < best->pos.y))
            best = &p;
    if (!best && !w.pucks.empty()) best = &w.pucks[0];
    in.targetX = best ? best->pos.x + wobble : ArenaW / 2;
    return in;
}

/**
 * Two-player worlds (both paddles steered like humans). Every step is also sent through the network
 * snapshot format into a client-side copy, which must re-serialize to the identical bytes.
 */
int versusSelfTest(std::mt19937& rng) {
    int problems = 0;
    const auto& levels = singlePlayerLevels();
    for (size_t li = 0; li < levels.size(); ++li) {
        const Level& level = levels[li];
        LevelSet set = makeLevelSet(level.set, rng);
        int scores[2] = {0, 0}, round = 1, serve = Human, frames = 0, events = 0, mismatches = 0, stalls = 0;
        std::size_t biggest = 0;
        while (scores[0] < WinningScore && scores[1] < WinningScore && round < 12) {
            World host(level, set, round, serve, rng());
            World client(level, set, round, serve, 0);
            std::uniform_real_distribution<float> wob(-330, 330); // sloppier than the solo pilot, or two of them rally forever
            float wobble[2] = {wob(rng), wob(rng)};
            HumanInput pilot[2];
            int f = 0;
            for (; f < 60 * 180 && !host.roundOver; ++f) {
                if (f % 90 == 0) wobble[0] = wob(rng), wobble[1] = wob(rng);
                if (f % 12 == 0)
                    for (int side = 0; side < 2; ++side) pilot[side] = autoPilot(host, wobble[side], side);
                host.step(pilot[Human], pilot[Cpu]);
                events += int(host.events.size());

                sf::Packet sent, echoed;
                writeSnapshot(sent, host);
                biggest = std::max(biggest, sent.getDataSize());
                if (!readSnapshot(sent, client)) {
                    ++mismatches;
                    continue;
                }
                writeSnapshot(echoed, client);
                if (sent.getDataSize() != echoed.getDataSize() ||
                    std::memcmp(sent.getData(), echoed.getData(), sent.getDataSize()) != 0)
                    ++mismatches;
            }
            frames += f;
            int loser = host.roundOver ? host.roundLoser : Human;
            if (!host.roundOver) ++stalls; // two bots can keep a rally going; not a physics problem
            ++scores[other(loser)];
            serve = loser;
            ++round;
        }
        std::printf("versus %2zu %-16s  p1 %d - %d p2 | %4.1fs/round | %5d events | snapshot mismatches %d, max %zu B%s\n", li + 1,
                    set.name.c_str(), scores[0], scores[1], frames / 60.f / std::max(1, round - 1), events, mismatches, biggest,
                    stalls ? "  (endless rally)" : "");
        problems += mismatches;
    }

    Flow a, b;
    a.state = 4, a.paused = true, a.levelIndex = 7, a.levelSerial = 3, a.worldId = 9, a.setName = "diamond";
    a.scores[0] = 2, a.scores[1] = 1, a.round = 4, a.serve = Cpu, a.roundLoser = Human;
    sf::Packet p;
    p << a;
    p >> b;
    if (a != b) std::printf("  !! flow message round trip failed\n"), ++problems;

    std::string host;
    unsigned short port = 0;
    bool ok = parseAddress("192.168.1.20", host, port) && host == "192.168.1.20" && port == DefaultPort &&
              parseAddress(" example.org:4000 ", host, port) && host == "example.org" && port == 4000 &&
              parseAddress("[::1]:5000", host, port) && host == "::1" && port == 5000 && !parseAddress("x:99999", host, port);
    if (!ok) std::printf("  !! address parsing failed\n"), ++problems;
    return problems;
}

/** Join codes round-trip and catch typos; two Nets on loopback connect and deliver over a lossy link. */
int netSelfTest(std::mt19937& rng) {
    int problems = 0;

    // --- join codes ---
    int bad = 0, typos = 0, caught = 0;
    for (int i = 0; i < 2000; ++i) {
        Endpoint e{std::uint32_t(rng()) | 1, i % 2 ? DefaultPort : static_cast<unsigned short>(1 + rng() % 65535)};
        std::string code = encodeJoinCode(e);
        std::optional<Endpoint> back = decodeJoinCode(code);
        std::string sloppy = code;
        for (char& c : sloppy) c = char(std::tolower(static_cast<unsigned char>(c)));
        sloppy.erase(std::remove(sloppy.begin(), sloppy.end(), '-'), sloppy.end());
        if (!back || *back != e || decodeJoinCode(sloppy) != back || code.size() != (e.port == DefaultPort ? 9u : 14u)) ++bad;
        // one wrong character
        std::size_t at = rng() % code.size();
        if (code[at] == '-') continue;
        std::string typo = code;
        typo[at] = typo[at] == 'X' ? 'Y' : 'X';
        ++typos;
        if (!decodeJoinCode(typo)) ++caught;
    }
    std::printf("join codes: e.g. %s / %s | %d bad round trips | %d/%d single typos caught\n",
                encodeJoinCode({0xC0A80114, DefaultPort}).c_str(), encodeJoinCode({0x5DB8D822, 40123}).c_str(), bad, caught, typos);
    if (bad || caught < typos * 9 / 10) std::printf("  !! join codes\n"), ++problems;

    // --- loopback session with 30% packet loss both ways ---
    Net host, joiner;
    if (!host.host(0, false) || !joiner.open(0, false)) {
        std::printf("  !! could not open UDP sockets\n");
        return problems + 1;
    }
    host.setTestLoss(0.3f);
    joiner.setTestLoss(0.3f);
    joiner.connect(Endpoint{0x7F000001, host.localPort()}); // by code...
    const int kCount = 300;
    int got[2] = {0, 0}, disorder = 0, unreliable = 0;
    sf::Clock clock;
    bool sent = false;
    while (clock.getElapsedTime() < sf::seconds(20)) {
        host.poll();
        joiner.poll();
        if (host.connected() && joiner.connected() && !sent) {
            for (int i = 0; i < kCount; ++i) {
                sf::Packet a, b, u;
                a << std::int32_t(i);
                b << std::int32_t(i);
                u << std::int32_t(-1);
                host.send(a);
                joiner.send(b);
                host.send(u, false);
            }
            sent = true;
        }
        Net* nets[2] = {&host, &joiner};
        for (int side = 0; side < 2; ++side)
            while (std::optional<sf::Packet> p = nets[side]->receive()) {
                std::int32_t v = 0;
                *p >> v;
                if (v < 0) ++unreliable;
                else if (v != got[side]++) ++disorder;
            }
        if (got[0] == kCount && got[1] == kCount) break;
        sf::sleep(sf::milliseconds(2));
    }
    std::printf("loopback: connected %s | reliable %d+%d/%d in order (%d out of order) | unreliable %d/%d | %.1fs at 30%% loss\n",
                host.connected() && joiner.connected() ? "yes" : "NO", got[0], got[1], kCount, disorder, unreliable, kCount,
                clock.getElapsedTime().asSeconds());
    if (got[0] != kCount || got[1] != kCount || disorder) std::printf("  !! loopback session\n"), ++problems;

    joiner.close(); // says Bye
    for (int i = 0; i < 50 && host.status() != Net::Status::Failed; ++i) {
        host.poll();
        sf::sleep(sf::milliseconds(5));
    }
    if (host.status() != Net::Status::Failed) std::printf("  !! host did not notice the joiner leaving\n"), ++problems;

    // --- the other way round: the host types the joiner's code, the joiner never typed anything ---
    Net host2, joiner2;
    if (!host2.host(0, false) || !joiner2.open(0, false)) return problems + 1;
    host2.punch(Endpoint{0x7F000001, joiner2.localPort()});
    for (int i = 0; i < 500 && !(host2.connected() && joiner2.connected()); ++i) {
        host2.poll();
        joiner2.poll();
        sf::sleep(sf::milliseconds(2));
    }
    std::printf("reverse knock: %s\n", host2.connected() && joiner2.connected() ? "connected" : "FAILED");
    if (!host2.connected() || !joiner2.connected()) ++problems;
    return problems;
}


int runSelfTest() {
    std::mt19937 rng(1234);
    int problems = 0;
    const auto& levels = singlePlayerLevels();
    for (size_t li = 0; li < levels.size(); ++li) {
        const Level& level = levels[li];
        LevelSet set = makeLevelSet(level.set, rng);
        int scores[2] = {0, 0};
        int round = 1, serve = Human, totalFrames = 0, timeouts = 0;
        float maxSpeed = 0;
        std::map<EventType, int> counts;
        while (scores[0] < WinningScore && scores[1] < WinningScore && round < 12) {
            World w(level, set, round, serve, rng());
            AI ai(level.ai, rng());
            std::uniform_real_distribution<float> wob(-200, 200);
            float wobble = wob(rng);
            int f = 0;
            HumanInput pilot;
            for (; f < 60 * 180 && !w.roundOver; ++f) {
                if (f % 90 == 0) wobble = wob(rng);
                if (f % 12 == 0) pilot = autoPilot(w, wobble, Human); // ~200 ms human reaction time
                ai.update(w);
                w.step(pilot);
                for (auto& e : w.events) counts[e.type]++;
                for (auto& p : w.pucks) {
                    if (!std::isfinite(p.pos.x) || !std::isfinite(p.pos.y) || !std::isfinite(p.vel.x)) {
                        std::printf("  !! level %zu: non-finite puck state at frame %d\n", li + 1, f);
                        ++problems;
                        w.roundOver = true;
                        break;
                    }
                    if (p.pos.x < -1 || p.pos.x > ArenaW + 1 || p.pos.y < -1 || p.pos.y > ArenaH + 1) {
                        std::printf("  !! level %zu: puck escaped arena (%.0f, %.0f)\n", li + 1, p.pos.x, p.pos.y);
                        ++problems;
                    }
                    maxSpeed = std::max(maxSpeed, gfx::length(p.vel) / UnitSpeed);
                }
            }
            totalFrames += f;
            if (!w.roundOver) {
                ++timeouts;
                w.roundLoser = Human;
                for (auto& p : w.pucks)
                    std::printf("  timeout puck pos (%.0f, %.0f) vel (%.1f, %.1f) ghost %.2f\n", p.pos.x, p.pos.y, p.vel.x, p.vel.y,
                                p.ghostTimer);
            }
            ++scores[other(w.roundLoser)];
            serve = w.roundLoser;
            ++round;
        }
        std::printf("level %2zu %-16s  you %d - %d cpu | %4.1fs/round | max speed %.2f | paddle hits %4d shield breaks %3d "
                    "extras %3d/%3d obstacles destroyed %3d%s\n",
                    li + 1, set.name.c_str(), scores[0], scores[1], totalFrames / 60.f / std::max(1, round - 1), maxSpeed,
                    counts[EventType::PaddleHit], counts[EventType::ShieldBreak], counts[EventType::ExtraActivate],
                    counts[EventType::ExtraSpawn], counts[EventType::ObstacleDestroyed], timeouts ? "  (round timeouts!)" : "");
        if (timeouts) ++problems;
    }
    problems += versusSelfTest(rng);
    problems += netSelfTest(rng);
    std::printf(problems ? "SELFTEST: %d problem(s)\n" : "SELFTEST: OK\n", problems);
    return problems ? 1 : 0;
}

} // namespace game
