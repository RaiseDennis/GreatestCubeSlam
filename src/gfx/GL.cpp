#include "gfx/GL.hpp"

#include <SFML/Window/Context.hpp>
#include <cstdio>

namespace gl {

#define CS_GL_DEFINE(ret, name, args) PFN_##name name = nullptr;
CS_GL_FUNCTIONS(CS_GL_DEFINE)
#undef CS_GL_DEFINE

bool load() {
    bool ok = true;
#define CS_GL_LOAD(ret, name, args)                                                          \
    name = reinterpret_cast<PFN_##name>(sf::Context::getFunction("gl" #name));             \
    if (!name) {                                                                           \
        std::fprintf(stderr, "[gl] missing function gl" #name "\n");                       \
        ok = false;                                                                        \
    }
    CS_GL_FUNCTIONS(CS_GL_LOAD)
#undef CS_GL_LOAD
    return ok;
}

} // namespace gl
