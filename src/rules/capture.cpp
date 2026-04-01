// Regles de capture -- capture de paires style Pente (X-OO-X)
//
// En Ninuki-renju, quand un joueur pose une pierre et encadre une paire
// adverse avec ses propres pierres (motif X-OO-X), la paire adverse est
// retiree du plateau. Capturer 5 paires (10 pierres) gagne la partie.
//
// execute_captures_fast : version optimisee qui stocke les positions
// capturees dans un tableau fixe (CaptureInfo, pas de std::vector)
// pour eviter les allocations memoire dans les chemins chauds de la
// recherche. undo_captures permet de "defaire" la capture pour le
// backtracking de l'alpha-beta.

#include "gomoku/rules/capture.hpp"

namespace gomoku {

// 4 axis directions; each is checked with both signs (8 rays total)
static constexpr int DIRECTIONS[4][2] = {
    {0, 1},   // Horizontal
    {1, 0},   // Vertical
    {1, 1},   // Diagonal SE
    {1, -1},  // Diagonal SW
};

std::vector<Pos> get_captured_positions(const Board& board, Pos pos, Stone stone) {
    std::vector<Pos> captured;
    Stone opp = opponent(stone);

    for (const auto& dir : DIRECTIONS) {
        // Check both signs along this axis
        for (int sign : {-1, 1}) {
            int dr = dir[0] * sign;
            int dc = dir[1] * sign;

            // Pattern: placed(pos) - opp(+1) - opp(+2) - own(+3)
            int r3 = pos.row + dr * 3;
            int c3 = pos.col + dc * 3;
            if (!Pos::is_valid(r3, c3)) continue;

            Pos pos1{uint8_t(pos.row + dr),     uint8_t(pos.col + dc)};
            Pos pos2{uint8_t(pos.row + dr * 2), uint8_t(pos.col + dc * 2)};
            Pos pos3{uint8_t(r3),               uint8_t(c3)};

            if (board.get(pos1) == opp &&
                board.get(pos2) == opp &&
                board.get(pos3) == stone)
            {
                captured.push_back(pos1);
                captured.push_back(pos2);
            }
        }
    }
    return captured;
}

bool has_capture(const Board& board, Pos pos, Stone stone) {
    Stone opp = opponent(stone);

    for (const auto& dir : DIRECTIONS) {
        for (int sign : {-1, 1}) {
            int dr = dir[0] * sign;
            int dc = dir[1] * sign;

            int r3 = pos.row + dr * 3;
            int c3 = pos.col + dc * 3;
            if (!Pos::is_valid(r3, c3)) continue;

            Pos pos1{uint8_t(pos.row + dr),     uint8_t(pos.col + dc)};
            Pos pos2{uint8_t(pos.row + dr * 2), uint8_t(pos.col + dc * 2)};
            Pos pos3{uint8_t(r3),               uint8_t(c3)};

            if (board.get(pos1) == opp &&
                board.get(pos2) == opp &&
                board.get(pos3) == stone)
            {
                return true;
            }
        }
    }
    return false;
}

uint8_t count_captures_fast(const Board& board, Pos pos, Stone stone) {
    Stone opp = opponent(stone);
    uint8_t pairs = 0;

    for (const auto& dir : DIRECTIONS) {
        for (int sign : {-1, 1}) {
            int dr = dir[0] * sign;
            int dc = dir[1] * sign;

            int r3 = pos.row + dr * 3;
            int c3 = pos.col + dc * 3;
            if (!Pos::is_valid(r3, c3)) continue;

            Pos pos1{uint8_t(pos.row + dr),     uint8_t(pos.col + dc)};
            Pos pos2{uint8_t(pos.row + dr * 2), uint8_t(pos.col + dc * 2)};
            Pos pos3{uint8_t(r3),               uint8_t(c3)};

            if (board.get(pos1) == opp &&
                board.get(pos2) == opp &&
                board.get(pos3) == stone)
            {
                pairs++;
            }
        }
    }
    return pairs;
}

CaptureInfo execute_captures_fast(Board& board, Pos pos, Stone stone) {
    // 
    Stone opp = opponent(stone);
    CaptureInfo info{};

    for (const auto& dir : DIRECTIONS) {
        for (int sign : {-1, 1}) {
            int dr = dir[0] * sign;
            int dc = dir[1] * sign;

            int r3 = pos.row + dr * 3;
            int c3 = pos.col + dc * 3;
            if (!Pos::is_valid(r3, c3)) continue;

            Pos pos1{uint8_t(pos.row + dr),     uint8_t(pos.col + dc)};
            Pos pos2{uint8_t(pos.row + dr * 2), uint8_t(pos.col + dc * 2)};
            Pos pos3{uint8_t(r3),               uint8_t(c3)};

            if (board.get(pos1) == opp &&
                board.get(pos2) == opp &&
                board.get(pos3) == stone)
            {
                size_t idx = info.count;
                info.positions[idx]     = pos1;
                info.positions[idx + 1] = pos2;
                info.count += 2;
                info.pairs += 1;
                board.remove_stone(pos1);
                board.remove_stone(pos2);
            }
        }
    }

    board.add_captures(stone, info.pairs);
    return info;
}

void undo_captures(Board& board, Stone stone, const CaptureInfo& info) {
    Stone opp = opponent(stone);
    for (uint8_t i = 0; i < info.count; ++i) {
        board.place_stone(info.positions[i], opp);
    }
    board.sub_captures(stone, info.pairs);
}

} // namespace gomoku
