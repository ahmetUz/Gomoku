#pragma once

// Hachage de Zobrist -- identification rapide des positions
//
// Chaque position du plateau a un hash unique calcule en O(1) par
// simple XOR quand on pose ou retire une pierre. C'est ce qui rend
// la table de transposition efficace : on identifie instantanement
// si une position a deja ete evaluee.
// Generateur deterministe (LCG de Knuth, seed fixe) pour la reproductibilite.

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
