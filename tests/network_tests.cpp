/**
 * @file network_tests.cpp
 * @brief Tests de contrat du protocole, framing, transport TCP et horloge RTT.
 *
 * Aucun framework externe : chaque CHECK affiche sa ligne avant de quitter.
 */
#include "net/ConnectionMonitor.hpp"
#include "net/LanPeer.hpp"
#include "net/LineFramer.hpp"
#include "net/Protocol.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace tetris;
using namespace std::chrono_literals;

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "ERREUR " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
    std::exit(1); \
} } while (false)

void validFrame(const std::string& encoded, protocol::Type expected) {
    auto parsed = protocol::decode(encoded);
    CHECK(parsed);
    CHECK(parsed.message->type == expected);
}

void invalidFrame(std::string_view frame) {
    const auto parsed = protocol::decode(frame);
    CHECK(!parsed);
    CHECK(!parsed.error.empty());
}

void testCodec() {
    using namespace protocol;
    validFrame(hello(), Type::Hello);
    validFrame(newRound(1, 4294967295U), Type::NewRound);
    validFrame(garbage(42, 4), Type::Garbage);
    validFrame(death(99), Type::Death);
    validFrame(winner(15, 'H'), Type::Winner);
    validFrame(winner(15, 'C'), Type::Winner);
    validFrame(rematch(15), Type::Rematch);
    validFrame(ping(987654U), Type::Ping);
    validFrame(pong(987654U), Type::Pong);
    Board board = emptyBoard();
    board[19][0] = 'I'; board[19][9] = '#';
    Statistics stats{500, 11, 2, 19, 1};
    auto parsed = decode(snapshot(3, stats, board));
    CHECK(parsed && parsed.message->round == 3);
    CHECK(parsed.message->stats.score == 500);
    CHECK(parsed.message->stats.level == 2);
    CHECK(parsed.message->board == board);
    CHECK(decode(newRound(7, 4294967295U)).message->seed == 4294967295U);
}

void testMalformedFrames() {
    for (const auto invalid : {"", "H|1", "H|999", "H|2|EXTRA", "X|1", "N|1|-7",
                              "N|0|123", "N|1|4294967296", "G|1|0", "G|1|5",
                              "G|1|4|extra", "D|1|extra", "W|1|X", "R|-1",
                              "P|-1", "Q|4294967296", "H|2\r", "PING\n"})
        invalidFrame(invalid);
    invalidFrame(std::string(2049, 'X'));
    auto badStats = protocol::snapshot(1, Statistics{50, 10, 1, 1, 0}, emptyBoard());
    invalidFrame(badStats); // le niveau correct apres dix lignes est 2
    auto badBoard = protocol::snapshot(1, Statistics{0, 0, 1, 1, 0}, emptyBoard());
    badBoard.back() = '$';
    invalidFrame(badBoard);
    badBoard.pop_back();
    invalidFrame(badBoard);
}

void testLineFraming() {
    LineFramer framer;
    CHECK(framer.feed("N|1|12"));
    CHECK(framer.take().empty());
    CHECK(framer.feed("34\nP|7\nG|1"));
    auto frames = framer.take();
    CHECK(frames.size() == 2);
    CHECK(frames[0] == "N|1|1234");
    CHECK(frames[1] == "P|7");
    CHECK(framer.feed("|4\n"));
    CHECK(framer.take() == std::vector<std::string>{"G|1|4"});
    CHECK(!framer.feed("\n")); // une ligne vide est interdite
    CHECK(!framer.feed("H|2\n")); // erreur persistante, pas de reprise implicite
    framer.clear();
    CHECK(framer.feed("H|2\n"));
    CHECK(framer.take() == std::vector<std::string>{"H|2"});
    framer.clear();
    CHECK(!framer.feed(std::string(2049, 'A')));
    framer.clear();
    CHECK(!framer.feed(std::string_view("H|2\0", 4))); // les octets NUL sont interdits
    framer.clear();
    for (int i = 0; i < 200; ++i) CHECK(framer.feed("P|1\n"));
    CHECK(!framer.feed("P|2\n")); // pas de croissance non bornee de la file
}

void testMonitor() {
    ConnectionMonitor monitor;
    const auto base = ConnectionMonitor::Time{} + 20s;
    monitor.connected(base);
    CHECK(monitor.pingDue(base));
    CHECK(!monitor.handshakeExpired(base + 5s));
    CHECK(monitor.handshakeExpired(base + 5001ms));
    CHECK(!monitor.peerExpired(base + 8s));
    CHECK(monitor.peerExpired(base + 8001ms));
    const auto nonce = monitor.markPing(base);
    CHECK(!monitor.pingDue(base + 2s)); // une seule sonde en vol
    CHECK(!monitor.probeExpired(base + 5s));
    CHECK(monitor.probeExpired(base + 5001ms));
    monitor.onPong(nonce + 1, base + 10ms);
    CHECK(!monitor.rttMilliseconds());
    monitor.onPong(nonce, base + 24ms);
    CHECK(monitor.rttMilliseconds() == 24);
    CHECK(!monitor.probeExpired(base + 6001ms));
    CHECK(!monitor.pingDue(base + 500ms));
    CHECK(monitor.pingDue(base + 1000ms));
    monitor.received(base + 7s);
    CHECK(!monitor.peerExpired(base + 14s));
    CHECK(monitor.peerExpired(base + 16s));
}

void pollPeers(LanPeer& host, LanPeer& client, int rounds = 500) {
    for (int i = 0; i < rounds; ++i) {
        host.poll(); client.poll();
        if (host.phase() == LanPhase::Connected && client.phase() == LanPhase::Connected) return;
        std::this_thread::sleep_for(1ms);
    }
    CHECK(false && "Connexion non etablie");
}

void testTransport() {
    LanPeer host, client;
    CHECK(host.host(0) && host.boundPort());
    CHECK(!client.join("localhost", host.boundPort())); // pas de DNS synchrone
    CHECK(client.phase() == LanPhase::Failed);
    CHECK(client.join("127.0.0.1", host.boundPort()));
    pollPeers(host, client);
    CHECK(client.send(protocol::hello()));
    CHECK(client.send(protocol::ping(123)));
    CHECK(!client.send("A\nB"));
    bool gotHello = false, gotPing = false;
    for (int i = 0; i < 300 && !gotPing; ++i) {
        host.poll(); client.poll();
        for (const auto& frame : host.takeMessages()) {
            gotHello |= frame == protocol::hello();
            gotPing |= frame == protocol::ping(123);
        }
        std::this_thread::sleep_for(1ms);
    }
    CHECK(gotHello && gotPing);
    CHECK(client.bytesSent() > 0);
    CHECK(host.bytesReceived() > 0);
    CHECK(host.send(protocol::pong(123)));
    bool received = false;
    for (int i = 0; i < 300 && !received; ++i) {
        host.poll(); client.poll();
        for (const auto& frame : client.takeMessages())
            received |= frame == protocol::pong(123);
        std::this_thread::sleep_for(1ms);
    }
    CHECK(received);
    client.close();
    for (int i = 0; i < 300 && host.phase() != LanPhase::Disconnected; ++i) {
        host.poll(); std::this_thread::sleep_for(1ms);
    }
    CHECK(host.phase() == LanPhase::Disconnected);
    CHECK(host.error().find("deconnecte") != std::string::npos);
}

void testRawFragmentedSocket() {
    LanPeer server;
    CHECK(server.host(0));
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    sockaddr_in addr{}; addr.sin_family = AF_INET;
    addr.sin_port = htons(server.boundPort());
    CHECK(::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    for (int i = 0; i < 50 && server.phase() != LanPhase::Connected; ++i)
        { server.poll(); std::this_thread::sleep_for(1ms); }
    CHECK(server.phase() == LanPhase::Connected);
    CHECK(::send(fd, "N|1|", 4, 0) == 4);
    server.poll(); CHECK(server.takeMessages().empty());
    CHECK(::send(fd, "42\nG|1|4\n", 9, 0) == 9);
    std::vector<std::string> got;
    for (int i = 0; i < 100 && got.size() < 2; ++i) {
        server.poll();
        auto chunk = server.takeMessages();
        got.insert(got.end(), chunk.begin(), chunk.end());
        std::this_thread::sleep_for(1ms);
    }
    CHECK(got == (std::vector<std::string>{"N|1|42", "G|1|4"}));
    CHECK(::send(fd, "BAD\r\n", 5, 0) == 5);
    for (int i = 0; i < 100 && server.phase() == LanPhase::Connected; ++i)
        { server.poll(); std::this_thread::sleep_for(1ms); }
    CHECK(server.phase() == LanPhase::Failed);
    CHECK(server.error().find("caractere") != std::string::npos);
    ::close(fd);
}

int main() {
    testCodec(); testMalformedFrames(); testLineFraming(); testMonitor();
    testTransport(); testRawFragmentedSocket();
    std::cout << "[OK] Protocole v2, fragmentation/reassemblage, validation, RTT, TCP loopback.\n";
}
