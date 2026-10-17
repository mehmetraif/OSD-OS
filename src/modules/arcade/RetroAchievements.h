#pragma once
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// The leaderboards on the Arcade screen: RetroAchievements' Web API, read
// with the account's Web API key, over HTTPS (Qt's own, not RetroArch's).
// A game is found by its hash (libretro::achievementsHash() of its file) in
// RetroAchievements' list of arcade games, which is big and changes seldom,
// so it is kept a week in <data>/arcade/retroachievements-games.json; then
// come its leaderboards, the one of high scores among them, and that one's
// first entries, kept ten minutes. Only the game last asked for is read, a
// moment after it was: a list scrolled through asks for none of the games
// passed.
class RetroAchievements : public QObject {
    Q_OBJECT
public:
    static const QUrl kApi;     // https://retroachievements.org/API/

    // home: where the list of games is kept. api: kApi, or a stand-in's.
    RetroAchievements(const QString &home, const QUrl &api, QObject *parent = nullptr);

    // The account's key; "" for none. Another forgets what the last read.
    void setKey(const QString &key);

    // A game's leaderboard, by its file ("sf2.zip"): { state, title, entries }.
    // state: "loading" (asked for, boardReady() to come), "ready" (entries,
    // the first three as { rank ("1ST"…), score, name }, none for a board no
    // one has a score on yet; title, the leaderboard's), "none"
    // (RetroAchievements has no leaderboard of the game), or "failed" (it
    // couldn't be read: no key, no connection, the key refused).
    QVariantMap board(const QString &fileName);

signals:
    void boardReady(const QString &fileName);
    // RetroAchievements refused the key (HTTP 401 or 403).
    void keyRefused();

private:
    struct Board {
        QString state;
        QString title;
        QVariantList entries;
        QDateTime read;
    };
    using Answer = std::function<void(const QByteArray &json, bool ok)>;

    // The game last asked for, read now.
    void readWanted();
    void readList();
    void readBoards(const QString &fileName, int game);
    void readEntries(const QString &fileName, int leaderboard, const QString &title);
    void get(const QString &endpoint, const QList<std::pair<QString, QString>> &query, const Answer &answer);
    // A game's board read (or not): kept, and the next game asked for read.
    void done(const QString &fileName, const Board &board);
    // A board read recently enough to show as it is.
    bool fresh(const Board &board) const;
    bool listFresh() const;
    void loadList();

    QString m_home;
    QUrl m_api;
    QString m_key;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_wait = nullptr;
    QString m_wanted;               // the game last asked for
    QHash<QString, Board> m_boards; // by the game's file
    QHash<QString, int> m_games;    // a hash → its game's id, for games with leaderboards
    QDateTime m_listRead;           // when the list was read
    QDateTime m_listRetry;          // after a list that couldn't be read, when to try again
    bool m_listLoaded = false;      // the list kept has been looked for
};
