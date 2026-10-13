/**
 * @file main.cpp
 * @brief Arguments CLI et instanciation du contrôleur App.
 */
#include "ui/App.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void usage(const char* app) {
    std::cout << "Tetris Auto LAN - C++17 / SDL2 (Linux)\n\n"
              << "Usage : " << app << " [--solo | --host | --join IP[:PORT]] [--port PORT]\n"
              << "  --solo       : commencer une partie locale\n"
              << "  --host       : creer une partie LAN\n"
              << "  --join IP    : rejoindre une IPv4 numerique [:PORT]\n"
              << "  --port N     : port TCP (defaut 7777)\n"
              << "  --help       : afficher cette aide\n";
}
} // namespace

// Point d'entrée minimal : traduire les options CLI vers les modes d'App.
// Une erreur d'argument empêche la création d'une fenêtre graphique.
int main(int argc, char* argv[]) {
    uint16_t port = 7777;
    std::string mode;
    std::string address;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--help" || option == "-h") { usage(argv[0]); return 0; }
        if (option == "--solo" || option == "--host") {
            mode = option.substr(2);
        } else if (option == "--join" && i + 1 < argc) {
            mode = "join";
            address = argv[++i];
        } else if (option == "--port" && i + 1 < argc) {
            try {
                size_t consumed = 0;
                const int parsed = std::stoi(argv[++i], &consumed);
                if (consumed != std::string(argv[i]).size() || parsed < 1 || parsed > 65535)
                    throw std::out_of_range("port");
                port = static_cast<uint16_t>(parsed);
            } catch (const std::exception&) {
                std::cerr << "Port invalide, valeur attendue entre 1 et 65535.\n";
                return 2;
            }
        } else {
            std::cerr << "Argument inconnu : " << option << "\n";
            usage(argv[0]);
            return 2;
        }
    }
    tetris::App app(port, mode, address);
    return app.run();
}
