/** @file LineFramer.hpp @brief Reconstitution des trames ASCII sur flux TCP. */
#pragma once
#include "net/Protocol.hpp"
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace tetris {
/**
 * TCP ne preserve PAS les frontieres entre deux appels a send().
 * Cette classe accepte des morceaux arbitraires et produit les lignes completes.
 * Limites : 2048 octets/trame et 200 trames en attente ; aucune perte silencieuse.
 */
class LineFramer {
public:
    bool feed(std::string_view bytes);
    std::vector<std::string> take();
    void clear();
    const std::string& error() const { return error_; }
private:
    bool appendByte(unsigned char ch);
    static constexpr size_t kMaxPendingFrames = 200;
    std::string partial_;
    std::deque<std::string> ready_;
    std::string error_;
};
} // namespace tetris
