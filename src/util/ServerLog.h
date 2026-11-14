#pragma once

#include <QNetworkReply>
#include <QString>

// What a media server's page asked for and didn't get, in the log. Plex's,
// Jellyfin's and Emby's detail, show and season pages show nothing when one
// of their loads fails, so the saved logs are what there is to say why: one
// line a failed load, from the backend that asked.
namespace serverlog {

// Why a request failed, in a few words: the status the server answered with
// (and its phrase for it, where it sent one), else the network's own word
// (Connection refused, Host nas.local not found). Never the address asked:
// Qt's text for a request turned down has it whole, the user's id in it.
inline QString why(const QNetworkReply *reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status > 0) {
        const QString phrase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString().trimmed();
        return QStringLiteral("HTTP %1").arg(status) + (phrase.isEmpty() ? QString() : QLatin1Char(' ') + phrase);
    }
    const QString said = reply->errorString();
    return said.contains(QLatin1String("://")) ? QStringLiteral("network error %1").arg(int(reply->error())) : said;
}

// The line: `[Jellyfin] the seasons of show 7a1c could not be loaded from
// nas.local: HTTP 404 Not Found`. tag is the backend's own (Plex, Jellyfin,
// Emby), what names what was asked for by the item's id, and the host is the
// server's: neither a token nor the user is in any of them. `reason` says
// why where the server did answer and the answer is of no use (not the item
// asked for); without one, why() does.
inline void loadFailed(const char *tag, const QString &what, const QNetworkReply *reply,
                       const QString &reason = QString()) {
    qWarning("[%s] %s could not be loaded from %s: %s", tag, qPrintable(what), qPrintable(reply->url().host()),
             qPrintable(reason.isEmpty() ? why(reply) : reason));
}

} // namespace serverlog
