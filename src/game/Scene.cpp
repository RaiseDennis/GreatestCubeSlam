#include "game/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace game {

using namespace cfg;
using gfx::Color;
using gfx::Mat4;
using gfx::MeshBuilder;
using gfx::Vec3;

namespace {

const float HW = ArenaW / 2 * S; // arena half width  (8.5)
const float HL = ArenaH / 2 * S; // arena half length (~12.28)
const float kRobotZ = -HL - 7.0f;

float hash2(int x, int y) {
    std::uint32_t h = std::uint32_t(x) * 374761393u + std::uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float(h ^ (h >> 16)) / 4294967295.f;
}

float valueNoise(float x, float y) {
    int xi = int(std::floor(x)), yi = int(std::floor(y));
    float fx = x - xi, fy = y - yi;
    float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    float a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
    return gfx::lerp(gfx::lerp(a, b, u), gfx::lerp(c, d, u), v);
}

float fbm(float x, float y) {
    float sum = 0, amp = 0.5f;
    for (int i = 0; i < 4; ++i) {
        sum += valueNoise(x, y) * amp;
        x *= 2.03f; y *= 2.03f; amp *= 0.5f;
    }
    return sum;
}

float terrainHeight(float x, float z) {
    float dx = std::max(0.f, std::abs(x) - (HW + 3)), dz = std::max(0.f, std::abs(z) - (HL + 3));
    float d = std::sqrt(dx * dx + dz * dz);
    float rise = gfx::smoothstep(d / 40.f) * (8 + 30 * fbm(x * 0.025f + 10, z * 0.025f + 3));
    float bumps = (fbm(x * 0.12f, z * 0.12f) - 0.5f) * 2.5f * std::min(1.f, d / 8.f);
    return -5 + rise + bumps;
}

Color brighten(Color c, float k) { return {std::min(1.f, c.r * k), std::min(1.f, c.g * k), std::min(1.f, c.b * k), c.a}; }

} // namespace

bool Scene::init() {
    MeshBuilder b;
    b.box({0, 0, 0}, {1, 1, 1});
    cube_.upload(b);
    MeshBuilder o;
    o.octahedron({0, 0, 0}, 1, {1, 1, 1});
    octa_.upload(o);
    buildArena();
    buildTerrain();
    return true;
}

void Scene::setupLevel(const World& world, const Theme& theme) {
    theme_ = &theme;
    buildArena();
    buildTerrain();
    rebuildObstacles(world);
    particles_.clear();
    floatTexts_.clear();
    fog_ = 0;
}

void Scene::buildArena() {
    const Theme& t = *theme_;
    MeshBuilder b;
    b.box({0, -2.62f, 0}, {2 * HW + 1.6f, 5.2f, 2 * HL + 1.6f}, t.pedestal.rgb());
    b.box({0, -0.1f, 0}, {2 * HW, 0.2f, 2 * HL}, t.arena.rgb());
    for (float s : {-1.f, 1.f}) {
        b.box({s * (HW + 0.35f), 0.3f, 0}, {0.7f, 0.8f, 2 * HL + 1.6f}, t.wall.rgb());
        b.box({s * (HW + 0.35f), 0.72f, 0}, {0.74f, 0.06f, 2 * HL + 1.64f}, brighten(t.wall, 1.12f).rgb());
        b.box({0, -0.05f, s * (HL + 0.45f)}, {2 * HW, 0.1f, 0.9f}, brighten(t.pedestal, 0.92f).rgb());
    }
    b.box({0, 0.003f, 0}, {2 * HW, 0.01f, 0.07f}, t.grid.rgb());
    arena_.upload(b);

    MeshBuilder g;
    const float y = 0.004f;
    for (int c = 0; c <= Columns; ++c) {
        float x = -HW + c * (2 * HW / Columns);
        g.line({x, y, -HL}, {x, y, HL}, t.grid.rgb());
    }
    for (int r = 0; r <= Rows; ++r) {
        float z = -HL + r * (2 * HL / Rows);
        g.line({-HW, y, z}, {HW, y, z}, t.grid.rgb());
    }
    grid_.upload(g, GL_LINES);
}

void Scene::buildTerrain() {
    const Theme& t = *theme_;
    MeshBuilder b;
    const int N = 110;
    const float size = 300, cx = 0, cz = -50;
    auto P = [&](int i, int j) {
        float x = cx - size / 2 + size * i / N, z = cz - size / 2 + size * j / N;
        return Vec3{x, terrainHeight(x, z), z};
    };
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> jitter(0.94f, 1.06f);
    auto shade = [&](Vec3 a, Vec3 b2, Vec3 c) {
        float h = (a.y + b2.y + c.y) / 3;
        float k = gfx::clamp((h + 5) / 26.f, 0, 1);
        Color col = gfx::mix(t.terrainLow, t.terrainHigh, k);
        return brighten(col, jitter(rng)).rgb();
    };
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            Vec3 p00 = P(i, j), p10 = P(i + 1, j), p01 = P(i, j + 1), p11 = P(i + 1, j + 1);
            Vec3 n1 = gfx::normalize(gfx::cross(p01 - p00, p10 - p00));
            if (n1.y < 0) n1 = -n1;
            b.triangle(p00, p01, p10, n1, shade(p00, p01, p10));
            Vec3 n2 = gfx::normalize(gfx::cross(p11 - p10, p01 - p10));
            if (n2.y < 0) n2 = -n2;
            b.triangle(p10, p01, p11, n2, shade(p10, p01, p11));
        }

    // Low-poly forest.
    std::uniform_real_distribution<float> ux(-140, 140), uz(-190, 70), us(0.7f, 1.8f);
    int placed = 0;
    for (int tries = 0; tries < 4000 && placed < 520; ++tries) {
        float x = ux(rng), z = uz(rng);
        float dx = std::max(0.f, std::abs(x) - (HW + 4)), dz = std::max(0.f, std::abs(z) - (HL + 4));
        float d = std::sqrt(dx * dx + dz * dz);
        if (d < 3 || (std::abs(x) < 9 && z < -HL && z > -HL - 14)) continue; // keep the robot's spot clear
        if (fbm(x * 0.05f, z * 0.05f) < 0.45f) continue;                     // clustered groves
        float y = terrainHeight(x, z);
        if (y > 18) continue;
        float s = us(rng);
        Color c = brighten(t.tree, jitter(rng));
        b.box({x, y + 0.5f * s, z}, {0.3f * s, 1.0f * s, 0.3f * s}, brighten(t.tree, 0.7f).rgb());
        b.cone({x, y + 0.8f * s, z}, 1.3f * s, 2.4f * s, 6, c.rgb());
        b.cone({x, y + 2.0f * s, z}, 0.95f * s, 2.0f * s, 6, brighten(c, 1.08f).rgb());
        ++placed;
    }
    terrain_.upload(b);
}

void Scene::rebuildObstacles(const World& world) {
    obstacleMeshes_.clear();
    for (auto& o : world.obstacles) {
        std::vector<gfx::Vec2> poly;
        for (auto& v : o.poly) poly.push_back({(v.x - ArenaW / 2) * S, (v.y - ArenaH / 2) * S});
        MeshBuilder b;
        Color c = o.def.destroyable ? theme_->block : theme_->obstacle;
        b.prism(poly, 0, o.def.destroyable ? 0.75f : 0.95f, c.rgb());
        obstacleMeshes_.emplace_back().upload(b);
    }
}

void Scene::burst(Vec3 at, Color color, int count, float speed, float size, bool glow) {
    std::uniform_real_distribution<float> u(-1, 1), l(0.5f, 1.2f);
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.pos = at;
        p.vel = Vec3{u(rng_), std::abs(u(rng_)) * 1.4f + 0.3f, u(rng_)} * speed;
        p.rot = {u(rng_) * 3, u(rng_) * 3, u(rng_) * 3};
        p.spin = {u(rng_) * 8, u(rng_) * 8, u(rng_) * 8};
        p.size = size * l(rng_);
        p.maxLife = p.life = l(rng_) * (glow ? 0.6f : 1.4f);
        p.color = color;
        p.glow = glow;
        p.gravity = !glow;
        particles_.push_back(p);
    }
}

void Scene::floatText(Vec3 pos, const std::string& text, Color color, float scale) {
    floatTexts_.push_back({pos, text, color, 1.6f, scale});
}

void Scene::robotExplode() {
    if (!robotAlive_) return;
    robotAlive_ = false;
    burst({0, 5, kRobotZ}, theme_->robot, 70, 9, 0.9f);
    burst({0, 1, kRobotZ}, theme_->accent, 40, 8, 0.6f);
    burst({0, 5, kRobotZ}, Color::hex(0xffb020), 40, 10, 0.5f, true);
    shake_ = 1.6f;
}

void Scene::handleEvents(const World& world) {
    for (auto& e : world.events) {
        Vec3 at = toWorld(e.pos, 0.4f);
        switch (e.type) {
        case EventType::PaddleHit:
            burst(at, e.side == Human ? theme_->human : theme_->cpu, 6, 3, 0.18f);
            shake(0.06f);
            break;
        case EventType::WallHit: burst(at, theme_->wall, 3, 2, 0.14f); break;
        case EventType::ShieldHit:
            burst(at, e.side == Human ? theme_->shieldHuman : theme_->shieldCpu, 10, 4, 0.2f, true);
            break;
        case EventType::ShieldBreak:
            burst(at, e.side == Human ? theme_->shieldHuman : theme_->shieldCpu, 22, 6, 0.3f);
            shake(0.3f);
            break;
        case EventType::ObstacleHit: burst(at, theme_->obstacle, 4, 2, 0.15f); break;
        case EventType::ObstacleDestroyed:
            burst(at, theme_->block, 20, 6, 0.32f);
            shake(0.22f);
            break;
        case EventType::ExtraSpawn:
            burst(toWorld(e.pos, 0.9f), extraColor(e.extra), 12, 3, 0.2f, true);
            break;
        case EventType::ExtraActivate:
            burst(toWorld(e.pos, 0.9f), extraColor(e.extra), 26, 7, 0.28f, true);
            floatText(toWorld(e.pos, 1.5f), std::string(extraName(e.extra)) + (e.side == Human ? "!" : " (CUBOT)"),
                      extraColor(e.extra), 1.2f);
            break;
        case EventType::ExtraExpire: burst(toWorld(e.pos, 0.9f), extraColor(e.extra), 8, 2, 0.15f, true); break;
        case EventType::LaserFire: burst(at, Color::hex(0xff3030), 6, 2, 0.15f, true); break;
        case EventType::PaddleShrink:
            burst(at, Color::hex(0xff3030), 16, 5, 0.25f, true);
            floatText(at + Vec3{0, 1, 0}, "SHRUNK!", Color::hex(0xff3030));
            shake(0.25f);
            break;
        case EventType::BombExplode:
            burst(at, Color::hex(0xff7a1a), 60, 12, 0.45f, true);
            burst(at, Color::hex(0x333333), 30, 8, 0.4f);
            floatText(at + Vec3{0, 1.5f, 0}, "BOOM!", Color::hex(0xff5a1a), 1.6f);
            shake(1.2f);
            break;
        case EventType::FireballCharged:
            burst(at, Color::hex(0xff6a1a), 14, 3, 0.25f, true);
            break;
        case EventType::FireballLaunched:
            burst(at, Color::hex(0xff6a1a), 24, 6, 0.3f, true);
            shake(0.3f);
            break;
        case EventType::Dizzy:
            floatText(at + Vec3{0, 1, 0}, "DIZZY!", Color::hex(0xffa020));
            break;
        case EventType::ForceOn: burst(toWorld(e.pos, 0.3f), theme_->accent, 10, 3, 0.18f, true); break;
        case EventType::ForceOff: break;
        case EventType::RoundOver: {
            Color c = e.side == Human ? theme_->shieldHuman : theme_->shieldCpu;
            burst(at, c, 50, 11, 0.42f);
            burst(at, Color::hex(0xffffff), 30, 9, 0.3f, true);
            shake(1.3f);
            break;
        }
        }
    }
}

gfx::Camera Scene::referenceCamera() const {
    gfx::Camera c;
    c.fovDegrees = 50;
    c.eye = {humanX_ * 0.25f, 10.5f, HL + 13.5f};
    c.target = {humanX_ * 0.08f, 0, -3.0f};
    return c;
}

void Scene::update(float dt, const World* world) {
    time_ += dt;
    introTime_ += dt;
    shake_ = std::max(0.f, shake_ - dt * 2.5f);
    robotHappy_ = std::max(0.f, robotHappy_ - dt);
    robotHurt_ = std::max(0.f, robotHurt_ - dt);

    if (world) {
        float fogTarget = world->players[Human].fogTimer > 0 ? 1.f : 0.f;
        fog_ = gfx::approach(fog_, fogTarget, 4, dt);
        humanX_ = gfx::approach(humanX_, (world->paddles[Human].x - ArenaW / 2) * S, 10, dt);
        cpuPaddleX_ = (world->paddles[Cpu].x - ArenaW / 2) * S;
        float look = 0;
        if (!world->pucks.empty()) look = gfx::clamp((world->pucks[0].pos.x - ArenaW / 2) / (ArenaW / 2), -1, 1);
        robotLook_ = gfx::approach(robotLook_, look, 8, dt);

        // Puck trails.
        std::uniform_real_distribution<float> u(-1, 1);
        for (auto& p : world->pucks) {
            if (p.ghostballTimer > 0) continue;
            Vec3 at = toWorld(p.pos, 0.3f);
            Particle t;
            t.pos = at + Vec3{u(rng_) * 0.2f, 0, u(rng_) * 0.2f};
            t.rot = {0, u(rng_), 0};
            t.gravity = false;
            if (p.fireball) {
                t.vel = {u(rng_) * 0.5f, 1.5f, u(rng_) * 0.5f};
                t.size = 0.45f;
                t.maxLife = t.life = 0.45f;
                t.color = u(rng_) > 0 ? Color::hex(0xff5a14) : Color::hex(0xffc414);
                t.glow = true;
            } else {
                t.size = 0.3f;
                t.maxLife = t.life = 0.22f;
                t.color = theme_->puck;
                t.glow = true;
            }
            particles_.push_back(t);
            if (p.timebombTimer > 0 && u(rng_) > 0.3f) {
                Particle s = t;
                s.pos = at + Vec3{0, 0.4f, 0};
                s.vel = {u(rng_) * 2, 2.5f, u(rng_) * 2};
                s.size = 0.12f;
                s.color = Color::hex(0xffe060);
                s.maxLife = s.life = 0.4f;
                particles_.push_back(s);
            }
        }
    }

    for (auto& p : particles_) {
        p.life -= dt;
        if (p.gravity) p.vel.y -= 14 * dt;
        p.pos += p.vel * dt;
        p.rot += p.spin * dt;
        if (p.gravity && p.pos.y < p.size / 2 && std::abs(p.pos.x) < HW && std::abs(p.pos.z) < HL + 1) {
            p.pos.y = p.size / 2;
            p.vel.y *= -0.35f;
            p.vel.x *= 0.7f;
            p.vel.z *= 0.7f;
            p.spin *= 0.6f;
        }
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.life <= 0 || p.pos.y < -30; }),
                     particles_.end());
    if (particles_.size() > 3000) particles_.erase(particles_.begin(), particles_.begin() + (particles_.size() - 3000));

    for (auto& f : floatTexts_) {
        f.life -= dt;
        f.pos.y += dt * 1.1f;
    }
    floatTexts_.erase(std::remove_if(floatTexts_.begin(), floatTexts_.end(), [](const FloatText& f) { return f.life <= 0; }),
                      floatTexts_.end());

    // Camera.
    gfx::Camera ref = referenceCamera();
    if (titleMode_) {
        float a = time_ * 0.1f;
        camera_.eye = {std::sin(a) * 32, 15 + std::sin(time_ * 0.3f) * 3, std::cos(a) * 32 - 4};
        camera_.target = {0, 1.5f, -4};
    } else {
        float t = gfx::smoothstep(introTime_ / 2.6f);
        Vec3 startEye{-16, 22, kRobotZ + 22}, startTarget{0, 5, kRobotZ};
        camera_.eye = gfx::lerp(startEye, ref.eye, t);
        camera_.target = gfx::lerp(startTarget, ref.target, t);
    }
    if (shake_ > 0) {
        std::uniform_real_distribution<float> u(-1, 1);
        Vec3 j{u(rng_), u(rng_), u(rng_)};
        camera_.eye += j * (shake_ * 0.3f);
        camera_.target += j * (shake_ * 0.15f);
    }
    camera_.fovDegrees = 50;
}

gfx::Mat4 Scene::beam(Vec3 a, Vec3 b, float t) const {
    Vec3 z = b - a;
    float len = gfx::length(z);
    z = z / std::max(len, 1e-4f);
    Vec3 x = gfx::normalize(gfx::cross({0, 1, 0}, z));
    if (gfx::length(x) < 0.5f) x = {1, 0, 0};
    Vec3 y = gfx::cross(z, x);
    Vec3 mid = (a + b) * 0.5f;
    Mat4 m;
    m.m[0] = x.x * t; m.m[1] = x.y * t; m.m[2] = x.z * t;
    m.m[4] = y.x * t; m.m[5] = y.y * t; m.m[6] = y.z * t;
    m.m[8] = z.x * len; m.m[9] = z.y * len; m.m[10] = z.z * len;
    m.m[12] = mid.x; m.m[13] = mid.y; m.m[14] = mid.z;
    return m;
}

void Scene::cube(gfx::Renderer& r, Vec3 center, Vec3 size, Color c, float emissive, Vec3 euler) {
    r.draw(cube_, Mat4::trs(center, euler, size), c, emissive);
}

void Scene::drawRobot(gfx::Renderer& r, const World* world) {
    const Theme& t = *theme_;
    const float z = kRobotZ;
    Color dark = Color::hex(0x1d2230);
    if (!robotAlive_) {
        // Smouldering remains.
        cube(r, {0, -3.5f, z}, {7, 3, 4}, brighten(t.robot, 0.5f));
        cube(r, {1.5f, -1.8f, z + 0.5f}, {2, 0.6f, 2}, brighten(t.robot, 0.4f), 0, {0.3f, 0.5f, 0.2f});
        return;
    }
    float bob = std::sin(time_ * 2) * 0.15f;
    float sx = 0;
    if (robotHappy_ > 0) bob = std::abs(std::sin(time_ * 10)) * 0.6f;
    if (robotHurt_ > 0) sx = std::sin(time_ * 55) * 0.25f * std::min(1.f, robotHurt_);
    Color body = t.robot;
    if (robotHurt_ > 0 && std::sin(time_ * 22) > 0) body = gfx::mix(body, Color::hex(0xff3030), 0.55f);

    // Body, chest light, neck.
    cube(r, {sx, -1.5f + bob * 0.3f, z}, {7, 7, 4}, body);
    float blink = 0.5f + 0.5f * std::sin(time_ * 4);
    cube(r, {sx, 0.2f + bob * 0.3f, z + 2.01f}, {3.2f, 1.1f, 0.06f}, t.accent, 0.5f + 0.5f * blink);
    for (int i = 0; i < 3; ++i)
        cube(r, {sx - 1 + i * 1.0f, -1.2f + bob * 0.3f, z + 2.01f}, {0.5f, 0.5f, 0.06f}, i == int(time_ * 3) % 3 ? t.accent : dark, 0.8f);
    cube(r, {sx, 2.4f + bob * 0.6f, z}, {2, 0.9f, 2}, brighten(body, 0.75f));

    // Head.
    float hy = 5.3f + bob;
    cube(r, {sx, hy, z}, {6.4f, 4.4f, 4.4f}, body);
    cube(r, {sx, hy - 0.1f, z + 2.21f}, {5.2f, 3.2f, 0.06f}, dark);
    for (float s : {-1.f, 1.f}) cube(r, {sx + s * 3.5f, hy, z}, {0.6f, 1.8f, 1.8f}, t.accent);
    cube(r, {sx, hy + 2.9f, z}, {0.15f, 1.4f, 0.15f}, dark);
    r.draw(octa_, Mat4::trs({sx, hy + 3.8f, z}, {0, time_ * 2, 0}, {0.45f, 0.45f, 0.45f}), t.accent, 0.4f + 0.6f * blink);

    // Eyes follow the puck.
    const float fz = z + 2.27f;
    for (float s : {-1.f, 1.f}) {
        float ex = sx + s * 1.3f, ey = hy + 0.45f;
        if (robotHurt_ > 0) {
            cube(r, {ex, ey, fz}, {1.3f, 0.22f, 0.06f}, Color::hex(0xff4040), 1, {0, 0, 0.78f});
            cube(r, {ex, ey, fz}, {1.3f, 0.22f, 0.06f}, Color::hex(0xff4040), 1, {0, 0, -0.78f});
        } else if (robotHappy_ > 0) {
            cube(r, {ex, ey + 0.2f, fz}, {1.2f, 0.3f, 0.06f}, t.accent, 1);
        } else {
            bool blinkNow = std::fmod(time_, 4.f) < 0.12f;
            cube(r, {ex, ey, fz}, {1.2f, blinkNow ? 0.12f : 1.2f, 0.06f}, Color::hex(0xf2f7ff), 1);
            if (!blinkNow) cube(r, {ex + robotLook_ * 0.3f, ey - 0.1f, fz + 0.03f}, {0.5f, 0.5f, 0.06f}, dark);
        }
    }

    // Mouth.
    for (int i = 0; i < 6; ++i) {
        float u = (i - 2.5f) / 2.5f;
        float my = hy - 1.05f;
        if (robotHappy_ > 0) my += 0.35f * u * u;
        else if (robotHurt_ > 0) my += 0.2f - 0.35f * u * u;
        else my += (world && !world->pucks.empty() ? 0.05f * std::sin(time_ * 12 + i) : 0);
        cube(r, {sx + u * 1.5f, my, fz}, {0.46f, 0.18f, 0.06f}, t.accent, 0.9f);
    }

    // Arms reaching for the CPU paddle, so CUBOT visibly "plays".
    float px = gfx::clamp(cpuPaddleX_, -HW + 1, HW - 1);
    for (float s : {-1.f, 1.f}) {
        Vec3 shoulder{sx + s * 3.9f, 0.8f + bob * 0.3f, z + 0.5f};
        Vec3 hand{px + s * 1.6f, 0.9f, -HL - 1.6f};
        Vec3 elbow = (shoulder + hand) * 0.5f + Vec3{s * 0.8f, 1.6f, 0};
        r.draw(cube_, beam(shoulder, elbow, 0.7f), brighten(body, 0.9f));
        r.draw(cube_, beam(elbow, hand, 0.6f), brighten(body, 0.9f));
        cube(r, hand, {0.95f, 0.7f, 0.95f}, t.accent);
    }
}

void Scene::render(gfx::Renderer& r, const World* world, int width, int height) {
    const Theme& t = *theme_;
    labels_.clear();
    r.setFog(t.sky, gfx::lerp(60, 3, fog_), gfx::lerp(190, 17, fog_));
    r.begin(camera_, width, height, t.sky);

    const Mat4 I;
    r.draw(terrain_, I);
    r.draw(arena_, I);
    r.draw(grid_, I, {1, 1, 1, 1});

    Color white{1, 1, 1, 1};
    if (world) {
        for (size_t i = 0; i < world->obstacles.size() && i < obstacleMeshes_.size(); ++i) {
            const Obstacle& o = world->obstacles[i];
            if (o.alive) r.draw(obstacleMeshes_[i], I, white, o.flash * 0.7f);
        }

        // Force fields: a ring of cubes spiralling inwards while active.
        for (auto& f : world->forces) {
            Vec3 c = toWorld({f.def.x, f.def.y}, 0.3f);
            float radius = f.radius() * S;
            const int n = 14;
            for (int ring = 0; ring < (f.active ? 3 : 1); ++ring) {
                float phase = std::fmod(time_ * 0.7f + ring / 3.f, 1.f);
                float rr = f.active ? radius * (1 - phase) : radius;
                for (int i = 0; i < n; ++i) {
                    float a = 2 * gfx::PI * i / n + time_ * (f.active ? 2.f : 0.3f);
                    Vec3 p = c + Vec3{std::cos(a) * rr, f.active ? 0.2f : 0.05f, std::sin(a) * rr};
                    if (f.active) cube(r, p, {0.2f, 0.2f, 0.2f}, t.accent, 0.8f * phase + 0.2f, {a, a, 0});
                    else cube(r, p, {0.12f, 0.06f, 0.12f}, t.grid);
                }
            }
            r.draw(octa_, Mat4::trs(c + Vec3{0, 0.5f, 0}, {0, time_, 0}, Vec3{0.35f, 0.5f, 0.35f} * (f.active ? 1.3f : 0.8f)),
                   f.active ? t.accent : t.obstacle, f.active ? 0.9f : 0);
        }

        // Paddles.
        for (int side = 0; side < 2; ++side) {
            const Paddle& pd = world->paddles[side];
            Vec3 c = toWorld({pd.x, pd.y}, 0.35f + pd.hitPulse * 0.08f);
            Color col = side == Human ? t.human : t.cpu;
            float em = pd.hitPulse * 0.4f;
            if (pd.fireball) {
                col = gfx::mix(col, Color::hex(0xff5a14), 0.5f + 0.5f * std::sin(time_ * 14));
                em = 0.7f;
            }
            if (pd.shrinkTimer > 0) col = gfx::mix(col, Color::hex(0x888888), 0.4f);
            Vec3 rot{0, pd.dizzyTimer > 0 ? std::sin(time_ * 30) * 0.2f : 0, 0};
            Vec3 size{pd.width() * S, 0.7f + pd.hitPulse * 0.16f, Unit * S * 0.92f};
            cube(r, c, size, col, em, rot);
            Color stripe = side == Human ? t.shieldHuman : t.shieldCpu;
            cube(r, c + Vec3{0, size.y / 2 + 0.02f, 0}, {size.x * 0.92f, 0.04f, 0.28f}, stripe, 0.6f, rot);
            if (pd.laserTimer > 0) {
                float dir = side == Human ? -1.f : 1.f;
                cube(r, c + Vec3{0, 0.45f, dir * 0.35f}, {0.25f, 0.25f, 0.7f}, Color::hex(0xff2a2a), 0.9f);
            }
        }

        // Pucks.
        for (auto& p : world->pucks) {
            if (p.ghostballTimer > 0) continue;
            Vec3 c = toWorld(p.pos, 0.3f);
            Color col = t.puck;
            float em = 0.15f;
            if (p.fireball) {
                col = gfx::mix(Color::hex(0xff3a0a), Color::hex(0xffc414), 0.5f + 0.5f * std::sin(time_ * 25));
                em = 0.9f;
            }
            if (p.timebombTimer > 0) {
                float rate = 3 + (4 - p.timebombTimer) * 4;
                if (std::sin(time_ * rate * 2 * gfx::PI) > 0) { col = Color::hex(0xff2020); em = 1; }
            }
            cube(r, c, {Unit * S, 0.6f, Unit * S}, col, em);
        }

        // Extras.
        for (auto& e : world->extras) {
            if (!e.alive) continue;
            float grow = std::min(1.f, e.age / ExtraGhostTime);
            Vec3 c = toWorld(e.pos, 0.9f + std::sin(time_ * 2 + e.id) * 0.15f);
            Color col = extraColor(e.def.type);
            if (e.def.type == ExtraType::DeathBall) {
                float pulse = 0.5f + 0.5f * std::sin(time_ * 8);
                cube(r, c, Vec3{1, 1, 1} * grow, col, 0, {time_, time_ * 1.3f, 0});
                for (int i = 0; i < 6; ++i) {
                    float a = i * gfx::PI / 3 + time_;
                    r.draw(octa_, Mat4::trs(c + Vec3{std::cos(a) * 0.8f, std::sin(a * 2) * 0.3f, std::sin(a) * 0.8f}, {}, Vec3{0.18f, 0.18f, 0.18f} * grow),
                           Color::hex(0xff2020), pulse);
                }
            } else {
                cube(r, c, Vec3{0.72f, 0.72f, 0.72f} * grow, col, 0.35f, {0.6f, time_ * 1.5f + e.id, 0.6f});
                r.draw(octa_, Mat4::trs(c + Vec3{0, 0.75f, 0}, {0, -time_ * 3, 0}, Vec3{0.14f, 0.2f, 0.14f} * grow), white, 1);
            }
            gfx::Vec2 px;
            if (r.project(c + Vec3{0, 1.25f, 0}, px)) labels_.push_back({px, extraName(e.def.type), col, 0.8f});
        }

        for (auto& b : world->bullets) cube(r, toWorld(b.pos, 0.4f), {0.25f, 0.25f, 1.5f}, Color::hex(0xff2a2a), 1);
    }

    drawRobot(r, world);

    for (auto& p : particles_) {
        if (p.glow) continue;
        float k = std::min(1.f, p.life / (p.maxLife * 0.4f));
        cube(r, p.pos, Vec3{1, 1, 1} * (p.size * k), p.color, 0.1f, p.rot);
    }

    // ---- translucent pass ----
    r.setDepthWrite(false);
    if (world) {
        for (auto& s : world->shields) {
            bool proof = world->players[s.side].bulletproofTimer > 0;
            Color col = proof ? Color::hex(0xffc414) : (s.side == Human ? t.shieldHuman : t.shieldCpu);
            float alpha = proof ? 0.85f : 0.55f + s.flash * 0.4f;
            Vec3 c = toWorld({s.x, s.y}, 0.45f);
            cube(r, c, {s.w * S, 0.9f, std::max(s.h * S, 0.14f)}, col.withAlpha(alpha), proof ? 0.8f : 0.3f + s.flash * 0.6f);
        }
        for (auto& p : world->pucks) {
            Vec3 c = toWorld(p.pos, 0.012f);
            cube(r, c, {Unit * S * 1.15f, 0.01f, Unit * S * 1.15f}, Color{0, 0, 0, p.ghostballTimer > 0 ? 0.04f : 0.18f});
            if (p.ghostballTimer > 0) {
                float flicker = std::fmod(time_, 0.6f) < 0.08f ? 0.35f : 0.07f;
                cube(r, toWorld(p.pos, 0.3f), {Unit * S, 0.6f, Unit * S}, t.puck.withAlpha(flicker), 0.5f);
            }
        }
    }
    r.setAdditive(true);
    for (auto& p : particles_) {
        if (!p.glow) continue;
        float k = p.life / p.maxLife;
        cube(r, p.pos, Vec3{1, 1, 1} * (p.size * (0.4f + 0.6f * k)), p.color.withAlpha(k * 0.9f), 1, p.rot);
    }
    r.setAdditive(false);
    r.setDepthWrite(true);
    r.end();

    for (auto& f : floatTexts_) {
        gfx::Vec2 px;
        if (r.project(f.pos, px)) labels_.push_back({px, f.text, f.color.withAlpha(std::min(1.f, f.life * 2)), f.scale});
    }
}

} // namespace game
