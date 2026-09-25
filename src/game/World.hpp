#pragma once
#include "game/Config.hpp"
#include "game/Levels.hpp"

#include <random>
#include <vector>

namespace game {

using gfx::Vec2;

enum Side { Human = 0, Cpu = 1 }; // Human defends the bottom (large y), CPU the top.
inline int other(int side) { return 1 - side; }

struct Puck {
    Vec2 pos, vel;             // vel in arena units per frame
    bool alive = true;
    int bounces = 0;
    int lastHit = -1;          // side that touched it last
    float ghostTimer = 0;      // passes through paddles / back wall (after a shield hit)
    float ghostballTimer = 0;  // (nearly) invisible
    int fireball = 0;          // 1 = on fire
    float timebombTimer = 0;   // > 0: counting down to explosion
    float dampUntil = 0, damping = 1;
    bool hitShieldThisFrame = false;
    float sinceProgress = 0;   // seconds since a paddle/shield touch (stall detection)
};

struct Paddle {
    float x = cfg::ArenaW / 2, prevX = cfg::ArenaW / 2, y = 0;
    float resizeTimer = 0;     // big paddle
    float shrinkTimer = 0;     // hit by laser
    bool fireball = false;     // next puck hit becomes a fireball
    float dizzyTimer = 0;
    float laserTimer = 0, laserShotTimer = 0;
    float hitPulse = 0;        // visual

    float scale() const { return (resizeTimer > 0 ? 1.75f : 1.f) * (shrinkTimer > 0 ? 0.5f : 1.f); }
    float width() const { return cfg::Unit * cfg::PaddleWidthUnits * scale(); }
    float velocity() const { return x - prevX; }
};

struct Shield {
    float x, y, w, h;
    int side;
    int index;
    bool alive = true;
    float flash = 0;
};

struct Obstacle {
    ObstacleDef def;
    std::vector<Vec2> poly; // world space
    Vec2 center;
    bool alive = true;
    float flash = 0;
};

struct Force {
    ForceDef def;
    bool active = false;
    float toggleTimer = cfg::ForceInterval;
    float radius() const { return def.mass / 2; }
};

struct Extra {
    ExtraDef def;
    Vec2 pos;
    float age = 0;
    float lifetime = -1; // < 0 = until picked up / replaced
    bool alive = true;
    int id = 0;
};

struct Bullet {
    Vec2 pos, vel;
    int owner;
    bool alive = true;
};

struct PlayerState {
    std::vector<int> shields; // 1 = up, 0 = down
    float bulletproofTimer = 0;
    float mirroredTimer = 0;  // controls inverted
    float fogTimer = 0;       // vision obscured
};

enum class EventType {
    PaddleHit, WallHit, ShieldHit, ShieldBreak, ObstacleHit, ObstacleDestroyed,
    ExtraSpawn, ExtraActivate, ExtraExpire, LaserFire, PaddleShrink, BombExplode,
    FireballCharged, FireballLaunched, Dizzy, ForceOn, ForceOff, RoundOver,
};

struct Event {
    EventType type;
    Vec2 pos;
    int side = -1;
    ExtraType extra = ExtraType::Random;
};

struct HumanInput {
    float targetX = cfg::ArenaW / 2; // absolute arena x the player wants the paddle at (mouse)
    bool useAxis = false;            // keyboard: move relative instead
    float axis = 0;                  // -1..1
};

/** The authoritative game simulation for one round. Stepped at a fixed 60 Hz. */
class World {
public:
    World(const Level& level, const LevelSet& set, int round, int serveTowards, std::uint32_t seed);

    void step(const HumanInput& input);

    // --- state (read by renderer/AI) ---
    const Level& level;
    LevelSet set;
    int round; // 1-based
    std::vector<Puck> pucks;
    Paddle paddles[2];
    PlayerState players[2];
    std::vector<Shield> shields;
    std::vector<Obstacle> obstacles;
    std::vector<Force> forces;
    std::vector<Extra> extras;
    std::vector<Bullet> bullets;
    std::vector<Event> events; // cleared at the start of every step
    int frame = 0;

    bool roundOver = false;
    int roundLoser = -1;
    Vec2 roundOverPos;

    float deathballTimer = 0;

    /** Direct paddle control for the AI (absolute x, clamped). */
    void setCpuPaddleX(float x);

private:
    void createShields(int side);
    void stepPuck(Puck& p);
    void collidePuck(Puck& p);
    void bounceOffPaddle(Puck& p, int side, Vec2 normal);
    void hitShield(Puck& p, Shield& s);
    void hitExtra(Puck& p, Extra& e);
    void applyExtra(Puck& p, const ExtraDef& def, int side, Vec2 at);
    void checkMinSpeed(Puck& p);
    void checkMaxSpeed(Puck& p);
    void setSpeed(Puck& p, float speedUnits);
    float puckSpeed(const Puck& p) const;
    void endRound(int loser, Vec2 at);
    void spawnExtra();
    void stepBullets();
    void stepTimers();
    void clampPaddle(Paddle& pd);
    void regenerateShield(int side);
    bool overlapsSomething(Vec2 pos, float half) const;

    std::mt19937 rng_;
    std::vector<Puck> pendingPucks_; // spawned mid-step (multiball)
    float cpuTargetX_ = cfg::ArenaW / 2;
    float nextSpawn_ = -1;
    int nextExtraId_ = 1;
};

} // namespace game
