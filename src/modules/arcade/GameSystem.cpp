#include "GameSystem.h"

#include "../../AppCore.h"
#include "../../util/Board.h"
#include "../../util/LegacyNames.h"
#include "../../util/RootRequest.h"

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QStringList>

namespace {

const QString kStem = QStringLiteral("game");
const QString kModuleId = QStringLiteral("com.osdos.arcade");

} // namespace

GameSystem::GameSystem(AppCore *appCore, QObject *parent) : QObject(parent), m_appCore(appCore) {}

GameSystem::~GameSystem() {
    // Closed with the app: the helper puts everything back as it sees the
    // connection close.
    close();
}

bool GameSystem::fullSpeedWanted() const {
    const QVariant value = m_appCore ? m_appCore->get_setting(kModuleId, QLatin1String(kFullSpeedKey)) : QVariant();
    if (!value.isValid() || value.isNull())
        return true;
    if (value.typeId() == QMetaType::Bool)
        return value.toBool();
    return value.toString().compare(QLatin1String("Off"), Qt::CaseInsensitive) != 0;
}

void GameSystem::started(bool network) {
    // A game before this one never ended here (it can't: ended() always
    // follows): its connection closed first, which the helper takes as its
    // end.
    close();
    if (!rootrequest::available(kStem, kLauncherApi))
        return;
    m_socket = new QLocalSocket(this);
    m_reply.clear();
    m_startLogged = false;
    m_why.clear();
    QByteArray line = fullSpeedWanted() ? "start full-speed" : "start as-is";
    if (network)
        line += " network";
    connect(m_socket, &QLocalSocket::connected, this, [this, line] { m_socket->write(line + '\n'); });
    connect(m_socket, &QLocalSocket::readyRead, this, &GameSystem::readAnswer);
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) {
        if (error != QLocalSocket::PeerClosedError && !m_startLogged) {
            m_startLogged = true;
            m_why = QStringLiteral("osdos-game.socket couldn't be asked: ") + m_socket->errorString();
            qWarning("[Arcade] CPU governor for the game: %s", qPrintable(m_why));
        }
    });
    m_socket->connectToServer(rootrequest::socketPath(kStem));
}

void GameSystem::readAnswer() {
    m_reply.append(m_socket->readAll());
    const int end = m_reply.indexOf('\n');
    if (m_startLogged || end < 0)
        return;
    m_startLogged = true;
    const QString answer = QString::fromUtf8(m_reply.left(end)).trimmed();
    m_reply.remove(0, end + 1);
    if (answer.startsWith(QLatin1String("ok kept ")))
        qInfo("[Arcade] CPU governor for the game kept at %s", qPrintable(answer.mid(8)));
    else if (answer.startsWith(QLatin1String("ok ")))
        qInfo("[Arcade] CPU governor for the game: %s", qPrintable(answer.mid(3)));
    else {
        m_why = QStringLiteral("osdos-game.socket said \"") + answer + QLatin1Char('"');
        qWarning("[Arcade] CPU governor for the game: %s", qPrintable(m_why));
    }
}

QString GameSystem::ended() {
    QString notice;
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
        m_socket->write("end\n");
        m_socket->flush();
        // The helper's line of what the Pi did, after its answer to the
        // start: a few milliseconds' work for it, waited for at most
        // kEndWaitMs, as the screen comes back from the game.
        const auto summaryEnd = [this] { return m_startLogged ? m_reply.indexOf('\n') : -1; };
        QElapsedTimer waited;
        waited.start();
        while (summaryEnd() < 0 && m_socket->state() == QLocalSocket::ConnectedState
               && waited.elapsed() < kEndWaitMs)
            m_socket->waitForReadyRead(int(qMax<qint64>(1, kEndWaitMs - waited.elapsed())));
        if (summaryEnd() < 0 && m_socket->bytesAvailable() > 0)
            readAnswer();
        const int end = summaryEnd();
        if (end >= 0) {
            const QString line = QString::fromUtf8(m_reply.left(end)).trimmed();
            const QString flags = line.section(QLatin1Char(' '), 0, 0);
            if (flags.startsWith(QLatin1String("notice="))) {
                qInfo("arcade: system during the game: %s", qPrintable(line.section(QLatin1Char(' '), 1)));
                notice = noticeOnce(flags.mid(7));
            } else {
                qInfo("arcade: system during the game: not measured (osdos-game.socket said \"%s\")",
                      qPrintable(line));
            }
        } else {
            qInfo("arcade: system during the game: not measured (%s)",
                  m_why.isEmpty() ? "osdos-game.socket didn't answer in time" : qPrintable(m_why));
        }
    } else if (board::family() != board::Family::Other) {
        // On a Pi without the helper, or one it couldn't be asked on: said,
        // so that the log tells why the line has nothing.
        const QString why = !m_why.isEmpty() ? m_why
                            : legacy::envInt("LAUNCHER_API") < kLauncherApi
                                ? QStringLiteral("the launcher is below level %1").arg(kLauncherApi)
                                : QStringLiteral("no osdos-game.socket");
        qInfo("arcade: system during the game: not measured (%s)", qPrintable(why));
    }
    close();
    return notice;
}

QString GameSystem::noticeFor(const QString &flags) {
    const QStringList kinds = flags.split(QLatin1Char(','), Qt::SkipEmptyParts);
    const bool power = kinds.contains(QLatin1String("power"));
    const bool throttled = kinds.contains(QLatin1String("throttled"));
    if (power && throttled)
        return QStringLiteral("The Pi ran short of power and slowed itself down during the game: "
                              "a power supply made for it, and a heatsink or a fan, keep it at full speed.");
    if (power)
        return QStringLiteral("The Pi ran short of power during the game, which slows it down: "
                              "use a power supply made for it.");
    if (throttled)
        return QStringLiteral("The Pi slowed itself down during the game to keep cool: "
                              "a heatsink or a fan keeps it at full speed.");
    return QString();
}

QString GameSystem::noticeOnce(const QString &flags) {
    const QString notice = noticeFor(flags);
    if (notice.isEmpty() || m_told.contains(notice))
        return QString();
    m_told.insert(notice);
    return notice;
}

QString GameSystem::joined(const QString &notice, const QString &more) {
    if (more.isEmpty())
        return notice;
    return notice.isEmpty() ? more : notice + QLatin1Char(' ') + more;
}

void GameSystem::close() {
    if (!m_socket)
        return;
    m_socket->disconnect(this);
    m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
}
