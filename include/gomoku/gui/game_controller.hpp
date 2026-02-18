#pragma once

#include <SFML/Graphics.hpp>
#include "gomoku/board/board.hpp"
#include "gomoku/engine/engine.hpp"
#include "gomoku/rules/capture.hpp"
#include "gomoku/rules/win.hpp"
#include "gomoku/rules/forbidden.hpp"
#include "board_renderer.hpp"
#include "ui_panel.hpp"
#include <vector>
#include <optional>
#include <thread>
#include <mutex>
#include <atomic>

namespace gomoku {
namespace gui {

struct MoveRecord {
    Pos pos;
    Stone color;
    CaptureInfo captures;
};

class GameController {
public:
    explicit GameController(const sf::Font& font);
    ~GameController();

    void run();

private:
    // Rendering
    sf::RenderWindow window_;
    BoardRenderer board_renderer_;
    UIPanel ui_panel_;

    // Game state
    Board board_;
    GameMode mode_ = GameMode::PvE_Black;
    GamePhase phase_ = GamePhase::Menu;
    Stone current_turn_ = Stone::Black;
    std::optional<Pos> last_move_;
    std::vector<MoveRecord> history_;
    std::optional<Stone> winner_;
    MoveResult last_ai_result_;

    // AI thread
    AIEngine engine_;
    std::thread ai_thread_;
    std::mutex ai_mutex_;
    std::atomic<bool> ai_thinking_{false};
    std::atomic<bool> ai_done_{false};
    MoveResult ai_result_;
    uint64_t move_gen_ = 0;        // incremented on every state change
    uint64_t ai_started_gen_ = 0;  // generation when AI was launched

    // Hover
    std::optional<Pos> hover_pos_;

    // Event handling
    void handle_events();
    void handle_click(float x, float y);
    void handle_key(sf::Keyboard::Key key);

    // Game logic
    bool execute_move(Pos pos);
    bool undo_move();
    void new_game(GameMode mode);
    bool is_ai_turn() const;
    void start_ai_turn();
    void check_ai_result();

    // Drawing
    void draw();
};

} // namespace gui
} // namespace gomoku
