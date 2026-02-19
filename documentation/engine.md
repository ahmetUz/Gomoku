# Engine Layer

## Notions

### Pipeline AIEngine (engine.cpp)
L'IA suit un pipeline décisionnel en cascade (du plus rapide au plus coûteux) :

1. **Opening book** : premiers coups pré-calculés (centre, réponses standard)
2. **Break five** : si l'adversaire a un five cassable → jouer le coup de cassage
3. **Immediate win** : si on peut gagner immédiatement (five ou 5e capture) → jouer
4. **Opponent threats** : si l'adversaire menace de gagner → bloquer
5. **VCF** : chercher une victoire par fours continus
6. **Opponent VCF** : bloquer le VCF adverse
7. **Alpha-Beta** : recherche complète avec iterative deepening

### Time Management
- `time_limit_ms_` : 500ms par défaut
- L'iterative deepening s'arrête quand le temps est écoulé
- Le meilleur coup de la dernière itération complète est retourné

## Code notable

- Le pipeline court-circuite dès qu'une étape produit un coup — les étapes coûteuses ne sont pas exécutées si une réponse triviale existe
