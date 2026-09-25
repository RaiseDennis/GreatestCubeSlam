// Headless simulation of every level (both paddles automated) to catch physics/logic bugs.
#include "game/AI.hpp"
#include "game/World.hpp"

#include <cmath>
#include <cstdio>
#include <map>

namespace game {

using namespace cfg;

/** Simple human stand-in: follows the most threatening puck with a little error. */
HumanInput autoPilot(const World& w, float wobble) {
    HumanInput in;
    const Puck* best = nullptr;
    for (auto& p : w.pucks)
        if (p.vel.y > 0 && (!best || p.pos.y > best->pos.y)) best = &p;
    if (!best && !w.pucks.empty()) best = &w.pucks[0];
    in.targetX = best ? best->pos.x + wobble : ArenaW / 2;
    return in;
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
                if (f % 12 == 0) pilot = autoPilot(w, wobble); // ~200 ms human reaction time
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
    std::printf(problems ? "SELFTEST: %d problem(s)\n" : "SELFTEST: OK\n", problems);
    return problems ? 1 : 0;
}

} // namespace game
