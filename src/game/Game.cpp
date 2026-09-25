#include "game/Game.hpp"

#include "gfx/GL.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

namespace game {

using namespace cfg;

namespace {

sf::Color toSf(gfx::Color c) {
    auto b = [](float v) { return std::uint8_t(std::clamp(v, 0.f, 1.f) * 255); };
    return {b(c.r), b(c.g), b(c.b), b(c.a)};
}

std::string fmt(const char* f, float v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

const sf::Color kInk{24, 28, 38};

} // namespace

bool Game::init() {
    if (!gl::load()) {
        std::fprintf(stderr, "OpenGL 2.0+ is required.\n");
        return false;
    }
    if (!renderer_.init()) return false;
    scene_.init();
    audio_.init();

    const char* fonts[] = {"C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/arial.ttf",
                           "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"};
    for (const char* f : fonts) {
        sf::Font font;
        if (font.openFromFile(f)) {
            font_ = std::move(font);
            break;
        }
    }
    if (!font_) std::fprintf(stderr, "No system font found; HUD text disabled.\n");

    window_.setKeyRepeatEnabled(false);
    setState(State::Title);
    if (opts_.startLevel >= 0) startLevel(opts_.startLevel);
    return true;
}

void Game::run() {
    sf::Clock clock;
    while (window_.isOpen()) {
        while (const std::optional event = window_.pollEvent()) handleEvent(*event);
        if (!window_.isOpen()) break;
        float dt = std::min(clock.restart().asSeconds(), 0.1f);
        runTime_ += dt;
        update(dt);
        draw();

        if (!opts_.screenshotPath.empty() && runTime_ >= opts_.screenshotAt) {
            sf::Texture shot(window_.getSize());
            shot.update(window_);
            if (!shot.copyToImage().saveToFile(opts_.screenshotPath))
                std::fprintf(stderr, "failed to save %s\n", opts_.screenshotPath.c_str());
            window_.close();
        }
    }
}

// ---------------------------------------------------------------- flow

void Game::setState(State s) {
    state_ = s;
    stateTime_ = 0;
    lastCountdown_ = -1;
    bool playing = s == State::Intro || s == State::Countdown || s == State::Playing || s == State::RoundEnd;
    window_.setMouseCursorVisible(!playing);
    scene_.setTitleMode(s == State::Title);
}

void Game::startLevel(int index) {
    const auto& levels = singlePlayerLevels();
    levelIndex_ = std::clamp(index, 0, int(levels.size()) - 1);
    scores_[0] = scores_[1] = 0;
    round_ = 1;
    serve_ = Human;
    paused_ = false;
    set_ = makeLevelSet(levels[levelIndex_].set, rng_);
    newRound();
    scene_.setupLevel(*world_, themes()[levelIndex_ % themes().size()]);
    scene_.robotReset();
    scene_.startIntro();
    input_ = {};
    setState(State::Intro);
}

void Game::newRound() {
    const Level& level = singlePlayerLevels()[levelIndex_];
    world_ = std::make_unique<World>(level, set_, round_, serve_, rng_());
    ai_ = std::make_unique<AI>(level.ai, rng_());
    scene_.rebuildObstacles(*world_);
    accumulator_ = 0;
}

void Game::confirm() {
    if (paused_) { paused_ = false; window_.setMouseCursorVisible(false); return; }
    switch (state_) {
    case State::Title: startLevel(titleLevel_); break;
    case State::Intro: stateTime_ = 99; break;
    case State::LevelWon:
        if (levelIndex_ + 1 < int(singlePlayerLevels().size())) startLevel(levelIndex_ + 1);
        else setState(State::Victory);
        break;
    case State::GameOver: startLevel(levelIndex_); break;
    case State::Victory: setState(State::Title); break;
    default: break;
    }
}

void Game::handleEvent(const sf::Event& e) {
    if (e.is<sf::Event::Closed>()) {
        window_.close();
        return;
    }
    if (const auto* r = e.getIf<sf::Event::Resized>()) {
        window_.setView(sf::View(sf::FloatRect({0, 0}, {float(r->size.x), float(r->size.y)})));
        return;
    }
    const bool inPlay = state_ == State::Countdown || state_ == State::Playing || state_ == State::RoundEnd || state_ == State::Intro;
    if (const auto* k = e.getIf<sf::Event::KeyPressed>()) {
        using K = sf::Keyboard::Key;
        switch (k->code) {
        case K::Escape:
            if (paused_) paused_ = false;
            else if (inPlay) paused_ = true;
            else if (state_ == State::Title) window_.close();
            else setState(State::Title);
            window_.setMouseCursorVisible(paused_ || !inPlay);
            break;
        case K::P:
            if (inPlay) { paused_ = !paused_; window_.setMouseCursorVisible(paused_); }
            break;
        case K::Q:
            if (paused_) { paused_ = false; setState(State::Title); }
            break;
        case K::M: audio_.toggleMute(); break;
        case K::Enter:
        case K::Space: confirm(); break;
        case K::Left:
        case K::A:
        case K::Right:
        case K::D:
            if (state_ == State::Title) {
                int n = int(singlePlayerLevels().size());
                int d = (k->code == K::Left || k->code == K::A) ? -1 : 1;
                titleLevel_ = (titleLevel_ + d + n) % n;
                audio_.play(Sfx::Beep, 1.2f, 40);
            } else {
                keyboardMode_ = true;
            }
            break;
        default: break;
        }
    }
    if (const auto* m = e.getIf<sf::Event::MouseMoved>()) {
        if (std::abs(m->position.x - mouse_.x) + std::abs(m->position.y - mouse_.y) > 2) keyboardMode_ = false;
        mouse_ = m->position;
    }
    if (const auto* b = e.getIf<sf::Event::MouseButtonPressed>()) {
        if (b->button == sf::Mouse::Button::Left && (!inPlay || paused_)) confirm();
    }
}

HumanInput autoPilot(const World& w, float wobble); // SelfTest.cpp

void Game::updateMouseTarget() {
    using K = sf::Keyboard::Key;
    if (opts_.demo && world_) {
        if (int(runTime_ * 60) % 90 == 0) demoWobble_ = std::sin(runTime_ * 7.3f) * 180;
        input_ = autoPilot(*world_, demoWobble_);
        return;
    }
    if (keyboardMode_) {
        float axis = 0;
        if (sf::Keyboard::isKeyPressed(K::Left) || sf::Keyboard::isKeyPressed(K::A)) axis -= 1;
        if (sf::Keyboard::isKeyPressed(K::Right) || sf::Keyboard::isKeyPressed(K::D)) axis += 1;
        input_.useAxis = true;
        input_.axis = axis;
        return;
    }
    // Map the cursor onto the paddle's line in 3D so the paddle sits right under it.
    auto size = window_.getSize();
    gfx::Camera cam = scene_.referenceCamera();
    gfx::Mat4 vp = cam.projection(float(size.x) / float(std::max(1u, size.y))) * cam.view();
    float z = (ArenaH / 2 - Unit) * S;
    auto px = [&](float x) {
        gfx::Vec4 c = vp * gfx::Vec4({x, 0.35f, z}, 1);
        return (c.x / c.w * 0.5f + 0.5f) * float(size.x);
    };
    float left = px(-ArenaW / 2 * S), right = px(ArenaW / 2 * S);
    if (std::abs(right - left) < 1) return;
    input_.useAxis = false;
    input_.targetX = std::clamp((float(mouse_.x) - left) / (right - left) * ArenaW, 0.f, ArenaW);
}

void Game::update(float dt) {
    if (paused_) return;
    stateTime_ += dt;
    flash_ = std::max(0.f, flash_ - dt * 2.5f);

    switch (state_) {
    case State::Title: break;
    case State::LevelWon:
    case State::GameOver:
        if (opts_.demo && stateTime_ > 3) confirm();
        break;
    case State::Intro:
        if (stateTime_ > 2.6f) setState(State::Countdown);
        break;
    case State::Countdown: {
        updateMouseTarget();
        int n = 3 - int(stateTime_ / 0.7f);
        if (n != lastCountdown_ && n > 0) {
            audio_.play(Sfx::Beep);
            lastCountdown_ = n;
        }
        if (stateTime_ >= 2.1f) {
            audio_.play(Sfx::Go);
            setState(State::Playing);
        }
        break;
    }
    case State::Playing: {
        updateMouseTarget();
        accumulator_ += dt;
        int steps = 0;
        while (accumulator_ >= Timestep && steps < 6 && state_ == State::Playing) {
            fixedStep();
            accumulator_ -= Timestep;
            ++steps;
        }
        if (steps == 6) accumulator_ = 0;
        break;
    }
    case State::RoundEnd:
        if (stateTime_ > 2.3f) {
            if (scores_[Human] >= WinningScore) {
                scene_.robotExplode();
                audio_.play(Sfx::Explode);
                audio_.play(Sfx::LevelWin);
                setState(State::LevelWon);
            } else if (scores_[Cpu] >= WinningScore) {
                audio_.play(Sfx::GameOver);
                scene_.robotHappy();
                setState(State::GameOver);
            } else {
                newRound();
                setState(State::Countdown);
            }
        }
        break;
    default: break;
    }

    scene_.update(dt, state_ == State::Title ? nullptr : world_.get());
}

void Game::fixedStep() {
    ai_->update(*world_);
    world_->step(input_);
    scene_.handleEvents(*world_);
    playEventSounds();

    if (world_->roundOver) {
        int loser = world_->roundLoser;
        int winner = other(loser);
        ++scores_[winner];
        serve_ = loser;
        ++round_;
        if (winner == Human) {
            scene_.robotHurt();
            audio_.play(Sfx::Score);
            flashColor_ = sf::Color::White;
        } else {
            scene_.robotHappy();
            audio_.play(Sfx::Lose);
            flashColor_ = sf::Color(255, 60, 60);
        }
        flash_ = 1;
        setState(State::RoundEnd);
    }
}

void Game::playEventSounds() {
    std::set<Sfx> played;
    auto play = [&](Sfx s, float pitch = 1, float vol = 70) {
        if (played.insert(s).second) audio_.play(s, pitch, vol);
    };
    for (auto& e : world_->events) {
        switch (e.type) {
        case EventType::PaddleHit: play(e.side == Human ? Sfx::PaddleHit : Sfx::CpuHit); break;
        case EventType::WallHit: play(Sfx::Wall, 1, 50); break;
        case EventType::ShieldHit: play(Sfx::Shield); break;
        case EventType::ShieldBreak: play(Sfx::ShieldBreak); break;
        case EventType::ObstacleHit: play(Sfx::Obstacle, 1, 55); break;
        case EventType::ObstacleDestroyed: play(Sfx::Destroy); break;
        case EventType::ExtraSpawn: play(Sfx::Spawn, 1, 50); break;
        case EventType::ExtraActivate: play(Sfx::Activate); break;
        case EventType::ExtraExpire: play(Sfx::Spawn, 0.6f, 35); break;
        case EventType::LaserFire: play(Sfx::Laser, 1, 55); break;
        case EventType::PaddleShrink: play(Sfx::Shrink); break;
        case EventType::BombExplode: play(Sfx::Explode); break;
        case EventType::FireballCharged:
        case EventType::FireballLaunched: play(Sfx::Fireball); break;
        case EventType::Dizzy: play(Sfx::Shrink, 1.6f); break;
        case EventType::ForceOn: play(Sfx::Force, 1, 45); break;
        case EventType::ForceOff: play(Sfx::Force, 0.7f, 35); break;
        case EventType::RoundOver: play(Sfx::Explode, 1.3f, 80); break;
        }
    }
}

// ---------------------------------------------------------------- drawing

void Game::draw() {
    auto size = window_.getSize();
    const World* w = state_ == State::Title ? nullptr : world_.get();
    scene_.render(renderer_, w, int(size.x), int(size.y));
    window_.resetGLStates();
    drawHud();
    window_.display();
}

void Game::drawText(const std::string& s, sf::Vector2f pos, unsigned size, sf::Color color, float alignX, float outline,
                    sf::Color outlineColor) {
    if (!font_) return;
    sf::Text text(*font_, s, size);
    text.setFillColor(color);
    if (outline > 0) {
        text.setOutlineThickness(outline);
        outlineColor.a = color.a;
        text.setOutlineColor(outlineColor);
    }
    sf::FloatRect b = text.getLocalBounds();
    text.setOrigin({b.position.x + b.size.x * alignX, b.position.y + b.size.y * 0.5f});
    text.setPosition({std::round(pos.x), std::round(pos.y)});
    window_.draw(text);
}

void Game::drawHud() {
    const auto size = window_.getSize();
    const float W = float(size.x), H = float(size.y);
    const float ui = std::clamp(H / 800.f, 0.6f, 2.f);
    auto sz = [&](float s) { return unsigned(std::max(8.f, s * ui)); };
    const sf::Color white(255, 255, 255);

    for (auto& l : scene_.labels())
        drawText(l.text, {l.px.x, l.px.y}, sz(17 * l.scale), toSf(l.color), 0.5f, 2.5f * ui, kInk);

    if (state_ == State::Title) {
        float pulse = 0.6f + 0.4f * std::sin(stateTime_ * 3);
        drawText("GREATEST", {W / 2, H * 0.17f}, sz(34), white, 0.5f, 3 * ui, kInk);
        drawText("CUBE SLAM", {W / 2, H * 0.27f}, sz(96), white, 0.5f, 6 * ui, kInk);
        drawText("a 3D arcade remake built with SFML", {W / 2, H * 0.37f}, sz(20), sf::Color(255, 255, 255, 220), 0.5f, 2 * ui, kInk);

        const Level& lvl = singlePlayerLevels()[titleLevel_];
        (void)lvl;
        drawText("<   LEVEL " + std::to_string(titleLevel_ + 1) + "   >", {W / 2, H * 0.55f}, sz(40), white, 0.5f, 4 * ui, kInk);
        drawText(std::string(themes()[titleLevel_ % themes().size()].name) + "  -  " + std::to_string(lvl.shields) +
                     (lvl.shields == 1 ? " shield" : " shields"),
                 {W / 2, H * 0.62f}, sz(20), sf::Color(255, 255, 255, 220), 0.5f, 2 * ui, kInk);
        drawText("CLICK or press ENTER to play", {W / 2, H * 0.74f}, sz(28), sf::Color(255, 255, 255, std::uint8_t(255 * pulse)), 0.5f,
                 3 * ui, kInk);
        drawText("Mouse or A/D/Arrows: move    Esc/P: pause    M: mute" + std::string(audio_.muted() ? " (muted)" : ""),
                 {W / 2, H * 0.92f}, sz(17), white, 0.5f, 2 * ui, kInk);
        return;
    }

    if (!world_) return;
    const World& w = *world_;
    const Theme& th = scene_.theme();

    // --- top bar: level + score pips ---
    drawText("LEVEL " + std::to_string(levelIndex_ + 1) + "/" + std::to_string(singlePlayerLevels().size()), {24 * ui, 30 * ui},
             sz(24), white, 0, 3 * ui, kInk);
    drawText(w.set.name == "empty" ? std::string(th.name) : std::string(th.name) + " - " + w.set.name, {24 * ui, 58 * ui}, sz(15),
             sf::Color(255, 255, 255, 210), 0, 2 * ui, kInk);

    const float cy = 32 * ui, r = 9 * ui, gap = 26 * ui;
    drawText("YOU", {W / 2 - 4.2f * gap, cy}, sz(20), white, 1, 3 * ui, kInk);
    drawText("CUBOT", {W / 2 + 4.2f * gap, cy}, sz(20), white, 0, 3 * ui, kInk);
    for (int side = 0; side < 2; ++side)
        for (int i = 0; i < WinningScore; ++i) {
            sf::CircleShape c(r);
            c.setOrigin({r, r});
            float x = side == Human ? W / 2 - gap * (3.2f - i) : W / 2 + gap * (1.2f + i);
            c.setPosition({x, cy});
            c.setOutlineThickness(2.5f * ui);
            c.setOutlineColor(kInk);
            sf::Color filled = toSf(side == Human ? th.shieldHuman : th.shieldCpu);
            c.setFillColor(scores_[side] > i ? filled : sf::Color(255, 255, 255, 90));
            window_.draw(c);
        }

    // --- active effect lists ---
    auto effects = [&](int side) {
        std::vector<std::string> list;
        const Paddle& pd = w.paddles[side];
        const PlayerState& pl = w.players[side];
        if (pd.fireball) list.push_back("FIREBALL READY");
        if (pd.resizeTimer > 0) list.push_back(fmt("BIG PADDLE %.0fs", std::ceil(pd.resizeTimer)));
        if (pd.shrinkTimer > 0) list.push_back(fmt("SHRUNK %.0fs", std::ceil(pd.shrinkTimer)));
        if (pd.laserTimer > 0) list.push_back(fmt("LASER %.0fs", std::ceil(pd.laserTimer)));
        if (pd.dizzyTimer > 0) list.push_back("DIZZY");
        if (pl.bulletproofTimer > 0) list.push_back(fmt("BULLETPROOF %.0fs", std::ceil(pl.bulletproofTimer)));
        if (pl.mirroredTimer > 0) list.push_back(fmt("MIRRORED %.0fs", std::ceil(pl.mirroredTimer)));
        if (pl.fogTimer > 0) list.push_back(fmt("FOG %.0fs", std::ceil(pl.fogTimer)));
        int shieldsUp = 0;
        for (int s : pl.shields) shieldsUp += s;
        list.push_back("SHIELDS " + std::to_string(shieldsUp) + "/" + std::to_string(pl.shields.size()));
        return list;
    };
    auto human = effects(Human), cpu = effects(Cpu);
    for (size_t i = 0; i < human.size(); ++i)
        drawText(human[i], {24 * ui, H - (24 + 26 * float(human.size() - 1 - i)) * ui}, sz(18), toSf(th.shieldHuman), 0, 3 * ui, kInk);
    for (size_t i = 0; i < cpu.size(); ++i)
        drawText(cpu[i], {W - 24 * ui, H - (24 + 26 * float(cpu.size() - 1 - i)) * ui}, sz(18), toSf(th.shieldCpu), 1, 3 * ui, kInk);

    std::vector<std::string> center;
    for (auto& p : w.pucks) {
        if (p.timebombTimer > 0) center.push_back(fmt("TIME BOMB %.1f", p.timebombTimer));
        if (p.ghostballTimer > 0) center.push_back("GHOST BALL");
    }
    if (w.deathballTimer > 0 && !w.roundOver) center.push_back("DEATH BALL - DON'T TOUCH IT!");
    for (size_t i = 0; i < center.size(); ++i)
        drawText(center[i], {W / 2, H - (28 + 28 * float(i)) * ui}, sz(20), sf::Color(255, 90, 60), 0.5f, 3 * ui, kInk);

    // --- big centre messages ---
    auto big = [&](const std::string& s, float y, float scale, sf::Color c) {
        drawText(s, {W / 2, H * y}, sz(64 * scale), c, 0.5f, 5 * ui * scale, kInk);
    };
    auto hint = [&](const std::string& s, float y) {
        float a = 0.6f + 0.4f * std::sin(stateTime_ * 4);
        drawText(s, {W / 2, H * y}, sz(24), sf::Color(255, 255, 255, std::uint8_t(255 * a)), 0.5f, 3 * ui, kInk);
    };
    switch (state_) {
    case State::Intro: {
        std::uint8_t a = std::uint8_t(255 * std::clamp(std::min(stateTime_ * 2, (2.6f - stateTime_) * 2), 0.f, 1.f));
        big("LEVEL " + std::to_string(levelIndex_ + 1), 0.4f, 1.2f, sf::Color(255, 255, 255, a));
        drawText("First to " + std::to_string(WinningScore) + " wins", {W / 2, H * 0.5f}, sz(24), sf::Color(255, 255, 255, a), 0.5f,
                 3 * ui, kInk);
        break;
    }
    case State::Countdown: {
        int n = std::max(1, 3 - int(stateTime_ / 0.7f));
        float t = std::fmod(stateTime_, 0.7f) / 0.7f;
        big(std::to_string(n), 0.42f, 1.8f - t * 0.6f, sf::Color(255, 255, 255, std::uint8_t(255 * (1 - t * 0.6f))));
        break;
    }
    case State::Playing:
        if (stateTime_ < 0.7f) big("SLAM!", 0.42f, 1.4f + stateTime_, sf::Color(255, 255, 255, std::uint8_t(255 * (1 - stateTime_ / 0.7f))));
        break;
    case State::RoundEnd:
        if (w.roundLoser == Cpu) big("YOU SCORE!", 0.42f, 1, toSf(th.shieldHuman));
        else big("CUBOT SCORES!", 0.42f, 1, toSf(th.shieldCpu));
        break;
    case State::LevelWon:
        big("LEVEL CLEAR!", 0.38f, 1.2f, white);
        hint(levelIndex_ + 1 < int(singlePlayerLevels().size()) ? "Click or press Enter for level " + std::to_string(levelIndex_ + 2)
                                                                 : "Click or press Enter",
             0.5f);
        break;
    case State::GameOver:
        big("CUBOT WINS", 0.38f, 1.2f, toSf(th.shieldCpu));
        hint("Enter: retry level    Esc: menu", 0.5f);
        break;
    case State::Victory:
        big("YOU BEAT CUBOT!", 0.36f, 1.2f, white);
        drawText("All " + std::to_string(singlePlayerLevels().size()) + " levels cleared", {W / 2, H * 0.46f}, sz(28), white, 0.5f, 3 * ui,
                 kInk);
        hint("Press Enter", 0.56f);
        break;
    default: break;
    }

    if (flash_ > 0) {
        sf::RectangleShape f({W, H});
        sf::Color c = flashColor_;
        c.a = std::uint8_t(160 * flash_);
        f.setFillColor(c);
        window_.draw(f);
    }
    if (paused_) {
        sf::RectangleShape shade({W, H});
        shade.setFillColor(sf::Color(10, 12, 20, 150));
        window_.draw(shade);
        big("PAUSED", 0.4f, 1, white);
        drawText("Enter: resume    Q: quit to menu    M: mute", {W / 2, H * 0.5f}, sz(22), white, 0.5f, 3 * ui, kInk);
    }
    if (audio_.muted()) drawText("MUTED", {W - 24 * ui, 30 * ui}, sz(16), white, 1, 2 * ui, kInk);
}

} // namespace game
