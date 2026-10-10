#include "LocalFilesBackend.h"
#include "RemovableDrives.h"
#include "util/AtomicFile.h"
#include "util/AsyncDirectoryCache.h"
#include "util/LegacyNames.h"
#include "../../AppCore.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <QFileInfo>
#include <QVariantMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

// supported image types
static const QStringList kImageExts = {
    "jpg", "jpeg", "png", "gif", "webp", "bmp", "tif", "tiff"
};
// supported playlist types
static const QStringList kPlaylistExts = { 
    "m3u", "m3u8" 
};
// full list of supported playback types (combo of video, image and playlist)
static const QStringList kMediaExts =
    QStringList{ "mp4", "mkv", "avi", "mov", "m4v", "webm", "wmv", "flv", "f4v", "mpg", "mpeg", "vob" }
    + kImageExts
    + kPlaylistExts;
// The file systems' own folders, never media: ext4's, and those Windows leaves
// on a drive it has had.
static const QStringList kSystemFolders = {
    "lost+found", "System Volume Information", "$RECYCLE.BIN"
};

// A search under way (search()): the folder it walks, then the others.
struct LocalFilesBackend::SearchRun {
    QString path;
    std::atomic_bool cancelled { false };
};

// No more matches than a tree column is good for: the first, by name.
static constexpr int kSearchLimit = 200;
LocalFilesBackend::~LocalFilesBackend() {
    if (m_search)
        m_search->cancelled = true;
}

LocalFilesBackend::LocalFilesBackend(const QString &appRoot, const QString &dataRoot, AppCore *appCore,
                                     QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot), m_appCore(appCore)
{
    m_directories = new AsyncDirectoryCache(this);
    m_directories->ready = [this](const QString &path) { emit entriesReady(path); };
    m_drives = new RemovableDrives(this);
    connect(m_drives, &RemovableDrives::changed, this, &LocalFilesBackend::clearDirectoryCache);
    connect(m_drives, &RemovableDrives::changed, this, &LocalFilesBackend::drivesChanged);
    m_mediaRoot = defaultMediaRoot();
    m_drives->setMediaRoot(m_mediaRoot);
    // Resolve the configured media directory (falls back to the default above).
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonObject cfg = QJsonDocument::fromJson(f.readAll()).object();
        QString dir = cfg["modules"].toObject()["com.osdos.local_files"].toObject()
                          ["media_directory"].toString();
        if (!dir.isEmpty())
            setMediaRoot(dir);
    }
}

bool LocalFilesBackend::isImage(const QString &path) const {
    return kImageExts.contains(QFileInfo(path).suffix().toLower());
}

bool LocalFilesBackend::isPlaylist(const QString &path) const {
    return kPlaylistExts.contains(QFileInfo(path).suffix().toLower());
}

// True if an .m3u/.m3u8 references at least one image entry. Used to decide whether
// the slideshow-redraw mpv script is needed (see MpvController::loadAndPlay): mpv's
// KMS output won't repaint consecutive same-size stills without it.
bool LocalFilesBackend::playlistContainsImages(const QString &path) const {
    if (!isPlaylist(path))
        return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (isImage(line))
            return true;
    }
    return false;
}

QString LocalFilesBackend::historyFilePath() const {
    return m_dataRoot + "/local_files_history.json";
}

QVariantMap LocalFilesBackend::loadHistory() const {
    QFile file(historyFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

void LocalFilesBackend::saveHistory(const QVariantMap &history) {
    writeFileAtomically(historyFilePath(),
                        QJsonDocument(QJsonObject::fromVariantMap(history)).toJson(QJsonDocument::Compact));
}

QVariantMap LocalFilesBackend::getSavedPosition(const QString &filePath) {
    const QVariant val = loadHistory().value(filePath);
    if (!val.isValid())
        return {};
    if (val.canConvert<QVariantMap>()) {
        QVariantMap entry = val.toMap();
        if (!entry.contains("plPos")) entry["plPos"] = -1;
        return entry;
    }
    // Legacy: plain int stored (pos only)
    return {{"pos", val.toInt()}, {"plPos", -1}};
}

void LocalFilesBackend::savePosition(const QString &filePath, int positionMs, int playlistPos) {
    QVariantMap history = loadHistory();
    QVariantMap entry;
    entry["pos"]   = positionMs;
    entry["plPos"] = playlistPos;
    history[filePath] = entry;
    saveHistory(history);
}

void LocalFilesBackend::clearPosition(const QString &filePath) {
    QVariantMap history = loadHistory();
    history.remove(filePath);
    saveHistory(history);
}

void LocalFilesBackend::get_auto_subtitles_options() {
    QVariantList options;
    QVariantMap forced; forced["id"] = "forced"; forced["label"] = "Forced Only"; forced["old"] = false;
    QVariantMap on;     on["id"] = "on";         on["label"] = "On";              on["old"] = true;
    QVariantMap off;    off["id"] = "off";       off["label"] = "Off";
    options << forced << on << off;
    emit dynamicOptionsReady("auto_subtitles", options);
}

void LocalFilesBackend::get_resume_playback_options() {
    QVariantList options;
    QVariantMap ask; ask["id"] = "ask"; ask["label"] = "Ask";
    QVariantMap yes; yes["id"] = "yes"; yes["label"] = "Always";
    QVariantMap no;  no["id"]  = "no";  no["label"]  = "Never";
    options << ask << yes << no;
    emit dynamicOptionsReady("resume_playback", options);
}

void LocalFilesBackend::get_shuffle_playback_options() {
    QVariantList options;
    QVariantMap ask; ask["id"] = "ask"; ask["label"] = "Ask";
    QVariantMap yes; yes["id"] = "yes"; yes["label"] = "Always"; yes["old"] = true;
    QVariantMap no;  no["id"]  = "no";  no["label"]  = "Never";  no["old"]  = false;
    options << ask << yes << no;
    emit dynamicOptionsReady("shuffle_playback", options);
}

void LocalFilesBackend::get_image_duration_options() {
    QVariantList options;
    QVariantMap five;   five["id"]   = "5";  five["label"]   = "5 Seconds";
    QVariantMap ten;    ten["id"]    = "10"; ten["label"]    = "10 Seconds";
    QVariantMap thirty; thirty["id"] = "30"; thirty["label"] = "30 Seconds";
    QVariantMap sixty;  sixty["id"]  = "60"; sixty["label"]  = "60 Seconds";
    options << five << ten << thirty << sixty;
    emit dynamicOptionsReady("image_duration", options);
}

void LocalFilesBackend::get_subtitle_languages() {
    QStringList addedLabels;
    QVariantList options;

    QFile file(m_appRoot + "/modules/local_files/iso639-1.json");
    if (!file.open(QIODevice::ReadOnly))
        return;

    options.append(QVariantMap{{"id","-"},{"label","Any"}});

    QVariantList locList = QJsonDocument::fromJson(file.readAll()).toVariant().toList();
    for (const QVariant loc : locList)
    {
        QVariantMap langOption = QVariantMap{{"id",loc.toJsonObject()["id"].toString()},{"label",loc.toJsonObject()["label"].toString()}};
        if (langOption["label"].toString() == "" || addedLabels.contains(langOption["label"].toString())) continue;
        addedLabels.append(langOption["label"].toString());
        options.append(langOption);
    }

    emit dynamicOptionsReady("sub_lang", options);
}

QVariant LocalFilesBackend::entries(const QString &path) {
    static const QString kModuleId = QStringLiteral("com.osdos.local_files");
    if (path == QLatin1String("recent") || path == QLatin1String("favorites"))
        return existing(m_appCore ? m_appCore->get_list(kModuleId, path) : QVariantList());
    if (path.startsWith(QLatin1String("search/")))
        return search(path, path.mid(7));
    return decorateEntries(path, getItems(path));
}

void LocalFilesBackend::clearDirectoryCache() { m_directories->clear(); }

QVariant LocalFilesBackend::requestEntries(const QString &path) {
    if (path == QLatin1String("recent") || path == QLatin1String("favorites")
            || path.startsWith(QLatin1String("search/"))) return entries(path);
    if (!allowedDirectory(path)) return QVariantList();
    const QVariant result = m_directories->fetch(path, [path]() { return readDirectory(path); });
    if (!result.isValid()) return {};
    return decorateEntries(path, result.toList());
}

QVariantList LocalFilesBackend::decorateEntries(const QString &path, QVariantList items) const {
    if (path != m_mediaRoot)
        return items;
    // The drives plugged in, under their labels, before the media folder's own.
    QVariantList drives;
    const QList<RemovableDrives::Drive> plugged = m_drives->drives();
    for (const RemovableDrives::Drive &d : plugged) {
        drives << QVariantMap{{QStringLiteral("name"), QStringLiteral("USB: ") + d.name},
                              {QStringLiteral("path"), d.path},
                              {QStringLiteral("isFolder"), true}};
    }
    items = drives + items;
    // Nothing in the media folder and no drive: nothing to search either.
    if (items.isEmpty())
        return items;
    auto folder = [](const char *name, const char *path) {
        return QVariantMap{{QStringLiteral("name"), QString::fromLatin1(name)},
                           {QStringLiteral("path"), QString::fromLatin1(path)},
                           {QStringLiteral("isFolder"), true}};
    };
    return QVariantList{folder("Recently Watched", "recent"), folder("Favorites", "favorites"),
                        QVariantMap{{QStringLiteral("name"), QStringLiteral("Search")},
                                    {QStringLiteral("path"), QStringLiteral("search")},
                                    {QStringLiteral("isFolder"), false},
                                    {QStringLiteral("kind"), QStringLiteral("search")}}} + items;
}

QString LocalFilesBackend::mediaRoot() const {
    return m_mediaRoot;
}

QString LocalFilesBackend::defaultMediaRoot() const {
    const QString dir = legacy::env("MEDIA_DIR");
    return dir.isEmpty() ? m_dataRoot + QStringLiteral("/media") : dir;
}

void LocalFilesBackend::setMediaRoot(const QString &path) {
    clearDirectoryCache();
    // An empty (reset) setting means back to the default.
    m_mediaRoot = path.isEmpty() ? defaultMediaRoot() : path;
    QDir().mkpath(m_mediaRoot);
    m_drives->setMediaRoot(m_mediaRoot);
    // What was found was found in the old folder.
    if (m_search)
        m_search->cancelled = true;
    m_search.reset();
    m_foundPath.clear();
    m_found.clear();
    qDebug("[LocalFiles] media root: %s", qPrintable(m_mediaRoot));
}

void LocalFilesBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value) {
    if (moduleId == QLatin1String("com.osdos.local_files") && key == QLatin1String("media_directory"))
        setMediaRoot(value.toString());
}

bool LocalFilesBackend::allowedDirectory(const QString &path) const {
    const QString clean = QDir(path).absolutePath();
    const QString root = QDir(m_mediaRoot).absolutePath();
    return clean == root || clean.startsWith(root.endsWith('/') ? root : root + '/')
           || m_drives->holds(clean);
}

QVariantList LocalFilesBackend::getItems(const QString &path) {
    return allowedDirectory(path) ? readDirectory(path) : QVariantList();
}

QVariantList LocalFilesBackend::readDirectory(const QString &path) {
    QVariantList result;
    const QDir dir(path);
    const auto entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                            QDir::DirsFirst | QDir::Name);
    for (const QFileInfo &info : entries) {
        if (info.isDir() && kSystemFolders.contains(info.fileName(), Qt::CaseInsensitive)) continue;
        const auto item = entryFor(path, info.fileName(), info.isDir());
        if (!item.isEmpty()) result.append(item);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Search: names under the media folder, scanned off the UI thread.
// ---------------------------------------------------------------------------

// A tree entry for a name, as getItems() makes them; empty for a file that
// isn't media.
QVariantMap LocalFilesBackend::entryFor(const QString &dirPath, const QString &name, bool isDir) {
    const QString full = QDir(dirPath).absoluteFilePath(name);
    QVariantMap item;
    if (isDir) {
        if (kPlaylistExts.contains(QFileInfo(name).suffix().toLower())
                && QFileInfo::exists(full + QLatin1Char('/') + name)) {
            item["name"] = name;
            item["path"] = full + QLatin1Char('/') + name;
            item["isFolder"] = false;
            return item;
        }
        item["name"] = name;
        item["path"] = full;
        item["isFolder"] = true;
        return item;
    }
    if (!kMediaExts.contains(QFileInfo(name).suffix().toLower()))
        return {};
    item["name"] = name;
    item["path"] = full;
    item["isFolder"] = false;
    return item;
}

QVariant LocalFilesBackend::search(const QString &path, const QString &words, bool fresh) {
    if (!fresh && path == m_foundPath)
        return m_found;
    if (!fresh && m_search && m_search->path == path)
        return QVariant();
    m_foundPath.clear();
    m_found.clear();
    if (m_search)
        m_search->cancelled = true;
    const auto run = std::make_shared<SearchRun>();
    run->path = path;
    m_search = run;
    QStringList roots { m_mediaRoot };
    for (const RemovableDrives::Drive &d : m_drives->drives())
        roots << d.path;
    const QStringList terms = words.simplified().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher, run]() {
        watcher->deleteLater();
        if (run->cancelled || m_search != run)
            return;
        m_foundPath = run->path;
        m_found = watcher->result();
        m_search.reset();
        emit searchReady(m_foundPath);
    });
    // Only value snapshots enter the worker: it never accesses the backend or
    // RemovableDrives, and can safely finish after the backend is destroyed.
    watcher->setFuture(QtConcurrent::run([run, roots, terms]() {
        QVariantList found;
        auto less = [](const QVariant &a, const QVariant &b) {
            const QVariantMap left = a.toMap(), right = b.toMap();
            const int order = QString::compare(left.value("name").toString(),
                                               right.value("name").toString(), Qt::CaseInsensitive);
            return order != 0 ? order < 0
                              : left.value("path").toString() < right.value("path").toString();
        };
        for (const QString &root : roots) {
            if (run->cancelled)
                return QVariantList();
            // Not through symbolic links: one pointing back up would never end.
            QDirIterator it(root, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (!run->cancelled && it.hasNext()) {
                it.next();
                const QFileInfo info = it.fileInfo();
                const QString name = info.fileName().toLower();
                if (terms.isEmpty() || !std::all_of(terms.cbegin(), terms.cend(),
                        [&name](const QString &term) { return name.contains(term); }))
                    continue;
                const QVariantMap item = entryFor(info.absolutePath(), info.fileName(), info.isDir());
                if (item.isEmpty())
                    continue;
                // A max heap keeps the alphabetically first 200, using O(200)
                // result storage and O(log 200) work per matching entry.
                const QVariant candidate(item);
                if (found.size() < kSearchLimit) {
                    found.append(candidate);
                    std::push_heap(found.begin(), found.end(), less);
                } else if (less(candidate, found.front())) {
                    std::pop_heap(found.begin(), found.end(), less);
                    found.back() = candidate;
                    std::push_heap(found.begin(), found.end(), less);
                }
            }
        }
        std::sort_heap(found.begin(), found.end(), less);
        return found;
    }));
    return QVariant();
}

QVariantList LocalFilesBackend::existing(const QVariantList &entries) const {
    QVariantList kept;
    for (const QVariant &v : entries) {
        if (QFileInfo::exists(v.toMap().value("path").toString()))
            kept << v;
    }
    return kept;
}
