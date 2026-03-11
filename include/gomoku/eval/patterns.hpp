#pragma once

// Scores des patterns -- echelle de valeur des motifs au Gomoku
//
// Chaque motif (cinq, quatre ouvert, trois ouvert, etc.) a une valeur
// numerique qui reflete sa dangerosité. L'echelle est soigneusement
// calibree pour les regles Ninuki-renju avec captures de paires.

#include <cstdint>

namespace gomoku {

// Pattern scores for evaluation -- carefully tuned for strong play.
struct PatternScore {
    // Winning patterns
    static constexpr int FIVE        = 1'000'000;  // Five in a row
    static constexpr int CAPTURE_WIN = 1'000'000;  // 5 pair captures

    // Strong attacking patterns
    static constexpr int OPEN_FOUR   = 100'000;    // _OOOO_ (unstoppable)
    static constexpr int CLOSED_FOUR =  50'000;    // XOOOO_ or _OOOOX

    // Moderate threats
    static constexpr int OPEN_THREE  =  10'000;    // _OOO_ (becomes open four)
    static constexpr int CLOSED_THREE =  1'500;    // XOOO_ or _OOOX

    // Building patterns
    static constexpr int OPEN_TWO    =   1'000;    // _OO_ (potential to grow)
    static constexpr int CLOSED_TWO  =     200;    // XOO_ or _OOX

    // Capture related -- critical in Ninuki-renju
    static constexpr int NEAR_CAPTURE_WIN  = 80'000;   // 4 pairs (one more = win)
};

// Capture-based scoring with non-linear weights.
// MUST be symmetric for negamax: capture_score(a, b) == -capture_score(b, a).
int capture_score(uint8_t my_captures, uint8_t opp_captures);

} // namespace gomoku
