#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <functional>

class QProcess;

class AppCore;
class DisplayHandoff;
class WebPlayerBackend;

// Backend for the YouTube module (V1 "feed" approach — no account needed).
//
// The user lists channel IDs (one per line) in <dataRoot>/youtube_subscriptions.txt.
// Video lists come from each channel's official RSS feed (titles, exact publish
// dates, channel name — ~15 newest videos), fetched unauthenticated, four at a
// time (the channel opened first). Results are
// cached in memory per channel for the session (kCacheTtlMs TTL), so the first
// entry into Subscriptions or Channels fills the cache for every other view.
//
// Playlists come from <dataRoot>/youtube_playlists.txt (one playlist URL or ID
// per line, optional "My Name | <url>" display-name prefix). RSS feeds for
// playlists stop at 15 entries, so playlist contents are fetched by spawning
// yt-dlp --flat-playlist instead (async QProcess, same session cache TTL).
//
// Two user files besides subscriptions/playlists:
//   youtube_history.json     — watch history + resume positions, keyed by videoId
//   youtube_watch_later.json — ordered saved-video list (newest first)
//
// The module browses all of it, and YouTube's search (yt-dlp ytsearch), as one
// tree (TreeBrowser) through listing().
//
// An account is optional. SIGN IN (the module's settings) opens Google's
// sign-in in Chromium, in a profile of its own (browser, a WebPlayerBackend);
// from then on every yt-dlp run here and in mpv reads the sign-in from that
// profile (--cookies-from-browser), so YouTube sees the account: fewer bot
// checks, and age-restricted videos play. SIGN OUT deletes the profile.
class YouTubeBackend : public QObject {
    Q_OBJECT
    // What stands in the way of browsing, when anything does; "" otherwise.
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
    // SIGN IN's browser (SignIn.qml opens Google's sign-in with it).
    Q_PROPERTY(QObject *browser READ browser CONSTANT)
public:
    explicit YouTubeBackend(const QString &appRoot, const QString &dataRoot, AppCore *appCore,
                            DisplayHandoff *handoff, QObject *parent = nullptr);

    QObject *browser() const;

    // manifest: sign_out (action). Deletes SIGN IN's browser profile, and with
    // it the sign-in yt-dlp reads.
    Q_INVOKABLE void signOut();

    // The yt-dlp format the ADVANCED settings ask for: at most the
    // playback_resolution's height (240p to 2160p, unknown → 480p) and, with
    // max_frame_rate "30", 30 fps; H.264 first (video_codec "H.264", which the
    // Pi decodes in hardware) or whatever looks best ("Any"); the audio track
    // in audio_language ("original": the one the video was made in). Each
    // falls back to what the video has, down to its best single file.
    Q_INVOKABLE QString ytdlFormat(const QString &resolution, const QString &codec,
                                   const QString &maxFrameRate, const QString &audioLanguage) const;

    // mpv's arguments for a video from the ADVANCED settings, given as
    // { resolution, codec, maxFrameRate, audioLanguage, subtitles,
    // subtitleLanguage, speed }: the format above, the subtitles yt-dlp is to
    // fetch ("On", or "With Auto" for the automatic captions too) and the
    // speed ("1.25x"), and the account once signed in. Player.qml selects the
    // subtitles (--slang).
    Q_INVOKABLE QStringList playbackArgs(const QVariantMap &settings) const;

    // The module's ADVANCED settings as playbackArgs() takes them, with
    // their defaults: { resolution, codec, maxFrameRate, audioLanguage,
    // subtitles, subtitleLanguage, speed }. The one place they are read.
    Q_INVOKABLE QVariantMap playbackSettings() const;

    // yt-dlp's options for downloading a video the way it would play (the
    // Playlists module's offline lists): the same format, and the account
    // once signed in. Video and sound come apart above 360p, and only ffmpeg
    // (canMerge) puts them back together: without it, the best file that
    // has both.
    QStringList downloadArgs(bool canMerge) const;

    // ADVANCED's language lists (options_slot), by yt-dlp's language codes.
    Q_INVOKABLE void get_audio_languages();
    Q_INVOKABLE void get_subtitle_languages();

    // Watch history (youtube_history.json). A finished video stays in history
    // with pos 0 (so it lists under RECENTLY WATCHED but never prompts to resume);
    // entries are pruned to the kMaxHistoryItems most recently played.
    Q_INVOKABLE QVariantMap  getSavedPosition(const QString &videoId);
    Q_INVOKABLE void         savePosition(const QString &videoId, int positionMs,
                                          const QString &title, const QString &channelName);
    Q_INVOKABLE QVariantList getHistory() const;   // displayable entries, newest first
    Q_INVOKABLE void         delete_history();     // settings action slot

    // Watch later (youtube_watch_later.json), newest-saved first, manual removal only
    Q_INVOKABLE QVariantList getWatchLater() const;
    Q_INVOKABLE bool         isInWatchLater(const QString &videoId) const;
    Q_INVOKABLE void         addToWatchLater(const QString &videoId, const QString &title,
                                             const QString &channelName);
    Q_INVOKABLE void         removeFromWatchLater(const QString &videoId);
    Q_INVOKABLE void         delete_watch_later(); // settings action slot

    // The module's tree, by path:
    //   home             SEARCH, then SUBSCRIPTIONS, CHANNELS, PLAYLISTS and
    //                    WATCH LATER, each where it has anything (the view
    //                    puts RECENTLY WATCHED and FAVORITES before them)
    //   subscriptions    the subscriptions feed, newest first
    //   channels         the subscribed channels: channel/<id> each
    //   playlists        youtube_playlists.txt's: playlist/<id> each
    //   watchlater
    //   history          what was played, newest first (RECENTLY WATCHED)
    //   search/<words>   YouTube's matches for the words, MORE at the end
    // Videos carry what Player.qml plays (videoId, url, title, channelName)
    // and isShort. An invalid QVariant (undefined in QML) means the entries are
    // on their way, and listingReady(path) follows. A playlist only a branch
    // asks for (preview) isn't fetched: yt-dlp is slow on a Pi.
    Q_INVOKABLE QVariant listing(const QString &path, bool preview = false);
    // The tree's entries for a folder, as the module's browser shows them:
    // RECENTLY WATCHED and FAVORITES ahead of the home folder's, the module's
    // favourites (AppCore's list) for "favorites", listing()'s for the rest,
    // Shorts left out unless DISPLAY SHORTS is on. undefined while a listing
    // is on its way (listingReady follows). The Playlists module browses
    // with it too.
    Q_INVOKABLE QVariant entries(const QString &path, bool preview = false);
    // Loads the next matches of a search onto its end.
    Q_INVOKABLE void     loadMore(const QString &path);
    // What a video's info screen shows: detailsReady(path, { title, facts,
    // summary, rows, complete }) follows at once with what the list knows,
    // and again, complete, with what yt-dlp adds (length, views, the whole
    // description).
    Q_INVOKABLE void     loadDetails(const QVariantMap &video);
    QString problem() const { return m_problem; }
    // The channels' feeds' address: YouTube's, or a stand-in's (a test's).
    void setFeedUrl(const QUrl &url) { m_feedUrl = url; }

signals:
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    void listingReady(const QString &path);
    void detailsReady(const QString &path, const QVariantMap &details);
    void problemChanged();

private:
    struct ChannelEntry {
        QString      channelId;
        QString      channelName;      // from the RSS feed <title>
        QVariantList videos;           // newest first
        qint64       fetchedMs = 0;    // 0 = never fetched successfully
        bool         feedOk    = false;
    };

    struct PlaylistEntry {
        QString      playlistId;
        QString      fileName;         // optional "Name |" override from the file
        QString      fetchedTitle;     // playlist_title reported by yt-dlp
        QVariantList videos;           // playlist order
        qint64       fetchedMs = 0;
        bool         fetchOk   = false;
    };

    struct PlaylistFileRef {
        QString id;
        QString name;                  // empty when the line had no "Name |" prefix
    };

    struct Search {
        QVariantList videos;           // YouTube's order
        int          requested = 0;    // matches asked for so far
        bool         loading   = false;
        bool         exhausted = false;
        qint64       failedMs  = 0;
    };

    QString      historyFilePath() const;
    QVariantMap  loadHistory() const;
    void         saveHistory(const QVariantMap &history);
    QString      watchLaterFilePath() const;
    QVariantList loadWatchLater() const;
    void         saveWatchLater(const QVariantList &list);

    QStringList  readSubscriptionIds() const;
    void         ensureFresh(bool forceRefresh);
    void         spawnNextChannelFetch();
    void         refreshChannel(const QString &channelId);
    void         finishAggregate();
    QVariantList buildFeed() const;
    QVariantList buildChannelList() const;
    QNetworkRequest makeRequest(const QUrl &url) const;

    QVariant     channelListing(const QString &path);
    QVariant     playlistListing(const QString &path, bool preview);
    QVariant     searchListing(const QString &path);
    void         searchPage(const QString &path);
    QVariantList videoEntries(const QVariantList &videos) const;
    void         answerLater(const QStringList &paths);
    QVariantMap  detailsOf(const QVariantMap &video, bool complete) const;
    void         fetchDetails(const QVariantMap &video);
    void         setProblem(const QString &problem);
    // What makes yt-dlp read SIGN IN's sign-in from its browser profile, as
    // the browser left it, on each run: --cookies-from-browser's value, and
    // the option with it. Empty while that browser has kept nothing (never
    // opened, or signed out).
    QString      cookiesFromBrowser() const;
    QStringList  cookieArgs() const;

    QList<PlaylistFileRef> readPlaylistEntries() const;
    void         ensurePlaylistsFresh(bool forceRefresh);
    void         spawnNextPlaylistFetch();
    void         finishPlaylistAggregate();
    // ytdlFormat() from the ADVANCED settings as playbackSettings() gives them.
    QString      ytdlFormat(const QVariantMap &settings) const;
    // Runs yt-dlp with the arguments and hands done the process once it has
    // ended, however that was: finished, killed at the timeout, or never
    // started. The process is freed after.
    void         runYtDlp(const QStringList &args, std::function<void(QProcess *)> done);

    QString m_appRoot;
    QString m_dataRoot;
    AppCore *m_appCore = nullptr;
    QNetworkAccessManager m_nam;
    WebPlayerBackend *m_browser = nullptr;

    QHash<QString, ChannelEntry> m_channels;  // in-memory session cache
    QStringList m_channelOrder;               // channel IDs in file order (deduped)
    QStringList m_channelFetchQueue;          // stale IDs waiting for a request slot
    int m_pendingChannels      = 0;
    int m_activeChannelFetches = 0;
    QUrl m_feedUrl{ QStringLiteral("https://www.youtube.com/feeds/videos.xml") };

    // Playlist mirror of the channel cache/refresh state, fed by yt-dlp
    // subprocesses instead of RSS requests.
    QHash<QString, PlaylistEntry> m_playlists;
    QStringList m_playlistOrder;              // playlist IDs in file order (deduped)
    QStringList m_playlistFetchQueue;         // stale IDs waiting for a process slot
    int m_pendingPlaylists       = 0;
    int m_activePlaylistFetches  = 0;

    // The tree's view of the caches: when each last filled (or failed
    // outright), and the paths waiting for that.
    qint64      m_channelsLoadedMs  = 0;
    qint64      m_channelsFailedMs  = 0;
    QStringList m_channelWaits;
    qint64      m_playlistsLoadedMs = 0;
    qint64      m_playlistsFailedMs = 0;
    QStringList m_playlistWaits;
    QHash<QString, Search> m_searches;        // search/<words> -> matches
    QHash<QString, QVariantMap> m_details;    // videoId -> what yt-dlp told of it
    bool        m_fetchingDetails = false;
    QVariantMap m_detailsNext;                // asked for while a fetch ran
    QString     m_problem;

    static constexpr qint64 kCacheTtlMs      = 15 * 60 * 1000;
    static constexpr int    kMaxFeedItems    = 100;
    static constexpr int    kMaxHistoryItems = 100;
    static constexpr int    kMaxPlaylistItems = 500;             // caps infinite Mix/Radio lists
    static constexpr int    kMaxConcurrentPlaylistFetches = 2;   // yt-dlp is heavy on the Pi
    // Feeds asked for at once: hundreds of subscriptions at a time would
    // run into YouTube's limits.
    static constexpr int    kMaxConcurrentChannelFetches = 4;
    static constexpr int    kPlaylistFetchTimeoutMs = 60000;
    static constexpr int    kSearchPageSize  = 20;
    // A source that failed outright is shown empty this long before the tree
    // may ask again, so a tree refreshing on the failure can't loop on it.
    static constexpr qint64 kRetryAfterMs    = 15000;
};
