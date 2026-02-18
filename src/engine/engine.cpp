#include "gomoku/engine/engine.hpp"
#include "gomoku/search/tt.hpp"
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
    // Columns: A=0, B=1, ..., H=7, J=8 (skip I), K=9, ...
    char col_char;
    if (pos.col < 8) {
        col_char = 'A' + pos.col;
    } else {
        col_char = 'A' + pos.col + 1; // skip 'I'
    }
    // Rows: 1=0, 2=1, ..., 19=18 (board display: bottom=1, top=19)
    return std::string(1, col_char) + std::to_string(pos.row + 1);
}

// Write a log message to both gomoku_ai.log and stderr
void ai_log(const std::string& msg) {
    std::ofstream file("gomoku_ai.log", std::ios::app);
    if (file.is_open()) {
        file << msg << "\n";
        file.flush();
    }
    std::cerr << msg << "\n";
}

// MoveResult factory methods

uint64_t MoveResult::compute_nps(uint64_t nodes, uint64_t time_ms) {
    if (time_ms == 0) {
        return 0;
    }
    return nodes * 1000 / time_ms / 1000;
}

MoveResult MoveResult::immediate_win(Pos pos, uint64_t time_ms) {
    MoveResult result;
    result.best_move = pos;
    result.score = 1'000'000;
    result.search_type = SearchType::ImmediateWin;
    result.time_ms = time_ms;
    result.nodes = 1;
    result.depth = 0;
    result.tt_usage = 0;
    result.nps = 0;
    return result;
}

MoveResult MoveResult::vcf_win(Pos pos, uint64_t time_ms, uint64_t nodes) {
    MoveResult result;
    result.best_move = pos;
    result.score = 900'000;
    result.search_type = SearchType::VCF;
    result.time_ms = time_ms;
    result.nodes = nodes;
    result.depth = 0;
    result.tt_usage = 0;
    result.nps = compute_nps(nodes, time_ms);
    return result;
}

MoveResult MoveResult::vct_win(Pos pos, uint64_t time_ms, uint64_t nodes) {
    MoveResult result;
    result.best_move = pos;
    result.score = 800'000;
    result.search_type = SearchType::VCT;
    result.time_ms = time_ms;
    result.nodes = nodes;
    result.depth = 0;
    result.tt_usage = 0;
    result.nps = compute_nps(nodes, time_ms);
    return result;
}

MoveResult MoveResult::no_move(uint64_t time_ms) {
    MoveResult result;
    result.best_move = std::nullopt;
    result.score = 0;
    result.search_type = SearchType::AlphaBeta;
    result.time_ms = time_ms;
    result.nodes = 0;
    result.depth = 0;
    result.tt_usage = 0;
    result.nps = 0;
    return result;
}

MoveResult MoveResult::defense(Pos pos, int32_t score, uint64_t time_ms, uint64_t nodes) {
    MoveResult result;
    result.best_move = pos;
    result.score = score;
    result.search_type = SearchType::Defense;
    result.time_ms = time_ms;
    result.nodes = nodes;
    result.depth = 0;
    result.tt_usage = 0;
    result.nps = 0;
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

MoveResult MoveResult::alpha_beta(Pos pos, int32_t score, uint64_t time_ms, uint64_t nodes) {
    MoveResult result;
    result.best_move = pos;
    result.score = score;
    result.search_type = SearchType::AlphaBeta;
    result.time_ms = time_ms;
    result.nodes = nodes;
    result.depth = 0;
    result.tt_usage = 0;
    result.nps = 0;
    return result;
}

// AIEngine implementation

AIEngine::AIEngine()
    : searcher_(64)
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

std::optional<Pos> AIEngine::get_move(const Board& board, Stone color) {
    return get_move_with_stats(board, color).best_move;
}

MoveResult AIEngine::get_move_with_stats(const Board& board, Stone color) {
    auto start = std::chrono::steady_clock::now();

    // Actual game move number: stones on board + captured stones (removed) + 1
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

    // 0. Opening book for fast early game response
    if (auto opening_move = get_opening_move(board, color)) {
        ai_log("  Stage 0 OPENING: " + pos_to_notation(*opening_move) + " (book move)");
        auto elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count());
        return MoveResult::alpha_beta(*opening_move, 0, elapsed, 1);
    }

    // 0.5: Check if opponent has an existing breakable five — MUST break it NOW
    Stone opponent = gomoku::opponent(color);
    if (auto opp_five = find_five_positions(board, opponent)) {
        if (can_break_five_by_capture(board, *opp_five, opponent)) {
            auto break_moves = find_five_break_moves(board, *opp_five, opponent);
            std::vector<Pos> valid_breaks;
            for (auto p : break_moves) {
                if (is_valid_move(board, p, color)) {
                    valid_breaks.push_back(p);
                }
            }

            std::ostringstream break_strs;
            for (size_t i = 0; i < valid_breaks.size(); ++i) {
                if (i > 0) break_strs << ", ";
                break_strs << pos_to_notation(valid_breaks[i]);
            }
            ai_log("  Stage 0.5 BREAK FIVE: opponent five exists! Break moves: [" + break_strs.str() + "]");

            if (valid_breaks.size() == 1) {
                // Check if the single break allows opponent to recreate an UNBREAKABLE five
                Pos brk = valid_breaks[0];
                Board test_board = board;
                test_board.place_stone(brk, color);
                CaptureInfo cap_info = execute_captures_fast(test_board, brk, color);
                bool recreates_unbreakable = false;

                for (uint8_t i = 0; i < cap_info.count; ++i) {
                    Pos cap_pos = cap_info.positions[i];
                    test_board.place_stone(cap_pos, opponent);
                    if (has_five_at_pos(test_board, cap_pos, opponent)) {
                        if (auto new_five = find_five_line_at_pos(test_board, cap_pos, opponent)) {
                            if (!can_break_five_by_capture(test_board, *new_five, opponent)) {
                                recreates_unbreakable = true;
                            }
                        }
                    }
                    test_board.remove_stone(cap_pos);
                    if (recreates_unbreakable) break;
                }

                if (recreates_unbreakable) {
                    ai_log("  >>> FORCED BREAK " + pos_to_notation(brk) +
                           " rejected: opponent recreates UNBREAKABLE five — falling through to alpha-beta");
                } else {
                    ai_log("  >>> FORCED BREAK: " + pos_to_notation(brk));
                    auto elapsed = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start).count());
                    return MoveResult::defense(brk, -900'000, elapsed, 1);
                }
            } else if (valid_breaks.empty()) {
                ai_log("  Stage 0.5 BREAK FIVE: NO valid break moves — opponent wins!");
            } else {
                // Multiple break moves: evaluate each
                Pos best_move = valid_breaks[0];
                int32_t best_score = std::numeric_limits<int32_t>::min();
                bool any_safe_break = false;
                Board test_board = board;

                for (auto brk : valid_breaks) {
                    test_board.place_stone(brk, color);
                    CaptureInfo cap_info = execute_captures_fast(test_board, brk, color);

                    bool recreates_unbreakable = false;
                    for (uint8_t i = 0; i < cap_info.count; ++i) {
                        Pos cap_pos = cap_info.positions[i];
                        test_board.place_stone(cap_pos, opponent);
                        if (has_five_at_pos(test_board, cap_pos, opponent)) {
                            if (auto new_five = find_five_line_at_pos(test_board, cap_pos, opponent)) {
                                if (!can_break_five_by_capture(test_board, *new_five, opponent)) {
                                    recreates_unbreakable = true;
                                }
                            }
                        }
                        test_board.remove_stone(cap_pos);
                        if (recreates_unbreakable) break;
                    }

                    if (!recreates_unbreakable) {
                        int32_t score = evaluate(test_board, color);
                        if (score > best_score || !any_safe_break) {
                            best_score = score;
                            best_move = brk;
                        }
                        any_safe_break = true;
                    } else {
                        ai_log("    Break " + pos_to_notation(brk) +
                               " rejected: opponent recreates UNBREAKABLE five");
                    }

                    undo_captures(test_board, color, cap_info);
                    test_board.remove_stone(brk);
                }

                if (any_safe_break) {
                    ai_log("  >>> BEST BREAK: " + pos_to_notation(best_move) +
                           " (eval=" + std::to_string(best_score) + ")");
                    auto elapsed = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start).count());
                    return MoveResult::defense(best_move, -900'000, elapsed, valid_breaks.size());
                }
                ai_log("  Stage 0.5: All breaks lead to UNBREAKABLE recreation — falling through to alpha-beta");
            }
        } else {
            ai_log("  Stage 0.5 WARNING: Opponent has UNBREAKABLE five!");
        }
    }

    // 1. Check for immediate winning move
    if (auto win_move = find_immediate_win(board, color)) {
        ai_log("  Stage 1 IMMEDIATE WIN: " + pos_to_notation(*win_move));
        auto elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count());
        return MoveResult::immediate_win(*win_move, elapsed);
    }
    ai_log("  Stage 1 Immediate win: none");

    // 2. Check if opponent can win immediately - MUST block
    auto opponent_threats = find_winning_moves(board, opponent);
    oss.str("");
    oss << "  Stage 2 Opponent threats: " << opponent_threats.size() << " positions";
    if (!opponent_threats.empty()) {
        oss << " [";
        for (size_t i = 0; i < opponent_threats.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << pos_to_notation(opponent_threats[i]);
        }
        oss << "]";
    }
    ai_log(oss.str());

    if (opponent_threats.size() == 1) {
        Pos block_pos = opponent_threats[0];
        if (is_valid_move(board, block_pos, color)) {
            ai_log("  >>> DEFENSE (block immediate): " + pos_to_notation(block_pos));
            auto elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count());
            return MoveResult::defense(block_pos, -900'000, elapsed, 1);
        }
    } else if (opponent_threats.size() >= 2) {
        ai_log("  WARNING: Opponent has OPEN FOUR (2+ wins) - likely lost!");
    }

    // 3. Search VCF (Victory by Continuous Fours) - our forced win
    uint8_t opp_captures = board.captures(opponent);
    bool vcf_reliable = opp_captures < 4;
    if (vcf_reliable) {
        ThreatResult vcf_result = threat_searcher_.search_vcf(board, color);
        if (vcf_result.found && !vcf_result.winning_sequence.empty()) {
            oss.str("");
            oss << "  Stage 3 OUR VCF FOUND: sequence=[";
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
        ai_log("  Stage 3 Our VCF: not found (" + std::to_string(threat_searcher_.nodes()) + "nodes)");
    } else {
        ai_log("  Stage 3 VCF SKIPPED: opponent has " + std::to_string(opp_captures) +
               " captures (unreliable)");
    }

    // 4. Check opponent VCF - if opponent has a forced win, we must block
    uint8_t our_captures = board.captures(color);
    bool opp_vcf_reliable = our_captures < 4;
    if (opp_vcf_reliable) {
        ThreatResult opp_vcf = threat_searcher_.search_vcf(board, opponent);
        if (opp_vcf.found && !opp_vcf.winning_sequence.empty()) {
            oss.str("");
            oss << "  Stage 4 OPPONENT VCF FOUND: sequence=[";
            for (size_t i = 0; i < opp_vcf.winning_sequence.size(); ++i) {
                if (i > 0) oss << " -> ";
                oss << pos_to_notation(opp_vcf.winning_sequence[i]);
            }
            oss << "]";
            ai_log(oss.str());
            Pos block_pos = opp_vcf.winning_sequence[0];
            if (is_valid_move(board, block_pos, color)) {
                ai_log("  >>> DEFENSE (block VCF): " + pos_to_notation(block_pos));
                auto elapsed = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count());
                return MoveResult::defense(block_pos, -800'000, elapsed,
                                          threat_searcher_.nodes());
            }
        }
        ai_log("  Stage 4 Opponent VCF: not found (" + std::to_string(threat_searcher_.nodes()) + "nodes)");
    } else {
        ai_log("  Stage 4 Opponent VCF SKIPPED: we have " + std::to_string(our_captures) +
               " captures (can counter)");
    }

    // 5. Alpha-Beta search handles ALL strategy
    uint64_t adaptive_time = compute_time_limit(board);
    SearchResult result = searcher_.search_timed(board, color, max_depth_, adaptive_time);
    uint8_t tt_usage = tt_stats().usage_percent;
    auto elapsed = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count());

    oss.str("");
    oss << "  Stage 5 ALPHA-BETA: move=";
    if (result.best_move) {
        oss << pos_to_notation(*result.best_move);
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

uint64_t AIEngine::compute_time_limit(const Board& board) const {
    uint32_t stones = board.stone_count();

    uint32_t pct;
    if (stones <= 2) {
        pct = 30;  // Very early: center/adjacent, trivial
    } else if (stones <= 4) {
        pct = 60;  // Opening: still simple positions
    } else {
        pct = 100; // Mid-game+: full time for deep search
    }

    // Apply percentage with minimum floor of 300ms
    uint64_t adjusted = (time_limit_ms_ * pct / 100);
    return std::max(adjusted, uint64_t(300));
}

std::vector<Pos> AIEngine::find_winning_moves(const Board& board, Stone color) const {
    std::vector<Pos> wins;
    bool near_capture_win = board.captures(color) >= 4;
    Board test_board = board;

    for (uint8_t r = 0; r < BOARD_SIZE; ++r) {
        for (uint8_t c = 0; c < BOARD_SIZE; ++c) {
            Pos pos(r, c);
            if (!is_valid_move(board, pos, color)) {
                continue;
            }

            // Make move
            test_board.place_stone(pos, color);
            CaptureInfo cap_info = execute_captures_fast(test_board, pos, color);

            // Fast five-in-a-row check
            if (has_five_at_pos(test_board, pos, color)) {
                if (auto five = find_five_positions(test_board, color)) {
                    if (!can_break_five_by_capture(test_board, *five, color)) {
                        wins.push_back(pos);
                    }
                }
            }

            // Capture win check
            if (near_capture_win && test_board.captures(color) >= 5) {
                if (std::find(wins.begin(), wins.end(), pos) == wins.end()) {
                    wins.push_back(pos);
                }
            }

            // Unmake move
            undo_captures(test_board, color, cap_info);
            test_board.remove_stone(pos);
        }
    }
    return wins;
}

std::optional<Pos> AIEngine::find_immediate_win(const Board& board, Stone color) const {
    bool near_capture_win = board.captures(color) >= 4;
    Board test_board = board;

    for (uint8_t r = 0; r < BOARD_SIZE; ++r) {
        for (uint8_t c = 0; c < BOARD_SIZE; ++c) {
            Pos pos(r, c);
            if (!is_valid_move(board, pos, color)) {
                continue;
            }

            // Make move
            test_board.place_stone(pos, color);
            CaptureInfo cap_info = execute_captures_fast(test_board, pos, color);

            // Check five-in-a-row
            if (has_five_at_pos(test_board, pos, color)) {
                if (auto five = find_five_positions(test_board, color)) {
                    if (!can_break_five_by_capture(test_board, *five, color)) {
                        // Unbreakable five → immediate win
                        return pos;
                    }
                    // Five is STATICALLY breakable. Check if all breaks are illusory
                    if (is_illusory_break(test_board, *five, color)) {
                        return pos;
                    }
                }
            }

            // Check capture win
            if (near_capture_win && test_board.captures(color) >= 5) {
                return pos;
            }

            // Unmake move
            undo_captures(test_board, color, cap_info);
            test_board.remove_stone(pos);
        }
    }
    return std::nullopt;
}

bool AIEngine::is_illusory_break(const Board& board,
                                  const std::vector<Pos>& five_positions,
                                  Stone five_color) {
    Stone opponent = gomoku::opponent(five_color);
    auto break_moves = find_five_break_moves(board, five_positions, five_color);

    if (break_moves.empty()) {
        return false;
    }

    for (auto break_pos : break_moves) {
        // Simulate opponent's break capture
        Board sim = board;
        sim.place_stone(break_pos, opponent);
        CaptureInfo cap_info = execute_captures_fast(sim, break_pos, opponent);

        // Find which five stones were captured
        std::optional<Pos> captured_five_stone = std::nullopt;
        uint8_t captured_five_count = 0;
        for (uint8_t i = 0; i < cap_info.count; ++i) {
            if (std::find(five_positions.begin(), five_positions.end(),
                         cap_info.positions[i]) != five_positions.end()) {
                captured_five_stone = cap_info.positions[i];
                captured_five_count++;
            }
        }

        // If two or more five stones captured, can't recreate with one replay
        if (captured_five_count >= 2) {
            return false;
        }

        if (!captured_five_stone) {
            return false; // Break doesn't hit five stones (shouldn't happen)
        }

        Pos replay_pos = *captured_five_stone;

        // Position must be empty after capture
        if (!sim.is_empty(replay_pos)) {
            return false;
        }

        // Simulate replay
        sim.place_stone(replay_pos, five_color);

        // Check if five is recreated at replay position
        if (!has_five_at_pos(sim, replay_pos, five_color)) {
            return false;
        }

        // Check if recreated five is now unbreakable
        if (auto new_five = find_five_line_at_pos(sim, replay_pos, five_color)) {
            if (can_break_five_by_capture(sim, *new_five, five_color)) {
                return false; // Recreated five is still breakable → genuine break
            }
        } else {
            return false;
        }
    }

    return true; // All breaks are illusory → effectively unbreakable
}

void AIEngine::set_max_depth(int8_t depth) {
    max_depth_ = depth;
}

void AIEngine::set_time_limit(uint64_t time_ms) {
    time_limit_ms_ = time_ms;
}

void AIEngine::clear_cache() {
    searcher_.clear_tt();
    searcher_.clear_history();
}

TTStats AIEngine::tt_stats() const {
    return searcher_.tt_stats();
}

std::optional<Pos> AIEngine::get_opening_move(const Board& board, Stone color) const {
    // Empty board → center is universally optimal
    if (board.stone_count() == 0) {
        return Pos(9, 9);
    }

    // Second move: play diagonally adjacent to opponent's only stone
    if (board.stone_count() == 1) {
        Stone opponent = gomoku::opponent(color);
        const Bitboard* opp_bb = board.stones(opponent);
        if (opp_bb) {
            auto begin = opp_bb->begin();
            if (begin != opp_bb->end()) {
                Pos opp_pos = *begin;
                int32_t center = BOARD_SIZE / 2;
                int32_t diagonals[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
                std::optional<Pos> best = std::nullopt;
                int32_t best_dist = std::numeric_limits<int32_t>::max();

                for (auto [dr, dc] : diagonals) {
                    int32_t nr = static_cast<int32_t>(opp_pos.row) + dr;
                    int32_t nc = static_cast<int32_t>(opp_pos.col) + dc;
                    if (Pos::is_valid(nr, nc)) {
                        int32_t dist = std::abs(nr - center) + std::abs(nc - center);
                        if (dist < best_dist) {
                            best_dist = dist;
                            best = Pos(static_cast<uint8_t>(nr), static_cast<uint8_t>(nc));
                        }
                    }
                }
                return best;
            }
        }
    }

    // Third move: our 2nd stone as second player (opponent has 2 stones)
    if (board.stone_count() == 3) {
        Stone opponent = gomoku::opponent(color);
        const Bitboard* my_bb = board.stones(color);
        const Bitboard* opp_bb = board.stones(opponent);

        if (my_bb && opp_bb) {
            auto my_begin = my_bb->begin();
            auto opp_begin = opp_bb->begin();

            if (my_begin != my_bb->end() && opp_begin != opp_bb->end()) {
                Pos my_pos = *my_begin;
                Pos opp1 = *opp_begin;
                ++opp_begin;
                if (opp_begin != opp_bb->end()) {
                    Pos opp2 = *opp_begin;
                    ++my_begin;
                    ++opp_begin;

                    bool same_row = opp1.row == opp2.row;
                    bool same_col = opp1.col == opp2.col;

                    if (!(my_begin != my_bb->end()) && !(opp_begin != opp_bb->end()) &&
                        (same_row || same_col)) {
                        int32_t center = BOARD_SIZE / 2;
                        int32_t diags[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
                        Pos opp_stones[2] = {opp1, opp2};

                        std::optional<Pos> best = std::nullopt;
                        int32_t best_score = std::numeric_limits<int32_t>::min();

                        for (auto opp_pos : opp_stones) {
                            for (auto [dr, dc] : diags) {
                                int32_t nr = static_cast<int32_t>(opp_pos.row) + dr;
                                int32_t nc = static_cast<int32_t>(opp_pos.col) + dc;
                                if (!Pos::is_valid(nr, nc)) continue;

                                Pos p(static_cast<uint8_t>(nr), static_cast<uint8_t>(nc));
                                if (board.get(p) != Stone::Empty) continue;

                                int32_t center_dist = std::abs(nr - center) + std::abs(nc - center);

                                // Bonus: on same row/column as our stone (connectivity)
                                int32_t connectivity = 0;
                                if (nr == static_cast<int32_t>(my_pos.row) ||
                                    nc == static_cast<int32_t>(my_pos.col)) {
                                    connectivity = 10;
                                }

                                // Bonus: diagonal-adjacent to BOTH opponent stones
                                int32_t multi_disrupt = 0;
                                for (auto op : opp_stones) {
                                    if (std::abs(static_cast<int32_t>(op.row) - nr) == 1 &&
                                        std::abs(static_cast<int32_t>(op.col) - nc) == 1) {
                                        multi_disrupt += 5;
                                    }
                                }

                                int32_t score = 100 - center_dist * 15 + connectivity + multi_disrupt;
                                if (score > best_score) {
                                    best_score = score;
                                    best = p;
                                }
                            }
                        }
                        return best;
                    }
                }
            }
        }
    }

    // Everything else goes through full search pipeline
    return std::nullopt;
}

} // namespace gomoku
