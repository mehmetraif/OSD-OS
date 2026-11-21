#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QString>
#include <QTimer>

// The control connection to one of the app's mpvs (its --input-ipc-server):
// the player's (MpvController), the menu music's (MenuMusic) and a module's
// sound (BackgroundMpv). mpv makes its socket a beat after it starts, so a
// single connect loses the race: connectTo() tries it every `retryMs` until
// it is made, or stopConnecting(). command() writes one command as mpv's JSON
// IPC takes it, a line of {"command": [...]}.
//
// A QLocalSocket otherwise, its signals and state as they are.
class MpvSocket : public QLocalSocket {
public:
    explicit MpvSocket(QObject *parent = nullptr, int retryMs = 100) : QLocalSocket(parent) {
        m_retry.setInterval(retryMs);
        QObject::connect(&m_retry, &QTimer::timeout, this, [this]() {
            if (state() == QLocalSocket::UnconnectedState)
                connectToServer(m_path);
        });
        QObject::connect(this, &QLocalSocket::connected, &m_retry, &QTimer::stop);
    }

    // Tried every retryMs from now, until connected.
    void connectTo(const QString &path) {
        m_path = path;
        m_retry.start();
    }
    void stopConnecting() { m_retry.stop(); }

    // Written where connected; false, and nothing sent, where not.
    bool command(const QJsonArray &args) {
        if (state() != QLocalSocket::ConnectedState)
            return false;
        write(QJsonDocument(QJsonObject{ { QStringLiteral("command"), args } }).toJson(QJsonDocument::Compact) + '\n');
        return true;
    }

private:
    QTimer  m_retry;
    QString m_path;
};
