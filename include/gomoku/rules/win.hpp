#pragma once

// Win condition checking for Ninuki-renju (Pente-style Gomoku)
//
// Win conditions:
// 1. Five or more stones in a row (unbreakable)
// 2. Capture 10 opponent stones (5 pairs)
//
// Endgame capture rule: a 5-in-a-row only wins if the opponent
// cannot break it by capturing a pair from the line.

#include "gomoku/board/board.hpp"
#include <vector>
#include <optional>

namespace gomoku {

// Check if there's 5+ in a row for the given color (iterates ALL stones).
bool has_five_in_row(const Board& board, Stone stone);

// Fast five-in-a-row check at a specific position (4 directions, no allocation).
bool has_five_at_pos(const Board& board, Pos pos, Stone color);

// Find positions forming a 5+ line at pos (rare path, only when five is known).
std::optional<std::vector<Pos>> find_five_line_at_pos(
    const Board& board, Pos pos, Stone color);

// Find any 5-in-a-row for the given color. Returns the line positions if found.
std::optional<std::vector<Pos>> find_five_positions(
    const Board& board, Stone stone);

// Check if opponent can break the five by capturing part of it.
// This is a STATIC game-rule check (no look-ahead for recreation).
bool can_break_five_by_capture(
    const Board& board,
    const std::vector<Pos>& five_positions,
    Stone five_color);

// Find all positions where opponent can break the five by capture.
std::vector<Pos> find_five_break_moves(
    const Board& board,
    const std::vector<Pos>& five_positions,
    Stone five_color);

// Check for a winner after last_player just moved.
// Win conditions: 5 pair captures, or 5-in-a-row.
//
// Temporal rule for breakable fives:
//   1. If the OTHER player has a five on the board, last_player didn't
//      break it → other player wins (their chance has passed).
//   2. If last_player just formed a five:
//      - Unbreakable → last_player wins.
//      - Breakable → game continues (other gets one turn to break it).
std::optional<Stone> check_winner(const Board& board, Stone last_player);

} // namespace gomoku
