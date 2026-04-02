// Orchestration de la recherche : Lazy SMP, iterative deepening, PVS root
//
// Workflow :
// search_timed            → lance N threads en parallele (Lazy SMP)
//   search_iterative      → boucle depth 1, 2, 3... avec gestion du temps
//     search_with_aspiration → fenetre etroite autour du score precedent
//       search_root        → PVS a la racine (1er coup fenetre complete)
//         alpha_beta(...)  → (dans alpha_beta.cpp)
//
// La recherche recursive (alpha_beta, quiescence, five_break) est dans
// alpha_beta.cpp. Le tri des coups est dans move_ordering.cpp.

#include "../minimax_internal.hpp"

namespace gomoku {

// =========================================================================
//  SearchStats
// =========================================================================

double SearchStats::first_move_rate() const {
    if (beta_cutoffs == 0) return 0.0;
    return static_cast<double>(first_move_cutoffs) / static_cast<double>(beta_cutoffs) * 100.0;
}

double SearchStats::tt_score_rate() const {
    if (tt_probes == 0) return 0.0;
    return static_cast<double>(tt_score_hits) / static_cast<double>(tt_probes) * 100.0;
}

void SearchStats::merge(const SearchStats& other) {
    beta_cutoffs += other.beta_cutoffs;
    first_move_cutoffs += other.first_move_cutoffs;
    tt_probes += other.tt_probes;
    tt_score_hits += other.tt_score_hits;
    tt_move_hits += other.tt_move_hits;
}

// =========================================================================
//  Constructeurs
// =========================================================================

// Detecte le nombre de coeurs CPU (max 8) et alloue la TT partagee.
Searcher::Searcher(size_t tt_size_mb) {
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    num_threads = std::min(num_threads, size_t(8));
    shared_ = std::make_shared<SharedState>(tt_size_mb);
    max_depth_ = 10;
    num_threads_ = num_threads;
    std::memset(history_, 0, sizeof(history_));
}

Searcher::Searcher(size_t tt_size_mb, size_t num_threads)
    : shared_(std::make_shared<SharedState>(tt_size_mb))
    , max_depth_(10)
    , num_threads_(std::max(num_threads, size_t(1))) {
    std::memset(history_, 0, sizeof(history_));
}

// =========================================================================
//  search_timed -- Lazy SMP (recherche parallele)
// =========================================================================
// 1. Preparation (reset stop, timers)
// 2. Lancement des threads helpers avec offsets de profondeur differents
// 3. Recherche du thread principal (herite de l'history precedente)
// 4. Signal d'arret aux helpers
// 5. Collecte : on garde le resultat le plus profond / meilleur score
// 6. Sauvegarde de l'history pour le prochain appel

SearchResult Searcher::search_timed(
    const Board& board, Stone color, int8_t max_depth, uint64_t time_limit_ms) {

    // --- 1. Preparation ---
    shared_->stopped.store(false, std::memory_order_relaxed);
    max_depth_ = max_depth;
    auto start = std::chrono::steady_clock::now();
    auto time_limit = std::chrono::milliseconds(time_limit_ms);

    std::vector<std::thread> threads;
    std::vector<SearchResult> helper_results;

    // --- 2. Lancement des threads helpers (workers 1..N) ---
    helper_results.resize(num_threads_ - 1);
    for (size_t thread_id = 1; thread_id < num_threads_; ++thread_id) {
        size_t result_idx = thread_id - 1;
        Board board_clone = board;
        auto shared_copy = shared_;
        int8_t start_depth_offset = static_cast<int8_t>(thread_id);

        threads.emplace_back([shared_copy, board_clone, color, max_depth,
                              start, time_limit, start_depth_offset,
                              &helper_results, result_idx]() mutable {
            WorkerSearcher w;
            w.shared = shared_copy;
            w.nodes = 0;
            w.max_depth = max_depth;
            std::memset(w.killer_moves, 0xFF, sizeof(w.killer_moves));
            std::memset(w.history, 0, sizeof(w.history));
            std::memset(w.countermove, 0xFF, sizeof(w.countermove));
            w.last_move_for_ordering = Pos::sentinel();
            w.start_time = start;
            w.time_limit = time_limit;
            w.stats = SearchStats{};

            helper_results[result_idx] =
                w.search_iterative(board_clone, color, max_depth, start_depth_offset);
        });
    }

    // --- 3. Recherche du thread principal (worker 0) ---
    WorkerSearcher main_worker;
    main_worker.shared = shared_;
    main_worker.nodes = 0;
    main_worker.max_depth = max_depth;
    std::memset(main_worker.killer_moves, 0xFF, sizeof(main_worker.killer_moves));
    std::memcpy(main_worker.history, history_, sizeof(history_));
    std::memset(main_worker.countermove, 0xFF, sizeof(main_worker.countermove));
    main_worker.last_move_for_ordering = Pos::sentinel();
    main_worker.start_time = start;
    main_worker.time_limit = time_limit;
    main_worker.stats = SearchStats{};

    SearchResult main_result =
        main_worker.search_iterative(board, color, max_depth, 0);

    // --- 4. Signal d'arret aux helpers ---
    shared_->stopped.store(true, std::memory_order_relaxed);

    // --- 5. Collecte des resultats ---
    SearchResult best = main_result;
    uint64_t total_nodes = best.nodes;
    SearchStats merged_stats = best.stats;

    for (auto& t : threads) {
        t.join();
    }
    for (auto& result : helper_results) {
        total_nodes += result.nodes;
        merged_stats.merge(result.stats);
        if (result.depth > best.depth
            || (result.depth == best.depth && result.score > best.score)) {
            best = result;
        }
    }

    // --- 6. Sauvegarde de l'history ---
    best.nodes = total_nodes;
    best.stats = merged_stats;
    std::memcpy(history_, main_worker.history, sizeof(history_));
    return best;
}

void Searcher::clear_history() {
    std::memset(history_, 0, sizeof(history_));
}

void Searcher::stop() {
    shared_->stopped.store(true, std::memory_order_relaxed);
}

TTStats Searcher::tt_stats() const {
    return shared_->tt.stats();
}

void Searcher::clear_tt() const {
    shared_->tt.clear();
}

// =========================================================================
//  search_iterative -- boucle depth 1..max avec gestion du temps
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

    constexpr int8_t MIN_DEPTH = 6;

    bool prev_was_winning = false;
    bool prev_was_losing = false;

    int8_t first_depth = std::max(int8_t(1 + start_depth_offset), int8_t(1));

    for (int8_t depth = first_depth; depth <= max_depth_arg; ++depth) {
        if (is_stopped()) break;

        // Decroissance de l'history.
        if (depth > first_depth) {
            for (auto& color_hist : history)
                for (auto& row : color_hist)
                    for (auto& val : row)
                        val >>= 1;
        }

        auto depth_start = std::chrono::steady_clock::now();

        SearchResult result = search_with_aspiration(work_board, color, depth, best_result.score);

        if (is_stopped()) break;

        best_result = result;
        best_result.depth = depth;

        // Early exit si victoire/defaite confirmee sur 2 profondeurs.
        bool is_winning = best_result.score >= PatternScore::FIVE - 100;
        bool is_losing  = best_result.score <= -(PatternScore::FIVE - 100);

        if (is_winning && prev_was_winning && depth >= MIN_DEPTH) break;
        if (is_losing  && prev_was_losing  && depth >= MIN_DEPTH) break;

        prev_was_winning = is_winning;
        prev_was_losing  = is_losing;

        auto depth_time = std::chrono::steady_clock::now() - depth_start;

        if (depth < MIN_DEPTH) {
            prev_depth_time = depth_time;
            continue;
        }

        if (should_stop_deepening(search_start, soft_limit, depth_time, prev_depth_time))
            break;

        prev_depth_time = depth_time;
    }

    best_result.nodes = nodes;
    best_result.stats = stats;
    return best_result;
}

// =========================================================================
//  search_with_aspiration -- fenetre etroite [score-100, score+100]
// =========================================================================

SearchResult WorkerSearcher::search_with_aspiration(
    Board& board, Stone color, int8_t depth, int32_t prev_score) {

    int32_t asp_alpha, asp_beta;
    if (depth >= 3 && std::abs(prev_score) < PatternScore::FIVE - 100) {
        asp_alpha = prev_score - ASP_WINDOW;
        asp_beta  = prev_score + ASP_WINDOW;
    } else {
        asp_alpha = -INF;
        asp_beta  = INF;
    }

    SearchResult result;
    for (;;) {
        result = search_root(board, color, depth, asp_alpha, asp_beta);
        if (is_stopped()) break;
        if (result.score <= asp_alpha) {
            asp_alpha = -INF;
        } else if (result.score >= asp_beta) {
            asp_beta = INF;
        } else {
            break;
        }
    }
    return result;
}

// =========================================================================
//  should_stop_deepening -- prediction temps prochaine profondeur
// =========================================================================

bool WorkerSearcher::should_stop_deepening(
    std::chrono::steady_clock::time_point search_start,
    std::chrono::milliseconds soft_limit,
    std::chrono::steady_clock::duration depth_time,
    std::chrono::steady_clock::duration prev_depth_time) const {

    auto total_elapsed = std::chrono::steady_clock::now() - search_start;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_elapsed);
    auto limit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(soft_limit);

    if (elapsed_ms.count() * 2 > limit_ms.count()) return true;

    auto remaining = soft_limit - std::min(elapsed_ms, limit_ms);

    auto depth_ms = std::chrono::duration_cast<std::chrono::milliseconds>(depth_time).count();
    auto prev_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(prev_depth_time).count();

    std::chrono::milliseconds estimated_next;
    if (prev_ms > 0 && depth_ms > 0) {
        double bf = static_cast<double>(depth_ms)
                  / static_cast<double>(std::max(prev_ms, int64_t(1)));
        bf = std::clamp(bf, 1.5, 5.0);
        estimated_next = std::chrono::milliseconds(
            static_cast<int64_t>(static_cast<double>(depth_ms) * bf * 1.2));
    } else {
        estimated_next = std::chrono::duration_cast<std::chrono::milliseconds>(depth_time) * 4;
    }

    return estimated_next > remaining;
}

// =========================================================================
//  search_root -- PVS a la racine
// =========================================================================

SearchResult WorkerSearcher::search_root(
    Board& board, Stone color, int8_t depth, int32_t alpha, int32_t beta) {

    Pos best_move = Pos::sentinel();
    int32_t best_score = -INF;

    uint64_t hash = shared->zobrist.hash(board, color);
    Pos tt_move = shared->tt.get_best_move(hash).value_or(Pos::sentinel());
    last_move_for_ordering = Pos::sentinel();
    MoveList moves = generate_moves_ordered(board, color, tt_move, depth, MAX_ROOT_MOVES);

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

        int8_t extension = move_creates_four(board, mov, color) ? int8_t(1) : int8_t(0);

        // PVS (Principal Variation Search) :
        // - i == 0 : premier coup (suppose meilleur) → fenetre complete [-beta, -alpha]
        // - i > 0  : coups suivants → fenetre nulle [-(alpha+1), -alpha]
        //            pour tester rapidement s'ils sont meilleurs que le premier.
        //            Si oui (score > alpha), re-search avec fenetre complete.
        int32_t score;
        if (i == 0) {
            score = -alpha_beta(
                board, opponent(color), depth - 1 + extension,
                -beta, -alpha, mov, child_hash, true);
        } else {
            // Scout : fenetre nulle, "ce coup est-il meilleur que alpha ?"
            score = -alpha_beta(
                board, opponent(color), depth - 1 + extension,
                -(alpha + 1), -alpha, mov, child_hash, true);
            if (!is_stopped() && score > alpha && score < beta) {
                // Oui → re-search complete pour connaitre le vrai score
                score = -alpha_beta(
                    board, opponent(color), depth - 1 + extension,
                    -beta, -alpha, mov, child_hash, true);
            }
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
