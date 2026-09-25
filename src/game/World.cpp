#include "game/World.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace game {

using namespace cfg;
using gfx::clamp;
using gfx::dot;
using gfx::length;
using gfx::normalize;

namespace {

constexpr float kHalf = Unit / 2; // puck half size

float sign(float v) { return v < 0 ? -1.f : 1.f; }

/** Separating-axis test for convex polygons. On overlap, `mtv` pushes `a` out of `b`. */
bool satCollide(const std::vector<Vec2>& a, const std::vector<Vec2>& b, Vec2& mtv) {
    float best = std::numeric_limits<float>::max();
    Vec2 bestAxis;
    auto testAxes = [&](const std::vector<Vec2>& poly) {
        for (size_t i = 0; i < poly.size(); ++i) {
            Vec2 e = poly[(i + 1) % poly.size()] - poly[i];
            Vec2 axis = normalize(Vec2{-e.y, e.x});
            float minA = 1e30f, maxA = -1e30f, minB = 1e30f, maxB = -1e30f;
            for (auto& v : a) { float d = dot(v, axis); minA = std::min(minA, d); maxA = std::max(maxA, d); }
            for (auto& v : b) { float d = dot(v, axis); minB = std::min(minB, d); maxB = std::max(maxB, d); }
            float overlap = std::min(maxA - minB, maxB - minA);
            if (overlap <= 0) return false;
            if (overlap < best) { best = overlap; bestAxis = axis; }
        }
        return true;
    };
    if (!testAxes(a) || !testAxes(b)) return false;
    Vec2 ca, cb;
    for (auto& v : a) ca += v;
    for (auto& v : b) cb += v;
    ca = ca / float(a.size());
    cb = cb / float(b.size());
    if (dot(ca - cb, bestAxis) < 0) bestAxis = -bestAxis;
    mtv = bestAxis * best;
    return true;
}

std::vector<Vec2> rectPoly(Vec2 c, float w, float h) {
    return {{c.x - w / 2, c.y - h / 2}, {c.x + w / 2, c.y - h / 2}, {c.x + w / 2, c.y + h / 2}, {c.x - w / 2, c.y + h / 2}};
}

bool aabbOverlap(Vec2 a, float aw, float ah, Vec2 b, float bw, float bh) {
    return std::abs(a.x - b.x) < (aw + bw) / 2 && std::abs(a.y - b.y) < (ah + bh) / 2;
}

float defaultDuration(ExtraType t) {
    switch (t) {
    case ExtraType::GhostBall: return 7;
    case ExtraType::MirroredControls: return 10;
    case ExtraType::BulletProof: return 7;
    case ExtraType::PaddleResize: return 10;
    case ExtraType::DeathBall: return 5;
    case ExtraType::Laser: return 5;
    case ExtraType::Fog: return 5;
    default: return 0;
    }
}

} // namespace

World::World(const Level& lvl, const LevelSet& s, int rnd, int serveTowards, std::uint32_t seed)
    : level(lvl), set(s), round(rnd), rng_(seed) {
    paddles[Human].y = ArenaH - Unit;
    paddles[Cpu].y = Unit;

    createShields(Human);
    createShields(Cpu);

    for (auto& def : set.obstacles) {
        Obstacle o;
        o.def = def;
        o.center = {def.x, def.y};
        for (auto& v : obstaclePolygon(def)) o.poly.push_back(v + o.center);
        obstacles.push_back(o);
    }
    for (auto& def : set.forces) forces.push_back(Force{def});

    Puck p;
    p.pos = {ArenaW / 2, ArenaH / 2};
    float dir = serveTowards == Human ? 1.f : -1.f;
    p.vel = {0, dir * puckSpeed(p) * UnitSpeed};
    pucks.push_back(p);
}

void World::createShields(int side) {
    int n = std::max(1, level.shields);
    players[side].shields.assign(n, 1);
    float w = float(Columns) / n * Unit - 5;
    float h = Unit / 8;
    for (int i = 0; i < n; ++i) {
        Shield s;
        s.w = w;
        s.h = h;
        s.x = w * i + w / 2 + 5 * i + (ArenaW - Columns * Unit) / 2;
        s.y = side == Cpu ? h : ArenaH - h;
        s.side = side;
        s.index = i;
        shields.push_back(s);
    }
}

void World::setCpuPaddleX(float x) { cpuTargetX_ = x; }

void World::clampPaddle(Paddle& pd) {
    float hw = pd.width() / 2;
    pd.x = clamp(pd.x, hw, ArenaW - hw);
}

float World::puckSpeed(const Puck& p) const {
    return std::min(level.puck.speed + level.puck.speedup * p.bounces, level.puck.maxSpeed);
}

void World::setSpeed(Puck& p, float speedUnits) {
    Vec2 dir = normalize(p.vel);
    if (length(dir) < 0.5f) dir = {0, 1};
    p.vel = dir * (speedUnits * UnitSpeed);
}

void World::checkMinSpeed(Puck& p) {
    if (!p.alive) return;
    if (std::abs(p.vel.y) < MinYSpeed) p.vel.y = sign(p.vel.y) * MinYSpeed;
    if (length(p.vel) / UnitSpeed < 1) setSpeed(p, 1);
}

void World::checkMaxSpeed(Puck& p) {
    if (!p.alive || p.dampUntil > 0) return;
    float maxSpeed = level.puck.maxSpeed * (p.fireball == 1 ? 1.5f : 1.f);
    if (length(p.vel) / UnitSpeed > maxSpeed) setSpeed(p, maxSpeed);
}

void World::step(const HumanInput& input) {
    events.clear();
    ++frame;
    if (roundOver) return;

    stepTimers();

    // --- paddles ---
    {
        Paddle& h = paddles[Human];
        const bool mirrored = players[Human].mirroredTimer > 0;
        float target;
        if (input.useAxis) target = h.x + input.axis * (mirrored ? -1.f : 1.f) * PaddleMaxStep * 2;
        else target = mirrored ? ArenaW - input.targetX : input.targetX;
        if (h.dizzyTimer > 0) target += std::sin(frame * 0.9f) * 160.f * std::min(1.f, h.dizzyTimer);
        h.prevX = h.x;
        h.x += clamp((target - h.x) * 0.5f, -PaddleMaxStep, PaddleMaxStep);
        clampPaddle(h);

        Paddle& c = paddles[Cpu];
        float ct = cpuTargetX_;
        if (c.dizzyTimer > 0) ct += std::sin(frame * 0.9f) * 160.f * std::min(1.f, c.dizzyTimer);
        c.prevX = c.x;
        c.x = ct;
        clampPaddle(c);
    }

    // --- pucks ---
    for (auto& p : pucks) {
        stepPuck(p);
        if (roundOver) return;
    }
    pucks.insert(pucks.end(), pendingPucks_.begin(), pendingPucks_.end());
    pendingPucks_.clear();
    pucks.erase(std::remove_if(pucks.begin(), pucks.end(), [](const Puck& p) { return !p.alive; }), pucks.end());

    stepBullets();

    shields.erase(std::remove_if(shields.begin(), shields.end(), [](const Shield& s) { return !s.alive; }), shields.end());
    extras.erase(std::remove_if(extras.begin(), extras.end(), [](const Extra& e) { return !e.alive; }), extras.end());
}

void World::stepTimers() {
    const float dt = Timestep;

    for (auto& pl : players) {
        pl.bulletproofTimer = std::max(0.f, pl.bulletproofTimer - dt);
        pl.mirroredTimer = std::max(0.f, pl.mirroredTimer - dt);
        pl.fogTimer = std::max(0.f, pl.fogTimer - dt);
    }

    for (int side = 0; side < 2; ++side) {
        Paddle& pd = paddles[side];
        pd.resizeTimer = std::max(0.f, pd.resizeTimer - dt);
        pd.shrinkTimer = std::max(0.f, pd.shrinkTimer - dt);
        pd.dizzyTimer = std::max(0.f, pd.dizzyTimer - dt);
        pd.hitPulse = std::max(0.f, pd.hitPulse - dt * 3);
        if (pd.laserTimer > 0) {
            pd.laserTimer -= dt;
            pd.laserShotTimer -= dt;
            if (pd.laserShotTimer <= 0) {
                pd.laserShotTimer = 1.f;
                float dir = side == Human ? -1.f : 1.f;
                Bullet b;
                b.owner = side;
                b.pos = {pd.x, pd.y + dir * (Unit / 2 + 80)};
                b.vel = {0, dir * UnitSpeed * BulletSpeed};
                bullets.push_back(b);
                events.push_back({EventType::LaserFire, b.pos, side});
            }
        }
    }

    for (auto& f : forces) {
        f.toggleTimer -= dt;
        if (f.toggleTimer <= 0) {
            f.toggleTimer = ForceInterval;
            f.active = !f.active;
            events.push_back({f.active ? EventType::ForceOn : EventType::ForceOff, {f.def.x, f.def.y}});
            if (!f.active)
                for (auto& p : pucks) { checkMinSpeed(p); checkMaxSpeed(p); }
        }
    }

    for (auto& s : shields) s.flash = std::max(0.f, s.flash - dt * 3);
    for (auto& o : obstacles) o.flash = std::max(0.f, o.flash - dt * 3);

    for (auto& e : extras) {
        e.age += dt;
        if (e.lifetime > 0 && e.age > e.lifetime && e.alive) {
            e.alive = false;
            events.push_back({EventType::ExtraExpire, e.pos, -1, e.def.type});
        }
    }
    deathballTimer = std::max(0.f, deathballTimer - dt);

    if (nextSpawn_ < 0) {
        std::uniform_real_distribution<float> d(level.minSpawnTime, level.maxSpawnTime);
        nextSpawn_ = d(rng_);
    }
    nextSpawn_ -= dt;
    if (nextSpawn_ <= 0) {
        spawnExtra();
        nextSpawn_ = -1;
    }
}

void World::stepPuck(Puck& p) {
    const float dt = Timestep;
    p.hitShieldThisFrame = false;

    // Attracting / repelling force fields.
    for (auto& f : forces) {
        if (!f.active) continue;
        Vec2 diff = Vec2{f.def.x, f.def.y} - p.pos;
        float distSq = gfx::lengthSq(diff);
        float r = f.radius();
        if (distSq >= r * r) continue;
        distSq = std::max(100.f, distSq);
        float mag = 5.f * f.def.mass / distSq * f.def.power;
        if (f.def.attract) mag = std::min(0.65f, mag);
        else mag = -mag;
        p.vel += diff / std::sqrt(distSq) * mag * 1.5f;
    }

    // Momentum speed-up decays back to the base speed.
    if (p.dampUntil > 0) {
        p.vel *= p.damping;
        if (length(p.vel) < p.dampUntil) {
            p.dampUntil = 0;
            p.damping = 1;
        }
    }

    for (int i = 0; i < Substeps; ++i) {
        p.pos += p.vel / float(Substeps);
        collidePuck(p);
        if (!p.alive || roundOver) return;
    }
    checkMaxSpeed(p);

    // Break perfectly vertical loops between obstacles (e.g. tip-to-tip bouncing).
    p.sinceProgress += dt;
    if (p.sinceProgress > 6) {
        std::uniform_real_distribution<float> side(-1, 1);
        float speed = length(p.vel);
        p.vel.x += (side(rng_) < 0 ? -1.f : 1.f) * speed * 0.35f;
        setSpeed(p, speed / UnitSpeed);
        p.sinceProgress = 3;
    }

    p.ghostTimer = std::max(0.f, p.ghostTimer - dt);
    p.ghostballTimer = std::max(0.f, p.ghostballTimer - dt);

    if (p.timebombTimer > 0) {
        p.timebombTimer -= dt;
        if (p.timebombTimer <= 0) {
            p.timebombTimer = 0;
            const float radius = ArenaH / 2;
            for (auto& s : shields) {
                if (!s.alive) continue;
                Vec2 d = Vec2{s.x, s.y} - p.pos;
                if (gfx::lengthSq(d) < radius * radius) {
                    s.alive = false;
                    players[s.side].shields[s.index] = 0;
                    events.push_back({EventType::ShieldBreak, {s.x, s.y}, s.side});
                }
            }
            events.push_back({EventType::BombExplode, p.pos});
        }
    }
}

void World::collidePuck(Puck& p) {
    // Side walls.
    if (p.pos.x < kHalf) {
        p.pos.x = kHalf;
        if (p.vel.x < 0) { p.vel.x = -p.vel.x; events.push_back({EventType::WallHit, p.pos}); checkMinSpeed(p); }
    } else if (p.pos.x > ArenaW - kHalf) {
        p.pos.x = ArenaW - kHalf;
        if (p.vel.x > 0) { p.vel.x = -p.vel.x; events.push_back({EventType::WallHit, p.pos}); checkMinSpeed(p); }
    }

    // Shields (static: they block even a ghosted puck).
    for (auto& s : shields) {
        if (!s.alive || !aabbOverlap(p.pos, Unit, Unit, {s.x, s.y}, s.w, s.h)) continue;
        if (s.side == Cpu) { p.pos.y = s.y + s.h / 2 + kHalf; p.vel.y = std::abs(p.vel.y); }
        else { p.pos.y = s.y - s.h / 2 - kHalf; p.vel.y = -std::abs(p.vel.y); }
        hitShield(p, s);
        break;
    }

    // Back walls: a clean hit loses the round.
    if (p.pos.y < kHalf || p.pos.y > ArenaH - kHalf) {
        int side = p.pos.y < kHalf ? Cpu : Human;
        if (p.ghostTimer <= 0) {
            endRound(side, p.pos);
            return;
        }
        p.pos.y = clamp(p.pos.y, kHalf, ArenaH - kHalf);
        p.vel.y = side == Cpu ? std::abs(p.vel.y) : -std::abs(p.vel.y);
        events.push_back({EventType::WallHit, p.pos});
    }

    // Paddles.
    if (p.ghostTimer <= 0) {
        for (int side = 0; side < 2; ++side) {
            Paddle& pd = paddles[side];
            float w = pd.width();
            float ox = (kHalf + w / 2) - std::abs(p.pos.x - pd.x);
            float oy = (kHalf + Unit / 2) - std::abs(p.pos.y - pd.y);
            if (ox <= 0 || oy <= 0) continue;
            if (oy <= ox) {
                Vec2 n{0, sign(p.pos.y - pd.y)};
                p.pos.y += n.y * oy;
                if (dot(p.vel, n) < 0) bounceOffPaddle(p, side, n);
            } else {
                Vec2 n{sign(p.pos.x - pd.x), 0};
                p.pos.x += n.x * ox;
                if (dot(p.vel, n) < 0 || std::abs(pd.velocity()) > 0) {
                    p.vel.x = n.x * std::max(std::abs(p.vel.x), std::abs(pd.velocity()) * 1.2f);
                    events.push_back({EventType::WallHit, p.pos});
                    checkMinSpeed(p);
                }
                // Squeezed between paddle and wall: pop it out in front of/behind the paddle.
                if (p.pos.x < kHalf || p.pos.x > ArenaW - kHalf) {
                    p.pos.x = clamp(p.pos.x, kHalf, ArenaW - kHalf);
                    float ny = side == Human ? -1.f : 1.f; // always towards the arena
                    p.pos.y = pd.y + ny * (Unit / 2 + kHalf + 1);
                    p.vel.y = ny * std::max(std::abs(p.vel.y), MinYSpeed);
                }
            }
        }
    }

    // Obstacles.
    auto puckPoly = rectPoly(p.pos, Unit, Unit);
    for (auto& o : obstacles) {
        if (!o.alive) continue;
        Vec2 mtv;
        if (!satCollide(puckPoly, o.poly, mtv)) continue;
        p.pos += mtv;
        Vec2 n = normalize(mtv);
        float vn = dot(p.vel, n);
        if (vn < 0) p.vel -= n * (2 * vn);
        // A dead-straight bounce off a tip tends to loop forever; add a little English.
        float speed = length(p.vel);
        if (std::abs(p.vel.x) < speed * 0.08f) {
            std::uniform_real_distribution<float> side(-1, 1);
            p.vel.x += (side(rng_) < 0 ? -1.f : 1.f) * speed * 0.15f;
            p.vel = normalize(p.vel) * speed;
        }
        o.flash = 1;
        events.push_back({EventType::ObstacleHit, p.pos});
        if (o.def.destroyable) {
            o.alive = false;
            events.push_back({EventType::ObstacleDestroyed, o.center});
        }
        checkMinSpeed(p);
        puckPoly = rectPoly(p.pos, Unit, Unit);
    }

    // Extras (pick-ups: no bounce).
    for (auto& e : extras) {
        if (!e.alive || e.age < ExtraGhostTime) continue;
        if (aabbOverlap(p.pos, Unit, Unit, e.pos, Unit, Unit)) hitExtra(p, e);
        if (roundOver) return;
    }
}

void World::bounceOffPaddle(Puck& p, int side, Vec2 normal) {
    Paddle& pd = paddles[side];
    float len = length(p.vel);

    if (p.lastHit != side) ++p.bounces;
    p.lastHit = side;

    // A fireball puck makes the receiving paddle dizzy.
    if (p.fireball == 1) {
        pd.dizzyTimer = 1.6f;
        p.fireball = 0;
        events.push_back({EventType::Dizzy, {pd.x, pd.y}, side});
    }

    float speed = puckSpeed(p);
    if (pd.fireball) {
        speed *= FireballSpeedup;
        p.fireball = 1;
        pd.fireball = false;
        events.push_back({EventType::FireballLaunched, p.pos, side});
    }

    // Steer by where the puck hit the paddle...
    float d = clamp((p.pos.x - pd.x) / (pd.width() / 2 + kHalf), -1.f, 1.f);
    Vec2 v = normalize(Vec2{d * SteerWidth, normal.y}) * len;

    // ...and by the paddle's momentum ("slam").
    float m = pd.velocity();
    v.x += m;
    if (std::abs(m) > 1) {
        // A moving paddle "slams" the puck: a temporary boost that damps back to normal.
        float ms = std::min(std::abs(m), PaddleMaxStep) / PaddleMaxStep;
        p.dampUntil = speed * UnitSpeed;
        p.damping = 0.985f;
        speed += ms * 0.6f;
    }
    v = normalize(v) * (speed * UnitSpeed);

    // Keep it heading away from the paddle at a playable angle.
    float total = length(v);
    if (std::abs(v.y) < total * 0.35f || sign(v.y) != normal.y) {
        v.y = normal.y * total * 0.35f;
        v.x = sign(v.x) * std::sqrt(std::max(0.f, total * total - v.y * v.y));
    }
    p.vel = v;
    p.sinceProgress = 0;
    pd.hitPulse = 1;
    events.push_back({EventType::PaddleHit, p.pos, side});
    checkMinSpeed(p);
}

void World::hitShield(Puck& p, Shield& s) {
    if (p.hitShieldThisFrame) return;
    p.hitShieldThisFrame = true;
    p.sinceProgress = 0;
    p.ghostTimer = 0.4f;
    p.fireball = 0;
    p.lastHit = s.side;
    s.flash = 1;
    events.push_back({EventType::ShieldHit, {s.x, s.y}, s.side});
    if (players[s.side].bulletproofTimer <= 0) {
        players[s.side].shields[s.index] = 0;
        s.alive = false;
        events.push_back({EventType::ShieldBreak, {s.x, s.y}, s.side});
    }
    checkMinSpeed(p);
}

void World::regenerateShield(int side) {
    auto& list = players[side].shields;
    for (int i = 0; i < int(list.size()); ++i) {
        if (list[i] != 0) continue;
        list[i] = 1;
        int n = int(list.size());
        Shield s;
        s.w = float(Columns) / n * Unit - 5;
        s.h = Unit / 8;
        s.x = s.w * i + s.w / 2 + 5 * i + (ArenaW - Columns * Unit) / 2;
        s.y = side == Cpu ? s.h : ArenaH - s.h;
        s.side = side;
        s.index = i;
        s.flash = 1;
        shields.push_back(s);
        return;
    }
}

void World::hitExtra(Puck& p, Extra& e) {
    if (p.lastHit < 0) return; // nobody to credit yet
    e.alive = false;

    ExtraDef def = e.def;
    if (def.type == ExtraType::Random) {
        std::vector<ExtraDef> pool;
        for (auto& x : level.extras)
            if (x.type != ExtraType::Random && x.type != ExtraType::DeathBall) pool.push_back(x);
        if (pool.empty()) return;
        std::uniform_int_distribution<size_t> pick(0, pool.size() - 1);
        def = pool[pick(rng_)];
    }
    events.push_back({EventType::ExtraActivate, e.pos, p.lastHit, def.type});
    applyExtra(p, def, p.lastHit, e.pos);
}

void World::applyExtra(Puck& p, const ExtraDef& def, int side, Vec2 at) {
    float dur = def.duration > 0 ? def.duration : defaultDuration(def.type);
    switch (def.type) {
    case ExtraType::ExtraLife: regenerateShield(side); break;
    case ExtraType::GhostBall: p.ghostballTimer = dur; break;
    case ExtraType::FireBall:
        paddles[side].fireball = true;
        events.push_back({EventType::FireballCharged, {paddles[side].x, paddles[side].y}, side});
        break;
    case ExtraType::MirroredControls: players[other(side)].mirroredTimer = dur; break;
    case ExtraType::BulletProof: players[side].bulletproofTimer = dur; break;
    case ExtraType::PaddleResize: paddles[side].resizeTimer = dur; break;
    case ExtraType::DeathBall:
        deathballTimer = 0;
        endRound(side, at);
        break;
    case ExtraType::TimeBomb: p.timebombTimer = 4; break;
    case ExtraType::Laser:
        paddles[other(side)].laserTimer = 0; // only one laser at a time
        paddles[side].laserTimer = dur;
        paddles[side].laserShotTimer = 0.5f;
        break;
    case ExtraType::Fog: players[other(side)].fogTimer = dur; break;
    case ExtraType::MultiBall: {
        Puck n;
        n.pos = {ArenaW / 2, ArenaH / 2};
        n.lastHit = p.lastHit;
        n.bounces = p.bounces;
        n.ghostTimer = 0.2f;
        n.vel = {0, p.vel.y < 0 ? UnitSpeed : -UnitSpeed};
        pendingPucks_.push_back(n); // appended after the puck loop (p is a reference into pucks)
        break;
    }
    case ExtraType::Random: break;
    }
}

bool World::overlapsSomething(Vec2 pos, float half) const {
    for (auto& p : pucks)
        if (aabbOverlap(pos, half * 2, half * 2, p.pos, Unit * 1.5f, Unit * 1.5f)) return true;
    for (auto& e : extras)
        if (e.alive && aabbOverlap(pos, half * 2, half * 2, e.pos, Unit, Unit)) return true;
    auto poly = rectPoly(pos, half * 2, half * 2);
    for (auto& o : obstacles) {
        Vec2 mtv;
        if (o.alive && satCollide(poly, o.poly, mtv)) return true;
    }
    return false;
}

void World::spawnExtra() {
    std::vector<const ExtraDef*> pool;
    for (auto& d : level.extras) {
        if (d.round && d.round > round) continue;
        if (d.simultaneous) {
            int count = 0;
            for (auto& e : extras) count += e.alive && e.def.type == d.type;
            if (count >= d.simultaneous) continue;
        }
        pool.push_back(&d);
    }
    if (pool.empty()) return;

    float total = 0;
    for (auto* d : pool) total += d->probability > 0 ? d->probability : DefaultProbability;
    std::uniform_real_distribution<float> roll(0, total);
    float r = roll(rng_);
    const ExtraDef* chosen = pool.back();
    for (auto* d : pool) {
        r -= d->probability > 0 ? d->probability : DefaultProbability;
        if (r <= 0) { chosen = d; break; }
    }

    int alive = 0;
    for (auto& e : extras) alive += e.alive;
    if (alive >= level.maxExtras) {
        for (auto& e : extras)
            if (e.alive) {
                e.alive = false;
                events.push_back({EventType::ExtraExpire, e.pos, -1, e.def.type});
                break;
            }
    }

    Vec2 pos;
    bool found = false;
    if (chosen->fixedPosition) {
        pos = chosen->position;
        found = !overlapsSomething(pos, Unit / 2);
    } else {
        std::vector<Vec2> positions = level.positions.empty() ? set.positions : level.positions;
        std::shuffle(positions.begin(), positions.end(), rng_);
        for (auto& c : positions)
            if (!overlapsSomething(c, Unit / 2)) { pos = c; found = true; break; }
    }
    if (!found) return;

    Extra e;
    e.def = *chosen;
    e.pos = pos;
    e.id = nextExtraId_++;
    if (chosen->type == ExtraType::DeathBall) {
        e.lifetime = chosen->duration > 0 ? chosen->duration : defaultDuration(ExtraType::DeathBall);
        deathballTimer = e.lifetime;
    }
    extras.push_back(e);
    events.push_back({EventType::ExtraSpawn, pos, -1, chosen->type});
}

void World::stepBullets() {
    for (auto& b : bullets) {
        if (!b.alive) continue;
        b.pos += b.vel;
        if (b.pos.y < -200 || b.pos.y > ArenaH + 200) { b.alive = false; continue; }
        Paddle& target = paddles[other(b.owner)];
        if (aabbOverlap(b.pos, Unit, 150, {target.x, target.y}, target.width(), Unit)) {
            b.alive = false;
            target.shrinkTimer = 5;
            events.push_back({EventType::PaddleShrink, b.pos, other(b.owner)});
            continue;
        }
        auto poly = rectPoly(b.pos, Unit * 0.5f, 150);
        for (auto& o : obstacles) {
            Vec2 mtv;
            if (o.alive && satCollide(poly, o.poly, mtv)) {
                b.alive = false;
                o.flash = 1;
                events.push_back({EventType::ObstacleHit, b.pos});
                break;
            }
        }
    }
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return !b.alive; }), bullets.end());
}

void World::endRound(int loser, Vec2 at) {
    if (roundOver) return;
    roundOver = true;
    roundLoser = loser;
    roundOverPos = at;
    events.push_back({EventType::RoundOver, at, loser});
}

} // namespace game
