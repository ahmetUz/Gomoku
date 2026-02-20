// Gomoku AI Performance Benchmark
//
// Simulates full AI-vs-AI games and targeted mid-game positions
// to measure response times, node counts, search depth, and TT efficiency.
//
// Usage: ./build/bench/gomoku_bench

#include "gomoku/engine/engine.hpp"
#include "gomoku/rules/capture.hpp"
#include "gomoku/rules/win.hpp"
#include "gomoku/rules/forbidden.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <cmath>

using namespace gomoku;

// ── Per-move statistics ──────────────────────────────────────────────────────

struct MoveStat {
    uint32_t move_num;
    uint64_t time_ms;
    uint64_t nodes;
    int8_t   depth;
    int32_t  score;
    uint8_t  tt_usage;
    uint64_t nps;
    SearchType type;
    std::string notation;
};

struct GameReport {
    std::string name;
    std::vector<MoveStat> moves;
    std::optional<Stone> winner;
    uint32_t total_moves;
    uint64_t total_time_ms;
};

// ── Helpers ──────────────────────────────────────────────────────────────────

const char* search_type_str(SearchType t) {
    switch (t) {
        case SearchType::ImmediateWin: return "ImmWin";
        case SearchType::VCF:          return "VCF";
        case SearchType::VCT:          return "VCT";
        case SearchType::Defense:      return "Def";
        case SearchType::AlphaBeta:    return "AB";
    }
    return "?";
}

void print_separator(const std::string& title) {
    std::cout << "\n" << std::string(72, '=') << "\n"
              << "  " << title << "\n"
              << std::string(72, '=') << "\n";
}

void print_move_table(const std::vector<MoveStat>& moves) {
    std::cout << std::left
              << std::setw(5)  << "#"
              << std::setw(8)  << "Move"
              << std::setw(8)  << "Type"
              << std::setw(10) << "Time(ms)"
              << std::setw(12) << "Nodes"
              << std::setw(7)  << "Depth"
              << std::setw(10) << "Score"
              << std::setw(8)  << "TT%"
              << std::setw(10) << "kNPS"
              << "\n"
              << std::string(72, '-') << "\n";

    for (auto& m : moves) {
        std::cout << std::left
                  << std::setw(5)  << m.move_num
                  << std::setw(8)  << m.notation
                  << std::setw(8)  << search_type_str(m.type)
                  << std::setw(10) << m.time_ms
                  << std::setw(12) << m.nodes
                  << std::setw(7)  << static_cast<int>(m.depth)
                  << std::setw(10) << m.score
                  << std::setw(8)  << static_cast<int>(m.tt_usage)
                  << std::setw(10) << m.nps
                  << "\n";
    }
}

void print_summary(const GameReport& report) {
    if (report.moves.empty()) return;

    // Filter only alpha-beta moves (the expensive ones)
    std::vector<uint64_t> ab_times;
    std::vector<uint64_t> ab_nodes;
    std::vector<int8_t> ab_depths;
    std::vector<uint64_t> all_times;

    for (auto& m : report.moves) {
        all_times.push_back(m.time_ms);
        if (m.type == SearchType::AlphaBeta) {
            ab_times.push_back(m.time_ms);
            ab_nodes.push_back(m.nodes);
            ab_depths.push_back(m.depth);
        }
    }

    auto avg = [](const std::vector<uint64_t>& v) -> double {
        if (v.empty()) return 0;
        return static_cast<double>(std::accumulate(v.begin(), v.end(), uint64_t(0)))
               / static_cast<double>(v.size());
    };
    auto max_val = [](const std::vector<uint64_t>& v) -> uint64_t {
        return v.empty() ? 0 : *std::max_element(v.begin(), v.end());
    };
    auto percentile = [](std::vector<uint64_t> v, double p) -> uint64_t {
        if (v.empty()) return 0;
        std::sort(v.begin(), v.end());
        size_t idx = static_cast<size_t>(std::ceil(p / 100.0 * static_cast<double>(v.size()))) - 1;
        return v[std::min(idx, v.size() - 1)];
    };

    std::cout << "\n--- Summary ---\n";
    std::cout << "  Winner: " << (report.winner ? (*report.winner == Stone::Black ? "Black" : "White") : "None (draw/limit)") << "\n";
    std::cout << "  Total moves: " << report.total_moves << "\n";
    std::cout << "  Total game time: " << report.total_time_ms << " ms\n";
    std::cout << "\n  All moves:\n";
    std::cout << "    Avg time:  " << std::fixed << std::setprecision(1) << avg(all_times) << " ms\n";
    std::cout << "    Max time:  " << max_val(all_times) << " ms\n";
    std::cout << "    P50 time:  " << percentile(all_times, 50) << " ms\n";
    std::cout << "    P90 time:  " << percentile(all_times, 90) << " ms\n";
    std::cout << "    P95 time:  " << percentile(all_times, 95) << " ms\n";

    if (!ab_times.empty()) {
        std::cout << "\n  Alpha-Beta moves only (" << ab_times.size() << " moves):\n";
        std::cout << "    Avg time:  " << std::fixed << std::setprecision(1) << avg(ab_times) << " ms\n";
        std::cout << "    Max time:  " << max_val(ab_times) << " ms\n";
        std::cout << "    P50 time:  " << percentile(ab_times, 50) << " ms\n";
        std::cout << "    P90 time:  " << percentile(ab_times, 90) << " ms\n";
        std::cout << "    P95 time:  " << percentile(ab_times, 95) << " ms\n";
        std::cout << "    Avg nodes: " << std::fixed << std::setprecision(0) << avg(ab_nodes) << "\n";
        std::cout << "    Max nodes: " << max_val(ab_nodes) << "\n";

        int8_t max_d = *std::max_element(ab_depths.begin(), ab_depths.end());
        double avg_d = 0;
        for (auto d : ab_depths) avg_d += d;
        avg_d /= static_cast<double>(ab_depths.size());
        std::cout << "    Avg depth: " << std::fixed << std::setprecision(1) << avg_d << "\n";
        std::cout << "    Max depth: " << static_cast<int>(max_d) << "\n";
    }
}

// ── Game simulation ─────────────────────────────────────────────────────────

GameReport simulate_ai_vs_ai(const std::string& name, AIEngine& engine,
                              uint32_t max_moves = 100) {
    GameReport report;
    report.name = name;
    report.winner = std::nullopt;

    Board board;
    Stone turn = Stone::Black;

    auto game_start = std::chrono::steady_clock::now();

    for (uint32_t move = 1; move <= max_moves; ++move) {
        MoveResult result = engine.get_move_with_stats(board, turn);

        if (result.best_move.is_sentinel()) {
            break; // no move found
        }

        MoveStat stat;
        stat.move_num = move;
        stat.time_ms = result.time_ms;
        stat.nodes = result.nodes;
        stat.depth = result.depth;
        stat.score = result.score;
        stat.tt_usage = result.tt_usage;
        stat.nps = result.nps;
        stat.type = result.search_type;
        stat.notation = pos_to_notation(result.best_move);
        report.moves.push_back(stat);

        // Execute the move
        board.place_stone(result.best_move, turn);
        execute_captures_fast(board, result.best_move, turn);

        // Check win
        auto winner = check_winner(board, turn);
        if (winner) {
            report.winner = winner;
            report.total_moves = move;
            auto game_end = std::chrono::steady_clock::now();
            report.total_time_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(game_end - game_start).count());
            return report;
        }

        turn = opponent(turn);
    }

    report.total_moves = static_cast<uint32_t>(report.moves.size());
    auto game_end = std::chrono::steady_clock::now();
    report.total_time_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(game_end - game_start).count());
    return report;
}

// ── Scenario: mid-game position with tension ────────────────────────────────

GameReport benchmark_midgame_tension(AIEngine& engine) {
    // Set up a complex mid-game position with threats on both sides
    // Black has a strong center presence, White has flanking stones
    Board board;

    // Build a tense mid-game position (~20 stones)
    // Black stones: center cluster with potential lines
    Pos black_stones[] = {
        {9,9}, {9,10}, {8,9}, {10,8}, {8,11},
        {7,8}, {10,10}, {11,7}, {6,12}, {8,7}
    };
    // White stones: surrounding, blocking, with own threats
    Pos white_stones[] = {
        {9,8}, {8,10}, {10,9}, {7,9}, {9,11},
        {10,7}, {7,11}, {11,8}, {6,9}, {9,7}
    };

    for (auto p : black_stones) board.place_stone(p, Stone::Black);
    for (auto p : white_stones) board.place_stone(p, Stone::White);

    GameReport report;
    report.name = "Mid-game Tension (20 stones, complex)";

    auto game_start = std::chrono::steady_clock::now();
    Stone turn = Stone::Black;

    // Play 20 more moves from this position
    for (uint32_t move = 1; move <= 20; ++move) {
        MoveResult result = engine.get_move_with_stats(board, turn);
        if (result.best_move.is_sentinel()) break;

        MoveStat stat;
        stat.move_num = move;
        stat.time_ms = result.time_ms;
        stat.nodes = result.nodes;
        stat.depth = result.depth;
        stat.score = result.score;
        stat.tt_usage = result.tt_usage;
        stat.nps = result.nps;
        stat.type = result.search_type;
        stat.notation = pos_to_notation(result.best_move);
        report.moves.push_back(stat);

        board.place_stone(result.best_move, turn);
        execute_captures_fast(board, result.best_move, turn);

        auto winner = check_winner(board, turn);
        if (winner) {
            report.winner = winner;
            break;
        }
        turn = opponent(turn);
    }

    report.total_moves = static_cast<uint32_t>(report.moves.size());
    auto game_end = std::chrono::steady_clock::now();
    report.total_time_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(game_end - game_start).count());
    return report;
}

// ── Scenario: capture-heavy game ────────────────────────────────────────────

GameReport benchmark_capture_pressure(AIEngine& engine) {
    // Position with many capture opportunities — forces engine to evaluate captures
    Board board;

    // Create pairs that can be captured: B-WW-_ patterns
    // Pattern 1: row 9
    board.place_stone({9,5}, Stone::Black);
    board.place_stone({9,6}, Stone::White);
    board.place_stone({9,7}, Stone::White);
    // Pattern 2: col 9
    board.place_stone({5,9}, Stone::Black);
    board.place_stone({6,9}, Stone::White);
    board.place_stone({7,9}, Stone::White);
    // Pattern 3: diagonal
    board.place_stone({5,5}, Stone::Black);
    board.place_stone({6,6}, Stone::White);
    board.place_stone({7,7}, Stone::White);
    // Additional stones to create tension
    board.place_stone({9,9}, Stone::Black);
    board.place_stone({10,10}, Stone::Black);
    board.place_stone({8,8}, Stone::White);
    board.place_stone({9,10}, Stone::White);
    board.place_stone({10,9}, Stone::White);
    board.place_stone({11,9}, Stone::Black);
    board.place_stone({8,10}, Stone::Black);

    GameReport report;
    report.name = "Capture Pressure (16 stones, many capture threats)";

    auto game_start = std::chrono::steady_clock::now();
    Stone turn = Stone::Black; // Black to play, can capture on multiple axes

    for (uint32_t move = 1; move <= 20; ++move) {
        MoveResult result = engine.get_move_with_stats(board, turn);
        if (result.best_move.is_sentinel()) break;

        MoveStat stat;
        stat.move_num = move;
        stat.time_ms = result.time_ms;
        stat.nodes = result.nodes;
        stat.depth = result.depth;
        stat.score = result.score;
        stat.tt_usage = result.tt_usage;
        stat.nps = result.nps;
        stat.type = result.search_type;
        stat.notation = pos_to_notation(result.best_move);
        report.moves.push_back(stat);

        board.place_stone(result.best_move, turn);
        execute_captures_fast(board, result.best_move, turn);

        auto winner = check_winner(board, turn);
        if (winner) {
            report.winner = winner;
            break;
        }
        turn = opponent(turn);
    }

    report.total_moves = static_cast<uint32_t>(report.moves.size());
    auto game_end = std::chrono::steady_clock::now();
    report.total_time_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(game_end - game_start).count());
    return report;
}

// ── Scenario: late-game dense board ─────────────────────────────────────────

GameReport benchmark_dense_board(AIEngine& engine) {
    // Dense board (~40 stones) — large move generation, deep evaluation
    Board board;

    Pos black_stones[] = {
        {9,9}, {9,10}, {9,11}, {8,9}, {8,10},
        {10,8}, {10,10}, {7,8}, {7,10}, {11,7},
        {11,9}, {6,9}, {6,11}, {12,8}, {12,10},
        {5,10}, {13,9}, {8,7}, {10,12}, {7,12}
    };
    Pos white_stones[] = {
        {9,8}, {8,11}, {10,9}, {10,11}, {8,8},
        {7,9}, {7,11}, {11,8}, {11,10}, {6,10},
        {6,8}, {12,9}, {12,7}, {13,8}, {13,10},
        {5,9}, {5,11}, {9,12}, {10,7}, {7,7}
    };

    for (auto p : black_stones) board.place_stone(p, Stone::Black);
    for (auto p : white_stones) board.place_stone(p, Stone::White);

    GameReport report;
    report.name = "Dense Board (40 stones, late-game)";

    auto game_start = std::chrono::steady_clock::now();
    Stone turn = Stone::Black;

    for (uint32_t move = 1; move <= 20; ++move) {
        MoveResult result = engine.get_move_with_stats(board, turn);
        if (result.best_move.is_sentinel()) break;

        MoveStat stat;
        stat.move_num = move;
        stat.time_ms = result.time_ms;
        stat.nodes = result.nodes;
        stat.depth = result.depth;
        stat.score = result.score;
        stat.tt_usage = result.tt_usage;
        stat.nps = result.nps;
        stat.type = result.search_type;
        stat.notation = pos_to_notation(result.best_move);
        report.moves.push_back(stat);

        board.place_stone(result.best_move, turn);
        execute_captures_fast(board, result.best_move, turn);

        auto winner = check_winner(board, turn);
        if (winner) {
            report.winner = winner;
            break;
        }
        turn = opponent(turn);
    }

    report.total_moves = static_cast<uint32_t>(report.moves.size());
    auto game_end = std::chrono::steady_clock::now();
    report.total_time_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(game_end - game_start).count());
    return report;
}

// ── Scenario: open early game (worst for branching factor) ──────────────────

GameReport benchmark_early_game(AIEngine& engine) {
    // Only 4 stones on board — huge branching factor, tests move ordering
    Board board;
    board.place_stone({9,9}, Stone::Black);
    board.place_stone({9,10}, Stone::White);
    board.place_stone({10,9}, Stone::Black);
    board.place_stone({8,10}, Stone::White);

    GameReport report;
    report.name = "Early Game (4 stones, wide search tree)";

    auto game_start = std::chrono::steady_clock::now();
    Stone turn = Stone::Black;

    // Play 16 moves (8 per side) to traverse early -> mid game
    for (uint32_t move = 1; move <= 16; ++move) {
        MoveResult result = engine.get_move_with_stats(board, turn);
        if (result.best_move.is_sentinel()) break;

        MoveStat stat;
        stat.move_num = move;
        stat.time_ms = result.time_ms;
        stat.nodes = result.nodes;
        stat.depth = result.depth;
        stat.score = result.score;
        stat.tt_usage = result.tt_usage;
        stat.nps = result.nps;
        stat.type = result.search_type;
        stat.notation = pos_to_notation(result.best_move);
        report.moves.push_back(stat);

        board.place_stone(result.best_move, turn);
        execute_captures_fast(board, result.best_move, turn);

        auto winner = check_winner(board, turn);
        if (winner) {
            report.winner = winner;
            break;
        }
        turn = opponent(turn);
    }

    report.total_moves = static_cast<uint32_t>(report.moves.size());
    auto game_end = std::chrono::steady_clock::now();
    report.total_time_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(game_end - game_start).count());
    return report;
}

// ── Global summary ──────────────────────────────────────────────────────────

void print_global_summary(const std::vector<GameReport>& reports) {
    print_separator("GLOBAL PERFORMANCE SUMMARY");

    std::vector<uint64_t> all_ab_times;
    std::vector<uint64_t> all_times;
    uint64_t total_nodes = 0;
    uint64_t total_time = 0;
    uint32_t total_moves = 0;

    for (auto& r : reports) {
        for (auto& m : r.moves) {
            all_times.push_back(m.time_ms);
            total_nodes += m.nodes;
            if (m.type == SearchType::AlphaBeta) {
                all_ab_times.push_back(m.time_ms);
            }
        }
        total_time += r.total_time_ms;
        total_moves += r.total_moves;
    }

    std::sort(all_times.begin(), all_times.end());
    std::sort(all_ab_times.begin(), all_ab_times.end());

    auto percentile = [](const std::vector<uint64_t>& sorted, double p) -> uint64_t {
        if (sorted.empty()) return 0;
        size_t idx = static_cast<size_t>(std::ceil(p / 100.0 * static_cast<double>(sorted.size()))) - 1;
        return sorted[std::min(idx, sorted.size() - 1)];
    };
    auto avg = [](const std::vector<uint64_t>& v) -> double {
        if (v.empty()) return 0;
        return static_cast<double>(std::accumulate(v.begin(), v.end(), uint64_t(0)))
               / static_cast<double>(v.size());
    };

    std::cout << "\n  Total scenarios:     " << reports.size() << "\n";
    std::cout << "  Total moves played:  " << total_moves << "\n";
    std::cout << "  Total wall time:     " << total_time << " ms ("
              << std::fixed << std::setprecision(1) << (total_time / 1000.0) << " s)\n";
    std::cout << "  Total nodes:         " << total_nodes << "\n";

    std::cout << "\n  ALL moves (" << all_times.size() << " total):\n";
    std::cout << "    Avg:  " << std::fixed << std::setprecision(1) << avg(all_times) << " ms\n";
    std::cout << "    P50:  " << percentile(all_times, 50) << " ms\n";
    std::cout << "    P90:  " << percentile(all_times, 90) << " ms\n";
    std::cout << "    P95:  " << percentile(all_times, 95) << " ms\n";
    std::cout << "    P99:  " << percentile(all_times, 99) << " ms\n";
    std::cout << "    Max:  " << (all_times.empty() ? 0 : all_times.back()) << " ms\n";

    if (!all_ab_times.empty()) {
        std::cout << "\n  Alpha-Beta moves only (" << all_ab_times.size() << "):\n";
        std::cout << "    Avg:  " << std::fixed << std::setprecision(1) << avg(all_ab_times) << " ms\n";
        std::cout << "    P50:  " << percentile(all_ab_times, 50) << " ms\n";
        std::cout << "    P90:  " << percentile(all_ab_times, 90) << " ms\n";
        std::cout << "    P95:  " << percentile(all_ab_times, 95) << " ms\n";
        std::cout << "    P99:  " << percentile(all_ab_times, 99) << " ms\n";
        std::cout << "    Max:  " << all_ab_times.back() << " ms\n";
    }

    // Time distribution histogram
    std::cout << "\n  Time distribution:\n";
    uint32_t buckets[] = {0, 0, 0, 0, 0, 0, 0}; // <10, <50, <100, <200, <500, <1000, >=1000
    const char* labels[] = {"<10ms", "<50ms", "<100ms", "<200ms", "<500ms", "<1000ms", ">=1000ms"};
    uint64_t thresholds[] = {10, 50, 100, 200, 500, 1000};
    for (auto t : all_times) {
        bool placed = false;
        for (int i = 0; i < 6; ++i) {
            if (t < thresholds[i]) { buckets[i]++; placed = true; break; }
        }
        if (!placed) buckets[6]++;
    }
    for (int i = 0; i < 7; ++i) {
        double pct = all_times.empty() ? 0 :
            (100.0 * static_cast<double>(buckets[i]) / static_cast<double>(all_times.size()));
        std::cout << "    " << std::setw(9) << std::left << labels[i]
                  << std::setw(5) << std::right << buckets[i]
                  << "  (" << std::fixed << std::setprecision(1) << std::setw(5) << pct << "%)\n";
    }
}

// ── Main ────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n"
              << "========================================\n"
              << "  GOMOKU AI PERFORMANCE BENCHMARK\n"
              << "========================================\n";

    // Use default engine config (64MB TT, depth 20, 500ms)
    AIEngine engine;
    std::vector<GameReport> reports;

    // 1. Full AI vs AI game
    {
        print_separator("Scenario 1: AI vs AI Full Game (max 60 moves)");
        engine.clear_cache();
        auto report = simulate_ai_vs_ai("AI vs AI Full Game", engine, 60);
        print_move_table(report.moves);
        print_summary(report);
        reports.push_back(std::move(report));
    }

    // 2. Early game (wide branching)
    {
        print_separator("Scenario 2: Early Game (4 stones, wide search)");
        engine.clear_cache();
        auto report = benchmark_early_game(engine);
        print_move_table(report.moves);
        print_summary(report);
        reports.push_back(std::move(report));
    }

    // 3. Mid-game tension
    {
        print_separator("Scenario 3: Mid-game Tension (20 stones)");
        engine.clear_cache();
        auto report = benchmark_midgame_tension(engine);
        print_move_table(report.moves);
        print_summary(report);
        reports.push_back(std::move(report));
    }

    // 4. Capture pressure
    {
        print_separator("Scenario 4: Capture Pressure (16 stones)");
        engine.clear_cache();
        auto report = benchmark_capture_pressure(engine);
        print_move_table(report.moves);
        print_summary(report);
        reports.push_back(std::move(report));
    }

    // 5. Dense late-game board
    {
        print_separator("Scenario 5: Dense Board (40 stones, late-game)");
        engine.clear_cache();
        auto report = benchmark_dense_board(engine);
        print_move_table(report.moves);
        print_summary(report);
        reports.push_back(std::move(report));
    }

    // Global summary
    print_global_summary(reports);

    std::cout << "\n========================================\n"
              << "  BENCHMARK COMPLETE\n"
              << "========================================\n\n";

    return 0;
}
