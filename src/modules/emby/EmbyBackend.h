#pragma once
#include <QHash>
#include <QString>

#include "../../util/EmbyApiBackend.h"

class EmbyBackend : public EmbyApiBackend {
    Q_OBJECT
public:
    explicit EmbyBackend(const QString &dataRoot, AppCore *appCore = nullptr, QObject *parent = nullptr);

    // Username/password sign-in — direct to a single server.
    Q_INVOKABLE void authenticate(const QString &serverUrl, const QString &username, const QString &password);

    // Emby Connect — cloud account linking (Emby's equivalent of a "connect
    // once, pick your server" flow). Three steps: authenticate against
    // connect.emby.media, list the account's servers, then exchange the chosen
    // server's access key for a local server token.
    Q_INVOKABLE void connect_authenticate(const QString &usernameOrEmail, const QString &password);
    Q_INVOKABLE void connect_select_server(const QString &serverUrl, const QString &accessKey);

    // Intro/credit skip — derived from Emby's chapter markers (IntroStart/
    // IntroEnd/CreditsStart). Emits Intro/Outro segments the Player consumes.
    Q_INVOKABLE void fetchSegments(const QString &itemId);

    // The Playlists module's server (MediaServer): see the interface.
    QString streamUrl(const QString &itemId) const override;

signals:
    // Emby Connect: emitted with the list of servers on the account when more
    // than one is available so the UI can present a picker. Each entry is a map
    // of {name, address, localAddress, remoteAddress, accessKey, systemId}.
    void connectServersReady(const QVariantList &servers);
    void connectFailed(const QString &message);

private:
    friend class MediaServersTest;

    QString m_connectUserId;      // Emby Connect account user id (cloud)
    QString m_connectAccessToken; // Emby Connect account token (cloud)

    // Emby reads the token from its X-Emby-Authorization header, and from
    // X-Emby-Token.
    void authorize(QNetworkRequest &request, const QString &token) const override;
    // Emby authenticates a sidecar's and a stream's request by api_key.
    bool tokenInUrls() const override { return true; }
    // The Emby Connect account forgotten with the sign-in.
    void clearAuthState() override;
    // The device's registration dropped with the token.
    void revokeToken() override;
    bool resumeNeedsMediaTypes() const override { return true; }
    // The profile with its own bitrate cap, which Emby's needs.
    QJsonObject deviceProfile(bool directPlay, int maxBitrate) const override;
    // Text subtitles as sidecars always, image ones embedded for Direct Play
    // and burnt in for a transcode.
    QJsonArray subtitleProfiles(bool directPlay) const override;
    // A Dolby Vision source, which mpv can't show, transcoded to SDR.
    bool transcodeInstead(const QJsonObject &source, const QString &itemId, const QString &mediaSourceId,
                          int audioStreamIndex, int subtitleStreamIndex) override;

    // Emby Connect helpers (step 2 of the flow, and post-exchange persistence).
    void fetchConnectServers();
    void persistConnectAuthAndFinish();
    // Step 3, at one address of the server: its access key exchanged for a
    // local token at serverUrl, and at fallbackUrl (when there is one) should
    // serverUrl give none.
    void exchangeConnectKey(const QString &serverUrl, const QString &accessKey,
                            const QString &fallbackUrl);
    // The remote address of each server Emby Connect listed last with a LAN
    // address too, by its access key.
    QHash<QString, QString> m_connectRemotes;
};
