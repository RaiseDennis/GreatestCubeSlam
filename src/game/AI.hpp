#pragma once
#include "game/World.hpp"

#include <random>

namespace game {

/** CPU opponent. Tracks the nearest incoming puck with per-level speed/reaction/view/confusion. */
class AI {
public:
    explicit AI(const AIParams& brain, std::uint32_t seed) : brain_(brain), rng_(seed) {}

    /** Decides where the CPU paddle goes this frame (call before World::step). */
    void update(World& world);

private:
    AIParams brain_;
    std::mt19937 rng_;
    float time_ = 0;
    float noise_ = 0;
    float confusionOffset_ = 0;
    float x_ = cfg::ArenaW / 2;
};

} // namespace game
