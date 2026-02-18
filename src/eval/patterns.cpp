// Pattern scoring implementation -- capture-based scoring

#include "gomoku/eval/patterns.hpp"
#include <algorithm>

namespace gomoku {

int capture_score(uint8_t my_captures, uint8_t opp_captures) {
    // Non-linear scoring: closer to win = exponentially more valuable.
    // Each level significantly higher than pattern threats at that stage.
    static constexpr int CAP_WEIGHTS[6] = {
        0,
        5'000,                        // 1 capture: significant
        7'000,                        // 2 captures: moderate
        20'000,                       // 3 captures: serious threat
        PatternScore::NEAR_CAPTURE_WIN, // 4 captures: 80K, near-winning
        PatternScore::CAPTURE_WIN,      // 5 captures: 1M, game over
    };

    int my_idx  = std::min(int(my_captures), 5);
    int opp_idx = std::min(int(opp_captures), 5);

    return CAP_WEIGHTS[my_idx] - CAP_WEIGHTS[opp_idx];
}

} // namespace gomoku
