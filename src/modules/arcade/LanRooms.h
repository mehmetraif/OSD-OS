#pragma once
#include "Libretro.h"
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QSet>
#include <QElapsedTimer>
#include <QString>
#include <functional>
#include <optional>

class QNetworkDatagram;
class QProcess;
class QTimer;
class QUdpSocket;

// LAN Play's sockets: the games RetroArch hosts on the local network, found
// by a query broadcast there that each host answers (libretro::lanQuery(),
// libretro::readLanAnswer()), and the ports OSD/OS's hosts give the machines
// joining them (libretro::PortClaims, at libretro::kLanClaimPort). Nothing of
// the games folder: the backend makes the rooms it lists of rooms().
class LanRooms : public QObject {
    Q_OBJECT
public:
    explicit LanRooms(QObject *parent = nullptr);

    // Where the query goes: broadcast to every network's hosts at
    // RetroArch's port, or a stand-in's address and port (a test's).
    void setDiscovery(const QString &address, quint16 port);
    // How long the answers are listened for: a second, or a test's.
    void setListenTime(int ms);
    // Where a host's OSD/OS gives the machines joining their ports
    // (libretro::kLanClaimPort), or a test's port.
    void setClaimPort(quint16 port);

    // The query sent, unless one's answers are still listened for: the
    // answers taken as they come for the time they are listened for, then
    // roomsRead() (at once, once this call is done with, for a query that
    // couldn't be sent). The query goes from a socket of its own, closed
    // once its answers have been listened for (or by forget()).
    void query();
    // The rooms the last query found, those without a password of
    // FinalBurn Neo's, in the order they were first heard (and of them, a
    // room of the query before not heard in this one: a broadcast is sent
    // but once, and it, or the answer, can be lost on the way), an id kept
    // while the host answers with the same game; why the network couldn't
    // be asked ("" when it could); and whether it has been.
    const QList<libretro::Room> &rooms() const { return m_lanRooms; }
    QString problem() const { return m_lanProblem; }
    bool asked() const { return m_lanRead; }
    // LAN Play turned off: the rooms heard forgotten, so that none of them is
    // offered once it is on again, before the network has been asked.
    void forget();

    // Joining a host's game: this machine's ports (as many as players) asked
    // of the host's OSD/OS at the claim port, three times at most, a quarter
    // of a second each; portsAnswered() once it has said which this
    // machine's player 1 takes, with the layout it said before them
    // (Classic, from an older OSD/OS that says none, after a moment), or
    // hasn't (nullopt), when the host is sent a release of whatever it
    // claimed. False when the host can't be asked at all. The ask goes from
    // a socket of its own, closed once it is answered or given up on.
    bool askForPorts(const QString &host, int players);
    // Whether this machine's way to the local network is Wi-Fi: no cable's
    // interface up with an address of a private network
    // (libretro::isLanAddress()), and a Wi-Fi one with one.
    static bool overWifi();

    // The ports a host gave this machine, given back as its game ends
    // (claimedFrom: that host; "" for none); and those of an answer that came
    // too late, or from another host. The release is a datagram sent twice,
    // kReleaseAgainMs apart, so that one lost on Wi-Fi doesn't keep the
    // ports from the next machine; now sends both at once (the app
    // stopping). Asking the same host for ports again before the second goes
    // out keeps it back, so that it doesn't give up the ports just asked for.
    void setClaimedFrom(const QString &host) { m_claimedFrom = host; }
    void releaseClaimed(bool now = false);
    void releaseClaim(const QString &address, bool now = false);
    static constexpr int kReleaseAgainMs = 300;

    // Hosting: the ports taken counted (this machine's own first, 1 to own,
    // of most, each machine joining's players no more than each), and each
    // OSD/OS joining told the layout and given its ports at the claim port,
    // until stopClaims() (the game ends). False without the socket for it: a
    // machine joining then takes RetroArch's first free port.
    bool startClaims(int own, int most, int each, libretro::Launch::Layout layout);
    void stopClaims();
    // While hosting, a claim is dropped (libretro::PortClaims::expire()) once
    // RetroArch's netplay port (libretro::kLanPort) no longer has a
    // connection from its address after having had one, or has had none
    // kClaimGraceMs after it was claimed. The connections are read from
    // /proc/net/tcp and tcp6 every kClaimCheckMs on Linux; elsewhere from
    // `netstat -an`, run only while a claim hasn't been seen connected
    // (every kClaimCheckElsewhereMs) and when RetroArch's log says a client
    // left (clientLeft()). A joiner rejoining from the same address claims
    // anew, and is given its time again.
    void clientLeft();
    static constexpr int kClaimGraceMs = 45000;
    static constexpr int kClaimCheckMs = 2000;
    static constexpr int kClaimCheckElsewhereMs = 15000;
    // For tests: what the connections are read from (the addresses, nothing
    // where they can't be told) and the time a claim is given.
    void setPeerSource(std::function<std::optional<QSet<QString>>()> source) { m_peerSource = std::move(source); }
    void setClaimGrace(int ms) { m_claimGrace = ms; }
    void setClaimCheckInterval(int ms) { m_claimCheckMs = ms; }
    // An answer to the ask for ports as it is read off the socket: taken
    // only from a sender on the local network (libretro::isLanSender()), as
    // the rooms' answers and the claims are; true once the game starts on
    // it. Public for a test's datagram from a sender it can't send from.
    bool takePortsAnswer(const QNetworkDatagram &answer);
    // For tests: the local ports the LAN query and the ask for ports are
    // bound to while their answers are listened for, 0 once they are closed.
    quint16 queryPort() const;
    quint16 askPort() const;
    bool releaseSocketOpen() const { return m_releaseSocket != nullptr; }
    // For tests: whether the second release of address's ports is waiting.
    bool releasing(const QString &address) const { return m_releasesAgain.contains(QHostAddress(address).toString()); }

signals:
    void roomsRead();
    void portsAnswered(const std::optional<libretro::PortRange> &ports, libretro::Launch::Layout layout);

private:
    // The answers taken as they come while the query is listened for, and
    // the rooms made of them once it has been, or why the network couldn't
    // be asked.
    void readLanAnswers();
    void finishLanRead();
    bool askAgain();
    void readPortsAnswer();
    void answerPorts(const std::optional<libretro::PortRange> &ports);
    void answerClaims();
    void checkClaims();
    void closeSocket(QUdpSocket *&socket);
    void closeReleaseSocket();
    void expireClaims(const QSet<QString> &connected);

    // The socket the query goes out of and the answers come to, where it
    // goes ("" to broadcast it), the time the answers are listened for, the
    // rooms heard meanwhile and those of the last query (and of them, by
    // lanKey(), those kept without being heard in it), why the network
    // couldn't be asked, whether it has been, and each room's id by its host
    // and game (those no longer heard forgotten once there are many).
    QUdpSocket *m_lanSocket = nullptr;
    QString m_lanAddress;
    quint16 m_lanPort = libretro::kLanPort;
    QTimer *m_lanListen = nullptr;
    int     m_lanListenMs = 0;
    QList<libretro::Room> m_lanHeard;
    QList<libretro::Room> m_lanRooms;
    QSet<QString> m_lanMissed;
    QString m_lanFailure;         // the query under way's
    QString m_lanProblem;
    bool    m_lanRead = false;
    QHash<QString, int> m_lanIds;
    int     m_lanNextId = 1;
    // Six players over the network (libretro::PortClaims): hosting, the
    // socket the machines joining ask for their ports at, and the count of
    // the ports taken; joining, the host asked (how many players, how many
    // times, its layout as it said before the ports, Classic until it does,
    // and whether it has; the ports that came before it did, waiting a moment
    // for it), and the host whose OSD/OS gave this machine its ports, given
    // back as the game ends ("" for none).
    QUdpSocket *m_claimSocket = nullptr;
    libretro::PortClaims m_claims;
    libretro::Launch::Layout m_claimLayout = libretro::Launch::Layout::Classic;
    quint16 m_claimPort = libretro::kLanClaimPort;
    struct Asking {
        QString host;
        int players = 1;
        int asked = 0;
        libretro::Launch::Layout layout = libretro::Launch::Layout::Classic;
        bool layoutSaid = false;
        std::optional<libretro::PortRange> ports;
    };
    std::optional<Asking> m_asking;
    QUdpSocket *m_claimAsk = nullptr;
    QTimer *m_claimWait = nullptr;
    QString m_claimedFrom;
    // The second release of each host's ports, waiting kReleaseAgainMs, by
    // its address.
    QHash<QString, QTimer *> m_releasesAgain;
    QUdpSocket *m_releaseSocket = nullptr;
    // Hosting: how old each claim is, how often the connections are looked
    // at, and the netstat under way elsewhere.
    QElapsedTimer m_claimClock;
    QTimer *m_claimCheck = nullptr;
    QProcess *m_netstat = nullptr;
    std::function<std::optional<QSet<QString>>()> m_peerSource;
    int m_claimGrace = kClaimGraceMs;
    int m_claimCheckMs = kClaimCheckMs;
};
