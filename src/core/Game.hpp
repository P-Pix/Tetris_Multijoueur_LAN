/**
 * @file Game.hpp
 * @brief État et API du jeu : règles, gravité, score, réserve et malus LAN.
 */
#pragma once
#include "core/Pieces.hpp"
#include <cstdint>
#include <deque>
#include <random>

namespace tetris {
/** Compteurs exposés à l’interface et transmis par les snapshots LAN. */
struct Statistics {
    int score = 0;
    int lines = 0;
    int level = 1;
    int pieces = 0;
    int tetrises = 0;
};

/** Commandes abstraites indépendantes du clavier ou du réseau. */
enum class Action { Left, Right, RotateCW, RotateCCW, SoftDrop, HardDrop, Hold };

/**
 * @brief Partie Tetris autonome, sans dépendance au rendu ou à SDL2.
 *
 * L'appelant gère pause, réseau et IA. Le moteur conserve la séquence 7-bag,
 * les collisions, le score, la réserve et les pénalités du mode versus.
 */
class Game {
public:
    explicit Game(uint32_t seed = 1);
    /** Nouvelle partie ; une même seed génère la même séquence de pièces. */
    void reset(uint32_t seed);
    /** Effectue une action manuelle, si possible, et retourne son succès. */
    bool act(Action action);
    /** Fait progresser la gravité en temps simulé, hors pause gérée par App. */
    void tick(int milliseconds);
    /** Placement IA instantané : l'emplacement doit être valide et posé au sol. */
    bool forcePlacement(int rotation, int x, int y);
    /** Enfile des lignes de pénalité (au maximum 20), appliquées au verrouillage. */
    void receiveGarbage(int lines);
    /** Lit puis remet à zéro les attaques à transmettre à l'adversaire. */
    int takeOutgoingGarbage();
    const Board& board() const { return board_; }
    const ActivePiece& current() const { return current_; }
    const Statistics& stats() const { return stats_; }
    const std::deque<char>& next() const { return queue_; }
    char holdType() const { return holdType_; }
    bool canHold() const { return canHold_; }
    bool gameOver() const { return gameOver_; }
    void fail() { gameOver_ = true; }
    int pendingGarbage() const { return pendingGarbage_; }
    int gravityInterval() const;
    /** Copie du plateau avec la pièce active, destinée à l'affichage LAN. */
    Board visibleBoard() const;

private:
    void refillBag();
    char drawType();
    void ensureQueue();
    ActivePiece makePiece(char type) const;
    void spawnNext();
    void setGameOver() { gameOver_ = true; }
    bool move(int dx);
    bool rotate(int direction);
    bool softDrop(bool manual);
    bool hardDrop(bool manual);
    bool hold();
    void lock();
    bool addLockedCells();
    void updateScore(const ClearResult& result);
    void resolveGarbage(int cleared);
    void insertGarbage(int lines);
    int attackFor(int lines) const;

    Board board_ = emptyBoard();
    ActivePiece current_{};
    Statistics stats_{};
    std::deque<char> queue_;
    std::deque<char> bag_;
    // Générateurs séparés : les trous des malus ne changent pas le tirage des pièces.
    std::mt19937 rng_;
    std::mt19937 garbageRng_;
    char holdType_ = '.';
    bool canHold_ = true;
    bool gameOver_ = false;
    int gravityMs_ = 0;
    // Entrant différé jusqu'au prochain verrouillage ; sortant consommé par App.
    int pendingGarbage_ = 0;
    int outgoingGarbage_ = 0;
};
} // namespace tetris
