/** @file LanPeer.cpp @brief Sockets Linux : recv/send partiels, borne I/O, RAII. */
#include "net/LanPeer.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <set>

namespace tetris {
namespace {
void closeFd(int& fd) { if (fd != -1) { ::close(fd); fd = -1; } }
bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
std::string systemError(const char* operation) {
    return std::string(operation) + ": " + std::strerror(errno);
}
void lowLatency(int fd) {
    int enable = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
}
} // namespace

LanPeer::~LanPeer() { close(); }

void LanPeer::clearBuffers() {
    framer_.clear(); outgoing_.clear();
    outgoingOffset_ = queuedBytes_ = 0;
}

void LanPeer::close() {
    closeFd(socket_); closeFd(listener_);
    clearBuffers();
    phase_ = LanPhase::Idle;
    role_ = LanRole::None;
    error_.clear(); boundPort_ = 0;
    bytesReceived_ = bytesSent_ = 0;
}

void LanPeer::fail(const std::string& reason) {
    error_ = reason;
    closeFd(socket_); closeFd(listener_);
    clearBuffers();
    phase_ = LanPhase::Failed;
}

bool LanPeer::setNonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags != -1 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool LanPeer::host(uint16_t port) {
    close(); role_ = LanRole::Host;
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0) { fail(systemError("socket")); return false; }
    int reuse = 1;
    ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(listener_, 1) < 0 || !setNonblocking(listener_)) {
        fail(systemError("Ecoute TCP")); return false;
    }
    socklen_t length = sizeof(addr);
    if (::getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &length) == 0)
        boundPort_ = ntohs(addr.sin_port);
    phase_ = LanPhase::Listening;
    return true;
}

// inet_pton remplace getaddrinfo : une resolution DNS pouvait figer la frame SDL.
bool LanPeer::join(const std::string& ipv4, uint16_t port) {
    close(); role_ = LanRole::Client;
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    if (::inet_pton(AF_INET, ipv4.c_str(), &target.sin_addr) != 1) {
        fail("Adresse IPv4 numerique invalide"); return false;
    }
    socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ < 0 || !setNonblocking(socket_)) {
        fail(systemError("Socket client")); return false;
    }
    const int rc = ::connect(socket_, reinterpret_cast<sockaddr*>(&target), sizeof(target));
    if (rc == 0) { phase_ = LanPhase::Connected; lowLatency(socket_); }
    else if (errno == EINPROGRESS) phase_ = LanPhase::Connecting;
    else { fail(systemError("Connexion TCP")); return false; }
    connectStarted_ = std::chrono::steady_clock::now();
    return true;
}

void LanPeer::acceptConnection() {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    const int fd = ::accept(listener_, reinterpret_cast<sockaddr*>(&address), &length);
    if (fd < 0) {
        if (!wouldBlock() && errno != EINTR) fail(systemError("accept"));
        return;
    }
    socket_ = fd;
    closeFd(listener_);
    if (!setNonblocking(socket_)) { fail(systemError("Socket accepte")); return; }
    lowLatency(socket_);
    phase_ = LanPhase::Connected;
}

void LanPeer::finishConnecting() {
    if (std::chrono::steady_clock::now() - connectStarted_ > std::chrono::seconds(5)) {
        fail("Delai de connexion TCP depasse (5s)"); return;
    }
    fd_set writable;
    FD_ZERO(&writable); FD_SET(socket_, &writable);
    timeval timeout{};
    const int ready = ::select(socket_ + 1, nullptr, &writable, nullptr, &timeout);
    if (ready < 0) { if (errno != EINTR) fail(systemError("select")); return; }
    if (ready == 0) return;
    int code = 0; socklen_t length = sizeof(code);
    if (::getsockopt(socket_, SOL_SOCKET, SO_ERROR, &code, &length) < 0 || code != 0) {
        if (code) errno = code;
        fail(systemError("Connexion impossible")); return;
    }
    lowLatency(socket_);
    phase_ = LanPhase::Connected;
}

void LanPeer::disconnected() {
    closeFd(socket_);
    phase_ = LanPhase::Disconnected;
    error_ = "L'autre joueur s'est deconnecte";
    clearBuffers();
}

// Le nombre d'octets recus est independant du nombre de trames completes.
void LanPeer::readAvailable() {
    char buffer[4096];
    size_t budget = kIoBudget;
    while (budget > 0 && phase_ == LanPhase::Connected) {
        const size_t amount = std::min(budget, sizeof(buffer));
        const ssize_t n = ::recv(socket_, buffer, amount, 0);
        if (n == 0) { disconnected(); return; }
        if (n < 0) {
            if (wouldBlock() || errno == EINTR) return;
            fail(systemError("Reception TCP")); return;
        }
        bytesReceived_ += static_cast<uint64_t>(n);
        budget -= static_cast<size_t>(n);
        if (!framer_.feed(std::string_view(buffer, static_cast<size_t>(n)))) {
            fail(framer_.error()); return;
        }
    }
}

void LanPeer::writeAvailable() {
    size_t budget = kIoBudget;
    while (budget > 0 && !outgoing_.empty() && phase_ == LanPhase::Connected) {
        const auto& frame = outgoing_.front();
        const size_t amount = std::min(budget, frame.size() - outgoingOffset_);
        const ssize_t n = ::send(socket_, frame.data() + outgoingOffset_, amount, MSG_NOSIGNAL);
        if (n < 0) {
            if (wouldBlock() || errno == EINTR) return;
            fail(systemError("Envoi TCP")); return;
        }
        if (n == 0) return;
        outgoingOffset_ += static_cast<size_t>(n);
        queuedBytes_ -= static_cast<size_t>(n);
        bytesSent_ += static_cast<uint64_t>(n);
        budget -= static_cast<size_t>(n);
        if (outgoingOffset_ == frame.size()) { outgoing_.pop_front(); outgoingOffset_ = 0; }
    }
}

void LanPeer::poll() {
    if (phase_ == LanPhase::Listening) acceptConnection();
    if (phase_ == LanPhase::Connecting) finishConnecting();
    if (phase_ == LanPhase::Connected) {
        readAvailable();
        if (phase_ == LanPhase::Connected) writeAvailable();
    }
}

bool LanPeer::send(const std::string& frame) {
    if (phase_ != LanPhase::Connected || frame.empty() || frame.size() > protocol::kMaxFrame ||
        queuedBytes_ + frame.size() + 1 > kMaxOutgoing) return false;
    for (unsigned char c : frame) if (c < 32 || c > 126) return false;
    outgoing_.push_back(frame + '\n');
    queuedBytes_ += frame.size() + 1;
    return true;
}

std::vector<std::string> LanPeer::takeMessages() { return framer_.take(); }

std::vector<std::string> LanPeer::localAddresses() {
    std::set<std::string> addresses;
    ifaddrs* interfaces = nullptr;
    if (::getifaddrs(&interfaces) != 0) return {};
    for (auto* item = interfaces; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET) continue;
        char ipv4[INET_ADDRSTRLEN]{};
        const auto* addr = reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
        if (!::inet_ntop(AF_INET, &addr->sin_addr, ipv4, sizeof(ipv4))) continue;
        if (std::string(ipv4).rfind("127.", 0) != 0) addresses.emplace(ipv4);
    }
    ::freeifaddrs(interfaces);
    return {addresses.begin(), addresses.end()};
}
} // namespace tetris
