# Board Layer

## Notions

### Bitboard
Representation compacte du plateau 19x19 (361 cases) via 6 `uint64_t` (384 bits, 23 inutilisés).
- Opérations O(1) : set, clear, test d'un bit
- Itération rapide des pierres via `__builtin_ctzll` (count trailing zeros) — on pop les bits un par un
- Deux bitboards par Board : `black` et `white`

### Pos (Position)
Couple `(row, col)` en `uint8_t`. Index linéaire = `row * 19 + col`.
- **Sentinel pattern** : `Pos(255, 255)` remplace `std::optional<Pos>` dans les hot paths pour éviter le surcoût de branchement

### Stone
Enum : `Empty=0, Black=1, White=2`.
- `opponent()` est branchless via XOR : `static_cast<uint8_t>(s) ^ 3` (Black↔White, ne gère pas Empty)

### Board
État complet du jeu : 2 bitboards + compteurs de captures (`uint8_t` par couleur).
- `place_stone` / `remove_stone` : modifient les deux bitboards
- `get(pos)` : teste les deux bitboards pour déterminer la couleur
- `is_empty(pos)` : ni noir ni blanc
- `stones(color)` : retourne le Bitboard correspondant (pour itération)

## Code notable

- **BitboardIterator** (`include/gomoku/board/bitboard.hpp`) : itère via pointeur `const uint64_t*` au lieu d'un index, éliminant une multiplication par tour de boucle
- **Branchless Board methods** (`include/gomoku/board/board.hpp`) : `get()`, `is_empty()`, `opponent()` sans branches pour la prédiction de branchement dans les hot paths
