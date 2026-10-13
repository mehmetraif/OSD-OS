#include "ArcadeBackend.h"
#include "../scripts/ScriptLauncher.h"
#include "../../AppCore.h"
#include "../../audio/AudioOutput.h"
#include "../../input/ControllerProfile.h"
#include "../../util/AtomicFile.h"
#include "../../util/LegacyNames.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace {

const QString kModuleId = QStringLiteral("com.osdos.arcade");

// MAME asked about this many sets at a time: a short enough command line, and
// what it says comes in batches.
constexpr int kBatch = 300;
// A batch MAME hasn't answered in this long is given up on.
constexpr int kScanTimeoutMs = 120000;

// MAME's exit codes (src/emu/main.h).
constexpr int kMissingFiles = 2;
constexpr int kNoSuchSystem = 5;
constexpr int kInvalidConfig = 6;

// Folders beside the games that aren't games: the BIOS sets' (on MAME's
// rompath too) and the samples' (its samplepath).
const QStringList kNotSets = { QStringLiteral("bios"), QStringLiteral("samples") };

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

// The tree's folders by players, decade and maker.
const char *const kKinds[] = { "players", "years", "makers" };

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

} // namespace

ArcadeBackend::ArcadeBackend(const QString &appRoot, const QString &dataRoot, AppCore *appCore,
                             DisplayHandoff *handoff, QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot),
      m_home(dataRoot + QStringLiteral("/arcade")), m_appCore(appCore)
{
    m_launcher = new ScriptLauncher(m_appRoot, m_dataRoot, handoff, this);
    m_launcher->setHandoffOwner(QStringLiteral("arcade"));
    connect(m_launcher, &ScriptLauncher::runningChanged, this, &ArcadeBackend::runningChanged);
    connect(m_launcher, &ScriptLauncher::finished, this, [this](int exitCode, const QString &reason) {
        const QString notice = noticeFor(exitCode, reason);
        if (!notice.isEmpty())
            qWarning("[Arcade] %s: %s", qPrintable(m_playing), qPrintable(notice));
        m_playing.clear();
        emit finished(exitCode, reason, notice);
        // The sets MAME was still to be asked about, held while it played.
        if (!m_scan && !m_toAsk.isEmpty())
            scanNext();
    });

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
    for (const char *key : { "games_directory", "high_scores", "video" }) {
        const QString k = QLatin1String(key);
        applySetting(k, m_appCore ? m_appCore->get_setting(kModuleId, k) : QVariant());
    }
}

ArcadeBackend::~ArcadeBackend() {
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
    }
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
    const QFileInfo dropIn(m_dataRoot + QStringLiteral("/bin/mame"));
    if (dropIn.isFile() && dropIn.isExecutable())
        return dropIn.absoluteFilePath();
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("mame"));
    if (!onPath.isEmpty())
        return onPath;
    // Debian's, which a service's PATH leaves out.
    const QFileInfo debian(QStringLiteral("/usr/games/mame"));
    if (debian.isFile() && debian.isExecutable())
        return debian.absoluteFilePath();
    return QString();
}

QString ArcadeBackend::mameIdentity() const {
    const QFileInfo program(mamePath());
    if (!program.exists())
        return QString();
    return QStringLiteral("%1:%2:%3").arg(program.canonicalFilePath()).arg(program.size())
                                    .arg(program.lastModified().toMSecsSinceEpoch());
}

void ArcadeBackend::listFolder() {
    // Unsorted, and no check of each entry's permissions: tens of thousands
    // of sets in a full set of MAME's games.
    QHash<QString, QString> sets;
    QSet<QString> misnamed;
    QSet<QString> folders;
    m_otherFolders.clear();
    QDirIterator it(m_gamesFolder, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
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
                m_otherFolders.insert(name, info.fileName());
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
        if (sets.contains(name) && (isFolder || !folders.contains(name)))
            continue;
        sets.insert(name, info.fileName());
        if (isFolder)
            folders.insert(name);
        else
            folders.remove(name);
        // MAME opens a set by its name in lower case: where the filesystem
        // tells cases apart (ext4, not the card's exFAT), PACMAN.ZIP isn't
        // the pacman.zip it looks for.
        if (asMame != info.fileName() && mame::isShortName(name)
            && !QFileInfo::exists(m_gamesFolder + QLatin1Char('/') + asMame))
            misnamed.insert(name);
        else
            misnamed.remove(name);
    }
    // The tree stays as it was made while the folder is as it was.
    if (sets == m_sets && misnamed == m_misnamed)
        return;
    m_sets = std::move(sets);
    m_misnamed = std::move(misnamed);
    m_folders = std::move(folders);
    invalidate();
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
    if (!m_cacheLoaded)
        loadCache();
    // There, so that whoever copies games onto the card sees where they go.
    QDir().mkpath(m_gamesFolder);
    listFolder();
    // A MAME changed (an update, a drop-in) may know the sets differently:
    // all of them are asked about again. With none there is nothing to ask,
    // and what the last one said is kept.
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
    // As the list opens there is nothing shown yet; anything that is lists
    // the folder as it is now.
    emitShown();
}

void ArcadeBackend::rescan() {
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
// What MAME says of the sets (-listxml), kept in games.json.

void ArcadeBackend::loadCache() {
    m_cacheLoaded = true;
    QFile file(m_home + QStringLiteral("/games.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject cache = QJsonDocument::fromJson(file.readAll()).object();
    m_identity = cache.value(QStringLiteral("mame")).toString();
    m_build = cache.value(QStringLiteral("build")).toString();
    const QJsonObject machines = cache.value(QStringLiteral("machines")).toObject();
    for (auto it = machines.constBegin(); it != machines.constEnd(); ++it)
        m_machines.insert(it.key(), mame::Machine::fromVariant(it.key(), it.value().toObject().toVariantMap()));
    const QJsonArray unknown = cache.value(QStringLiteral("unknown")).toArray();
    for (const QJsonValue &name : unknown)
        m_unknown.insert(name.toString());
    invalidate();
}

void ArcadeBackend::saveCache() const {
    QJsonObject machines;
    for (auto it = m_machines.constBegin(); it != m_machines.constEnd(); ++it)
        machines.insert(it.key(), QJsonObject::fromVariantMap(it.value().toVariant()));
    QStringList unknown(m_unknown.cbegin(), m_unknown.cend());
    unknown.sort();
    const QJsonObject cache{ { QStringLiteral("mame"), m_identity },
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
}

// ---------------------------------------------------------------------------
// The tree.

const ArcadeBackend::Index &ArcadeBackend::index() const {
    if (m_indexed)
        return m_index;
    Index index;
    QList<std::pair<QString, QString>> byName;   // the name sorted by, the set
    byName.reserve(m_sets.size());
    for (auto it = m_sets.cbegin(); it != m_sets.cend(); ++it) {
        const QString &set = it.key();
        if (!mame::isShortName(set) || m_unknown.contains(set) || m_misnamed.contains(set)) {
            index.unknown << set;
            continue;
        }
        const auto machine = m_machines.constFind(set);
        // One MAME wasn't asked about (there is none) is listed by its name.
        if (machine == m_machines.constEnd())
            byName.append({ set.toCaseFolded(), set });
        else if (machine->isGame())
            byName.append({ (machine->description.isEmpty() ? set : machine->description).toCaseFolded(), set });
    }
    std::sort(byName.begin(), byName.end());
    index.games.reserve(byName.size());
    for (const auto &[name, set] : std::as_const(byName)) {
        index.games << set;
        const auto machine = m_machines.constFind(set);
        if (machine == m_machines.constEnd())
            continue;
        for (const char *kind : kKinds) {
            const QString k = QLatin1String(kind);
            const QString place = folderOf(k, *machine);
            if (!place.isEmpty())
                index.places[k][place] << set;
        }
    }
    index.unknown.sort();
    m_index = std::move(index);
    m_indexed = true;
    return m_index;
}

QString ArcadeBackend::folderOf(const QString &kind, const mame::Machine &machine) {
    if (kind == QLatin1String("players"))
        return machine.players > 0 ? QString::number(machine.players) : QString();
    if (kind == QLatin1String("years")) {
        const QString decade = mame::decadeOf(machine.year);
        return decade.isEmpty() ? QStringLiteral("unknown") : decade;
    }
    return mame::makerOf(machine.manufacturer);
}

QVariantList ArcadeBackend::folders(const QString &kind) const {
    QStringList places = index().places.value(kind).keys();
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
    QVariantList list;
    for (const QString &place : std::as_const(places)) {
        QString name = place;
        if (kind == QLatin1String("players"))
            name = place == QLatin1String("1") ? QStringLiteral("1 Player") : place + QStringLiteral(" Players");
        else if (kind == QLatin1String("years") && place == QLatin1String("unknown"))
            name = QStringLiteral("Year Unknown");
        list << folder(name, kind + QLatin1Char('/') + place);
    }
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
    return { { QStringLiteral("name"), known && !machine->description.isEmpty() ? machine->description : set },
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

QString ArcadeBackend::mameFileName(const QString &set) const {
    return m_folders.contains(set) ? set
                                   : set + QLatin1Char('.') + QFileInfo(m_sets.value(set)).suffix().toLower();
}

QVariantMap ArcadeBackend::unknownEntry(const QString &set) const {
    QString detail;
    if (!mame::isShortName(set))
        detail = QStringLiteral("Not the name of a set MAME knows\nA set is named as MAME names its game: pacman.zip");
    else if (m_misnamed.contains(set))
        detail = QStringLiteral("MAME looks for %1, in lower case\nRename the file to %1").arg(mameFileName(set));
    else
        detail = QStringLiteral("MAME has no game called %1\nIts set may be for another version of MAME").arg(set);
    return { { QStringLiteral("name"), m_sets.value(set) },
             { QStringLiteral("path"), QStringLiteral("unknown:") + set },
             { QStringLiteral("isFolder"), false },
             { QStringLiteral("kind"), QStringLiteral("unknown") },
             { QStringLiteral("detail"), detail } };
}

QVariantList ArcadeBackend::listEntries(const QString &list) const {
    QVariantList entries;
    if (!m_appCore)
        return entries;
    const QVariantList stored = m_appCore->get_list(kModuleId, list);
    for (const QVariant &v : stored) {
        const QString path = v.toMap().value(QStringLiteral("path")).toString();
        if (!path.startsWith(QLatin1String("game:")))
            continue;
        // Those still in the folder, as MAME says they are now.
        const QString set = path.mid(5);
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
            top << folder(QStringLiteral("Not Recognized"), QStringLiteral("unknown"));
        return top;
    }
    if (path == QLatin1String("all"))
        return gameEntries(games.games);
    if (path.startsWith(QLatin1String("search/"))) {
        const QString words = path.mid(7).trimmed();
        QStringList found;
        for (const QString &set : games.games) {
            const auto machine = m_machines.constFind(set);
            const QString name = machine != m_machines.constEnd() ? machine->description : QString();
            if (set.contains(words, Qt::CaseInsensitive) || name.contains(words, Qt::CaseInsensitive))
                found << set;
        }
        return gameEntries(found);
    }
    for (const char *kind : kKinds) {
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
    const QFileInfo program(mamePath());
    QStringList candidates;
    // Beside the program as installed: Debian's (/usr/games/../share/games/
    // mame), Homebrew's (bin/../share/mame), and a MAME of its own folder.
    for (const QString &dir : { program.absolutePath(), QFileInfo(program.canonicalFilePath()).absolutePath() }) {
        candidates << dir + QStringLiteral("/../share/games/mame/plugins")
                   << dir + QStringLiteral("/../share/mame/plugins")
                   << dir + QStringLiteral("/plugins");
    }
    candidates << QStringLiteral("/usr/share/games/mame/plugins")
               << QStringLiteral("/usr/local/share/games/mame/plugins");
    for (const QString &candidate : std::as_const(candidates)) {
        if (QFileInfo::exists(candidate + QStringLiteral("/hiscore/init.lua")))
            return QDir::cleanPath(candidate);
    }
    return QString();
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

bool ArcadeBackend::play(const QString &name) {
    m_lastError.clear();
    if (m_launcher->isBusy()) {
        m_lastError = QStringLiteral("A game is still running");
        return false;
    }
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
    if (!m_sets.contains(name))
        listFolder();
    if (!m_sets.contains(name)) {
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

    const auto machine = m_machines.constFind(name);
    ScriptEntry entry;
    entry.path = program;
    entry.basename = QStringLiteral("mame");
    entry.meta.name = machine != m_machines.constEnd() && !machine->description.isEmpty()
                          ? machine->description : name;
    entry.meta.mode = QStringLiteral("takeover");
    entry.argv = args;
    // A MAME of its own folder runs there, as by hand, to find its files
    // (MAME's paths are relative to where it runs) and its own mame.ini.
    // What it writes goes to <data>/arcade either way (kOutputs).
    const QString own = ownFolder();
    entry.workingDirectory = own.isEmpty() ? m_home : own;

    QString error;
    if (!m_launcher->start(entry, &error)) {
        m_lastError = error;
        return false;
    }
    m_playing = name;
    if (!m_played) {
        m_played = true;
        emit playedChanged();
    }
    return true;
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
