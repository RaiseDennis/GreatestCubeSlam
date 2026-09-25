#pragma once
#include "game/AI.hpp"
#include "game/Audio.hpp"
#include "game/Scene.hpp"
#include "game/World.hpp"
#include "gfx/Renderer.hpp"

#include <SFML/Graphics.hpp>

#include <memory>
#include <optional>
#include <random>
#include <string>

namespace game {

class Game {
public:
    struct Options {
        int startLevel = -1;          // skip the title screen
        bool demo = false;            // autopilot drives the player's paddle
        std::string screenshotPath;   // save a PNG and quit...
        float screenshotAt = 5;       // ...after this many seconds
    };

    Game(sf::RenderWindow& window, Options opts) : window_(window), opts_(std::move(opts)) {}
    bool init();
    void run();

private:
    enum class State { Title, Intro, Countdown, Playing, RoundEnd, LevelWon, GameOver, Victory };

    void startLevel(int index);
    void newRound();
    void setState(State s);
    void handleEvent(const sf::Event& e);
    void update(float dt);
    void fixedStep();
    void playEventSounds();
    void updateMouseTarget();
    void draw();
    void drawHud();
    void drawText(const std::string& s, sf::Vector2f pos, unsigned size, sf::Color color, float alignX = 0.5f,
                  float outline = 0, sf::Color outlineColor = sf::Color::Black);
    void confirm(); // Enter / click

    sf::RenderWindow& window_;
    Options opts_;
    float runTime_ = 0;
    float demoWobble_ = 0;
    gfx::Renderer renderer_;
    Scene scene_;
    Audio audio_;
    std::optional<sf::Font> font_;

    State state_ = State::Title;
    float stateTime_ = 0;
    bool paused_ = false;

    int levelIndex_ = 0;
    int titleLevel_ = 0;
    int scores_[2] = {0, 0};
    int round_ = 1;
    int serve_ = Human;
    int lastRoundLoser_ = -1;
    LevelSet set_;
    std::unique_ptr<World> world_;
    std::unique_ptr<AI> ai_;
    std::mt19937 rng_{std::random_device{}()};

    HumanInput input_;
    bool keyboardMode_ = false;
    sf::Vector2i mouse_{0, 0};
    float accumulator_ = 0;
    int lastCountdown_ = -1;
    float flash_ = 0;
    sf::Color flashColor_ = sf::Color::White;
};

} // namespace game
