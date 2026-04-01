#pragma once

#include <SFML/Graphics.hpp>
#include "gomoku/board/board.hpp"
#include "gomoku/rules/forbidden.hpp"
#include "gui_constants.hpp"
#include <optional>
#include <vector>

namespace gomoku {
namespace gui {

class BoardRenderer {
public:
    void init(const sf::Font& font);
    void draw(sf::RenderWindow& window, const Board& board,
              std::optional<Pos> last_move, std::optional<Pos> hover_pos,
              Stone hover_color, std::optional<Pos> hint_pos = std::nullopt) const;

private:
    const sf::Font* font_ = nullptr;

    // Cached static elements (built once in init)
    sf::RectangleShape bg_;
    sf::VertexArray grid_lines_;
    std::vector<sf::CircleShape> hoshi_dots_;
    std::vector<sf::Text> coord_labels_;

    void draw_stones(sf::RenderWindow& window, const Board& board,
                     std::optional<Pos> last_move) const;
    void draw_hover(sf::RenderWindow& window, Pos pos, Stone color) const;
    void draw_hint(sf::RenderWindow& window, Pos pos, Stone color) const;
    void draw_forbidden(sf::RenderWindow& window, const Board& board,
                        Stone color) const;
};

} // namespace gui
} // namespace gomoku
