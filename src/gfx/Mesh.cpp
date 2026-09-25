#include "gfx/Mesh.hpp"

#include <utility>

namespace gfx {

void MeshBuilder::triangle(Vec3 a, Vec3 b, Vec3 c, Vec3 color) {
    triangle(a, b, c, normalize(cross(b - a, c - a)), color);
}

void MeshBuilder::triangle(Vec3 a, Vec3 b, Vec3 c, Vec3 n, Vec3 color) {
    vertices.push_back({a, n, color});
    vertices.push_back({b, n, color});
    vertices.push_back({c, n, color});
}

void MeshBuilder::quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 color) {
    Vec3 n = normalize(cross(b - a, c - a));
    triangle(a, b, c, n, color);
    triangle(a, c, d, n, color);
}

void MeshBuilder::line(Vec3 a, Vec3 b, Vec3 color) {
    vertices.push_back({a, {0, 1, 0}, color});
    vertices.push_back({b, {0, 1, 0}, color});
}

void MeshBuilder::box(Vec3 c, Vec3 s, Vec3 color) {
    Vec3 h = s * 0.5f;
    Vec3 p[8] = {
        {c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z},
        {c.x + h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y + h.y, c.z - h.z},
        {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z},
        {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z},
    };
    quad(p[4], p[5], p[6], p[7], color); // +z
    quad(p[1], p[0], p[3], p[2], color); // -z
    quad(p[5], p[1], p[2], p[6], color); // +x
    quad(p[0], p[4], p[7], p[3], color); // -x
    quad(p[7], p[6], p[2], p[3], color); // +y
    quad(p[0], p[1], p[5], p[4], color); // -y
}

void MeshBuilder::prism(const std::vector<Vec2>& poly, float y0, float y1, Vec3 color) {
    if (poly.size() < 3) return;
    Vec2 centroid;
    for (auto& p : poly) centroid += p;
    centroid = centroid / float(poly.size());

    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        Vec2 a = poly[i], b = poly[(i + 1) % n];
        // Caps (fan around centroid); normals are explicit so winding doesn't matter.
        triangle({centroid.x, y1, centroid.y}, {a.x, y1, a.y}, {b.x, y1, b.y}, {0, 1, 0}, color);
        triangle({centroid.x, y0, centroid.y}, {b.x, y0, b.y}, {a.x, y0, a.y}, {0, -1, 0}, color);
        // Side: outward normal points away from the centroid.
        Vec2 e = b - a;
        Vec2 nrm = normalize(Vec2{e.y, -e.x});
        Vec2 mid = (a + b) * 0.5f;
        if (dot(nrm, mid - centroid) < 0) nrm = -nrm;
        Vec3 n3{nrm.x, 0, nrm.y};
        triangle({a.x, y0, a.y}, {b.x, y0, b.y}, {b.x, y1, b.y}, n3, color);
        triangle({a.x, y0, a.y}, {b.x, y1, b.y}, {a.x, y1, a.y}, n3, color);
    }
}

void MeshBuilder::cone(Vec3 base, float radius, float height, int sides, Vec3 color) {
    Vec3 tip = base + Vec3{0, height, 0};
    for (int i = 0; i < sides; ++i) {
        float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
        Vec3 p0 = base + Vec3{std::cos(a0) * radius, 0, std::sin(a0) * radius};
        Vec3 p1 = base + Vec3{std::cos(a1) * radius, 0, std::sin(a1) * radius};
        Vec3 n = normalize(cross(p0 - tip, p1 - tip));
        if (n.y < 0) n = -n;
        triangle(tip, p0, p1, n, color);
    }
}

void MeshBuilder::octahedron(Vec3 c, float r, Vec3 color) {
    Vec3 top = c + Vec3{0, r, 0}, bot = c - Vec3{0, r, 0};
    Vec3 ring[4] = {c + Vec3{r, 0, 0}, c + Vec3{0, 0, r}, c + Vec3{-r, 0, 0}, c + Vec3{0, 0, -r}};
    for (int i = 0; i < 4; ++i) {
        Vec3 a = ring[i], b = ring[(i + 1) % 4];
        Vec3 nt = normalize((a + b + top) / 3.f - c);
        Vec3 nb = normalize((a + b + bot) / 3.f - c);
        triangle(top, a, b, nt, color);
        triangle(bot, b, a, nb, color);
    }
}

Mesh& Mesh::operator=(Mesh&& o) noexcept {
    std::swap(vbo_, o.vbo_);
    std::swap(count_, o.count_);
    std::swap(primitive_, o.primitive_);
    return *this;
}

Mesh::~Mesh() {
    if (vbo_) gl::DeleteBuffers(1, &vbo_);
}

void Mesh::upload(const MeshBuilder& b, GLenum primitive) {
    if (!vbo_) gl::GenBuffers(1, &vbo_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    gl::BufferData(gl::ARRAY_BUFFER, gl::SizeIPtr(b.vertices.size() * sizeof(Vertex)), b.vertices.data(), gl::STATIC_DRAW);
    gl::BindBuffer(gl::ARRAY_BUFFER, 0);
    count_ = GLsizei(b.vertices.size());
    primitive_ = primitive;
}

void Mesh::draw() const {
    if (!count_) return;
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    const auto stride = GLsizei(sizeof(Vertex));
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, pos)));
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, normal)));
    gl::VertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, color)));
    glDrawArrays(primitive_, 0, count_);
}

} // namespace gfx
