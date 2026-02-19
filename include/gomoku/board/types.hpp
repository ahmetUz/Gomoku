#pragma once

#include <cstdint>
#include <cstddef>

namespace gomoku {

// Board dimensions
constexpr int BOARD_SIZE = 19;
constexpr int TOTAL_CELLS = BOARD_SIZE * BOARD_SIZE; // 361

// Stone colors
// ? Using uint8_t to stock in 1 octet, essential for the hot pack (evaluation, search)
enum class Stone : uint8_t {
    Empty = 0,
    Black = 1,
    White = 2,
};

// Get the opponent's color. Branchless via XOR trick.
// Black(1) ^ 3 = 2(White), White(2) ^ 3 = 1(Black), Empty(0) ^ 3 = 3 (unused).
// Callers in hot paths always pass Black or White.
inline Stone opponent(Stone s) {
    return static_cast<Stone>(static_cast<uint8_t>(s) ^ 3u);
}

// Position on the 19x19 board
struct Pos {
    uint8_t row;
    uint8_t col;

    constexpr Pos() : row(0), col(0) {}
    constexpr Pos(uint8_t r, uint8_t c) : row(r), col(c) {}

    // Convert to linear index (row-major order)
    constexpr size_t to_index() const {
        return static_cast<size_t>(row) * BOARD_SIZE + col;
    }

    // Create from linear index
    static constexpr Pos from_index(size_t idx) {
        return Pos(
            static_cast<uint8_t>(idx / BOARD_SIZE),
            static_cast<uint8_t>(idx % BOARD_SIZE)
        );
    }

    // Check if (row, col) is within board bounds
    static constexpr bool is_valid(int r, int c) {
        return r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE;
    }

    // Sentinel value representing "no position" (replaces Option<Pos> in hot paths)
    // ? What we return when we say "no position". Must be outside the valid board range.
    static constexpr Pos sentinel() { return Pos(255, 255); }
    constexpr bool is_sentinel() const { return row == 255; }

    constexpr bool operator==(const Pos& o) const { return row == o.row && col == o.col; }
    constexpr bool operator!=(const Pos& o) const { return !(*this == o); }

    // Row-major ordering, consistent with Rust Ord implementation
    constexpr bool operator<(const Pos& o) const { return to_index() < o.to_index(); }
    constexpr bool operator<=(const Pos& o) const { return to_index() <= o.to_index(); }
    constexpr bool operator>(const Pos& o) const { return to_index() > o.to_index(); }
    constexpr bool operator>=(const Pos& o) const { return to_index() >= o.to_index(); }
};

} // namespace gomoku
