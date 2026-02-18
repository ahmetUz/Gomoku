#include <catch2/catch_test_macros.hpp>
#include "gomoku/eval/heuristic.hpp"
#include "gomoku/eval/patterns.hpp"

using namespace gomoku;

TEST_CASE("Heuristic: empty board", "[heuristic]") {
    Board board;
    REQUIRE(evaluate(board, Stone::Black) == 0);
}

TEST_CASE("Heuristic: center bonus", "[heuristic]") {
    Board board;
    board.place_stone(Pos(9, 9), Stone::Black);

    int score = evaluate(board, Stone::Black);
    REQUIRE(score > 0);
}

TEST_CASE("Heuristic: corner less valuable", "[heuristic]") {
    Board board_center;
    board_center.place_stone(Pos(9, 9), Stone::Black);

    Board board_corner;
    board_corner.place_stone(Pos(0, 0), Stone::Black);

    int center_score = evaluate(board_center, Stone::Black);
    int corner_score = evaluate(board_corner, Stone::Black);
    REQUIRE(center_score > corner_score);
}

TEST_CASE("Heuristic: winning position", "[heuristic]") {
    Board board;
    for (uint8_t i = 0; i < 5; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }

    // Pattern scoring should produce very high score for five-in-a-row
    int score = evaluate(board, Stone::Black);
    REQUIRE(score >= PatternScore::FIVE);
}

TEST_CASE("Heuristic: losing position", "[heuristic]") {
    Board board;
    for (uint8_t i = 0; i < 5; ++i) {
        board.place_stone(Pos(9, i), Stone::White);
    }

    int score = evaluate(board, Stone::Black);
    REQUIRE(score <= -PatternScore::FIVE);
}

TEST_CASE("Heuristic: capture win", "[heuristic]") {
    Board board;
    board.add_captures(Stone::Black, 5);
    REQUIRE(evaluate(board, Stone::Black) == PatternScore::FIVE);
}

TEST_CASE("Heuristic: capture loss", "[heuristic]") {
    Board board;
    board.add_captures(Stone::White, 5);
    REQUIRE(evaluate(board, Stone::Black) == -PatternScore::FIVE);
}

TEST_CASE("Heuristic: open four", "[heuristic]") {
    Board board;
    // _OOOO_ : stones at cols 1-4, empty at 0 and 5
    for (uint8_t i = 1; i < 5; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }

    int score = evaluate(board, Stone::Black);
    REQUIRE(score > 0);
    REQUIRE(score < PatternScore::FIVE);
}

TEST_CASE("Heuristic: closed four", "[heuristic]") {
    Board board;
    // XOOOO_ : white at col 0, blacks at 1-4
    board.place_stone(Pos(9, 0), Stone::White);
    for (uint8_t i = 1; i < 5; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }

    int score = evaluate(board, Stone::Black);
    REQUIRE(score > 0);
}

TEST_CASE("Heuristic: open three", "[heuristic]") {
    Board board;
    // _OOO_ : stones at cols 1-3
    for (uint8_t i = 1; i < 4; ++i) {
        board.place_stone(Pos(9, i), Stone::Black);
    }

    int score = evaluate(board, Stone::Black);
    REQUIRE(score > 0);
}

TEST_CASE("Heuristic: negamax symmetry", "[heuristic]") {
    // REQUIRES: evaluate(board, Black) == -evaluate(board, White)
    Board board;
    board.place_stone(Pos(9, 7), Stone::Black);
    board.place_stone(Pos(9, 8), Stone::Black);
    board.place_stone(Pos(9, 9), Stone::Black);  // Open three for Black
    board.place_stone(Pos(5, 5), Stone::White);
    board.place_stone(Pos(5, 6), Stone::White);   // Open two for White

    int black_score = evaluate(board, Stone::Black);
    int white_score = evaluate(board, Stone::White);
    REQUIRE(black_score == -white_score);
}

TEST_CASE("Heuristic: perspective correct", "[heuristic]") {
    Board board1;
    Board board2;

    // Board1: Black has open three
    for (uint8_t i = 1; i < 4; ++i) {
        board1.place_stone(Pos(9, i), Stone::Black);
    }
    // Board2: White has open three
    for (uint8_t i = 1; i < 4; ++i) {
        board2.place_stone(Pos(9, i), Stone::White);
    }

    int our_advantage = evaluate(board1, Stone::Black);
    int their_advantage = evaluate(board2, Stone::Black);
    REQUIRE(our_advantage > 0);
    REQUIRE(their_advantage < 0);
}

TEST_CASE("Heuristic: multiple patterns", "[heuristic]") {
    Board board;
    // Two separate open twos
    board.place_stone(Pos(5, 5), Stone::Black);
    board.place_stone(Pos(5, 6), Stone::Black);
    board.place_stone(Pos(10, 10), Stone::Black);
    board.place_stone(Pos(10, 11), Stone::Black);

    int score = evaluate(board, Stone::Black);
    REQUIRE(score > 0);
}

TEST_CASE("Heuristic: diagonal pattern", "[heuristic]") {
    Board board;
    for (uint8_t i = 0; i < 3; ++i) {
        board.place_stone(Pos(5 + i, 5 + i), Stone::Black);
    }

    int score = evaluate(board, Stone::Black);
    REQUIRE(score > 0);
}

TEST_CASE("Heuristic: symmetry", "[heuristic]") {
    Board board;
    board.place_stone(Pos(9, 9), Stone::Black);
    board.place_stone(Pos(9, 10), Stone::White);

    int black_score = evaluate(board, Stone::Black);
    int white_score = evaluate(board, Stone::White);
    // Scores should reflect opposite perspectives
    bool opposite = ((black_score > 0) != (white_score > 0)) ||
                    (black_score == 0 && white_score == 0);
    REQUIRE(opposite);
}

TEST_CASE("Heuristic: captures matter", "[heuristic]") {
    Board board1;
    Board board2;

    board1.place_stone(Pos(9, 9), Stone::Black);

    board2.place_stone(Pos(9, 9), Stone::Black);
    board2.add_captures(Stone::Black, 2);

    int score1 = evaluate(board1, Stone::Black);
    int score2 = evaluate(board2, Stone::Black);
    REQUIRE(score2 > score1);
}

TEST_CASE("Heuristic: near capture win", "[heuristic]") {
    Board board;
    board.add_captures(Stone::Black, 4);

    int score = evaluate(board, Stone::Black);
    REQUIRE(score >= PatternScore::NEAR_CAPTURE_WIN);
}
