// Recherche recursive : negamax alpha-beta, quiescence, five-break
//
// Ce fichier contient uniquement le workflow principal de la recherche.
// Les optimisations (NMP, RFP, Razoring, LMR, PVS, Futility, LMP, IID)
// sont dans pruning.cpp.
//
// alpha_beta()        : negamax -- terminal → TT → pruning → boucle → TT store
// quiescence()        : ne chercher que cinq, quatre, capture-wins
// search_five_break() : capturer pour casser un cinq adverse (regle Ninuki)
//
// Appelees depuis search_root() dans searcher.cpp.

#include "../minimax_internal.hpp"

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

    MoveList forcing_moves = generate_forcing_moves(board, color, opp, fours_allowed);
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
//  alpha_beta -- negamax avec elagage
// =========================================================================
// Orchestrateur : terminal → TT → pruning → generation → boucle → TT store.
// Les optimisations individuelles sont dans pruning.cpp.

int32_t WorkerSearcher::alpha_beta(
    Board& board, Stone color, int8_t depth,
    int32_t alpha, int32_t beta, Pos last_move,
    uint64_t hash, bool allow_null) {

    ++nodes;

    if ((nodes & 511) == 0 && check_time()) return 0;
    if (is_stopped()) return 0;

    // --- Verifications terminales ---
    // On verifie si l'adversaire (qui vient de jouer) a deja gagne,
    // car en negamax c'est toujours le joueur precedent qui peut avoir
    // conclu la partie avec son dernier coup.
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

    // Si le joueur courant a deja un cinq sur le plateau, victoire immediate.
    // Le guard stone_count >= 10 evite un scan couteux en debut de partie
    // (impossible d'avoir un cinq avec moins de 10 pierres totales).
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

    // Desactive les pruning heuristiques si on est proche d'une victoire/defaite.
    bool non_terminal = std::abs(alpha) < PatternScore::FIVE - 100
        && std::abs(beta) < PatternScore::FIVE - 100;
    int32_t static_eval = non_terminal ? evaluate(board, color) : 0;

    // --- Pruning pre-recherche (pruning.cpp) ---
    int32_t pruned_score;
    if (try_rfp(depth, non_terminal, static_eval, beta, pruned_score))
        return pruned_score;
    if (try_razoring(board, color, depth, non_terminal, static_eval,
                     alpha, beta, last_move, hash, pruned_score))
        return pruned_score;
    if (try_nmp(board, color, depth, non_terminal, static_eval,
                alpha, beta, last_move, hash, allow_null, pruned_score))
        return pruned_score;

    // --- Generation des coups ---
    Pos tt_move = shared->tt.get_best_move(hash).value_or(Pos::sentinel());
    if (!tt_move.is_sentinel()) stats.tt_move_hits += 1;
    tt_move = try_iid(board, color, depth, alpha, beta, last_move, hash, tt_move);

    last_move_for_ordering = last_move;
    MoveList moves = generate_moves_ordered(board, color, tt_move, depth, MAX_INTERNAL_MOVES);
    if (moves.empty()) return evaluate(board, color);

    // Filtrage adaptatif des coups.
    bool is_tactical = moves.top_score >= 850000;
    size_t max_moves = compute_max_moves(depth, is_tactical);
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

    // --- Boucle principale ---
    bool futility_ok = depth <= 4 && non_terminal;
    int32_t futility_margin = compute_futility_margin(depth);

    int32_t best_score = -INF;
    Pos best_move = Pos::sentinel();
    EntryType entry_type = EntryType::UpperBound;

    for (size_t i = 0; i < moves.size(); ++i) {
        Pos mov = moves[i].first;
        int32_t move_score = moves[i].second;

        // Pruning dans la boucle (pruning.cpp)
        if (should_futility_prune(futility_ok, i, static_eval,
                                  futility_margin, alpha, move_score))
            continue;
        if (should_lmp(i, depth, move_score))
            continue;

        // Jouer le coup et executer les captures qu'il declenche
        board.place_stone(mov, color);
        CaptureInfo cap_info = execute_captures_fast(board, mov, color);
        uint64_t child_hash = compute_child_hash(*shared, hash, board, mov, color, cap_info);

        bool is_capture = cap_info.pairs > 0;
        // Extension : si ce coup cree un quatre, on cherche 1 ply plus profond
        int8_t extension = (depth >= 2 && move_creates_four(board, mov, color))
            ? int8_t(1) : int8_t(0);

        // Recherche PVS + LMR (pruning.cpp)
        int32_t score = search_pvs(
            board, color, depth, alpha, beta, mov, child_hash,
            i, move_score, is_capture, extension);

        // Annuler le coup
        undo_captures(board, color, cap_info);
        board.remove_stone(mov);

        if (is_stopped()) return 0;

        // Nouveau meilleur coup trouve
        if (score > best_score) {
            best_score = score;
            best_move = mov;
        }

        // Beta cutoff : l'adversaire ne nous laissera pas arriver ici
        if (score >= beta) {
            update_cutoff_heuristics(mov, color, depth, last_move, i);
            entry_type = EntryType::LowerBound;
            break;
        }

        // Amelioration d'alpha : on a trouve un meilleur score garanti
        if (score > alpha) {
            alpha = score;
            entry_type = EntryType::Exact;
        }
    }

    // --- TT store ---
    shared->tt.store(hash, depth, best_score, entry_type,
        best_move.is_sentinel() ? std::nullopt : std::optional<Pos>(best_move));
    return best_score;
}

} // namespace gomoku
