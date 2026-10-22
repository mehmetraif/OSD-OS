#pragma once
#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QString>
#include <QUrl>
#include <algorithm>

// What the Arcade module's requests share: the lobby's, catver.ini's,
// RetroAchievements' and FinalBurn Neo's downloads.
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

// A badge's name as RetroAchievements gives it, and as it goes in a path.
inline bool isBadgeName(const QString &name) {
    static const QRegularExpression valid(QRegularExpression::anchoredPattern(QStringLiteral("[0-9A-Za-z]{1,32}")));
    return valid.match(name).hasMatch();
}

} // namespace arcadehttp
