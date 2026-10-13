/**
 * @file Canvas.hpp
 * @brief Interface de dessin SDL2 et gestion RAII des ressources graphiques.
 *
 * Les appels du moteur Tetris ne dependent pas de SDL2 : seule la couche UI
 * utilise SDL2, ce qui permet de remplacer le moteur de rendu sans modifier
 * les regles, l'IA ou le protocole LAN.
 */
#pragma once
#include <cstdint>
#include <string>

// Evite de propager les en-tetes SDL2 a tous les modules utilisant Canvas.
struct SDL_Window;
struct SDL_Renderer;

namespace tetris {
/** Couleur RGB independante du format de pixels de SDL2. */
struct Color {
    uint8_t r = 0, g = 0, b = 0;
};

namespace palette {
constexpr Color background{7, 11, 20};
constexpr Color panel{20, 28, 45};
constexpr Color panelAlt{13, 20, 34};
constexpr Color border{49, 65, 89};
constexpr Color text{242, 247, 255};
constexpr Color dim{138, 158, 184};
constexpr Color cyan{100, 225, 255};
constexpr Color purple{165, 115, 253};
constexpr Color green{100, 242, 173};
constexpr Color red{255, 106, 128};
constexpr Color yellow{255, 225, 94};
constexpr Color mutedLine{27, 38, 58};
} // namespace palette

/**
 * @brief Fenetre SDL2, dessin 2D et police bitmap 5x7 sans SDL_ttf.
 *
 * L'interface est dessinee dans un repere logique de 1220x820 points.
 * SDL_Renderer recoit des coordonnees de pixels reellement affichees ;
 * les facteurs d'echelle preservent le ratio (letterboxing). Les clics
 * utilisent les dimensions de fenetre SDL, pouvant differer des pixels
 * du renderer sur un ecran haute densite (HiDPI).
 */
class Canvas {
public:
    Canvas();
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    bool isOpen() const { return window_ != nullptr && renderer_ != nullptr; }
    /** Recalcule les transformations apres un redimensionnement de la fenetre. */
    void resize(int width, int height);
    /** Efface la frame et dessine son decor de fond. */
    void begin();
    /** Affiche la frame SDL2 complete a l'ecran. */
    void finish();
    void rect(int x, int y, int w, int h, Color color);
    void stroke(int x, int y, int w, int h, Color color, int thick = 1);
    void line(int x1, int y1, int x2, int y2, Color color);
    void text(int x, int y, const std::string& text, int scale, Color color);
    void textCentered(int cx, int y, const std::string& text, int scale, Color color);
    int textWidth(const std::string& text, int scale) const;
    void borderCard(int x, int y, int w, int h, Color bg = palette::panel);
    void cell(int x, int y, int size, char type, bool ghost = false);
    /** Teste une hitbox logique a partir d'un clic SDL en pixels fenetre. */
    bool hit(int screenX, int screenY, int x, int y, int w, int h) const;
    Color pieceColor(char type) const;

private:
    void setColor(Color color);
    int tx(int x) const;
    int ty(int y) const;
    void glyph(int x, int y, char ch, int pixelSize);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool ownsSdl_ = false;
    // Espace de sortie (pixels SDL_Renderer).
    double scale_ = 1.0;
    double ox_ = 0.0;
    double oy_ = 0.0;
    // Espace d'entree (coordonnees des SDL_MouseButtonEvent).
    double mouseScale_ = 1.0;
    double mouseOx_ = 0.0;
    double mouseOy_ = 0.0;
    uint32_t activeColor_ = 0xffffffffU;
};
} // namespace tetris
