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

constexpr std::uint32_t kMagic = 0x43534C4D; // "CSLM"
constexpr std::uint16_t kProtocol = 1;

struct MenuEntry {
    const char* label;
    Game::Mode mode;
};
const MenuEntry kMenu[] = {
    {"1 PLAYER  vs CUBOT", Game::Mode::Solo},
    {"2 PLAYERS  same computer", Game::Mode::Local},
    {"HOST ONLINE GAME", Game::Mode::Host},
    {"JOIN ONLINE GAME", Game::Mode::Client},
};
constexpr int kMenuCount = int(std::size(kMenu));

sf::Color toSf(gfx::Color c) {
    auto b = [](float v) { return std::uint8_t(std::clamp(v, 0.f, 1.f) * 255); };
    return {b(c.r), b(c.g), b(c.b), b(c.a)};
}

std::string fmt(const char* f, float v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

sf::Packet message(Msg m) {
    sf::Packet p;
    p << std::uint8_t(m);
    return p;
}

/** Local versus uses the same arenas, minus fog: on a shared screen it would blind both players. */
const std::vector<Level>& localVersusLevels() {
    static const std::vector<Level> list = [] {
        std::vector<Level> l = singlePlayerLevels();
        for (auto& lv : l)
            lv.extras.erase(std::remove_if(lv.extras.begin(), lv.extras.end(), [](const ExtraDef& d) { return d.type == ExtraType::Fog; }),
                            lv.extras.end());
        return l;
    }();
    return list;
}

bool addressChar(char32_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == ':' || c == '-' ||
           c == '[' || c == ']' || c == '_';
}

/** Draws the client's own paddle where it predicts it, instead of where the (older) snapshot says. */
void placePredictedPaddle(World& w, float x) {
    Paddle& pd = w.paddles[Cpu];
    float hw = pd.width() / 2;
    pd.x = std::clamp(x + w.dizzyOffset(Cpu), hw, ArenaW - hw);
}

const sf::Color kInk{24, 28, 38};

} // namespace

HumanInput autoPilot(const World& w, float wobble, int side); // SelfTest.cpp

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
    if (opts_.startLevel >= 0) titleLevel_ = std::clamp(opts_.startLevel, 0, int(singlePlayerLevels().size()) - 1);
    if (opts_.host) {
        startHosting();
    } else if (!opts_.join.empty()) {
        joinAddress_ = opts_.join;
        startJoining();
        connectToHost();
    } else if (opts_.local) {
        setMode(Mode::Local);
        startLevel(titleLevel_);
    } else if (opts_.startLevel >= 0) {
        startLevel(titleLevel_);
    }
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
    net_.close();
}

// ---------------------------------------------------------------- helpers

bool Game::inPlay() const {
    return state_ == State::Intro || state_ == State::Countdown || state_ == State::Playing || state_ == State::RoundEnd;
}

void Game::updateCursor() { window_.setMouseCursorVisible(paused_ || !inPlay()); }

const std::vector<Level>& Game::levels() const { return mode_ == Mode::Local ? localVersusLevels() : singlePlayerLevels(); }

std::string Game::sideName(int side) const {
    switch (mode_) {
    case Mode::Solo: return side == Human ? "YOU" : "CUBOT";
    case Mode::Local: return side == Human ? "P1" : "P2";
    default: return side == localSide() ? "YOU" : "RIVAL";
    }
}

sf::Color Game::sideColor(int side) const {
    const Theme& th = scene_.theme();
    return toSf(side == Human ? th.shieldHuman : th.shieldCpu);
}

// ---------------------------------------------------------------- flow

void Game::setMode(Mode m) {
    mode_ = m;
    switch (m) {
    case Mode::Solo:
        scene_.setView(Scene::View::Solo, true);
        scene_.setSideTags("!", " (CUBOT)");
        break;
    case Mode::Local:
        scene_.setView(Scene::View::Overhead, false);
        scene_.setSideTags(" (P1)", " (P2)");
        break;
    case Mode::Host:
        scene_.setView(Scene::View::Solo, false);
        scene_.setSideTags("!", " (RIVAL)");
        break;
    case Mode::Client:
        scene_.setView(Scene::View::Top, false);
        scene_.setSideTags(" (RIVAL)", "!");
        break;
    }
}

void Game::setState(State s) {
    state_ = s;
    stateTime_ = 0;
    lastCountdown_ = -1;
    updateCursor();
    scene_.setTitleMode(s == State::Title || s == State::Lobby);
}

void Game::startLevel(int index) {
    levelIndex_ = std::clamp(index, 0, int(levels().size()) - 1);
    scores_[0] = scores_[1] = 0;
    round_ = 1;
    serve_ = Human;
    paused_ = false;
    ++levelSerial_;
    set_ = makeLevelSet(levels()[levelIndex_].set, rng_);
    newRound();
    scene_.setupLevel(*world_, themes()[levelIndex_ % themes().size()]);
    scene_.robotReset();
    scene_.startIntro();
    input_ = input2_ = {};
    setState(State::Intro);
}

void Game::newRound() {
    const Level& level = levels()[levelIndex_];
    world_ = std::make_unique<World>(level, set_, round_, serve_, rng_());
    if (mode_ == Mode::Solo) ai_ = std::make_unique<AI>(level.ai, rng_());
    else ai_.reset();
    ++worldId_;
    remoteTarget_ = remoteX_ = predictedX_ = ArenaW / 2;
    scene_.rebuildObstacles(*world_);
    accumulator_ = 0;
}

void Game::quitToTitle(const std::string& notice) {
    net_.close();
    handshaken_ = helloSent_ = false;
    paused_ = false;
    setMode(Mode::Solo);
    setState(State::Title);
    if (!notice.empty()) {
        notice_ = notice;
        noticeTime_ = 6;
    }
}

void Game::togglePause() {
    if (mode_ == Mode::Client) {
        net_.send(message(Msg::PauseToggle)); // the host decides; the flow message brings the result back
        return;
    }
    paused_ = !paused_;
    updateCursor();
}

void Game::confirm() {
    if (paused_) {
        togglePause();
        return;
    }
    switch (state_) {
    case State::Title: {
        Mode m = kMenu[menuItem_].mode;
        if (m == Mode::Host) startHosting();
        else if (m == Mode::Client) startJoining();
        else {
            setMode(m);
            startLevel(titleLevel_);
        }
        break;
    }
    case State::Lobby:
        if (mode_ == Mode::Client && net_.status() != Net::Status::Connecting && !net_.connected()) connectToHost();
        break;
    case State::Intro:
        if (mode_ != Mode::Client) stateTime_ = 99;
        break;
    case State::LevelWon:
        if (levelIndex_ + 1 < int(levels().size())) startLevel(levelIndex_ + 1);
        else setState(State::Victory);
        break;
    case State::GameOver: startLevel(levelIndex_); break;
    case State::Victory: setState(State::Title); break;
    case State::MatchOver:
        if (mode_ != Mode::Client) startLevel((levelIndex_ + 1) % int(levels().size()));
        break;
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
    using K = sf::Keyboard::Key;

    // Typing the host address: letters are text here, not shortcuts.
    if (state_ == State::Lobby && mode_ == Mode::Client) {
        const bool editable = net_.status() != Net::Status::Connecting && !net_.connected();
        if (const auto* t = e.getIf<sf::Event::TextEntered>()) {
            if (editable && addressChar(t->unicode) && joinAddress_.size() < 64) joinAddress_ += char(t->unicode);
            return;
        }
        if (const auto* k = e.getIf<sf::Event::KeyPressed>()) {
            if (k->code == K::Escape) quitToTitle();
            else if (k->code == K::Enter) confirm();
            else if (editable && k->code == K::Backspace && !joinAddress_.empty()) joinAddress_.pop_back();
            else if (editable && k->code == K::V && k->control) {
                for (char32_t c : sf::Clipboard::getString())
                    if (addressChar(c) && joinAddress_.size() < 64) joinAddress_ += char(c);
            }
            return;
        }
    }

    if (const auto* k = e.getIf<sf::Event::KeyPressed>()) {
        switch (k->code) {
        case K::Escape:
            if (paused_ || inPlay()) togglePause();
            else if (state_ == State::Title) window_.close();
            else quitToTitle();
            break;
        case K::P:
            if (inPlay()) togglePause();
            break;
        case K::Q:
            if (paused_) quitToTitle();
            break;
        case K::M: audio_.toggleMute(); break;
        case K::Enter:
        case K::Space: confirm(); break;
        case K::Up:
        case K::W:
        case K::Down:
        case K::S:
            if (state_ == State::Title) {
                int d = (k->code == K::Up || k->code == K::W) ? -1 : 1;
                menuItem_ = (menuItem_ + d + kMenuCount) % kMenuCount;
                audio_.play(Sfx::Beep, 1.2f, 40);
            }
            break;
        case K::Left:
        case K::A:
        case K::Right:
        case K::D:
            if (state_ == State::Title) {
                if (kMenu[menuItem_].mode == Mode::Client) break; // the host picks the arena
                int n = int(singlePlayerLevels().size());
                int d = (k->code == K::Left || k->code == K::A) ? -1 : 1;
                titleLevel_ = (titleLevel_ + d + n) % n;
                audio_.play(Sfx::Beep, 1.2f, 40);
            } else if (mode_ != Mode::Local || k->code == K::A || k->code == K::D) {
                keyboardMode_ = true; // in local versus the arrows belong to player 2
            }
            break;
        default: break;
        }
    }
    if (const auto* m = e.getIf<sf::Event::MouseMoved>()) {
        if (std::abs(m->position.x - mouse_.x) + std::abs(m->position.y - mouse_.y) > 2) keyboardMode_ = false;
        mouse_ = m->position;
        if (state_ == State::Title)
            for (int i = 0; i < int(menuRects_.size()); ++i)
                if (menuRects_[i].contains(sf::Vector2f(mouse_)) && menuItem_ != i) {
                    menuItem_ = i;
                    audio_.play(Sfx::Beep, 1.2f, 25);
                }
    }
    if (const auto* b = e.getIf<sf::Event::MouseButtonPressed>()) {
        if (b->button == sf::Mouse::Button::Left && (!inPlay() || paused_)) confirm();
    }
}

void Game::updateMouseTarget() {
    using K = sf::Keyboard::Key;
    if (opts_.demo && world_) {
        if (int(runTime_ * 60) % 90 == 0) demoWobble_ = std::sin(runTime_ * 7.3f) * 180;
        input_ = autoPilot(*world_, demoWobble_, localSide());
        if (mode_ == Mode::Local) input2_ = autoPilot(*world_, -demoWobble_, Cpu);
        return;
    }
    if (mode_ == Mode::Local) {
        // Player 2 sits at the far end but sees the same screen, so screen-left is arena-left.
        float axis = 0;
        if (sf::Keyboard::isKeyPressed(K::Left)) axis -= 1;
        if (sf::Keyboard::isKeyPressed(K::Right)) axis += 1;
        input2_.useAxis = true;
        input2_.axis = axis;
    }
    if (keyboardMode_) {
        const bool arrows = mode_ != Mode::Local;
        float axis = 0;
        if (sf::Keyboard::isKeyPressed(K::A) || (arrows && sf::Keyboard::isKeyPressed(K::Left))) axis -= 1;
        if (sf::Keyboard::isKeyPressed(K::D) || (arrows && sf::Keyboard::isKeyPressed(K::Right))) axis += 1;
        if (mode_ == Mode::Client) axis = -axis; // the client looks down the arena from the other end
        input_.useAxis = true;
        input_.axis = axis;
        return;
    }
    // Map the cursor onto the local paddle's line in 3D so the paddle sits right under it.
    // (From the far end the arena appears mirrored; the projection takes care of that.)
    auto size = window_.getSize();
    gfx::Camera cam = scene_.referenceCamera();
    gfx::Mat4 vp = cam.projection(float(size.x) / float(std::max(1u, size.y))) * cam.view();
    float z = (ArenaH / 2 - Unit) * S * (localSide() == Human ? 1.f : -1.f);
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
    noticeTime_ = std::max(0.f, noticeTime_ - dt);
    if (online()) netUpdate();

    if (!paused_) {
        stateTime_ += dt;
        flash_ = std::max(0.f, flash_ - dt * 2.5f);
        const bool host = mode_ != Mode::Client; // the online client follows the host's flow

        switch (state_) {
        case State::Title:
        case State::Lobby: break;
        case State::LevelWon:
        case State::GameOver:
        case State::MatchOver:
            if (opts_.demo && host && stateTime_ > 3) confirm();
            break;
        case State::Intro:
            if (host && stateTime_ > 2.6f) setState(State::Countdown);
            break;
        case State::Countdown: {
            updateMouseTarget();
            int n = 3 - int(stateTime_ / 0.7f);
            if (n != lastCountdown_ && n > 0) {
                audio_.play(Sfx::Beep);
                lastCountdown_ = n;
            }
            if (host && stateTime_ >= 2.1f) {
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
            if (!host || stateTime_ <= 2.3f) break;
            if (mode_ == Mode::Solo && scores_[Human] >= WinningScore) {
                scene_.robotExplode();
                audio_.play(Sfx::Explode);
                audio_.play(Sfx::LevelWin);
                setState(State::LevelWon);
            } else if (mode_ == Mode::Solo && scores_[Cpu] >= WinningScore) {
                audio_.play(Sfx::GameOver);
                scene_.robotHappy();
                setState(State::GameOver);
            } else if (scores_[Human] >= WinningScore || scores_[Cpu] >= WinningScore) {
                matchOverEffects();
                setState(State::MatchOver);
            } else {
                newRound();
                setState(State::Countdown);
            }
            break;
        default: break;
        }

        const bool showWorld = state_ != State::Title && state_ != State::Lobby;
        scene_.update(dt, showWorld ? world_.get() : nullptr);
    }

    // The host shares every change of the game flow (after this frame's snapshots, so they stay in order).
    if (mode_ == Mode::Host && handshaken_) {
        Flow f = currentFlow();
        if (f != flow_) {
            sf::Packet p = message(Msg::Flow);
            p << f;
            net_.send(std::move(p));
            flow_ = f;
        }
    }
}

void Game::fixedStep() {
    switch (mode_) {
    case Mode::Solo:
        ai_->update(*world_);
        world_->step(input_);
        break;
    case Mode::Local: world_->step(input_, input2_); break;
    case Mode::Host: {
        // The client paddle moves no faster than a local one, even when its updates arrive in bursts.
        remoteX_ += std::clamp(remoteTarget_ - remoteX_, -PaddleMaxStep, PaddleMaxStep);
        world_->setCpuPaddleX(remoteX_);
        world_->step(input_);
        sf::Packet p = message(Msg::Snapshot);
        p << worldId_;
        writeSnapshot(p, *world_);
        net_.send(std::move(p));
        break;
    }
    case Mode::Client: clientStep(); return;
    }
    scene_.handleEvents(*world_);
    playEventSounds();

    if (world_->roundOver) {
        int loser = world_->roundLoser;
        ++scores_[other(loser)];
        serve_ = loser;
        ++round_;
        roundOverEffects(loser);
        setState(State::RoundEnd);
    }
}

void Game::roundOverEffects(int loser) {
    if (loser != Human && loser != Cpu) return;
    const int winner = other(loser);
    const bool good = mode_ == Mode::Local || winner == localSide();
    if (mode_ == Mode::Solo) {
        if (winner == Human) scene_.robotHurt();
        else scene_.robotHappy();
    } else {
        scene_.shake(0.8f);
    }
    audio_.play(good ? Sfx::Score : Sfx::Lose);
    if (mode_ == Mode::Local) flashColor_ = sideColor(winner);
    else flashColor_ = good ? sf::Color::White : sf::Color(255, 60, 60);
    flash_ = 1;
}

void Game::matchOverEffects() {
    const int winner = scores_[Human] >= WinningScore ? Human : Cpu;
    audio_.play(mode_ == Mode::Local || winner == localSide() ? Sfx::LevelWin : Sfx::GameOver);
}

void Game::playEventSounds() {
    std::set<Sfx> played;
    auto play = [&](Sfx s, float pitch = 1, float vol = 70) {
        if (played.insert(s).second) audio_.play(s, pitch, vol);
    };
    for (auto& e : world_->events) {
        switch (e.type) {
        case EventType::PaddleHit: play(e.side == localSide() ? Sfx::PaddleHit : Sfx::CpuHit); break;
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

// ---------------------------------------------------------------- online

void Game::startHosting() {
    net_.close();
    setMode(Mode::Host);
    handshaken_ = helloSent_ = false;
    flow_ = {};
    if (!net_.host(opts_.port)) {
        quitToTitle(net_.error());
        return;
    }
    auto ip = sf::IpAddress::getLocalAddress();
    localAddress_ = ip ? ip->toString() : "unknown";
    setState(State::Lobby);
}

void Game::startJoining() {
    net_.close();
    setMode(Mode::Client);
    handshaken_ = helloSent_ = false;
    flow_ = {};
    world_.reset();
    notice_.clear();
    setState(State::Lobby);
}

void Game::connectToHost() {
    std::string host;
    unsigned short port = opts_.port;
    if (!parseAddress(joinAddress_, host, port)) {
        notice_ = "That doesn't look like an address";
        noticeTime_ = 6;
        return;
    }
    notice_.clear();
    helloSent_ = false;
    net_.connect(host, port);
}

void Game::netUpdate() {
    net_.poll();
    if (net_.status() == Net::Status::Failed) {
        std::string why = net_.error();
        net_.close();
        if (mode_ == Mode::Client && !handshaken_ && state_ == State::Lobby) {
            notice_ = why; // stay in the lobby so the address can be fixed
            noticeTime_ = 6;
        } else {
            quitToTitle(why);
        }
        return;
    }
    if (!net_.connected()) return;

    if (mode_ == Mode::Client && !helloSent_) {
        sf::Packet p = message(Msg::Hello);
        p << kMagic << kProtocol;
        net_.send(std::move(p));
        helloSent_ = true;
    }
    while (online()) {
        std::optional<sf::Packet> p = net_.receive();
        if (!p) break;
        if (mode_ == Mode::Host) handleHostMessage(*p);
        else handleClientMessage(*p);
    }
}

void Game::handleHostMessage(sf::Packet& p) {
    std::uint8_t type = 0;
    p >> type;
    switch (Msg(type)) {
    case Msg::Hello: {
        std::uint32_t magic = 0;
        std::uint16_t version = 0;
        p >> magic >> version;
        if (!p || magic != kMagic || version != kProtocol) {
            quitToTitle("The other player runs a different version of the game");
            return;
        }
        sf::Packet w = message(Msg::Welcome);
        w << kMagic << kProtocol;
        net_.send(std::move(w));
        handshaken_ = true;
        flow_ = {};
        startLevel(titleLevel_);
        break;
    }
    case Msg::PaddleX: {
        std::uint32_t id = 0;
        float x = 0;
        p >> id >> x;
        if (p && id == worldId_ && std::isfinite(x)) remoteTarget_ = std::clamp(x, 0.f, ArenaW);
        break;
    }
    case Msg::PauseToggle:
        if (handshaken_ && inPlay()) togglePause();
        break;
    default: break;
    }
}

void Game::handleClientMessage(sf::Packet& p) {
    std::uint8_t type = 0;
    p >> type;
    switch (Msg(type)) {
    case Msg::Welcome: {
        std::uint32_t magic = 0;
        std::uint16_t version = 0;
        p >> magic >> version;
        if (!p || magic != kMagic || version != kProtocol) {
            quitToTitle("The host runs a different version of the game");
            return;
        }
        handshaken_ = true;
        flow_ = {};
        break;
    }
    case Msg::Flow: {
        Flow f;
        p >> f;
        if (!p || !handshaken_) {
            quitToTitle("Garbled data from the host");
            return;
        }
        applyFlow(f);
        break;
    }
    case Msg::Snapshot: {
        std::uint32_t id = 0;
        p >> id;
        if (!world_ || id != flow_.worldId) break; // left over from the previous round
        if (!readSnapshot(p, *world_)) {
            quitToTitle("Out of sync with the host");
            return;
        }
        placePredictedPaddle(*world_, predictedX_);
        scene_.handleEvents(*world_);
        playEventSounds();
        break;
    }
    default: break;
    }
}

Flow Game::currentFlow() const {
    Flow f;
    f.state = std::uint8_t(state_);
    f.paused = paused_;
    f.levelIndex = levelIndex_;
    f.levelSerial = levelSerial_;
    f.worldId = worldId_;
    f.setName = set_.name;
    f.scores[0] = scores_[0];
    f.scores[1] = scores_[1];
    f.round = round_;
    f.serve = serve_;
    f.roundLoser = world_ ? world_->roundLoser : -1;
    return f;
}

void Game::applyFlow(const Flow& f) {
    const auto& lv = levels();
    const bool newLevel = !world_ || f.levelSerial != flow_.levelSerial;
    if (newLevel) {
        levelIndex_ = std::clamp(int(f.levelIndex), 0, int(lv.size()) - 1);
        set_ = makeLevelSet(f.setName, rng_); // the host sends a resolved name, so this is deterministic
    }
    if (newLevel || f.worldId != flow_.worldId) {
        // Same level + layout + round + serve as the host: identical starting world. Snapshots take over from there.
        world_ = std::make_unique<World>(lv[levelIndex_], set_, f.round, f.serve == Cpu ? Cpu : Human, 0);
        predictedX_ = ArenaW / 2;
        accumulator_ = 0;
        scene_.rebuildObstacles(*world_);
    }
    if (newLevel) {
        scene_.setupLevel(*world_, themes()[levelIndex_ % themes().size()]);
        scene_.startIntro();
        input_ = {};
    }
    scores_[0] = f.scores[0];
    scores_[1] = f.scores[1];
    round_ = f.round;
    serve_ = f.serve;

    State s = f.state <= std::uint8_t(State::MatchOver) ? State(f.state) : state_;
    if (s == State::Title || s == State::Lobby) s = state_;
    if (s != state_ || newLevel) {
        if (s == State::Playing && state_ == State::Countdown) audio_.play(Sfx::Go);
        if (s == State::RoundEnd) roundOverEffects(f.roundLoser);
        if (s == State::MatchOver) matchOverEffects();
        setState(s);
    }
    paused_ = f.paused;
    updateCursor();
    flow_ = f;
}

void Game::clientStep() {
    if (!world_) return;
    const float width = world_->paddles[Cpu].width();
    predictedX_ = World::movePaddleX(predictedX_, input_, world_->players[Cpu].mirroredTimer > 0, width);
    sf::Packet p = message(Msg::PaddleX);
    p << flow_.worldId << predictedX_;
    net_.send(std::move(p));
    placePredictedPaddle(*world_, predictedX_);
}

// ---------------------------------------------------------------- drawing

void Game::draw() {
    auto size = window_.getSize();
    const World* w = state_ == State::Title || state_ == State::Lobby ? nullptr : world_.get();
    scene_.render(renderer_, w, int(size.x), int(size.y));
    window_.resetGLStates();
    drawHud();
    window_.display();
}

sf::FloatRect Game::drawText(const std::string& s, sf::Vector2f pos, unsigned size, sf::Color color, float alignX, float outline,
                             sf::Color outlineColor) {
    if (!font_) return {};
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
    return text.getGlobalBounds();
}

void Game::drawTitle(float W, float H, float ui) {
    auto sz = [&](float s) { return unsigned(std::max(8.f, s * ui)); };
    const sf::Color white(255, 255, 255), soft(255, 255, 255, 215), dim(196, 204, 218);
    const float pulse = 0.6f + 0.4f * std::sin(stateTime_ * 3);

    drawText("GREATEST", {W / 2, H * 0.12f}, sz(34), white, 0.5f, 3 * ui, kInk);
    drawText("CUBE SLAM", {W / 2, H * 0.215f}, sz(96), white, 0.5f, 6 * ui, kInk);
    drawText("a 3D arcade remake built with SFML", {W / 2, H * 0.3f}, sz(20), soft, 0.5f, 2 * ui, kInk);
    if (noticeTime_ > 0 && !notice_.empty())
        drawText(notice_, {W / 2, H * 0.355f}, sz(22), sf::Color(255, 90, 70, std::uint8_t(255 * std::min(1.f, noticeTime_))), 0.5f,
                 3 * ui, kInk);

    menuRects_.clear();
    for (int i = 0; i < kMenuCount; ++i) {
        const bool sel = i == menuItem_;
        std::string label = sel ? std::string("> ") + kMenu[i].label + " <" : kMenu[i].label;
        sf::FloatRect r = drawText(label, {W / 2, H * (0.42f + 0.06f * i)}, sz(sel ? 32 : 27), sel ? white : dim, 0.5f, 3 * ui, kInk);
        // Generous hover area so the mouse doesn't have to hit the glyphs.
        r.position.x = std::min(r.position.x, W / 2 - 200 * ui);
        r.size.x = std::max(r.size.x, 400 * ui);
        r.position.y = H * (0.42f + 0.06f * i) - H * 0.03f;
        r.size.y = H * 0.06f;
        menuRects_.push_back(r);
    }

    const Mode mode = kMenu[menuItem_].mode;
    if (mode == Mode::Client) {
        drawText("The host picks the arena", {W / 2, H * 0.7f}, sz(24), soft, 0.5f, 3 * ui, kInk);
    } else {
        const Level& lvl = singlePlayerLevels()[titleLevel_];
        drawText(std::string(mode == Mode::Solo ? "<   LEVEL " : "<   ARENA ") + std::to_string(titleLevel_ + 1) + "   >",
                 {W / 2, H * 0.7f}, sz(34), white, 0.5f, 4 * ui, kInk);
        drawText(std::string(themes()[titleLevel_ % themes().size()].name) + "  -  " + std::to_string(lvl.shields) +
                     (lvl.shields == 1 ? " shield" : " shields"),
                 {W / 2, H * 0.755f}, sz(19), soft, 0.5f, 2 * ui, kInk);
    }
    drawText("CLICK or press ENTER", {W / 2, H * 0.835f}, sz(26), sf::Color(255, 255, 255, std::uint8_t(255 * pulse)), 0.5f, 3 * ui, kInk);

    std::string help;
    switch (mode) {
    case Mode::Solo: help = "Mouse or A/D/Arrows: move    Up/Down: mode    Left/Right: level"; break;
    case Mode::Local: help = "P1 (near): mouse or A/D    P2 (far): Left/Right arrows"; break;
    case Mode::Host: help = "Hosts on TCP port " + std::to_string(opts_.port) + "    Left/Right: arena"; break;
    case Mode::Client: help = "Connect to a friend who is hosting"; break;
    }
    help += "    Esc/P: pause    M: mute";
    if (audio_.muted()) help += " (muted)";
    drawText(help, {W / 2, H * 0.93f}, sz(17), white, 0.5f, 2 * ui, kInk);
}

void Game::drawLobby(float W, float H, float ui) {
    auto sz = [&](float s) { return unsigned(std::max(8.f, s * ui)); };
    const sf::Color white(255, 255, 255), soft(255, 255, 255, 215);
    const float pulse = 0.6f + 0.4f * std::sin(stateTime_ * 3);

    if (mode_ == Mode::Host) {
        drawText("HOSTING", {W / 2, H * 0.22f}, sz(72), white, 0.5f, 5 * ui, kInk);
        drawText(net_.connected() ? "Player connected..." : "Waiting for a player to join...", {W / 2, H * 0.36f}, sz(30),
                 sf::Color(255, 255, 255, std::uint8_t(255 * pulse)), 0.5f, 3 * ui, kInk);
        drawText("Your address:  " + localAddress_ + "   (port " + std::to_string(net_.port()) + ")", {W / 2, H * 0.47f}, sz(28), white,
                 0.5f, 3 * ui, kInk);
        drawText("Same network: share the address above.  Over the internet: forward TCP port " + std::to_string(net_.port()) +
                     " to this computer and share your public IP.",
                 {W / 2, H * 0.54f}, sz(17), soft, 0.5f, 2 * ui, kInk);
        drawText("ARENA " + std::to_string(titleLevel_ + 1) + "  -  " + themes()[titleLevel_ % themes().size()].name, {W / 2, H * 0.65f},
                 sz(24), white, 0.5f, 3 * ui, kInk);
        drawText("Esc: cancel", {W / 2, H * 0.9f}, sz(20), white, 0.5f, 2 * ui, kInk);
        return;
    }

    const bool connecting = net_.status() == Net::Status::Connecting;
    drawText("JOIN GAME", {W / 2, H * 0.22f}, sz(72), white, 0.5f, 5 * ui, kInk);
    drawText("Host address (IP or name, optionally :port)", {W / 2, H * 0.36f}, sz(22), soft, 0.5f, 2 * ui, kInk);

    const float boxW = 560 * ui, boxH = 64 * ui;
    sf::RectangleShape box({boxW, boxH});
    box.setOrigin({boxW / 2, boxH / 2});
    box.setPosition({W / 2, H * 0.45f});
    box.setFillColor(sf::Color(24, 28, 38, 190));
    box.setOutlineThickness(3 * ui);
    box.setOutlineColor(connecting ? sf::Color(255, 255, 255, 120) : sf::Color::White);
    window_.draw(box);
    const bool caret = !connecting && !net_.connected() && std::fmod(stateTime_, 1.f) < 0.55f;
    drawText(joinAddress_ + (caret ? "_" : " "), {W / 2, H * 0.45f}, sz(32), white, 0.5f);

    if (net_.connected())
        drawText("Connected! Waiting for the host...", {W / 2, H * 0.56f}, sz(26), white, 0.5f, 3 * ui, kInk);
    else if (connecting)
        drawText("Connecting...", {W / 2, H * 0.56f}, sz(26), sf::Color(255, 255, 255, std::uint8_t(255 * pulse)), 0.5f, 3 * ui, kInk);
    else if (noticeTime_ > 0 && !notice_.empty())
        drawText(notice_, {W / 2, H * 0.56f}, sz(24), sf::Color(255, 90, 70), 0.5f, 3 * ui, kInk);
    drawText("Enter: connect    Ctrl+V: paste    Esc: back", {W / 2, H * 0.9f}, sz(20), white, 0.5f, 2 * ui, kInk);
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
        drawTitle(W, H, ui);
        return;
    }
    if (state_ == State::Lobby) {
        drawLobby(W, H, ui);
        return;
    }

    if (!world_) return;
    const World& w = *world_;
    const Theme& th = scene_.theme();
    const bool solo = mode_ == Mode::Solo;
    const int me = localSide(), them = other(me);

    // --- top bar: level + score pips ---
    std::string where = w.set.name == "empty" ? std::string(th.name) : std::string(th.name) + " - " + w.set.name;
    if (solo) {
        drawText("LEVEL " + std::to_string(levelIndex_ + 1) + "/" + std::to_string(levels().size()), {24 * ui, 30 * ui}, sz(24), white, 0,
                 3 * ui, kInk);
    } else {
        drawText("ARENA " + std::to_string(levelIndex_ + 1), {24 * ui, 30 * ui}, sz(24), white, 0, 3 * ui, kInk);
        where += mode_ == Mode::Local ? "  |  LOCAL VERSUS" : "  |  ONLINE";
    }
    drawText(where, {24 * ui, 58 * ui}, sz(15), sf::Color(255, 255, 255, 210), 0, 2 * ui, kInk);

    const float cy = 32 * ui, r = 9 * ui, gap = 26 * ui;
    drawText(sideName(me), {W / 2 - 4.2f * gap, cy}, sz(20), white, 1, 3 * ui, kInk);
    drawText(sideName(them), {W / 2 + 4.2f * gap, cy}, sz(20), white, 0, 3 * ui, kInk);
    for (int side = 0; side < 2; ++side)
        for (int i = 0; i < WinningScore; ++i) {
            sf::CircleShape c(r);
            c.setOrigin({r, r});
            float x = side == me ? W / 2 - gap * (3.2f - i) : W / 2 + gap * (1.2f + i);
            c.setPosition({x, cy});
            c.setOutlineThickness(2.5f * ui);
            c.setOutlineColor(kInk);
            c.setFillColor(scores_[side] > i ? sideColor(side) : sf::Color(255, 255, 255, 90));
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
        if (!solo) list.back() = sideName(side) + "  " + list.back();
        return list;
    };
    auto mine = effects(me), theirs = effects(them);
    for (size_t i = 0; i < mine.size(); ++i)
        drawText(mine[i], {24 * ui, H - (24 + 26 * float(mine.size() - 1 - i)) * ui}, sz(18), sideColor(me), 0, 3 * ui, kInk);
    for (size_t i = 0; i < theirs.size(); ++i)
        drawText(theirs[i], {W - 24 * ui, H - (24 + 26 * float(theirs.size() - 1 - i)) * ui}, sz(18), sideColor(them), 1, 3 * ui, kInk);

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
    // "YOU SCORE!", "CUBOT SCORES!", "PLAYER 2 SCORES!", "RIVAL SCORES!"
    auto longName = [&](int side) {
        if (mode_ == Mode::Local) return std::string(side == Human ? "PLAYER 1" : "PLAYER 2");
        return sideName(side);
    };
    auto verb = [&](int side, const char* you, const char* others) {
        return longName(side) + (mode_ != Mode::Local && side == me ? you : others);
    };
    switch (state_) {
    case State::Intro: {
        std::uint8_t a = std::uint8_t(255 * std::clamp(std::min(stateTime_ * 2, (2.6f - stateTime_) * 2), 0.f, 1.f));
        big((solo ? "LEVEL " : "ARENA ") + std::to_string(levelIndex_ + 1), 0.4f, 1.2f, sf::Color(255, 255, 255, a));
        drawText("First to " + std::to_string(WinningScore) + " wins", {W / 2, H * 0.5f}, sz(24), sf::Color(255, 255, 255, a), 0.5f,
                 3 * ui, kInk);
        if (mode_ == Mode::Local)
            drawText("P1 (near): mouse or A/D      P2 (far): Left/Right arrows", {W / 2, H * 0.56f}, sz(22), sf::Color(255, 255, 255, a),
                     0.5f, 3 * ui, kInk);
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
        if (w.roundLoser == Human || w.roundLoser == Cpu) {
            int winner = other(w.roundLoser);
            big(verb(winner, " SCORE!", " SCORES!"), 0.42f, 1, sideColor(winner));
        }
        break;
    case State::LevelWon:
        big("LEVEL CLEAR!", 0.38f, 1.2f, white);
        hint(levelIndex_ + 1 < int(levels().size()) ? "Click or press Enter for level " + std::to_string(levelIndex_ + 2)
                                                     : "Click or press Enter",
             0.5f);
        break;
    case State::GameOver:
        big("CUBOT WINS", 0.38f, 1.2f, sideColor(Cpu));
        hint("Enter: retry level    Esc: menu", 0.5f);
        break;
    case State::Victory:
        big("YOU BEAT CUBOT!", 0.36f, 1.2f, white);
        drawText("All " + std::to_string(levels().size()) + " levels cleared", {W / 2, H * 0.46f}, sz(28), white, 0.5f, 3 * ui, kInk);
        hint("Press Enter", 0.56f);
        break;
    case State::MatchOver: {
        int winner = scores_[Human] >= WinningScore ? Human : Cpu;
        big(verb(winner, " WIN!", " WINS!"), 0.38f, 1.2f, sideColor(winner));
        drawText(std::to_string(scores_[me]) + " - " + std::to_string(scores_[them]), {W / 2, H * 0.47f}, sz(34), white, 0.5f, 3 * ui,
                 kInk);
        hint(mode_ == Mode::Client ? "Waiting for the host...    Esc: leave" : "Enter: next arena    Esc: menu", 0.56f);
        break;
    }
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
        if (online()) drawText("(paused for both players)", {W / 2, H * 0.55f}, sz(18), white, 0.5f, 2 * ui, kInk);
    }
    if (audio_.muted()) drawText("MUTED", {W - 24 * ui, 30 * ui}, sz(16), white, 1, 2 * ui, kInk);
}

} // namespace game
