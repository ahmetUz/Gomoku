// Gomoku AI Engine -- Terminal interface
//
// Terminal-based game for Ninuki-renju (Pente-style Gomoku).
// Features:
// - 19x19 ASCII board with standard notation (A1-T19, skip I)
// - PvE (human vs AI) and PvP (hotseat) modes
// - AI search with full pipeline (opening book, VCF, alpha-beta)
// - Capture display, last move marker, AI statistics
// - Undo support
//
// Translates the Rust eframe/egui GUI into a text-based interface.

#include "gomoku/engine/engine.hpp"
#include "gomoku/rules/capture.hpp"
#include "gomoku/rules/win.hpp"
#include "gomoku/rules/forbidden.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

using namespace gomoku;

// ANSI color codes for terminal display
namespace ansi {
    const char* reset   = "\033[0m";
    const char* bold    = "\033[1m";
    const char* dim     = "\033[2m";
    const char* red     = "\033[31m";
    const char* green   = "\033[32m";
    const char* yellow  = "\033[33m";
    const char* blue    = "\033[34m";
    const char* magenta = "\033[35m";
    const char* cyan    = "\033[36m";
    const char* white   = "\033[37m";
    const char* bg_red  = "\033[41m";
}

// Game mode
enum class GameMode {
    PvE_Black,  // Human plays Black, AI plays White
    PvE_White,  // Human plays White, AI plays Black
    PvP,        // Player vs Player (hotseat)
};

// Move record for undo
struct MoveRecord {
    Pos pos;
    Stone color;
    CaptureInfo captures;
};

// Game state
struct GameState {
    Board board;
    GameMode mode = GameMode::PvE_Black;
    Stone current_turn = Stone::Black;
    std::optional<Pos> last_move;
    std::vector<MoveRecord> history;
    std::optional<Stone> winner;
    bool game_over = false;
    MoveResult last_ai_result;
};

// Convert column index (0-18) to display character (A-T, skip I)
char col_to_char(uint8_t col) {
    if (col < 8) return 'A' + col;
    return 'A' + col + 1; // skip 'I'
}

// Convert display character to column index. Returns -1 on invalid input.
int char_to_col(char c) {
    c = static_cast<char>(std::toupper(c));
    if (c < 'A' || c > 'T') return -1;
    if (c == 'I') return -1;
    if (c < 'I') return c - 'A';
    return c - 'A' - 1;
}

// Parse move notation like "J10" into a Pos. Returns nullopt on invalid input.
std::optional<Pos> parse_move(const std::string& input) {
    if (input.size() < 2 || input.size() > 3) return std::nullopt;

    int col = char_to_col(input[0]);
    if (col < 0) return std::nullopt;

    std::string row_str = input.substr(1);
    int row;
    try {
        row = std::stoi(row_str);
    } catch (...) {
        return std::nullopt;
    }

    // Rows: 1-19 displayed, row 1 = index 0
    row -= 1;
    if (row < 0 || row >= BOARD_SIZE) return std::nullopt;

    return Pos(static_cast<uint8_t>(row), static_cast<uint8_t>(col));
}

// Display the board with coordinates and markers
void display_board(const GameState& state) {
    std::cout << "\n";

    // Column headers
    std::cout << "   ";
    for (uint8_t c = 0; c < BOARD_SIZE; ++c) {
        std::cout << " " << col_to_char(c);
    }
    std::cout << "\n";

    // Board rows (display from top = row 19 to bottom = row 1)
    for (int r = BOARD_SIZE - 1; r >= 0; --r) {
        // Row number (right-aligned)
        std::cout << (r + 1 < 10 ? "  " : " ") << (r + 1) << " ";

        for (uint8_t c = 0; c < BOARD_SIZE; ++c) {
            Pos pos(static_cast<uint8_t>(r), c);
            Stone s = state.board.get(pos);

            bool is_last = state.last_move.has_value() && *state.last_move == pos;

            if (s == Stone::Black) {
                if (is_last) {
                    std::cout << ansi::bold << ansi::red << " X" << ansi::reset;
                } else {
                    std::cout << ansi::bold << " X" << ansi::reset;
                }
            } else if (s == Stone::White) {
                if (is_last) {
                    std::cout << ansi::bold << ansi::red << " O" << ansi::reset;
                } else {
                    std::cout << " O";
                }
            } else {
                // Empty intersection
                bool is_star = (r == 3 || r == 9 || r == 15) &&
                               (c == 3 || c == 9 || c == 15);
                if (is_star) {
                    std::cout << ansi::dim << " +" << ansi::reset;
                } else {
                    std::cout << ansi::dim << " ." << ansi::reset;
                }
            }
        }

        std::cout << " " << (r + 1) << "\n";
    }

    // Column headers (bottom)
    std::cout << "   ";
    for (uint8_t c = 0; c < BOARD_SIZE; ++c) {
        std::cout << " " << col_to_char(c);
    }
    std::cout << "\n";
}

// Display game info (captures, turn, last AI result)
void display_info(const GameState& state) {
    std::cout << "\n";

    // Captures
    std::cout << "  Captures: "
              << ansi::bold << "Black " << static_cast<int>(state.board.black_captures)
              << "/5" << ansi::reset
              << "  |  "
              << "White " << static_cast<int>(state.board.white_captures)
              << "/5\n";

    // Turn indicator
    if (!state.game_over) {
        const char* turn_str = (state.current_turn == Stone::Black) ? "BLACK (X)" : "WHITE (O)";
        std::cout << "  Turn: " << ansi::bold << turn_str << ansi::reset;

        // Show move number
        std::cout << "  (move #" << (state.history.size() + 1) << ")\n";
    }

    // Last move
    if (state.last_move.has_value()) {
        std::cout << "  Last move: " << ansi::cyan
                  << pos_to_notation(*state.last_move) << ansi::reset << "\n";
    }

    // AI stats from last search
    if (!state.last_ai_result.best_move.is_sentinel()) {
        const auto& r = state.last_ai_result;
        std::cout << "  AI: ";

        // Search type
        switch (r.search_type) {
            case SearchType::ImmediateWin:
                std::cout << ansi::green << "ImmediateWin" << ansi::reset;
                break;
            case SearchType::VCF:
                std::cout << ansi::green << "VCF" << ansi::reset;
                break;
            case SearchType::VCT:
                std::cout << ansi::yellow << "VCT" << ansi::reset;
                break;
            case SearchType::Defense:
                std::cout << ansi::red << "Defense" << ansi::reset;
                break;
            case SearchType::AlphaBeta:
                std::cout << ansi::blue << "AlphaBeta" << ansi::reset;
                break;
        }

        std::cout << " | score=" << r.score
                  << " depth=" << static_cast<int>(r.depth)
                  << " nodes=" << r.nodes
                  << " time=" << r.time_ms << "ms";
        if (r.nps > 0) std::cout << " nps=" << r.nps << "k";
        if (r.tt_usage > 0) std::cout << " tt=" << static_cast<int>(r.tt_usage) << "%";
        std::cout << "\n";
    }
}

// Display game over message
void display_game_over(const GameState& state) {
    if (!state.winner.has_value()) return;

    const char* winner_str = (*state.winner == Stone::Black) ? "BLACK" : "WHITE";

    std::cout << "\n" << ansi::bold << ansi::green
              << "  *** " << winner_str << " WINS! ***"
              << ansi::reset << "\n";

    // Determine win type
    if (state.board.captures(*state.winner) >= 5) {
        std::cout << "  by 10 captures (5 pairs)\n";
    } else {
        std::cout << "  by 5-in-a-row\n";
    }
}

// Execute a move with capture handling, return true if valid
bool execute_move(GameState& state, Pos pos) {
    Stone color = state.current_turn;

    // Validate
    if (!state.board.is_empty(pos)) {
        std::cout << ansi::red << "  Invalid: position occupied" << ansi::reset << "\n";
        return false;
    }
    if (!is_valid_move(state.board, pos, color)) {
        if (is_double_three(state.board, pos, color)) {
            std::cout << ansi::red << "  Forbidden: double-three" << ansi::reset << "\n";
        } else {
            std::cout << ansi::red << "  Invalid move" << ansi::reset << "\n";
        }
        return false;
    }

    // Place stone
    state.board.place_stone(pos, color);
    CaptureInfo cap_info = execute_captures_fast(state.board, pos, color);

    // Record for undo
    state.history.push_back({pos, color, cap_info});
    state.last_move = pos;

    // Display captures
    if (cap_info.pairs > 0) {
        std::cout << "  " << ansi::yellow << "Captured " << static_cast<int>(cap_info.pairs)
                  << " pair(s)!" << ansi::reset << "\n";
    }

    // Check win
    auto winner = check_winner(state.board, color);
    if (winner.has_value()) {
        state.winner = winner;
        state.game_over = true;
    }

    // Switch turn
    state.current_turn = opponent(color);
    return true;
}

// Undo last move (or last 2 moves in PvE to get back to human's turn)
bool undo_move(GameState& state) {
    if (state.history.empty()) {
        std::cout << "  Nothing to undo.\n";
        return false;
    }

    // In PvE, undo 2 moves (AI + human) if possible
    int undo_count = 1;
    if (state.mode != GameMode::PvP && state.history.size() >= 2) {
        undo_count = 2;
    }

    for (int i = 0; i < undo_count && !state.history.empty(); ++i) {
        auto& record = state.history.back();

        // Undo captures
        undo_captures(state.board, record.color, record.captures);

        // Remove stone
        state.board.remove_stone(record.pos);

        // Restore turn
        state.current_turn = record.color;

        state.history.pop_back();
    }

    // Update last move
    if (!state.history.empty()) {
        state.last_move = state.history.back().pos;
    } else {
        state.last_move = std::nullopt;
    }

    // Clear game over
    state.winner = std::nullopt;
    state.game_over = false;

    std::cout << "  Undone " << undo_count << " move(s).\n";
    return true;
}

// Check if it's the AI's turn
bool is_ai_turn(const GameState& state) {
    switch (state.mode) {
        case GameMode::PvE_Black: return state.current_turn == Stone::White;
        case GameMode::PvE_White: return state.current_turn == Stone::Black;
        case GameMode::PvP: return false;
    }
    return false;
}

// Display help
void display_help() {
    std::cout << "\n  " << ansi::bold << "Commands:" << ansi::reset << "\n"
              << "    <move>  - Place stone (e.g., J10, A1, T19)\n"
              << "    undo    - Undo last move(s)\n"
              << "    hint    - Get AI suggestion for current position\n"
              << "    new     - Start a new game\n"
              << "    mode    - Change game mode\n"
              << "    help    - Show this help\n"
              << "    quit    - Exit\n"
              << "\n  " << ansi::bold << "Notation:" << ansi::reset << "\n"
              << "    Columns: A-H, J-T (letter I is skipped)\n"
              << "    Rows: 1-19 (1=bottom, 19=top)\n"
              << "    Example: K10 = center of the board\n\n";
}

// Select game mode interactively
GameMode select_mode() {
    std::cout << "\n  " << ansi::bold << "Select game mode:" << ansi::reset << "\n"
              << "    1. Play as Black vs AI (you go first)\n"
              << "    2. Play as White vs AI (AI goes first)\n"
              << "    3. Player vs Player (hotseat)\n"
              << "  Choice [1-3]: ";

    std::string input;
    std::getline(std::cin, input);

    if (input == "2") return GameMode::PvE_White;
    if (input == "3") return GameMode::PvP;
    return GameMode::PvE_Black; // default
}

int main() {
    std::cout << ansi::bold << "\n"
              << "  =============================================\n"
              << "  |         GOMOKU - Ninuki-renju             |\n"
              << "  =============================================\n"
              << ansi::reset << "\n"
              << "  Win by: 5-in-a-row (unbreakable) or 5 pair captures\n"
              << "  Rules: Pente-style pair capture, double-three forbidden for Black\n"
              << "  Type 'help' for commands.\n";

    // Select game mode
    GameMode mode = select_mode();

    // Create AI engine (64 MB TT, depth 20, 500ms)
    AIEngine engine;

    // Initialize game state
    GameState state;
    state.mode = mode;

    // Main game loop
    while (true) {
        // Display board and info
        display_board(state);
        display_info(state);

        // Check game over
        if (state.game_over) {
            display_game_over(state);
            std::cout << "\n  Type 'new' for a new game, 'quit' to exit.\n";

            std::cout << "  > ";
            std::string input;
            if (!std::getline(std::cin, input)) break;

            // Trim whitespace
            while (!input.empty() && std::isspace(input.front())) input.erase(input.begin());
            while (!input.empty() && std::isspace(input.back())) input.pop_back();

            // Convert to lowercase for commands
            std::string lower = input;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            if (lower == "new") {
                state = GameState{};
                state.mode = select_mode();
                engine.clear_cache();
                continue;
            } else if (lower == "quit" || lower == "q" || lower == "exit") {
                break;
            }
            continue;
        }

        // AI's turn
        if (is_ai_turn(state)) {
            const char* ai_color = (state.current_turn == Stone::Black) ? "Black" : "White";
            std::cout << "\n  " << ansi::cyan << "AI (" << ai_color
                      << ") is thinking..." << ansi::reset << std::flush;

            MoveResult result = engine.get_move_with_stats(state.board, state.current_turn);
            state.last_ai_result = result;

            if (!result.best_move.is_sentinel()) {
                std::cout << " " << pos_to_notation(result.best_move) << "\n";
                execute_move(state, result.best_move);
            } else {
                std::cout << ansi::red << " No move found!" << ansi::reset << "\n";
                break;
            }
            continue;
        }

        // Human's turn - get input
        std::cout << "  > ";
        std::string input;
        if (!std::getline(std::cin, input)) break;

        // Trim whitespace
        while (!input.empty() && std::isspace(input.front())) input.erase(input.begin());
        while (!input.empty() && std::isspace(input.back())) input.pop_back();
        if (input.empty()) continue;

        // Convert to lowercase for command comparison
        std::string lower = input;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // Handle commands
        if (lower == "quit" || lower == "q" || lower == "exit") {
            break;
        } else if (lower == "help" || lower == "h" || lower == "?") {
            display_help();
            continue;
        } else if (lower == "undo" || lower == "u") {
            undo_move(state);
            continue;
        } else if (lower == "new" || lower == "n") {
            state = GameState{};
            state.mode = select_mode();
            engine.clear_cache();
            continue;
        } else if (lower == "mode") {
            state = GameState{};
            state.mode = select_mode();
            engine.clear_cache();
            continue;
        } else if (lower == "hint") {
            std::cout << "  " << ansi::cyan << "Thinking..." << ansi::reset << std::flush;
            MoveResult result = engine.get_move_with_stats(state.board, state.current_turn);
            state.last_ai_result = result;
            if (!result.best_move.is_sentinel()) {
                std::cout << " Suggestion: " << ansi::bold
                          << pos_to_notation(result.best_move)
                          << ansi::reset << "\n";
            } else {
                std::cout << " No suggestion.\n";
            }
            continue;
        }

        // Parse as move notation (preserve original case for column letter)
        auto move_pos = parse_move(input);
        if (!move_pos.has_value()) {
            std::cout << ansi::red << "  Invalid notation. Use format like J10, A1, T19."
                      << ansi::reset << "\n";
            std::cout << "  Type 'help' for more info.\n";
            continue;
        }

        // Execute the move
        execute_move(state, *move_pos);
    }

    std::cout << "\n  Thanks for playing!\n\n";
    return 0;
}
