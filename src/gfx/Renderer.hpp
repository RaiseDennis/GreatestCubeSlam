#pragma once
#include "gfx/Math.hpp"
#include "gfx/Mesh.hpp"
#include "gfx/Shader.hpp"

namespace gfx {

struct Camera {
    Vec3 eye{0, 10, 10};
    Vec3 target{0, 0, 0};
    Vec3 up{0, 1, 0};
    float fovDegrees = 50;
    float zNear = 0.1f;
    float zFar = 400;

    Mat4 view() const { return Mat4::lookAt(eye, target, up); }
    Mat4 projection(float aspect) const { return Mat4::perspective(fovDegrees * PI / 180.f, aspect, zNear, zFar); }
};

/**
 * Forward renderer with one directional light + hemisphere ambient + distance fog.
 * Plays nicely with SFML: call begin()/end() around 3D drawing, then use SFML (after
 * RenderWindow::resetGLStates()) for 2D overlays.
 */
class Renderer {
public:
    bool init();

    void begin(const Camera& cam, int width, int height, Color clear);
    void end();

    void setLightDirection(Vec3 dirTowardsLight) { lightDir_ = normalize(dirTowardsLight); }
    void setFog(Color color, float start, float end) { fogColor_ = color; fogRange_ = {start, end}; }
    /** Disable depth writes for translucent objects (draw them last). */
    void setDepthWrite(bool on);
    /** Additive blending for glows/particles. */
    void setAdditive(bool on);

    /** tint multiplies vertex colors; emissive in [0,1] blends from lit to unlit (glow). */
    void draw(const Mesh& mesh, const Mat4& model, Color tint = {}, float emissive = 0);

    /** World -> window pixel coordinates. Returns false if behind the camera. */
    bool project(Vec3 world, Vec2& outPixels) const;

    const Mat4& viewProjection() const { return viewProj_; }

private:
    Shader lit_;
    Mat4 viewProj_;
    Vec3 eye_;
    Vec3 lightDir_ = normalize(Vec3{0.35f, 1.0f, 0.45f});
    Color fogColor_ = {1, 1, 1, 1};
    Vec2 fogRange_ = {60, 200};
    int width_ = 1, height_ = 1;
};

} // namespace gfx
