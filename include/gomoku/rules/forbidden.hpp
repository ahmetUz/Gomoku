#pragma once

// Double-three forbidden move rules for Gomoku
//
// A double-three is a move that creates two or more free-threes simultaneously.
// Free-three: 3 stones in a row with both ends open, that can become an
// unstoppable open-four if not blocked.
//
// Exception: double-three via capture IS allowed.

#include "gomoku/board/board.hpp"
#include <cstdint>

namespace gomoku {

// Pattern information for a line segment.
// Exposed in header for testability; normally used internally.
struct LinePattern {
    int stones[11];         // Relative positions from center stone (sorted)
    uint8_t stone_count;    // Number of stones in the pattern
    uint8_t open_ends;      // Number of open ends (0, 1, or 2)
    uint8_t span;           // Total span of the pattern (max - min + 1)

    LinePattern() : stones{}, stone_count(0), open_ends(0), span(0) {}
};

// Scan a line in both directions from pos, allowing one gap.
// Detects patterns like _OO_O_ (free-three with gap).
// The placed stone at pos is included as position 0 but board.get(pos)
// is never read -- so the original board can be passed without cloning.
LinePattern scan_line(const Board& board, Pos pos, Stone stone, int dr, int dc);

// Check if a LinePattern forms a free-three.
// Free-three: exactly 3 stones, both ends open, span <= 4.
bool is_free_three(const LinePattern& pattern);

// Count how many free-threes placing stone at pos would create.
uint8_t count_free_threes(const Board& board, Pos pos, Stone stone);

// Check if a move is a forbidden double-three.
// Creates 2+ free-threes simultaneously. Exception: captures allowed.
bool is_double_three(const Board& board, Pos pos, Stone stone);

// Check if a move is valid (position empty and not forbidden double-three).
bool is_valid_move(const Board& board, Pos pos, Stone stone);

} // namespace gomoku
