// Optimisations de l'alpha-beta -- fonctions de pruning et heuristiques
//
// Chaque technique est isolee dans sa propre fonction pour lisibilite.
// Toutes sont des methodes de WorkerSearcher, appelees depuis alpha_beta().
//
// Pre-recherche :
//   try_rfp       : Reverse Futility Pruning (eval >> beta → couper)
//   try_razoring  : Razoring (eval << alpha → quiescence)
//   try_nmp       : Null Move Pruning (passer son tour)
//   try_iid       : Internal Iterative Deepening (mini-recherche pour TT move)
//
// Gestion des coups :
//   compute_max_moves      : nombre de coups adaptatif
//   compute_futility_margin: marge futility par profondeur
//
// Pruning dans la boucle :
//   should_futility_prune  : ignorer coups calmes quand eval + marge <= alpha
//   should_lmp             : ignorer coups tardifs a faible profondeur
//   compute_lmr_reduction  : reduction logarithmique pour coups tardifs
//   search_pvs             : PVS + LMR pour un coup (dispatch complet)
//
// Post-cutoff :
//   update_cutoff_heuristics : killer moves, history, countermove

#include "../minimax_internal.hpp"

namespace gomoku {

// =========================================================================
// try_rfp -- Reverse Futility Pruning
// =========================================================================
// Si l'evaluation statique depasse beta d'une marge proportionnelle a la
// profondeur, on coupe immediatement : l'adversaire aurait deja coupe.

bool WorkerSearcher::try_rfp(
    int8_t depth, bool non_terminal,
    int32_t static_eval, int32_t beta, int32_t& score) {

    if (depth <= 3 && non_terminal
        && static_eval - PatternScore::OPEN_THREE * static_cast<int32_t>(depth) >= beta) {
        score = static_eval;
        return true;
    }
    return false;
}

// =========================================================================
// try_razoring -- Razoring
// =========================================================================
// Si l'evaluation est tres en dessous d'alpha, on tombe directement en
// quiescence. Si meme la quiescence ne remonte pas au-dessus d'alpha,
// on coupe.

bool WorkerSearcher::try_razoring(
    Board& board, Stone color, int8_t depth,
    bool non_terminal, int32_t static_eval,
    int32_t alpha, int32_t beta, Pos last_move,
    uint64_t hash, int32_t& score) {

    if (depth <= 3 && non_terminal
        && static_eval + PatternScore::OPEN_THREE * static_cast<int32_t>(depth) <= alpha) {
        int32_t qs_score = quiescence(board, color, alpha, beta, last_move, 0, hash);
        if (qs_score <= alpha) {
            score = qs_score;
            return true;
        }
    }
    return false;
}

// =========================================================================
// try_nmp -- Null Move Pruning
// =========================================================================
// On simule "passer son tour" avec une recherche reduite. Si meme en
// passant le score reste >= beta, la position est si bonne qu'on coupe.
// Verification : a haute profondeur, on confirme avec une vraie recherche.

bool WorkerSearcher::try_nmp(
    Board& board, Stone color, int8_t depth,
    bool non_terminal, int32_t static_eval,
    int32_t alpha, int32_t beta, Pos last_move,
    uint64_t hash, bool allow_null, int32_t& score) {

    if (!allow_null || depth < 3 || !non_terminal
        || static_eval < beta
        || is_threatened(board, color, last_move)) {
        return false;
    }

    int8_t r = static_cast<int8_t>(2 + depth / 6);
    int8_t null_depth = std::max(int8_t(depth - 1 - r), int8_t(0));

    uint64_t null_hash = shared->zobrist.toggle_side(hash);
    int32_t null_score = -alpha_beta(
        board, opponent(color), null_depth,
        -beta, -(beta - 1), last_move, null_hash, false);

    if (!is_stopped() && null_score >= beta) {
        if (depth <= 6) {
            score = beta;
            return true;
        }
        int32_t verify = alpha_beta(
            board, color, depth - r, alpha, beta, last_move, hash, false);
        if (!is_stopped() && verify >= beta) {
            score = beta;
            return true;
        }
    }
    return false;
}

// =========================================================================
// try_iid -- Internal Iterative Deepening
// =========================================================================
// Si on n'a pas de TT move et la profondeur est suffisante, on lance une
// mini-recherche pour en trouver un. Ameliore l'ordre des coups.

Pos WorkerSearcher::try_iid(
    Board& board, Stone color, int8_t depth,
    int32_t alpha, int32_t beta, Pos last_move,
    uint64_t hash, Pos tt_move) {

    if (!tt_move.is_sentinel() || depth < 6) return tt_move;

    int8_t iid_depth = std::max(int8_t(depth - 4), int8_t(1));
    alpha_beta(board, color, iid_depth, alpha, beta, last_move, hash, false);
    if (!is_stopped()) {
        return shared->tt.get_best_move(hash).value_or(Pos::sentinel());
    }
    return tt_move;
}

// =========================================================================
// compute_max_moves -- nombre de coups adaptatif
// =========================================================================
// En position tactique (menaces imminentes), on explore plus de coups.
// En position calme, on restreint davantage.

size_t WorkerSearcher::compute_max_moves(int8_t depth, bool is_tactical) {
    if (is_tactical) {
        switch (depth) {
            case 0: case 1: return 4;
            case 2: case 3: return 6;
            case 4: case 5: return 8;
            default: return 10;
        }
    } else {
        switch (depth) {
            case 0: case 1: return 3;
            case 2: case 3: return 4;
            case 4: case 5: return 6;
            default: return 8;
        }
    }
}

// =========================================================================
// compute_futility_margin -- marge futility par profondeur
// =========================================================================
// Plus la profondeur est grande, plus la marge pour considerer qu'un coup
// est "futile" (inutile) doit etre large.

int32_t WorkerSearcher::compute_futility_margin(int8_t depth) {
    switch (depth) {
        case 1: return PatternScore::CLOSED_FOUR;
        case 2: return PatternScore::OPEN_FOUR;
        case 3: return PatternScore::OPEN_FOUR + PatternScore::OPEN_THREE;
        default: return PatternScore::OPEN_FOUR * 2;
    }
}

// =========================================================================
// should_futility_prune -- test futility pour un coup
// =========================================================================
// Un coup "calme" (score < 800000) est ignore si eval + bonus optimiste
// ne peut toujours pas depasser alpha (le meilleur score deja garanti).

bool WorkerSearcher::should_futility_prune(
    bool futility_ok, size_t move_index,
    int32_t static_eval, int32_t futility_margin,
    int32_t alpha, int32_t move_score) {

    return futility_ok && move_index > 0
        && static_eval + futility_margin <= alpha
        && move_score < 800000;
}

// =========================================================================
// should_lmp -- Late Move Pruning
// =========================================================================
// A faible profondeur, les coups calmes arrives tard dans l'ordre de tri
// ont tres peu de chances d'etre meilleurs. On les ignore.

bool WorkerSearcher::should_lmp(
    size_t move_index, int8_t depth, int32_t move_score) {

    return move_index > 0 && depth <= 3
        && move_index >= (3 + static_cast<size_t>(depth) * 2)
        && move_score < 800000;
}

// =========================================================================
// compute_lmr_reduction -- reduction LMR
// =========================================================================
// Les coups tardifs non-tactiques sont explores a profondeur reduite.
// La reduction est logarithmique : sqrt(depth) * sqrt(move_index) / 2.
// Les coups tres faibles recoivent une reduction supplementaire.

int8_t WorkerSearcher::compute_lmr_reduction(
    int8_t depth, size_t move_index,
    int32_t move_score, bool is_capture, int8_t extension) {

    if (is_capture || extension > 0 || depth < 2 || move_index < 1)
        return 0;

    float d = static_cast<float>(depth);
    float m = static_cast<float>(move_index);
    int8_t r_val = static_cast<int8_t>(std::sqrt(d) * std::sqrt(m) / 2.0f);

    if (move_score < 500000)      r_val += 2;
    else if (move_score < 800000) r_val += 1;

    return std::clamp(r_val, int8_t(1), int8_t(depth - 2));
}

// =========================================================================
// search_pvs -- Principal Variation Search + LMR
// =========================================================================
// Dispatch complet de la recherche pour un coup :
// - 1er coup : fenetre complete [-beta, -alpha]
// - Coups suivants : fenetre nulle d'abord, re-search si necessaire
// - LMR : reduction pour les coups tardifs, re-search si score > alpha

int32_t WorkerSearcher::search_pvs(
    Board& board, Stone color, int8_t depth,
    int32_t alpha, int32_t beta, Pos mov,
    uint64_t child_hash, size_t move_index,
    int32_t move_score, bool is_capture, int8_t extension) {

    Stone opp = opponent(color);

    // Premier coup : fenetre complete.
    if (move_index == 0) {
        return -alpha_beta(
            board, opp, depth - 1 + extension,
            -beta, -alpha, mov, child_hash, true);
    }

    // LMR : reduction pour les coups tardifs.
    int8_t reduction = compute_lmr_reduction(
        depth, move_index, move_score, is_capture, extension);

    int8_t search_depth = std::max(
        int8_t(depth - 1 + extension - reduction), int8_t(0));

    // PVS : fenetre nulle d'abord.
    int32_t score = -alpha_beta(
        board, opp, search_depth,
        -(alpha + 1), -alpha, mov, child_hash, true);

    // Re-search pleine profondeur si LMR a reduit et score > alpha.
    if (!is_stopped() && reduction > 0 && score > alpha) {
        score = -alpha_beta(
            board, opp, depth - 1 + extension,
            -(alpha + 1), -alpha, mov, child_hash, true);
    }

    // Re-search fenetre complete si score entre alpha et beta.
    if (!is_stopped() && score > alpha && score < beta) {
        score = -alpha_beta(
            board, opp, depth - 1 + extension,
            -beta, -alpha, mov, child_hash, true);
    }

    return score;
}

// =========================================================================
// update_cutoff_heuristics -- mise a jour apres beta cutoff
// =========================================================================
// Quand un coup cause un cutoff, on retient :
// - Killer moves : les 2 derniers coups qui ont coupe a ce ply
// - History : bonus quadratique (depth^2) pour ce coup
// - Countermove : meilleure reponse au dernier coup adverse

void WorkerSearcher::update_cutoff_heuristics(
    Pos mov, Stone color, int8_t depth, Pos last_move, size_t move_index) {

    stats.beta_cutoffs += 1;
    if (move_index == 0) stats.first_move_cutoffs += 1;

    // Killer moves
    auto ply = static_cast<size_t>(std::max(int8_t(max_depth - depth), int8_t(0)));
    if (ply < 64) {
        if (killer_moves[ply][0] != mov) {
            killer_moves[ply][1] = killer_moves[ply][0];
            killer_moves[ply][0] = mov;
        }
    }

    // History
    int cidx = (color == Stone::Black) ? 0 : 1;
    history[cidx][mov.row][mov.col] +=
        static_cast<int32_t>(depth) * static_cast<int32_t>(depth);

    // Countermove
    int opp_idx = (color == Stone::Black) ? 1 : 0;
    countermove[opp_idx][last_move.row][last_move.col] = mov;
}

} // namespace gomoku
