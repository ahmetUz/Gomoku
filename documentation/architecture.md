# Gomoku AI — Architecture & Algorithmes

Documentation complète du flux d'exécution, des algorithmes implémentés et de leur mapping vers le code source.

---

## Table des matières

1. [Vue d'ensemble du flux d'exécution](#1-vue-densemble-du-flux-dexécution)
2. [Représentation du plateau](#2-représentation-du-plateau)
3. [Règles du jeu](#3-règles-du-jeu)
4. [Évaluation statique](#4-évaluation-statique)
5. [Hachage Zobrist](#5-hachage-zobrist)
6. [Table de transposition](#6-table-de-transposition)
7. [Recherche de menaces (VCF/VCT)](#7-recherche-de-menaces-vcfvct)
8. [Recherche Alpha-Beta](#8-recherche-alpha-beta)
9. [Moteur (pipeline de décision)](#9-moteur-pipeline-de-décision)
10. [Interface graphique & CLI](#10-interface-graphique--cli)
11. [Graphe d'appel complet](#11-graphe-dappel-complet)

---

## 1. Vue d'ensemble du flux d'exécution

Quand l'IA doit jouer, voici le chemin complet :

```
L'utilisateur joue (GUI clic / CLI notation)
    |
    v
GameController / terminal game loop
    |
    v
AIEngine::get_move_with_stats(board, color)
    |
    |-- Stage 0 : Opening book (coups 1-3)
    |-- Stage 0.5 : Casser un cinq adverse existant
    |-- Stage 1 : Victoire immédiate (5 en ligne / 5e capture)
    |-- Stage 2 : Bloquer la victoire immédiate adverse
    |-- Stage 3 : VCF offensif (victoire forcée par fours)
    |-- Stage 4 : VCF défensif (bloquer la victoire forcée adverse)
    |-- Stage 5 : Recherche Alpha-Beta complète
    |
    v
Le coup est exécuté sur le plateau
```

Chaque stage est un **filtre en cascade** : si un stage trouve un coup, les stages suivants sont ignorés. Cela garantit que les situations tactiques urgentes sont traitées en priorité, sans perdre de temps dans l'alpha-beta.

---

## 2. Représentation du plateau

### Concept : Bitboard

Au lieu de stocker le plateau dans un tableau `Stone[19][19]` (361 octets), on utilise des **bitboards** : chaque couleur a son propre tableau de 6 entiers 64 bits (6 x 64 = 384 bits >= 361 cases). Chaque bit représente une case : 1 = pierre présente, 0 = vide.

**Avantages :**
- Compter les pierres = `popcount` sur 6 mots (6 instructions CPU)
- Tester une case = un shift + un AND (1 instruction)
- Itérer les pierres = parcourir uniquement les bits à 1 (sauter les cases vides)

### Structure

```
Board
  ├── black : Bitboard (6 x uint64)     -- pierres noires
  ├── white : Bitboard (6 x uint64)     -- pierres blanches
  ├── black_captures : uint8             -- paires capturées par noir (0-5)
  └── white_captures : uint8             -- paires capturées par blanc (0-5)
```

### Mapping code

| Concept | Fichier |
|---------|---------|
| Bitboard (set/clear/get/count/iterate) | `include/gomoku/board/bitboard.hpp` + `src/board/bitboard.cpp` |
| BitboardIterator (parcours des bits) | `src/board/bitboard.cpp` |
| Board (place/remove/get/captures) | `include/gomoku/board/board.hpp` |
| Types (Stone, Pos, opponent()) | `include/gomoku/board/types.hpp` |

### L'itérateur de bitboard

Pour parcourir toutes les pierres d'une couleur, l'itérateur utilise `__builtin_ctzll` (count trailing zeros) pour trouver le prochain bit à 1, puis `word &= word - 1` pour l'effacer. C'est O(nombre de pierres) au lieu de O(361).

---

## 3. Règles du jeu

### 3.1 Capture de paires

Règle Pente : le pattern `X-OO-X` capture les deux pierres `O`. Quand on pose une pierre, on scanne 4 axes (horizontal, vertical, 2 diagonales) dans les 2 sens (= 8 rayons). Pour chaque rayon : si les 2 cases suivantes sont adverses et la 3e est notre couleur, capture.

**Deux implémentations :**
- `execute_captures()` : utilise `std::vector` (allocation heap) — conservée pour les tests
- `execute_captures_fast()` : utilise `CaptureInfo` (tableau fixe sur la stack, max 16 positions) — utilisée en production

`undo_captures()` permet de défaire les captures (repose les pierres, décrémente le compteur).

| Concept | Fichier |
|---------|---------|
| Détection/exécution de captures | `src/rules/capture.cpp` |
| CaptureInfo (structure zero-alloc) | `include/gomoku/rules/capture.hpp` |

### 3.2 Conditions de victoire

Deux façons de gagner :
1. **5 en ligne** (ou plus) de sa couleur, **incassable** par capture
2. **5 paires capturées** (10 pierres adverses au total)

Le concept de "cinq cassable" est central : si un adversaire peut capturer une paire qui fait partie du cinq, le cinq ne compte pas comme victoire. Le jeu continue. L'IA vérifie cela avec `can_break_five_by_capture()`.

Subtilité : un cinq peut être "cassable" en théorie mais **illusoirement cassable** en pratique. Si après chaque capture de rupture possible, le joueur peut reposer et former un cinq incassable, alors toutes les ruptures sont illusoires et le cinq est en fait gagnant. C'est vérifié par `is_illusory_break()`.

| Concept | Fichier |
|---------|---------|
| Détection de 5 en ligne | `src/rules/win.cpp` |
| Vérification de cinq cassable | `src/rules/win.cpp` (`can_break_five_by_capture`) |
| Vérification de cassure illusoire | `src/engine/engine.cpp` (`is_illusory_break`) |
| Détection du gagnant | `src/rules/win.cpp` (`check_winner`) |

### 3.3 Coups interdits (double-three)

Le joueur Noir ne peut pas jouer un coup qui crée **simultanément deux "free threes"** (trois ouvertes). Un three est "free" (libre) s'il a exactement 3 pierres, les deux extrémités ouvertes, et un span <= 4.

**Exception** : si le coup capture une paire, le double-three est autorisé.

Le scan de ligne autorise un "gap" (trou) dans le three : `_X.XX_` est aussi un free three.

| Concept | Fichier |
|---------|---------|
| Scan de ligne (avec gap) | `src/rules/forbidden.cpp` (`scan_line`) |
| Détection free three | `src/rules/forbidden.cpp` (`is_free_three`, `creates_free_three_in_direction`) |
| Double-three / validité du coup | `src/rules/forbidden.cpp` (`is_double_three`, `is_valid_move`) |

---

## 4. Évaluation statique

### Concept

L'évaluation statique est la fonction qui "juge" une position sans chercher plus loin. Elle attribue un **score numérique** : positif = favorable pour le joueur courant, négatif = favorable pour l'adversaire. C'est la "feuille" de l'arbre de recherche.

### Échelle des patterns

Chaque motif sur le plateau a une valeur calibrée :

| Pattern | Score | Signification |
|---------|-------|---------------|
| FIVE | 1 000 000 | Cinq en ligne (victoire) |
| CAPTURE_WIN | 1 000 000 | 5e paire capturée (victoire) |
| OPEN_FOUR | 100 000 | `_XXXX_` — imparable |
| NEAR_CAPTURE_WIN | 80 000 | 4 paires capturées (une de plus = victoire) |
| CLOSED_FOUR | 50 000 | `OXXXX_` — bloquable d'un côté |
| OPEN_THREE | 10 000 | `_XXX_` — deviendra un four ouvert |
| CLOSED_THREE | 1 500 | `OXXX_` — bloquable |
| OPEN_TWO | 1 000 | `_XX_` — potentiel |
| CLOSED_TWO | 200 | `OXX_` — limité |

### Composition de l'évaluation

L'évaluation combine plusieurs composantes pour chaque couleur :

1. **Score de patterns** : pour chaque pierre, scan 4 directions. Filtre "line-start" : on ne compte un segment que si la case précédente n'est pas de notre couleur (évite les doublons). Classifie chaque ligne en pattern.

2. **Bonus de combinaisons** : certaines combinaisons de patterns sont synergiques :
   - Open four + (closed four ou open three) = bonus OPEN_FOUR
   - Double closed four = bonus OPEN_FOUR
   - Closed four + open three = bonus OPEN_FOUR
   - Double/triple/quadruple open two = bonus progressif

3. **Score de captures** : non-linéaire (0→0, 1→5K, 2→7K, 3→20K, 4→80K, 5→1M). L'écart entre 3 et 4 captures est énorme car à 4 captures, un seul coup peut gagner.

4. **Bonus de position** : distance de Manhattan au centre. Les pierres centrales valent plus.

5. **Bonus de connectivité** : +160 par voisin de même couleur.

6. **Pénalité de vulnérabilité** : chaque paire alliée capturable est une faiblesse. La pénalité augmente exponentiellement avec le nombre de captures adverses (10K/20K/40K/80K par paire vulnérable).

Le score final est : `score_nous - score_adversaire + capture_score`. C'est **symétrique** pour le negamax.

| Concept | Fichier |
|---------|---------|
| Évaluation d'une ligne | `src/eval/heuristic.cpp` (`evaluate_line`) |
| Évaluation d'une couleur | `src/eval/heuristic.cpp` (`evaluate_color`) |
| Évaluation globale | `src/eval/heuristic.cpp` (`evaluate`) |
| Score de captures non-linéaire | `src/eval/patterns.cpp` (`capture_score`) |
| Constantes de score | `include/gomoku/eval/patterns.hpp` (`PatternScore`) |

---

## 5. Hachage Zobrist

### Concept

Le hachage Zobrist est une technique pour identifier une position de plateau par un nombre 64 bits. L'idée : à chaque (case, couleur) on associe un nombre aléatoire fixe. Le hash d'une position = XOR de tous les nombres correspondant aux pierres présentes.

**Propriété clé** : le XOR est réversible. Poser une pierre = XOR du nombre. Retirer la même pierre = re-XOR du même nombre (annulation). Cela permet des **mises à jour incrémentales O(1)** au lieu de recalculer le hash complet à chaque coup.

### Composantes du hash

- 361 clés pour les pierres noires (une par case)
- 361 clés pour les pierres blanches
- 1 clé pour le côté au trait (side-to-move)
- 12 clés pour les compteurs de captures (2 couleurs x 6 niveaux 0-5)

### Opérations incrémentales

| Opération | Ce qui est XORé |
|-----------|-----------------|
| Poser une pierre | clé(case, couleur) + clé(side) |
| Capturer une pierre | clé(case, couleur capturée) seulement (pas de toggle side, c'est le même coup) |
| Null move | clé(side) seulement |
| Changement de captures | XOR out ancien compteur, XOR in nouveau |

### Pourquoi c'est important

Sans Zobrist, la table de transposition serait inutile : il faudrait comparer 361 cases pour savoir si deux positions sont identiques. Avec Zobrist, une comparaison de 2 entiers 64 bits suffit.

| Concept | Fichier |
|---------|---------|
| Table Zobrist et opérations | `src/search/zobrist.cpp` |
| Déclarations | `include/gomoku/search/zobrist.hpp` |

---

## 6. Table de transposition

### Concept

La table de transposition (TT) est un **cache de résultats de recherche**. Quand l'alpha-beta évalue une position, il stocke le résultat (score, meilleur coup, profondeur) dans la TT. Si la même position est rencontrée à nouveau (par un autre ordre de coups), le résultat est réutilisé au lieu d'être recalculé.

C'est l'optimisation la plus impactante : elle évite la ré-exploration exponentielle de l'arbre.

### Types d'entrées

| Type | Signification | Quand c'est utilisable |
|------|---------------|------------------------|
| Exact | Le score est exact | Toujours |
| LowerBound | Le vrai score est >= valeur stockée | Si la valeur >= beta (cutoff) |
| UpperBound | Le vrai score est <= valeur stockée | Si la valeur <= alpha (fail-low) |

### Implémentation lock-free (AtomicTT)

Pour le multi-threading (Lazy SMP), une TT classique avec mutex serait trop lente. L'`AtomicTT` utilise le **truc XOR de Hyatt** :

- Deux tableaux parallèles : `keys[]` et `data[]`, tous deux `atomic<uint64_t>`
- Stockage : `key = hash XOR packed_data`
- Lecture : `stored_key XOR stored_data` doit redonner le `hash` cherché
- Si un thread écrit pendant qu'un autre lit, les données sont incohérentes → le XOR ne matchera pas → la lecture est simplement ignorée (miss)

Aucun lock, aucune corruption : les lectures "torn" sont naturellement détectées et ignorées.

### Packing des données

Chaque entrée est compressée en 42 bits dans un `uint64_t` :

```
[depth: 8 bits][score: 21 bits][type: 2 bits][has_move: 1 bit][row: 5 bits][col: 5 bits]
```

### Politique de remplacement

On remplace une entrée si : la case est vide, le hash est le même (mise à jour), ou la nouvelle profondeur est >= l'ancienne.

| Concept | Fichier |
|---------|---------|
| AtomicTT (lock-free, production) | `src/search/tt.cpp` |
| TranspositionTable (single-thread, tests) | `src/search/tt.cpp` |
| Pack/unpack des entrées | `src/search/tt.cpp` |
| TTStats (statistiques d'utilisation) | `src/search/tt.cpp` |
| Déclarations | `include/gomoku/search/tt.hpp` |

---

## 7. Recherche de menaces (VCF/VCT)

### Concept : VCF (Victory by Continuous Fours)

Le VCF est un algorithme qui cherche une **victoire forcée par une séquence de fours**. L'idée : si à chaque coup on crée un "four" (4 pierres alignées avec au moins un bout ouvert), l'adversaire est OBLIGÉ de bloquer. On peut donc enchaîner les fours jusqu'à créer un five incassable.

C'est un arbre **OR-AND** :
- **Noeud OR (attaquant)** : on choisit UN four parmi tous les possibles. Si un seul mène à la victoire, c'est gagné.
- **Noeud AND (défenseur)** : le défenseur a UNE seule réponse forcée (bloquer le four). S'il y a plusieurs défenses possibles, le VCF échoue sur cette branche (la situation n'est plus forcée).

### Algorithme VCF détaillé

```
vcf_search(board, color, depth, sequence):
    Si depth == 0 : échec

    fours = find_four_threats(board, color)    // tous les coups créant un four

    Pour chaque four :
        Poser la pierre
        Exécuter les captures

        Si cinq incassable ou capture-win : VICTOIRE trouvée
        Si cinq cassable : ignorer (l'adversaire le cassera)
        Si une capture libère une case permettant à l'adversaire de gagner : ignorer

        defenses = find_defense_moves(board, four, color)

        Si 0 défenses : VICTOIRE (l'adversaire ne peut pas répondre)
        Si 1 défense :
            L'adversaire joue sa défense unique
            Récursion : vcf_search(board, color, depth-1, sequence)
        Si 2+ défenses : échec (pas forcé)

        Défaire tout (unmake)
```

### Concept : VCT (Victory by Continuous Threats)

Le VCT est plus général : il utilise aussi les **open threes** (trois ouverts) comme menaces, pas seulement les fours. Contre un open three, le défenseur a souvent plusieurs défenses possibles. Le VCT doit donc prouver qu'on gagne contre TOUTES les défenses (vrai noeud AND).

Le VCT essaie d'abord le VCF (plus rapide et plus précis), puis tombe en fallback sur le VCT si le VCF échoue.

### Défenses possibles

Pour un four, les défenses sont :
- Bloquer aux extrémités ouvertes du four
- Capturer une paire qui casse le four (retire une pierre de l'alignement)
- Si le défenseur a 3+ captures : n'importe quelle capture (stratégique, car proche de la capture-win)

| Concept | Fichier |
|---------|---------|
| VCF (recherche récursive) | `src/search/threat.cpp` (`vcf_search`) |
| VCT (recherche récursive) | `src/search/threat.cpp` (`vct_search`) |
| Détection de fours/fives/open threes | `src/search/threat.cpp` (`creates_four`, `creates_five_or_more`, `creates_open_three`) |
| Génération de défenses | `src/search/threat.cpp` (`find_defense_moves`, `find_threat_defenses`) |
| Entry points | `src/search/threat.cpp` (`search_vcf`, `search_vct`) |
| Déclarations | `include/gomoku/search/threat.hpp` |

---

## 8. Recherche Alpha-Beta

C'est le coeur algorithmique du moteur. Le fichier `src/search/alphabeta.cpp` fait ~2000 lignes et contient une dizaine de techniques d'optimisation.

### 8.1 Alpha-Beta de base

L'algorithme alpha-beta est une amélioration du minimax. Au lieu d'explorer TOUT l'arbre, il maintient une **fenêtre [alpha, beta]** :
- **alpha** = le meilleur score que le joueur courant peut garantir
- **beta** = le meilleur score que l'adversaire peut garantir

Si un coup produit un score >= beta, on coupe (**beta cutoff**) : l'adversaire ne permettra jamais cette position. Cela élimine en moyenne la moitié de l'arbre.

Le code utilise la formulation **negamax** : au lieu d'alterner max/min, le score est toujours du point de vue du joueur courant, et on inverse le signe à chaque niveau.

| Concept | Fichier |
|---------|---------|
| Fonction alpha-beta principale | `src/search/alphabeta.cpp` (`alpha_beta`) |
| Recherche à la racine | `src/search/alphabeta.cpp` (`search_root`) |

### 8.2 Iterative Deepening (approfondissement itératif)

Au lieu de chercher directement à profondeur 20, on cherche d'abord à profondeur 1, puis 2, puis 3, etc. Cela semble wasteful mais :

1. **Gestion du temps** : on peut s'arrêter à tout moment et retourner le meilleur coup de la dernière profondeur complète
2. **Meilleur ordonnancement** : le meilleur coup de profondeur N-1 est cherché en premier à profondeur N (via la TT), ce qui améliore drastiquement les coupures alpha-beta
3. **Le coût est faible** : grâce au branching factor, la dernière itération représente ~80% du temps total

**Profondeur minimale garantie** : 6 plies. Avant cette profondeur, pas de cutoff par temps.

**Confirmation de gain/perte** : un score terminal (victoire/défaite) doit être confirmé par 2 profondeurs consécutives avant d'arrêter tôt.

| Concept | Fichier |
|---------|---------|
| Iterative deepening | `src/search/alphabeta.cpp` (`search_iterative`) |

### 8.3 Aspiration Windows (fenêtres d'aspiration)

A partir de la profondeur 3, au lieu de chercher avec la fenêtre [-INF, +INF], on utilise une fenêtre étroite autour du score de la profondeur précédente : `[score - 100, score + 100]`.

- Si le vrai score est dans la fenêtre : beaucoup plus de coupures → recherche plus rapide
- Si le score tombe hors de la fenêtre (fail-low ou fail-high) : on relance avec la fenêtre complète

| Concept | Fichier |
|---------|---------|
| Aspiration windows | `src/search/alphabeta.cpp` (`search_iterative`, constante `ASP_WINDOW = 100`) |

### 8.4 Principal Variation Search (PVS)

Optimisation de la fenêtre de recherche à chaque noeud :
1. Le **premier coup** (supposé le meilleur grâce à l'ordonnancement) est cherché avec la fenêtre complète `[alpha, beta]`
2. Les **coups suivants** sont cherchés avec une **fenêtre nulle** `[alpha, alpha+1]` (scout search)
3. Si un coup suivant dépasse alpha (le scout "fail-high"), on re-cherche avec la fenêtre complète

Intuition : si l'ordonnancement est bon, le premier coup est le meilleur et les autres ne dépassent pas alpha. La fenêtre nulle est très rapide car elle génère un maximum de coupures.

| Concept | Fichier |
|---------|---------|
| PVS | `src/search/alphabeta.cpp` (`search_root` et `alpha_beta`) |

### 8.5 Null Move Pruning (NMP)

Idée : "et si on passait notre tour ?" Si, même en passant, le score est déjà >= beta, alors la position est tellement bonne qu'on peut couper sans chercher tous les coups.

- Réduction adaptive : R = 2 + depth/6
- Recherche avec fenêtre nulle à profondeur réduite
- **Garde** : désactivé si la position est "menacée" (`is_threatened` vérifie : adversaire a 4+ captures, 4+ pierres consécutives, un three ouvert, un pattern de capture)
- **Vérification** : à profondeur > 6, si le NMP réussit, on vérifie avec une recherche à profondeur réduite (évite le zugzwang)

| Concept | Fichier |
|---------|---------|
| Null move pruning | `src/search/alphabeta.cpp` (`alpha_beta`) |
| Détection de position menacée | `src/search/alphabeta.cpp` (`is_threatened`) |

### 8.6 Reverse Futility Pruning (RFP)

A faible profondeur (depth <= 3), si l'évaluation statique est déjà TELLEMENT au-dessus de beta qu'aucune séquence de coups ne pourrait la faire baisser suffisamment, on coupe.

Formule : si `eval - OPEN_THREE * depth >= beta`, retourner eval.

| Concept | Fichier |
|---------|---------|
| Reverse futility pruning | `src/search/alphabeta.cpp` (`alpha_beta`) |

### 8.7 Razoring

Inverse du RFP : à faible profondeur (depth <= 3), si l'évaluation est tellement EN DESSOUS d'alpha qu'on ne pourra probablement pas s'en sortir, on tombe directement en quiescence search au lieu de chercher tous les coups.

Formule : si `eval + OPEN_THREE * depth <= alpha`, retourner quiescence.

| Concept | Fichier |
|---------|---------|
| Razoring | `src/search/alphabeta.cpp` (`alpha_beta`) |

### 8.8 Futility Pruning

A profondeur <= 4, pour les coups non-tactiques (score d'ordonnancement < 800 000) : si `eval + marge <= alpha`, le coup est probablement futile et est ignoré.

Les marges augmentent avec la profondeur : `[CLOSED_THREE, OPEN_THREE, OPEN_FOUR/2, OPEN_FOUR]`.

| Concept | Fichier |
|---------|---------|
| Futility pruning | `src/search/alphabeta.cpp` (`alpha_beta`) |

### 8.9 Late Move Pruning (LMP)

A profondeur <= 3, les coups au-delà de la position `3 + depth*2` dans l'ordonnancement et qui ne sont pas tactiques sont purement ignorés.

Raisonnement : si les N premiers coups (bien ordonnés) n'ont pas suffi, les suivants ont très peu de chance de changer le résultat.

| Concept | Fichier |
|---------|---------|
| Late move pruning | `src/search/alphabeta.cpp` (`alpha_beta`) |

### 8.10 Late Move Reductions (LMR)

Pour les coups non-tactiques au-delà du premier, on réduit la profondeur de recherche. Si le coup est intéressant (dépasse alpha avec profondeur réduite), on re-cherche à profondeur complète.

Formule : `reduction = sqrt(depth) * sqrt(move_index) / 2`, ajusté :
- +2 pour les coups très calmes (score < 500K)
- +1 pour les coups semi-tactiques (score < 800K)
- Clampé à [1, depth-2]

| Concept | Fichier |
|---------|---------|
| LMR | `src/search/alphabeta.cpp` (`alpha_beta`) |

### 8.11 Internal Iterative Deepening (IID)

Quand on arrive à un noeud sans coup TT (pas de meilleur coup connu) et que la profondeur est >= 6, on fait d'abord une recherche rapide à profondeur `depth - 4` pour trouver un bon premier coup. Ce coup servira de TT move pour la recherche complète.

| Concept | Fichier |
|---------|---------|
| IID | `src/search/alphabeta.cpp` (`alpha_beta`) |

### 8.12 Threat Extension

Quand un coup crée un four (menace directe de five), la profondeur est étendue de +1. Cela empêche l'effet d'horizon : l'IA ne "cache" pas une menace en la poussant juste au-delà de sa profondeur de recherche.

| Concept | Fichier |
|---------|---------|
| Threat extension | `src/search/alphabeta.cpp` (`search_root`, `alpha_beta`) |
| Détection de four | `src/search/alphabeta.cpp` (`move_creates_four`) |

### 8.13 Quiescence Search

A profondeur 0, au lieu de retourner l'évaluation statique (qui peut être trompeuse si un échange tactique est en cours), on continue à chercher les **coups forcants uniquement** :

- Fives (priorité 900)
- Blocage de five adverse (850)
- Fours ouverts (800)
- Fours fermés (700)
- Captures gagnantes (890)

L'évaluation statique ("stand-pat") sert de borne inférieure : on peut toujours choisir de ne rien faire.

Limites : max 16 plies de quiescence, max 8 coups forcants (4 après profondeur QS > 2), plus de fours après profondeur QS > 6.

| Concept | Fichier |
|---------|---------|
| Quiescence search | `src/search/alphabeta.cpp` (`quiescence`) |

### 8.14 Ordonnancement des coups (Move Ordering)

L'efficacité de l'alpha-beta dépend **énormément** de l'ordre dans lequel on explore les coups. Un bon ordonnancement explore le meilleur coup en premier, maximisant les coupures.

**Hiérarchie de priorité** (de la plus haute à la plus basse) :

| Priorité | Score | Type de coup |
|----------|-------|-------------|
| 1 | 1 000 000 | Coup TT (meilleur coup de la table de transposition) |
| 2 | 900 000 | Notre five (victoire) |
| 3 | 895 000 | Five adverse (blocage obligatoire) |
| 4 | 890 000 | Notre capture gagnante (5e paire) |
| 5 | 885 000 | Capture gagnante adverse |
| 6 | 880 000 | Double-four fork (notre) |
| 7 | 878 000 | Four + three fork (notre) |
| 8 | 870 000 | Open four (notre) |
| 9 | 860-868K | Blocage de forks adverses |
| 10 | 855 000 | Urgence de capture (adversaire a 3+ captures) |
| 11 | 840 000 | Double open three |
| 12 | 810-830K | Fours fermés / threes ouverts |
| 13 | 600 000 | Nos captures |
| 14 | 500 000 | Killer move (1er) |
| 15 | 490 000 | Killer move (2e) |
| 16 | 400 000 | Counter-move |
| 17 | variable | History heuristic + bonus centre + proximité |

**Killer moves** : 2 coups par profondeur qui ont causé un beta cutoff récemment. Ils ne créent pas forcément de menace tactique mais ont été efficaces.

**History heuristic** : table `history[color][row][col]` incrémentée de `depth^2` à chaque beta cutoff. Plus un coup cause des coupures, plus il est exploré tôt. Les scores sont divisés par 2 à chaque nouvelle profondeur d'itérative deepening ("gravity") pour éviter la staleness.

**Counter-move** : le meilleur coup trouvé en réponse au dernier coup adverse. Stocké dans `countermove[color][row][col]`.

La génération de coups utilise un **radius-2** : on ne considère que les cases vides à distance <= 2 d'une pierre existante. Au-delà, les coups sont trop éloignés pour être pertinents. Un **partial sort** trie uniquement les K meilleurs coups nécessaires.

| Concept | Fichier |
|---------|---------|
| Score de coup | `src/search/alphabeta.cpp` (`score_move`) |
| Génération de coups candidats | `src/search/alphabeta.cpp` (`generate_moves_ordered`) |
| Killer/history/countermove updates | `src/search/alphabeta.cpp` (`alpha_beta`, après beta cutoff) |
| MoveList (structure stack-alloc) | `src/search/alphabeta.cpp` |

### 8.15 Lazy SMP (parallélisme)

La recherche multi-thread utilise **Lazy SMP** :

- **Shared** : la table de transposition (AtomicTT, lock-free), un flag `stopped`
- **Non-shared** : chaque thread a ses propres killers, history, countermove, compteurs de noeuds

Les threads helpers commencent à des **profondeurs décalées** (offsets 1, 2, 3, ...) pour naturellement diversifier l'exploration. Tous les threads écrivent dans la même TT, donc les découvertes d'un thread profitent aux autres.

Le thread principal contrôle le temps. Quand il finit ou que le temps expire, il signale `stopped` et tous les threads s'arrêtent. Le meilleur résultat est sélectionné : profondeur la plus grande, puis score le plus haut.

| Concept | Fichier |
|---------|---------|
| Lazy SMP orchestration | `src/search/alphabeta.cpp` (`search_timed`) |
| Shared state | `src/search/alphabeta.cpp` (`SharedState`) |
| Worker par thread | `src/search/alphabeta.cpp` (`WorkerSearcher`) |

### 8.16 Five-Break Search

Quand l'adversaire a un cinq cassable sur le plateau, une recherche spécialisée n'explore que les coups de capture qui cassent ce cinq. Après la capture, on retombe dans l'alpha-beta normal. Cela évite de gaspiller du temps sur des coups non pertinents.

| Concept | Fichier |
|---------|---------|
| Five-break search | `src/search/alphabeta.cpp` (`search_five_break`) |

### 8.17 Scan de ligne fusionné

La fonction `count_line_both` scanne une ligne dans les deux directions et pour les DEUX couleurs simultanément, en un seul passage. Elle retourne 8 métriques (pierres, bouts ouverts, gaps, run max pour chaque couleur). Cela évite de scanner 2 fois la même direction.

| Concept | Fichier |
|---------|---------|
| Dual-color fused line scan | `src/search/alphabeta.cpp` (`count_line_both`) |

---

## 9. Moteur (pipeline de décision)

### Stages détaillés

Le moteur (`AIEngine::get_move_with_stats`) exécute les stages dans cet ordre :

**Stage 0 — Opening book**
Pour les 1-3 premiers coups, des réponses préprogrammées :
- Plateau vide → centre (9,9)
- 1 pierre → diagonale adjacente à l'adversaire, favorisant le centre
- 3 pierres → diagonale adjacente, scoring par distance au centre + connectivité + disruption

**Stage 0.5 — Break five**
Vérifie si l'adversaire a DÉJÀ un cinq en ligne. Si oui, cherche des coups de capture pour le casser. Simule chaque rupture et vérifie que l'adversaire ne peut pas recréer un cinq incassable. Si toutes les ruptures sont illusoires, passe au stage 1 (le cinq est en fait gagnant pour l'adversaire ou non).

**Stage 1 — Victoire immédiate**
Scan brut de toutes les cases valides. Pour chacune : poser, capturer, vérifier five incassable ou 5e paire. Le premier coup gagnant trouvé est retourné.

**Stage 2 — Blocage victoire adverse**
Même scan mais pour l'adversaire. Si l'adversaire a exactement 1 coup gagnant, on le bloque. Si 0 : pas de menace. Si 2+ : on ne peut pas tout bloquer, on tombe en alpha-beta.

**Stage 3 — VCF offensif**
Recherche de victoire forcée par fours continus. Profondeur max 30. Désactivé si l'adversaire a 4+ captures (trop de captures déstabilisent les fours).

**Stage 4 — VCF défensif**
Cherche si l'ADVERSAIRE a un VCF. Si oui, bloque le premier coup de sa séquence. Désactivé si nous avons 4+ captures.

**Stage 5 — Alpha-Beta**
Recherche complète avec toutes les optimisations. Temps adaptatif :
- Début de partie (<=2 pierres) : 30% du budget temps
- Transition (<=4 pierres) : 60%
- Mi/fin de partie : 100%
- Minimum : 300ms

| Concept | Fichier |
|---------|---------|
| Pipeline complet | `src/engine/engine.cpp` (`get_move_with_stats`) |
| Opening book | `src/engine/engine.cpp` (`get_opening_move`) |
| Victoire immédiate | `src/engine/engine.cpp` (`find_immediate_win`) |
| Coups gagnants adverses | `src/engine/engine.cpp` (`find_winning_moves`) |
| Break illusoire | `src/engine/engine.cpp` (`is_illusory_break`) |
| Temps adaptatif | `src/engine/engine.cpp` (`compute_time_limit`) |

---

## 10. Interface graphique & CLI

### GUI (SFML)

La GUI utilise SFML 2.6 et suit un pattern classique game-loop :

```
gui_main.cpp : charge la font, crée GameController
    |
    v
GameController::run() : boucle à ~60 FPS
    |
    ├── handle_events() : events SFML (clic, clavier, resize)
    |     ├── handle_click() : menu/plateau/boutons
    |     └── handle_key() : U=undo, H=hint, N=new, Esc=menu
    |
    ├── check_ai_result() : l'IA a fini dans son thread ?
    ├── check_hint_result() : le hint a fini ?
    ├── start_ai_turn() : lance l'IA dans un thread séparé
    |
    └── draw()
          ├── BoardRenderer::draw() : grille, pierres, hover, hint
          └── UIPanel::draw_playing() : info panel, boutons, stats
```

**Thread safety** : l'IA tourne dans un thread séparé. Communication par `atomic<bool>` flags et mutex. Un compteur de génération (`move_gen_`) invalide les résultats périmés (si l'utilisateur undo pendant que l'IA réfléchit).

### CLI

Le CLI est un programme séquentiel simple : afficher le plateau → lire l'input → exécuter le coup → répéter. L'IA est appelée de manière bloquante.

| Concept | Fichier |
|---------|---------|
| Entry point GUI | `src/gui/gui_main.cpp` |
| Game loop + events + threads | `src/gui/game_controller.cpp` |
| Rendu du plateau | `src/gui/board_renderer.cpp` |
| Panel d'info + boutons | `src/gui/ui_panel.cpp` |
| Constantes GUI (tailles, couleurs) | `include/gomoku/gui/gui_constants.hpp` |
| Entry point CLI | `src/main.cpp` |

---

## 11. Graphe d'appel complet

```
GUI: GameController::run()              CLI: main()
         |                                    |
         v                                    v
    AIEngine::get_move_with_stats(board, color)
         |
         +-- get_opening_move()                          [Stage 0]
         |
         +-- find_five_positions()                       [Stage 0.5]
         |    +-- has_five_at_pos()
         |    +-- can_break_five_by_capture()
         |    +-- find_five_break_moves()
         |    +-- is_illusory_break()
         |         +-- execute_captures_fast()
         |         +-- has_five_at_pos()
         |         +-- can_break_five_by_capture()
         |         +-- undo_captures()
         |
         +-- find_immediate_win()                        [Stage 1]
         |    +-- is_valid_move() -> is_double_three()
         |    +-- execute_captures_fast()
         |    +-- has_five_at_pos()
         |    +-- find_five_positions()
         |    +-- can_break_five_by_capture()
         |    +-- is_illusory_break()
         |    +-- undo_captures()
         |
         +-- find_winning_moves(opponent)                [Stage 2]
         |    (même appels que Stage 1)
         |
         +-- ThreatSearcher::search_vcf()                [Stage 3 & 4]
         |    +-- vcf_search() [récursif]
         |         +-- find_four_threats()
         |         |    +-- creates_five_or_more()
         |         |    +-- creates_four()
         |         +-- execute_captures_fast()
         |         +-- has_five_at_pos()
         |         +-- can_break_five_by_capture()
         |         +-- find_defense_moves()
         |         +-- undo_captures()
         |
         +-- Searcher::search_timed()                    [Stage 5]
              +-- [spawn N threads]
              +-- WorkerSearcher::search_iterative()
                   +-- search_root()
                   |    +-- generate_moves_ordered()
                   |    |    +-- score_move()
                   |    |         +-- count_line_both()
                   |    |         +-- has_capture()
                   |    +-- is_valid_move()
                   |    +-- ZobristTable::update_place/capture/capture_count()
                   |    +-- execute_captures_fast()
                   |    +-- alpha_beta() [récursif]
                   |    +-- undo_captures()
                   |    +-- AtomicTT::store()
                   |
                   +-- alpha_beta() [récursif]
                        +-- AtomicTT::probe()
                        +-- evaluate()
                        |    +-- evaluate_color()
                        |         +-- evaluate_line()
                        |    +-- capture_score()
                        +-- quiescence()
                        +-- search_five_break()
                        +-- generate_moves_ordered()
                        +-- [NMP, RFP, razoring, futility, LMP, LMR, PVS, IID, threat ext.]
                        +-- AtomicTT::store()
```
