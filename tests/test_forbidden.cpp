#include <catch2/catch_test_macros.hpp>
#include "gomoku/rules/forbidden.hpp"
#include "gomoku/rules/capture.hpp"

using namespace gomoku;

TEST_CASE("Forbidden: not double three on empty board", "[forbidden]") {
    Board board;
    REQUIRE_FALSE(is_double_three(board, Pos(9, 9), Stone::Black));
}

TEST_CASE("Forbidden: valid move on empty pos", "[forbidden]") {
    Board board;
    REQUIRE(is_valid_move(board, Pos(9, 9), Stone::Black));
}

TEST_CASE("Forbidden: invalid move on occupied pos", "[forbidden]") {
    Board board;
    board.place_stone(Pos(9, 9), Stone::Black);
    REQUIRE_FALSE(is_valid_move(board, Pos(9, 9), Stone::White));
}

TEST_CASE("Forbidden: free three consecutive horizontal", "[forbidden]") {
    Board board;
    // _ B _ B _  (place at 7 creates _ B B B _)
    board.place_stone(Pos(9, 6), Stone::Black);
    board.place_stone(Pos(9, 8), Stone::Black);

    uint8_t ft = count_free_threes(board, Pos(9, 7), Stone::Black);
    REQUIRE(ft == 1);
}

TEST_CASE("Forbidden: free three with gap", "[forbidden]") {
    Board board;
    // _ B B _ _  (place at 9 creates _ B B _ B _)
    board.place_stone(Pos(9, 6), Stone::Black);
    board.place_stone(Pos(9, 7), Stone::Black);

    uint8_t ft = count_free_threes(board, Pos(9, 9), Stone::Black);
    REQUIRE(ft == 1);
}

TEST_CASE("Forbidden: not free three when blocked", "[forbidden]") {
    Board board;
    // W B _ B _  (one end blocked by opponent)
    board.place_stone(Pos(9, 5), Stone::White);
    board.place_stone(Pos(9, 6), Stone::Black);
    board.place_stone(Pos(9, 8), Stone::Black);

    uint8_t ft = count_free_threes(board, Pos(9, 7), Stone::Black);
    REQUIRE(ft == 0);
}

TEST_CASE("Forbidden: double three cross pattern", "[forbidden]") {
    Board board;
    // Horizontal: _ B _ B _
    board.place_stone(Pos(9, 8), Stone::Black);
    board.place_stone(Pos(9, 10), Stone::Black);
    // Vertical: _ B _ B _
    board.place_stone(Pos(8, 9), Stone::Black);
    board.place_stone(Pos(10, 9), Stone::Black);

    REQUIRE(is_double_three(board, Pos(9, 9), Stone::Black));
    REQUIRE_FALSE(is_valid_move(board, Pos(9, 9), Stone::Black));
}

TEST_CASE("Forbidden: double three diagonal cross", "[forbidden]") {
    Board board;
    // Diagonal SE: B at (8,8), (10,10)
    board.place_stone(Pos(8, 8), Stone::Black);
    board.place_stone(Pos(10, 10), Stone::Black);
    // Diagonal SW: B at (8,10), (10,8)
    board.place_stone(Pos(8, 10), Stone::Black);
    board.place_stone(Pos(10, 8), Stone::Black);

    uint8_t ft = count_free_threes(board, Pos(9, 9), Stone::Black);
    REQUIRE(ft == 2);
    REQUIRE(is_double_three(board, Pos(9, 9), Stone::Black));
}

TEST_CASE("Forbidden: single free three allowed", "[forbidden]") {
    Board board;
    // Only horizontal: _ B _ B _
    board.place_stone(Pos(9, 8), Stone::Black);
    board.place_stone(Pos(9, 10), Stone::Black);

    uint8_t ft = count_free_threes(board, Pos(9, 9), Stone::Black);
    REQUIRE(ft == 1);
    REQUIRE_FALSE(is_double_three(board, Pos(9, 9), Stone::Black));
    REQUIRE(is_valid_move(board, Pos(9, 9), Stone::Black));
}

TEST_CASE("Forbidden: double three with capture allowed", "[forbidden]") {
    Board board;
    // Create capture pattern + potential free-threes
    board.place_stone(Pos(9, 5), Stone::Black);
    board.place_stone(Pos(9, 7), Stone::White);
    board.place_stone(Pos(9, 8), Stone::White);
    board.place_stone(Pos(9, 9), Stone::Black);

    board.place_stone(Pos(8, 6), Stone::Black);
    board.place_stone(Pos(10, 6), Stone::Black);
    board.place_stone(Pos(8, 8), Stone::Black);
    board.place_stone(Pos(10, 4), Stone::Black);

    // Verify capture exists at (9,6)
    auto captures = get_captured_positions(board, Pos(9, 6), Stone::Black);
    REQUIRE(captures.size() == 2);
    // Even if this creates free-threes, it's allowed because of capture
    REQUIRE_FALSE(is_double_three(board, Pos(9, 6), Stone::Black));
}

TEST_CASE("Forbidden: four stones not free three", "[forbidden]") {
    Board board;
    // 4 stones in a row is NOT a free-three
    board.place_stone(Pos(9, 6), Stone::Black);
    board.place_stone(Pos(9, 7), Stone::Black);
    board.place_stone(Pos(9, 9), Stone::Black);

    // Place at 8 creates 4 in a row
    uint8_t ft = count_free_threes(board, Pos(9, 8), Stone::Black);
    REQUIRE(ft == 0);
}

TEST_CASE("Forbidden: two stones not free three", "[forbidden]") {
    Board board;
    board.place_stone(Pos(9, 8), Stone::Black);

    uint8_t ft = count_free_threes(board, Pos(9, 9), Stone::Black);
    REQUIRE(ft == 0);
}

TEST_CASE("Forbidden: edge position blocked by edge", "[forbidden]") {
    Board board;
    // At edge: B B B _ but left side is at col -1 (board boundary)
    board.place_stone(Pos(0, 0), Stone::Black);
    board.place_stone(Pos(0, 2), Stone::Black);

    uint8_t ft = count_free_threes(board, Pos(0, 1), Stone::Black);
    REQUIRE(ft == 0);
}

TEST_CASE("Forbidden: edge with space is free three", "[forbidden]") {
    Board board;
    // _ B _ B _  at row 0, cols 0-4
    board.place_stone(Pos(0, 1), Stone::Black);
    board.place_stone(Pos(0, 3), Stone::Black);

    // Test scan_line directly
    Board temp = board;
    temp.place_stone(Pos(0, 2), Stone::Black);
    LinePattern pattern = scan_line(temp, Pos(0, 2), Stone::Black, 0, 1);

    REQUIRE(pattern.stone_count == 3);
    REQUIRE(pattern.open_ends == 2);
    REQUIRE(pattern.span == 3);

    uint8_t ft = count_free_threes(board, Pos(0, 2), Stone::Black);
    REQUIRE(ft == 1);
}

TEST_CASE("Forbidden: free three pattern scan", "[forbidden]") {
    Board board;
    // _ B _ B _  (place at 7 creates consecutive 3: _ B B B _)
    board.place_stone(Pos(9, 6), Stone::Black);
    board.place_stone(Pos(9, 8), Stone::Black);

    Board temp = board;
    temp.place_stone(Pos(9, 7), Stone::Black);
    LinePattern pattern = scan_line(temp, Pos(9, 7), Stone::Black, 0, 1);

    REQUIRE(pattern.stone_count == 3);
    REQUIRE(pattern.open_ends == 2);
    REQUIRE(pattern.span == 3);
}

TEST_CASE("Forbidden: is_free_three logic", "[forbidden]") {
    // Consecutive pattern: _OOO_
    LinePattern consecutive;
    consecutive.stones[0] = -1; consecutive.stones[1] = 0; consecutive.stones[2] = 1;
    consecutive.stone_count = 3;
    consecutive.open_ends = 2;
    consecutive.span = 3;
    REQUIRE(is_free_three(consecutive));

    // Gapped pattern: _OO_O_ or _O_OO_
    LinePattern gapped;
    gapped.stones[0] = -1; gapped.stones[1] = 0; gapped.stones[2] = 2;
    gapped.stone_count = 3;
    gapped.open_ends = 2;
    gapped.span = 4;
    REQUIRE(is_free_three(gapped));

    // Blocked pattern
    LinePattern blocked;
    blocked.stones[0] = -1; blocked.stones[1] = 0; blocked.stones[2] = 1;
    blocked.stone_count = 3;
    blocked.open_ends = 1;
    blocked.span = 3;
    REQUIRE_FALSE(is_free_three(blocked));

    // 4 stones (not free-three)
    LinePattern four;
    four.stones[0] = -1; four.stones[1] = 0; four.stones[2] = 1; four.stones[3] = 2;
    four.stone_count = 4;
    four.open_ends = 2;
    four.span = 4;
    REQUIRE_FALSE(is_free_three(four));

    // Too spread pattern
    LinePattern spread;
    spread.stones[0] = -2; spread.stones[1] = 0; spread.stones[2] = 3;
    spread.stone_count = 3;
    spread.open_ends = 2;
    spread.span = 6;
    REQUIRE_FALSE(is_free_three(spread));
}

// Regression test: Game 1 Move #23 (H10) was a double-three that wasn't detected.
// Horizontal: F10-G10-H10 = _BBB_ (free-three) -- BUT K10 exists at +2 via gap,
// making scan_line see 4 stones instead of 3.
// Vertical: H10-H11-H12 = _BBB_ (free-three, correctly detected).
// With the consecutive fallback, both free-threes are now detected.
TEST_CASE("Forbidden: regression - double three with gap-connected stone", "[forbidden]") {
    Board board;
    // Black stones
    board.place_stone(Pos(9, 9), Stone::Black);    // K10
    board.place_stone(Pos(11, 7), Stone::Black);   // H12
    board.place_stone(Pos(9, 6), Stone::Black);    // G10
    board.place_stone(Pos(9, 5), Stone::Black);    // F10
    board.place_stone(Pos(6, 8), Stone::Black);    // J7
    board.place_stone(Pos(10, 7), Stone::Black);   // H11
    board.place_stone(Pos(7, 10), Stone::Black);   // L8

    // White stones
    board.place_stone(Pos(10, 11), Stone::White);  // M11
    board.place_stone(Pos(13, 9), Stone::White);   // K14
    board.place_stone(Pos(8, 6), Stone::White);    // G9
    board.place_stone(Pos(11, 8), Stone::White);   // J12
    board.place_stone(Pos(10, 9), Stone::White);   // K11

    // H10 = Pos(9, 7) -- should be forbidden double-three
    Pos pos(9, 7);
    uint8_t ft = count_free_threes(board, pos, Stone::Black);
    REQUIRE(ft == 2);
    REQUIRE(is_double_three(board, pos, Stone::Black));
    REQUIRE_FALSE(is_valid_move(board, pos, Stone::Black));
}

// Verify consecutive fallback doesn't produce false positives
// when the consecutive subset is blocked or has only 2 stones.
TEST_CASE("Forbidden: consecutive fallback no false positive", "[forbidden]") {
    Board board;
    // W B B _ B _  (left end blocked by opponent)
    board.place_stone(Pos(9, 4), Stone::White);
    board.place_stone(Pos(9, 5), Stone::Black);
    board.place_stone(Pos(9, 7), Stone::Black);

    uint8_t ft = count_free_threes(board, Pos(9, 6), Stone::Black);
    REQUIRE(ft == 0);
}

TEST_CASE("Forbidden: triple free three", "[forbidden]") {
    Board board;
    // Create 3 free-threes at center (horizontal, vertical, diagonal)
    // Horizontal
    board.place_stone(Pos(9, 8), Stone::Black);
    board.place_stone(Pos(9, 10), Stone::Black);
    // Vertical
    board.place_stone(Pos(8, 9), Stone::Black);
    board.place_stone(Pos(10, 9), Stone::Black);
    // Diagonal SE
    board.place_stone(Pos(8, 8), Stone::Black);
    board.place_stone(Pos(10, 10), Stone::Black);

    uint8_t ft = count_free_threes(board, Pos(9, 9), Stone::Black);
    REQUIRE(ft >= 2);
    REQUIRE(is_double_three(board, Pos(9, 9), Stone::Black));
}
