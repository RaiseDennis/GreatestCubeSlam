#include "gfx/Renderer.hpp"

namespace gfx {

namespace {
const char* kVertex = R"(
#version 120
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec3 aColor;
uniform mat4 uModel;
uniform mat4 uViewProj;
varying vec3 vNormal;
varying vec3 vColor;
varying vec3 vWorld;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorld = world.xyz;
    vNormal = mat3(uModel) * aNormal;
    vColor = aColor;
    gl_Position = uViewProj * world;
}
)";

const char* kFragment = R"(
#version 120
varying vec3 vNormal;
varying vec3 vColor;
varying vec3 vWorld;
uniform vec4 uTint;
uniform float uEmissive;
uniform vec3 uLightDir;
uniform vec3 uEye;
uniform vec4 uFogColor;
uniform vec2 uFogRange;
void main() {
    vec3 n = normalize(vNormal);
    vec3 base = vColor * uTint.rgb;
    float diffuse = max(dot(n, uLightDir), 0.0);
    float hemi = 0.5 + 0.5 * n.y;                   // sky/ground ambient
    vec3 lit = base * (0.30 + 0.30 * hemi + 0.55 * diffuse);
    vec3 color = mix(lit, base, uEmissive);
    float d = length(vWorld - uEye);
    float fog = clamp((d - uFogRange.x) / max(uFogRange.y - uFogRange.x, 0.001), 0.0, 1.0);
    color = mix(color, uFogColor.rgb, fog * uFogColor.a);
    gl_FragColor = vec4(color, uTint.a);
}
)";
} // namespace

bool Renderer::init() {
    return lit_.build(kVertex, kFragment, {"aPos", "aNormal", "aColor"});
}

void Renderer::begin(const Camera& cam, int width, int height, Color clear) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    viewProj_ = cam.projection(float(width_) / float(height_)) * cam.view();
    eye_ = cam.eye;

    // SFML may have left fixed-function client arrays enabled; they alias generic attribute 0 on some drivers.
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_TEXTURE_2D);

    glViewport(0, 0, width_, height_);
    glEnable(gl::MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(clear.r, clear.g, clear.b, 1);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    lit_.use();
    lit_.set("uViewProj", viewProj_);
    lit_.set("uLightDir", lightDir_);
    lit_.set("uEye", eye_);
    lit_.set("uFogColor", fogColor_);
    lit_.set("uFogRange", fogRange_);
    for (GLuint i = 0; i < 3; ++i) gl::EnableVertexAttribArray(i);
}

void Renderer::end() {
    for (GLuint i = 0; i < 3; ++i) gl::DisableVertexAttribArray(i);
    gl::BindBuffer(gl::ARRAY_BUFFER, 0);
    gl::UseProgram(0);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
}

void Renderer::setDepthWrite(bool on) { glDepthMask(on ? GL_TRUE : GL_FALSE); }

void Renderer::setAdditive(bool on) {
    if (on) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::draw(const Mesh& mesh, const Mat4& model, Color tint, float emissive) {
    lit_.set("uModel", model);
    lit_.set("uTint", tint);
    lit_.set("uEmissive", emissive);
    lit_.set("uFogColor", fogColor_);
    lit_.set("uFogRange", fogRange_);
    lit_.set("uLightDir", lightDir_);
    mesh.draw();
}

bool Renderer::project(Vec3 world, Vec2& out) const {
    Vec4 clip = viewProj_ * Vec4(world, 1);
    if (clip.w <= 0.001f) return false;
    float nx = clip.x / clip.w, ny = clip.y / clip.w;
    out = {(nx * 0.5f + 0.5f) * float(width_), (1 - (ny * 0.5f + 0.5f)) * float(height_)};
    return true;
}

} // namespace gfx
