/** @file InputMap.hpp @brief Adaptation des touches SDL2 vers les actions metier. */
#pragma once
#include "core/Game.hpp"
#include <optional>

namespace tetris {
/** Une action ne depend pas du clavier qui l'a generee. */
std::optional<Action> gameActionForKey(int sdlKey);
/** Ignore l'autorepetition pour pause, hard drop, hold et changement d'ecran. */
bool repeatableGameKey(int sdlKey);
} // namespace tetris
