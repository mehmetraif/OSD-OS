#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>

class AppCore;
class QLocalSocket;

// What the system does for an arcade game, around RetroArch or MAME: on a
// Raspberry Pi with OSD/OS's root helpers (the image, or scripts/install.sh),
// the CPU's governor at performance for the length of the game and put back
// as it ends, through osdos-game.socket (osdos-stop game), and the flag the
// system's own timers wait on. The connection is held for the whole game:
// should the app die or its service stop mid-game, the helper sees it close
// and puts everything back all the same.
//
// ArcadeBackend calls started() once RetroArch or MAME is running and
// ended() when it has ended; everywhere else (a computer, a Pi without the
// helper) neither does anything, nor says anything off a Pi.
class GameSystem : public QObject {
    Q_OBJECT
public:
    explicit GameSystem(AppCore *appCore, QObject *parent = nullptr);
    ~GameSystem() override;

    // The launcher level that has osdos-game.socket (OSDOS_LAUNCHER_API).
    static constexpr int kLauncherApi = 11;
    // The Arcade module's setting that lets a game have the CPU at full
    // speed, On when nothing is saved.
    static constexpr const char *kFullSpeedKey = "cpu_full_speed";

    // A game has started; network: it plays over the network.
    void started(bool network);
    // The game has ended: everything the helper changed put back.
    void ended();

    // The setting as saved, On by default.
    bool fullSpeedWanted() const;

private:
    void close();
    void readAnswer();

    AppCore *m_appCore = nullptr;
    QLocalSocket *m_socket = nullptr;
    QByteArray m_reply;
    // The helper's answer to the game's start was logged.
    bool m_startLogged = false;
};
