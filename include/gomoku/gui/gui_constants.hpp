#pragma once

#include <SFML/Graphics.hpp>
#include <array>
#include <cmath>
#include <utility>

namespace gomoku {
namespace gui {

// Window dimensions
constexpr int CELL_SIZE = 36;
constexpr int MARGIN_LEFT = 46;
constexpr int MARGIN_TOP = 36;
constexpr int BOARD_PANEL_WIDTH = 720;
constexpr int SIDE_PANEL_WIDTH = 280;
constexpr int WINDOW_WIDTH = BOARD_PANEL_WIDTH + SIDE_PANEL_WIDTH;
constexpr int WINDOW_HEIGHT = 720;

// Stone rendering
constexpr float STONE_RADIUS = CELL_SIZE * 0.42f;
constexpr float HOSHI_RADIUS = 4.0f;

// Colors
inline const sf::Color BOARD_COLOR(222, 184, 135);
inline const sf::Color GRID_COLOR(80, 60, 30);
inline const sf::Color PANEL_BG(50, 50, 55);
inline const sf::Color TEXT_COLOR(230, 230, 230);
inline const sf::Color HIGHLIGHT_COLOR(220, 50, 50);
inline const sf::Color BLACK_STONE_COLOR(30, 30, 30);
inline const sf::Color BLACK_HIGHLIGHT(70, 70, 70);
inline const sf::Color WHITE_STONE_COLOR(240, 240, 240);
inline const sf::Color WHITE_HIGHLIGHT(255, 255, 255);
inline const sf::Color HOVER_BLACK(30, 30, 30, 100);
inline const sf::Color HOVER_WHITE(240, 240, 240, 100);
inline const sf::Color HINT_BLACK(50, 180, 50, 140);
inline const sf::Color HINT_WHITE(50, 220, 50, 140);
inline const sf::Color BTN_BG(80, 80, 90);
inline const sf::Color BTN_HOVER(100, 100, 115);
inline const sf::Color BTN_TEXT(230, 230, 230);
inline const sf::Color FORBIDDEN_COLOR(220, 40, 40, 120);

// Hoshi (star) points
constexpr std::array<std::pair<int,int>, 9> HOSHI_POINTS = {{
    {3, 3}, {3, 9}, {3, 15},
    {9, 3}, {9, 9}, {9, 15},
    {15, 3}, {15, 9}, {15, 15}
}};

// Board pos -> pixel center
inline sf::Vector2f pos_to_pixel(int row, int col) {
    return {
        static_cast<float>(MARGIN_LEFT + col * CELL_SIZE),
        static_cast<float>(MARGIN_TOP + (18 - row) * CELL_SIZE)
    };
}

// Pixel -> board pos; returns {-1,-1} if outside snap threshold
inline std::pair<int,int> pixel_to_pos(float mx, float my) {
    float col_f = (mx - MARGIN_LEFT) / static_cast<float>(CELL_SIZE);
    float row_f = 18.0f - (my - MARGIN_TOP) / static_cast<float>(CELL_SIZE);

    int col = static_cast<int>(std::round(col_f));
    int row = static_cast<int>(std::round(row_f));

    if (col < 0 || col >= 19 || row < 0 || row >= 19) return {-1, -1};

    if (std::abs(col_f - col) > 0.45f || std::abs(row_f - row) > 0.45f)
        return {-1, -1};

    return {row, col};
}

} // namespace gui
} // namespace gomoku
