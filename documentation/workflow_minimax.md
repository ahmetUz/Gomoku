# Workflow Minimax

Comment la recherche explore l'arbre de jeu.

## Fichiers : `src/minimax/core/`

## Vue d'ensemble

```
search_timed (Lazy SMP, N threads)
  └─ search_iterative (depth 1, 2, 3...)
       └─ search_with_aspiration (fenetre etroite)
            └─ search_root (PVS a la racine)
                 └─ alpha_beta (negamax recursif)
                      └─ quiescence (coups forcants)
```

## 1. Lazy SMP (`iterative_deepening.cpp`)

On lance N threads en parallele (max 8). Chaque thread fait sa propre
recherche independante, mais ils partagent la transposition table (TT).

- Thread 0 : commence a depth 1 (history heritee)
- Thread 1 : commence a depth 2 (history vide)
- Thread 2 : commence a depth 3, etc.

Les threads explorent des branches differentes (killers/history differents)
et remplissent la TT avec des positions variees. Chaque thread beneficie
des resultats des autres via la TT.

## 2. Iterative Deepening (`iterative_deepening.cpp`)

On cherche d'abord a depth 1, puis 2, puis 3... Chaque iteration
remplit la TT et les heuristiques de tri. On s'arrete quand :
- Le temps est ecoule
- Une victoire/defaite est confirmee sur 2 profondeurs consecutives

## 3. Aspiration Window (`iterative_deepening.cpp`)

Au lieu de chercher avec [-infini, +infini], on utilise une fenetre
etroite [score_precedent - 100, score_precedent + 100]. Si le vrai
score est en dehors, on reouvre la fenetre.

## 4. PVS a la racine (`iterative_deepening.cpp`)

- Coup 0 (suppose meilleur) : fenetre complete [-beta, -alpha]
- Coups suivants : fenetre nulle [-(alpha+1), -alpha]
  - Si score > alpha : re-search complete pour le vrai score
  - Si score <= alpha : coup moins bon, on passe

## 5. Move Ordering (`ordering/move_ordering.cpp`)

Le tri des coups est crucial pour maximiser les cutoffs :

| Priorite | Heuristique | Score |
|---|---|---|
| 1 | TT move (recherche precedente) | 1 000 000 |
| 2 | Killer move #1 (cutoff a ce ply) | 500 000 |
| 3 | Killer move #2 | 490 000 |
| 4 | Countermove (reponse au dernier coup) | 400 000 |
| 5 | Analyse tactique (menaces) | variable |
| 6 | History table | variable |
