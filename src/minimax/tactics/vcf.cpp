// VCF (Victory by Continuous Fours) -- detection de victoires forcees
//
// On cherche a gagner en enchainant uniquement des "quatres" (4 pierres
// alignees avec un bout ouvert). L'adversaire est FORCE de bloquer chaque
// quatre, donc on controle entierement la partie. Si a un moment
// l'adversaire ne peut plus bloquer, on gagne. L'arbre est tres etroit
// (une seule reponse possible a chaque etape).
//
// Lance AVANT l'alpha-beta : si on trouve une victoire forcee, pas besoin
// de chercher plus loin.
//
// Subtilite Ninuki : les captures peuvent servir de defense. Un adversaire
// menace par un quatre peut capturer une paire du quatre au lieu de bloquer,
// ou s'il a 3+ captures, toute capture le rapproche d'une victoire par
// captures et compte comme defense.

#include "gomoku/minimax/vcf.hpp"
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

// Check if placing stone at pos would capture any stone from the targets list.
// Zero-allocation alternative to get_captured_positions + std::find.
static bool captures_any_of(const Board& board, Pos pos, Stone stone,
                            const Pos* targets, int target_count) {
    Stone opp = opponent(stone);
    for (const auto& dir : DIRECTIONS) {
        for (int sign : {-1, 1}) {
            int dr = dir[0] * sign;
            int dc = dir[1] * sign;
            int r3 = pos.row + dr * 3;
            int c3 = pos.col + dc * 3;
            if (!Pos::is_valid(r3, c3)) continue;
            Pos pos1{uint8_t(pos.row + dr),     uint8_t(pos.col + dc)};
            Pos pos2{uint8_t(pos.row + dr * 2), uint8_t(pos.col + dc * 2)};
            Pos pos3{uint8_t(r3),               uint8_t(c3)};
            if (board.get(pos1) == opp && board.get(pos2) == opp && board.get(pos3) == stone) {
                for (int i = 0; i < target_count; ++i) {
                    if (targets[i] == pos1 || targets[i] == pos2) return true;
                }
            }
        }
    }
    return false;
}

ThreatSearcher::ThreatSearcher()
    : max_vcf_depth_(30), nodes_(0) {}

ThreatSearcher::ThreatSearcher(uint8_t vcf_depth, uint8_t /*vct_depth*/)
    : max_vcf_depth_(vcf_depth), nodes_(0) {}

bool ThreatSearcher::is_timed_out() const {
    if (search_time_limit_ms_ == 0) return false;
    if ((nodes_ & 255) != 0) return false; // check every 256 nodes
    auto elapsed = std::chrono::steady_clock::now() - search_start_;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
        >= static_cast<int64_t>(search_time_limit_ms_);
}

ThreatResult ThreatSearcher::search_vcf(const Board& board, Stone color,
                                         uint64_t time_limit_ms) {
    nodes_ = 0;
    search_start_ = std::chrono::steady_clock::now();
    search_time_limit_ms_ = time_limit_ms;
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

    if (depth > max_vcf_depth_ || is_timed_out()) {
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
            // Use find_five_line_at_pos (scans 4 dirs from known pos)
            // instead of find_five_positions (scans ALL stones).
            auto five = find_five_line_at_pos(board, threat_move, color);
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

    // Find capture moves as defenses (zero-allocation path)
    // In Ninuki-renju, the defender can ignore the four and capture instead:
    // - Captures that break the four (remove stones from the four pattern)
    // - ANY capture when defender has 3+ captures (closing in on capture-win)
    bool capture_is_strategic = defender_captures >= 3;
    int four_pos_count = static_cast<int>(four_positions.size());
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            Pos pos{uint8_t(r), uint8_t(c)};
            if (!is_valid_move(board, pos, defender)) {
                continue;
            }

            if (capture_is_strategic) {
                // Any capture is a valid defense when close to capture-win
                if (has_capture(board, pos, defender)) {
                    defenses.push_back(pos);
                }
            } else if (four_pos_count > 0) {
                // Only captures that break the four pattern
                if (captures_any_of(board, pos, defender,
                                    four_positions.data(), four_pos_count)) {
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
