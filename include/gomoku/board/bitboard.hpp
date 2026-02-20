#pragma once

#include "types.hpp"
#include <cstdint>


// Bitboard -- representation compacte du plateau en 6 x uint64_t (384 >= 361 cases).
// Au lieu de parcourir 361 cases une par une, on traite 64 bits d'un coup.
// Indispensable pour les scans de patterns et l'evaluation rapide.
namespace gomoku {

// Forward declaration for the iterator
    class BitboardIterator;

    class Bitboard {
    public:
        // Raw bit storage -- public for direct access in hot paths (zobrist, eval)
        uint64_t bits[6];

        // Create an empty bitboard (all zeros)
        constexpr Bitboard() : bits{0, 0, 0, 0, 0, 0} {}

        // Set the bit at the given position
        inline void set(Pos pos) {
            size_t idx = pos.to_index();
            bits[idx / 64] |= 1ULL << (idx % 64);
        }

        // Clear the bit at the given position
        inline void clear(Pos pos) {
            size_t idx = pos.to_index();
            bits[idx / 64] &= ~(1ULL << (idx % 64));
        }

        // Check if the bit is set at the given position
        inline bool get(Pos pos) const {
            size_t idx = pos.to_index();
            return (bits[idx / 64] >> (idx % 64)) & 1;
        }

        // Count total set bits (popcount)
        uint32_t count() const;

        // Check if all bits are zero
        bool is_empty() const;

        // Range-for support: iterate over all set bit positions
        BitboardIterator begin() const;
        BitboardIterator end() const;

        bool operator==(const Bitboard& o) const;
        bool operator!=(const Bitboard& o) const { return !(*this == o); }
    };

    // Iterator over set bits in a Bitboard.
    // Yields Pos for each set bit, in ascending index order.
    class BitboardIterator {
    public:
        BitboardIterator() : bits_(nullptr), word_idx_(6), current_word_(0) {}
        BitboardIterator(const uint64_t* bits, int start_word);

        Pos operator*() const;
        BitboardIterator& operator++();
        bool operator!=(const BitboardIterator& o) const;

    private:
        const uint64_t* bits_;  // pointer instead of 48-byte copy
        int word_idx_;
        uint64_t current_word_;

        // Advance to the next word that has set bits
        void advance_to_next_bit();
    };

} // namespace gomoku
