#pragma once
#include "game/AI.hpp"
#include "game/Audio.hpp"
#include "game/Net.hpp"
#include "game/Scene.hpp"
#include "game/World.hpp"
#include "gfx/Renderer.hpp"

#include <SFML/Graphics.hpp>

#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace game {

class Game {
public:
    enum class Mode { Solo, Local, Host, Client };

    struct Options {
        int startLevel = -1;          // skip the title screen
        bool demo = false;            // autopilot drives the player's paddle
        std::string screenshotPath;   // save a PNG and quit...
        float screenshotAt = 5;       // ...after this many seconds
        bool local = false;           // start a local two-player match
        bool host = false;            // start hosting an online match
        std::string join;             // join an online match at this address
        unsigned short port = DefaultPort;
    };

    Game(sf::RenderWindow& window, Options opts) : window_(window), opts_(std::move(opts)) {}
    bool init();
    void run();

private:
    enum class State { Title, Lobby, Intro, Countdown, Playing, RoundEnd, LevelWon, GameOver, Victory, MatchOver };

    // --- flow ---
    void setMode(Mode m);
    void startLevel(int index);
    void newRound();
    void setState(State s);
    void handleEvent(const sf::Event& e);
    void update(float dt);
    void fixedStep();
    void roundOverEffects(int loser);
    void matchOverEffects();
    void playEventSounds();
    void updateMouseTarget();
    void confirm(); // Enter / click
    void togglePause();
    void quitToTitle(const std::string& notice = {});

    // --- online ---
    void startHosting();
    void startJoining();
    void connectToHost();
    void netUpdate();
    void handleHostMessage(sf::Packet& p);
    void handleClientMessage(sf::Packet& p);
    void applyFlow(const Flow& f);
    Flow currentFlow() const;
    void clientStep();
    bool online() const { return mode_ == Mode::Host || mode_ == Mode::Client; }

    // --- helpers ---
    bool inPlay() const;
    void updateCursor();
    const std::vector<Level>& levels() const;
    int localSide() const { return mode_ == Mode::Client ? Cpu : Human; }
    std::string sideName(int side) const;
    sf::Color sideColor(int side) const;

    // --- drawing ---
    void draw();
    void drawHud();
    void drawTitle(float W, float H, float ui);
    void drawLobby(float W, float H, float ui);
    sf::FloatRect drawText(const std::string& s, sf::Vector2f pos, unsigned size, sf::Color color, float alignX = 0.5f,
                           float outline = 0, sf::Color outlineColor = sf::Color::Black);

    sf::RenderWindow& window_;
    Options opts_;
    float runTime_ = 0;
    float demoWobble_ = 0;
    gfx::Renderer renderer_;
    Scene scene_;
    Audio audio_;
    std::optional<sf::Font> font_;

    Mode mode_ = Mode::Solo;
    State state_ = State::Title;
    float stateTime_ = 0;
    bool paused_ = false;

    // title menu
    int menuItem_ = 0;
    std::vector<sf::FloatRect> menuRects_;
    std::string notice_;
    float noticeTime_ = 0;

    int levelIndex_ = 0;
    int titleLevel_ = 0;
    int scores_[2] = {0, 0};
    int round_ = 1;
    int serve_ = Human;
    LevelSet set_;
    std::unique_ptr<World> world_;
    std::unique_ptr<AI> ai_;
    std::mt19937 rng_{std::random_device{}()};

    HumanInput input_;  // the local player (player 1 in local versus)
    HumanInput input2_; // player 2 in local versus
    bool keyboardMode_ = false;
    sf::Vector2i mouse_{0, 0};
    float accumulator_ = 0;
    int lastCountdown_ = -1;
    float flash_ = 0;
    sf::Color flashColor_ = sf::Color::White;

    // online
    Net net_;
    bool handshaken_ = false, helloSent_ = false;
    std::string joinAddress_ = "127.0.0.1";
    std::string localAddress_;
    std::uint32_t levelSerial_ = 0, worldId_ = 0; // host counters
    Flow flow_;                                   // host: last sent, client: last received
    float remoteTarget_ = cfg::ArenaW / 2;        // host: where the client wants its paddle
    float remoteX_ = cfg::ArenaW / 2;             // host: client paddle, rate-limited like a local one
    float predictedX_ = cfg::ArenaW / 2;          // client: own paddle, simulated locally for zero input lag
};

} // namespace game
