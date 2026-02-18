#include <catch2/catch_test_macros.hpp>
#include "gomoku/search/zobrist.hpp"
#include "gomoku/board/board.hpp"

using namespace gomoku;

TEST_CASE("zobrist_empty_board", "[zobrist]") {
    ZobristTable zt;
    Board board;

    uint64_t hash1 = zt.hash(board, Stone::Black);
    uint64_t hash2 = zt.hash(board, Stone::White);

    // Different side to move = different hash
    REQUIRE(hash1 != hash2);

    // Empty board includes capture hash for (0,0) captures
    uint64_t cap_base = zt.capture_keys[0][0] ^ zt.capture_keys[1][0];
    REQUIRE(hash2 == cap_base);
    REQUIRE(hash1 == (zt.black_to_move ^ cap_base));
}

TEST_CASE("zobrist_deterministic", "[zobrist]") {
    ZobristTable zt1;
    ZobristTable zt2;
    Board board;

    // Same table = same hash (deterministic random values)
    REQUIRE(zt1.hash(board, Stone::Black) == zt2.hash(board, Stone::Black));
}

TEST_CASE("zobrist_incremental", "[zobrist]") {
    ZobristTable zt;
    Board board;
    Pos pos(9, 9);

    uint64_t hash1 = zt.hash(board, Stone::Black);
    board.place_stone(pos, Stone::Black);
    uint64_t hash2 = zt.hash(board, Stone::White);

    // Incremental should match full computation
    uint64_t hash_inc = zt.update_place(hash1, pos, Stone::Black);
    REQUIRE(hash_inc == hash2);
}

TEST_CASE("zobrist_different_positions", "[zobrist]") {
    ZobristTable zt;
    Board board1, board2;

    board1.place_stone(Pos(9, 9), Stone::Black);
    board2.place_stone(Pos(10, 10), Stone::Black);

    uint64_t hash1 = zt.hash(board1, Stone::White);
    uint64_t hash2 = zt.hash(board2, Stone::White);

    // Different positions = different hash
    REQUIRE(hash1 != hash2);
}

TEST_CASE("zobrist_same_position_different_path", "[zobrist]") {
    ZobristTable zt;
    Board board1, board2;

    // Path 1: Black at (9,9), then White at (10,10)
    board1.place_stone(Pos(9, 9), Stone::Black);
    board1.place_stone(Pos(10, 10), Stone::White);

    // Path 2: White at (10,10), then Black at (9,9)
    board2.place_stone(Pos(10, 10), Stone::White);
    board2.place_stone(Pos(9, 9), Stone::Black);

    // Same final position = same hash (path independent)
    uint64_t hash1 = zt.hash(board1, Stone::Black);
    uint64_t hash2 = zt.hash(board2, Stone::Black);
    REQUIRE(hash1 == hash2);
}

TEST_CASE("zobrist_undo", "[zobrist]") {
    ZobristTable zt;
    Board board;
    Pos pos(9, 9);

    uint64_t hash_empty = zt.hash(board, Stone::Black);

    board.place_stone(pos, Stone::Black);
    uint64_t hash_with_stone = zt.hash(board, Stone::White);

    // Incremental update for place
    uint64_t hash_inc = zt.update_place(hash_empty, pos, Stone::Black);
    REQUIRE(hash_inc == hash_with_stone);

    // Incremental update for remove (should get back to original with side toggle)
    board.remove_stone(pos);
    uint64_t hash_after_remove = zt.hash(board, Stone::Black);

    // Removing stone and toggling side should get back to original
    uint64_t hash_inc_remove = zt.update_remove(hash_with_stone, pos, Stone::Black);
    REQUIRE(hash_inc_remove == hash_after_remove);
    REQUIRE(hash_after_remove == hash_empty);
}

TEST_CASE("zobrist_capture_no_side_toggle", "[zobrist]") {
    ZobristTable zt;
    Board board;

    // Place some stones
    board.place_stone(Pos(5, 5), Stone::Black);
    board.place_stone(Pos(5, 6), Stone::White);
    board.place_stone(Pos(5, 7), Stone::White);

    uint64_t hash_before = zt.hash(board, Stone::Black);

    // Simulate capture: remove white stones without toggling side
    uint64_t hash_after_cap1 = zt.update_capture(hash_before, Pos(5, 6), Stone::White);
    uint64_t hash_after_cap2 = zt.update_capture(hash_after_cap1, Pos(5, 7), Stone::White);

    // Verify by computing full hash after removing stones
    board.remove_stone(Pos(5, 6));
    board.remove_stone(Pos(5, 7));
    uint64_t hash_full = zt.hash(board, Stone::Black);

    REQUIRE(hash_after_cap2 == hash_full);
}

TEST_CASE("zobrist_symmetry", "[zobrist]") {
    ZobristTable zt;

    // XOR is commutative: order of operations shouldn't matter
    uint64_t h1 = zt.black_keys[0] ^ zt.white_keys[1] ^ zt.black_keys[2];
    uint64_t h2 = zt.black_keys[2] ^ zt.black_keys[0] ^ zt.white_keys[1];
    REQUIRE(h1 == h2);
}

TEST_CASE("zobrist_collision_resistance", "[zobrist]") {
    ZobristTable zt;

    // Test that nearby positions have different hashes
    Board board1, board2;

    board1.place_stone(Pos(9, 9), Stone::Black);
    board2.place_stone(Pos(9, 10), Stone::Black);

    uint64_t hash1 = zt.hash(board1, Stone::Black);
    uint64_t hash2 = zt.hash(board2, Stone::Black);

    REQUIRE(hash1 != hash2);
}

TEST_CASE("zobrist_all_corners", "[zobrist]") {
    ZobristTable zt;
    Board board;

    // Place stones at all four corners
    Pos corners[] = {
        Pos(0, 0),
        Pos(0, 18),
        Pos(18, 0),
        Pos(18, 18),
    };

    for (auto pos : corners) {
        board.place_stone(pos, Stone::Black);
    }

    uint64_t hash = zt.hash(board, Stone::White);

    // Hash should be XOR of all four corner values + capture hashes for (0,0)
    uint64_t expected = zt.black_keys[corners[0].to_index()]
        ^ zt.black_keys[corners[1].to_index()]
        ^ zt.black_keys[corners[2].to_index()]
        ^ zt.black_keys[corners[3].to_index()]
        ^ zt.capture_keys[0][0]
        ^ zt.capture_keys[1][0];

    REQUIRE(hash == expected);
}
