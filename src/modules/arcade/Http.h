#pragma once
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <functional>

// What the Arcade module's requests share: the lobby's, catver.ini's,
// RetroAchievements', ArcadeDB's and the installers' downloads.
namespace arcadehttp {

// Marks a reply given up on for its size (limitReply()).
constexpr const char *kTooBig = "osdosTooBig";

// A request as the module makes them: no redirect to a less safe scheme, the
// app named, and given up on once nothing has come for timeoutMs.
inline QNetworkRequest requestFor(const QUrl &url, int timeoutMs) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("OSD-OS/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(timeoutMs);
    return request;
}

// A reply far bigger than what was asked for isn't it, and isn't read whole:
// given up on once its size says so (its header, or as much as has come), and
// marked so (kTooBig).
inline void limitReply(QNetworkReply *reply, qint64 limit) {
    const auto tooBig = [reply, limit](qint64 size) {
        if (reply->isRunning() && size > limit) {
            reply->setProperty(kTooBig, true);
            reply->abort();
        }
    };
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, tooBig] {
        tooBig(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong());
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [tooBig](qint64 received, qint64 total) {
        tooBig(std::max(received, total));
    });
}

// A finished reply given up on, or with more than limit to read, which a
// fast answer reaches before its progress says so.
inline bool tooBig(QNetworkReply *reply, qint64 limit) {
    return reply->property(kTooBig).toBool() || reply->bytesAvailable() > limit;
}

// A server's answer that there were too many requests (HTTP 429), or that
// it is busy (503): nothing more is asked of it for a while.
inline bool asksForAPause(int status) {
    return status == 429 || status == 503;
}

// How long such an answer says to wait (Retry-After, in seconds, an hour at
// most), else fallbackSeconds.
inline int pauseAsked(QNetworkReply *reply, int fallbackSeconds) {
    bool ok = false;
    const int seconds = reply->rawHeader("Retry-After").trimmed().toInt(&ok);
    return ok && seconds > 0 ? std::min(seconds, 3600) : fallbackSeconds;
}

// A file downloaded as the installers download theirs, written into path as
// it comes (made anew), and given up on once nothing has come for timeoutMs
// (a Wi-Fi that dropped: TCP alone takes a quarter of an hour to say so) or
// once it is bigger than maxBytes (what it can be, for the message: "FinalBurn
// Neo's files"). onProgress as it comes, then onDone("") once it is whole on
// the disk, else onDone(why not), the server's answer first (Qt takes a 404
// for an error too); the file is left for the caller to remove. context: the
// caller, which the download goes with. The reply, nullptr when path couldn't
// be written (onDone has been called).
inline QNetworkReply *downloadToFile(QNetworkAccessManager *nam, const QUrl &url, const QString &path, int timeoutMs,
                                     qint64 maxBytes, const QString &what, QObject *context,
                                     const std::function<void(qint64 received, qint64 total)> &onProgress,
                                     const std::function<void(const QString &error)> &onDone) {
    QNetworkReply *reply = nam->get(requestFor(url, timeoutMs));
    limitReply(reply, maxBytes);
    auto *file = new QFile(path, reply);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString error = QStringLiteral("%1 could not be written: %2").arg(file->fileName(), file->errorString());
        // Nothing listens to it yet: it goes here, the file with it.
        reply->abort();
        reply->deleteLater();
        onDone(error);
        return nullptr;
    }
    QObject::connect(reply, &QNetworkReply::readyRead, file, [reply, file] {
        if (file->write(reply->readAll()) < 0)
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, context, onProgress);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, file, maxBytes, what, onDone] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        file->write(reply->readAll());
        const bool flushed = file->flush();
        file->close();
        if (reply->error() != QNetworkReply::NoError || status != 200 || !flushed) {
            onDone(tooBig(reply, maxBytes)                  ? QStringLiteral("%1 sent more than %2 can be").arg(reply->url().host(), what)
                   : status != 0 && status != 200              ? QStringLiteral("%1 answered %2").arg(reply->url().host()).arg(status)
                   : reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                              : file->errorString());
            return;
        }
        onDone(QString());
    });
    return reply;
}

// A badge's name as RetroAchievements gives it, and as it goes in a path.
inline bool isBadgeName(const QString &name) {
    static const QRegularExpression valid(QRegularExpression::anchoredPattern(QStringLiteral("[0-9A-Za-z]{1,32}")));
    return valid.match(name).hasMatch();
}

// A picture: a PNG or a JPEG, as its first bytes say.
inline bool isPicture(const QByteArray &data) {
    return data.startsWith("\x89PNG\r\n\x1a\n") || data.startsWith("\xff\xd8\xff");
}

// A server's pictures downloaded one at a time, in the order they were
// asked for (RetroAchievements' media server, ArcadeDB's): each one's done
// with what it holds, a picture (isPicture()) of limit at most, or nothing
// (empty) when it couldn't be had. Nothing is asked while the server's pause
// lasts (pausedUntil, its owner's); an answer asking for one
// (asksForAPause()) goes to onPause, which sets the pause and says whether
// the file is asked for again after it (tried: how many times it was before
// for that reason), else it is had as nothing. context: the server's
// object, which the queue is a member of and its downloads go with.
class MediaQueue {
public:
    using Done = std::function<void(const QByteArray &data)>;
    using OnPause = std::function<bool(QNetworkReply *reply, int status, const QString &name, int tried)>;

    // server: whose files they are, as the log says it ("ArcadeDB's").
    MediaQueue(QObject *context, QNetworkAccessManager *nam, int timeoutMs, const char *server,
               const QDateTime *pausedUntil, OnPause onPause)
        : m_context(context), m_nam(nam), m_timeoutMs(timeoutMs), m_server(server), m_pausedUntil(pausedUntil),
          m_onPause(std::move(onPause)) {}
    MediaQueue(const MediaQueue &) = delete;
    MediaQueue &operator=(const MediaQueue &) = delete;

    // url downloaded after those asked for before it; name: what the log
    // calls it.
    void add(const QUrl &url, qint64 limit, const QString &name, const Done &done) {
        m_queue.append({ url, limit, name, done, 0 });
        next();
    }

private:
    struct Item {
        QUrl url;
        qint64 limit = 0;
        QString name;
        Done done;
        int tried = 0;
    };

    void next() {
        if (m_reply || m_queue.isEmpty())
            return;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        if (m_pausedUntil->isValid() && *m_pausedUntil > now) {
            // The next file once the pause is over.
            QTimer::singleShot(static_cast<int>(std::max<qint64>(1, now.msecsTo(*m_pausedUntil))), m_context,
                               [this] { next(); });
            return;
        }
        const Item item = m_queue.takeFirst();
        m_reply = m_nam->get(requestFor(item.url, m_timeoutMs));
        QNetworkReply *reply = m_reply;
        limitReply(reply, item.limit);
        QObject::connect(reply, &QNetworkReply::finished, m_context, [this, reply, item] {
            reply->deleteLater();
            m_reply = nullptr;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (asksForAPause(status) && m_onPause(reply, status, item.name, item.tried)) {
                Item again = item;
                ++again.tried;
                m_queue.prepend(again);
                next();
                return;
            }
            QByteArray data;
            if (reply->error() == QNetworkReply::NoError)
                data = reply->readAll();
            if (data.size() > item.limit || !isPicture(data)) {
                qWarning("[Arcade] %s %s could not be downloaded (HTTP %d, error %d)", m_server, qPrintable(item.name),
                         status, int(reply->error()));
                data.clear();
            }
            item.done(data);
            next();
        });
    }

    QObject *m_context;
    QNetworkAccessManager *m_nam;
    int m_timeoutMs;
    const char *m_server;
    const QDateTime *m_pausedUntil;
    OnPause m_onPause;
    QList<Item> m_queue;
    QNetworkReply *m_reply = nullptr;   // the download under way
};

} // namespace arcadehttp
