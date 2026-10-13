/**
 * @file Protocol.hpp
 * @brief Protocole applicatif versionne, independant des sockets et de SDL2.
 *
 * Les fonctions decode() et encode*() sont testables sans ouvrir de connexion.
 * Toute entree reseau doit etre validee AVANT de modifier un Game.
 */
#pragma once
#include "core/Game.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tetris::protocol {
constexpr int kVersion = 2;
constexpr size_t kMaxFrame = 2048;

enum class Type { Hello, NewRound, Snapshot, Garbage, Death, Winner, Rematch, Ping, Pong };

/** Message decode et valide : aucun champ utilisateur non controle. */
struct Message {
    Type type = Type::Hello;
    int round = 0;
    uint32_t seed = 0;
    Statistics stats{};
    Board board = emptyBoard();
    int lines = 0;
    char winner = '\0';
    uint32_t nonce = 0;
};

struct DecodeResult {
    std::optional<Message> message;
    std::string error;
    explicit operator bool() const { return message.has_value(); }
};

/** Parse un message ASCII sans separateur de trame (\n retire par LineFramer). */
DecodeResult decode(std::string_view frame);
std::string hello();
std::string newRound(int round, uint32_t seed);
std::string snapshot(int round, const Statistics& stats, const Board& board);
std::string garbage(int round, int lines);
std::string death(int round);
std::string winner(int round, char side);
std::string rematch(int round);
std::string ping(uint32_t nonce);
std::string pong(uint32_t nonce);
} // namespace tetris::protocol
