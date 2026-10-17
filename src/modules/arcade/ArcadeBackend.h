#pragma once
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <functional>
#include <utility>
#include "Libretro.h"
#include "Mame.h"

class AppCore;
class DisplayHandoff;
class FbneoInstaller;
class RetroAchievements;
class QNetworkAccessManager;
class QNetworkReply;
class ScriptLauncher;
class QTimer;

// The Arcade module: the arcade games in its folder, played in MAME (the
// standalone emulator, a program of its own: Debian's mame package on the
// OSD/OS image), or in RetroArch with the FinalBurn Neo core (libretro::),
// for a game it has, where RetroAchievements and online play are.
//
// - The games are the folder's sets: a zip, a 7z or a folder each, named
//   after the game's short name, BIOS sets beside them. MAME says what each
//   is (-listxml, asked only about sets it hasn't been asked about, kept in
//   <data>/arcade/games.json until MAME itself changes, read as the list
//   first opens), and BIOS sets and devices aren't listed. The folder,
//   games.json, catver.ini and the pictures are read off the GUI thread
//   (read()): a full set is tens of thousands of files.
// - Two screens list them, Settings → Arcade → Screen's. The original, the
//   tree (requestEntries), lists them under Recently Played, Favorites,
//   Search, All Games, and by players, decade and maker, and those MAME has
//   no game of, or can't open by their names, under Not Recognized. The
//   Arcade screen lists them a filter at a time (filters(), filterGames()):
//   All Games, Favorites, Recently Played, each genre (made from catver.ini's
//   categories, in the games folder or in <data>/arcade), each number of
//   players, each decade and each publisher, and Not Recognized, as
//   Settings → Arcade → Menu Filters chooses; gameDetails() says what a game
//   is: its system, maker, year, players, picture (the games folder's snap
//   folder, or MAME's own snapshots) and how long it has been played
//   (<data>/arcade/playtime.json).
// - A game plays as a takeover through a ScriptLauncher of the module's own
//   ("arcade" to DisplayHandoff), MAME's keeping in <data>/arcade: its
//   settings, NVRAM, high scores (the hiscore plugin, High Scores), save
//   states and the like, and the controller file Settings → Controls'
//   players are given to it in (mame::controllerConfig). The credits bought
//   on the screen go in as it starts (mame::coinScript). It ends the way
//   MAME does, Coin and Start together on any player's controller or Esc,
//   never stopped from here: MAME ignores SIGTERM, and a game killed loses
//   what it would have saved.
// - Settings → Arcade → Play at Startup is the app's startup favourite
//   (app.startup_favorite, which a game's options set too) while it is one
//   of this module's games: the two are kept the same.
class ArcadeBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // The folder is being read, or MAME asked about sets it hasn't been
    // asked about yet.
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // The folder is being read (refresh()): what is in it is known once it
    // is false again, which a startup favourite waits for.
    Q_PROPERTY(bool reading READ reading NOTIFY readingChanged)
    // What stands in the way of the list, for a help line: a folder that
    // can't be read, no MAME. "" when nothing does.
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
    // The games folder is there to be read (and copied into).
    Q_PROPERTY(bool folderReadable READ folderReadable NOTIFY problemChanged)
    // A game has been played this run: on the original screen, the first
    // waits longer before MAME takes the screen, for how to come back to be
    // read.
    Q_PROPERTY(bool played READ played NOTIFY playedChanged)
    // The RetroAchievements account RetroArch plays FinalBurn Neo's games
    // under (Online High Scores): its user ("" for none), and its state:
    // "none", "pending" (signed in here, RetroArch to take the password at
    // the next game and swap it for a token), "signedIn" (a token), or
    // "failed" (achievementsMessage says why).
    Q_PROPERTY(QString achievementsUser READ achievementsUser NOTIFY achievementsChanged)
    Q_PROPERTY(QString achievementsState READ achievementsState NOTIFY achievementsChanged)
    Q_PROPERTY(QString achievementsMessage READ achievementsMessage NOTIFY achievementsChanged)
    // A Web API key is kept, for the leaderboards on the Arcade screen; and
    // RetroAchievements refused it.
    Q_PROPERTY(bool achievementsKey READ achievementsKey NOTIFY achievementsChanged)
    Q_PROPERTY(bool achievementsKeyRefused READ achievementsKeyRefused NOTIFY achievementsChanged)
    // Settings → Arcade → FinalBurn Neo's download (downloadFbneo()): under
    // way, how far (0 to 100), and why the last one failed ("" when it
    // didn't).
    Q_PROPERTY(bool fbneoDownloading READ fbneoDownloading NOTIFY fbneoChanged)
    Q_PROPERTY(int fbneoProgress READ fbneoProgress NOTIFY fbneoChanged)
    Q_PROPERTY(QString fbneoMessage READ fbneoMessage NOTIFY fbneoChanged)
public:
    explicit ArcadeBackend(const QString &appRoot, const QString &dataRoot, AppCore *appCore,
                           DisplayHandoff *handoff, QObject *parent = nullptr);
    ~ArcadeBackend() override;

    // Where the games are: the module's Games Folder, else Games/Arcade in
    // Local Files' default media folder (on the image, the card's OSD-OS
    // partition).
    Q_INVOKABLE QString gamesFolder() const { return m_gamesFolder; }
    // Looks in the folder again (each time the list opens), off the GUI
    // thread, and then asks MAME about any set it hasn't been asked about.
    Q_INVOKABLE void refresh();

    // The original screen's tree: a folder of it ("top" its top), or
    // undefined while MAME is being asked: entriesReady(path) follows (then
    // "top" holds only Recently Played and Favorites, and entriesReady("top")
    // follows too). A game is { name, path ("game:" and its set), isFolder,
    // game (its set), detail (its year, maker, players, how well MAME runs it
    // and its file, a line each) }; a set MAME doesn't know, or can't open by
    // its name, is { name (its file's), path ("unknown:" and the set), kind:
    // "unknown", detail (why, two lines) }. Every folder asked for gets
    // entriesReady again once what it holds may have changed: MAME has
    // answered.
    Q_INVOKABLE QVariant requestEntries(const QString &path);

    // The filters with games in them, in the order ◄ ► goes round them:
    // [{ id, name, count }]. id: "all", "favorites", "recent",
    // "genres/<genre>", "players/<n>", "years/<decade>" ("years/unknown"),
    // "makers/<publisher>", "unknown" (Not Recognized); all but All Games
    // only while Menu Filters shows them, but Not Recognized in All Games'
    // place when MAME knows none of the sets. While MAME is being asked,
    // only the lists, All Games not yet: gamesChanged() follows once it has
    // answered.
    Q_INVOKABLE QVariantList filters();
    // A page of a filter: its games from first on, count at most, as
    // { set, name (its title), favorite }; Not Recognized's as { set, name
    // (its file's), unknown: true }.
    Q_INVOKABLE QVariantList filterGames(const QString &filter, int first, int count);
    // Where a set is in a filter: -1 when it isn't in it. On Online Games,
    // where a game is once for each room that hosts it, room's row (a room's
    // id), else the game's first.
    Q_INVOKABLE int filterIndex(const QString &filter, const QString &set, int room = -1);
    // What the screen says of a set: { set, name (MAME's description),
    // system, maker, year, players, playTime (seconds played), picture (a
    // file URL, "" for none), vertical }; for one under Not Recognized
    // { set, name (its file's), unknown: true, detail (why, and what to do,
    // a line each) }; {} for a set not in the folder.
    Q_INVOKABLE QVariantMap gameDetails(const QString &set);

    // Starts a game in its engine (engineOf()), which has the screen until it
    // ends: finished(). credits: the coins MAME puts in as it starts (9 at
    // most; FinalBurn Neo takes them from the coin button). False, with
    // lastError(), when it couldn't be started; nothing was.
    Q_INVOKABLE bool play(const QString &name, int credits = 0);
    // The engine a game plays in: "fbneo", RetroArch with the FinalBurn Neo
    // core, for a set FinalBurn Neo has, chosen in its options, or by itself
    // (Auto) while Online High Scores or Online Play is on, which only it
    // has, or with no MAME; else "mame".
    Q_INVOKABLE QString engineOf(const QString &set) const;
    // A game's choices of engine, for its options: [{ id ("auto", "mame",
    // "fbneo"), label, current }], Auto naming the one it picks. Only Auto
    // and MAME for a set FinalBurn Neo hasn't, or without it.
    Q_INVOKABLE QVariantList engineChoices(const QString &set) const;
    Q_INVOKABLE void setEngine(const QString &set, const QString &engine);
    // RetroArch, the FinalBurn Neo core and its list of games are there.
    Q_INVOKABLE bool fbneoReady() const;
    // FinalBurn Neo can play the game: it is ready, has the set, and the set
    // is a zip or a 7z named as it looks for it (not a folder of files, not
    // in capitals).
    bool fbneoPlays(const QString &set) const;

    // Online Play (Settings → Arcade → Online Play): the netplay lobby's
    // rooms (lobby.libretro.com) of the folder's games FinalBurn Neo plays,
    // without a password, newest first: [{ id, set, title, host, country }].
    // refreshRooms() reads the lobby again, once at a time: roomsChanged(),
    // and the Online Games folder and filter listed again, once it has.
    Q_INVOKABLE QVariantList rooms() const;
    Q_INVOKABLE void refreshRooms();
    // A room joined, or a game hosted through the relay Settings chose, in
    // RetroArch with FinalBurn Neo, as play() starts a game: finished() when
    // it ends; false, with lastError(), when it couldn't be started.
    Q_INVOKABLE bool joinRoom(int id);
    Q_INVOKABLE bool hostGame(const QString &set);
    // Whether a game can be hosted: Online Play on, FinalBurn Neo there, and
    // the set its, a zip or a 7z.
    Q_INVOKABLE bool canHost(const QString &set) const;
    // The lobby's address: lobby.libretro.com's, or a stand-in's.
    void setLobbyUrl(const QUrl &url) { m_lobbyUrl = url; }

    // Account page (Settings → Arcade → RetroAchievements Account). The
    // password is kept in memory only, for RetroArch to sign in with at the
    // next game (RetroAchievements' own way: a launcher may not), never
    // written but into that game's config, which goes as the game ends.
    Q_INVOKABLE void signInAchievements(const QString &user, const QString &password);
    Q_INVOKABLE void signOutAchievements();
    // The account's Web API key (RetroAchievements' settings page), for
    // reading leaderboards; "" forgets it.
    Q_INVOKABLE void setAchievementsKey(const QString &key);
    // The Arcade screen's high scores for a game that plays in FinalBurn Neo
    // while Online High Scores is on and a key is kept: its RetroAchievements
    // leaderboard (RetroAchievements::board()), { state, title, entries };
    // { state: "off" } for any other game, whose are the High Scores
    // module's. leaderboardReady() once one asked for has been read.
    Q_INVOKABLE QVariantMap leaderboard(const QString &set);

    // FinalBurn Neo's page (Settings → Arcade → FinalBurn Neo): { retroarch
    // (its program, "" for none), core ("" for none), coreDate (its file's,
    // yyyy-MM-dd), downloaded (the core is the one downloadFbneo() put
    // there), games (how many its list has), license (its file, "" for
    // none), canDownload (libretro builds it for this machine, and this
    // build can unpack it) }.
    Q_INVOKABLE QVariantMap fbneoInfo() const;
    // The licence's text (fbneoInfo().license), "" for none.
    Q_INVOKABLE QString fbneoLicenseText() const;
    // Downloads the core, its list of games and its licence (FbneoInstaller);
    // fbneoChanged() as it goes, and the list read again once it is in.
    Q_INVOKABLE void downloadFbneo();
    Q_INVOKABLE QString lastError() const { return m_lastError; }
    // What MAME printed in the last game's run.
    Q_INVOKABLE QString output() const;

    // manifest: rescan (action): MAME asked about every set again.
    Q_INVOKABLE void rescan();
    // manifest: start_filter's choices (dynamicOptionsReady): All Games, and
    // Favorites, Recently Played and the genres, players, decades and
    // publishers there are, those Menu Filters shows.
    Q_INVOKABLE void startFilterOptions();
    // manifest: menu_filters' (dynamicOptionsReady): under a heading each,
    // the lists, the genres, players, decades and publishers there are, as
    // { id (the filter's), label, default }; a heading as { label,
    // heading: true }. A publisher's is off until turned on: a full set has
    // hundreds.
    Q_INVOKABLE void menuFilterOptions();
    // manifest: play_at_startup's: None and the favorites.
    Q_INVOKABLE void startupGameOptions();

    // The sets played last, newest first: the High Scores module's Recently
    // Played, as main.cpp hands them over.
    QStringList recentSets() const { return listedSets(QStringLiteral("recent")); }

    // MAME's program: DATA_ROOT/bin/mame, else mame on the PATH, else
    // Debian's /usr/games/mame (not on a service's PATH). "" for none.
    QString mamePath() const;
    // RetroArch's (libretro::programPath()).
    QString retroarchPath() const;

    bool    running() const;
    // The folder being read; or a batch being asked, or more to ask once the
    // game playing has ended.
    bool    scanning() const { return m_reading || m_scan != nullptr || !m_toAsk.isEmpty(); }
    bool    reading() const { return m_reading; }
    QString problem() const { return m_problem; }
    bool    folderReadable() const { return m_folderReadable; }
    bool    played() const { return m_played; }
    QString achievementsUser() const { return m_raUser; }
    QString achievementsState() const;
    QString achievementsMessage() const;
    bool    achievementsKey() const { return !m_raKey.isEmpty(); }
    bool    achievementsKeyRefused() const { return m_raKeyRefused; }
    bool    fbneoDownloading() const;
    int     fbneoProgress() const;
    QString fbneoMessage() const { return m_fbneoMessage; }

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

signals:
    void entriesReady(const QString &path);
    // What the filters hold may have changed: MAME has answered, the folder
    // isn't as it was, or Menu Filters has changed.
    void gamesChanged();
    void runningChanged();
    void scanningChanged();
    void readingChanged();
    void problemChanged();
    void playedChanged();
    void achievementsChanged();
    void leaderboardReady(const QString &set);
    void roomsChanged();
    void fbneoChanged();
    // reason: "ok", "failed", "stopped" or "failed_to_start" (ScriptLauncher's);
    // notice: what to tell the player when it isn't "ok", else "".
    void finished(int exitCode, const QString &reason, const QString &notice);
    void dynamicOptionsReady(const QString &key, const QVariant &options);

private:
    void applySetting(const QString &key, const QVariant &value);
    void setGamesFolder(const QString &path);
    void setProblem(const QString &problem);
    // What makes the list's own problem: no folder, no MAME, MAME not
    // answering.
    void checkProblem();

    // What refresh() reads, by value: the read may finish after this object
    // is gone.
    struct Read {
        QString folder;                          // the games folder read
        QHash<QString, QString> sets;            // as m_sets, m_misnamed and so on
        QSet<QString> misnamed;
        QSet<QString> folders;
        QHash<QString, QString> otherFolders;
        // games.json, when it was to be read (cacheAsked): what MAME said.
        bool cacheAsked = false;
        QString identity;
        QString build;
        QHash<QString, mame::Machine> machines;
        QSet<QString> unknown;
        // catver.ini, when it wasn't the one read before (genresRead).
        bool genresRead = false;
        QString genresFrom;
        QHash<QString, QString> genres;
        // The pictures: the games folder's snap by set, and the first MAME
        // took of each game.
        QHash<QString, QString> snaps;
        QHash<QString, QString> shots;
        // FinalBurn Neo's list of games, when it wasn't the one read before
        // (fbneoRead).
        bool fbneoRead = false;
        QString fbneoFrom;
        QHash<QString, QString> fbneoGames;
    };
    // Off the GUI thread: the games folder (made should it not be there),
    // games.json when withCache, catver.ini unless it is still genresFrom's,
    // FinalBurn Neo's list (dat) unless it is still datFrom's, and the
    // pictures.
    static Read read(const QString &folder, const QString &home, bool withCache, const QString &genresFrom,
                     const QString &dat, const QString &datFrom);
    void startRead();
    // What was read taken, the read begun again should the folder have
    // changed meanwhile; then, for refresh(), MAME asked about new sets.
    void onRead();
    // fn once the folder and games.json have been read: at once when they
    // have, for the settings' choices.
    void afterRead(std::function<void()> fn);
    void saveCache() const;
    // What MAME said of the sets, all forgotten: it is asked again.
    void forget();
    // MAME's program as it is: its path, size and time, which change with it.
    QString mameIdentity() const;
    void startScan();
    void scanNext();
    // The MAME asked about a batch stopped, for a game starting: the sets
    // it hadn't answered for are asked about first once the game has ended.
    void pauseScan();
    void takeMachines();
    void onScanFinished(int exitCode, QProcess::ExitStatus status);
    // Every set asked about: games.json saved, and the views told.
    void finishScan();
    // entriesReady for every folder asked for since the last time, and
    // gamesChanged().
    void emitShown();

    // A set under Not Recognized: a name that can't be a set's, one MAME has
    // no game of, or one MAME can't open by its name.
    bool isUnrecognized(const QString &set) const;
    // A set listed as a game: in the folder, a name MAME opens, and one of
    // its games (or one it wasn't asked about), not a BIOS or a device.
    bool isGame(const QString &set) const;
    // MAME's description of a set's machine, else the set's name; what it is
    // sorted by, and its title on the list.
    QString descriptionOf(const QString &set) const;
    QString sortKey(const QString &set) const;
    QString titleOf(const QString &set) const;

    // The folder's games (not BIOS sets, devices or sets MAME doesn't know)
    // by name, the genres, players, decades and makers they are under, and
    // the sets under Not Recognized: made again only once the folder or what
    // MAME says of it has changed (invalidate()), as a full set of MAME's
    // games is tens of thousands.
    struct Index {
        QStringList games;
        QHash<QString, QMap<QString, QStringList>> places;   // kind → place → its games
        // kind → its places in the order they are listed, with what each is
        // called: players by number, decades with the years unknown last,
        // the rest by name.
        QHash<QString, QList<std::pair<QString, QString>>> sorted;
        QStringList unknown;
    };
    const Index &index() const;
    void invalidate() { m_indexed = false; }
    // A game's place among the genres, players, decades or makers; "" for
    // none.
    QString placeOf(const QString &kind, const QString &set, const mame::Machine *machine) const;
    // The tree's folders of "players", "years" or "makers".
    QVariantList folders(const QString &kind) const;
    QVariantList gameEntries(const QStringList &sets) const;
    QVariantMap gameEntry(const QString &set) const;
    QVariantMap unknownEntry(const QString &set) const;
    QVariantList listEntries(const QString &list) const;
    // The filters of the genres, players, decades and publishers that Menu
    // Filters shows: [{ id, name, count }].
    QVariantList placeFilters() const;
    // Whether Menu Filters shows a filter: as turned on or off there, else
    // on, but a publisher's.
    bool menuShows(const QString &filter) const;
    // A filter's sets, in the order it lists them.
    QStringList filterSets(const QString &filter) const;
    // Why a set is under Not Recognized, and what to do.
    QString unknownDetail(const QString &set) const;
    // The games folder's snap, else MAME's own snapshot; "" for none. As
    // read with the folder (m_snaps, m_shots), till the list opens again.
    QString pictureOf(const QString &set) const;
    // The sets on one of the module's lists (AppCore's), newest first.
    QStringList listedSets(const QString &list) const;
    // The file name MAME opens a set by: its short name in lower case.
    QString mameFileName(const QString &set) const;
    // The games folder's bios, samples or snap folder, as it is named
    // there; "" for none.
    QString otherFolder(const QString &name) const;
    QString rompath() const;
    QString pluginsPath() const;
    // The folder of a MAME with its own files beside it (an upstream build,
    // GroovyMAME): bgfx, plugins, hash, ini. "" for one installed as the
    // system's (Debian's), whose mame.ini says where they are.
    QString ownFolder() const;
    QString noticeFor(int exitCode, const QString &reason) const;
    QString fbneoNotice(int exitCode, const QString &reason, const libretro::Run &run) const;
    // A game started in RetroArch with FinalBurn Neo, the checks play() makes
    // of every game made: alone, hosted online or joining a room.
    bool playFbneo(const QString &name, libretro::Launch::Netplay netplay = libretro::Launch::Netplay::None,
                   const QString &joinAddress = QString());
    // The Online Games folder's entries: the rooms, or a line saying why
    // there are none.
    QVariantList roomEntries() const;
    // RetroArch's folder in <data>/arcade, and its config and log there.
    QString retroarchHome() const { return m_home + QStringLiteral("/retroarch"); }
    // What a FinalBurn Neo game's run left: the token RetroArch swapped the
    // password for, or why it couldn't; the config, with the password or the
    // token in it, goes.
    void takeAchievements(const libretro::Run &run);
    // fbneoPlays() but for FinalBurn Neo being ready, asked once for many.
    bool fbneoHas(const QString &set) const;
    void saveAchievements() const;

    // How long each game has been played: playtime.json, read when first
    // needed.
    qint64 playTimeOf(const QString &set);
    void addPlayTime(const QString &set, qint64 seconds);

    // The app's startup favourite's set, while it is one of this module's
    // games; "".
    QString startupSet() const;
    // play_at_startup made what the app's startup favourite is.
    void syncStartupGame();

    QString m_appRoot;
    QString m_dataRoot;
    QString m_home;               // <data>/arcade: MAME's keeping
    AppCore *m_appCore = nullptr;
    ScriptLauncher *m_launcher = nullptr;

    // Settings.
    QString m_gamesFolder;
    bool    m_highScores = true;
    QString m_video;              // "" for MAME's own (mame.ini's)
    QVariantMap m_menuFilters;    // Menu Filters: filter id → shown
    bool    m_achievements = false;   // Online High Scores: RetroAchievements
    bool    m_onlinePlay = false;
    QVariantMap m_engines;        // set → "mame" or "fbneo", as its options chose
    QString m_onlineName;         // the name online play shows ("" for the account's)
    QString m_relay = QStringLiteral("nyc");   // the relay a hosted game goes through

    // The RetroAchievements account: <data>/arcade/retroachievements.json,
    // owner-only, but the password, which RetroArch has once and swaps.
    QString m_raUser;
    QString m_raToken;
    QString m_raKey;
    bool    m_raKeyRefused = false;
    QString m_raPassword;
    QString m_raFailure;
    bool    m_raPasswordGiven = false;   // the game playing was given it

    FbneoInstaller *m_installer = nullptr;
    RetroAchievements *m_leaderboards = nullptr;

    // The netplay lobby: its rooms as last read, why it couldn't be read
    // ("" when it was), whether it has been, and the read under way.
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_lobbyReply = nullptr;
    QUrl m_lobbyUrl{ QStringLiteral("http://lobby.libretro.com/list") };
    QList<libretro::Room> m_rooms;
    QString m_roomsProblem;
    bool    m_roomsRead = false;
    QString m_fbneoMessage;

    // The folder's sets, and what MAME said of every set it was asked about.
    QHash<QString, QString> m_sets;
    bool    m_listed = false;     // the folder has been read
    QSet<QString> m_folders;      // sets that are a folder of files
    QHash<QString, QString> m_otherFolders;   // "bios", "samples", "snap" → as named there
    QSet<QString> m_misnamed;     // sets whose file MAME can't open by its name
    QHash<QString, mame::Machine> m_machines;
    QSet<QString> m_unknown;      // sets MAME has no machine of
    QString m_identity;           // mameIdentity() the cache was made with
    QString m_build;              // MAME's build, as it says
    bool    m_cacheLoaded = false;
    QHash<QString, QString> m_genres;   // set → genre, catver.ini's
    QString m_genresFrom;         // the catver.ini read: its path, size and time
    QHash<QString, QString> m_snaps;    // set → its picture in the games folder's snap
    QHash<QString, QString> m_shots;    // set → the first snapshot MAME took of it
    QHash<QString, QString> m_fbneoGames;   // set → its description, FinalBurn Neo's
    QString m_fbneoFrom;          // the list of them read: its path, size and time
    mutable Index m_index;
    mutable bool  m_indexed = false;

    // The read under way (read()); another to follow it, the list having
    // opened meanwhile; MAME to be asked once it is done (refresh()), about
    // every set when Read the Games Again came meanwhile (rescan()); and what
    // waits for it (afterRead()).
    QFutureWatcher<Read> *m_watcher = nullptr;
    bool m_reading = false;
    bool m_readAgain = false;
    bool m_scanAfterRead = false;
    bool m_rescanAfterRead = false;
    QList<std::function<void()>> m_afterRead;

    // A -listxml run, a batch of sets at a time. The next waits for a game
    // that is playing to end.
    QProcess *m_scan = nullptr;
    QTimer   *m_scanTimer = nullptr;
    QStringList m_toAsk;
    QStringList m_asked;
    mame::ListXmlReader m_reader;
    bool m_scanFailed = false;

    // The tree's folders asked for since entriesReady was last sent for them.
    QSet<QString> m_shown;

    QHash<QString, qint64> m_playTime;   // set → seconds
    bool m_playTimeLoaded = false;

    QString m_problem;
    bool    m_folderReadable = true;
    QString m_lastError;
    QString m_playing;
    QString m_playingEngine;      // "mame" or "fbneo"
    QElapsedTimer m_playClock;    // since the game playing started
    bool    m_played = false;
};
