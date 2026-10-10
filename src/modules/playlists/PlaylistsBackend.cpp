#include "PlaylistsBackend.h"

#include "../../AppCore.h"
#include "../../util/FileNames.h"
#include "../../util/DurableFile.h"
#include "../../util/YtDlpLocator.h"
#include "../local_files/LocalFilesBackend.h"
#include "../youtube/YouTubeBackend.h"
#include "MediaServer.h"
#include "ServerDownload.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <memory>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace {

const QString kModuleId = QStringLiteral("com.osdos.playlists");
const QString kLocalFiles = QStringLiteral("com.osdos.local_files");
const QString kYouTube = QStringLiteral("com.osdos.youtube");

// The prefix of an item's key, by module: a server's is its id's last part
// ("jellyfin:", "emby:").
QString keyPrefix(const QString &moduleId) {
    if (moduleId == kLocalFiles) return QStringLiteral("local:");
    if (moduleId == kYouTube) return QStringLiteral("youtube:");
    return moduleId.section(QLatin1Char('.'), -1) + QLatin1Char(':');
}

// The folder a module's downloads go in, under the download folder.
QString sourceFolderName(const QString &moduleId) {
    if (moduleId == kYouTube) return QStringLiteral("YouTube");
    QString name = moduleId.section(QLatin1Char('.'), -1);
    name[0] = name[0].toUpper();
    return name;
}

// A file's data flushed to the card, then its folder's entry for it: the film
// partition is exFAT, which a power cut mid-write can leave half done. Slow
// on a card (seconds for a film), so never on the app's thread.
void syncFolder(const QString &folder) {
    const QString error = syncPath(folder, true);
    if (!error.isEmpty()) qWarning("[Playlists] folder sync failed: %s", qPrintable(error));
}

// A YouTube video's id, from whichever of its fields an entry has.
QString youtubeId(const QVariantMap &entry) {
    QString id = entry.value(QStringLiteral("videoId")).toString();
    if (!id.isEmpty())
        return id;
    static const QRegularExpression kFromUrl(QStringLiteral("[?&]v=([A-Za-z0-9_-]{6,})"));
    const QRegularExpressionMatch m = kFromUrl.match(entry.value(QStringLiteral("url")).toString());
    if (m.hasMatch())
        return m.captured(1);
    const QString path = entry.value(QStringLiteral("path")).toString();
    if (path.startsWith(QLatin1String("video/")))
        return path.mid(6);
    return {};
}

} // namespace

PlaylistsBackend::PlaylistsBackend(const QString &dataRoot, AppCore *appCore, LocalFilesBackend *localFiles,
                                   YouTubeBackend *youtube, const QHash<QString, MediaServer *> &servers,
                                   QObject *parent)
    : QObject(parent), m_dataRoot(dataRoot), m_appCore(appCore), m_localFiles(localFiles),
      m_youtube(youtube), m_servers(servers) {
    load();
    // What was left to download goes on once the app has settled (and the
    // network, at boot, has had a chance to come up).
    QTimer::singleShot(15000, this, &PlaylistsBackend::queueOfflineItems);
}

PlaylistsBackend::~PlaylistsBackend() {
    // A download cut short is begun again next time; its part file stays for
    // yt-dlp to carry on from.
    if (m_active.process) {
        m_active.process->disconnect(this);
        m_active.process->kill();
        m_active.process->waitForFinished(2000);
    }
    if (m_active.download)
        m_active.download->cancel();
}

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

void PlaylistsBackend::load() {
    QFile file(m_dataRoot + QStringLiteral("/playlists.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &v : root.value(QStringLiteral("playlists")).toArray())
        m_playlists << v.toObject();
    const QJsonObject downloads = root.value(QStringLiteral("downloads")).toObject();
    for (auto it = downloads.begin(); it != downloads.end(); ++it)
        m_downloads.insert(it.key(), it.value().toObject());
}

void PlaylistsBackend::save() const {
    QJsonArray playlists;
    for (const QJsonObject &p : m_playlists)
        playlists << p;
    QJsonObject downloads;
    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it)
        downloads.insert(it.key(), it.value());
    QJsonObject root;
    root.insert(QStringLiteral("playlists"), playlists);
    root.insert(QStringLiteral("downloads"), downloads);
    QDir().mkpath(m_dataRoot);
    QSaveFile file(m_dataRoot + QStringLiteral("/playlists.json"));
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("[Playlists] can't write playlists.json");
        return;
    }
    file.write(QJsonDocument(root).toJson());
    file.commit();
}

int PlaylistsBackend::indexOf(const QString &id) const {
    for (int i = 0; i < m_playlists.size(); ++i)
        if (m_playlists[i].value(QStringLiteral("id")).toString() == id)
            return i;
    return -1;
}

QJsonObject PlaylistsBackend::itemByKey(const QString &key) const {
    for (const QJsonObject &p : m_playlists)
        for (const QJsonValue &v : p.value(QStringLiteral("items")).toArray())
            if (v.toObject().value(QStringLiteral("key")).toString() == key)
                return v.toObject();
    return {};
}

QString PlaylistsBackend::downloadFolder() const {
    const QString chosen = m_appCore
        ? m_appCore->get_setting(kModuleId, QStringLiteral("download_folder")).toString().trimmed()
        : QString();
    if (!chosen.isEmpty())
        return chosen;
    const QString media = m_localFiles ? m_localFiles->mediaRoot() : m_dataRoot + QStringLiteral("/media");
    return QDir(media).filePath(QStringLiteral("Playlists"));
}

QString PlaylistsBackend::sourceFolder(const QString &name) const {
    const QString folder = QDir(downloadFolder()).filePath(name);
    QDir().mkpath(folder);
    return folder;
}

void PlaylistsBackend::onSettingChanged(const QString &moduleId, const QString &key,
                                        const QVariant &value) {
    Q_UNUSED(value)
    // Downloads made so far stay where they are; what comes next goes to the
    // folder now chosen.
    if (moduleId == kModuleId && key == QLatin1String("download_folder"))
        emit playlistsChanged();
}

// ---------------------------------------------------------------------------
// Lists
// ---------------------------------------------------------------------------

QVariantList PlaylistsBackend::playlists() const {
    QVariantList out;
    for (const QJsonObject &p : m_playlists) {
        const QString kind = p.value(QStringLiteral("kind")).toString();
        const QJsonArray items = p.value(QStringLiteral("items")).toArray();
        int ready = 0;
        for (const QJsonValue &v : items) {
            int percent = 0;
            QString reason;
            if (itemState(v.toObject(), kind, &percent, &reason) == QLatin1String("ready"))
                ++ready;
        }
        out << QVariantMap{{QStringLiteral("id"), p.value(QStringLiteral("id")).toString()},
                           {QStringLiteral("name"), p.value(QStringLiteral("name")).toString()},
                           {QStringLiteral("kind"), kind},
                           {QStringLiteral("order"), p.value(QStringLiteral("order")).toString()},
                           {QStringLiteral("count"), int(items.size())},
                           {QStringLiteral("ready"), ready}};
    }
    return out;
}

QVariantMap PlaylistsBackend::playlist(const QString &id) const {
    const int i = indexOf(id);
    if (i < 0)
        return {};
    const QJsonObject p = m_playlists[i];
    const QString kind = p.value(QStringLiteral("kind")).toString();
    QVariantList items;
    for (const QJsonValue &v : p.value(QStringLiteral("items")).toArray()) {
        const QJsonObject item = v.toObject();
        int percent = 0;
        QString reason;
        const QString state = itemState(item, kind, &percent, &reason);
        items << QVariantMap{{QStringLiteral("id"), item.value(QStringLiteral("id")).toString()},
                             {QStringLiteral("module"), item.value(QStringLiteral("module")).toString()},
                             {QStringLiteral("key"), item.value(QStringLiteral("key")).toString()},
                             {QStringLiteral("title"), item.value(QStringLiteral("title")).toString()},
                             {QStringLiteral("source"), item.value(QStringLiteral("source")).toObject().toVariantMap()},
                             {QStringLiteral("state"), state},
                             {QStringLiteral("percent"), percent},
                             {QStringLiteral("reason"), reason}};
    }
    return {{QStringLiteral("id"), id},
            {QStringLiteral("name"), p.value(QStringLiteral("name")).toString()},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("order"), p.value(QStringLiteral("order")).toString()},
            {QStringLiteral("items"), items}};
}

QString PlaylistsBackend::createPlaylist(const QString &name, const QString &kind) {
    QJsonObject p;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.insert(QStringLiteral("id"), id);
    p.insert(QStringLiteral("name"), name.trimmed().isEmpty() ? QStringLiteral("Playlist") : name.trimmed());
    p.insert(QStringLiteral("kind"), kind == QLatin1String("offline") ? QStringLiteral("offline")
                                                                     : QStringLiteral("online"));
    p.insert(QStringLiteral("order"), QStringLiteral("inorder"));
    p.insert(QStringLiteral("items"), QJsonArray());
    m_playlists << p;
    save();
    emit playlistsChanged();
    return id;
}

void PlaylistsBackend::renamePlaylist(const QString &id, const QString &name) {
    const int i = indexOf(id);
    if (i < 0 || name.trimmed().isEmpty())
        return;
    m_playlists[i].insert(QStringLiteral("name"), name.trimmed());
    save();
    emit playlistsChanged();
}

void PlaylistsBackend::deletePlaylist(const QString &id) {
    const int i = indexOf(id);
    if (i < 0)
        return;
    m_playlists.removeAt(i);
    m_prepared.remove(id);
    removeM3us(id);
    dropUnreferenced();
    save();
    emit playlistsChanged();
}

void PlaylistsBackend::setOrder(const QString &id, const QString &order) {
    const int i = indexOf(id);
    if (i < 0)
        return;
    m_playlists[i].insert(QStringLiteral("order"), order == QLatin1String("shuffle") ? QStringLiteral("shuffle")
                                                                                    : QStringLiteral("inorder"));
    save();
    emit playlistsChanged();
}

bool PlaylistsBackend::supports(const QString &moduleId) const {
    // Every source mpv plays here can also be put on the device: Local Files'
    // files are on it already, the others download.
    return moduleId == kLocalFiles || moduleId == kYouTube || m_servers.contains(moduleId);
}

QJsonObject PlaylistsBackend::itemFor(const QString &moduleId, const QVariantMap &entry) const {
    QJsonObject source;
    QString id;
    QString title = entry.value(QStringLiteral("title")).toString();
    if (title.isEmpty())
        title = entry.value(QStringLiteral("name")).toString();
    if (moduleId == kLocalFiles) {
        if (entry.value(QStringLiteral("isFolder")).toBool())
            return {};
        id = entry.value(QStringLiteral("path")).toString();
        source.insert(QStringLiteral("path"), id);
        if (title.isEmpty())
            title = QFileInfo(id).completeBaseName();
    } else if (moduleId == kYouTube) {
        id = youtubeId(entry);
        source.insert(QStringLiteral("videoId"), id);
        source.insert(QStringLiteral("channel"), entry.value(QStringLiteral("channelName")).toString());
    } else if (m_servers.contains(moduleId)) {
        if (entry.value(QStringLiteral("isFolder")).toBool())
            return {};
        id = entry.value(QStringLiteral("itemId")).toString();
        source.insert(QStringLiteral("itemId"), id);
        // An episode: its show, to tell it from another show's "Pilot".
        const QString series = entry.value(QStringLiteral("grandparentTitle")).toString();
        if (entry.value(QStringLiteral("type")).toString() == QLatin1String("episode")
            && !series.isEmpty() && !title.startsWith(series))
            title = series + QStringLiteral(" - ") + title;
    }
    if (id.isEmpty())
        return {};
    QJsonObject item;
    item.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    item.insert(QStringLiteral("module"), moduleId);
    item.insert(QStringLiteral("key"), keyPrefix(moduleId) + id);
    item.insert(QStringLiteral("title"), title.isEmpty() ? id : title);
    item.insert(QStringLiteral("source"), source);
    return item;
}

QVariantMap PlaylistsBackend::addEntry(const QString &playlistId, const QString &moduleId,
                                       const QVariantMap &entry) {
    const int i = indexOf(playlistId);
    if (i < 0)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("reason"), QStringLiteral("unknown")}};
    const QJsonObject item = supports(moduleId) ? itemFor(moduleId, entry) : QJsonObject();
    if (item.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("reason"), QStringLiteral("unsupported")}};

    const QString title = item.value(QStringLiteral("title")).toString();
    QJsonArray items = m_playlists[i].value(QStringLiteral("items")).toArray();
    const QString key = item.value(QStringLiteral("key")).toString();
    for (const QJsonValue &v : items)
        if (v.toObject().value(QStringLiteral("key")).toString() == key)
            return {{QStringLiteral("ok"), false}, {QStringLiteral("reason"), QStringLiteral("duplicate")},
                    {QStringLiteral("title"), title}};
    items << item;
    m_playlists[i].insert(QStringLiteral("items"), items);
    save();
    const bool downloading = m_playlists[i].value(QStringLiteral("kind")).toString() == QLatin1String("offline")
                             && moduleId != kLocalFiles && enqueue(key);
    emit playlistsChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("title"), title},
            {QStringLiteral("downloading"), downloading}};
}

void PlaylistsBackend::removeItem(const QString &playlistId, const QString &itemId) {
    const int i = indexOf(playlistId);
    if (i < 0)
        return;
    QJsonArray items = m_playlists[i].value(QStringLiteral("items")).toArray();
    for (int j = 0; j < items.size(); ++j) {
        if (items[j].toObject().value(QStringLiteral("id")).toString() == itemId) {
            items.removeAt(j);
            break;
        }
    }
    m_playlists[i].insert(QStringLiteral("items"), items);
    dropUnreferenced();
    save();
    emit playlistsChanged();
}

void PlaylistsBackend::moveItem(const QString &playlistId, const QString &itemId, int delta) {
    const int i = indexOf(playlistId);
    if (i < 0)
        return;
    QJsonArray items = m_playlists[i].value(QStringLiteral("items")).toArray();
    for (int j = 0; j < items.size(); ++j) {
        if (items[j].toObject().value(QStringLiteral("id")).toString() != itemId)
            continue;
        const int to = j + delta;
        if (to < 0 || to >= items.size())
            return;
        const QJsonValue moved = items[j];
        items.removeAt(j);
        items.insert(to, moved);
        m_playlists[i].insert(QStringLiteral("items"), items);
        save();
        emit playlistsChanged();
        return;
    }
}

void PlaylistsBackend::retryDownloads(const QString &playlistId) {
    const int i = indexOf(playlistId);
    if (i < 0)
        return;
    for (const QJsonValue &v : m_playlists[i].value(QStringLiteral("items")).toArray()) {
        const QString key = v.toObject().value(QStringLiteral("key")).toString();
        if (m_downloads.value(key).value(QStringLiteral("state")).toString() == QLatin1String("failed"))
            m_downloads.remove(key);
    }
    save();
    queueOfflineItems();
    emit playlistsChanged();
}

// ---------------------------------------------------------------------------
// What plays
// ---------------------------------------------------------------------------

QString PlaylistsBackend::itemState(const QJsonObject &item, const QString &kind, int *percent,
                                    QString *reason) const {
    const QString module = item.value(QStringLiteral("module")).toString();
    const QJsonObject source = item.value(QStringLiteral("source")).toObject();
    if (module == kLocalFiles)
        return QFileInfo::exists(source.value(QStringLiteral("path")).toString()) ? QStringLiteral("ready")
                                                                                  : QStringLiteral("missing");
    if (kind != QLatin1String("offline")) {
        const MediaServer *server = m_servers.value(module);
        if (server && !server->signedIn()) {
            *reason = QStringLiteral("signed out");
            return QStringLiteral("missing");
        }
        return QStringLiteral("ready");
    }
    const QString key = item.value(QStringLiteral("key")).toString();
    if (m_active.key == key) {
        *percent = m_active.percent;
        return QStringLiteral("downloading");
    }
    const QJsonObject download = m_downloads.value(key);
    const QString state = download.value(QStringLiteral("state")).toString();
    if (state == QLatin1String("done") && QFileInfo::exists(download.value(QStringLiteral("path")).toString()))
        return QStringLiteral("ready");
    if (state == QLatin1String("failed")) {
        *reason = download.value(QStringLiteral("reason")).toString();
        return QStringLiteral("failed");
    }
    return QStringLiteral("queued");
}

QString PlaylistsBackend::playableUrl(const QJsonObject &item, const QString &kind) const {
    const QString module = item.value(QStringLiteral("module")).toString();
    const QJsonObject source = item.value(QStringLiteral("source")).toObject();
    if (module == kLocalFiles) {
        const QString path = source.value(QStringLiteral("path")).toString();
        return QFileInfo::exists(path) ? path : QString();
    }
    if (kind == QLatin1String("offline")) {
        const QJsonObject download = m_downloads.value(item.value(QStringLiteral("key")).toString());
        const QString path = download.value(QStringLiteral("path")).toString();
        return download.value(QStringLiteral("state")).toString() == QLatin1String("done") && QFileInfo::exists(path)
                   ? path : QString();
    }
    if (module == kYouTube)
        return QStringLiteral("https://www.youtube.com/watch?v=") + source.value(QStringLiteral("videoId")).toString();
    const MediaServer *server = m_servers.value(module);
    return server && server->signedIn() ? server->streamUrl(source.value(QStringLiteral("itemId")).toString())
                                        : QString();
}

QVariantMap PlaylistsBackend::prepare(const QString &playlistId, const QString &fromItemId) {
    const int i = indexOf(playlistId);
    if (i < 0)
        return {};
    const QJsonObject p = m_playlists[i];
    const QString kind = p.value(QStringLiteral("kind")).toString();
    const bool shuffled = p.value(QStringLiteral("order")).toString() == QLatin1String("shuffle");
    struct Entry {
        QString id;
        QString title;
        QString url;
    };
    QList<Entry> entries;
    bool youtube = false;
    bool images = false;
    for (const QJsonValue &v : p.value(QStringLiteral("items")).toArray()) {
        const QJsonObject item = v.toObject();
        const QString url = playableUrl(item, kind);
        if (url.isEmpty())
            continue;
        const QString module = item.value(QStringLiteral("module")).toString();
        if (kind != QLatin1String("offline") && module == kYouTube)
            youtube = true;
        if (module == kLocalFiles && m_localFiles && m_localFiles->isImage(url))
            images = true;
        // The title mpv's display shows, on one line.
        entries << Entry{item.value(QStringLiteral("id")).toString(),
                         item.value(QStringLiteral("title")).toString().simplified(), url};
    }
    // Shuffled here rather than by mpv, so the m3u's order is the order it
    // plays in and a place in it names a video (savePosition). The video it
    // is played from, if any, first.
    if (shuffled) {
        std::shuffle(entries.begin(), entries.end(), *QRandomGenerator::global());
        for (int j = 0; j < entries.size(); ++j) {
            if (entries[j].id == fromItemId) {
                entries.move(j, 0);
                break;
            }
        }
    }
    QStringList ids;
    QString m3u = QStringLiteral("#EXTM3U\n");
    for (const Entry &e : entries) {
        m3u += QStringLiteral("#EXTINF:-1,") + e.title + QLatin1Char('\n') + e.url + QLatin1Char('\n');
        ids << e.id;
    }
    // A new file each time: MpvController takes an identical command line for
    // the same session (one still playing behind the menus is carried on,
    // not started again), and a list played afresh is a new one. It may hold
    // a server's token: for this user's eyes only, and the last one only.
    const QString folder = m_dataRoot + QStringLiteral("/playlists");
    QDir().mkpath(folder);
    removeM3us(playlistId);
    const QString path = folder + QLatin1Char('/') + playlistId + QLatin1Char('-')
                         + QString::number(++m_serial) + QStringLiteral(".m3u");
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m3u.toUtf8());
        file.commit();
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    m_prepared.insert(playlistId, ids);

    // Where it stopped, for a list in order played from its start.
    const QJsonObject resume = p.value(QStringLiteral("resume")).toObject();
    const int resumeIndex = shuffled || !fromItemId.isEmpty()
                                ? -1 : int(ids.indexOf(resume.value(QStringLiteral("itemId")).toString()));
    return {{QStringLiteral("file"), path},
            {QStringLiteral("count"), int(ids.size())},
            {QStringLiteral("youtube"), youtube},
            {QStringLiteral("images"), images},
            {QStringLiteral("startIndex"), fromItemId.isEmpty() ? -1 : int(ids.indexOf(fromItemId))},
            {QStringLiteral("resumeIndex"), resumeIndex},
            {QStringLiteral("resumeMs"), resumeIndex >= 0 ? resume.value(QStringLiteral("positionMs")).toInt() : 0}};
}

void PlaylistsBackend::removeM3us(const QString &playlistId) const {
    QDir folder(m_dataRoot + QStringLiteral("/playlists"));
    for (const QString &name : folder.entryList({playlistId + QStringLiteral("*.m3u")}, QDir::Files))
        folder.remove(name);
}

int PlaylistsBackend::savedPositionMs(const QString &playlistId) const {
    const int i = indexOf(playlistId);
    if (i < 0)
        return 0;
    return m_playlists[i].value(QStringLiteral("resume")).toObject().value(QStringLiteral("positionMs")).toInt();
}

void PlaylistsBackend::savePosition(const QString &playlistId, int index, int positionMs) {
    const int i = indexOf(playlistId);
    const QStringList ids = m_prepared.value(playlistId);
    if (i < 0 || index < 0 || index >= ids.size())
        return;
    m_playlists[i].insert(QStringLiteral("resume"),
                          QJsonObject{{QStringLiteral("itemId"), ids[index]},
                                      {QStringLiteral("positionMs"), positionMs}});
    save();
}

void PlaylistsBackend::clearPosition(const QString &playlistId) {
    const int i = indexOf(playlistId);
    if (i < 0 || !m_playlists[i].contains(QStringLiteral("resume")))
        return;
    m_playlists[i].remove(QStringLiteral("resume"));
    save();
}

// ---------------------------------------------------------------------------
// The servers' folders
// ---------------------------------------------------------------------------

QVariant PlaylistsBackend::serverListing(const QString &moduleId, const QString &parentId, bool preview) {
    const QString key = moduleId + QLatin1Char('|') + parentId;
    const auto cached = m_listings.constFind(key);
    if (cached != m_listings.constEnd())
        return *cached;
    MediaServer *server = m_servers.value(moduleId);
    if (!server || !server->signedIn())
        return QVariantList();
    if (preview || m_listingsPending.contains(key))
        return QVariant();
    m_listingsPending.insert(key);
    const int epoch = m_listingsEpoch;
    // Episodes in a season come with their numbers; what is being watched
    // doesn't, as the servers' own views have them.
    const bool numbered = parentId != QLatin1String("resume") && parentId != QLatin1String("nextup");
    server->browse(parentId, this, [this, key, moduleId, parentId, epoch, numbered](bool ok, const QVariantList &items) {
        // Asked for again since (forgetListings): this answer is not wanted.
        if (epoch != m_listingsEpoch)
            return;
        m_listingsPending.remove(key);
        if (!ok)
            qWarning("[Playlists] %s listing %s failed", qPrintable(moduleId), qPrintable(parentId));
        QVariantList out;
        for (const QVariant &v : items) {
            QVariantMap item = v.toMap();
            QString name = item.value(QStringLiteral("title")).toString();
            if (numbered && item.value(QStringLiteral("type")).toString() == QLatin1String("episode")
                && item.value(QStringLiteral("index")).toInt() > 0)
                name = QString::number(item.value(QStringLiteral("index")).toInt()) + QStringLiteral(". ") + name;
            item.insert(QStringLiteral("name"), name);
            out << item;
        }
        m_listings.insert(key, out);
        emit serverListingReady(moduleId, parentId);
    });
    return QVariant();
}

void PlaylistsBackend::forgetListings() {
    m_listings.clear();
    m_listingsPending.clear();
    ++m_listingsEpoch;
}

// ---------------------------------------------------------------------------
// Downloads
// ---------------------------------------------------------------------------

bool PlaylistsBackend::referencedOffline(const QString &key) const {
    for (const QJsonObject &p : m_playlists) {
        if (p.value(QStringLiteral("kind")).toString() != QLatin1String("offline"))
            continue;
        for (const QJsonValue &v : p.value(QStringLiteral("items")).toArray())
            if (v.toObject().value(QStringLiteral("key")).toString() == key)
                return true;
    }
    return false;
}

void PlaylistsBackend::dropUnreferenced() {
    QStringList keys = m_downloads.keys() + m_queue;
    if (!m_active.key.isEmpty())
        keys << m_active.key;
    keys.removeDuplicates();
    QSet<QString> folders;
    for (const QString &key : keys) {
        if (referencedOffline(key))
            continue;
        cancel(key);
        const QJsonObject download = m_downloads.take(key);
        const QString path = download.value(QStringLiteral("path")).toString();
        if (!path.isEmpty() && QFile::remove(path))
            folders.insert(QFileInfo(path).absolutePath());
    }
    // The folders' entries flushed to the card, each once, off the app's
    // thread.
    if (!folders.isEmpty()) {
        QThread *thread = QThread::create([folders]() {
            for (const QString &folder : folders)
                syncFolder(folder);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }
}

void PlaylistsBackend::queueOfflineItems() {
    for (const QJsonObject &p : m_playlists) {
        if (p.value(QStringLiteral("kind")).toString() != QLatin1String("offline"))
            continue;
        for (const QJsonValue &v : p.value(QStringLiteral("items")).toArray()) {
            const QJsonObject item = v.toObject();
            if (item.value(QStringLiteral("module")).toString() == kLocalFiles)
                continue;
            const QString key = item.value(QStringLiteral("key")).toString();
            const QJsonObject download = m_downloads.value(key);
            const QString state = download.value(QStringLiteral("state")).toString();
            // One the server refused stays refused until asked again.
            if (state == QLatin1String("failed")
                && download.value(QStringLiteral("reason")).toString() == QLatin1String("not allowed"))
                continue;
            enqueue(key);
        }
    }
}

bool PlaylistsBackend::enqueue(const QString &key) {
    if (m_active.key == key || m_queue.contains(key))
        return true;
    // On the device already, for another list: never fetched twice.
    const QJsonObject download = m_downloads.value(key);
    if (download.value(QStringLiteral("state")).toString() == QLatin1String("done")
        && QFileInfo::exists(download.value(QStringLiteral("path")).toString()))
        return false;
    m_queue << key;
    QTimer::singleShot(0, this, &PlaylistsBackend::startNext);
    return true;
}

void PlaylistsBackend::startNext() {
    if (!m_active.key.isEmpty())
        return;
    while (!m_queue.isEmpty()) {
        const QString key = m_queue.takeFirst();
        if (!referencedOffline(key))
            continue;
        const QJsonObject item = itemByKey(key);
        const QString module = item.value(QStringLiteral("module")).toString();
        if (module != kYouTube && !m_servers.contains(module))
            continue;
        m_active = Active();
        m_active.key = key;
        if (module == kYouTube)
            startYouTube(key, item);
        else
            startServer(key, item);
        emit playlistsChanged();
        return;
    }
}

void PlaylistsBackend::failLater(const QString &reason) {
    m_active.reason = reason;
    QTimer::singleShot(0, this, [this]() { finish(false); });
}

bool PlaylistsBackend::writable(const QString &folder) {
    const QFileInfo info(folder);
    if (info.isDir() && info.isWritable())
        return true;
    // A card from before the film partition was writable, say (ro in fstab).
    failLater(QStringLiteral("can't write to ") + downloadFolder());
    return false;
}

void PlaylistsBackend::startYouTube(const QString &key, const QJsonObject &item) {
    const QString program = ytdlp::locate(m_dataRoot);
    if (program.isEmpty() || !m_youtube) {
        failLater(QStringLiteral("no yt-dlp"));
        return;
    }
    const QString folder = sourceFolder(sourceFolderName(kYouTube));
    if (!writable(folder))
        return;
    const QString videoId = item.value(QStringLiteral("source")).toObject().value(QStringLiteral("videoId")).toString();
    // The way the YouTube module plays it (its ADVANCED settings), as a file;
    // ffmpeg puts the picture and sound YouTube sends apart back together.
    QStringList args = m_youtube->downloadArgs(!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty());
    args << QStringLiteral("--newline") << QStringLiteral("--no-playlist") << QStringLiteral("--no-mtime")
         // exFAT's rules for names, which are Windows'.
         << QStringLiteral("--windows-filenames")
         << QStringLiteral("-o") << folder + QStringLiteral("/%(title).80B [%(id)s].%(ext)s")
         // The file's path once it is complete; quiet otherwise, but for the progress.
         << QStringLiteral("--print") << QStringLiteral("after_move:filepath")
         << QStringLiteral("--no-simulate") << QStringLiteral("--progress")
         << QStringLiteral("--") << QStringLiteral("https://www.youtube.com/watch?v=") + videoId;

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_UNIX
    // Its own process group, so that cancel() reaches the ffmpeg it runs too.
    process->setChildProcessModifier([]() { ::setsid(); });
#endif
    m_active.process = process;
    connect(process, &QProcess::readyRead, this, [this, process, key]() {
        static const QRegularExpression kPercent(QStringLiteral("\\[download\\]\\s+([0-9.]+)%"));
        while (process->canReadLine()) {
            const QString line = QString::fromUtf8(process->readLine()).trimmed();
            const QRegularExpressionMatch m = kPercent.match(line);
            if (m.hasMatch()) {
                const int percent = int(m.captured(1).toDouble());
                if (percent != m_active.percent) {
                    m_active.percent = percent;
                    emit downloadProgress(key, percent);
                }
            } else if (line.startsWith(QLatin1Char('/')) && QFileInfo::exists(line)) {
                m_active.finalPath = line;
            } else if (line.startsWith(QLatin1String("ERROR:"))) {
                m_active.reason = line.mid(6).trimmed();
            }
        }
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        const QString rest = QString::fromUtf8(process->readAll()).trimmed();
        for (const QString &line : rest.split(QLatin1Char('\n'))) {
            const QString l = line.trimmed();
            if (l.startsWith(QLatin1Char('/')) && QFileInfo::exists(l))
                m_active.finalPath = l;
        }
        finish(status == QProcess::NormalExit && code == 0 && !m_active.finalPath.isEmpty());
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_active.reason = QStringLiteral("no yt-dlp");
            finish(false);
        }
    });
    qDebug("[Playlists] downloading %s", qPrintable(key));
    process->start(program, args);
}

void PlaylistsBackend::startServer(const QString &key, const QJsonObject &item) {
    const QString module = item.value(QStringLiteral("module")).toString();
    const MediaServer *server = m_servers.value(module);
    if (!server || !server->signedIn()) {
        failLater(QStringLiteral("signed out"));
        return;
    }
    const QString folder = sourceFolder(sourceFolderName(module));
    if (!writable(folder))
        return;
    const QString itemId = item.value(QStringLiteral("source")).toObject().value(QStringLiteral("itemId")).toString();
    const QString base = folder + QLatin1Char('/')
                         + safeFileName(item.value(QStringLiteral("title")).toString(), QLatin1Char(' '), 80,
                                        QStringLiteral("video"))
                         + QStringLiteral(" [") + itemId + QLatin1Char(']');
    ServerDownload *download = ServerDownload::start(server->downloadRequest(itemId), base, this);
    m_active.download = download;
    connect(download, &ServerDownload::progress, this, [this, key](int percent) {
        if (percent != m_active.percent) {
            m_active.percent = percent;
            emit downloadProgress(key, percent);
        }
    });
    connect(download, &ServerDownload::finished, this, [this](bool ok, const QString &finalPath, const QString &reason) {
        m_active.finalPath = finalPath;
        m_active.reason = reason;
        finish(ok);
    });
    qDebug("[Playlists] downloading %s", qPrintable(key));
}

void PlaylistsBackend::finish(bool ok) {
    if (m_active.key.isEmpty() || m_active.flushing)
        return;
    if (m_active.process) {
        m_active.process->deleteLater();
        m_active.process = nullptr;
    }
    m_active.download = nullptr;
    if (!ok) {
        record(false);
        return;
    }
    // The file to the card before it is counted as there, off the app's
    // thread: the write-back of a film takes a while. Until then the item is
    // still downloading, at 100%.
    m_active.flushing = true;
    m_active.percent = 100;
    const QString key = m_active.key;
    const QString path = m_active.finalPath;
    const quint64 generation = ++m_flushGeneration;
    auto error = std::make_shared<QString>();
    QThread *thread = QThread::create([path, error]() {
        *error = syncPath(path);
        if (error->isEmpty()) *error = syncPath(QFileInfo(path).absolutePath(), true);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, key, generation, error]() {
        // Cancelled while it was flushing: its record is gone with it.
        if (m_active.key == key && m_active.flushing && m_flushGeneration == generation) {
            if (!error->isEmpty()) m_active.reason = QStringLiteral("Could not sync download: ") + *error;
            record(error->isEmpty());
        }
    });
    thread->start();
}

void PlaylistsBackend::record(bool ok) {
    const QString key = m_active.key;
    if (ok) {
        m_downloads.insert(key, QJsonObject{{QStringLiteral("path"), m_active.finalPath},
                                            {QStringLiteral("state"), QStringLiteral("done")},
                                            {QStringLiteral("bytes"), double(QFileInfo(m_active.finalPath).size())}});
        qDebug("[Playlists] downloaded %s: %s", qPrintable(key), qPrintable(m_active.finalPath));
    } else {
        // What was fetched stays for a retry to carry on from (yt-dlp does);
        // only a download no list wants loses it (cancel).
        const QString reason = m_active.reason.isEmpty() ? QStringLiteral("error") : m_active.reason;
        m_downloads.insert(key, QJsonObject{{QStringLiteral("state"), QStringLiteral("failed")},
                                            {QStringLiteral("reason"), reason}});
        qWarning("[Playlists] download of %s failed: %s", qPrintable(key), qPrintable(reason));
    }
    m_active = Active();
    // Taken off every offline list while it came: not kept.
    if (!referencedOffline(key))
        dropUnreferenced();
    save();
    emit playlistsChanged();
    QTimer::singleShot(0, this, &PlaylistsBackend::startNext);
}

void PlaylistsBackend::cancel(const QString &key) {
    m_queue.removeAll(key);
    if (m_active.key != key)
        return;
    if (QProcess *process = m_active.process) {
        process->disconnect(this);
        // yt-dlp and the ffmpeg under it, by their group: an interrupt first,
        // for yt-dlp to tidy up, and the kill a moment later if it hasn't
        // gone; the app's thread never waits on it.
#ifdef Q_OS_UNIX
        if (process->processId() > 0)
            ::killpg(pid_t(process->processId()), SIGINT);
        else
            process->kill();
#else
        process->kill();
#endif
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        QTimer::singleShot(2000, process, [process]() {
            if (process->state() != QProcess::NotRunning)
                process->kill();
        });
    }
    if (m_active.download)
        m_active.download->cancel();
    // A finished file still on its way to the card, or what a download got
    // through: no list wants it now.
    if (!m_active.finalPath.isEmpty())
        QFile::remove(m_active.finalPath);
    removePartials(key);
    m_active = Active();
    QTimer::singleShot(0, this, &PlaylistsBackend::startNext);
}

void PlaylistsBackend::removePartials(const QString &key) const {
    // What yt-dlp leaves of a video it didn't finish: its .part files and the
    // streams it was to merge (".f137.mp4"), all named with "[<id>]".
    if (!key.startsWith(QLatin1String("youtube:")))
        return;
    const QString tag = QLatin1Char('[') + key.mid(8) + QLatin1Char(']');
    QDir dir(QDir(downloadFolder()).filePath(sourceFolderName(kYouTube)));
    for (const QFileInfo &f : dir.entryInfoList(QDir::Files))
        if (f.fileName().contains(tag))
            QFile::remove(f.absoluteFilePath());
}
