# Pruning : Optimisations de la Recherche

Chaque technique coupe des branches de l'arbre pour aller plus vite
sans perdre en qualite de jeu.

## Fichier : `src/minimax/core/pruning.cpp`

---

## 1. Reverse Futility Pruning (RFP)

**Principe** : si l'evaluation statique depasse largement beta, on coupe.

```
Condition : depth <= 3 et eval - marge >= beta
Action   : return eval (pas besoin de chercher)
```

Exemple : beta = 100, eval = 500 → l'adversaire a deja mieux ailleurs,
cette branche ne sera jamais choisie.

---

## 2. Razoring

**Principe** : si l'evaluation est tres en dessous d'alpha, on tombe
directement en quiescence pour verifier.

```
Condition : depth <= 3 et eval + marge <= alpha
Action   : quiescence. Si toujours <= alpha → return
```

Exemple : alpha = 100, eval = -50 → meme avec un bonus optimiste,
on ne depassera pas alpha. On verifie en quiescence.

---

## 3. Null Move Pruning (NMP)

**Principe** : on simule "passer son tour" avec une recherche reduite.
Si meme en passant, le score reste >= beta, la position est si bonne
qu'on coupe.

```
Condition : depth >= 3, eval >= beta, pas de menace immediate
Action   : recherche reduite sans jouer. Si >= beta → return beta
           A haute profondeur, verification avec une vraie recherche
```

---

## 4. Internal Iterative Deepening (IID)

**Principe** : si on n'a pas de TT move et la profondeur est suffisante,
on lance une mini-recherche pour en trouver un.

```
Condition : pas de TT move et depth >= 6
Action   : recherche a depth-4, puis lire le TT move
```

Ameliore l'ordre des coups quand la TT n'a pas encore de resultat.

---

## 5. Futility Pruning

**Principe** : un coup calme est ignore si meme avec un bonus optimiste,
il ne peut pas depasser alpha.

```
Condition : depth <= 4 et eval + bonus <= alpha et coup calme (score < 800000)
Action   : continue (sauter ce coup)
```

Ne s'applique jamais au premier coup ni aux coups tactiques.

---

## 6. Late Move Pruning (LMP)

**Principe** : les coups calmes arrives tard dans l'ordre de tri ont
tres peu de chances d'etre meilleurs. On les ignore.

```
Condition : depth <= 3, coup tardif (index >= 3 + depth*2), coup calme
Action   : continue (sauter ce coup)
```

---

## 7. Late Move Reduction (LMR)

**Principe** : les coups tardifs non-tactiques sont explores a profondeur
reduite. Si le resultat est prometteur, on re-search a pleine profondeur.

```
Condition : depth >= 2, pas une capture, pas d'extension
Reduction : sqrt(depth) * sqrt(index) / 2 (+ bonus si coup tres faible)
Action   : recherche reduite. Si score > alpha → re-search complete
```

---

## 8. Principal Variation Search (PVS)

**Principe** : le premier coup (suppose meilleur) est cherche avec la
fenetre complete. Les suivants sont testes avec une fenetre nulle (taille 1)
pour verifier rapidement s'ils sont meilleurs.

```
Coup 0 : fenetre complete [-beta, -alpha]
Coup N : fenetre nulle [-(alpha+1), -alpha]
         Si score > alpha → re-search complete [-beta, -alpha]
```

---

## Resume des cutoffs dans le code

| Technique | Ou dans le code | Type de cutoff |
|---|---|---|
| RFP | `negamax.cpp:220` → `return` | Tout le noeud |
| Razoring | `negamax.cpp:222` → `return` | Tout le noeud |
| NMP | `negamax.cpp:225` → `return` | Tout le noeud |
| TT probe | `negamax.cpp:209` → `return` | Tout le noeud |
| Futility | `negamax.cpp:265` → `continue` | Une branche |
| LMP | `negamax.cpp:268` → `continue` | Une branche |
| Beta cutoff | `negamax.cpp:298` → `break` | Toutes les branches restantes |
