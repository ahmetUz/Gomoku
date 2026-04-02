// Moteur IA principal -- orchestration de toutes les recherches
//
// Pipeline de decision :
// 1) VCF (victoire forcee par quatres continus, 50ms max)
// 2) Recherche alpha-beta (pour tout le reste)
//
// L'alpha-beta gere nativement les cas simples (ouverture, victoire
// immediate, defense) grace au tri des coups et a la quiescence search.
// Les VCF sont gardes car ils detectent des sequences forcees profondes
// (jusqu'a 30 plies) que l'alpha-beta ne verrait pas dans son budget temps.

#include "gomoku/engine/engine.hpp"
#include "gomoku/minimax/transposition_table.hpp"
#include "gomoku/rules/capture.hpp"
#include "gomoku/rules/win.hpp"
#include "gomoku/rules/forbidden.hpp"
#include "gomoku/eval/heuristic.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace gomoku {

// Format a board position as human-readable notation (e.g., "J10")
std::string pos_to_notation(Pos pos) {
    char col_char;
    if (pos.col < 8) {
        col_char = 'A' + pos.col;
    } else {
        col_char = 'A' + pos.col + 1; // skip 'I'
    }
    return std::string(1, col_char) + std::to_string(pos.row + 1);
}

// Write a log message to both gomoku_ai.log and stderr.
void ai_log(const std::string& msg) {
#ifdef NDEBUG
    (void)msg;
#else
    static std::ofstream file("gomoku_ai.log", std::ios::app);
    if (file.is_open()) {
        file << msg << "\n";
        file.flush();
    }
    std::cerr << msg << "\n";
#endif
}

// =========================================================================
// MoveResult factory methods
// =========================================================================

uint64_t MoveResult::compute_nps(uint64_t nodes, uint64_t time_ms) {
    if (time_ms == 0) return 0;
    return nodes * 1000 / time_ms / 1000;
}

MoveResult MoveResult::vcf_win(Pos pos, uint64_t time_ms, uint64_t nodes) {
    MoveResult result;
    result.best_move = pos;
    result.score = 900'000;
    result.search_type = SearchType::VCF;
    result.time_ms = time_ms;
    result.nodes = nodes;
    result.nps = compute_nps(nodes, time_ms);
    return result;
}

MoveResult MoveResult::from_alphabeta(const SearchResult& search_result, uint64_t time_ms, uint8_t tt_usage) {
    MoveResult result;
    result.best_move = search_result.best_move;
    result.score = search_result.score;
    result.search_type = SearchType::AlphaBeta;
    result.time_ms = time_ms;
    result.nodes = search_result.nodes;
    result.depth = search_result.depth;
    result.tt_usage = tt_usage;
    result.nps = compute_nps(search_result.nodes, time_ms);
    return result;
}

// =========================================================================
// AIEngine
// =========================================================================

AIEngine::AIEngine()
    : searcher_(16)
    , threat_searcher_(30, 12)
    , max_depth_(20)
    , time_limit_ms_(500)
{
}

AIEngine::AIEngine(size_t tt_size_mb, int8_t max_depth, uint64_t time_limit_ms)
    : searcher_(tt_size_mb)
    , threat_searcher_(30, 12)
    , max_depth_(max_depth)
    , time_limit_ms_(time_limit_ms)
{
}

// =========================================================================
// get_move_with_stats -- pipeline de decision
// =========================================================================

MoveResult AIEngine::get_move_with_stats(const Board& board, Stone color) {
    auto start = std::chrono::steady_clock::now();

    uint32_t total_captured = 2 * (board.captures(Stone::Black) + board.captures(Stone::White));
    uint32_t move_num = board.stone_count() + total_captured + 1;
    const char* color_str = (color == Stone::Black) ? "Black" : "White";

    std::string separator(60, '=');
    std::ostringstream oss;
    oss << "\n" << separator << "\n"
        << "[Move #" << move_num << " | AI: " << color_str
        << " | Stones: " << board.stone_count()
        << " | B-cap: " << static_cast<int>(board.captures(Stone::Black))
        << " W-cap: " << static_cast<int>(board.captures(Stone::White)) << "]";
    ai_log(oss.str());

    // ? opponent color, used for capture checks and VCF skip logic
    Stone opponent = gomoku::opponent(color);

    // 1. Search VCF (Victory by Continuous Fours) - our forced win
    // Skipped if opponent has >= 4 captures (capture win could interfere).
    uint8_t opp_captures = board.captures(opponent);
    if (opp_captures < 4) {
        ThreatResult vcf_result = threat_searcher_.search_vcf(board, color, 50);
        if (vcf_result.found && !vcf_result.winning_sequence.empty()) {
            oss.str("");
            oss << "  VCF FOUND: sequence=[";
            for (size_t i = 0; i < vcf_result.winning_sequence.size(); ++i) {
                if (i > 0) oss << " -> ";
                oss << pos_to_notation(vcf_result.winning_sequence[i]);
            }
            oss << "]";
            ai_log(oss.str());
            auto elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count());
            return MoveResult::vcf_win(vcf_result.winning_sequence[0], elapsed,
                                      threat_searcher_.nodes());
        }
        ai_log("  VCF: not found (" + std::to_string(threat_searcher_.nodes()) + " nodes)");
    } else {
        ai_log("  VCF SKIPPED: opponent has " + std::to_string(opp_captures) +
               " captures (unreliable)");
    }

    // 2. Alpha-Beta search
    SearchResult result = searcher_.search_timed(board, color, max_depth_, time_limit_ms_);
    uint8_t tt_usage = tt_stats().usage_percent;
    auto elapsed = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count());

    oss.str("");
    oss << "  ALPHA-BETA: move=";
    if (!result.best_move.is_sentinel()) {
        oss << pos_to_notation(result.best_move);
    } else {
        oss << "none";
    }
    oss << " score=" << result.score
        << " depth=" << static_cast<int>(result.depth)
        << " nodes=" << result.nodes
        << " time=" << elapsed << "ms"
        << " nps=" << MoveResult::compute_nps(result.nodes, elapsed) << "k"
        << " tt=" << static_cast<int>(tt_usage) << "%";
    ai_log(oss.str());

    oss.str("");
    oss << "    Stats: beta_cutoffs=" << result.stats.beta_cutoffs
        << " first_move_rate=" << std::fixed << std::setprecision(1)
        << result.stats.first_move_rate() << "%"
        << " tt_probes=" << result.stats.tt_probes
        << " tt_score_rate=" << result.stats.tt_score_rate() << "%"
        << " tt_move_hits=" << result.stats.tt_move_hits;
    ai_log(oss.str());

    return MoveResult::from_alphabeta(result, elapsed, tt_usage);
}

// =========================================================================
// Configuration
// =========================================================================

void AIEngine::stop() {
    searcher_.stop();
}

void AIEngine::clear_cache() {
    searcher_.clear_tt();
    searcher_.clear_history();
}

TTStats AIEngine::tt_stats() const {
    return searcher_.tt_stats();
}

} // namespace gomoku
