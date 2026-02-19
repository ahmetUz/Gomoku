# Search Layer

## Notions

### Negamax Alpha-Beta
Recherche arborescente avec élagage alpha-beta. Convention negamax : le score est toujours du point de vue du joueur courant.

### Iterative Deepening
Recherche depth 1, puis 2, puis 3... jusqu'au timeout. Avantages :
- Meilleur move ordering à chaque itération (PV de l'itération précédente)
- Contrôle naturel du temps

### Aspiration Windows
Au lieu de chercher [-∞, +∞], on cherche [score_précédent - delta, score_précédent + delta]. Si le score sort de la fenêtre → re-search élargie. Réduit l'arbre de recherche.

### PVS (Principal Variation Search)
Premier coup cherché avec fenêtre complète, les suivants avec fenêtre nulle [alpha, alpha+1]. Si le score > alpha → re-search complète. Exploite le fait que le premier coup (grâce au move ordering) est souvent le meilleur.

### LMR (Late Move Reductions)
Les coups tardifs dans la liste ordonnée sont cherchés à profondeur réduite. Si le score est surprenant → re-search à profondeur complète.

### NMP (Null Move Pruning)
On "passe son tour" et cherche à profondeur réduite. Si l'adversaire ne peut pas exploiter ce tour gratuit (score ≥ beta) → cutoff. Ne s'applique pas en VCF/quiescence.

### IID (Internal Iterative Deepening)
Quand la TT ne donne pas de best move, on fait une recherche rapide à profondeur réduite pour en trouver un.

### Transposition Table (tt.hpp/cpp)
- **AtomicTT** : table lock-free pour Lazy SMP. Trick XOR de Hyatt (1994) : stocke `key ^ data` pour détecter les torn reads
- **Packing 42-bit** : depth(8) + score(21) + type(2) + has_move(1) + row(5) + col(5)
- Types d'entrée : Exact, LowerBound (fail-high), UpperBound (fail-low)

### Zobrist Hashing (zobrist.hpp/cpp)
Hash incrémental via XOR de clés aléatoires pré-calculées (LCG déterministe, constantes de Knuth MMIX).
- Une clé par (position, couleur)
- Clé side-to-move (toggle à chaque coup)
- Clés capture_count (car même position + captures différentes = état différent)
- Mise à jour O(1) : `hash ^= old_key ^ new_key`

### Threat Space Search (threat.hpp/cpp)
- **VCF** (Victory by Continuous Fours) : cherche une séquence de fours forcés menant à un five. Chaque coup force une réponse.
- **VCT** (Victory by Continuous Threats) : plus large que VCF, inclut les threes ouverts. Cherche si une séquence de menaces mène à un VCF ou un five.
- Budget limité (nodes) pour éviter l'explosion combinatoire.

### Lazy SMP
Parallélisme : N threads cherchent la même position avec des profondeurs légèrement différentes. Pas de synchronisation — juste une TT atomique partagée. La diversité naturelle des ordres d'exploration suffit.

### MoveList (struct stack-allocated)
Remplace `std::vector<pair<Pos, int32_t>>` dans les hot paths. 128 entrées max, 1KB sur la stack, zéro allocation heap.

## Code notable

- `generate_moves_ordered()` (`src/search/alphabeta.cpp`) : tri des coups par score heuristique, killer moves, countermove, TT move
- Killer moves et countermove utilisent le **sentinel pattern** `Pos(255,255)` au lieu de `std::optional`
- `find_five_line_at_pos()` utilisé dans VCF/VCT au lieu de `find_five_positions()` (on connaît déjà la position du five)
