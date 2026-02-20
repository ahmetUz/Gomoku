#pragma once

// Table de transposition -- memoire cache des positions evaluees
//
// Deux implementations :
// 1. TranspositionTable : mono-thread, pour les tests (std::optional<TTEntry>)
// 2. AtomicTT : lock-free pour la recherche parallele Lazy SMP
//    Utilise le XOR trick (Hyatt 1994) : key = hash XOR data.
//    Les lectures corrompues (torn reads) donnent un hash different
//    et sont ignorees silencieusement. Pas besoin de verrous.

#include "gomoku/board/types.hpp"
#include <cstdint>
#include <cstddef>
#include <optional>
#include <tuple>
#include <vector>
#include <atomic>
#include <memory>

namespace gomoku {

// Entry type for score interpretation
enum class EntryType : uint8_t {
    Exact      = 0, // Exact score (search completed normally)
    LowerBound = 1, // Score >= stored value (beta cutoff)
    UpperBound = 2, // Score <= stored value (alpha fail-low)
};

// Transposition table entry
struct TTEntry {
    uint64_t hash;
    int8_t depth;
    int32_t score;
    EntryType entry_type;
    std::optional<Pos> best_move;
};

// Statistics about transposition table usage
struct TTStats {
    size_t size;
    size_t used;
    uint8_t usage_percent;
};

// =========================================================================
// Single-threaded TranspositionTable
// =========================================================================

class TranspositionTable {
public:
    explicit TranspositionTable(size_t size_mb);

    // Probe: returns (score, best_move) if entry found.
    // Score is 0 if entry exists but depth insufficient (best_move still returned).
    std::optional<std::pair<int32_t, std::optional<Pos>>>
    probe(uint64_t hash, int8_t depth, int32_t alpha, int32_t beta) const;

    // Get best move for move ordering (ignores depth/score).
    std::optional<Pos> get_best_move(uint64_t hash) const;

    // Store a search result. Depth-preferred replacement policy.
    void store(uint64_t hash, int8_t depth, int32_t score,
               EntryType entry_type, std::optional<Pos> best_move);

    void clear();
    TTStats stats() const;
    size_t table_size() const { return size_; }

private:
    std::vector<std::optional<TTEntry>> entries_;
    size_t size_;
};

// =========================================================================
// Pack/unpack for AtomicTT (exposed for testing)
// =========================================================================

// Pack TT entry fields into a single uint64_t.
// Layout: depth[0..7] score[8..28] type[29..30] has_move[31] row[32..36] col[37..41]
uint64_t pack_entry(int8_t depth, int32_t score, EntryType entry_type,
                    std::optional<Pos> best_move);

// Unpack a uint64_t back into TT entry fields.
std::tuple<int8_t, int32_t, EntryType, std::optional<Pos>>
unpack_entry(uint64_t data);

// =========================================================================
// Lock-free AtomicTT for Lazy SMP
// =========================================================================

class AtomicTT {
public:
    explicit AtomicTT(size_t size_mb);

    // Probe: returns (score, best_move) if valid entry found and usable.
    // Returns nullopt if entry not found, hash mismatch, or depth insufficient.
    std::optional<std::pair<int32_t, std::optional<Pos>>>
    probe(uint64_t hash, int8_t depth, int32_t alpha, int32_t beta) const;

    // Get best move for move ordering.
    std::optional<Pos> get_best_move(uint64_t hash) const;

    // Store (thread-safe, uses XOR trick).
    void store(uint64_t hash, int8_t depth, int32_t score,
               EntryType entry_type, std::optional<Pos> best_move);

    void clear();
    TTStats stats() const;

private:
    // Raw atomic arrays (std::atomic is not copyable/movable, so use unique_ptr)
    std::unique_ptr<std::atomic<uint64_t>[]> keys_;
    std::unique_ptr<std::atomic<uint64_t>[]> data_;
    size_t size_;
};

} // namespace gomoku
