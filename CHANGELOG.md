# Historique des modifications

## 2.0.0 - Protocole v2, durcissement LAN, documentation d'architecture

- **Architecture :** séparation des sources `AppInput`, `AppNetwork`, `AppRender`, `InputMap` ; moteur `Game` inchangé.
- **Transport :** TCP IPv4 non bloquant ; IP numérique sans DNS sur le thread SDL, budget I/O par frame (64 Kio/sens), file sortante bornée, files de trames dédiées.
- **Protocole :** encodeur/décodeur typés (`Protocol`), caractères/champs/entiers validés, handshake `H|2`, vérification des rôles et du numéro de manche.
- **Robustesse :** détection fermeture, erreur réseau, handshake/connexion/silence/PING expirés, refus des pairs incompatibles.
- **Observation :** RTT applicatif P/Q, compteurs TX/RX d'octets réellement transférés, journaux optionnels `TETRIS_NET_TRACE=1`.
- **Tests :** nouvelles suites fragmentation, mauvais messages, timeouts, boucle TCP brute et loopback ; CTest headless et ASan/UBSan.
- **Documentation :** `CODE.md`, guide d'entretien, spécification réseau, ADR, matrice de tests et limites connues.
- **Distribution :** retrait de l'ancien exécutable embarqué pour éviter une divergence entre sources et binaire ; compilation locale CMake.

**Rupture de compatibilité :** les deux joueurs doivent utiliser la version protocole 2. Les versions précédentes sans handshake ne peuvent pas participer à un match v2.
