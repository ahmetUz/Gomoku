# Rules Layer

## Notions

### Captures (Pente-style)
Pattern `X-OO-X` : si un joueur pose une pierre qui encadre exactement 2 pierres adverses, ces 2 pierres sont retirées.
- `count_captures_fast()` : compte les paires capturables dans les 4 directions (8 sens)
- `execute_captures_fast()` : effectue les captures et retourne un `CaptureInfo` (stack-allocated, max 4 paires = 8 pierres)
- `get_captured_positions()` : retourne les positions capturées (utilisé uniquement dans les tests et les chemins rares)

### Victoire (win.cpp)
Deux conditions de victoire :
1. **5+ pierres alignées** (horizontal, vertical, 2 diagonales)
2. **5 paires capturées** (10 pierres adverses retirées)

**Règle temporelle des fives cassables** : si un five peut être cassé par une capture adverse (pattern X-OO-X où OO fait partie du five), l'adversaire a UN tour pour le casser. Si il ne le casse pas → le five gagne.
- `check_winner(board, last_player)` implémente cette logique temporelle
- `can_break_five_by_capture()` : check statique de cassabilité (rayon 2 autour du five)

### Coups interdits (forbidden.cpp)
**Double-three** : un coup qui crée simultanément 2+ "threes" ouverts est interdit.
- `scan_line()` : détecte un three dans une direction, supporte les gaps (ex: X_XXX)
- `scan_line_consecutive()` : fallback sans gap pour les cas edge
- `is_double_three()` : compte les threes dans les 4 directions, interdit si ≥ 2

## Code notable

- `has_five_at_pos()` (`src/rules/win.cpp:26`) : check zero-alloc en 4 directions depuis une position donnée — utilisé dans le hot path alpha-beta
- `check_winner()` logique temporelle (`src/rules/win.cpp:227`) : le point crucial du fix — l'ancien code était purement statique et ne déclarait jamais un five cassable comme gagnant
