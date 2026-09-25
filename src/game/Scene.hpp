#pragma once
#include "game/Theme.hpp"
#include "game/World.hpp"
#include "gfx/Renderer.hpp"

#include <random>
#include <string>
#include <vector>

namespace game {

/** 2D label produced by the 3D scene for the HUD to draw (already projected). */
struct Label {
    gfx::Vec2 px;
    std::string text;
    gfx::Color color;
    float scale = 1;
};

/** Visual representation of a World: meshes, camera, particles, and CUBOT the robot. */
class Scene {
public:
    /** Solo: behind the bottom paddle. Overhead: shared view for two local players. Top: behind the top paddle. */
    enum class View { Solo, Overhead, Top };

    bool init();
    /** Camera and presentation for the game mode. CUBOT only shows up when it is playing. */
    void setView(View view, bool cubotPlays) { view_ = view; cubotPlays_ = cubotPlays; }
    View view() const { return view_; }
    /** Suffixes for power-up pick-up texts, per side (e.g. "!" and " (CUBOT)"). */
    void setSideTags(std::string bottom, std::string top) { sideTags_[0] = std::move(bottom); sideTags_[1] = std::move(top); }
    void setupLevel(const World& world, const Theme& theme);
    void rebuildObstacles(const World& world);

    void handleEvents(const World& world);
    void update(float dt, const World* world);
    void render(gfx::Renderer& r, const World* world, int width, int height);

    // Presentation cues from the game flow.
    void startIntro() { introTime_ = 0; }
    void setTitleMode(bool on) { titleMode_ = on; }
    void robotHappy() { robotHappy_ = 2.2f; }
    void robotHurt() { robotHurt_ = 2.2f; shake_ = std::max(shake_, 0.8f); }
    void robotExplode();
    void robotReset() { robotAlive_ = true; robotHappy_ = robotHurt_ = 0; }
    void shake(float amount) { shake_ = std::max(shake_, amount); }

    const gfx::Camera& camera() const { return camera_; }
    /** Camera without shake/intro, used to map the mouse to the paddle. */
    gfx::Camera referenceCamera() const;

    const std::vector<Label>& labels() const { return labels_; }
    void floatText(gfx::Vec3 pos, const std::string& text, gfx::Color color, float scale = 1);

    const Theme& theme() const { return *theme_; }

private:
    struct Particle {
        gfx::Vec3 pos, vel, rot, spin;
        float size, life, maxLife;
        gfx::Color color;
        bool glow = false;
        bool gravity = true;
    };
    struct FloatText {
        gfx::Vec3 pos;
        std::string text;
        gfx::Color color;
        float life, scale;
    };

    void burst(gfx::Vec3 at, gfx::Color color, int count, float speed, float size, bool glow = false);
    void buildArena();
    void buildTerrain();
    void drawRobot(gfx::Renderer& r, const World* world);
    void cube(gfx::Renderer& r, gfx::Vec3 center, gfx::Vec3 size, gfx::Color c, float emissive = 0, gfx::Vec3 euler = {});
    gfx::Mat4 beam(gfx::Vec3 a, gfx::Vec3 b, float thickness) const;

    const Theme* theme_ = &themes()[0];
    gfx::Mesh cube_, octa_, arena_, grid_, terrain_;
    std::vector<gfx::Mesh> obstacleMeshes_;
    std::vector<Particle> particles_;
    std::vector<FloatText> floatTexts_;
    std::vector<Label> labels_;
    gfx::Camera camera_;
    std::mt19937 rng_{42};

    float time_ = 0;
    float introTime_ = 99;
    float shake_ = 0;
    float fog_ = 0; // 0..1 fog amount for the viewing player
    float humanX_ = 0; // paddle the camera follows
    bool titleMode_ = true;
    View view_ = View::Solo;
    bool cubotPlays_ = true;
    std::string sideTags_[2] = {"!", " (CUBOT)"};

    float robotHappy_ = 0, robotHurt_ = 0;
    bool robotAlive_ = true;
    float robotLook_ = 0;
    float cpuPaddleX_ = 0;
};

} // namespace game
