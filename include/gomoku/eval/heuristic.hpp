#pragma once

// Evaluation heuristique -- le "jugement" statique du moteur
//
// Attribue un score a une position sans chercher plus loin dans l'arbre.
// Combine : patterns de ligne, avantage de captures, controle du centre,
// connectivite des pierres, et penalite de vulnerabilite aux captures.
// Symetrique pour le negamax : evaluate(board, Black) == -evaluate(board, White)

#include "gomoku/board/board.hpp"

namespace gomoku {

// Evaluate the board from the perspective of the given color.
// Positive = advantage, negative = disadvantage.
// Returns PatternScore::FIVE for capture win, -FIVE for capture loss.
int evaluate(const Board& board, Stone color);

} // namespace gomoku
