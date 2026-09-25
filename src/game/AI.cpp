#include "game/AI.hpp"

#include <algorithm>
#include <cmath>

namespace game {

using namespace cfg;

void AI::update(World& world) {
    // React to our own paddle hits from the previous step: pick a new "mistake" offset.
    for (auto& e : world.events)
        if (e.type == EventType::PaddleHit && e.side == Cpu) {
            std::uniform_real_distribution<float> d(-250, 250);
            confusionOffset_ = d(rng_);
        }

    const PlayerState& me = world.players[Cpu];
    float viewRange = brain_.viewRange;
    float confusion = brain_.confusion;
    if (me.fogTimer > 0) viewRange *= 0.35f;                 // can't see far through the fog
    if (me.mirroredTimer > 0) confusion = std::min(1.f, confusion + 0.8f);

    const Paddle& pd = world.paddles[Cpu];
    x_ = pd.x;

    // Closest puck within view (distance along the arena length).
    const Puck* closest = nullptr;
    float minDist = ArenaH * viewRange;
    for (auto& p : world.pucks) {
        float ghostPenalty = p.ghostballTimer > 0 && p.lastHit == Human ? 0.5f : 1.f; // ghost balls are hard to spot
        float dist = std::abs(p.pos.y - pd.y) / ghostPenalty;
        if (dist < minDist) {
            minDist = dist;
            closest = &p;
        }
    }

    float targetX = ArenaW * 0.5f;
    if (closest) targetX = closest->pos.x + confusionOffset_ * confusion * 0.5f;
    else targetX += confusionOffset_ * confusion;

    if (noise_ < 1) noise_ += 0.004f;
    targetX += confusionOffset_ * confusion * 0.3f * noise_;
    time_ += 0.1f;
    targetX += std::sin(time_ * 2.2f) * 40 * noise_ * confusion;

    if (me.mirroredTimer > 0) targetX = ArenaW - targetX * 0.6f - ArenaW * 0.2f; // flailing

    if (closest && closest->vel.y < 0) {
        // The original's per-frame step (maxSpeed * reaction) is far too slow for our paddle model,
        // so scale it while keeping the per-level ordering of difficulty.
        float step = brain_.maxSpeed * brain_.reaction * 3.5f;
        x_ += std::clamp((targetX - x_) * 0.5f, -step, step);
    } else {
        x_ += (targetX - x_) * 0.01f * noise_ * brain_.reaction * 3;
    }

    world.setCpuPaddleX(x_);
}

} // namespace game
