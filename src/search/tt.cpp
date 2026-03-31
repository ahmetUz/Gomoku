// Table de transposition (TT) -- memoire cache des resultats de recherche
//
// Quand le moteur evalue une position, il stocke le resultat dans une grande
// table indexee par le hash de la position. Si la meme position se presente
// plus tard (par une autre sequence de coups), on reutilise le resultat au
// lieu de tout recalculer.
//
// AtomicTT : version lock-free pour la recherche parallele (Lazy SMP)
//   Utilise le "XOR trick" de Hyatt (1994) : on stocke key = hash XOR data.
//   En lecture, si (key XOR data) != hash attendu, c'est une lecture
//   corrompue (torn read) et on ignore l'entree. Pas besoin de mutex.
//
// Chaque entree contient : profondeur, score, type (exact/borne), meilleur coup.
// Le tout est compresse en 42 bits pour tenir dans un seul uint64_t atomique.

#include "gomoku/search/tt.hpp"
#include <algorithm>
#include <cstddef>

namespace gomoku {

// =========================================================================
// Lock-free AtomicTT for Lazy SMP parallel search
// =========================================================================

// Pack a TT entry into a u64 for atomic storage.
//
// Layout (42 bits used):
// bits [0..7]   depth (i8 → u8: +128 offset)        8 bits
// bits [8..28]  score (i32 → u21: +1_048_576)       21 bits
// bits [29..30] entry_type (0=Exact,1=LB,2=UB)       2 bits
// bits [31]     has_move (bool)                       1 bit
// bits [32..36] row (u5, 0-18)                        5 bits
// bits [37..41] col (u5, 0-18)                        5 bits
uint64_t pack_entry(int8_t depth, int32_t score, EntryType entry_type,
                    std::optional<Pos> best_move) {
    uint64_t d = static_cast<uint64_t>(static_cast<int16_t>(depth) + 128) & 0xFF;

    // Clamp score to 21-bit range [-1_048_575, 1_048_575] to prevent silent overflow.
    // In practice scores rarely exceed FIVE (1M), but this is cheap insurance.
    int32_t clamped = std::clamp(score, -1048575, 1048575);
    uint64_t s = static_cast<uint64_t>(static_cast<int64_t>(clamped) + 1048576) & 0x1FFFFF; // 21 bits

    uint64_t t = static_cast<uint64_t>(entry_type);

    uint64_t has_move;
    uint64_t row;
    uint64_t col;
    if (best_move.has_value()) {
        has_move = 1;
        row = static_cast<uint64_t>(best_move.value().row);
        col = static_cast<uint64_t>(best_move.value().col);
    } else {
        has_move = 0;
        row = 0;
        col = 0;
    }

    return d | (s << 8) | (t << 29) | (has_move << 31) | (row << 32) | (col << 37);
}

// Unpack a u64 back into TT entry fields.
std::tuple<int8_t, int32_t, EntryType, std::optional<Pos>>
unpack_entry(uint64_t data) {
    int16_t d = static_cast<int16_t>(data & 0xFF) - 128;
    int8_t depth = static_cast<int8_t>(d);

    int64_t s = static_cast<int64_t>((data >> 8) & 0x1FFFFF) - 1048576;
    int32_t score = static_cast<int32_t>(s);

    uint64_t t = (data >> 29) & 0x3;
    EntryType entry_type;
    switch (t) {
        case 0:
            entry_type = EntryType::Exact;
            break;
        case 1:
            entry_type = EntryType::LowerBound;
            break;
        default:
            entry_type = EntryType::UpperBound;
            break;
    }

    bool has_move = ((data >> 31) & 1) != 0;
    std::optional<Pos> best_move;
    if (has_move) {
        uint8_t row = static_cast<uint8_t>((data >> 32) & 0x1F);
        uint8_t col = static_cast<uint8_t>((data >> 37) & 0x1F);
        best_move = Pos{row, col};
    } else {
        best_move = std::nullopt;
    }

    return std::make_tuple(depth, score, entry_type, best_move);
}

AtomicTT::AtomicTT(size_t size_mb) {
    // Each slot = 2 x AtomicU64 = 16 bytes
    size_t slot_size = 16;
    size_t size = std::max((size_mb * 1024 * 1024) / slot_size, size_t(1024));

    keys_ = std::unique_ptr<std::atomic<uint64_t>[]>(new std::atomic<uint64_t>[size]{});
    data_ = std::unique_ptr<std::atomic<uint64_t>[]>(new std::atomic<uint64_t>[size]{});
    size_ = size;
}

std::optional<std::pair<int32_t, std::optional<Pos>>>
AtomicTT::probe(uint64_t hash, int8_t depth, int32_t alpha, int32_t beta) const {
    size_t idx = static_cast<size_t>(hash) % size_;
    uint64_t key = keys_[idx].load(std::memory_order_relaxed);
    uint64_t raw_data = data_[idx].load(std::memory_order_relaxed);

    // Empty slot
    if (key == 0 && raw_data == 0) {
        return std::nullopt;
    }

    // XOR verification: torn read → hash mismatch → safe miss
    if ((key ^ raw_data) != hash) {
        return std::nullopt;
    }

    auto [entry_depth, score, entry_type, best_move] = unpack_entry(raw_data);

    if (entry_depth >= depth) {
        switch (entry_type) {
            case EntryType::Exact:
                return std::make_pair(score, best_move);
            case EntryType::LowerBound:
                if (score >= beta) {
                    return std::make_pair(score, best_move);
                }
                break;
            case EntryType::UpperBound:
                if (score <= alpha) {
                    return std::make_pair(score, best_move);
                }
                break;
        }
    }

    // Score not usable at this depth/window — callers use get_best_move() for ordering
    return std::nullopt;
}

std::optional<Pos> AtomicTT::get_best_move(uint64_t hash) const {
    size_t idx = static_cast<size_t>(hash) % size_;
    uint64_t key = keys_[idx].load(std::memory_order_relaxed);
    uint64_t raw_data = data_[idx].load(std::memory_order_relaxed);

    if (key == 0 && raw_data == 0) {
        return std::nullopt;
    }
    if ((key ^ raw_data) != hash) {
        return std::nullopt;
    }

    auto [_depth, _score, _entry_type, best_move] = unpack_entry(raw_data);
    return best_move;
}

void AtomicTT::store(uint64_t hash, int8_t depth, int32_t score,
                     EntryType entry_type, std::optional<Pos> best_move) {
    size_t idx = static_cast<size_t>(hash) % size_;

    // Check replacement policy: replace if empty, same hash, or deeper
    uint64_t existing_data = data_[idx].load(std::memory_order_relaxed);
    uint64_t existing_key = keys_[idx].load(std::memory_order_relaxed);

    if (existing_data != 0 || existing_key != 0) {
        uint64_t existing_hash = existing_key ^ existing_data;
        if (existing_hash != hash) {
            // Different position: only replace if deeper
            auto [existing_depth, _, __, ___] = unpack_entry(existing_data);
            if (depth < existing_depth) {
                return;
            }
        }
    }

    uint64_t packed = pack_entry(depth, score, entry_type, best_move);
    uint64_t key = hash ^ packed;

    // Write data first, then key. This ordering means a concurrent reader
    // either sees old (key, data) pair or gets a hash mismatch on torn read.
    data_[idx].store(packed, std::memory_order_relaxed);
    keys_[idx].store(key, std::memory_order_relaxed);
}

void AtomicTT::clear() {
    for (size_t i = 0; i < size_; ++i) {
        keys_[i].store(0, std::memory_order_relaxed);
        data_[i].store(0, std::memory_order_relaxed);
    }
}

TTStats AtomicTT::stats() const {
    size_t used = 0;
    // Sample every 64th entry for speed (approximate is fine for stats)
    size_t step = (size_ > 65536) ? 64 : 1;
    size_t sampled = 0;

    for (size_t i = 0; i < size_; i += step) {
        sampled++;
        uint64_t k = keys_[i].load(std::memory_order_relaxed);
        uint64_t d = data_[i].load(std::memory_order_relaxed);
        if (k != 0 || d != 0) {
            used++;
        }
    }

    size_t estimated_used = (step > 1) ? (used * size_ / sampled) : used;

    return TTStats{
        size_,
        estimated_used,
        static_cast<uint8_t>(static_cast<double>(estimated_used) / size_ * 100.0)
    };
}

} // namespace gomoku
