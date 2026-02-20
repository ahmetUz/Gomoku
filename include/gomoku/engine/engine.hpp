#pragma once

// Main AI Engine integrating all search components
//
// The engine orchestrates all search algorithms to find the best move
// in any position. The search follows a priority system:
//
// 1. Opening book for fast early game
// 2. Break opponent's existing breakable five
// 3. Immediate win (5-in-a-row or capture win)
// 4. Block opponent's immediate win
// 5. VCF (Victory by Continuous Fours) for forced win
// 6. Block opponent's VCF
// 7. Alpha-Beta search for all other positions

#include "gomoku/board/board.hpp"
#include "gomoku/search/alphabeta.hpp"
#include "gomoku/search/threat.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gomoku {

// Format a board position as human-readable notation (e.g., "J10").
// Columns: A=0, B=1, ..., H=7, J=8 (skip I), K=9, ...
// Rows: 1=0, 2=1, ..., 19=18
std::string pos_to_notation(Pos pos);

// Write a log message to both gomoku_ai.log and stderr.
void ai_log(const std::string& msg);

// Type of search that produced the result.
// Indicates which phase of the search hierarchy found the move.
enum class SearchType {
    ImmediateWin,  // Found immediate winning move (5-in-a-row or capture win)
    VCF,           // Found forced win via Victory by Continuous Fours
    VCT,           // Found forced win via Victory by Continuous Threats
    Defense,       // Defensive move to block opponent's threat
    AlphaBeta,     // Regular alpha-beta search result
};

// Result of a move search with detailed statistics.
struct MoveResult {
    Pos best_move = Pos::sentinel();  // Best move found (sentinel = no move)
    int32_t score = 0;             // Evaluation score
    SearchType search_type = SearchType::AlphaBeta;
    uint64_t time_ms = 0;         // Time taken in milliseconds
    uint64_t nodes = 0;           // Number of nodes searched
    int8_t depth = 0;             // Search depth reached
    uint8_t tt_usage = 0;         // Transposition table usage percentage (0-100)
    uint64_t nps = 0;             // Nodes per second (kN/s)

    // Compute nodes per second in kN/s
    static uint64_t compute_nps(uint64_t nodes, uint64_t time_ms);

    // Factory methods for creating results from different search stages
    static MoveResult immediate_win(Pos pos, uint64_t time_ms);
    static MoveResult vcf_win(Pos pos, uint64_t time_ms, uint64_t nodes);
    static MoveResult vct_win(Pos pos, uint64_t time_ms, uint64_t nodes);
    static MoveResult defense(Pos pos, int32_t score, uint64_t time_ms, uint64_t nodes);
    static MoveResult from_alphabeta(const SearchResult& result, uint64_t time_ms, uint8_t tt_usage);
    static MoveResult alpha_beta(Pos pos, int32_t score, uint64_t time_ms, uint64_t nodes);
    static MoveResult no_move(uint64_t time_ms);
};

// Main AI Engine for Gomoku.
//
// Integrates VCF/VCT threat search, alpha-beta with transposition table,
// and immediate win/loss detection into a priority-based search pipeline.
//
// Default config: 16 MB TT, depth 20, 500ms time limit.
class AIEngine {
public:
    // Create with default settings (16 MB TT, depth 20, 500ms).
    AIEngine();

    // Create with custom configuration.
    AIEngine(size_t tt_size_mb, int8_t max_depth, uint64_t time_limit_ms);

    // Get the best move (convenience, returns only the move).
    std::optional<Pos> get_move(const Board& board, Stone color);

    // Get the best move with detailed search statistics.
    // Full pipeline: opening book -> break five -> immediate win ->
    //   block win -> VCF -> block VCF -> alpha-beta
    MoveResult get_move_with_stats(const Board& board, Stone color);

    // Configuration
    void set_max_depth(int8_t depth);
    void set_time_limit(uint64_t time_ms);
    int8_t max_depth() const { return max_depth_; }

    // Cache management
    void clear_cache();
    TTStats tt_stats() const;

    // Opening book for first 1-3 moves (public for testing).
    std::optional<Pos> get_opening_move(const Board& board, Stone color) const;

    // Check if all break captures on a five are illusory (public for testing).
    // A break is "illusory" when the five-holder can replay and create
    // an unbreakable five after the break capture.
    static bool is_illusory_break(const Board& board,
                                  const std::vector<Pos>& five_positions,
                                  Stone five_color);

    // Find ALL positions where color can win immediately (public for testing).
    std::vector<Pos> find_winning_moves(const Board& board, Stone color) const;

private:
    Searcher searcher_;
    ThreatSearcher threat_searcher_;
    int8_t max_depth_;
    uint64_t time_limit_ms_;

    // Find an immediate winning move (5-in-a-row or 5th capture pair).
    std::optional<Pos> find_immediate_win(const Board& board, Stone color) const;

    // Compute adaptive time limit based on game phase.
    uint64_t compute_time_limit(const Board& board) const;
};

} // namespace gomoku
