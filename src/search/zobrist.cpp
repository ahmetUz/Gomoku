#include "gomoku/search/zobrist.hpp"
#include <algorithm>

namespace gomoku {

ZobristTable::ZobristTable() {
    // Use a simple LCG for deterministic "random" values
    // Same seed = same table = reproducible hashes
    // Constants from Knuth's MMIX LCG
    uint64_t seed = 0x1234'5678'9ABC'DEF0;
    auto next_rand = [&seed]() -> uint64_t {
        seed = seed * 6'364'136'223'846'793'005ULL + 1;
        return seed;
    };

    for (int i = 0; i < TOTAL_CELLS; ++i) {
        black_keys[i] = next_rand();
        white_keys[i] = next_rand();
    }

    for (int color = 0; color < 2; ++color) {
        for (int count = 0; count < 6; ++count) {
            capture_keys[color][count] = next_rand();
        }
    }

    black_to_move = next_rand();
}

uint64_t ZobristTable::hash(const Board& board, Stone side_to_move) const {
    uint64_t h = 0;

    // XOR in all black stones
    for (auto pos : board.black) {
        h ^= black_keys[pos.to_index()];
    }

    // XOR in all white stones
    for (auto pos : board.white) {
        h ^= white_keys[pos.to_index()];
    }

    // XOR in side-to-move component if black is to move
    if (side_to_move == Stone::Black) {
        h ^= black_to_move;
    }

    // Include capture counts in hash to distinguish positions with same stones
    // but different capture counts (affects win conditions)
    h ^= capture_keys[0][std::min(board.captures(Stone::Black), uint8_t(5))];
    h ^= capture_keys[1][std::min(board.captures(Stone::White), uint8_t(5))];

    return h;
}

uint64_t ZobristTable::update_place(uint64_t h, Pos pos, Stone stone) const {
    size_t idx = pos.to_index();
    uint64_t stone_hash = 0;

    switch (stone) {
        case Stone::Black: stone_hash = black_keys[idx]; break;
        case Stone::White: stone_hash = white_keys[idx]; break;
        default: break;
    }

    return h ^ stone_hash ^ black_to_move;
}

uint64_t ZobristTable::update_remove(uint64_t h, Pos pos, Stone stone) const {
    // XOR is its own inverse: a ^ b ^ b = a
    return update_place(h, pos, stone);
}

uint64_t ZobristTable::update_capture(uint64_t h, Pos pos, Stone stone) const {
    size_t idx = pos.to_index();
    uint64_t stone_hash = 0;

    switch (stone) {
        case Stone::Black: stone_hash = black_keys[idx]; break;
        case Stone::White: stone_hash = white_keys[idx]; break;
        default: break;
    }

    return h ^ stone_hash;
}

uint64_t ZobristTable::toggle_side(uint64_t h) const {
    return h ^ black_to_move;
}

uint64_t ZobristTable::update_capture_count(uint64_t h, Stone color,
                                            uint8_t old_count, uint8_t new_count) const {
    int cidx = (color == Stone::Black) ? 0 : 1;
    return h ^ capture_keys[cidx][std::min(old_count, uint8_t(5))]
             ^ capture_keys[cidx][std::min(new_count, uint8_t(5))];
}

} // namespace gomoku
