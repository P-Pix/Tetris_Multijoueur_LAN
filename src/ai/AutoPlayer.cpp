/**
 * @file AutoPlayer.cpp
 * @brief Recherche 3-ply et fonction heuristique pondérée El-Tetris / Dellacherie+.
 */
#include "ai/AutoPlayer.hpp"
#include "core/Game.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>

namespace tetris {
namespace {
// Jeu de poids historique d'El-Tetris (variante à six métriques Dellacherie).
// Référence exacte : https://github.com/YuhanXiaoJY/Implementation-of-El-Tetris
// Les chiffres à nombreuses décimales sont des paramètres heuristiques, pas
// des constantes physiques ; ne pas les arrondir sans mesure de régression.
namespace weights {
constexpr double kLandingHeight = -4.500158825082766;
constexpr double kErodedPieceCells = 3.4181268101392694;
constexpr double kRowTransitions = -3.2178882868487753;
constexpr double kColumnTransitions = -9.348695305445199;
constexpr double kHoles = -7.899265427351652;
constexpr double kWells = -3.3855972247263626;

// Paramètres supplémentaires apportés dans app.js (adaptation du projet JS).
constexpr double kClearedLinesSquared = 1.8;
constexpr double kTotalHeightPenalty = 0.18;
constexpr double kBumpinessPenalty = 0.24;
constexpr int kDangerHeight = 16;
constexpr double kDangerHeightStep = 18.0;
constexpr int kCriticalHeight = 19;
constexpr double kCriticalHeightPenalty = 200.0;
// Poids relatif de chaque niveau prospectif de la recherche 3-ply.
constexpr double kFutureDiscount = 0.64;
} // namespace weights

// Construit un plateau hypothétique ; le moteur Game n'est pas modifié.
Board place(const Board& board, char type, int rotation, int x, int y) {
    Board out = board;
    for (auto cell : rotations(type)[rotation]) {
        const int yy = y + cell.y;
        if (yy < 0) return emptyBoard(); // l'appelant valide l'occupation avant
        out[yy][x + cell.x] = type;
    }
    return out;
}

// Mesure d'« érosion » : lignes complétées x cases de la pièce retirées.
int erodedCells(const Shape& shape, int y, const ClearResult& result) {
    int cells = 0;
    for (auto cell : shape)
        if (result.clearedRows[y + cell.y]) ++cells;
    return result.lines * cells;
}

std::array<int, kCols> heightsOf(const Board& b) {
    std::array<int, kCols> heights{};
    for (int x = 0; x < kCols; ++x)
        for (int y = 0; y < kRows; ++y)
            if (b[y][x] != '.') { heights[x] = kRows - y; break; }
    return heights;
}

// Une case vide compte comme trou si elle a au moins un bloc au-dessus.
int holesIn(const Board& b) {
    int holes = 0;
    for (int x = 0; x < kCols; ++x) {
        bool seen = false;
        for (int y = 0; y < kRows; ++y) {
            if (b[y][x] != '.') seen = true;
            else if (seen) ++holes;
        }
    }
    return holes;
}

// Les deux murs latéraux sont considérés comme occupés ; chaque
// passage plein <-> vide est pénalisé.
int rowTransitions(const Board& b) {
    int total = 0;
    for (const auto& row : b) {
        bool prev = true;
        for (char c : row) {
            const bool occupied = (c != '.');
            if (occupied != prev) ++total;
            prev = occupied;
        }
        if (!prev) ++total;
    }
    return total;
}

// Le bord supérieur et le sol sont considérés comme occupés.
int columnTransitions(const Board& b) {
    int total = 0;
    for (int x = 0; x < kCols; ++x) {
        bool prev = true;
        for (int y = 0; y < kRows; ++y) {
            const bool occupied = (b[y][x] != '.');
            if (occupied != prev) ++total;
            prev = occupied;
        }
        if (!prev) ++total;
    }
    return total;
}

// Somme triangulaire des profondeurs de puits : 1+2+...+profondeur.
int wellSums(const Board& b) {
    int total = 0;
    for (int x = 0; x < kCols; ++x) {
        int depth = 0;
        for (int y = 0; y < kRows; ++y) {
            const bool left = x == 0 || b[y][x - 1] != '.';
            const bool right = x == kCols - 1 || b[y][x + 1] != '.';
            if (b[y][x] == '.' && left && right) total += ++depth;
            else depth = 0;
        }
    }
    return total;
}

int bumpiness(const std::array<int, kCols>& heights) {
    int total = 0;
    for (int x = 0; x + 1 < kCols; ++x)
        total += std::abs(heights[x] - heights[x + 1]);
    return total;
}

// Cache de recherche : 20 masques 10 bits (2 octets par ligne) + type +
// profondeur. L'IA évalue l'occupation, jamais la couleur des cellules.
std::string boardKey(const Board& board, char type, int depth) {
    std::string key;
    key.reserve(kRows * 2 + 4);
    key += type;
    key += static_cast<char>('0' + depth);
    for (const auto& row : board) {
        uint16_t mask = 0;
        for (int x = 0; x < kCols; ++x)
            if (row[x] != '.') mask |= static_cast<uint16_t>(1U << x);
        key += static_cast<char>(mask & 255);
        key += static_cast<char>(mask >> 8);
    }
    return key;
}

struct SearchResult {
    double score = -1e12;
    AutoMove move{};
    bool valid = false;
};

using Cache = std::unordered_map<std::string, double>;

// Descente récursive : essaie toutes les positions de chaque pièce.
// Seules les notes des sous-arbres (depth <= 2) sont mémoïsées ; le choix
// de coup n'a de sens qu'au niveau racine.
SearchResult search(const Board& board, const std::array<char, 3>& types,
                    int depth, Cache& cache) {
    constexpr double discount = weights::kFutureDiscount;
    const char type = types[3 - depth];
    const std::string key = depth <= 2 ? boardKey(board, type, depth) : std::string{};
    if (!key.empty()) {
        auto it = cache.find(key);
        if (it != cache.end()) return {it->second, {}, true};
    }
    SearchResult best;
    for (const auto& move : enumerateMoves(board, type)) {
        double value = evaluateMove(move);
        if (depth > 1) value += discount * search(move.board, types, depth - 1, cache).score;
        if (!best.valid || value > best.score) best = {value, move, true};
    }
    if (!key.empty()) cache.emplace(key, best.score);
    return best;
}
} // namespace

// Énumération exhaustive des rotations/colonnes avec « chute verticale ».
// Les poses qui resteraient au-dessus du plateau sont ignorées.
std::vector<AutoMove> enumerateMoves(const Board& board, char type) {
    std::vector<AutoMove> moves;
    const auto& rs = rotations(type);
    for (int r = 0; r < static_cast<int>(rs.size()); ++r) {
        const Shape& shape = rs[r];
        for (int x = 0; x <= kCols - shapeWidth(shape); ++x) {
            const int y = dropY(board, type, r, x);
            if (y < -10) continue;
            const bool aboveTop = std::any_of(shape.begin(), shape.end(),
                                               [y](Cell cell) { return y + cell.y < 0; });
            if (aboveTop) continue;
            const auto clear = clearLines(place(board, type, r, x, y));
            double avgY = 0;
            for (auto cell : shape) avgY += y + cell.y;
            moves.push_back({x, y, r, clear.board, clear.lines,
                             kRows - avgY / 4, erodedCells(shape, y, clear), true});
        }
    }
    return moves;
}

// La somme pondérée est un « score de qualité du plateau », distinct des
// points de jeu. Plus la valeur est grande, plus le placement est souhaitable.
double evaluateMove(const AutoMove& move) {
    const auto heights = heightsOf(move.board);
    const int peak = *std::max_element(heights.begin(), heights.end());
    int totalHeight = 0;
    for (int h : heights) totalHeight += h;
    double value = weights::kLandingHeight * move.landingHeight
                 + weights::kErodedPieceCells * move.erodedPieceCells
                 + weights::kRowTransitions * rowTransitions(move.board)
                 + weights::kColumnTransitions * columnTransitions(move.board)
                 + weights::kHoles * holesIn(move.board)
                 + weights::kWells * wellSums(move.board);
    value += weights::kClearedLinesSquared * move.lines * move.lines;
    value -= weights::kTotalHeightPenalty * totalHeight
           + weights::kBumpinessPenalty * bumpiness(heights);
    if (peak >= weights::kDangerHeight)
        value -= (peak - (weights::kDangerHeight - 1)) * weights::kDangerHeightStep;
    if (peak >= weights::kCriticalHeight) value -= weights::kCriticalHeightPenalty;
    return value;
}

// Aucune mutation pendant la recherche ; l'exécution appartient à App
// via Game::forcePlacement. Le hold n'est pas exploré par cette IA.
AutoMove AutoPlayer::bestMove(const Game& game) const {
    if (game.gameOver() || game.next().size() < 2) return {};
    const std::array<char, 3> types{game.current().type, game.next()[0], game.next()[1]};
    Cache cache;
    return search(game.board(), types, 3, cache).move;
}
} // namespace tetris
