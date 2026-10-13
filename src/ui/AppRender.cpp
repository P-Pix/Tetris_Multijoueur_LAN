/** @file AppRender.cpp @brief Rendu du menu, des plateaux et des diagnostics. */
#include "ui/App.hpp"
#include <algorithm>

namespace tetris {
namespace {
using namespace palette;
std::string str(int value) { return std::to_string(value); }
}

void App::renderHeader(const std::string& status) {
    canvas_.text(34, 27, "TETRIS", 7, text);
    canvas_.text(298, 40, "AUTO", 4, cyan);
    canvas_.text(424, 48, "C++ / SDL2 / LAN", 3, purple);
    canvas_.text(34, 87, "SDL2 NATIF  |  IA 3-PLY  |  TCP IPV4 V2", 2, dim);
    const int width = std::max(145, canvas_.textWidth(status, 2) + 32);
    canvas_.borderCard(1183 - width, 39, width, 39, panelAlt);
    canvas_.text(1199 - width, 50, status, 2, green);
}

void App::renderButton(int x, int y, int w, int h, const std::string& label,
                       ButtonId id, bool accent) {
    const Color fg = accent ? background : text;
    canvas_.borderCard(x, y, w, h, accent ? cyan : panel);
    if (accent) canvas_.rect(x + 2, y + 2, w - 4, h - 4, cyan);
    canvas_.textCentered(x + w / 2, y + (h - 21) / 2, label, 3, fg);
    buttons_.push_back({x, y, w, h, id});
}

void App::renderMenu() {
    canvas_.textCentered(610, 142, "CHOISIS TON MODE DE JEU", 4, text);
    canvas_.textCentered(610, 188, "EN SOLO OU A DEUX SUR LE MEME RESEAU LOCAL", 2, dim);
    renderButton(65, 290, 345, 115, "F1  SOLO", ButtonId::Solo, true);
    renderButton(438, 290, 345, 115, "F2  HEBERGER", ButtonId::Host);
    renderButton(811, 290, 345, 115, "F3  REJOINDRE", ButtonId::Join);
    canvas_.textCentered(610, 430, "LAN VERSUS : 2 ORDINATEURS, 1 PARTIE, DES LIGNES BONUS", 2, dim);
    canvas_.borderCard(145, 508, 930, 190);
    canvas_.text(182, 545, "SOLO", 3, cyan);
    canvas_.text(182, 591, "JEU CLASSIQUE, HOLD, FANTOME", 2, text);
    canvas_.text(182, 618, "IA AUTOMATIQUE DELLACHERIE+", 2, dim);
    canvas_.line(615, 532, 615, 674, border);
    canvas_.text(655, 545, "VERSUS LAN", 3, green);
    canvas_.text(655, 591, "2 PLATEAUX ET SCORE EN DIRECT", 2, text);
    canvas_.text(655, 618, "ATTAQUES ET REVANCHE", 2, dim);
    canvas_.textCentered(610, 754, "AUCUN SERVICE EXTERNE - PARTIE HEBERGEE CHEZ TOI", 2, dim);
}

void App::renderJoin() {
    canvas_.borderCard(248, 171, 724, 465);
    canvas_.textCentered(610, 215, "REJOINDRE UNE PARTIE LAN", 4, cyan);
    canvas_.text(313, 293, "IP DU JOUEUR QUI HEBERGE", 2, dim);
    canvas_.rect(307, 339, 606, 68, panelAlt);
    canvas_.stroke(307, 339, 606, 68, cyan, 2);
    const std::string view = joinAddress_.substr(std::max<int>(0, static_cast<int>(joinAddress_.size()) - 27));
    canvas_.text(325, 358, view.empty() ? "192.168.1.42:7777" : view + "_", 3,
                 view.empty() ? dim : text);
    canvas_.text(313, 428, "EXEMPLE : 192.168.1.42:7777", 2, dim);
    canvas_.text(313, 458, "ENTREE POUR SE CONNECTER", 2, dim);
    if (!error_.empty()) canvas_.text(313, 490, error_, 2, red);
    renderButton(310, 536, 377, 56, "SE CONNECTER", ButtonId::Connect, true);
    renderButton(708, 536, 203, 56, "RETOUR", ButtonId::Back);
}

void App::renderWaiting() {
    canvas_.borderCard(228, 177, 764, 466);
    const bool host = lan_.role() == LanRole::Host;
    canvas_.textCentered(610, 218, host ? "PARTIE OUVERTE" : "CONNEXION LAN", 5, cyan);
    const bool negotiating = lan_.phase() == LanPhase::Connected && !helloReceived_;
    const std::string label = negotiating ? "NEGOCIATION DU PROTOCOLE LAN..." :
        (host ? "EN ATTENTE DU DEUXIEME JOUEUR" : "CONNEXION AU JOUEUR HOTE...");
    canvas_.textCentered(610, 290, label, 3, text);
    canvas_.text(282, 379, "PORT TCP : " + str(host ? lan_.boundPort() : connectedPort_), 3, green);
    if (host) {
        const auto& addresses = hostAddresses_;
        canvas_.text(282, 432, "TON IP SUR LE RESEAU :", 2, dim);
        int pos = 470;
        for (size_t i = 0; i < addresses.size() && i < 3; ++i) {
            canvas_.text(288, pos, addresses[i] + ":" + str(lan_.boundPort()), 3, text);
            pos += 39;
        }
        if (addresses.empty()) canvas_.text(288, 472, "IP -4 ADDR POUR VERIFIER", 2, dim);
    } else canvas_.text(282, 434, "CIBLE : " + joinAddress_, 3, text);
    renderButton(450, 561, 320, 54, "ANNULER", ButtonId::Back);
}

void App::renderError() {
    canvas_.borderCard(197, 211, 826, 390);
    canvas_.textCentered(610, 250, "CONNEXION INTERROMPUE", 4, red);
    canvas_.textCentered(610, 320, "VERIFIE L'IP, LE PORT ET LE PARE-FEU", 2, dim);
    std::string display = error_;
    if (display.size() > 47) display.resize(47);
    canvas_.textCentered(610, 375, display, 2, text);
    renderButton(440, 475, 340, 66, "MENU PRINCIPAL", ButtonId::Back, true);
}

// Le plateau distant ne connaît que les cases du dernier snapshot.
// Pour le plateau local, on superpose pièce active et fantôme (ghost).
void App::renderBoard(const Board& board, int x, int y, int size, const Game* active) {
    const int width = kCols * size, height = kRows * size;
    canvas_.rect(x - 10, y - 10, width + 20, height + 20, {9, 15, 28});
    canvas_.stroke(x - 10, y - 10, width + 20, height + 20, border, 2);
    canvas_.rect(x, y, width, height, {9, 15, 27});
    for (int row = 0; row <= kRows; ++row)
        canvas_.line(x, y + row * size, x + width, y + row * size, mutedLine);
    for (int col = 0; col <= kCols; ++col)
        canvas_.line(x + col * size, y, x + col * size, y + height, mutedLine);
    for (int row = 0; row < kRows; ++row)
        for (int col = 0; col < kCols; ++col)
            if (board[row][col] != '.')
                canvas_.cell(x + col * size, y + row * size, size, board[row][col]);
    if (!active || active->gameOver()) return;
    auto piece = active->current();
    auto ghost = piece;
    while (true) {
        auto below = ghost;
        ++below.y;
        if (collides(active->board(), below)) break;
        ghost = below;
    }
    for (auto cell : rotations(piece.type)[piece.rotation]) {
        const int gy = ghost.y + cell.y;
        if (gy >= 0) canvas_.cell(x + (ghost.x + cell.x) * size, y + gy * size, size, piece.type, true);
        const int py = piece.y + cell.y;
        if (py >= 0) canvas_.cell(x + (piece.x + cell.x) * size, y + py * size, size, piece.type);
    }
}

void App::renderMini(char type, int x, int y, int width, int height, int size) {
    if (type == '.') return;
    const auto& shape = rotations(type)[0];
    const int startX = x + (width - shapeWidth(shape) * size) / 2;
    const int startY = y + (height - shapeHeight(shape) * size) / 2;
    for (auto c : shape) canvas_.cell(startX + c.x * size, startY + c.y * size, size, type);
}

void App::renderNext(int x, int y, int width, int size, int gap, int limit) {
    const auto& queue = game_.next();
    const int amount = std::min({limit, kNextCount, static_cast<int>(queue.size())});
    for (int i = 0; i < amount; ++i) {
        if (i) canvas_.line(x + 16, y + i * gap, x + width - 16, y + i * gap, mutedLine);
        renderMini(queue[i], x, y + i * gap, width, gap, size);
    }
}

void App::renderStats(int x, int y, int width, const Statistics& stats, bool opponent) {
    canvas_.borderCard(x, y, width, 230);
    canvas_.text(x + 16, y + 20, opponent ? "ADVERSAIRE" : "TON SCORE", 2, dim);
    canvas_.text(x + 16, y + 60, str(stats.score), 4, opponent ? red : cyan);
    canvas_.line(x + 16, y + 117, x + width - 16, y + 117, border);
    canvas_.text(x + 15, y + 129, "LIGNES " + str(stats.lines), 2, text);
    canvas_.text(x + 15, y + 153, "NIVEAU " + str(stats.level), 2, text);
    canvas_.text(x + 15, y + 177, "PIECES " + str(stats.pieces), 2, dim);
    canvas_.text(x + 15, y + 201, "TETRIS " + str(stats.tetrises), 2, dim);
}

void App::renderControls(int x, int y) {
    canvas_.borderCard(x, y, 325, 195);
    canvas_.text(x + 19, y + 17, "RACCOURCIS", 2, dim);
    canvas_.text(x + 19, y + 51, "< >  DEPLACER", 2, text);
    canvas_.text(x + 19, y + 77, "X / Z  ROTATION", 2, text);
    canvas_.text(x + 19, y + 103, "BAS  DESCENDRE", 2, text);
    canvas_.text(x + 19, y + 129, "ESPACE  CHUTE", 2, text);
    canvas_.text(x + 19, y + 155, "C  HOLD", 2, text);
}

void App::renderSolo() {
    renderStats(32, 143, 325, game_.stats(), false);
    canvas_.borderCard(32, 388, 325, 131);
    canvas_.text(49, 408, "HOLD", 2, dim);
    renderMini(game_.holdType(), 106, 431, 176, 73, 22);
    renderControls(32, 533);
    renderBoard(game_.board(), 421, 145, 29, &game_);
    canvas_.borderCard(757, 143, 424, 293);
    canvas_.text(776, 164, "A VENIR", 2, dim);
    renderNext(777, 196, 382, 13, 46, kNextCount);
    canvas_.borderCard(757, 450, 424, 263);
    canvas_.text(774, 471, "MOTEUR IA", 2, cyan);
    canvas_.text(774, 516, "RECHERCHE : 3-PLY", 2, text);
    canvas_.text(774, 543, "EVALUATION : DELLACHERIE+", 2, text);
    canvas_.text(774, 570, "CADENCE : " + str(aiSpeedMs_) + " MS", 2, text);
    canvas_.text(774, 613, "A : " + std::string(autoMode_ ? "ACTIF" : "INACTIF"), 3,
                 autoMode_ ? green : dim);
    canvas_.text(774, 657, "[ / ] : REGLER LA VITESSE", 2, dim);
    renderButton(408, 756, 110, 45, "R", ButtonId::Restart);
    renderButton(528, 756, 110, 45, "P", ButtonId::Pause);
    renderButton(648, 756, 125, 45, "AUTO", ButtonId::Auto, autoMode_);
    if (paused_ || game_.gameOver()) {
        canvas_.borderCard(444, 390, 245, 108, {10, 14, 26});
        canvas_.textCentered(566, 430, paused_ ? "PAUSE" : "GAME OVER", 3,
                             paused_ ? yellow : red);
    }
}

void App::renderCenterPanel() {
    canvas_.borderCard(535, 150, 164, 540, panelAlt);
    canvas_.textCentered(617, 186, "VERSUS", 2, dim);
    canvas_.textCentered(617, 220, "VS", 5, cyan);
    canvas_.line(552, 292, 682, 292, border);
    canvas_.textCentered(617, 317, "HOLD", 2, dim);
    renderMini(game_.holdType(), 546, 341, 140, 61, 15);
    canvas_.line(552, 420, 682, 420, border);
    canvas_.textCentered(617, 449, "SUIVANT", 2, dim);
    renderMini(game_.next().front(), 546, 484, 140, 64, 16);
    canvas_.line(552, 579, 682, 579, border);
    canvas_.textCentered(617, 606, "MALUS", 2, dim);
    canvas_.textCentered(617, 637, str(game_.pendingGarbage()), 3,
                         game_.pendingGarbage() > 0 ? red : green);
}

void App::renderVersus() {
    renderStats(20, 155, 202, game_.stats(), false);
    renderStats(996, 155, 204, remote_.stats, true);
    renderBoard(game_.board(), 245, 158, 27, &game_);
    renderCenterPanel();
    renderBoard(remote_.board, 714, 158, 27, nullptr);
    canvas_.borderCard(20, 405, 201, 242);
    canvas_.text(36, 426, "COMMANDES", 2, cyan);
    canvas_.text(36, 467, "< > BOUGER", 2, text);
    canvas_.text(36, 496, "X Z TOURNER", 2, text);
    canvas_.text(36, 525, "BAS / ESPACE", 2, text);
    canvas_.text(36, 554, "C : HOLD", 2, text);
    canvas_.text(36, 605, "ECHAP MENU", 2, dim);
    canvas_.borderCard(996, 405, 204, 284);
    canvas_.text(1007, 428, "LIGNES BONUS", 2, green);
    canvas_.text(1007, 474, "1 = 0", 2, dim);
    canvas_.text(1007, 503, "2 = 1", 2, text);
    canvas_.text(1007, 532, "3 = 2", 2, text);
    canvas_.text(1007, 561, "4 = 4", 2, cyan);
    const auto latency = networkMonitor_.rttMilliseconds();
    canvas_.text(1007, 596, "TCP " + str(lan_.role() == LanRole::Host ? port_ : connectedPort_), 2, dim);
    canvas_.text(1007, 618, latency ? "RTT " + str(*latency) + " MS" : "RTT EN ATTENTE", 2,
                 latency ? green : dim);
    canvas_.text(1007, 641, "TX " + str(static_cast<int>(lan_.bytesSent() / 1024)) + " KIO", 2, dim);
    canvas_.text(1007, 661, "RX " + str(static_cast<int>(lan_.bytesReceived() / 1024)) + " KIO", 2, dim);
    canvas_.textCentered(379, 730, "TON PLATEAU", 3, cyan);
    canvas_.textCentered(849, 730, remote_.received ? "ADVERSAIRE" : "SYNCHRO...", 3, red);
    canvas_.textCentered(610, 789, screen_ == Screen::Finished ? "MATCH TERMINE - R POUR REVANCHE" :
                         (clientReportedDeath_ ? "RESULTAT EN ATTENTE..." :
                         "LE PREMIER GAME OVER PERD - ECHAP POUR SORTIR"), 2, dim);
}

void App::renderFinished() {
    renderVersus();
    canvas_.borderCard(380, 281, 462, 262, {13, 22, 38});
    canvas_.textCentered(610, 324, result_ == Result::Win ? "VICTOIRE" : "DEFAITE", 5,
                         result_ == Result::Win ? green : red);
    canvas_.textCentered(610, 391, "REVANCHE LAN ?", 3, text);
    renderButton(432, 446, 355, 55, localReady_ ? "EN ATTENTE..." : "R  REVANCHE",
                 ButtonId::Rematch, !localReady_);
}

// Reconstruction des zones cliquables à chaque frame, uniquement à
// partir des boutons visibles dans l'écran courant.
void App::render() {
    buttons_.clear();
    canvas_.begin();
    std::string status = "MENU";
    if (screen_ == Screen::Solo) status = autoMode_ ? "AUTO IA" : "SOLO";
    if (screen_ == Screen::Waiting) status = "LAN EN ATTENTE";
    if (screen_ == Screen::Versus) status = "LAN EN COURS";
    if (screen_ == Screen::Finished) status = "FIN DE MATCH";
    if (screen_ == Screen::Error) status = "ERREUR LAN";
    renderHeader(status);
    switch (screen_) {
        case Screen::Menu: renderMenu(); break;
        case Screen::JoinForm: renderJoin(); break;
        case Screen::Waiting: renderWaiting(); break;
        case Screen::Error: renderError(); break;
        case Screen::Solo: renderSolo(); break;
        case Screen::Versus: renderVersus(); break;
        case Screen::Finished: renderFinished(); break;
    }
    canvas_.finish();
}
} // namespace tetris
