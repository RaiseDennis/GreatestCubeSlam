#include "game/Levels.hpp"

#include "game/Config.hpp"

#include <cmath>

namespace game {

using namespace cfg;

const char* extraName(ExtraType t) {
    switch (t) {
    case ExtraType::ExtraLife: return "EXTRA LIFE";
    case ExtraType::GhostBall: return "GHOST BALL";
    case ExtraType::FireBall: return "FIREBALL";
    case ExtraType::MirroredControls: return "MIRROR";
    case ExtraType::BulletProof: return "BULLETPROOF";
    case ExtraType::PaddleResize: return "BIG PADDLE";
    case ExtraType::DeathBall: return "DEATH BALL";
    case ExtraType::TimeBomb: return "TIME BOMB";
    case ExtraType::Laser: return "LASER";
    case ExtraType::Fog: return "FOG";
    case ExtraType::MultiBall: return "MULTIBALL";
    case ExtraType::Random: return "???";
    }
    return "";
}

gfx::Color extraColor(ExtraType t) {
    switch (t) {
    case ExtraType::ExtraLife: return gfx::Color::hex(0x3ddc84);
    case ExtraType::GhostBall: return gfx::Color::hex(0xb8c6ff);
    case ExtraType::FireBall: return gfx::Color::hex(0xff6a1a);
    case ExtraType::MirroredControls: return gfx::Color::hex(0xd65bff);
    case ExtraType::BulletProof: return gfx::Color::hex(0xffc414);
    case ExtraType::PaddleResize: return gfx::Color::hex(0x2fb6ff);
    case ExtraType::DeathBall: return gfx::Color::hex(0x1a1a1a);
    case ExtraType::TimeBomb: return gfx::Color::hex(0xff2d55);
    case ExtraType::Laser: return gfx::Color::hex(0xff1f1f);
    case ExtraType::Fog: return gfx::Color::hex(0x9aa3a8);
    case ExtraType::MultiBall: return gfx::Color::hex(0xfff05a);
    case ExtraType::Random: return gfx::Color::hex(0xffffff);
    }
    return {};
}

namespace {

ExtraDef ex(ExtraType t, float probability = 0, int round = 0, float duration = 0) {
    ExtraDef d;
    d.type = t;
    d.probability = probability;
    d.round = round;
    d.duration = duration;
    return d;
}

using E = ExtraType;

std::vector<Level> buildLevels() {
    std::vector<Level> L(12);

    // 1
    L[0].maxExtras = 3;
    L[0].ai = {10, 0.2f, 0.4f, 1};
    L[0].puck = {1.3f, 0.1f, 2};
    L[0].shields = 1;
    L[0].set = "empty";
    L[0].extras = {ex(E::ExtraLife, 10, 2), ex(E::GhostBall, 6, 4), ex(E::PaddleResize)};

    // 2
    L[1].minSpawnTime = 2; L[1].maxSpawnTime = 6; L[1].maxExtras = 3;
    L[1].ai = {10, 0.2f, 0.6f, 0.8f};
    L[1].puck = {1.3f, 0.1f, 2};
    L[1].shields = 2;
    L[1].set = "empty";
    {
        ExtraDef life = ex(E::ExtraLife, 40); life.simultaneous = 2;
        ExtraDef multi = ex(E::MultiBall, 60); multi.simultaneous = 1;
        L[1].extras = {life, multi, ex(E::FireBall, 40, 2), ex(E::Fog, 40, 3), ex(E::GhostBall, 60, 4)};
    }

    // 3
    L[2].minSpawnTime = 3; L[2].maxSpawnTime = 6; L[2].maxExtras = 4;
    L[2].ai = {10, 0.2f, 0.5f, 0.7f};
    L[2].puck = {1.3f, 0.1f, 2};
    L[2].shields = 3;
    L[2].set = "triangles";
    L[2].extras = {ex(E::ExtraLife, 6), ex(E::TimeBomb, 6, 2), ex(E::Laser, 12, 3), ex(E::GhostBall, 6, 2), ex(E::Fog, 1)};
    L[2].positions = {{200, 200}, {200, 2000}, {1400, 200}, {1400, 2000}};

    // 4
    L[3].maxExtras = 4;
    L[3].ai = {10, 0.3f, 0.5f, 0.5f};
    L[3].puck = {1.5f, 0.1f, 2.5f};
    L[3].shields = 4;
    L[3].set = "centerattract";
    L[3].extras = {ex(E::ExtraLife, 20), ex(E::Laser, 15), ex(E::BulletProof, 10, 0, 10), ex(E::GhostBall, 10, 4), ex(E::TimeBomb, 10, 2)};

    // 5
    L[4].ai = {10, 0.34f, 0.5f, 0.5f};
    L[4].puck = {1.6f, 0.1f, 2.5f};
    L[4].shields = 5;
    L[4].set = "diagonalattract";
    L[4].extras = {ex(E::ExtraLife, 5), ex(E::Fog, 0, 0, 5), ex(E::FireBall, 10), ex(E::GhostBall, 5), ex(E::PaddleResize, 10, 2), ex(E::Laser, 10, 3)};

    // 6
    L[5].minSpawnTime = 3; L[5].maxSpawnTime = 6; L[5].maxExtras = 3;
    L[5].ai = {15, 0.25f, 0.3f, 0.5f};
    L[5].puck = {1.5f, 0.1f, 2};
    L[5].shields = 6;
    L[5].set = "hexagon";
    L[5].extras = {ex(E::FireBall, 5), ex(E::ExtraLife, 10), ex(E::Fog, 5, 0, 6), ex(E::BulletProof, 5)};

    // 7
    L[6].minSpawnTime = 3; L[6].maxSpawnTime = 5;
    L[6].ai = {19, 0.3f, 0.3f, 0.5f};
    L[6].puck = {1.7f, 0.1f, 2.5f};
    L[6].shields = 7;
    L[6].set = "diagonalblocks";
    L[6].extras = {ex(E::ExtraLife, 10), ex(E::Laser, 20), ex(E::PaddleResize, 20), ex(E::Fog, 5, 0, 5),
                   ex(E::FireBall, 10), ex(E::GhostBall, 5), ex(E::BulletProof, 10, 0, 10)};

    // 8
    L[7].ai = {19, 0.3f, 0.3f, 0.45f};
    L[7].puck = {1.8f, 0.1f, 2.5f};
    L[7].shields = 8;
    L[7].set = "diamond";
    L[7].extras = {ex(E::ExtraLife), ex(E::Fog, 0, 0, 10), ex(E::FireBall), ex(E::GhostBall), ex(E::BulletProof, 0, 0, 10),
                   ex(E::MirroredControls, 0, 0, 10)};

    // 9
    L[8].ai = {13, 0.3f, 0.6f, 0.5f};
    L[8].puck = {1.6f, 0.1f, 2.5f};
    L[8].shields = 9;
    L[8].set = "breakout";
    L[8].extras = {ex(E::ExtraLife), ex(E::Fog, 0, 0, 10), ex(E::MultiBall), ex(E::FireBall), ex(E::GhostBall),
                   ex(E::BulletProof, 0, 0, 5), ex(E::MirroredControls, 0, 0, 10)};

    // 10
    L[9].maxExtras = 4;
    L[9].ai = {17, 0.3f, 0.5f, 0.5f};
    L[9].puck = {1.6f, 0.1f, 2.6f};
    L[9].shields = 10;
    L[9].set = "deathballblocks";
    {
        ExtraDef death = ex(E::DeathBall, 5, 3, 6);
        death.fixedPosition = true;
        death.position = {ArenaW / 2, ArenaH / 2};
        L[9].extras = {death, ex(E::ExtraLife, 2), ex(E::TimeBomb, 2), ex(E::FireBall, 2), ex(E::GhostBall, 2, 2),
                       ex(E::BulletProof, 2, 2, 10)};
    }

    // 11 + 12
    for (int i : {10, 11}) {
        L[i].ai = {15, 0.2f, 0.5f, 0.5f};
        L[i].puck = {1.7f, 0.2f, 2.3f};
        L[i].shields = i + 1;
        L[i].extras = {ex(E::ExtraLife), ex(E::Laser), ex(E::Fog), ex(E::PaddleResize), ex(E::MirroredControls),
                       ex(E::GhostBall, 0, 2), ex(E::BulletProof, 0, 2), ex(E::TimeBomb, 0, 3), ex(E::Laser, 0, 3)};
    }
    L[10].set = "diamondsnake";
    L[11].set = "random";
    return L;
}

const std::vector<Vec2> kDefaultPositions = {{200, 200}, {850, 1700}, {850, 700}, {200, 2000}, {1400, 200}, {1400, 2000}};

ObstacleDef obs(ObstacleShape s, float x, float y, float w = 0, float h = 0, bool destroyable = false) {
    return {s, x, y, w, h, destroyable};
}

} // namespace

const std::vector<Level>& singlePlayerLevels() {
    static const std::vector<Level> levels = buildLevels();
    return levels;
}

LevelSet makeLevelSet(const std::string& requested, std::mt19937& rng) {
    static const char* randomPool[] = {"triangles", "centerattract", "diagonalattract", "hexagon", "diagonalblocks",
                                       "diamond", "breakout", "diamondsnake", "tridiamonds", "trianglesbarrier",
                                       "columns", "arrows", "pipe", "barrier", "diagonalrocks"};
    std::string name = requested;
    if (name == "random") {
        std::uniform_int_distribution<size_t> pick(0, std::size(randomPool) - 1);
        name = randomPool[pick(rng)];
    }

    const float aw = ArenaW, ah = ArenaH, hw = aw / 2, hh = ah / 2, us = Unit;
    using O = ObstacleShape;
    LevelSet s;
    s.name = name;
    s.positions = kDefaultPositions;

    if (name == "triangles") {
        s.obstacles = {obs(O::TriangleLeft, 100, hh), obs(O::TriangleRight, aw - 100, hh)};
    } else if (name == "centerattract") {
        s.forces = {{true, hw + us * 0.5f, hh, 800}};
    } else if (name == "diagonalattract") {
        s.forces = {{true, aw * .75f, ah * .25f, 600, .5f}, {true, aw * .5f + us * .5f, ah * .5f, 800, .4f},
                    {true, aw * .25f, ah * .75f, 600, .5f}};
    } else if (name == "hexagon") {
        s.obstacles = {obs(O::Hexagon, hw, hh)};
        s.positions = {{200, 200}, {850, 1700}, {200, 2000}, {1400, 200}, {1400, 2000}};
    } else if (name == "diagonalblocks") {
        s.obstacles = {obs(O::Rect, us * 4, hh + us * 7, 2, 2, true), obs(O::Rect, hw, hh, 2, 2, true),
                       obs(O::Rect, aw - us * 4, hh - us * 7, 2, 2, true)};
    } else if (name == "diamond") {
        s.obstacles = {obs(O::Diamond, hw, hh, 2)};
    } else if (name == "breakout") {
        for (int i = 0; i < 9; ++i) {
            s.obstacles.push_back(obs(O::Rect, us * (1.5f + 2 * i), ah * .5f, 1, 1, true));
            s.obstacles.push_back(obs(O::Rect, us * (0.5f + 2 * i), ah * .5f + us * 3, 1, 1, true));
            s.obstacles.push_back(obs(O::Rect, us * (0.5f + 2 * i), ah * .5f - us * 3, 1, 1, true));
        }
    } else if (name == "deathballblocks") {
        for (int i = -3; i <= 3; ++i) {
            s.obstacles.push_back(obs(O::Rect, hw - us * 3.5f, hh + us * i, 1, 1, true));
            s.obstacles.push_back(obs(O::Rect, hw + us * 3.5f, hh + us * i, 1, 1, true));
        }
    } else if (name == "diamondsnake") {
        s.obstacles = {obs(O::Diamond, hw, hh + us * 5, 2), obs(O::Diamond, hw, hh - us * 5, 2),
                       obs(O::TriangleLeft, hw + us - 45, hh + us * 1.5f, 1.5f, 1.5f),
                       obs(O::TriangleRight, hw - us + 45, hh - us * 1.5f, 1.5f, 1.5f),
                       obs(O::TriangleLeft, 40, hh + us * 1.5f, 1, 2), obs(O::TriangleRight, aw - 40, hh - us * 1.5f, 1, 2)};
    } else if (name == "tridiamonds") {
        s.obstacles = {obs(O::Diamond, hw, hh, 2), obs(O::TriangleLeft, 40, hh, 1, 2), obs(O::TriangleRight, aw - 40, hh, 1, 2),
                       obs(O::Diamond, us * 3, hh + us * 6, 1), obs(O::Diamond, aw - us * 3, hh - us * 6, 1)};
    } else if (name == "trianglesbarrier") {
        s.obstacles = {obs(O::TriangleLeft, 40, hh, 1, 2), obs(O::TriangleRight, aw - 40, hh, 1, 2),
                       obs(O::Diamond, hw - 5 * us, hh + 5 * us, 1), obs(O::Diamond, hw + 5 * us, hh - 5 * us, 1),
                       obs(O::Diamond, hw, hh, 1)};
    } else if (name == "diagonalrocks") {
        s.obstacles = {obs(O::TriangleLeft, 40, hh, 1, 2), obs(O::TriangleRight, aw - 40, hh, 1, 2), obs(O::Diamond, hw, hh, 2)};
    } else if (name == "columns") {
        for (int i = -2; i <= 2; ++i) s.obstacles.push_back(obs(O::Rect, hw + us * 3 * i, hh, 1, 3));
    } else if (name == "arrows") {
        s.obstacles = {obs(O::Rect, us * .5f, hh + us * 2.2f, 1, 4, true), obs(O::Rect, us * 1.5f, hh + us * 2.2f, 1, 4, true),
                       obs(O::Rect, aw - us * .5f, hh - us * 2.2f, 1, 4, true), obs(O::Rect, aw - us * 1.5f, hh - us * 2.2f, 1, 4, true),
                       obs(O::TriangleLeft, us * 3 - 30, hh + us * 2.2f, 2, 2), obs(O::TriangleRight, aw - us * 3 + 30, hh - us * 2.2f, 2, 2)};
    } else if (name == "pipe") {
        s.obstacles = {obs(O::Rect, hw - us * 3, us * 16, 2, 8), obs(O::Rect, hw + us * 3, us * 10, 2, 8)};
    } else if (name == "barrier") {
        for (int i = 10; i <= 17; ++i) s.obstacles.push_back(obs(O::Rect, hw, us * i, 2, 1, i != 12 && i != 15));
    } else if (name == "octagon") {
        s.obstacles = {obs(O::Octagon, hw, hh)};
    }
    // "empty": nothing
    return s;
}

std::vector<Vec2> obstaclePolygon(const ObstacleDef& d) {
    std::vector<Vec2> p;
    auto finish = [&] {
        Vec2 c;
        for (auto& v : p) c += v;
        c = c / float(p.size());
        for (auto& v : p) v -= c;
    };
    switch (d.shape) {
    case ObstacleShape::TriangleLeft:
    case ObstacleShape::TriangleRight: {
        float w = Unit * (d.sizeW > 0 ? d.sizeW : 3), h = Unit * (d.sizeH > 0 ? d.sizeH : 4);
        float dir = d.shape == ObstacleShape::TriangleLeft ? 1.f : -1.f;
        p = {{0, -h}, {w * dir, 0}, {0, h}};
        break;
    }
    case ObstacleShape::Diamond: {
        float w = Unit * (d.sizeW > 0 ? d.sizeW : 4);
        p = {{w, 0}, {0, w}, {-w, 0}, {0, -w}};
        break;
    }
    case ObstacleShape::Hexagon:
    case ObstacleShape::Octagon: {
        int n = d.shape == ObstacleShape::Hexagon ? 6 : 8;
        float w = Unit * (d.sizeW > 0 ? d.sizeW : (n == 6 ? 3 : 2.5f));
        for (int i = 0; i < n; ++i) {
            float a = 2 * gfx::PI * i / n + (n == 8 ? gfx::PI / 8 : 0);
            p.push_back({w * std::cos(a), w * std::sin(a)});
        }
        break;
    }
    case ObstacleShape::Rect: {
        float w = Unit * (d.sizeW > 0 ? d.sizeW : 1), h = Unit * (d.sizeH > 0 ? d.sizeH : 1);
        p = {{-w / 2, -h / 2}, {w / 2, -h / 2}, {w / 2, h / 2}, {-w / 2, h / 2}};
        break;
    }
    }
    finish();
    return p;
}

} // namespace game
