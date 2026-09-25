#pragma once
// Simulation constants. The simulation runs in "arena units" (the arena is 1700 wide,
// 18 columns x 26 rows of 94-unit cells) at a fixed 60 Hz, like the original game.
#include "gfx/Math.hpp"

namespace cfg {

constexpr float ArenaW = 1700.f;
constexpr int Columns = 18;
constexpr int Rows = 26;
constexpr float ArenaH = ArenaW / Columns * Rows; // ~2455.6
constexpr float Unit = 94.f;                      // round(1700/18)
constexpr float UnitSpeed = 18.f;                 // arena units per frame at speed 1.0
constexpr float Timestep = 1.f / 60.f;
constexpr int Substeps = 4;

constexpr float MinYSpeed = 10.f;
constexpr float BulletSpeed = 1.6f;
constexpr float ExtraGhostTime = 0.4f;
constexpr float DefaultProbability = 10.f;
constexpr float ForceInterval = 3.f;
constexpr float FireballSpeedup = 1.5f;
constexpr float SteerWidth = 0.6f;
constexpr float PaddleWidthUnits = 5.f;
constexpr float PaddleMaxStep = 60.f; // max human paddle movement per frame
constexpr int WinningScore = 3;

/** Simulation -> render space scale (1 unit cell ~ 0.94 world units). */
constexpr float S = 0.01f;

/** Map a simulation point to the 3D floor plane (x right, z towards the human player). */
inline gfx::Vec3 toWorld(gfx::Vec2 p, float y = 0) { return {(p.x - ArenaW / 2) * S, y, (p.y - ArenaH / 2) * S}; }

} // namespace cfg
