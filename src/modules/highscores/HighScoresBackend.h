#pragma once
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <functional>
#include "ScoreTable.h"

template <typename T> class QFutureWatcher;

// The High Scores module: the high score tables of the games played in the
// Arcade module, as MAME keeps them, in <data>/arcade:
//
// - hiscore/<set>.hi, the hiscore plugin's (Arcade's High Scores setting),
//   for the games that forget their table when they are switched off;
// - nvram/<set>/, the games' own NVRAM, for those that keep it there.
//
// They are raw memory, a layout each game of its own. openhi2txt
// (thirdparty/openhi2txt) reads them through hi2txt's game definitions
// (modules/highscores/data/hi2txt.zip), with MAME's hiscore.dat to tell a
// game's versions apart. A game's files are read again only once they have
// changed (MAME writes them as a game ends): what was read is kept in
// <data>/highscores/scores.json, and a game's name is the Arcade module's
// (arcade/games.json). The reading is done on a worker thread.
//
// The tree (requestEntries): Recently Played, in the Arcade module's own
// order, All Games, a game's table (or its tables, a folder each) as lines
// of text, and Not Readable for the files hi2txt has no description of, or
// can't make out.
class HighScoresBackend : public QObject {
    Q_OBJECT
    // The score files are being read: entriesReady follows for every folder
    // asked for meanwhile.
    Q_PROPERTY(bool reading READ reading NOTIFY readingChanged)
    // What keeps the tables from being read, for the module's screens: a
    // build without openhi2txt, missing definitions. "" when nothing does.
    Q_PROPERTY(QString problem READ problem CONSTANT)
public:
    // recentSets: the sets the Arcade module played last, newest first, for
    // Recently Played (main.cpp hands over ArcadeBackend::recentSets). With
    // none, there is no Recently Played.
    explicit HighScoresBackend(const QString &appRoot, const QString &dataRoot,
                               std::function<QStringList()> recentSets = {}, QObject *parent = nullptr);
    ~HighScoresBackend() override;

    // Looks at MAME's score files again (each time the list opens), and
    // reads those new or changed since.
    Q_INVOKABLE void refresh();
    // A folder of the tree ("top" its top), or undefined while the files are
    // read for the first time: entriesReady(path) follows. A game is { name,
    // path ("game:" and its set), isFolder, game (its set), detail (its best
    // entry, its fields, its file) }; a table's line, or a field of a game
    // with no table, is { name (the line), path, isFolder: false, kind:
    // "row", detail (its cells by column) }; a file that can't be read is
    // { name, path ("unreadable:" and the set), kind: "unreadable", detail
    // (why, and the file) }.
    Q_INVOKABLE QVariant requestEntries(const QString &path);

    bool    reading() const { return m_reading; }
    QString problem() const { return m_problem; }

    // The score files of a game, as MAME left them: its set, the file to
    // show, and what its files are (scores::Game::stamp).
    struct Source {
        QString set;
        QString file;
        QString stamp;
    };
    // What a read hands back to the main thread.
    struct Read {
        QList<scores::Game> games;
        QHash<QString, QString> names;   // set → its name, from arcade/games.json
    };

signals:
    void entriesReady(const QString &path);
    void readingChanged();

private:
    QHash<QString, Source> listSources() const;
    // openhi2txt's MAME folder: links to the Arcade module's hiscore and
    // nvram, and to MAME's hiscore.dat.
    void prepareRoot() const;
    // What the games are read with, the definitions and hiscore.dat, and
    // how what is read is kept: when it changes, every game is read again.
    QString definitionsIdentity() const;
    QString gamesJsonIdentity() const;
    void loadCache();
    void saveCache() const;
    void onRead();
    void emitShown();

    QList<const scores::Game *> recentGames() const;
    QString nameOf(const QString &set) const;
    QVariantMap gameEntry(const scores::Game &game) const;
    QVariantList rowEntries(const QString &set, const scores::Table &table, const QString &key) const;

    QString m_appRoot;
    QString m_dataRoot;
    QString m_arcade;        // <data>/arcade: MAME's, as the Arcade module runs it
    QString m_home;          // <data>/highscores
    QString m_definitions;   // hi2txt.zip
    QString m_problem;
    std::function<QStringList()> m_recentSets;

    QHash<QString, scores::Game> m_games;
    QHash<QString, QString> m_names;
    QString m_identity;      // definitionsIdentity() the games were read with
    QString m_readIdentity;  // the one the read running uses
    QString m_namesFrom;     // gamesJsonIdentity() the names were taken from
    bool m_loaded = false;
    bool m_reading = false;
    bool m_again = false;    // a refresh asked for while reading
    QFutureWatcher<Read> *m_watcher = nullptr;

    // The folders asked for since entriesReady was last sent for them.
    QSet<QString> m_shown;
};
