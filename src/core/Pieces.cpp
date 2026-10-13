/**
 * @file Pieces.cpp
 * @brief Géométrie normalisée des sept pièces et opérations sur le plateau.
 */
#include "core/Pieces.hpp"
#include <algorithm>
#include <stdexcept>

namespace tetris {
namespace {
// Ramène le coin supérieur gauche au point (0,0) et trie les cases :
// deux rotations équivalentes ont ainsi la même représentation canonique.
Shape normalize(Shape shape) {
    int minX = shape[0].x;
    int minY = shape[0].y;
    for (auto cell : shape) { minX = std::min(minX, cell.x); minY = std::min(minY, cell.y); }
    for (auto& cell : shape) { cell.x -= minX; cell.y -= minY; }
    std::sort(shape.begin(), shape.end(), [](Cell a, Cell b) {
        return a.y == b.y ? a.x < b.x : a.y < b.y;
    });
    return shape;
}

// Rotation de 90° dans un repère écran (ordonnée croissante vers le bas).
Shape rotateClockwise(const Shape& shape) {
    Shape result{};
    for (size_t i = 0; i < shape.size(); ++i)
        result[i] = {shape[i].y, -shape[i].x};
    return normalize(result);
}

// Déduplique les orientations symétriques (I=2, O=1, T=4, etc.).
std::vector<Shape> makeRotations(Shape first) {
    std::vector<Shape> result;
    Shape current = normalize(first);
    for (int i = 0; i < 4; ++i) {
        const bool duplicate = std::any_of(result.begin(), result.end(),
                                           [&](const Shape& s) {
            for (size_t j = 0; j < s.size(); ++j)
                if (s[j].x != current[j].x || s[j].y != current[j].y) return false;
            return true;
        });
        if (!duplicate) result.push_back(current);
        current = rotateClockwise(current);
    }
    return result;
}

// Bibliothèque construite une seule fois : mêmes formes que le projet JS.
const std::array<std::vector<Shape>, 7>& library() {
    static const auto result = std::array<std::vector<Shape>, 7>{
        makeRotations({Cell{0,1}, {1,1}, {2,1}, {3,1}}), // I
        makeRotations({Cell{0,0}, {0,1}, {1,1}, {2,1}}), // J
        makeRotations({Cell{2,0}, {0,1}, {1,1}, {2,1}}), // L
        makeRotations({Cell{1,0}, {2,0}, {1,1}, {2,1}}), // O
        makeRotations({Cell{1,0}, {2,0}, {0,1}, {1,1}}), // S
        makeRotations({Cell{1,0}, {0,1}, {1,1}, {2,1}}), // T
        makeRotations({Cell{0,0}, {1,0}, {1,1}, {2,1}})  // Z
    };
    return result;
}

int typeIndex(char type) {
    switch (type) {
        case 'I': return 0; case 'J': return 1; case 'L': return 2;
        case 'O': return 3; case 'S': return 4; case 'T': return 5;
        case 'Z': return 6;
        default: throw std::invalid_argument("Invalid tetromino type");
    }
}
} // namespace

Board emptyBoard() {
    Board result{};
    for (auto& row : result) row.fill('.');
    return result;
}

// Balayage du bas vers le haut : compacte les lignes conservées et
// laisse des lignes vides en tête. clearedRows se réfère à l'ancienne grille.
ClearResult clearLines(const Board& board) {
    ClearResult result{emptyBoard(), 0, {}};
    int dst = kRows - 1;
    for (int src = kRows - 1; src >= 0; --src) {
        const bool full = std::all_of(board[src].begin(), board[src].end(),
                                      [](char ch) { return ch != '.'; });
        if (full) { result.clearedRows[src] = true; ++result.lines; }
        else result.board[dst--] = board[src];
    }
    return result;
}

const std::vector<Shape>& rotations(char type) { return library().at(typeIndex(type)); }

int shapeWidth(const Shape& shape) {
    int highest = 0;
    for (auto cell : shape) highest = std::max(highest, cell.x);
    return highest + 1;
}

int shapeHeight(const Shape& shape) {
    int highest = 0;
    for (auto cell : shape) highest = std::max(highest, cell.y);
    return highest + 1;
}

// Les cases au-dessus du plafond sont tolérées pendant l'apparition;
// elles deviennent une défaite si la pièce doit s'y verrouiller.
bool collides(const Board& board, const ActivePiece& piece) {
    const auto& rs = rotations(piece.type);
    if (piece.rotation < 0 || piece.rotation >= static_cast<int>(rs.size())) return true;
    for (auto cell : rs[piece.rotation]) {
        const int x = piece.x + cell.x;
        const int y = piece.y + cell.y;
        if (x < 0 || x >= kCols || y >= kRows) return true;
        if (y >= 0 && board[y][x] != '.') return true;
    }
    return false;
}

// Utilisé par la recherche IA pour déposer directement une pièce.
// Valeur sentinelle -1000 quand la position de départ est déjà invalide.
int dropY(const Board& board, char type, int rotation, int x, int startY) {
    ActivePiece piece{type, rotation, x, startY};
    if (collides(board, piece)) return -1000;
    while (true) {
        ++piece.y;
        if (collides(board, piece)) return piece.y - 1;
    }
}
} // namespace tetris
