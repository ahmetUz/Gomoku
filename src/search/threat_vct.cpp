// VCT (Victory by Continuous Threats) -- victoire forcee par menaces
//
// Version plus generale du VCF qui inclut aussi les "trois ouverts"
// comme menaces. L'arbre est plus large (l'adversaire a plus de
// reponses possibles) mais peut trouver des victoires que le VCF rate.
//
// Non utilise dans le pipeline actuel (l'alpha-beta couvre ces cas),
// mais conserve comme feature implementee.

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

// Check if placing stone at pos would capture any stone from the targets list.
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
        board.place_stone(threat_move, color);
        CaptureInfo cap_info = execute_captures_fast(board, threat_move, color);

        sequence.push_back(threat_move);

        // Check for immediate win
        bool found_win = false;
        bool is_breakable_five = false;
        if (has_five_at_pos(board, threat_move, color)) {
            auto five = find_five_line_at_pos(board, threat_move, color);
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
        size_t seq_size_before_defense = sequence.size();
        for (const Pos& defense : defenses) {
            board.place_stone(defense, defender);
            CaptureInfo def_cap = execute_captures_fast(board, defense, defender);

            bool beaten = vct_search(board, color, depth + 1, sequence);

            undo_captures(board, defender, def_cap);
            board.remove_stone(defense);
            sequence.resize(seq_size_before_defense);

            if (!beaten) {
                all_defenses_beaten = false;
                break;
            }
        }

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

            if (creates_five_or_more(board, pos, color)) {
                winning_moves.push_back(pos);
            } else if (creates_four(board, pos, color)) {
                four_threats.push_back(pos);
            } else if (creates_open_three(board, pos, color)) {
                three_threats.push_back(pos);
            }
        }
    }

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

    for (int dir = 0; dir < 4; dir++) {
        int dr = DIRECTIONS[dir][0];
        int dc = DIRECTIONS[dir][1];
        std::vector<Pos> line_positions;
        line_positions.push_back(threat_move);
        int line_count = 1;

        for (int sign : {-1, 1}) {
            int r = threat_move.row;
            int c = threat_move.col;

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

            int def_r = r + dr * sign;
            int def_c = c + dc * sign;
            if (Pos::is_valid(def_r, def_c)) {
                Pos p{uint8_t(def_r), uint8_t(def_c)};
                if (board.get(p) == Stone::Empty && is_valid_move(board, p, defender)) {
                    defenses.push_back(p);
                }
            }
        }

        if (line_count >= 3) {
            threat_positions.insert(threat_positions.end(), line_positions.begin(), line_positions.end());
        }
    }

    std::sort(threat_positions.begin(), threat_positions.end());
    threat_positions.erase(std::unique(threat_positions.begin(), threat_positions.end()), threat_positions.end());

    int threat_pos_count = static_cast<int>(threat_positions.size());
    if (threat_pos_count > 0) {
        for (int r = 0; r < BOARD_SIZE; r++) {
            for (int c = 0; c < BOARD_SIZE; c++) {
                Pos pos{uint8_t(r), uint8_t(c)};
                if (!is_valid_move(board, pos, defender)) {
                    continue;
                }
                if (captures_any_of(board, pos, defender,
                                    threat_positions.data(), threat_pos_count)) {
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
