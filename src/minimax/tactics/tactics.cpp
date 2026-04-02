// Analyse tactique locale -- detection de menaces et coups forcants
//
// Fonctions d'analyse locale utilisees par le negamax :
// - count_line_both         : scan bidirectionnel (pierres + trous, 2 couleurs)
// - move_creates_four       : ce coup cree-t-il un quatre ? (extensions)
// - is_threatened           : position sous menace ? (NMP), via 3 helpers :
//     has_line_threat, has_gap_threat, has_capture_setup_threat
// - generate_forcing_moves  : coups forcants pour la quiescence

#include "../minimax_internal.hpp"

namespace gomoku {

// =========================================================================
// count_line_both -- analyse de ligne bidirectionnelle
// =========================================================================
//
// Scanne une ligne dans les deux sens a partir de `pos`, dans la direction
// (dr, dc), pour les deux couleurs simultanement.
//
// Parametres :
//   my_bb   : bitboard du joueur courant
//   opp_bb  : bitboard de l'adversaire
//   pos     : position centrale (le coup qu'on evalue)
//   dr, dc  : direction du scan (ex: 1,0 = vertical, 0,1 = horizontal,
//             1,1 = diagonale, 1,-1 = anti-diagonale)
//
// Retour (LineBothResult, 8 champs) :
//
//   mc          : nombre total de pierres "my" alignees sur la ligne
//                 (inclut pos + pierres dans les 2 sens, meme a travers un trou)
//                 Exemple : X X _ X X  =>  mc = 5  (pos au centre)
//
//   mo          : nombre de bouts ouverts (open ends) pour "my" (0, 1 ou 2)
//                 Un bout est ouvert si la ligne se termine sur une case vide.
//                 Exemple : _ X X X _  =>  mo = 2
//                           O X X X _  =>  mo = 1
//
//   m_gap       : true si la ligne "my" contient un trou (une case vide entre
//                 deux pierres). Un seul trou max est detecte.
//                 Exemple : X X _ X  =>  m_gap = true
//                           X X X    =>  m_gap = false
//
//   mc_consec   : nombre de pierres "my" consecutives (sans trou) autour de pos.
//                 C'est la sous-sequence ininterrompue qui passe par pos.
//                 Exemple : X X _ X X  =>  mc_consec = 3 (si pos est au centre)
//
//   oc          : idem mc, mais pour l'adversaire
//   oo          : idem mo, mais pour l'adversaire
//   oc_gap      : idem m_gap, mais pour l'adversaire (= o_gap dans le struct)
//   oc_consec   : idem mc_consec, mais pour l'adversaire
//
// Fonctionnement :
//   1. Scan dans le sens positif (dr, dc) depuis pos
//   2. Scan dans le sens negatif (-dr, -dc) depuis pos
//   Pour chaque sens et chaque couleur, on avance tant que :
//     - on rencontre des pierres de cette couleur => on incremente le compteur
//     - on rencontre une case vide :
//         * si pas encore de trou ET la case suivante est une pierre alliee
//           => on marque un trou (gap) et on continue
//         * sinon => bout ouvert (+1 a mo/oo) et on arrete
//     - on rencontre une pierre adverse => on arrete pour cette couleur
//
// Utilise par : move_ordering (score_move) et evaluation tactique

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
//
// Decompose en 3 helpers : lignes consecutives, patterns a trou, captures.

// Quatre ou trois ouvert consecutif de l'adversaire pres de last_move.
static bool has_line_threat(const Board& board, Stone opp, Pos last_move) {
    constexpr int8_t sz = static_cast<int8_t>(BOARD_SIZE);
    constexpr int8_t dirs[4][2] = {{1,0},{0,1},{1,1},{1,-1}};

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
    return false;
}

// Pattern a trou (OO_O, O_OO) de l'adversaire pres de last_move.
static bool has_gap_threat(const Board& board, Stone opp, Pos last_move) {
    constexpr int8_t sz = static_cast<int8_t>(BOARD_SIZE);
    constexpr int8_t dirs[4][2] = {{1,0},{0,1},{1,1},{1,-1}};

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
    return false;
}

// L'adversaire encadre une de nos paires (menace de capture).
static bool has_capture_setup_threat(const Board& board, Stone color, Pos last_move) {
    constexpr int8_t sz = static_cast<int8_t>(BOARD_SIZE);
    constexpr int8_t dirs[4][2] = {{1,0},{0,1},{1,1},{1,-1}};

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
                if (s1 == color && s2 == color && s3 == Stone::Empty) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Orchestrateur : position sous menace si captures adverses >= 4, ou menace de ligne/gap/capture.
bool WorkerSearcher::is_threatened(const Board& board, Stone color, Pos last_move) {
    Stone opp = opponent(color);
    if (board.captures(opp) >= 4) return true;
    return has_line_threat(board, opp, last_move)
        || has_gap_threat(board, opp, last_move)
        || has_capture_setup_threat(board, color, last_move);
}

// =========================================================================
// generate_forcing_moves -- coups forcants pour la quiescence
// =========================================================================
// Genere les cinq, quatre et capture-wins dans un rayon de 2.
// Utilisee par quiescence() pour eviter l'effet d'horizon.

MoveList WorkerSearcher::generate_forcing_moves(
    const Board& board, Stone color, Stone opp, bool fours_allowed) const {

    constexpr int8_t sz = static_cast<int8_t>(BOARD_SIZE);
    constexpr int8_t dirs[4][2] = {{1,0},{0,1},{1,1},{1,-1}};

    MoveList forcing_moves;
    bool seen[BOARD_SIZE][BOARD_SIZE] = {};

    const Bitboard* bitboards[2] = {&board.black, &board.white};
    for (const auto* bb : bitboards) {
        for (Pos stone_pos : *bb) {
            for (int32_t dr = -2; dr <= 2; ++dr) {
                for (int32_t dc = -2; dc <= 2; ++dc) {
                    int32_t r = static_cast<int32_t>(stone_pos.row) + dr;
                    int32_t c = static_cast<int32_t>(stone_pos.col) + dc;
                    if (!Pos::is_valid(r, c)) continue;
                    auto ru = static_cast<size_t>(r);
                    auto cu = static_cast<size_t>(c);
                    if (seen[ru][cu]) continue;
                    seen[ru][cu] = true;

                    Pos pos{uint8_t(r), uint8_t(c)};
                    if (board.get(pos) != Stone::Empty) continue;
                    if (!is_valid_move(board, pos, color)) continue;

                    int32_t priority = 0;
                    for (auto& dd : dirs) {
                        int8_t ddr = dd[0], ddc = dd[1];
                        // Notre ligne
                        int32_t mc = 1;
                        int8_t rr = static_cast<int8_t>(pos.row) + ddr;
                        int8_t cc = static_cast<int8_t>(pos.col) + ddc;
                        while (rr >= 0 && rr < sz && cc >= 0 && cc < sz
                               && board.get(Pos{uint8_t(rr), uint8_t(cc)}) == color) {
                            ++mc; rr += ddr; cc += ddc;
                        }
                        int32_t mo_p = (rr >= 0 && rr < sz && cc >= 0 && cc < sz
                            && board.get(Pos{uint8_t(rr), uint8_t(cc)}) == Stone::Empty) ? 1 : 0;
                        rr = static_cast<int8_t>(pos.row) - ddr;
                        cc = static_cast<int8_t>(pos.col) - ddc;
                        while (rr >= 0 && rr < sz && cc >= 0 && cc < sz
                               && board.get(Pos{uint8_t(rr), uint8_t(cc)}) == color) {
                            ++mc; rr -= ddr; cc -= ddc;
                        }
                        mo_p += (rr >= 0 && rr < sz && cc >= 0 && cc < sz
                            && board.get(Pos{uint8_t(rr), uint8_t(cc)}) == Stone::Empty) ? 1 : 0;

                        if (mc >= 5) { priority = 900; break; }
                        if (fours_allowed && mc == 4 && mo_p >= 1) {
                            priority = std::max(priority, (mo_p == 2) ? 800 : 700);
                        }

                        // Ligne adverse
                        int32_t oc = 1;
                        rr = static_cast<int8_t>(pos.row) + ddr;
                        cc = static_cast<int8_t>(pos.col) + ddc;
                        while (rr >= 0 && rr < sz && cc >= 0 && cc < sz
                               && board.get(Pos{uint8_t(rr), uint8_t(cc)}) == opp) {
                            ++oc; rr += ddr; cc += ddc;
                        }
                        rr = static_cast<int8_t>(pos.row) - ddr;
                        cc = static_cast<int8_t>(pos.col) - ddc;
                        while (rr >= 0 && rr < sz && cc >= 0 && cc < sz
                               && board.get(Pos{uint8_t(rr), uint8_t(cc)}) == opp) {
                            ++oc; rr -= ddr; cc -= ddc;
                        }
                        if (oc >= 5) { priority = std::max(priority, 850); }
                    }

                    // Victoire par capture
                    if (priority == 0) {
                        uint8_t cap_count = count_captures_fast(board, pos, color);
                        if (cap_count > 0 && board.captures(color) + cap_count >= 5) {
                            priority = 890;
                        }
                    }

                    if (priority > 0) {
                        forcing_moves.push_back(pos, priority);
                    }
                }
            }
        }
    }

    return forcing_moves;
}

} // namespace gomoku
