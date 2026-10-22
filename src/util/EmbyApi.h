#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

// The Emby API's URLs, which Jellyfin shares, for what the Playlists module
// asks of either server (MediaServer): one place for both backends.
namespace embyapi {

// A folder's children: parentId "" for this user's libraries, "resume" and
// "nextup" for what they are watching and the episodes next in their shows,
// else a library's, show's or season's. Seasons and episodes come by their
// numbers, anything else by name; no images or user data, which the lists
// don't show.
inline QUrl browseUrl(const QString &serverUrl, const QString &userId, const QString &parentId) {
    QUrl url;
    QUrlQuery query;
    if (parentId.isEmpty()) {
        url = QUrl(serverUrl + QStringLiteral("/Users/") + userId + QStringLiteral("/Views"));
    } else if (parentId == QLatin1String("resume")) {
        url = QUrl(serverUrl + QStringLiteral("/Users/") + userId + QStringLiteral("/Items/Resume"));
        query.addQueryItem(QStringLiteral("Limit"), QStringLiteral("50"));
        query.addQueryItem(QStringLiteral("MediaTypes"), QStringLiteral("Video"));
    } else if (parentId == QLatin1String("nextup")) {
        url = QUrl(serverUrl + QStringLiteral("/Shows/NextUp"));
        query.addQueryItem(QStringLiteral("UserId"), userId);
        query.addQueryItem(QStringLiteral("Limit"), QStringLiteral("50"));
    } else {
        url = QUrl(serverUrl + QStringLiteral("/Users/") + userId + QStringLiteral("/Items"));
        query.addQueryItem(QStringLiteral("ParentId"), parentId);
        query.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("ParentIndexNumber,IndexNumber,SortName"));
        query.addQueryItem(QStringLiteral("SortOrder"), QStringLiteral("Ascending"));
    }
    query.addQueryItem(QStringLiteral("EnableImages"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("false"));
    url.setQuery(query);
    return url;
}

// The item's original file, as the server lets this user download it (401 or
// 403 otherwise).
inline QUrl downloadUrl(const QString &serverUrl, const QString &itemId) {
    return QUrl(serverUrl + QStringLiteral("/Items/") + itemId + QStringLiteral("/Download"));
}

// A URL mpv streams the item from, with the token in its query rather than
// a header, so that in a playlist mixing sources the token goes to this
// server and nowhere else. Jellyfin reads the token as ApiKey as well as
// Emby's api_key.
inline QString streamUrl(const QString &serverUrl, const QString &itemId, const QString &token,
                         bool camelCaseKeyToo) {
    QUrl url(serverUrl + QStringLiteral("/Videos/") + itemId + QStringLiteral("/stream"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("static"), QStringLiteral("true"));
    if (camelCaseKeyToo)
        query.addQueryItem(QStringLiteral("ApiKey"), token);
    query.addQueryItem(QStringLiteral("api_key"), token);
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

// Whether a server's list (Items[]) has the item.
inline bool hasItem(const QJsonArray &items, const QString &itemId) {
    for (const auto &v : items) {
        if (v.toObject()[QLatin1String("Id")].toString() == itemId)
            return true;
    }
    return false;
}

// Of a series' episodes, in any order, the one after season/index: the
// smallest season above it, or the smallest index above it in the same
// season. Empty for none.
inline QJsonObject nextEpisode(const QJsonArray &episodes, int season, int index) {
    QJsonObject next;
    int nextSeason = 0;
    int nextIndex = 0;
    for (const auto &v : episodes) {
        const QJsonObject e = v.toObject();
        const int s = e[QLatin1String("ParentIndexNumber")].toInt();
        const int i = e[QLatin1String("IndexNumber")].toInt();
        if (s < season || (s == season && i <= index))
            continue;
        if (next.isEmpty() || s < nextSeason || (s == nextSeason && i < nextIndex)) {
            next = e;
            nextSeason = s;
            nextIndex = i;
        }
    }
    return next;
}

} // namespace embyapi
