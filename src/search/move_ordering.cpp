// Generation et tri des coups candidats
//
// Le tri des coups est l'element le plus important pour la performance de
// l'elagage alpha-beta. Si le meilleur coup est essaye en premier, toutes
// les branches alternatives sont coupees immediatement.
//
// Contenu :
// - generate_moves_ordered : genere les candidats dans un rayon de 2
// - score_move : attribue un score de priorite a chaque coup
// - count_line_both : analyse de ligne bidirectionnelle (optimisation)
// - move_creates_four : detection de creation de quatre (threat extension)
// - is_threatened : detection de menace (pour le Null Move Pruning)

#include "worker_searcher.hpp"

namespace gomoku {

// =========================================================================
// count_line_both -- analyse de ligne bidirectionnelle
// =========================================================================
// Scanne une ligne dans une direction donnee a partir d'une position,
// en comptant simultanement les pierres des deux couleurs. Detecte aussi
// les "trous" (gaps) dans les lignes (ex: OO_O = 3 pierres avec un trou).

WorkerSearcher::LineBothResult WorkerSearcher::count_line_both(
    const Bitboard& my_bb, const Bitboard& opp_bb,
    Pos pos, int8_t dr, int8_t dc) {

    constexpr int8_t sz = static_cast<int8_t>(BOARD_SIZE);

    int32_t mc = 1, mo = 0;
    bool m_gap = false;
    int32_t mc_pos = 0, mc_neg = 0;

    int32_t oc = 1, oo = 0;
    bool o_gap = false;
    int32_t oc_pos = 0, oc_neg = 0;

    // Scan one direction (positive or negative).
    // Template-like lambda avoids duplicating the ~40-line scan body.
    auto scan_direction = [&](int8_t step_r, int8_t step_c,
                              int32_t& mc_dir, int32_t& oc_dir) {
        int8_t r = static_cast<int8_t>(pos.row) + step_r;
        int8_t c = static_cast<int8_t>(pos.col) + step_c;
        bool my_active = true, my_consec = true;
        bool opp_active = true, opp_consec = true;

        while ((my_active || opp_active) && r >= 0 && r < sz && c >= 0 && c < sz) {
            Pos p{uint8_t(r), uint8_t(c)};
            bool is_my = my_bb.get(p);
            bool is_opp = is_my ? false : opp_bb.get(p);

            if (is_my) {
                if (my_active) {
                    ++mc;
                    if (my_consec) ++mc_dir;
                }
                if (opp_active) opp_active = false;
            } else if (is_opp) {
                if (opp_active) {
                    ++oc;
                    if (opp_consec) ++oc_dir;
                }
                if (my_active) my_active = false;
            } else {
                // Empty cell
                if (my_active) {
                    if (!m_gap) {
                        my_consec = false;
                        int8_t nr = r + step_r;
                        int8_t nc = c + step_c;
                        if (nr >= 0 && nr < sz && nc >= 0 && nc < sz
                            && my_bb.get(Pos{uint8_t(nr), uint8_t(nc)})) {
                            m_gap = true;
                        } else {
                            ++mo;
                            my_active = false;
                        }
                    } else {
                        ++mo;
                        my_active = false;
                    }
                }
                if (opp_active) {
                    if (!o_gap) {
                        opp_consec = false;
                        int8_t nr = r + step_r;
                        int8_t nc = c + step_c;
                        if (nr >= 0 && nr < sz && nc >= 0 && nc < sz
                            && opp_bb.get(Pos{uint8_t(nr), uint8_t(nc)})) {
                            o_gap = true;
                        } else {
                            ++oo;
                            opp_active = false;
                        }
                    } else {
                        ++oo;
                        opp_active = false;
                    }
                }
            }
            r += step_r;
            c += step_c;
        }
    };

    scan_direction(dr, dc, mc_pos, oc_pos);
    scan_direction(static_cast<int8_t>(-dr), static_cast<int8_t>(-dc), mc_neg, oc_neg);

    return {mc, mo, m_gap, 1 + mc_pos + mc_neg,
            oc, oo, o_gap, 1 + oc_pos + oc_neg};
}

// =========================================================================
// move_creates_four -- detection de creation de quatre
// =========================================================================
// Un quatre (4 pierres alignees, >= 1 bout ouvert) est une menace forcante.
// Sert pour le threat extension (+1 ply) dans alpha-beta et search_root.

bool WorkerSearcher::move_creates_four(const Board& board, Pos pos, Stone color) {
    constexpr int8_t sz = static_cast<int8_t>(BOARD_SIZE);
    constexpr int8_t dirs[4][2] = {{1,0},{0,1},{1,1},{1,-1}};

    for (auto& d : dirs) {
        int8_t dr = d[0], dc = d[1];
        int32_t count = 1;
        int32_t open_ends = 0;

        int8_t r = static_cast<int8_t>(pos.row) + dr;
        int8_t c = static_cast<int8_t>(pos.col) + dc;
        while (r >= 0 && r < sz && c >= 0 && c < sz) {
            if (board.get(Pos{uint8_t(r), uint8_t(c)}) == color) {
                ++count; r += dr; c += dc;
            } else {
                if (board.get(Pos{uint8_t(r), uint8_t(c)}) == Stone::Empty)
                    ++open_ends;
                break;
            }
        }
        r = static_cast<int8_t>(pos.row) - dr;
        c = static_cast<int8_t>(pos.col) - dc;
        while (r >= 0 && r < sz && c >= 0 && c < sz) {
            if (board.get(Pos{uint8_t(r), uint8_t(c)}) == color) {
                ++count; r -= dr; c -= dc;
            } else {
                if (board.get(Pos{uint8_t(r), uint8_t(c)}) == Stone::Empty)
                    ++open_ends;
                break;
            }
        }
        if (count == 4 && open_ends >= 1) return true;
    }
    return false;
}

// =========================================================================
// is_threatened -- detection de menace pour NMP
// =========================================================================
// Determine si la position est "sous pression" : l'adversaire a une menace
// immediate (quatre, trois ouvert, menace de capture). On n'ose pas
// "passer son tour" (NMP) si on est deja menace.

bool WorkerSearcher::is_threatened(const Board& board, Stone color, Pos last_move) {
    Stone opp = opponent(color);
    if (board.captures(opp) >= 4) return true;

    constexpr int8_t sz = static_cast<int8_t>(BOARD_SIZE);
    constexpr int8_t dirs[4][2] = {{1,0},{0,1},{1,1},{1,-1}};

    // Check consecutive opponent lines near last_move
    for (auto& d : dirs) {
        int8_t dr = d[0], dc = d[1];
        int32_t count = 1;
        int32_t open_ends = 0;

        int8_t r = static_cast<int8_t>(last_move.row) + dr;
        int8_t c = static_cast<int8_t>(last_move.col) + dc;
        while (r >= 0 && r < sz && c >= 0 && c < sz) {
            if (board.get(Pos{uint8_t(r), uint8_t(c)}) == opp) {
                ++count; r += dr; c += dc;
            } else {
                if (board.get(Pos{uint8_t(r), uint8_t(c)}) == Stone::Empty)
                    ++open_ends;
                break;
            }
        }
        r = static_cast<int8_t>(last_move.row) - dr;
        c = static_cast<int8_t>(last_move.col) - dc;
        while (r >= 0 && r < sz && c >= 0 && c < sz) {
            if (board.get(Pos{uint8_t(r), uint8_t(c)}) == opp) {
                ++count; r -= dr; c -= dc;
            } else {
                if (board.get(Pos{uint8_t(r), uint8_t(c)}) == Stone::Empty)
                    ++open_ends;
                break;
            }
        }
        if (count >= 4 || (count >= 3 && open_ends >= 2)) return true;
    }

    // Gap pattern threat: OO_O or O_OO near last_move
    for (auto& d : dirs) {
        int8_t dr = d[0], dc = d[1];
        for (int8_t sign : {-1, 1}) {
            int8_t sdr = dr * sign;
            int8_t sdc = dc * sign;
            int32_t gap_count = 1;
            bool gap_used = false;
            int32_t gap_open_ends = 0;

            for (int8_t i = 1; i <= 4; ++i) {
                int8_t gr = static_cast<int8_t>(last_move.row) + sdr * i;
                int8_t gc = static_cast<int8_t>(last_move.col) + sdc * i;
                if (gr < 0 || gr >= sz || gc < 0 || gc >= sz) break;
                Stone s = board.get(Pos{uint8_t(gr), uint8_t(gc)});
                if (s == opp) {
                    ++gap_count;
                } else if (s == Stone::Empty && !gap_used) {
                    int8_t nr = gr + sdr;
                    int8_t nc = gc + sdc;
                    if (nr >= 0 && nr < sz && nc >= 0 && nc < sz
                        && board.get(Pos{uint8_t(nr), uint8_t(nc)}) == opp) {
                        gap_used = true;
                        continue;
                    }
                    ++gap_open_ends;
                    break;
                } else {
                    break;
                }
            }
            for (int8_t i = 1; i <= 4; ++i) {
                int8_t gr = static_cast<int8_t>(last_move.row) - sdr * i;
                int8_t gc = static_cast<int8_t>(last_move.col) - sdc * i;
                if (gr < 0 || gr >= sz || gc < 0 || gc >= sz) break;
                Stone s = board.get(Pos{uint8_t(gr), uint8_t(gc)});
                if (s == opp) {
                    ++gap_count;
                } else if (s == Stone::Empty) {
                    ++gap_open_ends;
                    break;
                } else {
                    break;
                }
            }
            if (gap_used && (gap_count >= 4 || (gap_count >= 3 && gap_open_ends >= 2))) {
                return true;
            }
        }
    }

    // Capture setup: opponent brackets our pair
    Stone us = color;
    for (auto& d : dirs) {
        int8_t dr = d[0], dc = d[1];
        for (int8_t sign : {-1, 1}) {
            int8_t sdr = dr * sign;
            int8_t sdc = dc * sign;
            int8_t r1 = static_cast<int8_t>(last_move.row) + sdr;
            int8_t c1 = static_cast<int8_t>(last_move.col) + sdc;
            int8_t r2 = r1 + sdr;
            int8_t c2 = c1 + sdc;
            int8_t r3 = r2 + sdr;
            int8_t c3 = c2 + sdc;
            if (r1 >= 0 && r1 < sz && c1 >= 0 && c1 < sz
                && r2 >= 0 && r2 < sz && c2 >= 0 && c2 < sz
                && r3 >= 0 && r3 < sz && c3 >= 0 && c3 < sz) {
                Stone s1 = board.get(Pos{uint8_t(r1), uint8_t(c1)});
                Stone s2 = board.get(Pos{uint8_t(r2), uint8_t(c2)});
                Stone s3 = board.get(Pos{uint8_t(r3), uint8_t(c3)});
                if (s1 == us && s2 == us && s3 == Stone::Empty) {
                    return true;
                }
            }
        }
    }
    return false;
}

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

int32_t WorkerSearcher::score_move(
    const Board& board, Pos mov, Stone color,
    Pos tt_move, int8_t depth) const {

    Stone opp = opponent(color);

    if (!tt_move.is_sentinel() && tt_move == mov) return 1000000;

    const Bitboard& my_bb = *board.stones(color);
    const Bitboard& opp_bb = *board.stones(opp);

    constexpr int8_t sz = static_cast<int8_t>(BOARD_SIZE);
    constexpr int8_t dirs[4][2] = {{1,0},{0,1},{1,1},{1,-1}};
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

    uint8_t opp_caps = board.captures(opp);
    int32_t setup_weight;
    if (opp_caps >= 3)      setup_weight = 100000;
    else if (opp_caps >= 2) setup_weight = 75000;
    else                    setup_weight = 50000;

    int8_t mr = static_cast<int8_t>(mov.row);
    int8_t mc_col = static_cast<int8_t>(mov.col);

    for (auto& d : dirs) {
        int8_t dr = d[0], dc = d[1];

        // --- Line analysis ---
        auto [mc, mo, mc_gap, mc_consec, oc, oo, oc_gap, oc_consec] =
            count_line_both(my_bb, opp_bb, mov, dr, dc);

        if (mc_consec >= 5) {
            my_five = true;
        } else if (mc >= 5 && mc_gap) {
            my_open_four_count += 1;
        }
        if (oc_consec >= 5) {
            opp_five = true;
        } else if (oc >= 5 && oc_gap) {
            opp_open_four_count += 1;
        }
        if (mc == 4) {
            if (mo == 2)      my_open_four_count += 1;
            else if (mo == 1) my_closed_four_count += 1;
        }
        if (oc == 4) {
            if (oo == 2)      opp_open_four_count += 1;
            else if (oo == 1) opp_closed_four_count += 1;
        }
        if (mc == 3 && mo == 2) my_open_three_count += 1;
        if (oc == 3 && oo == 2) opp_open_three_count += 1;
        if (mc == 2) {
            if (mo == 2)      my_two_score += 500;
            else if (mo == 1) my_two_score += 150;
        }
        if (oc == 2 && oo == 2) my_two_score += 200;
        if (mc >= 2 && mo >= 1) ++my_developing_dirs;
        if (oc >= 2 && oo >= 1) ++opp_developing_dirs;

        // --- Capture + vulnerability + proximity (fused, 2 signs) ---
        for (int8_t sign : {1, -1}) {
            int8_t sdr = dr * sign, sdc = dc * sign;

            int8_t r1 = mr + sdr,     c1 = mc_col + sdc;
            int8_t r2 = mr + sdr * 2, c2 = mc_col + sdc * 2;
            int8_t r3 = mr + sdr * 3, c3 = mc_col + sdc * 3;

            // Proximity: my stone at distance 1
            if (r1 >= 0 && r1 < sz && c1 >= 0 && c1 < sz
                && my_bb.get(Pos{uint8_t(r1), uint8_t(c1)})) {
                proximity += 200;
            }

            if (r3 < 0 || r3 >= sz || c3 < 0 || c3 >= sz) continue;

            Pos p1{uint8_t(r1), uint8_t(c1)};
            Pos p2{uint8_t(r2), uint8_t(c2)};
            Pos p3{uint8_t(r3), uint8_t(c3)};

            bool p1_my = my_bb.get(p1), p1_opp = opp_bb.get(p1);
            bool p2_my = my_bb.get(p2), p2_opp = opp_bb.get(p2);
            bool p3_my = my_bb.get(p3), p3_opp = opp_bb.get(p3);

            if (p1_opp && p2_opp && p3_my) ++my_capture_pairs;
            if (p1_my && p2_my && p3_opp) ++opp_capture_pairs;

            // Vulnerability: mov creates a capturable pair
            int8_t rb = mr - sdr, cb = mc_col - sdc;
            if (rb >= 0 && rb < sz && cb >= 0 && cb < sz) {
                Pos pb{uint8_t(rb), uint8_t(cb)};
                bool pb_empty = !my_bb.get(pb) && !opp_bb.get(pb);
                bool p2_empty = !p2_my && !p2_opp;

                if (pb_empty && p1_my && p2_opp) ++vuln_count;
                if (opp_bb.get(pb) && p1_my && p2_empty) ++vuln_count;
                if (pb_empty && p1_my && p2_empty) ++setup_vuln_count;

                bool p1_empty = !p1_my && !p1_opp;

                if (opp_bb.get(pb) && p1_my && p2_empty)
                    imm_cap_penalty += 150000;
                if (pb_empty && p1_my && p2_empty)
                    imm_cap_penalty += setup_weight;

                if (my_bb.get(pb)) {
                    int8_t rb2 = mr - sdr * 2, cb2 = mc_col - sdc * 2;
                    if (rb2 >= 0 && rb2 < sz && cb2 >= 0 && cb2 < sz) {
                        Pos pb2{uint8_t(rb2), uint8_t(cb2)};
                        bool pb2_empty = !my_bb.get(pb2) && !opp_bb.get(pb2);
                        if (pb2_empty && p1_opp) ++vuln_count;
                        if (opp_bb.get(pb2) && p1_empty) ++vuln_count;
                        if (pb2_empty && p1_empty) ++setup_vuln_count;
                    }
                }
            }
        }
    }

    // --- Priority ladder ---
    int32_t my_total_fours = my_open_four_count + my_closed_four_count;
    int32_t opp_total_fours = opp_open_four_count + opp_closed_four_count;

    if (my_five) return 900000;
    if (opp_five) return 895000;

    if (my_capture_pairs > 0 && static_cast<int32_t>(board.captures(color)) + my_capture_pairs >= 5)
        return 890000;
    if (opp_capture_pairs > 0 && static_cast<int32_t>(opp_caps) + opp_capture_pairs >= 5)
        return 885000;

    if (my_total_fours >= 2) return 880000;
    if (my_total_fours >= 1 && my_open_three_count >= 1) return 878000;
    if (my_open_four_count >= 1) return 870000;

    if (opp_total_fours >= 2) return 868000;
    if (opp_total_fours >= 1 && opp_open_three_count >= 1) return 866000;
    if (opp_open_four_count >= 1) return 860000;

    if (opp_capture_pairs > 0 && opp_caps >= 3) return 855000;
    if (opp_capture_pairs > 0 && opp_caps >= 2) return 845000;

    if (my_open_three_count >= 2) return 840000;
    if (opp_open_three_count >= 2) return 838000;

    if (my_closed_four_count >= 1) return 830000;
    if (opp_closed_four_count >= 1) return 820000;
    if (my_open_three_count >= 1) return 810000;
    if (opp_open_three_count >= 1) return 800000;

    if (my_capture_pairs > 0) {
        int32_t my_caps = static_cast<int32_t>(board.captures(color));
        int32_t cap_urgency;
        if (my_caps + my_capture_pairs >= 4)  cap_urgency = 150000;
        else if (my_caps >= 2)                cap_urgency = 80000;
        else                                  cap_urgency = 50000;
        return 600000 + my_capture_pairs * cap_urgency;
    }

    if (opp_capture_pairs > 0) {
        return 550000 + static_cast<int32_t>(opp_caps) * 30000;
    }

    // Capture penalty from vulnerability scan
    int32_t capture_penalty = 0;
    {
        int32_t total = vuln_count + setup_vuln_count;
        if (total > 0) {
            int32_t opp_caps_i32 = static_cast<int32_t>(opp_caps);
            int32_t base_penalty = 20000;
            int32_t urgency;
            if (opp_caps_i32 >= 3)      urgency = 4;
            else if (opp_caps_i32 >= 2) urgency = 2;
            else                        urgency = 1;
            capture_penalty = vuln_count * base_penalty * urgency
                            + setup_vuln_count * (base_penalty / 2) * urgency;
        }
        capture_penalty += imm_cap_penalty;
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
    switch (my_developing_dirs) {
        case 0: case 1: development_bonus = 0; break;
        case 2: development_bonus = 50000; break;
        default: development_bonus = 100000; break;
    }
    int32_t disruption_bonus;
    switch (opp_developing_dirs) {
        case 0: case 1: disruption_bonus = 0; break;
        case 2: disruption_bonus = 30000; break;
        default: disruption_bonus = 80000; break;
    }

    return hist + center_bonus + my_two_score + proximity
         + development_bonus + disruption_bonus - capture_penalty;
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
