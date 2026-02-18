#pragma once

// Alpha-Beta search with iterative deepening and transposition table
//
// Features:
// - Iterative deepening for time management and move ordering
// - Transposition table (AtomicTT) for avoiding redundant searches
// - Lazy SMP: parallel search with lock-free shared TT
//
// Optimizations (each wrapped in #ifndef GOMOKU_NO_XXX):
// - Aspiration windows, PVS, LMR, Null Move Pruning
// - Reverse Futility, Razoring, Futility Pruning, Late Move Pruning
// - Threat Extensions, IID, Quiescence Search

#include "gomoku/board/board.hpp"
#include <cstdint>
#include <optional>
#include <memory>

namespace gomoku {

// Forward declarations (implementation details in .cpp)
struct SharedState;
struct TTStats;

// Search statistics for diagnostics and tuning
struct SearchStats {
    uint64_t beta_cutoffs = 0;
    uint64_t first_move_cutoffs = 0;
    uint64_t tt_probes = 0;
    uint64_t tt_score_hits = 0;
    uint64_t tt_move_hits = 0;

    // First-move cutoff rate (target: ~90% for good move ordering)
    double first_move_rate() const;
    // TT score hit rate
    double tt_score_rate() const;
    // Merge another stats into this one (for combining worker stats)
    void merge(const SearchStats& other);
};

// Search result containing the best move found and associated statistics
struct SearchResult {
    std::optional<Pos> best_move;
    int32_t score = 0;
    int8_t depth = 0;
    uint64_t nodes = 0;
    SearchStats stats;
};

// Alpha-Beta search engine with iterative deepening and transposition table.
//
// Internally uses Lazy SMP for parallel search when num_threads > 1.
// The searcher maintains a transposition table across searches.
// Call clear_tt() for a new game.
class Searcher {
public:
    // Create with auto-detected thread count (up to 8).
    explicit Searcher(size_t tt_size_mb);

    // Create with explicit thread count.
    Searcher(size_t tt_size_mb, size_t num_threads);

    // Single-threaded search (deterministic, for tests).
    SearchResult search(const Board& board, Stone color, int8_t max_depth);

    // Timed search with Lazy SMP parallel search.
    // Minimum depth 10, soft time limit with prediction-based cutoff.
    SearchResult search_timed(const Board& board, Stone color,
                              int8_t max_depth, uint64_t time_limit_ms);

    void clear_history();
    TTStats tt_stats() const;
    void clear_tt() const;

private:
    std::shared_ptr<SharedState> shared_;
    int8_t max_depth_;
    size_t num_threads_;
    int32_t history_[2][BOARD_SIZE][BOARD_SIZE];
};

} // namespace gomoku
