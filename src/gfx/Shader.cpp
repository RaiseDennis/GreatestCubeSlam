#include "gfx/Shader.hpp"

#include <cstdio>
#include <vector>

namespace gfx {

namespace {
GLuint compile(GLenum type, const char* src) {
    GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    GLint ok = 0;
    gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        gl::GetShaderiv(s, gl::INFO_LOG_LENGTH, &len);
        std::vector<char> log(len + 1);
        gl::GetShaderInfoLog(s, len, nullptr, log.data());
        std::fprintf(stderr, "[shader] compile error:\n%s\n", log.data());
        gl::DeleteShader(s);
        return 0;
    }
    return s;
}
} // namespace

Shader::~Shader() {
    if (program_) gl::DeleteProgram(program_);
}

bool Shader::build(const char* vertexSrc, const char* fragmentSrc, std::initializer_list<const char*> attributes) {
    GLuint vs = compile(gl::VERTEX_SHADER, vertexSrc);
    GLuint fs = compile(gl::FRAGMENT_SHADER, fragmentSrc);
    if (!vs || !fs) return false;

    program_ = gl::CreateProgram();
    gl::AttachShader(program_, vs);
    gl::AttachShader(program_, fs);
    GLuint index = 0;
    for (const char* a : attributes) gl::BindAttribLocation(program_, index++, a);
    gl::LinkProgram(program_);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);

    GLint ok = 0;
    gl::GetProgramiv(program_, gl::LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        gl::GetProgramiv(program_, gl::INFO_LOG_LENGTH, &len);
        std::vector<char> log(len + 1);
        gl::GetProgramInfoLog(program_, len, nullptr, log.data());
        std::fprintf(stderr, "[shader] link error:\n%s\n", log.data());
        return false;
    }
    return true;
}

GLint Shader::uniform(const char* name) {
    auto it = uniforms_.find(name);
    if (it != uniforms_.end()) return it->second;
    GLint loc = gl::GetUniformLocation(program_, name);
    uniforms_.emplace(name, loc);
    return loc;
}

} // namespace gfx
