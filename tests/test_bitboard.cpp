#include <catch2/catch_test_macros.hpp>
#include "gomoku/board/bitboard.hpp"
#include "gomoku/board/types.hpp"
#include <vector>
#include <algorithm>

using namespace gomoku;

TEST_CASE("Bitboard: new bitboard is empty", "[bitboard]") {
    Bitboard bb;
    REQUIRE(bb.is_empty());
    REQUIRE(bb.count() == 0);
}

TEST_CASE("Bitboard: set and get", "[bitboard]") {
    Bitboard bb;
    Pos pos(9, 9);

    REQUIRE_FALSE(bb.get(pos));
    bb.set(pos);
    REQUIRE(bb.get(pos));
    REQUIRE(bb.count() == 1);
}

TEST_CASE("Bitboard: clear", "[bitboard]") {
    Bitboard bb;
    Pos pos(9, 9);

    bb.set(pos);
    REQUIRE(bb.get(pos));
    bb.clear(pos);
    REQUIRE_FALSE(bb.get(pos));
    REQUIRE(bb.count() == 0);
}

TEST_CASE("Bitboard: multiple positions", "[bitboard]") {
    Bitboard bb;

    bb.set(Pos(0, 0));
    bb.set(Pos(9, 9));
    bb.set(Pos(18, 18));

    REQUIRE(bb.get(Pos(0, 0)));
    REQUIRE(bb.get(Pos(9, 9)));
    REQUIRE(bb.get(Pos(18, 18)));
    REQUIRE_FALSE(bb.get(Pos(5, 5)));
    REQUIRE(bb.count() == 3);
}

TEST_CASE("Bitboard: iterator", "[bitboard]") {
    Bitboard bb;
    bb.set(Pos(0, 0));
    bb.set(Pos(5, 5));
    bb.set(Pos(10, 10));

    std::vector<Pos> positions;
    for (auto pos : bb) {
        positions.push_back(pos);
    }
    REQUIRE(positions.size() == 3);
    REQUIRE(std::find(positions.begin(), positions.end(), Pos(0, 0)) != positions.end());
    REQUIRE(std::find(positions.begin(), positions.end(), Pos(5, 5)) != positions.end());
    REQUIRE(std::find(positions.begin(), positions.end(), Pos(10, 10)) != positions.end());
}

TEST_CASE("Bitboard: word boundaries", "[bitboard]") {
    Bitboard bb;

    // Word 0: indices 0-63, Word 1: indices 64-127, etc.
    bb.set(Pos::from_index(63));   // End of word 0
    bb.set(Pos::from_index(64));   // Start of word 1
    bb.set(Pos::from_index(127));  // End of word 1
    bb.set(Pos::from_index(128));  // Start of word 2

    REQUIRE(bb.get(Pos::from_index(63)));
    REQUIRE(bb.get(Pos::from_index(64)));
    REQUIRE(bb.get(Pos::from_index(127)));
    REQUIRE(bb.get(Pos::from_index(128)));
    REQUIRE(bb.count() == 4);
}
