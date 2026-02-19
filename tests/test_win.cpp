#include <catch2/catch_test_macros.hpp>
#include "gomoku/rules/win.hpp"
#include <optional>

using namespace gomoku;

TEST_CASE("Win: five in row horizontal", "[win]") {
    Board board;
    for (uint8_t i = 0; i < 5; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }
    REQUIRE(has_five_in_row(board, Stone::Black));
    REQUIRE_FALSE(has_five_in_row(board, Stone::White));
}

TEST_CASE("Win: five in row vertical", "[win]") {
    Board board;
    for (uint8_t i = 0; i < 5; ++i) {
        board.place_stone(Pos(i, 9), Stone::Black);
    }
    REQUIRE(has_five_in_row(board, Stone::Black));
}

TEST_CASE("Win: five in row diagonal", "[win]") {
    Board board;
    for (uint8_t i = 0; i < 5; ++i) {
        board.place_stone(Pos(i, i), Stone::White);
    }
    REQUIRE(has_five_in_row(board, Stone::White));
}

TEST_CASE("Win: six in row also wins", "[win]") {
    Board board;
    for (uint8_t i = 0; i < 6; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }
    REQUIRE(has_five_in_row(board, Stone::Black));
}

TEST_CASE("Win: four in row not win", "[win]") {
    Board board;
    for (uint8_t i = 0; i < 4; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }
    REQUIRE_FALSE(has_five_in_row(board, Stone::Black));
}

TEST_CASE("Win: capture win", "[win]") {
    Board board;
    board.add_captures(Stone::Black, 5);
    REQUIRE(check_winner(board, Stone::Black) == Stone::Black);
}

TEST_CASE("Win: capture win white", "[win]") {
    Board board;
    board.add_captures(Stone::White, 5);
    REQUIRE(check_winner(board, Stone::White) == Stone::White);
}

TEST_CASE("Win: breakable five", "[win]") {
    // Five at row 9, cols 5-9 (Black), with White bracket at (7,7)
    // and extra Black at (8,7). White can capture (9,7)+(8,7) via (10,7).
    Board board;
    board.place_stone(Pos(7, 7), Stone::White);
    for (uint8_t i = 5; i < 10; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }
    board.place_stone(Pos(8, 7), Stone::Black);

    auto five = find_five_positions(board, Stone::Black);
    REQUIRE(five.has_value());
    // STATIC check: the five IS physically breakable
    REQUIRE(can_break_five_by_capture(board, five.value(), Stone::Black));
}

TEST_CASE("Win: unbreakable five wins", "[win]") {
    Board board;
    // 5 blacks with no capture threat
    for (uint8_t i = 5; i < 10; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }
    REQUIRE(check_winner(board, Stone::Black) == Stone::Black);
}

TEST_CASE("Win: no winner", "[win]") {
    Board board;
    REQUIRE(check_winner(board, Stone::Black) == std::nullopt);
}

TEST_CASE("Win: diagonal SW five", "[win]") {
    Board board;
    // Diagonal from (4, 8) to (8, 4)
    for (uint8_t i = 0; i < 5; ++i) {
        board.place_stone(Pos(4 + i, 8 - i), Stone::White);
    }
    REQUIRE(has_five_in_row(board, Stone::White));
    REQUIRE(check_winner(board, Stone::White) == Stone::White);
}

TEST_CASE("Win: five at board edge", "[win]") {
    Board board;
    // 5 blacks at bottom edge
    for (uint8_t i = 0; i < 5; ++i) {
        board.place_stone(Pos(18, i), Stone::Black);
    }
    REQUIRE(has_five_in_row(board, Stone::Black));
    REQUIRE(check_winner(board, Stone::Black) == Stone::Black);
}

TEST_CASE("Win: five at corner", "[win]") {
    Board board;
    // Diagonal from (14, 14) to (18, 18)
    for (uint8_t i = 0; i < 5; ++i) {
        board.place_stone(Pos(14 + i, 14 + i), Stone::White);
    }
    REQUIRE(has_five_in_row(board, Stone::White));
    REQUIRE(check_winner(board, Stone::White) == Stone::White);
}

TEST_CASE("Win: empty not five", "[win]") {
    Board board;
    REQUIRE_FALSE(has_five_in_row(board, Stone::Black));
    REQUIRE_FALSE(has_five_in_row(board, Stone::White));
    REQUIRE_FALSE(find_five_positions(board, Stone::Empty).has_value());
}

TEST_CASE("Win: capture beats five", "[win]") {
    // If both have winning conditions, capture is checked first
    Board board;
    board.add_captures(Stone::White, 5);
    for (uint8_t i = 0; i < 5; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }
    // White wins by capture (checked first), regardless of who moved last
    REQUIRE(check_winner(board, Stone::Black) == Stone::White);
    REQUIRE(check_winner(board, Stone::White) == Stone::White);
}

TEST_CASE("Win: breakable five - temporal rule", "[win]") {
    // Black has a breakable five at row 9, cols 5-9.
    // White stone at (7,7), Black stone at (8,7) → White can capture (9,7)+(8,7) via (10,7).
    Board board;
    board.place_stone(Pos(7, 7), Stone::White);
    for (uint8_t i = 5; i < 10; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }
    board.place_stone(Pos(8, 7), Stone::Black);

    // Black just formed the five → breakable → game continues (White gets a chance)
    REQUIRE(check_winner(board, Stone::Black) == std::nullopt);

    // But if White moves next and doesn't break it → Black wins
    // Simulate: White plays somewhere irrelevant
    board.place_stone(Pos(0, 0), Stone::White);
    REQUIRE(check_winner(board, Stone::White) == Stone::Black);
}

TEST_CASE("Win: opponent five not broken means they win", "[win]") {
    // Black has an unbreakable five. White just moved (didn't break it) → Black wins.
    Board board;
    for (uint8_t i = 0; i < 5; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }
    // White just played somewhere else
    board.place_stone(Pos(0, 0), Stone::White);
    REQUIRE(check_winner(board, Stone::White) == Stone::Black);
}
