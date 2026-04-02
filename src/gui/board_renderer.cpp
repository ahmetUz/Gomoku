#include "gomoku/gui/board_renderer.hpp"
#include <string>

namespace gomoku {
namespace gui {

static char col_label(int col) {
    return (col < 8) ? static_cast<char>('A' + col) : static_cast<char>('A' + col + 1);
}

void BoardRenderer::init(const sf::Font& font) {
    font_ = &font;

    // Background
    bg_.setSize(sf::Vector2f(BOARD_PANEL_WIDTH, WINDOW_HEIGHT));
    bg_.setFillColor(BOARD_COLOR);

    // Grid lines (19 vertical + 19 horizontal = 76 vertices)
    grid_lines_.setPrimitiveType(sf::Lines);
    grid_lines_.resize(76);
    for (int i = 0; i < 19; ++i) {
        // Vertical line (column i)
        sf::Vector2f top = pos_to_pixel(18, i);
        sf::Vector2f bot = pos_to_pixel(0, i);
        grid_lines_[i * 2]     = sf::Vertex(top, GRID_COLOR);
        grid_lines_[i * 2 + 1] = sf::Vertex(bot, GRID_COLOR);

        // Horizontal line (row i)
        sf::Vector2f left  = pos_to_pixel(i, 0);
        sf::Vector2f right = pos_to_pixel(i, 18);
        grid_lines_[38 + i * 2]     = sf::Vertex(left, GRID_COLOR);
        grid_lines_[38 + i * 2 + 1] = sf::Vertex(right, GRID_COLOR);
    }

    // Hoshi points
    hoshi_dots_.reserve(HOSHI_POINTS.size());
    for (auto [r, c] : HOSHI_POINTS) {
        sf::CircleShape dot(HOSHI_RADIUS);
        dot.setFillColor(GRID_COLOR);
        dot.setOrigin(HOSHI_RADIUS, HOSHI_RADIUS);
        dot.setPosition(pos_to_pixel(r, c));
        hoshi_dots_.push_back(dot);
    }

    // Coordinate labels (19 columns x 2 + 19 rows x 2 = 76 labels)
    coord_labels_.reserve(76);
    for (int c = 0; c < 19; ++c) {
        std::string label(1, col_label(c));

        sf::Text text;
        text.setFont(*font_);
        text.setString(label);
        text.setCharacterSize(12);
        text.setFillColor(GRID_COLOR);
        sf::FloatRect bounds = text.getLocalBounds();
        float x = MARGIN_LEFT + c * CELL_SIZE - bounds.width / 2 - bounds.left;

        // Top label
        sf::Text top_label = text;
        top_label.setPosition(x, MARGIN_TOP - 24);
        coord_labels_.push_back(top_label);

        // Bottom label
        sf::Text bot_label = text;
        bot_label.setPosition(x, MARGIN_TOP + 18 * CELL_SIZE + 8);
        coord_labels_.push_back(bot_label);
    }

    for (int r = 0; r < 19; ++r) {
        std::string label = std::to_string(r + 1);

        sf::Text text;
        text.setFont(*font_);
        text.setString(label);
        text.setCharacterSize(12);
        text.setFillColor(GRID_COLOR);
        sf::FloatRect bounds = text.getLocalBounds();
        float y = MARGIN_TOP + (18 - r) * CELL_SIZE - bounds.height / 2 - bounds.top - 2;

        // Left label
        sf::Text left_label = text;
        left_label.setPosition(MARGIN_LEFT - 12 - bounds.width, y);
        coord_labels_.push_back(left_label);

        // Right label
        sf::Text right_label = text;
        right_label.setPosition(MARGIN_LEFT + 18 * CELL_SIZE + 8, y);
        coord_labels_.push_back(right_label);
    }
}

void BoardRenderer::draw(sf::RenderWindow& window, const Board& board,
                          std::optional<Pos> last_move, std::optional<Pos> hover_pos,
                          Stone hover_color, std::optional<Pos> hint_pos) const {
    // Draw cached static elements
    window.draw(bg_);
    window.draw(grid_lines_);
    for (const auto& dot : hoshi_dots_) {
        window.draw(dot);
    }
    for (const auto& label : coord_labels_) {
        window.draw(label);
    }

    // Dynamic elements
    draw_stones(window, board, last_move);
    draw_forbidden(window, board, hover_color);
    if (hint_pos.has_value()) {
        draw_hint(window, *hint_pos, hover_color);
    }
    if (hover_pos.has_value()) {
        draw_hover(window, *hover_pos, hover_color);
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

void BoardRenderer::draw_hint(sf::RenderWindow& window, Pos pos, Stone color) const {
    sf::Vector2f px = pos_to_pixel(pos.row, pos.col);
    sf::CircleShape ghost(STONE_RADIUS);
    ghost.setOrigin(STONE_RADIUS, STONE_RADIUS);
    ghost.setPosition(px);
    ghost.setFillColor(color == Stone::Black ? HINT_BLACK : HINT_WHITE);
    window.draw(ghost);
}

void BoardRenderer::draw_forbidden(sf::RenderWindow& window, const Board& board,
                                    Stone color) const {
    for (int r = 0; r < 19; ++r) {
        for (int c = 0; c < 19; ++c) {
            Pos pos(static_cast<uint8_t>(r), static_cast<uint8_t>(c));
            if (!board.is_empty(pos)) continue;
            if (!is_double_three(board, pos, color)) continue;

            sf::Vector2f px = pos_to_pixel(r, c);
            float half = STONE_RADIUS * 0.75f;
            constexpr float THICK = 4.0f;
            constexpr float CAP_R = THICK * 0.5f;

            auto draw_bar = [&](float x1, float y1, float x2, float y2) {
                float dx = x2 - x1, dy = y2 - y1;
                float len = std::sqrt(dx * dx + dy * dy);
                float nx = -dy / len * THICK * 0.5f;
                float ny =  dx / len * THICK * 0.5f;
                sf::ConvexShape bar(4);
                bar.setPoint(0, {x1 + nx, y1 + ny});
                bar.setPoint(1, {x2 + nx, y2 + ny});
                bar.setPoint(2, {x2 - nx, y2 - ny});
                bar.setPoint(3, {x1 - nx, y1 - ny});
                bar.setFillColor(FORBIDDEN_COLOR);
                window.draw(bar);
                // Round caps
                sf::CircleShape cap(CAP_R);
                cap.setOrigin(CAP_R, CAP_R);
                cap.setFillColor(FORBIDDEN_COLOR);
                cap.setPosition(x1, y1);
                window.draw(cap);
                cap.setPosition(x2, y2);
                window.draw(cap);
            };
            draw_bar(px.x - half, px.y - half, px.x + half, px.y + half);
            draw_bar(px.x + half, px.y - half, px.x - half, px.y + half);
        }
    }
}

} // namespace gui
} // namespace gomoku
