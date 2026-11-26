#include "ServerDownload.h"

#include "../../util/ServerLog.h"
#include "../../util/SslErrors.h"

#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>
#include <QThread>
#include <QUrl>
#include <atomic>
#include <memory>

namespace {

// How far past the size the server announced (Content-Length) a download may
// run before it is stopped: a server's answer is that size, and one that
// sends more (framing it otherwise, by chunks) would fill the card.
constexpr qint64 kSizeMargin = qint64(1) << 20;
// The free space a download leaves on its folder's volume at the least: the
// card holds the system and the app's own data too. Checked as it starts,
// against what is still to come where the server says, and again every
// kFreeSpaceCheckBytes written.
constexpr qint64 kDefaultFreeSpaceFloor = qint64(200) << 20;
constexpr qint64 kFreeSpaceCheckBytes = qint64(16) << 20;
std::atomic<qint64> g_freeSpaceFloor{ kDefaultFreeSpaceFloor };

// What the server's reply says the file is: from its Content-Disposition
// name, else its type.
QString extensionOf(QNetworkReply *reply) {
    static const QRegularExpression kName(
        QStringLiteral("filename\\*?=(?:UTF-8'')?\"?([^\";]+)\"?"), QRegularExpression::CaseInsensitiveOption);
    const QString disposition = QString::fromUtf8(reply->rawHeader("Content-Disposition"));
    const QRegularExpressionMatch m = kName.match(disposition);
    if (m.hasMatch()) {
        const QString suffix = QFileInfo(QUrl::fromPercentEncoding(m.captured(1).toUtf8())).suffix();
        if (!suffix.isEmpty() && suffix.size() <= 5)
            return suffix.toLower();
    }
    const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
    if (type.contains(QLatin1String("mp4"))) return QStringLiteral("mp4");
    if (type.contains(QLatin1String("webm"))) return QStringLiteral("webm");
    if (type.contains(QLatin1String("quicktime"))) return QStringLiteral("mov");
    if (type.contains(QLatin1String("msvideo"))) return QStringLiteral("avi");
    return QStringLiteral("mkv");
}

// The transfer, living on the download's thread.
class Worker : public QObject {
    Q_OBJECT
public:
    Worker(const QNetworkRequest &request, const QString &base)
        : m_request(request), m_base(base) {}

public slots:
    void run() {
        m_nam = new QNetworkAccessManager(this);
        m_reply = m_nam->get(m_request);
        // Lets the socket hold the server back while the card catches up.
        m_reply->setReadBufferSize(4 * 1024 * 1024);
        connect(m_reply, &QNetworkReply::sslErrors, m_reply, [this](const QList<QSslError> &errors) {
            // The server's own certificate: self-signed on a LAN, as the
            // backends allow for it.
            if (m_reply->url().host() != m_request.url().host())
                return;
            const QList<QSslError> allowed = expectedLanSslErrors(errors);
            if (!allowed.isEmpty())
                m_reply->ignoreSslErrors(allowed);
        });
        connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
            if (m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() < 300)
                if (!writePending()) m_reply->abort();
        });
        connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
            if (total > 0)
                emit progress(int(received * 100 / total));
        });
        connect(m_reply, &QNetworkReply::finished, this, &Worker::onFinished);
    }

    void abort() {
        if (m_completed) return;
        m_completed = true;
        if (m_reply) {
            m_reply->disconnect(this);
            m_reply->abort();
        }
        m_file.reset();
        emit done(false, QString(), QStringLiteral("cancelled"), false);
    }

signals:
    void progress(int percent);
    void done(bool ok, const QString &finalPath, const QString &reason, bool mayPass);

private:
    // Whether writing still more (of what the server says is to come)
    // leaves the folder's volume its floor of free space; else why not, in
    // m_writeError. A volume whose free space can't be read isn't held up.
    bool leavesFloor(qint64 more) {
        const qint64 floor = g_freeSpaceFloor.load();
        QStorageInfo volume(QFileInfo(m_finalPath).absolutePath());
        volume.refresh();
        if (!volume.isValid() || volume.bytesAvailable() < 0 || volume.bytesAvailable() - more >= floor)
            return true;
        m_writeError = QStringLiteral("not enough free space: less than %1 MB would be left").arg(floor >> 20);
        return false;
    }

    bool writePending() {
        if (!m_writeError.isEmpty()) return false;
        if (!m_file) {
            m_finalPath = m_base + QLatin1Char('.') + extensionOf(m_reply);
            // The size the server says, where it says it of the bytes that
            // come: a reply it compresses (Content-Encoding) is larger once
            // unpacked than it says.
            const QVariant length = m_reply->header(QNetworkRequest::ContentLengthHeader);
            const QByteArray encoding = m_reply->rawHeader("Content-Encoding").trimmed().toLower();
            if (length.isValid() && (encoding.isEmpty() || encoding == "identity"))
                m_announced = std::max<qint64>(0, length.toLongLong());
            if (!leavesFloor(std::max<qint64>(0, m_announced)))
                return false;
            m_file = std::make_unique<QSaveFile>(m_finalPath);
            if (!m_file->open(QIODevice::WriteOnly)) {
                m_writeError = m_file->errorString();
                return false;
            }
        }
        const QByteArray bytes = m_reply->readAll();
        if (m_announced >= 0 && m_written + bytes.size() > m_announced + kSizeMargin) {
            m_writeError = QStringLiteral("larger than the server said it was");
            return false;
        }
        if (m_written + bytes.size() - m_checkedAt >= kFreeSpaceCheckBytes) {
            m_checkedAt = m_written + bytes.size();
            if (!leavesFloor(bytes.size() + std::max<qint64>(0, m_announced - m_checkedAt)))
                return false;
        }
        if (m_file->write(bytes) != bytes.size()) {
            m_writeError = m_file->errorString();
            return false;
        }
        m_written += bytes.size();
        return true;
    }

    void onFinished() {
        if (m_completed) return;
        m_completed = true;
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = m_writeError.isEmpty() && m_reply->error() == QNetworkReply::NoError
                  && status >= 200 && status < 300;
        QString finalPath;
        QString reason;
        // The network's failure, or the server's own trouble (no answer, a
        // transfer cut, a 5xx as it starts): may pass by itself. Not what
        // the server refuses or hasn't (a 4xx), nor a file that couldn't be
        // written.
        bool mayPass = false;
        if (ok) {
            ok = writePending();
            if (ok) {
                ok = m_file->commit();
                if (!ok) m_writeError = m_file->errorString();
            }
            finalPath = m_finalPath;
        } else {
            // The status, or the network's own word: never Qt's errorString(),
            // which names the whole address, the server and the item's id in
            // it, and the reason is shown, saved and logged.
            reason = (status == 401 || status == 403) ? QStringLiteral("not allowed") : serverlog::why(m_reply);
            mayPass = status < 400 || status >= 500;
        }
        if (!m_writeError.isEmpty()) {
            reason = m_writeError;
            mayPass = false;
        }
        m_file.reset(); // An uncommitted temporary file is removed; the old target survives.
        m_reply->deleteLater();
        m_reply = nullptr;
        emit done(ok, ok ? finalPath : QString(), reason, mayPass);
    }

    QNetworkRequest m_request;
    QString m_base;
    std::unique_ptr<QSaveFile> m_file;
    QString m_finalPath;
    QString m_writeError;
    qint64 m_announced = -1;   // the server's Content-Length; -1 where it says none
    qint64 m_written = 0;
    qint64 m_checkedAt = 0;    // m_written as the free space was last looked at
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_completed = false;
};

} // namespace

ServerDownload::ServerDownload(QObject *parent) : QObject(parent) {}

ServerDownload::~ServerDownload() {
    // A blocked disk write cannot be interrupted by quit(). The independent
    // thread owns its remaining lifetime and cleans up after cancellation.
    cancel();
}

ServerDownload *ServerDownload::start(const QNetworkRequest &request, const QString &base, QObject *parent) {
    auto *download = new ServerDownload(parent);
    QNetworkRequest timed = request;
    // No bytes for this long and the transfer is given up: a server that
    // went to sleep would otherwise hold the download slot for good.
    timed.setTransferTimeout(30000);
    auto *worker = new Worker(timed, base);
    download->m_worker = worker;
    auto *thread = new QThread;
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &Worker::run);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(worker, &Worker::progress, download, &ServerDownload::progress);
    connect(worker, &Worker::done, download,
            [download](bool ok, const QString &finalPath, const QString &reason, bool mayPass) {
        if (!download->m_cancelled)
            emit download->finished(ok, finalPath, reason, mayPass);
        // Nothing left to abort: the thread is quit (below) only once this
        // has run, so a cancel() can never reach a worker being freed.
        download->m_worker = nullptr;
        download->deleteLater();
    });
    // Queued, to the app's thread (the QThread object's), and after the
    // handler above: the worker is freed as its thread ends, and until then
    // cancel() may still post to it.
    connect(worker, &Worker::done, thread, &QThread::quit, Qt::QueuedConnection);
    thread->start();
    return download;
}

void ServerDownload::setFreeSpaceFloor(qint64 bytes) {
    g_freeSpaceFloor.store(bytes < 0 ? kDefaultFreeSpaceFloor : bytes);
}

void ServerDownload::cancel() {
    if (m_cancelled)
        return;
    m_cancelled = true;
    if (m_worker) QMetaObject::invokeMethod(m_worker, "abort", Qt::QueuedConnection);
}

#include "ServerDownload.moc"
