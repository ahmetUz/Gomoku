// Heuristic evaluation implementation -- single-pass pattern + position scoring

#include "gomoku/eval/heuristic.hpp"
#include "gomoku/eval/patterns.hpp"
#include "gomoku/board/bitboard.hpp"
#include <utility>
#include <cstdlib>

namespace gomoku {

// 4 axis directions for line checking
static constexpr int DIRECTIONS[4][2] = {
    {0, 1},   // Horizontal
    {1, 0},   // Vertical
    {1, 1},   // Diagonal SE
    {1, -1},  // Diagonal SW
};

// Maximum Manhattan distance from center on 19x19 board
static constexpr int MAX_CENTER_DIST = 18;

// Weight per distance unit from center.
// Higher weight prevents scattered stone placement.
static constexpr int POSITION_WEIGHT = 8;

// Vulnerability penalty weight scaled by opponent's capture count.
// Higher captures = much higher penalty per vulnerable pair (exponential danger).
static int vuln_weight(uint8_t opp_captures) {
    if (opp_captures <= 1) return 10'000;   // Vulnerability matters even early
    if (opp_captures == 2) return 20'000;   // Opponent actively hunting
    if (opp_captures == 3) return 40'000;   // Serious strategic threat
    return 80'000;                          // One more capture = instant loss
}

// Evaluate a single line pattern from pos in a given direction.
// Uses direct bitboard access for speed. Line-start filter handled by caller.
// prev_open: whether the cell before pos (negative direction) is empty.
static int evaluate_line(
    const Bitboard& my_bb,
    const Bitboard& opp_bb,
    Pos pos,
    int dr, int dc,
    bool prev_open)
{
    int count = 1;                          // Start with stone at pos
    int open_ends = prev_open ? 1 : 0;
    bool has_gap = false;
    int total_span = 1;                     // Positions used (stones + gap)

    // Extend in positive direction, allowing one gap
    int r = pos.row + dr;
    int c = pos.col + dc;
    while (Pos::is_valid(r, c)) {
        Pos p{uint8_t(r), uint8_t(c)};
        if (my_bb.get(p)) {
            count++;
            total_span++;
        } else if (opp_bb.get(p)) {
            break;  // Opponent stone blocks
        } else if (!has_gap) {
            // Empty cell, no gap used yet -- check for stone after gap
            int next_r = r + dr;
            int next_c = c + dc;
            if (Pos::is_valid(next_r, next_c) &&
                my_bb.get(Pos{uint8_t(next_r), uint8_t(next_c)}))
            {
                has_gap = true;
                total_span++;
                r += dr;
                c += dc;
                continue;
            }
            // No stone after gap -- open end
            open_ends++;
            break;
        } else {
            // Second empty cell (gap already used) -- open end
            open_ends++;
            break;
        }
        r += dr;
        c += dc;
    }

    // Score based on pattern type
    if (has_gap) {
        // Gap patterns: count stones (not gap), span determines if filling gap completes 5.
        // Gap patterns are never actual five-in-a-row; filling the gap is one move away.
        if (count >= 5)                          return PatternScore::OPEN_FOUR;
        if (count == 4 && total_span == 5)       return PatternScore::OPEN_FOUR;
        if (count == 4)                          return PatternScore::CLOSED_FOUR;
        if (count == 3 && open_ends == 2)        return PatternScore::OPEN_THREE;
        if (count == 3 && open_ends == 1)        return PatternScore::CLOSED_THREE;
        return 0;
    } else {
        if (count >= 5)                          return PatternScore::FIVE;
        if (count == 4 && open_ends == 2)        return PatternScore::OPEN_FOUR;
        if (count == 4 && open_ends == 1)        return PatternScore::CLOSED_FOUR;
        if (count == 3 && open_ends == 2)        return PatternScore::OPEN_THREE;
        if (count == 3 && open_ends == 1)        return PatternScore::CLOSED_THREE;
        if (count == 2 && open_ends == 2)        return PatternScore::OPEN_TWO;
        if (count == 2 && open_ends == 1)        return PatternScore::CLOSED_TWO;
        return 0;
    }
}

// Single-pass evaluation for one color using direct bitboard access.
// Combines pattern scoring, position bonus, connectivity, and vulnerability.
// Returns {total_score, vulnerable_pair_count}.
static std::pair<int, int> evaluate_color(const Board& board, Stone color) {
    const Bitboard* my_bb = board.stones(color);
    if (!my_bb) return {0, 0};
    // color is always Black or White, so opponent always has a bitboard
    const Bitboard* opp_bb = board.stones(opponent(color));

    int center = BOARD_SIZE / 2;
    int score = 0;
    int open_fours = 0;
    int closed_fours = 0;
    int open_threes = 0;
    int vuln = 0;
    int open_twos = 0;

    for (auto pos : *my_bb) {
        // --- Pattern scoring (4 directions) with line-start filter ---
        for (const auto& dir : DIRECTIONS) {
            int dr = dir[0], dc = dir[1];

            // Line-start filter: skip if prev pos has same-color stone.
            // Ensures each line segment is counted exactly once.
            int prev_r = pos.row - dr;
            int prev_c = pos.col - dc;
            if (Pos::is_valid(prev_r, prev_c) &&
                my_bb->get(Pos{uint8_t(prev_r), uint8_t(prev_c)}))
            {
                continue;
            }

            // prev is either off-board or not our stone. Check if open end.
            bool prev_open = Pos::is_valid(prev_r, prev_c) &&
                !opp_bb->get(Pos{uint8_t(prev_r), uint8_t(prev_c)});

            int pattern_score = evaluate_line(*my_bb, *opp_bb, pos, dr, dc, prev_open);
            score += pattern_score;

            if (pattern_score >= PatternScore::OPEN_FOUR) {
                open_fours++;
            } else if (pattern_score >= PatternScore::CLOSED_FOUR) {
                closed_fours++;
            } else if (pattern_score >= PatternScore::OPEN_THREE) {
                open_threes++;
            } else if (pattern_score >= PatternScore::OPEN_TWO &&
                       pattern_score < PatternScore::CLOSED_THREE)
            {
                open_twos++;
            }
        }

        // --- Position bonus (center control) ---
        int dist = std::abs(int(pos.row) - center) + std::abs(int(pos.col) - center);
        score += (MAX_CENTER_DIST - dist) * POSITION_WEIGHT;

        // --- Connectivity bonus: unidirectional (positive only) ---
        // Each adjacent pair counted once from the stone with lower dir offset.
        for (const auto& dir : DIRECTIONS) {
            int nr = pos.row + dir[0];
            int nc = pos.col + dir[1];
            if (Pos::is_valid(nr, nc) &&
                my_bb->get(Pos{uint8_t(nr), uint8_t(nc)}))
            {
                score += 160;
            }
        }

        // --- Vulnerability: ally-ally pair capturable by opponent ---
        for (const auto& dir : DIRECTIONS) {
            int dr = dir[0], dc = dir[1];
            int r1 = pos.row + dr;
            int c1 = pos.col + dc;
            if (!Pos::is_valid(r1, c1)) continue;
            Pos p1{uint8_t(r1), uint8_t(c1)};
            if (!my_bb->get(p1)) continue;

            int rb = pos.row - dr;
            int cb = pos.col - dc;
            int ra = r1 + dr;
            int ca = c1 + dc;

            // Before position (rb, cb)
            bool b_empty = false, b_opp = false;
            if (Pos::is_valid(rb, cb)) {
                Pos pb{uint8_t(rb), uint8_t(cb)};
                b_opp = opp_bb->get(pb);
                b_empty = !b_opp && !my_bb->get(pb);
            }

            // After position (ra, ca)
            bool a_empty = false, a_opp = false;
            if (Pos::is_valid(ra, ca)) {
                Pos pa{uint8_t(ra), uint8_t(ca)};
                a_opp = opp_bb->get(pa);
                a_empty = !a_opp && !my_bb->get(pa);
            }

            // empty-ally-ally-opp: opponent plays at empty to capture
            if (b_empty && a_opp) vuln++;
            // opp-ally-ally-empty: opponent plays at empty to capture
            if (b_opp && a_empty) vuln++;
        }
    }

    // Multiple threat combination bonuses (often unblockable)
    if (open_fours >= 1 && (closed_fours >= 1 || open_threes >= 1)) {
        score += PatternScore::OPEN_FOUR;
    }
    if (closed_fours >= 2) {
        score += PatternScore::OPEN_FOUR;
    }
    if (closed_fours >= 1 && open_threes >= 1) {
        score += PatternScore::OPEN_FOUR;
    }
    // Double open three: opponent can only block one -> the other becomes open four
    if (open_threes >= 2) {
        score += PatternScore::OPEN_FOUR;
    }

    // Multi-directional development bonus (open twos)
    if (open_twos >= 4) {
        score += 8'000;
    } else if (open_twos >= 3) {
        score += 5'000;
    } else if (open_twos >= 2) {
        score += 3'000;
    }

    return {score, vuln};
}

int evaluate(const Board& board, Stone color) {
    Stone opp = opponent(color);

    // Quick capture-win check (O(1) -- just reads stored count).
    if (board.captures(color) >= 5) return PatternScore::FIVE;
    if (board.captures(opp) >= 5)   return -PatternScore::FIVE;

    int cap_score = capture_score(board.captures(color), board.captures(opp));

    // Single-pass evaluation per color: patterns + position + vulnerability.
    // SYMMETRIC for negamax: evaluate(board, Black) == -evaluate(board, White).
    auto [my_score, my_vuln]   = evaluate_color(board, color);
    auto [opp_score, opp_vuln] = evaluate_color(board, opp);

    uint8_t my_caps  = board.captures(color);
    uint8_t opp_caps = board.captures(opp);
    int vuln_penalty = my_vuln * vuln_weight(opp_caps) - opp_vuln * vuln_weight(my_caps);

    return cap_score + (my_score - opp_score) - vuln_penalty;
}

} // namespace gomoku
