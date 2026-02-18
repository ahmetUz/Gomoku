#pragma once

#include <SFML/Graphics.hpp>
#include "gomoku/board/board.hpp"
#include "gui_constants.hpp"
#include <optional>

namespace gomoku {
namespace gui {

class BoardRenderer {
public:
    void init(const sf::Font& font);
    void draw(sf::RenderWindow& window, const Board& board,
              std::optional<Pos> last_move, std::optional<Pos> hover_pos,
              Stone hover_color) const;

private:
    const sf::Font* font_ = nullptr;

    void draw_background(sf::RenderWindow& window) const;
    void draw_grid(sf::RenderWindow& window) const;
    void draw_hoshi(sf::RenderWindow& window) const;
    void draw_coordinates(sf::RenderWindow& window) const;
    void draw_stones(sf::RenderWindow& window, const Board& board,
                     std::optional<Pos> last_move) const;
    void draw_hover(sf::RenderWindow& window, Pos pos, Stone color) const;
};

} // namespace gui
} // namespace gomoku
