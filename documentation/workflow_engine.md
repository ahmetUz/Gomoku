# Workflow Engine

Comment l'engine decide du meilleur coup a jouer.

## Fichier : `src/engine/engine.cpp`

## Etapes

1. **Coups immediats** : avant de lancer la recherche, l'engine verifie :
   - Y a-t-il un coup gagnant immediat ? (cinq en ligne, capture win)
   - L'adversaire a-t-il une menace immediate a bloquer ?
   - Si oui, on joue ce coup sans recherche minimax

2. **Recherche minimax** : si aucun coup immediat, on lance `search_timed()`
   - On passe le plateau, la couleur, la profondeur max, et le temps limite
   - La recherche retourne le meilleur coup avec son score

3. **Validation** : le coup retourne est verifie contre les regles
   - Pas de coup interdit (double trois pour Noir en Ninuki)
   - Pas de coup sur une case occupee

## Interface

```cpp
SearchResult search_timed(
    const Board& board,     // position courante
    Stone color,            // qui joue
    int8_t max_depth,       // profondeur max
    uint64_t time_limit_ms  // temps limite en ms
);
```

Le `SearchResult` contient :
- `best_move` : le meilleur coup trouve
- `score` : le score du coup
- `depth` : la profondeur atteinte
- `nodes` : nombre de noeuds explores
- `stats` : statistiques (cutoffs, TT hits, etc.)
