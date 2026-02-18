#include "gomoku/gui/board_renderer.hpp"
#include <string>

namespace gomoku {
namespace gui {

static char col_label(int col) {
    return (col < 8) ? static_cast<char>('A' + col) : static_cast<char>('A' + col + 1);
}

void BoardRenderer::init(const sf::Font& font) {
    font_ = &font;
}

void BoardRenderer::draw(sf::RenderWindow& window, const Board& board,
                          std::optional<Pos> last_move, std::optional<Pos> hover_pos,
                          Stone hover_color) const {
    draw_background(window);
    draw_grid(window);
    draw_hoshi(window);
    draw_coordinates(window);
    draw_stones(window, board, last_move);
    if (hover_pos.has_value()) {
        draw_hover(window, *hover_pos, hover_color);
    }
}

void BoardRenderer::draw_background(sf::RenderWindow& window) const {
    sf::RectangleShape bg(sf::Vector2f(BOARD_PANEL_WIDTH, WINDOW_HEIGHT));
    bg.setFillColor(BOARD_COLOR);
    window.draw(bg);
}

void BoardRenderer::draw_grid(sf::RenderWindow& window) const {
    for (int i = 0; i < 19; ++i) {
        // Vertical line (column i)
        sf::Vector2f top = pos_to_pixel(18, i);
        sf::Vector2f bot = pos_to_pixel(0, i);
        sf::Vertex vline[] = {
            sf::Vertex(top, GRID_COLOR),
            sf::Vertex(bot, GRID_COLOR)
        };
        window.draw(vline, 2, sf::Lines);

        // Horizontal line (row i)
        sf::Vector2f left = pos_to_pixel(i, 0);
        sf::Vector2f right = pos_to_pixel(i, 18);
        sf::Vertex hline[] = {
            sf::Vertex(left, GRID_COLOR),
            sf::Vertex(right, GRID_COLOR)
        };
        window.draw(hline, 2, sf::Lines);
    }
}

void BoardRenderer::draw_hoshi(sf::RenderWindow& window) const {
    for (auto [r, c] : HOSHI_POINTS) {
        sf::CircleShape dot(HOSHI_RADIUS);
        dot.setFillColor(GRID_COLOR);
        dot.setOrigin(HOSHI_RADIUS, HOSHI_RADIUS);
        dot.setPosition(pos_to_pixel(r, c));
        window.draw(dot);
    }
}

void BoardRenderer::draw_coordinates(sf::RenderWindow& window) const {
    sf::Text text;
    text.setFont(*font_);
    text.setCharacterSize(12);
    text.setFillColor(GRID_COLOR);

    // Column labels (A-T, skip I)
    for (int c = 0; c < 19; ++c) {
        std::string label(1, col_label(c));
        text.setString(label);
        sf::FloatRect bounds = text.getLocalBounds();
        float x = MARGIN_LEFT + c * CELL_SIZE - bounds.width / 2 - bounds.left;

        text.setPosition(x, MARGIN_TOP - 24);
        window.draw(text);
        text.setPosition(x, MARGIN_TOP + 18 * CELL_SIZE + 8);
        window.draw(text);
    }

    // Row labels (1-19)
    for (int r = 0; r < 19; ++r) {
        std::string label = std::to_string(r + 1);
        text.setString(label);
        sf::FloatRect bounds = text.getLocalBounds();
        float y = MARGIN_TOP + (18 - r) * CELL_SIZE - bounds.height / 2 - bounds.top - 2;

        text.setPosition(MARGIN_LEFT - 12 - bounds.width, y);
        window.draw(text);
        text.setPosition(MARGIN_LEFT + 18 * CELL_SIZE + 8, y);
        window.draw(text);
    }
}

void BoardRenderer::draw_stones(sf::RenderWindow& window, const Board& board,
                                 std::optional<Pos> last_move) const {
    for (int r = 0; r < 19; ++r) {
        for (int c = 0; c < 19; ++c) {
            Pos pos(static_cast<uint8_t>(r), static_cast<uint8_t>(c));
            Stone s = board.get(pos);
            if (s == Stone::Empty) continue;

            sf::Vector2f px = pos_to_pixel(r, c);

            sf::CircleShape stone(STONE_RADIUS);
            stone.setOrigin(STONE_RADIUS, STONE_RADIUS);
            stone.setPosition(px);

            if (s == Stone::Black) {
                stone.setFillColor(BLACK_STONE_COLOR);
                window.draw(stone);
                // 3D highlight
                sf::CircleShape hl(STONE_RADIUS * 0.35f);
                hl.setOrigin(STONE_RADIUS * 0.35f, STONE_RADIUS * 0.35f);
                hl.setPosition(px.x - STONE_RADIUS * 0.2f, px.y - STONE_RADIUS * 0.25f);
                hl.setFillColor(BLACK_HIGHLIGHT);
                window.draw(hl);
            } else {
                stone.setFillColor(WHITE_STONE_COLOR);
                stone.setOutlineColor(sf::Color(180, 180, 180));
                stone.setOutlineThickness(1.0f);
                window.draw(stone);
                sf::CircleShape hl(STONE_RADIUS * 0.3f);
                hl.setOrigin(STONE_RADIUS * 0.3f, STONE_RADIUS * 0.3f);
                hl.setPosition(px.x - STONE_RADIUS * 0.2f, px.y - STONE_RADIUS * 0.25f);
                hl.setFillColor(WHITE_HIGHLIGHT);
                window.draw(hl);
            }

            // Last move marker (red dot)
            if (last_move.has_value() && *last_move == pos) {
                sf::CircleShape marker(4.0f);
                marker.setOrigin(4.0f, 4.0f);
                marker.setPosition(px);
                marker.setFillColor(HIGHLIGHT_COLOR);
                window.draw(marker);
            }
        }
    }
}

void BoardRenderer::draw_hover(sf::RenderWindow& window, Pos pos, Stone color) const {
    sf::Vector2f px = pos_to_pixel(pos.row, pos.col);
    sf::CircleShape ghost(STONE_RADIUS);
    ghost.setOrigin(STONE_RADIUS, STONE_RADIUS);
    ghost.setPosition(px);
    ghost.setFillColor(color == Stone::Black ? HOVER_BLACK : HOVER_WHITE);
    window.draw(ghost);
}

} // namespace gui
} // namespace gomoku
