# GUI Layer

## Notions

### SFML
Interface graphique basée sur SFML (Simple and Fast Multimedia Library). Rendu 2D du plateau, des pierres, et interactions souris.

### GameController (game_controller.cpp)
Orchestrateur de la partie côté GUI :
- Gère le tour courant, l'historique des coups, et la détection de victoire
- `execute_move()` : place la pierre, exécute les captures, vérifie le gagnant via `check_winner(board, color)`
- `undo_move()` : annule le dernier coup (restore captures et pierres)
- Lance l'IA dans un thread séparé pour ne pas bloquer le rendu

### Mode CLI (main.cpp)
Version terminal du jeu avec affichage ANSI. Même logique que le GUI : boucle tour par tour, input humain ou IA.

## Code notable

- L'IA tourne en async dans le GUI (`std::async`) — le résultat est polled à chaque frame
- `check_winner(board_, color)` : le `color` passé est celui du joueur qui vient de jouer (crucial pour la règle temporelle)