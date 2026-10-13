/**
 * @file Game.cpp
 * @brief Implémentation déterministe du jeu : 7-bag, verrouillage et attaques.
 */
#include "core/Game.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>

namespace tetris {
Game::Game(uint32_t seed) { reset(seed); }

// La graine de manche assure un ordre de pièces identique aux deux joueurs.
void Game::reset(uint32_t seed) {
    board_ = emptyBoard();
    stats_ = {};
    queue_.clear(); bag_.clear();
    rng_.seed(seed);
    // 0x9e3779b9 est un mélange de graine classique (« golden ratio »).
    // Il sépare les trous de malus du 7-bag, sans imposer une nouvelle seed LAN.
    garbageRng_.seed(seed ^ 0x9e3779b9U);
    holdType_ = '.'; canHold_ = true; gameOver_ = false;
    gravityMs_ = 0; pendingGarbage_ = 0; outgoingGarbage_ = 0;
    ensureQueue();
    spawnNext();
}

void Game::refillBag() {
    std::array<char, 7> types{'I', 'J', 'L', 'O', 'S', 'T', 'Z'};
    // Shuffle Fisher-Yates avec mt19937 et modulo au lieu de std::shuffle :
    // même seed = même ordre sur les postes utilisant ce code/protocole.
    for (int i = 6; i > 0; --i) {
        const int j = static_cast<int>(rng_() % static_cast<uint32_t>(i + 1));
        std::swap(types[i], types[j]);
    }
    for (auto type : types) bag_.push_back(type);
}

char Game::drawType() {
    if (bag_.size() < 7) refillBag();
    const char type = bag_.front();
    bag_.pop_front();
    return type;
}

void Game::ensureQueue() {
    while (queue_.size() < kNextCount + 4) queue_.push_back(drawType());
}

ActivePiece Game::makePiece(char type) const {
    const int width = shapeWidth(rotations(type)[0]);
    return {type, 0, (kCols - width) / 2, -1};
}

// Retire la première pièce attendue ; canHold_ n'est réarmé qu'au spawn.
void Game::spawnNext() {
    ensureQueue();
    current_ = makePiece(queue_.front());
    queue_.pop_front();
    ensureQueue();
    canHold_ = true;
    ++stats_.pieces;
    if (collides(board_, current_)) setGameOver();
}

bool Game::move(int dx) {
    auto candidate = current_;
    candidate.x += dx;
    if (collides(board_, candidate)) return false;
    current_ = candidate;
    return true;
}

// Wall-kicks minimaux : on essaie la rotation au centre puis +/-1 et +/-2.
// Ce n'est pas le système de rotation SRS moderne.
bool Game::rotate(int direction) {
    const int count = static_cast<int>(rotations(current_.type).size());
    if (count == 1) return true;
    const int index = (current_.rotation + (direction > 0 ? 1 : -1) + count) % count;
    for (int kick : {0, -1, 1, -2, 2}) {
        auto candidate = current_;
        candidate.rotation = index;
        candidate.x += kick;
        if (!collides(board_, candidate)) { current_ = candidate; return true; }
    }
    return false;
}

bool Game::softDrop(bool manual) {
    auto candidate = current_;
    ++candidate.y;
    if (!collides(board_, candidate)) {
        current_ = candidate;
        if (manual) ++stats_.score;
        return true;
    }
    lock();
    return false;
}

bool Game::hardDrop(bool manual) {
    int distance = 0;
    while (true) {
        auto candidate = current_;
        ++candidate.y;
        if (collides(board_, candidate)) break;
        current_ = candidate;
        ++distance;
    }
    if (manual) stats_.score += distance * 2;
    lock();
    return true;
}

// Un seul échange autorisé par pièce verrouillée. Le premier hold prend
// la pièce suivante et incrémente aussi le compteur « pièces ».
bool Game::hold() {
    if (!canHold_) return false;
    const char old = holdType_;
    holdType_ = current_.type;
    if (old != '.') {
        current_ = makePiece(old);
        if (collides(board_, current_)) setGameOver();
    } else spawnNext();
    canHold_ = false;
    return true;
}

bool Game::act(Action action) {
    if (gameOver_) return false;
    switch (action) {
        case Action::Left: return move(-1);
        case Action::Right: return move(1);
        case Action::RotateCW: return rotate(1);
        case Action::RotateCCW: return rotate(-1);
        case Action::SoftDrop: return softDrop(true);
        case Action::HardDrop: return hardDrop(true);
        case Action::Hold: return hold();
    }
    return false;
}

// Le moteur IA place directement la pièce au dernier cran possible ;
// il ne cherche pas une séquence de touches accessible pour y parvenir.
bool Game::forcePlacement(int rotation, int x, int y) {
    if (gameOver_) return false;
    ActivePiece candidate{current_.type, rotation, x, y};
    if (collides(board_, candidate)) return false;
    auto below = candidate;
    ++below.y;
    if (!collides(board_, below)) return false;
    current_ = candidate;
    lock();
    return true;
}

int Game::gravityInterval() const { return std::max(85, 820 - (stats_.level - 1) * 65); }

// Temps reçu par App ; la tranche par appel est bornée à 1000 ms pour
// éviter un rattrapage illimité après une longue suspension du processus.
void Game::tick(int milliseconds) {
    if (gameOver_ || milliseconds <= 0) return;
    gravityMs_ += std::min(milliseconds, 1000);
    while (gravityMs_ >= gravityInterval() && !gameOver_) {
        gravityMs_ -= gravityInterval();
        softDrop(false);
    }
}

// Le verrouillage au-dessus du plafond fait perdre la partie.
bool Game::addLockedCells() {
    for (auto cell : rotations(current_.type)[current_.rotation]) {
        const int x = current_.x + cell.x;
        const int y = current_.y + cell.y;
        if (y < 0) return false;
        board_[y][x] = current_.type;
    }
    return true;
}

// Barème d'origine : 100/300/500/800 points multipliés par le niveau.
void Game::updateScore(const ClearResult& result) {
    static constexpr std::array<int, 5> scores{0, 100, 300, 500, 800};
    if (result.lines == 0) return;
    stats_.score += scores.at(result.lines) * stats_.level;
    stats_.lines += result.lines;
    if (result.lines == 4) ++stats_.tetrises;
    stats_.level = 1 + stats_.lines / 10;
}

int Game::attackFor(int lines) const {
    static constexpr std::array<int, 5> attacks{0, 0, 1, 2, 4};
    return attacks.at(std::clamp(lines, 0, 4));
}

// Compensation : on annule d'abord des attaques entrantes en attente ;
// l'excédent de la ligne offensive devient un malus sortant. Le reliquat
// entrant est ensuite appliqué localement en bas du plateau.
void Game::resolveGarbage(int cleared) {
    int attack = attackFor(cleared);
    const int cancelled = std::min(attack, pendingGarbage_);
    attack -= cancelled;
    pendingGarbage_ -= cancelled;
    outgoingGarbage_ += attack;
    if (pendingGarbage_ > 0) insertGarbage(pendingGarbage_);
    pendingGarbage_ = 0;
}

// Chaque ligne grise pousse le plateau vers le haut et crée un trou
// pseudo-aléatoire. Si le plafond était occupé, c'est un top-out.
void Game::insertGarbage(int lines) {
    const int count = std::min(lines, kRows);
    for (int i = 0; i < count; ++i) {
        if (std::any_of(board_[0].begin(), board_[0].end(), [](char ch) { return ch != '.'; }))
            setGameOver();
        for (int y = 0; y < kRows - 1; ++y) board_[y] = board_[y + 1];
        board_[kRows - 1].fill('#');
        board_[kRows - 1][garbageRng_() % kCols] = '.';
    }
}

// Ordre important : fixer -> supprimer lignes -> score -> malus -> spawn.
void Game::lock() {
    if (!addLockedCells()) { setGameOver(); return; }
    auto cleared = clearLines(board_);
    board_ = cleared.board;
    updateScore(cleared);
    resolveGarbage(cleared.lines);
    if (!gameOver_) spawnNext();
    gravityMs_ = 0;
}

void Game::receiveGarbage(int lines) {
    if (!gameOver_ && lines > 0) pendingGarbage_ = std::min(20, pendingGarbage_ + std::min(lines, 20));
}

int Game::takeOutgoingGarbage() {
    const int result = outgoingGarbage_;
    outgoingGarbage_ = 0;
    return result;
}

// La pièce active est dessinée dans la copie envoyée à l'adversaire.
// L'état métier board_ conserve uniquement les blocs déjà fixés.
Board Game::visibleBoard() const {
    Board out = board_;
    if (gameOver_) return out;
    for (auto cell : rotations(current_.type)[current_.rotation]) {
        const int x = current_.x + cell.x, y = current_.y + cell.y;
        if (x >= 0 && x < kCols && y >= 0 && y < kRows) out[y][x] = current_.type;
    }
    return out;
}
} // namespace tetris
