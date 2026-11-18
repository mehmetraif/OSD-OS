#include "EmbyBackend.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>

#include "../../util/EmbyApi.h"
#include "../../util/ServerLog.h"
#include "../../util/SslErrors.h"
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>
#include <QCoreApplication>
#include <QUuid>
#include <QSslError>
#include <QSet>
#include <QRegularExpression>

static const QString kModuleId = QStringLiteral("com.osdos.emby");

// Library CollectionTypes the module knows how to browse + play. Anything else
// (music, books, photos, mixed/empty, etc.) is hidden from both the browse list
// and the settings multiselect. Mirrors the Plex module's kSupportedLibraryTypes.
static const QSet<QString> kSupportedCollectionTypes = {
    QStringLiteral("movies"), QStringLiteral("tvshows"), QStringLiteral("homevideos"),
    QStringLiteral("boxsets")
};

// X-Application header the Emby Connect cloud service (connect.emby.media)
// expects — "AppName/AppVersion".
static QString connectAppHeader() {
    return QStringLiteral("OSD-OS/%1").arg(QCoreApplication::applicationVersion());
}

static const QString kConnectBaseUrl = QStringLiteral("https://connect.emby.media/service");

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

EmbyBackend::EmbyBackend(const QString &dataRoot, QObject *parent)
    : EmbyApiBackend(kModuleId, "Emby", QStringLiteral("emby_auth.json"), dataRoot, parent)
{
}

// ---------------------------------------------------------------------------
// Auth state persistence
// ---------------------------------------------------------------------------

void EmbyBackend::clearAuthState() {
    m_serverUrl.clear();
    m_accessToken.clear();
    m_userId.clear();
    m_userName.clear();
    m_serverName.clear();
    m_connectUserId.clear();
    m_connectAccessToken.clear();
    m_currentPlaySessionId.clear();
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

int EmbyBackend::videoQualityBitrate() const {
    QString quality = moduleConfig()["video_quality"].toString("auto");
    if (quality == QLatin1String("auto"))  return 0; // direct play — no cap
    if (quality == QLatin1String("1080p")) return 10000000;
    if (quality == QLatin1String("720p"))  return 6000000;
    if (quality == QLatin1String("576p"))  return 4500000;
    return 4000000; // 480p default
}

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

void EmbyBackend::authorize(QNetworkRequest &request, const QString &token) const {
    // Emby's canonical auth header (Jellyfin renamed this to "Authorization").
    request.setRawHeader("X-Emby-Authorization", authorization(token).toLatin1());
    if (!token.isEmpty())
        request.setRawHeader("X-Emby-Token", token.toLatin1());
}

QString EmbyBackend::streamUrl(const QString &itemId) const {
    return embyapi::streamUrl(m_serverUrl, itemId, m_accessToken, false);
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------

void EmbyBackend::check_auth() {
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
            qWarning("[EmbyBackend] Token rejected — signing out");
            clearAuthState();
            notifyAuthState();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("AUTH CHECK FAILED: " + serverlog::why(reply));
            return;
        }
        notifyAuthState();
    });
}

void EmbyBackend::logout() {
    // Revoke the access token server-side so it can't be reused, and drop the
    // device registration. /Sessions/Logout alone leaves the device listed under
    // Dashboard > Devices (Jellyfin removes it), so signing out in OSD/OS would
    // otherwise leave a stale entry behind on every sign-out — and since
    // clearAuthState() regenerates m_deviceId, the next sign-in registers a new
    // one rather than reusing it. DELETE /Devices takes either the reported
    // device id or Emby's internal numeric id; we only know the former.
    if (has_auth()) {
        // Build both requests up front, while the token is still valid.
        QUrl devUrl(m_serverUrl + "/Devices");
        { QUrlQuery dq; dq.addQueryItem("Id", m_deviceId); devUrl.setQuery(dq); }
        const QNetworkRequest deviceReq = serverRequest(devUrl);
        const QNetworkRequest logoutReq = serverRequest(QUrl(m_serverUrl + "/Sessions/Logout"));
        // The revoke goes out once the deauth has answered, after
        // clearAuthState() below has forgotten the server: its host for a
        // self-signed LAN cert is kept here.
        const QString serverHost = QUrl(m_serverUrl).host();

        // Deauth the device first, then revoke the token — the other order would
        // 401 the delete.
        auto *devReply = m_nam->deleteResource(deviceReq);
        ignoreSslErrors(devReply);
        connect(devReply, &QNetworkReply::finished, this, [this, devReply, logoutReq, serverHost]() {
            devReply->deleteLater();
            if (devReply->error() != QNetworkReply::NoError)
                qWarning("[Emby] device deauth failed: %s", qPrintable(serverlog::why(devReply)));
            auto *reply = m_nam->post(logoutReq, QByteArray());
            ignoreSslErrors(reply, serverHost);
            connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
        });
    }
    clearAuthState();
    notifyAuthState();
}

// ---------------------------------------------------------------------------
// Username / password sign-in
//
// Emby has no Quick Connect API (that is Jellyfin-only), so the module signs in
// with the classic /Users/AuthenticateByName endpoint. The response carries the
// access token and the user record; the server name is fetched separately from
// /System/Info/Public for the header, mirroring the Plex/Jellyfin modules.
// ---------------------------------------------------------------------------

void EmbyBackend::authenticate(const QString &serverUrl, const QString &username, const QString &password) {
    QString normalized = normalizeServerUrl(serverUrl);
    if (normalized.isEmpty()) {
        emit errorOccurred("SERVER URL REQUIRED");
        return;
    }
    if (username.isEmpty()) {
        emit errorOccurred("USERNAME REQUIRED");
        return;
    }

    // Set the server URL up front so ignoreSslErrors() can match the host of the
    // in-flight reply against it (self-signed LAN certs).
    m_serverUrl = normalized;

    QUrl url(normalized + "/Users/AuthenticateByName");
    QJsonObject body;
    body["Username"] = username;
    body["Pw"]       = password;   // Emby's plaintext-over-TLS password field
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    authorize(req, QString());

    auto *reply = m_nam->post(req, payload);
    ignoreSslErrors(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray respBody = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            if (status == 401)
                emit errorOccurred("INCORRECT USERNAME OR PASSWORD");
            else if (status > 0)
                emit errorOccurred("SIGN IN FAILED (HTTP " + QString::number(status) + ")");
            else
                emit errorOccurred("CONNECTION FAILED: " + serverlog::why(reply));
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(respBody).object();
        QString token = data["AccessToken"].toString();
        QJsonObject user = data["User"].toObject();

        if (token.isEmpty() || user["Id"].toString().isEmpty()) {
            emit errorOccurred("INVALID AUTH RESPONSE");
            return;
        }

        m_accessToken = token;
        m_userId      = user["Id"].toString();
        m_userName    = user["Name"].toString();
        m_serverName  = m_serverUrl; // temp: fetch real name below

        // Fetch actual server name for the header
        {
            QUrl infoUrl(m_serverUrl + "/System/Info/Public");
            auto *infoReply = serverGet(infoUrl);
            connect(infoReply, &QNetworkReply::finished, this, [this, infoReply]() {
                infoReply->deleteLater();
                if (infoReply->error() == QNetworkReply::NoError) {
                    QJsonObject infoData = QJsonDocument::fromJson(infoReply->readAll()).object();
                    QString name = infoData["ServerName"].toString();
                    if (!name.isEmpty()) {
                        m_serverName = name;
                        saveAuthState();
                    }
                }
            });
        }

        saveAuthState();

        // server_url is persisted to config.json by Root.qml (via appCore.save_setting)
        // once authStateChanged routes to Libraries — the single funnel for both the
        // direct and Emby Connect sign-in paths.
        notifyAuthState();
    });
}

// ---------------------------------------------------------------------------
// Emby Connect (cloud account linking)
//
// Step 1: POST connect.emby.media/service/user/authenticate {nameOrEmail, rawpw}
//         -> ConnectAccessToken + ConnectUserId
// Step 2: GET  connect.emby.media/service/servers?userId={ConnectUserId}
//         -> [{ Name, Url, LocalAddress, AccessKey, SystemId }]
// Step 3: GET  {serverUrl}/Connect/Exchange?format=json&ConnectUserId={id}
//         with X-MediaBrowser-Token: {AccessKey}
//         -> LocalUserId + AccessToken (the normal per-server credentials)
//         at the server's LocalAddress first, then at its Url when that
//         gives none
// ---------------------------------------------------------------------------

void EmbyBackend::connect_authenticate(const QString &usernameOrEmail, const QString &password) {
    if (usernameOrEmail.isEmpty()) {
        emit connectFailed("EMBY CONNECT EMAIL REQUIRED");
        return;
    }

    QUrl url(kConnectBaseUrl + "/user/authenticate");
    QJsonObject body;
    body["nameOrEmail"] = usernameOrEmail;
    body["rawpw"]       = password;
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Application", connectAppHeader().toLatin1());

    // connect.emby.media has a valid public certificate — do NOT relax SSL here
    // (ignoreSslErrors only relaxes for the configured local server host anyway).
    auto *reply = m_nam->post(req, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray respBody = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            if (status == 401)
                emit connectFailed("INCORRECT EMBY CONNECT EMAIL OR PASSWORD");
            else if (status > 0)
                emit connectFailed("EMBY CONNECT SIGN IN FAILED (HTTP " + QString::number(status) + ")");
            else
                emit connectFailed("CONNECTION FAILED: " + serverlog::why(reply));
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(respBody).object();
        // The service returns AccessToken + User.Id; some responses spell these
        // ConnectAccessToken + ConnectUserId — accept either.
        QString token = data["AccessToken"].toString();
        if (token.isEmpty()) token = data["ConnectAccessToken"].toString();
        QString connectUserId = data["User"].toObject()["Id"].toString();
        if (connectUserId.isEmpty()) connectUserId = data["ConnectUserId"].toString();

        if (token.isEmpty() || connectUserId.isEmpty()) {
            emit connectFailed("INVALID EMBY CONNECT RESPONSE");
            return;
        }

        m_connectAccessToken = token;
        m_connectUserId      = connectUserId;
        fetchConnectServers();
    });
}

void EmbyBackend::fetchConnectServers() {
    QUrl url(kConnectBaseUrl + "/servers");
    { QUrlQuery q; q.addQueryItem("userId", m_connectUserId); url.setQuery(q); }

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Application", connectAppHeader().toLatin1());
    req.setRawHeader("X-Connect-UserToken", m_connectAccessToken.toLatin1());

    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit connectFailed("FAILED TO LOAD SERVERS: " + serverlog::why(reply));
            return;
        }

        QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
        QVariantList servers;
        m_connectRemotes.clear();
        for (const QJsonValue &v : arr) {
            QJsonObject s = v.toObject();
            const QString local  = normalizeServerUrl(s["LocalAddress"].toString());
            const QString remote = normalizeServerUrl(s["Url"].toString());
            // Prefer the LAN address (a CRT device usually shares the network with
            // its server); fall back to the remote/WAN address, which is also
            // tried when the LAN one gives no exchange (connect_select_server).
            const QString address = !local.isEmpty() ? local : remote;
            if (address.isEmpty())
                continue;
            if (!remote.isEmpty() && remote != address)
                m_connectRemotes.insert(s["AccessKey"].toString(), remote);
            servers.append(QVariantMap{
                {"name",          s["Name"].toString()},
                {"address",       address},
                {"localAddress",  local},
                {"remoteAddress", remote},
                {"accessKey",     s["AccessKey"].toString()},
                {"systemId",      s["SystemId"].toString()},
            });
        }

        if (servers.isEmpty()) {
            emit connectFailed("NO SERVERS LINKED TO THIS ACCOUNT");
            return;
        }
        if (servers.size() == 1) {
            // Only one server — skip the picker and exchange straight away.
            const QVariantMap only = servers.first().toMap();
            connect_select_server(only["address"].toString(), only["accessKey"].toString());
            return;
        }
        emit connectServersReady(servers);
    });
}

void EmbyBackend::connect_select_server(const QString &serverUrl, const QString &accessKey) {
    const QString normalized = normalizeServerUrl(serverUrl);
    if (normalized.isEmpty() || accessKey.isEmpty()) {
        emit connectFailed("INVALID SERVER SELECTION");
        return;
    }
    // The address Emby Connect has for the server from outside, for when the
    // one chosen (its LAN address, where it names one) gives no exchange: a
    // server in another house, or one in a container that names an address of
    // the container's own.
    const QString remote = m_connectRemotes.value(accessKey);
    exchangeConnectKey(normalized, accessKey, remote == normalized ? QString() : remote);
}

void EmbyBackend::exchangeConnectKey(const QString &serverUrl, const QString &accessKey,
                                     const QString &fallbackUrl) {
    // Set the server URL up front so ignoreSslErrors() matches the exchange
    // reply's host (self-signed LAN servers).
    m_serverUrl = serverUrl;

    QUrl url(serverUrl + "/Connect/Exchange");
    { QUrlQuery q; q.addQueryItem("format", "json"); q.addQueryItem("ConnectUserId", m_connectUserId); url.setQuery(q); }

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Application", connectAppHeader().toLatin1());
    req.setRawHeader("X-MediaBrowser-Token", accessKey.toLatin1());
    req.setRawHeader("X-Emby-Token", accessKey.toLatin1());
    req.setRawHeader("X-Emby-Authorization", authorization(QString()).toLatin1());

    auto *reply = m_nam->get(req);
    ignoreSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, accessKey, fallbackUrl]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // No exchange at this address (nothing there, or something else that
        // answers in the server's place on this network): the server's other
        // address is tried before the failure is said.
        const auto failed = [this, accessKey, fallbackUrl](const QString &message) {
            if (fallbackUrl.isEmpty())
                emit connectFailed(message);
            else
                exchangeConnectKey(fallbackUrl, accessKey, QString());
        };
        if (reply->error() != QNetworkReply::NoError) {
            failed("SERVER EXCHANGE FAILED (HTTP " + QString::number(status) + ")");
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        const QString localUserId = data["LocalUserId"].toString();
        const QString token       = data["AccessToken"].toString();
        if (localUserId.isEmpty() || token.isEmpty()) {
            failed("INVALID EXCHANGE RESPONSE");
            return;
        }

        m_userId      = localUserId;
        m_accessToken = token;
        m_serverName  = m_serverUrl; // real name fetched in persistConnectAuthAndFinish
        persistConnectAuthAndFinish();
    });
}

void EmbyBackend::persistConnectAuthAndFinish() {
    saveAuthState();

    // server_url is persisted to config.json by Root.qml (via appCore.save_setting)
    // on authStateChanged — same funnel as the direct sign-in path.

    // Fetch the display name of the local user (the exchange only returns an id).
    {
        QUrl userUrl(m_serverUrl + "/Users/" + m_userId);
        auto *userReply = serverGet(userUrl);
        connect(userReply, &QNetworkReply::finished, this, [this, userReply]() {
            userReply->deleteLater();
            if (userReply->error() == QNetworkReply::NoError) {
                QString name = QJsonDocument::fromJson(userReply->readAll()).object()["Name"].toString();
                if (!name.isEmpty()) {
                    m_userName = name;
                    saveAuthState();
                }
            }
        });
    }

    // Fetch the friendly server name for the header.
    {
        QUrl infoUrl(m_serverUrl + "/System/Info/Public");
        auto *infoReply = serverGet(infoUrl);
        connect(infoReply, &QNetworkReply::finished, this, [this, infoReply]() {
            infoReply->deleteLater();
            if (infoReply->error() == QNetworkReply::NoError) {
                QString name = QJsonDocument::fromJson(infoReply->readAll()).object()["ServerName"].toString();
                if (!name.isEmpty()) {
                    m_serverName = name;
                    saveAuthState();
                }
            }
        });
    }

    notifyAuthState();
}

// ---------------------------------------------------------------------------
// Browse
// ---------------------------------------------------------------------------

void EmbyBackend::load_libraries() {
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
        QJsonObject libEnabled = moduleConfig()["libraries"].toObject();
        QVariantList libraries;
        for (const QJsonValue &v : items) {
            QJsonObject item = v.toObject();
            if (!kSupportedCollectionTypes.contains(item["CollectionType"].toString()))
                continue;
            QString libId = item["Id"].toString();
            if (!libEnabled.isEmpty() && !libEnabled[libId].toBool(true))
                continue;
            libraries.append(QVariantMap{
                {"key",            libId},
                {"itemId",         libId},
                {"title",          item["Name"].toString().toUpper()},
                {"collectionType", item["CollectionType"].toString()},
            });
        }

        // Prepend the Continue Watching / Up Next shelves, but only when they
        // actually have content. Probe each (limit=1) before emitting the list.
        // mediaTypes is required: Emby's Resume returns an empty list (HTTP 200,
        // TotalRecordCount 0) without a media-type or item-type filter, however
        // many resume points exist. Jellyfin needs no such filter.
        QUrl resumeUrl(m_serverUrl + "/Users/" + m_userId + "/Items/Resume");
        { QUrlQuery rq; rq.addQueryItem("mediaTypes", "Video");
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

void EmbyBackend::probeHasItems(const QUrl &url, std::function<void(bool)> cb) {
    // Which shelf, for the log: the path's last two parts (Items/Resume,
    // Shows/NextUp), never the user's id before them.
    const QString probePath = url.path().section(QLatin1Char('/'), -2);
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [reply, cb, probePath]() {
        reply->deleteLater();
        bool has = false;
        if (reply->error() == QNetworkReply::NoError) {
            has = !QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray().isEmpty();
        } else {
            // A failed probe hides the shelf exactly like an empty one, so warn —
            // otherwise a broken request is indistinguishable from "nothing to
            // resume" and stays invisible (as the missing mediaTypes=Video did).
            qWarning("[Emby] shelf probe failed (%s): %s",
                     qPrintable(probePath), qPrintable(serverlog::why(reply)));
        }
        cb(has);
    });
}

void EmbyBackend::load_episodes(const QString &seriesId, const QString &seasonId) {
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
    // No sortBy: Emby accepts the param but has no aired-order sort value, and
    // "AiredEpisodeOrder" (a Jellyfin sort name) makes it 500 with a
    // SQLiteException. Emby returns season episodes in index order natively.
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
            serverlog::loadFailed("Emby", "the episodes of season " + seasonId, reply);
            emit errorOccurred("LOAD EPISODES FAILED: " + serverlog::why(reply));
            return;
        }
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        if (!answer.contains(QLatin1String("Items")))
            serverlog::loadFailed("Emby", "the episodes of season " + seasonId, reply,
                                  QStringLiteral("the answer is not a list"));
        QJsonArray items = answer["Items"].toArray();
        QVariantList result;
        for (const QJsonValue &v : items)
            result.append(formatItem(v.toObject()));
        emit episodesLoaded(result);
    });
}

void EmbyBackend::load_continue_watching() {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items/Resume");
    QUrlQuery q;
    q.addQueryItem("mediaTypes", "Video");   // required on Emby — see load_libraries
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

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void EmbyBackend::get_playback_url(const QString &itemId, const QString &mediaSourceId,
                                       int audioStreamIndex, int subtitleStreamIndex,
                                       bool forceTranscode) {
    if (!has_auth()) {
        emit errorOccurred("NOT AUTHENTICATED");
        return;
    }

    // "auto" (Direct Play) lets the server serve the original file untouched
    // when the source is compatible; any other value forces a quality-capped
    // HLS transcode. forceTranscode overrides "auto" for a fallback retry after
    // a direct-play failure (see Player.qml onPlaybackEnded).
    const bool directPlay = !forceTranscode
                          && (moduleConfig()["video_quality"].toString("auto")
                              == QLatin1String("auto"));
    int maxBitrate = videoQualityBitrate();
    int maxHeight  = videoQualityMaxHeight();
    // Dolby Vision → SDR re-request: cap an otherwise-uncapped ("auto") transcode
    // to 1080p/20Mbps so the server can start it promptly and keep up in real time.
    if (m_forceSdrTranscodeCap) {
        if (maxBitrate <= 0) maxBitrate = 20000000;
        if (maxHeight  <= 0) maxHeight  = 1080;
        m_forceSdrTranscodeCap = false;
    }

    QUrl url(m_serverUrl + "/Items/" + itemId + "/PlaybackInfo");
    QJsonObject body;
    body["UserId"]                 = m_userId;
    body["MediaSourceId"]          = mediaSourceId;
    if (audioStreamIndex >= 0)
        body["AudioStreamIndex"]   = audioStreamIndex;
    // For direct play the device profile already advertises Embed for every
    // subtitle format (including PGS/VOBSUB), so the server knows we handle
    // them client-side and won't force a transcode.  Pass the user's actual
    // selection (or omit if off) so the static stream includes all tracks.
    // Hardcoding -1 here caused newer *Jellyfin* servers to strip sub tracks
    // from the stream, breaking both --sid for image subs and sidecar URLs.
    // Carried over from the Jellyfin module; not verified against Emby.
    if (subtitleStreamIndex >= 0)
        body["SubtitleStreamIndex"] = subtitleStreamIndex;
    if (maxBitrate > 0) body["MaxStreamingBitrate"] = maxBitrate;
    if (maxHeight  > 0) body["MaxHeight"]           = maxHeight;
    body["EnableDirectPlay"]       = directPlay;
    body["EnableDirectStream"]     = directPlay;

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
    QJsonArray subtitleProfiles;
    if (directPlay) {
        // Direct play serves the original file whole; the video stream is never
        // touched. How the *subtitle* reaches mpv depends on its kind, and the
        // Method we advertise per format has to match — otherwise Emby, unlike
        // Jellyfin, refuses direct play the moment a subtitle is selected and
        // falls back to a full transcode (SubtitleMethod=Encode / burn-in).
        //
        //   Text subs (subrip/ass/vtt/…): the client fetches them as a sidecar
        //   file (see subUrl in extract_item_detail) and hands them to mpv as
        //   --sub-file. That is External delivery, so advertise Method=External.
        //   Advertising only Embed (as the Jellyfin port did) is why Emby
        //   transcoded on every subtitle-on: it couldn't embed the selected sub
        //   into a *static* stream and had no permitted alternative, so it burnt
        //   it in. Jellyfin defaults external text subs to External regardless;
        //   Emby honours only what the profile declares.
        //
        //   Image subs (PGS/VOBSUB): no text sidecar exists, so mpv selects them
        //   from the embedded stream via --sid. That is Embed delivery.
        auto addSub = [&](const char *fmt, const char *method) {
            QJsonObject s;
            s["Format"] = QString::fromLatin1(fmt);
            s["Method"] = QString::fromLatin1(method);
            subtitleProfiles.append(s);
        };
        addSub("subrip",   "External");
        addSub("srt",      "External");
        addSub("ass",      "External");
        addSub("ssa",      "External");
        addSub("vtt",      "External");
        addSub("webvtt",   "External");
        addSub("mov_text", "External");
        addSub("pgssub",   "Embed");
        addSub("dvbsub",   "Embed");
        addSub("dvdsub",   "Embed");
    } else {
        // Transcode: text subs are delivered as a sidecar, exactly as they are
        // for direct play — only image subs are burnt in.
        //
        // Burning text subs in (Method=Encode) is what the Plex module does and
        // what this module used to do, but it makes the server re-encode the
        // video with the subtitle painted on, and Emby 500s on that for every
        // subrip tested here, SDR sources included. A burnt-in sub therefore
        // meant no playback at all on the transcode path.
        //
        // External is safe because it is NOT the soft HLS subtitle rendition
        // that mpv renders unreliably after a seek: the sidecar is a separate
        // /Videos/.../Subtitles/{i}/Stream.srt fetch, independent of the HLS
        // video, and Player.qml hands the selected one to mpv as --sub-file on
        // the transcode path too (see doStartPlayback). Verified
        // against Emby 4.9.5.0: with Method=External the returned
        // TranscodingUrl carries no SubtitleMethod and no SubtitleStreamIndex,
        // so the video transcodes untouched.
        //
        // Image subs (PGS/VOBSUB) have no text sidecar and cannot ride along in
        // an HLS rendition, so burn-in stays their only option.
        auto addSub = [&](const char *fmt, const char *method) {
            QJsonObject s;
            s["Format"] = QString::fromLatin1(fmt);
            s["Method"] = QString::fromLatin1(method);
            subtitleProfiles.append(s);
        };
        addSub("subrip",   "External");
        addSub("srt",      "External");
        addSub("ass",      "External");
        addSub("ssa",      "External");
        addSub("vtt",      "External");
        addSub("webvtt",   "External");
        addSub("mov_text", "External");
        addSub("pgssub",   "Encode");
        addSub("dvbsub",   "Encode");
        addSub("dvdsub",   "Encode");
    }
    profile["SubtitleProfiles"] = subtitleProfiles;

    // Emby applies a default streaming bitrate cap when the profile doesn't
    // declare one, and silently refuses direct play for any source above it —
    // SupportsDirectPlay=0 / SupportsDirectStream=0 with no TranscodeReasons.
    // Anything much past a couple of Mbps is denied, which is most real media.
    // Advertise an effectively unlimited cap for direct play; the quality tiers
    // keep their own cap so the server still sizes the transcode correctly.
    profile["MaxStreamingBitrate"] = maxBitrate > 0 ? maxBitrate : 2000000000;

    QJsonArray directPlayProfiles;
    if (directPlay) {
        // mpv plays virtually anything, so advertise a match-all profile —
        // omitted Container/VideoCodec/AudioCodec fields match every value in
        // Emby's profile matcher too (verified against Emby 4.9.5.0: mkv/mp4/avi
        // with h264/hevc/vc1/mpeg2 all direct-play through this wildcard).
        // A codec whitelist here silently forced
        // transcodes on exact-name misses (pcm_s16le vs pcm, webvtt vs vtt, …).
        // If mpv truly can't play a file, the transcode retry in Player.qml
        // onPlaybackEnded is the safety net. An empty array (transcode mode)
        // tells the server nothing can be direct-played.
        QJsonObject dp;
        dp["Type"] = QStringLiteral("Video");
        directPlayProfiles.append(dp);
    }
    profile["DirectPlayProfiles"] = directPlayProfiles;
    body["DeviceProfile"] = profile;

    const int request = ++m_playbackRequest;
    auto *reply = serverPost(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, request, itemId, mediaSourceId, audioStreamIndex, subtitleStreamIndex, directPlay]() {
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

        // Dolby Vision won't render. mpv on our targets (macOS videotoolbox, Pi)
        // can't play the DV enhancement layer — it decodes the file but bails at
        // video output ("Dolby Vision enhancement-layer playback is not supported"),
        // so a DV title direct-plays for a moment then exits. Force an SDR H.264
        // transcode instead: the server tone-maps DV→SDR (what Plex/Jellyfin do),
        // and a CRT is SDR anyway. Done client-side off the response's reported
        // range because a DeviceProfile CodecProfile rejecting VideoRangeType=DOVI
        // was verified to be ignored by Emby 4.9.5.0 (the wildcard DirectPlay
        // profile still won). forceTranscode=true on the re-request means directPlay
        // is false there, so this never recurses.
        if (directPlay) {
            QString videoRange;
            for (const QJsonValue &sv : source["MediaStreams"].toArray()) {
                const QJsonObject st = sv.toObject();
                if (st["Type"].toString() == QLatin1String("Video")) {
                    videoRange = st["VideoRange"].toString();
                    if (videoRange.isEmpty())
                        videoRange = st["VideoRangeType"].toString();
                    break;
                }
            }
            if (videoRange.contains(QLatin1String("dolby"), Qt::CaseInsensitive)
                || videoRange.compare(QLatin1String("DOVI"), Qt::CaseInsensitive) == 0) {
                qInfo("[Emby] Dolby Vision source (range=%s) — forcing SDR transcode; mpv can't render the DV enhancement layer",
                      qPrintable(videoRange));
                // Keep the selected subtitle if it can survive the transcode.
                // This used to drop every subtitle, because the transcode path
                // burnt the selected sub in (SubtitleMethod=Encode) and this
                // server 500s on burn-in, so a burned sub meant no playback at
                // all. Text subs are now delivered as a sidecar (Method=External
                // in the transcode profile) and never touch the video, so there
                // is nothing left to work around for them.
                //
                // Image subs (PGS/VOBSUB) have no sidecar and are still burnt in,
                // which is exactly the case that takes the whole transcode down,
                // so those are still dropped rather than losing playback.
                int keptSubtitle = subtitleStreamIndex;
                if (keptSubtitle >= 0) {
                    bool isTextSub = false;
                    for (const QJsonValue &sv : source["MediaStreams"].toArray()) {
                        const QJsonObject st = sv.toObject();
                        if (st["Type"].toString() == QLatin1String("Subtitle")
                            && st["Index"].toInt() == keptSubtitle) {
                            isTextSub = st["IsTextSubtitleStream"].toBool();
                            break;
                        }
                    }
                    if (!isTextSub) {
                        qInfo("[Emby] DV transcode: dropping image subtitle %d (burn-in would fail)",
                              keptSubtitle);
                        keptSubtitle = -1;
                    }
                }
                m_forceSdrTranscodeCap = true;
                get_playback_url(itemId, mediaSourceId, audioStreamIndex,
                                 keptSubtitle, /*forceTranscode=*/true);
                return;
            }
        }

        // Direct play when requested AND the server confirms the source is
        // compatible. Serves the original file via /Videos/{id}/stream?static=true;
        // mpv handles embedded audio/subtitle tracks (Player.qml direct-play branch).
        // PlaySessionId from the PlaybackInfo response is reused for the stream
        // URL and every /Sessions report so the server correlates the dashboard
        // session with this stream — and tears the transcode down on Stopped.
        const QString playSessionId = data["PlaySessionId"].toString();

        if (directPlay && (source["SupportsDirectPlay"].toBool()
                           || source["SupportsDirectStream"].toBool())) {
            const QString srcId = source["Id"].toString(mediaSourceId);
            QString directUrl = m_serverUrl + "/Videos/" + itemId + "/stream"
                              + "?static=true"
                              + "&mediaSourceId=" + srcId;
            if (!playSessionId.isEmpty())
                directUrl += "&PlaySessionId=" + playSessionId;
            // Emby authenticates the media request via api_key; mpv only carries
            // the Jellyfin-style Authorization header, so embed the token here.
            if (!m_accessToken.isEmpty())
                directUrl += "&api_key=" + m_accessToken;
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
            qWarning("[Emby] direct play denied (SupportsDirectPlay=%d SupportsDirectStream=%d): %s",
                     source["SupportsDirectPlay"].toBool(),
                     source["SupportsDirectStream"].toBool(),
                     reasons.isEmpty() ? "no TranscodeReasons given" : qPrintable(reasons));
        }
        QString transcodeUrl = source["TranscodingUrl"].toString();
        if (transcodeUrl.isEmpty()) {
            emit errorOccurred("NO TRANSCODE URL");
            return;
        }
        m_currentPlaySessionId = playSessionId;
        m_currentPlayMethod    = QStringLiteral("Transcode");

        // Build the full URL. Unlike the Jellyfin module (which strips api_key and
        // relies on mpv's Authorization header), Emby authenticates the HLS request
        // via api_key, so keep the server-supplied token and add one if it's absent.
        // When the user selected OFF, still strip any SubtitleStreamIndex and
        // SubtitleMethod the server may have added from default metadata.
        QUrl parsedUrl(m_serverUrl + transcodeUrl);
        {
            QUrlQuery q(parsedUrl);
            bool hasApiKey = false;
            const auto items = q.queryItems();
            for (const auto &kv : items) {
                if (kv.first.compare(QLatin1String("api_key"), Qt::CaseInsensitive) == 0 ||
                    kv.first.compare(QLatin1String("apikey"),  Qt::CaseInsensitive) == 0)
                    hasApiKey = true;
            }
            if (!hasApiKey && !m_accessToken.isEmpty())
                q.addQueryItem("api_key", m_accessToken);
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
        {
            const int maxHeight = videoQualityMaxHeight();
            if (maxHeight > 0) {
                QRegularExpression heightRe("(MaxHeight|Height)=[^&]+",
                                            QRegularExpression::CaseInsensitiveOption);
                const QString replacement = "MaxHeight=" + QString::number(maxHeight);
                if (fullUrl.contains(heightRe))
                    fullUrl.replace(heightRe, replacement);
                else
                    fullUrl += "&" + replacement;
            }
        }

        report_playback_start(itemId, mediaSourceId,
                              audioStreamIndex >= 0 ? QString::number(audioStreamIndex) : QString(),
                              subtitleStreamIndex >= 0 ? QString::number(subtitleStreamIndex) : QString());
        emit streamUrlReady(fullUrl);
    });
}

void EmbyBackend::load_next_episode(const QString &currentItemId) {
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

// The series' episodes from the current one (startItemId), a page of them:
// it holds the next whatever the series' length, where a list of the whole
// series was cut at 500. No sortBy (see load_episodes): Emby lists them in
// their order natively. A server that doesn't skip to the item (its page
// starts at the first episode) is asked once more for the whole series.
void EmbyBackend::fetchNextEpisode(const QString &seriesId, const QString &itemId, int currentSeason,
                                   int currentIndex, bool fromItem) {
    QUrl epUrl(m_serverUrl + "/Shows/" + seriesId + "/Episodes");
    QUrlQuery epQ;
    epQ.addQueryItem("userId", m_userId);
    epQ.addQueryItem("fields", "MediaSources,MediaStreams,Overview,Genres,UserData");
    epQ.addQueryItem("enableUserData", "true");
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

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void EmbyBackend::getVideoQualities() {
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

void EmbyBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value) {
    if (moduleId != kModuleId)
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

void EmbyBackend::fetchSegments(const QString &itemId) {
    if (!has_auth()) return;

    // Emby has no MediaSegments API (that is Jellyfin 10.10+). Instead it records
    // intro/credit boundaries as chapter markers on the item — chapters whose
    // MarkerType is IntroStart / IntroEnd / CreditsStart. Fetch the item with the
    // Chapters field and synthesise the same {type,startMs,endMs} segments the
    // Player consumes: an "Intro" (IntroStart→IntroEnd) and an "Outro"
    // (CreditsStart→end of file). Markers only exist once the server has detected
    // them, so an item without detection simply yields no segments.
    QUrl url(m_serverUrl + "/Users/" + m_userId + "/Items/" + itemId);
    QUrlQuery q;
    q.addQueryItem("fields", "Chapters");
    url.setQuery(q);

    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonObject item = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonArray chapters = item["Chapters"].toArray();
        const double runtimeMs = item["RunTimeTicks"].toDouble() / 10000.0;

        double introStartMs  = -1.0;
        double introEndMs    = -1.0;
        double creditsStartMs = -1.0;
        for (const QJsonValue &val : chapters) {
            QJsonObject ch = val.toObject();
            const QString marker = ch["MarkerType"].toString();
            const double posMs = ch["StartPositionTicks"].toDouble() / 10000.0;
            if (marker == QLatin1String("IntroStart"))        introStartMs  = posMs;
            else if (marker == QLatin1String("IntroEnd"))     introEndMs    = posMs;
            else if (marker == QLatin1String("CreditsStart")) creditsStartMs = posMs;
        }

        QVariantList segments;
        if (introStartMs >= 0.0 && introEndMs > introStartMs) {
            segments.append(QVariantMap{
                {"type",    QStringLiteral("Intro")},
                {"startMs", introStartMs},
                {"endMs",   introEndMs},
            });
        }
        if (creditsStartMs >= 0.0) {
            // Credits run to the end of the file; fall back to a fixed window if
            // the runtime is unknown.
            const double endMs = runtimeMs > creditsStartMs ? runtimeMs
                                                            : creditsStartMs + 600000.0;
            segments.append(QVariantMap{
                {"type",    QStringLiteral("Outro")},
                {"startMs", creditsStartMs},
                {"endMs",   endMs},
            });
        }

        emit segmentsReady(itemId, segments);
    });
}
