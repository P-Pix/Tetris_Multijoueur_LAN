/**
 * @file Canvas.cpp
 * @brief Fenetre SDL2 et rendu de formes et de textes en pixels adaptatifs.
 */
#include "ui/Canvas.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <iostream>
#include <unordered_map>

namespace tetris {
namespace {
using Bitmap = std::array<uint8_t, 7>;

// Police bitmap 5x7 embarquée : pas de dépendance à une police système.
const Bitmap& bitmap(char ch) {
    static const Bitmap empty{};
    static const std::unordered_map<char, Bitmap> chars{
        {'A', {14,17,17,31,17,17,17}}, {'B', {30,17,17,30,17,17,30}},
        {'C', {14,17,16,16,16,17,14}}, {'D', {30,17,17,17,17,17,30}},
        {'E', {31,16,16,30,16,16,31}}, {'F', {31,16,16,30,16,16,16}},
        {'G', {14,17,16,23,17,17,15}}, {'H', {17,17,17,31,17,17,17}},
        {'I', {31,4,4,4,4,4,31}},    {'J', {7,2,2,2,18,18,12}},
        {'K', {17,18,20,24,20,18,17}}, {'L', {16,16,16,16,16,16,31}},
        {'M', {17,27,21,21,17,17,17}}, {'N', {17,25,21,19,17,17,17}},
        {'O', {14,17,17,17,17,17,14}}, {'P', {30,17,17,30,16,16,16}},
        {'Q', {14,17,17,17,21,18,13}}, {'R', {30,17,17,30,20,18,17}},
        {'S', {15,16,16,14,1,1,30}}, {'T', {31,4,4,4,4,4,4}},
        {'U', {17,17,17,17,17,17,14}}, {'V', {17,17,17,17,17,10,4}},
        {'W', {17,17,17,21,21,21,10}}, {'X', {17,17,10,4,10,17,17}},
        {'Y', {17,17,10,4,4,4,4}}, {'Z', {31,1,2,4,8,16,31}},
        {'0', {14,17,19,21,25,17,14}}, {'1', {4,12,4,4,4,4,14}},
        {'2', {14,17,1,2,4,8,31}}, {'3', {30,1,1,14,1,1,30}},
        {'4', {2,6,10,18,31,2,2}}, {'5', {31,16,16,30,1,1,30}},
        {'6', {14,16,16,30,17,17,14}}, {'7', {31,1,2,4,8,8,8}},
        {'8', {14,17,17,14,17,17,14}}, {'9', {14,17,17,15,1,1,14}},
        {'.', {0,0,0,0,0,12,12}}, {'-', {0,0,0,31,0,0,0}},
        {':', {0,12,12,0,12,12,0}}, {'/', {1,2,2,4,8,8,16}},
        {'+', {0,4,4,31,4,4,0}}, {'_', {0,0,0,0,0,0,31}},
        {'=', {0,0,31,0,31,0,0}}, {'!', {4,4,4,4,4,0,4}},
        {'?', {14,17,1,2,4,0,4}}, {'<', {2,4,8,16,8,4,2}},
        {'>', {8,4,2,1,2,4,8}}, {'(', {2,4,8,8,8,4,2}},
        {')', {8,4,2,2,2,4,8}}, {'[', {14,8,8,8,8,8,14}},
        {']', {14,2,2,2,2,2,14}}, {'#', {10,10,31,10,31,10,10}},
        {'%', {17,18,2,4,8,9,17}}, {'*', {0,21,14,31,14,21,0}},
        {'|', {4,4,4,4,4,4,4}}, {' ', {0,0,0,0,0,0,0}},
        {',', {0,0,0,0,12,12,8}}, {'\'', {4,4,8,0,0,0,0}}
    };
    const auto it = chars.find(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    return it == chars.end() ? empty : it->second;
}

Color brighter(Color c, int amount) {
    auto adjust = [amount](uint8_t channel) {
        return static_cast<uint8_t>(std::clamp(int(channel) + amount, 0, 255));
    };
    return {adjust(c.r), adjust(c.g), adjust(c.b)};
}
} // namespace

// SDL_Init / SDL_Quit forment le cycle de vie du sous-systeme graphique.
// SDL2 choisit lui-meme son backend disponible (Wayland, X11, etc.).
Canvas::Canvas() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "Initialisation SDL2 impossible : " << SDL_GetError() << "\n";
        return;
    }
    ownsSdl_ = true;
    window_ = SDL_CreateWindow("Tetris Auto LAN - SDL2 / C++17",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               1220, 820, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        std::cerr << "Creation de la fenetre SDL2 impossible : " << SDL_GetError() << "\n";
        return;
    }
    SDL_SetWindowMinimumSize(window_, 800, 540);
    // Preferer le GPU, mais rester utilisable avec un pilote logiciel / dummy.
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
        std::cerr << "Creation du renderer SDL2 impossible : " << SDL_GetError() << "\n";
        return;
    }
    int width = 0, height = 0;
    SDL_GetWindowSize(window_, &width, &height);
    resize(width, height);
}

// Liberation en ordre inverse : renderer -> fenetre -> sous-systeme SDL2.
Canvas::~Canvas() {
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    if (ownsSdl_) SDL_Quit();
}

// Le rendu et les clics utilisent des dimensions differentes si SDL2 est
// configure en HiDPI. On calcule donc deux transformations independantes.
void Canvas::resize(int width, int height) {
    if (!renderer_ || width <= 0 || height <= 0) return;
    int outputWidth = width, outputHeight = height;
    if (SDL_GetRendererOutputSize(renderer_, &outputWidth, &outputHeight) != 0) {
        outputWidth = width;
        outputHeight = height;
    }
    scale_ = std::min(outputWidth / 1220.0, outputHeight / 820.0);
    ox_ = (outputWidth - 1220 * scale_) / 2.0;
    oy_ = (outputHeight - 820 * scale_) / 2.0;
    mouseScale_ = std::min(width / 1220.0, height / 820.0);
    mouseOx_ = (width - 1220 * mouseScale_) / 2.0;
    mouseOy_ = (height - 820 * mouseScale_) / 2.0;
}

int Canvas::tx(int x) const { return static_cast<int>(std::lround(ox_ + x * scale_)); }
int Canvas::ty(int y) const { return static_cast<int>(std::lround(oy_ + y * scale_)); }

// Eviter des changements d'etat SDL repetes pour une meme couleur.
void Canvas::setColor(Color color) {
    const uint32_t id = (uint32_t(color.r) << 16) | (uint32_t(color.g) << 8) | color.b;
    if (activeColor_ == id) return;
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);
    activeColor_ = id;
}

// SDL_Renderer maintient deja sa propre frame ; aucun Pixmap X11 requis.
void Canvas::begin() {
    activeColor_ = 0xffffffffU;
    setColor(palette::background);
    SDL_RenderClear(renderer_);
    for (int i = 0; i < 16; ++i) {
        const Color tint{static_cast<uint8_t>(8 + i / 3),
                         static_cast<uint8_t>(12 + i / 2),
                         static_cast<uint8_t>(22 + i)};
        rect(0, i * 26, 1220, 26, tint);
    }
    line(32, 105, 1188, 105, palette::border);
}

void Canvas::finish() { SDL_RenderPresent(renderer_); }

void Canvas::rect(int x, int y, int w, int h, Color color) {
    if (w <= 0 || h <= 0) return;
    setColor(color);
    const SDL_Rect bounds{tx(x), ty(y),
                          std::max(1, tx(x + w) - tx(x)),
                          std::max(1, ty(y + h) - ty(y))};
    SDL_RenderFillRect(renderer_, &bounds);
}

void Canvas::stroke(int x, int y, int w, int h, Color color, int thick) {
    rect(x, y, w, thick, color);
    rect(x, y + h - thick, w, thick, color);
    rect(x, y, thick, h, color);
    rect(x + w - thick, y, thick, h, color);
}

void Canvas::line(int x1, int y1, int x2, int y2, Color color) {
    setColor(color);
    SDL_RenderDrawLine(renderer_, tx(x1), ty(y1), tx(x2), ty(y2));
}

// Chaque bit de la police 5x7 est dessine avec SDL_RenderFillRect.
void Canvas::glyph(int x, int y, char ch, int pixelSize) {
    const Bitmap& bits = bitmap(ch);
    for (int row = 0; row < 7; ++row)
        for (int col = 0; col < 5; ++col)
            if (bits[row] & (1 << (4 - col))) {
                const int left = tx(x + col * pixelSize);
                const int top = ty(y + row * pixelSize);
                const SDL_Rect pixel{left, top,
                                     std::max(1, tx(x + (col + 1) * pixelSize) - left),
                                     std::max(1, ty(y + (row + 1) * pixelSize) - top)};
                SDL_RenderFillRect(renderer_, &pixel);
            }
}

void Canvas::text(int x, int y, const std::string& value, int scale, Color color) {
    setColor(color);
    for (char ch : value) {
        glyph(x, y, ch, scale);
        x += 6 * scale;
    }
}

int Canvas::textWidth(const std::string& value, int scale) const {
    return static_cast<int>(value.size()) * 6 * scale;
}

void Canvas::textCentered(int cx, int y, const std::string& value, int scale, Color color) {
    text(cx - textWidth(value, scale) / 2, y, value, scale, color);
}

void Canvas::borderCard(int x, int y, int w, int h, Color bg) {
    rect(x + 5, y + 7, w, h, {5, 8, 15});
    rect(x, y, w, h, bg);
    stroke(x, y, w, h, palette::border);
    rect(x + 1, y + 1, w - 2, 2, {32, 45, 64});
}

Color Canvas::pieceColor(char type) const {
    switch (type) {
        case 'I': return {86, 217, 255};
        case 'J': return {108, 132, 255};
        case 'L': return {255, 171, 92};
        case 'O': return {255, 225, 93};
        case 'S': return {101, 239, 143};
        case 'T': return {183, 124, 255};
        case 'Z': return {255, 109, 130};
        case '#': return {117, 132, 150};
        default: return {33, 43, 61};
    }
}

void Canvas::cell(int x, int y, int size, char type, bool ghost) {
    const Color color = pieceColor(type);
    if (ghost) { stroke(x + 3, y + 3, size - 6, size - 6, brighter(color, -55), 2); return; }
    rect(x + 1, y + 2, size - 2, size - 2, brighter(color, -55));
    rect(x + 2, y + 2, size - 4, size - 6, color);
    rect(x + 4, y + 4, size - 8, std::max(2, size / 7), brighter(color, 25));
    line(x + size - 3, y + 3, x + size - 3, y + size - 3, brighter(color, -85));
}

// Défaire la transformation visuelle garantit des clics alignés avec
// les boutons, même quand la fenêtre est redimensionnée.
bool Canvas::hit(int sx, int sy, int x, int y, int w, int h) const {
    const double lx = (sx - mouseOx_) / mouseScale_;
    const double ly = (sy - mouseOy_) / mouseScale_;
    return lx >= x && ly >= y && lx < x + w && ly < y + h;
}
} // namespace tetris
