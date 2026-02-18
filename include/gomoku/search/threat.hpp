#pragma once

// VCF/VCT threat search for forced wins
//
// - VCF (Victory by Continuous Fours): Finds winning sequences using only
//   four-threats. Opponent must defend each four; we continue until we win.
// - VCT (Victory by Continuous Threats): More general, includes open-three
//   threats. Tries VCF first, then falls back to VCT.
//
// These are powerful pruning techniques that find forced wins much faster
// than regular alpha-beta by only considering forcing moves.

#include "gomoku/board/board.hpp"
#include <vector>
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

    // Search for VCF (Victory by Continuous Fours)
    ThreatResult search_vcf(const Board& board, Stone color);

    // Search for VCT (Victory by Continuous Threats)
    // Tries VCF first, then falls back to VCT with open-three threats.
    ThreatResult search_vct(const Board& board, Stone color);

    uint64_t nodes() const { return nodes_; }
    void reset_nodes() { nodes_ = 0; }

    // Public for testing (mirrors Rust test access patterns)
    bool creates_five_or_more(const Board& board, Pos pos, Stone color) const;
    bool creates_four(const Board& board, Pos pos, Stone color) const;
    bool creates_open_three(const Board& board, Pos pos, Stone color) const;
    std::vector<Pos> find_four_threats(const Board& board, Stone color) const;
    std::vector<Pos> find_defense_moves(
        const Board& board, Pos threat_move, Stone attacker) const;

    uint8_t max_vcf_depth() const { return max_vcf_depth_; }
    uint8_t max_vct_depth() const { return max_vct_depth_; }

private:
    uint8_t max_vcf_depth_;
    uint8_t max_vct_depth_;
    uint64_t nodes_;

    bool vcf_search(Board& board, Stone color, uint8_t depth,
                    std::vector<Pos>& sequence);
    bool vct_search(Board& board, Stone color, uint8_t depth,
                    std::vector<Pos>& sequence);
    std::vector<Pos> find_all_threats(const Board& board, Stone color) const;
    std::vector<Pos> find_threat_defenses(
        const Board& board, Pos threat_move, Stone attacker) const;
};

} // namespace gomoku
