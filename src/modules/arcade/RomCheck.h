#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QMultiHash>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <functional>

// Settings → Arcade → Check ROMs: whether each game's files are whole for
// the engines that play it. The user starts it, as it reads every archive of
// the folder (minutes on a Pi); nothing starts it by itself. FinalBurn Neo's
// side compares the CRCs in each archive's table with its list's (its DAT);
// MAME's is MAME's own -verifyroms, which does the same against MAME's list.
// Neither decodes a file.
namespace romcheck {

// A ROM a set needs: one with a good dump, which has a CRC.
struct Rom {
    QString name;
    qint64 size = -1;   // -1 where the list doesn't say
    quint32 crc = 0;
};
// A set of FinalBurn Neo's list: its ROMs, and the set it takes those it
// shares from (romof: its parent, or a BIOS set).
struct DatSet {
    QString romOf;
    QList<Rom> roms;
};
// Every set of FinalBurn Neo's DAT (ClrMame Pro's XML), BIOS sets too.
QHash<QString, DatSet> readDat(const QString &path);

// What an archive holds: each file's CRC with its size. ok false for one
// that can't be read: a broken archive, or a zip in a build without minizip.
using Contents = QMultiHash<quint32, qint64>;
Contents contentsOf(const QString &archive, bool *ok);

// The ROMs of a set found neither in its archive nor in those of the sets it
// takes ROMs from (archiveOf gives each one's path, "" for none in the
// folder). unreadable is set when its own archive couldn't be read, and
// then nothing is said of it. cache keeps the archives read, by path, for
// the sets after it (a BIOS set is read once).
QStringList missingRoms(const QString &set, const QHash<QString, DatSet> &dat,
                        const std::function<QString(const QString &)> &archiveOf,
                        QHash<QString, Contents> *cache, bool *unreadable);

// A file's stamp as a set's result is kept against: its size and when it
// was last written; "" for none.
QString stampOf(const QFileInfo &file);

// MAME's -verifyroms, read as it comes: each set's verdict ("romset pacman
// is good", "romset sf2ua [sf2] is best available", "romset mslug [neogeo]
// is bad") and the files it lacks ("mslug       : sp-s2.sp1 (131072 bytes) -
// NOT FOUND"). A set MAME found nothing of at all it says nothing of.
struct Verdict {
    bool good = false;
    QStringList missing;
};
class VerifyReader {
public:
    void addData(const QByteArray &chunk);
    // What a run said once it has ended: the line it ended on without a
    // newline too.
    void finish();
    // The verdicts read since the last call, by set.
    QHash<QString, Verdict> take();

private:
    void readLine(const QString &line);
    QByteArray m_pending;
    QHash<QString, QStringList> m_missing;   // as read, before the set's verdict
    QHash<QString, Verdict> m_verdicts;
};

// One set's result: whole (1), not (-1) or not checked (0) for each engine,
// what it lacks for each, and the stamps of the archives its files come
// from as they were at the check (itself, its parent, the sets it takes
// ROMs from); it holds while they are unchanged.
struct State {
    int mame = 0;
    int fbneo = 0;
    QStringList mameMissing;
    QStringList fbneoMissing;
    QHash<QString, QString> family;
};
// A check's results, as romcheck.json keeps them: the sets', when it was
// made, and the MAME and FinalBurn Neo list it was made with (what they
// say holds while those are the same).
struct Results {
    QHash<QString, State> sets;
    QDateTime checked;
    QString mame;
    QString fbneo;
};
Results load(const QString &path);
QByteArray toJson(const Results &results);

} // namespace romcheck

// A check under way, a stage at a time: the archives first (their stamps
// and FinalBurn Neo's CRCs, off the app's thread), then MAME's -verifyroms,
// a batch of sets at a time. MAME's batches stop while a game or MAME's
// -listxml runs (pause()), and go on after (resume()).
class RomCheck : public QObject {
    Q_OBJECT
public:
    struct Job {
        // MAME's side: the program, its -rompath, where it runs and the sets
        // it knows; "" or none to leave it out.
        QString mame;
        QString rompath;
        QString workDir;
        QStringList mameSets;
        // FinalBurn Neo's: its list, and the sets it has (zips and 7z).
        QString dat;
        QStringList fbneoSets;
        // Every archive's path by its set's name (the games folder's and
        // its bios folder's), each one's stamp, and each set's parent.
        QHash<QString, QString> archives;
        QHash<QString, QString> stamps;
        QHash<QString, QString> parents;
        // What the results are made with (romcheck::Results).
        QString mameFrom;
        QString fbneoFrom;
    };

    explicit RomCheck(QObject *parent = nullptr);
    ~RomCheck() override;

    bool active() const { return m_active; }
    // Starts one, held from the start (held: a game or MAME's -listxml
    // running) till resume(). Nothing while one is under way.
    void start(const Job &job, bool held);
    // MAME's batch stopped where it had got to, the rest asked after resume().
    void pause();
    void resume();
    // { active, stage ("Archives" or "MAME"), done, total, held, result
    // ("" for none yet, "Done", or why it stopped) }.
    QVariantMap progress() const;

signals:
    void progressChanged();
    void done(const romcheck::Results &results);

private:
    void archivesRead();
    void verifyNext();
    void verifyRead();
    void verifyFinished(int exitCode, QProcess::ExitStatus status);
    void stopProcess();
    void finish(const QString &result);

    Job m_job;
    bool m_active = false;
    bool m_held = false;
    QString m_stage;
    int m_done = 0;
    int m_total = 0;
    QString m_result;
    romcheck::Results m_results;
    QFutureWatcher<QHash<QString, romcheck::State>> *m_archives = nullptr;
    // MAME's side: the sets still to ask, those of the batch asked, and
    // those it has answered for.
    QStringList m_toVerify;
    QStringList m_batch;
    QSet<QString> m_answered;
    QProcess *m_process = nullptr;
    romcheck::VerifyReader m_reader;
    QTimer *m_watchdog = nullptr;
};
