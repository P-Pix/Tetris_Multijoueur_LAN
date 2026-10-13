/** @file AppNetwork.cpp @brief Etats de session, arbitrage et heartbeat LAN. */
#include "ui/App.hpp"
#include <SDL.h>
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iostream>

namespace tetris {
namespace {
// La syntaxe de la cible est une IPv4 numerique avec port facultatif.
// La resolution DNS est volontairement evitee sur le thread de rendu.
bool parseAddress(const std::string& typed, uint16_t defaultPort,
                  std::string& host, uint16_t& port) {
    if (typed.empty() || typed.size() > 48) return false;
    host = typed;
    port = defaultPort;
    const auto separator = typed.rfind(':');
    if (separator == std::string::npos) return true;
    host = typed.substr(0, separator);
    const auto portText = typed.substr(separator + 1);
    if (host.empty() || portText.empty()) return false;
    int parsed = 0;
    const auto [end, error] = std::from_chars(portText.data(),
                                               portText.data() + portText.size(), parsed);
    if (error != std::errc{} || end != portText.data() + portText.size() ||
        parsed < 1 || parsed > 65535) return false;
    port = static_cast<uint16_t>(parsed);
    return true;
}

std::string str(int value) { return std::to_string(value); }

// Diagnostic des transitions metier, active uniquement si demande.
void traceLan(const char* event, const std::string& details) {
    static const bool enabled = [] {
        const char* env = std::getenv("TETRIS_NET_TRACE");
        return env && std::string(env) == "1";
    }();
    if (enabled) std::clog << "[LAN] " << event << " " << details << '\n';
}
} // namespace

// Une nouvelle session ne doit jamais reutiliser le numero de la precedente.
void App::resetLanSession() {
    helloSent_ = helloReceived_ = false;
    localReady_ = remoteReady_ = clientReportedDeath_ = false;
    activeRound_ = 0;
    remote_ = {};
    result_ = Result::Undecided;
    snapshotElapsedMs_ = 0;
}

void App::startSolo() {
    SDL_StopTextInput();
    lan_.close();
    resetLanSession();
    game_.reset(randomSeed());
    paused_ = autoMode_ = false;
    autoElapsedMs_ = 0;
    screen_ = Screen::Solo;
}

void App::startHost() {
    SDL_StopTextInput();
    lan_.close();
    resetLanSession();
    hostAddresses_ = LanPeer::localAddresses();
    screen_ = Screen::Waiting;
    error_.clear();
    if (!lan_.host(port_)) { error_ = lan_.error(); screen_ = Screen::Error; }
}

void App::showJoin() {
    lan_.close(); resetLanSession();
    screen_ = Screen::JoinForm;
    error_.clear();
    SDL_StartTextInput();
}

void App::connectToHost() {
    std::string host;
    uint16_t port = port_;
    if (!parseAddress(joinAddress_, port_, host, port)) {
        error_ = "ADRESSE OU PORT INVALIDE";
        screen_ = Screen::JoinForm;
        return;
    }
    SDL_StopTextInput();
    lan_.close(); resetLanSession();
    connectedPort_ = port;
    screen_ = Screen::Waiting;
    error_.clear();
    if (!lan_.join(host, port)) { error_ = lan_.error(); screen_ = Screen::Error; }
}

void App::backToMenu() {
    SDL_StopTextInput();
    lan_.close(); resetLanSession();
    screen_ = Screen::Menu;
    paused_ = autoMode_ = false;
}

// La manche identifie chaque message : on ignore ceux reçus trop tard
// et l'on remet les drapeaux de revanche/défaite à zéro à chaque départ.
void App::newRound(int round, uint32_t seed) {
    activeRound_ = round;
    game_.reset(seed);
    remote_ = {};
    localReady_ = remoteReady_ = clientReportedDeath_ = false;
    snapshotElapsedMs_ = 1000;
    result_ = Result::Undecided;
    screen_ = Screen::Versus;
}

// Aucun echec d'enfilement de message n'est ignore silencieusement.
bool App::sendOrFail(const std::string& frame) {
    if (lan_.send(frame)) return true;
    lan_.fail("File d'envoi TCP saturee ou connexion fermee");
    return false;
}

void App::startHostRound() {
    if (activeRound_ >= 1000000) { lan_.fail("Limite de manches atteinte"); return; }
    const int round = activeRound_ + 1;
    const uint32_t seed = randomSeed();
    if (sendOrFail(protocol::newRound(round, seed))) {
        traceLan("ROUND_START_HOST", "round=" + str(round));
        newRound(round, seed);
    }
}

void App::startClientRound(int round, uint32_t seed) {
    if (lan_.role() != LanRole::Client || round <= activeRound_) return;
    if (round != activeRound_ + 1 ||
        (screen_ != Screen::Waiting && screen_ != Screen::Finished)) {
        lan_.fail("Transition de manche inattendue"); return;
    }
    traceLan("ROUND_START_CLIENT", "round=" + str(round));
    newRound(round, seed);
}

void App::requestRematch() {
    if (screen_ != Screen::Finished || localReady_) return;
    localReady_ = true;
    if (lan_.role() == LanRole::Client) sendOrFail(protocol::rematch(activeRound_));
    maybeRematch();
}

void App::maybeRematch() {
    if (screen_ == Screen::Finished && lan_.role() == LanRole::Host &&
        localReady_ && remoteReady_) startHostRound();
}

void App::hostFinish(char winnerSide) {
    if (screen_ != Screen::Versus || lan_.role() != LanRole::Host) return;
    if (!sendOrFail(protocol::winner(activeRound_, winnerSide))) return;
    traceLan("ROUND_FINISHED", "round=" + str(activeRound_) + " winner=" + winnerSide);
    result_ = winnerSide == 'H' ? Result::Win : Result::Lose;
    screen_ = Screen::Finished;
}

void App::clientDeath() {
    if (clientReportedDeath_ || lan_.role() != LanRole::Client) return;
    if (sendOrFail(protocol::death(activeRound_))) clientReportedDeath_ = true;
}

// Le tableau de bord adverse n'est JAMAIS injecte dans Game local.
void App::processRemoteState(const protocol::Message& m) {
    if (m.round != activeRound_ ||
        (screen_ != Screen::Versus && screen_ != Screen::Finished)) return;
    remote_.board = m.board;
    remote_.stats = m.stats;
    remote_.received = true;
}

// Les decisions d'arbitrage ne sont emises que par le joueur hote.
void App::processControlMessage(const protocol::Message& m) {
    if (m.type == protocol::Type::Snapshot) { processRemoteState(m); return; }
    if (m.round != activeRound_) return;
    if (m.type == protocol::Type::Garbage && screen_ == Screen::Versus)
        game_.receiveGarbage(m.lines);
    if (m.type == protocol::Type::Death && lan_.role() == LanRole::Host)
        hostFinish('H');
    if (m.type == protocol::Type::Winner && lan_.role() == LanRole::Client &&
        screen_ == Screen::Versus) {
        result_ = m.winner == 'C' ? Result::Win : Result::Lose;
        screen_ = Screen::Finished;
    }
    if (m.type == protocol::Type::Rematch && lan_.role() == LanRole::Host &&
        screen_ == Screen::Finished) { remoteReady_ = true; maybeRematch(); }
}

void App::processRoundMessage(const protocol::Message& m) {
    if (m.type == protocol::Type::NewRound) {
        if (lan_.role() != LanRole::Client) { lan_.fail("N reserve a l'hote"); return; }
        startClientRound(m.round, m.seed);
        return;
    }
    if ((m.type == protocol::Type::Death || m.type == protocol::Type::Rematch) &&
        lan_.role() != LanRole::Host) { lan_.fail("Message client inattendu"); return; }
    if (m.type == protocol::Type::Winner && lan_.role() != LanRole::Client) {
        lan_.fail("Arbitrage reserve a l'hote"); return;
    }
    processControlMessage(m);
}

void App::dispatchMessage(const protocol::Message& m) {
    switch (m.type) {
        case protocol::Type::Ping: sendOrFail(protocol::pong(m.nonce)); return;
        case protocol::Type::Pong:
            networkMonitor_.onPong(m.nonce, ConnectionMonitor::Clock::now()); return;
        case protocol::Type::Hello: return;
        default: processRoundMessage(m); return;
    }
}

void App::receiveMessage(const std::string& frame) {
    const auto decoded = protocol::decode(frame);
    if (!decoded) { lan_.fail("Trame rejetee : " + decoded.error); return; }
    networkMonitor_.received(ConnectionMonitor::Clock::now());
    const auto& m = *decoded.message;
    if (m.type == protocol::Type::Hello) {
        if (helloReceived_) lan_.fail("HELLO duplique");
        else { helloReceived_ = true; traceLan("HANDSHAKE_OK", "protocol=v2"); }
        return;
    }
    if (!helloReceived_) { lan_.fail("HELLO attendu avant les donnees"); return; }
    dispatchMessage(m);
}

void App::sendSnapshot() {
    if (lan_.phase() == LanPhase::Connected)
        sendOrFail(protocol::snapshot(activeRound_, game_.stats(), game_.visibleBoard()));
}

void App::sendOutgoingGarbage() {
    const int outgoing = game_.takeOutgoingGarbage();
    if (outgoing > 0 && screen_ == Screen::Versus)
        sendOrFail(protocol::garbage(activeRound_, std::min(outgoing, 4)));
}

void App::tryFinishRound() {
    if (!game_.gameOver() || screen_ != Screen::Versus) return;
    sendSnapshot();
    if (lan_.phase() != LanPhase::Connected) return;
    if (lan_.role() == LanRole::Host) hostFinish('C');
    else clientDeath();
}

void App::beginHandshake() {
    networkMonitor_.connected(ConnectionMonitor::Clock::now());
    if (sendOrFail(protocol::hello())) {
        helloSent_ = true;
        traceLan("HELLO_SENT", "protocol=v2");
    }
}

bool App::networkFailed() {
    if (lan_.phase() != LanPhase::Failed && lan_.phase() != LanPhase::Disconnected)
        return false;
    error_ = lan_.error();
    traceLan("DISCONNECT", error_);
    screen_ = Screen::Error;
    return true;
}

void App::checkPeerTimeouts(ConnectionMonitor::Time now) {
    if (!helloReceived_ && networkMonitor_.handshakeExpired(now))
        lan_.fail("Negociation LAN expiree (5s)");
    else if (helloReceived_ && networkMonitor_.peerExpired(now))
        lan_.fail("Connexion silencieuse depuis plus de 8s");
    else if (helloReceived_ && networkMonitor_.probeExpired(now))
        lan_.fail("Absence de PONG depuis plus de 5s");
}

void App::pollHeartbeat(ConnectionMonitor::Time now) {
    if (!helloReceived_ || !networkMonitor_.pingDue(now)) return;
    const uint32_t nonce = networkMonitor_.markPing(now);
    sendOrFail(protocol::ping(nonce));
}

// Preparation de la connexion, verification de la session et distribution
// des messages. Renvoie false si le jeu ne peut pas avancer cette frame.
bool App::pollNetworkSession() {
    lan_.poll();
    if (networkFailed() || lan_.phase() != LanPhase::Connected) return false;
    if (!helloSent_) beginHandshake();
    if (networkFailed()) return false;
    for (const auto& frame : lan_.takeMessages()) {
        receiveMessage(frame);
        if (lan_.phase() != LanPhase::Connected) break;
    }
    if (networkFailed()) return false;
    const auto now = ConnectionMonitor::Clock::now();
    checkPeerTimeouts(now);
    if (networkFailed()) return false;
    pollHeartbeat(now);
    return !networkFailed() && helloReceived_;
}

void App::updateNetwork(int ms) {
    if (!pollNetworkSession()) return;
    if (screen_ == Screen::Waiting && lan_.role() == LanRole::Host) startHostRound();
    if (screen_ != Screen::Versus) return;
    if (!game_.gameOver()) game_.tick(ms);
    sendOutgoingGarbage();
    tryFinishRound();
    snapshotElapsedMs_ += ms;
    if (snapshotElapsedMs_ >= 90) { sendSnapshot(); snapshotElapsedMs_ = 0; }
}

} // namespace tetris
