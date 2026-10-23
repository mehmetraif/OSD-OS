#include "ArcadeDb.h"
#include "Http.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
#include <utility>

namespace {

constexpr int kTimeoutMs = 20000;
// An answer about kMostAsked sets is some hundreds of kilobytes; a picture
// is tens.
constexpr qint64 kMaxAnswer = 4 * 1024 * 1024;
constexpr qint64 kMaxPicture = 4 * 1024 * 1024;
// After too many requests, without a Retry-After.
constexpr int kHoldSeconds = 60;

bool isPicture(const QByteArray &data) {
    return data.startsWith("\x89PNG\r\n\x1a\n") || data.startsWith("\xff\xd8\xff");
}

} // namespace

const QUrl ArcadeDb::kService(QStringLiteral("https://adb.arcadeitalia.net/service_scraper.php"));

ArcadeDb::ArcadeDb(const QUrl &service, QObject *parent)
    : QObject(parent), m_service(service), m_nam(new QNetworkAccessManager(this))
{
}

void ArcadeDb::lookUp(const QStringList &sets) {
    // No service to ask (OSDOS_ARCADEDB_URL empty): every look fails, as one
    // the service couldn't answer.
    if (!m_service.isValid() || m_service.host().isEmpty()) {
        QTimer::singleShot(0, this, [this, sets] { emit failed(sets, 0); });
        return;
    }
    for (qsizetype first = 0; first < sets.size(); first += kMostAsked)
        m_queue.append(sets.mid(first, kMostAsked));
    next();
}

void ArcadeDb::next() {
    if (m_reply || m_queue.isEmpty())
        return;
    if (heldOff()) {
        // Asked again once the hold is over.
        QTimer::singleShot(static_cast<int>(std::max<qint64>(1, QDateTime::currentDateTimeUtc().msecsTo(m_heldUntil))),
                           this, &ArcadeDb::next);
        return;
    }
    const QStringList sets = m_queue.takeFirst();
    QUrl url = m_service;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ajax"), QStringLiteral("query_mame"));
    query.addQueryItem(QStringLiteral("game_name"), sets.join(QLatin1Char(';')));
    query.addQueryItem(QStringLiteral("use_parent"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("lang"), QStringLiteral("en"));
    url.setQuery(query);
    m_reply = m_nam->get(arcadehttp::requestFor(url, kTimeoutMs));
    QNetworkReply *reply = m_reply;
    arcadehttp::limitReply(reply, kMaxAnswer);
    connect(reply, &QNetworkReply::finished, this, [this, reply, sets] {
        reply->deleteLater();
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Its size before it is read (read, nothing is left to measure).
        const bool tooBig = arcadehttp::tooBig(reply, kMaxAnswer);
        const QByteArray json = reply->error() == QNetworkReply::NoError && !tooBig ? reply->readAll() : QByteArray();
        // The service's answer is a list of games; anything else (an error of
        // its own, a page) is no answer about the sets.
        const QJsonValue result = QJsonDocument::fromJson(json).object().value(QLatin1String("result"));
        if (reply->error() != QNetworkReply::NoError || tooBig || !result.isArray()) {
            // Too many requests: held off for as long as it says.
            int hold = 0;
            if (arcadehttp::asksForAPause(status)) {
                hold = arcadehttp::pauseAsked(reply, kHoldSeconds);
                m_heldUntil = QDateTime::currentDateTimeUtc().addSecs(hold);
            }
            qWarning("[Arcade] ArcadeDB could not be asked about %lld games (HTTP %d, error %d)",
                     static_cast<long long>(sets.size()), status, int(reply->error()));
            emit failed(sets, hold);
            next();
            return;
        }
        // What it has of each set asked about, by the set's name; a set it
        // doesn't know is answered without pictures.
        QHash<QString, QJsonObject> games;
        for (const QJsonValue &value : result.toArray()) {
            const QJsonObject game = value.toObject();
            games.insert(game.value(QLatin1String("game_name")).toString().toLower(), game);
        }
        for (const QString &set : sets) {
            const QJsonObject game = games.value(set.toLower());
            // Pictures elsewhere than the service's own server aren't
            // downloaded; a set with those alone isn't one without pictures
            // (which would be left for two months) but one that couldn't be
            // asked about, to be asked about again.
            bool elsewhere = false;
            const auto address = [this, &game, &elsewhere](const char *key) {
                const QString given = game.value(QLatin1String(key)).toString();
                if (given.isEmpty())
                    return QString();
                const QUrl url(given);
                if (isOurs(url))
                    return url.toString();
                elsewhere = true;
                return QString();
            };
            const QString ingame = address("url_image_ingame");
            const QString title = address("url_image_title");
            if (elsewhere && ingame.isEmpty() && title.isEmpty()) {
                qWarning("[Arcade] ArcadeDB's pictures of %s are on another server", qPrintable(set));
                emit failed({ set }, 0);
            } else {
                emit found(set, ingame, title);
            }
        }
        next();
    });
}

bool ArcadeDb::isOurs(const QUrl &url) const {
    return url.isValid() && !url.host().isEmpty() && url.host() == m_service.host()
           && url.scheme() == m_service.scheme() && url.port() == m_service.port();
}

void ArcadeDb::media(const QUrl &url, const std::function<void(const QByteArray &data)> &done) {
    if (!isOurs(url)) {
        QTimer::singleShot(0, this, [done] { done(QByteArray()); });
        return;
    }
    m_mediaQueue.append({ url, done });
    nextMedia();
}

void ArcadeDb::nextMedia() {
    if (m_mediaReply || m_mediaQueue.isEmpty())
        return;
    if (heldOff()) {
        QTimer::singleShot(static_cast<int>(std::max<qint64>(1, QDateTime::currentDateTimeUtc().msecsTo(m_heldUntil))),
                           this, &ArcadeDb::nextMedia);
        return;
    }
    const Media next = m_mediaQueue.takeFirst();
    m_mediaReply = m_nam->get(arcadehttp::requestFor(next.url, kTimeoutMs));
    QNetworkReply *reply = m_mediaReply;
    arcadehttp::limitReply(reply, kMaxPicture);
    connect(reply, &QNetworkReply::finished, this, [this, reply, next] {
        reply->deleteLater();
        m_mediaReply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray data;
        if (reply->error() == QNetworkReply::NoError)
            data = reply->readAll();
        if (data.size() > kMaxPicture || !isPicture(data)) {
            if (arcadehttp::asksForAPause(status))
                m_heldUntil = QDateTime::currentDateTimeUtc().addSecs(arcadehttp::pauseAsked(reply, kHoldSeconds));
            qWarning("[Arcade] ArcadeDB's %s could not be downloaded (HTTP %d, error %d)",
                     qPrintable(next.url.path() + QLatin1Char('?') + next.url.query()), status, int(reply->error()));
            data.clear();
        }
        next.done(data);
        nextMedia();
    });
}
