#include "gomoku/board/bitboard.hpp"

namespace gomoku {

    uint32_t Bitboard::count() const {
        uint32_t total = 0;
        for (int i = 0; i < 6; ++i) {
            total += static_cast<uint32_t>(__builtin_popcountll(bits[i]));
        }
        return total;
    }

    bool Bitboard::is_empty() const {
        for (int i = 0; i < 6; ++i) {
            if (bits[i] != 0) return false;
        }
        return true;
    }

    BitboardIterator Bitboard::begin() const {
        return BitboardIterator(bits, 0);
    }

    BitboardIterator Bitboard::end() const {
        return BitboardIterator(); // sentinel: word_idx_ == 6
    }

    bool Bitboard::operator==(const Bitboard& o) const {
        for (int i = 0; i < 6; ++i) {
            if (bits[i] != o.bits[i]) return false;
        }
        return true;
    }

    // --- BitboardIterator ---

    BitboardIterator::BitboardIterator(const uint64_t* bits, int start_word)
        : bits_(bits), word_idx_(start_word), current_word_(0)
    {
        if (word_idx_ < 6) {
            current_word_ = bits_[word_idx_];
            advance_to_next_bit();
        }
    }

    void BitboardIterator::advance_to_next_bit() {
        // Skip empty words
        while (current_word_ == 0) {
            ++word_idx_;
            if (word_idx_ >= 6) return;
            current_word_ = bits_[word_idx_];
        }
    }

    Pos BitboardIterator::operator*() const {
        int bit_pos = __builtin_ctzll(current_word_);
        size_t idx = static_cast<size_t>(word_idx_) * 64 + bit_pos;
        return Pos::from_index(idx);
    }

    BitboardIterator& BitboardIterator::operator++() {
        // Clear the lowest set bit
        current_word_ &= current_word_ - 1;
        advance_to_next_bit();

        // Check if we've gone past valid board positions (361 cells, not 384)
        if (word_idx_ < 6) {
            int bit_pos = __builtin_ctzll(current_word_);
            size_t idx = static_cast<size_t>(word_idx_) * 64 + bit_pos;
            if (idx >= static_cast<size_t>(TOTAL_CELLS)) {
                word_idx_ = 6; // Mark as end
            }
        }
        return *this;
    }

    bool BitboardIterator::operator!=(const BitboardIterator& o) const {
        return word_idx_ != o.word_idx_ || current_word_ != o.current_word_;
    }

} // namespace gomoku
