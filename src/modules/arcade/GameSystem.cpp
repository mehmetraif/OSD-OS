#include "GameSystem.h"

#include "../../AppCore.h"
#include "../../util/Board.h"
#include "../../util/RootRequest.h"

#include <QLocalSocket>

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
    QByteArray line = fullSpeedWanted() ? "start full-speed" : "start as-is";
    if (network)
        line += " network";
    connect(m_socket, &QLocalSocket::connected, this, [this, line] { m_socket->write(line + '\n'); });
    connect(m_socket, &QLocalSocket::readyRead, this, &GameSystem::readAnswer);
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) {
        if (error != QLocalSocket::PeerClosedError && !m_startLogged) {
            m_startLogged = true;
            qWarning("[Arcade] CPU governor for the game: osdos-game.socket couldn't be asked (%s)",
                     qPrintable(m_socket->errorString()));
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
    else
        qWarning("[Arcade] CPU governor for the game: osdos-game.socket said \"%s\"", qPrintable(answer));
}

void GameSystem::ended() {
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
        m_socket->write("end\n");
        m_socket->flush();
        m_socket->waitForBytesWritten(200);
    }
    close();
}

void GameSystem::close() {
    if (!m_socket)
        return;
    m_socket->disconnect(this);
    m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
}
