#pragma once
#include "gfx/Math.hpp"

#include <vector>

namespace game {

struct Theme {
    const char* name;
    gfx::Color sky, arena, grid, pedestal, wall;
    gfx::Color terrainLow, terrainHigh, tree;
    gfx::Color puck, human, cpu, shieldHuman, shieldCpu;
    gfx::Color obstacle, block, robot, accent;
};

inline const std::vector<Theme>& themes() {
    using C = gfx::Color;
    static const std::vector<Theme> list = {
        {"Meadow", C::hex(0xedecd6), C::hex(0xf6f3e4), C::hex(0xd9d3b8), C::hex(0xe0d9bf), C::hex(0xcfc7a8),
         C::hex(0x7fa46f), C::hex(0x2f5d4f), C::hex(0x2b4c3f),
         C::hex(0xffc400), C::hex(0x2f3e4c), C::hex(0xe0493a), C::hex(0x35a2ff), C::hex(0xff6a55),
         C::hex(0xb9b196), C::hex(0x86aecb), C::hex(0xe9e3cd), C::hex(0xff5a3c)},
        {"Dusk", C::hex(0xf5d9c6), C::hex(0xfbeee3), C::hex(0xe8cdb9), C::hex(0xe7c9b3), C::hex(0xd8b39b),
         C::hex(0x93708f), C::hex(0x40304f), C::hex(0x3b2a47),
         C::hex(0xffd84a), C::hex(0x3d2f52), C::hex(0xd93f6a), C::hex(0x5e7bff), C::hex(0xff5d8f),
         C::hex(0xc49d8d), C::hex(0xa38cc9), C::hex(0xf4e1d4), C::hex(0xff4f7b)},
        {"Glacier", C::hex(0xe3eef3), C::hex(0xf5fafc), C::hex(0xcfdfe7), C::hex(0xd6e4ea), C::hex(0xbcd0da),
         C::hex(0xa5c0cc), C::hex(0x4b6b7e), C::hex(0x2d4757),
         C::hex(0xff9f1a), C::hex(0x24394a), C::hex(0xe8543d), C::hex(0x2c8cff), C::hex(0xff7a4d),
         C::hex(0x9fb5c1), C::hex(0x7dc4d6), C::hex(0xeef5f8), C::hex(0x1fb5c9)},
        {"Desert", C::hex(0xf6e6c6), C::hex(0xfcf4e2), C::hex(0xecd8b3), C::hex(0xead3a8), C::hex(0xdcc193),
         C::hex(0xd9a468), C::hex(0x985634), C::hex(0x587a3a),
         C::hex(0xfff05a), C::hex(0x4a3526), C::hex(0xc9412b), C::hex(0x2fa7a0), C::hex(0xff7f3f),
         C::hex(0xcfae7d), C::hex(0xd98c5f), C::hex(0xf7ead2), C::hex(0xe8742a)},
        {"Night", C::hex(0x1b2130), C::hex(0x2a3245), C::hex(0x3c4863), C::hex(0x252c3d), C::hex(0x343d55),
         C::hex(0x2b3b4d), C::hex(0x121a24), C::hex(0x0e1a1f),
         C::hex(0xffe066), C::hex(0xd9e4ff), C::hex(0xff4d6d), C::hex(0x4dd2ff), C::hex(0xff4d6d),
         C::hex(0x46526e), C::hex(0x6b5bd6), C::hex(0x3a4560), C::hex(0x4dffc3)},
    };
    return list;
}

} // namespace game
