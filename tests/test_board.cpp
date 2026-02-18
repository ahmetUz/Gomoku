#include <catch2/catch_test_macros.hpp>
#include "gomoku/board/board.hpp"
#include "gomoku/board/types.hpp"

using namespace gomoku;

// --- Stone and Pos tests ---

TEST_CASE("Stone: opponent", "[types]") {
    REQUIRE(opponent(Stone::Black) == Stone::White);
    REQUIRE(opponent(Stone::White) == Stone::Black);
    REQUIRE(opponent(Stone::Empty) == Stone::Empty);
}

TEST_CASE("Pos: new", "[types]") {
    Pos pos(9, 9);
    REQUIRE(pos.row == 9);
    REQUIRE(pos.col == 9);
}

TEST_CASE("Pos: conversion", "[types]") {
    Pos pos(9, 9); // Center
    REQUIRE(pos.to_index() == 9 * 19 + 9);
    REQUIRE(pos.to_index() == 180);

    Pos pos2 = Pos::from_index(180);
    REQUIRE(pos2.row == 9);
    REQUIRE(pos2.col == 9);
}

TEST_CASE("Pos: validity", "[types]") {
    REQUIRE(Pos::is_valid(0, 0));
    REQUIRE(Pos::is_valid(18, 18));
    REQUIRE(Pos::is_valid(9, 9));
    REQUIRE_FALSE(Pos::is_valid(-1, 0));
    REQUIRE_FALSE(Pos::is_valid(0, -1));
    REQUIRE_FALSE(Pos::is_valid(19, 0));
    REQUIRE_FALSE(Pos::is_valid(0, 19));
}

TEST_CASE("Board constants", "[types]") {
    REQUIRE(BOARD_SIZE == 19);
    REQUIRE(TOTAL_CELLS == 361);
}

TEST_CASE("Pos: ordering", "[types]") {
    Pos pos1(0, 0);
    Pos pos2(0, 1);
    Pos pos3(1, 0);

    REQUIRE(pos1 < pos2);
    REQUIRE(pos2 < pos3);
    REQUIRE(pos1 < pos3);
}

TEST_CASE("Pos: corner indices", "[types]") {
    REQUIRE(Pos(0, 0).to_index() == 0);      // Top-left
    REQUIRE(Pos(0, 18).to_index() == 18);     // Top-right
    REQUIRE(Pos(18, 0).to_index() == 342);    // Bottom-left
    REQUIRE(Pos(18, 18).to_index() == 360);   // Bottom-right
}

// --- Board tests ---

TEST_CASE("Board: new board is empty", "[board]") {
    Board board;
    REQUIRE(board.stone_count() == 0);
    REQUIRE(board.is_board_empty());
}

TEST_CASE("Board: place and get", "[board]") {
    Board board;
    Pos pos(9, 9);

    REQUIRE(board.get(pos) == Stone::Empty);
    board.place_stone(pos, Stone::Black);
    REQUIRE(board.get(pos) == Stone::Black);

    board.remove_stone(pos);
    REQUIRE(board.get(pos) == Stone::Empty);
}

TEST_CASE("Board: captures", "[board]") {
    Board board;
    REQUIRE(board.captures(Stone::Black) == 0);

    board.add_captures(Stone::Black, 2);
    REQUIRE(board.captures(Stone::Black) == 2);
    REQUIRE(board.captures(Stone::White) == 0);
}

TEST_CASE("Board: stone count", "[board]") {
    Board board;
    board.place_stone(Pos(0, 0), Stone::Black);
    board.place_stone(Pos(1, 1), Stone::White);
    board.place_stone(Pos(2, 2), Stone::Black);

    REQUIRE(board.stone_count() == 3);
    REQUIRE_FALSE(board.is_board_empty());
}

TEST_CASE("Board: stones() returns correct bitboard pointers", "[board]") {
    Board board;
    board.place_stone(Pos(5, 5), Stone::Black);

    const Bitboard* bb = board.stones(Stone::Black);
    REQUIRE(bb != nullptr);
    REQUIRE(bb->get(Pos(5, 5)));

    REQUIRE(board.stones(Stone::Empty) == nullptr);
}

TEST_CASE("Pos: sentinel", "[types]") {
    Pos s = Pos::sentinel();
    REQUIRE(s.is_sentinel());
    REQUIRE(s.row == 255);

    Pos normal(9, 9);
    REQUIRE_FALSE(normal.is_sentinel());
}
