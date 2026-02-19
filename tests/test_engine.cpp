#include <catch2/catch_test_macros.hpp>
#include "gomoku/engine/engine.hpp"
#include "gomoku/search/tt.hpp"
#include "gomoku/rules/win.hpp"
#include "gomoku/rules/capture.hpp"
#include "gomoku/rules/forbidden.hpp"
#include "gomoku/search/alphabeta.hpp"

using namespace gomoku;

TEST_CASE("test_engine_creation", "[engine]") {
    AIEngine engine;
    REQUIRE(engine.max_depth() == 20);
}

TEST_CASE("test_engine_with_config", "[engine]") {
    AIEngine engine(16, 8, 100);
    REQUIRE(engine.max_depth() == 8);
}

TEST_CASE("test_engine_finds_immediate_win", "[engine]") {
    Board board;
    // 4 in a row - one away from win
    for (uint8_t i = 0; i < 4; ++i) {
        board.place_stone(Pos{9, i}, Stone::Black);
    }

    AIEngine engine;
    MoveResult result = engine.get_move_with_stats(board, Stone::Black);

    REQUIRE(!result.best_move.is_sentinel());
    REQUIRE(result.best_move == Pos{9, 4});
    REQUIRE(result.search_type == SearchType::ImmediateWin);
}

TEST_CASE("test_engine_blocks_opponent_win", "[engine]") {
    Board board;
    // White has 4 in a row
    for (uint8_t i = 0; i < 4; ++i) {
        board.place_stone(Pos{9, i}, Stone::White);
    }
    board.place_stone(Pos{10, 5}, Stone::Black); // Some black stone

    AIEngine engine;
    MoveResult result = engine.get_move_with_stats(board, Stone::Black);

    // Should block at (9,4) - alpha-beta detects opponent's winning threat
    REQUIRE(!result.best_move.is_sentinel());
    REQUIRE(result.best_move == Pos{9, 4});
}

TEST_CASE("test_engine_empty_board", "[engine]") {
    Board board;
    // Use smaller depth for faster test
    AIEngine engine(8, 4, 500);
    auto result = engine.get_move(board, Stone::Black);

    // Should play center
    REQUIRE(result == Pos{9, 9});
}

TEST_CASE("test_opening_book_disrupts_diagonal", "[engine]") {
    // Reproduce the losing game pattern: K10(B), J9(W), K8(B)
    // AI (White) should NOT play K9 (blocks column but ignores diagonal)
    // Should play L9 or similar to disrupt K8's diagonal expansion
    Board board;
    board.place_stone(Pos{9, 9}, Stone::Black);  // K10
    board.place_stone(Pos{8, 8}, Stone::White);   // J9
    board.place_stone(Pos{7, 9}, Stone::Black);   // K8

    AIEngine engine;
    auto result = engine.get_opening_move(board, Stone::White);

    // L9 (8,10) disrupts K8's diagonal toward M10 and connects to J9 via row
    REQUIRE(result == Pos{8, 10}); // Expected L9 to disrupt diagonal

    // Also verify through the full pipeline
    AIEngine engine2;
    MoveResult move_result = engine2.get_move_with_stats(board, Stone::White);
    REQUIRE(!move_result.best_move.is_sentinel());
    REQUIRE(move_result.best_move == Pos{8, 10});
}

TEST_CASE("test_opening_book_skips_diagonal_pair", "[engine]") {
    // K10(B) + L9(B) = diagonal pair → book should NOT apply
    // Alpha-beta should handle this instead of a rigid book response
    Board board;
    board.place_stone(Pos{9, 9}, Stone::Black);   // K10
    board.place_stone(Pos{8, 8}, Stone::White);    // J9
    board.place_stone(Pos{8, 10}, Stone::Black);   // L9

    AIEngine engine;
    auto result = engine.get_opening_move(board, Stone::White);

    // Diagonal pair → no book move, fall through to search
    REQUIRE(!result.has_value()); // Diagonal pair should not trigger opening book
}

TEST_CASE("test_engine_vcf_detection", "[engine]") {
    Board board;
    // Set up position with 4 in a row (immediate win, not just VCF)
    board.place_stone(Pos{9, 5}, Stone::Black);
    board.place_stone(Pos{9, 6}, Stone::Black);
    board.place_stone(Pos{9, 7}, Stone::Black);
    board.place_stone(Pos{9, 8}, Stone::Black);

    AIEngine engine;
    MoveResult result = engine.get_move_with_stats(board, Stone::Black);

    // Should find immediate win
    REQUIRE(!result.best_move.is_sentinel());
    REQUIRE(result.search_type == SearchType::ImmediateWin);
}

TEST_CASE("test_engine_time_reasonable", "[engine]") {
    Board board;
    // Create a position where there's already some activity
    board.place_stone(Pos{9, 9}, Stone::Black);
    board.place_stone(Pos{10, 10}, Stone::White);
    board.place_stone(Pos{9, 10}, Stone::Black);
    board.place_stone(Pos{8, 9}, Stone::White);

    // Use depth 2 for speed test
    AIEngine engine(8, 2, 100);
    MoveResult result = engine.get_move_with_stats(board, Stone::Black);

    // Allow generous time (60 seconds)
    uint64_t max_time_ms = 60000;

    REQUIRE(result.time_ms < max_time_ms);
}

TEST_CASE("test_capture_win_detection", "[engine]") {
    Board board;
    // Set up near capture win scenario
    board.black_captures = 4; // 4 pairs = 8 stones

    // Place a capturable pair - this creates an immediate win via capture
    // B-W-W-? pattern at row 9, Black plays at col 11 to capture
    board.place_stone(Pos{9, 8}, Stone::Black);
    board.place_stone(Pos{9, 9}, Stone::White);
    board.place_stone(Pos{9, 10}, Stone::White);
    // Add scattered stones away from capture to exceed threshold
    board.place_stone(Pos{3, 3}, Stone::Black);
    board.place_stone(Pos{3, 15}, Stone::White);
    board.place_stone(Pos{15, 3}, Stone::Black);
    board.place_stone(Pos{15, 15}, Stone::White);
    board.place_stone(Pos{5, 5}, Stone::Black);
    board.place_stone(Pos{5, 13}, Stone::White);

    AIEngine engine;
    auto result = engine.get_move(board, Stone::Black);

    // Should find capture at (9,11) for the win
    REQUIRE(result == Pos{9, 11});
}

TEST_CASE("test_engine_clear_cache", "[engine]") {
    AIEngine engine(8, 4, 500);

    // Verify clear_cache works by checking stats reset
    // First, manually trigger some TT usage through internal searcher
    Board board;
    // Create a mid-game position with scattered stones to force alpha-beta
    // Position has no immediate threats but requires search
    for (uint8_t i = 0; i < 5; ++i) {
        board.place_stone(Pos{static_cast<uint8_t>(4 + i), 4}, Stone::Black);
        board.place_stone(Pos{static_cast<uint8_t>(4 + i), 14}, Stone::White);
    }
    // This should trigger alpha-beta search (>8 stones, no immediate win)
    engine.get_move(board, Stone::Black);

    // Clear cache
    engine.clear_cache();
    TTStats stats_after = engine.tt_stats();
    REQUIRE(stats_after.used == 0); // TT should be empty after clear
}

TEST_CASE("test_engine_set_depth", "[engine]") {
    AIEngine engine;
    REQUIRE(engine.max_depth() == 20);

    engine.set_max_depth(12);
    REQUIRE(engine.max_depth() == 12);
}

TEST_CASE("test_engine_set_time_limit", "[engine]") {
    AIEngine engine;
    engine.set_time_limit(1000);
    // Time limit is stored but not actively used yet
    // This test just ensures no panic
}

TEST_CASE("test_engine_default", "[engine]") {
    AIEngine engine;
    REQUIRE(engine.max_depth() == 20);
}

TEST_CASE("test_move_result_types", "[engine]") {
    Pos pos{9, 9};

    MoveResult win = MoveResult::immediate_win(pos, 10);
    REQUIRE(win.search_type == SearchType::ImmediateWin);
    REQUIRE(win.score == 1000000);

    MoveResult vcf = MoveResult::vcf_win(pos, 20, 100);
    REQUIRE(vcf.search_type == SearchType::VCF);
    REQUIRE(vcf.score == 900000);

    MoveResult vct = MoveResult::vct_win(pos, 30, 200);
    REQUIRE(vct.search_type == SearchType::VCT);
    REQUIRE(vct.score == 800000);

    MoveResult defense = MoveResult::defense(pos, -100000, 40, 50);
    REQUIRE(defense.search_type == SearchType::Defense);

    MoveResult no_move = MoveResult::no_move(50);
    REQUIRE(no_move.best_move.is_sentinel());
}

TEST_CASE("test_engine_responds_to_threat", "[engine]") {
    Board board;
    // White has 4 in a row - Black must block
    board.place_stone(Pos{9, 6}, Stone::White);
    board.place_stone(Pos{9, 7}, Stone::White);
    board.place_stone(Pos{9, 8}, Stone::White);
    board.place_stone(Pos{9, 9}, Stone::White);
    // Black has a stone nearby
    board.place_stone(Pos{8, 8}, Stone::Black);

    AIEngine engine(8, 4, 500);
    MoveResult result = engine.get_move_with_stats(board, Stone::Black);

    // Should find blocking move
    REQUIRE(!result.best_move.is_sentinel());
    // Should block at one of the ends
    Pos m = result.best_move;
    bool blocked = (m == Pos{9, 5} || m == Pos{9, 10});
    REQUIRE(blocked);
}

TEST_CASE("test_engine_multiple_searches", "[engine]") {
    // Use smaller depth for faster test
    AIEngine engine(8, 4, 500);
    Board board;

    // Multiple searches should work correctly
    auto result1 = engine.get_move(board, Stone::Black);
    auto result2 = engine.get_move(board, Stone::Black);

    // Results should be consistent
    REQUIRE(result1 == result2);
}

TEST_CASE("test_engine_alternating_colors", "[engine]") {
    // Use smaller depth for faster test
    AIEngine engine(8, 4, 500);
    Board board;

    // Simulate a few moves
    auto black_move = engine.get_move(board, Stone::Black);
    REQUIRE(black_move.has_value());
    board.place_stone(*black_move, Stone::Black);

    auto white_move = engine.get_move(board, Stone::White);
    REQUIRE(white_move.has_value());
    board.place_stone(*white_move, Stone::White);

    // Continue playing
    auto black_move2 = engine.get_move(board, Stone::Black);
    REQUIRE(black_move2.has_value());
}

TEST_CASE("test_search_type_equality", "[engine]") {
    REQUIRE(SearchType::ImmediateWin == SearchType::ImmediateWin);
    REQUIRE(SearchType::ImmediateWin != SearchType::VCF);
    REQUIRE(SearchType::VCF != SearchType::VCT);
    REQUIRE(SearchType::Defense != SearchType::AlphaBeta);
}

TEST_CASE("test_engine_blocks_gap_pattern", "[engine]") {
    // Test gap pattern: OO_OO where filling the gap completes 5
    // This is critical - AI must block at the gap position
    //
    // Pattern on column 12 (M column):
    // M14 = (5, 12) - Black
    // M13 = (6, 12) - Black
    // M12 = (7, 12) - EMPTY (gap)
    // M11 = (8, 12) - Black
    // M10 = (9, 12) - Black
    Board board;
    board.place_stone(Pos{5, 12}, Stone::Black);  // M14
    board.place_stone(Pos{6, 12}, Stone::Black);  // M13
    // Gap at (7, 12) - M12
    board.place_stone(Pos{8, 12}, Stone::Black);  // M11
    board.place_stone(Pos{9, 12}, Stone::Black);  // M10

    // Add some White stones
    board.place_stone(Pos{9, 9}, Stone::White);
    board.place_stone(Pos{10, 10}, Stone::White);

    AIEngine engine(8, 6, 500);
    MoveResult result = engine.get_move_with_stats(board, Stone::White);

    // White MUST block at M12 (7, 12) - the gap position
    REQUIRE(!result.best_move.is_sentinel());
    Pos block_pos = result.best_move;
    REQUIRE(block_pos == Pos{7, 12});
}

TEST_CASE("test_engine_blocks_horizontal_gap", "[engine]") {
    // Test horizontal gap pattern: OO_OO
    Board board;
    board.place_stone(Pos{9, 5}, Stone::Black);
    board.place_stone(Pos{9, 6}, Stone::Black);
    // Gap at (9, 7)
    board.place_stone(Pos{9, 8}, Stone::Black);
    board.place_stone(Pos{9, 9}, Stone::Black);

    board.place_stone(Pos{5, 5}, Stone::White);

    AIEngine engine(8, 6, 500);
    MoveResult result = engine.get_move_with_stats(board, Stone::White);

    REQUIRE(!result.best_move.is_sentinel());
    Pos block_pos = result.best_move;
    REQUIRE(block_pos == Pos{9, 7});
}

TEST_CASE("test_search_depth_benchmark", "[engine]") {
    // Mid-game position with ~10 stones to measure realistic search depth
    Board board;
    const std::pair<uint8_t, uint8_t> moves[] = {
        {9, 9}, {9, 10}, {10, 9}, {8, 10}, {10, 10},
        {8, 8}, {11, 8}, {7, 11}, {10, 8}, {8, 9}
    };
    for (size_t i = 0; i < 10; ++i) {
        Stone s = (i % 2 == 0) ? Stone::Black : Stone::White;
        board.place_stone(Pos{moves[i].first, moves[i].second}, s);
    }

    AIEngine engine; // Default: depth 20, 500ms
    MoveResult result = engine.get_move_with_stats(board, Stone::Black);

    // Verify AI searches at sufficient depth.
    // If the AI found a forced win/threat (VCF/VCT/immediate), early exit is correct.
    // Otherwise, depth 10+ is the project requirement.
    bool found_forced_result = std::abs(result.score) >= 799900 ||
        result.search_type == SearchType::VCF ||
        result.search_type == SearchType::ImmediateWin;
    REQUIRE((result.depth >= 10 || found_forced_result));
}

TEST_CASE("test_mid_game_search_quality", "[engine]") {
    // Mid-game position with some development from both sides.
    Board board;
    const std::pair<uint8_t, uint8_t> moves[] = {
        {9, 9}, {7, 7}, {9, 11}, {7, 11}, {11, 9},
        {11, 11}, {5, 9}, {5, 7}, {9, 5}, {11, 7}
    };
    for (size_t i = 0; i < 10; ++i) {
        Stone s = (i % 2 == 0) ? Stone::Black : Stone::White;
        board.place_stone(Pos{moves[i].first, moves[i].second}, s);
    }

    AIEngine engine;
    MoveResult result = engine.get_move_with_stats(board, Stone::Black);

    // Should find a reasonable move - via alpha-beta depth 8+ or VCF/VCT forced win
    REQUIRE(!result.best_move.is_sentinel());
    bool found_forced = result.search_type == SearchType::VCF ||
                       result.search_type == SearchType::ImmediateWin;
    REQUIRE((result.depth >= 8 || found_forced));
    // Time should be under hard limit
    REQUIRE(result.time_ms < 2000);
}

TEST_CASE("test_engine_breaks_existing_five", "[engine]") {
    Board board;
    // Reconstruct the exact board state from game log at move #26
    // Notation: col letters skip I (A=0..H=7, J=8, K=9, L=10, M=11, N=12)
    // Row numbers: 1=row0, n=row(n-1)

    // Black stones (13 total):
    board.place_stone(Pos{7, 9}, Stone::Black);   // K8
    board.place_stone(Pos{6, 8}, Stone::Black);   // J7
    board.place_stone(Pos{8, 10}, Stone::Black);  // L9
    board.place_stone(Pos{9, 9}, Stone::Black);   // K10
    board.place_stone(Pos{10, 8}, Stone::Black);  // J11
    board.place_stone(Pos{8, 7}, Stone::Black);   // H9  (part of five)
    board.place_stone(Pos{11, 7}, Stone::Black);  // H12
    board.place_stone(Pos{9, 7}, Stone::Black);   // H10
    board.place_stone(Pos{9, 8}, Stone::Black);   // J10 (part of five)
    board.place_stone(Pos{9, 6}, Stone::Black);   // G10
    board.place_stone(Pos{10, 9}, Stone::Black);  // K11 (part of five)
    board.place_stone(Pos{11, 10}, Stone::Black); // L12 (part of five)
    board.place_stone(Pos{12, 11}, Stone::Black); // M13 (part of five)
    // Black captures: 1 pair
    board.add_captures(Stone::Black, 1);

    // White stones (10 total):
    board.place_stone(Pos{5, 7}, Stone::White);   // H6
    board.place_stone(Pos{10, 12}, Stone::White);  // N11
    board.place_stone(Pos{7, 11}, Stone::White);  // M8
    board.place_stone(Pos{8, 9}, Stone::White);   // K9 (replayed)
    board.place_stone(Pos{12, 6}, Stone::White);  // G13
    board.place_stone(Pos{10, 7}, Stone::White);  // H11
    board.place_stone(Pos{9, 10}, Stone::White);  // L10
    board.place_stone(Pos{9, 5}, Stone::White);   // F10
    board.place_stone(Pos{7, 8}, Stone::White);   // J8
    board.place_stone(Pos{7, 6}, Stone::White);   // G8

    // Verify Black has a five
    auto five = find_five_positions(board, Stone::Black);
    REQUIRE(five.has_value());
    std::vector<Pos> five_positions = *five;
    REQUIRE(five_positions.size() >= 5);

    // Verify the five is breakable
    REQUIRE(can_break_five_by_capture(board, five_positions, Stone::Black));

    // AI (White) MUST break the five
    AIEngine engine;
    MoveResult result = engine.get_move_with_stats(board, Stone::White);

    REQUIRE(!result.best_move.is_sentinel());
    Pos ai_move = result.best_move;

    // Verify the AI's move actually breaks the five:
    // Place the stone and check if a capture removes part of the five
    Board test = board;
    test.place_stone(ai_move, Stone::White);
    std::vector<Pos> caps = get_captured_positions(test, ai_move, Stone::White);
    bool breaks_five = false;
    for (const Pos& cap : caps) {
        for (const Pos& fpos : five_positions) {
            if (cap == fpos) {
                breaks_five = true;
                break;
            }
        }
        if (breaks_five) break;
    }
    REQUIRE(breaks_five);

    // Should be classified as defense or alpha-beta
    bool correct_type = result.search_type == SearchType::Defense ||
                       result.search_type == SearchType::AlphaBeta;
    REQUIRE(correct_type);
}

TEST_CASE("test_depth_collapse_regression", "[engine]") {
    Board board;
    // Game 1 position from gomoku_ai.log
    // Notation: K10 = col K (10), row 10 (9 in 0-indexed)
    const std::pair<uint8_t, uint8_t> moves[] = {
        {9, 10}, {7, 10}, {10, 9}, {7, 12}, {9, 11}, {5, 14}, {9, 9}
    };
    for (size_t i = 0; i < 7; ++i) {
        Stone s = (i % 2 == 0) ? Stone::Black : Stone::White;
        board.place_stone(Pos{moves[i].first, moves[i].second}, s);
    }

    AIEngine engine;
    MoveResult result = engine.get_move_with_stats(board, Stone::White);

    REQUIRE(!result.best_move.is_sentinel());
    bool found_forced = std::abs(result.score) >= 799900 ||
        result.search_type == SearchType::VCF ||
        result.search_type == SearchType::ImmediateWin;
    REQUIRE((result.depth >= 8 || found_forced));
}

TEST_CASE("test_game5_open_four_detection", "[engine]") {
    Board board;

    // Black stones (7): K10, L10, M10, N10, M12, M9, G9
    board.place_stone(Pos{9, 9}, Stone::Black); // K10
    board.place_stone(Pos{9, 10}, Stone::Black); // L10
    board.place_stone(Pos{9, 11}, Stone::Black); // M10
    board.place_stone(Pos{9, 12}, Stone::Black); // N10
    board.place_stone(Pos{11, 11}, Stone::Black); // M12
    board.place_stone(Pos{8, 11}, Stone::Black); // M9
    board.place_stone(Pos{8, 6}, Stone::Black); // G9

    // White stones (6): J9, L9, K9, M11, H9, N8
    board.place_stone(Pos{8, 8}, Stone::White); // J9
    board.place_stone(Pos{8, 10}, Stone::White); // L9
    board.place_stone(Pos{8, 9}, Stone::White); // K9
    board.place_stone(Pos{10, 11}, Stone::White); // M11
    board.place_stone(Pos{8, 7}, Stone::White); // H9
    board.place_stone(Pos{7, 12}, Stone::White); // N8

    // Verify board state
    REQUIRE(board.get(Pos{9, 9}) == Stone::Black);
    REQUIRE(board.get(Pos{9, 10}) == Stone::Black);
    REQUIRE(board.get(Pos{9, 11}) == Stone::Black);
    REQUIRE(board.get(Pos{9, 12}) == Stone::Black);
    REQUIRE(board.is_empty(Pos{9, 8}));
    REQUIRE(board.is_empty(Pos{9, 13}));

    // Test sub-functions individually
    Pos j10{9, 8};
    Pos o10{9, 13};

    // 1. is_valid_move should allow both
    REQUIRE(is_valid_move(board, j10, Stone::Black));
    REQUIRE(is_valid_move(board, o10, Stone::Black));

    // 2. has_five_at_pos should detect five after placing
    Board test_board = board;
    test_board.place_stone(j10, Stone::Black);
    REQUIRE(has_five_at_pos(test_board, j10, Stone::Black));
    test_board.remove_stone(j10);

    test_board.place_stone(o10, Stone::Black);
    REQUIRE(has_five_at_pos(test_board, o10, Stone::Black));
    test_board.remove_stone(o10);

    // 3. find_winning_moves: the fives ARE breakable (M9 at 8,11 allows capture of M10)
    // This is CORRECT behavior — not a bug in find_winning_moves
    AIEngine engine;
    std::vector<Pos> threats = engine.find_winning_moves(board, Stone::Black);
    // Both fives are breakable via M11-M10-M9 capture, so 0 threats is correct
    REQUIRE(threats.size() == 0);

    // 4. After K11 captures L10+M9, Black replays L10 → unbreakable open four
    Board post_capture = board;
    // Simulate K11 capture
    post_capture.place_stone(Pos{10, 9}, Stone::White); // K11
    post_capture.remove_stone(Pos{9, 10}); // capture L10
    post_capture.remove_stone(Pos{8, 11}); // capture M9
    post_capture.add_captures(Stone::White, 1);

    // Black replays L10
    post_capture.place_stone(Pos{9, 10}, Stone::Black); // L10 replay

    // Now M9 is gone — fives should be UNBREAKABLE
    std::vector<Pos> threats_after = engine.find_winning_moves(post_capture, Stone::Black);
    REQUIRE(threats_after.size() >= 2);
}

TEST_CASE("test_game5_k11_is_good_capture", "[engine]") {
    Board board;

    // Black stones (7): K10, L10, M10, N10, M12, M9, G9
    board.place_stone(Pos{9, 9}, Stone::Black); // K10
    board.place_stone(Pos{9, 10}, Stone::Black); // L10
    board.place_stone(Pos{9, 11}, Stone::Black); // M10
    board.place_stone(Pos{9, 12}, Stone::Black); // N10
    board.place_stone(Pos{11, 11}, Stone::Black); // M12
    board.place_stone(Pos{8, 11}, Stone::Black); // M9
    board.place_stone(Pos{8, 6}, Stone::Black); // G9

    // White stones (6): J9, L9, K9, M11, H9, N8
    board.place_stone(Pos{8, 8}, Stone::White); // J9
    board.place_stone(Pos{8, 10}, Stone::White); // L9
    board.place_stone(Pos{8, 9}, Stone::White); // K9
    board.place_stone(Pos{10, 11}, Stone::White); // M11
    board.place_stone(Pos{8, 7}, Stone::White); // H9
    board.place_stone(Pos{7, 12}, Stone::White); // N8

    AIEngine engine(20, 10, 2000);
    MoveResult result = engine.get_move_with_stats(board, Stone::White);

    // K11 captures L10+M9, setting up M9 five threat.
    // AI should evaluate this position positively.
    REQUIRE(result.score > 0);
}

TEST_CASE("test_game5_move16_white_immediate_win", "[engine]") {
    Board board;

    // Move 16 board state (after K11 captures L10+M9, then Black replays L10)
    // Black (6): K10, L10, M10, N10, M12, G9
    board.place_stone(Pos{9, 9}, Stone::Black); // K10
    board.place_stone(Pos{9, 10}, Stone::Black); // L10
    board.place_stone(Pos{9, 11}, Stone::Black); // M10
    board.place_stone(Pos{9, 12}, Stone::Black); // N10
    board.place_stone(Pos{11, 11}, Stone::Black); // M12
    board.place_stone(Pos{8, 6}, Stone::Black); // G9
    // White (7): J9, L9, K9, M11, H9, N8, K11
    board.place_stone(Pos{8, 8}, Stone::White); // J9
    board.place_stone(Pos{8, 10}, Stone::White); // L9
    board.place_stone(Pos{8, 9}, Stone::White); // K9
    board.place_stone(Pos{10, 11}, Stone::White); // M11
    board.place_stone(Pos{8, 7}, Stone::White); // H9
    board.place_stone(Pos{7, 12}, Stone::White); // N8
    board.place_stone(Pos{10, 9}, Stone::White); // K11
    board.add_captures(Stone::White, 1);

    // Verify White's row 8: H9-J9-K9-L9 = 4 consecutive, M9 empty
    REQUIRE(board.get(Pos{8, 7}) == Stone::White); // H9
    REQUIRE(board.get(Pos{8, 8}) == Stone::White); // J9
    REQUIRE(board.get(Pos{8, 9}) == Stone::White); // K9
    REQUIRE(board.get(Pos{8, 10}) == Stone::White); // L9
    REQUIRE(board.is_empty(Pos{8, 11})); // M9 should be empty

    // Verify M9 creates a five
    Pos m9{8, 11};
    Board test = board;
    test.place_stone(m9, Stone::White);
    REQUIRE(has_five_at_pos(test, m9, Stone::White));

    // STATIC check: the five IS breakable (O7 captures N8+M9)
    auto five = find_five_positions(test, Stone::White);
    REQUIRE(five.has_value());
    REQUIRE(can_break_five_by_capture(test, *five, Stone::White));

    // But the break is illusory: after O7 captures, White replays M9 → unbreakable
    REQUIRE(AIEngine::is_illusory_break(test, *five, Stone::White));

    // Full engine pipeline should find M9 as immediate win (illusory break)
    AIEngine engine;
    MoveResult move_result = engine.get_move_with_stats(board, Stone::White);
    REQUIRE(!move_result.best_move.is_sentinel());
    REQUIRE(move_result.best_move == m9);
    REQUIRE(move_result.search_type == SearchType::ImmediateWin);
}

TEST_CASE("test_game5_post_capture_search", "[engine]") {
    Board board;

    // Board after K11 capture (L10+M9 removed), Black's turn
    // Black (5): K10, M10, N10, M12, G9
    board.place_stone(Pos{9, 9}, Stone::Black); // K10
    board.place_stone(Pos{9, 11}, Stone::Black); // M10
    board.place_stone(Pos{9, 12}, Stone::Black); // N10
    board.place_stone(Pos{11, 11}, Stone::Black); // M12
    board.place_stone(Pos{8, 6}, Stone::Black); // G9
    // White (7): J9, L9, K9, M11, H9, N8, K11
    board.place_stone(Pos{8, 8}, Stone::White); // J9
    board.place_stone(Pos{8, 10}, Stone::White); // L9
    board.place_stone(Pos{8, 9}, Stone::White); // K9
    board.place_stone(Pos{10, 11}, Stone::White); // M11
    board.place_stone(Pos{8, 7}, Stone::White); // H9
    board.place_stone(Pos{7, 12}, Stone::White); // N8
    board.place_stone(Pos{10, 9}, Stone::White); // K11
    board.add_captures(Stone::White, 1);

    // Run search for Black
    AIEngine engine(20, 10, 2000);
    MoveResult result = engine.get_move_with_stats(board, Stone::Black);

    // Just verify search completes without crashing
    REQUIRE(!result.best_move.is_sentinel());

    // Run search for White too (to compare)
    AIEngine engine2(20, 10, 2000);
    MoveResult result_w = engine2.get_move_with_stats(board, Stone::White);
    REQUIRE(!result_w.best_move.is_sentinel());
}

TEST_CASE("test_game5_k11_l10_white_perspective", "[engine]") {
    Board board;

    // Board after K11 capture + L10 replay
    // Black (6): K10, L10, M10, N10, M12, G9
    board.place_stone(Pos{9, 9}, Stone::Black); // K10
    board.place_stone(Pos{9, 10}, Stone::Black); // L10
    board.place_stone(Pos{9, 11}, Stone::Black); // M10
    board.place_stone(Pos{9, 12}, Stone::Black); // N10
    board.place_stone(Pos{11, 11}, Stone::Black); // M12
    board.place_stone(Pos{8, 6}, Stone::Black); // G9
    // White (7): J9, L9, K9, M11, H9, N8, K11
    board.place_stone(Pos{8, 8}, Stone::White); // J9
    board.place_stone(Pos{8, 10}, Stone::White); // L9
    board.place_stone(Pos{8, 9}, Stone::White); // K9
    board.place_stone(Pos{10, 11}, Stone::White); // M11
    board.place_stone(Pos{8, 7}, Stone::White); // H9
    board.place_stone(Pos{7, 12}, Stone::White); // N8
    board.place_stone(Pos{10, 9}, Stone::White); // K11
    board.add_captures(Stone::White, 1);

    // Alpha-beta should find M9 as a winning move
    Searcher searcher(16);
    SearchResult result = searcher.search(board, Stone::White, 8);

    Pos m9{8, 11};
    REQUIRE(result.best_move == m9);
    REQUIRE(result.score > 900000);
}
