#pragma once
// Tiny OpenGL 2.x function loader built on SFML's context (no GLEW/glad needed).
// Core 1.1 functions (glClear, glDrawArrays, ...) come straight from SFML/OpenGL.hpp.
#include <SFML/OpenGL.hpp>
#include <cstddef>

#ifndef APIENTRY
#define APIENTRY
#endif

namespace gl {

using Char = char;
using SizeIPtr = std::ptrdiff_t;

constexpr GLenum ARRAY_BUFFER = 0x8892;
constexpr GLenum STATIC_DRAW = 0x88E4;
constexpr GLenum DYNAMIC_DRAW = 0x88E8;
constexpr GLenum FRAGMENT_SHADER = 0x8B30;
constexpr GLenum VERTEX_SHADER = 0x8B31;
constexpr GLenum COMPILE_STATUS = 0x8B81;
constexpr GLenum LINK_STATUS = 0x8B82;
constexpr GLenum INFO_LOG_LENGTH = 0x8B84;
constexpr GLenum MULTISAMPLE = 0x809D;

#define CS_GL_FUNCTIONS(X)                                                                                 \
    X(GLuint, CreateShader, (GLenum type))                                                                 \
    X(void, ShaderSource, (GLuint s, GLsizei n, const Char* const* src, const GLint* len))                 \
    X(void, CompileShader, (GLuint s))                                                                     \
    X(void, GetShaderiv, (GLuint s, GLenum p, GLint* out))                                                 \
    X(void, GetShaderInfoLog, (GLuint s, GLsizei max, GLsizei* len, Char* log))                            \
    X(void, DeleteShader, (GLuint s))                                                                      \
    X(GLuint, CreateProgram, ())                                                                           \
    X(void, AttachShader, (GLuint p, GLuint s))                                                            \
    X(void, BindAttribLocation, (GLuint p, GLuint index, const Char* name))                                \
    X(void, LinkProgram, (GLuint p))                                                                       \
    X(void, GetProgramiv, (GLuint p, GLenum pname, GLint* out))                                            \
    X(void, GetProgramInfoLog, (GLuint p, GLsizei max, GLsizei* len, Char* log))                           \
    X(void, DeleteProgram, (GLuint p))                                                                     \
    X(void, UseProgram, (GLuint p))                                                                        \
    X(GLint, GetUniformLocation, (GLuint p, const Char* name))                                             \
    X(void, Uniform1i, (GLint loc, GLint v))                                                               \
    X(void, Uniform1f, (GLint loc, GLfloat v))                                                             \
    X(void, Uniform2f, (GLint loc, GLfloat a, GLfloat b))                                                  \
    X(void, Uniform3f, (GLint loc, GLfloat a, GLfloat b, GLfloat c))                                       \
    X(void, Uniform4f, (GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d))                            \
    X(void, UniformMatrix4fv, (GLint loc, GLsizei n, GLboolean transpose, const GLfloat* v))               \
    X(void, GenBuffers, (GLsizei n, GLuint* out))                                                          \
    X(void, DeleteBuffers, (GLsizei n, const GLuint* bufs))                                                \
    X(void, BindBuffer, (GLenum target, GLuint buf))                                                       \
    X(void, BufferData, (GLenum target, SizeIPtr size, const void* data, GLenum usage))                    \
    X(void, VertexAttribPointer, (GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void* p)) \
    X(void, EnableVertexAttribArray, (GLuint i))                                                           \
    X(void, DisableVertexAttribArray, (GLuint i))

#define CS_GL_DECLARE(ret, name, args) \
    using PFN_##name = ret(APIENTRY*) args; \
    extern PFN_##name name;
CS_GL_FUNCTIONS(CS_GL_DECLARE)
#undef CS_GL_DECLARE

/** Loads all function pointers. Requires an active OpenGL context. Returns false if any are missing. */
bool load();

} // namespace gl
