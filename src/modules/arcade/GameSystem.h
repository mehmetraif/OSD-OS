#pragma once
#include <QByteArray>
#include <QObject>
#include <QSet>
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
// The helper also reads what the Pi did meanwhile (its clock, its
// temperature, its throttling, and in a network game on Wi-Fi the radio's
// scans), which ended() logs as one line, "arcade: system during the game:
// ...", and of which it returns what is worth a sentence on screen.
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

    // The nice value RetroArch or MAME runs at: -5 on a Raspberry Pi, ahead
    // of the app and the system's services (no real-time priority, which
    // could starve them), 0 elsewhere. The launcher gives it only where the
    // app's service allows it (LimitNICE=-5).
    static int emulatorNice();

    // A game has started; network: it plays over the network.
    void started(bool network);
    // The game has ended: everything the helper changed put back, and what
    // the Pi did during it logged. Returns a sentence for the player when
    // the Pi ran short of power or slowed itself down during it, the first
    // time each is so this run; else "".
    QString ended();

    // The sentence for the helper's notice= flags ("power", "throttled",
    // "power,throttled"; "" for "none").
    static QString noticeFor(const QString &flags);
    // notice and more, one after the other.
    static QString joined(const QString &notice, const QString &more);
    // How long ended() waits for the helper's line.
    static constexpr int kEndWaitMs = 1500;

    // The setting as saved, On by default.
    bool fullSpeedWanted() const;

private:
    void close();
    void readAnswer();
    QString noticeOnce(const QString &flags);

    AppCore *m_appCore = nullptr;
    QLocalSocket *m_socket = nullptr;
    QByteArray m_reply;
    // The helper's answer to the game's start was logged.
    bool m_startLogged = false;
    // Why the helper couldn't be asked, for the line at the end.
    QString m_why;
    // The sentences already said this run.
    QSet<QString> m_told;
};
