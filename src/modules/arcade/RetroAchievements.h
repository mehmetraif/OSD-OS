#pragma once
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// What the Arcade module reads of RetroAchievements: its Web API, with the
// account's Web API key, and its media server, over HTTPS (Qt's own, not
// RetroArch's). A game is found by its hash (libretro::achievementsHash() of
// its file) in RetroAchievements' list of arcade games, which is big and
// changes seldom, so it is kept a week in
// <data>/arcade/retroachievements-games.json (hasGame(), those with
// achievements or leaderboards: the Arcade screen's RetroAchievements filter;
// knowsGame(), every one). Of a game then come:
// - its leaderboards, the one of high scores among them, and that one's
//   first entries, kept ten minutes (board(): the Arcade screen's);
// - its achievements, and which of them the account has earned, kept five
//   minutes (achievements(): its Game Details');
// - where its pictures are on the media server (findPictures(): the Arcade
//   screen's, which ArcadeBackend downloads with media()).
// One request is under way at a time, achievements first, and of each only
// the game last asked for is read, a moment after it was: a list scrolled
// through asks for none of the games passed. A game's pictures asked for by
// themselves (findPicturesLater()) come after all of those. The files of
// the media server, the games' pictures and the achievements' badges, come
// through media().
class RetroAchievements : public QObject {
    Q_OBJECT
public:
    static const QUrl kApi;     // https://retroachievements.org/API/
    static const QUrl kMedia;   // https://media.retroachievements.org/

    // home: where the list of games is kept. api, media:
    // kApi and kMedia, or a stand-in's.
    RetroAchievements(const QString &home, const QUrl &api, const QUrl &media, QObject *parent = nullptr);

    // The Web API's and the media server's addresses: a stand-in's.
    void setServers(const QUrl &api, const QUrl &media) { m_api = api; m_media = media; }
    // The account's key; "" for none. Another forgets what the last read.
    void setKey(const QString &key);
    // The key asked about at once, with the Web API's smallest answer (its
    // consoles), unless an answer already came with it: keyAccepted() or
    // keyRefused().
    void checkKey();
    // An answer came with the key, and none refused it since.
    bool keyWorks() const { return m_keyAccepted; }
    // The account's user, whose achievements achievements() says were
    // earned; "" for none. Another forgets the achievements read.
    void setUser(const QString &user);

    // A game's leaderboard, by its file ("sf2.zip"): { state, title, entries }.
    // state: "loading" (asked for, boardReady() to come), "ready" (entries,
    // the first three as { rank ("1ST"…), score, name }, none for a board no
    // one has a score on yet; title, the leaderboard's), "none"
    // (RetroAchievements has no leaderboard of the game), or "failed" (it
    // couldn't be read: no key, no connection, the key refused).
    QVariantMap board(const QString &fileName);

    // A game's achievements, by its file: { state, achievements, progress }.
    // state as a board's, achievementsReady() to come while it is "loading",
    // and "none" for a game RetroAchievements has no achievements of (at
    // once for one its list of games hasn't, or has without achievements,
    // while that list is fresh).
    // achievements: [{ id, title, description, points, badge (its name, ""
    // for none), earned (the day, yyyy-MM-dd; "" for one not earned),
    // hardcore (earned in hardcore mode) }], in RetroAchievements' order.
    // progress: earned says what the user has earned; false without a user,
    // when none reads as earned.
    QVariantMap achievements(const QString &fileName);
    // A game's achievements read again when next asked for: it was played.
    void forgetAchievements(const QString &fileName);

    // The pictures of the game of the first of games that RetroAchievements
    // has a game of (a set's file, then its parent's; one whose game a file
    // after it has too goes by that file, the pictures being that game's):
    // its title screen, a screenshot of it in play and its box art, as paths
    // on the media server ("/Images/012345.png"), its placeholders left out.
    // picturesFound() once read; for a file in looked (its pictures looked
    // for before) at once, "looked", nothing read. Only the last asked for
    // is read, a moment after it was.
    void findPictures(const QStringList &games, const QStringList &looked);
    // The same for a game asked for by itself rather than shown (Read the
    // Games Again's): read once nothing else is wanted, at once rather than
    // a moment after, and neither taking the place of the game shown nor
    // taken over by it. Another asked for in its turn takes its place. A
    // read stopped for another key is read again with it.
    void findPicturesLater(const QStringList &games, const QStringList &looked);
    // The game asked for so is yet to be read, or being read: until its
    // pictures are found (picturesFound()), or it is done without them (its
    // pictures looked for already, none of the games RetroAchievements', or
    // no list of games to find it in).
    bool laterPending() const { return !m_laterPictures.isEmpty() || !m_laterRead.isEmpty(); }
    // A file of the media server's (a path findPictures() gave), downloaded
    // after those asked for before it: done with what it holds, a PNG or a
    // JPEG (a badge of 512 KB at most, a picture of 4 MB); empty when it
    // couldn't be downloaded or is neither.
    void media(const QString &path, const std::function<void(const QByteArray &data)> &done);

    // The list of games wanted: the one kept read in at once, and one a week
    // old, or none, read again a moment after, as the key allows
    // (listRead()); one a week old that couldn't be read again is used as it
    // is until it is tried again.
    void wantList();
    // Whether RetroAchievements has achievements or leaderboards of a game,
    // by its file ("sf2.zip"), as the list read in says (wantList()).
    bool hasGame(const QString &fileName) const;
    // Whether it has the game at all, one with neither too: its pictures.
    bool knowsGame(const QString &fileName) const { return gameOf(fileName) > 0; }
    // The file whose game is read for a set's pictures, of its own and its
    // parent's (findPictures()'s games, the set's first): the last of them
    // with the first game known among them, so a clone's parent's where
    // both are one game there; "" when it knows none of them.
    QString pictureGame(const QStringList &fileNames) const;
    // The list wanted is still to be read: a moment from now, or under way.
    bool listPending() const { return m_wantedList; }
    // A list is kept, read now or before: one to look games up in at all.
    bool hasList() const { return !m_games.isEmpty(); }
    // None is kept, and the last read of one failed (off line, say): the
    // next is a while off (readList()), and nothing wanted meanwhile can be
    // found.
    bool listFailed() const {
        return m_games.isEmpty() && m_listRetry.isValid() && m_listRetry > QDateTime::currentDateTimeUtc();
    }

signals:
    void boardReady(const QString &fileName);
    void achievementsReady(const QString &fileName);
    // state: "found" (paths; none for a game without pictures), "unknown"
    // (none of the games is RetroAchievements'; fileName, the first),
    // "looked" (its pictures were looked for before: nothing read) or
    // "failed" (the game couldn't be read); fileName, the file whose game it
    // is.
    void picturesFound(const QString &fileName, const QString &state, const QStringList &paths);
    // RetroAchievements refused the key (HTTP 401, or a 403 in its own
    // words; not its guard's 403 page, nor a 429 asking for a pause, after
    // which the request is asked again); or took it, the first of its
    // answers to the key come.
    void keyRefused();
    void keyAccepted();
    // The list of games was read: which games RetroAchievements has may have
    // changed.
    void listRead();

private:
    struct Board {
        QString state;
        QString title;
        QVariantList entries;
        QDateTime read;
    };
    struct Progress {
        QString state;
        QVariantList achievements;
        QDateTime read;
    };
    struct Media {
        QString path;
        std::function<void(const QByteArray &)> done;
        int tried = 0;   // how many times the server asked for a pause over it
    };
    using Answer = std::function<void(const QByteArray &json, bool ok)>;

    // What is wanted read now: achievements, then a board, then pictures.
    void readWanted();
    bool wanting() const;
    // Every game wanted can't be read: there is no list of games to find it
    // in.
    void failWanted();
    // A game's id, by its file's hash: 0 for none.
    int gameOf(const QString &fileName) const;
    void readList();
    void readBoards(const QString &fileName, int game);
    void readEntries(const QString &fileName, int leaderboard, const QString &title);
    void readAchievements(const QString &fileName, int game);
    // later: findPicturesLater()'s game (m_laterRead).
    void readPictures(const QString &fileName, int game, bool later);
    // tried: how many times the server has asked for a pause over this
    // request, which is asked again after each, kMostTries at most.
    void get(const QString &endpoint, const QList<std::pair<QString, QString>> &query, const Answer &answer,
             int tried = 0);
    // A pause the server asked for (429, 503) is still on.
    bool pausing() const;
    // A game's board read (or not): kept, and the next game asked for read.
    void done(const QString &fileName, const Board &board);
    void doneAchievements(const QString &fileName, const Progress &progress);
    // Read recently enough to show as it is.
    bool fresh(const Board &board) const;
    bool fresh(const Progress &progress) const;
    bool listFresh() const;
    void loadList();
    void nextMedia();

    QString m_home;
    QUrl m_api;
    QUrl m_media;
    QString m_key;
    bool    m_keyAccepted = false;      // an answer came with it
    bool    m_keyCheck = false;         // checkKey() asked, to be read first
    QString m_user;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_wait = nullptr;
    QString m_wanted;                   // the board last asked for
    QString m_wantedAchievements;       // the achievements last asked for
    QStringList m_wantedPictures;       // the games whose pictures were last asked for
    QStringList m_lookedPictures;       // of them, those looked for before
    QStringList m_laterPictures;        // findPicturesLater()'s, read after all the rest
    QStringList m_laterLooked;
    QStringList m_laterRead;            // findPicturesLater()'s being read, and its looked
    QStringList m_laterReadLooked;
    bool m_wantedList = false;          // the list itself (wantList())
    QHash<QString, Board> m_boards;     // by the game's file
    QHash<QString, Progress> m_progress;    // by the game's file
    QHash<QString, int> m_games;        // a hash → its game's id
    mutable QHash<QString, int> m_gameIds;  // a file → its game's id, as gameOf() found it in m_games
    QSet<int> m_achievementGames;       // the games with achievements
    QSet<int> m_boardGames;             // the games with leaderboards
    QDateTime m_listRead;               // when the list was read
    QDateTime m_listRetry;              // after a list that couldn't be read, when to try again
    bool m_listLoaded = false;          // the list kept has been looked for
    // The media server's files to download, and the one under way.
    QList<Media> m_mediaQueue;
    QNetworkReply *m_mediaReply = nullptr;
    // A pause the Web API or the media server asked for: until when, and
    // whether a request of the Web API's waits it out to be asked again.
    QDateTime m_pausedUntil;
    bool m_pausedRequest = false;
};
