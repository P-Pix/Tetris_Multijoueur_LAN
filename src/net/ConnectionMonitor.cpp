/** @file ConnectionMonitor.cpp @brief Temporisations sans comparaison d'horloges murales. */
#include "net/ConnectionMonitor.hpp"
#include <algorithm>

namespace tetris {
void ConnectionMonitor::connected(Time now) {
    sinceConnect_ = lastRx_ = now;
    lastPing_ = now - std::chrono::seconds(2);
    waitingPong_ = false;
    nextNonce_ = outstanding_ = 0;
    lastRttMs_.reset();
}

bool ConnectionMonitor::handshakeExpired(Time now) const {
    return now - sinceConnect_ > std::chrono::seconds(5);
}

bool ConnectionMonitor::peerExpired(Time now) const {
    return now - lastRx_ > std::chrono::seconds(8);
}

bool ConnectionMonitor::probeExpired(Time now) const {
    return waitingPong_ && now - lastPing_ > std::chrono::seconds(5);
}

bool ConnectionMonitor::pingDue(Time now) const {
    return !waitingPong_ && now - lastPing_ >= std::chrono::seconds(1);
}

uint32_t ConnectionMonitor::markPing(Time now) {
    if (++nextNonce_ == 0) ++nextNonce_;
    outstanding_ = nextNonce_;
    lastPing_ = now;
    waitingPong_ = true;
    return outstanding_;
}

void ConnectionMonitor::onPong(uint32_t nonce, Time now) {
    if (!waitingPong_ || nonce != outstanding_) return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPing_).count();
    lastRttMs_ = static_cast<int>(std::clamp<int64_t>(elapsed, 0, 60000));
    waitingPong_ = false;
}
} // namespace tetris
