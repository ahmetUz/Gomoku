// Double-three forbidden move rules implementation

#include "gomoku/rules/forbidden.hpp"
#include "gomoku/rules/capture.hpp"
#include <algorithm>

namespace gomoku {

// 4 axis directions
static constexpr int DIRECTIONS[4][2] = {
    {0, 1},   // Horizontal
    {1, 0},   // Vertical
    {1, 1},   // Diagonal SE
    {1, -1},  // Diagonal SW
};

LinePattern scan_line(const Board& board, Pos pos, Stone stone, int dr, int dc) {
    Stone opp = opponent(stone);
    LinePattern pat;
    pat.stones[0] = 0;      // The placed stone at position 0
    pat.stone_count = 1;

    // --- Scan positive direction ---
    bool open_end_pos = false;
    bool gap_used_pos = false;

    for (int i = 1; i <= 5; ++i) {
        int r = pos.row + dr * i;
        int c = pos.col + dc * i;
        if (!Pos::is_valid(r, c)) break;

        Stone cell = board.get(Pos(uint8_t(r), uint8_t(c)));
        if (cell == stone) {
            pat.stones[pat.stone_count++] = i;
        } else if (cell == opp) {
            break;
        } else {
            // Empty cell
            if (!gap_used_pos) {
                // Check if there's a stone after this gap
                int next_r = pos.row + dr * (i + 1);
                int next_c = pos.col + dc * (i + 1);
                if (Pos::is_valid(next_r, next_c)) {
                    if (board.get(Pos(uint8_t(next_r), uint8_t(next_c))) == stone) {
                        gap_used_pos = true;
                        continue;  // Skip the gap, keep scanning
                    }
                }
            }
            open_end_pos = true;
            break;
        }
    }
    if (open_end_pos) pat.open_ends++;

    // --- Scan negative direction ---
    bool open_end_neg = false;
    bool gap_used_neg = false;

    for (int i = 1; i <= 5; ++i) {
        int r = pos.row - dr * i;
        int c = pos.col - dc * i;
        if (!Pos::is_valid(r, c)) break;

        Stone cell = board.get(Pos(uint8_t(r), uint8_t(c)));
        if (cell == stone) {
            pat.stones[pat.stone_count++] = -i;
        } else if (cell == opp) {
            break;
        } else {
            if (!gap_used_neg) {
                int next_r = pos.row - dr * (i + 1);
                int next_c = pos.col - dc * (i + 1);
                if (Pos::is_valid(next_r, next_c)) {
                    if (board.get(Pos(uint8_t(next_r), uint8_t(next_c))) == stone) {
                        gap_used_neg = true;
                        continue;
                    }
                }
            }
            open_end_neg = true;
            break;
        }
    }
    if (open_end_neg) pat.open_ends++;

    // Sort stones and compute span
    std::sort(pat.stones, pat.stones + pat.stone_count);
    if (pat.stone_count == 0) {
        pat.span = 0;
    } else {
        pat.span = uint8_t(pat.stones[pat.stone_count - 1] - pat.stones[0] + 1);
    }

    return pat;
}

// Scan without allowing gaps (consecutive stones only)
static LinePattern scan_line_consecutive(
    const Board& board, Pos pos, Stone stone, int dr, int dc)
{
    Stone opp = opponent(stone);
    LinePattern pat;
    pat.stones[0] = 0;
    pat.stone_count = 1;

    // Positive direction -- consecutive only
    bool open_end_pos = false;
    for (int i = 1; i <= 5; ++i) {
        int r = pos.row + dr * i;
        int c = pos.col + dc * i;
        if (!Pos::is_valid(r, c)) break;
        Stone cell = board.get(Pos(uint8_t(r), uint8_t(c)));
        if (cell == stone) {
            pat.stones[pat.stone_count++] = i;
        } else if (cell == opp) {
            break;
        } else {
            open_end_pos = true;
            break;
        }
    }
    if (open_end_pos) pat.open_ends++;

    // Negative direction -- consecutive only
    bool open_end_neg = false;
    for (int i = 1; i <= 5; ++i) {
        int r = pos.row - dr * i;
        int c = pos.col - dc * i;
        if (!Pos::is_valid(r, c)) break;
        Stone cell = board.get(Pos(uint8_t(r), uint8_t(c)));
        if (cell == stone) {
            pat.stones[pat.stone_count++] = -i;
        } else if (cell == opp) {
            break;
        } else {
            open_end_neg = true;
            break;
        }
    }
    if (open_end_neg) pat.open_ends++;

    std::sort(pat.stones, pat.stones + pat.stone_count);
    if (pat.stone_count == 0) {
        pat.span = 0;
    } else {
        pat.span = uint8_t(pat.stones[pat.stone_count - 1] - pat.stones[0] + 1);
    }
    return pat;
}

bool is_free_three(const LinePattern& pattern) {
    // Must have exactly 3 stones
    if (pattern.stone_count != 3) return false;

    // Must have both ends open
    if (pattern.open_ends < 2) return false;

    // Check the span:
    // - Consecutive OOO: span = 3
    // - One gap OO_O or O_OO: span = 4
    // - Span > 4 means too spread out
    if (pattern.span > 4) return false;

    // For span = 4 (with gap), verify the pattern can become open-four
    if (pattern.span == 4) {
        int min = pattern.stones[0];
        int max = pattern.stones[2];

        // Three stones must have exactly one gap of size 1
        bool has_single_gap =
            (max - min == 3) &&
            ((pattern.stones[1] - pattern.stones[0] == 1 &&
              pattern.stones[2] - pattern.stones[1] == 2) ||
             (pattern.stones[1] - pattern.stones[0] == 2 &&
              pattern.stones[2] - pattern.stones[1] == 1));
        return has_single_gap;
    }

    // span == 3: consecutive three OOO
    return true;
}

// Check if placing stone creates a free-three in one direction
static bool creates_free_three_in_direction(
    const Board& board, Pos pos, Stone stone, int dr, int dc)
{
    // scan_line starts with stones=[0] (the placed stone) and only reads
    // cells at distance 1+ from pos. It never reads board.get(pos).
    // So we can analyze the original board without cloning.
    LinePattern pattern = scan_line(board, pos, stone, dr, dc);
    if (is_free_three(pattern)) return true;

    // Fallback: when gap-inclusive scan finds >3 stones, a consecutive
    // subset might form a free-three hidden by the extra stone(s).
    if (pattern.stone_count > 3) {
        LinePattern consec = scan_line_consecutive(board, pos, stone, dr, dc);
        if (is_free_three(consec)) return true;
    }
    return false;
}

uint8_t count_free_threes(const Board& board, Pos pos, Stone stone) {
    uint8_t count = 0;

    for (const auto& dir : DIRECTIONS) {
        if (creates_free_three_in_direction(board, pos, stone, dir[0], dir[1])) {
            count++;
            // Early exit: double-three only needs 2+
            if (count >= 2) return count;
        }
    }
    return count;
}

bool is_double_three(const Board& board, Pos pos, Stone stone) {
    // Exception: if this move captures, double-three is allowed
    if (has_capture(board, pos, stone)) return false;

    return count_free_threes(board, pos, stone) >= 2;
}

bool is_valid_move(const Board& board, Pos pos, Stone stone) {
    if (!board.is_empty(pos)) return false;
    if (is_double_three(board, pos, stone)) return false;
    return true;
}

} // namespace gomoku
