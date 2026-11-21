#pragma once
#include <QObject>
#include <QHash>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <functional>

#include "../modules/playlists/MediaServer.h"
#include "KeptLists.h"

class AppCore;

// What Jellyfin's and Emby's backends share. The two servers speak one API
// (Emby's, which Jellyfin was made from), so the sign-in kept between runs,
// the requests, the browsing, the playback reports and the settings' choices
// are written once, here. JellyfinBackend and EmbyBackend derive from it with
// their module's id, and add what is their server's own: the header it reads
// the token from, the sign-in, and what they override below.
class EmbyApiBackend : public QObject, public MediaServer {
    Q_OBJECT
public:
    // Auth
    Q_INVOKABLE bool has_auth() const;
    Q_INVOKABLE QString get_server_name();
    Q_INVOKABLE QString get_user_name();
    // The server signed in to (Root.qml keeps it in the module's settings).
    Q_INVOKABLE QString get_server_url() const { return m_serverUrl; }
    Q_INVOKABLE QString get_auth_state();
    Q_INVOKABLE void check_auth();
    Q_INVOKABLE void logout();

    // Browse
    Q_INVOKABLE void load_libraries();
    Q_INVOKABLE void load_items(const QString &parentId, const QString &includeTypes, const QString &sortBy);
    Q_INVOKABLE void load_item_detail(const QString &itemId);
    Q_INVOKABLE void load_boxset_children(const QString &parentId);
    Q_INVOKABLE void load_folder_children(const QString &parentId);
    Q_INVOKABLE void load_seasons(const QString &seriesId);
    Q_INVOKABLE void load_episodes(const QString &seriesId, const QString &seasonId);
    Q_INVOKABLE void load_continue_watching();
    Q_INVOKABLE void load_up_next();
    Q_INVOKABLE void load_series_next_up(const QString &seriesId);

    // Playback
    // The URL mpv plays the item from, told by streamUrlReady: the original
    // file where Video Quality is Direct Play and the server lets it be
    // played whole, else a transcode (always, with forceTranscode: the retry
    // after a direct play that failed).
    Q_INVOKABLE void get_playback_url(const QString &itemId, const QString &mediaSourceId,
                                       int audioStreamIndex, int subtitleStreamIndex,
                                       bool forceTranscode = false);
    // Forgets the stream a view asked for and no longer waits for (its launch
    // given up with back): get_playback_url answers only the last one asked
    // for, so a reply that comes late neither starts in a view that didn't ask
    // nor reports a playback the server would show as playing.
    Q_INVOKABLE void cancel_playback_url();
    Q_INVOKABLE void load_next_episode(const QString &currentItemId);
    // Where the item has got to, as it plays (MediaServerPlayer, every ten
    // seconds). Always as playing (IsPaused false): the player doesn't follow
    // whether mpv is paused.
    Q_INVOKABLE void update_playback_progress(const QString &itemId, const QString &mediaSourceId, qint64 positionTicks);
    Q_INVOKABLE void report_playback_stopped(const QString &itemId, const QString &mediaSourceId, qint64 positionTicks, bool failed = false);

    // URL helpers for QML
    Q_INVOKABLE QString get_access_token() const { return m_accessToken; }

    // The Playlists module's server (MediaServer): see the interface.
    bool signedIn() const override { return has_auth(); }
    QNetworkRequest downloadRequest(const QString &itemId) const override;
    void browse(const QString &parentId, QObject *context,
                std::function<void(bool ok, const QVariantList &items)> done) override;

    // Settings
    Q_INVOKABLE void getLibraries();
    Q_INVOKABLE virtual void getVideoQualities();
    Q_INVOKABLE void get_resume_playback_options();
    // SIGN IN's value: dynamicOptionsReady("sign_in", [{ id, label }]), the
    // user signed in as, or [] while no one is.
    Q_INVOKABLE void get_account();
    Q_INVOKABLE void load_server_preferences();

    Q_INVOKABLE int get_last_audio_lang_idx() const;
    Q_INVOKABLE int get_last_sub_lang_idx() const;
    Q_INVOKABLE void set_last_track_langs(const QString &audioLang, const QString &subLang,
                                           int audioLangIdx = -1, int subLangIdx = -1);

signals:
    void authStateChanged();
    void librariesLoaded(const QVariant &libraries);
    void itemsLoaded(const QVariant &items);
    void itemLoaded(const QVariant &detail);
    void boxsetChildrenLoaded(const QVariant &children);
    void folderChildrenLoaded(const QVariant &children);
    void seasonsLoaded(const QVariant &seasons);
    void episodesLoaded(const QVariant &episodes);
    void continueWatchingLoaded(const QVariant &items);
    void upNextLoaded(const QVariant &items);
    // Resume-or-next-unwatched episode for a series (empty map if none)
    void seriesNextUpReady(const QVariantMap &detail);
    // The stream get_playback_url asked for: its URL, how it plays
    // ("DirectPlay" or "Transcode"), and whether a text subtitle comes as a
    // file beside it (its subUrl, handed to mpv) rather than burnt into the
    // picture, as subtitleProfiles() asked the server for it.
    void streamUrlReady(const QString &url, const QString &playMethod, bool subtitlesBeside);
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    void segmentsReady(const QString &itemId, const QVariantList &segments);
    void errorOccurred(const QString &message);

    // Emitted when server-side language preferences are loaded
    void serverLanguagePreferencesReady(const QString &audioLanguage, const QString &subtitleLanguage, const QString &subtitleMode);

    // Emitted with the next episode's full detail (empty map if none)
    void nextEpisodeReady(const QVariantMap &detail);

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

protected:
    // moduleId is the module's (com.osdos.jellyfin), whose settings are
    // read through appCore; tag names the server in the log (Jellyfin);
    // authFile is where the sign-in is kept in dataRoot (jellyfin_auth.json).
    EmbyApiBackend(const QString &moduleId, const char *tag, const QString &authFile,
                   const QString &dataRoot, AppCore *appCore, QObject *parent);

    // The headers the server reads the token from, token empty for a request
    // made before there is one (a sign-in's).
    virtual void authorize(QNetworkRequest &request, const QString &token) const = 0;
    // The MediaBrowser authorization both servers read, in their header for
    // it: this app on this device, and the token where there is one.
    QString authorization(const QString &token) const;
    // Whether the URLs mpv is handed (a subtitle's sidecar, the stream) carry
    // the token in their query (Emby's api_key): mpv fetches them itself,
    // with only the Jellyfin-style Authorization header. urlToken() is the
    // token for them, empty where the header does.
    virtual bool tokenInUrls() const { return false; }
    QString urlToken() const { return tokenInUrls() ? m_accessToken : QString(); }

    QNetworkRequest serverRequest(const QUrl &url) const;
    QNetworkReply *serverGet(const QUrl &url);
    QNetworkReply *serverPost(const QUrl &url, const QByteArray &body);

    // A self-signed LAN server's certificate taken for a reply to the
    // configured server (its host read as the request is issued), or to
    // serverHost.
    void ignoreSslErrors(QNetworkReply *reply) const;
    static void ignoreSslErrors(QNetworkReply *reply, const QString &serverHost);

    static QString normalizeServerUrl(const QString &url);

    void notifyAuthState();
    void loadAuthState();
    void saveAuthState();
    // The sign-in forgotten, its file too, the tracks its user chose last,
    // and a new device id made for the next: a backend forgets what else it
    // knew of the server.
    virtual void clearAuthState();
    // The token revoked on the server as the user signs out (logout()),
    // while it is still known: /Sessions/Logout.
    virtual void revokeToken();
    // The stored token found good by check_auth().
    virtual void tokenChecked() {}
    // The server's own name, for the header, asked once signed in and kept
    // with the sign-in as it comes.
    void fetchServerName();

    // Whether Continue Watching (Resume) is asked for videos by name: Emby's
    // returns an empty list (HTTP 200, TotalRecordCount 0) without a
    // media-type or item-type filter, however many resume points exist.
    // Jellyfin needs no such filter.
    virtual bool resumeNeedsMediaTypes() const { return false; }
    // The order a series' episodes are asked for in (sortBy), none for the
    // server's own: Emby accepts sortBy but has no aired-order value
    // ("AiredEpisodeOrder", Jellyfin's, makes it 500 with a SQLiteException),
    // and lists them in index order itself.
    virtual QString episodeOrder() const { return QString(); }

    // Lightweight "does this list have any items?" probe (GET with limit=1).
    void probeHasItems(const QUrl &url, std::function<void(bool)> cb);
    void fetchNextEpisode(const QString &seriesId, const QString &itemId, int currentSeason, int currentIndex,
                          bool fromItem);

    // An item's row, its subtitles' URLs on serverUrl, with urlToken where
    // there is one: of the item alone, so a list can be read away from the
    // screen's thread (loadList).
    static QVariantMap formatItem(const QJsonObject &item, const QString &serverUrl,
                                  const QString &urlToken);
    QVariantMap formatItem(const QJsonObject &item) const {
        return formatItem(item, m_serverUrl, urlToken());
    }

    // A list the list view asked for (Items.qml), told by answer: a library's
    // items, a folder's or a box set's. Each one asked for supersedes the
    // last, the user having moved on: a reply to an older one is dropped, not
    // shown in the view after it. read makes the rows of the reply, away from
    // the screen's thread (a whole library is megabytes of JSON). A list given
    // a keep key is kept a while (KeptLists), so going back to it (from a
    // film's detail screen) shows it without asking the server again; a
    // playback ending forgets them all, its played state having changed.
    void loadList(void (EmbyApiBackend::*answer)(const QVariant &), const QString &keep,
                  const QUrl &url, const QString &failure,
                  std::function<QVariantList(const QByteArray &)> read);

    // The playback get_playback_url() resolved, told to the server as it starts.
    void report_playback_start(const QString &itemId, const QString &mediaSourceId, const QString &audioStreamId,
                               const QString &subtitleStreamId);

    // get_playback_url(), with sdrCap: an otherwise uncapped transcode held
    // to 1080p and 20 Mbps (Emby's Dolby Vision source, transcoded to SDR).
    void requestPlayback(const QString &itemId, const QString &mediaSourceId, int audioStreamIndex,
                         int subtitleStreamIndex, bool forceTranscode, bool sdrCap);
    // The device profile the PlaybackInfo request sends: an HLS transcode,
    // and for Direct Play a profile that matches anything mpv is given, the
    // subtitles as the server delivers them (subtitleProfiles()). maxBitrate
    // is the request's cap (0 for none), for a server that wants it in the
    // profile too (Emby).
    virtual QJsonObject deviceProfile(bool directPlay, int maxBitrate) const;
    // How each subtitle format reaches mpv, for Direct Play or a transcode:
    // the server's own (Jellyfin's and Emby's differ).
    virtual QJsonArray subtitleProfiles(bool directPlay) const = 0;
    // Whether those profiles have the server send a text subtitle as a file
    // beside the stream (External), for streamUrlReady.
    bool subtitlesBeside(bool directPlay) const;
    // The source PlaybackInfo answered with for Direct Play, before it is
    // played: true where the backend has asked for a transcode of it
    // instead (Emby's Dolby Vision sources), and it is not played.
    virtual bool transcodeInstead(const QJsonObject &source, const QString &itemId,
                                  const QString &mediaSourceId, int audioStreamIndex,
                                  int subtitleStreamIndex);
    // The URL mpv plays the original file from (Direct Play), with the
    // session reported with and urlToken where there is one.
    static QString directStreamUrl(const QString &serverUrl, const QString &itemId,
                                   const QString &mediaSourceId, const QString &playSessionId,
                                   const QString &urlToken);
    // The URL mpv plays the transcode from, of the TranscodingUrl the server
    // answered with: on serverUrl; the token taken out of it where the header
    // carries it (tokenInUrl false), else kept, and token added where the
    // server left it out; the subtitle the server took from the item's
    // defaults taken out where the user chose none (subtitleStreamIndex -1);
    // its PlaySessionId the one reported with; and held to maxHeight, where
    // there is one (the server leaves it out at times).
    static QString rewriteTranscodeUrl(const QString &serverUrl, const QString &transcodeUrl,
                                       const QString &playSessionId, int subtitleStreamIndex,
                                       int maxHeight, bool tokenInUrl, const QString &token);

    // One of the module's settings, as AppCore has it (config.json, read
    // again only once it has changed, with what is held back from it); as
    // config.json has it for a backend made without AppCore (a test's).
    QVariant setting(const QString &key) const;
    // Settings → Video Quality: auto (Direct Play), 1080p, 720p, 576p or 480p.
    QString videoQuality() const;
    // A quality's cap on the bitrate, 0 for none (Direct Play's on Emby).
    virtual int qualityBitrate(const QString &quality) const;
    // A quality's cap on the height, 0 for none (Direct Play).
    static int qualityMaxHeight(const QString &quality);

    const QString m_moduleId;
    const char *const m_tag;
    const QString m_authFile;
    QString m_dataRoot;
    AppCore *m_appCore = nullptr;
    QNetworkAccessManager *m_nam;

    QString m_serverUrl;
    QString m_accessToken;
    QString m_userId;
    QString m_userName;
    QString m_serverName;
    QString m_deviceId;
    QString m_currentPlaySessionId;
    QString m_currentPlayMethod; // "DirectPlay" or "Transcode" — for /Sessions reporting
    QString m_lastAudioLang;
    QString m_lastSubLang;
    int m_lastAudioLangIdx = -1;
    int m_lastSubLangIdx = -1;
    // Last auth state broadcast to QML, so notifyAuthState() can suppress
    // re-announcing an unchanged one.
    QString m_lastAuthState;
    // The stream asked for last (get_playback_url): a reply to one asked for
    // before it, or given up with cancel_playback_url, is dropped.
    int m_playbackRequest = 0;
    // A show's seasons, a season's episodes and a show's next episode asked
    // for last (load_seasons, load_episodes, load_series_next_up): one
    // answering after another was asked for is dropped, as Plex's are, so a
    // show or season left for another never shows up in its place, and a
    // show's PLAY never opens another show's episode.
    int m_seasonsRequest = 0;
    int m_episodesRequest = 0;
    int m_nextUpRequest = 0;

    KeptLists m_keptLists;
    int m_listRequest = 0;

private:
    friend class MediaServersTest;
};
