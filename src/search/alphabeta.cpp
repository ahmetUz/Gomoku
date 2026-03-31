// Searcher : interface publique du moteur de recherche Alpha-Beta
//
// Ce fichier contient uniquement l'API publique exposee par alphabeta.hpp :
// - SearchStats (statistiques de diagnostic)
// - Searcher (constructeurs, search_timed avec Lazy SMP, stop, etc.)
//
// L'implementation interne (WorkerSearcher, alpha-beta recursif, tri des
// coups) est repartie dans les fichiers voisins :
// - search_iterative.cpp : approfondissement iteratif + recherche racine
// - search_core.cpp      : alpha-beta recursif, quiescence, five-break
// - move_ordering.cpp    : generation et tri des coups candidats

#include "worker_searcher.hpp"

namespace gomoku {

// =========================================================================
// SearchStats
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
// Searcher : constructeurs
// =========================================================================

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
// Searcher::search_timed -- Lazy SMP parallel search
// =========================================================================
// On lance N threads qui cherchent tous independamment le meilleur coup.
// Chaque thread partage la table de transposition (TT), donc quand un
// thread trouve un bon coup, les autres en beneficient.
// Les threads commencent a des profondeurs differentes pour diversifier
// l'exploration. A la fin, on garde le meilleur resultat (plus profond,
// puis meilleur score).

SearchResult Searcher::search_timed(
    const Board& board, Stone color, int8_t max_depth, uint64_t time_limit_ms) {

    shared_->stopped.store(false, std::memory_order_relaxed);
    max_depth_ = max_depth;
    auto start = std::chrono::steady_clock::now();
    auto time_limit = std::chrono::milliseconds(time_limit_ms);

    // Spawn helper threads (workers 1..N)
    std::vector<std::thread> threads;
    std::vector<SearchResult> helper_results;

#ifndef GOMOKU_NO_LAZY_SMP
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
#endif

    // Main thread = worker 0
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

    // Signal all workers to stop
    shared_->stopped.store(true, std::memory_order_relaxed);

    // Collect results -- pick best (deepest, then highest score)
    SearchResult best = main_result;
    uint64_t total_nodes = best.nodes;
    SearchStats merged_stats = best.stats;

#ifndef GOMOKU_NO_LAZY_SMP
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
#endif

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

} // namespace gomoku
