// VCF/VCT threat search for forced wins
//
// This module implements specialized threat-space search algorithms:
// - VCF (Victory by Continuous Fours): Finds winning sequences using only four-threats
// - VCT (Victory by Continuous Threats): More general, includes open-three threats
//
// These are powerful pruning techniques that can find forced wins much faster
// than regular alpha-beta search by only considering forcing moves.

#include "gomoku/search/threat.hpp"
#include "gomoku/rules/capture.hpp"
#include "gomoku/rules/win.hpp"
#include "gomoku/rules/forbidden.hpp"
#include <algorithm>

namespace gomoku {

// Direction vectors for line checking (4 directions)
static constexpr int DIRECTIONS[4][2] = {
    {0, 1},   // Horizontal
    {1, 0},   // Vertical
    {1, 1},   // Diagonal SE
    {1, -1},  // Diagonal SW
};

ThreatSearcher::ThreatSearcher()
    : max_vcf_depth_(30), max_vct_depth_(20), nodes_(0) {}

ThreatSearcher::ThreatSearcher(uint8_t vcf_depth, uint8_t vct_depth)
    : max_vcf_depth_(vcf_depth), max_vct_depth_(vct_depth), nodes_(0) {}

ThreatResult ThreatSearcher::search_vcf(const Board& board, Stone color) {
    nodes_ = 0;
    std::vector<Pos> sequence;
    Board work_board = board;

    if (vcf_search(work_board, color, 0, sequence)) {
        return ThreatResult::found_win(std::move(sequence));
    } else {
        return ThreatResult::not_found();
    }
}

bool ThreatSearcher::vcf_search(Board& board, Stone color, uint8_t depth,
                                std::vector<Pos>& sequence) {
    nodes_ += 1;

    if (depth > max_vcf_depth_) {
        return false;
    }

    // Find all moves that create a four
    std::vector<Pos> threats = find_four_threats(board, color);

    for (const Pos& threat_move : threats) {
        // Make the threat move
        board.place_stone(threat_move, color);
        CaptureInfo cap_info = execute_captures_fast(board, threat_move, color);

        sequence.push_back(threat_move);

        // Check for immediate win by five-in-a-row
        bool found_win = false;
        bool is_breakable_five = false;
        if (has_five_at_pos(board, threat_move, color)) {
            auto five = find_five_positions(board, color);
            if (five.has_value()) {
                if (!can_break_five_by_capture(board, *five, color)) {
                    found_win = true;
                } else {
                    // Breakable five: opponent can capture to destroy it.
                    // find_defense_moves only handles fours (count==4), not fives,
                    // so it would return empty defenses and falsely declare a win.
                    // Mark as breakable and skip find_defense_moves below.
                    is_breakable_five = true;
                }
            }
        }

        // Check for capture win (5 pairs = 10 stones)
        if (!found_win && board.captures(color) >= 5) {
            found_win = true;
        }

        if (found_win) {
            // Unmake before returning (board must be restored)
            undo_captures(board, color, cap_info);
            board.remove_stone(threat_move);
            return true;
        }

        // Breakable five: skip this move — it's not a guaranteed VCF win
        if (is_breakable_five) {
            undo_captures(board, color, cap_info);
            board.remove_stone(threat_move);
            sequence.pop_back();
            continue;
        }

        // Check if captures freed positions that let defender win immediately.
        // After capturing a pair, the freed positions can be replayed by
        // the defender to complete a five in a different direction.
        if (cap_info.count > 0) {
            bool defender_wins = false;
            Stone defender = opponent(color);
            for (size_t i = 0; i < cap_info.count; i++) {
                if (creates_five_or_more(board, cap_info.positions[i], defender)) {
                    defender_wins = true;
                    break;
                }
            }
            if (defender_wins) {
                undo_captures(board, color, cap_info);
                board.remove_stone(threat_move);
                sequence.pop_back();
                continue;
            }
        }

        // Find opponent's forced defenses against this four
        std::vector<Pos> defenses = find_defense_moves(board, threat_move, color);

        if (defenses.empty()) {
            // No defense means we win
            undo_captures(board, color, cap_info);
            board.remove_stone(threat_move);
            return true;
        }

        // If only one defense, opponent is forced to play there
        if (defenses.size() == 1) {
            Pos defense = defenses[0];
            Stone defender = opponent(color);
            board.place_stone(defense, defender);
            CaptureInfo def_cap = execute_captures_fast(board, defense, defender);

            bool result = vcf_search(board, color, depth + 1, sequence);

            // Unmake defense
            undo_captures(board, defender, def_cap);
            board.remove_stone(defense);

            if (result) {
                undo_captures(board, color, cap_info);
                board.remove_stone(threat_move);
                return true;
            }
        }
        // Multiple defenses: VCF fails at this branch

        // Unmake threat move
        undo_captures(board, color, cap_info);
        board.remove_stone(threat_move);

        sequence.pop_back();
    }

    return false;
}

std::vector<Pos> ThreatSearcher::find_four_threats(const Board& board, Stone color) const {
    std::vector<Pos> winning_moves;
    std::vector<Pos> four_threats;

    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            Pos pos{uint8_t(r), uint8_t(c)};
            if (!is_valid_move(board, pos, color)) {
                continue;
            }

            // Check if this creates a winning five first
            if (creates_five_or_more(board, pos, color)) {
                winning_moves.push_back(pos);
            } else if (creates_four(board, pos, color)) {
                four_threats.push_back(pos);
            }
        }
    }

    // Prioritize winning moves
    winning_moves.insert(winning_moves.end(), four_threats.begin(), four_threats.end());
    return winning_moves;
}

bool ThreatSearcher::creates_five_or_more(const Board& board, Pos pos, Stone color) const {
    for (int dir = 0; dir < 4; dir++) {
        int dr = DIRECTIONS[dir][0];
        int dc = DIRECTIONS[dir][1];
        int count = 1; // The stone we're placing

        // Scan positive direction
        int r = pos.row + dr;
        int c = pos.col + dc;
        while (Pos::is_valid(r, c)) {
            Pos p{uint8_t(r), uint8_t(c)};
            if (board.get(p) == color) {
                count++;
            } else {
                break;
            }
            r += dr;
            c += dc;
        }

        // Scan negative direction
        r = pos.row - dr;
        c = pos.col - dc;
        while (Pos::is_valid(r, c)) {
            Pos p{uint8_t(r), uint8_t(c)};
            if (board.get(p) == color) {
                count++;
            } else {
                break;
            }
            r -= dr;
            c -= dc;
        }

        if (count >= 5) {
            return true;
        }
    }

    return false;
}

bool ThreatSearcher::creates_four(const Board& board, Pos pos, Stone color) const {
    for (int dir = 0; dir < 4; dir++) {
        int dr = DIRECTIONS[dir][0];
        int dc = DIRECTIONS[dir][1];
        int count = 1; // The stone we're placing
        int open_ends = 0;

        // Scan positive direction
        int r = pos.row + dr;
        int c = pos.col + dc;
        while (Pos::is_valid(r, c)) {
            Pos p{uint8_t(r), uint8_t(c)};
            Stone s = board.get(p);
            if (s == color) {
                count++;
            } else if (s == Stone::Empty) {
                open_ends++;
                break;
            } else {
                break; // Opponent stone blocks
            }
            r += dr;
            c += dc;
        }

        // Scan negative direction
        r = pos.row - dr;
        c = pos.col - dc;
        while (Pos::is_valid(r, c)) {
            Pos p{uint8_t(r), uint8_t(c)};
            Stone s = board.get(p);
            if (s == color) {
                count++;
            } else if (s == Stone::Empty) {
                open_ends++;
                break;
            } else {
                break; // Opponent stone blocks
            }
            r -= dr;
            c -= dc;
        }

        // A four needs exactly 4 stones and at least one open end
        if (count == 4 && open_ends >= 1) {
            return true;
        }
    }

    return false;
}

std::vector<Pos> ThreatSearcher::find_defense_moves(
    const Board& board, Pos threat_move, Stone attacker) const {
    Stone defender = opponent(attacker);
    std::vector<Pos> defenses;
    std::vector<Pos> four_positions;
    uint8_t defender_captures = board.captures(defender);

    // Find blocking moves at the extension points of the four
    // Also collect the positions of the four-pattern stones
    for (int dir = 0; dir < 4; dir++) {
        int dr = DIRECTIONS[dir][0];
        int dc = DIRECTIONS[dir][1];
        int count = 1;
        std::vector<Pos> extension_points;
        std::vector<Pos> line_positions;
        line_positions.push_back(threat_move);

        // Scan positive direction
        int r = threat_move.row + dr;
        int c = threat_move.col + dc;
        while (Pos::is_valid(r, c)) {
            Pos p{uint8_t(r), uint8_t(c)};
            Stone s = board.get(p);
            if (s == attacker) {
                count++;
                line_positions.push_back(p);
            } else if (s == Stone::Empty) {
                extension_points.push_back(p);
                break;
            } else {
                break;
            }
            r += dr;
            c += dc;
        }

        // Scan negative direction
        r = threat_move.row - dr;
        c = threat_move.col - dc;
        while (Pos::is_valid(r, c)) {
            Pos p{uint8_t(r), uint8_t(c)};
            Stone s = board.get(p);
            if (s == attacker) {
                count++;
                line_positions.push_back(p);
            } else if (s == Stone::Empty) {
                extension_points.push_back(p);
                break;
            } else {
                break;
            }
            r -= dr;
            c -= dc;
        }

        // If this direction has a four, the extension points are defenses
        if (count == 4) {
            for (const Pos& ext : extension_points) {
                if (is_valid_move(board, ext, defender)) {
                    defenses.push_back(ext);
                }
            }
            // Collect the four-pattern positions for capture validation
            four_positions.insert(four_positions.end(), line_positions.begin(), line_positions.end());
        }
    }

    // Deduplicate four_positions
    std::sort(four_positions.begin(), four_positions.end());
    four_positions.erase(std::unique(four_positions.begin(), four_positions.end()), four_positions.end());

    // Find capture moves as defenses
    // In Ninuki-renju, the defender can ignore the four and capture instead:
    // - Captures that break the four (remove stones from the four pattern)
    // - ANY capture when defender has 3+ captures (closing in on capture-win)
    bool capture_is_strategic = defender_captures >= 3;
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            Pos pos{uint8_t(r), uint8_t(c)};
            if (!is_valid_move(board, pos, defender)) {
                continue;
            }

            std::vector<Pos> captured = get_captured_positions(board, pos, defender);
            if (!captured.empty()) {
                // Add as defense if:
                // 1. Capture breaks the four pattern, OR
                // 2. Defender has 3+ captures (any capture is strategically significant)
                bool breaks_four = std::any_of(captured.begin(), captured.end(),
                    [&four_positions](const Pos& cap) {
                        return std::find(four_positions.begin(), four_positions.end(), cap) != four_positions.end();
                    });
                if (capture_is_strategic || breaks_four) {
                    defenses.push_back(pos);
                }
            }
        }
    }

    std::sort(defenses.begin(), defenses.end());
    defenses.erase(std::unique(defenses.begin(), defenses.end()), defenses.end());
    return defenses;
}

ThreatResult ThreatSearcher::search_vct(const Board& board, Stone color) {
    nodes_ = 0;
    std::vector<Pos> sequence;
    Board work_board = board;

    // First try VCF (faster and more forcing)
    if (vcf_search(work_board, color, 0, sequence)) {
        return ThreatResult::found_win(std::move(sequence));
    }

    sequence.clear();
    if (vct_search(work_board, color, 0, sequence)) {
        return ThreatResult::found_win(std::move(sequence));
    } else {
        return ThreatResult::not_found();
    }
}

bool ThreatSearcher::vct_search(Board& board, Stone color, uint8_t depth,
                                std::vector<Pos>& sequence) {
    nodes_ += 1;

    if (depth > max_vct_depth_) {
        return false;
    }

    // Find all threat moves (fours and open-threes)
    std::vector<Pos> threats = find_all_threats(board, color);

    for (const Pos& threat_move : threats) {
        // Make the threat move
        board.place_stone(threat_move, color);
        CaptureInfo cap_info = execute_captures_fast(board, threat_move, color);

        sequence.push_back(threat_move);

        // Check for immediate win
        bool found_win = false;
        bool is_breakable_five = false;
        if (has_five_at_pos(board, threat_move, color)) {
            auto five = find_five_positions(board, color);
            if (five.has_value()) {
                if (!can_break_five_by_capture(board, *five, color)) {
                    found_win = true;
                } else {
                    is_breakable_five = true;
                }
            }
        }

        if (!found_win && board.captures(color) >= 5) {
            found_win = true;
        }

        if (found_win) {
            undo_captures(board, color, cap_info);
            board.remove_stone(threat_move);
            return true;
        }

        // Breakable five: skip — not a guaranteed win
        if (is_breakable_five) {
            undo_captures(board, color, cap_info);
            board.remove_stone(threat_move);
            sequence.pop_back();
            continue;
        }

        // Check if captures freed positions that let defender win immediately
        if (cap_info.count > 0) {
            bool defender_wins = false;
            Stone defender = opponent(color);
            for (size_t i = 0; i < cap_info.count; i++) {
                if (creates_five_or_more(board, cap_info.positions[i], defender)) {
                    defender_wins = true;
                    break;
                }
            }
            if (defender_wins) {
                undo_captures(board, color, cap_info);
                board.remove_stone(threat_move);
                sequence.pop_back();
                continue;
            }
        }

        // Try VCF from this position (faster path to victory)
        std::vector<Pos> vcf_seq;
        if (vcf_search(board, color, 0, vcf_seq)) {
            sequence.insert(sequence.end(), vcf_seq.begin(), vcf_seq.end());
            undo_captures(board, color, cap_info);
            board.remove_stone(threat_move);
            return true;
        }

        // Find all possible defenses
        std::vector<Pos> defenses = find_threat_defenses(board, threat_move, color);

        if (defenses.empty()) {
            undo_captures(board, color, cap_info);
            board.remove_stone(threat_move);
            return true;
        }

        // For VCT, we need to beat ALL possible defenses
        bool all_defenses_beaten = true;
        Stone defender = opponent(color);
        for (const Pos& defense : defenses) {
            board.place_stone(defense, defender);
            CaptureInfo def_cap = execute_captures_fast(board, defense, defender);

            // Recursively try to find a win against this defense
            std::vector<Pos> sub_sequence = sequence;
            bool beaten = vct_search(board, color, depth + 1, sub_sequence);

            // Unmake defense
            undo_captures(board, defender, def_cap);
            board.remove_stone(defense);

            if (!beaten) {
                all_defenses_beaten = false;
                break;
            }
        }

        // Unmake threat move
        undo_captures(board, color, cap_info);
        board.remove_stone(threat_move);

        if (all_defenses_beaten) {
            return true;
        }

        sequence.pop_back();
    }

    return false;
}

std::vector<Pos> ThreatSearcher::find_all_threats(const Board& board, Stone color) const {
    std::vector<Pos> winning_moves;
    std::vector<Pos> four_threats;
    std::vector<Pos> three_threats;

    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            Pos pos{uint8_t(r), uint8_t(c)};
            if (!is_valid_move(board, pos, color)) {
                continue;
            }

            // Prioritize winning moves > fours > open-threes
            if (creates_five_or_more(board, pos, color)) {
                winning_moves.push_back(pos);
            } else if (creates_four(board, pos, color)) {
                four_threats.push_back(pos);
            } else if (creates_open_three(board, pos, color)) {
                three_threats.push_back(pos);
            }
        }
    }

    // Combine with priority order
    winning_moves.insert(winning_moves.end(), four_threats.begin(), four_threats.end());
    winning_moves.insert(winning_moves.end(), three_threats.begin(), three_threats.end());
    return winning_moves;
}

bool ThreatSearcher::creates_open_three(const Board& board, Pos pos, Stone color) const {
    for (int dir = 0; dir < 4; dir++) {
        int dr = DIRECTIONS[dir][0];
        int dc = DIRECTIONS[dir][1];
        int count = 1;
        int open_ends = 0;

        // Scan positive direction
        int r = pos.row + dr;
        int c = pos.col + dc;
        while (Pos::is_valid(r, c)) {
            Pos p{uint8_t(r), uint8_t(c)};
            Stone s = board.get(p);
            if (s == color) {
                count++;
            } else if (s == Stone::Empty) {
                open_ends++;
                break;
            } else {
                break;
            }
            r += dr;
            c += dc;
        }

        // Scan negative direction
        r = pos.row - dr;
        c = pos.col - dc;
        while (Pos::is_valid(r, c)) {
            Pos p{uint8_t(r), uint8_t(c)};
            Stone s = board.get(p);
            if (s == color) {
                count++;
            } else if (s == Stone::Empty) {
                open_ends++;
                break;
            } else {
                break;
            }
            r -= dr;
            c -= dc;
        }

        // Open three needs exactly 3 stones and both ends open
        if (count == 3 && open_ends == 2) {
            return true;
        }
    }

    return false;
}

std::vector<Pos> ThreatSearcher::find_threat_defenses(
    const Board& board, Pos threat_move, Stone attacker) const {
    Stone defender = opponent(attacker);
    std::vector<Pos> defenses;
    std::vector<Pos> threat_positions;

    // Find blocking positions along each direction
    // Also collect the positions of the threat-pattern stones
    for (int dir = 0; dir < 4; dir++) {
        int dr = DIRECTIONS[dir][0];
        int dc = DIRECTIONS[dir][1];
        std::vector<Pos> line_positions;
        line_positions.push_back(threat_move);
        int line_count = 1;

        // Check both extension directions from the threat
        for (int sign : {-1, 1}) {
            int r = threat_move.row;
            int c = threat_move.col;

            // Move to the end of the line of attacker stones
            while (Pos::is_valid(r + dr * sign, c + dc * sign)) {
                Pos next{uint8_t(r + dr * sign), uint8_t(c + dc * sign)};
                if (board.get(next) == attacker) {
                    r += dr * sign;
                    c += dc * sign;
                    line_positions.push_back(next);
                    line_count++;
                } else {
                    break;
                }
            }

            // The next position after the line is a potential defense
            int def_r = r + dr * sign;
            int def_c = c + dc * sign;
            if (Pos::is_valid(def_r, def_c)) {
                Pos p{uint8_t(def_r), uint8_t(def_c)};
                if (board.get(p) == Stone::Empty && is_valid_move(board, p, defender)) {
                    defenses.push_back(p);
                }
            }
        }

        // If this direction has a meaningful threat (3+ stones), collect positions
        if (line_count >= 3) {
            threat_positions.insert(threat_positions.end(), line_positions.begin(), line_positions.end());
        }
    }

    // Deduplicate threat_positions
    std::sort(threat_positions.begin(), threat_positions.end());
    threat_positions.erase(std::unique(threat_positions.begin(), threat_positions.end()), threat_positions.end());

    // Add capture defenses that actually break the threat
    // Only include captures that remove stones that are part of the threat pattern
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            Pos pos{uint8_t(r), uint8_t(c)};
            if (!is_valid_move(board, pos, defender)) {
                continue;
            }
            std::vector<Pos> captured = get_captured_positions(board, pos, defender);
            if (!captured.empty()) {
                // Only add as defense if any captured stone is part of the threat pattern
                bool breaks_threat = std::any_of(captured.begin(), captured.end(),
                    [&threat_positions](const Pos& cap) {
                        return std::find(threat_positions.begin(), threat_positions.end(), cap) != threat_positions.end();
                    });
                if (breaks_threat) {
                    defenses.push_back(pos);
                }
            }
        }
    }

    std::sort(defenses.begin(), defenses.end());
    defenses.erase(std::unique(defenses.begin(), defenses.end()), defenses.end());
    return defenses;
}

} // namespace gomoku
