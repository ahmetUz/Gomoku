# Workflow Principal

Flux global du programme, de l'entree utilisateur au coup joue.

## Vue d'ensemble

```
main.cpp
  |
  v
Engine (engine.cpp)
  |
  v
Searcher (iterative_deepening.cpp)
  |
  v
alpha_beta (negamax.cpp)
  |
  v
evaluate (heuristic.cpp)
```

## Etapes

1. **main.cpp** : point d'entree, boucle de jeu (CLI ou GUI)
2. **Engine** : recoit la position courante, decide du meilleur coup
   - Verifie les coups immediats (victoire, blocage)
   - Lance la recherche minimax si necessaire
3. **Searcher** : recherche parallele (Lazy SMP)
   - Lance N threads sur la meme position
   - Chaque thread fait un iterative deepening (depth 1, 2, 3...)
   - Les threads partagent une transposition table (TT)
4. **alpha_beta** : negamax recursif avec elagage
   - Genere et trie les coups candidats
   - Explore l'arbre avec pruning (RFP, razoring, NMP, LMR, futility)
   - Appelle quiescence en fin d'arbre
5. **evaluate** : score statique de la position
   - Alignements, position, connectivite, vulnerabilite, combinaisons
   - Poids ajustes selon la phase de jeu

## Fichiers cles

| Fichier | Role |
|---|---|
| `src/main.cpp` | Point d'entree |
| `src/engine/engine.cpp` | Orchestration du coup |
| `src/minimax/core/iterative_deepening.cpp` | Lazy SMP + iterative deepening |
| `src/minimax/core/negamax.cpp` | alpha_beta, quiescence |
| `src/minimax/core/pruning.cpp` | Optimisations de recherche |
| `src/minimax/ordering/move_ordering.cpp` | Tri des coups |
| `src/eval/heuristic.cpp` | Evaluation statique |
| `src/rules/*.cpp` | Regles du jeu (captures, victoire, coups interdits) |
