#include "gomoku/gui/game_controller.hpp"
#include <SFML/Graphics.hpp>
#include <iostream>

int main() {
    sf::Font font;

    // Try multiple paths for the font
    bool loaded = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
    if (!loaded)
        loaded = font.loadFromFile("../assets/fonts/DejaVuSans.ttf");
    if (!loaded)
        loaded = font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    if (!loaded) {
        std::cerr << "Error: Could not load font DejaVuSans.ttf\n"
                  << "Tried: assets/fonts/DejaVuSans.ttf, "
                     "../assets/fonts/DejaVuSans.ttf, "
                     "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf\n";
        return 1;
    }

    gomoku::gui::GameController controller(font);
    controller.run();
    return 0;
}
