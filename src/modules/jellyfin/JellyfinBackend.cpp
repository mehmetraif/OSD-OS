#include "JellyfinBackend.h"

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
#include <QSslError>
#include <QRegularExpression>

static const QString kModuleId = QStringLiteral("com.osdos.jellyfin");

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

JellyfinBackend::JellyfinBackend(const QString &dataRoot, QObject *parent)
    : EmbyApiBackend(kModuleId, "Jellyfin", QStringLiteral("jellyfin_auth.json"), dataRoot, parent)
{
}

// ---------------------------------------------------------------------------
// Auth state persistence
// ---------------------------------------------------------------------------

void JellyfinBackend::clearAuthState() {
    EmbyApiBackend::clearAuthState();
    m_capabilitiesProbed = false;
    m_hasCapability = false;
}

// ---------------------------------------------------------------------------
// Config helpers
// ---------------------------------------------------------------------------

int JellyfinBackend::videoQualityBitrate() const {
    QString quality = moduleConfig()["video_quality"].toString("auto");
    if (quality == QLatin1String("auto"))  return 1000000000; // direct play — 1000 Mbps cap
    if (quality == QLatin1String("1080p")) return 10000000;
    if (quality == QLatin1String("720p"))  return 6000000;
    if (quality == QLatin1String("576p"))  return 4500000;
    return 4000000; // 480p default
}

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

void JellyfinBackend::authorize(QNetworkRequest &request, const QString &token) const {
    request.setRawHeader("Authorization", authorization(token).toLatin1());
}

QString JellyfinBackend::streamUrl(const QString &itemId) const {
    return embyapi::streamUrl(m_serverUrl, itemId, m_accessToken, true);
}

// ---------------------------------------------------------------------------
// Quick Connect
// ---------------------------------------------------------------------------

void JellyfinBackend::quick_connect_initiate(const QString &serverUrl) {
    QString normalized = normalizeServerUrl(serverUrl);
    if (normalized.isEmpty()) {
        emit errorOccurred("SERVER URL REQUIRED");
        return;
    }

    m_quickConnectServerUrl = normalized;
    QUrl url(normalized + "/QuickConnect/Initiate");

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    authorize(req, QString());

    // Initiate uses empty POST body
    auto *reply = m_nam->post(req, QByteArray());
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors) {
        for (const QSslError &e : errors)
            qDebug("[JellyfinBackend] QC SSL error (ignored): %s", qPrintable(e.errorString()));
        QList<QSslError> allowed = expectedLanSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();

        // The server's refusal first, by its status: Qt marks every answer of
        // 400 and above as an error too, and asked first the error took them
        // all, in Qt's words for it ("Host requires authentication" for a
        // server with Quick Connect off). What is left as an error had no
        // such answer: nothing at the address, or a connection that broke.
        if (status >= 400) {
            qWarning("[JellyfinBackend] QC Initiate HTTP %d", status);
            if (status == 401)
                emit quickConnectFailed("QUICK CONNECT NOT ENABLED ON SERVER");
            else
                emit quickConnectFailed("SERVER ERROR (HTTP " + QString::number(status) + ")");
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit quickConnectFailed("CONNECTION FAILED: " + serverlog::why(reply));
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(body).object();
        QString secret = data["Secret"].toString();
        QString code   = data["Code"].toString();

        if (secret.isEmpty() || code.isEmpty()) {
            emit quickConnectFailed("INVALID RESPONSE FROM SERVER");
            return;
        }

        m_quickConnectSecret = secret;
        emit quickConnectCodeReady(code, secret);
    });
}

void JellyfinBackend::quick_connect_poll(const QString &secret) {
    if (secret.isEmpty()) {
        emit quickConnectFailed("NO SECRET");
        return;
    }

    QUrl url(m_quickConnectServerUrl + "/QuickConnect/Connect?secret=" + secret);
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    authorize(req, QString());

    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors) {
        for (const QSslError &e : errors)
            qDebug("[JellyfinBackend] QC SSL error (ignored): %s", qPrintable(e.errorString()));
        QList<QSslError> allowed = expectedLanSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, secret]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();

        // A poll that didn't reach the server (the network a moment away, a
        // timeout) is not the code's end: the next poll asks again, as Plex's
        // PIN does. A status from the server is its answer.
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            qWarning("[JellyfinBackend] Quick Connect poll didn't get through: %s", qPrintable(serverlog::why(reply)));
            return;
        }
        if (status >= 400) {
            // 404 means secret expired or never existed
            if (status == 404)
                emit quickConnectFailed("CODE EXPIRED — RETRY");
            else
                emit quickConnectFailed("SERVER ERROR (HTTP " + QString::number(status) + ")");
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(body).object();
        if (data["Authenticated"].toBool()) {
            emit quickConnectApproved();
        }
        // else: still waiting — QML Timer continues polling
    });
}

void JellyfinBackend::quick_connect_authenticate(const QString &secret) {
    if (secret.isEmpty()) {
        emit errorOccurred("NO SECRET");
        return;
    }

    QUrl url(m_quickConnectServerUrl + "/Users/AuthenticateWithQuickConnect");
    QJsonObject body;
    body["Secret"] = secret;
    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    authorize(req, QString());

    auto *reply = m_nam->post(req, payload);
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors) {
        for (const QSslError &e : errors)
            qDebug("[JellyfinBackend] QC SSL error (ignored): %s", qPrintable(e.errorString()));
        QList<QSslError> allowed = expectedLanSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();

        // As the start above: the server's refusal by its status, then an
        // exchange that got no such answer, in the network's words.
        if (status >= 400) {
            qWarning("[JellyfinBackend] QC Auth HTTP %d", status);
            emit errorOccurred("AUTH FAILED (HTTP " + QString::number(status) + ")");
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("AUTH FAILED: " + serverlog::why(reply));
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(body).object();
        QString token = data["AccessToken"].toString();
        QJsonObject user = data["User"].toObject();

        if (token.isEmpty() || user["Id"].toString().isEmpty()) {
            emit errorOccurred("INVALID AUTH RESPONSE");
            return;
        }

        m_serverUrl   = m_quickConnectServerUrl;
        m_accessToken = token;
        m_userId      = user["Id"].toString();
        m_userName    = user["Name"].toString();
        m_serverName  = m_quickConnectServerUrl; // temp: fetch real name below
        m_quickConnectSecret.clear();

        fetchServerName();
        saveAuthState();
        // The server's URL goes in the module's settings from the module's
        // Root.qml, as it hears authStateChanged (get_server_url()).

        probeCapabilities();
        notifyAuthState();
    });
}

void JellyfinBackend::quick_connect_cancel() {
    if (m_quickConnectSecret.isEmpty())
        return;
    QUrl url(m_quickConnectServerUrl + "/QuickConnect/Connect?secret=" + m_quickConnectSecret);
    QNetworkRequest req(url);
    m_nam->deleteResource(req);
    m_quickConnectSecret.clear();
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void JellyfinBackend::get_playback_url(const QString &itemId, const QString &mediaSourceId,
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
    const int maxBitrate = videoQualityBitrate();
    const int maxHeight  = videoQualityMaxHeight();

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
    // Hardcoding -1 here caused newer Jellyfin servers to strip sub tracks
    // from the stream, breaking both --sid for image subs and sidecar URLs.
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
        // Direct play serves the original file whole and mpv renders embedded
        // subtitles itself. Advertise Embed (not Encode) so the server doesn't
        // force a transcode just to burn in / convert the selected subtitle.
        auto addProfile = [&](const char *fmt, const char *method) {
            QJsonObject s;
            s["Format"] = QString::fromLatin1(fmt);
            s["Method"] = QString::fromLatin1(method);
            subtitleProfiles.append(s);
        };
        // Text formats can also arrive as a sidecar file sitting next to the video
        // ("… - External" tracks). Those can't be embedded into a static stream, so
        // advertising Embed alone made the server deny direct play entirely
        // (SubtitleCodecNotSupported) and fall back to a burn-in transcode. External
        // tells it to keep direct play and serve the sub separately — which is what
        // the app already does with them: every text sub is fetched from
        // /Videos/{id}/{src}/Subtitles/{idx}/Stream.{ext} and passed to mpv as a
        // --sub-file (see the subUrl built in formatItem + buildSubArgs in Player.qml).
        auto addText = [&](const char *fmt) {
            addProfile(fmt, "External");
            addProfile(fmt, "Embed");
        };
        addText("subrip");
        addText("srt");
        addText("ass");
        addText("ssa");
        addText("vtt");
        addText("webvtt");
        addText("mov_text");
        // Image subs have no text sidecar for mpv to load, so they can only come
        // from the embedded stream (selected with --sid).
        addProfile("pgssub", "Embed");
        addProfile("dvbsub", "Embed");
        addProfile("dvdsub", "Embed");
    } else {
        // Transcode: burn the selected subtitle into the video (like the Plex
        // module). Soft HLS subtitle renditions are unreliable in mpv — they
        // report as selected but frequently never render, especially after a
        // seek — so we always burn here. The server bakes SubtitleMethod=Encode
        // into TranscodingUrl from this profile + the body's SubtitleStreamIndex.
        auto addBurnin = [&](const char *fmt) {
            QJsonObject s;
            s["Format"] = QString::fromLatin1(fmt);
            s["Method"] = QStringLiteral("Encode");
            subtitleProfiles.append(s);
        };
        addBurnin("subrip");
        addBurnin("srt");
        addBurnin("ass");
        addBurnin("ssa");
        addBurnin("vtt");
        addBurnin("mov_text");
        addBurnin("pgssub");
        addBurnin("dvbsub");
        addBurnin("dvdsub");
    }
    profile["SubtitleProfiles"] = subtitleProfiles;
    QJsonArray directPlayProfiles;
    if (directPlay) {
        // mpv plays virtually anything, so advertise a match-all profile —
        // omitted Container/VideoCodec/AudioCodec fields match every value in
        // Jellyfin's profile matcher. A codec whitelist here silently forced
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
            qWarning("[Jellyfin] direct play denied (SupportsDirectPlay=%d SupportsDirectStream=%d): %s",
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

        // Build the full URL, then strip any api_key from the server-generated
        // TranscodingUrl — the Authorization header (passed via --http-header-fields
        // to mpv) covers authentication. When the user selected OFF, also strip
        // any SubtitleStreamIndex and SubtitleMethod the server may have added
        // from default metadata.
        QUrl parsedUrl(m_serverUrl + transcodeUrl);
        {
            QUrlQuery q(parsedUrl);
            const auto items = q.queryItems();
            for (const auto &kv : items) {
                if (kv.first.compare(QLatin1String("api_key"), Qt::CaseInsensitive) == 0 ||
                    kv.first.compare(QLatin1String("apikey"),  Qt::CaseInsensitive) == 0)
                    q.removeAllQueryItems(kv.first);
            }
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

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void JellyfinBackend::getVideoQualities() {
    EmbyApiBackend::getVideoQualities();

    // The settings page asks for this row's choices each time it opens, and
    // for nothing that says what the server can do: tell it here, or Intro
    // Skip and Credit Skip stay "Not offered by the server" on every server.
    probeCapabilities();
}

void JellyfinBackend::probeCapabilities() {
    if (!has_auth()) return;

    // Already probed: re-emit cached state. This is important because
    // ModuleSettings.qml is destroyed/recreated on navigation and the
    // one-shot dynamicOptionsReady signal may have been missed.
    if (m_capabilitiesProbed) {
        if (m_hasCapability)
            emit dynamicOptionsReady("_capabilities",
                QVariantList{QString("mediasegments")});
        else
            emit dynamicOptionsReady("_capabilities", QVariantList{});
        return;
    }

    // First probe: use a null GUID — if the MediaSegments route exists
    // (plugin installed), the server returns a non-404 HTTP response.
    // If the route doesn't exist, ASP.NET returns 404.
    //
    // The non-404 on capable servers is because Jellyfin's GetItemById
    // throws on an empty GUID (→ 400/500) before its item-not-found 404
    // path is reached. If a future Jellyfin returns plain 404 for the
    // empty GUID, this probe reports "no capability" and the skip settings
    // stay hidden; switch to a /System/Info/Public version check (the
    // MediaSegments API is core since 10.10) if that ever happens.
    QUrl url(m_serverUrl + "/MediaSegments/00000000-0000-0000-0000-000000000000");
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        // fetchSegments may have handled the probe while we were waiting
        if (m_capabilitiesProbed) {
            // fetchSegments already emitted — sync our cached flag
            // m_hasCapability was already set by fetchSegments
            return;
        }

        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200) {
            // Got a definitive HTTP response from the server
            m_capabilitiesProbed = true;
            m_hasCapability = (status != 404);
            if (m_hasCapability) {
                emit dynamicOptionsReady("_capabilities",
                    QVariantList{QString("mediasegments")});
            } else {
                emit dynamicOptionsReady("_capabilities", QVariantList{});
            }
        }
        // Network error (status == 0): leave m_capabilitiesProbed false so
        // fetchSegments can retry with a real item ID
    });
}

void JellyfinBackend::fetchSegments(const QString &itemId) {
    QUrl url(m_serverUrl + "/MediaSegments/" + itemId);
    auto *reply = serverGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();

        // One-shot capability probe on first call (fallback if
        // probeCapabilities failed or was never called). Like
        // probeCapabilities, only latch on a definitive HTTP response —
        // a transient network error (status 0) must not permanently mark
        // the server as lacking the capability.
        if (!m_capabilitiesProbed) {
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 200) {
                m_capabilitiesProbed = true;
                m_hasCapability = (status != 404);
                if (m_hasCapability) {
                    emit dynamicOptionsReady("_capabilities",
                        QVariantList{QString("mediasegments")});
                } else {
                    emit dynamicOptionsReady("_capabilities", QVariantList{});
                    return;  // no segments to process, server doesn't support it
                }
            }
        }

        // Parse segments from the response
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonArray items = root["Items"].toArray();

        QVariantList segments;
        for (const QJsonValue &val : items) {
            QJsonObject item = val.toObject();
            QString type = item["Type"].toString();
            // Only include Intro and Outro segments
            if (type != "Intro" && type != "Outro") continue;

            QVariantMap seg;
            seg["type"]    = type;                                  // "Intro" or "Outro"
            seg["startMs"] = item["StartTicks"].toDouble() / 10000.0;  // ticks → ms
            seg["endMs"]   = item["EndTicks"].toDouble()   / 10000.0;
            segments.append(seg);
        }

        emit segmentsReady(itemId, segments);
    });
}
