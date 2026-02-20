// Verification des conditions de victoire -- cinq a la suite et captures
//
// En Ninuki-renju, on gagne soit en alignant 5 pierres (ou plus), soit
// en capturant 5 paires adverses. Mais il y a une subtilite : un cinq
// n'est pas forcement definitif. Si l'adversaire peut capturer une paire
// qui fait partie du cinq (motif X-OO-X sur les pierres du cinq), le
// cinq est "cassable" et l'adversaire a un tour pour le briser.
// C'est check_winner qui gere cette logique : si le cinq est cassable,
// la partie continue.

#include "gomoku/rules/win.hpp"
#include "gomoku/rules/capture.hpp"
#include <algorithm>

namespace gomoku {

// 4 axis directions for line checking
static constexpr int DIRECTIONS[4][2] = {
    {0, 1},   // Horizontal
    {1, 0},   // Vertical
    {1, 1},   // Diagonal SE
    {1, -1},  // Diagonal SW
};

bool has_five_in_row(const Board& board, Stone stone) {
    const Bitboard* bb = board.stones(stone);
    if (!bb) return false;
    for (auto pos : *bb) {
        if (has_five_at_pos(board, pos, stone)) return true;
    }
    return false;
}

bool has_five_at_pos(const Board& board, Pos pos, Stone color) {
    // Direction order matches Rust: (1,0), (0,1), (1,1), (1,-1)
    constexpr int SZ = 19;
    constexpr int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

    for (const auto& d : dirs) {
        int dr = d[0], dc = d[1];
        int count = 1;

        // Positive direction
        int r = pos.row + dr;
        int c = pos.col + dc;
        while (r >= 0 && r < SZ && c >= 0 && c < SZ) {
            if (board.get(Pos(uint8_t(r), uint8_t(c))) == color) {
                count++;
                r += dr;
                c += dc;
            } else {
                break;
            }
        }

        // Negative direction
        r = pos.row - dr;
        c = pos.col - dc;
        while (r >= 0 && r < SZ && c >= 0 && c < SZ) {
            if (board.get(Pos(uint8_t(r), uint8_t(c))) == color) {
                count++;
                r -= dr;
                c -= dc;
            } else {
                break;
            }
        }

        if (count >= 5) return true;
    }
    return false;
}

std::optional<std::vector<Pos>> find_five_line_at_pos(
    const Board& board, Pos pos, Stone color)
{
    constexpr int SZ = 19;
    constexpr int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

    for (const auto& d : dirs) {
        int dr = d[0], dc = d[1];
        std::vector<Pos> line = {pos};

        // Positive direction
        int r = pos.row + dr;
        int c = pos.col + dc;
        while (r >= 0 && r < SZ && c >= 0 && c < SZ) {
            if (board.get(Pos(uint8_t(r), uint8_t(c))) == color) {
                line.push_back(Pos(uint8_t(r), uint8_t(c)));
                r += dr;
                c += dc;
            } else {
                break;
            }
        }

        // Negative direction
        r = pos.row - dr;
        c = pos.col - dc;
        while (r >= 0 && r < SZ && c >= 0 && c < SZ) {
            if (board.get(Pos(uint8_t(r), uint8_t(c))) == color) {
                line.push_back(Pos(uint8_t(r), uint8_t(c)));
                r -= dr;
                c -= dc;
            } else {
                break;
            }
        }

        if (line.size() >= 5) return line;
    }
    return std::nullopt;
}

std::optional<std::vector<Pos>> find_five_positions(
    const Board& board, Stone stone)
{
    const Bitboard* bb = board.stones(stone);
    if (!bb) return std::nullopt;

    for (auto pos : *bb) {
        for (const auto& dir : DIRECTIONS) {
            int dr = dir[0], dc = dir[1];
            std::vector<Pos> line = {pos};

            // Extend in negative direction first
            for (int i = 1; i < 5; ++i) {
                int r = pos.row - dr * i;
                int c = pos.col - dc * i;
                if (!Pos::is_valid(r, c)) break;
                Pos prev{uint8_t(r), uint8_t(c)};
                if (board.get(prev) == stone) {
                    line.insert(line.begin(), prev);
                } else {
                    break;
                }
            }

            // Extend in positive direction
            for (int i = 1; i < 5; ++i) {
                int r = pos.row + dr * i;
                int c = pos.col + dc * i;
                if (!Pos::is_valid(r, c)) break;
                Pos next{uint8_t(r), uint8_t(c)};
                if (board.get(next) == stone) {
                    line.push_back(next);
                } else {
                    break;
                }
            }

            if (line.size() >= 5) return line;
        }
    }
    return std::nullopt;
}

bool can_break_five_by_capture(
    const Board& board,
    const std::vector<Pos>& five_positions,
    Stone five_color)
{
    Stone opp = opponent(five_color);

    // For each empty position within radius 2 of the five stones.
    // Radius 2 because capture pattern X-OO-X means the capturing
    // stone can be up to 2 steps away from the nearest five-stone.
    for (const auto& pos : five_positions) {
        for (int dr = -2; dr <= 2; ++dr) {
            for (int dc = -2; dc <= 2; ++dc) {
                if (dr == 0 && dc == 0) continue;

                int r = pos.row + dr;
                int c = pos.col + dc;
                if (!Pos::is_valid(r, c)) continue;

                Pos adj_pos{uint8_t(r), uint8_t(c)};
                if (!board.is_empty(adj_pos)) continue;

                // Check if opponent placing here would capture part of the five
                auto would_capture = get_captured_positions(board, adj_pos, opp);
                for (const auto& cap : would_capture) {
                    if (std::find(five_positions.begin(), five_positions.end(), cap)
                        != five_positions.end())
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

std::vector<Pos> find_five_break_moves(
    const Board& board,
    const std::vector<Pos>& five_positions,
    Stone five_color)
{
    Stone opp = opponent(five_color);
    std::vector<Pos> break_moves;

    for (const auto& pos : five_positions) {
        for (int dr = -2; dr <= 2; ++dr) {
            for (int dc = -2; dc <= 2; ++dc) {
                if (dr == 0 && dc == 0) continue;

                int r = pos.row + dr;
                int c = pos.col + dc;
                if (!Pos::is_valid(r, c)) continue;

                Pos adj_pos{uint8_t(r), uint8_t(c)};
                if (!board.is_empty(adj_pos)) continue;
                if (std::find(break_moves.begin(), break_moves.end(), adj_pos)
                    != break_moves.end())
                {
                    continue;
                }

                auto would_capture = get_captured_positions(board, adj_pos, opp);
                for (const auto& cap : would_capture) {
                    if (std::find(five_positions.begin(), five_positions.end(), cap)
                        != five_positions.end())
                    {
                        break_moves.push_back(adj_pos);
                        break;
                    }
                }
            }
        }
    }
    return break_moves;
}

std::optional<Stone> check_winner(const Board& board, Stone last_player) {
    // Check capture win first (5 pairs = 10 stones)
    if (board.captures(Stone::Black) >= 5) return Stone::Black;
    if (board.captures(Stone::White) >= 5) return Stone::White;

    Stone other = opponent(last_player);

    // 1. If the OTHER player already had a five on the board,
    //    last_player just moved and didn't break it → other wins.
    auto other_five = find_five_positions(board, other);
    if (other_five.has_value()) {
        return other;
    }

    // 2. If last_player just formed a five:
    //    - Unbreakable → last_player wins immediately.
    //    - Breakable → game continues (other gets one turn to break it).
    auto my_five = find_five_positions(board, last_player);
    if (my_five.has_value()) {
        if (!can_break_five_by_capture(board, my_five.value(), last_player)) {
            return last_player;
        }
        // Breakable: game continues, other player gets a chance
    }

    return std::nullopt;
}

} // namespace gomoku
