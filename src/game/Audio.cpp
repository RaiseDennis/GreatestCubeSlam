#include "game/Audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace game {

namespace {

constexpr unsigned kRate = 44100;
constexpr float kTau = 6.2831853f;

enum class Wave { Sine, Square, Triangle, Saw, Noise };

struct Tone {
    float f0, f1;      // start/end frequency (exponential glide)
    float duration;
    Wave wave = Wave::Square;
    float volume = 0.5f;
    float attack = 0.004f;
    float delay = 0;   // start offset in seconds
};

using Samples = std::vector<float>;

void addTone(Samples& out, const Tone& t) {
    static std::mt19937 rng(1234);
    std::uniform_real_distribution<float> noise(-1, 1);
    size_t start = size_t(t.delay * kRate);
    size_t n = size_t(t.duration * kRate);
    if (out.size() < start + n) out.resize(start + n, 0.f);
    float phase = 0;
    float held = 0;
    for (size_t i = 0; i < n; ++i) {
        float u = float(i) / float(n);
        float f = t.f0 * std::pow(t.f1 / t.f0, u);
        phase += f / kRate;
        phase -= std::floor(phase);
        float s = 0;
        switch (t.wave) {
        case Wave::Sine: s = std::sin(phase * kTau); break;
        case Wave::Square: s = phase < 0.5f ? 0.6f : -0.6f; break;
        case Wave::Triangle: s = 4 * std::abs(phase - 0.5f) - 1; break;
        case Wave::Saw: s = 2 * phase - 1; break;
        case Wave::Noise:
            // sample-and-hold noise; f controls the "pitch" of the noise
            if (i % std::max<size_t>(1, size_t(kRate / std::max(f, 1.f))) == 0) held = noise(rng);
            s = held;
            break;
        }
        float time = float(i) / kRate;
        float env = std::min(1.f, time / t.attack) * std::pow(1 - u, 2.f);
        out[start + i] += s * env * t.volume;
    }
}

bool toBuffer(sf::SoundBuffer& buf, const Samples& in) {
    std::vector<std::int16_t> pcm(in.size());
    for (size_t i = 0; i < in.size(); ++i) pcm[i] = std::int16_t(std::clamp(in[i], -1.f, 1.f) * 30000);
    return buf.loadFromSamples(pcm.data(), pcm.size(), 1, kRate, {sf::SoundChannel::Mono});
}

Samples make(std::initializer_list<Tone> tones) {
    Samples s;
    for (auto& t : tones) addTone(s, t);
    return s;
}

} // namespace

void Audio::init() {
    using W = Wave;
    const Samples defs[] = {
        /* PaddleHit */ make({{520, 700, 0.09f, W::Square, 0.45f}, {1040, 900, 0.05f, W::Sine, 0.3f}}),
        /* CpuHit    */ make({{380, 480, 0.09f, W::Square, 0.40f}}),
        /* Wall      */ make({{220, 180, 0.06f, W::Triangle, 0.6f}}),
        /* Shield    */ make({{900, 300, 0.18f, W::Saw, 0.35f}, {3000, 800, 0.12f, W::Noise, 0.25f}}),
        /* ShieldBrk */ make({{600, 80, 0.35f, W::Square, 0.35f}, {4000, 400, 0.35f, W::Noise, 0.35f}}),
        /* Obstacle  */ make({{160, 120, 0.08f, W::Square, 0.35f}}),
        /* Destroy   */ make({{2000, 200, 0.25f, W::Noise, 0.45f}, {300, 90, 0.2f, W::Square, 0.25f}}),
        /* Spawn     */ make({{880, 880, 0.08f, W::Sine, 0.3f}, {1320, 1320, 0.12f, W::Sine, 0.3f, 0.004f, 0.07f}}),
        /* Activate  */ make({{440, 1760, 0.25f, W::Square, 0.3f}, {660, 2640, 0.25f, W::Sine, 0.2f, 0.004f, 0.05f}}),
        /* Laser     */ make({{1800, 200, 0.18f, W::Saw, 0.35f}}),
        /* Shrink    */ make({{800, 150, 0.3f, W::Triangle, 0.5f}}),
        /* Explode   */ make({{1500, 60, 0.8f, W::Noise, 0.7f}, {90, 40, 0.8f, W::Sine, 0.6f}}),
        /* Score     */ make({{523, 523, 0.12f, W::Square, 0.3f}, {659, 659, 0.12f, W::Square, 0.3f, 0.004f, 0.1f},
                              {784, 784, 0.12f, W::Square, 0.3f, 0.004f, 0.2f}, {1046, 1046, 0.3f, W::Square, 0.3f, 0.004f, 0.3f}}),
        /* Lose      */ make({{392, 392, 0.15f, W::Triangle, 0.5f}, {311, 311, 0.15f, W::Triangle, 0.5f, 0.004f, 0.15f},
                              {262, 180, 0.45f, W::Triangle, 0.5f, 0.004f, 0.3f}}),
        /* Beep      */ make({{660, 660, 0.14f, W::Square, 0.35f}}),
        /* Go        */ make({{990, 990, 0.35f, W::Square, 0.35f}, {1320, 1320, 0.35f, W::Sine, 0.25f}}),
        /* LevelWin  */ make({{523, 523, 0.12f, W::Square, 0.3f}, {659, 659, 0.12f, W::Square, 0.3f, 0.004f, 0.12f},
                              {784, 784, 0.12f, W::Square, 0.3f, 0.004f, 0.24f}, {1046, 1046, 0.12f, W::Square, 0.3f, 0.004f, 0.36f},
                              {784, 784, 0.12f, W::Square, 0.3f, 0.004f, 0.48f}, {1046, 1046, 0.6f, W::Square, 0.35f, 0.004f, 0.6f}}),
        /* GameOver  */ make({{330, 330, 0.3f, W::Saw, 0.3f}, {294, 294, 0.3f, W::Saw, 0.3f, 0.004f, 0.3f},
                              {262, 262, 0.3f, W::Saw, 0.3f, 0.004f, 0.6f}, {196, 110, 0.9f, W::Saw, 0.3f, 0.004f, 0.9f}}),
        /* Fireball  */ make({{200, 900, 0.3f, W::Noise, 0.4f}, {150, 400, 0.3f, W::Saw, 0.25f}}),
        /* Force     */ make({{110, 220, 0.3f, W::Sine, 0.4f}}),
    };
    static_assert(std::size(defs) == size_t(Sfx::Count), "one definition per Sfx");
    ready_ = true;
    for (size_t i = 0; i < std::size(defs); ++i) ready_ &= toBuffer(buffers_[i], defs[i]);
}

void Audio::play(Sfx s, float pitch, float volume) {
    if (!ready_ || muted_) return;
    const sf::SoundBuffer& buf = buffers_[size_t(s)];
    sf::Sound* voice = nullptr;
    for (auto& v : voices_)
        if (v->getStatus() != sf::Sound::Status::Playing) { voice = v.get(); break; }
    if (!voice) {
        if (voices_.size() >= 24) voice = voices_.front().get();
        else voice = voices_.emplace_back(std::make_unique<sf::Sound>(buf)).get();
    }
    voice->stop();
    voice->setBuffer(buf);
    voice->setPitch(pitch);
    voice->setVolume(volume);
    voice->play();
}

} // namespace game
