#pragma once

// Zobrist hashing for position identification
//
// Zobrist hashing allows O(1) incremental hash updates when placing/removing
// stones. Essential for efficient transposition table lookups during search.
//
// Uses a deterministic LCG (Knuth's MMIX constants) with fixed seed
// so that hashes are reproducible across runs.

#include "gomoku/board/board.hpp"
#include <cstdint>

namespace gomoku {

class ZobristTable {
public:
    // Create a new Zobrist table with deterministic random values.
    ZobristTable();

    // Compute the full hash for a board position.
    // Iterates over all stones -- use incremental updates during search.
    uint64_t hash(const Board& board, Stone side_to_move) const;

    // Incrementally update hash after placing a stone.
    // Also toggles the side-to-move component. O(1).
    uint64_t update_place(uint64_t h, Pos pos, Stone stone) const;

    // Incrementally update hash after removing a stone.
    // XOR is its own inverse, so identical to update_place.
    uint64_t update_remove(uint64_t h, Pos pos, Stone stone) const;

    // Update hash for a capture (removing opponent stone without toggling side).
    uint64_t update_capture(uint64_t h, Pos pos, Stone stone) const;

    // Toggle the side-to-move component (used for null move pruning).
    uint64_t toggle_side(uint64_t h) const;

    // Update hash when capture count changes for a color.
    uint64_t update_capture_count(uint64_t h, Stone color,
                                  uint8_t old_count, uint8_t new_count) const;

    // Public for testing (Rust tests directly access these fields)
    uint64_t black_keys[TOTAL_CELLS];
    uint64_t white_keys[TOTAL_CELLS];
    uint64_t black_to_move;
    uint64_t capture_keys[2][6]; // [color_index][count 0..5]
};

} // namespace gomoku
