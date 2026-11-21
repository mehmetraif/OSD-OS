#include "EmbyApiBackend.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>

#include "../AppCore.h"
#include "AtomicFile.h"
#include "EmbyApi.h"
#include "ServerLog.h"
#include "SslErrors.h"
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>
#include <QCoreApplication>
#include <QUuid>
#include <QSslError>
#include <QSysInfo>
#include <QSet>
#include <QDateTime>
#include <QRegularExpression>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

// Library CollectionTypes the module knows how to browse + play. Anything else
// (music, books, photos, mixed/empty, etc.) is hidden from both the browse list
// and the settings multiselect. Mirrors the Plex module's kSupportedLibraryTypes.
static const QSet<QString> kSupportedCollectionTypes = {
    QStringLiteral("movies"), QStringLiteral("tvshows"), QStringLiteral("homevideos"),
    QStringLiteral("boxsets")
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

EmbyApiBackend::EmbyApiBackend(const QString &moduleId, const char *tag, const QString &authFile,
                               const QString &dataRoot, AppCore *appCore, QObject *parent)
    : QObject(parent)
    , m_moduleId(moduleId)
    , m_tag(tag)
    , m_authFile(authFile)
    , m_dataRoot(dataRoot)
    , m_appCore(appCore)
    , m_nam(new QNetworkAccessManager(this))
{
    // A reply the server stops sending (a sleeping NAS, a proxy holding the
    // socket, Wi-Fi gone) fails after this long without a byte, instead of
    // leaving its view on LOADING..., as Plex's does.
    m_nam->setTransferTimeout(30000);
    loadAuthState();
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    // Seed the change guard from the restored state so a token that is still
    // valid at startup is not reported as a transition (see notifyAuthState).
    m_lastAuthState = get_auth_state();
}

// authStateChanged drives navigation in Root.qml, so emitting it when nothing
// actually changed reloads the current view on top of itself. check_auth()
// re-validating a good token is the common case: Root has already shown
// Libraries by then, and an unconditional emit reloaded it, or threw the user
// back to it from a library opened meanwhile. Only signal real transitions.
void EmbyApiBackend::notifyAuthState() {
    const QString state = get_auth_state();
    if (state == m_lastAuthState)
        return;
    m_lastAuthState = state;
    emit authStateChanged();
}

// ---------------------------------------------------------------------------
// Auth state persistence
// ---------------------------------------------------------------------------

QString EmbyApiBackend::normalizeServerUrl(const QString &url) {
    QString u = url.trimmed();
    while (u.endsWith('/'))
        u.chop(1);
    return u;
}

void EmbyApiBackend::loadAuthState() {
    QFile f(m_dataRoot + "/" + m_authFile);
    if (!f.open(QIODevice::ReadOnly))
        return;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    QJsonObject auth = doc.object();
    m_serverUrl  = normalizeServerUrl(auth["serverUrl"].toString());
    m_accessToken = auth["accessToken"].toString();
    m_userId     = auth["userId"].toString();
    m_userName   = auth["userName"].toString();
    m_serverName = auth["serverName"].toString();
    m_deviceId   = auth["deviceId"].toString();
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
}

void EmbyApiBackend::saveAuthState() {
    QJsonObject auth;
    auth["serverUrl"]   = m_serverUrl;
    auth["accessToken"] = m_accessToken;
    auth["userId"]      = m_userId;
    auth["userName"]    = m_userName;
    auth["serverName"]  = m_serverName;
    auth["deviceId"]    = m_deviceId;

    writeFileAtomically(m_dataRoot + "/" + m_authFile, QJsonDocument(auth).toJson(QJsonDocument::Indented),
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void EmbyApiBackend::clearAuthState() {
    m_serverUrl.clear();
    m_accessToken.clear();
    m_userId.clear();
    m_userName.clear();
    m_serverName.clear();
    m_currentPlaySessionId.clear();
    // The audio and subtitle languages chosen last were this user's: the
    // next one signed in gets the ones the server keeps for them
    // (load_server_preferences()), which these would stand in front of.
    m_lastAudioLang.clear();
    m_lastSubLang.clear();
    m_lastAudioLangIdx = -1;
    m_lastSubLangIdx = -1;
    // Sign out will wipe the auth file as the device is removed from access
    // on the server end as well. This will generate a fresh deviceId so any
    // in-session re-login / QuickConnect creates one that will be synced with the
    // new device we give access to on the server and it will persist until either
    // the user signs out or manually de-auths the device from the server end.
    // saveAuthState() will recreate the file on the next successful login.
    m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QFile::remove(m_dataRoot + "/" + m_authFile);
}

// ---------------------------------------------------------------------------
// Config helpers
// ---------------------------------------------------------------------------

QVariant EmbyApiBackend::setting(const QString &key) const {
    if (m_appCore)
        return m_appCore->get_setting(m_moduleId, key);
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object()["modules"].toObject()[m_moduleId].toObject()[key].toVariant();
    }
    return {};
}

QString EmbyApiBackend::videoQuality() const {
    const QVariant quality = setting(QStringLiteral("video_quality"));
    return quality.typeId() == QMetaType::QString ? quality.toString() : QStringLiteral("auto");
}

int EmbyApiBackend::qualityBitrate(const QString &quality) const {
    if (quality == QLatin1String("auto"))  return 0; // direct play — no cap
    if (quality == QLatin1String("1080p")) return 10000000;
    if (quality == QLatin1String("720p"))  return 6000000;
    if (quality == QLatin1String("576p"))  return 4500000;
    return 4000000; // 480p default
}

int EmbyApiBackend::qualityMaxHeight(const QString &quality) {
    if (quality == QLatin1String("auto"))  return 0; // direct play — no cap
    if (quality == QLatin1String("1080p")) return 1080;
    if (quality == QLatin1String("720p"))  return 720;
    if (quality == QLatin1String("576p"))  return 576;
    return 480;
}

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

QString EmbyApiBackend::authorization(const QString &token) const {
    QString auth = QStringLiteral("MediaBrowser Client=\"OSD/OS\", Device=\"%1\", DeviceId=\"%2\", Version=\"%3\"")
                       .arg(QSysInfo::machineHostName(), m_deviceId, QCoreApplication::applicationVersion());
    if (!token.isEmpty())
        auth += QStringLiteral(", Token=\"%1\"").arg(token);
    return auth;
}

QNetworkRequest EmbyApiBackend::downloadRequest(const QString &itemId) const {
    QNetworkRequest req = serverRequest(embyapi::downloadUrl(m_serverUrl, itemId));
    req.setRawHeader("Accept", "*/*");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

void EmbyApiBackend::browse(const QString &parentId, QObject *context,
                            std::function<void(bool, const QVariantList &)> done) {
    auto *reply = serverGet(embyapi::browseUrl(m_serverUrl, m_userId, parentId));
    const bool libraries = parentId.isEmpty();
    connect(reply, &QNetworkReply::finished, context, [this, reply, libraries, done]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(false, {});
            return;
        }
        QVariantList items;
        const QJsonArray array = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        for (const QJsonValue &v : array) {
            const QJsonObject item = v.toObject();
            // The libraries with videos in them; in one, its folders and videos.
            const bool wanted = libraries
                ? kSupportedCollectionTypes.contains(item["CollectionType"].toString())
                : item["IsFolder"].toBool() || item["MediaType"].toString() == QLatin1String("Video");
            if (wanted)
                items.append(formatItem(item));
        }
        done(true, items);
    });
}

QNetworkRequest EmbyApiBackend::serverRequest(const QUrl &url) const {
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    authorize(req, m_accessToken);
    return req;
}

QNetworkReply *EmbyApiBackend::serverGet(const QUrl &url) {
    auto *reply = m_nam->get(serverRequest(url));
    ignoreSslErrors(reply);
    return reply;
}

QNetworkReply *EmbyApiBackend::serverPost(const QUrl &url, const QByteArray &body) {
    QNetworkRequest req = serverRequest(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = m_nam->post(req, body);
    ignoreSslErrors(reply);
    return reply;
}

void EmbyApiBackend::ignoreSslErrors(QNetworkReply *reply) const {
    // The host read now, as the request is issued: the sslErrors callback fires
    // later, and logout() clears m_serverUrl the moment it has sent its revoke
    // request (Emby's device deauth first), so reading it then would fail the
    // host match and refuse to relax a self-signed LAN cert.
    ignoreSslErrors(reply, QUrl(m_serverUrl).host());
}

void EmbyApiBackend::ignoreSslErrors(QNetworkReply *reply, const QString &serverHost) {
    connect(reply, &QNetworkReply::sslErrors, reply, [reply, serverHost](const QList<QSslError> &errors) {
        // Only relax for the configured server — typical of self-signed LAN certs
        if (reply->url().host() != serverHost)
            return;
        QList<QSslError> allowed = expectedLanSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------

bool EmbyApiBackend::has_auth() const {
    return !m_accessToken.isEmpty() && !m_userId.isEmpty() && !m_serverUrl.isEmpty();
}

QString EmbyApiBackend::get_server_name() {
    return m_serverName;
}

QString EmbyApiBackend::get_user_name() {
    return m_userName;
}

QString EmbyApiBackend::get_auth_state() {
    return has_auth() ? QStringLiteral("authed") : QStringLiteral("none");
}

void EmbyApiBackend::check_auth() {
    if (!has_auth()) {
        notifyAuthState();
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId);
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401) {
            qWarning("[%sBackend] Token rejected — signing out", m_tag);
            clearAuthState();
            notifyAuthState();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("AUTH CHECK FAILED: " + serverlog::why(reply));
            return;
        }
        notifyAuthState();
        tokenChecked();
    });
}

void EmbyApiBackend::logout() {
    // Revoke the access token server-side so it can't be reused
    if (has_auth())
        revokeToken();
    clearAuthState();
    notifyAuthState();
}

void EmbyApiBackend::revokeToken() {
    QUrl url(m_serverUrl + "/Sessions/Logout");
    auto *reply = m_nam->post(serverRequest(url), QByteArray());
    // A self-signed LAN server takes the revoke too: the host is read here,
    // before clearAuthState() forgets it.
    ignoreSslErrors(reply);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void EmbyApiBackend::fetchServerName() {
    // Through serverGet: a self-signed LAN server's certificate is taken, as
    // for every other request to it.
    auto *reply = serverGet(QUrl(m_serverUrl + "/System/Info/Public"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject infoData = QJsonDocument::fromJson(reply->readAll()).object();
            QString name = infoData["ServerName"].toString();
            if (!name.isEmpty()) {
                m_serverName = name;
                saveAuthState();
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Item formatting
// ---------------------------------------------------------------------------

QVariantMap EmbyApiBackend::formatItem(const QJsonObject &item, const QString &serverUrl,
                                       const QString &urlToken) {
    QJsonObject userData = item["UserData"].toObject();
    QJsonObject imageTags = item["ImageTags"].toObject();
    QJsonArray mediaSources = item["MediaSources"].toArray();
    QJsonObject mediaSource = mediaSources.isEmpty() ? QJsonObject() : mediaSources[0].toObject();
    QJsonArray streams = mediaSource["MediaStreams"].toArray();

    QVariantList audioStreams;
    QVariantList subtitleStreams;
    for (const QJsonValue &v : streams) {
        QJsonObject s = v.toObject();
        QString type = s["Type"].toString();
        if (type == QLatin1String("Audio")) {
            QVariantMap as;
            as["id"]          = QString::number(s["Index"].toInt());
            as["language"]    = s["Language"].toString();
            as["codec"]       = s["Codec"].toString();
            as["channels"]    = s["ChannelLayout"].toString().isEmpty()
                                   ? s["Channels"].toVariant()
                                   : QVariant(s["ChannelLayout"].toString());
            as["selected"]    = s["IsDefault"].toBool();
            as["displayTitle"]= s["DisplayTitle"].toString();
            as["title"]       = s["Title"].toString();
            audioStreams.append(as);
        } else if (type == QLatin1String("Subtitle")) {
            const int idx     = s["Index"].toInt();
            const QString codec = s["Codec"].toString().toLower();
            const bool isText = s["IsTextSubtitleStream"].toBool();
            QVariantMap ss;
            ss["id"]          = QString::number(idx);
            ss["language"]    = s["Language"].toString();
            ss["codec"]       = codec;
            ss["selected"]    = s["IsDefault"].toBool();
            ss["forced"]      = s["IsForced"].toBool();
            // In a file beside the video, not in the file itself: not in a
            // Direct Play stream for mpv's --sid to count (MediaServerPlayer.qml).
            ss["external"]    = s["IsExternal"].toBool();
            ss["displayTitle"]= s["DisplayTitle"].toString();
            ss["title"]       = s["Title"].toString();
            // Image subs (PGS/VOBSUB) have no text sidecar — mpv renders them
            // from the embedded (direct-played) stream via --sid.
            ss["imageSubtitle"] = !isText;
            // Text subs are fetched as a sidecar file and handed to mpv as a
            // --sub-file, so direct play never has to transcode to show them.
            // (Mirrors PlexBackend's per-stream subUrl.)
            QString subUrl;
            if (isText) {
                const QString ext = (codec == "ass" || codec == "ssa") ? "ass"
                                  : (codec == "subrip" || codec == "srt") ? "srt"
                                  : "vtt";
                subUrl = serverUrl + "/Videos/" + item["Id"].toString() + "/"
                       + mediaSource["Id"].toString() + "/Subtitles/"
                       + QString::number(idx) + "/Stream." + ext;
                // mpv fetches sidecars itself and only carries the Jellyfin-style
                // Authorization header; Emby authenticates the sidecar via the
                // api_key query param, so the token goes in the URL there
                // (urlToken).
                if (!urlToken.isEmpty())
                    subUrl += "?api_key=" + urlToken;
            }
            ss["subUrl"] = subUrl;
            subtitleStreams.append(ss);
        }
    }

    QVariantList genres;
    for (const QJsonValue &v : item["Genres"].toArray())
        genres.append(v.toVariant());

    QVariantMap map;
    map["itemId"]          = item["Id"].toString();
    map["seriesId"]        = item["SeriesId"].toString();
    map["title"]           = item["Name"].toString();
    // Server-side sort key (only present when the caller asked for the SortName
    // field). The letter-nav panel buckets on this so its letters agree with the
    // sortBy=SortName ordering the browse queries use.
    map["titleSort"]       = item["SortName"].toString().toUpper();
    map["type"]            = item["Type"].toString().toLower();
    map["overview"]        = item["Overview"].toString();
    QString releaseDate = item["ReleaseDate"].toString();
    QString premiereDate = item["PremiereDate"].toString();
    map["releaseDate"]    = releaseDate.isEmpty() ? premiereDate : releaseDate;
    map["year"]            = item["ProductionYear"].toVariant();
    map["genres"]          = genres;
    map["duration"]        = item["RunTimeTicks"].toDouble() / 10000.0;
    map["viewOffset"]      = userData["PlaybackPositionTicks"].toDouble() / 10000.0;
    map["played"]          = userData["Played"].toBool();
    map["isFolder"]        = item["IsFolder"].toBool();
    map["leafCount"]       = item["ChildCount"].toInt();
    map["index"]           = item["IndexNumber"].toInt();
    map["parentIndex"]     = item["ParentIndexNumber"].toInt();
    map["grandparentTitle"]= item["SeriesName"].toString().isEmpty()
                                ? item["Album"].toString()
                                : item["SeriesName"].toString();
    map["imageTag"]        = imageTags["Primary"].toString();
    map["mediaSourceId"]   = mediaSource["Id"].toString();
    map["audioStreams"]    = audioStreams;
    map["subtitleStreams"]= subtitleStreams;
    return map;
}


// ---------------------------------------------------------------------------
// Browse
// ---------------------------------------------------------------------------

void EmbyApiBackend::load_libraries() {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Views");
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD LIBRARIES FAILED: " + serverlog::why(reply));
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        // Honour the user's library filter (Settings → Libraries). Empty map ==
        // never configured, so show everything; otherwise hide explicitly-disabled ones.
        const QVariantMap libEnabled = setting(QStringLiteral("libraries")).toMap();
        QVariantList libraries;
        for (const QJsonValue &v : items) {
            QJsonObject item = v.toObject();
            if (!kSupportedCollectionTypes.contains(item["CollectionType"].toString()))
                continue;
            QString libId = item["Id"].toString();
            const QVariant enabled = libEnabled.value(libId);
            if (enabled.typeId() == QMetaType::Bool && !enabled.toBool())
                continue;
            libraries.append(QVariantMap{
                {"key",            libId},
                {"itemId",         libId},
                {"title",          item["Name"].toString().toUpper()},
                {"collectionType", item["CollectionType"].toString()},
            });
        }

        // Prepend the Continue Watching / Up Next shelves, but only when they
        // actually have content. Probe each (limit=1) before emitting the list,
        // Resume asked for videos where the server needs it to be
        // (resumeNeedsMediaTypes()).
        QUrl resumeUrl(m_serverUrl + "/Users/" + m_userId + "/Items/Resume");
        { QUrlQuery rq; if (resumeNeedsMediaTypes()) rq.addQueryItem("mediaTypes", "Video");
                        rq.addQueryItem("limit", "1"); resumeUrl.setQuery(rq); }
        QUrl nextUrl(m_serverUrl + "/Shows/NextUp");
        { QUrlQuery nq; nq.addQueryItem("userId", m_userId); nq.addQueryItem("limit", "1"); nextUrl.setQuery(nq); }

        probeHasItems(resumeUrl, [this, libraries, nextUrl](bool hasResume) {
            probeHasItems(nextUrl, [this, libraries, hasResume](bool hasUpNext) {
                QVariantList combined = libraries;
                if (hasUpNext)
                    combined.prepend(QVariantMap{{"key", "up_next"}, {"title", "NEXT UP"}});
                if (hasResume)
                    combined.prepend(QVariantMap{{"key", "continue_watching"}, {"title", "CONTINUE WATCHING"}});
                emit librariesLoaded(combined);
            });
        });
    });
}

void EmbyApiBackend::probeHasItems(const QUrl &url, std::function<void(bool)> cb) {
    // Which shelf, for the log: the path's last two parts (Items/Resume,
    // Shows/NextUp), never the user's id before them.
    const QString probePath = url.path().section(QLatin1Char('/'), -2);
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb, probePath]() {
        reply->deleteLater();
        bool has = false;
        if (reply->error() == QNetworkReply::NoError) {
            has = !QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray().isEmpty();
        } else {
            // A failed probe hides the shelf exactly like an empty one, so warn —
            // otherwise a broken request is indistinguishable from "nothing to
            // resume" and stays invisible (as Emby's missing mediaTypes=Video did).
            qWarning("[%s] shelf probe failed (%s): %s", m_tag,
                     qPrintable(probePath), qPrintable(serverlog::why(reply)));
        }
        cb(has);
    });
}

void EmbyApiBackend::load_items(const QString &parentId, const QString &includeTypes, const QString &sortBy) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items");
    QUrlQuery q;
    q.addQueryItem("parentId", parentId);
    q.addQueryItem("recursive", "true");
    // Browse rows only need title/year/overview/played state. Skip the heavy
    // per-item MediaSources/MediaStreams here (now that the list is unbounded) —
    // Item.qml re-fetches full detail via load_item_detail when an item is opened.
    q.addQueryItem("fields", "Overview,Genres,UserData,SortName");
    if (!includeTypes.isEmpty())
        q.addQueryItem("includeItemTypes", includeTypes);
    if (!sortBy.isEmpty()) {
        q.addQueryItem("sortBy", sortBy);
        q.addQueryItem("sortOrder", "Ascending");
    }
    // No limit — return the full library so the list is complete A–Z (matches the
    // Plex module's unbounded /library/sections/{id}/all). Both servers return
    // all matching items when limit is omitted.
    url.setQuery(q);

    const QString serverUrl = m_serverUrl, token = urlToken();
    loadList(&EmbyApiBackend::itemsLoaded, "items " + parentId + ' ' + includeTypes + ' ' + sortBy,
             url, "LOAD ITEMS FAILED: ", [serverUrl, token](const QByteArray &data) {
        QVariantList result;
        for (const QJsonValue &v : QJsonDocument::fromJson(data).object()["Items"].toArray())
            result.append(formatItem(v.toObject(), serverUrl, token));
        return result;
    });
}

void EmbyApiBackend::loadList(void (EmbyApiBackend::*answer)(const QVariant &), const QString &keep,
                              const QUrl &url, const QString &failure,
                              std::function<QVariantList(const QByteArray &)> read) {
    // Long enough to go into a film's detail screen and back, or play a
    // trailer, short enough to show what was added on the server meanwhile.
    constexpr qint64 kListKeepMs = 10 * 60 * 1000;
    constexpr int kListsKept = 6;
    const int request = ++m_listRequest;
    // Kept per server and user: a list of another's isn't theirs.
    const QString kept = keep.isEmpty() ? QString()
                                        : m_serverUrl + QLatin1Char(' ') + m_userId + QLatin1Char(' ') + keep;
    const auto found = m_keptLists.constFind(kept);
    if (!kept.isEmpty() && found != m_keptLists.constEnd()
        && QDateTime::currentMSecsSinceEpoch() - found->at < kListKeepMs) {
        // Told as a reply is: once the view that asked has done asking.
        const QVariantList rows = found->rows;
        QTimer::singleShot(0, this, [this, request, answer, rows]() {
            if (request == m_listRequest)
                emit (this->*answer)(rows);
        });
        return;
    }
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request, answer, kept, failure, read]() {
        reply->deleteLater();
        if (request != m_listRequest)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(failure + serverlog::why(reply));
            return;
        }
        auto *watcher = new QFutureWatcher<QVariantList>(this);
        connect(watcher, &QFutureWatcher<QVariantList>::finished, this,
                [this, watcher, request, answer, kept]() {
            watcher->deleteLater();
            if (request != m_listRequest)
                return;
            const QVariantList rows = watcher->result();
            if (!kept.isEmpty()) {
                m_keptLists.insert(kept, { rows, QDateTime::currentMSecsSinceEpoch() });
                m_keptOrder.removeAll(kept);
                m_keptOrder.append(kept);
                while (m_keptOrder.size() > kListsKept)
                    m_keptLists.remove(m_keptOrder.takeFirst());
            }
            emit (this->*answer)(rows);
        });
        watcher->setFuture(QtConcurrent::run(read, reply->readAll()));
    });
}

void EmbyApiBackend::load_item_detail(const QString &itemId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items/" + itemId);
    QUrlQuery q;
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    url.setQuery(q);

    // The detail page shows nothing of an item that didn't come, so the log
    // says which and why (serverlog), a load that failed or an answer that
    // is something else (a proxy's sign-in page): the page drops that one.
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            serverlog::loadFailed(m_tag, "item " + itemId, reply);
            emit errorOccurred("LOAD ITEM DETAIL FAILED: " + serverlog::why(reply));
            return;
        }

        QJsonObject item = QJsonDocument::fromJson(reply->readAll()).object();
        if (item["Id"].toString().isEmpty())
            serverlog::loadFailed(m_tag, "item " + itemId, reply, QStringLiteral("the answer is not the item"));
        emit itemLoaded(formatItem(item));
    });
}

void EmbyApiBackend::load_boxset_children(const QString &parentId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    // Direct members only (recursive=false) and no item-type filter — a box set
    // can hold movies, series, episodes, and nested box sets, and we want them
    // all. Used both for the library-level box-set list (parentId = library) and
    // for an individual box set's contents (parentId = box-set id). recursive=false
    // keeps nested box sets out of the parent listing so nesting only surfaces by
    // drilling into a box set.
    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items");
    QUrlQuery q;
    q.addQueryItem("parentId", parentId);
    q.addQueryItem("recursive", "false");
    // SortName for the A–Z panel's letters (formatItem's titleSort).
    q.addQueryItem("fields", "Overview,Genres,UserData,ChildCount,ReleaseDate,PremiereDate,SortName");
    q.addQueryItem("sortBy", "SortName");
    q.addQueryItem("sortOrder", "Ascending");
    url.setQuery(q);

    const QString serverUrl = m_serverUrl, token = urlToken();
    loadList(&EmbyApiBackend::boxsetChildrenLoaded, QString(), url, "LOAD BOXSET FAILED: ",
             [serverUrl, token](const QByteArray &data) {
        QVariantList result;
        for (const QJsonValue &v : QJsonDocument::fromJson(data).object()["Items"].toArray())
            result.append(formatItem(v.toObject(), serverUrl, token));
        return result;
    });
}

void EmbyApiBackend::load_folder_children(const QString &parentId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    // homevideos browse: a homevideos library is a tree, so we list one level at
    // a time (recursive=false) with no item-type filter — a folder can hold both
    // sub-folders and videos. Same query shape as load_boxset_children; ChildCount
    // is requested so the QML can tell containers from leaves.
    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items");
    QUrlQuery q;
    q.addQueryItem("parentId", parentId);
    q.addQueryItem("recursive", "false");
    q.addQueryItem("fields", "Overview,Genres,UserData,ChildCount,SortName");
    q.addQueryItem("sortBy", "SortName");
    q.addQueryItem("sortOrder", "Ascending");
    url.setQuery(q);

    const QString serverUrl = m_serverUrl, token = urlToken();
    loadList(&EmbyApiBackend::folderChildrenLoaded, "folder " + parentId, url, "LOAD FOLDER FAILED: ",
             [serverUrl, token](const QByteArray &data) {
        QVariantList result;
        for (const QJsonValue &v : QJsonDocument::fromJson(data).object()["Items"].toArray()) {
            QVariantMap map = formatItem(v.toObject(), serverUrl, token);
            // Keep navigable folders and playable videos; drop photos / photo
            // albums / audio so nothing un-playable can be selected.
            const QString t = map["type"].toString();
            if (map["isFolder"].toBool()) {
                if (t != "photoalbum") result.append(map);
            } else if (t == "video" || t == "movie" || t == "episode") {
                result.append(map);
            }
        }
        return result;
    });
}

void EmbyApiBackend::load_seasons(const QString &seriesId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Shows/" + seriesId + "/Seasons");
    QUrlQuery q;
    q.addQueryItem("userId", m_userId);
    q.addQueryItem("fields", "Overview,MediaSources,MediaStreams,UserData");
    q.addQueryItem("enableUserData", "true");
    url.setQuery(q);

    // As the detail page's item: a show's page without its seasons says
    // nothing, the log does.
    const int request = ++m_seasonsRequest;
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, seriesId, request]() {
        reply->deleteLater();
        // Another show's seasons asked for since: this one was left.
        if (request != m_seasonsRequest) return;
        if (reply->error() != QNetworkReply::NoError) {
            serverlog::loadFailed(m_tag, "the seasons of show " + seriesId, reply);
            emit errorOccurred("LOAD SEASONS FAILED: " + serverlog::why(reply));
            return;
        }
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        if (!answer.contains(QLatin1String("Items")))
            serverlog::loadFailed(m_tag, "the seasons of show " + seriesId, reply,
                                  QStringLiteral("the answer is not a list"));
        QJsonArray items = answer["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit seasonsLoaded(result);
    });
}

void EmbyApiBackend::load_episodes(const QString &seriesId, const QString &seasonId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Shows/" + seriesId + "/Episodes");
    QUrlQuery q;
    q.addQueryItem("userId", m_userId);
    q.addQueryItem("seasonId", seasonId);
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    q.addQueryItem("enableUserData", "true");
    q.addQueryItem("limit", "500");
    if (!episodeOrder().isEmpty())
        q.addQueryItem("sortBy", episodeOrder());
    url.setQuery(q);

    // As the detail page's item: a season's page without its episodes says
    // nothing, the log does.
    const int request = ++m_episodesRequest;
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, seasonId, request]() {
        reply->deleteLater();
        // Another season's episodes asked for since: this one was left.
        if (request != m_episodesRequest) return;
        if (reply->error() != QNetworkReply::NoError) {
            serverlog::loadFailed(m_tag, "the episodes of season " + seasonId, reply);
            emit errorOccurred("LOAD EPISODES FAILED: " + serverlog::why(reply));
            return;
        }
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        if (!answer.contains(QLatin1String("Items")))
            serverlog::loadFailed(m_tag, "the episodes of season " + seasonId, reply,
                                  QStringLiteral("the answer is not a list"));
        QJsonArray items = answer["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit episodesLoaded(result);
    });
}

void EmbyApiBackend::load_continue_watching() {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items/Resume");
    QUrlQuery q;
    if (resumeNeedsMediaTypes())
        q.addQueryItem("mediaTypes", "Video");
    q.addQueryItem("limit", "20");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    url.setQuery(q);

    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD CONTINUE WATCHING FAILED: " + serverlog::why(reply));
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit continueWatchingLoaded(result);
    });
}

void EmbyApiBackend::load_up_next() {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Shows/NextUp");
    QUrlQuery q;
    q.addQueryItem("userId", m_userId);
    q.addQueryItem("limit", "20");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    q.addQueryItem("enableUserData", "true");
    url.setQuery(q);

    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD UP NEXT FAILED: " + serverlog::why(reply));
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit upNextLoaded(result);
    });
}

void EmbyApiBackend::load_series_next_up(const QString &seriesId) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    // Server computes the resume-or-next-unwatched episode for this series.
    QUrl url(m_serverUrl + "/Shows/NextUp");
    QUrlQuery q;
    q.addQueryItem("userId", m_userId);
    q.addQueryItem("seriesId", seriesId);
    q.addQueryItem("limit", "1");
    q.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    q.addQueryItem("enableUserData", "true");
    url.setQuery(q);

    const int request = ++m_nextUpRequest;
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, seriesId, request]() {
        reply->deleteLater();
        // Another show's next episode asked for since (its PLAY): this one
        // was left, and opens nothing from that show's page.
        if (request != m_nextUpRequest) return;
        if (reply->error() != QNetworkReply::NoError) {
            // The show's PLAY then does nothing, with nothing said: the log does.
            serverlog::loadFailed(m_tag, "the next episode of show " + seriesId, reply);
            emit errorOccurred("LOAD SERIES NEXT UP FAILED: " + serverlog::why(reply));
            return;
        }
        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        // Empty when the series has never been started — caller falls back to
        // playing the first season's first episode.
        emit seriesNextUpReady(items.isEmpty() ? QVariantMap{}
                                               : formatItem(items[0].toObject()));
    });
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void EmbyApiBackend::get_playback_url(const QString &itemId, const QString &mediaSourceId,
                                      int audioStreamIndex, int subtitleStreamIndex,
                                      bool forceTranscode) {
    requestPlayback(itemId, mediaSourceId, audioStreamIndex, subtitleStreamIndex, forceTranscode, false);
}

void EmbyApiBackend::requestPlayback(const QString &itemId, const QString &mediaSourceId,
                                     int audioStreamIndex, int subtitleStreamIndex,
                                     bool forceTranscode, bool sdrCap) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    // "auto" (Direct Play) lets the server serve the original file untouched
    // when the source is compatible; any other value forces a quality-capped
    // HLS transcode. forceTranscode overrides "auto" for a fallback retry after
    // a direct-play failure (see Player.qml onPlaybackEnded). The setting is
    // read once: the reply holds the transcode to the quality's height.
    const QString quality = videoQuality();
    const bool directPlay = !forceTranscode && quality == QLatin1String("auto");
    const int qualityHeight = qualityMaxHeight(quality);
    int maxBitrate = qualityBitrate(quality);
    int maxHeight  = qualityHeight;
    // Dolby Vision → SDR re-request: cap an otherwise-uncapped ("auto") transcode
    // to 1080p/20Mbps so the server can start it promptly and keep up in real time.
    if (sdrCap) {
        if (maxBitrate <= 0) maxBitrate = 20000000;
        if (maxHeight  <= 0) maxHeight  = 1080;
    }

    QUrl url(m_serverUrl + "/Items/" + itemId + "/PlaybackInfo");
    QJsonObject body;
    body["UserId"]                 = m_userId;
    body["MediaSourceId"]          = mediaSourceId;
    if (audioStreamIndex >= 0)
        body["AudioStreamIndex"]   = audioStreamIndex;
    // For direct play the device profile already advertises every subtitle
    // format (including PGS/VOBSUB) as handled client-side, so the server
    // won't force a transcode. Pass the user's actual selection (or omit if
    // off) so the static stream includes all tracks. Hardcoding -1 here
    // caused newer Jellyfin servers to strip sub tracks from the stream,
    // breaking both --sid for image subs and sidecar URLs (not verified
    // against Emby).
    if (subtitleStreamIndex >= 0)
        body["SubtitleStreamIndex"] = subtitleStreamIndex;
    if (maxBitrate > 0) body["MaxStreamingBitrate"] = maxBitrate;
    if (maxHeight  > 0) body["MaxHeight"]           = maxHeight;
    body["EnableDirectPlay"]       = directPlay;
    body["EnableDirectStream"]     = directPlay;
    body["DeviceProfile"]          = deviceProfile(directPlay, maxBitrate);

    const int request = ++m_playbackRequest;
    auto *reply = serverPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, request, itemId, mediaSourceId, audioStreamIndex,
                                                    subtitleStreamIndex, directPlay, forceTranscode, sdrCap,
                                                    qualityHeight]() {
        reply->deleteLater();
        // Not wanted any more: another stream asked for since, or given up.
        // Nothing of it is reported to the server, which would show it playing.
        if (request != m_playbackRequest)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("PLAYBACK INFO FAILED: " + serverlog::why(reply));
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonArray sources = data["MediaSources"].toArray();
        if (sources.isEmpty()) {
            emit errorOccurred("NO PLAYABLE SOURCE");
            return;
        }

        QJsonObject source = sources[0].toObject();
        // A server's own reason to transcode instead (Emby's Dolby Vision),
        // asked of a Direct Play and of a transcode the player forced (a
        // direct play that failed, a track switched in the middle of one),
        // never of the re-request that answers it.
        if ((directPlay || forceTranscode) && !sdrCap
            && transcodeInstead(source, itemId, mediaSourceId, audioStreamIndex, subtitleStreamIndex))
            return;

        // Direct play when requested AND the server confirms the source is
        // compatible. Serves the original file via /Videos/{id}/stream?static=true;
        // mpv handles embedded audio/subtitle tracks (Player.qml direct-play branch).
        // PlaySessionId from the PlaybackInfo response is reused for the stream
        // URL and every /Sessions report so the server correlates the dashboard
        // session with this stream — and tears the transcode down on Stopped.
        const QString playSessionId = data["PlaySessionId"].toString();

        if (directPlay && (source["SupportsDirectPlay"].toBool()
                           || source["SupportsDirectStream"].toBool())) {
            const QString directUrl = directStreamUrl(m_serverUrl, itemId, source["Id"].toString(mediaSourceId),
                                                      playSessionId, urlToken());
            m_currentPlaySessionId = playSessionId;
            m_currentPlayMethod    = QStringLiteral("DirectPlay");
            report_playback_start(itemId, mediaSourceId,
                                  audioStreamIndex >= 0 ? QString::number(audioStreamIndex) : QString(),
                                  subtitleStreamIndex >= 0 ? QString::number(subtitleStreamIndex) : QString());
            emit streamUrlReady(directUrl);
            return;
        }

        // Transcode path — used for the bitrate tiers, and as a graceful
        // fallback when the server reports the source can't be direct-played.
        // That fallback is asked for again as a transcode: the TranscodingUrl
        // here was built on the Direct Play profile, whose subtitles are
        // External or Embed, so the server would burn none in, where the
        // player takes a transcode's subtitle as burnt in (Jellyfin) or an
        // image one as burnt in (Emby): the chosen subtitle never showed.
        if (directPlay) {
            // TranscodeReasons is an array of strings on older servers, a
            // comma-joined flags string on 10.9+.
            const QJsonValue tr = source["TranscodeReasons"];
            QString reasons = tr.toString();
            if (tr.isArray()) {
                QStringList list;
                for (const QJsonValue &r : tr.toArray())
                    list << r.toString();
                reasons = list.join(", ");
            }
            qWarning("[%s] direct play denied (SupportsDirectPlay=%d SupportsDirectStream=%d): %s", m_tag,
                     source["SupportsDirectPlay"].toBool(),
                     source["SupportsDirectStream"].toBool(),
                     reasons.isEmpty() ? "no TranscodeReasons given" : qPrintable(reasons));
            requestPlayback(itemId, mediaSourceId, audioStreamIndex, subtitleStreamIndex, true, sdrCap);
            return;
        }
        QString transcodeUrl = source["TranscodingUrl"].toString();
        if (transcodeUrl.isEmpty()) {
            emit errorOccurred("NO TRANSCODE URL");
            return;
        }
        m_currentPlaySessionId = playSessionId;
        m_currentPlayMethod    = QStringLiteral("Transcode");

        const QString fullUrl = rewriteTranscodeUrl(m_serverUrl, transcodeUrl, playSessionId, subtitleStreamIndex,
                                                    qualityHeight, tokenInUrls(), m_accessToken);
        report_playback_start(itemId, mediaSourceId,
                              audioStreamIndex >= 0 ? QString::number(audioStreamIndex) : QString(),
                              subtitleStreamIndex >= 0 ? QString::number(subtitleStreamIndex) : QString());
        emit streamUrlReady(fullUrl);
    });
}

QJsonObject EmbyApiBackend::deviceProfile(bool directPlay, int maxBitrate) const {
    Q_UNUSED(maxBitrate)
    // Device profile — advertises the HLS transcode target, plus (for direct
    // play) the broad set of containers/codecs mpv can play natively.
    QJsonObject profile;
    QJsonArray transcodingProfiles;
    QJsonObject tp;
    tp["Container"]  = QStringLiteral("ts");
    tp["Type"]       = QStringLiteral("Video");
    tp["VideoCodec"]  = QStringLiteral("h264");
    tp["AudioCodec"]  = QStringLiteral("aac,mp3");
    tp["Protocol"]    = QStringLiteral("hls");
    transcodingProfiles.append(tp);
    profile["TranscodingProfiles"] = transcodingProfiles;
    profile["SubtitleProfiles"] = subtitleProfiles(directPlay);
    QJsonArray directPlayProfiles;
    if (directPlay) {
        // mpv plays virtually anything, so advertise a match-all profile —
        // omitted Container/VideoCodec/AudioCodec fields match every value in
        // Jellyfin's profile matcher, and in Emby's too (verified against Emby
        // 4.9.5.0: mkv/mp4/avi with h264/hevc/vc1/mpeg2 all direct-play through
        // this wildcard). A codec whitelist here silently forced
        // transcodes on exact-name misses (pcm_s16le vs pcm, webvtt vs vtt, …).
        // If mpv truly can't play a file, the transcode retry in Player.qml
        // onPlaybackEnded is the safety net. An empty array (transcode mode)
        // tells the server nothing can be direct-played.
        QJsonObject dp;
        dp["Type"] = QStringLiteral("Video");
        directPlayProfiles.append(dp);
    }
    profile["DirectPlayProfiles"] = directPlayProfiles;
    return profile;
}

bool EmbyApiBackend::transcodeInstead(const QJsonObject &, const QString &, const QString &, int, int) {
    return false;
}

QString EmbyApiBackend::directStreamUrl(const QString &serverUrl, const QString &itemId,
                                        const QString &mediaSourceId, const QString &playSessionId,
                                        const QString &urlToken) {
    QString directUrl = serverUrl + "/Videos/" + itemId + "/stream"
                      + "?static=true"
                      + "&mediaSourceId=" + mediaSourceId;
    if (!playSessionId.isEmpty())
        directUrl += "&PlaySessionId=" + playSessionId;
    // Emby authenticates the media request via api_key; mpv only carries
    // the Jellyfin-style Authorization header, so the token goes in the URL
    // there.
    if (!urlToken.isEmpty())
        directUrl += "&api_key=" + urlToken;
    return directUrl;
}

QString EmbyApiBackend::rewriteTranscodeUrl(const QString &serverUrl, const QString &transcodeUrl,
                                            const QString &playSessionId, int subtitleStreamIndex,
                                            int maxHeight, bool tokenInUrl, const QString &token) {
    // Build the full URL. Jellyfin's token is taken out of the server-generated
    // TranscodingUrl: the Authorization header (passed via --http-header-fields
    // to mpv) covers authentication. Emby authenticates the HLS request via
    // api_key, so its token is kept, and added if it's absent. When the user
    // selected OFF, also strip any SubtitleStreamIndex and SubtitleMethod the
    // server may have added from default metadata.
    QUrl parsedUrl(serverUrl + transcodeUrl);
    {
        QUrlQuery q(parsedUrl);
        bool hasApiKey = false;
        const auto items = q.queryItems();
        for (const auto &kv : items) {
            if (kv.first.compare(QLatin1String("api_key"), Qt::CaseInsensitive) == 0 ||
                kv.first.compare(QLatin1String("apikey"),  Qt::CaseInsensitive) == 0) {
                if (tokenInUrl)
                    hasApiKey = true;
                else
                    q.removeAllQueryItems(kv.first);
            }
        }
        if (tokenInUrl && !hasApiKey && !token.isEmpty())
            q.addQueryItem("api_key", token);
        if (subtitleStreamIndex < 0) {
            q.removeAllQueryItems("SubtitleStreamIndex");
            q.removeAllQueryItems("SubtitleMethod");
        }
        parsedUrl.setQuery(q);
    }
    QString fullUrl = parsedUrl.toString();

    // Pin the URL's PlaySessionId to the one we report with (they should
    // already match; this guarantees it even if the server differs).
    if (!playSessionId.isEmpty())
        fullUrl.replace(QRegularExpression("PlaySessionId=[^&]+"),
                        "PlaySessionId=" + playSessionId);

    // Enforce max height from quality setting — the server's TranscodingUrl may
    // include a VideoBitrate cap (from our PlaybackInfo POST) but omit MaxHeight,
    // resulting in a full-resolution transcode. Inject it here so 480p/720p etc.
    // actually constrain the output resolution.
    if (maxHeight > 0) {
        QRegularExpression heightRe("(MaxHeight|Height)=[^&]+",
                                    QRegularExpression::CaseInsensitiveOption);
        const QString replacement = "MaxHeight=" + QString::number(maxHeight);
        if (fullUrl.contains(heightRe))
            fullUrl.replace(heightRe, replacement);
        else
            fullUrl += "&" + replacement;
    }
    return fullUrl;
}

void EmbyApiBackend::cancel_playback_url() {
    ++m_playbackRequest;
}

void EmbyApiBackend::load_next_episode(const QString &currentItemId) {
    if (!has_auth()) {
        emit nextEpisodeReady(QVariantMap{});
        return;
    }

    // Step 1: fetch current episode to get seriesId + episode position
    QUrl detailUrl(m_serverUrl + "/Users/" + m_userId + "/Items/" + currentItemId);
    QUrlQuery detailQ;
    detailQ.addQueryItem("fields", "MediaSources");
    detailUrl.setQuery(detailQ);

    auto *detailReply = serverGet(detailUrl);
    connect(detailReply, &QNetworkReply::finished, this, [this, detailReply]() {
        detailReply->deleteLater();
        if (detailReply->error() != QNetworkReply::NoError) {
            emit nextEpisodeReady(QVariantMap{});
            return;
        }
        QJsonObject item = QJsonDocument::fromJson(detailReply->readAll()).object();
        QString seriesId      = item["SeriesId"].toString();
        int     currentIndex  = item["IndexNumber"].toInt();
        int     currentSeason = item["ParentIndexNumber"].toInt();

        if (seriesId.isEmpty() || item["Type"].toString() != QLatin1String("Episode")) {
            emit nextEpisodeReady(QVariantMap{});
            return;
        }

        // Step 2: the series' episodes from this one on
        fetchNextEpisode(seriesId, item["Id"].toString(), currentSeason, currentIndex, true);
    });
}

// The series' episodes in air order (episodeOrder(); Emby lists them in their
// order itself) from the current one (startItemId), a page of them: it holds
// the next whatever the series' length, where a list of the whole series was
// cut at 500. A server that doesn't skip to the item (its page starts at the
// first episode) is asked once more for the whole series.
void EmbyApiBackend::fetchNextEpisode(const QString &seriesId, const QString &itemId, int currentSeason,
                                      int currentIndex, bool fromItem) {
    QUrl epUrl(m_serverUrl + "/Shows/" + seriesId + "/Episodes");
    QUrlQuery epQ;
    epQ.addQueryItem("userId", m_userId);
    epQ.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    epQ.addQueryItem("enableUserData", "true");
    if (!episodeOrder().isEmpty())
        epQ.addQueryItem("sortBy", episodeOrder());
    if (fromItem)
        epQ.addQueryItem("startItemId", itemId);
    epQ.addQueryItem("limit", fromItem ? "50" : "500");
    epUrl.setQuery(epQ);

    auto *epReply = serverGet(epUrl);
    connect(epReply, &QNetworkReply::finished, this,
            [this, epReply, seriesId, itemId, currentSeason, currentIndex, fromItem]() {
        epReply->deleteLater();
        if (epReply->error() != QNetworkReply::NoError) {
            emit nextEpisodeReady(QVariantMap{});
            return;
        }
        const QJsonArray episodes = QJsonDocument::fromJson(epReply->readAll()).object()["Items"].toArray();
        if (fromItem && !embyapi::hasItem(episodes, itemId)) {
            fetchNextEpisode(seriesId, itemId, currentSeason, currentIndex, false);
            return;
        }
        const QJsonObject nextEp = embyapi::nextEpisode(episodes, currentSeason, currentIndex);
        emit nextEpisodeReady(nextEp.isEmpty() ? QVariantMap{} : formatItem(nextEp));
    });
}

void EmbyApiBackend::report_playback_start(const QString &itemId, const QString &mediaSourceId,
                                           const QString &audioStreamId, const QString &subtitleStreamId,
                                           qint64 startPositionTicks) {
    if (!has_auth()) return;

    // Called from get_playback_url() once PlaybackInfo resolves, so the session
    // id and play method are authoritative and shared with the stream URL and
    // the Progress/Stopped reports.
    QJsonObject body;
    body["ItemId"]            = itemId;
    body["MediaSourceId"]     = mediaSourceId;
    if (!m_currentPlaySessionId.isEmpty())
        body["PlaySessionId"] = m_currentPlaySessionId;
    body["PlayMethod"]        = m_currentPlayMethod.isEmpty() ? QStringLiteral("Transcode")
                                                             : m_currentPlayMethod;
    body["IsPaused"]          = false;
    body["CanSeek"]           = true;
    if (startPositionTicks > 0)
        body["StartPositionTicks"] = startPositionTicks;
    if (!audioStreamId.isEmpty())
        body["AudioStreamIndex"] = audioStreamId.toInt();
    if (!subtitleStreamId.isEmpty())
        body["SubtitleStreamIndex"] = subtitleStreamId.toInt();

    QUrl url(m_serverUrl + "/Sessions/Playing");
    auto *reply = serverPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            qDebug("[%s] Playback session started: %s", m_tag, qPrintable(m_currentPlaySessionId));
        else
            qWarning("[%s] Failed to start playback session: %s", m_tag,
                     qPrintable(serverlog::why(reply)));
    });
}

void EmbyApiBackend::update_playback_progress(const QString &itemId, const QString &mediaSourceId,
                                              qint64 positionTicks, bool isPaused) {
    if (!has_auth()) return;

    QJsonObject body;
    body["ItemId"]         = itemId;
    body["MediaSourceId"]  = mediaSourceId;
    body["PositionTicks"]  = positionTicks;
    body["IsPaused"]       = isPaused;
    body["PlayMethod"]     = m_currentPlayMethod.isEmpty() ? QStringLiteral("Transcode")
                                                           : m_currentPlayMethod;
    body["CanSeek"]        = true;
    if (!m_currentPlaySessionId.isEmpty())
        body["PlaySessionId"] = m_currentPlaySessionId;

    QUrl url(m_serverUrl + "/Sessions/Playing/Progress");
    auto *reply = serverPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError)
            qWarning("[%s] progress update failed: %s", m_tag, qPrintable(serverlog::why(reply)));
        reply->deleteLater();
    });
}

void EmbyApiBackend::report_playback_stopped(const QString &itemId, const QString &mediaSourceId,
                                             qint64 positionTicks, bool failed) {
    if (!has_auth()) return;

    QJsonObject body;
    body["ItemId"]        = itemId;
    body["MediaSourceId"] = mediaSourceId;
    body["PositionTicks"] = positionTicks;
    body["PlayMethod"]    = m_currentPlayMethod.isEmpty() ? QStringLiteral("Transcode")
                                                          : m_currentPlayMethod;
    body["Failed"]        = failed;
    if (!m_currentPlaySessionId.isEmpty())
        body["PlaySessionId"] = m_currentPlaySessionId;

    QUrl url(m_serverUrl + "/Sessions/Playing/Stopped");
    auto *reply = serverPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    // Only clear the session id if it's still the one we reported on — the
    // transcode retry in Player.qml starts a new session right after reporting
    // the failed one stopped, and this reply may land after that.
    const QString reportedSessionId = m_currentPlaySessionId;
    connect(reply, &QNetworkReply::finished, this, [this, reply, reportedSessionId]() {
        if (reply->error() != QNetworkReply::NoError)
            qWarning("[%s] report stopped failed: %s", m_tag, qPrintable(serverlog::why(reply)));
        reply->deleteLater();
        if (m_currentPlaySessionId == reportedSessionId)
            m_currentPlaySessionId.clear();
    });
    // What was played is played, or partly, now: the lists kept show it as
    // it was.
    m_keptLists.clear();
    m_keptOrder.clear();
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void EmbyApiBackend::getLibraries() {
    if (!has_auth()) {
        emit dynamicOptionsReady("libraries", QVariantList());
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Views");
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QVariantList options;
        if (reply->error() != QNetworkReply::NoError) {
            // No libraries, and why: one entry that is no choice, which the
            // page says in their place (MultiSelectSettings.qml).
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            options.append(QVariantMap{{"problem", status > 0
                ? QStringLiteral("The server answered with an error (HTTP %1)").arg(status)
                : QStringLiteral("The server could not be reached")}});
            emit dynamicOptionsReady("libraries", options);
            return;
        }

        QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        for (const QJsonValue &v : items) {
            QJsonObject item = v.toObject();
            if (!kSupportedCollectionTypes.contains(item["CollectionType"].toString()))
                continue;
            options.append(QVariantMap{
                {"id",      item["Id"].toString()},
                {"label",   item["Name"].toString().toUpper()},
            });
        }
        emit dynamicOptionsReady("libraries", options);
    });
}

void EmbyApiBackend::getVideoQualities() {
    QVariantList options;
    auto add = [&](const QString &value, const QString &label) {
        QVariantMap m;
        m["id"]    = value;
        m["label"] = label;
        options.append(m);
    };
    add("auto",  "Direct Play");
    add("480p",  "480p (NTSC CRT)");
    add("576p",  "576p (PAL CRT)");
    add("720p",  "720p");
    add("1080p", "1080p");
    emit dynamicOptionsReady("video_quality", options);
}

void EmbyApiBackend::get_resume_playback_options() {
    QVariantList options;
    auto add = [&](const QString &value, const QString &label) {
        QVariantMap m;
        m["id"]    = value;
        m["label"] = label;
        options.append(m);
    };
    add("ask",    "Ask");      // prompt resume vs. start over when a position exists
    add("always", "Always");   // resume directly, no prompt
    emit dynamicOptionsReady("resume_playback", options);
}

void EmbyApiBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value) {
    if (moduleId != m_moduleId)
        return;

    // Settings → Server Address changed while signed in names another
    // server, and the token was issued by the one signed into: it goes to no
    // other host. That server signs this device out (its token revoked there,
    // where it was issued), and the sign-in screen offers the address typed,
    // which is then the one Settings shows and the one signed into. Signed
    // out, nothing here uses the setting: the sign-in screen reads it itself.
    // The same address again (the sign-in saving it) changes nothing.
    if (key == QLatin1String("server_url") && has_auth()
        && normalizeServerUrl(value.toString()) != m_serverUrl)
        logout();
}

int EmbyApiBackend::get_last_audio_lang_idx() const {
    return m_lastAudioLangIdx;
}

int EmbyApiBackend::get_last_sub_lang_idx() const {
    return m_lastSubLangIdx;
}

void EmbyApiBackend::set_last_track_langs(const QString &audioLang, const QString &subLang,
                                          int audioLangIdx, int subLangIdx) {
    m_lastAudioLang = audioLang;
    m_lastSubLang = subLang;
    m_lastAudioLangIdx = audioLangIdx;
    m_lastSubLangIdx = subLangIdx;
}

void EmbyApiBackend::load_server_preferences() {
    if (!has_auth()) {
        emit serverLanguagePreferencesReady(m_lastAudioLang, m_lastSubLang, QString());
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId);
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit serverLanguagePreferencesReady(m_lastAudioLang, m_lastSubLang, QString());
            return;
        }
        QJsonObject userData = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonObject config = userData["Configuration"].toObject();
        QString audioLang = !m_lastAudioLang.isEmpty() ? m_lastAudioLang : config["AudioLanguagePreference"].toString();
        QString subLang   = !m_lastSubLang.isEmpty()   ? m_lastSubLang   : config["SubtitleLanguagePreference"].toString();
        QString subMode   = config["SubtitleMode"].toString();
        emit serverLanguagePreferencesReady(audioLang, subLang, subMode);
    });
}
