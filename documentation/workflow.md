Vue d'ensemble : du clic au coup joue
======================================

1. Points d'entree
------------------

- main.cpp (terminal) : boucle de jeu texte. Demande le mode (PvE/PvP), lit
  les coups humains (ex: "J10"), appelle le moteur IA.
- gui/gui_main.cpp → gui/game_controller.cpp (SFML) : boucle 60 FPS, gere
  les clics, lance l'IA dans un thread separe, affiche le plateau via
  board_renderer.cpp et le panel stats via ui_panel.cpp.


2. Quand c'est au tour de l'IA
-------------------------------

L'appel central est AIEngine::get_move_with_stats() dans engine/engine.cpp.
Il suit une cascade de 3 etapes -- des qu'une etape trouve un coup, on
s'arrete :

  Etape 1 : VCF (Victory by Continuous Fours)  ← search/threat.cpp
     └─ Suite forcee de "quatre" : l'adversaire DOIT bloquer a chaque fois
     └─ Si on gagne au bout : jouer le 1er coup de la sequence
     └─ Skipped si adversaire a >= 4 captures (capture win possible)
     └─ Timeout : 50ms max

  Etape 2 : Alpha-Beta (tout le reste)  ← search/*.cpp
     └─ searcher_.search_timed() avec temps complet (500ms par defaut)
     └─ Gere nativement : ouverture, victoire immediate, defense,
        block de menaces adverses, cinq cassable (via search_five_break)


3. La recherche Alpha-Beta (search/)
-------------------------------------

search_timed()                          ← alphabeta.cpp
  Lance N threads (Lazy SMP), tous partagent la TT
  │
  └─ search_iterative()                 ← search_iterative.cpp
      Pour depth = 1 a max_depth :
        Aspiration window [score-100, score+100]
        │
        └─ search_root()                ← search_iterative.cpp
            │
            ├─ generate_moves_ordered() ← move_ordering.cpp
            │     └─ score_move()       (tri des coups par priorite)
            │
            ├─ Filtre double-trois (is_valid_move)
            │
            └─ Pour chaque coup trie (PVS) :
                │
                └─ alpha_beta()         ← search_core.cpp  (recursif)
                    │
                    ├─ Checks terminaux (cinq, captures, five-break)
                    │
                    ├─ A depth 0 → quiescence()
                    │                └─ evaluate()  ← eval/heuristic.cpp
                    │
                    ├─ TT probe (cache des positions deja vues)
                    │
                    ├─ evaluate() pour decisions de pruning
                    │
                    ├─ Pruning pre-coups :
                    │   ├─ Reverse Futility Pruning (eval >> beta → coupe)
                    │   ├─ Razoring (eval << alpha → quiescence)
                    │   └─ Null Move Pruning (passer son tour, toujours bon ? → coupe)
                    │         └─ is_threatened()  ← move_ordering.cpp
                    │
                    ├─ IID si pas de TT move (mini-recherche depth-4)
                    │
                    ├─ generate_moves_ordered()  ← move_ordering.cpp
                    │     └─ score_move()
                    │
                    ├─ Filtre double-trois + limitation adaptive du nb de coups
                    │
                    └─ Pour chaque coup trie :
                        │
                        ├─ Pruning par coup :
                        │   ├─ Futility (eval + marge <= alpha → skip quiet)
                        │   └─ Late Move Pruning (coups tardifs a low depth → skip)
                        │
                        ├─ make_move (place_stone + execute_captures_fast)
                        │
                        ├─ Threat extension (+1 ply si le coup cree un quatre)
                        │     └─ move_creates_four()  ← move_ordering.cpp
                        │
                        ├─ PVS + LMR :
                        │   ├─ 1er coup : fenetre complete [-beta, -alpha]
                        │   └─ Suivants : fenetre nulle [-alpha-1, -alpha]
                        │       ├─ LMR : profondeur reduite (sqrt(d)*sqrt(i)/2)
                        │       ├─ Si depasse alpha → re-search depth complete
                        │       └─ Si depasse alpha en fenetre nulle → re-search fenetre complete
                        │
                        ├─ undo_move (undo_captures + remove_stone)
                        │
                        └─ Beta cutoff → mise a jour killer/history/countermove


4. Quiescence (dans search_core.cpp)
-------------------------------------

Appelee quand alpha_beta atteint depth 0. Evite l'effet d'horizon.

quiescence(board, color, alpha, beta)
  │
  ├─ Check terminal (cinq adverse, five-break)
  ├─ TT probe
  ├─ Stand-pat : evaluate(board, color) comme borne basse
  │
  ├─ Generation de coups forcants uniquement :
  │   ├─ Cinq (priorite 900)
  │   ├─ Bloquer cinq adverse (priorite 850)
  │   ├─ Capture gagnante (priorite 890)
  │   └─ Quatre ouvert/ferme (priorite 700-800, si qs_depth < 6)
  │
  └─ Pour chaque coup forcant :
      └─ quiescence() recursivement (max 16 plies)


5. Le tri des coups (move_ordering.cpp)
---------------------------------------

generate_moves_ordered() genere les candidats dans un rayon de 2 cases
autour des pierres existantes. score_move() attribue a chaque coup un
score de priorite selon l'echelle suivante :

  ┌──────────┬────────────────────────────────┐
  │ Priorite │             Coup               │
  ├──────────┼────────────────────────────────┤
  │ 1M       │ TT move (meilleur coup connu)  │
  │ 900K     │ Notre cinq                     │
  │ 895K     │ Bloquer cinq adverse           │
  │ 890K     │ Capture gagnante (5eme paire)  │
  │ 885K     │ Bloquer capture gagnante adv.  │
  │ 880K     │ Fourchette (double quatre)     │
  │ 878K     │ Fourchette (quatre + trois)    │
  │ 870K     │ Quatre ouvert                  │
  │ 868K     │ Bloquer double quatre adverse  │
  │ 866K     │ Bloquer quatre+trois adverse   │
  │ 860K     │ Bloquer quatre ouvert adverse  │
  │ 855K     │ Capture adverse urgente (4cap) │
  │ 845K     │ Capture adverse (3 cap)        │
  │ 840K     │ Double trois ouvert            │
  │ 838K     │ Bloquer double trois adverse   │
  │ 830K     │ Quatre ferme                   │
  │ 820K     │ Bloquer quatre ferme adverse   │
  │ 810K     │ Trois ouvert                   │
  │ 800K     │ Bloquer trois ouvert adverse   │
  │ 600K+    │ Nos captures                   │
  │ 550K+    │ Captures adverses              │
  │ 500K     │ Killer move #1                 │
  │ 490K     │ Killer move #2                 │
  │ 400K     │ Countermove                    │
  │ < 400K   │ History + centre + proximite   │
  │          │ + development + disruption     │
  │          │ - penalite de vulnerabilite    │
  └──────────┴────────────────────────────────┘

Helpers utilises par score_move :
  - count_line_both() : scan bidirectionnel pour les 2 couleurs a la fois
  - Inline capture/vulnerability detection (fusionne dans la boucle de dirs)


6. L'evaluation (eval/)
-----------------------

heuristic.cpp : evaluate(board, color) parcourt toutes les pierres, analyse
les 4 directions avec evaluate_line(), et additionne les scores de patterns
(FIVE, OPEN_FOUR, OPEN_THREE, etc.). Ajoute bonus de centre, connectivite,
combo multi-menaces, et penalite de vulnerabilite aux captures.

patterns.cpp : capture_score() donne un score non-lineaire selon le nombre
de captures (exponentiel en approchant 5).

Scores de patterns :
  FIVE = 1 000 000  |  OPEN_FOUR = 100 000  |  CLOSED_FOUR = 50 000
  OPEN_THREE = 10 000  |  CLOSED_THREE = 1 500
  OPEN_TWO = 1 000  |  CLOSED_TWO = 200
  NEAR_CAPTURE_WIN = 80 000


7. Les regles (rules/)
----------------------

- capture.cpp : detecte et execute le pattern X-OO-X (capture de paire).
  execute_captures_fast() + undo_captures() pour make/unmake.
- win.cpp : check_winner() verifie cinq incassable + 5 captures.
  can_break_five_by_capture() gere la regle Ninuki (cinq cassable).
- forbidden.cpp : is_valid_move() verifie la regle du double-trois
  (Noir uniquement). Exception : autorise si le coup capture.


8. Infrastructure (board/, search/)
-----------------------------------

- bitboard.cpp : 6 x uint64 = 384 bits. Get/set/count en O(1).
  Iteration des pierres via __builtin_ctzll.
- zobrist.cpp : hash incrementaux XOR pour identifier les positions.
  Permet les lookups TT en O(1). Encode pierres + captures + side-to-move.
- tt.cpp : table de transposition lock-free (AtomicTT, XOR trick de Hyatt).
  Pack/unpack en 42 bits pour stockage atomique.
- threat.cpp : recherche VCF (Victory by Continuous Fours) -- suites
  forcees de quatre ou l'adversaire n'a qu'une seule reponse a chaque fois.
  Depth limit 30. VCT (Continuous Threats) aussi disponible.
