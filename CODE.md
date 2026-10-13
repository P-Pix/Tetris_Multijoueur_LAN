# CODE.md - Architecture et fonctionnement du code C++17 / SDL2

**Version 2 - implémentation vérifiable.** Ce guide explique comment les fichiers communiquent, les invariants de la simulation et les responsabilités de chaque composant. Il conserve les explications détaillées de l'IA et des coefficients Dellacherie+.

Pour des arguments d'entretien et des réponses techniques aux six questions réseau, lire **[docs/ANALYSE_TECHNIQUE_RESEAU.md](docs/ANALYSE_TECHNIQUE_RESEAU.md)**. Le format exact des messages se trouve dans [docs/PROTOCOLE_LAN.md](docs/PROTOCOLE_LAN.md). Les justifications de conception sont dans [docs/DECISIONS_ARCHITECTURE.md](docs/DECISIONS_ARCHITECTURE.md).

## 1. Architecture : responsabilités et dépendances

```text
                          SDL2 events
                               |
                         ui/AppInput.cpp
                               |
               InputMap::gameActionForKey(SDLK_...)
                               |
                        enum class Action
                               v
  ai/AutoPlayer.cpp ------> core/Game.cpp ---------> core/Pieces.cpp
                               ^
                               |
                        ui/App.cpp
                    orchestration de loop
                      /              \
      ui/AppRender.cpp                ui/AppNetwork.cpp
        |                                  |
      ui/Canvas.cpp                   net/Protocol.cpp
          |                                |  trames validees
       SDL2 Renderer                  net/LineFramer.cpp
                                           |  lignes ASCII
                                       net/LanPeer.cpp
                                           |  nonblocking
                                        TCP IPv4
```

**Invariants principaux :** `Game` et `AutoPlayer` ne connaissent ni SDL2 ni les sockets ; un snapshot distant ne remplace jamais `Game::board_` ; les attaques sont transmises via l'API métier `receiveGarbage()` ; l'interface SDL2 reste sur un seul thread. Aucune donnée métier persistante ni serveur distant n'est requis.

### Répertoire des fichiers

| Emplacement                                   | Rôle distinct                                                               |
| --------------------------------------------- | --------------------------------------------------------------------------- |
| `src/main.cpp`                                | Point d'entrée : analyse CLI, création d'`App`.                             |
| `src/core/Pieces.*`                           | Formes, rotation, collisions, lignes et géométrie.                          |
| `src/core/Game.*`                             | État du jeu, gravité, 7-bag, score, réserve, attaques.                      |
| `src/ai/AutoPlayer.*`                         | Recherche 3-ply et heuristique d'évaluation.                                |
| `src/net/LanPeer.*`                           | Socket TCP IPv4 non bloquant, files d'envoi et compteurs d'octets.          |
| `src/net/LineFramer.*`                        | Transformation du flux TCP en lignes complètes et bornées.                  |
| `src/net/Protocol.*`                          | Encodage, validation et décodage des messages de jeu v2.                    |
| `src/net/ConnectionMonitor.*`                 | Horloge monotone, heartbeat, RTT et expiration.                             |
| `src/ui/InputMap.*`                           | Touches SDL2 → `enum class Action`.                                         |
| `src/ui/AppInput.cpp`                         | `SDL_PollEvent()`, `switch` de distribution, menus/clavier/souris.          |
| `src/ui/App.cpp`                              | Boucle principale, cadences et sélection solo/LAN.                          |
| `src/ui/AppNetwork.cpp`                       | Cycle de connexion, handshake, manches, arbitrage, snapshots, déconnexions. |
| `src/ui/AppRender.cpp`                        | Dessin des écrans, stats, panneaux et télémétrie réseau.                    |
| `src/ui/Canvas.*`                             | RAII SDL2, primitives de dessin, bitmap 5×7, coordonnées HiDPI.             |
| `tests/tests.cpp`                             | Tests des règles, IA et transport loopback existants.                       |
| `tests/network_tests.cpp`                     | Trames valides/invalides, fragmentation, RTT, erreurs, TCP loopback.        |
| `tests/sdl_tests.cpp`                         | Smoke test du renderer SDL2 `dummy` (si GUI activée).                       |
| `scripts/build.sh` / `scripts/ci-headless.sh` | Compilation GUI complète / tests sans dépendance graphique.                 |

**Principe de construction :** une **bibliothèque `tetris_core`** compile toutes les règles, l'IA et le réseau sans SDL2. L'exécutable SDL2 ne dépend de cette bibliothèque que par ses interfaces publiques. Des tests peuvent donc vérifier le protocole sur une machine sans écran.

## 2. Chemins d'exécution

### 2.1 Démarrer le programme

```text
main(argc, argv)
  └── App(port, mode, ip)
        ├── Canvas::Canvas()         -> SDL_Init / CreateWindow / CreateRenderer
        ├── Game::Game(seed=1)
        └── App::startSolo / startHost / connectToHost (selon CLI)

App::run()
  ├── AppInput::processEvents()      -> dispatchEvent(SDL_Event)
  │     ├── SDL_KEYDOWN             -> handleKey -> InputMap -> Game::act
  │     ├── SDL_TEXTINPUT           -> handleText (formulaire IPv4)
  │     ├── SDL_MOUSEBUTTONDOWN     -> hitbox / click
  │     ├── SDL_WINDOWEVENT         -> Canvas::resize
  │     └── SDL_QUIT                -> fin
  ├── App::update(delta 0..100ms)   -> updateSolo / updateNetwork
  ├── AppRender::render()            -> Canvas::begin / primitives / finish
  └── sleep_for(16ms)
```

L'horloge de simulation est **`std::chrono::steady_clock`**. La durée d'un tour est bornée à 100 ms pour ne pas rattraper soudain des secondes de gravité après un arrêt du processus. Ce n'est pas une boucle temps réel dur.

### 2.2 Partie solo et IA

- `App::updateSolo()` appelle `Game::tick(elapsed)` pour la gravité manuelle.
- Quand l'auto est activé, `AutoPlayer::bestMove(game)` choisit une pose ; `Game::forcePlacement()` l'applique si elle est valide.
- L'auto ne joue pas le versus, afin d'éviter une asymétrie non souhaitée pour la démonstration.

### 2.3 Échanges LAN

```text
AppNetwork::updateNetwork()
  ├── LanPeer::poll()               -> accept / finishConnect / read / write
  ├── beginHandshake()              -> H|2
  ├── receiveMessage(frame)
  │     ├── Protocol::decode()      -> valide la grammaire AVANT l'etat
  │     ├── HELLO                   -> version du pair acceptee
  │     ├── P / Q                   -> heartbeat RTT
  │     └── dispatchMessage()
  │            ├── N               -> newRound(seed)
  │            ├── S               -> RemoteState (jamais Game local)
  │            ├── G               -> Game::receiveGarbage()
  │            └── D / W / R       -> resultats et revanche
  ├── checkPeerTimeouts()           -> 5s HELLO / 8s inactivite / 5s PONG
  ├── pollHeartbeat()               -> P|nonce (~1 fois/seconde)
  ├── Game::tick(delta)
  ├── sendOutgoingGarbage()
  ├── tryFinishRound()
  └── sendSnapshot()                -> S|... (~90 ms)
```

L'hôte attend l'arrivée du pair en **écoute non bloquante**. Une fois le flux TCP ouvert, les deux machines s'échangent `H|2`. L'hôte ne transmet `N|round|seed` qu'après réception du `HELLO` distant. Lors de la revanche, les deux accords locaux doivent être présents ; les numéros de manche reviennent à 1 lors d'une nouvelle connexion.

## 3. Représentation du plateau et des pièces

Les dimensions définies dans `Pieces.hpp` sont `kCols = 10`, `kRows = 20` et `kNextCount = 5`.

```cpp
using Board = std::array<std::array<char, kCols>, kRows>;
```

La case `board[y][x]` est lue de haut en bas et de gauche à droite : `y=0` est la première ligne affichée, `y=19` est la dernière. Un caractère `'.'` indique une case vide ; `I`, `J`, `L`, `O`, `S`, `T`, `Z` sont les blocs de chaque forme ; `'#'` correspond aux blocs gris des malus LAN.

- **`Shape`** contient exactement quatre `Cell{x,y}` relatives.
- **`ActivePiece`** ajoute type, index de rotation et origine `(x,y)` ; un `y` négatif est toléré au départ.
- **`normalize()`** déplace la forme en `(0,0)` et trie les cellules, afin de comparer les rotations.
- **`makeRotations()`** crée les rotations horaires uniques, donc `O` n'en a qu'une et `I` en a deux.
- **`collides()`** détecte bords gauche/droit/bas et chevauchement ; une pièce peut temporairement dépasser le bord supérieur.
- **`dropY()`** recherche la position verticale finale par descente ; renvoie `-1000` si la pose initiale est impossible.
- **`clearLines()`** enlève toutes les rangées pleines, compacte les autres vers le bas et remplit le haut avec `'.'`. Elle retourne aussi `clearedRows`, utile à l'IA.

Les rotations comprennent un **petit wall-kick horizontal** (`0`, `-1`, `+1`, `-2`, `+2`) ; ce n'est pas une implémentation complète du standard SRS. Dans ce code, l'IA examine des atterrissages verticaux et ne vérifie pas s'ils sont tous réalisables avec une séquence de touches.

## 4. Fonctionnement du moteur `Game`

`Game` est autonome : les tests peuvent construire une partie sans fenêtre ni connexion. Les principales méthodes publiques sont :

| Méthode                        | Contrat                                                                               |
| ------------------------------ | ------------------------------------------------------------------------------------- |
| `Game(seed)` / `reset(seed)`   | Réinitialise tous les compteurs, le plateau, les files et les générateurs aléatoires. |
| `act(Action)`                  | Déplacement, rotation, descente, chute immédiate ou réserve.                          |
| `tick(milliseconds)`           | Applique la gravité selon le niveau courant, sans points de descente manuelle.        |
| `forcePlacement(rotation,x,y)` | Pose immédiatement le coup calculé par l'IA si la position est valide et stabilisée.  |
| `receiveGarbage(lines)`        | Ajoute une attaque entrante à la file (plafonnée à 20 lignes).                        |
| `takeOutgoingGarbage()`        | Donne et remet à zéro les lignes d'attaque sortantes.                                 |
| `visibleBoard()`               | Copie du plateau avec la pièce active pour les snapshots du réseau.                   |

### 4.1 Ordre des pièces et réserve

Le jeu utilise un générateur **7-bag** : chaque sac contient les sept types, mélangés par Fisher-Yates avec `std::mt19937`. Les pièces des cinq prochaines positions sont prévisualisées. Deux parties initialisées avec la **même graine** ont le même flux de pièces tant qu'elles les tirent de la même manière ; le `hold` peut donc décaler leurs suites effectivement jouées.

Le générateur de trous des malus, `garbageRng_`, utilise **un autre flux**, initialisé avec `seed ^ 0x9e3779b9U`. Cela empêche un malus de modifier la suite des pièces du 7-bag. Le littéral `0x9e3779b9U` est une constante usuelle de mélange de bits dérivée de la proportion dorée, **pas une règle du jeu**.

Le `hold` autorise **un échange par pièce verrouillée**. Le premier appel stocke la pièce active et tire la suivante. Un verrouillage normal réarme la réserve.

### 4.2 Gravité, verrouillage et score

Le délai entre deux descentes automatiques dépend du niveau :

```cpp
std::max(85, 820 - (level - 1) * 65); // millisecondes
```

Lorsqu'une pièce ne peut plus descendre, `Game::lock()` appelle dans cet ordre : `addLockedCells()` → `clearLines()` → `updateScore()` → `resolveGarbage()` → `spawnNext()` (si la partie n'est pas perdue).

| Lignes simultanément effacées | Points de base |
| ----------------------------: | -------------: |
|                             1 |   100 × niveau |
|                             2 |   300 × niveau |
|                             3 |   500 × niveau |
|                             4 |   800 × niveau |

Une descente manuelle réussie donne un point ; une chute immédiate donne deux points par case descendue. Le niveau vaut `1 + lignes_totales / 10` (division entière). Si un verrouillage doit fixer des blocs hors du plateau, la partie se termine.

### 4.3 Attaques du mode versus

Une suppression de **1, 2, 3 ou 4 lignes** produit **0, 1, 2 ou 4 lignes d'attaque**. À chaque verrouillage :

1. Une éventuelle attaque sortante **annule d'abord** des lignes d'attaque entrantes en attente.
2. Le surplus offensif non annulé est enregistré dans `outgoingGarbage_`.
3. Les pénalités entrantes restantes sont insérées **en bas** du plateau, en poussant celui-ci vers le haut.
4. Chaque ligne grise comporte un trou choisi via `garbageRng_`.
5. Si l'opération chasse des blocs par le haut, le joueur perd.

**Point important :** `Game` ne transmet rien lui-même. `App` récupère les attaques avec `takeOutgoingGarbage()` et les donne à `LanPeer`.

## 5. L'intelligence artificielle : `AutoPlayer`

### 5.1 Recherche « 3-ply »

L'IA examine la pièce active et les **deux suivantes** :

```text
                 Plateau courant
                       |
          Toutes les poses de pièce 1
                 /     |     \
               ...     ...   ...
                |       |     |
          Toutes les poses de pièce 2
                       |
          Toutes les poses de pièce 3
                       |
            Meilleure note cumulée
                       |
             Coup à jouer (pièce 1)
```

**`enumerateMoves()`** produit les atterrissages de chaque rotation à chaque colonne valide. Chaque candidat conserve un `Board` résultant indépendant, le nombre de lignes effacées, la hauteur d'atterrissage et la mesure d'érosion.

**`search()`** calcule récursivement le meilleur résultat futur et conserve le coup initial le mieux noté. Le score recherché suit la formule :

\[
S = E_1 + 0{,}64 E_2 + 0{,}64^2 E_3
\]

Les sous-arbres de profondeur 1 et 2 sont mis en cache dans `std::unordered_map`. La clé combine **le type de pièce**, **la profondeur** et **20 masques d'occupation sur 10 bits** (2 octets par ligne). Les couleurs des pièces ne comptent pas dans l'évaluation. Le cache est **recréé à chaque décision**, il n'est pas global à la partie.

Cette recherche est relativement coûteuse : elle teste de nombreuses combinaisons rotation-colonne. Elle **n'explore pas le `hold`**, et ne constitue ni un algorithme de jeu parfait ni un réseau de neurones entraîné.

### 5.2 À quoi servent les coefficients décimaux ?

`evaluateMove()` ne calcule **pas le score officiel de Tetris**. Elle donne une valeur d'intérêt à un plateau obtenu après une pose ; **plus elle est élevée, plus la pose est attractive**. Les six premiers coefficients correspondent aux valeurs diffusées pour **El-Tetris**, une variante fondée sur six indicateurs proposés par Pierre Dellacherie.

| Constante C++ (`weights::`) |         Valeur exacte | Indicateur / intuition                                                      |
| --------------------------- | --------------------: | --------------------------------------------------------------------------- |
| `kLandingHeight`            |  `-4.500158825082766` | Hauteur de pose : préfère jouer près du sol.                                |
| `kErodedPieceCells`         | `+3.4181268101392694` | Lignes supprimées × cases de la pièce concernées.                           |
| `kRowTransitions`           | `-3.2178882868487753` | Bordures pleines/vides par ligne ; évite la fragmentation horizontale.      |
| `kColumnTransitions`        |  `-9.348695305445199` | Bordures pleines/vides par colonne ; pénalise les irrégularités verticales. |
| `kHoles`                    |  `-7.899265427351652` | Cases vides ayant une case pleine au-dessus.                                |
| `kWells`                    | `-3.3855972247263626` | Profondeur cumulée des puits, avec pénalité triangulaire.                   |

Les coefficients avec de nombreuses décimales sont **des poids empiriques publiés**, non des constantes mathématiques universelles. Leur précision ne prouve pas un optimum. Ils ne doivent pas être modifiés au hasard si l'on souhaite comparer les résultats des différentes versions du moteur.

La version JavaScript d'origine ajoute ensuite ses **propres pénalités/récompenses** :

| Constante C++ (`weights::`) |  Valeur | Effet                                                      |
| --------------------------- | ------: | ---------------------------------------------------------- |
| `kClearedLinesSquared`      |   `1.8` | Bonus `1.8 × lignes²`, favorise un Tetris à quatre lignes. |
| `kTotalHeightPenalty`       |  `0.18` | Coût de la somme des dix hauteurs.                         |
| `kBumpinessPenalty`         |  `0.24` | Coût de la différence de hauteur entre colonnes voisines.  |
| `kDangerHeight`             |    `16` | Début d'une pénalité supplémentaire de hauteur.            |
| `kDangerHeightStep`         |  `18.0` | Pénalité de `18 × (hauteur_max - 15)` au-delà du seuil.    |
| `kCriticalHeight`           |    `19` | Seuil de danger critique.                                  |
| `kCriticalHeightPenalty`    | `200.0` | Pénalité fixe additionnelle au seuil critique.             |
| `kFutureDiscount`           |  `0.64` | Importance des états prospectifs.                          |

On peut résumer la formule comme ceci :

```text
E = (-4.500158... × hauteur_d'atterrissage)
  + (+3.418126... × erosion_des_cases)
  + (-3.217888... × transitions_lignes)
  + (-9.348695... × transitions_colonnes)
  + (-7.899265... × trous)
  + (-3.385597... × puits)
  + 1.8 × (lignes_supprimees)²
  - 0.18 × somme_hauteurs
  - 0.24 × rugosite
  - penalite_hauteur_dangereuse
```

**Exemple :** si un coup crée deux trous supplémentaires (toutes choses égales par ailleurs), sa note diminue d'environ `2 × 7.899 ≈ 15.8`. Le programme choisit alors plus volontiers une position évitant ces trous, sauf si d'autres avantages compensent cette perte.

**Provenance :** poids à six caractéristiques documentés dans le dépôt [Implementation of El-Tetris](https://github.com/YuhanXiaoJY/Implementation-of-El-Tetris) et la documentation [PyTetris](https://pypi.org/project/pytetris/). Pour le contexte scientifique des caractéristiques Dellacherie, voir [Improvements on Learning Tetris with Cross-Entropy Method (Thiery & Scherrer)](https://citeseerx.ist.psu.edu/document?doi=7bea02dfd7225cb47689ccae90a5383444294a32&repid=rep1&type=pdf). Les réglages additionnels du barème et de la recherche proviennent du `app.js` d'origine, pas de cette table de poids El-Tetris.

**Conseil de maintenance :** ajouter un benchmark sur une série fixe de graines avant de changer les poids : nombre de lignes, durée avant `game over`, distribution des hauteurs, temps de calcul et mémoire consommée. La qualité d'une heuristique se valide par des mesures, pas par la précision de ses décimales.

## 6. Transport TCP et protocole applicatif

### 6.1 Envoi et réception d'octets, pas de « paquets de jeu »

`LanPeer` possède ses deux descripteurs (`socket_` et `listener_`) et les libère dans `close()` / son destructeur. Les classes sont **non copiables** pour éviter une double fermeture.

- `host()` : `socket/bind/listen`, puis socket d'écoute non bloquant ; un adversaire unique.
- `join()` : validation d'une **IPv4 numérique** avec `inet_pton()`, `connect()` sur socket `O_NONBLOCK`, gestion de `EINPROGRESS`.
- `finishConnecting()` : `select()` à timeout **nul**, puis `getsockopt(SO_ERROR)` ; expiration après 5 secondes.
- `readAvailable()` : `recv()` tant qu'il y a des octets, avec un maximum de 64 Kio par passage ; `recv() == 0` indique fermeture ordonnée.
- `writeAvailable()` : `send(..., MSG_NOSIGNAL)` et avancement de `outgoingOffset_` jusqu'à transmission du suffixe ; maximum de 64 Kio par passage.
- `send()` : **ajoute à la file** des trames complètes (2 048 octets max), mais ne signifie pas que le pair a reçu l'information.
- `LineFramer` : reconstitue les trames malgré les découpages arbitraires du flux TCP, garde au plus 200 trames en attente, refuse les contrôles binaires.

Le format texte est versionné : `H|2` (salutation), `N` (manche), `S` (snapshot), `G` (attaque), `D` (défaite client), `W` (résultat hôte), `R` (revanche), `P`/`Q` (sondes). **Le parseur `Protocol::decode()` vérifie les tailles, types et valeurs** ; `AppNetwork` contrôle ensuite rôle et manche. Voir la spécification complète : [docs/PROTOCOLE_LAN.md](docs/PROTOCOLE_LAN.md).

### 6.2 Pourquoi plusieurs modules réseau ?

- `LanPeer` répond à « quels octets sont envoyés/reçus et quand ? ».
- `LineFramer` répond à « une trame est-elle complète et de taille acceptable ? ».
- `Protocol` répond à « une trame correspond-elle à une commande valide ? ».
- `ConnectionMonitor` répond à « le pair est-il encore réactif et quel RTT est observé ? ».
- `AppNetwork` répond à « la commande valide est-elle autorisée dans cette phase, et que change-t-elle au match ? ».

Chaque classe possède une responsabilité vérifiable séparément dans `tests/network_tests.cpp`. Cette séparation simplifie la maintenance et évite de mélanger du parsing non fiable avec les règles de Tetris.

### 6.3 Déconnexion, mesure de RTT et limites de confiance

`ConnectionMonitor` utilise `steady_clock`, des `P|nonce` et `Q|nonce` et une seule sonde en attente. Le temps de retour est affiché dans le versus, avec les octets réellement émis/reçus par la pile TCP. Les délais de 5/8 secondes ne sont pas des garanties temps réel strictes. En cas d'erreur, `App::networkFailed()` fait apparaître l'écran d'incident ; la manche **n'est pas automatiquement reprise**.

Le protocole est destiné à un LAN de confiance ; il n'authentifie pas le pair, ne chiffre pas, et ne résiste pas à un client qui fabrique de faux messages de jeu pourtant conformes. L'hôte arbitre seulement la victoire, pas les actions adverses.

## 7. SDL2, gestion de l'interface et saisies

`Canvas::Canvas()` initialise le sous-système vidéo et crée une fenêtre/renderer (accéléré si possible, logiciel sinon). Son destructeur libère renderer, fenêtre, SDL2 (ordre inverse). `AppRender` dessine dans un espace logique **1220×820** ; `Canvas::resize()` adapte séparément la taille des pixels graphiques et les coordonnées de souris HiDPI. La police **bitmap 5×7** est dans les sources, sans SDL_ttf.

La boucle SDL garde `SDL_PollEvent()`, qui est la primitive conseillée pour un jeu actif. En revanche, la logique de traitement est **découplée** :

```cpp
void App::processEvents() {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) dispatchEvent(event);
}

void App::dispatchEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_QUIT: running_ = false; break;
        case SDL_WINDOWEVENT: handleWindowEvent(event.window); break;
        case SDL_KEYDOWN: handleKey(event.key.keysym.sym, event.key.repeat != 0); break;
        case SDL_TEXTINPUT: handleText(event.text.text); break;
        case SDL_MOUSEBUTTONDOWN: handleMouseEvent(event.button); break;
        default: break;
    }
}
```

Le fichier `InputMap.cpp` traduit les touches SDL2 en `Action`, **sans modifier directement `Game`**. Les raccourcis à bascule ne sont pas répétés. `SDL_StartTextInput()` n'est appelé qu'à l'ouverture du formulaire d'adresse et `SDL_StopTextInput()` à sa fermeture. L'adresse IPv4 saisie n'autorise que chiffres, `.` et `:` (max 48 caractères) ; aucun DNS synchrone n'a lieu pendant le rendu.

`Canvas` ne référence pas directement Xlib/Xutil : le choix Wayland/X11 est une responsabilité interne éventuelle de SDL2. Le programme reste cependant Linux/POSIX pour le transport réseau.

## 8. Compiler, tester et examiner

**Version graphique Linux :**

```bash
sudo apt install -y build-essential cmake pkg-config libsdl2-dev
./scripts/build.sh
./build/tetris_lan --solo
./build/tetris_lan --host --port 7777
./build/tetris_lan --join 127.0.0.1:7777
```

**Mode CI indépendant de SDL2 :**

```bash
./scripts/ci-headless.sh
# ou
cmake -S . -B build-ci -DTETRIS_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-ci --parallel 2
ctest --test-dir build-ci --output-on-failure
```

**Instrumentation mémoire (moteur / réseau) :**

```bash
cmake -S . -B build-sanitize -DTETRIS_BUILD_GUI=OFF -DTETRIS_SANITIZE=ON
cmake --build build-sanitize --parallel 2
ctest --test-dir build-sanitize --output-on-failure
```

**Traces réseau volontairement activables :** `TETRIS_NET_TRACE=1 ./build/tetris_lan --host`. Les traces signalent HELLO envoyé/reçu, début de manche, résultat et déconnexion sans modifier la mécanique de jeu.

### Comment modifier correctement un comportement ?

| Changement                  | Modifier d'abord                   | Vérifier                                |
| --------------------------- | ---------------------------------- | --------------------------------------- |
| Gravité, score, hold, malus | `core/Game.cpp`                    | `tests/tests.cpp`                       |
| Rotation / géométrie        | `core/Pieces.cpp`                  | tests de formes et collisions           |
| Recherche / poids IA        | `ai/AutoPlayer.cpp`                | tests + benchmark sur graines fixes     |
| Ajouter un message LAN      | `net/Protocol.*`, `AppNetwork.cpp` | version du protocole et tests invalides |
| Réception TCP / files       | `LanPeer`, `LineFramer`            | tests de fragments et loopback          |
| Temporisations / RTT        | `ConnectionMonitor`                | tests avec horloge simulée              |
| Touche / action             | `InputMap.cpp`, `AppInput.cpp`     | compile SDL2 et test manuel             |
| Dessin / résolution         | `Canvas.cpp`, `AppRender.cpp`      | smoke test `SDL_VIDEODRIVER=dummy`      |

Le protocole v2 n'est **pas compatible** avec les anciennes versions. Un changement incompatible doit incrémenter `protocol::kVersion`, adapter la documentation et ajouter un test de protocole.

## 9. Limites assumées / extensions possibles

- Application **interactive à faible latence**, pas système temps réel dur ; pas d'ordonnancement déterministe garanti.
- Une seule connexion TCP IPv4 ; adresse numérique seulement, pas de DNS/IPv6.
- Pas de chiffrement, d'authentification, d'anti-triche, de reconnexion ni de replay.
- Snapshot distant environ toutes les 90 ms : affichage asynchrone, non identique à une simulation partagée strictement synchrone.
- L'IA 3-ply peut monopoliser le thread UI en solo ; worker/temps de calcul limité possibles à l'avenir.
- Les poids Dellacherie+ sont des heuristiques empiriques ; ils ne prouvent pas une stratégie optimale.
- Les rotations n'implémentent pas SRS complet ; l'IA choisit des atterrissages sans forcément trouver le chemin clavier équivalent.
- Le protocole ne valide pas la véracité des scores/attaques conformes ; le LAN doit être de confiance.

**En cas de reprise du projet :** lire `Pieces.hpp`, `Game.hpp`, `Protocol.hpp`, `LineFramer.cpp`, `LanPeer.cpp`, `ConnectionMonitor.hpp`, puis `AppNetwork.cpp` et `AppInput.cpp`. Commencer par exécuter les tests et par reproduire une session host+client en loopback.
