/** @file ConnectionMonitor.hpp @brief Suivi temporel testable avec une horloge monotone. */
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>

namespace tetris {
/** Heartbeat applicatif : delais et RTT, sans threads ni acces au socket. */
class ConnectionMonitor {
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;
    void connected(Time now);
    void received(Time now) { lastRx_ = now; }
    bool handshakeExpired(Time now) const;
    bool peerExpired(Time now) const;
    bool probeExpired(Time now) const;
    bool pingDue(Time now) const;
    uint32_t markPing(Time now);
    void onPong(uint32_t nonce, Time now);
    std::optional<int> rttMilliseconds() const { return lastRttMs_; }
private:
    Time sinceConnect_{};
    Time lastRx_{};
    Time lastPing_{};
    uint32_t nextNonce_ = 0;
    uint32_t outstanding_ = 0;
    bool waitingPong_ = false;
    std::optional<int> lastRttMs_;
};
} // namespace tetris
