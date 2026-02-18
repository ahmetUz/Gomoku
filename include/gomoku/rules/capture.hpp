#pragma once

// Capture rules for Ninuki-renju (Pente-style pair capture)
//
// Capture pattern: X-OO-X where X is the capturing player's stone
// and O is the opponent's stone. Only exactly 2 stones can be captured.

#include "gomoku/board/board.hpp"
#include <vector>
#include <cstdint>
#include <cstddef>

namespace gomoku {

// Maximum captured positions per move (8 rays x 2 stones each)
constexpr size_t MAX_CAPTURES = 16;

// Result of capture execution without heap allocation.
// Used with execute_captures_fast / undo_captures for the make/unmake pattern.
struct CaptureInfo {
    Pos positions[MAX_CAPTURES];
    uint8_t count = 0;   // Total individual stones captured
    uint8_t pairs = 0;   // Number of pairs captured
};

// Find positions that would be captured if stone is placed at pos.
// Returns pairs of captured opponent stones (always even count).
std::vector<Pos> get_captured_positions(const Board& board, Pos pos, Stone stone);

// Execute captures: find, remove stones, update capture count.
// Returns the positions that were captured.
std::vector<Pos> execute_captures(Board& board, Pos pos, Stone stone);

// Check if placing stone at pos would result in any captures (no allocation).
bool has_capture(const Board& board, Pos pos, Stone stone);

// Count how many pairs would be captured by a move (no allocation).
uint8_t count_captures(const Board& board, Pos pos, Stone stone);

// Identical to count_captures (no allocation path).
uint8_t count_captures_fast(const Board& board, Pos pos, Stone stone);

// Execute captures without heap allocation (for make/unmake pattern).
CaptureInfo execute_captures_fast(Board& board, Pos pos, Stone stone);

// Undo captures from a CaptureInfo (restore captured stones, decrement count).
void undo_captures(Board& board, Stone stone, const CaptureInfo& info);

} // namespace gomoku
