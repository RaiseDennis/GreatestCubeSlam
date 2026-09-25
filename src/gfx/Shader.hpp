#pragma once
#include "gfx/GL.hpp"
#include "gfx/Math.hpp"

#include <string>
#include <unordered_map>

namespace gfx {

/** A linked GLSL program with cached uniform locations. */
class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    /** Attribute names are bound to locations 0..n-1 in the given order before linking. */
    bool build(const char* vertexSrc, const char* fragmentSrc, std::initializer_list<const char*> attributes);

    void use() const { gl::UseProgram(program_); }
    GLint uniform(const char* name);

    void set(const char* name, float v) { gl::Uniform1f(uniform(name), v); }
    void set(const char* name, Vec2 v) { gl::Uniform2f(uniform(name), v.x, v.y); }
    void set(const char* name, Vec3 v) { gl::Uniform3f(uniform(name), v.x, v.y, v.z); }
    void set(const char* name, Color c) { gl::Uniform4f(uniform(name), c.r, c.g, c.b, c.a); }
    void set(const char* name, const Mat4& m) { gl::UniformMatrix4fv(uniform(name), 1, GL_FALSE, m.m); }

private:
    GLuint program_ = 0;
    std::unordered_map<std::string, GLint> uniforms_;
};

} // namespace gfx
