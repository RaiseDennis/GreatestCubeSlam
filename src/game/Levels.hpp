#pragma once
#include "gfx/Math.hpp"

#include <random>
#include <string>
#include <vector>

namespace game {

using gfx::Vec2;

enum class ExtraType {
    ExtraLife,
    GhostBall,
    FireBall,
    MirroredControls,
    BulletProof,
    PaddleResize,
    DeathBall,
    TimeBomb,
    Laser,
    Fog,
    MultiBall,
    Random,
};

const char* extraName(ExtraType t);
gfx::Color extraColor(ExtraType t);

struct ExtraDef {
    ExtraType type = ExtraType::Random;
    int round = 0;             // only spawns from this round on (1-based), 0 = always
    float probability = 0;     // 0 = default probability
    float duration = 0;        // seconds, 0 = type default
    int simultaneous = 0;      // max on the field at once, 0 = unlimited
    bool fixedPosition = false;
    Vec2 position;
};

struct AIParams {
    float maxSpeed = 20;
    float reaction = 0.9f;
    float viewRange = 0.6f;
    float confusion = 0;
};

struct PuckParams {
    float speed = 1.3f;
    float speedup = 0.1f;
    float maxSpeed = 2.f;
};

enum class ObstacleShape { TriangleLeft, TriangleRight, Diamond, Hexagon, Octagon, Rect };

struct ObstacleDef {
    ObstacleShape shape;
    float x, y;
    float sizeW = 0, sizeH = 0; // in units; 0 = shape default
    bool destroyable = false;
};

struct ForceDef {
    bool attract = true;
    float x, y;
    float mass = 10;
    float power = 1;
};

struct LevelSet {
    std::string name;
    std::vector<ObstacleDef> obstacles;
    std::vector<ForceDef> forces;
    std::vector<Vec2> positions; // where extras may spawn
};

struct Level {
    AIParams ai;
    PuckParams puck;
    int shields = 3;
    std::string set;
    std::vector<ExtraDef> extras;
    int maxExtras = 3;
    float minSpawnTime = 5, maxSpawnTime = 10;
    std::vector<Vec2> positions; // overrides the set's positions if not empty
};

/** The 12 single-player levels. */
const std::vector<Level>& singlePlayerLevels();

/** Builds a named obstacle/force layout. "random" picks one of the others. */
LevelSet makeLevelSet(const std::string& name, std::mt19937& rng);

/** Convex polygon for an obstacle, centred on its centroid, in arena units. */
std::vector<Vec2> obstaclePolygon(const ObstacleDef& def);

} // namespace game
