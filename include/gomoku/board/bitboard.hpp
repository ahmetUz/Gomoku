#pragma once

#include "types.hpp"
#include <cstdint>


// ? Bitboard representation to use 6x uint64_t to represent the 361 cells of the board (6*64=384 >= 361).
// ? It avoid the iteration of 19x19 cells for each operations on the board, and allows to make 6 instructions to CPU check the whole board, instead of 361 instructions. Essential for pattern matching and evaluation.
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
