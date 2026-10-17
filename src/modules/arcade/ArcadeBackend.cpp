#include "ArcadeBackend.h"
#include "../scripts/ScriptLauncher.h"
#include "../../AppCore.h"
#include "../../audio/AudioOutput.h"
#include "../../input/ControllerProfile.h"
#include "../../util/AtomicFile.h"
#include "../../util/LegacyNames.h"
#include "FbneoInstaller.h"
#include "Libretro.h"
#include "RetroAchievements.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <utility>

namespace {

const QString kModuleId = QStringLiteral("com.osdos.arcade");

// games.json's layout: one of another is read as none, and every set asked
// about again (2: each machine's source file, for its system).
constexpr int kCacheVersion = 2;

// The most credits a game is started with, as most games show at most 9.
constexpr int kMaxCredits = 9;

// MAME asked about this many sets at a time: a short enough command line, and
// what it says comes in batches.
constexpr int kBatch = 300;
// A batch MAME hasn't answered in this long is given up on.
constexpr int kScanTimeoutMs = 120000;

// MAME's exit codes (src/emu/main.h).
constexpr int kMissingFiles = 2;
constexpr int kNoSuchSystem = 5;
constexpr int kInvalidConfig = 6;

// What a file holding a token or a key is written with.
constexpr QFileDevice::Permissions kOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

// Folders beside the games that aren't games: the BIOS sets' (on MAME's
// rompath too), the samples' (its samplepath) and the games' pictures.
const QStringList kNotSets = { QStringLiteral("bios"), QStringLiteral("samples"), QStringLiteral("snap") };

// The folders MAME writes what it keeps of the games in, by the option that
// names each: all in <data>/arcade, whatever MAME's own settings say.
constexpr std::pair<const char *, const char *> kOutputs[] = {
    { "-cfg_directory", "cfg" },           // its settings, the buttons
    { "-nvram_directory", "nvram" },       // the games' NVRAM
    { "-input_directory", "inp" },         // input recordings
    { "-state_directory", "sta" },         // save states
    { "-snapshot_directory", "snap" },     // snapshots and videos
    { "-diff_directory", "diff" },         // what a game writes to its hard disk
};

// The places games are under: the Arcade screen's filters by genre, players,
// decade and publisher (the tree's maker), each kind under its heading in
// Menu Filters, and the tree's folders by players, decade and maker.
constexpr std::pair<const char *, const char *> kKinds[] = {
    { "genres", "Genres" }, { "players", "Players" }, { "years", "Years" }, { "makers", "Publishers" },
};
const char *const kFolderKinds[] = { "players", "years", "makers" };
// The lists among the filters, before the places, and Not Recognized after,
// which the tree has too.
constexpr std::pair<const char *, const char *> kLists[] = {
    { "favorites", "Favorites" }, { "recent", "Recently Played" },
};
constexpr std::pair<const char *, const char *> kUnknown = { "unknown", "Not Recognized" };

// A kind's places in the order they are listed, with what each is called:
// players by number, decades in order with the years unknown last, the rest
// by name.
QList<std::pair<QString, QString>> sortPlaces(const QString &kind, QStringList places) {
    if (kind == QLatin1String("players")) {
        std::sort(places.begin(), places.end(),
                  [](const QString &a, const QString &b) { return a.toInt() < b.toInt(); });
    } else if (kind == QLatin1String("years")) {
        std::sort(places.begin(), places.end(), [](const QString &a, const QString &b) {
            // The years without a decade last.
            if ((a == QLatin1String("unknown")) != (b == QLatin1String("unknown")))
                return b == QLatin1String("unknown");
            return a < b;
        });
    } else {
        std::sort(places.begin(), places.end(),
                  [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    }
    QList<std::pair<QString, QString>> sorted;
    for (const QString &place : std::as_const(places)) {
        QString name = place;
        if (kind == QLatin1String("players"))
            name = place == QLatin1String("1") ? QStringLiteral("1 Player") : place + QStringLiteral(" Players");
        else if (kind == QLatin1String("years") && place == QLatin1String("unknown"))
            name = QStringLiteral("Year Unknown");
        sorted.append({ place, name });
    }
    return sorted;
}

// Whether Menu Filters shows a filter it hasn't had turned on or off: all
// but a publisher's, as a full set has hundreds.
bool shownByDefault(const QString &filter) {
    return !filter.startsWith(QLatin1String("makers/"));
}

bool isOn(const QVariant &value, bool fallback) {
    if (!value.isValid() || value.isNull())
        return fallback;
    if (value.typeId() == QMetaType::Bool)
        return value.toBool();
    return value.toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;
}

QVariantMap folder(const QString &name, const QString &path) {
    return { { QStringLiteral("name"), name }, { QStringLiteral("path"), path },
             { QStringLiteral("isFolder"), true } };
}

// A file as it is now, to tell when it has changed: its path, size and time.
QString fileStamp(const QFileInfo &file) {
    return QStringLiteral("%1:%2:%3").arg(file.canonicalFilePath()).arg(file.size())
                                     .arg(file.lastModified().toMSecsSinceEpoch());
}

QVariantMap filter(const QString &id, const QString &name, qsizetype count) {
    return { { QStringLiteral("id"), id }, { QStringLiteral("name"), name },
             { QStringLiteral("count"), static_cast<int>(count) } };
}

// The first snapshot MAME took of a game (F12 in it), in its folder of them;
// "" for none.
QString firstShot(const QString &folder) {
    const QDir dir(folder);
    const QStringList shots = dir.entryList({ QStringLiteral("*.png") }, QDir::Files, QDir::Name);
    return shots.isEmpty() ? QString() : dir.filePath(shots.first());
}

} // namespace

ArcadeBackend::ArcadeBackend(const QString &appRoot, const QString &dataRoot, AppCore *appCore,
                             DisplayHandoff *handoff, QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot),
      m_home(dataRoot + QStringLiteral("/arcade")), m_appCore(appCore)
{
    m_launcher = new ScriptLauncher(m_appRoot, m_dataRoot, handoff, this);
    m_launcher->setHandoffOwner(QStringLiteral("arcade"));
    connect(m_launcher, &ScriptLauncher::runningChanged, this, &ArcadeBackend::runningChanged);
    connect(m_launcher, &ScriptLauncher::finished, this, [this](int exitCode, const QString &launcherReason) {
        QString reason = launcherReason;
        QString notice;
        bool refused = false;
        if (m_playingEngine == QLatin1String("fbneo")) {
            const libretro::Run run = libretro::readLog(retroarchHome() + QStringLiteral("/last.log"));
            takeAchievements(run);
            // FinalBurn Neo shows its own error for a game it can't start,
            // and RetroArch goes on: a game is one it started, as its log
            // says (a log there to say so).
            if (reason == QLatin1String("ok") && run.logged && !run.started)
                reason = QStringLiteral("failed");
            notice = fbneoNotice(exitCode, reason, run);
            refused = reason == QLatin1String("failed_to_start")
                      || (reason == QLatin1String("failed") && !m_launcher->lastRunCrashed() && !run.started);
        } else {
            notice = noticeFor(exitCode, reason);
            // The time it ran counts as played, unless MAME refused the game
            // (files missing, no such game, its settings wrong) or never ran.
            refused = reason == QLatin1String("failed_to_start")
                      || (reason == QLatin1String("failed") && !m_launcher->lastRunCrashed()
                          && (exitCode == kMissingFiles || exitCode == kNoSuchSystem
                              || exitCode == kInvalidConfig));
        }
        if (!notice.isEmpty())
            qWarning("[Arcade] %s: %s", qPrintable(m_playing), qPrintable(notice));
        if (!refused && !m_playing.isEmpty())
            addPlayTime(m_playing, (m_playClock.elapsed() + 500) / 1000);
        // A snapshot taken in the game (F12) is its picture now.
        if (!m_playing.isEmpty()) {
            const QString shot = firstShot(m_home + QStringLiteral("/snap/") + m_playing);
            if (shot.isEmpty())
                m_shots.remove(m_playing);
            else
                m_shots.insert(m_playing, shot);
        }
        m_playing.clear();
        emit finished(exitCode, reason, notice);
        // The sets MAME was still to be asked about, held while it played.
        if (!m_scan && !m_toAsk.isEmpty())
            scanNext();
    });

    m_watcher = new QFutureWatcher<Read>(this);
    connect(m_watcher, &QFutureWatcherBase::finished, this, &ArcadeBackend::onRead);

    m_installer = new FbneoInstaller(m_dataRoot, this);
    connect(m_installer, &FbneoInstaller::progressChanged, this, &ArcadeBackend::fbneoChanged);
    connect(m_installer, &FbneoInstaller::finished, this, [this](bool ok, const QString &message) {
        m_fbneoMessage = message;
        // Its list read with the folder, now that it has changed.
        if (ok)
            refresh();
        emit fbneoChanged();
    });

    // The RetroAchievements account, without its password, which is never
    // kept.
    QFile account(m_home + QStringLiteral("/retroachievements.json"));
    if (account.open(QIODevice::ReadOnly)) {
        const QJsonObject kept = QJsonDocument::fromJson(account.readAll()).object();
        m_raUser = kept.value(QStringLiteral("user")).toString();
        m_raToken = kept.value(QStringLiteral("token")).toString();
        m_raKey = kept.value(QStringLiteral("key")).toString();
    }
    m_leaderboards = new RetroAchievements(m_home, RetroAchievements::kApi, this);
    m_leaderboards->setKey(m_raKey);
    connect(m_leaderboards, &RetroAchievements::boardReady, this, [this](const QString &fileName) {
        emit leaderboardReady(QFileInfo(fileName).completeBaseName().toLower());
    });
    connect(m_leaderboards, &RetroAchievements::keyRefused, this, [this] {
        if (!std::exchange(m_raKeyRefused, true))
            emit achievementsChanged();
    });
    // A game's config is written anew for each game: one a game left that the
    // app didn't see end (it was stopped meanwhile) may hold the password.
    QFile::remove(retroarchHome() + QStringLiteral("/retroarch.cfg"));

    m_scanTimer = new QTimer(this);
    m_scanTimer->setSingleShot(true);
    m_scanTimer->setInterval(kScanTimeoutMs);
    connect(m_scanTimer, &QTimer::timeout, this, [this] {
        if (!m_scan)
            return;
        qWarning("[Arcade] MAME did not list the games within %d s", kScanTimeoutMs / 1000);
        m_scan->kill();
    });

    // The module's settings as they stand. games.json waits for the list to
    // open (refresh()): with a full set of MAME's games it is megabytes.
    for (const char *key : { "games_directory", "high_scores", "video", "menu_filters", "online_high_scores",
                             "engines" }) {
        const QString k = QLatin1String(key);
        applySetting(k, m_appCore ? m_appCore->get_setting(kModuleId, k) : QVariant());
    }
    // A Start Filter saved under one of catver.ini's own genres (0.0.9's:
    // Fighter, Driving) is saved again as the genre it became.
    if (m_appCore) {
        const QString start = m_appCore->get_setting(kModuleId, QStringLiteral("start_filter")).toString();
        if (start.startsWith(QLatin1String("genres/"))) {
            const QString genre = QStringLiteral("genres/") + mame::genreOf(start.mid(7));
            if (genre != start)
                m_appCore->save_setting(kModuleId, QStringLiteral("start_filter"), genre);
        }
    }

    // Play at Startup shows the app's startup favourite, whichever screen
    // set it (a game's options, Settings' own Play at Startup).
    if (m_appCore) {
        connect(m_appCore, &AppCore::appSettingChanged, this, [this](const QString &key) {
            if (key == QLatin1String("startup_favorite"))
                syncStartupGame();
        });
        syncStartupGame();
    }
}

ArcadeBackend::~ArcadeBackend() {
    // The read keeps nothing of this object's: it may finish on its own.
    m_watcher->disconnect(this);
    if (m_scan) {
        m_scan->disconnect(this);
        m_scan->kill();
        m_scan->waitForFinished(1000);
    }
}

void ArcadeBackend::onSettingChanged(const QString &moduleId, const QString &key,
                                     const QVariant &value) {
    if (moduleId == kModuleId)
        applySetting(key, value);
}

void ArcadeBackend::applySetting(const QString &key, const QVariant &value) {
    if (key == QLatin1String("games_directory")) {
        setGamesFolder(value.toString());
    } else if (key == QLatin1String("high_scores")) {
        m_highScores = isOn(value, true);
    } else if (key == QLatin1String("video")) {
        // Auto leaves it to MAME (Debian's mame.ini says opengl).
        static const QHash<QString, QString> kVideo = {
            { QStringLiteral("OpenGL"), QStringLiteral("opengl") },
            { QStringLiteral("BGFX"), QStringLiteral("bgfx") },
            { QStringLiteral("Accelerated"), QStringLiteral("accel") },
            { QStringLiteral("Software"), QStringLiteral("soft") },
        };
        m_video = kVideo.value(value.toString());
    } else if (key == QLatin1String("menu_filters")) {
        // Saved whole at every change (MultiSelectSettings.qml).
        m_menuFilters = value.toMap();
        emit gamesChanged();
    } else if (key == QLatin1String("online_high_scores")) {
        m_achievements = value.toString() == QLatin1String("RetroAchievements");
        emit gamesChanged();
    } else if (key == QLatin1String("engines")) {
        // Saved whole by setEngine().
        m_engines = value.toMap();
    } else if (key == QLatin1String("play_at_startup") && m_appCore) {
        // A favourite's set, or "" for none: the app's startup favourite
        // made it, as a game's options would.
        const QString set = value.toString();
        if (set == startupSet())
            return;
        if (set.isEmpty()) {
            // None: no game of this module's starts with the app. Another
            // module's favourite is left as it is.
            if (!startupSet().isEmpty())
                m_appCore->save_setting(QString(), QStringLiteral("startup_favorite"), QString());
            return;
        }
        // Named as its favourite is.
        QString name = set;
        const QVariantList favorites = m_appCore->get_list(kModuleId, QStringLiteral("favorites"));
        for (const QVariant &v : favorites) {
            const QVariantMap entry = v.toMap();
            if (entry.value(QStringLiteral("path")).toString() == QStringLiteral("game:") + set)
                name = entry.value(QStringLiteral("name"), set).toString();
        }
        m_appCore->save_setting(QString(), QStringLiteral("startup_favorite"),
                                QVariantMap{ { QStringLiteral("module"), kModuleId },
                                             { QStringLiteral("path"), QStringLiteral("game:") + set },
                                             { QStringLiteral("name"), name } });
    }
}

QString ArcadeBackend::startupSet() const {
    if (!m_appCore)
        return QString();
    const QVariantMap startup = m_appCore->get_setting(QString(), QStringLiteral("startup_favorite")).toMap();
    const QString path = startup.value(QStringLiteral("path")).toString();
    if (startup.value(QStringLiteral("module")).toString() != kModuleId || !path.startsWith(QLatin1String("game:")))
        return QString();
    return path.mid(5);
}

void ArcadeBackend::syncStartupGame() {
    // Saving it comes back to applySetting(), which finds them the same.
    const QString set = startupSet();
    if (m_appCore->get_setting(kModuleId, QStringLiteral("play_at_startup")).toString() != set)
        m_appCore->save_setting(kModuleId, QStringLiteral("play_at_startup"), set);
}

void ArcadeBackend::setGamesFolder(const QString &path) {
    QString folder = path;
    if (folder.isEmpty()) {
        const QString media = legacy::env("MEDIA_DIR");
        folder = (media.isEmpty() ? m_dataRoot + QStringLiteral("/media") : media)
                 + QStringLiteral("/Games/Arcade");
    }
    if (folder == m_gamesFolder)
        return;
    m_gamesFolder = folder;
    m_listed = false;
    m_snaps.clear();
    m_sets.clear();
    m_misnamed.clear();
    m_folders.clear();
    m_otherFolders.clear();
    invalidate();
    qInfo("[Arcade] games folder: %s", qPrintable(m_gamesFolder));
}

void ArcadeBackend::setProblem(const QString &problem) {
    if (problem == m_problem)
        return;
    m_problem = problem;
    emit problemChanged();
}

QString ArcadeBackend::mamePath() const {
    // One of the user's own first (a MAME built for CRTs, like GroovyMAME),
    // as yt-dlp's is; then the system's.
    return mame::programPath(m_dataRoot);
}

QString ArcadeBackend::retroarchPath() const {
    return libretro::programPath(m_dataRoot);
}

QString ArcadeBackend::mameIdentity() const {
    const QFileInfo program(mamePath());
    return program.exists() ? fileStamp(program) : QString();
}

ArcadeBackend::Read ArcadeBackend::read(const QString &folder, const QString &home, bool withCache,
                                        const QString &genresFrom, const QString &dat, const QString &datFrom) {
    Read read;
    read.folder = folder;
    // There, so that whoever copies games onto the card sees where they go.
    QDir().mkpath(folder);

    // The sets: unsorted, and no check of each entry's permissions.
    QDirIterator it(folder, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const bool isFolder = info.isDir();
        QString name;
        QString asMame;   // what MAME opens it as
        if (isFolder) {
            name = info.fileName().toLower();
            if (kNotSets.contains(name)) {
                // As it is named: Samples on a filesystem that tells cases
                // apart isn't samples.
                read.otherFolders.insert(name, info.fileName());
                continue;
            }
            asMame = name;
        } else {
            const QString suffix = info.suffix().toLower();
            if (suffix != QLatin1String("zip") && suffix != QLatin1String("7z"))
                continue;
            name = info.completeBaseName().toLower();
            asMame = name + QLatin1Char('.') + suffix;
        }
        // A zip ahead of a folder of the same name (where a game's CHD is).
        if (read.sets.contains(name) && (isFolder || !read.folders.contains(name)))
            continue;
        read.sets.insert(name, info.fileName());
        if (isFolder)
            read.folders.insert(name);
        else
            read.folders.remove(name);
        // MAME opens a set by its name in lower case: where the filesystem
        // tells cases apart (ext4, not the card's exFAT), PACMAN.ZIP isn't
        // the pacman.zip it looks for.
        if (asMame != info.fileName() && mame::isShortName(name)
            && !QFileInfo::exists(folder + QLatin1Char('/') + asMame))
            read.misnamed.insert(name);
        else
            read.misnamed.remove(name);
    }

    // What MAME said of the sets, while it hasn't been read: with a full set
    // of MAME's games it is megabytes.
    read.cacheAsked = withCache;
    QFile cacheFile(home + QStringLiteral("/games.json"));
    if (withCache && cacheFile.open(QIODevice::ReadOnly)) {
        const QJsonObject cache = QJsonDocument::fromJson(cacheFile.readAll()).object();
        cacheFile.close();
        // Made by an older version, without what this one keeps of each game.
        if (cache.value(QStringLiteral("version")).toInt() != kCacheVersion) {
            qInfo("[Arcade] games.json is from an older version: asking MAME about every game again");
        } else {
            read.identity = cache.value(QStringLiteral("mame")).toString();
            read.build = cache.value(QStringLiteral("build")).toString();
            const QJsonObject machines = cache.value(QStringLiteral("machines")).toObject();
            for (auto m = machines.constBegin(); m != machines.constEnd(); ++m)
                read.machines.insert(m.key(), mame::Machine::fromVariant(m.key(), m.value().toObject().toVariantMap()));
            const QJsonArray unknown = cache.value(QStringLiteral("unknown")).toArray();
            for (const QJsonValue &name : unknown)
                read.unknown.insert(name.toString());
        }
    }

    // catver.ini beside the games, else in the module's own folder, read
    // again once it has changed (or another is found).
    QString catver;
    for (const QString &candidate : { folder + QStringLiteral("/catver.ini"), home + QStringLiteral("/catver.ini") }) {
        if (QFileInfo(candidate).isFile()) {
            catver = candidate;
            break;
        }
    }
    const QString from = catver.isEmpty() ? QString() : fileStamp(QFileInfo(catver));
    if (from != genresFrom) {
        read.genresRead = true;
        read.genresFrom = from;
        if (!catver.isEmpty()) {
            read.genres = mame::readGenres(catver);
            qInfo("[Arcade] %lld games' genres from %s", static_cast<long long>(read.genres.size()),
                  qPrintable(catver));
        }
    }

    // FinalBurn Neo's games, read again once its list has changed (an
    // update downloaded, or another found).
    const QString fbneoFrom = dat.isEmpty() ? QString() : fileStamp(QFileInfo(dat));
    if (fbneoFrom != datFrom) {
        read.fbneoRead = true;
        read.fbneoFrom = fbneoFrom;
        if (!dat.isEmpty()) {
            read.fbneoGames = libretro::readDat(dat);
            qInfo("[Arcade] %lld of FinalBurn Neo's games from %s", static_cast<long long>(read.fbneoGames.size()),
                  qPrintable(dat));
        }
    }

    // The pictures, looked for here once rather than file by file as the
    // games are chosen (exFAT's folders have no index): the games folder's
    // snap, named by the set as MAME names it, the way the picture packs for
    // MAME have them, a .png before a .jpg; and the first MAME took of each
    // game, in its own snap folder.
    QHash<QString, int> ranks;   // set → how well its picture's name fits
    QDirIterator snaps(folder + QLatin1Char('/') + read.otherFolders.value(QStringLiteral("snap"), QStringLiteral("snap")),
                       QDir::Files);
    while (snaps.hasNext()) {
        snaps.next();
        const QFileInfo info = snaps.fileInfo();
        const QString suffix = info.suffix().toLower();
        if (suffix != QLatin1String("png") && suffix != QLatin1String("jpg"))
            continue;
        // Its exact name first (as MAME's own folder has it), then a .png.
        const QString set = info.completeBaseName().toLower();
        const int rank = (info.fileName() == set + QLatin1Char('.') + suffix ? 2 : 0)
                         + (suffix == QLatin1String("png") ? 1 : 0);
        if (rank > ranks.value(set, -1)) {
            ranks.insert(set, rank);
            read.snaps.insert(set, info.filePath());
        }
    }
    QDirIterator shots(home + QStringLiteral("/snap"), QDir::Dirs | QDir::NoDotAndDotDot);
    while (shots.hasNext()) {
        shots.next();
        const QString shot = firstShot(shots.filePath());
        if (!shot.isEmpty())
            read.shots.insert(shots.fileName(), shot);
    }
    return read;
}

void ArcadeBackend::startRead() {
    const bool wasScanning = scanning();
    const bool wasReading = std::exchange(m_reading, true);
    m_watcher->setFuture(QtConcurrent::run(&ArcadeBackend::read, m_gamesFolder, m_home, !m_cacheLoaded,
                                           m_genresFrom, libretro::datPath(m_dataRoot), m_fbneoFrom));
    if (!wasReading)
        emit readingChanged();
    if (!wasScanning)
        emit scanningChanged();
}

void ArcadeBackend::onRead() {
    Read read = m_watcher->result();
    // games.json, whatever folder was read: it is the module's own.
    if (read.cacheAsked && !m_cacheLoaded) {
        m_cacheLoaded = true;
        m_identity = read.identity;
        m_build = read.build;
        m_machines = std::move(read.machines);
        m_unknown = std::move(read.unknown);
        invalidate();
    }
    // Another folder chosen meanwhile, or the list opened again since this
    // began: read again.
    const bool again = std::exchange(m_readAgain, false);
    if (again || read.folder != m_gamesFolder) {
        startRead();
        return;
    }
    m_listed = true;
    m_otherFolders = std::move(read.otherFolders);
    // The list stays as it was made while the folder is as it was.
    if (read.sets != m_sets || read.misnamed != m_misnamed) {
        m_sets = std::move(read.sets);
        m_misnamed = std::move(read.misnamed);
        m_folders = std::move(read.folders);
        invalidate();
    }
    if (read.genresRead) {
        m_genresFrom = read.genresFrom;
        m_genres = std::move(read.genres);
        invalidate();
    }
    if (read.fbneoRead) {
        m_fbneoFrom = read.fbneoFrom;
        m_fbneoGames = std::move(read.fbneoGames);
    }
    m_snaps = std::move(read.snaps);
    m_shots = std::move(read.shots);
    m_reading = false;
    // Read the Games Again, asked while this read went on, as rescan() does
    // it: unless MAME is being asked already (a game paused it), what it said
    // of every set is forgotten, and it is asked about them all.
    if (std::exchange(m_rescanAfterRead, false) && !scanning()) {
        forget();
        saveCache();
        m_scanAfterRead = true;
    }
    if (std::exchange(m_scanAfterRead, false)) {
        // A MAME changed (an update, a drop-in) may know the sets
        // differently: all of them are asked about again. With none there
        // is nothing to ask, and what the last one said is kept.
        const QString identity = mameIdentity();
        if (!identity.isEmpty() && identity != m_identity) {
            if (!m_identity.isEmpty())
                qInfo("[Arcade] MAME has changed: asking it about every game again");
            forget();
            m_identity = identity;
        }
        m_scanFailed = false;
        checkProblem();
        startScan();
    }
    emit readingChanged();
    // Unless MAME is asked about the sets now (startScan() says so).
    if (!scanning())
        emit scanningChanged();
    // The folders shown list the folder as it is now.
    emitShown();
    const QList<std::function<void()>> waiting = std::exchange(m_afterRead, {});
    for (const std::function<void()> &fn : waiting)
        fn();
}

void ArcadeBackend::afterRead(std::function<void()> fn) {
    if (m_listed && m_cacheLoaded && !m_reading) {
        fn();
        return;
    }
    m_afterRead << std::move(fn);
    if (!m_reading)
        startRead();
}

void ArcadeBackend::checkProblem() {
    // The folder first: without it there is no list for anything else to
    // stand in the way of.
    const QFileInfo folder(m_gamesFolder);
    m_folderReadable = folder.isDir() && folder.isReadable() && folder.isExecutable();
    if (!m_folderReadable)
        setProblem(QStringLiteral("The games folder can't be read: ") + m_gamesFolder);
    else if (mamePath().isEmpty())
        setProblem(QStringLiteral("MAME is not installed"));
    else if (m_scanFailed)
        setProblem(QStringLiteral("MAME could not list the games"));
    else
        setProblem(QString());
}

void ArcadeBackend::refresh() {
    // The folder, games.json until it has been read, catver.ini and the
    // pictures put there since, or taken in a game; then MAME is asked about
    // what it hasn't been (onRead()). A read under way is followed by
    // another, the folder having perhaps changed since it began.
    m_scanAfterRead = true;
    if (m_reading) {
        m_readAgain = true;
        return;
    }
    startRead();
}

void ArcadeBackend::rescan() {
    // Asked while the folder is read: once it has been (onRead()).
    if (m_reading) {
        m_rescanAfterRead = true;
        return;
    }
    if (scanning())
        return;
    m_cacheLoaded = true;
    forget();
    saveCache();
    refresh();
}

void ArcadeBackend::forget() {
    m_machines.clear();
    m_unknown.clear();
    m_build.clear();
    invalidate();
}

// ---------------------------------------------------------------------------
// What MAME says of the sets (-listxml), kept in games.json (read with the
// folder, read()).

void ArcadeBackend::saveCache() const {
    QJsonObject machines;
    for (auto it = m_machines.constBegin(); it != m_machines.constEnd(); ++it)
        machines.insert(it.key(), QJsonObject::fromVariantMap(it.value().toVariant()));
    QStringList unknown(m_unknown.cbegin(), m_unknown.cend());
    unknown.sort();
    const QJsonObject cache{ { QStringLiteral("version"), kCacheVersion },
                             { QStringLiteral("mame"), m_identity },
                             { QStringLiteral("build"), m_build },
                             { QStringLiteral("machines"), machines },
                             { QStringLiteral("unknown"), QJsonArray::fromStringList(unknown) } };
    QDir().mkpath(m_home);
    writeFileAtomically(m_home + QStringLiteral("/games.json"), QJsonDocument(cache).toJson(QJsonDocument::Compact));
}

void ArcadeBackend::startScan() {
    if (scanning() || mamePath().isEmpty())
        return;
    QStringList ask;
    for (auto it = m_sets.cbegin(); it != m_sets.cend(); ++it) {
        const QString &set = it.key();
        // Not one MAME couldn't open by its name: it is under Not Recognized
        // whatever MAME says.
        if (mame::isShortName(set) && !m_misnamed.contains(set) && !m_machines.contains(set)
            && !m_unknown.contains(set))
            ask << set;
    }
    if (ask.isEmpty())
        return;
    ask.sort();
    qInfo("[Arcade] Asking MAME about %lld games", static_cast<long long>(ask.size()));
    m_toAsk = ask;
    scanNext();
    emit scanningChanged();
}

void ArcadeBackend::scanNext() {
    m_asked = m_toAsk.mid(0, kBatch);
    m_toAsk = m_toAsk.mid(kBatch);
    m_reader.reset();
    QDir().mkpath(m_home);
    m_scan = new QProcess(this);
    m_scan->setWorkingDirectory(m_home);
    // What it says on stderr is only which sets it has no machine of, which
    // the XML says too.
    m_scan->setStandardErrorFile(QProcess::nullDevice());
    connect(m_scan, &QProcess::readyReadStandardOutput, this, [this] {
        m_reader.addData(m_scan->readAllStandardOutput());
        takeMachines();
    });
    connect(m_scan, &QProcess::finished, this, &ArcadeBackend::onScanFinished);
    connect(m_scan, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // finished() never comes for one that didn't start.
        if (error == QProcess::FailedToStart)
            onScanFinished(-1, QProcess::CrashExit);
    });
    m_scan->start(mamePath(), QStringList{ QStringLiteral("-listxml"), QStringLiteral("-nodtd") } + m_asked);
    m_scanTimer->start();
}

void ArcadeBackend::pauseScan() {
    if (!m_scan)
        return;
    // Not a failure: what it listed in full is kept, nothing is taken for a
    // set it has no machine of, and the asking goes on after the game.
    m_scanTimer->stop();
    m_scan->disconnect(this);
    m_scan->kill();
    m_scan->waitForFinished(1000);
    m_scan->deleteLater();
    m_scan = nullptr;
    QStringList again;
    for (const QString &name : std::as_const(m_asked)) {
        if (!m_machines.contains(name))
            again << name;
    }
    m_asked.clear();
    m_toAsk = again + m_toAsk;
    // It had listed every set it was asked about, and only had to exit.
    if (m_toAsk.isEmpty())
        finishScan();
}

void ArcadeBackend::takeMachines() {
    const QList<mame::Machine> machines = m_reader.take();
    for (const mame::Machine &machine : machines) {
        // Only what was asked: the devices the games are made of come too.
        if (m_asked.contains(machine.name)) {
            m_machines.insert(machine.name, machine);
            invalidate();
        }
    }
}

void ArcadeBackend::onScanFinished(int exitCode, QProcess::ExitStatus status) {
    if (!m_scan)
        return;
    m_scanTimer->stop();
    m_reader.addData(m_scan->readAllStandardOutput());
    takeMachines();
    // An answer whole: the sets it has no machine of it says it hasn't
    // (exit code 5), and those it lists it lists in full.
    const bool answered = status == QProcess::NormalExit
                          && (exitCode == 0 || exitCode == kNoSuchSystem) && !m_reader.failed();
    if (answered) {
        for (const QString &name : std::as_const(m_asked)) {
            if (!m_machines.contains(name))
                m_unknown.insert(name);
        }
        invalidate();
        if (!m_reader.build().isEmpty())
            m_build = m_reader.build();
    } else {
        qWarning("[Arcade] MAME could not list the games (exit %d): %s", exitCode,
                 qPrintable(m_scan->errorString()));
        m_scanFailed = true;
        // The rest are asked about the next time the list opens.
        m_toAsk.clear();
    }
    m_scan->disconnect(this);
    m_scan->deleteLater();
    m_scan = nullptr;
    if (!m_toAsk.isEmpty()) {
        // Not beside a game, which would share the machine with it: the rest
        // once it has ended.
        if (!m_launcher->isBusy())
            scanNext();
        return;
    }
    finishScan();
}

void ArcadeBackend::finishScan() {
    saveCache();
    checkProblem();
    emit scanningChanged();
    emitShown();
}

void ArcadeBackend::emitShown() {
    // Taken first: the folders shown ask again as these come in.
    const QSet<QString> shown = std::exchange(m_shown, {});
    for (const QString &path : shown)
        emit entriesReady(path);
    emit gamesChanged();
}

// ---------------------------------------------------------------------------
// The filters.

bool ArcadeBackend::isUnrecognized(const QString &set) const {
    return !mame::isShortName(set) || m_unknown.contains(set) || m_misnamed.contains(set);
}

bool ArcadeBackend::isGame(const QString &set) const {
    if (!m_sets.contains(set) || isUnrecognized(set))
        return false;
    // One MAME wasn't asked about (there is none) is listed by its name.
    const auto machine = m_machines.constFind(set);
    return machine == m_machines.constEnd() || machine->isGame();
}

QString ArcadeBackend::descriptionOf(const QString &set) const {
    const auto machine = m_machines.constFind(set);
    return machine != m_machines.constEnd() && !machine->description.isEmpty() ? machine->description : set;
}

QString ArcadeBackend::sortKey(const QString &set) const {
    return descriptionOf(set).toCaseFolded();
}

QString ArcadeBackend::titleOf(const QString &set) const {
    return mame::titleOf(descriptionOf(set));
}

const ArcadeBackend::Index &ArcadeBackend::index() const {
    if (m_indexed)
        return m_index;
    Index index;
    QList<std::pair<QString, QString>> byName;   // the name sorted by, the set
    byName.reserve(m_sets.size());
    for (auto it = m_sets.cbegin(); it != m_sets.cend(); ++it) {
        const QString &set = it.key();
        if (isUnrecognized(set))
            index.unknown << set;
        else if (isGame(set))
            byName.append({ sortKey(set), set });
    }
    std::sort(byName.begin(), byName.end());
    index.games.reserve(byName.size());
    for (const auto &[name, set] : std::as_const(byName)) {
        index.games << set;
        const auto machine = m_machines.constFind(set);
        const mame::Machine *known = machine == m_machines.constEnd() ? nullptr : &machine.value();
        for (const auto &kind : kKinds) {
            const QString k = QLatin1String(kind.first);
            const QString place = placeOf(k, set, known);
            if (!place.isEmpty())
                index.places[k][place] << set;
        }
    }
    index.unknown.sort();
    for (const auto &kind : kKinds) {
        const QString k = QLatin1String(kind.first);
        index.sorted.insert(k, sortPlaces(k, index.places.value(k).keys()));
    }
    m_index = std::move(index);
    m_indexed = true;
    return m_index;
}

QString ArcadeBackend::placeOf(const QString &kind, const QString &set, const mame::Machine *machine) const {
    if (kind == QLatin1String("genres")) {
        // A clone's own, else its parent's.
        const QString genre = m_genres.value(set);
        return genre.isEmpty() && machine && !machine->cloneOf.isEmpty() ? m_genres.value(machine->cloneOf) : genre;
    }
    if (!machine)
        return QString();
    if (kind == QLatin1String("players"))
        return machine->players > 0 ? QString::number(machine->players) : QString();
    if (kind == QLatin1String("years")) {
        const QString decade = mame::decadeOf(machine->year);
        return decade.isEmpty() ? QStringLiteral("unknown") : decade;
    }
    return mame::makerOf(machine->manufacturer);
}

QVariantList ArcadeBackend::placeFilters() const {
    QVariantList list;
    for (const auto &kind : kKinds) {
        const QString k = QLatin1String(kind.first);
        const QMap<QString, QStringList> places = index().places.value(k);
        const QList<std::pair<QString, QString>> sorted = index().sorted.value(k);
        for (const auto &[place, name] : sorted) {
            const QString id = k + QLatin1Char('/') + place;
            if (menuShows(id))
                list << filter(id, name, places.value(place).size());
        }
    }
    return list;
}

bool ArcadeBackend::menuShows(const QString &filter) const {
    return m_menuFilters.value(filter, shownByDefault(filter)).toBool();
}

QStringList ArcadeBackend::filterSets(const QString &filter) const {
    // The lists as they are kept, but for what is no game in the folder now:
    // Favorites by name, Recently Played latest first.
    if (filter == QLatin1String("favorites") || filter == QLatin1String("recent")) {
        QStringList sets;
        const QStringList listed = listedSets(filter);
        for (const QString &set : listed) {
            if (isGame(set) && !sets.contains(set))
                sets << set;
        }
        if (filter == QLatin1String("favorites")) {
            std::sort(sets.begin(), sets.end(),
                      [this](const QString &a, const QString &b) { return sortKey(a) < sortKey(b); });
        }
        return sets;
    }
    // The rest once MAME has said what the sets are.
    if (scanning())
        return QStringList();
    const Index &games = index();
    if (filter == QLatin1String("all"))
        return games.games;
    if (filter == QLatin1String("unknown"))
        return games.unknown;
    const int slash = filter.indexOf(QLatin1Char('/'));
    return slash < 0 ? QStringList() : games.places.value(filter.left(slash)).value(filter.mid(slash + 1));
}

QVariantList ArcadeBackend::filters() {
    QVariantList list;
    if (!scanning() && !index().games.isEmpty())
        list << filter(QStringLiteral("all"), QStringLiteral("All Games"), index().games.size());
    for (const auto &[id, name] : kLists) {
        const qsizetype count = menuShows(QLatin1String(id)) ? filterSets(QLatin1String(id)).size() : 0;
        if (count > 0)
            list << filter(QLatin1String(id), QLatin1String(name), count);
    }
    if (scanning())
        return list;
    list << placeFilters();
    // Not Recognized while Menu Filters shows it, and in All Games' place
    // when MAME knows none of the sets: the screen says why.
    const Index &games = index();
    if (!games.unknown.isEmpty() && (menuShows(QLatin1String(kUnknown.first)) || games.games.isEmpty()))
        list << filter(QLatin1String(kUnknown.first), QLatin1String(kUnknown.second), games.unknown.size());
    return list;
}

QVariantList ArcadeBackend::filterGames(const QString &filter, int first, int count) {
    QVariantList games;
    const QStringList sets = filterSets(filter);
    if (first < 0 || count <= 0 || first >= sets.size())
        return games;
    const bool unknown = filter == QLatin1String("unknown");
    const QStringList favorites = unknown ? QStringList()
                                  : filter == QLatin1String("favorites") ? sets
                                  : listedSets(QStringLiteral("favorites"));
    const int last = static_cast<int>(std::min<qsizetype>(sets.size(), qsizetype(first) + count));
    for (int i = first; i < last; ++i) {
        const QString &set = sets.at(i);
        if (unknown) {
            games << QVariantMap{ { QStringLiteral("set"), set }, { QStringLiteral("name"), m_sets.value(set) },
                                  { QStringLiteral("unknown"), true } };
        } else {
            games << QVariantMap{ { QStringLiteral("set"), set }, { QStringLiteral("name"), titleOf(set) },
                                  { QStringLiteral("favorite"), favorites.contains(set) } };
        }
    }
    return games;
}

int ArcadeBackend::filterIndex(const QString &filter, const QString &set) {
    return static_cast<int>(filterSets(filter).indexOf(set));
}

QVariantMap ArcadeBackend::gameDetails(const QString &set) {
    if (!m_sets.contains(set))
        return QVariantMap();
    if (isUnrecognized(set)) {
        return { { QStringLiteral("set"), set }, { QStringLiteral("name"), m_sets.value(set) },
                 { QStringLiteral("unknown"), true }, { QStringLiteral("detail"), unknownDetail(set) } };
    }
    QVariantMap details{ { QStringLiteral("set"), set }, { QStringLiteral("name"), descriptionOf(set) },
                         { QStringLiteral("playTime"), playTimeOf(set) },
                         { QStringLiteral("picture"), pictureOf(set) },
                         { QStringLiteral("engine"), engineOf(set) } };
    const auto machine = m_machines.constFind(set);
    if (machine == m_machines.constEnd())
        return details;
    details.insert(QStringLiteral("system"), mame::systemOf(machine->sourceFile));
    details.insert(QStringLiteral("maker"), mame::makerOf(machine->manufacturer));
    details.insert(QStringLiteral("year"), machine->year);
    details.insert(QStringLiteral("players"), machine->players);
    details.insert(QStringLiteral("vertical"), machine->rotate == 90 || machine->rotate == 270);
    return details;
}

QString ArcadeBackend::pictureOf(const QString &set) const {
    // A clone's own, else its parent's: the games folder's snap first, then
    // the first MAME took of it.
    QStringList sets{ set };
    const auto machine = m_machines.constFind(set);
    if (machine != m_machines.constEnd() && !machine->cloneOf.isEmpty())
        sets << machine->cloneOf;
    for (const QHash<QString, QString> *pictures : { &m_snaps, &m_shots }) {
        for (const QString &s : std::as_const(sets)) {
            const QString path = pictures->value(s);
            if (!path.isEmpty())
                return QUrl::fromLocalFile(path).toString();
        }
    }
    return QString();
}

QString ArcadeBackend::mameFileName(const QString &set) const {
    return m_folders.contains(set) ? set
                                   : set + QLatin1Char('.') + QFileInfo(m_sets.value(set)).suffix().toLower();
}

QString ArcadeBackend::unknownDetail(const QString &set) const {
    if (!mame::isShortName(set))
        return QStringLiteral("Not the name of a set MAME knows\nA set is named as MAME names its game: pacman.zip");
    if (m_misnamed.contains(set))
        return QStringLiteral("MAME looks for %1, in lower case\nRename the file to %1").arg(mameFileName(set));
    return QStringLiteral("MAME has no game called %1\nIts set may be for another version of MAME").arg(set);
}

QStringList ArcadeBackend::listedSets(const QString &list) const {
    QStringList sets;
    if (!m_appCore)
        return sets;
    const QVariantList stored = m_appCore->get_list(kModuleId, list);
    for (const QVariant &v : stored) {
        const QString path = v.toMap().value(QStringLiteral("path")).toString();
        if (path.startsWith(QLatin1String("game:")))
            sets << path.mid(5);
    }
    return sets;
}

void ArcadeBackend::startFilterOptions() {
    // Once the folder and games.json have been read (Settings opened before
    // the list ever was): the filters are made of them.
    afterRead([this] {
        // Favorites and Recently Played whether or not they hold a game yet.
        QVariantList options{ QVariantMap{ { QStringLiteral("id"), QStringLiteral("all") },
                                           { QStringLiteral("label"), QStringLiteral("All Games") } } };
        for (const auto &[id, label] : kLists) {
            if (menuShows(QLatin1String(id)))
                options << QVariantMap{ { QStringLiteral("id"), QLatin1String(id) },
                                        { QStringLiteral("label"), QLatin1String(label) } };
        }
        const QVariantList places = placeFilters();
        for (const QVariant &v : places) {
            const QVariantMap place = v.toMap();
            options << QVariantMap{ { QStringLiteral("id"), place.value(QStringLiteral("id")) },
                                    { QStringLiteral("label"), place.value(QStringLiteral("name")) } };
        }
        emit dynamicOptionsReady(QStringLiteral("start_filter"), options);
    });
}

void ArcadeBackend::menuFilterOptions() {
    afterRead([this] {
        const auto heading = [](const QString &label) {
            return QVariantMap{ { QStringLiteral("label"), label }, { QStringLiteral("heading"), true } };
        };
        const auto choice = [](const QString &id, const QString &label) {
            return QVariantMap{ { QStringLiteral("id"), id }, { QStringLiteral("label"), label },
                                { QStringLiteral("default"), shownByDefault(id) } };
        };
        // The lists whether or not they hold a game yet.
        QVariantList options{ heading(QStringLiteral("Lists")) };
        for (const auto &[id, label] : kLists)
            options << choice(QLatin1String(id), QLatin1String(label));
        options << choice(QLatin1String(kUnknown.first), QLatin1String(kUnknown.second));
        for (const auto &[kind, label] : kKinds) {
            const QString k = QLatin1String(kind);
            const QList<std::pair<QString, QString>> sorted = index().sorted.value(k);
            if (sorted.isEmpty())
                continue;
            options << heading(QLatin1String(label));
            for (const auto &[place, name] : sorted)
                options << choice(k + QLatin1Char('/') + place, name);
        }
        emit dynamicOptionsReady(QStringLiteral("menu_filters"), options);
    });
}

void ArcadeBackend::startupGameOptions() {
    // The favorites, by their titles as they were put there: the app's
    // startup favourite is one of them.
    QList<std::pair<QString, QString>> games;   // the name, the set
    const QVariantList favorites = m_appCore ? m_appCore->get_list(kModuleId, QStringLiteral("favorites")) : QVariantList();
    for (const QVariant &v : favorites) {
        const QVariantMap entry = v.toMap();
        const QString path = entry.value(QStringLiteral("path")).toString();
        if (!path.startsWith(QLatin1String("game:")))
            continue;
        const QString set = path.mid(5);
        const QString name = entry.value(QStringLiteral("name")).toString();
        games.append({ mame::titleOf(name.isEmpty() ? set : name), set });
    }
    std::sort(games.begin(), games.end(), [](const auto &a, const auto &b) {
        return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
    });
    QVariantList options{ QVariantMap{ { QStringLiteral("id"), QString() }, { QStringLiteral("label"), QStringLiteral("None") } } };
    for (const auto &[name, set] : std::as_const(games))
        options << QVariantMap{ { QStringLiteral("id"), set }, { QStringLiteral("label"), name } };
    emit dynamicOptionsReady(QStringLiteral("play_at_startup"), options);
}

// ---------------------------------------------------------------------------
// The original screen's tree.

QVariantList ArcadeBackend::folders(const QString &kind) const {
    QVariantList list;
    const QList<std::pair<QString, QString>> sorted = index().sorted.value(kind);
    for (const auto &[place, name] : sorted)
        list << folder(name, kind + QLatin1Char('/') + place);
    return list;
}

QVariantMap ArcadeBackend::gameEntry(const QString &set) const {
    const auto machine = m_machines.constFind(set);
    const bool known = machine != m_machines.constEnd();
    QStringList detail;
    if (known) {
        if (!machine->year.isEmpty())
            detail << machine->year;
        if (!machine->manufacturer.isEmpty())
            detail << machine->manufacturer;
        if (machine->players > 0)
            detail << (machine->players == 1 ? QStringLiteral("1 player")
                                             : QStringLiteral("%1 players").arg(machine->players));
        if (machine->status == QLatin1String("preliminary"))
            detail << QStringLiteral("Not working in MAME");
        else if (machine->status == QLatin1String("imperfect"))
            detail << QStringLiteral("Imperfect in MAME");
    }
    detail << m_sets.value(set);
    return { { QStringLiteral("name"), descriptionOf(set) },
             { QStringLiteral("path"), QStringLiteral("game:") + set },
             { QStringLiteral("isFolder"), false },
             { QStringLiteral("game"), set },
             { QStringLiteral("detail"), detail.join(QLatin1Char('\n')) } };
}

QVariantList ArcadeBackend::gameEntries(const QStringList &sets) const {
    QVariantList entries;
    entries.reserve(sets.size());
    for (const QString &set : sets)
        entries << gameEntry(set);
    return entries;
}

QVariantMap ArcadeBackend::unknownEntry(const QString &set) const {
    return { { QStringLiteral("name"), m_sets.value(set) },
             { QStringLiteral("path"), QStringLiteral("unknown:") + set },
             { QStringLiteral("isFolder"), false },
             { QStringLiteral("kind"), QStringLiteral("unknown") },
             { QStringLiteral("detail"), unknownDetail(set) } };
}

QVariantList ArcadeBackend::listEntries(const QString &list) const {
    QVariantList entries;
    const QStringList sets = listedSets(list);
    for (const QString &set : sets) {
        // Those still in the folder, as MAME says they are now.
        if (m_sets.contains(set))
            entries << gameEntry(set);
    }
    return entries;
}

QVariant ArcadeBackend::requestEntries(const QString &path) {
    // To be listed again should what it holds change.
    m_shown.insert(path);
    if (path == QLatin1String("recent") || path == QLatin1String("favorites"))
        return listEntries(path);
    if (scanning()) {
        // The lists at once; the rest once MAME has said what the sets are.
        if (path == QLatin1String("top"))
            return QVariantList{ folder(QStringLiteral("Recently Played"), QStringLiteral("recent")),
                                 folder(QStringLiteral("Favorites"), QStringLiteral("favorites")) };
        return QVariant();
    }
    const Index &games = index();
    if (path == QLatin1String("top")) {
        QVariantList top;
        if (!games.games.isEmpty()) {
            top << folder(QStringLiteral("Recently Played"), QStringLiteral("recent"))
                << folder(QStringLiteral("Favorites"), QStringLiteral("favorites"))
                << QVariantMap{ { QStringLiteral("name"), QStringLiteral("Search") },
                                { QStringLiteral("path"), QStringLiteral("search") },
                                { QStringLiteral("isFolder"), false },
                                { QStringLiteral("kind"), QStringLiteral("search") } }
                << folder(QStringLiteral("All Games"), QStringLiteral("all"));
            // Only with what MAME said of them.
            for (const auto &[kind, name] : { std::pair{ "players", "Players" }, std::pair{ "years", "Years" },
                                              std::pair{ "makers", "Makers" } }) {
                if (!games.places.value(QLatin1String(kind)).isEmpty())
                    top << folder(QLatin1String(name), QLatin1String(kind));
            }
        }
        if (!games.unknown.isEmpty())
            top << folder(QLatin1String(kUnknown.second), QLatin1String(kUnknown.first));
        return top;
    }
    if (path == QLatin1String("all"))
        return gameEntries(games.games);
    if (path.startsWith(QLatin1String("search/"))) {
        const QString words = path.mid(7).trimmed();
        QStringList found;
        for (const QString &set : games.games) {
            if (set.contains(words, Qt::CaseInsensitive) || descriptionOf(set).contains(words, Qt::CaseInsensitive))
                found << set;
        }
        return gameEntries(found);
    }
    for (const char *kind : kFolderKinds) {
        const QString k = QLatin1String(kind);
        if (path == k)
            return folders(k);
        if (path.startsWith(k + QLatin1Char('/')))
            return gameEntries(games.places.value(k).value(path.mid(k.size() + 1)));
    }
    if (path == QLatin1String("unknown")) {
        QVariantList unknown;
        for (const QString &set : games.unknown)
            unknown << unknownEntry(set);
        return unknown;
    }
    return QVariantList();
}

// ---------------------------------------------------------------------------
// How long each game has been played.

qint64 ArcadeBackend::playTimeOf(const QString &set) {
    if (!m_playTimeLoaded) {
        m_playTimeLoaded = true;
        QFile file(m_home + QStringLiteral("/playtime.json"));
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject times = QJsonDocument::fromJson(file.readAll()).object();
            for (auto it = times.constBegin(); it != times.constEnd(); ++it)
                m_playTime.insert(it.key(), static_cast<qint64>(it.value().toDouble()));
        }
    }
    return m_playTime.value(set);
}

void ArcadeBackend::addPlayTime(const QString &set, qint64 seconds) {
    if (seconds <= 0)
        return;
    m_playTime.insert(set, playTimeOf(set) + seconds);
    QJsonObject times;
    for (auto it = m_playTime.constBegin(); it != m_playTime.constEnd(); ++it)
        times.insert(it.key(), static_cast<double>(it.value()));
    QDir().mkpath(m_home);
    if (!writeFileAtomically(m_home + QStringLiteral("/playtime.json"), QJsonDocument(times).toJson(QJsonDocument::Compact)))
        qWarning("[Arcade] The play times could not be saved");
}

// ---------------------------------------------------------------------------
// Playing.

bool ArcadeBackend::running() const {
    return m_launcher->isBusy();
}

QString ArcadeBackend::output() const {
    return m_launcher->outputText();
}

QString ArcadeBackend::otherFolder(const QString &name) const {
    const QString folder = m_gamesFolder + QLatin1Char('/') + m_otherFolders.value(name, name);
    return QFileInfo(folder).isDir() ? folder : QString();
}

QString ArcadeBackend::rompath() const {
    // BIOS sets beside the games, or in a bios folder there.
    const QString bios = otherFolder(QStringLiteral("bios"));
    return bios.isEmpty() ? m_gamesFolder : m_gamesFolder + QLatin1Char(';') + bios;
}

QString ArcadeBackend::pluginsPath() const {
    return mame::pluginsFolder(mamePath());
}

QString ArcadeBackend::ownFolder() const {
    const QFileInfo program(mamePath());
    if (!program.exists())
        return QString();
    // Where it really is first: bin/mame may be a link to it.
    for (const QString &dir : { QFileInfo(program.canonicalFilePath()).absolutePath(), program.absolutePath() }) {
        if (QFileInfo(dir + QStringLiteral("/bgfx")).isDir() || QFileInfo(dir + QStringLiteral("/plugins")).isDir())
            return dir;
    }
    return QString();
}

bool ArcadeBackend::play(const QString &name, int credits) {
    m_lastError.clear();
    if (m_launcher->isBusy()) {
        m_lastError = QStringLiteral("A game is still running");
        return false;
    }
    if (engineOf(name) == QLatin1String("fbneo"))
        return playFbneo(name);
    const QString program = mamePath();
    if (program.isEmpty()) {
        m_lastError = QStringLiteral("MAME is not installed");
        return false;
    }
    // Only a set MAME may have a game of: a name that is one can't be taken
    // for one of MAME's options either.
    if (!mame::isShortName(name)) {
        m_lastError = QStringLiteral("MAME has no game called ") + name;
        return false;
    }
    // A set not in the folder as it was read (a startup favourite, before
    // it has been): looked for by itself, by the names MAME opens it by.
    const QString inFolder = m_gamesFolder + QLatin1Char('/') + name;
    if (!m_sets.contains(name) && !QFileInfo(inFolder + QStringLiteral(".zip")).isFile()
        && !QFileInfo(inFolder + QStringLiteral(".7z")).isFile() && !QFileInfo(inFolder).isDir()) {
        m_lastError = QStringLiteral("The game is no longer in the games folder");
        return false;
    }
    if (m_misnamed.contains(name)) {
        m_lastError = QStringLiteral("MAME looks for %1, in lower case: rename the file to %1")
                          .arg(mameFileName(name));
        return false;
    }

    // MAME's keeping, and the players as Settings → Controls has them now.
    for (const char *dir : { "cfg", "nvram", "ctrlr" })
        QDir().mkpath(m_home + QLatin1Char('/') + QLatin1String(dir));
    if (!writeFileAtomically(m_home + QStringLiteral("/ctrlr/osdos.cfg"),
                             mame::controllerConfig(controllers::playerGuids()).toUtf8())) {
        m_lastError = QStringLiteral("MAME's controller file could not be written");
        return false;
    }

    QStringList args{ name,
                      QStringLiteral("-rompath"), rompath(),
                      QStringLiteral("-homepath"), m_home };
    for (const auto &[option, dir] : kOutputs)
        args << QLatin1String(option) << m_home + QLatin1Char('/') + QLatin1String(dir);
    args << QStringLiteral("-ctrlrpath") << m_home + QStringLiteral("/ctrlr")
         << QStringLiteral("-ctrlr") << QStringLiteral("osdos")
         << QStringLiteral("-skip_gameinfo");
    // The sounds some early games play from recordings (Donkey Kong's,
    // Space Invaders'), in a samples folder beside the games.
    const QString samples = otherFolder(QStringLiteral("samples"));
    if (!samples.isEmpty())
        args << QStringLiteral("-samplepath") << samples;
    // The hiscore plugin keeps each game's table in <data>/arcade/hiscore,
    // for those that forget it at power-off (most of the early ones).
    if (m_highScores) {
        const QString plugins = pluginsPath();
        if (plugins.isEmpty())
            qWarning("[Arcade] MAME's hiscore plugin wasn't found: high scores won't be kept");
        else
            args << QStringLiteral("-pluginspath") << plugins << QStringLiteral("-plugin") << QStringLiteral("hiscore");
    }
    if (!m_video.isEmpty())
        args << QStringLiteral("-video") << m_video;
    // MAME may not start at all without a sound card it can open (no USB
    // card on a Pi 5 whose HDMI is off): the game is played in silence.
    if (!AudioOutput::hasPlaybackCard())
        args << QStringLiteral("-sound") << QStringLiteral("none");
    // The credits bought on the Arcade screen, put in as the game starts.
    // Without the script the game still starts, with no credits.
    if (credits > 0) {
        const QString coins = m_home + QStringLiteral("/coins.lua");
        if (writeFileAtomically(coins, mame::coinScript(std::min(credits, kMaxCredits)).toUtf8()))
            args << QStringLiteral("-autoboot_script") << coins;
        else
            qWarning("[Arcade] The credits could not be written for MAME: the game starts without them");
    }

    const auto machine = m_machines.constFind(name);
    ScriptEntry entry;
    entry.path = program;
    entry.basename = QStringLiteral("mame");
    entry.meta.name = descriptionOf(name);
    entry.meta.mode = QStringLiteral("takeover");
    entry.argv = args;
    // A MAME of its own folder runs there, as by hand, to find its files
    // (MAME's paths are relative to where it runs) and its own mame.ini.
    // What it writes goes to <data>/arcade either way (kOutputs).
    const QString own = ownFolder();
    entry.workingDirectory = own.isEmpty() ? m_home : own;

    // MAME asked about the sets meanwhile would share the machine with the
    // game: it is stopped, and asked again once the game has ended.
    pauseScan();
    QString error;
    if (!m_launcher->start(entry, &error)) {
        m_lastError = error;
        if (!m_scan && !m_toAsk.isEmpty())
            scanNext();
        return false;
    }
    m_playing = name;
    m_playingEngine = QStringLiteral("mame");
    m_playClock.start();
    if (!m_played) {
        m_played = true;
        emit playedChanged();
    }
    return true;
}

QString ArcadeBackend::engineOf(const QString &set) const {
    const bool fbneo = fbneoReady() && m_fbneoGames.contains(set);
    const QString chosen = m_engines.value(set).toString();
    if (chosen == QLatin1String("mame") && !mamePath().isEmpty())
        return QStringLiteral("mame");
    if (fbneo && (chosen == QLatin1String("fbneo") || m_achievements || mamePath().isEmpty()))
        return QStringLiteral("fbneo");
    return QStringLiteral("mame");
}

QVariantList ArcadeBackend::engineChoices(const QString &set) const {
    const QString chosen = m_engines.value(set).toString();
    const auto choice = [&chosen](const QString &id, const QString &label) {
        return QVariantMap{ { QStringLiteral("id"), id }, { QStringLiteral("label"), label },
                            { QStringLiteral("current"), chosen == id || (id == QLatin1String("auto") && chosen.isEmpty()) } };
    };
    // Auto as it stands: what the game would play in left to it.
    const bool fbneo = fbneoReady() && m_fbneoGames.contains(set);
    const bool autoFbneo = fbneo && (m_achievements || mamePath().isEmpty());
    QVariantList choices{ choice(QStringLiteral("auto"),
                                 autoFbneo ? QStringLiteral("Auto (FinalBurn Neo)") : QStringLiteral("Auto (MAME)")),
                          choice(QStringLiteral("mame"), QStringLiteral("MAME")) };
    if (fbneo)
        choices << choice(QStringLiteral("fbneo"), QStringLiteral("FinalBurn Neo"));
    return choices;
}

void ArcadeBackend::setEngine(const QString &set, const QString &engine) {
    if (!m_appCore || !mame::isShortName(set))
        return;
    QVariantMap engines = m_engines;
    if (engine == QLatin1String("mame") || engine == QLatin1String("fbneo"))
        engines.insert(set, engine);
    else
        engines.remove(set);
    // Comes back through onSettingChanged(), which takes it.
    m_appCore->save_setting(kModuleId, QStringLiteral("engines"), engines);
}

bool ArcadeBackend::fbneoReady() const {
    return !retroarchPath().isEmpty() && !libretro::corePath(m_dataRoot).isEmpty() && !m_fbneoGames.isEmpty();
}

bool ArcadeBackend::playFbneo(const QString &name) {
    const QString program = retroarchPath();
    if (program.isEmpty()) {
        m_lastError = QStringLiteral("RetroArch is not installed");
        return false;
    }
    const QString core = libretro::corePath(m_dataRoot);
    if (core.isEmpty()) {
        m_lastError = QStringLiteral("FinalBurn Neo is not installed: Settings → Arcade → FinalBurn Neo");
        return false;
    }
    if (!m_fbneoGames.contains(name)) {
        m_lastError = QStringLiteral("FinalBurn Neo has no game called ") + name;
        return false;
    }
    // FinalBurn Neo opens a zip or a 7z, never a folder of files.
    const QString file = m_sets.value(name);
    if (file.isEmpty() || m_folders.contains(name)) {
        m_lastError = m_folders.contains(name) ? QStringLiteral("FinalBurn Neo plays a game's zip or 7z, not a folder")
                                               : QStringLiteral("The game is no longer in the games folder");
        return false;
    }
    if (m_misnamed.contains(name)) {
        m_lastError = QStringLiteral("FinalBurn Neo looks for %1, in lower case: rename the file to %1")
                          .arg(mameFileName(name));
        return false;
    }

    // RetroArch's keeping, in a folder of its own: a config with a token in
    // it is no one else's to read.
    const QString home = retroarchHome();
    for (const char *dir : { "system/fbneo", "saves", "states", "config" })
        QDir().mkpath(home + QLatin1Char('/') + QLatin1String(dir));
    QFile::setPermissions(home, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    // The BIOS sets: FinalBurn Neo looks beside the game, then in
    // system/fbneo, where the games folder's bios folder's are linked anew.
    const QString fbneo = home + QStringLiteral("/system/fbneo");
    QDirIterator links(fbneo, QDir::Files | QDir::System);
    while (links.hasNext()) {
        links.next();
        if (links.fileInfo().isSymLink())
            QFile::remove(links.filePath());
    }
    const QString bios = otherFolder(QStringLiteral("bios"));
    if (!bios.isEmpty()) {
        QDirIterator sets(bios, { QStringLiteral("*.zip"), QStringLiteral("*.7z") }, QDir::Files);
        while (sets.hasNext()) {
            sets.next();
            QFile::link(sets.filePath(), fbneo + QLatin1Char('/') + sets.fileName());
        }
    }

    libretro::Launch launch;
    launch.core = core;
    launch.content = m_gamesFolder + QLatin1Char('/') + file;
    launch.config = home + QStringLiteral("/retroarch.cfg");
    launch.log = home + QStringLiteral("/last.log");
    launch.systemDir = home + QStringLiteral("/system");
    launch.saveDir = home + QStringLiteral("/saves");
    launch.stateDir = home + QStringLiteral("/states");
    launch.configDir = home + QStringLiteral("/config");
#ifdef Q_OS_LINUX
    // A desktop's RetroArch draws in its window; the console's needs KMS.
    launch.console = qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY");
#else
    launch.console = false;
#endif
    launch.sound = AudioOutput::hasPlaybackCard();
    const QStringList guids = controllers::playerGuids();
    const QStringList names = controllers::playerNames();
    for (int p = 0; p < guids.size(); ++p)
        launch.pads << (guids.at(p).isEmpty() ? QString() : libretro::reservedDevice(guids.at(p), names.value(p)));
    m_raPasswordGiven = false;
    if (m_achievements && !m_raUser.isEmpty()) {
        launch.achievementsUser = m_raUser;
        launch.achievementsToken = m_raToken;
        if (m_raToken.isEmpty() && !m_raPassword.isEmpty()) {
            launch.achievementsPassword = m_raPassword;
            m_raPasswordGiven = true;
        }
    }
    if (!writeFileAtomically(launch.config, libretro::configText(launch).toUtf8(), kOwnerOnly)) {
        m_lastError = QStringLiteral("RetroArch's settings could not be written");
        return false;
    }
    // This run's log: the last one's mustn't speak for it.
    QFile::remove(launch.log);

    ScriptEntry entry;
    entry.path = program;
    entry.basename = QStringLiteral("retroarch");
    entry.meta.name = descriptionOf(name);
    entry.meta.mode = QStringLiteral("takeover");
    entry.argv = libretro::arguments(launch);
    entry.workingDirectory = home;
    // The pads the menus know of the controller database OSD/OS reads, for
    // RetroArch's SDL too.
    const QString mappings = m_dataRoot + QStringLiteral("/gamecontrollerdb.txt");
    if (QFileInfo::exists(mappings))
        entry.environment << qMakePair(QStringLiteral("SDL_GAMECONTROLLERCONFIG_FILE"), mappings);

    // As for MAME: the batch MAME is asked meanwhile shares the machine.
    pauseScan();
    QString error;
    if (!m_launcher->start(entry, &error)) {
        m_lastError = error;
        if (m_raPasswordGiven) {
            m_raPasswordGiven = false;
            QFile::remove(launch.config);
        }
        if (!m_scan && !m_toAsk.isEmpty())
            scanNext();
        return false;
    }
    m_playing = name;
    m_playingEngine = QStringLiteral("fbneo");
    m_playClock.start();
    if (!m_played) {
        m_played = true;
        emit playedChanged();
    }
    return true;
}

QString ArcadeBackend::fbneoNotice(int exitCode, const QString &reason, const libretro::Run &run) const {
    if (reason == QLatin1String("ok"))
        return QString();
    if (reason == QLatin1String("failed_to_start"))
        return QStringLiteral("RetroArch did not start");
    if (reason == QLatin1String("stopped"))
        return QStringLiteral("The game was stopped");
    if (m_launcher->lastRunCrashed())
        return QStringLiteral("RetroArch closed unexpectedly (signal %1)").arg(exitCode);
    if (!run.missing.isEmpty()) {
        QString files = run.missing.mid(0, 3).join(QStringLiteral(", "));
        if (run.missing.size() > 3)
            files += QStringLiteral(" and %1 more").arg(run.missing.size() - 3);
        return QStringLiteral("Missing %1: FinalBurn Neo's sets aren't always MAME's\n"
                              "Choose MAME for the game in its options").arg(files);
    }
    if (!run.started && exitCode == 0)
        return QStringLiteral("FinalBurn Neo could not start the game: its set may be for another version");
    if (!run.error.isEmpty())
        return QStringLiteral("RetroArch stopped: ") + run.error;
    return QStringLiteral("RetroArch closed with error %1").arg(exitCode);
}

QVariantMap ArcadeBackend::fbneoInfo() const {
    const QString core = libretro::corePath(m_dataRoot);
    const QString license = libretro::downloadedLicense(m_dataRoot);
    const QString imageLicense = QStringLiteral("/usr/local/share/doc/fbneo/license.txt");
#ifdef OSDOS_MINIZIP
    const bool unpacks = true;
#else
    const bool unpacks = false;
#endif
    return { { QStringLiteral("retroarch"), retroarchPath() },
             { QStringLiteral("core"), core },
             { QStringLiteral("coreDate"), core.isEmpty() ? QString()
                                           : QFileInfo(core).lastModified().toString(QStringLiteral("yyyy-MM-dd")) },
             { QStringLiteral("downloaded"), !core.isEmpty() && core == libretro::downloadedCore(m_dataRoot) },
             { QStringLiteral("games"), static_cast<int>(m_fbneoGames.size()) },
             { QStringLiteral("license"), QFileInfo::exists(license) ? license
                                          : QFileInfo::exists(imageLicense) ? imageLicense : QString() },
             { QStringLiteral("canDownload"), unpacks && !FbneoInstaller::defaultSource().core.isEmpty() } };
}

QString ArcadeBackend::fbneoLicenseText() const {
    QFile file(fbneoInfo().value(QStringLiteral("license")).toString());
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void ArcadeBackend::downloadFbneo() {
    if (m_installer->running())
        return;
    m_fbneoMessage.clear();
    m_installer->start(FbneoInstaller::defaultSource());
    emit fbneoChanged();
}

bool ArcadeBackend::fbneoDownloading() const {
    return m_installer->running();
}

int ArcadeBackend::fbneoProgress() const {
    return m_installer->progress();
}

QString ArcadeBackend::achievementsMessage() const {
    if (achievementsState() == QLatin1String("failed") && m_raFailure.isEmpty())
        return QStringLiteral("Sign in again: the password is only kept until the next game");
    return m_raFailure;
}

QString ArcadeBackend::achievementsState() const {
    if (m_raUser.isEmpty())
        return QStringLiteral("none");
    if (!m_raToken.isEmpty())
        return QStringLiteral("signedIn");
    if (!m_raPassword.isEmpty())
        return QStringLiteral("pending");
    return QStringLiteral("failed");
}

void ArcadeBackend::signInAchievements(const QString &user, const QString &password) {
    const QString name = user.trimmed();
    if (name.isEmpty() || password.isEmpty())
        return;
    m_raUser = name;
    m_raToken.clear();
    m_raPassword = password;
    m_raFailure.clear();
    saveAchievements();
    emit achievementsChanged();
}

void ArcadeBackend::signOutAchievements() {
    m_raUser.clear();
    m_raToken.clear();
    m_raPassword.clear();
    m_raFailure.clear();
    saveAchievements();
    emit achievementsChanged();
}

void ArcadeBackend::setAchievementsKey(const QString &key) {
    m_raKey = key.trimmed();
    m_raKeyRefused = false;
    m_leaderboards->setKey(m_raKey);
    saveAchievements();
    emit achievementsChanged();
}

QVariantMap ArcadeBackend::leaderboard(const QString &set) {
    const QString file = m_sets.value(set);
    if (!m_achievements || m_raKey.isEmpty() || file.isEmpty() || engineOf(set) != QLatin1String("fbneo"))
        return { { QStringLiteral("state"), QStringLiteral("off") } };
    return m_leaderboards->board(QFileInfo(file).fileName());
}

void ArcadeBackend::saveAchievements() const {
    QDir().mkpath(m_home);
    const QJsonObject kept{ { QStringLiteral("user"), m_raUser }, { QStringLiteral("token"), m_raToken },
                            { QStringLiteral("key"), m_raKey } };
    writeFileAtomically(m_home + QStringLiteral("/retroachievements.json"),
                        QJsonDocument(kept).toJson(QJsonDocument::Compact), kOwnerOnly);
}

void ArcadeBackend::takeAchievements(const libretro::Run &run) {
    const QString config = retroarchHome() + QStringLiteral("/retroarch.cfg");
    bool changed = false;
    if (std::exchange(m_raPasswordGiven, false)) {
        // RetroArch saved its config on exit, the token in it if it signed
        // in; the password goes whatever became of it.
        const QString token = libretro::savedToken(config);
        QFile::remove(config);
        m_raPassword.clear();
        if (!token.isEmpty()) {
            m_raToken = token;
            m_raFailure.clear();
        } else {
            m_raFailure = run.loginFailure.isEmpty() ? QStringLiteral("RetroArch did not sign in: sign in again")
                                                     : run.loginFailure;
        }
        saveAchievements();
        changed = true;
    } else if (!m_raToken.isEmpty() && !run.loginFailure.isEmpty()) {
        // The token no longer taken (the password changed since): signed out.
        m_raToken.clear();
        m_raFailure = run.loginFailure;
        saveAchievements();
        changed = true;
    }
    if (changed)
        emit achievementsChanged();
}

QString ArcadeBackend::noticeFor(int exitCode, const QString &reason) const {
    if (reason == QLatin1String("ok"))
        return QString();
    if (reason == QLatin1String("failed_to_start"))
        return QStringLiteral("MAME did not start");
    if (reason == QLatin1String("stopped"))
        return QStringLiteral("The game was stopped");
    const QString output = m_launcher->outputText();
    if (m_launcher->lastRunCrashed()) {
        // Nothing MAME printed says why: it didn't get to.
        return QStringLiteral("MAME closed unexpectedly (signal %1)\n"
                              "Try another Video in the Arcade module's settings").arg(exitCode);
    }
    switch (exitCode) {
    case kMissingFiles: {
        const mame::Missing missing = mame::missingFiles(output);
        if (missing.files.isEmpty())
            return QStringLiteral("Files the game needs are missing");
        QString files = missing.files.mid(0, 3).join(QStringLiteral(", "));
        if (missing.files.size() > 3)
            files += QStringLiteral(" and %1 more").arg(missing.files.size() - 3);
        // As the sets are in the folder (a zip, a 7z or a folder of files);
        // one that isn't there, as a zip.
        QStringList sets;
        for (const QString &set : missing.sets) {
            sets << (!m_sets.contains(set)     ? set + QStringLiteral(".zip")
                     : m_folders.contains(set) ? m_sets.value(set) + QLatin1Char('/')
                                               : m_sets.value(set));
        }
        return sets.isEmpty() ? QStringLiteral("Missing %1").arg(files)
                              : QStringLiteral("Missing %1, looked for in %2").arg(files, sets.join(QStringLiteral(", ")));
    }
    case kNoSuchSystem:
        return QStringLiteral("MAME has no game called ") + m_playing;
    case kInvalidConfig:
        return QStringLiteral("MAME's settings are not right: ") + mame::lastError(output);
    default: {
        const QString line = mame::lastError(output);
        if (line.isEmpty())
            return QStringLiteral("MAME closed with error %1").arg(exitCode);
        QString notice = QStringLiteral("MAME stopped: ") + line;
        // MAME's picture: the module's Video setting may draw it another way.
        static const QRegularExpression video(QStringLiteral("opengl|bgfx|video|render|window|display|egl|drm"),
                                              QRegularExpression::CaseInsensitiveOption);
        if (video.match(line).hasMatch())
            notice += QStringLiteral("\nTry another Video in the Arcade module's settings");
        return notice;
    }
    }
}
