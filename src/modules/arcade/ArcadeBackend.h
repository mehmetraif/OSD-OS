#pragma once
#include <QHash>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include "Mame.h"

class AppCore;
class DisplayHandoff;
class ScriptLauncher;
class QTimer;

// The Arcade module: the arcade games in its folder, played in MAME (the
// standalone emulator, a program of its own: Debian's mame package on the
// OSD/OS image).
//
// - The games are the folder's sets: a zip, a 7z or a folder each, named
//   after the game's short name, BIOS sets beside them. MAME says what each
//   is (-listxml, asked only about sets it hasn't been asked about, kept in
//   <data>/arcade/games.json until MAME itself changes, read as the list
//   first opens), and BIOS sets and devices aren't listed.
// - The tree (requestEntries) lists them under Recently Played, Favorites,
//   Search, All Games, and by players, decade and maker, and those MAME has
//   no game of, or can't open by their names, under Not Recognized.
// - A game plays as a takeover through a ScriptLauncher of the module's own
//   ("arcade" to DisplayHandoff), MAME's keeping in <data>/arcade: its
//   settings, NVRAM, high scores (the hiscore plugin, High Scores), save
//   states and the like, and the controller file Settings → Controls'
//   players are given to it in (mame::controllerConfig). It ends the way
//   MAME does, Coin and Start
//   together on player 1's controller or Esc, never stopped from here: MAME
//   ignores SIGTERM, and a game killed loses what it would have saved.
class ArcadeBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // MAME is being asked about sets it hasn't been asked about yet.
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // What stands in the way of the list, for a help line: a folder that
    // can't be read, no MAME. "" when nothing does.
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
    // The games folder is there to be read (and copied into).
    Q_PROPERTY(bool folderReadable READ folderReadable NOTIFY problemChanged)
    // A game has been played this run: the first waits longer before MAME
    // takes the screen, for how to come back to be read.
    Q_PROPERTY(bool played READ played NOTIFY playedChanged)
public:
    explicit ArcadeBackend(const QString &appRoot, const QString &dataRoot, AppCore *appCore,
                           DisplayHandoff *handoff, QObject *parent = nullptr);
    ~ArcadeBackend() override;

    // Where the games are: the module's Games Folder, else Games/Arcade in
    // Local Files' default media folder (on the image, the card's OSD-OS
    // partition).
    Q_INVOKABLE QString gamesFolder() const { return m_gamesFolder; }
    // Looks in the folder again (each time the list opens), and asks MAME
    // about any set it hasn't been asked about.
    Q_INVOKABLE void refresh();
    // A folder of the tree ("top" its top), or undefined while MAME is being
    // asked: entriesReady(path) follows (then "top" holds only Recently
    // Played and Favorites, and entriesReady("top") follows too). A game is
    // { name, path ("game:" and its set), isFolder, game (its set), detail
    // (its year, maker, players, how well MAME runs it and its file, a line
    // each) }; a set MAME doesn't know, or can't open by its name, is
    // { name (its file's), path ("unknown:" and the set), kind: "unknown",
    // detail (why, two lines) }. Every folder asked for gets entriesReady
    // again once what it holds may have changed: MAME has answered.
    Q_INVOKABLE QVariant requestEntries(const QString &path);

    // Starts a game in MAME, which has the screen until it ends: finished().
    // False, with lastError(), when it couldn't be started; nothing was.
    Q_INVOKABLE bool play(const QString &name);
    Q_INVOKABLE QString lastError() const { return m_lastError; }
    // What MAME printed in the last game's run.
    Q_INVOKABLE QString output() const;

    // manifest: rescan (action): MAME asked about every set again.
    Q_INVOKABLE void rescan();

    // MAME's program: DATA_ROOT/bin/mame, else mame on the PATH, else
    // Debian's /usr/games/mame (not on a service's PATH). "" for none.
    QString mamePath() const;

    bool    running() const;
    // A batch being asked, or more to ask once the game playing has ended.
    bool    scanning() const { return m_scan != nullptr || !m_toAsk.isEmpty(); }
    QString problem() const { return m_problem; }
    bool    folderReadable() const { return m_folderReadable; }
    bool    played() const { return m_played; }

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

signals:
    void entriesReady(const QString &path);
    void runningChanged();
    void scanningChanged();
    void problemChanged();
    void playedChanged();
    // reason: "ok", "failed", "stopped" or "failed_to_start" (ScriptLauncher's);
    // notice: what to tell the player when it isn't "ok", else "".
    void finished(int exitCode, const QString &reason, const QString &notice);

private:
    void applySetting(const QString &key, const QVariant &value);
    void setGamesFolder(const QString &path);
    void setProblem(const QString &problem);
    // The sets in the folder now: short name → its file's or folder's name.
    void listFolder();
    // What makes the list's own problem: no folder, no MAME, MAME not
    // answering.
    void checkProblem();
    void loadCache();
    void saveCache() const;
    // What MAME said of the sets, all forgotten: it is asked again.
    void forget();
    // MAME's program as it is: its path, size and time, which change with it.
    QString mameIdentity() const;
    void startScan();
    void scanNext();
    void takeMachines();
    void onScanFinished(int exitCode, QProcess::ExitStatus status);
    // entriesReady for every folder asked for since the last time.
    void emitShown();

    // The folder's games (not BIOS sets, devices or sets MAME doesn't know)
    // by name, the players, decades and makers they are under, and the sets
    // under Not Recognized: made again only once the folder or what MAME
    // says of it has changed (invalidate()), as a full set of MAME's games
    // is tens of thousands.
    struct Index {
        QStringList games;
        QHash<QString, QMap<QString, QStringList>> places;   // kind → place → its games
        QStringList unknown;
    };
    const Index &index() const;
    void invalidate() { m_indexed = false; }
    // The folders of "players", "years" or "makers", and a game's place in them.
    QVariantList folders(const QString &kind) const;
    static QString folderOf(const QString &kind, const mame::Machine &machine);
    QVariantList gameEntries(const QStringList &sets) const;
    QVariantMap gameEntry(const QString &set) const;
    QVariantMap unknownEntry(const QString &set) const;
    QVariantList listEntries(const QString &list) const;
    // The file name MAME opens a set by: its short name in lower case.
    QString mameFileName(const QString &set) const;
    // The games folder's bios or samples folder, as it is named there; ""
    // for none.
    QString otherFolder(const QString &name) const;
    QString rompath() const;
    QString pluginsPath() const;
    // The folder of a MAME with its own files beside it (an upstream build,
    // GroovyMAME): bgfx, plugins, hash, ini. "" for one installed as the
    // system's (Debian's), whose mame.ini says where they are.
    QString ownFolder() const;
    QString noticeFor(int exitCode, const QString &reason) const;

    QString m_appRoot;
    QString m_dataRoot;
    QString m_home;               // <data>/arcade: MAME's keeping
    AppCore *m_appCore = nullptr;
    ScriptLauncher *m_launcher = nullptr;

    // Settings.
    QString m_gamesFolder;
    bool    m_highScores = true;
    QString m_video;              // "" for MAME's own (mame.ini's)

    // The folder's sets, and what MAME said of every set it was asked about.
    QHash<QString, QString> m_sets;
    QSet<QString> m_folders;      // sets that are a folder of files
    QHash<QString, QString> m_otherFolders;   // "bios", "samples" → as named there
    QSet<QString> m_misnamed;     // sets whose file MAME can't open by its name
    QHash<QString, mame::Machine> m_machines;
    QSet<QString> m_unknown;      // sets MAME has no machine of
    QString m_identity;           // mameIdentity() the cache was made with
    QString m_build;              // MAME's build, as it says
    bool    m_cacheLoaded = false;
    mutable Index m_index;
    mutable bool  m_indexed = false;

    // A -listxml run, a batch of sets at a time. The next waits for a game
    // that is playing to end.
    QProcess *m_scan = nullptr;
    QTimer   *m_scanTimer = nullptr;
    QStringList m_toAsk;
    QStringList m_asked;
    mame::ListXmlReader m_reader;
    bool m_scanFailed = false;

    // The folders asked for since entriesReady was last sent for them.
    QSet<QString> m_shown;

    QString m_problem;
    bool    m_folderReadable = true;
    QString m_lastError;
    QString m_playing;
    bool    m_played = false;
};
