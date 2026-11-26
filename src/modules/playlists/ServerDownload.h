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
// else its type). A transfer that stops moving for half a minute fails, and
// so does one that runs more than a megabyte past the size the server
// announced, or that would leave less than 200 MB free on the folder's volume
// (checked as it starts and every 16 MB written). The
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

    // For tests: the free space a download must leave (200 MB; a negative
    // value brings that back).
    static void setFreeSpaceFloor(qint64 bytes);

signals:
    void progress(int percent);
    // reason is "not allowed" for a 401 or 403, else the network's error, or
    // why the file couldn't be written ("larger than the server said it
    // was", "not enough free space: less than 200 MB would be left"). mayPass says a failure may pass by
    // itself, so that trying again later is worth it: the network's, or the
    // server's own trouble (a 5xx), not what the server refuses or hasn't (a
    // 4xx), nor a file that couldn't be written.
    void finished(bool ok, const QString &finalPath, const QString &reason, bool mayPass);

private:
    explicit ServerDownload(QObject *parent);
    ~ServerDownload() override;

    QPointer<QObject> m_worker;
    bool m_cancelled = false;
};
