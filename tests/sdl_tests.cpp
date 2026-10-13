/**
 * @file sdl_tests.cpp
 * @brief Test de fumee de l'interface SDL2 sans ecran (SDL_VIDEODRIVER=dummy).
 *
 * Le renderer logiciel SDL2 permet de tester le dessin natif sans X11, et
 * les hitboxes restent verifiables sans simulation de clics systeme.
 */
#include "ui/Canvas.hpp"
#include <iostream>

int main() {
    tetris::Canvas canvas;
    if (!canvas.isOpen()) {
        std::cerr << "SDL2 dummy : fenetre ou renderer indisponible\n";
        return 1;
    }
    canvas.begin();
    canvas.rect(120, 200, 200, 100, tetris::palette::cyan);
    canvas.stroke(120, 200, 200, 100, tetris::palette::yellow, 3);
    canvas.text(145, 229, "SDL2 TEST", 3, tetris::palette::text);
    canvas.cell(420, 220, 30, 'I');
    canvas.cell(460, 220, 30, 'T', true);
    canvas.finish();
    if (!canvas.hit(130, 210, 120, 200, 200, 100)) return 2;
    if (canvas.hit(50, 50, 120, 200, 200, 100)) return 3;
    if (canvas.textWidth("SDL2", 3) != 72) return 4;
    std::cout << "SDL2 dummy : creation, rendu et hitboxes OK\n";
    return 0;
}
