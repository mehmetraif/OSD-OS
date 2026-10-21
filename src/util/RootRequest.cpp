#include "RootRequest.h"
#include "LegacyNames.h"

#include <QFileInfo>
#include <QLocalSocket>
#include <QTimer>

#include <memory>

namespace rootrequest {

QString socketPath(const QString &stem) {
    return legacy::env("RUN_DIR", QStringLiteral("/run")) + QStringLiteral("/osdos-") + stem + QStringLiteral(".sock");
}

bool available(const QString &stem, int launcherApi) {
    return legacy::envInt("LAUNCHER_API") >= launcherApi && QFileInfo::exists(socketPath(stem));
}

void ask(QObject *owner, const QString &stem, const QByteArray &line, int timeoutMs,
         std::function<void(const Answer &)> done) {
    const QString unit = QStringLiteral("osdos-") + stem + QStringLiteral(".socket");
    auto *socket = new QLocalSocket(owner);
    auto reply = std::make_shared<QByteArray>();
    auto sent = std::make_shared<bool>(false);
    auto finished = std::make_shared<bool>(false);
    const auto finish = [socket, reply, sent, finished, done](const QString &failure) {
        if (*finished)
            return;
        *finished = true;
        if (socket->isOpen())
            reply->append(socket->readAll());
        const QByteArray answer = reply->left(reply->indexOf('\n')).trimmed();
        socket->deleteLater();
        Answer result;
        result.ok = answer == "ok";
        result.answered = !answer.isEmpty();
        result.why = result.ok ? QString() : result.answered ? QString::fromUtf8(answer) : failure;
        result.sent = *sent;
        done(result);
    };
    QObject::connect(socket, &QLocalSocket::connected, owner, [socket, sent, line] {
        socket->write(line + '\n');
        *sent = true;
    });
    QObject::connect(socket, &QLocalSocket::readyRead, owner, [socket, reply, finish] {
        reply->append(socket->readAll());
        if (reply->contains('\n'))
            finish(QString());
    });
    QObject::connect(socket, &QLocalSocket::disconnected, owner, [finish] {
        finish(QStringLiteral("no answer"));
    });
    QObject::connect(socket, &QLocalSocket::errorOccurred, owner,
                     [socket, finish, unit](QLocalSocket::LocalSocketError error) {
        // The helper's end closed without its line, said before disconnected.
        if (error == QLocalSocket::PeerClosedError) {
            finish(QStringLiteral("no answer"));
            return;
        }
        const bool away = error == QLocalSocket::ServerNotFoundError || error == QLocalSocket::ConnectionRefusedError;
        finish(away ? unit + QStringLiteral(" isn't listening") : socket->errorString());
    });
    QTimer::singleShot(timeoutMs, owner, [finish] {
        finish(QStringLiteral("no answer in time"));
    });
    socket->connectToServer(socketPath(stem));
}

} // namespace rootrequest
