// Alpha-Beta search with iterative deepening and transposition table
//
// Coeur du moteur de recherche IA pour le Gomoku. L'idee centrale est d'explorer
// l'arbre des coups possibles en profondeur, en eliminant les branches inutiles
// grace a l'elagage alpha-beta. On imagine : "si je joue ici, mon adversaire
// jouera la, et moi ensuite..." etc. C'est le meme raisonnement qu'un joueur
// humain, mais systematique et tres rapide.
//
// Le fichier combine plusieurs techniques d'IA de jeux, chacune apportant un
// gain de performance specifique. Ensemble, elles permettent au moteur d'atteindre
// une profondeur de recherche de 10+ coups en moins de 500ms.
//
// Techniques utilisees :
// - Negamax avec elagage alpha-beta (le socle de la recherche)
// - Iterative deepening (chercher d'abord peu profond, puis de plus en plus)
// - Table de transposition (se souvenir des positions deja evaluees)
// - Lazy SMP (recherche parallele sur plusieurs coeurs)
// - Aspiration windows, PVS, LMR, NMP, futility pruning, etc.

#include "gomoku/search/alphabeta.hpp"
#include "gomoku/search/zobrist.hpp"
#include "gomoku/search/tt.hpp"
#include "gomoku/eval/heuristic.hpp"
#include "gomoku/eval/patterns.hpp"
#include "gomoku/rules/capture.hpp"
#include "gomoku/rules/win.hpp"
#include "gomoku/rules/forbidden.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace gomoku {

// =========================================================================
// Constants
// =========================================================================

// Infinity score for alpha-beta bounds
static constexpr int32_t INF = PatternScore::FIVE + 1;

// Nombre max de coups evalues a la racine de l'arbre de recherche.
// Le tri par priorite (defense d'abord) place les coups critiques en tete,
// donc 25 suffit largement pour couvrir toutes les menaces.
static constexpr size_t MAX_ROOT_MOVES = 25;

// Nombre max de coups evalues dans les noeuds internes de l'arbre.
// Plus on est profond dans l'arbre, moins on explore de coups : c'est le
// concept de "branching factor reduit" qui permet d'aller plus profond.
static constexpr size_t MAX_INTERNAL_MOVES = 12;

// Taille de la fenetre d'aspiration (aspiration window).
// Au lieu de chercher avec une fenetre [-infini, +infini], on commence avec
// une fenetre etroite autour du score precedent. Si le vrai score est dedans,
// on gagne du temps. Sinon, on relance avec la fenetre pleine.
static constexpr int32_t ASP_WINDOW = 100;

// Liste de coups allouee sur la pile (pas sur le tas/heap).
// Evite les allocations memoire dynamiques dans les chemins chauds.
// 128 entrees x 8 octets = 1 Ko par niveau de recursion.
struct MoveList {
    static constexpr size_t CAPACITY = 128;
    std::pair<Pos, int32_t> data[CAPACITY];
    size_t count = 0;
    int32_t top_score = 0;  // highest score (set after sort)

    void push_back(Pos pos, int32_t score) {
        if (count < CAPACITY) data[count++] = {pos, score};
    }
    bool empty() const { return count == 0; }
    size_t size() const { return count; }

    std::pair<Pos, int32_t>* begin() { return data; }
    std::pair<Pos, int32_t>* end() { return data + count; }
    const std::pair<Pos, int32_t>* begin() const { return data; }
    const std::pair<Pos, int32_t>* end() const { return data + count; }

    std::pair<Pos, int32_t>& operator[](size_t i) { return data[i]; }
    const std::pair<Pos, int32_t>& operator[](size_t i) const { return data[i]; }
    std::pair<Pos, int32_t>& front() { return data[0]; }

    void sort_descending() {
        std::sort(data, data + count,
            [](const auto& a, const auto& b) { return a.second > b.second; });
        top_score = count > 0 ? data[0].second : 0;
    }

    // Tri partiel : on ne garantit le tri que sur les k premiers elements.
    // Inutile de trier 100 coups si on n'en explore que 12 (partial sort).
    void partial_sort_descending(size_t k) {
        if (count <= 1) {
            top_score = count > 0 ? data[0].second : 0;
            return;
        }
        k = std::min(k, count);
        std::partial_sort(data, data + k, data + count,
            [](const auto& a, const auto& b) { return a.second > b.second; });
        top_score = data[0].second;
    }

    // Remove elements where pred returns true, preserving order of kept elements.
    // Equivalent to erase(remove_if(...), end()).
    template<typename Pred>
    void remove_if(Pred pred) {
        size_t write = 0;
        for (size_t read = 0; read < count; ++read) {
            if (!pred(data[read])) {
                if (write != read) data[write] = data[read];
                ++write;
            }
        }
        count = write;
    }
};

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
// SharedState : etat partage entre tous les threads de recherche
// =========================================================================
// En Lazy SMP, chaque thread explore l'arbre independamment mais partage
// la table de transposition (TT). Quand un thread trouve un bon coup, les
// autres en beneficient via la TT sans synchronisation lourde.

struct SharedState {
    ZobristTable zobrist;
    AtomicTT tt;
    std::atomic<bool> stopped;

    explicit SharedState(size_t tt_mb)
        : zobrist(), tt(tt_mb), stopped(false) {}
};

// =========================================================================
// WorkerSearcher : etat local a chaque thread de recherche
// =========================================================================
// Chaque thread a ses propres killer moves, historique, et compteurs.
// Killer moves : on retient les 2 meilleurs coups qui ont provoque un cutoff
// a chaque profondeur. L'idee : si un coup etait bon dans une position voisine,
// il a de bonnes chances d'etre bon ici aussi.
// History heuristic : on accumule des points pour les coups qui s'averent bons
// au fil de la recherche. Plus un coup a souvent ete bon, plus on l'essaie tot.
// Countermove : on retient la meilleure reponse au dernier coup de l'adversaire.

struct WorkerSearcher {
    std::shared_ptr<SharedState> shared;
    uint64_t nodes;
    int8_t max_depth;
    Pos killer_moves[64][2];
    int32_t history[2][BOARD_SIZE][BOARD_SIZE];
    Pos countermove[2][BOARD_SIZE][BOARD_SIZE];
    Pos last_move_for_ordering = Pos::sentinel();
    std::optional<std::chrono::steady_clock::time_point> start_time;
    std::optional<std::chrono::milliseconds> time_limit;
    SearchStats stats;

    // Maximum quiescence search depth (plies of forcing moves).
    // VCF-style fours are fully forcing, so we can search deep without explosion.
    static constexpr int8_t MAX_QS_DEPTH = 16;

    // Check if search should stop (time limit or global stop signal).
    bool is_stopped() const {
        return shared->stopped.load(std::memory_order_relaxed);
    }

    // Check time and set global stop if exceeded.
    bool check_time() const {
        if (shared->stopped.load(std::memory_order_relaxed)) {
            return true;
        }
        if (start_time.has_value() && time_limit.has_value()) {
            auto elapsed = std::chrono::steady_clock::now() - *start_time;
            if (elapsed >= *time_limit) {
                shared->stopped.store(true, std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

    // Iterative deepening search. start_depth_offset allows workers
    // to begin at different depths for natural tree diversification.
    SearchResult search_iterative(
        const Board& board, Stone color, int8_t max_depth_arg, int8_t start_depth_offset);

    // Root-level search with full alpha-beta window.
    SearchResult search_root(
        Board& board, Stone color, int8_t depth, int32_t alpha, int32_t beta);

    // Recursive alpha-beta search with negamax formulation.
    int32_t alpha_beta(
        Board& board, Stone color, int8_t depth,
        int32_t alpha, int32_t beta, Pos last_move,
        uint64_t hash, bool allow_null);

    // Quiescence search at leaf nodes of alpha-beta.
    int32_t quiescence(
        Board& board, Stone color, int32_t alpha, int32_t beta,
        Pos last_move, int8_t qs_depth, uint64_t hash);

    // Search only break moves when opponent has a breakable five.
    int32_t search_five_break(
        Board& board, Stone color, int8_t depth,
        int32_t alpha, int32_t beta,
        const std::vector<Pos>& five_positions, Stone five_color,
        uint64_t hash);

    // Generate candidate moves ordered by priority.
    // Fills a stack-allocated MoveList sorted by score descending.
    // sort_limit: only the top sort_limit entries are guaranteed sorted.
    MoveList generate_moves_ordered(
        const Board& board, Stone color,
        Pos tt_move, int8_t depth,
        size_t sort_limit = MoveList::CAPACITY) const;

    // Score a move for ordering purposes (defense-first philosophy).
    int32_t score_move(
        const Board& board, Pos mov, Stone color,
        Pos tt_move, int8_t depth) const;

    // Scan a line from pos in both directions for both colors simultaneously.
    // Returns {my_count, my_open, my_gap, my_consec, opp_count, opp_open, opp_gap, opp_consec}.
    struct LineBothResult {
        int32_t mc, mo;
        bool m_gap;
        int32_t mc_consec;
        int32_t oc, oo;
        bool o_gap;
        int32_t oc_consec;
    };

    static LineBothResult count_line_both(
        const Bitboard& my_bb, const Bitboard& opp_bb,
        Pos pos, int8_t dr, int8_t dc);

    // Check if the stone just placed at pos creates a four (4 in a row with >= 1 open end).
    static bool move_creates_four(const Board& board, Pos pos, Stone color);

    // Check if the side to move faces an immediate tactical threat.
    static bool is_threatened(const Board& board, Stone color, Pos last_move);
};

// =========================================================================
// WorkerSearcher: search_iterative (approfondissement iteratif)
// =========================================================================
// L'approfondissement iteratif consiste a chercher d'abord a profondeur 1,
// puis 2, puis 3, etc. Ca semble gaspiller du temps, mais en pratique :
// 1) Chaque iteration est ~10x plus rapide que la suivante (arbre exponentiel)
// 2) Les resultats des iterations precedentes guident le tri des coups
// 3) On peut s'arreter a tout moment et garder le meilleur resultat connu
// C'est la base de la gestion du temps : on ne lance la profondeur suivante
// que si on estime avoir assez de temps pour la terminer.

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
    // avant de commencer a gerer le temps. En dessous, la recherche est trop
    // superficielle pour donner des resultats fiables.
    int8_t min_depth = 6;

    // Confirmation de victoire/defaite : on exige que DEUX profondeurs
    // consecutives s'accordent sur un score terminal avant de s'arreter.
    // Ca evite les "faux gains" ou la profondeur d voit une victoire
    // mais la profondeur d+1 trouve la refutation.
    bool prev_was_winning = false;
    bool prev_was_losing = false;

    // Workers with offset skip early depths (they're cheap anyway and TT handles it)
    int8_t first_depth = std::max(int8_t(1 + start_depth_offset), int8_t(1));

    for (int8_t depth = first_depth; depth <= max_depth_arg; ++depth) {
        if (is_stopped()) break;

        // History gravity: halve all history scores at each new depth.
        // Ensures recent search results outweigh stale move ordering data.
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

        // Aspiration window : on parie que le score ne va pas changer
        // beaucoup entre deux profondeurs. On cherche dans une fenetre
        // etroite [score-100, score+100]. Si le pari est bon, la recherche
        // est beaucoup plus rapide. Si le score sort de la fenetre (fail-low
        // ou fail-high), on relance avec la fenetre complete.
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
                // On fail-low, immediately open to -INF (no second re-search)
                asp_alpha = -INF;
            } else if (result.score >= asp_beta) {
                // On fail-high, immediately open to INF
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

        // Early exit: winning or confirmed loss -- only after reaching min_depth
        // AND confirmed over two consecutive depths.
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
// WorkerSearcher: search_root (recherche a la racine)
// =========================================================================
// Le noeud racine est special : c'est le premier niveau de l'arbre, la ou
// on explore tous les coups legaux et on choisit le meilleur. On y applique
// aussi PVS (Principal Variation Search) : le premier coup est cherche avec
// une fenetre complete, et les suivants avec une fenetre "nulle" (zero-width).
// L'idee : le premier coup, grace au bon tri, est souvent le meilleur.
// Les autres coups n'ont qu'a prouver qu'ils sont pires. Si l'un d'eux
// se revele meilleur, on relance une recherche complete pour le confirmer.

SearchResult WorkerSearcher::search_root(
    Board& board, Stone color, int8_t depth, int32_t alpha, int32_t beta) {

    Pos best_move = Pos::sentinel();
    int32_t best_score = -INF;

    uint64_t hash = shared->zobrist.hash(board, color);
    Pos tt_move = shared->tt.get_best_move(hash).value_or(Pos::sentinel());
    last_move_for_ordering = Pos::sentinel();
    MoveList moves = generate_moves_ordered(board, color, tt_move, depth, MAX_ROOT_MOVES);

    // Lazy double-three: keep the first MAX_ROOT_MOVES valid moves.
    // Forbidden (double-three) moves may score high, so we can't truncate
    // first -- that would displace valid defensive moves from the top-N.
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

        uint64_t child_hash = shared->zobrist.update_place(hash, mov, color);
        for (uint8_t j = 0; j < cap_info.count; ++j) {
            child_hash = shared->zobrist.update_capture(
                child_hash, cap_info.positions[j], opponent(color));
        }
        if (cap_info.pairs > 0) {
            uint8_t new_count = board.captures(color);
            uint8_t old_count = new_count - cap_info.pairs;
            child_hash = shared->zobrist.update_capture_count(
                child_hash, color, old_count, new_count);
        }

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

    // Store root result in TT for reuse by other workers (Lazy SMP) and next iteration
    if (!is_stopped()) {
        EntryType entry_type = (best_score >= beta)
            ? EntryType::LowerBound
            : EntryType::Exact; // Root always starts with full window
        shared->tt.store(hash, depth, best_score, entry_type, best_move.is_sentinel() ? std::nullopt : std::optional<Pos>(best_move));
    }

    SearchResult result;
    result.best_move = best_move;
    result.score = best_score;
    result.depth = depth;
    result.nodes = nodes;
    result.stats = stats;
    return result;
}

// =========================================================================
// WorkerSearcher: move_creates_four (extension de menace)
// =========================================================================
// Quand un coup cree un quatre (4 pierres alignees avec au moins un bout
// ouvert), c'est une menace forcante : l'adversaire DOIT y repondre.
// On accorde alors +1 ply de profondeur pour ne pas couper la recherche
// au milieu d'une sequence tactique. C'est le concept de "threat extension".

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
// WorkerSearcher: is_threatened (detection de menace)
// =========================================================================
// Determine si la position est "sous pression" : l'adversaire a une menace
// immediate (quatre, trois ouvert, menace de capture). Sert principalement
// pour le Null Move Pruning : on n'ose pas "passer son tour" si on est
// deja menace, car l'adversaire pourrait concretiser sa menace.

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
        // Threatened by: 4+ in a row, OR open three (3 with 2 open ends)
        if (count >= 4 || (count >= 3 && open_ends >= 2)) return true;
    }

    // Gap pattern threat: opponent has 3+ stones with one gap in any direction
    // from last_move. E.g., O_OO or OO_O -- filling gap creates an open four.
    for (auto& d : dirs) {
        int8_t dr = d[0], dc = d[1];
        for (int8_t sign : {-1, 1}) {
            int8_t sdr = dr * sign;
            int8_t sdc = dc * sign;
            int32_t gap_count = 1; // last_move stone
            bool gap_used = false;
            int32_t gap_open_ends = 0;

            // Scan positive direction (from last_move)
            for (int8_t i = 1; i <= 4; ++i) {
                int8_t gr = static_cast<int8_t>(last_move.row) + sdr * i;
                int8_t gc = static_cast<int8_t>(last_move.col) + sdc * i;
                if (gr < 0 || gr >= sz || gc < 0 || gc >= sz) break;
                Stone s = board.get(Pos{uint8_t(gr), uint8_t(gc)});
                if (s == opp) {
                    ++gap_count;
                } else if (s == Stone::Empty && !gap_used) {
                    // Check stone after gap
                    int8_t nr = gr + sdr;
                    int8_t nc = gc + sdc;
                    if (nr >= 0 && nr < sz && nc >= 0 && nc < sz
                        && board.get(Pos{uint8_t(nr), uint8_t(nc)}) == opp) {
                        gap_used = true;
                        continue; // skip gap, next iteration picks up the stone
                    }
                    ++gap_open_ends;
                    break;
                } else {
                    break;
                }
            }
            // Scan negative direction
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
            // Gap pattern: 3+ stones with gap AND open ends -> strong threat
            if (gap_used && (gap_count >= 4 || (gap_count >= 3 && gap_open_ends >= 2))) {
                return true;
            }
        }
    }

    // Capture setup: opponent's last_move brackets our pair on one side
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
// WorkerSearcher: quiescence (recherche de quiescence)
// =========================================================================
// Quand la recherche alpha-beta atteint sa profondeur maximale, on ne
// s'arrete pas brutalement : on continue a explorer les coups "forcants"
// (cinq, quatre, captures gagnantes) jusqu'a ce que la position soit
// "calme" (quiescente). Ca evite l'effet d'horizon, ou le moteur rate
// une menace qui se concretise juste apres la limite de profondeur.
// C'est comme si un joueur se disait : "avant de conclure, verifions
// qu'aucune suite forcee ne change radicalement l'evaluation."

int32_t WorkerSearcher::quiescence(
    Board& board, Stone color, int32_t alpha, int32_t beta,
    Pos last_move, int8_t qs_depth, uint64_t hash) {

    ++nodes;

    // Time check (less frequent in QS -- every 4096 nodes)
    if ((nodes & 4095) == 0 && check_time()) return 0;
    if (is_stopped()) return 0;

    // Terminal: opponent just won
    Stone last_player = opponent(color);
    if (board.captures(last_player) >= 5) return -PatternScore::FIVE;
    if (has_five_at_pos(board, last_move, last_player)) {
        // Check breakable five (endgame capture rule)
        auto five_line = find_five_line_at_pos(board, last_move, last_player);
        if (five_line.has_value()) {
            if (can_break_five_by_capture(board, *five_line, last_player)) {
                return search_five_break(
                    board, color, 0, alpha, beta, *five_line, last_player, hash);
            }
        }
        return -PatternScore::FIVE;
    }

    // TT probe: reuse results from previous searches or other QS nodes.
    if (auto probe = shared->tt.probe(hash, 0, alpha, beta)) {
        return probe->first;
    }

    // Stand-pat: static evaluation as lower bound
    int32_t stand_pat = evaluate(board, color);

    // Beta cutoff: position is already too good (fail high)
    if (stand_pat >= beta) return stand_pat;

    int32_t original_alpha = alpha;
    if (stand_pat > alpha) alpha = stand_pat;

    // Depth limit for quiescence
    if (qs_depth >= MAX_QS_DEPTH) return stand_pat;

    // After depth 4 in QS, only search fives (no more fours)
    bool fours_allowed = qs_depth < 6;

    Stone opp = opponent(color);
    constexpr int8_t sz = static_cast<int8_t>(BOARD_SIZE);
    constexpr int8_t dirs[4][2] = {{1,0},{0,1},{1,1},{1,-1}};

    // Generate forcing moves only: fives, fours, capture-wins.
    MoveList forcing_moves;
    bool seen[BOARD_SIZE][BOARD_SIZE] = {};

    // Iterate all stones (black then white)
    for (Pos stone_pos : board.black) {
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
                    // Our line
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

                    // Opponent line
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

                // Capture-win check
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
    for (Pos stone_pos : board.white) {
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

    if (forcing_moves.empty()) return stand_pat;

    // Sort by priority (highest first)
    forcing_moves.sort_descending();

    // Move count pruning (PentaZen-style): limit forcing moves per QS node.
    size_t max_qs_moves = (qs_depth <= 2) ? 8 : 4;

    int32_t best_score = stand_pat;
    Pos best_move = Pos::sentinel();
    size_t moves_searched = 0;

    for (auto& [mov, priority] : forcing_moves) {
        // Always search fives (priority >= 850), limit fours
        if (priority < 850) {
            if (moves_searched >= max_qs_moves) break;
        }
        ++moves_searched;
        board.place_stone(mov, color);
        CaptureInfo cap_info = execute_captures_fast(board, mov, color);

        // Compute child hash for TT
        uint64_t child_hash = shared->zobrist.update_place(hash, mov, color);
        for (uint8_t j = 0; j < cap_info.count; ++j) {
            child_hash = shared->zobrist.update_capture(
                child_hash, cap_info.positions[j], opponent(color));
        }
        if (cap_info.pairs > 0) {
            uint8_t new_count = board.captures(color);
            uint8_t old_count = new_count - cap_info.pairs;
            child_hash = shared->zobrist.update_capture_count(
                child_hash, color, old_count, new_count);
        }

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
        if (score >= beta) break; // Beta cutoff
    }

    // TT store: cache QS result at depth 0.
    if (!is_stopped()) {
        EntryType entry_type;
        if (best_score >= beta) {
            entry_type = EntryType::LowerBound;
        } else if (best_score > original_alpha) {
            entry_type = EntryType::Exact;
        } else {
            entry_type = EntryType::UpperBound;
        }
        shared->tt.store(hash, 0, best_score, entry_type, best_move.is_sentinel() ? std::nullopt : std::optional<Pos>(best_move));
    }

    return best_score;
}

// =========================================================================
// WorkerSearcher: search_five_break (capture sur un cinq)
// =========================================================================
// En Ninuki-renju, un cinq n'est pas forcement gagnant : si l'adversaire
// peut capturer une paire qui fait partie du cinq, celui-ci est "cassable".
// Cette fonction explore uniquement les coups de capture qui brisent le
// cinq adverse, puis continue en alpha-beta normal apres la capture.

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

        // Make move
        board.place_stone(break_pos, color);
        CaptureInfo cap_info = execute_captures_fast(board, break_pos, color);

        // Update Zobrist hash
        uint64_t child_hash = shared->zobrist.update_place(hash, break_pos, color);
        for (uint8_t j = 0; j < cap_info.count; ++j) {
            child_hash = shared->zobrist.update_capture(
                child_hash, cap_info.positions[j], opponent(color));
        }
        if (cap_info.pairs > 0) {
            uint8_t new_count = board.captures(color);
            uint8_t old_count = new_count - cap_info.pairs;
            child_hash = shared->zobrist.update_capture_count(
                child_hash, color, old_count, new_count);
        }

        // Recurse: depth-1 into normal alpha-beta (handles depth<=0 -> quiescence)
        int8_t search_depth = std::max(int8_t(depth - 1), int8_t(0));
        int32_t score = -alpha_beta(
            board, opponent(color), search_depth,
            -beta, -alpha, break_pos, child_hash, true);

        // Unmake move
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
// WorkerSearcher: alpha_beta (coeur de la recherche negamax)
// =========================================================================
// C'est la fonction recursive principale. Elle explore l'arbre de jeu en
// utilisant le framework "negamax" : au lieu de maximiser pour un joueur
// et minimiser pour l'autre, on inverse toujours le signe du score.
// Chaque joueur cherche a maximiser SON score, et le score de l'adversaire
// est le negatif du notre.
//
// Alpha et beta representent la fenetre de recherche :
// - alpha = meilleur score garanti pour le joueur actuel
// - beta = meilleur score garanti pour l'adversaire
// Si on trouve un score >= beta, on coupe (l'adversaire ne permettrait
// jamais cette position). C'est l'elagage alpha-beta.
//
// Plusieurs optimisations s'ajoutent par-dessus (NMP, LMR, PVS, etc.)
// pour reduire drastiquement le nombre de noeuds explores.

int32_t WorkerSearcher::alpha_beta(
    Board& board, Stone color, int8_t depth,
    int32_t alpha, int32_t beta, Pos last_move,
    uint64_t hash, bool allow_null) {

    ++nodes;

    // Time check every 512 nodes (~1.5ms at 300k NPS)
    if ((nodes & 511) == 0) {
        if (check_time()) return 0;
    }
    if (is_stopped()) return 0;

    // Fast terminal check
    Stone last_player = opponent(color);
    if (board.captures(last_player) >= 5) return -PatternScore::FIVE;
    if (has_five_at_pos(board, last_move, last_player)) {
        // Check if the five is breakable by capture (endgame rule).
        auto five_line = find_five_line_at_pos(board, last_move, last_player);
        if (five_line.has_value()) {
            if (can_break_five_by_capture(board, *five_line, last_player)) {
                return search_five_break(
                    board, color, depth, alpha, beta, *five_line, last_player, hash);
            }
        }
        return -PatternScore::FIVE;
    }

    // Check if the side to move already has an existing five on the board.
    if (board.stone_count() >= 10 && has_five_in_row(board, color)) {
        return PatternScore::FIVE;
    }

    if (depth <= 0) {
#ifndef GOMOKU_NO_QS
        return quiescence(board, color, alpha, beta, last_move, 0, hash);
#else
        return evaluate(board, color);
#endif
    }

    // TT probe
    stats.tt_probes += 1;
    if (auto probe = shared->tt.probe(hash, depth, alpha, beta)) {
        stats.tt_score_hits += 1;
        return probe->first;
    }

    // Pre-compute static eval for pruning decisions.
    bool non_terminal = std::abs(alpha) < PatternScore::FIVE - 100
        && std::abs(beta) < PatternScore::FIVE - 100;
    int32_t static_eval = non_terminal ? evaluate(board, color) : 0;

    // Reverse Futility Pruning (RFP) : "elagage par futilite inverse"
    // Si l'evaluation statique est deja TRES au-dessus de beta (la borne
    // adverse), meme en perdant un open three par niveau, on resterait
    // au-dessus. Inutile de chercher plus loin : on coupe directement.
    // C'est l'inverse du futility pruning (qui regarde en dessous d'alpha).
#ifndef GOMOKU_NO_RFP
    if (depth <= 3 && non_terminal
        && static_eval - PatternScore::OPEN_THREE * static_cast<int32_t>(depth) >= beta) {
        return static_eval;
    }
#endif

    // Razoring : si l'evaluation statique est tres en dessous d'alpha,
    // on tombe directement en quiescence search. L'idee : si notre
    // position est si mauvaise que meme un gros bonus ne suffirait pas
    // a rattraper alpha, autant verifier rapidement avec la quiescence
    // plutot que de perdre du temps dans une recherche complete.
#ifndef GOMOKU_NO_RAZORING
    if (depth <= 3 && non_terminal
        && static_eval + PatternScore::OPEN_THREE * static_cast<int32_t>(depth) <= alpha) {
        int32_t qs_score = quiescence(board, color, alpha, beta, last_move, 0, hash);
        if (qs_score <= alpha) return qs_score;
    }
#endif

    // Null Move Pruning (NMP) : on simule "passer son tour" (ne rien jouer)
    // et on cherche avec une profondeur reduite. Si meme en passant son
    // tour on est encore au-dessus de beta, c'est que la position est si
    // bonne qu'on peut couper. C'est tres efficace dans les positions calmes.
    // On ne l'utilise PAS quand on est menace (is_threatened) car passer
    // son tour serait catastrophique. R adaptatif : 2 + depth/6.
    // Verification : au-dela de depth 6, on verifie le cutoff pour eviter
    // les "faux positifs" dans les positions de zugzwang (rares au Gomoku).
#ifndef GOMOKU_NO_NMP
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
#endif

    Pos tt_move = shared->tt.get_best_move(hash).value_or(Pos::sentinel());
    if (!tt_move.is_sentinel()) stats.tt_move_hits += 1;

    // Internal Iterative Deepening (IID) : quand on n'a pas de "meilleur
    // coup" en memoire (pas de TT move), on lance une mini-recherche a
    // profondeur reduite pour en trouver un. Ce coup sera ensuite essaye
    // en premier dans la vraie recherche, ce qui ameliore enormement
    // l'elagage alpha-beta (le premier coup coupe souvent tout le reste).
#ifndef GOMOKU_NO_IID
    if (tt_move.is_sentinel() && depth >= 6) {
        int8_t iid_depth = std::max(int8_t(depth - 4), int8_t(1));
        alpha_beta(board, color, iid_depth, alpha, beta, last_move, hash, false);
        if (!is_stopped()) {
            tt_move = shared->tt.get_best_move(hash).value_or(Pos::sentinel());
        }
    }
#endif

    last_move_for_ordering = last_move;
    MoveList moves = generate_moves_ordered(board, color, tt_move, depth, MAX_INTERNAL_MOVES);
    if (moves.empty()) return evaluate(board, color);

    // Limitation adaptative du nombre de coups : dans les positions calmes
    // (pas de menace tactique), on explore moins de coups qu'en position
    // tendue. Par exemple, a profondeur 2 en position calme, on ne regarde
    // que 4 coups au lieu de 6. Ca reduit drastiquement le branching factor.
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

    // Lazy double-three: keep the first max_moves valid moves.
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

    // Futility Pruning : a faible profondeur, si l'evaluation statique +
    // une marge genereuse ne depasse toujours pas alpha, les coups restants
    // (non tactiques) sont ignores. L'idee : un coup "tranquille" ne peut
    // pas rattraper un ecart aussi grand en si peu de coups.
    // Les marges augmentent avec la profondeur restante.
#ifndef GOMOKU_NO_FUTILITY
    bool futility_ok = depth <= 4 && non_terminal;
    int32_t futility_margin;
    switch (depth) {
        case 1: futility_margin = PatternScore::CLOSED_FOUR; break;
        case 2: futility_margin = PatternScore::OPEN_FOUR; break;
        case 3: futility_margin = PatternScore::OPEN_FOUR + PatternScore::OPEN_THREE; break;
        default: futility_margin = PatternScore::OPEN_FOUR * 2; break;
    }
#endif

    int32_t best_score = -INF;
    Pos best_move = Pos::sentinel();
    EntryType entry_type = EntryType::UpperBound;

    for (size_t i = 0; i < moves.size(); ++i) {
        Pos mov = moves[i].first;
        int32_t move_score = moves[i].second;

        // Futility pruning
#ifndef GOMOKU_NO_FUTILITY
        if (futility_ok && i > 0 && static_eval + futility_margin <= alpha) {
            if (move_score < 800000) continue;
        }
#endif

        // Late Move Pruning (LMP) : les coups tries tres tard dans l'ordre
        // (ceux qui avaient un mauvais score de tri) ont tres peu de chances
        // d'etre le meilleur coup. A faible profondeur, on les saute
        // completement. Le seuil depend de la profondeur : 3 + depth * 2.
#ifndef GOMOKU_NO_LMP
        if (i > 0 && depth <= 3
            && i >= (3 + static_cast<size_t>(depth) * 2)
            && move_score < 800000) {
            continue;
        }
#endif

        board.place_stone(mov, color);
        CaptureInfo cap_info = execute_captures_fast(board, mov, color);

        uint64_t child_hash = shared->zobrist.update_place(hash, mov, color);
        for (uint8_t j = 0; j < cap_info.count; ++j) {
            child_hash = shared->zobrist.update_capture(
                child_hash, cap_info.positions[j], opponent(color));
        }
        if (cap_info.pairs > 0) {
            uint8_t new_count = board.captures(color);
            uint8_t old_count = new_count - cap_info.pairs;
            child_hash = shared->zobrist.update_capture_count(
                child_hash, color, old_count, new_count);
        }

        bool is_capture = cap_info.pairs > 0;

        // Threat extension
#ifndef GOMOKU_NO_THREAT_EXT
        int8_t extension = (depth >= 2 && move_creates_four(board, mov, color))
            ? int8_t(1) : int8_t(0);
#else
        int8_t extension = 0;
#endif

        // PVS + LMR (Principal Variation Search + Late Move Reductions)
        //
        // PVS : le 1er coup est cherche avec la fenetre complete [alpha, beta].
        // Les suivants avec une fenetre nulle [alpha, alpha+1]. Si un coup
        // depasse alpha, on relance avec la fenetre complete pour confirmer.
        //
        // LMR : les coups tardifs dans l'ordre sont cherches a profondeur
        // reduite. Reduction logarithmique : sqrt(depth) * sqrt(move_index) / 2.
        // Les coups calmes (score < 500k) recoivent +2 de reduction,
        // les semi-tactiques (score < 800k) recoivent +1. Si la recherche
        // reduite depasse alpha, on re-cherche a profondeur normale.
        int32_t score;
        if (i == 0) {
            score = -alpha_beta(
                board, opponent(color), depth - 1 + extension,
                -beta, -alpha, mov, child_hash, true);
        } else {
#ifndef GOMOKU_NO_LMR
            // LMR: logarithmic reduction + score-aware adjustment
            int8_t reduction;
            if (is_capture || extension > 0 || depth < 2 || i < 1) {
                reduction = 0;
            } else {
                float d = static_cast<float>(depth);
                float m = static_cast<float>(i);
                int8_t r_val = static_cast<int8_t>(std::sqrt(d) * std::sqrt(m) / 2.0f);
                // Score-aware: quiet moves with no tactical value get more reduction
                if (move_score < 500000) r_val += 2;
                else if (move_score < 800000) r_val += 1;
                reduction = std::clamp(r_val, int8_t(1), int8_t(depth - 2));
            }
#else
            int8_t reduction = 0;
#endif
            int8_t search_depth = std::max(int8_t(depth - 1 + extension - reduction), int8_t(0));

#ifndef GOMOKU_NO_PVS
            score = -alpha_beta(
                board, opponent(color), search_depth,
                -(alpha + 1), -alpha, mov, child_hash, true);

#ifndef GOMOKU_NO_LMR
            if (!is_stopped() && reduction > 0 && score > alpha) {
                score = -alpha_beta(
                    board, opponent(color), depth - 1 + extension,
                    -(alpha + 1), -alpha, mov, child_hash, true);
            }
#endif

            if (!is_stopped() && score > alpha && score < beta) {
                score = -alpha_beta(
                    board, opponent(color), depth - 1 + extension,
                    -beta, -alpha, mov, child_hash, true);
            }
#else
            score = -alpha_beta(
                board, opponent(color), search_depth,
                -beta, -alpha, mov, child_hash, true);
#endif
        }

        undo_captures(board, color, cap_info);
        board.remove_stone(mov);

        if (is_stopped()) return 0;

        if (score > best_score) {
            best_score = score;
            best_move = mov;
        }

        if (score >= beta) {
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

            // Countermove: record best response to opponent's last move
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

    shared->tt.store(hash, depth, best_score, entry_type, best_move.is_sentinel() ? std::nullopt : std::optional<Pos>(best_move));
    return best_score;
}

// =========================================================================
// WorkerSearcher: count_line_both (analyse de ligne bidirectionnelle)
// =========================================================================
// Scanne une ligne dans une direction donnee a partir d'une position,
// en comptant simultanement les pierres de notre couleur ET celles de
// l'adversaire. C'est une optimisation : au lieu de faire deux passes
// separees, on fait tout en un seul parcours. Permet aussi de detecter
// les "trous" (gaps) dans les lignes (ex: OO_O = 3 pierres avec un trou).

WorkerSearcher::LineBothResult WorkerSearcher::count_line_both(
    const Bitboard& my_bb, const Bitboard& opp_bb,
    Pos pos, int8_t dr, int8_t dc) {

    constexpr int8_t sz = static_cast<int8_t>(BOARD_SIZE);

    // My color accumulators
    int32_t mc = 1, mo = 0;
    bool m_gap = false;
    int32_t mc_pos = 0, mc_neg = 0;

    // Opponent color accumulators
    int32_t oc = 1, oo = 0;
    bool o_gap = false;
    int32_t oc_pos = 0, oc_neg = 0;

    // === Positive direction ===
    {
        int8_t r = static_cast<int8_t>(pos.row) + dr;
        int8_t c = static_cast<int8_t>(pos.col) + dc;
        bool my_active = true, my_consec = true;
        bool opp_active = true, opp_consec = true;

        while ((my_active || opp_active) && r >= 0 && r < sz && c >= 0 && c < sz) {
            Pos p{uint8_t(r), uint8_t(c)};
            bool is_my = my_bb.get(p);
            bool is_opp = is_my ? false : opp_bb.get(p);

            if (is_my) {
                if (my_active) {
                    ++mc;
                    if (my_consec) ++mc_pos;
                }
                if (opp_active) opp_active = false;
            } else if (is_opp) {
                if (opp_active) {
                    ++oc;
                    if (opp_consec) ++oc_pos;
                }
                if (my_active) my_active = false;
            } else {
                // Empty cell
                if (my_active) {
                    if (!m_gap) {
                        my_consec = false;
                        int8_t nr = r + dr;
                        int8_t nc = c + dc;
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
                        int8_t nr = r + dr;
                        int8_t nc = c + dc;
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
            r += dr;
            c += dc;
        }
    }

    // === Negative direction ===
    {
        int8_t r = static_cast<int8_t>(pos.row) - dr;
        int8_t c = static_cast<int8_t>(pos.col) - dc;
        bool my_active = true, my_consec = true;
        bool opp_active = true, opp_consec = true;

        while ((my_active || opp_active) && r >= 0 && r < sz && c >= 0 && c < sz) {
            Pos p{uint8_t(r), uint8_t(c)};
            bool is_my = my_bb.get(p);
            bool is_opp = is_my ? false : opp_bb.get(p);

            if (is_my) {
                if (my_active) {
                    ++mc;
                    if (my_consec) ++mc_neg;
                }
                if (opp_active) opp_active = false;
            } else if (is_opp) {
                if (opp_active) {
                    ++oc;
                    if (opp_consec) ++oc_neg;
                }
                if (my_active) my_active = false;
            } else {
                // Empty cell
                if (my_active) {
                    if (!m_gap) {
                        my_consec = false;
                        int8_t nr = r - dr;
                        int8_t nc = c - dc;
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
                        int8_t nr = r - dr;
                        int8_t nc = c - dc;
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
            r -= dr;
            c -= dc;
        }
    }

    return {mc, mo, m_gap, 1 + mc_pos + mc_neg,
            oc, oo, o_gap, 1 + oc_pos + oc_neg};
}

// =========================================================================
// WorkerSearcher: score_move (evaluation de priorite d'un coup)
// =========================================================================
// Le tri des coups est l'element le plus important pour la performance de
// l'elagage alpha-beta. Si le meilleur coup est essaye en premier, toutes
// les branches alternatives sont coupees immediatement.
//
// Philosophie "defense d'abord" : on attribue un score de priorite a chaque
// coup selon une echelle stricte. Les coups les plus urgents (cinq,
// bloquer un cinq, captures gagnantes, fourchettes) sont toujours en tete.
//
// Heuristiques de tri :
// - TT move : 1 000 000 (le meilleur coup de la recherche precedente)
// - Killer moves : 500 000 / 490 000 (coups qui ont cause un cutoff)
// - Countermove : 400 000 (meilleure reponse au dernier coup adverse)
// - History : accumule au fil de la recherche (coups souvent bons)
//
// Le scan de vulnerabilite aux captures est fusionne dans la meme boucle
// de directions pour eviter un parcours supplementaire.

int32_t WorkerSearcher::score_move(
    const Board& board, Pos mov, Stone color,
    Pos tt_move, int8_t depth) const {

    Stone opp = opponent(color);

    if (!tt_move.is_sentinel() && tt_move == mov) return 1000000;

    // Direct bitboard access
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

    // Inline capture detection (replaces 2x count_captures_fast calls)
    int32_t my_capture_pairs = 0;
    int32_t opp_capture_pairs = 0;

    // Fused vulnerability + proximity scan accumulators
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

        // --- Line analysis (existing count_line_both) ---
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

        // --- Inline capture + vulnerability + proximity (fused, 2 signs) ---
        for (int8_t sign : {1, -1}) {
            int8_t sdr = dr * sign, sdc = dc * sign;

            // Positions at distance 1, 2, 3 from mov
            int8_t r1 = mr + sdr,     c1 = mc_col + sdc;
            int8_t r2 = mr + sdr * 2, c2 = mc_col + sdc * 2;
            int8_t r3 = mr + sdr * 3, c3 = mc_col + sdc * 3;

            // Proximity: my stone at distance 1
            if (r1 >= 0 && r1 < sz && c1 >= 0 && c1 < sz
                && my_bb.get(Pos{uint8_t(r1), uint8_t(c1)})) {
                proximity += 200;
            }

            // Capture detection + vulnerability (needs distance 1,2,3 in bounds)
            if (r3 < 0 || r3 >= sz || c3 < 0 || c3 >= sz) continue;

            Pos p1{uint8_t(r1), uint8_t(c1)};
            Pos p2{uint8_t(r2), uint8_t(c2)};
            Pos p3{uint8_t(r3), uint8_t(c3)};

            bool p1_my = my_bb.get(p1), p1_opp = opp_bb.get(p1);
            bool p2_my = my_bb.get(p2), p2_opp = opp_bb.get(p2);
            bool p3_my = my_bb.get(p3), p3_opp = opp_bb.get(p3);

            // MY capture: mov -> opp -> opp -> my
            if (p1_opp && p2_opp && p3_my) ++my_capture_pairs;
            // OPP capture (if opp placed here): mov -> my -> my -> opp
            if (p1_my && p2_my && p3_opp) ++opp_capture_pairs;

            // Vulnerability: mov creates a capturable pair with p1
            // Pattern: empty(-1) MOV MY(+1) OPP(+2) -> opp captures at -1
            // We check the reverse sign for the "before" position
            int8_t rb = mr - sdr, cb = mc_col - sdc;
            if (rb >= 0 && rb < sz && cb >= 0 && cb < sz) {
                Pos pb{uint8_t(rb), uint8_t(cb)};
                bool pb_empty = !my_bb.get(pb) && !opp_bb.get(pb);
                bool p2_empty = !p2_my && !p2_opp;

                // empty-MOV-ally-opp: opponent places at empty to capture
                if (pb_empty && p1_my && p2_opp) ++vuln_count;
                // opp-MOV-ally-empty: opponent places at p2 to capture
                if (opp_bb.get(pb) && p1_my && p2_empty) ++vuln_count;
                // empty-MOV-ally-empty: 2-move setup vulnerability
                if (pb_empty && p1_my && p2_empty) ++setup_vuln_count;

                // Immediate capture penalty patterns
                bool p1_empty = !p1_my && !p1_opp;

                // opp @ before, ally @ +1, empty @ +2 -> 1-move capture threat
                if (opp_bb.get(pb) && p1_my && p2_empty)
                    imm_cap_penalty += 150000;
                // empty @ before, ally @ +1, empty @ +2 -> 2-move setup
                if (pb_empty && p1_my && p2_empty)
                    imm_cap_penalty += setup_weight;

                // Also check: empty-ally-MOV-opp and opp-ally-MOV-empty patterns
                // (ally at pb, opponent/empty at p1)
                if (!my_bb.get(pb) && !opp_bb.get(pb)) {
                    // skip, pb is empty
                } else if (my_bb.get(pb)) {
                    // ally-MOV: check rb2 and p1
                    int8_t rb2 = mr - sdr * 2, cb2 = mc_col - sdc * 2;
                    if (rb2 >= 0 && rb2 < sz && cb2 >= 0 && cb2 < sz) {
                        Pos pb2{uint8_t(rb2), uint8_t(cb2)};
                        bool pb2_empty = !my_bb.get(pb2) && !opp_bb.get(pb2);
                        // empty-ally-MOV-opp -> vuln
                        if (pb2_empty && p1_opp) ++vuln_count;
                        // opp-ally-MOV-empty -> vuln
                        if (opp_bb.get(pb2) && p1_empty) ++vuln_count;
                        // empty-ally-MOV-empty -> setup vuln
                        if (pb2_empty && p1_empty) ++setup_vuln_count;
                    }
                }
            }
        }
    }

    // Derived totals for fork detection
    int32_t my_total_fours = my_open_four_count + my_closed_four_count;
    int32_t opp_total_fours = opp_open_four_count + opp_closed_four_count;

    // === Priority ladder with fork detection ===
    if (my_five) return 900000;
    if (opp_five) return 895000;

    if (my_capture_pairs > 0 && static_cast<int32_t>(board.captures(color)) + my_capture_pairs >= 5)
        return 890000;
    if (opp_capture_pairs > 0 && static_cast<int32_t>(opp_caps) + opp_capture_pairs >= 5)
        return 885000;

    // MY FORKS
    if (my_total_fours >= 2) return 880000;
    if (my_total_fours >= 1 && my_open_three_count >= 1) return 878000;
    if (my_open_four_count >= 1) return 870000;

    // BLOCK OPPONENT FORKS
    if (opp_total_fours >= 2) return 868000;
    if (opp_total_fours >= 1 && opp_open_three_count >= 1) return 866000;
    if (opp_open_four_count >= 1) return 860000;

    // Capture-based urgency
    if (opp_capture_pairs > 0 && opp_caps >= 3) return 855000;
    if (opp_capture_pairs > 0 && opp_caps >= 2) return 845000;

    // Double open three fork
    if (my_open_three_count >= 2) return 840000;
    if (opp_open_three_count >= 2) return 838000;

    // Single forcing threats
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

    // Compute capture penalty from fused vulnerability scan
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

    // Multi-directional development bonus
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
// WorkerSearcher: generate_moves_ordered (generation de coups candidats)
// =========================================================================
// Genere tous les coups candidats dans un rayon de 2 cases autour des
// pierres existantes. Chaque coup est evalue par score_move puis trie
// par priorite decroissante. On utilise un tri partiel (partial sort) :
// seuls les k premiers coups sont garantis tries, car on n'en explorera
// qu'une douzaine au maximum. Trier 12 coups sur 100 est bien plus rapide
// que de trier les 100.

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

    // Iterate black then white
    for (Pos pos : board.black) {
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
    for (Pos pos : board.white) {
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

    moves.partial_sort_descending(sort_limit);
    return moves;
}

// =========================================================================
// Searcher: public API (interface publique du moteur de recherche)
// =========================================================================
// Deux modes de recherche :
// - search() : deterministe, mono-thread, pour les tests unitaires
// - search_timed() : avec Lazy SMP (parallelisme) et gestion du temps
//
// Lazy SMP : on lance N threads qui cherchent tous independamment le
// meilleur coup. Chaque thread partage la table de transposition (TT),
// donc quand un thread trouve un bon coup, les autres en beneficient.
// Les threads commencent a des profondeurs differentes pour diversifier
// l'exploration. A la fin, on garde le meilleur resultat (plus profond,
// puis meilleur score).
//
// Gestion du temps : a chaque profondeur, on estime le temps de la
// profondeur suivante grace au "branching factor" observe (rapport entre
// les temps des deux dernieres profondeurs). Si l'estimation depasse le
// temps restant, on s'arrete. Un facteur de securite de 1.2x evite les
// depassements.

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

SearchResult Searcher::search(const Board& board, Stone color, int8_t max_depth) {
    shared_->stopped.store(false, std::memory_order_relaxed);
    max_depth_ = max_depth;

    WorkerSearcher worker;
    worker.shared = shared_;
    worker.nodes = 0;
    worker.max_depth = max_depth;
    std::memset(worker.killer_moves, 0xFF, sizeof(worker.killer_moves));
    std::memcpy(worker.history, history_, sizeof(history_));
    std::memset(worker.countermove, 0xFF, sizeof(worker.countermove));
    worker.last_move_for_ordering = Pos::sentinel();
    worker.start_time = std::nullopt;
    worker.time_limit = std::nullopt;
    worker.stats = SearchStats{};

    SearchResult best_result;
    best_result.score = 0;
    best_result.depth = 0;
    best_result.nodes = 0;

    Board work_board = board;
    bool prev_was_winning = false;
    bool prev_was_losing = false;

    for (int8_t depth = 1; depth <= max_depth; ++depth) {
        SearchResult result = worker.search_root(work_board, color, depth, -INF, INF);
        best_result = result;
        best_result.depth = depth;

        bool is_winning = best_result.score >= PatternScore::FIVE - 100;
        bool is_losing = best_result.score <= -(PatternScore::FIVE - 100);

        if (is_winning && prev_was_winning && depth >= 12) break;
        if (is_losing && prev_was_losing && depth >= 10) break;

        prev_was_winning = is_winning;
        prev_was_losing = is_losing;
    }

    best_result.nodes = worker.nodes;
    best_result.stats = worker.stats;
    std::memcpy(history_, worker.history, sizeof(history_));
    return best_result;
}

SearchResult Searcher::search_timed(
    const Board& board, Stone color, int8_t max_depth, uint64_t time_limit_ms) {

    shared_->stopped.store(false, std::memory_order_relaxed);
    max_depth_ = max_depth;
    auto start = std::chrono::steady_clock::now();
    // Hard time limit — check_time every 512 nodes provides ~1.5ms resolution.
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

    // Collect results -- pick best (deepest search, then highest score)
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

TTStats Searcher::tt_stats() const {
    return shared_->tt.stats();
}

void Searcher::clear_tt() const {
    shared_->tt.clear();
}

} // namespace gomoku
