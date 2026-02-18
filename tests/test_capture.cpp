#include <catch2/catch_test_macros.hpp>
#include "gomoku/rules/capture.hpp"
#include <algorithm>

using namespace gomoku;

TEST_CASE("Capture: horizontal", "[capture]") {
    Board board;
    // B _ W W B  at row 9, cols 5-9
    board.place_stone(Pos(9, 5), Stone::Black);
    board.place_stone(Pos(9, 7), Stone::White);
    board.place_stone(Pos(9, 8), Stone::White);
    board.place_stone(Pos(9, 9), Stone::Black);

    auto captured = get_captured_positions(board, Pos(9, 6), Stone::Black);
    REQUIRE(captured.size() == 2);
    REQUIRE(std::find(captured.begin(), captured.end(), Pos(9, 7)) != captured.end());
    REQUIRE(std::find(captured.begin(), captured.end(), Pos(9, 8)) != captured.end());
}

TEST_CASE("Capture: vertical", "[capture]") {
    Board board;
    board.place_stone(Pos(5, 9), Stone::Black);
    board.place_stone(Pos(7, 9), Stone::White);
    board.place_stone(Pos(8, 9), Stone::White);
    board.place_stone(Pos(9, 9), Stone::Black);

    auto captured = get_captured_positions(board, Pos(6, 9), Stone::Black);
    REQUIRE(captured.size() == 2);
    REQUIRE(std::find(captured.begin(), captured.end(), Pos(7, 9)) != captured.end());
    REQUIRE(std::find(captured.begin(), captured.end(), Pos(8, 9)) != captured.end());
}

TEST_CASE("Capture: diagonal SE", "[capture]") {
    Board board;
    board.place_stone(Pos(5, 5), Stone::Black);
    board.place_stone(Pos(7, 7), Stone::White);
    board.place_stone(Pos(8, 8), Stone::White);
    board.place_stone(Pos(9, 9), Stone::Black);

    auto captured = get_captured_positions(board, Pos(6, 6), Stone::Black);
    REQUIRE(captured.size() == 2);
    REQUIRE(std::find(captured.begin(), captured.end(), Pos(7, 7)) != captured.end());
    REQUIRE(std::find(captured.begin(), captured.end(), Pos(8, 8)) != captured.end());
}

TEST_CASE("Capture: diagonal SW", "[capture]") {
    Board board;
    board.place_stone(Pos(5, 9), Stone::Black);
    board.place_stone(Pos(7, 7), Stone::White);
    board.place_stone(Pos(8, 6), Stone::White);
    board.place_stone(Pos(9, 5), Stone::Black);

    auto captured = get_captured_positions(board, Pos(6, 8), Stone::Black);
    REQUIRE(captured.size() == 2);
    REQUIRE(std::find(captured.begin(), captured.end(), Pos(7, 7)) != captured.end());
    REQUIRE(std::find(captured.begin(), captured.end(), Pos(8, 6)) != captured.end());
}

TEST_CASE("Capture: no capture with single stone", "[capture]") {
    Board board;
    // B _ W B  (only 1 white stone - no capture)
    board.place_stone(Pos(9, 5), Stone::Black);
    board.place_stone(Pos(9, 7), Stone::White);
    board.place_stone(Pos(9, 8), Stone::Black);

    auto captured = get_captured_positions(board, Pos(9, 6), Stone::Black);
    REQUIRE(captured.size() == 0);
}

TEST_CASE("Capture: no capture with three stones", "[capture]") {
    Board board;
    // B _ W W W B  (3 white stones - must be exactly 2)
    board.place_stone(Pos(9, 5), Stone::Black);
    board.place_stone(Pos(9, 7), Stone::White);
    board.place_stone(Pos(9, 8), Stone::White);
    board.place_stone(Pos(9, 9), Stone::White);
    board.place_stone(Pos(9, 10), Stone::Black);

    auto captured = get_captured_positions(board, Pos(9, 6), Stone::Black);
    REQUIRE(captured.size() == 0);
}

TEST_CASE("Capture: execute capture", "[capture]") {
    Board board;
    board.place_stone(Pos(9, 5), Stone::Black);
    board.place_stone(Pos(9, 7), Stone::White);
    board.place_stone(Pos(9, 8), Stone::White);
    board.place_stone(Pos(9, 9), Stone::Black);

    board.place_stone(Pos(9, 6), Stone::Black);
    auto captured = execute_captures(board, Pos(9, 6), Stone::Black);

    REQUIRE(captured.size() == 2);
    REQUIRE(board.captures(Stone::Black) == 1);  // 1 pair
    REQUIRE(board.is_empty(Pos(9, 7)));
    REQUIRE(board.is_empty(Pos(9, 8)));
}

TEST_CASE("Capture: multiple captures same move", "[capture]") {
    Board board;
    // B W W _ W W B  at row 9, cols 3-9
    board.place_stone(Pos(9, 3), Stone::Black);
    board.place_stone(Pos(9, 4), Stone::White);
    board.place_stone(Pos(9, 5), Stone::White);
    board.place_stone(Pos(9, 7), Stone::White);
    board.place_stone(Pos(9, 8), Stone::White);
    board.place_stone(Pos(9, 9), Stone::Black);

    board.place_stone(Pos(9, 6), Stone::Black);
    auto captured = execute_captures(board, Pos(9, 6), Stone::Black);

    REQUIRE(captured.size() == 4);  // 2 pairs = 4 stones
    REQUIRE(board.captures(Stone::Black) == 2);
}

TEST_CASE("Capture: has_capture", "[capture]") {
    Board board;
    board.place_stone(Pos(9, 5), Stone::Black);
    board.place_stone(Pos(9, 7), Stone::White);
    board.place_stone(Pos(9, 8), Stone::White);
    board.place_stone(Pos(9, 9), Stone::Black);

    REQUIRE(has_capture(board, Pos(9, 6), Stone::Black));
    REQUIRE_FALSE(has_capture(board, Pos(9, 6), Stone::White));
    REQUIRE_FALSE(has_capture(board, Pos(0, 0), Stone::Black));
}

TEST_CASE("Capture: count captures", "[capture]") {
    Board board;
    // Setup for 2 pairs capture
    board.place_stone(Pos(9, 3), Stone::Black);
    board.place_stone(Pos(9, 4), Stone::White);
    board.place_stone(Pos(9, 5), Stone::White);
    board.place_stone(Pos(9, 7), Stone::White);
    board.place_stone(Pos(9, 8), Stone::White);
    board.place_stone(Pos(9, 9), Stone::Black);

    REQUIRE(count_captures(board, Pos(9, 6), Stone::Black) == 2);
}

TEST_CASE("Capture: white captures black", "[capture]") {
    Board board;
    // W _ B B W
    board.place_stone(Pos(5, 5), Stone::White);
    board.place_stone(Pos(5, 7), Stone::Black);
    board.place_stone(Pos(5, 8), Stone::Black);
    board.place_stone(Pos(5, 9), Stone::White);

    board.place_stone(Pos(5, 6), Stone::White);
    auto captured = execute_captures(board, Pos(5, 6), Stone::White);

    REQUIRE(captured.size() == 2);
    REQUIRE(board.captures(Stone::White) == 1);
    REQUIRE(board.is_empty(Pos(5, 7)));
    REQUIRE(board.is_empty(Pos(5, 8)));
}

TEST_CASE("Capture: at board edge", "[capture]") {
    Board board;
    // Edge: B _ W W B starting from column 0
    board.place_stone(Pos(0, 0), Stone::Black);
    board.place_stone(Pos(0, 2), Stone::White);
    board.place_stone(Pos(0, 3), Stone::White);
    board.place_stone(Pos(0, 4), Stone::Black);

    auto captured = get_captured_positions(board, Pos(0, 1), Stone::Black);
    REQUIRE(captured.size() == 2);
}

TEST_CASE("Capture: no capture out of bounds", "[capture]") {
    Board board;
    board.place_stone(Pos(0, 0), Stone::Black);
    board.place_stone(Pos(0, 1), Stone::White);

    // Near edge - should not crash
    auto captured = get_captured_positions(board, Pos(0, 2), Stone::Black);
    REQUIRE(captured.size() == 0);
}

TEST_CASE("Capture: cross capture (4 pairs)", "[capture]") {
    Board board;
    Pos center(9, 9);

    // Horizontal: both directions from center
    board.place_stone(Pos(9, 6), Stone::Black);
    board.place_stone(Pos(9, 7), Stone::White);
    board.place_stone(Pos(9, 8), Stone::White);
    board.place_stone(Pos(9, 10), Stone::White);
    board.place_stone(Pos(9, 11), Stone::White);
    board.place_stone(Pos(9, 12), Stone::Black);

    // Vertical: both directions from center
    board.place_stone(Pos(6, 9), Stone::Black);
    board.place_stone(Pos(7, 9), Stone::White);
    board.place_stone(Pos(8, 9), Stone::White);
    board.place_stone(Pos(10, 9), Stone::White);
    board.place_stone(Pos(11, 9), Stone::White);
    board.place_stone(Pos(12, 9), Stone::Black);

    board.place_stone(center, Stone::Black);
    auto captured = execute_captures(board, center, Stone::Black);

    // Should capture 4 pairs = 8 stones
    REQUIRE(captured.size() == 8);
    REQUIRE(board.captures(Stone::Black) == 4);
}
