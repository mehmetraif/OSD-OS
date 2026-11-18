#pragma once
#include <QString>

#include "../../util/EmbyApiBackend.h"

class JellyfinBackend : public EmbyApiBackend {
    Q_OBJECT
public:
    explicit JellyfinBackend(const QString &dataRoot, QObject *parent = nullptr);

    // Quick Connect (no password needed — user approves on another device)
    Q_INVOKABLE void quick_connect_initiate(const QString &serverUrl);
    Q_INVOKABLE void quick_connect_poll(const QString &secret);
    Q_INVOKABLE void quick_connect_authenticate(const QString &secret);
    Q_INVOKABLE void quick_connect_cancel();

    // Intro/outro skip — MediaSegments API
    Q_INVOKABLE void fetchSegments(const QString &itemId);

    // The Playlists module's server (MediaServer): see the interface.
    QString streamUrl(const QString &itemId) const override;

    // Settings: the choices, and what the server can do (probeCapabilities()).
    void getVideoQualities() override;

signals:
    // Quick Connect signals
    void quickConnectCodeReady(const QString &code, const QString &secret);
    void quickConnectApproved();
    void quickConnectFailed(const QString &message);

private:
    friend class MediaServersTest;

    QString m_quickConnectSecret;
    QString m_quickConnectServerUrl;
    bool m_capabilitiesProbed = false;
    bool m_hasCapability = false;

    // Jellyfin reads the token from its Authorization header.
    void authorize(QNetworkRequest &request, const QString &token) const override;
    // What this server could do says nothing of the next one signed into.
    void clearAuthState() override;
    // A token found good: what its server can do is asked then.
    void tokenChecked() override { probeCapabilities(); }
    QString episodeOrder() const override { return QStringLiteral("AiredEpisodeOrder"); }
    // Text subtitles as sidecars or embedded for Direct Play, every subtitle
    // burnt in for a transcode.
    QJsonArray subtitleProfiles(bool directPlay) const override;
    // Direct Play asks for 1000 Mbps at most.
    int qualityBitrate(const QString &quality) const override;

    // Whether the server has the MediaSegments API (Intro Skip and Credit
    // Skip), asked once a sign-in and told to the settings page.
    void probeCapabilities();
};
