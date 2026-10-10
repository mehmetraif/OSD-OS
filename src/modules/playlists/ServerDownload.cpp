#include "ServerDownload.h"

#include "../../util/SslErrors.h"

#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>
#include <QUrl>
#include <memory>

namespace {

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
        // Cancelled before it got going.
        if (m_aborted)
            return;
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
        m_aborted = true;
        if (m_reply) {
            m_reply->disconnect(this);
            m_reply->abort();
        }
        m_file.reset();
        emit done(false, QString(), QStringLiteral("cancelled"));
    }

signals:
    void progress(int percent);
    void done(bool ok, const QString &finalPath, const QString &reason);

private:
    bool writePending() {
        if (!m_writeError.isEmpty()) return false;
        if (!m_file) {
            m_finalPath = m_base + QLatin1Char('.') + extensionOf(m_reply);
            m_file = std::make_unique<QSaveFile>(m_finalPath);
            if (!m_file->open(QIODevice::WriteOnly)) {
                m_writeError = m_file->errorString();
                return false;
            }
        }
        const QByteArray bytes = m_reply->readAll();
        if (m_file->write(bytes) != bytes.size()) {
            m_writeError = m_file->errorString();
            return false;
        }
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
        if (ok) {
            ok = writePending();
            if (ok) {
                ok = m_file->commit();
                if (!ok) m_writeError = m_file->errorString();
            }
            finalPath = m_finalPath;
        } else {
            reason = (status == 401 || status == 403) ? QStringLiteral("not allowed") : m_reply->errorString();
        }
        if (!m_writeError.isEmpty()) reason = m_writeError;
        m_file.reset(); // An uncommitted temporary file is removed; the old target survives.
        m_reply->deleteLater();
        m_reply = nullptr;
        emit done(ok, ok ? finalPath : QString(), reason);
    }

    QNetworkRequest m_request;
    QString m_base;
    std::unique_ptr<QSaveFile> m_file;
    QString m_finalPath;
    QString m_writeError;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_aborted = false;
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
    connect(worker, &Worker::done, thread, &QThread::quit, Qt::DirectConnection);
    connect(worker, &Worker::progress, download, &ServerDownload::progress);
    connect(worker, &Worker::done, download, [download](bool ok, const QString &finalPath, const QString &reason) {
        if (!download->m_cancelled)
            emit download->finished(ok, finalPath, reason);
        download->deleteLater();
    });
    thread->start();
    return download;
}

void ServerDownload::cancel() {
    if (m_cancelled)
        return;
    m_cancelled = true;
    if (m_worker) QMetaObject::invokeMethod(m_worker, "abort", Qt::QueuedConnection);
}

#include "ServerDownload.moc"
