#pragma once
#include <SFML/Audio.hpp>

#include <array>
#include <memory>
#include <vector>

namespace game {

enum class Sfx {
    PaddleHit, CpuHit, Wall, Shield, ShieldBreak, Obstacle, Destroy, Spawn, Activate,
    Laser, Shrink, Explode, Score, Lose, Beep, Go, LevelWin, GameOver, Fireball, Force, Count
};

/** All sound effects are synthesised at start-up; no audio files needed. */
class Audio {
public:
    void init();
    void play(Sfx s, float pitch = 1.f, float volume = 70.f);
    void toggleMute() { muted_ = !muted_; }
    bool muted() const { return muted_; }

private:
    std::array<sf::SoundBuffer, size_t(Sfx::Count)> buffers_;
    std::vector<std::unique_ptr<sf::Sound>> voices_;
    bool muted_ = false;
    bool ready_ = false;
};

} // namespace game
