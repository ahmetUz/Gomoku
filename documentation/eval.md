# Evaluation Layer

## Notions

### Patterns de ligne (patterns.hpp/cpp)
Scoring des configurations de pierres alignées. Chaque ligne est évaluée selon :
- **Nombre de pierres** (2, 3, 4, 5)
- **Ouverture** : open (deux côtés libres) vs half-open (un côté bloqué) vs closed
- **Gaps** : trou dans la ligne (ex: XX_XX) — compté comme configuration distincte

Scores typiques : FIVE=100000, OPEN_FOUR=15000, OPEN_THREE=1500, HALF_OPEN_FOUR=3000...

### Heuristic (heuristic.cpp)
Évaluation single-pass d'une position pour un joueur donné :
1. **Line-start filter** : ne compte chaque ligne qu'une fois (démarre uniquement si la case précédente n'est pas de la même couleur)
2. **Pattern scoring** : identifie la longueur, l'ouverture, et les gaps
3. **Position bonus** : pondération par centralité (centre du plateau vaut plus)
4. **Connectivity** : bonus pour pierres adjacentes alliées
5. **Vulnerability** : malus pour pierres exposées aux captures

### Évaluation complète
`evaluate()` = score_joueur - score_adversaire + bonus_captures + bonus_combinaisons
- **Combo bonuses** : open_three + half_open_four ensemble vaut un bonus supplémentaire
- Le double-counting des gap patterns est un artefact symétrique connu (n'affecte pas la comparaison)

## Code notable

- `evaluate_line()` (`src/eval/heuristic.cpp`) : cœur du pattern matching, gère les gaps et l'ouverture en un seul scan
- `CAPTURE_THREAT` et `CAPTURE_PAIR` dans `patterns.hpp` sont des constantes mortes (jamais utilisées)
