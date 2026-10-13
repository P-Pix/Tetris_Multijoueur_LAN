/** @file App.cpp @brief Boucle SDL, coordination du moteur et cadence de simulation. */
#include "ui/App.hpp"
#include <algorithm>
#include <chrono>
#include <thread>

namespace tetris {
App::App(uint16_t port, const std::string& command, const std::string& address)
    : port_(port), connectedPort_(port), joinAddress_(address),
      seedRng_(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())) {
    if (command == "solo") startSolo();
    if (command == "host") startHost();
    if (command == "join") connectToHost();
}

uint32_t App::randomSeed() { return seedRng_(); }

// Boucle mono-thread : evenements -> simulation -> rendu. Chaque operation
// reseau dispose d'un budget d'octets ; aucune attente bloquante sur socket.
int App::run() {
    if (!canvas_.isOpen()) return 1;
    auto previous = std::chrono::steady_clock::now();
    while (running_) {
        processEvents();
        if (!running_) break;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - previous);
        previous = now;
        update(std::clamp(static_cast<int>(elapsed.count()), 0, 100));
        render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return 0;
}

// L'IA n'agit qu'en solo : recherche 3-ply puis pose instantanée.
void App::updateSolo(int ms) {
    if (paused_ || game_.gameOver()) return;
    if (!autoMode_) { game_.tick(ms); return; }
    autoElapsedMs_ += ms;
    if (autoElapsedMs_ < aiSpeedMs_) return;
    autoElapsedMs_ = 0;
    const AutoMove move = ai_.bestMove(game_);
    if (!move.valid) game_.fail();
    else game_.forcePlacement(move.rotation, move.x, move.y);
}

void App::update(int elapsedMs) {
    if (screen_ == Screen::Solo) updateSolo(elapsedMs);
    if (screen_ == Screen::Waiting || screen_ == Screen::Versus || screen_ == Screen::Finished)
        updateNetwork(elapsedMs);
}

// Les entrées manuelles sont bloquées en solo automatique ; en versus,
// elles déclenchent immédiatement la transmission d'éventuels malus.
void App::action(Action move) {
    if (screen_ == Screen::Solo && !paused_ && !autoMode_) game_.act(move);
    if (screen_ == Screen::Versus && !game_.gameOver()) game_.act(move);
    if (screen_ == Screen::Versus) { sendOutgoingGarbage(); tryFinishRound(); }
}

} // namespace tetris
