#pragma once
#include "gfx/GL.hpp"
#include "gfx/Math.hpp"

#include <vector>

namespace gfx {

struct Vertex {
    Vec3 pos;
    Vec3 normal;
    Vec3 color;
};

/** CPU-side geometry. Everything is flat-shaded, non-indexed triangles (or line pairs). */
class MeshBuilder {
public:
    std::vector<Vertex> vertices;

    void triangle(Vec3 a, Vec3 b, Vec3 c, Vec3 color);        // normal from winding (CCW = front)
    void triangle(Vec3 a, Vec3 b, Vec3 c, Vec3 n, Vec3 color);
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 color);     // a-b-c-d CCW
    void line(Vec3 a, Vec3 b, Vec3 color);

    /** Axis-aligned box centred on `center`. */
    void box(Vec3 center, Vec3 size, Vec3 color = {1, 1, 1});
    /** Extrude a convex 2D polygon (x, z) between y0 and y1. Works with either winding. */
    void prism(const std::vector<Vec2>& poly, float y0, float y1, Vec3 color = {1, 1, 1});
    /** Cone/pyramid with `sides` segments standing on y = base. */
    void cone(Vec3 base, float radius, float height, int sides, Vec3 color);
    /** Low-poly octahedron ("gem"). */
    void octahedron(Vec3 center, float radius, Vec3 color);
};

/** GPU vertex buffer. */
class Mesh {
public:
    Mesh() = default;
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& o) noexcept { *this = std::move(o); }
    Mesh& operator=(Mesh&& o) noexcept;

    void upload(const MeshBuilder& b, GLenum primitive = GL_TRIANGLES);
    void draw() const;
    bool empty() const { return count_ == 0; }

private:
    GLuint vbo_ = 0;
    GLsizei count_ = 0;
    GLenum primitive_ = GL_TRIANGLES;
};

} // namespace gfx
