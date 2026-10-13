# Tetris Auto LAN - C++17 / SDL2 / TCP IPv4 (v2)

Un **jeu natif Linux C++17 / SDL2** issu du Tetris HTML/JS fourni initialement, auquel un mode **versus LAN pour deux joueurs** a été ajouté. Le projet est conçu comme une **démonstration réseau argumentée et testable** : moteur de jeu indépendant, événements SDL2 séparés des actions métier, TCP non bloquant, protocole versionné, messages bornés, RTT visible et tests reproductibles.

> **Périmètre :** Linux (Lubuntu/Ubuntu notamment), réseau local de confiance. Le projet n'est pas un serveur de compétition anti-triche ni un système temps réel dur. Il ne dépend pas directement de X11 : SDL2 gère son backend graphique.

## 1. Démarrage rapide (Lubuntu / Ubuntu)

Installer les **dépendances de compilation** sur le poste :

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libsdl2-dev
```

Dans le dossier décompressé :

```bash
./lancer.sh
```

`lancer.sh` lance le binaire `build/tetris_lan` s'il existe, sinon exécute `scripts/build.sh` avant de le lancer. **Aucun ancien binaire précompilé n'est embarqué** : ce que vous exécutez correspond aux sources présentes.

Compilation / tests manuels :

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/tetris_lan
```

## 2. Jouer

### Solo

Sélectionner **F1** dans le menu, ou lancer :

```bash
./build/tetris_lan --solo
```

| Touche                 | Action                                   |
| ---------------------- | ---------------------------------------- |
| `←` / `→`              | Déplacement gauche / droite              |
| `↓`                    | Descente douce                           |
| `↑` ou `X`             | Rotation horaire                         |
| `Z`                    | Rotation inverse                         |
| `Espace`               | Chute immédiate                          |
| `C`                    | Hold : réserve de pièce                  |
| `A`                    | Activer / désactiver l'IA 3-ply          |
| `[` / `]` ou `-` / `+` | Régler la cadence IA                     |
| `P`                    | Pause (solo uniquement)                  |
| `R`                    | Nouvelle partie / revanche selon le mode |
| `Échap`                | Revenir au menu                          |

L'IA sélectionne des poses à partir des **3 prochaines pièces** et de poids heuristiques proches de Dellacherie/El-Tetris. Les coefficients sont documentés dans [`CODE.md`](CODE.md), sans masquer leur caractère empirique.

### Versus sur deux ordinateurs

Les deux PC sont sur le **même LAN**, disposent de la même version du projet et autorisent le port TCP 7777 entre eux.

**Ordinateur 1 (hôte) :** bouton **F2** ou :

```bash
./build/tetris_lan --host --port 7777
```

**Ordinateur 2 (client) :** bouton **F3**, saisir l'**adresse IPv4 numérique** affichée sur l'hôte (par exemple `192.168.1.42:7777`) puis `Entrée`, ou :

```bash
./build/tetris_lan --join 192.168.1.42:7777
```

Le protocole commence par un échange `H|2` ; une fois celui-ci validé, l'hôte choisit une graine de pièces et déclenche la manche. Chaque poste affiche son plateau et celui de l'adversaire. Les deux moteurs gardent le même ordre initial de pièces, mais chaque joueur conserve ses propres actions / son `hold`.

**Malus :** effacer 1 ligne envoie 0 ligne ; 2 → 1 ; 3 → 2 ; 4 → 4. Les attaques reçues sont appliquées à la pose suivante, avec compensation possible par les lignes que l'on élimine. Le premier `game over` met fin au match ; l'hôte arbitre le résultat. Les deux joueurs confirment la revanche avec `R`.

La carte latérale affiche **RTT (ms), TX et RX (Kio)**. Le RTT est une mesure applicative d'aller-retour, pas une mesure garantie du délai de jeu à sens unique.

### Tester sur un seul PC

Ouvrir deux terminaux :

```bash
# Terminal A
./build/tetris_lan --host --port 7777

# Terminal B
./build/tetris_lan --join 127.0.0.1:7777
```

### Diagnostic

```bash
# Traces d'etablissement, de manche et de deconnexion sur le terminal
TETRIS_NET_TRACE=1 ./build/tetris_lan --host --port 7777
```

En cas de connexion impossible, vérifier `hostname -I` côté hôte, pare-feu local et isolation Wi-Fi. Si UFW est actif et que la règle est appropriée à un LAN de confiance, ouvrir **uniquement** le port concerné (par exemple `sudo ufw allow 7777/tcp`). **Ne pas rediriger ce port depuis Internet** : il n'y a pas de TLS ni d'authentification.

## 3. Construire et tester le moteur sans SDL2 (CI / serveur)

SDL2 n'est pas nécessaire aux tests de logique métier et du transport :

```bash
./scripts/ci-headless.sh
# Equivalent :
cmake -S . -B build-ci -DTETRIS_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-ci --parallel 2
ctest --test-dir build-ci --output-on-failure
```

Pour activer AddressSanitizer et UBSan sur le cœur et les tests :

```bash
cmake -S . -B build-sanitize -DTETRIS_BUILD_GUI=OFF -DTETRIS_SANITIZE=ON
cmake --build build-sanitize --parallel 2
ctest --test-dir build-sanitize --output-on-failure
```

Le build GUI par défaut ajoute `tetris_lan` et un smoke test SDL2 utilisant `SDL_VIDEODRIVER=dummy`. Le rendu graphique complet sur deux postes exige un essai sur les PC visés.

## 4. Architecture et documents à lire

```text
TetrisAutoCPP/
  CMakeLists.txt                  Build C++17, tests et option headless
  README.md                       Demarrage et demo
  CODE.md                         Guide du code + IA
  docs/
    ANALYSE_TECHNIQUE_RESEAU.md   Six questions d'entretien : reponses argumentees
    ANALYSE_TECHNIQUE_RESEAU.pdf  Dossier de 10 pages pour presentation
    PROTOCOLE_LAN.md              Specification H/N/S/G/D/W/R/P/Q v2
    DECISIONS_ARCHITECTURE.md     Decisions / alternatives / compromis
    TESTS_VALIDATION.md           Matrice des tests, resultats et limites
  src/
    main.cpp
    core/                        Game, Pieces
    ai/                          AutoPlayer
    net/                         LanPeer, LineFramer, Protocol, ConnectionMonitor
    ui/                          App, AppInput, AppNetwork, AppRender, Canvas, InputMap
  tests/                         game tests, network tests, sdl dummy
  scripts/                       build.sh, ci-headless.sh
  lancer.sh
```

**Document imprimable :** [`docs/ANALYSE_TECHNIQUE_RESEAU.pdf`](docs/ANALYSE_TECHNIQUE_RESEAU.pdf) (également fourni à côté du ZIP).

**Ordre de lecture recommandé en entretien :** [`docs/ANALYSE_TECHNIQUE_RESEAU.md`](docs/ANALYSE_TECHNIQUE_RESEAU.md) → [`docs/DECISIONS_ARCHITECTURE.md`](docs/DECISIONS_ARCHITECTURE.md) → `src/net/LineFramer.cpp` → `src/net/Protocol.cpp` → `src/net/LanPeer.cpp` → `src/ui/AppNetwork.cpp` → tests.

## 5. Choix et limites assumés

- **TCP** simplifie l'ordre/la fiabilité, mais ce flux ne conserve pas les frontières applicatives ; `LineFramer` est obligatoire. TCP ne promet pas de délai maximum, ni la livraison après rupture.
- **Une seule boucle SDL** et sockets `O_NONBLOCK` simplifient le partage d'état. Le travail d'E/S est borné à 64 Kio par sens/frame ; l'IA solo reste susceptible de ralentir le rendu.
- **Protocole v2** : handshake obligatoire, validation des messages, phases et rôles, mais sans authentification ni anti-triche.
- **IPv4 numérique seulement** : pas de DNS synchrone susceptible de bloquer le thread graphique ; pas de découverte réseau automatique.
- **Timeouts** : connexion et handshake 5 s, silence valide 8 s, PING sans PONG 5 s ; le match est interrompu et ne reprend pas automatiquement.
- **Instantané distant ≠ simulation centrale** : l'hôte arbitre les résultats, chaque poste conserve son moteur ; aucune synchronisation frame à frame stricte.
- **Livraison des sources seulement** : compiler avec les en-têtes SDL2 officiels du poste cible ; pas de binaire pouvant devenir obsolète.

Pour le détail du raisonnement, les cas d'échec et les tests, consulter les documents dans `docs/`.
