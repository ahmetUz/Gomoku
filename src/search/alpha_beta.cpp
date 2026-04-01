// Recherche recursive : negamax alpha-beta, quiescence, five-break
//
// alpha_beta()        : negamax avec elagage et optimisations
//   - NMP  (Null Move Pruning)   : simuler "passer son tour"
//   - RFP  (Reverse Futility)    : couper si eval >> beta
//   - Razoring                   : tomber en quiescence si eval << alpha
//   - IID  (Internal Iterative)  : mini-recherche pour trouver un TT move
//   - LMR  (Late Move Reductions): profondeur reduite pour coups tardifs
//   - PVS  (Principal Variation)  : fenetre nulle pour coups non-PV
//   - Futility pruning           : ignorer coups calmes quand eval + marge <= alpha
//   - LMP  (Late Move Pruning)   : ignorer coups tardifs a faible profondeur
//
// quiescence()        : ne chercher que cinq, quatre, capture-wins
// search_five_break() : capturer pour casser un cinq adverse (regle Ninuki)
//
// Appelees depuis search_root() dans searcher.cpp.

#include "search_internal.hpp"

namespace gomoku {

// =========================================================================
//  quiescence -- coups forcants uniquement
// =========================================================================
// A la fin de l'arbre, on continue d'explorer les cinq, quatre et
// capture-wins pour eviter l'effet d'horizon.

int32_t WorkerSearcher::quiescence(
    Board& board, Stone color, int32_t alpha, int32_t beta,
    Pos last_move, int8_t qs_depth, uint64_t hash) {

    ++nodes;

    if ((nodes & 4095) == 0 && check_time()) return 0;
    if (is_stopped()) return 0;

    // Terminal : l'adversaire vient de gagner
    Stone last_player = opponent(color);
    if (board.captures(last_player) >= 5) return -PatternScore::FIVE;
    if (has_five_at_pos(board, last_move, last_player)) {
        auto five_line = find_five_line_at_pos(board, last_move, last_player);
        if (five_line.has_value()) {
            if (can_break_five_by_capture(board, *five_line, last_player)) {
                return search_five_break(
                    board, color, 0, alpha, beta, *five_line, last_player, hash);
            }
        }
        return -PatternScore::FIVE;
    }

    // TT probe
    if (auto probe = shared->tt.probe(hash, 0, alpha, beta)) {
        return probe->first;
    }

    // Stand-pat : evaluation statique comme borne basse
    int32_t stand_pat = evaluate(board, color);
    if (stand_pat >= beta) return stand_pat;

    int32_t original_alpha = alpha;
    if (stand_pat > alpha) alpha = stand_pat;

    if (qs_depth >= MAX_QS_DEPTH) return stand_pat;

    // Apres depth 6 en QS, ne chercher que les cinq (plus de fours)
    bool fours_allowed = qs_depth < 6;

    Stone opp = opponent(color);
    constexpr int8_t sz = static_cast<int8_t>(BOARD_SIZE);
    constexpr int8_t dirs[4][2] = {{1,0},{0,1},{1,1},{1,-1}};

    // Generation des coups forcants
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

    if (forcing_moves.empty()) return stand_pat;

    forcing_moves.sort_descending();

    size_t max_qs_moves = (qs_depth <= 2) ? 8 : 4;

    int32_t best_score = stand_pat;
    Pos best_move = Pos::sentinel();
    size_t moves_searched = 0;

    for (auto& [mov, priority] : forcing_moves) {
        if (priority < 850) {
            if (moves_searched >= max_qs_moves) break;
        }
        ++moves_searched;

        board.place_stone(mov, color);
        CaptureInfo cap_info = execute_captures_fast(board, mov, color);
        uint64_t child_hash = compute_child_hash(*shared, hash, board, mov, color, cap_info);

        int32_t score = -quiescence(
            board, opponent(color), -beta, -alpha, mov, qs_depth + 1, child_hash);

        undo_captures(board, color, cap_info);
        board.remove_stone(mov);

        if (is_stopped()) return 0;

        if (score > best_score) {
            best_score = score;
            best_move = mov;
        }
        if (score > alpha) alpha = score;
        if (score >= beta) break;
    }

    // TT store
    if (!is_stopped()) {
        EntryType entry_type;
        if (best_score >= beta) {
            entry_type = EntryType::LowerBound;
        } else if (best_score > original_alpha) {
            entry_type = EntryType::Exact;
        } else {
            entry_type = EntryType::UpperBound;
        }
        shared->tt.store(hash, 0, best_score, entry_type,
            best_move.is_sentinel() ? std::nullopt : std::optional<Pos>(best_move));
    }

    return best_score;
}

// =========================================================================
//  search_five_break -- capturer pour casser un cinq adverse
// =========================================================================
// En Ninuki-renju, un cinq n'est pas forcement gagnant : si l'adversaire
// peut capturer une paire qui fait partie du cinq, celui-ci est "cassable".

int32_t WorkerSearcher::search_five_break(
    Board& board, Stone color, int8_t depth,
    int32_t alpha, int32_t beta,
    const std::vector<Pos>& five_positions, Stone five_color,
    uint64_t hash) {

    std::vector<Pos> break_moves = find_five_break_moves(board, five_positions, five_color);
    if (break_moves.empty()) return -PatternScore::FIVE;

    int32_t best = -PatternScore::FIVE;
    for (const Pos& break_pos : break_moves) {
        if (!board.is_empty(break_pos)) continue;

        board.place_stone(break_pos, color);
        CaptureInfo cap_info = execute_captures_fast(board, break_pos, color);
        uint64_t child_hash = compute_child_hash(*shared, hash, board, break_pos, color, cap_info);

        int8_t search_depth = std::max(int8_t(depth - 1), int8_t(0));
        int32_t score = -alpha_beta(
            board, opponent(color), search_depth,
            -beta, -alpha, break_pos, child_hash, true);

        undo_captures(board, color, cap_info);
        board.remove_stone(break_pos);

        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) break;
            }
        }

        if (is_stopped()) return best;
    }
    return best;
}

// =========================================================================
//  alpha_beta -- negamax avec elagage et optimisations
// =========================================================================
// Alpha = meilleur score garanti pour nous, beta = meilleur pour l'adversaire.
// Si on trouve un score >= beta, on coupe (l'adversaire ne le permettrait pas).

int32_t WorkerSearcher::alpha_beta(
    Board& board, Stone color, int8_t depth,
    int32_t alpha, int32_t beta, Pos last_move,
    uint64_t hash, bool allow_null) {

    ++nodes;

    if ((nodes & 511) == 0) {
        if (check_time()) return 0;
    }
    if (is_stopped()) return 0;

    // --- Verifications terminales ---
    Stone last_player = opponent(color);
    if (board.captures(last_player) >= 5) return -PatternScore::FIVE;
    if (has_five_at_pos(board, last_move, last_player)) {
        auto five_line = find_five_line_at_pos(board, last_move, last_player);
        if (five_line.has_value()) {
            if (can_break_five_by_capture(board, *five_line, last_player)) {
                return search_five_break(
                    board, color, depth, alpha, beta, *five_line, last_player, hash);
            }
        }
        return -PatternScore::FIVE;
    }

    if (board.stone_count() >= 10 && has_five_in_row(board, color)) {
        return PatternScore::FIVE;
    }

    // --- Feuille : quiescence ---
    if (depth <= 0) {
        return quiescence(board, color, alpha, beta, last_move, 0, hash);
    }

    // --- TT probe ---
    stats.tt_probes += 1;
    if (auto probe = shared->tt.probe(hash, depth, alpha, beta)) {
        stats.tt_score_hits += 1;
        return probe->first;
    }

    bool non_terminal = std::abs(alpha) < PatternScore::FIVE - 100
        && std::abs(beta) < PatternScore::FIVE - 100;
    int32_t static_eval = non_terminal ? evaluate(board, color) : 0;

    // --- Reverse Futility Pruning (RFP) ---
    if (depth <= 3 && non_terminal
        && static_eval - PatternScore::OPEN_THREE * static_cast<int32_t>(depth) >= beta) {
        return static_eval;
    }

    // --- Razoring ---
    if (depth <= 3 && non_terminal
        && static_eval + PatternScore::OPEN_THREE * static_cast<int32_t>(depth) <= alpha) {
        int32_t qs_score = quiescence(board, color, alpha, beta, last_move, 0, hash);
        if (qs_score <= alpha) return qs_score;
    }

    // --- Null Move Pruning (NMP) ---
    if (allow_null && depth >= 3 && non_terminal
        && static_eval >= beta
        && !is_threatened(board, color, last_move)) {
        int8_t r = static_cast<int8_t>(2 + depth / 6);
        int8_t null_depth = std::max(int8_t(depth - 1 - r), int8_t(0));

        uint64_t null_hash = shared->zobrist.toggle_side(hash);
        int32_t null_score = -alpha_beta(
            board, opponent(color), null_depth,
            -beta, -(beta - 1), last_move, null_hash, false);

        if (!is_stopped() && null_score >= beta) {
            if (depth <= 6) return beta;
            int32_t verify = alpha_beta(
                board, color, depth - r, alpha, beta, last_move, hash, false);
            if (!is_stopped() && verify >= beta) return beta;
        }
    }

    // --- Generation des coups ---
    Pos tt_move = shared->tt.get_best_move(hash).value_or(Pos::sentinel());
    if (!tt_move.is_sentinel()) stats.tt_move_hits += 1;

    // IID : mini-recherche si pas de TT move.
    if (tt_move.is_sentinel() && depth >= 6) {
        int8_t iid_depth = std::max(int8_t(depth - 4), int8_t(1));
        alpha_beta(board, color, iid_depth, alpha, beta, last_move, hash, false);
        if (!is_stopped()) {
            tt_move = shared->tt.get_best_move(hash).value_or(Pos::sentinel());
        }
    }

    last_move_for_ordering = last_move;
    MoveList moves = generate_moves_ordered(board, color, tt_move, depth, MAX_INTERNAL_MOVES);
    if (moves.empty()) return evaluate(board, color);

    // Nombre de coups adaptatif.
    bool is_tactical = moves.top_score >= 850000;
    size_t max_moves;
    if (is_tactical) {
        switch (depth) {
            case 0: case 1: max_moves = 4; break;
            case 2: case 3: max_moves = 6; break;
            case 4: case 5: max_moves = 8; break;
            default: max_moves = 10; break;
        }
    } else {
        switch (depth) {
            case 0: case 1: max_moves = 3; break;
            case 2: case 3: max_moves = 4; break;
            case 4: case 5: max_moves = 6; break;
            default: max_moves = 8; break;
        }
    }

    {
        size_t valid_count = 0;
        moves.remove_if([&](const std::pair<Pos, int32_t>& entry) {
            if (valid_count >= max_moves) return true;
            if (is_valid_move(board, entry.first, color)) {
                ++valid_count;
                return false;
            }
            return true;
        });
    }

    // --- Futility pruning ---
    bool futility_ok = depth <= 4 && non_terminal;
    int32_t futility_margin;
    switch (depth) {
        case 1: futility_margin = PatternScore::CLOSED_FOUR; break;
        case 2: futility_margin = PatternScore::OPEN_FOUR; break;
        case 3: futility_margin = PatternScore::OPEN_FOUR + PatternScore::OPEN_THREE; break;
        default: futility_margin = PatternScore::OPEN_FOUR * 2; break;
    }

    // --- Boucle principale ---
    int32_t best_score = -INF;
    Pos best_move = Pos::sentinel();
    EntryType entry_type = EntryType::UpperBound;

    for (size_t i = 0; i < moves.size(); ++i) {
        Pos mov = moves[i].first;
        int32_t move_score = moves[i].second;

        // Futility pruning
        if (futility_ok && i > 0 && static_eval + futility_margin <= alpha) {
            if (move_score < 800000) continue;
        }

        // Late Move Pruning (LMP)
        if (i > 0 && depth <= 3
            && i >= (3 + static_cast<size_t>(depth) * 2)
            && move_score < 800000) {
            continue;
        }

        board.place_stone(mov, color);
        CaptureInfo cap_info = execute_captures_fast(board, mov, color);
        uint64_t child_hash = compute_child_hash(*shared, hash, board, mov, color, cap_info);

        bool is_capture = cap_info.pairs > 0;

        // Threat extension
        int8_t extension = (depth >= 2 && move_creates_four(board, mov, color))
            ? int8_t(1) : int8_t(0);

        // --- PVS + LMR ---
        int32_t score;
        if (i == 0) {
            score = -alpha_beta(
                board, opponent(color), depth - 1 + extension,
                -beta, -alpha, mov, child_hash, true);
        } else {
            // LMR : reduction logarithmique pour les coups tardifs.
            int8_t reduction;
            if (is_capture || extension > 0 || depth < 2 || i < 1) {
                reduction = 0;
            } else {
                float d = static_cast<float>(depth);
                float m = static_cast<float>(i);
                int8_t r_val = static_cast<int8_t>(std::sqrt(d) * std::sqrt(m) / 2.0f);
                if (move_score < 500000) r_val += 2;
                else if (move_score < 800000) r_val += 1;
                reduction = std::clamp(r_val, int8_t(1), int8_t(depth - 2));
            }
            int8_t search_depth = std::max(int8_t(depth - 1 + extension - reduction), int8_t(0));

            // PVS : fenetre nulle d'abord.
            score = -alpha_beta(
                board, opponent(color), search_depth,
                -(alpha + 1), -alpha, mov, child_hash, true);

            // Re-search pleine profondeur si LMR a reduit et score > alpha.
            if (!is_stopped() && reduction > 0 && score > alpha) {
                score = -alpha_beta(
                    board, opponent(color), depth - 1 + extension,
                    -(alpha + 1), -alpha, mov, child_hash, true);
            }

            // Re-search fenetre complete si score entre alpha et beta.
            if (!is_stopped() && score > alpha && score < beta) {
                score = -alpha_beta(
                    board, opponent(color), depth - 1 + extension,
                    -beta, -alpha, mov, child_hash, true);
            }
        }

        undo_captures(board, color, cap_info);
        board.remove_stone(mov);

        if (is_stopped()) return 0;

        if (score > best_score) {
            best_score = score;
            best_move = mov;
        }

        if (score >= beta) {
            // Beta cutoff : mise a jour killer, history, countermove.
            stats.beta_cutoffs += 1;
            if (i == 0) stats.first_move_cutoffs += 1;

            auto ply = static_cast<size_t>(std::max(int8_t(max_depth - depth), int8_t(0)));
            if (ply < 64) {
                if (killer_moves[ply][0] != mov) {
                    killer_moves[ply][1] = killer_moves[ply][0];
                    killer_moves[ply][0] = mov;
                }
            }
            int cidx = (color == Stone::Black) ? 0 : 1;
            history[cidx][mov.row][mov.col] +=
                static_cast<int32_t>(depth) * static_cast<int32_t>(depth);

            int opp_idx = (color == Stone::Black) ? 1 : 0;
            countermove[opp_idx][last_move.row][last_move.col] = mov;

            entry_type = EntryType::LowerBound;
            break;
        }

        if (score > alpha) {
            alpha = score;
            entry_type = EntryType::Exact;
        }
    }

    shared->tt.store(hash, depth, best_score, entry_type,
        best_move.is_sentinel() ? std::nullopt : std::optional<Pos>(best_move));
    return best_score;
}

} // namespace gomoku
