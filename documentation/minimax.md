# Minimax : Alpha-Beta avec Negamax

Explication de l'algorithme de base.

## Fichier : `src/minimax/core/negamax.cpp`

## Principe du Negamax

En negamax, le score est toujours du point de vue du joueur courant.
Quand on descend d'un niveau, on inverse le signe :

```
score pour moi = -(score pour l'adversaire)
```

Une seule fonction fait le travail des deux joueurs :

```cpp
score = -alpha_beta(board, opponent, depth-1, -beta, -alpha, ...)
//      ^                                      ^      ^
//      |                                      |      inversion
//      negation du resultat                   inversion + swap
```

## Alpha et Beta

- `alpha` : le meilleur score que je suis garanti d'obtenir (borne basse)
- `beta` : le score au-dela duquel l'adversaire ne me laissera pas aller

## Deroulement d'un noeud (`alpha_beta`)

```
1. Verifications terminales
   - L'adversaire a gagne ? (cinq, captures) → return -FIVE
   - J'ai gagne ? → return +FIVE

2. Depth 0 → quiescence (coups forcants seulement)

3. TT probe → position deja vue ? return le score sauvegarde

4. Pruning pre-recherche (voir pruning.md)

5. Generation et tri des coups

6. Boucle sur les coups :
   - Futility pruning → skip si coup inutile
   - LMP → skip si coup tardif a faible depth
   - Jouer le coup sur le plateau
   - PVS + LMR → recherche recursive
   - Annuler le coup
   - score >= beta → CUTOFF (break)
   - score > alpha → nouveau meilleur score

7. TT store → sauvegarder le resultat
```

## Beta Cutoff

Quand un coup donne un score >= beta, l'adversaire (au niveau au-dessus)
a deja une meilleure option ailleurs. Inutile de continuer → on coupe.

```
Noeud (alpha=10, beta=50)
├─ Coup A → score = 20 → alpha = 20, on continue
├─ Coup B → score = 60 >= beta(50) → CUTOFF, on arrete
├─ Coup C → jamais explore (coupe)
└─ Coup D → jamais explore (coupe)
```

## Quiescence

A depth 0, on ne s'arrete pas brutalement. On continue a explorer les
coups forcants (cinq, quatre, captures gagnantes) pour eviter l'effet
d'horizon. Le stand-pat (evaluation statique) sert de borne basse :
meme sans jouer, le joueur a au moins ce score.
