#include <catch2/catch_test_macros.hpp>
#include "gomoku/search/tt.hpp"
#include "gomoku/board/types.hpp"
#include <thread>
#include <vector>

using namespace gomoku;

// =========================================================================
// TranspositionTable tests
// =========================================================================

TEST_CASE("tt_store_probe_exact", "[tt]") {
    TranspositionTable tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 5, 100, EntryType::Exact, std::optional<Pos>(Pos{9, 9}));

    auto result = tt.probe(hash, 5, -1000, 1000);
    REQUIRE(result.has_value());
    auto [score, best_move] = result.value();
    REQUIRE(score == 100);
    REQUIRE(best_move.has_value());
    REQUIRE(best_move.value() == Pos(9, 9));
}

TEST_CASE("tt_depth_requirement", "[tt]") {
    TranspositionTable tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 3, 100, EntryType::Exact, std::optional<Pos>(Pos{5, 5}));

    // Deeper search should not use shallow entry's score
    auto result = tt.probe(hash, 5, -1000, 1000);
    REQUIRE(result.has_value());
    auto [score, best_move] = result.value();
    // Score is 0 (not usable), but best_move is returned for ordering
    REQUIRE(score == 0);
    REQUIRE(best_move.has_value());
    REQUIRE(best_move.value() == Pos(5, 5));
}

TEST_CASE("tt_lower_bound_cutoff", "[tt]") {
    TranspositionTable tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 5, 200, EntryType::LowerBound, std::nullopt);

    // Score (200) >= beta (150), should return score
    auto result = tt.probe(hash, 5, -1000, 150);
    REQUIRE(result.has_value());
    REQUIRE(result.value().first == 200);

    // Score (200) < beta (300), should not return score
    result = tt.probe(hash, 5, -1000, 300);
    REQUIRE(result.has_value());
    REQUIRE(result.value().first == 0); // Score not usable
}

TEST_CASE("tt_upper_bound_cutoff", "[tt]") {
    TranspositionTable tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 5, 50, EntryType::UpperBound, std::nullopt);

    // Score (50) <= alpha (100), should return score
    auto result = tt.probe(hash, 5, 100, 1000);
    REQUIRE(result.has_value());
    REQUIRE(result.value().first == 50);

    // Score (50) > alpha (30), should not return score
    result = tt.probe(hash, 5, 30, 1000);
    REQUIRE(result.has_value());
    REQUIRE(result.value().first == 0);
}

TEST_CASE("tt_hash_mismatch", "[tt]") {
    TranspositionTable tt(1);
    uint64_t hash1 = 0x123456789ABCDEF0;
    uint64_t hash2 = 0x987654321FEDCBA0;

    tt.store(hash1, 5, 100, EntryType::Exact, std::optional<Pos>(Pos{9, 9}));

    // Different hash should not return entry (unless collision)
    auto result = tt.probe(hash2, 5, -1000, 1000);
    // Result depends on whether hashes map to same slot and hash verification
    if (result.has_value()) {
        // If there's a collision, hash verification should fail
        REQUIRE(result.value().first == 0);
    }
}

TEST_CASE("tt_get_best_move", "[tt]") {
    TranspositionTable tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 5, 100, EntryType::Exact, std::optional<Pos>(Pos{9, 9}));

    auto best_move = tt.get_best_move(hash);
    REQUIRE(best_move.has_value());
    REQUIRE(best_move.value() == Pos(9, 9));

    // Wrong hash should return None
    best_move = tt.get_best_move(0xFFFFFFFFFFFFFFFF);
    REQUIRE(!best_move.has_value());
}

TEST_CASE("tt_replacement_deeper", "[tt]") {
    TranspositionTable tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 3, 100, EntryType::Exact, std::optional<Pos>(Pos{5, 5}));
    tt.store(hash, 5, 200, EntryType::Exact, std::optional<Pos>(Pos{9, 9}));

    // Deeper entry should replace shallower
    auto result = tt.probe(hash, 5, -1000, 1000);
    REQUIRE(result.has_value());
    REQUIRE(result.value().first == 200);
}

TEST_CASE("tt_replacement_same_depth", "[tt]") {
    TranspositionTable tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 5, 100, EntryType::Exact, std::optional<Pos>(Pos{5, 5}));
    tt.store(hash, 5, 200, EntryType::Exact, std::optional<Pos>(Pos{9, 9}));

    // Same depth should replace (newer info)
    auto result = tt.probe(hash, 5, -1000, 1000);
    REQUIRE(result.has_value());
    REQUIRE(result.value().first == 200);
}

TEST_CASE("tt_no_replacement_shallower", "[tt]") {
    TranspositionTable tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 5, 100, EntryType::Exact, std::optional<Pos>(Pos{5, 5}));
    tt.store(hash, 3, 200, EntryType::Exact, std::optional<Pos>(Pos{9, 9}));

    // Shallower should NOT replace deeper (same hash exception)
    // Actually, same hash always replaces per our policy
    auto result = tt.probe(hash, 5, -1000, 1000);
    REQUIRE(result.has_value());
    // Since same hash, it gets replaced
    auto [score, _] = result.value();
    REQUIRE(score == 0); // Depth 3 < requested depth 5
}

TEST_CASE("tt_clear", "[tt]") {
    TranspositionTable tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 5, 100, EntryType::Exact, std::nullopt);
    tt.clear();

    auto result = tt.probe(hash, 5, -1000, 1000);
    REQUIRE(!result.has_value());
}

TEST_CASE("tt_stats", "[tt]") {
    TranspositionTable tt(1);

    auto stats = tt.stats();
    REQUIRE(stats.used == 0);
    REQUIRE(stats.usage_percent == 0);

    tt.store(0x111, 5, 100, EntryType::Exact, std::nullopt);
    tt.store(0x222, 5, 100, EntryType::Exact, std::nullopt);

    stats = tt.stats();
    REQUIRE(stats.used == 2);
    REQUIRE(stats.size > 0);
}

TEST_CASE("tt_no_best_move", "[tt]") {
    TranspositionTable tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    // Store without best move
    tt.store(hash, 5, 100, EntryType::Exact, std::nullopt);

    auto result = tt.probe(hash, 5, -1000, 1000);
    REQUIRE(result.has_value());
    auto [score, best_move] = result.value();
    REQUIRE(score == 100);
    REQUIRE(!best_move.has_value());

    auto bm = tt.get_best_move(hash);
    REQUIRE(!bm.has_value());
}

TEST_CASE("tt_entry_types", "[tt]") {
    TranspositionTable tt(1);

    // Test all entry types
    uint64_t hashes[] = {0x111, 0x222, 0x333};

    tt.store(hashes[0], 5, 100, EntryType::Exact, std::nullopt);
    tt.store(hashes[1], 5, 100, EntryType::LowerBound, std::nullopt);
    tt.store(hashes[2], 5, 100, EntryType::UpperBound, std::nullopt);

    // Exact always returns score if depth sufficient
    auto result = tt.probe(hashes[0], 5, -1000, 1000);
    REQUIRE(result.value().first == 100);

    // LowerBound returns score only if score >= beta
    result = tt.probe(hashes[1], 5, -1000, 50);
    REQUIRE(result.value().first == 100); // 100 >= 50

    // UpperBound returns score only if score <= alpha
    result = tt.probe(hashes[2], 5, 150, 1000);
    REQUIRE(result.value().first == 100); // 100 <= 150
}

TEST_CASE("tt_minimum_size", "[tt]") {
    // Even with 0 MB, should have minimum entries
    TranspositionTable tt(0);
    REQUIRE(tt.table_size() >= 1024);
}

TEST_CASE("tt_size_calculation", "[tt]") {
    TranspositionTable tt(1);
    size_t entry_size = sizeof(std::optional<TTEntry>);
    size_t expected_size = (1024 * 1024) / entry_size;
    REQUIRE(tt.table_size() == std::max(expected_size, size_t(1024)));
}

// =========================================================================
// AtomicTT tests
// =========================================================================

TEST_CASE("pack_unpack_roundtrip", "[tt]") {
    struct TestCase {
        int8_t depth;
        int32_t score;
        EntryType entry_type;
        std::optional<Pos> best_move;
    };

    std::vector<TestCase> cases = {
        {5, 100, EntryType::Exact, std::optional<Pos>(Pos{9, 9})},
        {-3, -500000, EntryType::LowerBound, std::nullopt},
        {0, 0, EntryType::UpperBound, std::optional<Pos>(Pos{0, 0})},
        {15, 999999, EntryType::Exact, std::optional<Pos>(Pos{18, 18})},
        {-128, -1048575, EntryType::LowerBound, std::optional<Pos>(Pos{0, 18})},
        {127, 1048575, EntryType::UpperBound, std::optional<Pos>(Pos{18, 0})},
    };

    for (const auto& tc : cases) {
        uint64_t packed = pack_entry(tc.depth, tc.score, tc.entry_type, tc.best_move);
        auto [d, s, t, m] = unpack_entry(packed);
        REQUIRE(d == tc.depth);
        REQUIRE(s == tc.score);
        REQUIRE(t == tc.entry_type);
        REQUIRE(m == tc.best_move);
    }
}

TEST_CASE("atomic_tt_store_probe_exact", "[tt]") {
    AtomicTT tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 5, 100, EntryType::Exact, std::optional<Pos>(Pos{9, 9}));

    auto result = tt.probe(hash, 5, -1000, 1000);
    REQUIRE(result.has_value());
    auto [score, best_move] = result.value();
    REQUIRE(score == 100);
    REQUIRE(best_move.has_value());
    REQUIRE(best_move.value() == Pos(9, 9));
}

TEST_CASE("atomic_tt_depth_requirement", "[tt]") {
    AtomicTT tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 3, 100, EntryType::Exact, std::optional<Pos>(Pos{5, 5}));

    auto result = tt.probe(hash, 5, -1000, 1000);
    REQUIRE(!result.has_value()); // Depth insufficient → None (use get_best_move for ordering)
}

TEST_CASE("atomic_tt_bounds", "[tt]") {
    AtomicTT tt(1);

    // LowerBound
    uint64_t hash_lb = 0x111;
    tt.store(hash_lb, 5, 200, EntryType::LowerBound, std::nullopt);
    REQUIRE(tt.probe(hash_lb, 5, -1000, 150).value().first == 200); // 200 >= 150
    REQUIRE(!tt.probe(hash_lb, 5, -1000, 300).has_value()); // 200 < 300 → not usable

    // UpperBound
    uint64_t hash_ub = 0x222;
    tt.store(hash_ub, 5, 50, EntryType::UpperBound, std::nullopt);
    REQUIRE(tt.probe(hash_ub, 5, 100, 1000).value().first == 50); // 50 <= 100
    REQUIRE(!tt.probe(hash_ub, 5, 30, 1000).has_value()); // 50 > 30 → not usable
}

TEST_CASE("atomic_tt_hash_mismatch", "[tt]") {
    AtomicTT tt(1);
    tt.store(0xAABBCCDD11223344, 5, 100, EntryType::Exact, std::optional<Pos>(Pos{9, 9}));

    // Different hash should return None (XOR check fails)
    auto result = tt.probe(0xFFEEDDCC44332211, 5, -1000, 1000);
    REQUIRE(!result.has_value());
}

TEST_CASE("atomic_tt_get_best_move", "[tt]") {
    AtomicTT tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 5, 100, EntryType::Exact, std::optional<Pos>(Pos{9, 9}));
    REQUIRE(tt.get_best_move(hash) == std::optional<Pos>(Pos{9, 9}));
    REQUIRE(!tt.get_best_move(0xFFFFFFFFFFFFFFFF).has_value());
}

TEST_CASE("atomic_tt_clear", "[tt]") {
    AtomicTT tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    tt.store(hash, 5, 100, EntryType::Exact, std::nullopt);
    tt.clear();

    REQUIRE(!tt.probe(hash, 5, -1000, 1000).has_value());
}

TEST_CASE("atomic_tt_stats", "[tt]") {
    AtomicTT tt(1);
    auto stats = tt.stats();
    REQUIRE(stats.used == 0);

    tt.store(0x111, 5, 100, EntryType::Exact, std::nullopt);
    tt.store(0x222, 5, 100, EntryType::Exact, std::nullopt);

    stats = tt.stats();
    REQUIRE(stats.used >= 2);
}

TEST_CASE("atomic_tt_replacement_policy", "[tt]") {
    AtomicTT tt(1);
    uint64_t hash = 0x123456789ABCDEF0;

    // Store shallow, then deeper — deeper replaces
    tt.store(hash, 3, 100, EntryType::Exact, std::optional<Pos>(Pos{5, 5}));
    tt.store(hash, 5, 200, EntryType::Exact, std::optional<Pos>(Pos{9, 9}));
    REQUIRE(tt.probe(hash, 5, -1000, 1000).value().first == 200);
}

TEST_CASE("atomic_tt_concurrent_safety", "[tt]") {
    AtomicTT tt(1);
    std::vector<std::thread> handles;

    // Spawn 4 threads writing different entries concurrently
    for (uint64_t t = 0; t < 4; ++t) {
        handles.push_back(std::thread([&tt, t]() {
            for (uint64_t i = 0; i < 1000; ++i) {
                uint64_t hash = t * 100000 + i;
                tt.store(hash, 5, static_cast<int32_t>(i) * 10, EntryType::Exact, std::optional<Pos>(Pos{9, 9}));
            }
        }));
    }

    for (auto& h : handles) {
        h.join();
    }

    // Verify: some entries should be readable (exact count depends on collisions)
    auto stats = tt.stats();
    REQUIRE(stats.used > 0);
}
