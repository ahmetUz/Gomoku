// Approfondissement iteratif (iterative deepening) + recherche racine
//
// L'approfondissement iteratif consiste a chercher d'abord a profondeur 1,
// puis 2, puis 3, etc. Ca semble gaspiller du temps, mais en pratique :
// 1) Chaque iteration est ~10x plus rapide que la suivante (arbre exponentiel)
// 2) Les resultats des iterations precedentes guident le tri des coups
// 3) On peut s'arreter a tout moment et garder le meilleur resultat connu
//
// Le noeud racine est special : on y applique PVS (Principal Variation Search).
// Le premier coup est cherche avec la fenetre complete, les suivants avec une
// fenetre nulle. Si l'un d'eux se revele meilleur, on relance en fenetre complete.

#include "worker_searcher.hpp"

namespace gomoku {

// =========================================================================
// search_iterative
// =========================================================================

SearchResult WorkerSearcher::search_iterative(
    const Board& board, Stone color, int8_t max_depth_arg, int8_t start_depth_offset) {

    SearchResult best_result;
    best_result.score = 0;
    best_result.depth = 0;
    best_result.nodes = 0;

    Board work_board = board;
    auto search_start = start_time.value_or(std::chrono::steady_clock::now());
    auto soft_limit = time_limit.value_or(std::chrono::milliseconds(500));
    auto prev_depth_time = std::chrono::steady_clock::duration::zero();

    // Profondeur minimale garantie : on complete toujours au moins 6 niveaux
    // avant de commencer a gerer le temps.
    int8_t min_depth = 6;

    // Confirmation de victoire/defaite : on exige deux profondeurs
    // consecutives qui s'accordent sur un score terminal.
    bool prev_was_winning = false;
    bool prev_was_losing = false;

    // Workers with offset skip early depths (cheap and TT handles it)
    int8_t first_depth = std::max(int8_t(1 + start_depth_offset), int8_t(1));

    for (int8_t depth = first_depth; depth <= max_depth_arg; ++depth) {
        if (is_stopped()) break;

        // History gravity: halve all scores so recent results dominate.
        if (depth > first_depth) {
            for (auto& color_hist : history) {
                for (auto& row : color_hist) {
                    for (auto& val : row) {
                        val >>= 1;
                    }
                }
            }
        }

        auto depth_start = std::chrono::steady_clock::now();

        // Aspiration window : on parie que le score ne changera pas beaucoup
        // entre deux profondeurs. Fenetre etroite [score-100, score+100].
        // Si le score sort (fail-low/fail-high), on relance en fenetre complete.
#ifndef GOMOKU_NO_ASPIRATION
        int32_t asp_alpha, asp_beta;
        if (depth >= 3 && std::abs(best_result.score) < PatternScore::FIVE - 100) {
            asp_alpha = best_result.score - ASP_WINDOW;
            asp_beta = best_result.score + ASP_WINDOW;
        } else {
            asp_alpha = -INF;
            asp_beta = INF;
        }

        SearchResult result;
        for (;;) {
            result = search_root(work_board, color, depth, asp_alpha, asp_beta);
            if (is_stopped()) break;
            if (result.score <= asp_alpha) {
                asp_alpha = -INF;
            } else if (result.score >= asp_beta) {
                asp_beta = INF;
            } else {
                break;
            }
        }
#else
        SearchResult result = search_root(work_board, color, depth, -INF, INF);
#endif

        if (is_stopped()) break;

        best_result = result;
        best_result.depth = depth;
        auto depth_time = std::chrono::steady_clock::now() - depth_start;
        auto total_elapsed = std::chrono::steady_clock::now() - search_start;

        // Early exit: winning/losing confirmed over two consecutive depths.
        bool is_winning = best_result.score >= PatternScore::FIVE - 100;
        bool is_losing = best_result.score <= -(PatternScore::FIVE - 100);

        if (is_winning && prev_was_winning && depth >= min_depth) break;
        if (is_losing && prev_was_losing && depth >= min_depth) break;

        prev_was_winning = is_winning;
        prev_was_losing = is_losing;

        if (depth < min_depth) {
            prev_depth_time = depth_time;
            continue;
        }

        // Hard cutoff: if we've used > 50% of time, don't start next depth
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_elapsed);
        auto limit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(soft_limit);
        if (elapsed_ms.count() * 2 > limit_ms.count()) break;

        auto remaining = soft_limit - std::min(elapsed_ms, limit_ms);

        // Predict next depth time using observed branching factor.
        auto depth_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(depth_time).count();
        auto prev_depth_ms = std::chrono::duration_cast<std::chrono::milliseconds>(prev_depth_time).count();

        std::chrono::milliseconds estimated_next;
        if (prev_depth_ms > 0 && depth_time_ms > 0) {
            double bf = static_cast<double>(depth_time_ms) /
                        static_cast<double>(std::max(prev_depth_ms, int64_t(1)));
            bf = std::clamp(bf, 1.5, 5.0);
            estimated_next = std::chrono::milliseconds(
                static_cast<int64_t>(static_cast<double>(depth_time_ms) * bf * 1.2));
        } else {
            estimated_next = std::chrono::duration_cast<std::chrono::milliseconds>(depth_time) * 4;
        }

        prev_depth_time = depth_time;

        if (estimated_next > remaining) break;
    }

    best_result.nodes = nodes;
    best_result.stats = stats;
    return best_result;
}

// =========================================================================
// search_root -- PVS at root level
// =========================================================================

SearchResult WorkerSearcher::search_root(
    Board& board, Stone color, int8_t depth, int32_t alpha, int32_t beta) {

    Pos best_move = Pos::sentinel();
    int32_t best_score = -INF;

    uint64_t hash = shared->zobrist.hash(board, color);
    Pos tt_move = shared->tt.get_best_move(hash).value_or(Pos::sentinel());
    last_move_for_ordering = Pos::sentinel();
    MoveList moves = generate_moves_ordered(board, color, tt_move, depth, MAX_ROOT_MOVES);

    // Filter forbidden moves, keeping at most MAX_ROOT_MOVES valid ones.
    {
        size_t valid_count = 0;
        moves.remove_if([&](const std::pair<Pos, int32_t>& entry) {
            if (valid_count >= MAX_ROOT_MOVES) return true;
            if (is_valid_move(board, entry.first, color)) {
                ++valid_count;
                return false;
            }
            return true;
        });
    }

    for (size_t i = 0; i < moves.size(); ++i) {
        Pos mov = moves[i].first;

        board.place_stone(mov, color);
        CaptureInfo cap_info = execute_captures_fast(board, mov, color);
        uint64_t child_hash = compute_child_hash(*shared, hash, board, mov, color, cap_info);

        // Threat extension: forcing moves (creating a four) get +1 ply.
#ifndef GOMOKU_NO_THREAT_EXT
        int8_t extension = move_creates_four(board, mov, color) ? int8_t(1) : int8_t(0);
#else
        int8_t extension = 0;
#endif

        int32_t score;
        if (i == 0) {
            score = -alpha_beta(
                board, opponent(color), depth - 1 + extension,
                -beta, -alpha, mov, child_hash, true);
        } else {
#ifndef GOMOKU_NO_PVS
            score = -alpha_beta(
                board, opponent(color), depth - 1 + extension,
                -(alpha + 1), -alpha, mov, child_hash, true);
            if (!is_stopped() && score > alpha && score < beta) {
                score = -alpha_beta(
                    board, opponent(color), depth - 1 + extension,
                    -beta, -alpha, mov, child_hash, true);
            }
#else
            score = -alpha_beta(
                board, opponent(color), depth - 1 + extension,
                -beta, -alpha, mov, child_hash, true);
#endif
        }

        undo_captures(board, color, cap_info);
        board.remove_stone(mov);

        if (is_stopped()) break;

        if (score > best_score) {
            best_score = score;
            best_move = mov;
        }

        if (score >= beta) break;
        alpha = std::max(alpha, score);
    }

    // Store root result in TT for reuse by other workers and next iteration.
    if (!is_stopped()) {
        EntryType entry_type = (best_score >= beta)
            ? EntryType::LowerBound
            : EntryType::Exact;
        shared->tt.store(hash, depth, best_score, entry_type,
            best_move.is_sentinel() ? std::nullopt : std::optional<Pos>(best_move));
    }

    SearchResult result;
    result.best_move = best_move;
    result.score = best_score;
    result.depth = depth;
    result.nodes = nodes;
    result.stats = stats;
    return result;
}

} // namespace gomoku
