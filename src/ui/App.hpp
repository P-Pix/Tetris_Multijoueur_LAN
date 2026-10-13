/**
 * @file App.hpp
 * @brief Controleur SDL2, etat des ecrans et orchestration solo/LAN.
 */
#pragma once
#include "ai/AutoPlayer.hpp"
#include "core/Game.hpp"
#include "net/LanPeer.hpp"
#include "net/Protocol.hpp"
#include "net/ConnectionMonitor.hpp"
#include "ui/Canvas.hpp"
#include <chrono>
#include <optional>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

union SDL_Event;
struct SDL_WindowEvent;
struct SDL_MouseButtonEvent;

namespace tetris {
/** Machines à états des écrans ; un seul écran affiché à un instant donné. */
enum class Screen { Menu, Solo, JoinForm, Waiting, Versus, Finished, Error };
enum class ButtonId { Solo, Host, Join, Connect, Back, Restart, Pause, Auto, Rematch };
enum class Result { Undecided, Win, Lose };

/** Copie *visuelle* de l'état distant, pas un moteur Game en duplication. */
struct RemoteState {
    Board board = emptyBoard();
    Statistics stats{};
    bool received = false;
};

/**
 * @brief Composition des modules : événements, simulation, TCP et rendu.
 *
 * La boucle unique (~16 ms) appelle update puis render ; le mode versus
 * ne transmet pas les touches, uniquement snapshots/attaques/résultats.
 */
class App {
public:
    App(uint16_t port, const std::string& command, const std::string& joinAddress);
    int run();

private:
    struct Button {
        int x, y, w, h;
        ButtonId id;
    };
    /** La lecture SDL ne fait que distribuer : le moteur reste independant. */
    void processEvents();
    void dispatchEvent(const SDL_Event& event);
    void handleWindowEvent(const SDL_WindowEvent& event);
    void handleMouseEvent(const SDL_MouseButtonEvent& event);
    /** Convertit un SDL_KEYDOWN en commande de menu ou de Tetris. */
    void handleKey(int key, bool repeat);
    void handleMenuKey(int key);
    void handleJoinKey(int key);
    bool handleSoloShortcut(int key);
    /** Filtre les caracteres saisis dans le formulaire IP. */
    void handleText(const std::string& utf8);
    void handleMouse(int x, int y);
    void click(ButtonId id);
    void startSolo();
    void startHost();
    void showJoin();
    void connectToHost();
    void backToMenu();
    void startHostRound();
    void startClientRound(int round, uint32_t seed);
    /** Réinitialise le moteur et le suivi réseau pour ce numéro de manche. */
    void newRound(int round, uint32_t seed);
    void requestRematch();
    void maybeRematch();
    void hostFinish(char winner);
    void clientDeath();
    /** Uniquement des trames validees par le parseur Protocol. */
    void receiveMessage(const std::string& frame);
    void dispatchMessage(const protocol::Message& message);
    void processRemoteState(const protocol::Message& message);
    void processRoundMessage(const protocol::Message& message);
    void processControlMessage(const protocol::Message& message);
    void resetLanSession();
    void beginHandshake();
    void checkPeerTimeouts(ConnectionMonitor::Time now);
    void pollHeartbeat(ConnectionMonitor::Time now);
    bool pollNetworkSession();
    bool sendOrFail(const std::string& frame);
    bool networkFailed();
    void update(int elapsedMs);
    void updateSolo(int elapsedMs);
    /** Échanges non bloquants, gravité locale et snapshots (~90 ms). */
    void updateNetwork(int elapsedMs);
    void sendSnapshot();
    /** Envoie les attaques produites par Game après verrouillage. */
    void sendOutgoingGarbage();
    void tryFinishRound();
    uint32_t randomSeed();
    void action(Action action);

    void render();
    void renderHeader(const std::string& status);
    void renderMenu();
    void renderJoin();
    void renderWaiting();
    void renderError();
    void renderSolo();
    void renderVersus();
    void renderFinished();
    void renderBoard(const Board& b, int x, int y, int size, const Game* active);
    void renderMini(char type, int x, int y, int width, int height, int size);
    void renderNext(int x, int y, int width, int size, int gap, int limit);
    void renderStats(int x, int y, int width, const Statistics& stats, bool opponent);
    void renderControls(int x, int y);
    void renderButton(int x, int y, int w, int h, const std::string& label,
                      ButtonId id, bool accent = false);
    void renderCenterPanel();

    Canvas canvas_;
    LanPeer lan_;
    Game game_{1};
    AutoPlayer ai_;
    RemoteState remote_;
    Screen screen_ = Screen::Menu;
    Result result_ = Result::Undecided;
    std::vector<Button> buttons_;
    bool running_ = true;
    bool paused_ = false;
    bool autoMode_ = false;
    bool localReady_ = false;
    bool remoteReady_ = false;
    bool clientReportedDeath_ = false;
    bool helloSent_ = false;
    bool helloReceived_ = false;
    ConnectionMonitor networkMonitor_;
    std::vector<std::string> hostAddresses_;
    int aiSpeedMs_ = 120;
    int autoElapsedMs_ = 0;
    int snapshotElapsedMs_ = 0;
    int activeRound_ = 0;
    uint16_t port_ = 7777;
    uint16_t connectedPort_ = 7777;
    std::string joinAddress_;
    std::string error_;
    // L'hôte choisit une nouvelle seed de manches ; l'autre poste la reçoit.
    std::mt19937 seedRng_;
};
} // namespace tetris
