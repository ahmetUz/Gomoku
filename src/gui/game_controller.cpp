#include "gomoku/gui/game_controller.hpp"

namespace gomoku {
namespace gui {

GameController::GameController(const sf::Font& font)
    : window_(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "Gomoku",
              sf::Style::Titlebar | sf::Style::Close)
{
    // Do NOT use setFramerateLimit — it sleeps inside display() and
    // starves the event loop on WSL2 / certain compositors, causing
    // the window to freeze when dragged or during heavy AI computation.
    window_.setVerticalSyncEnabled(false);
    board_renderer_.init(font);
    ui_panel_.init(font);
}

GameController::~GameController() {
    if (ai_thread_.joinable()) {
        ai_thread_.join();
    }
}

void GameController::run() {
    sf::Clock frame_clock;
    constexpr int TARGET_FPS = 60;
    const sf::Time target_frame_time = sf::microseconds(1'000'000 / TARGET_FPS);

    while (window_.isOpen()) {
        // 1. Always drain the full event queue — this must never be
        //    gated by a sleep or frame timer, otherwise the window
        //    manager considers us unresponsive (freeze on drag, etc.).
        handle_events();

        // 2. Game-logic tick + redraw only at ~60 fps.
        if (frame_clock.getElapsedTime() >= target_frame_time) {
            frame_clock.restart();

            if (ai_done_.load()) {
                check_ai_result();
            }

            if (phase_ == GamePhase::Playing && !winner_.has_value() &&
                is_ai_turn() && !ai_thinking_.load()) {
                start_ai_turn();
            }

            draw();
        } else {
            // Yield the CPU briefly so we don't busy-wait, but keep
            // the sleep short (1 ms) so events are still polled fast.
            sf::sleep(sf::milliseconds(1));
        }
    }
}

// ─── Events ─────────────────────────────────────────────────────────────────

void GameController::handle_events() {
    sf::Event event;
    while (window_.pollEvent(event)) {
        if (event.type == sf::Event::Closed) {
            window_.close();
        }
        else if (event.type == sf::Event::KeyPressed) {
            handle_key(event.key.code);
        }
        else if (event.type == sf::Event::MouseButtonPressed) {
            if (event.mouseButton.button == sf::Mouse::Left) {
                handle_click(static_cast<float>(event.mouseButton.x),
                             static_cast<float>(event.mouseButton.y));
            }
        }
        else if (event.type == sf::Event::MouseMoved) {
            if (phase_ == GamePhase::Playing && !winner_.has_value() &&
                !ai_thinking_.load()) {
                auto [row, col] = pixel_to_pos(
                    static_cast<float>(event.mouseMove.x),
                    static_cast<float>(event.mouseMove.y));
                if (row >= 0 && col >= 0) {
                    Pos pos(static_cast<uint8_t>(row), static_cast<uint8_t>(col));
                    hover_pos_ = board_.is_empty(pos)
                        ? std::optional<Pos>(pos) : std::nullopt;
                } else {
                    hover_pos_ = std::nullopt;
                }
            } else {
                hover_pos_ = std::nullopt;
            }
        }
    }
}

void GameController::handle_click(float x, float y) {
    switch (phase_) {
        case GamePhase::Menu: {
            auto mode = ui_panel_.handle_menu_click(x, y);
            if (mode.has_value()) {
                new_game(*mode);
            }
            break;
        }
        case GamePhase::Playing: {
            if (winner_.has_value()) {
                if (ui_panel_.is_new_game_clicked(x, y))
                    phase_ = GamePhase::Menu;
                break;
            }

            if (ui_panel_.is_undo_clicked(x, y)) {
                undo_move();
                break;
            }
            if (ui_panel_.is_new_game_clicked(x, y)) {
                phase_ = GamePhase::Menu;
                break;
            }

            if (is_ai_turn()) break;

            auto [row, col] = pixel_to_pos(x, y);
            if (row >= 0 && col >= 0) {
                execute_move(Pos(static_cast<uint8_t>(row),
                                 static_cast<uint8_t>(col)));
            }
            break;
        }
        case GamePhase::GameOver: {
            if (ui_panel_.is_new_game_clicked(x, y))
                phase_ = GamePhase::Menu;
            break;
        }
    }
}

void GameController::handle_key(sf::Keyboard::Key key) {
    if (key == sf::Keyboard::Escape) {
        if (phase_ != GamePhase::Menu) {
            phase_ = GamePhase::Menu;
        } else {
            window_.close();
        }
    }
    else if (key == sf::Keyboard::U) {
        if (phase_ == GamePhase::Playing)
            undo_move();
    }
    else if (key == sf::Keyboard::N) {
        phase_ = GamePhase::Menu;
    }
}

// ─── Game logic ─────────────────────────────────────────────────────────────

bool GameController::execute_move(Pos pos) {
    Stone color = current_turn_;

    if (!board_.is_empty(pos)) return false;
    if (!is_valid_move(board_, pos, color)) return false;

    board_.place_stone(pos, color);
    CaptureInfo cap_info = execute_captures_fast(board_, pos, color);

    history_.push_back({pos, color, cap_info});
    last_move_ = pos;

    auto winner = check_winner(board_, color);
    if (winner.has_value()) {
        winner_ = winner;
    }

    current_turn_ = opponent(color);
    return true;
}

bool GameController::undo_move() {
    if (history_.empty()) return false;

    // Invalidate any in-flight AI result
    ++move_gen_;

    // Wait for AI thread to finish before mutating board
    if (ai_thread_.joinable()) ai_thread_.join();
    ai_thinking_.store(false);
    ai_done_.store(false);

    int undo_count = 1;
    if (mode_ != GameMode::PvP && history_.size() >= 2) {
        undo_count = 2;
    }

    for (int i = 0; i < undo_count && !history_.empty(); ++i) {
        auto& record = history_.back();
        undo_captures(board_, record.color, record.captures);
        board_.remove_stone(record.pos);
        current_turn_ = record.color;
        history_.pop_back();
    }

    last_move_ = history_.empty() ? std::nullopt
        : std::optional<Pos>(history_.back().pos);

    winner_ = std::nullopt;
    return true;
}

void GameController::new_game(GameMode mode) {
    ++move_gen_;  // invalidate any in-flight AI result

    if (ai_thread_.joinable()) {
        ai_thread_.join();
    }

    board_ = Board();
    mode_ = mode;
    phase_ = GamePhase::Playing;
    current_turn_ = Stone::Black;
    last_move_ = std::nullopt;
    history_.clear();
    winner_ = std::nullopt;
    last_ai_result_ = MoveResult();
    ai_thinking_.store(false);
    ai_done_.store(false);
    engine_.clear_cache();
}

bool GameController::is_ai_turn() const {
    if (winner_.has_value()) return false;
    switch (mode_) {
        case GameMode::PvE_Black: return current_turn_ == Stone::White;
        case GameMode::PvE_White: return current_turn_ == Stone::Black;
        case GameMode::PvP:       return false;
    }
    return false;
}

void GameController::start_ai_turn() {
    if (ai_thinking_.load()) return;

    if (ai_thread_.joinable()) ai_thread_.join();

    ai_thinking_.store(true);
    ai_done_.store(false);
    ai_started_gen_ = move_gen_;

    Board board_copy = board_;
    Stone color = current_turn_;

    ai_thread_ = std::thread([this, board_copy, color]() {
        MoveResult result = engine_.get_move_with_stats(board_copy, color);
        std::lock_guard<std::mutex> lock(ai_mutex_);
        ai_result_ = result;
        ai_done_.store(true);
    });
}

void GameController::check_ai_result() {
    MoveResult result;
    {
        std::lock_guard<std::mutex> lock(ai_mutex_);
        result = ai_result_;
    }

    if (ai_thread_.joinable()) ai_thread_.join();

    ai_thinking_.store(false);
    ai_done_.store(false);

    // Discard stale result (board changed via undo/new_game while AI was thinking)
    if (ai_started_gen_ != move_gen_) return;

    last_ai_result_ = result;

    if (!result.best_move.is_sentinel()) {
        if (!execute_move(result.best_move)) {
            // AI returned an invalid move — bump generation to prevent infinite retry
            ++move_gen_;
        }
    } else {
        // AI returned no move — bump generation to prevent infinite retry
        ++move_gen_;
    }
}

// ─── Drawing ────────────────────────────────────────────────────────────────

void GameController::draw() {
    window_.clear(sf::Color(30, 30, 35));

    switch (phase_) {
        case GamePhase::Menu:
            ui_panel_.draw_menu(window_);
            break;

        case GamePhase::Playing:
        case GamePhase::GameOver:
            board_renderer_.draw(window_, board_, last_move_, hover_pos_,
                                 current_turn_);
            ui_panel_.draw_playing(window_, board_, current_turn_,
                                   static_cast<int>(history_.size() + 1),
                                   last_move_, last_ai_result_,
                                   ai_thinking_.load(), mode_);
            if (winner_.has_value()) {
                ui_panel_.draw_game_over(window_, *winner_, board_);
            }
            break;
    }

    window_.display();
}

} // namespace gui
} // namespace gomoku
