/**
 * @file LanPeer.hpp
 * @brief Transport TCP IPv4 non bloquant et comptabilisation des octets reels.
 *
 * Le protocole de jeu est volontairement dans Protocol.* : LanPeer transporte
 * uniquement des trames ASCII, sans connaitre les regles de Tetris.
 */
#pragma once
#include "net/LineFramer.hpp"
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace tetris {
enum class LanPhase { Idle, Listening, Connecting, Connected, Disconnected, Failed };
enum class LanRole { None, Host, Client };

/** Une unique connexion socket possedee par cet objet (RAII, non copiable). */
class LanPeer {
public:
    LanPeer() = default;
    ~LanPeer();
    LanPeer(const LanPeer&) = delete;
    LanPeer& operator=(const LanPeer&) = delete;

    bool host(uint16_t port);
    /** Attend uniquement une IP numerique : aucun DNS bloquant dans le thread SDL. */
    bool join(const std::string& ipv4, uint16_t port);
    /** Une tranche d'E/S non bloquantes, bornee a 64 KiB par sens et par frame. */
    void poll();
    /** True = file locale acceptee ; ne signifie pas que l'adversaire a recu. */
    bool send(const std::string& asciiFrame);
    std::vector<std::string> takeMessages();
    void close();
    void fail(const std::string& reason);
    LanPhase phase() const { return phase_; }
    LanRole role() const { return role_; }
    const std::string& error() const { return error_; }
    uint16_t boundPort() const { return boundPort_; }
    uint64_t bytesReceived() const { return bytesReceived_; }
    uint64_t bytesSent() const { return bytesSent_; }
    size_t bytesQueued() const { return queuedBytes_; }
    static std::vector<std::string> localAddresses();

private:
    static bool setNonblocking(int fd);
    void acceptConnection();
    void finishConnecting();
    void readAvailable();
    void writeAvailable();
    void disconnected();
    void clearBuffers();
    static constexpr size_t kIoBudget = 64 * 1024;
    static constexpr size_t kMaxOutgoing = 256 * 1024;

    int socket_ = -1;
    int listener_ = -1;
    LanPhase phase_ = LanPhase::Idle;
    LanRole role_ = LanRole::None;
    uint16_t boundPort_ = 0;
    std::string error_;
    LineFramer framer_;
    // Queue de trames + offset pour ne pas faire erase(0,n) sur chaque send().
    std::deque<std::string> outgoing_;
    size_t outgoingOffset_ = 0;
    size_t queuedBytes_ = 0;
    uint64_t bytesReceived_ = 0;
    uint64_t bytesSent_ = 0;
    std::chrono::steady_clock::time_point connectStarted_{};
};
} // namespace tetris
