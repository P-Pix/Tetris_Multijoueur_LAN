/** @file AppInput.cpp @brief Distribution des evenements SDL2 et commandes utilisateur. */
#include "ui/App.hpp"
#include "ui/InputMap.hpp"
#include <SDL.h>
#include <algorithm>

namespace tetris {
// PollEvent reste la bonne primitive SDL2 ; le dispatch est decouple du jeu.
void App::processEvents() {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) dispatchEvent(event);
}

void App::dispatchEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_QUIT: running_ = false; break;
        case SDL_WINDOWEVENT: handleWindowEvent(event.window); break;
        case SDL_KEYDOWN: handleKey(event.key.keysym.sym, event.key.repeat != 0); break;
        case SDL_TEXTINPUT: handleText(event.text.text); break;
        case SDL_MOUSEBUTTONDOWN: handleMouseEvent(event.button); break;
        default: break;
    }
}

void App::handleWindowEvent(const SDL_WindowEvent& event) {
    if (event.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        canvas_.resize(event.data1, event.data2);
}

void App::handleMouseEvent(const SDL_MouseButtonEvent& event) {
    if (event.button == SDL_BUTTON_LEFT) handleMouse(event.x, event.y);
}

void App::handleMenuKey(int key) {
    switch (key) {
        case SDLK_F1: case SDLK_1: startSolo(); break;
        case SDLK_F2: case SDLK_2: startHost(); break;
        case SDLK_F3: case SDLK_3: showJoin(); break;
        default: break;
    }
}

void App::handleJoinKey(int key) {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) connectToHost();
    else if (key == SDLK_BACKSPACE && !joinAddress_.empty()) joinAddress_.pop_back();
}

bool App::handleSoloShortcut(int key) {
    switch (key) {
        case SDLK_r: startSolo(); return true;
        case SDLK_p: paused_ = !paused_; return true;
        case SDLK_a: autoMode_ = !autoMode_; autoElapsedMs_ = 0; return true;
        case SDLK_LEFTBRACKET: case SDLK_MINUS:
            aiSpeedMs_ = std::max(35, aiSpeedMs_ - 25); return true;
        case SDLK_RIGHTBRACKET: case SDLK_PLUS: case SDLK_EQUALS:
            aiSpeedMs_ = std::min(500, aiSpeedMs_ + 25); return true;
        default: return false;
    }
}

// Le dispatcher clavier ne connait ni rotations ni collisions : InputMap
// fournit une Action, ensuite seul Game decide si elle peut s'appliquer.
void App::handleKey(int key, bool repeat) {
    if (repeat && !repeatableGameKey(key)) return;
    if (key == SDLK_ESCAPE) { backToMenu(); return; }
    if (screen_ == Screen::Menu) { handleMenuKey(key); return; }
    if (screen_ == Screen::JoinForm) { handleJoinKey(key); return; }
    if (screen_ == Screen::Finished) {
        if (key == SDLK_r || key == SDLK_RETURN) requestRematch();
        return;
    }
    if (screen_ == Screen::Solo && handleSoloShortcut(key)) return;
    if (screen_ != Screen::Solo && screen_ != Screen::Versus) return;
    if (const auto mapped = gameActionForKey(key)) action(*mapped);
}

// SDL_TEXTINPUT est adapte a la saisie de caracteres : contrairement a
// SDL_KEYDOWN, il tient compte du clavier et du layout du systeme. Le
// champ de connexion est volontairement limite aux caracteres ASCII IPv4.
void App::handleText(const std::string& utf8) {
    if (screen_ != Screen::JoinForm) return;
    for (unsigned char ch : utf8) {
        const bool allowed = (ch >= '0' && ch <= '9') || ch == '.' || ch == ':';
        if (allowed && joinAddress_.size() < 48) joinAddress_ += static_cast<char>(ch);
    }
}

void App::click(ButtonId id) {
    switch (id) {
        case ButtonId::Solo: startSolo(); break;
        case ButtonId::Host: startHost(); break;
        case ButtonId::Join: showJoin(); break;
        case ButtonId::Connect: connectToHost(); break;
        case ButtonId::Back: backToMenu(); break;
        case ButtonId::Restart: startSolo(); break;
        case ButtonId::Pause: if (screen_ == Screen::Solo) paused_ = !paused_; break;
        case ButtonId::Auto: if (screen_ == Screen::Solo) autoMode_ = !autoMode_; break;
        case ButtonId::Rematch: requestRematch(); break;
    }
}

void App::handleMouse(int x, int y) {
    for (auto button : buttons_)
        if (canvas_.hit(x, y, button.x, button.y, button.w, button.h)) {
            click(button.id);
            return;
        }
}

} // namespace tetris
