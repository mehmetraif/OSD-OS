#pragma once
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <tuple>

class ArcadeDb;
class RetroAchievements;
class QThreadPool;
class QTimer;

// The Arcade module's pictures of its games: those in the games folder's
// snap and titles folders and MAME's own snapshots, read with the folder
// (read()), and those downloaded as a game is shown (want()) or for every
// game (download(), the look through every game): RetroAchievements' (its
// Web API key's, into the snap folder as <set>_<n>), then, for a game still
// without any, progetto-SNAPS' through ArcadeDB (snap/<set>.png and
// titles/<set>.png, as the picture packs for MAME lay them out). And the
// achievements' badges, kept beside them. What it needs of the games comes
// from the backend (Catalog); the pictures and the records of what was
// downloaded are written off the GUI thread, on the backend's one thread
// for its files (saves).
class ArcadePictures : public QObject {
    Q_OBJECT
public:
    // What the pictures need of the games, as the backend has them.
    class Catalog {
    public:
        // A set in the folder that isn't under Not Recognized: one whose
        // pictures are looked for.
        virtual bool recognized(const QString &set) const = 0;
        // A set, and the parent of a clone.
        virtual QStringList setAndParent(const QString &set) const = 0;
        // The folder's games, in the list's order.
        virtual QStringList games() const = 0;
        // The folder's sets by name, their files' names by them: a picture
        // numbered after a game isn't named as a set (sf2_1).
        virtual QHash<QString, QString> sets() const = 0;
        // The games folder's snap and titles folders, "" while there is no
        // games folder to read; and whether the snap folder can be written
        // in (or, while there is none, the games folder).
        virtual QString snapFolder() const = 0;
        virtual QString titlesFolder() const = 0;
        virtual bool snapWritable() const = 0;
        // The folder being read; it or MAME being asked; a game playing.
        virtual bool reading() const = 0;
        virtual bool scanning() const = 0;
        virtual bool running() const = 0;
        // The RetroAchievements account's Web API key: "none", "refused"
        // (RetroAchievements refused it) or "given".
        virtual QString achievementsKeyState() const = 0;

    protected:
        ~Catalog() = default;
    };

    // The pictures read with the games folder: its snap by set, those
    // numbered after a set in it (<set>_1.png…) in turn, its titles
    // folder's by set, and the first snapshot MAME took of each game.
    struct Found {
        QHash<QString, QString> snaps;
        QHash<QString, QStringList> numbered;
        QHash<QString, QString> titles;
        QHash<QString, QString> shots;
    };

    // home: <data>/arcade, where MAME's snapshots are, the look's marker
    // and the badges that can't go in the snap folder.
    ArcadePictures(const QString &home, Catalog *catalog, RetroAchievements *retroAchievements, ArcadeDb *arcadeDb,
                   QThreadPool *saves, QObject *parent = nullptr);

    // Off the GUI thread, with the games folder (sets: the folder's): the
    // pictures in snapFolder and titlesFolder, and in shotsFolder, MAME's
    // own snapshots, a folder of them by set.
    static Found read(const QString &snapFolder, const QString &titlesFolder, const QString &shotsFolder,
                      const QHash<QString, QString> &sets);
    // What a read of the folder found taken, with the pictures saved while
    // it went on, which it may not have seen.
    void takeRead(Found &&found);
    // Another games folder: its pictures are read with it.
    void forgetFolder();
    // A game played has ended: a snapshot taken in it (F12) is its picture
    // now.
    void gameEnded(const QString &set);

    // A game's pictures, as file URLs, in the order the Arcade screen shows
    // them: the games folder's snap of it, those numbered after it there
    // (RetroAchievements' among them), its title screen in the titles
    // folder, then the first snapshot MAME took of it; a clone's own, else
    // its parent's. As read with the folder (takeRead()), till the list
    // opens again, and those downloaded since.
    QStringList picturesOf(const QString &set) const;
    // ArcadeBackend::wantPictures().
    void want(const QString &set);
    // ArcadeBackend::picturesInfo() and picturesProgress().
    QVariantMap info() const;
    QVariantMap progress() const;
    // ArcadeBackend::downloadPictures(): the look through every game.
    void download();
    // Settings → Arcade → Pictures From: "RetroAchievements",
    // "progetto-SNAPS", "Off", or both for anything else.
    void setSource(const QString &source);
    // Read the Games Again: every game's pictures looked for once the games
    // have been read again (gamesRead()), a look under way starting anew
    // then.
    void lookAgain();
    // The games have been read, and MAME has answered: a look wanted
    // starts.
    void gamesRead();
    // The records that changed, written off the GUI thread: a moment after
    // the change (m_recordSave), before the snap folder changes, and as the
    // backend ends.
    void flushRecords();
    // ArcadeBackend::achievementBadge().
    QString badge(const QString &set, int id, const QString &name, bool earned);

    // How long the look through the games for pictures waits between two
    // steps (sweepPictures()): a second, or a test's; and how long after a
    // look that couldn't have every picture the next is made: ten minutes,
    // or a test's.
    void setSweepInterval(int ms);
    void setRetryInterval(int ms);
    // Whether that look is under way.
    bool sweeping() const;

signals:
    // A game's pictures have changed: one of RetroAchievements' or
    // progetto-SNAPS' is in. set: the one it is named after, the game's or
    // its parent's.
    void picturesChanged(const QString &set);
    void progressChanged();
    void badgesReady();

private:
    void onPicturesFound(const QString &name, const QString &state, const QStringList &paths);
    // Of a set and its parent (setAndParent()), those whose pictures have
    // been looked for: all in, being downloaded, or tried less than ten
    // minutes ago. The record read first (loadPictureRecord()).
    QStringList picturesLooked(const QStringList &names) const;
    // When a game whose pictures couldn't be had is asked about again: as
    // long on as the next look waits (setRetryInterval()), ten minutes, or
    // the pause a server asked for where that is longer.
    QDateTime pictureRetryTime(qint64 atLeastSeconds = 0) const;
    // The look through every game for pictures: Download Pictures', Read
    // the Games Again's once the folder has been read and MAME has answered
    // (gamesRead()), and the next start's for one cut short (its marker,
    // sweepMarker(), stays until a look ends with every picture had). In
    // two passes, a step every m_sweepTimer's interval, neither beside a
    // game being played nor while the folder is read or MAME asked:
    // - RetroAchievements', with its key: every game of the folder's, by
    //   name, its pictures downloaded as they would be as it is shown, a
    //   game at a time, not before RetroAchievements' list of games is in;
    //   those looked for before and those RetroAchievements hasn't passed
    //   over; a game's asked for once nothing else is (findPicturesLater()),
    //   and done with (laterPending()) and its pictures saved before the
    //   next's;
    // - progetto-SNAPS', through ArcadeDB: the games still without any
    //   picture, kMostAsked at a time, each batch's pictures saved before
    //   the next is asked about.
    // A game whose pictures couldn't be had is tried again in a look ten
    // minutes later, up to kSweepPasses looks in a row. With the folder
    // unwritable it stops, and a source gone (its key, its setting) passes
    // to the next pass; Download Pictures or Read the Games Again asked
    // again starts it anew.
    void startPictureSweep();
    void sweepPictures();
    void stopPictureSweep();
    // The pass done: the next, or the look ended.
    void nextSweepPass();
    void finishPictureSweep();
    QString sweepMarker() const;
    // RetroAchievements' key is kept, and wasn't refused.
    bool achievementsUsable() const;
    // progetto-SNAPS' pictures of a game without any (picturesOf()), through
    // ArcadeDB, as Pictures From allows: its screenshot in play saved as
    // snap/<set>.png, its title screen as titles/<set>.png, the way the
    // picture packs for MAME lay them out, never over a file there. Asked
    // about once: what ArcadeDB has of each set is noted in the snap
    // folder's .arcadedb.json (one it has no pictures of is asked about
    // again after kSnapsAgainSeconds), a try that failed is made again ten
    // minutes later, and one ArcadeDB turned away (too many requests) once
    // the pause it asked for is over.
    void wantSnaps(const QString &set);
    // m_snapAsk's: the set shown asked about, should it still be without a
    // picture.
    void askSnaps();
    void snapsAfter(const QString &name);
    bool snapsLooked(const QString &set) const;
    void onSnapsFound(const QString &set, const QString &ingame, const QString &title);
    void onSnapsFailed(const QStringList &sets, int holdSeconds);
    void onSnapDownloaded(const QString &set, bool title, const QByteArray &data);
    void finishSnap(const QString &set, bool saved);
    void loadSnapRecord();
    void saveSnapRecord();
    // A picture of RetroAchievements' downloaded for a game, or not (data
    // empty): saved off the GUI thread as <name>_<n> in the snap folder, n
    // the first number no picture of that name has there, so that a file
    // already there, the user's own or one saved before, is never written
    // over; then its record kept.
    void onPictureDownloaded(const QString &name, const QString &path, const QByteArray &data);
    void finishPicture(const QString &name, bool saved);
    // The pictures downloaded for each game (by the set they are named
    // after): .retroachievements.json in the snap folder, beside them, so
    // that it goes where they go (another games folder, another machine
    // with the same one); read when first needed in a folder, written where
    // the pictures are saved.
    void loadPictureRecord();
    void savePictureRecord();

    QString m_home;
    Catalog *m_catalog = nullptr;
    RetroAchievements *m_retroAchievements = nullptr;
    ArcadeDb *m_arcadeDb = nullptr;
    // Where the pictures and their record are saved, one at a time, in the
    // order they came.
    QThreadPool *m_pictureSaves = nullptr;

    QHash<QString, QString> m_snaps;    // set → its picture in the games folder's snap
    QHash<QString, QStringList> m_numbered;   // set → the pictures numbered after it there, in turn
    QHash<QString, QString> m_titles;   // set → its title screen in the games folder's titles
    QHash<QString, QString> m_shots;    // set → the first snapshot MAME took of it
    // Settings → Arcade → Pictures From: which sources a game's pictures
    // come from.
    bool m_picturesAchievements = true;
    bool m_picturesSnaps = true;
    // RetroAchievements' pictures: what was downloaded of each game into the
    // snap folder whose record is read (the pictures' paths on its media
    // server, and whether all of them are in), the downloads under way (how
    // many to come, whether one failed, and the folder they go in), the
    // games it has none of (this run), when a game whose pictures couldn't
    // be read may be tried again, and those saved while the folder was read,
    // which that read may not have seen.
    struct PictureRecord {
        QStringList saved;
        bool complete = false;
    };
    struct PictureJob {
        int pending = 0;
        bool failed = false;
        QString folder;
    };
    QHash<QString, PictureRecord> m_pictureRecord;
    QString m_pictureRecordFolder;
    QHash<QString, PictureJob> m_pictureJobs;
    QSet<QString> m_noPictures;
    QHash<QString, QDateTime> m_picturesRetry;
    QHash<QString, QStringList> m_savedDuringRead;
    // progetto-SNAPS' pictures through ArcadeDB: what it has of each set in
    // the snap folder whose record is read (when it was asked, and whether
    // it had pictures, all of them saved), the sets asked about or being
    // downloaded (how many pictures to come, whether one failed, and the
    // folders they go in), when a set whose pictures couldn't be had may be
    // tried again, the set shown that waits for RetroAchievements' answer
    // before ArcadeDB is asked, and the pictures saved while the folder was
    // read, which that read may not have seen.
    struct SnapRecord {
        QDateTime looked;
        bool had = false;
    };
    struct SnapJob {
        int pending = 0;
        bool failed = false;
        QString folder;
        bool sweep = false;   // asked about by the look through every game, not shown
        QString titles;       // the titles folder beside folder
    };
    QHash<QString, SnapRecord> m_snapRecord;
    QString m_snapRecordFolder;
    QHash<QString, SnapJob> m_snapJobs;
    QHash<QString, QDateTime> m_snapsRetry;
    QString m_snapsAfter;
    // The set shown that ArcadeDB is asked about a moment after it was shown
    // (askSnaps()): a list scrolled through asks about none of the games
    // passed, as RetroAchievements' findPictures() does.
    QTimer *m_snapAsk = nullptr;
    QString m_snapAskSet;
    // The records written a moment after they changed, both in one go
    // (flushRecords()), rather than at every set that comes in.
    QTimer *m_recordSave = nullptr;
    bool m_snapRecordDirty = false;
    bool m_pictureRecordDirty = false;
    QList<std::tuple<QString, QString, bool>> m_namedDuringRead;   // set, file, a title screen
    // The look through the games for pictures: wanted once the games have
    // been read (again), the pass it is on, the sets still to look at in
    // each (taken as a pass's first step can go: m_sweepListed,
    // m_sweepSnapsListed), the game asked of RetroAchievements (its set and
    // parent), how many games each pass has looked at of how many, how many
    // were asked about and failed, how many looks in a row have been made
    // (the next after failures, m_sweepAgain), and how the last ended.
    enum class SweepPass { Achievements, Snaps };
    bool m_sweepWanted = false;
    SweepPass m_sweepPass = SweepPass::Achievements;
    bool m_sweepListed = false;
    bool m_sweepSnapsListed = false;
    QStringList m_sweep;
    QStringList m_sweepSnaps;
    QStringList m_sweepGame;
    int m_sweepDone = 0;
    int m_sweepTotal = 0;
    int m_sweepAsked = 0;
    int m_sweepSnapsAsked = 0;
    int m_sweepFailed = 0;
    int m_sweepPasses = 0;
    QString m_sweepResult;
    QTimer *m_sweepTimer = nullptr;
    QTimer *m_sweepAgain = nullptr;
    int m_pictureRetryMs = 10 * 60 * 1000;   // how long before a failed game is tried again (a test's)
    // The achievements' badges being downloaded, by file, and when one that
    // couldn't be may be again.
    QSet<QString> m_badgesWanted;
    QHash<QString, QDateTime> m_badgesRetry;
};
