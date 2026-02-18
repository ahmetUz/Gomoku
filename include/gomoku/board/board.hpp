#pragma once

#include "types.hpp"
#include "bitboard.hpp"

namespace gomoku {

// Game board with capture tracking.
// Fields are public for direct access in hot paths (zobrist hash, eval).
class Board {
public:
    Bitboard black;          // Black stones bitboard
    Bitboard white;          // White stones bitboard
    uint8_t black_captures;  // Pairs captured by Black (0-5, 5 = win)
    uint8_t white_captures;  // Pairs captured by White (0-5, 5 = win)

    // Create an empty board
    Board() : black(), white(), black_captures(0), white_captures(0) {}

    // Get stone at position
    inline Stone get(Pos pos) const {
        if (black.get(pos)) return Stone::Black;
        if (white.get(pos)) return Stone::White;
        return Stone::Empty;
    }

    // Check if position is empty
    inline bool is_empty(Pos pos) const {
        return !black.get(pos) && !white.get(pos);
    }

    // Place a stone (without capture processing)
    inline void place_stone(Pos pos, Stone stone) {
        switch (stone) {
            case Stone::Black: black.set(pos); break;
            case Stone::White: white.set(pos); break;
            case Stone::Empty: break;
        }
    }

    // Remove a stone from both bitboards
    inline void remove_stone(Pos pos) {
        black.clear(pos);
        white.clear(pos);
    }

    // Get bitboard for a color.
    // Returns nullptr for Stone::Empty (matches Rust Option<&Bitboard> semantics).
    const Bitboard* stones(Stone stone) const {
        switch (stone) {
            case Stone::Black: return &black;
            case Stone::White: return &white;
            default:           return nullptr;
        }
    }

    // Get mutable bitboard for a color.
    Bitboard* stones_mut(Stone stone) {
        switch (stone) {
            case Stone::Black: return &black;
            case Stone::White: return &white;
            default:           return nullptr;
        }
    }

    // Get capture count for a color
    inline uint8_t captures(Stone stone) const {
        switch (stone) {
            case Stone::Black: return black_captures;
            case Stone::White: return white_captures;
            default:           return 0;
        }
    }

    // Add captures (saturating at 255)
    inline void add_captures(Stone stone, uint8_t count) {
        switch (stone) {
            case Stone::Black:
                black_captures = static_cast<uint8_t>(
                    (black_captures + count > 255) ? 255 : black_captures + count);
                break;
            case Stone::White:
                white_captures = static_cast<uint8_t>(
                    (white_captures + count > 255) ? 255 : white_captures + count);
                break;
            case Stone::Empty: break;
        }
    }

    // Subtract captures (saturating at 0) -- used for unmake
    inline void sub_captures(Stone stone, uint8_t count) {
        switch (stone) {
            case Stone::Black:
                black_captures = (black_captures >= count) ?
                    static_cast<uint8_t>(black_captures - count) : 0;
                break;
            case Stone::White:
                white_captures = (white_captures >= count) ?
                    static_cast<uint8_t>(white_captures - count) : 0;
                break;
            case Stone::Empty: break;
        }
    }

    // Total stones on the board
    inline uint32_t stone_count() const {
        return black.count() + white.count();
    }

    // Check if the board has no stones
    inline bool is_board_empty() const {
        return black.is_empty() && white.is_empty();
    }
};

} // namespace gomoku
