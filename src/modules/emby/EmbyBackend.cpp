#include "EmbyBackend.h"

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
#include <QSslError>

static const QString kModuleId = QStringLiteral("com.osdos.emby");

// X-Application header the Emby Connect cloud service (connect.emby.media)
// expects — "AppName/AppVersion".
static QString connectAppHeader() {
    return QStringLiteral("OSD-OS/%1").arg(QCoreApplication::applicationVersion());
}

static const QString kConnectBaseUrl = QStringLiteral("https://connect.emby.media/service");

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

EmbyBackend::EmbyBackend(const QString &dataRoot, AppCore *appCore, QObject *parent)
    : EmbyApiBackend(kModuleId, "Emby", QStringLiteral("emby_auth.json"), dataRoot, appCore, parent)
{
}

// ---------------------------------------------------------------------------
// Auth state persistence
// ---------------------------------------------------------------------------

void EmbyBackend::clearAuthState() {
    EmbyApiBackend::clearAuthState();
    m_connectUserId.clear();
    m_connectAccessToken.clear();
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

void EmbyBackend::revokeToken() {
    // Revoke the access token server-side so it can't be reused, and drop the
    // device registration. /Sessions/Logout alone leaves the device listed under
    // Dashboard > Devices (Jellyfin removes it), so signing out in OSD/OS would
    // otherwise leave a stale entry behind on every sign-out — and since
    // clearAuthState() regenerates m_deviceId, the next sign-in registers a new
    // one rather than reusing it. DELETE /Devices takes either the reported
    // device id or Emby's internal numeric id; we only know the former.
    //
    // Build both requests up front, while the token is still valid.
    QUrl devUrl(m_serverUrl + "/Devices");
    { QUrlQuery dq; dq.addQueryItem("Id", m_deviceId); devUrl.setQuery(dq); }
    const QNetworkRequest deviceReq = serverRequest(devUrl);
    const QNetworkRequest logoutReq = serverRequest(QUrl(m_serverUrl + "/Sessions/Logout"));
    // The revoke goes out once the deauth has answered, after
    // clearAuthState() has forgotten the server: its host for a
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
    // The server's host for a self-signed LAN cert: it becomes the server
    // signed in to only once the sign-in has succeeded.
    ignoreSslErrors(reply, url.host());

    connect(reply, &QNetworkReply::finished, this, [this, reply, normalized]() {
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

        m_serverUrl   = normalized;
        m_accessToken = token;
        m_userId      = user["Id"].toString();
        m_userName    = user["Name"].toString();
        m_serverName  = m_serverUrl; // temp: fetch real name below

        fetchServerName();
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
    QUrl url(serverUrl + "/Connect/Exchange");
    { QUrlQuery q; q.addQueryItem("format", "json"); q.addQueryItem("ConnectUserId", m_connectUserId); url.setQuery(q); }

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Application", connectAppHeader().toLatin1());
    req.setRawHeader("X-MediaBrowser-Token", accessKey.toLatin1());
    req.setRawHeader("X-Emby-Token", accessKey.toLatin1());
    req.setRawHeader("X-Emby-Authorization", authorization(QString()).toLatin1());

    auto *reply = m_nam->get(req);
    // The server's host for a self-signed LAN cert, as in authenticate().
    ignoreSslErrors(reply, url.host());
    connect(reply, &QNetworkReply::finished, this, [this, reply, serverUrl, accessKey, fallbackUrl]() {
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
            // Nothing answering has no status to say: the network's own
            // word for it then, as the other sign-ins say it.
            failed(status > 0 ? "SERVER EXCHANGE FAILED (HTTP " + QString::number(status) + ")"
                              : "SERVER EXCHANGE FAILED: " + serverlog::why(reply));
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        const QString localUserId = data["LocalUserId"].toString();
        const QString token       = data["AccessToken"].toString();
        if (localUserId.isEmpty() || token.isEmpty()) {
            failed("INVALID EXCHANGE RESPONSE");
            return;
        }

        m_serverUrl   = serverUrl;
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
    fetchServerName();

    notifyAuthState();
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

QJsonObject EmbyBackend::deviceProfile(bool directPlay, int maxBitrate) const {
    QJsonObject profile = EmbyApiBackend::deviceProfile(directPlay, maxBitrate);
    // Emby applies a default streaming bitrate cap when the profile doesn't
    // declare one, and silently refuses direct play for any source above it —
    // SupportsDirectPlay=0 / SupportsDirectStream=0 with no TranscodeReasons.
    // Anything much past a couple of Mbps is denied, which is most real media.
    // Advertise an effectively unlimited cap for direct play; the quality tiers
    // keep their own cap so the server still sizes the transcode correctly.
    profile["MaxStreamingBitrate"] = maxBitrate > 0 ? maxBitrate : 2000000000;
    return profile;
}

bool EmbyBackend::transcodeInstead(const QJsonObject &source, const QString &itemId,
                                   const QString &mediaSourceId, int audioStreamIndex,
                                   int subtitleStreamIndex) {
    // Dolby Vision won't render. mpv on our targets (macOS videotoolbox, Pi)
    // can't play the DV enhancement layer — it decodes the file but bails at
    // video output ("Dolby Vision enhancement-layer playback is not supported"),
    // so a DV title direct-plays for a moment then exits. Force an SDR H.264
    // transcode instead: the server tone-maps DV→SDR (what Plex/Jellyfin do),
    // and a CRT is SDR anyway. Done client-side off the response's reported
    // range because a DeviceProfile CodecProfile rejecting VideoRangeType=DOVI
    // was verified to be ignored by Emby 4.9.5.0 (the wildcard DirectPlay
    // profile still won). forceTranscode=true on the re-request means it asks
    // for no Direct Play, so this never recurses.
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
    if (!videoRange.contains(QLatin1String("dolby"), Qt::CaseInsensitive)
        && videoRange.compare(QLatin1String("DOVI"), Qt::CaseInsensitive) != 0)
        return false;

    qInfo("[Emby] Dolby Vision source (range=%s) — forcing SDR transcode; mpv can't render the DV enhancement layer",
          qPrintable(videoRange));
    // Keep the selected subtitle if it can survive the transcode. Text
    // subs are delivered as a sidecar (Method=External in the transcode
    // profile) and never touch the video.
    //
    // Image subs (PGS/VOBSUB) have no sidecar and are burnt in
    // (SubtitleMethod=Encode), which this server 500s on, taking the
    // whole transcode down: those are dropped rather than losing playback.
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
    // Capped to 1080p/20Mbps where no quality caps it (sdrCap): a full-4K,
    // uncapped transcode is too heavy to start promptly, and the display
    // targets are SDR anyway.
    requestPlayback(itemId, mediaSourceId, audioStreamIndex, keptSubtitle,
                    /*forceTranscode=*/true, /*sdrCap=*/true);
    return true;
}

QJsonArray EmbyBackend::subtitleProfiles(bool directPlay) const {
    QJsonArray profiles;
    if (directPlay) {
        // Direct play serves the original file whole; the video stream is never
        // touched. How the *subtitle* reaches mpv depends on its kind, and the
        // Method we advertise per format has to match — otherwise Emby, unlike
        // Jellyfin, refuses direct play the moment a subtitle is selected and
        // falls back to a full transcode (SubtitleMethod=Encode / burn-in).
        //
        //   Text subs (subrip/ass/vtt/…): the client fetches them as a sidecar
        //   file (see subUrl in formatItem) and hands them to mpv as
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
            profiles.append(s);
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
        // Burning text subs in (Method=Encode) is what the Plex module does,
        // but it makes the server re-encode the
        // video with the subtitle painted on, and Emby 500s on that for every
        // subrip tested here, SDR sources included. A burnt-in sub therefore
        // meant no playback at all on the transcode path.
        //
        // External is safe because it is NOT the soft HLS subtitle rendition
        // that mpv renders unreliably after a seek: the sidecar is a separate
        // /Videos/.../Subtitles/{i}/Stream.srt fetch, independent of the HLS
        // video, and MediaServerPlayer.qml hands the selected one to mpv as --sub-file on
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
            profiles.append(s);
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
    return profiles;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

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
