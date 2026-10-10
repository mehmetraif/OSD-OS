#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVariant>

class AppCore;
class LocalFilesBackend;
class MediaServer;
class QProcess;
class ServerDownload;
class YouTubeBackend;

// The Playlists module: lists of videos from other modules, played as one, in
// order or shuffled.
//
// A playlist is ONLINE or OFFLINE. An online one plays each video from where
// it lives: a file from Local Files, a YouTube video through yt-dlp, a
// Jellyfin or Emby item streamed from its server. An offline one plays only
// what is on the device: Local Files' files as they are, and a copy of every
// other video, downloaded once into the download folder (download_folder,
// else Local Files' folder's "Playlists") and shared by every offline
// playlist it is in, so a video is never fetched twice. Only sources that can
// download go on an offline list, and only sources mpv plays go on any (not
// Netflix or Prime Video). The servers are MediaServers (Jellyfin, Emby):
// the module never minds which it has.
//
// Kept in <data>/playlists.json:
//   { "playlists": [ { id, name, kind: "online"|"offline", order: "inorder"|
//                      "shuffle", items: [ { id, module, key, title, source } ],
//                      resume: { itemId, positionMs } } ],
//     "downloads": { key: { path, state: "done"|"failed", reason, bytes } } }
// An item's key names the video whatever list it is on: "local:<path>",
// "youtube:<videoId>", "jellyfin:<itemId>", "emby:<itemId>".
class PlaylistsBackend : public QObject {
    Q_OBJECT
public:
    PlaylistsBackend(const QString &dataRoot, AppCore *appCore, LocalFilesBackend *localFiles,
                     YouTubeBackend *youtube, const QHash<QString, MediaServer *> &servers,
                     QObject *parent = nullptr);
    ~PlaylistsBackend() override;

    // [{ id, name, kind, order, count, ready }]: ready is how many of an
    // offline list's videos are on the device.
    Q_INVOKABLE QVariantList playlists() const;
    // { id, name, kind, order, items: [{ id, module, key, title, source,
    // state, percent, reason }] }, state being "ready", "queued",
    // "downloading", "failed" or "missing" (a file gone, a server signed out
    // of); key names the download (downloadProgress).
    Q_INVOKABLE QVariantMap playlist(const QString &id) const;
    Q_INVOKABLE QString createPlaylist(const QString &name, const QString &kind);
    Q_INVOKABLE void renamePlaylist(const QString &id, const QString &name);
    Q_INVOKABLE void deletePlaylist(const QString &id);
    Q_INVOKABLE void setOrder(const QString &id, const QString &order);

    // Whether a module's videos can go on a list.
    Q_INVOKABLE bool supports(const QString &moduleId) const;
    // Puts an entry as its module has it (a tree's entry, a server's item) on
    // a list: { ok, reason ("unsupported", "duplicate", "unknown"), title,
    // downloading (whether it is now to be downloaded) }.
    Q_INVOKABLE QVariantMap addEntry(const QString &playlistId, const QString &moduleId,
                                     const QVariantMap &entry);
    Q_INVOKABLE void removeItem(const QString &playlistId, const QString &itemId);
    Q_INVOKABLE void moveItem(const QString &playlistId, const QString &itemId, int delta);
    // Tries a list's failed downloads again.
    Q_INVOKABLE void retryDownloads(const QString &playlistId);

    // Writes what plays into an m3u, in the order it plays (shuffled, for a
    // list that is, fromItemId first), and says how: { file, count, youtube
    // (whether yt-dlp is needed), images (whether a still image is on it),
    // startIndex (fromItemId's place, -1 for none), resumeIndex and resumeMs
    // (where a list in order stopped, -1 / 0 for none or when played from
    // fromItemId) }. What can't play (a file gone, a server signed out of, a
    // download not done) is left out.
    Q_INVOKABLE QVariantMap prepare(const QString &playlistId, const QString &fromItemId = QString());
    // Where a list stopped: a place in the m3u of the last prepare().
    Q_INVOKABLE void savePosition(const QString &playlistId, int index, int positionMs);
    Q_INVOKABLE void clearPosition(const QString &playlistId);
    // How far into its video the list stopped (0 for none).
    Q_INVOKABLE int savedPositionMs(const QString &playlistId) const;

    Q_INVOKABLE QString downloadFolder() const;

    // A server's folders for the module's own tree (ADD VIDEOS), as the
    // server lists them for this user (MediaServer::browse): parentId "" for
    // the libraries, "resume" and "nextup" for what is being watched. The
    // backend's items with a name for the tree, or undefined while on their
    // way (when preview, only those already here: no request), then
    // serverListingReady. forgetListings() has them all asked for again.
    Q_INVOKABLE QVariant serverListing(const QString &moduleId, const QString &parentId,
                                       bool preview = false);
    Q_INVOKABLE void forgetListings();

signals:
    void playlistsChanged();
    void serverListingReady(const QString &moduleId, const QString &parentId);
    // A download under way: its percent.
    void downloadProgress(const QString &key, int percent);

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

private:
    quint64 m_flushGeneration = 0;
    // The download under way: yt-dlp's process, or a server's file on its
    // own thread.
    struct Active {
        QString key;
        QPointer<QProcess> process;
        QPointer<ServerDownload> download;
        QString finalPath;
        QString reason;
        int percent = 0;
        // The finished file is on its way to the card (finish()).
        bool flushing = false;
    };

    void load();
    void save() const;
    int indexOf(const QString &id) const;
    QJsonObject itemByKey(const QString &key) const;
    QJsonObject itemFor(const QString &moduleId, const QVariantMap &entry) const;
    QString itemState(const QJsonObject &item, const QString &kind, int *percent,
                      QString *reason) const;
    QString playableUrl(const QJsonObject &item, const QString &kind) const;
    bool referencedOffline(const QString &key) const;
    // Downloads no offline list wants any more: stopped, their files deleted.
    void dropUnreferenced();

    void queueOfflineItems();
    // Whether it is to be downloaded (not when on the device already).
    bool enqueue(const QString &key);
    void startNext();
    void startYouTube(const QString &key, const QJsonObject &item);
    void startServer(const QString &key, const QJsonObject &item);
    // The download under way is over: its file flushed to the card, then
    // recorded (record), or its failure recorded at once.
    void finish(bool ok);
    void record(bool ok);
    // Fails the download under way, on the next tick: finish() ends it, so
    // not from inside whatever started it.
    void failLater(const QString &reason);
    void cancel(const QString &key);
    QString sourceFolder(const QString &name) const;
    // Whether a download can go in the folder; if not, it fails, saying so.
    bool writable(const QString &folder);
    void removePartials(const QString &key) const;
    void removeM3us(const QString &playlistId) const;

    QString m_dataRoot;
    AppCore *m_appCore = nullptr;
    LocalFilesBackend *m_localFiles = nullptr;
    YouTubeBackend *m_youtube = nullptr;
    // By module id.
    QHash<QString, MediaServer *> m_servers;

    QList<QJsonObject> m_playlists;
    QHash<QString, QJsonObject> m_downloads;
    // What the last prepare() put in each list's m3u, by item id.
    QHash<QString, QStringList> m_prepared;
    // Numbers each prepare()'s m3u.
    int m_serial = 0;

    // serverListing()'s, by "<moduleId>|<parentId>", and those on their way.
    QHash<QString, QVariantList> m_listings;
    QSet<QString> m_listingsPending;
    int m_listingsEpoch = 0;

    QStringList m_queue;
    Active m_active;
};
