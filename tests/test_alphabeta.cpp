#include <catch2/catch_test_macros.hpp>
#include "gomoku/search/alphabeta.hpp"
#include "gomoku/search/tt.hpp"
#include "gomoku/board/board.hpp"
#include "gomoku/board/types.hpp"
#include "gomoku/eval/patterns.hpp"
#include "gomoku/rules/win.hpp"

using namespace gomoku;

// =========================================================================
// Basic search tests
// =========================================================================

TEST_CASE("search_empty_board", "[alphabeta]") {
    Searcher searcher(16);
    Board board;

    SearchResult result = searcher.search(board, Stone::Black, 4);
    REQUIRE(!result.best_move.is_sentinel());
    REQUIRE(result.best_move == Pos(9, 9));
}

TEST_CASE("search_finds_winning_move", "[alphabeta]") {
    Searcher searcher(16);
    Board board;

    for (uint8_t i = 0; i < 4; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }

    SearchResult result = searcher.search(board, Stone::Black, 2);
    REQUIRE(result.best_move == Pos(9, 4));
}

TEST_CASE("search_blocks_opponent_win", "[alphabeta]") {
    Searcher searcher(16);
    Board board;

    for (uint8_t i = 0; i < 4; ++i) {
        board.place_stone(Pos(9, i), Stone::White);
    }
    board.place_stone(Pos(10, 0), Stone::Black);

    SearchResult result = searcher.search(board, Stone::Black, 4);
    REQUIRE(result.best_move == Pos(9, 4));
}

TEST_CASE("iterative_deepening_improves", "[alphabeta]") {
    Searcher searcher(16);
    Board board;

    board.place_stone(Pos(9, 9), Stone::Black);
    board.place_stone(Pos(9, 10), Stone::White);
    board.place_stone(Pos(9, 8), Stone::Black);
    board.place_stone(Pos(10, 9), Stone::White);
    board.place_stone(Pos(8, 9), Stone::Black);

    SearchResult result = searcher.search(board, Stone::White, 2);
    REQUIRE(result.depth >= 1);
    REQUIRE(result.nodes > 0);
}

TEST_CASE("search_with_captures", "[alphabeta]") {
    Searcher searcher(16);
    Board board;

    board.place_stone(Pos(9, 5), Stone::Black);
    board.place_stone(Pos(9, 7), Stone::White);
    board.place_stone(Pos(9, 8), Stone::White);
    board.place_stone(Pos(9, 9), Stone::Black);

    SearchResult result = searcher.search(board, Stone::Black, 4);
    REQUIRE(!result.best_move.is_sentinel());
    Pos mov = result.best_move;
    REQUIRE(mov.row >= 7);
    REQUIRE(mov.row <= 11);
    REQUIRE(mov.col >= 3);
    REQUIRE(mov.col <= 11);
}

// =========================================================================
// TT stats and clear tests
// =========================================================================

TEST_CASE("tt_stats_after_search", "[alphabeta]") {
    Searcher searcher(16);
    Board board;

    board.place_stone(Pos(9, 9), Stone::Black);

    searcher.search(board, Stone::White, 4);

    TTStats stats = searcher.tt_stats();
    REQUIRE(stats.used > 0);
}

TEST_CASE("clear_tt", "[alphabeta]") {
    Searcher searcher(16);
    Board board;

    board.place_stone(Pos(9, 9), Stone::Black);
    searcher.search(board, Stone::White, 4);

    TTStats stats_before = searcher.tt_stats();
    REQUIRE(stats_before.used > 0);

    searcher.clear_tt();

    TTStats stats_after = searcher.tt_stats();
    REQUIRE(stats_after.used == 0);
}

// =========================================================================
// Score detection tests
// =========================================================================

TEST_CASE("search_winning_score", "[alphabeta]") {
    Searcher searcher(16);
    Board board;

    for (uint8_t i = 0; i < 4; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }

    SearchResult result = searcher.search(board, Stone::Black, 2);
    REQUIRE(result.score >= PatternScore::FIVE - 100);
}

TEST_CASE("search_losing_score", "[alphabeta]") {
    Searcher searcher(16);
    Board board;

    for (uint8_t i = 0; i < 4; ++i) {
        board.place_stone(Pos(9, i), Stone::White);
    }
    board.place_stone(Pos(0, 0), Stone::Black);

    SearchResult result = searcher.search(board, Stone::Black, 2);
    REQUIRE(result.best_move == Pos(9, 4));
}

// =========================================================================
// Node count and consistency tests
// =========================================================================

TEST_CASE("search_node_count", "[alphabeta]") {
    Searcher searcher(16);
    Board board;

    SearchResult result = searcher.search(board, Stone::Black, 2);
    REQUIRE(result.nodes >= 1);
}

TEST_CASE("search_multiple_times", "[alphabeta]") {
    Searcher searcher(16);
    Board board;

    board.place_stone(Pos(9, 9), Stone::Black);

    SearchResult result1 = searcher.search(board, Stone::White, 4);
    REQUIRE(!result1.best_move.is_sentinel());

    SearchResult result2 = searcher.search(board, Stone::White, 4);
    REQUIRE(!result2.best_move.is_sentinel());

    REQUIRE((result2.nodes <= result1.nodes || result2.nodes < result1.nodes + 500));

    Pos m1 = result1.best_move;
    Pos m2 = result2.best_move;
    REQUIRE(std::abs(int(m1.row) - 9) <= 2);
    REQUIRE(std::abs(int(m1.col) - 9) <= 2);
    REQUIRE(std::abs(int(m2.row) - 9) <= 2);
    REQUIRE(std::abs(int(m2.col) - 9) <= 2);
}

// =========================================================================
// Parallel timed search
// =========================================================================

TEST_CASE("parallel_search_timed", "[alphabeta]") {
    Searcher searcher(16, 4);
    Board board;

    board.place_stone(Pos(9, 9), Stone::Black);
    board.place_stone(Pos(9, 10), Stone::White);
    board.place_stone(Pos(10, 9), Stone::Black);
    board.place_stone(Pos(8, 10), Stone::White);

    SearchResult result = searcher.search_timed(board, Stone::Black, 12, 500);
    REQUIRE(!result.best_move.is_sentinel());
    REQUIRE(result.depth >= 4);
    REQUIRE(result.nodes > 0);
}

// =========================================================================
// Quiescence tests
// =========================================================================

TEST_CASE("quiescence_detects_open_four", "[alphabeta]") {
    Searcher searcher(16, 1);
    Board board;

    // Black open three: _BBB_ at row 9
    board.place_stone(Pos(9, 8), Stone::Black);
    board.place_stone(Pos(9, 9), Stone::Black);
    board.place_stone(Pos(9, 10), Stone::Black);
    // White stones far away
    board.place_stone(Pos(0, 0), Stone::White);
    board.place_stone(Pos(0, 1), Stone::White);

    // Shallow search should still find the winning continuation
    SearchResult result = searcher.search(board, Stone::Black, 2);
    REQUIRE(result.score > PatternScore::OPEN_FOUR);
}

TEST_CASE("quiescence_four_threat_win", "[alphabeta]") {
    Searcher searcher(16, 1);
    Board board;

    // Black has OOOO_ (closed four) -> extending to five is forced
    board.place_stone(Pos(9, 7), Stone::Black);
    board.place_stone(Pos(9, 8), Stone::Black);
    board.place_stone(Pos(9, 9), Stone::Black);
    board.place_stone(Pos(9, 10), Stone::Black);
    // White blocks one side
    board.place_stone(Pos(9, 6), Stone::White);
    board.place_stone(Pos(0, 0), Stone::White);

    SearchResult result = searcher.search(board, Stone::Black, 1);
    REQUIRE(result.best_move == Pos(9, 11));
    REQUIRE(result.score >= PatternScore::FIVE - 100);
}

// =========================================================================
// Existing five detection
// =========================================================================

TEST_CASE("search_detects_existing_five", "[alphabeta]") {
    Searcher searcher(16);
    Board board;

    // Black five: (7,7)-(8,8)-(9,9)-(10,10)-(11,11)
    board.place_stone(Pos(7, 7), Stone::Black);
    board.place_stone(Pos(8, 8), Stone::Black);
    board.place_stone(Pos(9, 9), Stone::Black);
    board.place_stone(Pos(10, 10), Stone::Black);
    board.place_stone(Pos(11, 11), Stone::Black);

    // White stones far away (five is NOT breakable)
    board.place_stone(Pos(0, 0), Stone::White);
    board.place_stone(Pos(0, 1), Stone::White);
    board.place_stone(Pos(0, 2), Stone::White);
    board.place_stone(Pos(1, 0), Stone::White);
    board.place_stone(Pos(1, 1), Stone::White);

    // Verify Black has five
    auto five = find_five_positions(board, Stone::Black);
    REQUIRE(five.has_value());

    // White to move -- Black already has five on the board.
    SearchResult result = searcher.search(board, Stone::White, 4);

    // White should see this as a losing position
    REQUIRE(result.score <= -(PatternScore::FIVE - 100));
}
