/**
 * @file tests.cpp
 * @brief Tests CTest indépendants de l’interface, dont TCP en loopback.
 */
#include "ai/AutoPlayer.hpp"
#include "core/Game.hpp"
#include "net/LanPeer.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <chrono>
#include <iostream>
#include <set>
#include <string>
#include <thread>

using namespace tetris;

// Assertions sans framework externe : un échec arrête CTest avec code 1.
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "TEST KO " << __FILE__ << ":" << __LINE__ << ": " #condition "\n"; \
    std::exit(1); \
} } while (false)

// Contrats des sept types de pièces et de leurs rotations uniques.
void testShapes() {
    const std::string types = "IJLOSTZ";
    for (char type : types) {
        const auto& rs = rotations(type);
        CHECK(rs.size() >= 1 && rs.size() <= 4);
        for (const auto& shape : rs) {
            std::set<std::pair<int, int>> unique;
            for (auto cell : shape) unique.emplace(cell.x, cell.y);
            CHECK(unique.size() == 4);
            CHECK(shapeWidth(shape) <= 4 && shapeHeight(shape) <= 4);
        }
    }
    CHECK(rotations('I').size() == 2);
    CHECK(rotations('O').size() == 1);
    CHECK(rotations('T').size() == 4);
}

void testBoard() {
    Board b = emptyBoard();
    b[19].fill('J');
    b[17].fill('Z');
    b[18][3] = 'I';
    auto result = clearLines(b);
    CHECK(result.lines == 2);
    CHECK(result.clearedRows[17] && result.clearedRows[19]);
    CHECK(result.board[19][3] == 'I');
    CHECK(result.board[0][0] == '.');
}

// Même graine => même partie : prérequis de l’équité en LAN.
void testDeterministicBags() {
    Game a(123456), b(123456);
    for (int i = 0; i < 100; ++i) {
        CHECK(a.current().type == b.current().type);
        CHECK(a.next() == b.next());
        a.act(Action::HardDrop);
        b.act(Action::HardDrop);
        if (a.gameOver()) break;
    }
    Game game(3);
    std::set<char> seven;
    seven.insert(game.current().type);
    for (int i = 0; i < 6; ++i) {
        game.act(Action::HardDrop);
        seven.insert(game.current().type);
    }
    // Les 7 premières pièces du sac sont différentes, sauf si le plateau est bloqué.
    if (!game.gameOver()) CHECK(seven.size() == 7);
}

void testControls() {
    Game game(9);
    const auto first = game.current();
    CHECK(game.act(Action::Left));
    CHECK(game.current().x == first.x - 1);
    CHECK(game.act(Action::Right));
    CHECK(game.current().x == first.x);
    CHECK(game.act(Action::Hold));
    CHECK(game.holdType() == first.type);
    CHECK(!game.canHold());
    CHECK(!game.act(Action::Hold));
    const int score = game.stats().score;
    CHECK(game.act(Action::HardDrop));
    CHECK(game.stats().score > score);
    CHECK(game.canHold());
    CHECK(game.stats().pieces >= 3);
}

// Les malus appliqués sont reproductibles et peuvent provoquer le top-out.
void testGarbage() {
    Game a(7), b(7);
    a.receiveGarbage(2);
    b.receiveGarbage(2);
    a.act(Action::HardDrop);
    b.act(Action::HardDrop);
    CHECK(a.board() == b.board());
    int blocks = 0;
    for (int y : {18, 19})
        for (char cell : a.board()[y]) if (cell == '#') ++blocks;
    CHECK(blocks == 18);
    CHECK(a.pendingGarbage() == 0);
    Game c(8);
    c.receiveGarbage(20);
    c.act(Action::HardDrop);
    CHECK(c.gameOver());
}

void testAuto() {
    Game game(42);
    AutoPlayer ai;
    auto move = ai.bestMove(game);
    CHECK(move.valid);
    CHECK(move.y >= -4 && move.y < kRows);
    CHECK(game.forcePlacement(move.rotation, move.x, move.y));
    CHECK(game.stats().pieces == 2);
    CHECK(game.stats().score >= 0);
}

// Échange TCP réel sur 127.0.0.1 avec port éphémère, puis déconnexion.
void testLan() {
    LanPeer server, client;
    CHECK(server.host(0));
    CHECK(server.boundPort() > 0);
    CHECK(client.join("127.0.0.1", server.boundPort()));
    for (int i = 0; i < 500; ++i) {
        server.poll(); client.poll();
        if (server.phase() == LanPhase::Connected && client.phase() == LanPhase::Connected) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(server.phase() == LanPhase::Connected);
    CHECK(client.phase() == LanPhase::Connected);
    CHECK(server.send("N|1|12345"));
    CHECK(server.send("S|1|0|0|1|1|" + std::string(200, '.')));
    std::vector<std::string> frames;
    for (int i = 0; i < 500 && frames.size() < 2; ++i) {
        server.poll(); client.poll();
        auto incoming = client.takeMessages();
        frames.insert(frames.end(), incoming.begin(), incoming.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(frames.size() == 2);
    CHECK(frames[0] == "N|1|12345");
    CHECK(frames[1].substr(0, 8) == "S|1|0|0|");
    CHECK(client.send("G|1|4"));
    bool received = false;
    for (int i = 0; i < 500 && !received; ++i) {
        server.poll(); client.poll();
        for (const auto& line : server.takeMessages()) received |= line == "G|1|4";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(received);
    client.close();
    for (int i = 0; i < 500 && server.phase() != LanPhase::Disconnected; ++i) {
        server.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(server.phase() == LanPhase::Disconnected);
}

int main() {
    testShapes(); testBoard(); testDeterministicBags();
    testControls(); testGarbage(); testAuto(); testLan();
    std::cout << "[OK] Pieces, 7-bag, plateau, controle, hold, garbage, IA 3-ply, TCP LAN.\n";
}
