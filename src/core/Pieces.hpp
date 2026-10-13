/**
 * @file Pieces.hpp
 * @brief Types et règles géométriques partagés par le moteur de jeu et le moteur IA.
 */
#pragma once
#include <array>
#include <vector>

namespace tetris {
/** Dimensions et nombre de pièces visibles du plateau standard. */
constexpr int kCols = 10;
constexpr int kRows = 20;
constexpr int kNextCount = 5;
/** Grille fixe indexée [ligne][colonne], de haut en bas.
 *  '.' = vide ; I/J/L/O/S/T/Z = blocs ; '#' = ligne de malus.
 */
using Board = std::array<std::array<char, kCols>, kRows>;

/** Coordonnée relative dans un tétrimino, en cases entières. */
struct Cell { int x = 0; int y = 0; };
using Shape = std::array<Cell, 4>;

/** Pièce mobile : origine (x, y) pouvant être au-dessus du plateau (y négatif). */
struct ActivePiece {
    char type = 'I';
    int rotation = 0;
    int x = 3;
    int y = -1;
};

/** Résultat d'une suppression, avec position des anciennes lignes disparues. */
struct ClearResult {
    Board board{};
    int lines = 0;
    std::array<bool, kRows> clearedRows{};
};

/** Construit une grille de 20 lignes entièrement vides. */
Board emptyBoard();
/** Élimine toutes les lignes pleines ; conserve la position des lignes d'origine. */
ClearResult clearLines(const Board& board);
/** Variantes uniques d'un type de pièce après normalisation des rotations. */
const std::vector<Shape>& rotations(char type);
int shapeWidth(const Shape& shape);
int shapeHeight(const Shape& shape);
/** Vrai si hors côtés/bas ou superposition ; le dessus (y < 0) reste autorisé. */
bool collides(const Board& board, const ActivePiece& piece);
/** Coordonnée y du dernier emplacement avant collision ; -1000 si départ invalide. */
int dropY(const Board& board, char type, int rotation, int x, int startY = -4);
} // namespace tetris
