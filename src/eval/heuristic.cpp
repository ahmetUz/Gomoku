// Evaluation heuristique -- score statique d'une position sans recherche.
// Symetrique pour le negamax : eval(board, Black) == -eval(board, White).

#include "gomoku/eval/heuristic.hpp"
#include "gomoku/eval/patterns.hpp"
#include "gomoku/board/bitboard.hpp"
#include <utility>
#include <cstdlib>
#include <algorithm>

namespace gomoku {

// 4 directions de scan
static constexpr int DIRECTIONS[4][2] = {
    {0, 1},   // Horizontal
    {1, 0},   // Vertical
    {1, 1},   // Diagonal SE
    {1, -1},  // Diagonal SW
};

static constexpr int MAX_CENTER_DIST = 18;
static constexpr int POSITION_WEIGHT = 8;

// Poids de vulnerabilite, croissant avec les captures adverses.
static int vuln_weight(uint8_t opp_captures) {
    if (opp_captures <= 1) return 10'000;
    if (opp_captures == 2) return 20'000;
    if (opp_captures == 3) return 40'000;
    return 80'000;
}

// =========================================================================
// evaluate_line -- score un alignement dans une direction
// =========================================================================
// Compte les pierres alignees, detecte les trous (gap), et evalue la
// liberte de l'alignement (open_ends : 0=flanked, 1=half-free, 2=free).
// Verifie aussi si l'alignement a assez d'espace pour devenir un cinq.
static int evaluate_line(
    const Bitboard& my_bb,
    const Bitboard& opp_bb,
    Pos pos,
    int dr, int dc,
    bool prev_open)
{
    int count = 1;                          // pierres alignees (inclut pos)
    int open_ends = prev_open ? 1 : 0;     // bouts ouverts (0, 1 ou 2)
    bool has_gap = false;                   // trou detecte dans l'alignement
    int total_span = 1;                     // longueur totale (pierres + trou)

    // Avancer dans la direction positive
    int r = pos.row + dr;
    int c = pos.col + dc;
    while (Pos::is_valid(r, c)) {
        Pos p{uint8_t(r), uint8_t(c)};
        if (my_bb.get(p)) {
            count++;                        // pierre alliee → prolonger
            total_span++;
        } else if (opp_bb.get(p)) {
            break;                          // pierre adverse → bloque
        } else if (!has_gap) {
            // Case vide : verifier si une pierre suit (trou potentiel)
            int next_r = r + dr;
            int next_c = c + dc;
            if (Pos::is_valid(next_r, next_c) &&
                my_bb.get(Pos{uint8_t(next_r), uint8_t(next_c)}))
            {
                has_gap = true;             // trou confirme (ex: XX_X)
                total_span++;
                r += dr;
                c += dc;
                continue;
            }
            open_ends++;                    // pas de pierre apres → bout ouvert
            break;
        } else {
            open_ends++;                    // 2eme case vide → bout ouvert
            break;
        }
        r += dr;
        c += dc;
    }

    // Score selon le type de motif detecte
    if (has_gap) {
        // Motifs avec trou : remplir le trou peut completer un cinq
        if (count >= 5)                          return PatternScore::OPEN_FOUR;
        if (count == 4 && total_span == 5)       return PatternScore::OPEN_FOUR;
        if (count == 4)                          return PatternScore::CLOSED_FOUR;
        if (count == 3 && open_ends == 2)        return PatternScore::OPEN_THREE;
        if (count == 3 && open_ends == 1)        return PatternScore::CLOSED_THREE;
        return 0;
    } else {
        // Motifs sans trou : alignement continu
        if (count >= 5)                          return PatternScore::FIVE;
        if (count == 4 && open_ends == 2)        return PatternScore::OPEN_FOUR;
        if (count == 4 && open_ends == 1)        return PatternScore::CLOSED_FOUR;
        if (count == 3 && open_ends == 2)        return PatternScore::OPEN_THREE;
        if (count == 3 && open_ends == 1)        return PatternScore::CLOSED_THREE;
        if (count == 2 && open_ends == 2)        return PatternScore::OPEN_TWO;
        if (count == 2 && open_ends == 1)        return PatternScore::CLOSED_TWO;
        return 0;
    }
}

// =========================================================================
// Structs de resultat
// =========================================================================

struct AlignmentResult {
    int score;
    int open_fours;
    int closed_fours;
    int open_threes;
    int open_twos;
};

struct ConnVulnResult {
    int connectivity_score;
    int vuln_count;
};

// =========================================================================
// score_alignments -- detecte et score les motifs alignes (4 directions)
// =========================================================================
static AlignmentResult score_alignments(const Bitboard& my_bb, const Bitboard& opp_bb) {
    AlignmentResult result = {0, 0, 0, 0, 0};

    // Pour chaque pierre, scanner les 4 directions
    for (auto pos : my_bb) {
        for (const auto& dir : DIRECTIONS) {
            int dr = dir[0], dc = dir[1];

            // Filtre : ne traiter que le debut d'un segment (evite les doublons)
            int prev_r = pos.row - dr;
            int prev_c = pos.col - dc;
            if (Pos::is_valid(prev_r, prev_c) &&
                my_bb.get(Pos{uint8_t(prev_r), uint8_t(prev_c)}))
            {
                continue;
            }

            // Verifier si le bout arriere est ouvert (liberte)
            bool prev_open = Pos::is_valid(prev_r, prev_c) &&
                !opp_bb.get(Pos{uint8_t(prev_r), uint8_t(prev_c)});

            // Evaluer le motif sur cette ligne
            int pattern_score = evaluate_line(my_bb, opp_bb, pos, dr, dc, prev_open);
            result.score += pattern_score;

            // Comptabiliser le type de motif pour les combinaisons
            if (pattern_score >= PatternScore::OPEN_FOUR) {
                result.open_fours++;
            } else if (pattern_score >= PatternScore::CLOSED_FOUR) {
                result.closed_fours++;
            } else if (pattern_score >= PatternScore::OPEN_THREE) {
                result.open_threes++;
            } else if (pattern_score >= PatternScore::OPEN_TWO &&
                       pattern_score < PatternScore::CLOSED_THREE)
            {
                result.open_twos++;
            }
        }
    }

    return result;
}

// =========================================================================
// score_position -- bonus pour les pierres proches du centre
// =========================================================================
static int score_position(const Bitboard& my_bb) {
    int center = BOARD_SIZE / 2;
    int score = 0;

    for (auto pos : my_bb) {
        int dist = std::abs(int(pos.row) - center) + std::abs(int(pos.col) - center);
        score += (MAX_CENTER_DIST - dist) * POSITION_WEIGHT;
    }

    return score;
}

// =========================================================================
// score_connectivity_and_vulnerability -- bonus adjacence + paires capturables
// =========================================================================
// Bonus pour pierres alliees adjacentes.
// Penalite pour paires capturables par l'adversaire (pattern OXX_ ou _XXO).
static ConnVulnResult score_connectivity_and_vulnerability(
    const Bitboard& my_bb, const Bitboard& opp_bb)
{
    ConnVulnResult result = {0, 0};

    // Pour chaque pierre, verifier les 4 directions
    for (auto pos : my_bb) {
        for (const auto& dir : DIRECTIONS) {
            int dr = dir[0], dc = dir[1];
            int r1 = pos.row + dr;
            int c1 = pos.col + dc;
            if (!Pos::is_valid(r1, c1)) continue;
            Pos p1{uint8_t(r1), uint8_t(c1)};

            bool neighbor_is_mine = my_bb.get(p1);

            // Connectivity : bonus si voisin allie
            if (neighbor_is_mine) result.connectivity_score += 160;

            // Vulnerability : paire alliee capturable ?
            // On verifie les deux patterns : _XXO et OXX_
            if (neighbor_is_mine) {
                int rb = pos.row - dr;  // case avant la paire
                int cb = pos.col - dc;
                int ra = r1 + dr;       // case apres la paire
                int ca = c1 + dc;

                // Verifier case avant (b) et case apres (a)
                bool b_empty = false, b_opp = false;
                if (Pos::is_valid(rb, cb)) {
                    Pos pb{uint8_t(rb), uint8_t(cb)};
                    b_opp = opp_bb.get(pb);
                    b_empty = !b_opp && !my_bb.get(pb);
                }

                bool a_empty = false, a_opp = false;
                if (Pos::is_valid(ra, ca)) {
                    Pos pa{uint8_t(ra), uint8_t(ca)};
                    a_opp = opp_bb.get(pa);
                    a_empty = !a_opp && !my_bb.get(pa);
                }

                if (b_empty && a_opp) result.vuln_count++;  // _XXO
                if (b_opp && a_empty) result.vuln_count++;  // OXX_
            }
        }
    }

    return result;
}

// =========================================================================
// score_combinations -- bonus pour combinaisons de menaces imblocables
// =========================================================================
// Double quatre, quatre + trois ouvert, double trois ouvert, etc.
static int score_combinations(const AlignmentResult& a) {
    int score = 0;

    // Quatre ouvert + autre menace → quasi-imblocable
    if (a.open_fours >= 1 && (a.closed_fours >= 1 || a.open_threes >= 1)) {
        score += PatternScore::OPEN_FOUR;
    }
    // Double quatre ferme → l'adversaire ne peut en bloquer qu'un
    if (a.closed_fours >= 2) {
        score += PatternScore::OPEN_FOUR;
    }
    // Quatre ferme + trois ouvert → forcer la defense sur le quatre laisse le trois libre
    if (a.closed_fours >= 1 && a.open_threes >= 1) {
        score += PatternScore::OPEN_FOUR;
    }
    // Double trois ouvert → bloquer un laisse l'autre devenir quatre ouvert
    if (a.open_threes >= 2) {
        score += PatternScore::OPEN_FOUR;
    }

    // Bonus developpement multi-directionnel
    if (a.open_twos >= 4) {
        score += 8'000;
    } else if (a.open_twos >= 3) {
        score += 5'000;
    } else if (a.open_twos >= 2) {
        score += 3'000;
    }

    return score;
}

// =========================================================================
// game_phase -- phase de la partie basee sur les actions passees
// =========================================================================
// Retourne une valeur entre 0.0 (debut) et 1.0 (fin de partie).
// Basee sur le nombre de pierres posees et les captures effectuees,
// qui sont le resultat direct des actions passees des deux joueurs.
static float game_phase(uint32_t stone_count, uint8_t my_caps, uint8_t opp_caps) {
    // Les captures retirent des pierres du plateau → on les reinjecte
    float effective_stones = static_cast<float>(stone_count + (my_caps + opp_caps) * 2);
    return std::min(effective_stones / 60.0f, 1.0f);
}

// =========================================================================
// evaluate_color -- score total pour une couleur
// =========================================================================
// Ajuste les poids selon la phase de jeu :
//   - Debut : le centre compte plus, les patterns moins
//   - Fin   : les patterns et la vulnerabilite deviennent critiques
static std::pair<int, int> evaluate_color(
    const Board& board, Stone color, float phase)
{
    const Bitboard* my_bb = board.stones(color);
    if (!my_bb) return {0, 0};
    const Bitboard* opp_bb = board.stones(opponent(color));

    AlignmentResult alignments = score_alignments(*my_bb, *opp_bb);     // motifs alignes
    int position = score_position(*my_bb);                              // bonus centre
    auto [connectivity, vuln] = score_connectivity_and_vulnerability(*my_bb, *opp_bb); // adjacence + captures potentielles
    int combinations = score_combinations(alignments);                  // menaces combinees

    // Poids dynamiques selon la phase de jeu
    float w_pos   = 1.5f - phase;         // 1.5 en debut → 0.5 en fin
    float w_align = 0.8f + 0.2f * phase;  // 0.8 en debut → 1.0 en fin
    float w_conn  = 1.0f;                 // stable
    float w_comb  = 0.7f + 0.3f * phase;  // 0.7 en debut → 1.0 en fin

    int score = static_cast<int>(
        static_cast<float>(alignments.score) * w_align +
        static_cast<float>(position) * w_pos +
        static_cast<float>(connectivity) * w_conn +
        static_cast<float>(combinations) * w_comb
    );
    return {score, vuln};
}

// =========================================================================
// evaluate -- evalue la position pour les deux joueurs
// =========================================================================
int evaluate(const Board& board, Stone color) {
    Stone opp = opponent(color);

    // Victoire/defaite par captures
    if (board.captures(color) >= 5) return PatternScore::FIVE;
    if (board.captures(opp) >= 5)   return -PatternScore::FIVE;

    // Score base sur les pierres deja capturees par chaque joueur
    int cap_score = capture_score(board.captures(color), board.captures(opp));

    // Phase de jeu basee sur les actions passees (pierres posees + captures)
    uint8_t my_caps  = board.captures(color);
    uint8_t opp_caps = board.captures(opp);
    float phase = game_phase(board.stone_count(), my_caps, opp_caps);

    // Evaluation symetrique : on score les deux joueurs puis on fait la difference
    auto [my_score, my_vuln]   = evaluate_color(board, color, phase);
    auto [opp_score, opp_vuln] = evaluate_color(board, opp, phase);

    // Penalite de vulnerabilite : plus l'adversaire a de captures, plus mes paires exposees coutent cher
    int vuln_penalty = my_vuln * vuln_weight(opp_caps) - opp_vuln * vuln_weight(my_caps);

    return cap_score + (my_score - opp_score) - vuln_penalty;
}

} // namespace gomoku
