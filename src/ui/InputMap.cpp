/** @file InputMap.cpp @brief Mapping centralise : clavier SDL2 -> enum Action. */
#include "ui/InputMap.hpp"
#include <SDL.h>

namespace tetris {
std::optional<Action> gameActionForKey(int key) {
    switch (key) {
        case SDLK_LEFT:  return Action::Left;
        case SDLK_RIGHT: return Action::Right;
        case SDLK_DOWN:  return Action::SoftDrop;
        case SDLK_UP:
        case SDLK_x:     return Action::RotateCW;
        case SDLK_z:     return Action::RotateCCW;
        case SDLK_SPACE: return Action::HardDrop;
        case SDLK_c:     return Action::Hold;
        default: return std::nullopt;
    }
}

bool repeatableGameKey(int key) {
    switch (key) {
        case SDLK_LEFT: case SDLK_RIGHT: case SDLK_DOWN:
        case SDLK_UP: case SDLK_x: case SDLK_z: return true;
        default: return false;
    }
}
} // namespace tetris
