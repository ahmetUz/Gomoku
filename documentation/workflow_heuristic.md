# Workflow Heuristic

Comment l'evaluation statique score une position.

## Fichier : `src/eval/heuristic.cpp`

## Vue d'ensemble

```
evaluate(board, color)
  |
  ├─ Victoire/defaite par captures ?
  ├─ capture_score()                    → pierres capturees
  ├─ game_phase()                       → phase de jeu (debut/milieu/fin)
  |
  ├─ evaluate_color(board, color, phase)
  |   ├─ score_alignments()             → motifs alignes (cinq, quatre, trois...)
  |   ├─ score_position()               → bonus de centre
  |   ├─ score_connectivity_and_vulnerability() → adjacence + paires capturables
  |   └─ score_combinations()           → menaces combinees (double quatre, etc.)
  |
  ├─ evaluate_color(board, opponent, phase)   → meme chose pour l'adversaire
  |
  └─ return (mon_score - score_adversaire) + captures - vulnerabilite
```

## Fonctions

### score_alignments()

Scanne les 4 directions (horizontal, vertical, 2 diagonales) pour chaque
pierre. Detecte les motifs :

| Motif | Exemple | Score |
|---|---|---|
| Five | XXXXX | 1 000 000 |
| Open four | _XXXX_ | 100 000 |
| Closed four | OXXXX_ | 50 000 |
| Open three | _XXX_ | 10 000 |
| Closed three | OXXX_ | 1 500 |
| Open two | _XX_ | 1 000 |
| Closed two | OXX_ | 200 |

Chaque motif est pese selon sa **liberte** (open_ends) :
- 2 bouts ouverts (free) → score eleve
- 1 bout ouvert (half-free) → score moyen
- 0 bout ouvert (flanked) → score nul

Detecte aussi les **trous** (gap) : XX_X est score comme un quatre
car remplir le trou complete la ligne.

### score_position()

Bonus Manhattan par rapport au centre. Plus une pierre est proche du
centre, plus elle a de liberte directionnelle.

### score_connectivity_and_vulnerability()

**Connectivite** : bonus de 160 par paire de pierres alliees adjacentes.

**Vulnerabilite** : compte les paires capturables par l'adversaire
(patterns _XXO ou OXX_). Penalisee dans evaluate() avec un poids
croissant selon le nombre de captures adverses :
- 0-1 captures : x10 000
- 2 captures : x20 000
- 3 captures : x40 000
- 4 captures : x80 000

### score_combinations()

Detecte les combinaisons de menaces souvent imblocables :
- Quatre ouvert + autre menace
- Double quatre ferme
- Quatre ferme + trois ouvert
- Double trois ouvert
- Bonus pour developpement multi-directionnel (open twos)

### game_phase()

Ajuste les poids selon la phase de la partie (basee sur les pierres
posees et captures effectuees) :

| Phase | Centre | Patterns | Combinaisons |
|---|---|---|---|
| Debut (phase ~0) | x1.5 | x0.8 | x0.7 |
| Milieu (phase ~0.5) | x1.0 | x0.9 | x0.85 |
| Fin (phase ~1.0) | x0.5 | x1.0 | x1.0 |

C'est la partie **dynamique** de l'heuristique : les poids changent
en fonction des actions passees des deux joueurs.
