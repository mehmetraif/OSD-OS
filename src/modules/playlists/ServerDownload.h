#pragma once

#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QString>

class QThread;

// One download from a media server (MediaServer::downloadRequest), on a
// thread of its own: the reply is read and the file written there, so a
// server faster than the card never holds the app's thread (the kernel
// pauses whoever writes while the card catches up, and that is minutes for a
// film). The bytes go to a QSaveFile temporary file, committed atomically to
// <base>.<extension> only when complete. An existing good file survives failure.
// The target name is chosen once
// the server's reply says what the file is (its Content-Disposition name,
// else its type). A transfer that stops moving for half a minute fails. The
// server's own TLS allowances apply (SslErrors.h). Reports on the app's
// thread; deletes itself once it has.
//
//     auto *download = ServerDownload::start(server->downloadRequest(id), base, this);
//     connect(download, &ServerDownload::progress, …);
//     connect(download, &ServerDownload::finished, …);
class ServerDownload : public QObject {
    Q_OBJECT
public:
    static ServerDownload *start(const QNetworkRequest &request, const QString &base, QObject *parent);

    // Ends it and removes the part file; finished() doesn't follow.
    void cancel();

signals:
    void progress(int percent);
    // reason is "not allowed" for a 401 or 403, else the network's error.
    void finished(bool ok, const QString &finalPath, const QString &reason);

private:
    explicit ServerDownload(QObject *parent);
    ~ServerDownload() override;

    QPointer<QObject> m_worker;
    bool m_cancelled = false;
};
