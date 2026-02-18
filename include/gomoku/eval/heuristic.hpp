#pragma once

// Heuristic evaluation function for Gomoku board positions
//
// Evaluates board positions based on:
// - Win/loss detection (capture win)
// - Pattern scoring (fives, fours, threes, twos)
// - Capture advantage (non-linear)
// - Positional bonuses (center control, connectivity)
// - Vulnerability penalty (capturable pairs)
//
// SYMMETRIC for negamax: evaluate(board, Black) == -evaluate(board, White)

#include "gomoku/board/board.hpp"

namespace gomoku {

// Evaluate the board from the perspective of the given color.
// Positive = advantage, negative = disadvantage.
// Returns PatternScore::FIVE for capture win, -FIVE for capture loss.
int evaluate(const Board& board, Stone color);

} // namespace gomoku
