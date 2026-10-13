/**
 * @file AutoPlayer.hpp
 * @brief Contrat de l’IA : placements simulés et recherche sur trois pièces.
 */
#pragma once
#include "core/Pieces.hpp"
#include <array>
#include <vector>

namespace tetris {
class Game;
/** Coup simulé et ses métriques après suppression des lignes.
 *  Le plateau est copié pour rendre les branches de recherche indépendantes.
 */
struct AutoMove {
    int x = 0;
    int y = 0;
    int rotation = 0;
    Board board = emptyBoard();
    int lines = 0;
    double landingHeight = 0;
    int erodedPieceCells = 0;
    bool valid = false;
};

/** Énumère les rotations/colonnes déposables, sans vérifier un chemin clavier. */
std::vector<AutoMove> enumerateMoves(const Board& board, char type);
/** Note heuristique : valeur plus grande = placement préféré par l'IA. */
double evaluateMove(const AutoMove& move);

/** Recherche à trois niveaux : pièce courante + deux pièces de la file. */
class AutoPlayer {
public:
    AutoMove bestMove(const Game& game) const;
};
} // namespace tetris
