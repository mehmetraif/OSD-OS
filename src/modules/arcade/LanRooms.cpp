#include "LanRooms.h"
#include "../../util/NetInterfaces.h"
#include <QHostAddress>
#include <QFile>
#include <QNetworkDatagram>
#include <QProcess>
#include <QTimer>
#include <QUdpSocket>
#include <algorithm>
#include <utility>

namespace {

// How long the hosts on the local network are given to answer a query, and
// the most rooms taken of the answers.
constexpr int kLanListenMs = 1000;
constexpr qsizetype kMostLanRooms = 64;
// How long a host's OSD/OS is given to answer for the ports of a game joined
// on the local network, each time it is asked, and how many times at most.
constexpr int kClaimWaitMs = 250;
constexpr int kClaimAskings = 3;
// How long ports that came without the host's layout wait for it (a datagram
// of its own, sent before them: lost, or an older OSD/OS's, which says none).
constexpr int kLayoutWaitMs = 150;

// A room on the local network as its id is kept: where it is joined, its
// game and who hosts it.
QString lanKey(const libretro::Room &room) {
    return room.joinAddress + QLatin1Char('\n') + room.rom + QLatin1Char('\n') + room.host;
}
// Whether one of the rooms is joined at joinAddress: hosted there.
bool hostedAt(const QList<libretro::Room> &rooms, const QString &joinAddress) {
    return std::any_of(rooms.cbegin(), rooms.cend(),
                       [&joinAddress](const libretro::Room &room) { return room.joinAddress == joinAddress; });
}

// Where a query reaches every host of the networks this machine is on: the
// broadcast the system sends on its main one, and each network's own, which
// reaches the hosts of another.
QList<QHostAddress> broadcastAddresses() {
    QList<QHostAddress> targets{ QHostAddress(QHostAddress::Broadcast) };
    const QList<netif::Interface> interfaces = netif::list();
    for (const netif::Interface &interface : interfaces) {
        if (!interface.up || !interface.canBroadcast || interface.kind == netif::Kind::Loopback)
            continue;
        for (const netif::Address &entry : interface.addresses) {
            if (entry.broadcast.protocol() == QAbstractSocket::IPv4Protocol && !targets.contains(entry.broadcast))
                targets << entry.broadcast;
        }
    }
    return targets;
}

} // namespace

bool LanRooms::overWifi() {
    bool cable = false;
    bool wifi = false;
    const QList<netif::Interface> interfaces = netif::list();
    for (const netif::Interface &interface : interfaces) {
        if (!interface.up || !interface.running
            || (interface.kind != netif::Kind::Ethernet && interface.kind != netif::Kind::Wifi))
            continue;
        for (const netif::Address &entry : interface.addresses) {
            bool ipv4 = false;
            const quint32 number = entry.ip.toIPv4Address(&ipv4);
            if (!ipv4 || !libretro::isLanAddress(number))
                continue;
            (interface.kind == netif::Kind::Wifi ? wifi : cable) = true;
        }
    }
    return wifi && !cable;
}

LanRooms::LanRooms(QObject *parent) : QObject(parent) {
    m_lanListen = new QTimer(this);
    m_lanListen->setSingleShot(true);
    m_lanListenMs = kLanListenMs;
    connect(m_lanListen, &QTimer::timeout, this, &LanRooms::finishLanRead);
}

void LanRooms::setDiscovery(const QString &address, quint16 port) {
    m_lanAddress = address;
    m_lanPort = port;
}

void LanRooms::setListenTime(int ms) {
    m_lanListenMs = ms;
}

void LanRooms::setClaimPort(quint16 port) {
    m_claimPort = port;
}

void LanRooms::query() {
    // A query out: its answers are still being listened for.
    if (m_lanListen->isActive())
        return;
    // A socket of its own for each query, closed once its answers have been
    // listened for (finishLanRead()): nothing stays bound between them.
    if (!m_lanSocket) {
        m_lanSocket = new QUdpSocket(this);
        connect(m_lanSocket, &QUdpSocket::readyRead, this, &LanRooms::readLanAnswers);
    }
    m_lanFailure.clear();
    if (m_lanSocket->state() != QAbstractSocket::BoundState
        && !m_lanSocket->bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        m_lanFailure = m_lanSocket->errorString();
    } else {
        // Every host of the networks this machine is on, or a stand-in.
        const QList<QHostAddress> targets = m_lanAddress.isEmpty() ? broadcastAddresses()
                                                                   : QList<QHostAddress>{ QHostAddress(m_lanAddress) };
        bool sent = false;
        for (const QHostAddress &target : targets) {
            if (m_lanSocket->writeDatagram(libretro::lanQuery(), target, m_lanPort) >= 0)
                sent = true;
            else if (m_lanFailure.isEmpty())
                m_lanFailure = m_lanSocket->errorString();
        }
        if (sent)
            m_lanFailure.clear();
    }
    // The answers listened for; or, sent nowhere, why said the same way, once
    // this call is done with.
    m_lanListen->start(m_lanFailure.isEmpty() ? m_lanListenMs : 0);
}

void LanRooms::forget() {
    m_lanListen->stop();
    closeSocket(m_lanSocket);
    m_lanHeard.clear();
    m_lanRooms.clear();
    m_lanMissed.clear();
    m_lanProblem.clear();
    m_lanRead = false;
}

void LanRooms::readLanAnswers() {
    while (m_lanSocket->hasPendingDatagrams()) {
        // A byte more than an answer, so that a bigger packet is told apart.
        const QNetworkDatagram datagram = m_lanSocket->receiveDatagram(libretro::kLanAnswerSize + 1);
        // One that comes once the query's rooms have been made isn't taken,
        // nor one from outside the local network. Nothing proves where an
        // answer comes from, and RetroArch takes them as they come too: a room
        // only ever has RetroArch, once it is chosen, connect to the address
        // on this network the answer came from, at the port it gave.
        if (!m_lanListen->isActive() || !libretro::isLanSender(datagram.senderAddress()))
            continue;
        const QHostAddress from(datagram.senderAddress().toIPv4Address());
        std::optional<libretro::Room> room = libretro::readLanAnswer(datagram.data(), from.toString());
        // Only those that can be listed, so many at most: FinalBurn Neo's,
        // without a password (RetroArch would ask for it on its own screen).
        // A host once: the query reaches it by more than one broadcast
        // address, and it answers each.
        if (!room || !room->joinable || room->password || m_lanHeard.size() >= kMostLanRooms
            || hostedAt(m_lanHeard, room->joinAddress))
            continue;
        // Its id, the one it had while it was heard with the same game.
        const QString key = lanKey(*room);
        auto id = m_lanIds.constFind(key);
        if (id == m_lanIds.constEnd())
            id = m_lanIds.insert(key, m_lanNextId++);
        room->id = *id;
        m_lanHeard << *room;
    }
}

void LanRooms::closeSocket(QUdpSocket *&socket) {
    if (!socket)
        return;
    // Its own signal may be what is being handled: freed once that is done.
    socket->disconnect(this);
    socket->close();
    std::exchange(socket, nullptr)->deleteLater();
}

void LanRooms::finishLanRead() {
    // The query's time is up: what comes after it isn't taken, and the
    // socket isn't left bound until the next.
    closeSocket(m_lanSocket);
    const QString problem = m_lanFailure.isEmpty() ? QString()
                            : QStringLiteral("The local network can't be reached: ") + m_lanFailure;
    if (!problem.isEmpty() && problem != m_lanProblem)
        qWarning("[Arcade] %s", qPrintable(problem));
    // A room of the last query not heard in this one kept through it, unless
    // its host answered with another: a broadcast is sent but once, and it,
    // or the answer, can be lost on the way. Not through a second, nor a
    // query that couldn't be sent.
    QSet<QString> missed;
    if (m_lanFailure.isEmpty()) {
        for (const libretro::Room &room : std::as_const(m_lanRooms)) {
            const QString key = lanKey(room);
            if (m_lanMissed.contains(key) || m_lanHeard.size() >= kMostLanRooms
                || hostedAt(m_lanHeard, room.joinAddress))
                continue;
            m_lanHeard << room;
            missed.insert(key);
        }
    }
    m_lanMissed = std::move(missed);
    // In the order they were first heard, whichever answered first now.
    std::sort(m_lanHeard.begin(), m_lanHeard.end(),
              [](const libretro::Room &a, const libretro::Room &b) { return a.id < b.id; });
    m_lanRooms = std::exchange(m_lanHeard, {});
    m_lanProblem = problem;
    m_lanRead = true;
    // The ids of rooms no longer heard forgotten, once they are many.
    if (m_lanIds.size() > 4 * kMostLanRooms) {
        QHash<QString, int> kept;
        for (const libretro::Room &room : std::as_const(m_lanRooms))
            kept.insert(lanKey(room), room.id);
        m_lanIds = std::move(kept);
    }
    emit roomsRead();
}

bool LanRooms::askForPorts(const QString &host, int players) {
    // A second release of this host's ports not sent yet would give up the
    // ones about to be asked for.
    if (QTimer *again = m_releasesAgain.take(QHostAddress(host).toString())) {
        delete again;
        closeReleaseSocket();
    }
    m_asking = Asking{ host, players };
    if (askAgain())
        return true;
    m_asking.reset();
    closeSocket(m_claimAsk);
    return false;
}

bool LanRooms::askAgain() {
    if (!m_claimWait) {
        m_claimWait = new QTimer(this);
        m_claimWait->setSingleShot(true);
        // Not answered: asked again, then joined without an answer. Ports
        // answered without the layout, which hasn't come after them either:
        // joined on the Classic layout.
        connect(m_claimWait, &QTimer::timeout, this, [this] {
            if (!m_asking)
                return;
            if (const std::optional<libretro::PortRange> ports = m_asking->ports) {
                qInfo("[Arcade] %s said nothing of its layout: the Classic one", qPrintable(m_asking->host));
                answerPorts(ports);
                return;
            }
            if (askAgain())
                return;
            // Its socket closed with no answer: one coming later would find
            // none to give its ports back from, so the host is told now, as
            // it would have been then (a host that never heard the ask, or
            // RetroArch alone, has nothing to give back).
            if (m_asking->asked > 0)
                releaseClaim(m_asking->host);
            answerPorts(std::nullopt);
        });
    }
    // A socket of its own for each join's ask, closed once it has its answer
    // or has given up (answerPorts()): what an earlier ask was answered
    // later never reaches this one, and nothing stays bound between them.
    if (!m_claimAsk) {
        m_claimAsk = new QUdpSocket(this);
        connect(m_claimAsk, &QUdpSocket::readyRead, this, &LanRooms::readPortsAnswer);
    }
    const QHostAddress host(m_asking->host);
    if (m_asking->asked >= kClaimAskings || host.isNull()
        || (m_claimAsk->state() != QAbstractSocket::BoundState && !m_claimAsk->bind(QHostAddress(QHostAddress::AnyIPv4), 0))
        || m_claimAsk->writeDatagram(libretro::claimQuery(m_asking->players), host, m_claimPort) < 0)
        return false;
    ++m_asking->asked;
    m_claimWait->start(kClaimWaitMs);
    return true;
}

void LanRooms::readPortsAnswer() {
    while (m_claimAsk && m_claimAsk->hasPendingDatagrams()) {
        if (takePortsAnswer(m_claimAsk->receiveDatagram(64)))
            return;
    }
}

bool LanRooms::takePortsAnswer(const QNetworkDatagram &answer) {
    // From the local network alone, as the rooms' answers and the claims
    // are taken: nothing from elsewhere is read, nor sent its ports back.
    if (!libretro::isLanSender(answer.senderAddress()))
        return false;
    // The host's layout and its ports have both come: the game starts.
    const auto start = [this](const libretro::PortRange &ports) {
        m_claimWait->stop();
        qInfo("[Arcade] %s gives this machine's %d player(s) %d port(s) from %d, on the %s layout",
              qPrintable(m_asking->host), m_asking->players, ports.players, ports.first,
              m_asking->layout == libretro::Launch::Layout::Panel ? "panel" : "classic");
        answerPorts(ports);
    };
    const QHostAddress from(answer.senderAddress().toIPv4Address());
    const bool fromHost = m_asking && from == QHostAddress(m_asking->host);
    // The host's layout comes ahead of the ports (an older OSD/OS says
    // none: Classic), each a datagram of its own: ports that come first
    // wait a moment for it.
    if (const std::optional<libretro::Launch::Layout> layout = libretro::readClaimLayout(answer.data())) {
        if (!fromHost)
            return false;
        m_asking->layout = *layout;
        m_asking->layoutSaid = true;
        if (const std::optional<libretro::PortRange> waiting = m_asking->ports) {
            start(*waiting);
            return true;
        }
        return false;
    }
    const std::optional<libretro::PortRange> ports = libretro::readClaimAnswer(answer.data());
    // Neither, passed over: OSD/OS 0.0.28 to 0.0.32 said first that it
    // played at the game's own rate (OSDOS PACE game).
    if (!ports)
        return false;
    // One that comes too late, or from another host: its ports given
    // back. But not the ports this machine plays on: a host asked twice
    // answers twice, the second answer coming once the game has started
    // on the first, and giving those back had the host hand them to the
    // next machine too.
    if (!fromHost) {
        if (ports->first > 0 && from != QHostAddress(m_claimedFrom))
            releaseClaim(from.toString());
        return false;
    }
    if (!m_asking->layoutSaid) {
        m_asking->ports = ports;
        m_claimWait->start(kLayoutWaitMs);
        return false;
    }
    start(*ports);
    return true;
}

void LanRooms::answerPorts(const std::optional<libretro::PortRange> &ports) {
    const Asking asking = *std::exchange(m_asking, std::nullopt);
    closeSocket(m_claimAsk);
    emit portsAnswered(ports, asking.layout);
}

void LanRooms::releaseClaimed(bool now) {
    if (!m_claimedFrom.isEmpty())
        releaseClaim(std::exchange(m_claimedFrom, QString()), now);
}

void LanRooms::releaseClaim(const QString &address, bool now) {
    const QHostAddress host(address);
    if (!m_releaseSocket)
        m_releaseSocket = new QUdpSocket(this);
    m_releaseSocket->writeDatagram(libretro::claimRelease(), host, m_claimPort);
    const QString key = host.toString();
    delete m_releasesAgain.take(key);
    if (now) {
        m_releaseSocket->writeDatagram(libretro::claimRelease(), host, m_claimPort);
        closeReleaseSocket();
        return;
    }
    // Again a moment later: a datagram lost on the way would keep the ports
    // from the next machine for the rest of the host's game.
    auto *again = new QTimer(this);
    again->setSingleShot(true);
    connect(again, &QTimer::timeout, this, [this, host, key, again] {
        if (m_releasesAgain.value(key) != again)
            return;
        m_releasesAgain.remove(key);
        again->deleteLater();
        m_releaseSocket->writeDatagram(libretro::claimRelease(), host, m_claimPort);
        closeReleaseSocket();
    });
    m_releasesAgain.insert(key, again);
    again->start(kReleaseAgainMs);
}

void LanRooms::closeReleaseSocket() {
    // Sent from, never read: bound only while a second release waits, so
    // nothing stays bound once the ports are given back.
    if (m_releasesAgain.isEmpty())
        closeSocket(m_releaseSocket);
}

bool LanRooms::startClaims(int own, int most, int each, libretro::Launch::Layout layout) {
    stopClaims();
    m_claims.reset(own, most, each);
    m_claimLayout = layout;
    m_claimClock.start();
    m_claimSocket = new QUdpSocket(this);
    if (!m_claimSocket->bind(QHostAddress(QHostAddress::AnyIPv4), m_claimPort)) {
        qWarning("[Arcade] The machines joining can't be given their ports: %s", qPrintable(m_claimSocket->errorString()));
        stopClaims();
        return false;
    }
    connect(m_claimSocket, &QUdpSocket::readyRead, this, &LanRooms::answerClaims);
    // The claims looked at against RetroArch's connections as the game goes
    // on: often where it costs a small file's read (Linux, or a test's
    // source), else only while a claim is still to be seen connected.
#ifdef Q_OS_LINUX
    const bool cheap = true;
#else
    const bool cheap = bool(m_peerSource);
#endif
    m_claimCheck = new QTimer(this);
    m_claimCheck->setInterval(cheap ? m_claimCheckMs : kClaimCheckElsewhereMs);
    connect(m_claimCheck, &QTimer::timeout, this, [this, cheap] {
        if (cheap || m_claims.awaiting())
            checkClaims();
    });
    m_claimCheck->start();
    return true;
}

void LanRooms::clientLeft() {
    if (m_claimSocket)
        checkClaims();
}

void LanRooms::checkClaims() {
    if (m_claims.empty())
        return;
    if (m_peerSource) {
        if (const std::optional<QSet<QString>> peers = m_peerSource())
            expireClaims(*peers);
        return;
    }
#ifdef Q_OS_LINUX
    // Both tables: RetroArch listens on IPv6 where it can, its IPv4 peers
    // then ::ffff:a.b.c.d.
    QByteArray table;
    for (const char *path : { "/proc/net/tcp", "/proc/net/tcp6" }) {
        QFile file{ QString::fromLatin1(path) };
        if (file.open(QIODevice::ReadOnly))
            table += file.read(4 * 1024 * 1024) + '\n';
    }
    if (!table.trimmed().isEmpty())
        expireClaims(libretro::procNetPeers(table, libretro::kLanPort));
#else
    // netstat, asked without a wait: one at a time, its answer taken as it
    // comes, nothing done where it can't run.
    if (m_netstat)
        return;
    m_netstat = new QProcess(this);
    connect(m_netstat, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        QProcess *netstat = std::exchange(m_netstat, nullptr);
        netstat->deleteLater();
        if (status == QProcess::NormalExit && code == 0 && m_claimSocket)
            expireClaims(libretro::netstatPeers(netstat->readAllStandardOutput(), libretro::kLanPort));
    });
    connect(m_netstat, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        QProcess *netstat = std::exchange(m_netstat, nullptr);
        if (netstat)
            netstat->deleteLater();
    });
    m_netstat->start(QStringLiteral("netstat"), { QStringLiteral("-an") });
#endif
}

void LanRooms::expireClaims(const QSet<QString> &connected) {
    const QStringList dropped = m_claims.expire(connected, m_claimClock.elapsed(), m_claimGrace);
    for (const QString &address : dropped)
        qInfo("[Arcade] %s has no connection to the game: its ports are free again", qPrintable(address));
}

void LanRooms::answerClaims() {
    while (m_claimSocket->hasPendingDatagrams()) {
        // From the local network alone, as the rooms' answers are taken.
        const QNetworkDatagram packet = m_claimSocket->receiveDatagram(64);
        if (!libretro::isLanSender(packet.senderAddress()))
            continue;
        const QString from = QHostAddress(packet.senderAddress().toIPv4Address()).toString();
        if (const std::optional<int> players = libretro::readClaimQuery(packet.data())) {
            // The layout first, the ports after: a joiner starts its game on
            // the ports, and takes the layout with them; an older one reads
            // the ports alone.
            const libretro::PortRange ports = m_claims.claim(from, *players, m_claimClock.elapsed());
            m_claimSocket->writeDatagram(libretro::claimLayout(m_claimLayout), packet.senderAddress(),
                                         packet.senderPort());
            m_claimSocket->writeDatagram(libretro::claimAnswer(ports), packet.senderAddress(), packet.senderPort());
            qInfo("[Arcade] %s joins with %d player(s): %d port(s) from %d", qPrintable(from), *players, ports.players,
                  ports.first);
        } else if (libretro::isClaimRelease(packet.data())) {
            m_claims.release(from);
        }
    }
}

void LanRooms::stopClaims() {
    delete m_claimSocket;
    m_claimSocket = nullptr;
    delete m_claimCheck;
    m_claimCheck = nullptr;
}

quint16 LanRooms::queryPort() const {
    return m_lanSocket && m_lanSocket->state() == QAbstractSocket::BoundState ? m_lanSocket->localPort() : 0;
}

quint16 LanRooms::askPort() const {
    return m_claimAsk && m_claimAsk->state() == QAbstractSocket::BoundState ? m_claimAsk->localPort() : 0;
}
