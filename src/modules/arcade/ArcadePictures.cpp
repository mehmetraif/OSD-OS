#include "ArcadePictures.h"
#include "../../util/AtomicFile.h"
#include "../../util/DurableFile.h"
#include "ArcadeDb.h"
#include "Http.h"
#include "RetroAchievements.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <limits>
#include <utility>

namespace {

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

// The picture of that name in a folder (base: its path without its ending),
// a .png or a .jpg, in either case; "" for none.
QString pictureAt(const QString &base) {
    for (const char *ending : { ".png", ".jpg", ".PNG", ".JPG" }) {
        if (QFile::exists(base + QLatin1String(ending)))
            return base + QLatin1String(ending);
    }
    return QString();
}

// A picture saved in folder, as a .png or a .jpg as data is: numbered, as
// <name>_<n>, n the first number no picture of that name has there, nor a
// set of the folder's (sets) as its name, never over a file that is there
// (the pictures are saved one at a time); else as <name>, the way the
// picture packs for MAME name them, unless a picture of that name is there
// already, the user's own or one saved before, which is kept as it is and is
// the answer. Whole or not at all (writeFileAtomically(): written beside its
// name, and given it once it is on the card): written in place, a power cut
// on the way left a picture cut short under the name, which the next read
// of the folder took for one of the game's, or, named, kept as the game's
// for good. Its folder's entry flushed to the card too. Its path, "" when
// it couldn't be saved.
QString savePicture(const QString &folder, const QString &name, const QByteArray &data, bool numbered,
                    const QHash<QString, QString> &sets) {
    if (!QDir().mkpath(folder))
        return QString();
    QString base;
    if (numbered) {
        for (int n = 1; n <= 999 && base.isEmpty(); ++n) {
            const QString stem = QStringLiteral("%1_%2").arg(name).arg(n);
            if (!sets.contains(stem) && pictureAt(folder + QLatin1Char('/') + stem).isEmpty())
                base = folder + QLatin1Char('/') + stem;
        }
        if (base.isEmpty())
            return QString();
    } else {
        base = folder + QLatin1Char('/') + name;
        if (const QString there = pictureAt(base); !there.isEmpty())
            return there;
    }
    const QString path = base + (data.startsWith("\x89PNG") ? QStringLiteral(".png") : QStringLiteral(".jpg"));
    if (!writeFileAtomically(path, data)) {
        qWarning("[Arcade] A picture could not be saved in %s", qPrintable(folder));
        return QString();
    }
    syncPath(folder, true);
    return path;
}

// A picture of a folder of them by set (its name in lower case: the games
// folder's snap or titles folder), kept in pictures should it fit the set's
// name better than the one kept before (ranks): the exact name first (as
// MAME's own folder has it), then a .png. False for a file that is neither
// a .png nor a .jpg; set, the one it is named by.
bool rankPicture(const QFileInfo &info, QHash<QString, int> &ranks, QHash<QString, QString> &pictures,
                 QString *set = nullptr) {
    const QString suffix = info.suffix().toLower();
    if (suffix != QLatin1String("png") && suffix != QLatin1String("jpg"))
        return false;
    const QString name = info.completeBaseName().toLower();
    const int rank = (info.fileName() == name + QLatin1Char('.') + suffix ? 2 : 0)
                     + (suffix == QLatin1String("png") ? 1 : 0);
    if (rank > ranks.value(name, -1)) {
        ranks.insert(name, rank);
        pictures.insert(name, info.filePath());
    }
    if (set)
        *set = name;
    return true;
}

} // namespace

ArcadePictures::ArcadePictures(const QString &home, Catalog *catalog, RetroAchievements *retroAchievements,
                               ArcadeDb *arcadeDb, QThreadPool *saves, QObject *parent)
    : QObject(parent), m_home(home), m_catalog(catalog), m_retroAchievements(retroAchievements),
      m_arcadeDb(arcadeDb), m_pictureSaves(saves)
{
    connect(m_retroAchievements, &RetroAchievements::picturesFound, this, &ArcadePictures::onPicturesFound);
    m_sweepTimer = new QTimer(this);
    m_sweepTimer->setTimerType(Qt::PreciseTimer);
    m_sweepTimer->setInterval(kSweepStepMs);
    connect(m_sweepTimer, &QTimer::timeout, this, &ArcadePictures::sweepPictures);
    // After a look that couldn't have every picture: another, ten minutes on
    // (or once the last game's retry is due, finishPictureSweep()).
    m_sweepAgain = new QTimer(this);
    m_sweepAgain->setSingleShot(true);
    connect(m_sweepAgain, &QTimer::timeout, this, [this] {
        if (m_catalog->reading() || m_catalog->scanning())
            m_sweepWanted = true;
        else
            startPictureSweep();
    });
    // A look cut short (the power went, the app was closed) goes on at the
    // next start, once the games have been read.
    m_sweepWanted = QFile::exists(sweepMarker());
    // progetto-SNAPS' pictures, through ArcadeDB: the game shown asked about
    // a moment after it was shown.
    connect(m_arcadeDb, &ArcadeDb::found, this, &ArcadePictures::onSnapsFound);
    connect(m_arcadeDb, &ArcadeDb::failed, this, &ArcadePictures::onSnapsFailed);
    m_snapAsk = new QTimer(this);
    m_snapAsk->setSingleShot(true);
    m_snapAsk->setInterval(kSnapAskDelayMs);
    connect(m_snapAsk, &QTimer::timeout, this, &ArcadePictures::askSnaps);
    // The pictures' records, written a moment after they change.
    m_recordSave = new QTimer(this);
    m_recordSave->setSingleShot(true);
    m_recordSave->setInterval(kRecordSaveDelayMs);
    connect(m_recordSave, &QTimer::timeout, this, &ArcadePictures::flushRecords);
    // RetroAchievements' list of games read again: the games it had none of
    // may have some now.
    connect(m_retroAchievements, &RetroAchievements::listRead, this, [this] { m_noPictures.clear(); });
}

ArcadePictures::Found ArcadePictures::read(const QString &snapFolder, const QString &titlesFolder,
                                           const QString &shotsFolder, const QHash<QString, QString> &sets) {
    Found read;
    // The pictures, looked for here once rather than file by file as the
    // games are chosen (exFAT's folders have no index): the games folder's
    // snap, named by the set as MAME names it, the way the picture packs for
    // MAME have them, a .png before a .jpg, and those numbered after a set
    // there (sf2_1.png, sf2_2.jpg: RetroAchievements' and the user's own;
    // not a set's own snap, sf2_1.png while there is a set sf2_1); and the
    // first MAME took of each game, in its own snap folder.
    static const QRegularExpression numbered(QRegularExpression::anchoredPattern(QStringLiteral("(.+)_(\\d{1,3})")));
    QHash<QString, int> ranks;   // set → how well its picture's name fits
    QDirIterator snaps(snapFolder, QDir::Files);
    while (snaps.hasNext()) {
        snaps.next();
        const QFileInfo info = snaps.fileInfo();
        // An achievement's badge (badge()), no game's picture.
        if (info.fileName().startsWith(QLatin1String("achieve_"), Qt::CaseInsensitive))
            continue;
        QString set;
        if (!rankPicture(info, ranks, read.snaps, &set))
            continue;
        const QRegularExpressionMatch match = numbered.match(set);
        if (match.hasMatch() && !sets.contains(set))
            read.numbered[match.captured(1)] << info.filePath();
    }
    for (QStringList &pictures : read.numbered)
        std::sort(pictures.begin(), pictures.end(), beforeInTurn);
    // The title screens, in the games folder's titles, named by set as the
    // Titles pack for MAME has them: the exact name first, then a .png.
    QHash<QString, int> titleRanks;
    QDirIterator titles(titlesFolder, QDir::Files);
    while (titles.hasNext()) {
        titles.next();
        rankPicture(titles.fileInfo(), titleRanks, read.titles);
    }
    QDirIterator shots(shotsFolder, QDir::Dirs | QDir::NoDotAndDotDot);
    while (shots.hasNext()) {
        shots.next();
        const QString shot = firstShot(shots.filePath());
        if (!shot.isEmpty())
            read.shots.insert(shots.fileName(), shot);
    }
    return read;
}

void ArcadePictures::takeRead(Found &&found) {
    m_snaps = std::move(found.snaps);
    m_numbered = std::move(found.numbered);
    m_titles = std::move(found.titles);
    m_shots = std::move(found.shots);
    // Pictures saved while the folder was read, which it may not have seen.
    for (const Saved &saved : std::as_const(m_savedDuringRead))
        place(saved);
    m_savedDuringRead.clear();
}

void ArcadePictures::place(const Saved &saved) {
    if (saved.kind == Kind::Numbered) {
        addNumbered(m_numbered, saved.set, saved.file);
        return;
    }
    // A snap or a title screen the set has, the user's own, is kept.
    QHash<QString, QString> &pictures = saved.kind == Kind::Title ? m_titles : m_snaps;
    if (!pictures.contains(saved.set))
        pictures.insert(saved.set, saved.file);
}

void ArcadePictures::save(Kind kind, const QString &set, const QString &folder, const QString &snapFolderThen,
                          const QByteArray &data, const std::function<void(const QString &file)> &done) {
    const bool numbered = kind == Kind::Numbered;
    QtConcurrent::run(m_pictureSaves, savePicture, folder, set, data, numbered,
                      numbered ? m_catalog->sets() : QHash<QString, QString>())
        .then(this, [this, kind, set, snapFolderThen, done](const QString &file) {
            if (!file.isEmpty() && snapFolderThen == m_catalog->snapFolder()) {
                const Saved saved{ kind, set, file };
                place(saved);
                if (m_catalog->reading())
                    m_savedDuringRead << saved;
            }
            done(file);
            if (!file.isEmpty())
                emit picturesChanged(set);
        });
}

template <typename T>
void ArcadePictures::loadRecord(RecordFile<T> &record) {
    const QString folder = m_catalog->snapFolder();
    if (folder == record.folder())
        return;
    // The last folder's record written before it is let go of.
    flushRecords();
    record.load(folder);
}

template <typename T>
void ArcadePictures::recordChanged(RecordFile<T> &record) {
    record.markChanged();
    if (!m_recordSave->isActive())
        m_recordSave->start();
}

ArcadePictures::PictureRecord ArcadePictures::PictureRecord::fromJson(const QJsonObject &json) {
    PictureRecord record;
    for (const QJsonValue &path : json.value(QStringLiteral("saved")).toArray())
        record.saved << path.toString();
    record.complete = json.value(QStringLiteral("complete")).toBool();
    return record;
}

QJsonObject ArcadePictures::PictureRecord::toJson() const {
    return { { QStringLiteral("saved"), QJsonArray::fromStringList(saved) },
             { QStringLiteral("complete"), complete } };
}

ArcadePictures::SnapRecord ArcadePictures::SnapRecord::fromJson(const QJsonObject &json) {
    return { QDateTime::fromString(json.value(QStringLiteral("looked")).toString(), Qt::ISODate),
             json.value(QStringLiteral("had")).toBool() };
}

QJsonObject ArcadePictures::SnapRecord::toJson() const {
    return { { QStringLiteral("looked"), looked.toString(Qt::ISODate) }, { QStringLiteral("had"), had } };
}

void ArcadePictures::forgetFolder() {
    m_snaps.clear();
    m_numbered.clear();
    m_titles.clear();
}

void ArcadePictures::gameEnded(const QString &set) {
    const QString shot = firstShot(m_home + QStringLiteral("/snap/") + set);
    if (shot.isEmpty())
        m_shots.remove(set);
    else
        m_shots.insert(set, shot);
}

QStringList ArcadePictures::picturesOf(const QString &set) const {
    const QStringList sets = m_catalog->setAndParent(set);
    // The title screen first, as every game's pictures start from it: the
    // titles folder's, its own else its parent's. Then the numbered ones,
    // its own and of a clone its parent's (those RetroAchievements has of a
    // clone of the same game are saved under the parent's name), whose first
    // is RetroAchievements' title screen; then its snap, the parent's only
    // for a clone without any other; then MAME's own snapshot.
    QStringList pictures;
    for (const QString &s : sets) {
        const QString title = m_titles.value(s);
        if (!title.isEmpty()) {
            pictures << title;
            break;
        }
    }
    pictures << m_numbered.value(set);
    if (sets.size() > 1)
        pictures << m_numbered.value(sets.at(1));
    QString snap = m_snaps.value(set);
    if (snap.isEmpty() && sets.size() > 1 && pictures.isEmpty())
        snap = m_snaps.value(sets.at(1));
    if (!snap.isEmpty())
        pictures << snap;
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

bool ArcadePictures::achievementsUsable() const {
    return m_catalog->achievementsKeyState() == QLatin1String("given");
}

void ArcadePictures::want(const QString &set) {
    // None for a folder they can't be saved in (a read-only USB drive's).
    if (!m_catalog->recognized(set) || m_catalog->snapFolder().isEmpty() || !m_catalog->snapWritable())
        return;
    // RetroAchievements' first: the set's game, else its parent's.
    bool waiting = false;
    if (m_picturesAchievements && achievementsUsable() && !m_noPictures.contains(set)) {
        const QStringList names = m_catalog->setAndParent(set);
        loadRecord(m_pictureRecord);
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

void ArcadePictures::wantSnaps(const QString &set) {
    if (!m_picturesSnaps || !m_catalog->recognized(set) || !m_catalog->snapWritable())
        return;
    // A picture of any kind, its own or its parent's, is enough.
    if (!picturesOf(set).isEmpty())
        return;
    loadRecord(m_snapRecord);
    if (snapsLooked(set))
        return;
    // A moment on, should it still be shown: a list scrolled through asks
    // about none of the games passed.
    m_snapAskSet = set;
    m_snapAsk->start();
}

void ArcadePictures::askSnaps() {
    const QString set = std::exchange(m_snapAskSet, QString());
    // Still a game without a picture, and not asked about meanwhile (the
    // look through every game may have).
    if (set.isEmpty() || !m_picturesSnaps || !m_catalog->recognized(set) || !m_catalog->snapWritable()
        || !picturesOf(set).isEmpty())
        return;
    loadRecord(m_snapRecord);
    if (snapsLooked(set))
        return;
    m_snapJobs.insert(set, { 0, false, m_catalog->snapFolder(), m_catalog->titlesFolder(), false });
    m_arcadeDb->lookUp({ set });
}

bool ArcadePictures::snapsLooked(const QString &set) const {
    if (m_snapJobs.contains(set))
        return true;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto retry = m_snapsRetry.constFind(set);
    if (retry != m_snapsRetry.constEnd() && *retry > now)
        return true;
    const auto record = m_snapRecord.entries().constFind(set);
    if (record == m_snapRecord.entries().constEnd())
        return false;
    return record->had || (record->looked.isValid() && record->looked.secsTo(now) < kSnapsAgainSeconds);
}

void ArcadePictures::onSnapsFound(const QString &set, const QString &ingame, const QString &title) {
    auto job = m_snapJobs.find(set);
    if (job == m_snapJobs.end())
        return;
    loadRecord(m_snapRecord);
    const QString folder = job->folder;
    if (ingame.isEmpty() && title.isEmpty()) {
        // None: noted, so that it isn't asked about again for a while.
        if (folder == m_snapRecord.folder()) {
            m_snapRecord.entries().insert(set, { QDateTime::currentDateTimeUtc(), false });
            recordChanged(m_snapRecord);
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

void ArcadePictures::onSnapsFailed(const QStringList &sets, int holdSeconds) {
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
    emit progressChanged();
}

void ArcadePictures::onSnapDownloaded(const QString &set, bool title, const QByteArray &data) {
    const auto job = m_snapJobs.constFind(set);
    if (job == m_snapJobs.constEnd())
        return;
    if (data.isEmpty()) {
        finishSnap(set, false);
        return;
    }
    // Into the folders it was asked for, whatever the games folder is now:
    // the title screen beside the snap folder, in titles.
    save(title ? Kind::Title : Kind::Snap, set, title ? job->titles : job->folder, job->folder, data,
         [this, set](const QString &file) { finishSnap(set, !file.isEmpty()); });
}

void ArcadePictures::finishSnap(const QString &set, bool saved) {
    auto job = m_snapJobs.find(set);
    if (job == m_snapJobs.end())
        return;
    if (!job->step(saved))
        return;
    if (job->failed) {
        // Tried again in ten minutes.
        m_snapsRetry.insert(set, pictureRetryTime());
        if (job->sweep && m_sweepTimer->isActive())
            ++m_sweepFailed;
    } else if (job->folder == m_snapRecord.folder()) {
        m_snapRecord.entries().insert(set, { QDateTime::currentDateTimeUtc(), true });
        recordChanged(m_snapRecord);
    }
    m_snapJobs.erase(job);
    emit progressChanged();
}

void ArcadePictures::flushRecords() {
    m_recordSave->stop();
    // Each record beside its pictures, after the ones it notes (the same
    // queue).
    m_snapRecord.flush(m_pictureSaves);
    m_pictureRecord.flush(m_pictureSaves);
}

QStringList ArcadePictures::picturesLooked(const QStringList &names) const {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList looked;
    for (const QString &name : names) {
        const auto retry = m_picturesRetry.constFind(name);
        if (m_pictureRecord.entries().value(name).complete || m_pictureJobs.contains(name)
            || (retry != m_picturesRetry.constEnd() && *retry > now))
            looked << name;
    }
    return looked;
}

void ArcadePictures::setSweepInterval(int ms) {
    m_sweepTimer->setInterval(ms);
}

void ArcadePictures::setRetryInterval(int ms) {
    m_pictureRetryMs = ms;
}

QDateTime ArcadePictures::pictureRetryTime(qint64 atLeastSeconds) const {
    // The next look's games are due by the time it is made: a look that
    // couldn't have every picture waits as long as each game does
    // (finishPictureSweep()).
    return QDateTime::currentDateTimeUtc().addMSecs(std::max<qint64>(m_pictureRetryMs, atLeastSeconds * 1000));
}

bool ArcadePictures::sweeping() const {
    return m_sweepTimer->isActive();
}

QString ArcadePictures::sweepMarker() const {
    return m_home + QStringLiteral("/pictures-wanted");
}

void ArcadePictures::startPictureSweep() {
    m_sweepWanted = false;
    m_sweepAgain->stop();
    const bool achievements = m_picturesAchievements && achievementsUsable();
    if (!m_catalog->snapWritable() || (!achievements && !m_picturesSnaps)) {
        QFile::remove(sweepMarker());
        emit progressChanged();
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
    emit progressChanged();
}

void ArcadePictures::sweepPictures() {
    if (m_catalog->running() || m_catalog->scanning())
        return;
    if (!m_catalog->snapWritable()) {
        stopPictureSweep();
        return;
    }
    if (m_sweepPass == SweepPass::Achievements) {
        // The source gone meanwhile (its key, its setting): the next pass.
        if (!m_picturesAchievements || !achievementsUsable()) {
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
            m_sweep = m_catalog->games();
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
        loadRecord(m_pictureRecord);
        while (!m_sweep.isEmpty()) {
            const QString set = m_sweep.takeFirst();
            ++m_sweepDone;
            if (!m_catalog->recognized(set) || m_noPictures.contains(set))
                continue;
            // The game RetroAchievements reads for the set, its own or its
            // parent's (pictureGame()): none it knows, or one looked for
            // already (a clone's parent's pictures are its too, and a look
            // cut short had most of them in), and the set is passed at once,
            // with no second spent asking after it.
            const QStringList names = m_catalog->setAndParent(set);
            const QString game = m_retroAchievements->pictureGame(names);
            const QStringList looked = picturesLooked(names);
            if (game.isEmpty() || looked.contains(game))
                continue;
            m_sweepGame = names;
            ++m_sweepAsked;
            m_retroAchievements->findPicturesLater(names, looked);
            emit progressChanged();
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
        loadRecord(m_snapRecord);
        const QStringList games = m_catalog->games();
        for (const QString &set : games) {
            if (picturesOf(set).isEmpty() && !snapsLooked(set))
                m_sweepSnaps << set;
        }
        m_sweepTotal = static_cast<int>(m_sweepSnaps.size());
        m_sweepDone = 0;
        emit progressChanged();
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
        m_snapJobs.insert(set, { 0, false, m_catalog->snapFolder(), m_catalog->titlesFolder(), true });
    if (!batch.isEmpty()) {
        m_sweepSnapsAsked += static_cast<int>(batch.size());
        m_arcadeDb->lookUp(batch);
    }
    emit progressChanged();
}

void ArcadePictures::nextSweepPass() {
    if (m_sweepPass == SweepPass::Achievements) {
        m_sweepPass = SweepPass::Snaps;
        m_sweep.clear();
        m_sweepGame.clear();
        emit progressChanged();
        return;
    }
    finishPictureSweep();
}

void ArcadePictures::finishPictureSweep() {
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
    emit progressChanged();
}

void ArcadePictures::stopPictureSweep() {
    m_sweepTimer->stop();
    m_sweep.clear();
    m_sweepSnaps.clear();
    m_sweepGame.clear();
    // Nothing under way to count: the frame says so.
    m_sweepDone = 0;
    m_sweepTotal = 0;
    emit progressChanged();
}

void ArcadePictures::lookAgain() {
    stopPictureSweep();
    m_sweepAgain->stop();
    m_sweepPasses = 0;
    m_sweepWanted = true;
    emit progressChanged();
}

void ArcadePictures::gamesRead() {
    if (m_sweepWanted && !m_catalog->reading() && !m_catalog->scanning())
        startPictureSweep();
}

void ArcadePictures::download() {
    m_sweepPasses = 0;
    m_sweepAgain->stop();
    if (m_catalog->reading() || m_catalog->scanning()) {
        // Once the games have been read (gamesRead()): wanted, and said so.
        m_sweepWanted = true;
        emit progressChanged();
        return;
    }
    startPictureSweep();
}

QVariantMap ArcadePictures::progress() const {
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

void ArcadePictures::setSource(const QString &source) {
    // Both unless it says one, or none.
    m_picturesAchievements = source != QLatin1String("progetto-SNAPS") && source != QLatin1String("Off");
    m_picturesSnaps = source != QLatin1String("RetroAchievements") && source != QLatin1String("Off");
    emit progressChanged();
}

QVariantMap ArcadePictures::info() const {
    int withPictures = 0;
    const QStringList games = m_catalog->games();
    for (const QString &set : games) {
        if (!picturesOf(set).isEmpty())
            ++withPictures;
    }
    const QString key = m_catalog->achievementsKeyState();
    return { { QStringLiteral("games"), static_cast<int>(games.size()) },
             { QStringLiteral("withPictures"), withPictures },
             { QStringLiteral("achievements"), m_picturesAchievements },
             { QStringLiteral("snaps"), m_picturesSnaps },
             { QStringLiteral("key"), key == QLatin1String("given") && m_retroAchievements->keyWorks()
                                          ? QStringLiteral("works") : key },
             { QStringLiteral("writable"), m_catalog->snapWritable() },
             { QStringLiteral("snapFolder"), m_catalog->snapFolder() },
             { QStringLiteral("titlesFolder"), m_catalog->titlesFolder() } };
}

// The set shown waits for RetroAchievements' answer about name (its own or
// its parent's) before progetto-SNAPS' are asked for.
void ArcadePictures::snapsAfter(const QString &name) {
    if (m_snapsAfter.isEmpty() || !m_catalog->setAndParent(m_snapsAfter).contains(name))
        return;
    wantSnaps(std::exchange(m_snapsAfter, QString()));
}

void ArcadePictures::onPicturesFound(const QString &name, const QString &state, const QStringList &paths) {
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
    loadRecord(m_pictureRecord);
    if (m_pictureJobs.contains(name) || m_pictureRecord.folder().isEmpty())
        return;
    PictureRecord &record = m_pictureRecord.entries()[name];
    QStringList wanted;
    for (const QString &path : paths) {
        if (!record.saved.contains(path))
            wanted << path;
    }
    if (wanted.isEmpty()) {
        record.complete = true;
        recordChanged(m_pictureRecord);
        snapsAfter(name);
        return;
    }
    m_pictureJobs.insert(name, { static_cast<int>(wanted.size()), false, m_pictureRecord.folder() });
    for (const QString &path : std::as_const(wanted)) {
        m_retroAchievements->media(path, [this, name, path](const QByteArray &data) {
            onPictureDownloaded(name, path, data);
        });
    }
}

void ArcadePictures::onPictureDownloaded(const QString &name, const QString &path, const QByteArray &data) {
    const auto job = m_pictureJobs.constFind(name);
    if (job == m_pictureJobs.constEnd())
        return;
    // Into the folder it was asked for, whatever the games folder is now.
    const QString folder = job->folder;
    if (data.isEmpty()) {
        finishPicture(name, false);
        return;
    }
    save(Kind::Numbered, name, folder, folder, data, [this, name, path, folder](const QString &file) {
        if (!file.isEmpty() && folder == m_pictureRecord.folder())
            m_pictureRecord.entries()[name].saved << path;
        finishPicture(name, !file.isEmpty());
    });
}

void ArcadePictures::finishPicture(const QString &name, bool saved) {
    auto job = m_pictureJobs.find(name);
    if (job == m_pictureJobs.end())
        return;
    const bool recorded = job->folder == m_pictureRecord.folder();
    bool done = false;
    if (job->step(saved)) {
        // All in, or those that weren't tried again in ten minutes.
        if (job->failed) {
            m_picturesRetry.insert(name, pictureRetryTime());
            if (m_sweepTimer->isActive() && m_sweepGame.contains(name))
                ++m_sweepFailed;
        } else if (recorded) {
            m_pictureRecord.entries()[name].complete = true;
        }
        m_pictureJobs.erase(job);
        done = true;
    }
    // The record as it now stands, unless nothing came into it.
    if (saved && recorded)
        recordChanged(m_pictureRecord);
    if (done)
        snapsAfter(name);
}

QString ArcadePictures::badge(const QString &set, int id, const QString &name, bool earned) {
    if (id <= 0 || !arcadehttp::isBadgeName(name) || !m_catalog->recognized(set))
        return QString();
    const QString locked = earned ? QString() : QStringLiteral("_lock");
    const QString file = QStringLiteral("achieve_%1_%2%3.png").arg(set).arg(id).arg(locked);
    const QString snap = m_catalog->snapFolder();
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
    const QString folder = m_catalog->snapWritable() ? snap : kept;
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
        QtConcurrent::run(m_pictureSaves, [folder, path, data] {
            return QDir().mkpath(folder) && writeFileAtomically(path, data);
        }).then(this, saved);
    });
    return QString();
}
