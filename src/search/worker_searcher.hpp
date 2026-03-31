#pragma once

// Internal header -- per-thread search state for alpha-beta.
// NOT part of the public API. Only included by src/search/*.cpp files.

#include "gomoku/search/alphabeta.hpp"
#include "gomoku/search/zobrist.hpp"
#include "gomoku/search/tt.hpp"
#include "gomoku/eval/heuristic.hpp"
#include "gomoku/eval/patterns.hpp"
#include "gomoku/rules/capture.hpp"
#include "gomoku/rules/win.hpp"
#include "gomoku/rules/forbidden.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace gomoku {

// =========================================================================
// Constants
// =========================================================================

// Infinity score for alpha-beta bounds
static constexpr int32_t INF = PatternScore::FIVE + 1;

// Max moves evaluated at root vs internal nodes.
// Priority sorting places critical moves first, so these limits cover
// all tactical threats without wasting time on irrelevant candidates.
static constexpr size_t MAX_ROOT_MOVES = 25;
static constexpr size_t MAX_INTERNAL_MOVES = 12;

// Aspiration window half-width.
// At each new depth, we search [prev_score - ASP, prev_score + ASP].
// If the real score falls outside, we re-search with full window.
static constexpr int32_t ASP_WINDOW = 100;

// =========================================================================
// MoveList : stack-allocated move list (no heap allocation)
// =========================================================================
// 128 entries x 8 bytes = 1 KB per recursion level.

struct MoveList {
    static constexpr size_t CAPACITY = 128;
    std::pair<Pos, int32_t> data[CAPACITY];
    size_t count = 0;
    int32_t top_score = 0;

    void push_back(Pos pos, int32_t score) {
        if (count < CAPACITY) data[count++] = {pos, score};
    }
    bool empty() const { return count == 0; }
    size_t size() const { return count; }

    std::pair<Pos, int32_t>* begin() { return data; }
    std::pair<Pos, int32_t>* end() { return data + count; }
    const std::pair<Pos, int32_t>* begin() const { return data; }
    const std::pair<Pos, int32_t>* end() const { return data + count; }

    std::pair<Pos, int32_t>& operator[](size_t i) { return data[i]; }
    const std::pair<Pos, int32_t>& operator[](size_t i) const { return data[i]; }
    std::pair<Pos, int32_t>& front() { return data[0]; }

    void sort_descending() {
        std::sort(data, data + count,
            [](const auto& a, const auto& b) { return a.second > b.second; });
        top_score = count > 0 ? data[0].second : 0;
    }

    // Partial sort: only the first k elements are guaranteed sorted.
    // Useful when we only explore ~12 moves out of 100+.
    void partial_sort_descending(size_t k) {
        if (count <= 1) {
            top_score = count > 0 ? data[0].second : 0;
            return;
        }
        k = std::min(k, count);
        std::partial_sort(data, data + k, data + count,
            [](const auto& a, const auto& b) { return a.second > b.second; });
        top_score = data[0].second;
    }

    // Remove elements where pred returns true, preserving order.
    template<typename Pred>
    void remove_if(Pred pred) {
        size_t write = 0;
        for (size_t read = 0; read < count; ++read) {
            if (!pred(data[read])) {
                if (write != read) data[write] = data[read];
                ++write;
            }
        }
        count = write;
    }
};

// =========================================================================
// SharedState : state shared across all search threads (Lazy SMP)
// =========================================================================

struct SharedState {
    ZobristTable zobrist;
    AtomicTT tt;
    std::atomic<bool> stopped;

    explicit SharedState(size_t tt_mb)
        : zobrist(), tt(tt_mb), stopped(false) {}
};

// =========================================================================
// WorkerSearcher : per-thread search state
// =========================================================================
// Each Lazy SMP thread owns a WorkerSearcher with its own killer moves,
// history table, and node counter. They share the transposition table
// (AtomicTT) via SharedState.

struct WorkerSearcher {
    std::shared_ptr<SharedState> shared;
    uint64_t nodes;
    int8_t max_depth;
    Pos killer_moves[64][2];
    int32_t history[2][BOARD_SIZE][BOARD_SIZE];
    Pos countermove[2][BOARD_SIZE][BOARD_SIZE];
    Pos last_move_for_ordering = Pos::sentinel();
    std::optional<std::chrono::steady_clock::time_point> start_time;
    std::optional<std::chrono::milliseconds> time_limit;
    SearchStats stats;

    // Maximum quiescence depth (plies of forcing moves).
    static constexpr int8_t MAX_QS_DEPTH = 16;

    // Check if search should stop (time limit or global stop signal).
    bool is_stopped() const {
        return shared->stopped.load(std::memory_order_relaxed);
    }

    // Check time and set global stop if exceeded.
    bool check_time() const {
        if (shared->stopped.load(std::memory_order_relaxed)) {
            return true;
        }
        if (start_time.has_value() && time_limit.has_value()) {
            auto elapsed = std::chrono::steady_clock::now() - *start_time;
            if (elapsed >= *time_limit) {
                shared->stopped.store(true, std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

    // --- Search entry points ---

    // Iterative deepening with aspiration windows and time management.
    SearchResult search_iterative(
        const Board& board, Stone color,
        int8_t max_depth_arg, int8_t start_depth_offset);

    // Root-level PVS search.
    SearchResult search_root(
        Board& board, Stone color, int8_t depth,
        int32_t alpha, int32_t beta);

    // --- Core recursive search ---

    // Negamax alpha-beta with pruning (NMP, LMR, PVS, futility, etc.).
    int32_t alpha_beta(
        Board& board, Stone color, int8_t depth,
        int32_t alpha, int32_t beta, Pos last_move,
        uint64_t hash, bool allow_null);

    // Quiescence: search only forcing moves (fives, fours, capture-wins).
    int32_t quiescence(
        Board& board, Stone color, int32_t alpha, int32_t beta,
        Pos last_move, int8_t qs_depth, uint64_t hash);

    // Search break-capture moves against a breakable five.
    int32_t search_five_break(
        Board& board, Stone color, int8_t depth,
        int32_t alpha, int32_t beta,
        const std::vector<Pos>& five_positions, Stone five_color,
        uint64_t hash);

    // --- Move ordering ---

    // Generate candidate moves sorted by priority (partial sort).
    MoveList generate_moves_ordered(
        const Board& board, Stone color,
        Pos tt_move, int8_t depth,
        size_t sort_limit = MoveList::CAPACITY) const;

    // Score a single move for ordering (defense-first philosophy).
    int32_t score_move(
        const Board& board, Pos mov, Stone color,
        Pos tt_move, int8_t depth) const;

    // --- Static analysis helpers ---

    // Bidirectional line scan result for both colors at once.
    struct LineBothResult {
        int32_t mc, mo;
        bool m_gap;
        int32_t mc_consec;
        int32_t oc, oo;
        bool o_gap;
        int32_t oc_consec;
    };

    // Scan a line from pos in direction (dr,dc) for both colors.
    static LineBothResult count_line_both(
        const Bitboard& my_bb, const Bitboard& opp_bb,
        Pos pos, int8_t dr, int8_t dc);

    // Does placing at pos create a four (4 aligned, >= 1 open end)?
    static bool move_creates_four(const Board& board, Pos pos, Stone color);

    // Is the current side facing an immediate tactical threat?
    static bool is_threatened(const Board& board, Stone color, Pos last_move);
};

// Helper: compute child hash after placing a stone and executing captures.
// Used in search_root, alpha_beta, quiescence, search_five_break.
inline uint64_t compute_child_hash(
    const SharedState& shared, uint64_t parent_hash,
    const Board& board, Pos mov, Stone color,
    const CaptureInfo& cap_info) {

    uint64_t h = shared.zobrist.update_place(parent_hash, mov, color);
    for (uint8_t j = 0; j < cap_info.count; ++j) {
        h = shared.zobrist.update_capture(
            h, cap_info.positions[j], opponent(color));
    }
    if (cap_info.pairs > 0) {
        uint8_t new_count = board.captures(color);
        uint8_t old_count = new_count - cap_info.pairs;
        h = shared.zobrist.update_capture_count(
            h, color, old_count, new_count);
    }
    return h;
}

} // namespace gomoku
