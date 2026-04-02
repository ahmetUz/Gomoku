#pragma once

// Main AI Engine integrating all search components
//
// The engine orchestrates the search pipeline:
// 1. VCF (Victory by Continuous Fours) for forced win (50ms max)
// 2. Alpha-Beta search for all other positions
//
// Immediate wins, opening moves, and defensive blocks are handled
// natively by the alpha-beta search via move ordering and quiescence.

#include "gomoku/board/board.hpp"
#include "gomoku/minimax/searcher.hpp"
#include "gomoku/minimax/vcf.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gomoku {

// Format a board position as human-readable notation (e.g., "J10").
std::string pos_to_notation(Pos pos);

// Write a log message to both gomoku_ai.log and stderr.
void ai_log(const std::string& msg);

// Type of search that produced the result.
enum class SearchType {
    ImmediateWin,  // Kept for UI display compatibility
    VCF,           // Found forced win via Victory by Continuous Fours
    VCT,           // Found forced win via Victory by Continuous Threats
    Defense,       // Defensive move to block opponent's threat
    AlphaBeta,     // Regular alpha-beta search result
};

// Result of a move search with detailed statistics.
struct MoveResult {
    Pos best_move = Pos::sentinel();
    int32_t score = 0;
    SearchType search_type = SearchType::AlphaBeta;
    uint64_t time_ms = 0;
    uint64_t nodes = 0;
    int8_t depth = 0;
    uint8_t tt_usage = 0;
    uint64_t nps = 0;

    static uint64_t compute_nps(uint64_t nodes, uint64_t time_ms);

    // Factory methods
    static MoveResult vcf_win(Pos pos, uint64_t time_ms, uint64_t nodes);
    static MoveResult from_alphabeta(const SearchResult& result, uint64_t time_ms, uint8_t tt_usage);
};

// Main AI Engine for Gomoku.
//
// Pipeline: VCF search -> block opponent VCF -> alpha-beta.
// Default config: 16 MB TT, depth 20, 500ms time limit.
class AIEngine {
public:
    AIEngine();
    AIEngine(size_t tt_size_mb, int8_t max_depth, uint64_t time_limit_ms);

    // Get the best move with detailed search statistics.
    MoveResult get_move_with_stats(const Board& board, Stone color);

    void stop();
    void clear_cache();
    TTStats tt_stats() const;

private:
    Searcher searcher_;
    ThreatSearcher threat_searcher_;
    int8_t max_depth_;
    uint64_t time_limit_ms_;
};

} // namespace gomoku
