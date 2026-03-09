#pragma once

#include <SFML/Graphics.hpp>
#include "gomoku/board/board.hpp"
#include "gomoku/engine/engine.hpp"
#include "gui_constants.hpp"
#include <string>
#include <optional>

namespace gomoku {
namespace gui {

enum class GameMode {
    PvE_Black,
    PvE_White,
    PvP,
};

enum class GamePhase {
    Menu,
    Playing,
    GameOver,
};

class UIPanel {
public:
    void init(const sf::Font& font);

    // Menu screen
    void draw_menu(sf::RenderWindow& window, sf::Vector2f mouse_pos) const;
    std::optional<GameMode> handle_menu_click(float x, float y) const;

    // Playing screen side panel
    void draw_playing(sf::RenderWindow& window, const Board& board,
                      Stone current_turn, int move_number,
                      std::optional<Pos> last_move,
                      const MoveResult& last_ai_result,
                      bool ai_thinking, uint32_t ai_elapsed_ms,
                      GameMode mode, sf::Vector2f mouse_pos) const;

    // Game over overlay on the board area
    void draw_game_over(sf::RenderWindow& window, Stone winner,
                        const Board& board) const;

    // Button hit testing
    bool is_hint_clicked(float x, float y) const;
    bool is_undo_clicked(float x, float y) const;
    bool is_new_game_clicked(float x, float y) const;

private:
    const sf::Font* font_ = nullptr;

    mutable sf::FloatRect hint_btn_;
    mutable sf::FloatRect undo_btn_;
    mutable sf::FloatRect new_game_btn_;
    mutable sf::FloatRect pvb_btn_;
    mutable sf::FloatRect pvw_btn_;
    mutable sf::FloatRect pvp_btn_;

    void draw_button(sf::RenderWindow& window, const sf::FloatRect& rect,
                     const std::string& text, bool hovered = false) const;
};

} // namespace gui
} // namespace gomoku
