#include "game/Game.hpp"

#include <SFML/Graphics.hpp>

#include <cstdlib>
#include <cstring>
#include <string>

namespace game {
int runSelfTest();
}

int main(int argc, char** argv) {
    game::Game::Options opts;
    for (int i = 1; i < argc; ++i) {
        auto arg = [&](const char* name) { return std::strcmp(argv[i], name) == 0 && i + 1 < argc; };
        if (std::strcmp(argv[i], "--selftest") == 0) return game::runSelfTest();
        if (std::strcmp(argv[i], "--demo") == 0) opts.demo = true;
        else if (std::strcmp(argv[i], "--local") == 0) opts.local = true;
        else if (std::strcmp(argv[i], "--host") == 0) opts.host = true;
        else if (arg("--join")) opts.join = argv[++i];
        else if (arg("--port")) opts.port = static_cast<unsigned short>(std::atoi(argv[++i]));
        else if (arg("--level")) opts.startLevel = std::atoi(argv[++i]) - 1;
        else if (arg("--shot")) opts.screenshotPath = argv[++i];
        else if (arg("--shot-at")) opts.screenshotAt = float(std::atof(argv[++i]));
    }

    sf::ContextSettings settings;
    settings.depthBits = 24;
    settings.antiAliasingLevel = 4;

    sf::RenderWindow window(sf::VideoMode({1280, 800}), "Greatest Cube Slam", sf::Style::Default, sf::State::Windowed, settings);
    window.setVerticalSyncEnabled(true);
    if (!window.setActive(true)) return 1;

    game::Game game(window, opts);
    if (!game.init()) return 1;
    game.run();
    return 0;
}
