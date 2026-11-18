#pragma once
#include <QString>
#include <QUrl>
#include <functional>

#include "../../util/EmbyApiBackend.h"

class JellyfinBackend : public EmbyApiBackend {
    Q_OBJECT
public:
    explicit JellyfinBackend(const QString &dataRoot, QObject *parent = nullptr);

    // Auth
    Q_INVOKABLE void check_auth();
    Q_INVOKABLE void logout();

    // Quick Connect (no password needed — user approves on another device)
    Q_INVOKABLE void quick_connect_initiate(const QString &serverUrl);
    Q_INVOKABLE void quick_connect_poll(const QString &secret);
    Q_INVOKABLE void quick_connect_authenticate(const QString &secret);
    Q_INVOKABLE void quick_connect_cancel();

    // Browse
    Q_INVOKABLE void load_libraries();
    Q_INVOKABLE void load_episodes(const QString &seriesId, const QString &seasonId);
    Q_INVOKABLE void load_continue_watching();

    // Playback
    Q_INVOKABLE void get_playback_url(const QString &itemId, const QString &mediaSourceId,
                                       int audioStreamIndex, int subtitleStreamIndex,
                                       bool forceTranscode = false);
    Q_INVOKABLE void load_next_episode(const QString &currentItemId);

    // Intro/outro skip — MediaSegments API
    Q_INVOKABLE void fetchSegments(const QString &itemId);

    // The Playlists module's server (MediaServer): see the interface.
    QString streamUrl(const QString &itemId) const override;

    // Settings
    Q_INVOKABLE void getVideoQualities();

signals:
    // Quick Connect signals
    void quickConnectCodeReady(const QString &code, const QString &secret);
    void quickConnectApproved();
    void quickConnectFailed(const QString &message);

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

private:
    friend class MediaServersTest;

    QString m_quickConnectSecret;
    QString m_quickConnectServerUrl;
    bool m_capabilitiesProbed = false;
    bool m_hasCapability = false;

    // Jellyfin reads the token from its Authorization header.
    void authorize(QNetworkRequest &request, const QString &token) const override;

    // Lightweight "does this list have any items?" probe (GET with limit=1).
    void probeHasItems(const QUrl &url, std::function<void(bool)> cb);

    void clearAuthState();
    void fetchNextEpisode(const QString &seriesId, const QString &itemId, int currentSeason, int currentIndex,
                          bool fromItem);

    // Whether the server has the MediaSegments API (Intro Skip and Credit
    // Skip), asked once a sign-in and told to the settings page.
    void probeCapabilities();

    int videoQualityBitrate() const;
};
