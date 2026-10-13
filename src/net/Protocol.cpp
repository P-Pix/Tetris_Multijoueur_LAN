/** @file Protocol.cpp @brief Serialisation ASCII et verification stricte des trames LAN. */
#include "net/Protocol.hpp"
#include <array>
#include <charconv>
#include <vector>

namespace tetris::protocol {
namespace {
std::vector<std::string_view> fields(std::string_view frame) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start <= frame.size()) {
        const auto end = frame.find('|', start);
        parts.push_back(frame.substr(start, end == frame.npos ? end : end - start));
        if (end == frame.npos) break;
        start = end + 1;
    }
    return parts;
}

bool number(std::string_view source, int& out, int min, int max) {
    if (source.empty() || source.size() > 10) return false;
    const auto [end, err] = std::from_chars(source.data(), source.data() + source.size(), out);
    return err == std::errc{} && end == source.data() + source.size() && out >= min && out <= max;
}

bool unsignedNumber(std::string_view source, uint32_t& out) {
    if (source.empty() || source.size() > 10) return false;
    const auto [end, err] = std::from_chars(source.data(), source.data() + source.size(), out);
    return err == std::errc{} && end == source.data() + source.size();
}

bool parseBoard(std::string_view source, Board& board) {
    if (source.size() != kRows * kCols) return false;
    constexpr std::string_view allowed = ".IJLOSTZ#";
    for (int y = 0; y < kRows; ++y)
        for (int x = 0; x < kCols; ++x) {
            const char ch = source[y * kCols + x];
            if (allowed.find(ch) == allowed.npos) return false;
            board[y][x] = ch;
        }
    return true;
}

bool parseStats(const std::vector<std::string_view>& p, Statistics& s) {
    if (!number(p[2], s.score, 0, 1000000000) || !number(p[3], s.lines, 0, 10000000) ||
        !number(p[4], s.level, 1, 1000001) || !number(p[5], s.pieces, 1, 10000000) ||
        !number(p[6], s.tetrises, 0, 10000000)) return false;
    return s.level == 1 + s.lines / 10 && s.tetrises <= s.lines / 4;
}

std::string boardText(const Board& board) {
    std::string text;
    text.reserve(kRows * kCols);
    for (const auto& row : board) for (char cell : row) text.push_back(cell);
    return text;
}

std::string roundPrefix(char type, int round) {
    return std::string(1, type) + "|" + std::to_string(round);
}

DecodeResult failure(std::string error) { return {std::nullopt, std::move(error)}; }

DecodeResult parseHello(const std::vector<std::string_view>& p) {
    int version = 0;
    if (p.size() != 2 || !number(p[1], version, 1, 1000))
        return failure("Trame HELLO invalide");
    if (version != kVersion) return failure("Version du protocole incompatible (v2 requise)");
    Message m; m.type = Type::Hello;
    return {m, {}};
}

DecodeResult parseSnapshot(const std::vector<std::string_view>& p, Message m) {
    if (p.size() != 8 || !parseStats(p, m.stats) || !parseBoard(p[7], m.board))
        return failure("Snapshot invalide ou incoherent");
    m.type = Type::Snapshot;
    return {m, {}};
}

DecodeResult parseNewRound(const std::vector<std::string_view>& p, Message m) {
    if (p.size() != 3 || !unsignedNumber(p[2], m.seed))
        return failure("Nouvelle manche invalide");
    m.type = Type::NewRound;
    return {m, {}};
}

DecodeResult parseGarbage(const std::vector<std::string_view>& p, Message m) {
    if (p.size() != 3 || !number(p[2], m.lines, 1, 4))
        return failure("Attaque invalide (1..4 attendus)");
    m.type = Type::Garbage;
    return {m, {}};
}

DecodeResult parseWinner(const std::vector<std::string_view>& p, Message m) {
    if (p.size() != 3 || (p[2] != "H" && p[2] != "C"))
        return failure("Resultat de match invalide");
    m.type = Type::Winner; m.winner = p[2][0];
    return {m, {}};
}

DecodeResult parseNonce(const std::vector<std::string_view>& p, Type type) {
    Message m; m.type = type;
    if (p.size() != 2 || !unsignedNumber(p[1], m.nonce))
        return failure("Numero de sonde invalide");
    return {m, {}};
}
} // namespace

DecodeResult decode(std::string_view frame) {
    if (frame.empty() || frame.size() > kMaxFrame) return failure("Taille de trame invalide");
    for (unsigned char ch : frame)
        if (ch < 32 || ch > 126) return failure("Caractere de protocole interdit");
    const auto p = fields(frame);
    if (p[0] == "H") return parseHello(p);
    if (p[0] == "P") return parseNonce(p, Type::Ping);
    if (p[0] == "Q") return parseNonce(p, Type::Pong);
    Message m;
    if (p.size() < 2 || !number(p[1], m.round, 1, 1000000))
        return failure("Numero de manche invalide");
    if (p[0] == "N") return parseNewRound(p, m);
    if (p[0] == "S") return parseSnapshot(p, m);
    if (p[0] == "G") return parseGarbage(p, m);
    if (p[0] == "W") return parseWinner(p, m);
    if (p[0] == "D" && p.size() == 2) { m.type = Type::Death; return {m, {}}; }
    if (p[0] == "R" && p.size() == 2) { m.type = Type::Rematch; return {m, {}}; }
    return failure("Type ou nombre de champs non reconnu");
}

std::string hello() { return "H|" + std::to_string(kVersion); }
std::string newRound(int round, uint32_t seed) {
    return roundPrefix('N', round) + "|" + std::to_string(seed);
}
std::string snapshot(int round, const Statistics& s, const Board& b) {
    return roundPrefix('S', round) + "|" + std::to_string(s.score) + "|" +
           std::to_string(s.lines) + "|" + std::to_string(s.level) + "|" +
           std::to_string(s.pieces) + "|" + std::to_string(s.tetrises) + "|" + boardText(b);
}
std::string garbage(int round, int lines) { return roundPrefix('G', round) + "|" + std::to_string(lines); }
std::string death(int round) { return roundPrefix('D', round); }
std::string winner(int round, char side) { return roundPrefix('W', round) + "|" + side; }
std::string rematch(int round) { return roundPrefix('R', round); }
std::string ping(uint32_t nonce) { return "P|" + std::to_string(nonce); }
std::string pong(uint32_t nonce) { return "Q|" + std::to_string(nonce); }
} // namespace tetris::protocol
