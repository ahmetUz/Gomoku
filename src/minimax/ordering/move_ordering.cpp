// Tri des coups candidats -- scoring et generation
//
// Le tri des coups est l'element le plus important pour la performance de
// l'elagage alpha-beta. Si le meilleur coup est essaye en premier, toutes
// les branches alternatives sont coupees immediatement.
//
// Contenu :
// - MoveAnalysis          : resultat de l'analyse directionnelle
// - analyze_move_patterns : scan des 4 directions + captures/vulnerabilites
// - compute_priority      : echelle de priorite + killer/history/countermove
// - score_move            : orchestrateur (TT move → analyse → priorite)
// - generate_moves_ordered: genere les candidats dans un rayon de 2

#include "../minimax_internal.hpp"

namespace gomoku {

// =========================================================================
// score_move -- evaluation de priorite d'un coup
// =========================================================================
// Philosophie "defense d'abord" : echelle de priorite stricte.
// Les coups les plus urgents (cinq, bloquer cinq, captures gagnantes,
// fourchettes) sont toujours en tete.
//
// Heuristiques de tri :
// - TT move : 1 000 000    (meilleur coup de la recherche precedente)
// - Killer moves : 500 000  (coups ayant cause un cutoff a ce ply)
// - Countermove : 400 000   (meilleure reponse au dernier coup adverse)
// - History : accumule au fil de la recherche

// Resultat intermediaire de l'analyse directionnelle d'un coup.
struct MoveAnalysis {
    bool my_five = false;
    bool opp_five = false;
    int32_t my_open_four_count = 0;
    int32_t opp_open_four_count = 0;
    int32_t my_closed_four_count = 0;
    int32_t opp_closed_four_count = 0;
    int32_t my_open_three_count = 0;
    int32_t opp_open_three_count = 0;
    int32_t my_two_score = 0;
    int32_t my_developing_dirs = 0;
    int32_t opp_developing_dirs = 0;
    int32_t my_capture_pairs = 0;
    int32_t opp_capture_pairs = 0;
    int32_t proximity = 0;
    int32_t vuln_count = 0;
    int32_t setup_vuln_count = 0;
    int32_t imm_cap_penalty = 0;
    uint8_t my_captures = 0;
    uint8_t opp_captures = 0;
};

// Analyse directionnelle : scan des 4 directions + captures/vulnerabilites.
static MoveAnalysis analyze_move_patterns(
    const Board& board, const Bitboard& my_bb, const Bitboard& opp_bb,
    Pos mov, Stone color, Stone opp) {

    MoveAnalysis a;
    a.my_captures = board.captures(color);
    a.opp_captures = board.captures(opp);

    constexpr int8_t sz = static_cast<int8_t>(BOARD_SIZE);
    constexpr int8_t dirs[4][2] = {{1,0},{0,1},{1,1},{1,-1}};

    int32_t setup_weight;
    if (a.opp_captures >= 3)      setup_weight = 100000;
    else if (a.opp_captures >= 2) setup_weight = 75000;
    else                          setup_weight = 50000;

    int8_t mr = static_cast<int8_t>(mov.row);
    int8_t mc_col = static_cast<int8_t>(mov.col);

    for (auto& d : dirs) {
        int8_t dr = d[0], dc = d[1];

        // --- Analyse de ligne ---
        auto [mc, mo, mc_gap, mc_consec, oc, oo, oc_gap, oc_consec] =
            WorkerSearcher::count_line_both(my_bb, opp_bb, mov, dr, dc);

        if (mc_consec >= 5) {
            a.my_five = true;
        } else if (mc >= 5 && mc_gap) {
            a.my_open_four_count += 1;
        }
        if (oc_consec >= 5) {
            a.opp_five = true;
        } else if (oc >= 5 && oc_gap) {
            a.opp_open_four_count += 1;
        }
        if (mc == 4) {
            if (mo == 2)      a.my_open_four_count += 1;
            else if (mo == 1) a.my_closed_four_count += 1;
        }
        if (oc == 4) {
            if (oo == 2)      a.opp_open_four_count += 1;
            else if (oo == 1) a.opp_closed_four_count += 1;
        }
        if (mc == 3 && mo == 2) a.my_open_three_count += 1;
        if (oc == 3 && oo == 2) a.opp_open_three_count += 1;
        if (mc == 2) {
            if (mo == 2)      a.my_two_score += 500;
            else if (mo == 1) a.my_two_score += 150;
        }
        if (oc == 2 && oo == 2) a.my_two_score += 200;
        if (mc >= 2 && mo >= 1) ++a.my_developing_dirs;
        if (oc >= 2 && oo >= 1) ++a.opp_developing_dirs;

        // --- Capture + vulnerabilite + proximite (2 signes par direction) ---
        for (int8_t sign : {1, -1}) {
            int8_t sdr = dr * sign, sdc = dc * sign;

            int8_t r1 = mr + sdr,     c1 = mc_col + sdc;
            int8_t r2 = mr + sdr * 2, c2 = mc_col + sdc * 2;
            int8_t r3 = mr + sdr * 3, c3 = mc_col + sdc * 3;

            if (r1 >= 0 && r1 < sz && c1 >= 0 && c1 < sz
                && my_bb.get(Pos{uint8_t(r1), uint8_t(c1)})) {
                a.proximity += 200;
            }

            if (r3 < 0 || r3 >= sz || c3 < 0 || c3 >= sz) continue;

            Pos p1{uint8_t(r1), uint8_t(c1)};
            Pos p2{uint8_t(r2), uint8_t(c2)};
            Pos p3{uint8_t(r3), uint8_t(c3)};

            bool p1_my = my_bb.get(p1), p1_opp = opp_bb.get(p1);
            bool p2_my = my_bb.get(p2), p2_opp = opp_bb.get(p2);
            bool p3_my = my_bb.get(p3), p3_opp = opp_bb.get(p3);

            if (p1_opp && p2_opp && p3_my) ++a.my_capture_pairs;
            if (p1_my && p2_my && p3_opp) ++a.opp_capture_pairs;

            // Vulnerabilite : le coup cree une paire capturable
            int8_t rb = mr - sdr, cb = mc_col - sdc;
            if (rb >= 0 && rb < sz && cb >= 0 && cb < sz) {
                Pos pb{uint8_t(rb), uint8_t(cb)};
                bool pb_empty = !my_bb.get(pb) && !opp_bb.get(pb);
                bool p2_empty = !p2_my && !p2_opp;

                if (pb_empty && p1_my && p2_opp) ++a.vuln_count;
                if (opp_bb.get(pb) && p1_my && p2_empty) ++a.vuln_count;
                if (pb_empty && p1_my && p2_empty) ++a.setup_vuln_count;

                bool p1_empty = !p1_my && !p1_opp;

                if (opp_bb.get(pb) && p1_my && p2_empty)
                    a.imm_cap_penalty += 150000;
                if (pb_empty && p1_my && p2_empty)
                    a.imm_cap_penalty += setup_weight;

                if (my_bb.get(pb)) {
                    int8_t rb2 = mr - sdr * 2, cb2 = mc_col - sdc * 2;
                    if (rb2 >= 0 && rb2 < sz && cb2 >= 0 && cb2 < sz) {
                        Pos pb2{uint8_t(rb2), uint8_t(cb2)};
                        bool pb2_empty = !my_bb.get(pb2) && !opp_bb.get(pb2);
                        if (pb2_empty && p1_opp) ++a.vuln_count;
                        if (opp_bb.get(pb2) && p1_empty) ++a.vuln_count;
                        if (pb2_empty && p1_empty) ++a.setup_vuln_count;
                    }
                }
            }
        }
    }

    return a;
}

// Echelle de priorite + heuristiques (killer, history, countermove).
static int32_t compute_priority(
    const MoveAnalysis& a, Pos mov,
    const Pos killer_moves[64][2], int8_t max_depth, int8_t depth,
    Pos last_move_for_ordering,
    const Pos countermove[2][BOARD_SIZE][BOARD_SIZE],
    const int32_t history[2][BOARD_SIZE][BOARD_SIZE],
    Stone color) {

    int32_t my_total_fours = a.my_open_four_count + a.my_closed_four_count;
    int32_t opp_total_fours = a.opp_open_four_count + a.opp_closed_four_count;

    if (a.my_five) return 900000;
    if (a.opp_five) return 895000;

    if (a.my_capture_pairs > 0 && static_cast<int32_t>(a.my_captures) + a.my_capture_pairs >= 5)
        return 890000;
    if (a.opp_capture_pairs > 0 && static_cast<int32_t>(a.opp_captures) + a.opp_capture_pairs >= 5)
        return 885000;

    if (my_total_fours >= 2) return 880000;
    if (my_total_fours >= 1 && a.my_open_three_count >= 1) return 878000;
    if (a.my_open_four_count >= 1) return 870000;

    if (opp_total_fours >= 2) return 868000;
    if (opp_total_fours >= 1 && a.opp_open_three_count >= 1) return 866000;
    if (a.opp_open_four_count >= 1) return 860000;

    if (a.opp_capture_pairs > 0 && a.opp_captures >= 3) return 855000;
    if (a.opp_capture_pairs > 0 && a.opp_captures >= 2) return 845000;

    if (a.my_open_three_count >= 2) return 840000;
    if (a.opp_open_three_count >= 2) return 838000;

    if (a.my_closed_four_count >= 1) return 830000;
    if (a.opp_closed_four_count >= 1) return 820000;
    if (a.my_open_three_count >= 1) return 810000;
    if (a.opp_open_three_count >= 1) return 800000;

    if (a.my_capture_pairs > 0) {
        int32_t my_caps = static_cast<int32_t>(a.my_captures);
        int32_t cap_urgency;
        if (my_caps + a.my_capture_pairs >= 4)  cap_urgency = 150000;
        else if (my_caps >= 2)                  cap_urgency = 80000;
        else                                    cap_urgency = 50000;
        return 600000 + a.my_capture_pairs * cap_urgency;
    }

    if (a.opp_capture_pairs > 0) {
        return 550000 + static_cast<int32_t>(a.opp_captures) * 30000;
    }

    // Penalite de capture (vulnerabilite)
    int32_t capture_penalty = 0;
    {
        int32_t total = a.vuln_count + a.setup_vuln_count;
        if (total > 0) {
            int32_t opp_caps_i32 = static_cast<int32_t>(a.opp_captures);
            int32_t base_penalty = 20000;
            int32_t urgency;
            if (opp_caps_i32 >= 3)      urgency = 4;
            else if (opp_caps_i32 >= 2) urgency = 2;
            else                        urgency = 1;
            capture_penalty = a.vuln_count * base_penalty * urgency
                            + a.setup_vuln_count * (base_penalty / 2) * urgency;
        }
        capture_penalty += a.imm_cap_penalty;
    }

    // Killer moves
    auto ply = static_cast<size_t>(std::max(int8_t(max_depth - depth), int8_t(0)));
    if (ply < 64) {
        if (!killer_moves[ply][0].is_sentinel() && killer_moves[ply][0] == mov)
            return 500000 - capture_penalty;
        if (!killer_moves[ply][1].is_sentinel() && killer_moves[ply][1] == mov)
            return 490000 - capture_penalty;
    }

    // Countermove bonus
    if (!last_move_for_ordering.is_sentinel()) {
        Pos lm = last_move_for_ordering;
        int opp_idx = (color == Stone::Black) ? 1 : 0;
        if (!countermove[opp_idx][lm.row][lm.col].is_sentinel()
            && countermove[opp_idx][lm.row][lm.col] == mov) {
            return 400000 - capture_penalty;
        }
    }

    int cidx = (color == Stone::Black) ? 0 : 1;
    int32_t hist = history[cidx][mov.row][mov.col];

    int32_t center = BOARD_SIZE / 2;
    int32_t dist = std::abs(static_cast<int32_t>(mov.row) - center)
                 + std::abs(static_cast<int32_t>(mov.col) - center);
    int32_t center_bonus = (18 - dist) * 25;

    int32_t development_bonus;
    switch (a.my_developing_dirs) {
        case 0: case 1: development_bonus = 0; break;
        case 2: development_bonus = 50000; break;
        default: development_bonus = 100000; break;
    }
    int32_t disruption_bonus;
    switch (a.opp_developing_dirs) {
        case 0: case 1: disruption_bonus = 0; break;
        case 2: disruption_bonus = 30000; break;
        default: disruption_bonus = 80000; break;
    }

    return hist + center_bonus + a.my_two_score + a.proximity
         + development_bonus + disruption_bonus - capture_penalty;
}

// score_move : orchestrateur qui appelle analyze + compute.
int32_t WorkerSearcher::score_move(
    const Board& board, Pos mov, Stone color,
    Pos tt_move, int8_t depth) const {

    if (!tt_move.is_sentinel() && tt_move == mov) return 1000000;

    Stone opp = opponent(color);
    const Bitboard& my_bb = *board.stones(color);
    const Bitboard& opp_bb = *board.stones(opp);

    MoveAnalysis analysis = analyze_move_patterns(board, my_bb, opp_bb, mov, color, opp);

    return compute_priority(
        analysis, mov, killer_moves, max_depth, depth,
        last_move_for_ordering, countermove, history, color);
}

// =========================================================================
// generate_moves_ordered -- generation de coups candidats
// =========================================================================
// Genere tous les coups dans un rayon de 2 autour des pierres existantes.
// Tri partiel : seuls les k premiers sont garantis tries.

MoveList WorkerSearcher::generate_moves_ordered(
    const Board& board, Stone color,
    Pos tt_move, int8_t depth,
    size_t sort_limit) const {

    MoveList moves;

    if (board.is_board_empty()) {
        moves.push_back(Pos{9, 9}, 1000000);
        moves.top_score = 0;
        return moves;
    }

    bool seen[BOARD_SIZE][BOARD_SIZE] = {};
    constexpr int32_t radius = 2;

    // Single loop over both colors (deduplication of black/white iteration).
    const Bitboard* bitboards[2] = {&board.black, &board.white};
    for (const auto* bb : bitboards) {
        for (Pos pos : *bb) {
            for (int32_t dr = -radius; dr <= radius; ++dr) {
                for (int32_t dc = -radius; dc <= radius; ++dc) {
                    int32_t r = static_cast<int32_t>(pos.row) + dr;
                    int32_t c = static_cast<int32_t>(pos.col) + dc;
                    if (!Pos::is_valid(r, c)) continue;
                    auto r_usize = static_cast<size_t>(r);
                    auto c_usize = static_cast<size_t>(c);
                    if (seen[r_usize][c_usize]) continue;
                    seen[r_usize][c_usize] = true;

                    Pos new_pos{uint8_t(r), uint8_t(c)};
                    if (board.is_empty(new_pos)) {
                        int32_t s = score_move(board, new_pos, color, tt_move, depth);
                        moves.push_back(new_pos, s);
                    }
                }
            }
        }
    }

    moves.partial_sort_descending(sort_limit);
    return moves;
}

} // namespace gomoku
