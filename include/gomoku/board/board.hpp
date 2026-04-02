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

        // Get bitboard for a color (branchless table lookup).
        // Returns nullptr for Stone::Empty.
        inline const Bitboard* stones(Stone stone) const {
            const Bitboard* tbl[3] = {nullptr, &black, &white};
            return tbl[static_cast<uint8_t>(stone)];
        }

        // Get capture count for a color (branchless)
        inline uint8_t captures(Stone stone) const {
            const uint8_t tbl[3] = {0, black_captures, white_captures};
            return tbl[static_cast<uint8_t>(stone)];
        }

        // Get mutable pointer to capture count (branchless helper)
        inline uint8_t* captures_ptr(Stone stone) {
            uint8_t* tbl[3] = {nullptr, &black_captures, &white_captures};
            return tbl[static_cast<uint8_t>(stone)];
        }

        // Add captures (saturating at 255)
        inline void add_captures(Stone stone, uint8_t count) {
            uint8_t* cap = captures_ptr(stone);
            if (cap) {
                uint16_t sum = static_cast<uint16_t>(*cap) + count;
                *cap = static_cast<uint8_t>(sum > 255 ? 255 : sum);
            }
        }

        // Subtract captures (saturating at 0) -- used for unmake
        inline void sub_captures(Stone stone, uint8_t count) {
            uint8_t* cap = captures_ptr(stone);
            if (cap) {
                *cap = (*cap >= count) ? static_cast<uint8_t>(*cap - count) : 0;
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
