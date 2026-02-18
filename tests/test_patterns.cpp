#include <catch2/catch_test_macros.hpp>
#include "gomoku/eval/patterns.hpp"

using namespace gomoku;

TEST_CASE("Patterns: score hierarchy", "[patterns]") {
    REQUIRE(PatternScore::FIVE > PatternScore::OPEN_FOUR);
    REQUIRE(PatternScore::OPEN_FOUR > PatternScore::CLOSED_FOUR);
    REQUIRE(PatternScore::CLOSED_FOUR > PatternScore::OPEN_THREE);
    REQUIRE(PatternScore::OPEN_THREE > PatternScore::CLOSED_THREE);
    REQUIRE(PatternScore::CLOSED_THREE > PatternScore::OPEN_TWO);
    REQUIRE(PatternScore::OPEN_TWO > PatternScore::CLOSED_TWO);
}

TEST_CASE("Patterns: capture score zero", "[patterns]") {
    REQUIRE(capture_score(0, 0) == 0);
}

TEST_CASE("Patterns: capture score advantage", "[patterns]") {
    int score = capture_score(2, 0);
    REQUIRE(score > 0);
}

TEST_CASE("Patterns: capture score near win", "[patterns]") {
    int score = capture_score(4, 0);
    REQUIRE(score >= 60'000);
}

TEST_CASE("Patterns: capture score symmetric", "[patterns]") {
    // Negamax requires: capture_score(a, b) == -capture_score(b, a)
    REQUIRE(capture_score(1, 0) == -capture_score(0, 1));
    REQUIRE(capture_score(2, 1) == -capture_score(1, 2));
}

TEST_CASE("Patterns: capture score win", "[patterns]") {
    REQUIRE(capture_score(5, 0) == PatternScore::CAPTURE_WIN);
}

TEST_CASE("Patterns: capture score negamax symmetry (exhaustive)", "[patterns]") {
    for (uint8_t a = 0; a <= 5; ++a) {
        for (uint8_t b = 0; b <= 5; ++b) {
            int score_ab = capture_score(a, b);
            int score_ba = capture_score(b, a);
            REQUIRE(score_ab == -score_ba);
        }
    }
}
