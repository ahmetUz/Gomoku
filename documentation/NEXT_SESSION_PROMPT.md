# Contexte pour la reprise de session — Gomoku AI (42)

Colle ce prompt au début de la prochaine session.

---

## Projet

Gomoku AI en C++ (traduit de Rust), projet 42. Ninuki-renju (Pente-style) sur plateau 19x19. Branche git : `kenan`.

## Ce qu'on a fait ensemble

### Revue de code complète (Phases 1-4 terminées)
On a passé en revue **tout le codebase** avec 3 objectifs :
1. Fix/improve code quality et performance
2. Comprendre la structure en détail
3. Maîtriser les concepts algorithmiques pour l'évaluation

### Optimisations appliquées
- **BitboardIterator** : itération par pointeur `const uint64_t*` au lieu d'index (élimine multiplication)
- **Branchless** : `opponent()` via XOR, `get()`, `is_empty()` sans branches
- **Sentinel pattern** : `Pos(255,255)` remplace `optional<Pos>` pour killer_moves, countermove, TT best_move
- **MoveList struct** : stack-allocated (128 entries, 1KB) remplace `std::vector` dans `generate_moves_ordered`, `alpha_beta`, `quiescence`
- **has_five_in_row** : réécrit pour utiliser `has_five_at_pos` (zero-alloc) au lieu de `find_five_positions` (heap-alloc par direction)
- **threat.cpp** : `find_five_line_at_pos(board, threat_move, color)` au lieu de `find_five_positions(board, color)` dans VCF/VCT
- **zobrist.cpp** : fix warnings `stone_hash` non initialisé
- **VCT sub_sequence** : fix copie au lieu de référence + logging conditionnel

### Bug fix critique
- **`check_winner` temporal rule** : l'ancien code était purement statique — un five cassable n'était JAMAIS déclaré gagnant. Nouveau prototype `check_winner(board, last_player)` avec logique : si l'adversaire a un five et que last_player ne l'a pas cassé → l'adversaire gagne. Si last_player forme un five cassable → jeu continue (1 tour pour casser).

### État des tests
- **193 tests, 0 échecs**, 0 warnings de compilation
- 17 tests win dont 2 nouveaux tests temporels

## Ce qu'il reste à faire (par priorité)

### 1. Revue des tests (demandé par l'utilisateur)
Les tests n'avaient pas attrapé le bug `check_winner`. Passer en revue la couverture des tests, en particulier :
- Tests d'intégration win conditions (scénarios de jeu réalistes)
- Tests de capture + five interaction
- Tests forbidden moves edge cases

### 2. Performance (demandé par l'utilisateur — priorité haute)
Le temps de réponse moyen est proche de 0.5s, l'utilisateur veut mieux. Pistes :
- Profiler pour identifier les bottlenecks réels
- Move generation : `generate_moves_ordered` est le hot path
- Évaluation : `evaluate()` appelé à chaque nœud feuille
- TT hit rate : vérifier l'efficacité de la transposition table
- Lazy SMP scaling : vérifier que les threads aident vraiment

### 3. Revue Phase 5 (Engine + GUI) — pas encore faite
- `engine.cpp` : pipeline décisionnel, time management
- `game_controller.cpp` : logique de jeu côté GUI
- `gui_main.cpp` / `renderer.cpp` : rendu SFML

## Instructions de l'utilisateur (à respecter)

- **Prendre du recul** : toujours évaluer l'impact d'une modification sur le reste du programme
- **Œil critique** : guetter les angles d'optimisation ET les incohérences pas évidentes
- **Appliquer immédiatement** : ne pas lister les changements, les faire directement
- **Pas d'over-engineering** : uniquement les changements nécessaires
- **Builder et tester** après chaque batch de modifications
- L'utilisateur parle français, répondre en français

## Fichiers clés

- `src/search/alphabeta.cpp` (~1900 lignes) — cœur du moteur de recherche
- `src/rules/win.cpp` — conditions de victoire avec règle temporelle
- `src/eval/heuristic.cpp` — évaluation heuristique single-pass
- `src/search/threat.cpp` — VCF/VCT threat-space search
- `src/engine/engine.cpp` — pipeline IA
- `documentation/` — documentation par module (board, rules, eval, search, engine, gui)

## Build & Test

```bash
cmake --build build 2>&1 | tail -5
./build/tests/gomoku_tests
```
