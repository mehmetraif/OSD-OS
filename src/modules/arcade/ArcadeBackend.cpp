#include "ArcadeBackend.h"
#include "../scripts/ScriptLauncher.h"
#include "../../AppCore.h"
#include "../../audio/AudioOutput.h"
#include "../../input/ControllerProfile.h"
#include "../../util/AtomicFile.h"
#include "../../util/DeviceName.h"
#include "../../util/DurableFile.h"
#include "../../util/LegacyNames.h"
#include "ArcadeDb.h"
#include "FbneoInstaller.h"
#include "ProgramInstaller.h"
#include "Http.h"
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
#include <QCoreApplication>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QVersionNumber>
#include <QFuture>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <limits>
#include <utility>

namespace {

const QString kModuleId = QStringLiteral("com.osdos.arcade");

// games.json's layout: of one of another, only the sets MAME had no machine
// of are read, kept apart as every set is asked about again (2: each
// machine's source file, for its system; 3: its screen's size, for its
// picture on a tube).
constexpr int kCacheVersion = 3;

// The most credits a game is started with, as most games show at most 9.
constexpr int kMaxCredits = 9;

// MAME asked about this many sets at a time: a short enough command line, and
// what it says comes in batches.
constexpr int kBatch = 300;
// A batch MAME hasn't answered in this long is given up on.
constexpr int kScanTimeoutMs = 120000;

// MAME's exit codes (src/emu/main.h): those of a game MAME never ran. Not
// 3, its fatal error, which a game may hit after an hour of play.
constexpr int kFailedValidity = 1;
constexpr int kMissingFiles = 2;
constexpr int kDeviceError = 4;
constexpr int kNoSuchSystem = 5;
constexpr int kInvalidConfig = 6;

// What a file holding a token or a key is written with.
constexpr QFileDevice::Permissions kOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

// Settings → Arcade → Relay Server's choices, by RetroArch's names.
const QHash<QString, QString> kRelays = {
    { QStringLiteral("New York"), QStringLiteral("nyc") },
    { QStringLiteral("Madrid"), QStringLiteral("madrid") },
    { QStringLiteral("São Paulo"), QStringLiteral("saopaulo") },
    { QStringLiteral("Singapore"), QStringLiteral("singapore") },
    { QStringLiteral("Chuncheon"), QStringLiteral("chuncheon") },
};
// A sign-in RetroArch hasn't said how it went in this long is stopped (its
// token read all the same), and one stopped is killed after this.
constexpr int kSignInTimeoutMs = 20000;
constexpr int kSignInStopMs = 5000;
// An account OSD/OS Setup brought is checked as OSD/OS starts, when the
// network may not be up yet: while RetroAchievements can't be reached, again
// this much later, this many times in all.
constexpr int kSetupRetryMs = 30000;
constexpr int kSetupChecks = 10;

// How long the lobby is waited for.
constexpr int kLobbyTimeoutMs = 15000;
constexpr qint64 kLobbyMaxBytes = 4 * 1024 * 1024;
// How often what MAME has said so far is written to games.json during a scan.
constexpr int kCacheSaveMs = 30000;
// How long our own RetroArch, or the copy of the app asking our core, is
// given to say its version: a core is tens of megabytes to load on a Pi.
constexpr int kVersionReadMs = 10000;
// LAN Play: how long the hosts on the local network are given to answer a
// query, and the most rooms taken of the answers.
constexpr int kLanListenMs = 1000;
constexpr qsizetype kMostLanRooms = 64;
// How long a host's OSD/OS is given to answer for the ports of a game joined
// on the local network, each time it is asked, and how many times at most.
constexpr int kClaimWaitMs = 250;
constexpr int kClaimAskings = 3;
// How long ports that came without the host's layout wait for it (a datagram
// of its own, sent before them: lost, or an older OSD/OS's, which says none).
constexpr int kLayoutWaitMs = 150;
// Over the network, how often RetroArch's log is read as the game starts, and
// for how long: a host it can't reach it gives up on after 10 s, and the
// handshake takes a moment after; what fails later (a peer leaving) isn't a
// start that failed.
constexpr int kNetplayWatchMs = 500;
constexpr int kNetplayWatchFor = 60000;

// catver.ini, the games' genres, as progettosnaps publishes it with each MAME:
// downloaded into <data>/arcade when neither folder has one (it may not be
// handed out with OSD/OS, so it comes to each machine from its author's own
// place). Some megabytes; an hour before one that failed is tried again.
const QUrl kGenresUrl(QStringLiteral(
    "https://raw.githubusercontent.com/AntoPISA/MAME_SupportFiles/main/catver.ini/catver.ini"));
constexpr int kGenresTimeoutMs = 60000;
constexpr qint64 kGenresMaxBytes = 16 * 1024 * 1024;
constexpr qint64 kGenresRetrySeconds = 3600;

// Folders beside the games that aren't games: the BIOS sets' (on MAME's
// rompath too), the samples' (its samplepath) and the games' pictures, the
// screenshots and the title screens.
const QStringList kNotSets = { QStringLiteral("bios"), QStringLiteral("samples"), QStringLiteral("snap"),
                               QStringLiteral("titles") };

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
// The lists among the filters, before the places (the tree has the first
// two), and Not Recognized after, which the tree has too.
constexpr std::pair<const char *, const char *> kLists[] = {
    { "favorites", "Favorites" }, { "recent", "Recently Played" }, { "achievements", "RetroAchievements" },
    { "netplay", "Playable Online" },
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

using arcadehttp::requestFor;
using arcadehttp::limitReply;
using arcadehttp::tooBig;

// A publisher's filter under the company the name is one of, as the filters
// are made now: "makers/Nintendo of America" → "makers/Nintendo".
QString makerFilter(const QString &filter) {
    return QStringLiteral("makers/") + mame::makerOf(filter.mid(7));
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

// The filters of rooms (filterRooms()), where a game is once for each room
// that hosts it, and their sets, a room's at its place.
bool isRoomFilter(const QString &filter) {
    return filter == QLatin1String("online") || filter == QLatin1String("lan") || filter == QLatin1String("lobby");
}
QStringList roomSets(const QVariantList &rooms) {
    QStringList sets;
    sets.reserve(rooms.size());
    for (const QVariant &room : rooms)
        sets << room.toMap().value(QStringLiteral("set")).toString();
    return sets;
}

// A name, or what is typed, as a search reads it: in lower case, without
// its accents, of letters and digits alone, its words a space apart:
// "Pac-Man" is "pacman", "Street Fighter II: The World Warrior" "street
// fighter ii the world warrior".
QString searchable(const QString &text) {
    const QString folded = text.normalized(QString::NormalizationForm_KD).toCaseFolded();
    QString made;
    made.reserve(folded.size());
    bool apart = false;
    for (const QChar c : folded) {
        if (c.isLetterOrNumber()) {
            if (apart && !made.isEmpty())
                made += QLatin1Char(' ');
            apart = false;
            made += c;
        } else if (c.isSpace()) {
            apart = true;
        }
    }
    return made;
}

// The first snapshot MAME took of a game (F12 in it), in its folder of them;
// "" for none.
QString firstShot(const QString &folder) {
    const QDir dir(folder);
    const QStringList shots = dir.entryList({ QStringLiteral("*.png") }, QDir::Files, QDir::Name);
    return shots.isEmpty() ? QString() : dir.filePath(shots.first());
}

// How long after the game shown was ArcadeDB is asked about it: the games a
// list is scrolled through aren't (as RetroAchievements' findPictures()
// waits); and how long after a record changed it is written, with every
// change made meanwhile.
constexpr int kSnapAskDelayMs = 400;
constexpr int kRecordSaveDelayMs = 2000;
// The look through every game for pictures: at most a game's asked for a
// second, as RetroAchievements asks of those who read its Web API (a batch
// of ArcadeDB's in a step too), and how many looks in a row are made while
// some game's pictures couldn't be had.
constexpr int kSweepStepMs = 1000;
constexpr int kSweepPasses = 3;
// How long a set ArcadeDB had no pictures of is left before it is asked
// about again: its pictures follow MAME's releases, a few a year.
constexpr qint64 kSnapsAgainSeconds = 60 * 24 * 3600;

// The order a set's numbered pictures go round in: by their number, a .png
// before a .jpg of the same.
bool beforeInTurn(const QString &a, const QString &b) {
    const auto turn = [](const QString &path) {
        const QFileInfo info(path);
        const QString stem = info.completeBaseName();
        return std::pair(stem.mid(stem.lastIndexOf(QLatin1Char('_')) + 1).toInt(),
                         info.suffix().compare(QLatin1String("png"), Qt::CaseInsensitive) != 0);
    };
    return turn(a) < turn(b);
}

// A picture numbered after name, put among its others in turn.
void addNumbered(QHash<QString, QStringList> &numbered, const QString &name, const QString &picture) {
    QStringList &pictures = numbered[name];
    if (pictures.contains(picture))
        return;
    pictures << picture;
    std::sort(pictures.begin(), pictures.end(), beforeInTurn);
}

// A picture saved in folder as <name>_<n>.png (or .jpg, as data is), n the
// first number no picture of that name has there (.png or .jpg, in either
// case), nor a set of the folder's (sets) as its name: never over a file,
// which a new file only (NewOnly) makes sure of. Flushed to the card with its
// folder. Its path, "" when it couldn't be saved.
QString savePicture(const QString &folder, const QString &name, const QByteArray &data,
                    const QHash<QString, QString> &sets) {
    if (!QDir().mkpath(folder))
        return QString();
    const QString suffix = data.startsWith("\x89PNG") ? QStringLiteral("png") : QStringLiteral("jpg");
    const auto taken = [](const QString &base) {
        for (const char *ending : { ".png", ".jpg", ".PNG", ".JPG" }) {
            if (QFile::exists(base + QLatin1String(ending)))
                return true;
        }
        return false;
    };
    for (int n = 1; n <= 999; ++n) {
        const QString stem = QStringLiteral("%1_%2").arg(name).arg(n);
        const QString base = folder + QLatin1Char('/') + stem;
        if (sets.contains(stem) || taken(base))
            continue;
        QFile file(base + QLatin1Char('.') + suffix);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            // Made meanwhile: the next number.
            if (file.exists())
                continue;
            qWarning("[Arcade] A picture could not be saved in %s: %s", qPrintable(folder),
                     qPrintable(file.errorString()));
            return QString();
        }
        const bool written = file.write(data) == data.size() && file.flush();
        file.close();
        if (!written || !syncPath(file.fileName()).isEmpty()) {
            qWarning("[Arcade] A picture could not be saved in %s", qPrintable(folder));
            file.remove();
            return QString();
        }
        syncPath(folder, true);
        return file.fileName();
    }
    return QString();
}

// A picture saved in folder as <stem>.png (or .jpg, as data is), the way the
// picture packs for MAME name them, unless a picture of that name is there
// already (.png or .jpg, in either case), the user's own or one saved before,
// which is kept as it is and is the answer. Flushed to the card with its
// folder. Its path, "" when it couldn't be saved.
QString saveNamedPicture(const QString &folder, const QString &stem, const QByteArray &data) {
    if (!QDir().mkpath(folder))
        return QString();
    const QString base = folder + QLatin1Char('/') + stem;
    for (const char *ending : { ".png", ".jpg", ".PNG", ".JPG" }) {
        if (QFile::exists(base + QLatin1String(ending)))
            return base + QLatin1String(ending);
    }
    const QString suffix = data.startsWith("\x89PNG") ? QStringLiteral("png") : QStringLiteral("jpg");
    QFile file(base + QLatin1Char('.') + suffix);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        // Made meanwhile: it is the picture.
        if (file.exists())
            return file.fileName();
        qWarning("[Arcade] A picture could not be saved in %s: %s", qPrintable(folder), qPrintable(file.errorString()));
        return QString();
    }
    const bool written = file.write(data) == data.size() && file.flush();
    file.close();
    if (!written || !syncPath(file.fileName()).isEmpty()) {
        qWarning("[Arcade] A picture could not be saved in %s", qPrintable(folder));
        file.remove();
        return QString();
    }
    syncPath(folder, true);
    return file.fileName();
}

// A room on the local network as its id is kept: where it is joined, its
// game and who hosts it.
QString lanKey(const libretro::Room &room) {
    return room.joinAddress + QLatin1Char('\n') + room.rom + QLatin1Char('\n') + room.host;
}
// Whether one of the rooms is joined at joinAddress: hosted there.
bool hostedAt(const QList<libretro::Room> &rooms, const QString &joinAddress) {
    return std::any_of(rooms.cbegin(), rooms.cend(),
                       [&joinAddress](const libretro::Room &room) { return room.joinAddress == joinAddress; });
}

// A room's version against ours: "same", "other", or "unknown" where either
// isn't known. A core's whole, as RetroArch itself matches them when it
// connects; RetroArch's by its numbers (1.20 is 1.20.0).
QString coreMatch(const QString &theirs, const QString &ours) {
    return theirs.isEmpty() || ours.isEmpty() ? QStringLiteral("unknown")
           : theirs == ours                   ? QStringLiteral("same")
                                              : QStringLiteral("other");
}
QString retroarchMatch(const QString &theirs, const QString &ours) {
    const QVersionNumber a = QVersionNumber::fromString(theirs).normalized();
    const QVersionNumber b = QVersionNumber::fromString(ours).normalized();
    return a.isNull() || b.isNull() ? QStringLiteral("unknown") : a == b ? QStringLiteral("same") : QStringLiteral("other");
}

// Where a query reaches every host of the networks this machine is on: the
// broadcast the system sends on its main one, and each network's own, which
// reaches the hosts of another.
QList<QHostAddress> broadcastAddresses() {
    QList<QHostAddress> targets{ QHostAddress(QHostAddress::Broadcast) };
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : interfaces) {
        const QNetworkInterface::InterfaceFlags flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::CanBroadcast)
            || flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        const QList<QNetworkAddressEntry> entries = interface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            const QHostAddress broadcast = entry.broadcast();
            if (broadcast.protocol() == QAbstractSocket::IPv4Protocol && !targets.contains(broadcast))
                targets << broadcast;
        }
    }
    return targets;
}

// The pads as the menus know them, for a game's SDL (MAME's, RetroArch's):
// the controller database OSD/OS reads, a pad's buttons by their places, as
// InputManager has them, and every pad's mapping as OSD/OS's own SDL has it
// (controllers::padMappings()), so that the game's SDL, of another version or
// on another system, with another database, knows each pad as the menus do
// and numbers its buttons as the binds written for it do: each of a player's
// Arcade Buttons is the same button in the game.
QList<QPair<QString, QString>> padEnvironment(const QString &dataRoot) {
    QList<QPair<QString, QString>> environment{
        { QStringLiteral("SDL_GAMECONTROLLER_USE_BUTTON_LABELS"), QStringLiteral("0") } };
    const QString mappings = dataRoot + QStringLiteral("/gamecontrollerdb.txt");
    if (QFileInfo::exists(mappings))
        environment << qMakePair(QStringLiteral("SDL_GAMECONTROLLERCONFIG_FILE"), mappings);
    // After any mappings OSD/OS's own environment carries, which its SDL
    // read too, so a pad plugged in during the game has them as well.
    QStringList lines = controllers::padMappings();
    const QString own = qEnvironmentVariable("SDL_GAMECONTROLLERCONFIG");
    if (!own.isEmpty())
        lines.prepend(own);
    if (!lines.isEmpty())
        environment << qMakePair(QStringLiteral("SDL_GAMECONTROLLERCONFIG"), lines.join(QLatin1Char('\n')));
    return environment;
}

} // namespace

ArcadeBackend::ArcadeBackend(const QString &appRoot, const QString &dataRoot, AppCore *appCore,
                             DisplayHandoff *handoff, QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot),
      m_home(dataRoot + QStringLiteral("/arcade")), m_appCore(appCore)
{
    // The filters may hold other games: RetroAchievements' and Playable
    // Online's made again when next asked for, before the screen asks.
    connect(this, &ArcadeBackend::gamesChanged, this, [this] { m_madeSets.clear(); });
    m_launcher = new ScriptLauncher(m_appRoot, m_dataRoot, handoff, this);
    m_launcher->setHandoffOwner(QStringLiteral("arcade"));
    connect(m_launcher, &ScriptLauncher::runningChanged, this, &ArcadeBackend::runningChanged);
    connect(m_launcher, &ScriptLauncher::finished, this, [this](int exitCode, const QString &launcherReason) {
        QString reason = launcherReason;
        QString notice;
        bool refused = false;
        m_netplayWatch->stop();
        if (m_playingEngine == QLatin1String("fbneo")) {
            const libretro::Run run = libretro::readLog(retroarchHome() + QStringLiteral("/last.log"),
                                                        libretro::hosts(m_playingNetplay));
            takeAchievements(run);
            // FinalBurn Neo shows its own error for a game it can't start,
            // and RetroArch goes on: a game is one it started, as its log
            // says (a log there to say so). Over the network, one RetroArch
            // couldn't host or join and went on with alone (stopped by
            // watchNetplay(), or played out) isn't the game asked for.
            const bool alone = m_playingNetplay != libretro::Launch::Netplay::None && !run.netplayFailure.isEmpty();
            if ((reason == QLatin1String("ok") && run.logged && !run.started)
                || (alone && reason != QLatin1String("failed_to_start")))
                reason = QStringLiteral("failed");
            notice = fbneoNotice(exitCode, reason, run);
            // Stopped by watchNetplay(), RetroArch may die of the signal,
            // which the launcher counts a crash: not a game played either.
            refused = reason == QLatin1String("failed_to_start")
                      || (reason == QLatin1String("failed") && (alone || (!m_launcher->lastRunCrashed() && !run.started)));
        } else {
            notice = noticeFor(exitCode, reason);
            // The time it ran counts as played, unless MAME refused the game
            // (files missing, no such game, its settings wrong, a device it
            // couldn't have, a check it failed) or never ran.
            refused = reason == QLatin1String("failed_to_start")
                      || (reason == QLatin1String("failed") && !m_launcher->lastRunCrashed()
                          && (exitCode == kFailedValidity || exitCode == kMissingFiles
                              || exitCode == kDeviceError || exitCode == kNoSuchSystem
                              || exitCode == kInvalidConfig));
        }
        if (!notice.isEmpty())
            qWarning("[Arcade] %s: %s", qPrintable(m_playing), qPrintable(notice));
        if (!refused && !m_playing.isEmpty())
            addPlayTime(m_playing, (m_playClock.elapsed() + 500) / 1000);
        // Its achievements as the game left them, when next shown.
        if (!m_playing.isEmpty())
            m_retroAchievements->forgetAchievements(QFileInfo(m_sets.value(m_playing)).fileName());
        // A snapshot taken in the game (F12) is its picture now.
        if (!m_playing.isEmpty()) {
            const QString shot = firstShot(m_home + QStringLiteral("/snap/") + m_playing);
            if (shot.isEmpty())
                m_shots.remove(m_playing);
            else
                m_shots.insert(m_playing, shot);
        }
        // Over the network: no more ports given, and this machine's given
        // back to the host that gave them.
        stopClaims();
        if (!m_claimedFrom.isEmpty())
            releaseClaim(std::exchange(m_claimedFrom, QString()));
        m_playing.clear();
        emit finished(exitCode, reason, notice);
        // The sets MAME was still to be asked about, held while it played.
        if (!m_scan && !m_toAsk.isEmpty())
            scanNext();
    });
    m_netplayWatch = new QTimer(this);
    m_netplayWatch->setInterval(kNetplayWatchMs);
    connect(m_netplayWatch, &QTimer::timeout, this, &ArcadeBackend::watchNetplay);

    m_watcher = new QFutureWatcher<Read>(this);
    connect(m_watcher, &QFutureWatcherBase::finished, this, &ArcadeBackend::onRead);
    m_pictureSaves.setMaxThreadCount(1);

    m_installer = new FbneoInstaller(m_dataRoot, this);
    connect(m_installer, &FbneoInstaller::progressChanged, this, &ArcadeBackend::fbneoProgressChanged);
    connect(m_installer, &FbneoInstaller::finished, this, [this](bool ok, const QString &message) {
        m_fbneoMessage = message;
        // Its list read with the folder, now that it has changed.
        if (ok)
            refresh();
        emit fbneoChanged();
    });
    m_retroarchInstaller = new ProgramInstaller(this);
    connect(m_retroarchInstaller, &ProgramInstaller::progressChanged, this, &ArcadeBackend::fbneoProgressChanged);
    connect(m_retroarchInstaller, &ProgramInstaller::finished, this, [this](bool ok, const QString &message) {
        m_retroarchMessage = message;
        // The games read again with RetroArch there: FinalBurn Neo's play
        // now, and its version is asked as they are listed.
        if (ok)
            refresh();
        emit fbneoChanged();
    });
    m_mameInstaller = new ProgramInstaller(this);
    connect(m_mameInstaller, &ProgramInstaller::progressChanged, this, &ArcadeBackend::mameProgressChanged);
    connect(m_mameInstaller, &ProgramInstaller::finished, this, [this](bool ok, const QString &message) {
        m_mameMessage = message;
        // The games read again with MAME there, which is asked about every
        // set, as it is another MAME. A batch stopped for a download that
        // failed is asked again (downloadMame()).
        if (ok)
            refresh();
        else if (!m_scan && !m_toAsk.isEmpty() && !m_launcher->isBusy())
            scanNext();
        emit mameChanged();
    });

    // The RetroAchievements account, without its password, which is kept
    // nowhere but in OSD/OS Setup's file until RetroAchievements has had it
    // (takeSetupAccount()).
    QFile account(m_home + QStringLiteral("/retroachievements.json"));
    if (account.open(QIODevice::ReadOnly)) {
        const QJsonObject kept = QJsonDocument::fromJson(account.readAll()).object();
        m_raUser = kept.value(QStringLiteral("user")).toString();
        m_raToken = kept.value(QStringLiteral("token")).toString();
        m_raKey = kept.value(QStringLiteral("key")).toString();
    }
    m_retroAchievements = new RetroAchievements(m_home, RetroAchievements::kApi, RetroAchievements::kMedia, this);
    m_retroAchievements->setKey(m_raKey);
    m_retroAchievements->setUser(m_raUser);
    connect(m_retroAchievements, &RetroAchievements::boardReady, this, [this](const QString &fileName) {
        emit leaderboardReady(QFileInfo(fileName).completeBaseName().toLower());
    });
    connect(m_retroAchievements, &RetroAchievements::achievementsReady, this, [this](const QString &fileName) {
        emit gameAchievementsReady(QFileInfo(fileName).completeBaseName().toLower());
    });
    connect(m_retroAchievements, &RetroAchievements::picturesFound, this, &ArcadeBackend::onPicturesFound);
    m_sweepTimer = new QTimer(this);
    m_sweepTimer->setTimerType(Qt::PreciseTimer);
    m_sweepTimer->setInterval(kSweepStepMs);
    connect(m_sweepTimer, &QTimer::timeout, this, &ArcadeBackend::sweepPictures);
    // After a look that couldn't have every picture: another, ten minutes on
    // (or once the last game's retry is due, finishPictureSweep()).
    m_sweepAgain = new QTimer(this);
    m_sweepAgain->setSingleShot(true);
    connect(m_sweepAgain, &QTimer::timeout, this, [this] {
        if (m_reading || scanning())
            m_sweepWanted = true;
        else
            startPictureSweep();
    });
    // A look cut short (the power went, the app was closed) goes on at the
    // next start, once the games have been read.
    m_sweepWanted = QFile::exists(sweepMarker());
    // progetto-SNAPS' pictures, through ArcadeDB (OSDOS_ARCADEDB_URL names
    // another service; "" none, in tests): the game shown asked about a
    // moment after it was shown.
    m_arcadeDb = new ArcadeDb(QUrl(legacy::env("ARCADEDB_URL", ArcadeDb::kService.toString())), this);
    connect(m_arcadeDb, &ArcadeDb::found, this, &ArcadeBackend::onSnapsFound);
    connect(m_arcadeDb, &ArcadeDb::failed, this, &ArcadeBackend::onSnapsFailed);
    m_snapAsk = new QTimer(this);
    m_snapAsk->setSingleShot(true);
    m_snapAsk->setInterval(kSnapAskDelayMs);
    connect(m_snapAsk, &QTimer::timeout, this, &ArcadeBackend::askSnaps);
    // The pictures' records, written a moment after they change.
    m_recordSave = new QTimer(this);
    m_recordSave->setSingleShot(true);
    m_recordSave->setInterval(kRecordSaveDelayMs);
    connect(m_recordSave, &QTimer::timeout, this, &ArcadeBackend::flushRecords);
    // Its list of games read again: the RetroAchievements filter's games, and
    // the games it had none of, which it may have now.
    connect(m_retroAchievements, &RetroAchievements::listRead, this, [this] {
        m_noPictures.clear();
        emit gamesChanged();
    });
    connect(m_retroAchievements, &RetroAchievements::keyRefused, this, [this] {
        if (!std::exchange(m_raKeyRefused, true))
            emit achievementsChanged();
    });
    // An answer that came with the key: one refused before (a guard's page
    // taken for a refusal, or the account put right) is good again.
    connect(m_retroAchievements, &RetroAchievements::keyAccepted, this, [this] {
        m_raKeyRefused = false;
        emit achievementsChanged();
    });
    // The trophy among the status icons is the account's.
    connect(this, &ArcadeBackend::achievementsChanged, this, &ArcadeBackend::statusIconsChanged);
    // A game's config is written anew for each game: one a game left that the
    // app didn't see end (it was stopped meanwhile) may hold the password, as
    // may a sign-in's.
    QFile::remove(retroarchHome() + QStringLiteral("/retroarch.cfg"));
    QDir(signInDir()).removeRecursively();
    m_signInCore = QCoreApplication::applicationDirPath() + QStringLiteral("/osdos_signin_libretro.so");
    m_genresUrl = QUrl(legacy::env("GENRES_URL", kGenresUrl.toString()));
    m_lobbyUrl = QUrl(legacy::env("LOBBY_URL", m_lobbyUrl.toString()));
    m_coreInfoCommand = { QCoreApplication::applicationFilePath(), QStringLiteral("--core-info") };
    m_versionReadMs = kVersionReadMs;
    m_lanListen = new QTimer(this);
    m_lanListen->setSingleShot(true);
    m_lanListenMs = kLanListenMs;
    connect(m_lanListen, &QTimer::timeout, this, &ArcadeBackend::finishLanRead);

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
    for (const char *key : { "games_directory", "hide_bootlegs", "high_scores", "video", "screen_shape",
                             "integer_scale", "menu_filters", "online_high_scores", "online_play", "engines",
                             "online_name", "relay_server", "lan_play", "pictures_source" }) {
        const QString k = QLatin1String(key);
        applySetting(k, m_appCore ? m_appCore->get_setting(kModuleId, k) : QVariant());
    }
    // A Start Filter saved under one of catver.ini's own genres (0.0.9's:
    // Fighter, Driving) is saved again as the genre it became, and one saved
    // under a company's other name (Nintendo of America) as the company.
    if (m_appCore) {
        const QString start = m_appCore->get_setting(kModuleId, QStringLiteral("start_filter")).toString();
        const QString moved = start.startsWith(QLatin1String("genres/"))
                                  ? QStringLiteral("genres/") + mame::genreOf(start.mid(7))
                              : start.startsWith(QLatin1String("makers/")) ? makerFilter(start)
                                                                          : start;
        if (moved != start)
            m_appCore->save_setting(kModuleId, QStringLiteral("start_filter"), moved);
        // Menu Filters' publishers likewise: a company on where any of its
        // names was.
        QVariantMap filters;
        bool changed = false;
        for (auto it = m_menuFilters.constBegin(); it != m_menuFilters.constEnd(); ++it) {
            const QString filter = it.key().startsWith(QLatin1String("makers/")) ? makerFilter(it.key()) : it.key();
            changed = changed || filter != it.key();
            if (filter == QLatin1String("makers/"))
                continue;
            if (!filters.contains(filter) || isOn(it.value(), false))
                filters.insert(filter, it.value());
        }
        if (changed) {
            m_appCore->save_setting(kModuleId, QStringLiteral("menu_filters"), filters);
            m_menuFilters = filters;
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

    // An account OSD/OS Setup brought, taken once the event loop runs.
    m_setupRetryMs = kSetupRetryMs;
    QTimer::singleShot(0, this, &ArcadeBackend::takeSetupAccount);
}

ArcadeBackend::~ArcadeBackend() {
    // The pictures' records as they stand (m_pictureSaves waits for them).
    flushRecords();
    // The read keeps nothing of this object's: it may finish on its own.
    m_watcher->disconnect(this);
    if (m_scan) {
        m_scan->disconnect(this);
        m_scan->kill();
        m_scan->waitForFinished(1000);
    }
    if (QProcess *signIn = m_signIn) {
        stopSignIn();
        signIn->waitForFinished(1000);
    }
    // Our versions' reads: nothing of them is wanted any more.
    for (QProcess *read : { m_versionRead.data(), m_coreInfoRead.data() }) {
        if (read) {
            read->disconnect(this);
            read->kill();
            read->waitForFinished(1000);
        }
    }
}

void ArcadeBackend::onSettingChanged(const QString &moduleId, const QString &key,
                                     const QVariant &value) {
    if (moduleId != kModuleId)
        return;
    applySetting(key, value);
    // Start Filter offers RetroArch Lobby while Online Play is on: its
    // choices again, for the Settings page that has both.
    if (key == QLatin1String("online_play"))
        startFilterOptions();
}

void ArcadeBackend::applySetting(const QString &key, const QVariant &value) {
    if (key == QLatin1String("games_directory")) {
        setGamesFolder(value.toString());
    } else if (key == QLatin1String("hide_bootlegs")) {
        // The lists made again without the bootlegs and hacks, or with them
        // back (isGame()).
        m_hideBootlegs = isOn(value, false);
        invalidate();
        emit gamesChanged();
    } else if (key == QLatin1String("high_scores")) {
        m_highScores = isOn(value, true);
        emit statusIconsChanged();
    } else if (key == QLatin1String("video")) {
        // Auto leaves it to MAME (Debian's mame.ini says opengl).
        static const QHash<QString, QString> kVideo = {
            { QStringLiteral("OpenGL"), QStringLiteral("opengl") },
            { QStringLiteral("BGFX"), QStringLiteral("bgfx") },
            { QStringLiteral("Accelerated"), QStringLiteral("accel") },
            { QStringLiteral("Software"), QStringLiteral("soft") },
        };
        m_video = kVideo.value(value.toString());
    } else if (key == QLatin1String("screen_shape")) {
        // Auto (or nothing saved) goes by the screen (screenShape()).
        const QString shape = value.toString();
        m_screenShape = shape == QLatin1String("4:3") || shape == QLatin1String("16:9")
                                || shape == QLatin1String("Square Pixels") ? shape : QString();
    } else if (key == QLatin1String("integer_scale")) {
        const QString scale = value.toString();
        m_integerScale = scale == QLatin1String("On") || scale == QLatin1String("Off") ? scale : QString();
    } else if (key == QLatin1String("menu_filters")) {
        // Saved whole at every change (MultiSelectSettings.qml).
        m_menuFilters = value.toMap();
        emit gamesChanged();
    } else if (key == QLatin1String("online_high_scores")) {
        m_achievements = value.toString() == QLatin1String("RetroAchievements");
        emit gamesChanged();
        emit statusIconsChanged();
    } else if (key == QLatin1String("online_play")) {
        m_onlinePlay = value.toString() == QLatin1String("On");
        // Off: the lobby forgotten, and a read under way stopped, so that its
        // answer, however late, neither offers its rooms nor builds the
        // screens anew, and on again the lobby is read before a room is
        // offered, as LAN Play's are.
        if (!m_onlinePlay) {
            if (QNetworkReply *reply = std::exchange(m_lobbyReply, nullptr)) {
                reply->disconnect(this);
                reply->abort();
                reply->deleteLater();
            }
            m_rooms.clear();
            m_lobby.clear();
            m_roomsProblem.clear();
            m_roomsRead = false;
        }
        emit gamesChanged();
        emit statusIconsChanged();
    } else if (key == QLatin1String("engines")) {
        // Saved whole by setEngine(). A game moved to MAME leaves the
        // RetroAchievements filter.
        m_engines = value.toMap();
        emit gamesChanged();
    } else if (key == QLatin1String("online_name")) {
        m_onlineName = value.toString().trimmed();
    } else if (key == QLatin1String("relay_server")) {
        m_relay = kRelays.value(value.toString(), QStringLiteral("nyc"));
    } else if (key == QLatin1String("pictures_source")) {
        // Both unless it says one, or none.
        const QString source = value.toString();
        m_picturesAchievements = source != QLatin1String("progetto-SNAPS") && source != QLatin1String("Off");
        m_picturesSnaps = source != QLatin1String("RetroAchievements") && source != QLatin1String("Off");
        emit picturesProgressChanged();
    } else if (key == QLatin1String("lan_play")) {
        m_lanPlay = value.toString() == QLatin1String("On");
        // Off: the rooms heard forgotten, so that none of them is offered
        // once it is on again, before the network has been asked.
        if (!m_lanPlay) {
            m_lanListen->stop();
            m_lanHeard.clear();
            m_lanRooms.clear();
            m_lanMissed.clear();
            m_lanShown.clear();
            m_lanProblem.clear();
            m_lanRead = false;
        }
        emit gamesChanged();
        emit statusIconsChanged();
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
        // The sets MAME had no machine of before it was asked about every set
        // again; in one made by another version, all it had none of.
        const bool older = cache.value(QStringLiteral("version")).toInt() != kCacheVersion;
        for (const QString &key : { QStringLiteral("unknownBefore"), QStringLiteral("unknown") }) {
            if (key == QLatin1String("unknown") && !older)
                continue;
            const QJsonArray before = cache.value(key).toArray();
            for (const QJsonValue &name : before)
                read.unknownBefore.insert(name.toString());
        }
        // Made by an older version, without what this one keeps of each game.
        if (older) {
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
            read.fbneoGames = libretro::readDat(dat, &read.fbneoAspects, &read.fbneoMachines);
            qInfo("[Arcade] %lld of FinalBurn Neo's games from %s", static_cast<long long>(read.fbneoGames.size()),
                  qPrintable(dat));
        }
    }

    // The pictures, looked for here once rather than file by file as the
    // games are chosen (exFAT's folders have no index): the games folder's
    // snap, named by the set as MAME names it, the way the picture packs for
    // MAME have them, a .png before a .jpg, and those numbered after a set
    // there (sf2_1.png, sf2_2.jpg: RetroAchievements' and the user's own;
    // not a set's own snap, sf2_1.png while there is a set sf2_1); and the
    // first MAME took of each game, in its own snap folder.
    static const QRegularExpression numbered(QRegularExpression::anchoredPattern(QStringLiteral("(.+)_(\\d{1,3})")));
    QHash<QString, int> ranks;   // set → how well its picture's name fits
    QDirIterator snaps(folder + QLatin1Char('/') + read.otherFolders.value(QStringLiteral("snap"), QStringLiteral("snap")),
                       QDir::Files);
    while (snaps.hasNext()) {
        snaps.next();
        const QFileInfo info = snaps.fileInfo();
        const QString suffix = info.suffix().toLower();
        if (suffix != QLatin1String("png") && suffix != QLatin1String("jpg"))
            continue;
        // An achievement's badge (achievementBadge()), no game's picture.
        if (info.fileName().startsWith(QLatin1String("achieve_"), Qt::CaseInsensitive))
            continue;
        // Its exact name first (as MAME's own folder has it), then a .png.
        const QString set = info.completeBaseName().toLower();
        const int rank = (info.fileName() == set + QLatin1Char('.') + suffix ? 2 : 0)
                         + (suffix == QLatin1String("png") ? 1 : 0);
        if (rank > ranks.value(set, -1)) {
            ranks.insert(set, rank);
            read.snaps.insert(set, info.filePath());
        }
        const QRegularExpressionMatch match = numbered.match(set);
        if (match.hasMatch() && !read.sets.contains(set))
            read.numbered[match.captured(1)] << info.filePath();
    }
    for (QStringList &pictures : read.numbered)
        std::sort(pictures.begin(), pictures.end(), beforeInTurn);
    // The title screens, in the games folder's titles, named by set as the
    // Titles pack for MAME has them: the exact name first, then a .png.
    QHash<QString, int> titleRanks;
    QDirIterator titles(folder + QLatin1Char('/') + read.otherFolders.value(QStringLiteral("titles"), QStringLiteral("titles")),
                        QDir::Files);
    while (titles.hasNext()) {
        titles.next();
        const QFileInfo info = titles.fileInfo();
        const QString suffix = info.suffix().toLower();
        if (suffix != QLatin1String("png") && suffix != QLatin1String("jpg"))
            continue;
        const QString set = info.completeBaseName().toLower();
        const int rank = (info.fileName() == set + QLatin1Char('.') + suffix ? 2 : 0)
                         + (suffix == QLatin1String("png") ? 1 : 0);
        if (rank > titleRanks.value(set, -1)) {
            titleRanks.insert(set, rank);
            read.titles.insert(set, info.filePath());
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
        m_unknownBefore = std::move(read.unknownBefore);
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
        m_fbneoAspects = std::move(read.fbneoAspects);
        m_fbneoMachines = std::move(read.fbneoMachines);
        // The games MAME doesn't know that it has, and the filters made of
        // the games it plays, made again.
        invalidate();
    }
    m_snaps = std::move(read.snaps);
    m_numbered = std::move(read.numbered);
    m_titles = std::move(read.titles);
    m_shots = std::move(read.shots);
    // Pictures saved while the folder was read, which it may not have seen.
    for (auto it = m_savedDuringRead.constBegin(); it != m_savedDuringRead.constEnd(); ++it) {
        for (const QString &picture : it.value())
            addNumbered(m_numbered, it.key(), picture);
    }
    m_savedDuringRead.clear();
    for (const auto &[set, file, title] : std::as_const(m_namedDuringRead)) {
        QHash<QString, QString> &pictures = title ? m_titles : m_snaps;
        if (!pictures.contains(set))
            pictures.insert(set, file);
    }
    m_namedDuringRead.clear();
    m_reading = false;
    // Games and no catver.ini for their genres: one downloaded.
    if (m_genresFrom.isEmpty() && !m_sets.isEmpty())
        downloadGenres();
    // Read the Games Again, asked while this read went on, as rescan() does
    // it: unless MAME is being asked already (a game paused it), what it said
    // of every set is forgotten, and it is asked about them all.
    if (std::exchange(m_rescanAfterRead, false) && !scanning()) {
        // games.json stays as it was until MAME has answered again (rescan()).
        forget();
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
    // The lobby's rooms with the list, for Online Games, and the local
    // network's, for LAN Games.
    refreshRooms();
    refreshLanRooms();
    readAgain();
}

void ArcadeBackend::readAgain() {
    if (m_reading)
        m_readAgain = true;
    else
        startRead();
}

void ArcadeBackend::rescan() {
    // Every game's pictures too, once the games have been read again
    // (emitShown()): a look under way starts anew then.
    stopPictureSweep();
    m_sweepAgain->stop();
    m_sweepPasses = 0;
    m_sweepWanted = true;
    emit picturesProgressChanged();
    // Asked while the folder is read, or before games.json has been, whose
    // sets MAME has no machine of are to be kept apart as it is asked
    // again: once it has been (onRead()).
    if (m_reading || !m_cacheLoaded) {
        m_rescanAfterRead = true;
        if (!m_reading)
            refresh();
        return;
    }
    if (scanning())
        return;
    // games.json stays as it was until MAME has answered again (the scan
    // saves as it goes): stopped before then, the next start has what it
    // said before rather than no games at all.
    forget();
    refresh();
}

void ArcadeBackend::forget() {
    // Until it has answered again: the games only FinalBurn Neo has still
    // play in it. Not those it has answered for since it last had none.
    for (auto it = m_machines.cbegin(); it != m_machines.cend(); ++it)
        m_unknownBefore.remove(it.key());
    m_unknownBefore.unite(m_unknown);
    m_machines.clear();
    m_unknown.clear();
    m_build.clear();
    invalidate();
}

// ---------------------------------------------------------------------------
// What MAME says of the sets (-listxml), kept in games.json (read with the
// folder, read()).

void ArcadeBackend::saveCache() {
    // Off the app's thread, from a snapshot (the containers are implicitly
    // shared): tens of thousands of machines made into JSON and flushed to
    // the card take seconds on a Pi. The machines go in by name in order,
    // so that each insert into the JSON object appends: in hash order each
    // one moved every key after it, which was quadratic. One save at a time,
    // in order (m_pictureSaves is one thread), so the last written is the
    // newest; the pool waits for the last as the backend goes.
    m_cacheSaved.start();
    const QHash<QString, mame::Machine> machines = m_machines;
    const QSet<QString> unknown = m_unknown;
    const QSet<QString> unknownBefore = m_unknownBefore;
    const QString identity = m_identity;
    const QString build = m_build;
    const QString home = m_home;
    m_pictureSaves.start([machines, unknown, unknownBefore, identity, build, home] {
        QStringList names = machines.keys();
        names.sort();
        QJsonObject object;
        for (const QString &name : std::as_const(names))
            object.insert(name, QJsonObject::fromVariantMap(machines.value(name).toVariant()));
        const auto sorted = [](const QSet<QString> &sets) {
            QStringList list(sets.cbegin(), sets.cend());
            list.sort();
            return QJsonArray::fromStringList(list);
        };
        const QJsonObject cache{ { QStringLiteral("version"), kCacheVersion },
                                 { QStringLiteral("mame"), identity },
                                 { QStringLiteral("build"), build },
                                 { QStringLiteral("machines"), object },
                                 { QStringLiteral("unknown"), sorted(unknown) },
                                 { QStringLiteral("unknownBefore"), sorted(unknownBefore) } };
        QDir().mkpath(home);
        writeFileAtomically(home + QStringLiteral("/games.json"), QJsonDocument(cache).toJson(QJsonDocument::Compact));
    });
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
    // Not beside a game started while the folder was read: once it has
    // ended (the launcher's finished()).
    if (!m_launcher->isBusy())
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
        // Kept as it comes, every so often: a session shorter than the whole
        // scan of a full set (a Pi powered off before the end) keeps what was
        // answered, instead of asking about every set again next time.
        if (!m_toAsk.isEmpty() && (!m_cacheSaved.isValid() || m_cacheSaved.elapsed() >= kCacheSaveMs))
            saveCache();
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
    // Every set answered for: what MAME said before is said again or no
    // longer so.
    if (!m_scanFailed)
        m_unknownBefore.clear();
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
    if (m_sweepWanted && !m_reading && !scanning())
        startPictureSweep();
}

// ---------------------------------------------------------------------------
// The filters.

bool ArcadeBackend::isUnrecognized(const QString &set) const {
    return !mame::isShortName(set) || m_misnamed.contains(set) || (mameLacks(set) && !fbneoHas(set));
}

bool ArcadeBackend::mameLacks(const QString &set) const {
    return m_unknown.contains(set) || (m_unknownBefore.contains(set) && !m_machines.contains(set));
}

bool ArcadeBackend::fbneoOnly(const QString &set) const {
    return mameLacks(set) && fbneoHas(set);
}

bool ArcadeBackend::isGame(const QString &set) const {
    if (!m_sets.contains(set) || isUnrecognized(set))
        return false;
    // One MAME wasn't asked about (there is none) and FinalBurn Neo hasn't is
    // listed by its name.
    const mame::Machine *machine = machineOf(set);
    if (!machine)
        return true;
    // Hide Hacks and Bootlegs: the makers' own releases alone, whichever of
    // MAME and FinalBurn Neo's list describes the set.
    return machine->isGame() && !(m_hideBootlegs && mame::isBootlegOrHack(*machine));
}

const mame::Machine *ArcadeBackend::machineOf(const QString &set) const {
    if (const auto machine = m_machines.constFind(set); machine != m_machines.constEnd())
        return &machine.value();
    if (const auto machine = m_fbneoMachines.constFind(set); machine != m_fbneoMachines.constEnd() && fbneoHas(set))
        return &machine.value();
    return nullptr;
}

QString ArcadeBackend::descriptionOf(const QString &set) const {
    const mame::Machine *machine = machineOf(set);
    return machine && !machine->description.isEmpty() ? machine->description : set;
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
        const mame::Machine *known = machineOf(set);
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
    m_searchTexts.clear();
    m_searchFound = false;
    return m_index;
}

const QStringList &ArcadeBackend::searchTexts() const {
    const Index &games = index();
    if (m_searchTexts.isEmpty() && !games.games.isEmpty()) {
        m_searchTexts.reserve(games.games.size());
        for (const QString &set : games.games)
            m_searchTexts << searchable(descriptionOf(set)) + QLatin1Char(' ') + searchable(set);
    }
    return m_searchTexts;
}

QStringList ArcadeBackend::searchSets(const QString &words) const {
    const Index &games = index();
    const QStringList wanted = searchable(words).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (wanted.isEmpty())
        return games.games;
    const QStringList &texts = searchTexts();
    const QString whole = wanted.join(QLatin1Char(' '));
    // The words run together, as a name's signs go: "pac man" is Pac-Man's
    // "pacman".
    const QString joined = wanted.join(QString());
    const QString wordStart = QLatin1Char(' ') + wanted.first();
    QStringList best, next, rest;
    for (qsizetype i = 0; i < games.games.size(); ++i) {
        const QString &text = texts.at(i);
        if (!std::all_of(wanted.cbegin(), wanted.cend(), [&text](const QString &word) { return text.contains(word); }))
            continue;
        const QString &set = games.games.at(i);
        // Its name, or its set, what was typed; a word of either starting
        // with its first word; or the words anywhere in them.
        if (text.startsWith(whole) || text.startsWith(joined) || text.endsWith(QLatin1Char(' ') + whole))
            best << set;
        else if (text.startsWith(wanted.first()) || text.contains(wordStart))
            next << set;
        else
            rest << set;
    }
    return best + next + rest;
}

const QStringList &ArcadeBackend::searchRows() const {
    // The index first: a new one drops what was found in the last.
    index();
    if (!m_searchFound) {
        m_searchRows = QStringList{ QString() } + searchSets(m_search);
        m_searchFound = true;
    }
    return m_searchRows;
}

int ArcadeBackend::setSearch(const QString &words) {
    if (words != m_search) {
        m_search = words;
        m_searchFound = false;
    }
    return static_cast<int>(filterSets(QStringLiteral("search")).size());
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
    // The rooms of the lobby, or of the local network, a game's as often as
    // it is hosted; the whole lobby's, "" for a game not in the folder.
    if (isRoomFilter(filter))
        return roomSets(filterRooms(filter));
    // The rest once MAME has said what the sets are.
    if (scanning())
        return QStringList();
    // Search's own row, then the games found.
    if (filter == QLatin1String("search"))
        return searchRows();
    if (filter == QLatin1String("achievements") || filter == QLatin1String("netplay"))
        return madeSets(filter);
    const Index &games = index();
    if (filter == QLatin1String("all"))
        return games.games;
    if (filter == QLatin1String("unknown"))
        return games.unknown;
    const int slash = filter.indexOf(QLatin1Char('/'));
    return slash < 0 ? QStringList() : games.places.value(filter.left(slash)).value(filter.mid(slash + 1));
}

QVariantList ArcadeBackend::filterRooms(const QString &filter) const {
    if (filter == QLatin1String("lobby"))
        return m_onlinePlay ? lobby().value(QStringLiteral("rooms")).toList() : QVariantList();
    return filter == QLatin1String("lan") ? lanRooms() : filter == QLatin1String("online") ? rooms() : QVariantList();
}

QVariantList ArcadeBackend::filters() {
    QVariantList list;
    if (!scanning() && !index().games.isEmpty())
        list << filter(QStringLiteral("all"), QStringLiteral("All Games"), index().games.size());
    // While someone hosts one of the games: the lobby is read with the list,
    // and the local network asked. The whole lobby with Online Play on and
    // games to play, while it is read and when no one hosts a game too: the
    // screen says so. With none, the screen says why there are none.
    // LAN Games, with LAN Play on and games to play, also while no one on
    // the network hosts one: the screen says why (lanStatus()).
    if (const qsizetype online = rooms().size(); online > 0)
        list << filter(QStringLiteral("online"), QStringLiteral("Online Games"), online);
    if (const qsizetype lan = lanRooms().size(); lan > 0 || (m_lanPlay && !scanning() && !index().games.isEmpty()))
        list << filter(QStringLiteral("lan"), QStringLiteral("LAN Games"), lan);
    if (m_onlinePlay && !scanning() && !index().games.isEmpty())
        list << filter(QStringLiteral("lobby"), QStringLiteral("RetroArch Lobby"), m_lobby.size());
    for (const auto &[id, name] : kLists) {
        const qsizetype count = menuShows(QLatin1String(id)) ? filterSets(QLatin1String(id)).size() : 0;
        if (count > 0)
            list << filter(QLatin1String(id), QLatin1String(name), count);
    }
    if (scanning())
        return list;
    list << placeFilters();
    // Not Recognized while Menu Filters shows it, and in All Games' place
    // when none of the sets is a game: the screen says why.
    const Index &games = index();
    if (!games.unknown.isEmpty() && (menuShows(QLatin1String(kUnknown.first)) || games.games.isEmpty()))
        list << filter(QLatin1String(kUnknown.first), QLatin1String(kUnknown.second), games.unknown.size());
    // Search last: on the Arcade screen its keys lie past it, and ► from
    // their right end goes on round to the first filter.
    if (!games.games.isEmpty())
        list << filter(QStringLiteral("search"), QStringLiteral("Search"), filterSets(QStringLiteral("search")).size());
    return list;
}

QVariantList ArcadeBackend::filterGames(const QString &filter, int first, int count) {
    QVariantList games;
    // A filter of rooms read once, its sets from them.
    const QVariantList rooms = isRoomFilter(filter) ? filterRooms(filter) : QVariantList();
    const QStringList sets = isRoomFilter(filter) ? roomSets(rooms) : filterSets(filter);
    if (first < 0 || count <= 0 || first >= sets.size())
        return games;
    const bool unknown = filter == QLatin1String("unknown");
    const QStringList favorites = unknown ? QStringList()
                                  : filter == QLatin1String("favorites") ? sets
                                  : listedSets(QStringLiteral("favorites"));
    const int last = static_cast<int>(std::min<qsizetype>(sets.size(), qsizetype(first) + count));
    // A room: the game, who hosts it, and the room joined, with what is said
    // of it for the box (info); of the whole lobby, the room too, which may
    // be another core's game, by the name its host gave it.
    const bool lan = filter == QLatin1String("lan");
    const bool lobby = filter == QLatin1String("lobby");
    for (int i = first; i < last; ++i) {
        const QString &set = sets.at(i);
        if (!rooms.isEmpty()) {
            const QVariantMap room = rooms.value(i).toMap();
            const QString host = room.value(QStringLiteral("host")).toString();
            QVariantMap row{ { QStringLiteral("set"), set },
                             { QStringLiteral("name"), QStringLiteral("%1 (%2)").arg(
                                                           room.value(QStringLiteral("title")).toString(), host) },
                             { QStringLiteral("favorite"), favorites.contains(set) },
                             { QStringLiteral("room"), room.value(QStringLiteral("id")) },
                             { QStringLiteral("host"), host } };
            if (lobby) {
                row.insert(QStringLiteral("lobby"), room);
            } else {
                row.insert(QStringLiteral("lan"), lan);
                row.insert(QStringLiteral("detail"), room.value(QStringLiteral("detail")));
                row.insert(QStringLiteral("info"), room.value(QStringLiteral("info")));
            }
            games << row;
        } else if (filter == QLatin1String("search") && i == 0) {
            games << QVariantMap{ { QStringLiteral("set"), QString() }, { QStringLiteral("search"), true },
                                  { QStringLiteral("name"), m_search.isEmpty() ? QStringLiteral("Search") : m_search } };
        } else if (unknown) {
            games << QVariantMap{ { QStringLiteral("set"), set }, { QStringLiteral("name"), m_sets.value(set) },
                                  { QStringLiteral("unknown"), true } };
        } else {
            games << QVariantMap{ { QStringLiteral("set"), set }, { QStringLiteral("name"), titleOf(set) },
                                  { QStringLiteral("favorite"), favorites.contains(set) } };
        }
    }
    return games;
}

int ArcadeBackend::filterIndex(const QString &filter, const QString &set, int room) {
    if (!isRoomFilter(filter))
        return static_cast<int>(filterSets(filter).indexOf(set));
    const QVariantList rooms = filterRooms(filter);
    if (room >= 0) {
        for (int i = 0; i < rooms.size(); ++i) {
            if (rooms.at(i).toMap().value(QStringLiteral("id")).toInt() == room)
                return i;
        }
    }
    // The room gone: its game's first, if it is one of the folder's.
    return set.isEmpty() ? -1 : static_cast<int>(roomSets(rooms).indexOf(set));
}

QVariantMap ArcadeBackend::gameDetails(const QString &set) {
    if (!m_sets.contains(set))
        return QVariantMap();
    if (isUnrecognized(set)) {
        return { { QStringLiteral("set"), set }, { QStringLiteral("name"), m_sets.value(set) },
                 { QStringLiteral("unknown"), true }, { QStringLiteral("detail"), unknownDetail(set) } };
    }
    const QStringList pictures = picturesOf(set);
    QVariantMap details{ { QStringLiteral("set"), set }, { QStringLiteral("name"), descriptionOf(set) },
                         { QStringLiteral("playTime"), playTimeOf(set) },
                         { QStringLiteral("pictures"), pictures },
                         { QStringLiteral("picture"), pictures.value(0) },
                         { QStringLiteral("engine"), engineOf(set) } };
    const mame::Machine *machine = machineOf(set);
    if (!machine)
        return details;
    details.insert(QStringLiteral("system"), mame::systemOf(machine->sourceFile));
    details.insert(QStringLiteral("maker"), mame::makerOf(machine->manufacturer));
    details.insert(QStringLiteral("year"), machine->year);
    details.insert(QStringLiteral("players"), machine->players);
    details.insert(QStringLiteral("vertical"), machine->rotate == 90 || machine->rotate == 270);
    return details;
}

QStringList ArcadeBackend::setAndParent(const QString &set) const {
    QStringList sets{ set };
    const mame::Machine *machine = machineOf(set);
    if (machine && !machine->cloneOf.isEmpty())
        sets << machine->cloneOf;
    return sets;
}

QStringList ArcadeBackend::picturesOf(const QString &set) const {
    const QStringList sets = setAndParent(set);
    // Its own; then of a clone, its parent's numbered pictures (those
    // RetroAchievements has of a clone of the same game are saved under the
    // parent's name), and the parent's snap only for a clone without one.
    QStringList pictures;
    const QString snap = m_snaps.value(set);
    if (!snap.isEmpty())
        pictures << snap;
    pictures << m_numbered.value(set);
    if (sets.size() > 1) {
        const QString parentSnap = m_snaps.value(sets.at(1));
        if (pictures.isEmpty() && !parentSnap.isEmpty())
            pictures << parentSnap;
        pictures << m_numbered.value(sets.at(1));
    }
    // Its title screen, else its parent's; then MAME's own snapshot.
    for (const QString &s : sets) {
        const QString title = m_titles.value(s);
        if (!title.isEmpty()) {
            pictures << title;
            break;
        }
    }
    for (const QString &s : sets) {
        const QString shot = m_shots.value(s);
        if (!shot.isEmpty()) {
            pictures << shot;
            break;
        }
    }
    QStringList urls;
    for (const QString &picture : std::as_const(pictures))
        urls << QUrl::fromLocalFile(picture).toString();
    return urls;
}

QString ArcadeBackend::snapFolder() const {
    if (m_gamesFolder.isEmpty() || !m_folderReadable || !m_listed)
        return QString();
    return m_gamesFolder + QLatin1Char('/') + m_otherFolders.value(QStringLiteral("snap"), QStringLiteral("snap"));
}

QString ArcadeBackend::titlesFolder() const {
    if (m_gamesFolder.isEmpty() || !m_folderReadable || !m_listed)
        return QString();
    return m_gamesFolder + QLatin1Char('/') + m_otherFolders.value(QStringLiteral("titles"), QStringLiteral("titles"));
}

bool ArcadeBackend::snapWritable() const {
    const QString folder = snapFolder();
    return !folder.isEmpty() && QFileInfo(QFileInfo::exists(folder) ? folder : m_gamesFolder).isWritable();
}

void ArcadeBackend::wantPictures(const QString &set) {
    // None for a folder they can't be saved in (a read-only USB drive's).
    if (!m_sets.contains(set) || isUnrecognized(set) || snapFolder().isEmpty() || !snapWritable())
        return;
    // RetroAchievements' first: the set's game, else its parent's.
    bool waiting = false;
    if (m_picturesAchievements && !m_raKey.isEmpty() && !m_raKeyRefused && !m_noPictures.contains(set)) {
        const QStringList names = setAndParent(set);
        loadPictureRecord();
        const QStringList looked = picturesLooked(names);
        if (looked.size() < names.size()) {
            m_retroAchievements->findPictures(names, looked);
            waiting = true;
        } else {
            // Being downloaded: its pictures may be in in a moment.
            waiting = std::any_of(names.cbegin(), names.cend(), [this](const QString &name) {
                return m_pictureJobs.contains(name);
            });
        }
    }
    // progetto-SNAPS' for a game still without any, once RetroAchievements
    // has answered for it (onPicturesFound(), finishPicture()).
    if (!m_picturesSnaps)
        return;
    if (waiting)
        m_snapsAfter = set;
    else
        wantSnaps(set);
}

void ArcadeBackend::wantSnaps(const QString &set) {
    if (!m_picturesSnaps || !m_sets.contains(set) || isUnrecognized(set) || !snapWritable())
        return;
    // A picture of any kind, its own or its parent's, is enough.
    if (!picturesOf(set).isEmpty())
        return;
    loadSnapRecord();
    if (snapsLooked(set))
        return;
    // A moment on, should it still be shown: a list scrolled through asks
    // about none of the games passed.
    m_snapAskSet = set;
    m_snapAsk->start();
}

void ArcadeBackend::askSnaps() {
    const QString set = std::exchange(m_snapAskSet, QString());
    // Still a game without a picture, and not asked about meanwhile (the
    // look through every game may have).
    if (set.isEmpty() || !m_picturesSnaps || !m_sets.contains(set) || isUnrecognized(set) || !snapWritable()
        || !picturesOf(set).isEmpty())
        return;
    loadSnapRecord();
    if (snapsLooked(set))
        return;
    m_snapJobs.insert(set, { 0, false, snapFolder(), false });
    m_arcadeDb->lookUp({ set });
}

bool ArcadeBackend::snapsLooked(const QString &set) const {
    if (m_snapJobs.contains(set))
        return true;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto retry = m_snapsRetry.constFind(set);
    if (retry != m_snapsRetry.constEnd() && *retry > now)
        return true;
    const auto record = m_snapRecord.constFind(set);
    if (record == m_snapRecord.constEnd())
        return false;
    return record->had || (record->looked.isValid() && record->looked.secsTo(now) < kSnapsAgainSeconds);
}

void ArcadeBackend::onSnapsFound(const QString &set, const QString &ingame, const QString &title) {
    auto job = m_snapJobs.find(set);
    if (job == m_snapJobs.end())
        return;
    loadSnapRecord();
    const QString folder = job->folder;
    if (ingame.isEmpty() && title.isEmpty()) {
        // None: noted, so that it isn't asked about again for a while.
        if (folder == m_snapRecordFolder) {
            m_snapRecord.insert(set, { QDateTime::currentDateTimeUtc(), false });
            saveSnapRecord();
        }
        m_snapJobs.erase(job);
        return;
    }
    int pending = 0;
    if (!ingame.isEmpty()) {
        ++pending;
        m_arcadeDb->media(QUrl(ingame), [this, set](const QByteArray &data) { onSnapDownloaded(set, false, data); });
    }
    if (!title.isEmpty()) {
        ++pending;
        m_arcadeDb->media(QUrl(title), [this, set](const QByteArray &data) { onSnapDownloaded(set, true, data); });
    }
    job->pending = pending;
}

void ArcadeBackend::onSnapsFailed(const QStringList &sets, int holdSeconds) {
    // Tried again later: after the pause ArcadeDB asked for, at least ten
    // minutes on. Counted against the look through every game only where it
    // asked, not for a game shown meanwhile.
    const QDateTime again = pictureRetryTime(holdSeconds);
    for (const QString &set : sets) {
        const auto job = m_snapJobs.constFind(set);
        if (job == m_snapJobs.constEnd())
            continue;
        if (job->sweep && m_sweepTimer->isActive())
            ++m_sweepFailed;
        m_snapJobs.erase(job);
        m_snapsRetry.insert(set, again);
    }
    emit picturesProgressChanged();
}

void ArcadeBackend::onSnapDownloaded(const QString &set, bool title, const QByteArray &data) {
    const auto job = m_snapJobs.constFind(set);
    if (job == m_snapJobs.constEnd())
        return;
    if (data.isEmpty()) {
        finishSnap(set, false);
        return;
    }
    // Into the folders it was asked for, whatever the games folder is now:
    // the title screen beside the snap folder, in titles.
    const QString folder = title ? QFileInfo(job->folder).path() + QLatin1Char('/')
                                       + m_otherFolders.value(QStringLiteral("titles"), QStringLiteral("titles"))
                                 : job->folder;
    const QString snapFolderThen = job->folder;
    QtConcurrent::run(&m_pictureSaves, saveNamedPicture, folder, set, data)
        .then(this, [this, set, title, snapFolderThen](const QString &file) {
            if (file.isEmpty()) {
                finishSnap(set, false);
                return;
            }
            if (snapFolderThen == snapFolder()) {
                QHash<QString, QString> &pictures = title ? m_titles : m_snaps;
                if (!pictures.contains(set))
                    pictures.insert(set, file);
                if (m_reading)
                    m_namedDuringRead.append({ set, file, title });
            }
            finishSnap(set, true);
            emit picturesChanged(set);
        });
}

void ArcadeBackend::finishSnap(const QString &set, bool saved) {
    auto job = m_snapJobs.find(set);
    if (job == m_snapJobs.end())
        return;
    job->failed = job->failed || !saved;
    if (--job->pending > 0)
        return;
    if (job->failed) {
        // Tried again in ten minutes.
        m_snapsRetry.insert(set, pictureRetryTime());
        if (job->sweep && m_sweepTimer->isActive())
            ++m_sweepFailed;
    } else if (job->folder == m_snapRecordFolder) {
        m_snapRecord.insert(set, { QDateTime::currentDateTimeUtc(), true });
        saveSnapRecord();
    }
    m_snapJobs.erase(job);
    emit picturesProgressChanged();
}

void ArcadeBackend::loadSnapRecord() {
    const QString folder = snapFolder();
    if (folder == m_snapRecordFolder)
        return;
    // The last folder's record written before it is let go of.
    flushRecords();
    m_snapRecordFolder = folder;
    m_snapRecord.clear();
    QFile file(folder + QStringLiteral("/.arcadedb.json"));
    if (folder.isEmpty() || !file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject kept = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = kept.constBegin(); it != kept.constEnd(); ++it) {
        const QJsonObject set = it.value().toObject();
        m_snapRecord.insert(it.key(), { QDateTime::fromString(set.value(QStringLiteral("looked")).toString(), Qt::ISODate),
                                        set.value(QStringLiteral("had")).toBool() });
    }
}

void ArcadeBackend::saveSnapRecord() {
    m_snapRecordDirty = true;
    if (!m_recordSave->isActive())
        m_recordSave->start();
}

void ArcadeBackend::flushRecords() {
    m_recordSave->stop();
    // Each record beside its pictures, after the ones it notes (the same
    // queue), the JSON made off the GUI thread from a copy, its sets in
    // order (a QJsonObject keeps its keys sorted: inserted in order, none is
    // moved).
    if (std::exchange(m_snapRecordDirty, false) && !m_snapRecordFolder.isEmpty()) {
        const QString file = m_snapRecordFolder + QStringLiteral("/.arcadedb.json");
        const QHash<QString, SnapRecord> record = m_snapRecord;
        m_pictureSaves.start([file, record] {
            QStringList sets = record.keys();
            std::sort(sets.begin(), sets.end());
            QJsonObject kept;
            for (const QString &set : std::as_const(sets)) {
                const SnapRecord &noted = record[set];
                kept.insert(set, QJsonObject{ { QStringLiteral("looked"), noted.looked.toString(Qt::ISODate) },
                                              { QStringLiteral("had"), noted.had } });
            }
            writeFileAtomically(file, QJsonDocument(kept).toJson(QJsonDocument::Compact));
        });
    }
    if (std::exchange(m_pictureRecordDirty, false) && !m_pictureRecordFolder.isEmpty()) {
        const QString file = m_pictureRecordFolder + QStringLiteral("/.retroachievements.json");
        const QHash<QString, PictureRecord> record = m_pictureRecord;
        m_pictureSaves.start([file, record] {
            QStringList names = record.keys();
            std::sort(names.begin(), names.end());
            QJsonObject kept;
            for (const QString &name : std::as_const(names)) {
                const PictureRecord &noted = record[name];
                kept.insert(name, QJsonObject{ { QStringLiteral("saved"), QJsonArray::fromStringList(noted.saved) },
                                               { QStringLiteral("complete"), noted.complete } });
            }
            writeFileAtomically(file, QJsonDocument(kept).toJson(QJsonDocument::Compact));
        });
    }
}

QStringList ArcadeBackend::picturesLooked(const QStringList &names) const {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList looked;
    for (const QString &name : names) {
        const auto retry = m_picturesRetry.constFind(name);
        if (m_pictureRecord.value(name).complete || m_pictureJobs.contains(name)
            || (retry != m_picturesRetry.constEnd() && *retry > now))
            looked << name;
    }
    return looked;
}

void ArcadeBackend::setPictureSweepInterval(int ms) {
    m_sweepTimer->setInterval(ms);
}

void ArcadeBackend::setPictureRetryInterval(int ms) {
    m_pictureRetryMs = ms;
}

QDateTime ArcadeBackend::pictureRetryTime(qint64 atLeastSeconds) const {
    // The next look's games are due by the time it is made: a look that
    // couldn't have every picture waits as long as each game does
    // (finishPictureSweep()).
    return QDateTime::currentDateTimeUtc().addMSecs(std::max<qint64>(m_pictureRetryMs, atLeastSeconds * 1000));
}

bool ArcadeBackend::sweepingPictures() const {
    return m_sweepTimer->isActive();
}

QString ArcadeBackend::sweepMarker() const {
    return m_home + QStringLiteral("/pictures-wanted");
}

void ArcadeBackend::startPictureSweep() {
    m_sweepWanted = false;
    m_sweepAgain->stop();
    const bool achievements = m_picturesAchievements && !m_raKey.isEmpty() && !m_raKeyRefused;
    if (!snapWritable() || (!achievements && !m_picturesSnaps)) {
        QFile::remove(sweepMarker());
        emit picturesProgressChanged();
        return;
    }
    // Kept until a look ends with every picture had: a look cut short goes
    // on at the next start.
    QDir().mkpath(m_home);
    writeFileAnew(sweepMarker(), QByteArray("wanted\n"));
    m_sweepPass = achievements ? SweepPass::Achievements : SweepPass::Snaps;
    m_sweepListed = false;
    m_sweepSnapsListed = false;
    m_sweep.clear();
    m_sweepSnaps.clear();
    m_sweepGame.clear();
    m_sweepDone = 0;
    m_sweepTotal = 0;
    m_sweepAsked = 0;
    m_sweepSnapsAsked = 0;
    m_sweepFailed = 0;
    m_sweepResult.clear();
    m_sweepTimer->start();
    emit picturesProgressChanged();
}

void ArcadeBackend::sweepPictures() {
    if (running() || scanning())
        return;
    if (!snapWritable()) {
        stopPictureSweep();
        return;
    }
    if (m_sweepPass == SweepPass::Achievements) {
        // The source gone meanwhile (its key, its setting): the next pass.
        if (!m_picturesAchievements || m_raKey.isEmpty() || m_raKeyRefused) {
            nextSweepPass();
            return;
        }
        // RetroAchievements' list of games in first: the one kept, or one a
        // week old read again.
        if (!m_retroAchievements->hasList() && !m_retroAchievements->listPending()
            && m_retroAchievements->listFailed()) {
            // None could be read (off line, say) and none is kept: nothing to
            // sweep by. Wanted still, for the next read of the games, rather
            // than asked for again every second until the app ends (asking,
            // wantList(), would have it pending again at once).
            stopPictureSweep();
            m_sweepWanted = true;
            return;
        }
        m_retroAchievements->wantList();
        if (m_retroAchievements->listPending() || !m_retroAchievements->hasList())
            return;
        if (!std::exchange(m_sweepListed, true)) {
            m_sweep = index().games;
            m_sweepTotal = static_cast<int>(m_sweep.size());
            m_sweepDone = 0;
        }
        if (!m_sweepGame.isEmpty()) {
            const bool saving = std::any_of(m_sweepGame.cbegin(), m_sweepGame.cend(),
                                            [this](const QString &name) { return m_pictureJobs.contains(name); });
            if (m_retroAchievements->laterPending() || saving)
                return;
            m_sweepGame.clear();
        }
        loadPictureRecord();
        while (!m_sweep.isEmpty()) {
            const QString set = m_sweep.takeFirst();
            ++m_sweepDone;
            if (!m_sets.contains(set) || isUnrecognized(set) || m_noPictures.contains(set))
                continue;
            const QStringList names = setAndParent(set);
            const QStringList looked = picturesLooked(names);
            const bool known = std::any_of(names.cbegin(), names.cend(), [this](const QString &name) {
                return m_retroAchievements->knowsGame(name);
            });
            if (!known || looked.size() == names.size())
                continue;
            m_sweepGame = names;
            ++m_sweepAsked;
            m_retroAchievements->findPicturesLater(names, looked);
            emit picturesProgressChanged();
            return;
        }
        nextSweepPass();
        return;
    }
    // progetto-SNAPS', through ArcadeDB, for the games still without any
    // picture: a batch asked about, its pictures saved, then the next.
    if (!m_picturesSnaps) {
        finishPictureSweep();
        return;
    }
    if (!std::exchange(m_sweepSnapsListed, true)) {
        m_sweepSnaps.clear();
        loadSnapRecord();
        for (const QString &set : index().games) {
            if (picturesOf(set).isEmpty() && !snapsLooked(set))
                m_sweepSnaps << set;
        }
        m_sweepTotal = static_cast<int>(m_sweepSnaps.size());
        m_sweepDone = 0;
        emit picturesProgressChanged();
    }
    if (m_arcadeDb->busy() || !m_snapJobs.isEmpty())
        return;
    if (m_sweepSnaps.isEmpty()) {
        finishPictureSweep();
        return;
    }
    QStringList batch;
    while (batch.size() < ArcadeDb::kMostAsked && !m_sweepSnaps.isEmpty()) {
        const QString set = m_sweepSnaps.takeFirst();
        ++m_sweepDone;
        // Had meanwhile (shown, and asked about then).
        if (picturesOf(set).isEmpty() && !snapsLooked(set))
            batch << set;
    }
    for (const QString &set : std::as_const(batch))
        m_snapJobs.insert(set, { 0, false, snapFolder(), true });
    if (!batch.isEmpty()) {
        m_sweepSnapsAsked += static_cast<int>(batch.size());
        m_arcadeDb->lookUp(batch);
    }
    emit picturesProgressChanged();
}

void ArcadeBackend::nextSweepPass() {
    if (m_sweepPass == SweepPass::Achievements) {
        m_sweepPass = SweepPass::Snaps;
        m_sweep.clear();
        m_sweepGame.clear();
        emit picturesProgressChanged();
        return;
    }
    finishPictureSweep();
}

void ArcadeBackend::finishPictureSweep() {
    qInfo("[Arcade] Pictures looked for: %d games asked of RetroAchievements, %d of ArcadeDB, %d whose pictures "
          "couldn't be had",
          m_sweepAsked, m_sweepSnapsAsked, m_sweepFailed);
    m_sweepTimer->stop();
    m_sweep.clear();
    m_sweepSnaps.clear();
    m_sweepGame.clear();
    if (m_sweepFailed > 0 && ++m_sweepPasses < kSweepPasses) {
        // Those tried again once the last of their retries is due: ten
        // minutes on, or later where a server asked for a longer pause,
        // rather than a look that finds them all still waiting and ends.
        const QDateTime now = QDateTime::currentDateTimeUtc();
        qint64 wait = m_pictureRetryMs;
        for (const QHash<QString, QDateTime> *retries : { &m_snapsRetry, &m_picturesRetry }) {
            for (const QDateTime &due : *retries) {
                if (due > now)
                    wait = std::max(wait, now.msecsTo(due) + 1000);
            }
        }
        m_sweepResult = QStringLiteral("%1 Not Downloaded, Trying Again").arg(m_sweepFailed);
        m_sweepAgain->start(static_cast<int>(std::min<qint64>(wait, std::numeric_limits<int>::max())));
    } else {
        m_sweepResult = m_sweepFailed > 0 ? QStringLiteral("%1 Not Downloaded").arg(m_sweepFailed)
                                          : QStringLiteral("Done");
        m_sweepPasses = 0;
        QFile::remove(sweepMarker());
    }
    emit picturesProgressChanged();
}

void ArcadeBackend::stopPictureSweep() {
    m_sweepTimer->stop();
    m_sweep.clear();
    m_sweepSnaps.clear();
    m_sweepGame.clear();
    // Nothing under way to count: the frame says so.
    m_sweepDone = 0;
    m_sweepTotal = 0;
    emit picturesProgressChanged();
}

void ArcadeBackend::downloadPictures() {
    m_sweepPasses = 0;
    m_sweepAgain->stop();
    if (m_reading || scanning()) {
        // Once the games have been read (emitShown()): wanted, and said so.
        m_sweepWanted = true;
        emit picturesProgressChanged();
        return;
    }
    startPictureSweep();
}

QVariantMap ArcadeBackend::picturesProgress() const {
    const bool active = m_sweepTimer->isActive() || m_sweepAgain->isActive() || m_sweepWanted;
    return { { QStringLiteral("active"), active },
             { QStringLiteral("source"), m_sweepPass == SweepPass::Achievements ? QStringLiteral("RetroAchievements")
                                                                                 : QStringLiteral("progetto-SNAPS") },
             { QStringLiteral("done"), m_sweepDone },
             { QStringLiteral("total"), m_sweepTotal },
             { QStringLiteral("asked"), m_sweepAsked + m_sweepSnapsAsked },
             { QStringLiteral("failed"), m_sweepFailed },
             { QStringLiteral("again"), m_sweepAgain->isActive() },
             { QStringLiteral("result"), m_sweepResult } };
}

QVariantMap ArcadeBackend::picturesInfo() const {
    int withPictures = 0;
    const QStringList &games = index().games;
    for (const QString &set : games) {
        if (!picturesOf(set).isEmpty())
            ++withPictures;
    }
    return { { QStringLiteral("games"), static_cast<int>(games.size()) },
             { QStringLiteral("withPictures"), withPictures },
             { QStringLiteral("achievements"), m_picturesAchievements },
             { QStringLiteral("snaps"), m_picturesSnaps },
             { QStringLiteral("key"), m_raKey.isEmpty() ? QStringLiteral("none")
                                      : m_raKeyRefused ? QStringLiteral("refused")
                                      : m_retroAchievements->keyWorks() ? QStringLiteral("works") : QStringLiteral("given") },
             { QStringLiteral("writable"), snapWritable() },
             { QStringLiteral("snapFolder"), snapFolder() },
             { QStringLiteral("titlesFolder"), titlesFolder() } };
}

void ArcadeBackend::setPictureServer(const QUrl &arcadeDb) {
    m_arcadeDb->setService(arcadeDb);
}

// The set shown waits for RetroAchievements' answer about name (its own or
// its parent's) before progetto-SNAPS' are asked for.
void ArcadeBackend::snapsAfter(const QString &name) {
    if (m_snapsAfter.isEmpty() || !setAndParent(m_snapsAfter).contains(name))
        return;
    wantSnaps(std::exchange(m_snapsAfter, QString()));
}

void ArcadeBackend::onPicturesFound(const QString &name, const QString &state, const QStringList &paths) {
    if (state == QLatin1String("unknown")) {
        m_noPictures.insert(name);
        snapsAfter(name);
        return;
    }
    // Nothing read, its pictures looked for before: only progetto-SNAPS'
    // turn for the game shown, should it still be without any.
    if (state == QLatin1String("looked")) {
        snapsAfter(name);
        return;
    }
    if (state != QLatin1String("found")) {
        m_picturesRetry.insert(name, pictureRetryTime());
        if (m_sweepTimer->isActive() && m_sweepGame.contains(name))
            ++m_sweepFailed;
        snapsAfter(name);
        return;
    }
    loadPictureRecord();
    if (m_pictureJobs.contains(name) || m_pictureRecordFolder.isEmpty())
        return;
    PictureRecord &record = m_pictureRecord[name];
    QStringList wanted;
    for (const QString &path : paths) {
        if (!record.saved.contains(path))
            wanted << path;
    }
    if (wanted.isEmpty()) {
        record.complete = true;
        savePictureRecord();
        snapsAfter(name);
        return;
    }
    m_pictureJobs.insert(name, { static_cast<int>(wanted.size()), false, m_pictureRecordFolder });
    for (const QString &path : std::as_const(wanted)) {
        m_retroAchievements->media(path, [this, name, path](const QByteArray &data) {
            onPictureDownloaded(name, path, data);
        });
    }
}

void ArcadeBackend::onPictureDownloaded(const QString &name, const QString &path, const QByteArray &data) {
    const auto job = m_pictureJobs.constFind(name);
    if (job == m_pictureJobs.constEnd())
        return;
    // Into the folder it was asked for, whatever the games folder is now.
    const QString folder = job->folder;
    if (data.isEmpty()) {
        finishPicture(name, false);
        return;
    }
    QtConcurrent::run(&m_pictureSaves, savePicture, folder, name, data, m_sets).then(this, [this, name, path, folder](const QString &file) {
        if (file.isEmpty()) {
            finishPicture(name, false);
            return;
        }
        if (folder == m_pictureRecordFolder)
            m_pictureRecord[name].saved << path;
        if (folder == snapFolder()) {
            addNumbered(m_numbered, name, file);
            if (m_reading)
                m_savedDuringRead[name] << file;
        }
        finishPicture(name, true);
        emit picturesChanged(name);
    });
}

void ArcadeBackend::finishPicture(const QString &name, bool saved) {
    auto job = m_pictureJobs.find(name);
    if (job == m_pictureJobs.end())
        return;
    job->failed = job->failed || !saved;
    const bool recorded = job->folder == m_pictureRecordFolder;
    bool done = false;
    if (--job->pending <= 0) {
        // All in, or those that weren't tried again in ten minutes.
        if (job->failed) {
            m_picturesRetry.insert(name, pictureRetryTime());
            if (m_sweepTimer->isActive() && m_sweepGame.contains(name))
                ++m_sweepFailed;
        } else if (recorded) {
            m_pictureRecord[name].complete = true;
        }
        m_pictureJobs.erase(job);
        done = true;
    }
    // The record as it now stands, unless nothing came into it.
    if (saved && recorded)
        savePictureRecord();
    if (done)
        snapsAfter(name);
}

void ArcadeBackend::loadPictureRecord() {
    const QString folder = snapFolder();
    if (folder == m_pictureRecordFolder)
        return;
    // The last folder's record written before it is let go of.
    flushRecords();
    m_pictureRecordFolder = folder;
    m_pictureRecord.clear();
    QFile file(folder + QStringLiteral("/.retroachievements.json"));
    if (folder.isEmpty() || !file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject kept = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = kept.constBegin(); it != kept.constEnd(); ++it) {
        const QJsonObject game = it.value().toObject();
        PictureRecord record;
        for (const QJsonValue &path : game.value(QStringLiteral("saved")).toArray())
            record.saved << path.toString();
        record.complete = game.value(QStringLiteral("complete")).toBool();
        m_pictureRecord.insert(it.key(), record);
    }
}

void ArcadeBackend::savePictureRecord() {
    m_pictureRecordDirty = true;
    if (!m_recordSave->isActive())
        m_recordSave->start();
}

// What an engine looks for a set as, which a file named in capitals isn't.
QString ArcadeBackend::looksForLowerCase(const QString &engine, const QString &set) const {
    return QStringLiteral("%1 looks for %2, in lower case").arg(engine, mameFileName(set));
}

QString ArcadeBackend::mameFileName(const QString &set) const {
    return m_folders.contains(set) ? set
                                   : set + QLatin1Char('.') + QFileInfo(m_sets.value(set)).suffix().toLower();
}

QString ArcadeBackend::unknownDetail(const QString &set) const {
    if (!mame::isShortName(set))
        return QStringLiteral("Not the name of a set MAME knows\nA set is named as MAME names its game: pacman.zip");
    if (m_misnamed.contains(set))
        return looksForLowerCase(QStringLiteral("MAME"), set) + QStringLiteral("\nRename the file to ") + mameFileName(set);
    // FinalBurn Neo has it, but not as a folder of files.
    if (m_fbneoGames.contains(set))
        return QStringLiteral("MAME has no game called %1\nFinalBurn Neo has it, but plays a zip or a 7z, not a folder").arg(set);
    if (!m_fbneoGames.isEmpty())
        return QStringLiteral("Neither MAME nor FinalBurn Neo has a game called %1\nIts set may be for another version of MAME")
            .arg(set);
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
        // Favorites and Recently Played whether or not they hold a game yet;
        // with Online Play on, RetroArch's lobby, which may have no room.
        QVariantList options{ QVariantMap{ { QStringLiteral("id"), QStringLiteral("all") },
                                           { QStringLiteral("label"), QStringLiteral("All Games") } } };
        if (m_onlinePlay)
            options << QVariantMap{ { QStringLiteral("id"), QStringLiteral("lobby") },
                                    { QStringLiteral("label"), QStringLiteral("RetroArch Lobby") } };
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
    const mame::Machine *machine = machineOf(set);
    QStringList detail;
    if (machine) {
        // How well it plays in the engine whose word that is.
        const QString engine = m_machines.contains(set) ? QStringLiteral("MAME") : QStringLiteral("FinalBurn Neo");
        if (!machine->year.isEmpty())
            detail << machine->year;
        if (!machine->manufacturer.isEmpty())
            detail << machine->manufacturer;
        if (machine->players > 0)
            detail << (machine->players == 1 ? QStringLiteral("1 player")
                                             : QStringLiteral("%1 players").arg(machine->players));
        if (machine->status == QLatin1String("preliminary"))
            detail << QStringLiteral("Not working in ") + engine;
        else if (machine->status == QLatin1String("imperfect"))
            detail << QStringLiteral("Imperfect in ") + engine;
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
        // Those still in the folder, as MAME says they are now, and games as
        // the Arcade screen's lists have them (not a bootleg Hide Hacks and
        // Bootlegs leaves out).
        if (isGame(set))
            entries << gameEntry(set);
    }
    return entries;
}

QVariant ArcadeBackend::requestEntries(const QString &path) {
    // To be listed again should what it holds change.
    m_shown.insert(path);
    if (path == QLatin1String("recent") || path == QLatin1String("favorites"))
        return listEntries(path);
    // The rooms once the lobby has been read (refreshRooms()), or the local
    // network asked (refreshLanRooms()).
    if (path == QLatin1String("online")) {
        if (!m_roomsRead) {
            refreshRooms();
            return QVariant();
        }
        return roomEntries();
    }
    if (path == QLatin1String("lan")) {
        if (!m_lanRead) {
            refreshLanRooms();
            return QVariant();
        }
        return lanRoomEntries();
    }
    // Online Games and LAN Games first, with Online Play and LAN Play on.
    QVariantList top;
    const bool fbneo = (m_onlinePlay || m_lanPlay) && fbneoReady();
    if (m_onlinePlay && fbneo)
        top << folder(QStringLiteral("Online Games"), QStringLiteral("online"));
    if (m_lanPlay && fbneo)
        top << folder(QStringLiteral("LAN Games"), QStringLiteral("lan"));
    if (scanning()) {
        // The lists at once; the rest once MAME has said what the sets are.
        if (path == QLatin1String("top"))
            return top << folder(QStringLiteral("Recently Played"), QStringLiteral("recent"))
                       << folder(QStringLiteral("Favorites"), QStringLiteral("favorites"));
        return QVariant();
    }
    const Index &games = index();
    if (path == QLatin1String("top")) {
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
    // Found as the Arcade screen's Search finds them; nothing for no words.
    if (path.startsWith(QLatin1String("search/"))) {
        const QString words = path.mid(7);
        return gameEntries(searchable(words).isEmpty() ? QStringList() : searchSets(words));
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
    return m_launcher->isBusy() || m_pendingJoin.has_value();
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
    if (running()) {
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
        m_lastError = looksForLowerCase(QStringLiteral("MAME"), name) + QStringLiteral(": rename the file to ") + mameFileName(name);
        return false;
    }

    // MAME's keeping, and the players as Settings → Controls has them now,
    // with their Arcade Buttons.
    for (const char *dir : { "cfg", "nvram", "ctrlr" })
        QDir().mkpath(m_home + QLatin1Char('/') + QLatin1String(dir));
    QList<QStringList> arcade;
    for (const controllers::ArcadeBinds &binds : controllers::playerArcade())
        arcade << binds.mame;
    // The files made for each game (this one, the credits' script, FinalBurn
    // Neo's info and config below) are written in place, without a wait for
    // the card each.
    if (!writeFileAnew(m_home + QStringLiteral("/ctrlr/osdos.cfg"),
                       mame::controllerConfig(controllers::playerGuids(), arcade).toUtf8())) {
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
    // Emulator Video: the screen's shape, and the game's lines whole.
    args << mame::screenArguments(m_machines.value(name), m_screenSize.height(), screenShape(), integerScale());
    // MAME may not start at all without a sound card it can open (no USB
    // card on a Pi 5 whose HDMI is off): the game is played in silence.
    if (!AudioOutput::hasPlaybackCard())
        args << QStringLiteral("-sound") << QStringLiteral("none");
    // The credits bought on the Arcade screen, put in as the game starts.
    // Without the script the game still starts, with no credits.
    if (credits > 0) {
        const QString coins = m_home + QStringLiteral("/coins.lua");
        if (writeFileAnew(coins, mame::coinScript(std::min(credits, kMaxCredits)).toUtf8()))
            args << QStringLiteral("-autoboot_script") << coins;
        else
            qWarning("[Arcade] The credits could not be written for MAME: the game starts without them");
    }

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
    entry.environment = padEnvironment(m_dataRoot);

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

void ArcadeBackend::setScreen(bool tube, const QSize &size) {
    m_tube = tube;
    m_screenSize = size;
}

QString ArcadeBackend::screenShape() const {
    if (m_screenShape == QLatin1String("Square Pixels"))
        return QString();
    if (!m_screenShape.isEmpty())
        return m_screenShape;
    return m_tube ? QStringLiteral("4:3") : QString();
}

bool ArcadeBackend::integerScale() const {
    if (!m_integerScale.isEmpty())
        return m_integerScale == QLatin1String("On");
    return m_tube;
}

double ArcadeBackend::gameAspect(const QString &set) const {
    const double listed = m_fbneoAspects.value(set);
    if (listed > 0)
        return listed;
    const mame::Machine *machine = machineOf(set);
    const int rotate = machine ? machine->rotate : 0;
    return rotate == 90 || rotate == 270 ? 3.0 / 4.0 : 4.0 / 3.0;
}

QString ArcadeBackend::engineOf(const QString &set) const {
    return engineFor(set, fbneoReady(), !mamePath().isEmpty());
}

QString ArcadeBackend::engineFor(const QString &set, bool fbneoUsable, bool haveMame) const {
    // One MAME doesn't know, whatever was chosen: without FinalBurn Neo
    // ready, it says what is missing.
    if (fbneoOnly(set))
        return QStringLiteral("fbneo");
    const bool fbneo = fbneoUsable && fbneoHas(set);
    const QString chosen = m_engines.value(set).toString();
    if (chosen == QLatin1String("mame") && haveMame)
        return QStringLiteral("mame");
    if (fbneo && (chosen == QLatin1String("fbneo") || fbneoWanted() || !haveMame))
        return QStringLiteral("fbneo");
    return QStringLiteral("mame");
}

QStringList ArcadeBackend::madeSets(const QString &filter) const {
    if (const auto made = m_madeSets.constFind(filter); made != m_madeSets.constEnd())
        return *made;
    QStringList sets;
    if (filter == QLatin1String("achievements") && m_achievements && !m_raKey.isEmpty()) {
        // The games where achievements or a leaderboard's scores can be had:
        // with Online High Scores on RetroAchievements, those that play in
        // FinalBurn Neo and that RetroAchievements has a set of, by the hash
        // of their own file (a clone's own, not its parent's), as its list of
        // games says. The list needs the key.
        m_retroAchievements->wantList();
        const bool ready = fbneoReady();
        const bool haveMame = !mamePath().isEmpty();
        for (const QString &set : index().games) {
            if (ready && engineFor(set, ready, haveMame) == QLatin1String("fbneo")
                && m_retroAchievements->hasGame(QFileInfo(m_sets.value(set)).fileName()))
                sets << set;
        }
    } else if (filter == QLatin1String("netplay") && m_onlinePlay && fbneoReady()) {
        // The games that can be hosted, and joined in the lobby, with Online
        // Play on: FinalBurn Neo's, whichever engine plays them off line, as
        // a game online plays in it (canHost()).
        for (const QString &set : index().games) {
            if (fbneoHas(set))
                sets << set;
        }
    }
    m_madeSets.insert(filter, sets);
    return sets;
}

bool ArcadeBackend::filterPending(const QString &filter) const {
    return filter == QLatin1String("achievements") && m_achievements && !m_raKey.isEmpty() && menuShows(filter)
           && m_retroAchievements->listPending();
}

QVariantList ArcadeBackend::engineChoices(const QString &set) const {
    // One MAME doesn't know plays in FinalBurn Neo alone: MAME, chosen for
    // it before, is Auto, and with nothing to choose its options have no
    // Engine (EngineChooser's offered()).
    const bool only = fbneoOnly(set);
    QString chosen = m_engines.value(set).toString();
    if (only && chosen == QLatin1String("mame"))
        chosen.clear();
    const auto choice = [&chosen](const QString &id, const QString &label) {
        return QVariantMap{ { QStringLiteral("id"), id }, { QStringLiteral("label"), label },
                            { QStringLiteral("current"), chosen == id || (id == QLatin1String("auto") && chosen.isEmpty()) } };
    };
    if (only)
        return { choice(QStringLiteral("auto"), QStringLiteral("Auto (FinalBurn Neo)")),
                 choice(QStringLiteral("fbneo"), QStringLiteral("FinalBurn Neo")) };
    // Auto as it stands: what the game would play in left to it.
    const bool fbneo = fbneoPlays(set);
    const bool autoFbneo = fbneo && (fbneoWanted() || mamePath().isEmpty());
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

bool ArcadeBackend::fbneoPlays(const QString &set) const {
    return fbneoReady() && fbneoHas(set);
}

bool ArcadeBackend::fbneoHas(const QString &set) const {
    return m_fbneoGames.contains(set) && m_sets.contains(set) && !m_folders.contains(set) && !m_misnamed.contains(set);
}

int ArcadeBackend::hostPorts(const QString &set, int *most) const {
    const mame::Machine *machine = machineOf(set);
    // As many as the game plays at once. A game whose players aren't known
    // (a set MAME doesn't know: FinalBurn Neo's list doesn't say) may play
    // two or six: the host keeps port 1 alone, so that a machine joining has
    // port 2 whichever it is (a second stick of the host's would take it, and
    // in a two-player game the machine joining would then drive no one), and
    // the machines joining may take ports up to six.
    const int players = machine && machine->players > 0 ? std::min(machine->players, libretro::kMostPorts) : 0;
    if (most)
        *most = players > 0 ? players : libretro::kMostPorts;
    if (players < 2)
        return 1;
    return std::clamp(libretro::localPorts(controllers::playerGuids()), 1, players - 1);
}

QString ArcadeBackend::onlineLayout(bool hosting) const {
    if (!hosting)
        return QStringLiteral("classic");
    QList<bool> panels;
    const QStringList guids = controllers::playerGuids();
    const QStringList names = controllers::playerNames();
    const QList<controllers::ArcadeBinds> arcade = controllers::playerArcade();
    QList<QStringList> binds;
    for (int p = 0; p < guids.size(); ++p) {
        panels << (!guids.at(p).isEmpty() && controllers::isJammaStick(names.value(p)));
        binds << (guids.at(p).isEmpty() ? QStringList() : arcade.value(p).retroarch);
    }
    return libretro::hostLayout(panels, binds) == libretro::Launch::Layout::Panel ? QStringLiteral("panel")
                                                                                   : QStringLiteral("classic");
}

bool ArcadeBackend::playFbneo(const QString &name, libretro::Launch::Netplay netplay, const QString &joinAddress,
                              int portsFrom, int portsGiven, libretro::Launch::Layout layout) {
    // As play() checks before coming here: one game at a time, whether
    // hosted, joined or played alone.
    if (running()) {
        m_lastError = QStringLiteral("A game is still running");
        return false;
    }
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
        m_lastError = looksForLowerCase(QStringLiteral("FinalBurn Neo"), name) + QStringLiteral(": rename the file to ") + mameFileName(name);
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
            const QString target = fbneo + QLatin1Char('/') + sets.fileName();
#ifdef Q_OS_WIN
            // Windows makes no symbolic link without a privilege (QFile::link
            // makes a shortcut there, which FinalBurn Neo can't open): a copy
            // of the set, made anew once the set has changed.
            const QFileInfo have(target);
            const QFileInfo set = sets.fileInfo();
            if (!have.exists() || have.size() != set.size() || have.lastModified() < set.lastModified()) {
                QFile::remove(target);
                QFile::copy(set.filePath(), target);
            }
#else
            QFile::link(sets.filePath(), target);
#endif
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
    launch.remapDir = home + QStringLiteral("/remaps");
    launch.infoDir = home + QStringLiteral("/info");
#ifdef Q_OS_LINUX
    // A desktop's RetroArch draws in its window; the console's needs KMS.
    launch.console = qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY");
#else
    launch.console = false;
#endif
    launch.sound = AudioOutput::hasPlaybackCard();
    // Emulator Video: the picture's shape on the screen, and its lines whole.
    const QString shape = screenShape();
    if (!shape.isEmpty())
        launch.aspect = libretro::viewportAspect(gameAspect(name), m_screenSize,
                                                 shape == QLatin1String("16:9") ? 16.0 / 9.0 : 4.0 / 3.0);
    launch.integerScale = integerScale();
    const QStringList guids = controllers::playerGuids();
    const QStringList names = controllers::playerNames();
    const QList<controllers::ArcadeBinds> arcade = controllers::playerArcade();
    for (int p = 0; p < guids.size(); ++p) {
        launch.pads << (guids.at(p).isEmpty() ? QString() : libretro::reservedDevice(guids.at(p), names.value(p)));
        launch.panels << (!guids.at(p).isEmpty() && controllers::isJammaStick(names.value(p)));
        launch.arcade << (guids.at(p).isEmpty() ? QStringList() : arcade.value(p).retroarch);
    }
    m_raPasswordGiven = false;
    if (m_achievements && !m_raUser.isEmpty()) {
        // A sign-in still being checked: the game's RetroArch signs in
        // instead. (Without Online High Scores it goes on beside the game.)
        if (stopSignIn())
            emit achievementsChanged();
        launch.achievementsUser = m_raUser;
        launch.achievementsToken = m_raToken;
        if (m_raToken.isEmpty() && !m_raPassword.isEmpty()) {
            launch.achievementsPassword = m_raPassword;
            m_raPasswordGiven = true;
        }
    }
    launch.netplay = netplay;
    launch.joinAddress = joinAddress;
    launch.nickname = nickname(netplay == libretro::Launch::Netplay::LanHost
                               || netplay == libretro::Launch::Netplay::LanJoin);
    launch.relay = m_relay;
    using Netplay = libretro::Launch::Netplay;
    const bool hosting = netplay == Netplay::Host || netplay == Netplay::LanHost;
    // The controller types every port plays with online: a host's own
    // choice, by its players (the panel's, with a stick or Arcade Buttons
    // among them), told to the machines joining on the local network with
    // their ports; a machine joining takes what it was told, Classic from
    // a host that said nothing (RetroArch alone, an older OSD/OS, or a room
    // of the lobby).
    launch.layout = hosting ? libretro::hostLayout(launch.panels, launch.arcade) : layout;
    // The game's ports this machine's players take: a host's from 1
    // (hostPorts()); joining on the local network, those its host's OSD/OS
    // gave, or, with no answer, from 2, where more than one player is to
    // play. Joining a room of the lobby, RetroArch's own: one port, the
    // first free.
    int most = libretro::kMostPorts;
    const int own = hostPorts(name, &most);
    const int ports = libretro::localPorts(guids);
    if (hosting) {
        launch.portsFrom = 1;
        launch.ports = own;
    } else if (netplay == Netplay::LanJoin && portsFrom > 0) {
        launch.portsFrom = portsFrom;
        launch.ports = std::clamp(portsGiven, 1, ports);
    } else if (netplay == Netplay::LanJoin && portsFrom < 0 && ports > 1) {
        launch.portsFrom = 2;
        launch.ports = ports;
    }
    // Hosting on the local network: the machines joining given their ports
    // from now on. Without the socket for it this machine keeps to port 1,
    // so that a machine joining, unanswered, finds port 2 free.
    if (netplay == Netplay::LanHost && !startClaims(launch.ports, most, launch.layout))
        launch.ports = 1;
    // FinalBurn Neo's remap file: the arcade panels' players, and those with
    // Arcade Buttons, on its 6-Button Panel; none without one, nor online.
    // One of the user's own stays as it is, loaded in its place.
    const QString remap = libretro::coreRemapPath(launch.remapDir);
    const QString remapText = libretro::remapText(launch);
    bool written = true;
    if (!libretro::ownsRemap(remap))
        qInfo("[Arcade] %s is the user's own: left as it is", qPrintable(remap));
    else if (remapText.isEmpty())
        QFile::remove(remap);
    else
        // Whole, not in place as the other files: one cut short (the power
        // gone mid-write) would pass for the user's own (ownsRemap()) and
        // never be written again.
        written = QDir().mkpath(QFileInfo(remap).path()) && writeFileAtomically(remap, remapText.toUtf8(), kOwnerOnly);
    // FinalBurn Neo's info file, without which RetroArch neither hosts nor
    // joins a game.
    written = written && QDir().mkpath(launch.infoDir)
              && writeFileAnew(libretro::coreInfoPath(launch.infoDir), libretro::coreInfoText().toUtf8(), kOwnerOnly);
    // RetroAchievements' hardcore mode, the one whose scores the leaderboards
    // take, runs only while no core option it disallows is set, and FinalBurn
    // Neo's default allows patched sets, over which RetroArch would pause it
    // ("Setting not allowed: fbneo-allow-patched-romsets=enabled"). The
    // core's options are given the value hardcore wants, the rest kept, and
    // written only when that changes them.
    if (written && !launch.achievementsUser.isEmpty()) {
        const QString options = libretro::coreOptionsPath(launch.configDir);
        QFile file(options);
        const QString before = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        const QString after = libretro::withHardcoreOptions(before);
        if (after != before)
            written = QDir().mkpath(QFileInfo(options).path()) && writeFileAtomically(options, after.toUtf8(), kOwnerOnly);
    }
    if (!written || !writeFileAnew(launch.config, libretro::configText(launch).toUtf8(), kOwnerOnly)) {
        m_lastError = QStringLiteral("RetroArch's settings could not be written");
        m_raPasswordGiven = false;
        stopClaims();
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
    entry.environment = padEnvironment(m_dataRoot);

    // As for MAME: the batch MAME is asked meanwhile shares the machine.
    pauseScan();
    QString error;
    if (!m_launcher->start(entry, &error)) {
        m_lastError = error;
        if (m_raPasswordGiven) {
            m_raPasswordGiven = false;
            QFile::remove(launch.config);
        }
        stopClaims();
        if (!m_scan && !m_toAsk.isEmpty())
            scanNext();
        return false;
    }
    m_playing = name;
    m_playingEngine = QStringLiteral("fbneo");
    m_playingNetplay = netplay;
    m_playClock.start();
    // Over the network: RetroArch's log watched for the failure it would
    // play on alone after.
    if (netplay != libretro::Launch::Netplay::None) {
        m_netplayLogRead = 0;
        m_netplayLogTail.clear();
        m_netplayErrors.clear();
        m_netplayWatch->start();
    }
    if (!m_played) {
        m_played = true;
        emit playedChanged();
    }
    return true;
}

bool ArcadeBackend::canHost(const QString &set) const {
    return m_onlinePlay && fbneoPlays(set);
}

bool ArcadeBackend::hostGame(const QString &set) {
    return playFbneo(set, libretro::Launch::Netplay::Host);
}

bool ArcadeBackend::joinRoom(int id) {
    for (const libretro::Room &room : std::as_const(m_rooms)) {
        if (room.id == id)
            return playFbneo(room.game, libretro::Launch::Netplay::Join, room.joinAddress);
    }
    m_lastError = QStringLiteral("The game is no longer in RetroArch's lobby");
    return false;
}

bool ArcadeBackend::canHostLan(const QString &set) const {
    return m_lanPlay && fbneoPlays(set);
}

bool ArcadeBackend::hostLanGame(const QString &set) {
    return playFbneo(set, libretro::Launch::Netplay::LanHost);
}

bool ArcadeBackend::joinLanRoom(int id) {
    for (const libretro::Room &room : std::as_const(m_lanRooms)) {
        if (room.id != id)
            continue;
        if (running()) {
            m_lastError = QStringLiteral("A game is still running");
            return false;
        }
        // The players' ports asked of the host's OSD/OS first, the game
        // started once it has answered, or hasn't; a host that can't be
        // asked at all is joined at once, on RetroArch's own terms.
        m_pendingJoin = PendingJoin{ room.game, room.joinAddress, room.joinAddress.section(QLatin1Char('|'), 0, 0),
                                     libretro::localPorts(controllers::playerGuids()), 0 };
        if (askForPorts())
            return true;
        const PendingJoin join = *std::exchange(m_pendingJoin, std::nullopt);
        return playFbneo(join.game, libretro::Launch::Netplay::LanJoin, join.joinAddress, -1);
    }
    m_lastError = QStringLiteral("The game is no longer hosted on this network");
    return false;
}

bool ArcadeBackend::askForPorts() {
    if (!m_claimAsk) {
        m_claimAsk = new QUdpSocket(this);
        connect(m_claimAsk, &QUdpSocket::readyRead, this, &ArcadeBackend::readPortsAnswer);
        m_claimWait = new QTimer(this);
        m_claimWait->setSingleShot(true);
        // Not answered: asked again, then joined without an answer. Ports
        // answered without the layout, which hasn't come after them either:
        // joined on the Classic layout.
        connect(m_claimWait, &QTimer::timeout, this, [this] {
            if (!m_pendingJoin)
                return;
            if (const std::optional<libretro::PortRange> ports = m_pendingJoin->ports) {
                qInfo("[Arcade] %s said nothing of its layout: the Classic one", qPrintable(m_pendingJoin->host));
                startPendingJoin(ports);
                return;
            }
            if (!askForPorts())
                startPendingJoin(std::nullopt);
        });
    }
    const QHostAddress host(m_pendingJoin->host);
    if (m_pendingJoin->asked >= kClaimAskings || host.isNull()
        || (m_claimAsk->state() != QAbstractSocket::BoundState && !m_claimAsk->bind(QHostAddress(QHostAddress::AnyIPv4), 0))
        || m_claimAsk->writeDatagram(libretro::claimQuery(m_pendingJoin->players), host, m_claimPort) < 0)
        return false;
    ++m_pendingJoin->asked;
    m_claimWait->start(kClaimWaitMs);
    return true;
}

void ArcadeBackend::readPortsAnswer() {
    // The host's layout and its ports have both come: the game starts.
    const auto start = [this](const libretro::PortRange &ports) {
        m_claimWait->stop();
        qInfo("[Arcade] %s gives this machine's %d player(s) %d port(s) from %d, on the %s layout",
              qPrintable(m_pendingJoin->host), m_pendingJoin->players, ports.players, ports.first,
              m_pendingJoin->layout == libretro::Launch::Layout::Panel ? "panel" : "classic");
        startPendingJoin(ports);
    };
    while (m_claimAsk->hasPendingDatagrams()) {
        const QNetworkDatagram answer = m_claimAsk->receiveDatagram(64);
        const QHostAddress from(answer.senderAddress().toIPv4Address());
        const bool fromHost = m_pendingJoin && from == QHostAddress(m_pendingJoin->host);
        // The host's layout comes ahead of the ports (an older OSD/OS says
        // none: Classic), each a datagram of its own: ports that come first
        // wait a moment for it.
        if (const std::optional<libretro::Launch::Layout> layout = libretro::readClaimLayout(answer.data())) {
            if (!fromHost)
                continue;
            m_pendingJoin->layout = *layout;
            m_pendingJoin->layoutSaid = true;
            if (const std::optional<libretro::PortRange> waiting = m_pendingJoin->ports) {
                start(*waiting);
                return;
            }
            continue;
        }
        const std::optional<libretro::PortRange> ports = libretro::readClaimAnswer(answer.data());
        if (!ports)
            continue;
        // One that comes too late, or from another host: its ports given
        // back.
        if (!fromHost) {
            if (ports->first > 0)
                releaseClaim(from.toString());
            continue;
        }
        if (!m_pendingJoin->layoutSaid) {
            m_pendingJoin->ports = ports;
            m_claimWait->start(kLayoutWaitMs);
            continue;
        }
        start(*ports);
        return;
    }
}

void ArcadeBackend::startPendingJoin(const std::optional<libretro::PortRange> &ports) {
    const PendingJoin join = *std::exchange(m_pendingJoin, std::nullopt);
    if (!ports)
        qInfo("[Arcade] %s doesn't say which ports are taken: RetroArch is asked for them", qPrintable(join.host));
    const int first = ports ? ports->first : -1;
    if (playFbneo(join.game, libretro::Launch::Netplay::LanJoin, join.joinAddress, first, ports ? ports->players : 0,
                  ports ? join.layout : libretro::Launch::Layout::Classic)) {
        m_claimedFrom = first > 0 ? join.host : QString();
        return;
    }
    if (first > 0)
        releaseClaim(join.host);
    // Not started after all, said so as a game that ended: the last game's
    // output isn't this one's.
    m_launcher->clearOutput();
    emit finished(1, QStringLiteral("failed_to_start"), m_lastError);
}

void ArcadeBackend::releaseClaim(const QString &address) {
    QUdpSocket socket;
    socket.writeDatagram(libretro::claimRelease(), QHostAddress(address), m_claimPort);
}

bool ArcadeBackend::startClaims(int own, int most, libretro::Launch::Layout layout) {
    stopClaims();
    m_claims.reset(own, most);
    m_claimLayout = layout;
    m_claimSocket = new QUdpSocket(this);
    if (!m_claimSocket->bind(QHostAddress(QHostAddress::AnyIPv4), m_claimPort)) {
        qWarning("[Arcade] The machines joining can't be given their ports: %s", qPrintable(m_claimSocket->errorString()));
        stopClaims();
        return false;
    }
    connect(m_claimSocket, &QUdpSocket::readyRead, this, &ArcadeBackend::answerClaims);
    return true;
}

void ArcadeBackend::answerClaims() {
    while (m_claimSocket->hasPendingDatagrams()) {
        // From the local network alone, as the rooms' answers are taken.
        const QNetworkDatagram packet = m_claimSocket->receiveDatagram(64);
        if (!libretro::isLanSender(packet.senderAddress()))
            continue;
        const QString from = QHostAddress(packet.senderAddress().toIPv4Address()).toString();
        if (const std::optional<int> players = libretro::readClaimQuery(packet.data())) {
            // The layout first, the ports after: a joiner starts its game on
            // the ports, and takes the layout with them; an older one reads
            // the ports alone.
            const libretro::PortRange ports = m_claims.claim(from, *players);
            m_claimSocket->writeDatagram(libretro::claimLayout(m_claimLayout), packet.senderAddress(),
                                         packet.senderPort());
            m_claimSocket->writeDatagram(libretro::claimAnswer(ports), packet.senderAddress(), packet.senderPort());
            qInfo("[Arcade] %s joins with %d player(s): %d port(s) from %d", qPrintable(from), *players, ports.players,
                  ports.first);
        } else if (libretro::isClaimRelease(packet.data())) {
            m_claims.release(from);
        }
    }
}

void ArcadeBackend::stopClaims() {
    delete m_claimSocket;
    m_claimSocket = nullptr;
}

void ArcadeBackend::setLanDiscovery(const QString &address, quint16 port) {
    m_lanAddress = address;
    m_lanPort = port;
}

void ArcadeBackend::setLanListenTime(int ms) {
    m_lanListenMs = ms;
}

void ArcadeBackend::setLanClaimPort(quint16 port) {
    m_claimPort = port;
}

QVariantList ArcadeBackend::lanRooms() const {
    QVariantList list;
    if (!m_lanPlay || m_lanRooms.isEmpty() || !fbneoReady())
        return list;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const libretro::Room &room : m_lanRooms) {
        // As rooms(): a game in the folder FinalBurn Neo plays (only rooms
        // joined without a password are kept: readLanAnswers()).
        if (!fbneoHas(room.game))
            continue;
        // What its host answered with, as the lobby's rooms are told of
        // (FinalBurn Neo is ready, so ours are what is known of them), and
        // where it is joined: its host's address on this network, and the
        // game's file in the folder.
        QVariantMap info = roomInfo(room, now, true, m_coreVersion, m_retroarchVersion);
        info.insert(QStringLiteral("address"), room.joinAddress.section(QLatin1Char('|'), 0, 0));
        info.insert(QStringLiteral("file"), m_sets.value(room.game));
        list << QVariantMap{ { QStringLiteral("id"), room.id }, { QStringLiteral("set"), room.game },
                             { QStringLiteral("title"), titleOf(room.game) }, { QStringLiteral("host"), room.host },
                             { QStringLiteral("detail"), lanDetail(room) }, { QStringLiteral("info"), info } };
    }
    return list;
}

QVariantList ArcadeBackend::rooms() const {
    QVariantList list;
    if (!m_onlinePlay || !fbneoReady())
        return list;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const libretro::Room &room : m_rooms) {
        // A game in the folder FinalBurn Neo plays, joined without a
        // password (RetroArch would ask for one on its own screen); with
        // what the lobby says of it, as lobby() has it (FinalBurn Neo is
        // ready, so ours are what is known of them).
        if (room.password || !fbneoHas(room.game))
            continue;
        list << QVariantMap{ { QStringLiteral("id"), room.id }, { QStringLiteral("set"), room.game },
                             { QStringLiteral("title"), titleOf(room.game) },
                             { QStringLiteral("host"), room.host }, { QStringLiteral("country"), room.country },
                             { QStringLiteral("info"), roomInfo(room, now, true, m_coreVersion, m_retroarchVersion) } };
    }
    return list;
}

QVariantMap ArcadeBackend::roomInfo(const libretro::Room &room, const QDateTime &now, bool ready,
                                    const QString &yourCore, const QString &yourRetroArch) const {
    const bool fbneo = room.core == QLatin1String("FinalBurn Neo");
    const bool have = fbneo && !room.game.isEmpty() && fbneoHas(room.game);
    // A game of the folder by its name there; any other as its host has it.
    const QString title = have ? titleOf(room.game) : room.rom.isEmpty() ? QStringLiteral("?") : room.rom;
    // Why it can't be joined from here, as rooms() leaves it out.
    QString why;
    if (!fbneo)
        why = QStringLiteral("OSD/OS joins FinalBurn Neo's games alone: this one is %1's")
                  .arg(room.core.isEmpty() ? QStringLiteral("another core") : room.core);
    else if (!room.connectable)
        why = QStringLiteral("Its host's connection takes no players");
    else if (room.password)
        why = QStringLiteral("It asks for a password");
    else if (!room.joinable)
        why = room.game.isEmpty() ? QStringLiteral("Its game isn't named as a set")
              : room.joinAddress.isEmpty() ? QStringLiteral("The lobby gives no address it can be joined at")
                                           : QStringLiteral("Its host's RetroArch can't be joined from here");
    else if (!ready)
        why = QStringLiteral("FinalBurn Neo is not installed: Settings → Arcade → FinalBurn Neo");
    else if (!m_sets.contains(room.game))
        why = QStringLiteral("%1 isn't in your games folder").arg(room.game);
    else if (m_folders.contains(room.game))
        why = QStringLiteral("FinalBurn Neo plays a game's zip or 7z, not a folder");
    else if (!have)
        why = m_fbneoGames.contains(room.game)
                  ? looksForLowerCase(QStringLiteral("FinalBurn Neo"), room.game)
                  : QStringLiteral("FinalBurn Neo has no game called %1").arg(room.game);
    return { { QStringLiteral("id"), room.id }, { QStringLiteral("title"), title }, { QStringLiteral("rom"), room.rom },
             { QStringLiteral("set"), have ? room.game : QString() }, { QStringLiteral("crc"), room.crc },
             { QStringLiteral("core"), room.core }, { QStringLiteral("coreVersion"), room.coreVersion },
             { QStringLiteral("subsystem"), room.subsystem }, { QStringLiteral("retroarch"), room.retroarch },
             { QStringLiteral("frontend"), room.frontend }, { QStringLiteral("host"), room.host },
             { QStringLiteral("country"), room.country }, { QStringLiteral("relay"), room.relay },
             { QStringLiteral("players"), room.players }, { QStringLiteral("spectators"), room.spectators },
             { QStringLiteral("minutes"), room.created.isValid() ? std::max<qint64>(0, room.created.secsTo(now) / 60) : -1 },
             { QStringLiteral("password"), room.password }, { QStringLiteral("spectatePassword"), room.spectatePassword },
             { QStringLiteral("connectable"), room.connectable }, { QStringLiteral("joinable"), why.isEmpty() },
             { QStringLiteral("why"), why },
             { QStringLiteral("coreMatch"), !fbneo ? QString() : coreMatch(room.coreVersion, yourCore) },
             { QStringLiteral("retroarchMatch"), retroarchMatch(room.retroarch, yourRetroArch) } };
}

QVariantMap ArcadeBackend::lobby() const {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const bool ready = fbneoReady();
    // Ours, while its file is here, and whether it is read, still being
    // read, couldn't be, or isn't installed.
    const bool retroarchHere = !retroarchPath().isEmpty();
    const bool coreHere = !libretro::corePath(m_dataRoot).isEmpty();
    const QString yourRetroArch = retroarchHere ? m_retroarchVersion : QString();
    const QString yourCore = coreHere ? m_coreVersion : QString();
    const auto versionState = [](bool here, const QString &version, bool reading) {
        return !here ? QStringLiteral("missing") : !version.isEmpty() ? QStringLiteral("read")
               : reading ? QStringLiteral("reading") : QStringLiteral("failed");
    };
    struct Listed {
        bool fbneo;
        QString core;
        QString title;
        int id;
        QVariantMap entry;
    };
    QList<Listed> listed;
    for (const libretro::Room &room : m_lobby) {
        // What is said of it (roomInfo()), under its title: a game of the
        // folder by its name there; any other as its host has it.
        QVariantMap entry = roomInfo(room, now, ready, yourCore, yourRetroArch);
        listed.append({ room.core == QLatin1String("FinalBurn Neo"), room.core,
                        entry.value(QStringLiteral("title")).toString(), room.id, std::move(entry) });
    }
    // FinalBurn Neo's first, then by core and title, the newest first.
    std::sort(listed.begin(), listed.end(), [](const Listed &a, const Listed &b) {
        if (a.fbneo != b.fbneo)
            return a.fbneo;
        if (const int core = a.core.compare(b.core, Qt::CaseInsensitive))
            return core < 0;
        if (const int title = a.title.compare(b.title, Qt::CaseInsensitive))
            return title < 0;
        return a.id > b.id;
    });
    QVariantList rooms;
    for (const Listed &room : std::as_const(listed))
        rooms << room.entry;
    return { { QStringLiteral("state"), !m_roomsRead ? QStringLiteral("loading")
                                        : m_roomsProblem.isEmpty() ? QStringLiteral("ready") : QStringLiteral("failed") },
             { QStringLiteral("problem"), m_roomsProblem },
             { QStringLiteral("rooms"), rooms },
             { QStringLiteral("yourCore"), yourCore },
             { QStringLiteral("yourCoreState"), versionState(coreHere, yourCore, !m_coreInfoRead.isNull()) },
             { QStringLiteral("yourRetroArch"), yourRetroArch },
             { QStringLiteral("yourRetroArchState"), versionState(retroarchHere, yourRetroArch, !m_versionRead.isNull()) } };
}

void ArcadeBackend::refreshRooms() {
    // Ours too, which the rooms are matched against.
    if (m_onlinePlay)
        readOwnVersions();
    if (m_lobbyReply || !m_onlinePlay)
        return;
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    m_lobbyReply = m_nam->get(requestFor(m_lobbyUrl, kLobbyTimeoutMs));
    // A list of rooms is tens of kilobytes.
    limitReply(m_lobbyReply, kLobbyMaxBytes);
    connect(m_lobbyReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = std::exchange(m_lobbyReply, nullptr);
        reply->deleteLater();
        m_roomsRead = true;
        // What the Online Games folder and filter show of a room.
        const auto shown = [](const QList<libretro::Room> &rooms) {
            QStringList keys;
            for (const libretro::Room &room : rooms)
                keys << QString::number(room.id) + room.game + room.host + room.country + (room.password ? "p" : "");
            return keys;
        };
        const QStringList before = shown(m_rooms);
        const QString problemBefore = m_roomsProblem;
        const bool big = tooBig(reply, kLobbyMaxBytes);
        if (reply->error() == QNetworkReply::NoError && !big) {
            m_lobby = libretro::readWholeLobby(reply->readAll());
            m_rooms.clear();
            for (const libretro::Room &room : std::as_const(m_lobby)) {
                if (room.joinable)
                    m_rooms << room;
            }
            m_roomsProblem.clear();
        } else {
            m_rooms.clear();
            m_lobby.clear();
            m_roomsProblem = QStringLiteral("RetroArch's lobby can't be reached: ")
                             + (big ? QStringLiteral("its answer is too big") : reply->errorString());
            qWarning("[Arcade] %s", qPrintable(m_roomsProblem));
        }
        emit roomsChanged();
        // The Online Games folder, and the Arcade screen's filter: read again
        // only for rooms that came or went, not at every read of a lobby as
        // it was (one every twenty seconds while it is shown), which would
        // have the screen's filters made again each time.
        if (shown(m_rooms) != before || m_roomsProblem != problemBefore) {
            emit entriesReady(QStringLiteral("online"));
            emit gamesChanged();
        }
    });
}

void ArcadeBackend::downloadGenres() {
    if (m_genresReply || !m_genresUrl.isValid()
        || (m_genresRetry.isValid() && m_genresRetry > QDateTime::currentDateTimeUtc()))
        return;
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    qInfo("[Arcade] No catver.ini for the games' genres: downloading progettosnaps' into %s", qPrintable(m_home));
    m_genresReply = m_nam->get(requestFor(m_genresUrl, kGenresTimeoutMs));
    limitReply(m_genresReply, kGenresMaxBytes);
    connect(m_genresReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = std::exchange(m_genresReply, nullptr);
        reply->deleteLater();
        const bool big = tooBig(reply, kGenresMaxBytes);
        const QByteArray data = reply->error() == QNetworkReply::NoError && !big ? reply->readAll() : QByteArray();
        // catver.ini by its section of genres, else not taken.
        if (!data.contains("[Category]")) {
            qWarning("[Arcade] catver.ini couldn't be downloaded (%s): no genres for now",
                     qPrintable(big ? QStringLiteral("too big") : reply->errorString()));
            m_genresRetry = QDateTime::currentDateTimeUtc().addSecs(kGenresRetrySeconds);
            return;
        }
        // Megabytes, written to the card off the GUI thread (as the pictures
        // are), then read with the folder, now that it is there.
        const QString home = m_home;
        QtConcurrent::run(&m_pictureSaves, [home, data] {
            return QDir().mkpath(home) && writeFileAtomically(home + QStringLiteral("/catver.ini"), data);
        }).then(this, [this](bool written) {
            if (!written) {
                qWarning("[Arcade] catver.ini couldn't be written: no genres for now");
                m_genresRetry = QDateTime::currentDateTimeUtc().addSecs(kGenresRetrySeconds);
                return;
            }
            readAgain();
        });
    });
}

void ArcadeBackend::readOwnVersions() {
    // A file is done with once its read has ended, or been stopped before it
    // said its version twice (slow): asked again, a core loads faster from
    // the cache.
    const auto doneWith = [this](const QString &stamp, bool slow) {
        if (slow && !m_slowVersions.contains(stamp)) {
            m_slowVersions.insert(stamp);
            return false;
        }
        return true;
    };
    const QString program = retroarchPath();
    const QString programStamp = program.isEmpty() ? QString() : fileStamp(QFileInfo(program));
    if (programStamp != m_retroarchVersionOf && !m_versionRead) {
        if (program.isEmpty()) {
            m_retroarchVersion.clear();
            m_retroarchVersionOf.clear();
        } else if (const std::optional<QString> version = libretro::bundleVersion(program)) {
            // RetroArch.app opens as an app before it reads its arguments:
            // its version is its bundle's.
            m_retroarchVersion = *version;
            m_retroarchVersionOf = programStamp;
        } else {
            // The file before's version isn't this one's.
            m_retroarchVersion.clear();
            m_versionRead = askVersion(program, { QStringLiteral("--version") },
                                       [this, programStamp, doneWith](const QByteArray &output, bool timedOut) {
                m_versionRead = nullptr;
                m_retroarchVersion = libretro::readVersion(output);
                if (doneWith(programStamp, timedOut && m_retroarchVersion.isEmpty()))
                    m_retroarchVersionOf = programStamp;
            });
        }
    }
    // The core loaded by a copy of the app, not by this one: one that can't
    // be takes nothing down with it.
    const QString core = libretro::corePath(m_dataRoot);
    const QString coreStamp = core.isEmpty() ? QString() : fileStamp(QFileInfo(core));
    if (coreStamp != m_coreVersionOf && !m_coreInfoRead && !m_coreInfoCommand.isEmpty()) {
        if (core.isEmpty()) {
            m_coreVersion.clear();
            m_coreVersionOf.clear();
        } else {
            m_coreVersion.clear();
            m_coreInfoRead = askVersion(m_coreInfoCommand.first(), m_coreInfoCommand.mid(1) << core,
                                        [this, coreStamp, doneWith](const QByteArray &output, bool timedOut) {
                m_coreInfoRead = nullptr;
                m_coreVersion = libretro::readCoreInfo(output).version;
                if (doneWith(coreStamp, timedOut && m_coreVersion.isEmpty()))
                    m_coreVersionOf = coreStamp;
            });
        }
    }
}

QProcess *ArcadeBackend::askVersion(const QString &program, const QStringList &arguments,
                                    const std::function<void(const QByteArray &, bool)> &take) {
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    // Once, whichever way it ends.
    const auto done = [this, process, take] {
        if (process->property("done").toBool())
            return;
        process->setProperty("done", true);
        take(process->readAll(), process->property("timedOut").toBool());
        process->deleteLater();
        emit roomsChanged();
        // LAN Games' lines, which say it against each room's.
        showLanRooms();
    };
    connect(process, &QProcess::finished, this, done);
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done();
    });
    QTimer::singleShot(m_versionReadMs, process, [process] {
        process->setProperty("timedOut", true);
        process->kill();
    });
    process->start(program, arguments);
    return process;
}

// A room's row in the tree, of RetroArch's lobby or of the local network
// (lan), with what is said of it under its name.
static QVariantMap roomEntry(const QVariantMap &room, bool lan, const QString &detail) {
    const QString host = room.value(QStringLiteral("host")).toString();
    QVariantMap entry{ { QStringLiteral("name"), QStringLiteral("%1 (%2)").arg(room.value(QStringLiteral("title")).toString(), host) },
                       { QStringLiteral("path"), QStringLiteral("%1:%2").arg(lan ? QStringLiteral("lan") : QStringLiteral("room"))
                                                                        .arg(room.value(QStringLiteral("id")).toInt()) },
                       { QStringLiteral("isFolder"), false },
                       { QStringLiteral("kind"), QStringLiteral("room") },
                       { QStringLiteral("game"), room.value(QStringLiteral("set")) },
                       { QStringLiteral("room"), room.value(QStringLiteral("id")) },
                       { QStringLiteral("host"), host },
                       { QStringLiteral("detail"), detail } };
    if (lan)
        entry.insert(QStringLiteral("lan"), true);
    return entry;
}

QVariantList ArcadeBackend::roomEntries() const {
    QVariantList entries;
    for (const QVariant &value : rooms()) {
        const QVariantMap room = value.toMap();
        const QString country = room.value(QStringLiteral("country")).toString().toUpper();
        entries << roomEntry(room, false,
                             QStringLiteral("Hosted by %1%2 • %3")
                                 .arg(room.value(QStringLiteral("host")).toString(),
                                      country.isEmpty() ? QString() : QStringLiteral(" in ") + country,
                                      m_sets.value(room.value(QStringLiteral("set")).toString())));
    }
    if (entries.isEmpty()) {
        const bool failed = !m_roomsProblem.isEmpty();
        entries << QVariantMap{ { QStringLiteral("name"), failed ? QStringLiteral("Lobby Not Reachable") : QStringLiteral("No Games Online") },
                                { QStringLiteral("path"), QStringLiteral("online/none") },
                                { QStringLiteral("isFolder"), false },
                                { QStringLiteral("kind"), QStringLiteral("message") },
                                { QStringLiteral("detail"), failed ? m_roomsProblem
                                                            : QStringLiteral("No one in RetroArch's lobby hosts a game you have: "
                                                                             "host one from a game's options") } };
    }
    // The whole lobby, of every core (Lobby.qml).
    entries << QVariantMap{ { QStringLiteral("name"), QStringLiteral("RetroArch Lobby") },
                            { QStringLiteral("path"), QStringLiteral("lobby") },
                            { QStringLiteral("isFolder"), false },
                            { QStringLiteral("kind"), QStringLiteral("lobby") },
                            { QStringLiteral("detail"), QStringLiteral("Every game hosted in RetroArch's lobby, of every core, "
                                                                       "and what the lobby says of each") } };
    return entries;
}

void ArcadeBackend::refreshLanRooms() {
    if (!m_lanPlay)
        return;
    // Ours too, which the rooms are matched against.
    readOwnVersions();
    // A query out: its answers are still being listened for.
    if (m_lanListen->isActive())
        return;
    if (!m_lanSocket) {
        m_lanSocket = new QUdpSocket(this);
        connect(m_lanSocket, &QUdpSocket::readyRead, this, &ArcadeBackend::readLanAnswers);
    }
    m_lanFailure.clear();
    if (m_lanSocket->state() != QAbstractSocket::BoundState
        && !m_lanSocket->bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        m_lanFailure = m_lanSocket->errorString();
    } else {
        // Every host of the networks this machine is on, or a stand-in.
        const QList<QHostAddress> targets = m_lanAddress.isEmpty() ? broadcastAddresses()
                                                                   : QList<QHostAddress>{ QHostAddress(m_lanAddress) };
        bool sent = false;
        for (const QHostAddress &target : targets) {
            if (m_lanSocket->writeDatagram(libretro::lanQuery(), target, m_lanPort) >= 0)
                sent = true;
            else if (m_lanFailure.isEmpty())
                m_lanFailure = m_lanSocket->errorString();
        }
        if (sent)
            m_lanFailure.clear();
    }
    // The answers listened for; or, sent nowhere, why said the same way, once
    // this call is done with.
    m_lanListen->start(m_lanFailure.isEmpty() ? m_lanListenMs : 0);
}

void ArcadeBackend::readLanAnswers() {
    while (m_lanSocket->hasPendingDatagrams()) {
        // A byte more than an answer, so that a bigger packet is told apart.
        const QNetworkDatagram datagram = m_lanSocket->receiveDatagram(libretro::kLanAnswerSize + 1);
        // One that comes once the query's rooms have been made isn't taken,
        // nor one from outside the local network. Nothing proves where an
        // answer comes from, and RetroArch takes them as they come too: a room
        // only ever has RetroArch, once it is chosen, connect to the address
        // on this network the answer came from, at the port it gave.
        if (!m_lanListen->isActive() || !libretro::isLanSender(datagram.senderAddress()))
            continue;
        const QHostAddress from(datagram.senderAddress().toIPv4Address());
        std::optional<libretro::Room> room = libretro::readLanAnswer(datagram.data(), from.toString());
        // Only those that can be listed, so many at most: FinalBurn Neo's,
        // without a password (RetroArch would ask for it on its own screen).
        // A host once: the query reaches it by more than one broadcast
        // address, and it answers each.
        if (!room || !room->joinable || room->password || m_lanHeard.size() >= kMostLanRooms
            || hostedAt(m_lanHeard, room->joinAddress))
            continue;
        // Its id, the one it had while it was heard with the same game.
        const QString key = lanKey(*room);
        auto id = m_lanIds.constFind(key);
        if (id == m_lanIds.constEnd())
            id = m_lanIds.insert(key, m_lanNextId++);
        room->id = *id;
        m_lanHeard << *room;
    }
}

void ArcadeBackend::finishLanRead() {
    const QString problem = m_lanFailure.isEmpty() ? QString()
                            : QStringLiteral("The local network can't be reached: ") + m_lanFailure;
    if (!problem.isEmpty() && problem != m_lanProblem)
        qWarning("[Arcade] %s", qPrintable(problem));
    // A room of the last query not heard in this one kept through it, unless
    // its host answered with another: a broadcast is sent but once, and it,
    // or the answer, can be lost on the way. Not through a second, nor a
    // query that couldn't be sent.
    QSet<QString> missed;
    if (m_lanFailure.isEmpty()) {
        for (const libretro::Room &room : std::as_const(m_lanRooms)) {
            const QString key = lanKey(room);
            if (m_lanMissed.contains(key) || m_lanHeard.size() >= kMostLanRooms
                || hostedAt(m_lanHeard, room.joinAddress))
                continue;
            m_lanHeard << room;
            missed.insert(key);
        }
    }
    m_lanMissed = std::move(missed);
    // In the order they were first heard, whichever answered first now.
    std::sort(m_lanHeard.begin(), m_lanHeard.end(),
              [](const libretro::Room &a, const libretro::Room &b) { return a.id < b.id; });
    m_lanRooms = std::exchange(m_lanHeard, {});
    m_lanProblem = problem;
    m_lanRead = true;
    // The ids of rooms no longer heard forgotten, once they are many.
    if (m_lanIds.size() > 4 * kMostLanRooms) {
        QHash<QString, int> kept;
        for (const libretro::Room &room : std::as_const(m_lanRooms))
            kept.insert(lanKey(room), room.id);
        m_lanIds = std::move(kept);
    }
    emit lanRoomsChanged();
    showLanRooms();
}

void ArcadeBackend::showLanRooms() {
    if (!m_lanPlay || !m_lanRead)
        return;
    if (QVariantList shown = lanRoomEntries(); shown != m_lanShown) {
        m_lanShown = std::move(shown);
        emit entriesReady(QStringLiteral("lan"));
        emit gamesChanged();
    }
}

QVariantList ArcadeBackend::lanRoomEntries() const {
    QVariantList entries;
    for (const QVariant &value : lanRooms()) {
        const QVariantMap room = value.toMap();
        entries << roomEntry(room, true, room.value(QStringLiteral("detail")).toString());
    }
    if (entries.isEmpty()) {
        const QVariantMap status = lanStatus();
        entries << QVariantMap{ { QStringLiteral("name"), status.value(QStringLiteral("name")) },
                                { QStringLiteral("path"), QStringLiteral("lan/none") },
                                { QStringLiteral("isFolder"), false },
                                { QStringLiteral("kind"), QStringLiteral("message") },
                                { QStringLiteral("detail"), status.value(QStringLiteral("detail")) } };
    }
    return entries;
}

QVariantMap ArcadeBackend::lanStatus() const {
    const auto said = [](const QString &name, const QString &detail) {
        return QVariantMap{ { QStringLiteral("name"), name }, { QStringLiteral("detail"), detail } };
    };
    // FinalBurn Neo first: without it no game hosted there is one to join.
    if (!fbneoReady())
        return said(QStringLiteral("FinalBurn Neo Not Installed"),
                    QStringLiteral("LAN games play in FinalBurn Neo: Settings → Arcade → FinalBurn Neo"));
    if (!m_lanRead)
        return said(QStringLiteral("Asking the Local Network…"),
                    QStringLiteral("A game hosted on this network comes here as soon as it is found"));
    if (!m_lanProblem.isEmpty())
        return said(QStringLiteral("Network Not Reachable"), m_lanProblem);
    return said(QStringLiteral("No LAN Games"),
                QStringLiteral("No one on this network hosts a game you have: host one from a game's options"));
}

QString ArcadeBackend::nickname(bool lan) const {
    if (!m_onlineName.isEmpty())
        return m_onlineName;
    const QString device = lan ? device::name() : QString();
    if (!device.isEmpty())
        return device;
    return m_raUser.isEmpty() ? QStringLiteral("OSD-OS") : m_raUser;
}

QString ArcadeBackend::lanDetail(const libretro::Room &room) const {
    // Who hosts it, and where: two cabinets of one name told apart.
    QStringList parts{ QStringLiteral("Hosted by %1 at %2 on this network")
                           .arg(room.host.isEmpty() ? QStringLiteral("someone") : room.host,
                                room.joinAddress.section(QLatin1Char('|'), 0, 0)),
                       m_sets.value(room.game) };
    // Its core and RetroArch against ours, as the lobby's rooms are matched.
    const QString core = coreMatch(room.coreVersion, m_coreVersion);
    const QString retroarch = retroarchMatch(room.retroarch, m_retroarchVersion);
    if (core == QLatin1String("other"))
        parts << QStringLiteral("Its FinalBurn Neo is %1, yours %2").arg(room.coreVersion, m_coreVersion);
    if (retroarch == QLatin1String("other"))
        parts << QStringLiteral("Its RetroArch is %1, yours %2").arg(room.retroarch, m_retroarchVersion);
    if (core == QLatin1String("same") && retroarch == QLatin1String("same"))
        parts << QStringLiteral("The same FinalBurn Neo and RetroArch as yours");
    return parts.join(QStringLiteral(" • "));
}

void ArcadeBackend::watchNetplay() {
    // RetroArch that can't host or join (the host not reached, the handshake
    // refused, its port taken) says so once on its screen and plays the game
    // alone, as if nothing had been asked; its log says so too, a line at a
    // time as it goes. The log is read as it grows while the game starts,
    // and the game stopped at the first such line: the end then says why
    // (fbneoNotice()).
    if (m_playClock.elapsed() > kNetplayWatchFor || !m_launcher->isBusy()) {
        m_netplayWatch->stop();
        return;
    }
    QFile log(retroarchHome() + QStringLiteral("/last.log"));
    if (!log.open(QIODevice::ReadOnly) || log.size() <= m_netplayLogRead || !log.seek(m_netplayLogRead))
        return;
    m_netplayLogTail += log.readAll();
    m_netplayLogRead = log.pos();
    // Whole lines: the last may be half written.
    const qsizetype end = m_netplayLogTail.lastIndexOf('\n');
    if (end < 0)
        return;
    const QList<QByteArray> lines = m_netplayLogTail.left(end).split('\n');
    m_netplayLogTail.remove(0, end + 1);
    const bool hosting = libretro::hosts(m_playingNetplay);
    for (const QByteArray &line : lines) {
        const std::optional<QString> error = libretro::netplayError(QString::fromUtf8(line));
        if (!error)
            continue;
        m_netplayErrors << *error;
        const QString failure = libretro::netplayFailure(m_netplayErrors, hosting);
        if (failure.isEmpty())
            continue;
        qWarning("[Arcade] %s: RetroArch plays on alone, \"%s\": stopped", qPrintable(m_playing), qPrintable(failure));
        m_netplayWatch->stop();
        m_launcher->requestStop();
        return;
    }
}

QString ArcadeBackend::fbneoNotice(int exitCode, const QString &reason, const libretro::Run &run) const {
    if (reason == QLatin1String("ok"))
        return QString();
    if (reason == QLatin1String("failed_to_start"))
        return QStringLiteral("RetroArch did not start");
    // Over the network, the game RetroArch couldn't set up and went on with
    // alone: why, in its words.
    if (m_playingNetplay != libretro::Launch::Netplay::None && !run.netplayFailure.isEmpty()) {
        QString notice = QStringLiteral("Could not %1 the game: %2")
                             .arg(libretro::hosts(m_playingNetplay) ? QStringLiteral("host") : QStringLiteral("join"),
                                  run.netplayFailure);
        if (run.netplayFailure.startsWith(QLatin1String("Failed to connect to host")))
            notice += QStringLiteral("\nThe host didn't answer: its game may have ended, or a firewall there keeps the port closed");
        return notice;
    }
    if (reason == QLatin1String("stopped"))
        return QStringLiteral("The game was stopped");
    if (m_launcher->lastRunCrashed())
        return QStringLiteral("RetroArch closed unexpectedly (signal %1)").arg(exitCode);
    if (!run.missing.isEmpty()) {
        QString files = run.missing.mid(0, 3).join(QStringLiteral(", "));
        if (run.missing.size() > 3)
            files += QStringLiteral(" and %1 more").arg(run.missing.size() - 3);
        // No other engine to play it in: one MAME doesn't know, one played
        // online, or with no MAME.
        if (fbneoOnly(m_playing) || m_playingNetplay != libretro::Launch::Netplay::None || mamePath().isEmpty())
            return QStringLiteral("Missing %1: the set may be for another version of FinalBurn Neo\n"
                                  "Or a BIOS set it needs isn't in the games folder").arg(files);
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
             { QStringLiteral("canDownload"), unpacks && !FbneoInstaller::defaultSource().core.isEmpty() },
             { QStringLiteral("canDownloadRetroarch"), ProgramInstaller::retroarch(m_dataRoot).available },
             { QStringLiteral("retroarchDownloadVersion"), ProgramInstaller::retroarch(m_dataRoot).version },
             { QStringLiteral("retroarchDownloaded"), !retroarchPath().isEmpty()
                                                      && retroarchPath() == libretro::downloadedRetroArch(m_dataRoot) } };
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

void ArcadeBackend::downloadRetroArch() {
    if (m_retroarchInstaller->running())
        return;
    m_retroarchMessage.clear();
    m_retroarchInstaller->start(ProgramInstaller::retroarch(m_dataRoot));
    emit fbneoChanged();
}

bool ArcadeBackend::retroarchDownloading() const {
    return m_retroarchInstaller->running();
}

int ArcadeBackend::retroarchProgress() const {
    return m_retroarchInstaller->progress();
}

QVariantMap ArcadeBackend::mameInfo() const {
    const QString program = mamePath();
    const QString plugins = pluginsPath();
    return { { QStringLiteral("mame"), program },
             { QStringLiteral("downloaded"), !program.isEmpty() && program == mame::downloadedMame(m_dataRoot) },
             { QStringLiteral("canDownload"), ProgramInstaller::mame(m_dataRoot).available },
             { QStringLiteral("downloadVersion"), ProgramInstaller::mame(m_dataRoot).version },
             { QStringLiteral("plugins"), !plugins.isEmpty() },
             { QStringLiteral("pluginsFolder"), plugins } };
}

void ArcadeBackend::downloadMame() {
    // Not beside a game, which may be playing in the MAME to be replaced.
    if (m_mameInstaller->running() || m_launcher->isBusy())
        return;
    m_mameMessage.clear();
    const ProgramInstaller::Source source = ProgramInstaller::mame(m_dataRoot);
    // Nor beside a batch MAME is asked about, which would be running from
    // the folder the download puts its MAME in: stopped, as for a game
    // starting, and asked again should the download fail. A download that
    // succeeds reads every set anew (the installer's finished()).
    if (source.available)
        pauseScan();
    m_mameInstaller->start(source);
    emit mameChanged();
}

bool ArcadeBackend::mameDownloading() const {
    return m_mameInstaller->running();
}

int ArcadeBackend::mameProgress() const {
    return m_mameInstaller->progress();
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
    if (m_signIn)
        return QStringLiteral("checking");
    if (!m_raPassword.isEmpty())
        return QStringLiteral("pending");
    return QStringLiteral("failed");
}

void ArcadeBackend::signInAchievements(const QString &user, const QString &password) {
    if (user.trimmed().isEmpty() || password.isEmpty())
        return;
    // In place of an account OSD/OS Setup brought.
    forgetSetupAccount();
    beginSignIn(user, password);
}

void ArcadeBackend::takeSetupAccount() {
    QFile file(setupAccountPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject setup = QJsonDocument::fromJson(file.read(64 * 1024)).object();
    file.close();
    const QString key = setup.value(QStringLiteral("key")).toString().trimmed();
    const QString user = setup.value(QStringLiteral("user")).toString().trimmed();
    const QString password = setup.value(QStringLiteral("password")).toString();
    if (!key.isEmpty() && key != m_raKey)
        setAchievementsKey(key);
    // Without a password, or for the account signed in already: its key
    // alone (a file brought again keeps the token that works).
    if (user.isEmpty() || password.isEmpty()
        || (!m_raToken.isEmpty() && user.compare(m_raUser, Qt::CaseInsensitive) == 0)) {
        forgetSetupAccount();
        return;
    }
    qInfo("[Arcade] RetroAchievements account from OSD/OS Setup: %s", qPrintable(user));
    m_raFromSetup = true;
    m_setupChecks = 1;
    beginSignIn(user, password);
    // No check started at all (no RetroArch, or its sign-in core): none ever
    // will be, and the password waits in memory alone, for the next game.
    if (!m_signIn)
        forgetSetupAccount();
}

void ArcadeBackend::forgetSetupAccount() {
    m_raFromSetup = false;
    m_setupChecks = 0;
    QFile::remove(setupAccountPath());
}

void ArcadeBackend::beginSignIn(const QString &user, const QString &password) {
    const QString name = user.trimmed();
    stopSignIn();
    m_raUser = name;
    m_raToken.clear();
    m_raPassword = password;
    m_raFailure.clear();
    m_retroAchievements->setUser(m_raUser);
    saveAchievements();
    checkSignIn();
    emit achievementsChanged();
}

void ArcadeBackend::signOutAchievements() {
    stopSignIn();
    forgetSetupAccount();
    m_raUser.clear();
    m_raToken.clear();
    m_raPassword.clear();
    m_raFailure.clear();
    m_retroAchievements->setUser(QString());
    saveAchievements();
    emit achievementsChanged();
}

void ArcadeBackend::checkSignIn() {
    const QString program = retroarchPath();
    if (program.isEmpty() || !QFileInfo(m_signInCore).isFile())
        return;
    const QString dir = signInDir();
    if (!QDir().mkpath(dir))
        return;
    // What it keeps there, the password in its config, is no one else's.
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    libretro::SignIn signIn;
    signIn.core = m_signInCore;
    signIn.content = dir + QStringLiteral("/sign-in.osdos");
    signIn.config = dir + QStringLiteral("/retroarch.cfg");
    signIn.dir = dir;
    signIn.user = m_raUser;
    signIn.password = m_raPassword;
    if (!writeFileAtomically(signIn.content, QByteArrayLiteral("OSD/OS"), kOwnerOnly)
        || !writeFileAtomically(signIn.config, libretro::signInConfigText(signIn).toUtf8(), kOwnerOnly))
        return;

    m_signInResult = libretro::SignInLine::None;
    m_signInFailure.clear();
    m_signIn = new QProcess(this);
    m_signIn->setProgram(program);
    m_signIn->setArguments(libretro::signInArguments(signIn));
    m_signIn->setWorkingDirectory(dir);
    // Its log, on its output and its errors alike; and its own folder kept
    // with the rest (XDG_CONFIG_HOME), not the user's RetroArch's.
    m_signIn->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_CONFIG_HOME"), dir + QStringLiteral("/config"));
    m_signIn->setProcessEnvironment(environment);
    connect(m_signIn, &QProcess::readyReadStandardOutput, this, &ArcadeBackend::readSignIn);
    connect(m_signIn, &QProcess::finished, this, &ArcadeBackend::signInEnded);
    connect(m_signIn, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            signInEnded();
    });
    // Not said how it went in time: stopped, and what it saved read all the
    // same; and killed if even that doesn't end it. Once it has said, it is
    // being stopped already: a second SIGTERM would end it unsaved.
    QProcess *process = m_signIn;
    QTimer::singleShot(kSignInTimeoutMs, process, [this, process] {
        if (m_signInResult != libretro::SignInLine::None)
            return;
        process->terminate();
        QTimer::singleShot(kSignInStopMs, process, &QProcess::kill);
    });
    qInfo("[Arcade] RetroArch signs in as %s to check it", qPrintable(m_raUser));
    m_signIn->start();
}

void ArcadeBackend::readSignIn() {
    while (m_signIn && m_signIn->canReadLine()) {
        const QString line = QString::fromUtf8(m_signIn->readLine());
        if (m_signInResult != libretro::SignInLine::None)
            continue;
        m_signInResult = libretro::readSignInLine(line.trimmed(), &m_signInFailure);
        // Said: RetroArch ends, saving the token if it has one.
        if (m_signInResult != libretro::SignInLine::None) {
            m_signIn->terminate();
            QTimer::singleShot(kSignInStopMs, m_signIn, &QProcess::kill);
        }
    }
}

void ArcadeBackend::signInEnded() {
    if (!m_signIn)
        return;
    // What it said last, not read yet.
    readSignIn();
    const QString token = libretro::savedToken(signInDir() + QStringLiteral("/retroarch.cfg"));
    // The password went in its folder, the token it was swapped for too.
    stopSignIn();
    if (!token.isEmpty()) {
        qInfo("[Arcade] RetroArch signed in as %s", qPrintable(m_raUser));
        m_raToken = token;
        m_raPassword.clear();
        m_raFailure.clear();
        forgetSetupAccount();
    } else if (m_signInResult == libretro::SignInLine::Failed) {
        qInfo("[Arcade] RetroAchievements refused %s: %s", qPrintable(m_raUser), qPrintable(m_signInFailure));
        m_raPassword.clear();
        m_raFailure = m_signInFailure;
        forgetSetupAccount();
    } else {
        // The password waits for the next game; why it does: RetroAchievements
        // not reached, a sign-in RetroArch kept no token of, or nothing said
        // (in time, or at all: RetroArch couldn't start or load the core).
        qInfo("[Arcade] RetroArch couldn't sign in as %s now: at the next game", qPrintable(m_raUser));
        m_raFailure = (m_signInResult == libretro::SignInLine::Unreachable
                           ? (m_signInFailure.isEmpty() ? QStringLiteral("RetroAchievements couldn't be reached")
                                                        : m_signInFailure)
                           : m_signInResult == libretro::SignInLine::SignedIn
                               ? QStringLiteral("RetroArch signed in but kept no token")
                               : QStringLiteral("The sign-in couldn't be checked now"))
                      + QStringLiteral(": RetroArch signs in at the next game");
        // OSD/OS Setup's account, checked as OSD/OS started: again a while
        // later, the network up by then; once RetroAchievements has taken
        // the password, the file goes, and the password waits for the game.
        if (m_signInResult == libretro::SignInLine::SignedIn) {
            forgetSetupAccount();
        } else if (m_raFromSetup && m_setupChecks < kSetupChecks) {
            ++m_setupChecks;
            QTimer::singleShot(m_setupRetryMs, this, &ArcadeBackend::retrySetupSignIn);
        }
    }
    saveAchievements();
    emit achievementsChanged();
}

void ArcadeBackend::retrySetupSignIn() {
    // Done with meanwhile: signed in or out in Settings, or a game had the
    // password.
    if (!m_raFromSetup || m_signIn || !m_raToken.isEmpty() || m_raPassword.isEmpty() || m_raPasswordGiven)
        return;
    // Never a RetroArch beside a game's: once it is over.
    if (!m_playing.isEmpty()) {
        QTimer::singleShot(m_setupRetryMs, this, &ArcadeBackend::retrySetupSignIn);
        return;
    }
    checkSignIn();
    emit achievementsChanged();
}

bool ArcadeBackend::stopSignIn() {
    QProcess *process = std::exchange(m_signIn, nullptr);
    if (!process)
        return false;
    process->disconnect(this);
    if (process->state() == QProcess::NotRunning) {
        process->deleteLater();
    } else {
        // Gone once it has ended, or once it couldn't start after all.
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
        process->kill();
    }
    QDir(signInDir()).removeRecursively();
    return true;
}

void ArcadeBackend::setAchievementsKey(const QString &key) {
    m_raKey = key.trimmed();
    m_raKeyRefused = false;
    m_retroAchievements->setKey(m_raKey);
    m_retroAchievements->checkKey();
    saveAchievements();
    emit achievementsChanged();
    // The RetroAchievements filter comes or goes with it.
    emit gamesChanged();
}

void ArcadeBackend::checkAchievementsKey() {
    if (!m_raKeyRefused)
        m_retroAchievements->checkKey();
}

bool ArcadeBackend::achievementsKeyWorks() const {
    return m_retroAchievements->keyWorks();
}

QVariantMap ArcadeBackend::leaderboard(const QString &set) {
    const QString file = m_sets.value(set);
    if (!m_achievements || m_raKey.isEmpty() || file.isEmpty() || engineOf(set) != QLatin1String("fbneo"))
        return { { QStringLiteral("state"), QStringLiteral("off") } };
    return m_retroAchievements->board(QFileInfo(file).fileName());
}

void ArcadeBackend::setAchievementsServers(const QUrl &api, const QUrl &media) {
    m_retroAchievements->setServers(api, media);
}

QVariantMap ArcadeBackend::gameAchievements(const QString &set) {
    const QString file = m_sets.value(set);
    if (file.isEmpty() || isUnrecognized(set))
        return { { QStringLiteral("state"), QStringLiteral("none") } };
    if (m_raKey.isEmpty())
        return { { QStringLiteral("state"), QStringLiteral("nokey") } };
    if (m_raKeyRefused)
        return { { QStringLiteral("state"), QStringLiteral("refused") } };
    return m_retroAchievements->achievements(QFileInfo(file).fileName());
}

QVariantMap ArcadeBackend::achievementCount(const QString &set) {
    const QVariantMap found = gameAchievements(set);
    const QVariantList achievements = found.value(QStringLiteral("achievements")).toList();
    int earned = 0;
    for (const QVariant &achievement : achievements) {
        if (!achievement.toMap().value(QStringLiteral("earned")).toString().isEmpty())
            ++earned;
    }
    return { { QStringLiteral("state"), found.value(QStringLiteral("state")) },
             { QStringLiteral("earned"), earned },
             { QStringLiteral("total"), int(achievements.size()) },
             { QStringLiteral("progress"), found.value(QStringLiteral("progress")).toBool() } };
}

QString ArcadeBackend::achievementBadge(const QString &set, int id, const QString &name, bool earned) {
    if (id <= 0 || !arcadehttp::isBadgeName(name) || !m_sets.contains(set) || isUnrecognized(set))
        return QString();
    const QString locked = earned ? QString() : QStringLiteral("_lock");
    const QString file = QStringLiteral("achieve_%1_%2%3.png").arg(set).arg(id).arg(locked);
    const QString snap = snapFolder();
    const QString kept = m_home + QStringLiteral("/badges");
    for (const QString &folder : { snap, kept }) {
        if (!folder.isEmpty() && QFile::exists(folder + QLatin1Char('/') + file))
            return QUrl::fromLocalFile(folder + QLatin1Char('/') + file).toString();
    }
    // One download of each at a time; one that couldn't be, a minute later.
    const auto retry = m_badgesRetry.constFind(file);
    if (m_badgesWanted.contains(file)
        || (retry != m_badgesRetry.constEnd() && *retry > QDateTime::currentDateTimeUtc()))
        return QString();
    m_badgesWanted.insert(file);
    const QString folder = snapWritable() ? snap : kept;
    m_retroAchievements->media(QStringLiteral("/Badge/%1%2.png").arg(name, locked),
                               [this, file, folder](const QByteArray &data) {
        const auto saved = [this, file](bool written) {
            m_badgesWanted.remove(file);
            if (!written) {
                m_badgesRetry.insert(file, QDateTime::currentDateTimeUtc().addSecs(60));
                return;
            }
            m_badgesRetry.remove(file);
            emit badgesReady();
        };
        if (data.isEmpty()) {
            saved(false);
            return;
        }
        // Written where the pictures are, off the GUI thread.
        const QString path = folder + QLatin1Char('/') + file;
        QtConcurrent::run(&m_pictureSaves, [folder, path, data] {
            return QDir().mkpath(folder) && writeFileAtomically(path, data);
        }).then(this, saved);
    });
    return QString();
}

QStringList ArcadeBackend::statusIcons() const {
    QStringList icons;
    if (m_highScores)
        icons << QStringLiteral("hiscore");
    const QString account = achievementsState();
    if (m_achievements && (account == QLatin1String("signedIn") || account == QLatin1String("pending")
                           || account == QLatin1String("checking")))
        icons << QStringLiteral("trophy");
    if (m_onlinePlay)
        icons << QStringLiteral("globe");
    if (m_lanPlay)
        icons << QStringLiteral("network");
    return icons;
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
    // A RetroArch that never ran (it couldn't be started, or left no log)
    // saw neither the password nor the token: nothing became of them, and
    // the password stays for the next game, no game's any more (OSD/OS
    // Setup's sign-in may try it meanwhile, retrySetupSignIn()).
    if (!run.logged) {
        m_raPasswordGiven = false;
        QFile::remove(config);
        return;
    }
    bool changed = false;
    if (std::exchange(m_raPasswordGiven, false)) {
        // RetroArch saved its config on exit, the token in it if it signed
        // in; the password goes whatever became of it, and OSD/OS Setup's
        // account with it.
        const QString token = libretro::savedToken(config);
        m_raPassword.clear();
        forgetSetupAccount();
        if (!token.isEmpty()) {
            m_raToken = token;
            m_raFailure.clear();
        } else {
            m_raFailure = run.loginFailure.isEmpty() ? QStringLiteral("RetroArch did not sign in: sign in again")
                                                     : run.loginFailure;
        }
        saveAchievements();
        changed = true;
    } else if (!m_raToken.isEmpty() && libretro::loginRefused(run.loginFailure)) {
        // The token no longer taken (the password changed since, or it
        // expired): signed out.
        m_raToken.clear();
        m_raFailure = run.loginFailure;
        saveAchievements();
        changed = true;
    } else if (!run.loginFailure.isEmpty()) {
        // RetroAchievements not reached this time: still signed in.
        qWarning("[Arcade] RetroArch did not sign in to RetroAchievements: %s", qPrintable(run.loginFailure));
    }
    // A game's config is written anew for each game; the one it ended with
    // holds the token, or the password RetroArch was given: not left behind.
    QFile::remove(config);
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
