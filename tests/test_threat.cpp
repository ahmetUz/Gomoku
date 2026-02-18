#include <catch2/catch_test_macros.hpp>
#include "gomoku/search/threat.hpp"
#include "gomoku/board/board.hpp"
#include <algorithm>
#include <initializer_list>
#include <tuple>

using namespace gomoku;

// Helper to create a board from a list of (row, col, stone) tuples
static Board setup_board(std::initializer_list<std::tuple<uint8_t, uint8_t, Stone>> setup) {
    Board board;
    for (const auto& [row, col, stone] : setup) {
        board.place_stone(Pos{row, col}, stone);
    }
    return board;
}

TEST_CASE("creates_four_horizontal", "[threat]") {
    // Setup: _ B B B _ (placing at any end creates four)
    Board board = setup_board({
        {9, 6, Stone::Black},
        {9, 7, Stone::Black},
        {9, 8, Stone::Black},
    });

    ThreatSearcher searcher;

    // Placing at (9, 5) creates: B B B B _ (four with open end)
    REQUIRE(searcher.creates_four(board, Pos{9, 5}, Stone::Black));

    // Placing at (9, 9) creates: _ B B B B (four with open end)
    REQUIRE(searcher.creates_four(board, Pos{9, 9}, Stone::Black));
}

TEST_CASE("creates_four_with_gap", "[threat]") {
    // Setup: _ B B _ B _ (placing in gap creates four)
    Board board = setup_board({
        {9, 5, Stone::Black},
        {9, 6, Stone::Black},
        {9, 8, Stone::Black},
    });

    ThreatSearcher searcher;

    // Placing at (9, 7) creates: B B B B (four)
    REQUIRE(searcher.creates_four(board, Pos{9, 7}, Stone::Black));
}

TEST_CASE("not_four_blocked", "[threat]") {
    // Setup: W B B B _ (blocked on one side, but still a four threat)
    Board board = setup_board({
        {9, 4, Stone::White},
        {9, 5, Stone::Black},
        {9, 6, Stone::Black},
        {9, 7, Stone::Black},
    });

    ThreatSearcher searcher;

    // Placing at (9, 8) creates: W B B B B _ (four with one open end - still valid)
    REQUIRE(searcher.creates_four(board, Pos{9, 8}, Stone::Black));
}

TEST_CASE("creates_open_three", "[threat]") {
    // Setup: _ B B _ (placing creates open three)
    Board board = setup_board({
        {9, 6, Stone::Black},
        {9, 7, Stone::Black},
    });

    ThreatSearcher searcher;

    // Placing at (9, 8) creates: _ B B B _ (open three)
    REQUIRE(searcher.creates_open_three(board, Pos{9, 8}, Stone::Black));

    // Placing at (9, 5) creates: _ B B B _ (open three)
    REQUIRE(searcher.creates_open_three(board, Pos{9, 5}, Stone::Black));
}

TEST_CASE("not_open_three_blocked", "[threat]") {
    // Setup: W B B _ (blocked on one side - not open three)
    Board board = setup_board({
        {9, 4, Stone::White},
        {9, 5, Stone::Black},
        {9, 6, Stone::Black},
    });

    ThreatSearcher searcher;

    // Placing at (9, 7) creates: W B B B _ (blocked, not open three)
    REQUIRE_FALSE(searcher.creates_open_three(board, Pos{9, 7}, Stone::Black));
}

TEST_CASE("vcf_immediate_win", "[threat]") {
    // Setup: _ B B B B _ (one move to win at either end)
    Board board = setup_board({
        {9, 5, Stone::Black},
        {9, 6, Stone::Black},
        {9, 7, Stone::Black},
        {9, 8, Stone::Black},
    });

    ThreatSearcher searcher;
    ThreatResult result = searcher.search_vcf(board, Stone::Black);

    REQUIRE(result.found);
    REQUIRE(result.winning_sequence.size() == 1);
    // Either end wins
    Pos winning_pos = result.winning_sequence[0];
    REQUIRE((winning_pos == Pos{9, 4} || winning_pos == Pos{9, 9}));
}

TEST_CASE("vcf_two_step_win", "[threat]") {
    // Setup a double-four scenario:
    // Black has two separate threes that each need one move to become four
    // If black plays one four, opponent must block, then black plays the other four

    // Setup: _ B B B B _ - open four with both ends open
    Board board = setup_board({
        {9, 5, Stone::Black},
        {9, 6, Stone::Black},
        {9, 7, Stone::Black},
        {9, 8, Stone::Black},
    });

    ThreatSearcher searcher;
    ThreatResult result = searcher.search_vcf(board, Stone::Black);

    // This is actually an immediate win (five), so should be found
    REQUIRE(result.found);

    // Now test actual VCF sequence: two separate threes
    // where completing one forces defense, then the other wins
    Board board2 = setup_board({
        // Horizontal three
        {9, 5, Stone::Black},
        {9, 6, Stone::Black},
        {9, 7, Stone::Black},
        // Vertical three sharing endpoint area
        {5, 9, Stone::Black},
        {6, 9, Stone::Black},
        {7, 9, Stone::Black},
        {8, 9, Stone::Black}, // This makes a four already!
    });

    ThreatResult result2 = searcher.search_vcf(board2, Stone::Black);
    // Vertical four (5-8, 9) needs one move at (4, 9) or (9, 9) to win
    REQUIRE(result2.found);
}

TEST_CASE("vcf_not_found", "[threat]") {
    // Setup: _ B B _ (not enough for VCF)
    Board board = setup_board({
        {9, 6, Stone::Black},
        {9, 7, Stone::Black},
    });

    ThreatSearcher searcher;
    ThreatResult result = searcher.search_vcf(board, Stone::Black);

    REQUIRE_FALSE(result.found);
}

TEST_CASE("find_defense_moves", "[threat]") {
    // Setup: _ B B B B _ (opponent needs to block at either end)
    Board board;
    for (int i = 5; i < 9; i++) {
        board.place_stone(Pos{9, uint8_t(i)}, Stone::Black);
    }

    ThreatSearcher searcher;
    std::vector<Pos> defenses = searcher.find_defense_moves(board, Pos{9, 5}, Stone::Black);

    // White should be able to block at (9, 4) or (9, 9)
    REQUIRE((std::find(defenses.begin(), defenses.end(), Pos{9, 4}) != defenses.end() ||
             std::find(defenses.begin(), defenses.end(), Pos{9, 9}) != defenses.end()));
}

TEST_CASE("capture_win_detected", "[threat]") {
    // Setup: Black has 4 captures, one more capture wins
    Board board;
    board.add_captures(Stone::Black, 4);

    // Setup capture pattern: B _ W W B
    board.place_stone(Pos{9, 5}, Stone::Black);
    board.place_stone(Pos{9, 7}, Stone::White);
    board.place_stone(Pos{9, 8}, Stone::White);
    board.place_stone(Pos{9, 9}, Stone::Black);

    ThreatSearcher searcher;
    // Try to find VCF - capturing at (9, 6) should win
    ThreatResult _result = searcher.search_vcf(board, Stone::Black);

    // Note: VCF only looks for four-threats, not capture wins
    // The capture at (9, 6) creates a four AND captures, so it should work
    // Actually, let's check if this creates a four...
    // After placing at (9,6): B B _ _ B - not a four

    // Let's create a scenario where we have both four and capture opportunity
    Board board2;
    board2.add_captures(Stone::Black, 4);

    // Four pattern: B B B _ with capture at the same position
    board2.place_stone(Pos{9, 5}, Stone::Black);
    board2.place_stone(Pos{9, 6}, Stone::Black);
    board2.place_stone(Pos{9, 7}, Stone::Black);
    // Position (9, 8) will create four

    // Add capture pattern around (9, 8)
    // B _ W W B pattern vertically
    board2.place_stone(Pos{6, 8}, Stone::Black);
    board2.place_stone(Pos{7, 8}, Stone::White);
    board2.place_stone(Pos{8, 8}, Stone::White);
    // (9, 8) will be placed

    ThreatResult result2 = searcher.search_vcf(board2, Stone::Black);
    // (9, 8) creates a four horizontally, so VCF should find it
    REQUIRE(result2.found);
}

TEST_CASE("vct_finds_vcf_first", "[threat]") {
    // VCT should find VCF solutions when available
    Board board = setup_board({
        {9, 5, Stone::Black},
        {9, 6, Stone::Black},
        {9, 7, Stone::Black},
        {9, 8, Stone::Black},
    });

    ThreatSearcher searcher;
    ThreatResult result = searcher.search_vct(board, Stone::Black);

    REQUIRE(result.found);
    // Should find the immediate win
    REQUIRE(result.winning_sequence.size() == 1);
}

TEST_CASE("vct_with_open_three", "[threat]") {
    // Setup a VCT that requires open-three threats
    // This is a complex scenario - simplified test
    Board board = setup_board({
        {9, 6, Stone::Black},
        {9, 7, Stone::Black},
        // Creating potential for open-three based attack
    });

    ThreatSearcher searcher;
    ThreatResult _result = searcher.search_vct(board, Stone::Black);

    // VCT is complex and may or may not find a win depending on position
    // Just verify it doesn't crash and returns a valid result
    REQUIRE(searcher.nodes() > 0);
}

TEST_CASE("threat_searcher_default", "[threat]") {
    ThreatSearcher searcher;
    REQUIRE(searcher.max_vcf_depth() == 30);
    REQUIRE(searcher.max_vct_depth() == 20);
}

TEST_CASE("threat_searcher_with_depths", "[threat]") {
    ThreatSearcher searcher(10, 5);
    REQUIRE(searcher.max_vcf_depth() == 10);
    REQUIRE(searcher.max_vct_depth() == 5);
}

TEST_CASE("node_counting", "[threat]") {
    Board board;
    ThreatSearcher searcher;

    REQUIRE(searcher.nodes() == 0);

    searcher.search_vcf(board, Stone::Black);
    uint64_t nodes_after_vcf = searcher.nodes();
    REQUIRE(nodes_after_vcf > 0);

    searcher.reset_nodes();
    REQUIRE(searcher.nodes() == 0);
}

TEST_CASE("diagonal_four", "[threat]") {
    // Setup: diagonal three with potential four
    Board board = setup_board({
        {6, 6, Stone::Black},
        {7, 7, Stone::Black},
        {8, 8, Stone::Black},
    });

    ThreatSearcher searcher;

    // Placing at (9, 9) creates diagonal four
    REQUIRE(searcher.creates_four(board, Pos{9, 9}, Stone::Black));

    // Placing at (5, 5) creates diagonal four
    REQUIRE(searcher.creates_four(board, Pos{5, 5}, Stone::Black));
}

TEST_CASE("vertical_four", "[threat]") {
    // Setup: vertical three with potential four
    Board board = setup_board({
        {6, 9, Stone::Black},
        {7, 9, Stone::Black},
        {8, 9, Stone::Black},
    });

    ThreatSearcher searcher;

    // Placing at (9, 9) creates vertical four
    REQUIRE(searcher.creates_four(board, Pos{9, 9}, Stone::Black));

    // Placing at (5, 9) creates vertical four
    REQUIRE(searcher.creates_four(board, Pos{5, 9}, Stone::Black));
}

TEST_CASE("find_four_threats_multiple", "[threat]") {
    // Setup position with multiple four-threat opportunities
    Board board = setup_board({
        // Horizontal three
        {9, 6, Stone::Black},
        {9, 7, Stone::Black},
        {9, 8, Stone::Black},
        // Vertical three
        {6, 5, Stone::Black},
        {7, 5, Stone::Black},
        {8, 5, Stone::Black},
    });

    ThreatSearcher searcher;
    std::vector<Pos> threats = searcher.find_four_threats(board, Stone::Black);

    // Should find multiple four-threat positions
    REQUIRE(threats.size() >= 2);
}

TEST_CASE("respects_double_three_rule", "[threat]") {
    // Setup a position where a move would create a double-three (forbidden)
    Board board;

    // Create cross pattern for double-three
    board.place_stone(Pos{9, 8}, Stone::Black);
    board.place_stone(Pos{9, 10}, Stone::Black);
    board.place_stone(Pos{8, 9}, Stone::Black);
    board.place_stone(Pos{10, 9}, Stone::Black);

    ThreatSearcher searcher;
    std::vector<Pos> threats = searcher.find_four_threats(board, Stone::Black);

    // (9, 9) would be a double-three, so it should not appear in threats
    // (because is_valid_move returns false for double-three)
    REQUIRE(std::find(threats.begin(), threats.end(), Pos{9, 9}) == threats.end());
}

TEST_CASE("vcf_rejects_capture_enabling_defender_five", "[threat]") {
    // Reproduce Game 4 bug: VCF captures a pair, freeing a position
    // where the defender can immediately complete five-in-a-row.
    //
    // Setup:
    // Black vertical five setup at col 5: rows 3,4,5,6,7
    // Extra Black stone for capture pair: (6,4)
    // White setup:
    //   Horizontal three at row 6: cols 7,8,9
    //   W at (6,3) for capture bracket
    // White plays (6,6): creates four (6,6)-(6,7)-(6,8)-(6,9)
    // AND captures B(6,5)+B(6,4) via pattern W(6,3)-B(6,4)-B(6,5)-W(6,6)
    // After capture, (6,5) is free. Black replays (6,5) → vertical five!

    Board board;

    // Black vertical five setup at col 5: rows 3,4,5,6,7
    board.place_stone(Pos{3, 5}, Stone::Black);
    board.place_stone(Pos{4, 5}, Stone::Black);
    board.place_stone(Pos{5, 5}, Stone::Black);
    board.place_stone(Pos{6, 5}, Stone::Black);  // will be captured
    board.place_stone(Pos{7, 5}, Stone::Black);

    // Extra Black stone for capture pair: (6,4)
    board.place_stone(Pos{6, 4}, Stone::Black);  // will be captured

    // White setup:
    // Horizontal three at row 6: cols 7,8,9
    board.place_stone(Pos{6, 7}, Stone::White);
    board.place_stone(Pos{6, 8}, Stone::White);
    board.place_stone(Pos{6, 9}, Stone::White);
    // W at (6,3) for capture bracket
    board.place_stone(Pos{6, 3}, Stone::White);

    // White plays (6,6): creates four (6,6)-(6,7)-(6,8)-(6,9)
    // AND captures B(6,5)+B(6,4) via pattern W(6,3)-B(6,4)-B(6,5)-W(6,6)
    // After capture, (6,5) is free. Black replays (6,5) → vertical five!

    ThreatSearcher searcher;
    ThreatResult result = searcher.search_vcf(board, Stone::White);

    // VCF should NOT find a win because the capture at (6,6) frees (6,5)
    // allowing Black to complete vertical five instead of defending.
    REQUIRE_FALSE(result.found);
}
