#pragma once

// VCF/VCT threat search for forced wins
//
// - VCF (Victory by Continuous Fours): Finds winning sequences using only
//   four-threats. Opponent must defend each four; we continue until we win.
// - VCT (Victory by Continuous Threats): More general, includes open-three
//   threats. Tries VCF first, then falls back to VCT.
//
// Implementation split:
// - threat_vcf.cpp : VCF search (used in the engine pipeline)
// - threat_vct.cpp : VCT search (standalone, not called by the engine)

#include "gomoku/board/board.hpp"
#include <vector>
#include <chrono>
#include <cstdint>

namespace gomoku {

// Result of a VCF/VCT search
struct ThreatResult {
    std::vector<Pos> winning_sequence; // Attacker moves only
    bool found;

    static ThreatResult not_found() { return {{}, false}; }
    static ThreatResult found_win(std::vector<Pos> seq) {
        return {std::move(seq), true};
    }
};

// Threat searcher for VCF/VCT algorithms
class ThreatSearcher {
public:
    // Default depth limits: VCF=30, VCT=20
    ThreatSearcher();

    // Custom depth limits
    ThreatSearcher(uint8_t vcf_depth, uint8_t vct_depth);

    // Search for VCF (Victory by Continuous Fours).
    // time_limit_ms: abort if exceeded (0 = no limit).
    ThreatResult search_vcf(const Board& board, Stone color,
                            uint64_t time_limit_ms = 0);

    uint64_t nodes() const { return nodes_; }

private:
    uint8_t max_vcf_depth_;
    uint64_t nodes_;
    std::chrono::steady_clock::time_point search_start_;
    uint64_t search_time_limit_ms_ = 0;

    bool is_timed_out() const;

public:
    // Analysis helpers (also used by tests)
    bool creates_five_or_more(const Board& board, Pos pos, Stone color) const;
    bool creates_four(const Board& board, Pos pos, Stone color) const;

    std::vector<Pos> find_four_threats(const Board& board, Stone color) const;

private:
    std::vector<Pos> find_defense_moves(
        const Board& board, Pos threat_move, Stone attacker) const;

    bool vcf_search(Board& board, Stone color, uint8_t depth,
                    std::vector<Pos>& sequence);
};

} // namespace gomoku
