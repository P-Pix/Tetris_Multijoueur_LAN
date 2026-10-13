/** @file LineFramer.cpp @brief Assemble des lignes et borne les entrees malicieuses. */
#include "net/LineFramer.hpp"
#include <utility>

namespace tetris {
bool LineFramer::appendByte(unsigned char ch) {
    if (ch == '\n') {
        if (partial_.empty() || ready_.size() >= kMaxPendingFrames) {
            error_ = "Trame vide ou file de reception saturee";
            return false;
        }
        ready_.push_back(std::move(partial_));
        partial_.clear();
        return true;
    }
    if (ch < 32 || ch > 126 || partial_.size() >= protocol::kMaxFrame) {
        error_ = "Trame trop longue ou caractere binaire interdit";
        return false;
    }
    partial_.push_back(static_cast<char>(ch));
    return true;
}

bool LineFramer::feed(std::string_view bytes) {
    if (!error_.empty()) return false;
    for (unsigned char ch : bytes) if (!appendByte(ch)) return false;
    return true;
}

std::vector<std::string> LineFramer::take() {
    std::vector<std::string> out;
    out.reserve(ready_.size());
    while (!ready_.empty()) { out.push_back(std::move(ready_.front())); ready_.pop_front(); }
    return out;
}

void LineFramer::clear() { partial_.clear(); ready_.clear(); error_.clear(); }
} // namespace tetris
