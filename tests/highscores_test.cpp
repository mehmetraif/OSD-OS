#include "modules/highscores/HighScoresBackend.h"
#include "modules/highscores/ScoreTable.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// The High Scores module on MAME's files of four games (tests/data/highscores,
// see its README): Final Fight's and Ghosts'n Goblins' from the hiscore
// plugin, Tetris The Grand Master 2's EEPROM and Solar Fox's NVRAM, read by
// openhi2txt with hi2txt's definitions (modules/highscores/data/hi2txt.zip)
// and the plugin's hiscore.dat beside a stand-in for MAME. The tables as
// lines and what they say of an entry, Recently Played in the Arcade
// module's order, the names from its games.json, the files that can't be
// read and the NVRAM that holds no table, and a game read again only once
// its files have changed.
class HighScoresTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_data;
    QString m_arcade;    // <data>/arcade, MAME's folders as the Arcade module runs it
    QString m_dat;       // the stand-in MAME's hiscore.dat
    QDateTime m_time;    // when Final Fight was played, a day ago
    // The Arcade module's Recently Played, newest first.
    QStringList m_played = { QStringLiteral("ffight"), QStringLiteral("nothere"), QStringLiteral("tgm2"),
                             QStringLiteral("gng") };

    static QString sample(const QString &path) {
        return QStringLiteral(APP_DIR "/tests/data/highscores/") + path;
    }

    std::function<QStringList()> played() {
        return [this] { return m_played; };
    }

    void writeFile(const QString &path, const QByteArray &data) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
        f.write(data);
    }

    void copySample(const QString &from, const QString &to) {
        QFile f(sample(from));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.fileName()));
        writeFile(to, f.readAll());
    }

    static void setTime(const QString &path, const QDateTime &time) {
        QFile f(path);
        QVERIFY2(f.open(QIODevice::ReadWrite), qPrintable(path));
        QVERIFY(f.setFileTime(time, QFileDevice::FileModificationTime));
    }

    void writeGamesJson(const QString &gng) {
        writeFile(m_arcade + QStringLiteral("/games.json"),
                  "{\"mame\":\"x\",\"build\":\"0.276 (test)\",\"machines\":{"
                  "\"ffight\":{\"description\":\"Final Fight (World, set 1)\"},"
                  "\"gng\":{\"description\":\"" + gng.toUtf8() + "\"},"
                  "\"solarfox\":{\"description\":\"Solar Fox (upright)\"},"
                  "\"tgm2\":{\"description\":\"Tetris the Absolute The Grand Master 2\"}},"
                  "\"unknown\":[]}");
    }

    static QStringList names(const QVariant &entries) {
        QStringList list;
        const QVariantList all = entries.toList();
        for (const QVariant &v : all)
            list << v.toMap().value(QStringLiteral("name")).toString();
        return list;
    }

    static QVariantMap entry(const QVariant &entries, const QString &name) {
        const QVariantList all = entries.toList();
        for (const QVariant &v : all) {
            if (v.toMap().value(QStringLiteral("name")).toString() == name)
                return v.toMap();
        }
        return {};
    }

    // Looked at the files again, and done reading those that changed.
    static void refreshed(HighScoresBackend &scores) {
        scores.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!scores.reading(), 10000);
    }

    // What is kept is as the files are now, so that a test sees what its
    // own refresh reads, whichever test ran before it.
    void caughtUp() {
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        refreshed(scores);
    }

    static QStringList allGames() {
        return { QStringLiteral("Final Fight (World, set 1)"), QStringLiteral("Ghosts'n Goblins (World? set 1)"),
                 QStringLiteral("Solar Fox (upright)"), QStringLiteral("Tetris the Absolute The Grand Master 2") };
    }

private slots:
    void initTestCase() {
        QVERIFY(m_data.isValid());
        m_arcade = m_data.path() + QStringLiteral("/arcade");
        copySample(QStringLiteral("hiscore/ffight.hi"), m_arcade + QStringLiteral("/hiscore/ffight.hi"));
        copySample(QStringLiteral("hiscore/gng.hi"), m_arcade + QStringLiteral("/hiscore/gng.hi"));
        copySample(QStringLiteral("nvram/tgm2/eeprom"), m_arcade + QStringLiteral("/nvram/tgm2/eeprom"));
        copySample(QStringLiteral("nvram/solarfox/nvram"), m_arcade + QStringLiteral("/nvram/solarfox/nvram"));
        // A game hi2txt has no description of, and Final Fight's file as
        // another version of the game would leave it.
        writeFile(m_arcade + QStringLiteral("/hiscore/zzzunknown.hi"), "junk");
        QFile ffight(sample(QStringLiteral("hiscore/ffight.hi")));
        QVERIFY(ffight.open(QIODevice::ReadOnly));
        writeFile(m_arcade + QStringLiteral("/hiscore/ffightu.hi"), ffight.read(10));
        // NVRAM with settings in it, not scores: Pac-Man's are the hiscore
        // plugin's, and hi2txt has nothing of the other game.
        writeFile(m_arcade + QStringLiteral("/nvram/pacman/nvram"), "settings");
        writeFile(m_arcade + QStringLiteral("/nvram/zzznvram/nvram"), "settings");
        writeGamesJson(QStringLiteral("Ghosts'n Goblins (World? set 1)"));

        // MAME, where the module looks first, and its hiscore plugin where
        // Debian's is beside /usr/games.
        const QString mame = m_data.path() + QStringLiteral("/bin/mame");
        writeFile(mame, "#!/bin/sh\nexit 0\n");
        QVERIFY(QFile::setPermissions(mame, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString plugin = m_data.path() + QStringLiteral("/share/games/mame/plugins/hiscore");
        writeFile(plugin + QStringLiteral("/init.lua"), QByteArray());
        m_dat = plugin + QStringLiteral("/hiscore.dat");
        copySample(QStringLiteral("hiscore.dat"), m_dat);
        // And Pac-Man's table where no layout of hi2txt's has it, as a newer
        // MAME may: its NVRAM above still isn't a table that can't be read.
        QFile dat(m_dat);
        QVERIFY(dat.open(QIODevice::Append));
        dat.write("pacman:\n@:maincpu,program,4c00,10,00,00\n\n");

        m_time = QDateTime::currentDateTime().addDays(-1);
        setTime(m_arcade + QStringLiteral("/hiscore/ffight.hi"), m_time);
    }

    void helpers() {
        scores::Table table;
        table.id = QStringLiteral("TOP_SCORES");
        table.columns = { QStringLiteral("RANK"), QStringLiteral("SCORE"), QStringLiteral("NAME"),
                          QStringLiteral("UNUSED") };
        table.rows = { { QStringLiteral("1"), QStringLiteral("116,370"), QStringLiteral("ACD"), QString() },
                       { QStringLiteral("10"), QStringLiteral(" 5,000 "), QStringLiteral(" .Z"), QStringLiteral(" ") },
                       { QStringLiteral("2"), QStringLiteral("89,760"), QStringLiteral("BOB  ") } };
        // Numbers to the right, names to the left with their own leading
        // space, a column blank in every row left out.
        QCOMPARE(scores::lines(table), QStringList({ QStringLiteral(" 1  116,370  ACD"),
                                                     QStringLiteral("10    5,000   .Z"),
                                                     QStringLiteral(" 2   89,760  BOB") }));
        QCOMPARE(scores::describeRow(table, 1),
                 QStringList({ QStringLiteral("RANK 10"), QStringLiteral("SCORE 5,000"), QStringLiteral("NAME .Z") }));
        QVERIFY(scores::describeRow(table, 3).isEmpty());
        QCOMPARE(scores::label(table.id), QStringLiteral("TOP SCORES"));
        QCOMPARE(scores::label(QStringLiteral("VS CPU MODE")), QStringLiteral("VS CPU MODE"));

        // A time is a number; a class with a letter in it isn't.
        scores::Table times;
        times.columns = { QStringLiteral("CLASS"), QStringLiteral("TIME") };
        times.rows = { { QStringLiteral("S1"), QStringLiteral("20:00:00") }, { QStringLiteral("9"), QStringLiteral("1:30:00") } };
        QCOMPARE(scores::lines(times), QStringList({ QStringLiteral("S1  20:00:00"), QStringLiteral("9    1:30:00") }));

        scores::Game game;
        game.set = QStringLiteral("ffight");
        game.file = QStringLiteral("ffight.hi");
        game.stamp = QStringLiteral("ffight.hi:64:1234");
        game.tables = { table };
        game.fields = { qMakePair(QStringLiteral("TOP SCORE"), QStringLiteral("116,370")) };
        const scores::Game back = scores::Game::fromVariant(game.toVariant());
        QCOMPARE(back.file, game.file);
        QCOMPARE(back.stamp, game.stamp);
        QCOMPARE(back.tables.size(), 1);
        QCOMPARE(back.tables.first().id, table.id);
        QCOMPARE(back.tables.first().columns, table.columns);
        QCOMPARE(back.tables.first().rows, table.rows);
        QCOMPARE(back.fields, game.fields);
        QVERIFY(back.error.isEmpty());
        QVERIFY(!back.hidden);
        scores::Game hidden;
        hidden.hidden = true;
        hidden.error = QStringLiteral("why");
        QVERIFY(scores::Game::fromVariant(hidden.toVariant()).hidden);
        QCOMPARE(scores::Game::fromVariant(hidden.toVariant()).error, QStringLiteral("why"));
    }

    // Asked before the first read is done: nothing yet, then entriesReady.
    void firstRead() {
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        QVERIFY2(scores.problem().isEmpty(), qPrintable(scores.problem()));
        QSignalSpy ready(&scores, &HighScoresBackend::entriesReady);
        scores.refresh();
        QVERIFY(scores.reading());
        QVERIFY(!scores.requestEntries(QStringLiteral("top")).isValid());
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 10000);
        QCOMPARE(ready.first().first().toString(), QStringLiteral("top"));
        QVERIFY(!scores.reading());
        QCOMPARE(names(scores.requestEntries(QStringLiteral("top"))),
                 QStringList({ QStringLiteral("Recently Played"), QStringLiteral("All Games"),
                               QStringLiteral("Not Readable") }));
        // MAME's folders for openhi2txt: the Arcade module's, and the
        // plugin's hiscore.dat.
        const QString root = m_data.path() + QStringLiteral("/highscores/mame");
        QCOMPARE(QFileInfo(root + QStringLiteral("/hiscore")).symLinkTarget(), m_arcade + QStringLiteral("/hiscore"));
        QCOMPARE(QFileInfo(root + QStringLiteral("/plugins/hiscore/hiscore.dat")).symLinkTarget(), m_dat);
        QVERIFY(QFileInfo::exists(m_data.path() + QStringLiteral("/highscores/scores.json")));
    }

    void tables() {
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        refreshed(scores);
        const QVariant all = scores.requestEntries(QStringLiteral("all"));
        QCOMPARE(names(all), allGames());
        // A game: its best entry and its file.
        const QVariantMap ffight = entry(all, QStringLiteral("Final Fight (World, set 1)"));
        QCOMPARE(ffight.value(QStringLiteral("path")).toString(), QStringLiteral("game:ffight"));
        QVERIFY(ffight.value(QStringLiteral("isFolder")).toBool());
        QCOMPARE(ffight.value(QStringLiteral("detail")).toString(), QStringLiteral("1 116,370 ACD\nffight.hi"));
        QCOMPARE(entry(all, QStringLiteral("Tetris the Absolute The Grand Master 2")).value(QStringLiteral("detail")).toString(),
                 QStringLiteral("1 T.A 9 01:30:00\nnvram/tgm2/eeprom"));

        // One table: its lines, each entry's cells by column.
        const QVariant rows = scores.requestEntries(QStringLiteral("game:ffight"));
        QCOMPARE(names(rows), QStringList({ QStringLiteral("1  116,370  ACD"), QStringLiteral("2   89,760   .Z"),
                                            QStringLiteral("3   20,000  NIN"), QStringLiteral("4   17,000  CAP"),
                                            QStringLiteral("5   16,000  CAP") }));
        const QVariantMap second = rows.toList().at(1).toMap();
        QCOMPARE(second.value(QStringLiteral("kind")).toString(), QStringLiteral("row"));
        QVERIFY(!second.value(QStringLiteral("isFolder")).toBool());
        QCOMPARE(second.value(QStringLiteral("detail")).toString(), QStringLiteral("RANK 2\nSCORE 89,760\nNAME .Z"));

        // Ghosts'n Goblins' definition has two layouts of its file's size:
        // hiscore.dat's block picks the one that puts 17,200 first. Ten
        // entries: the ranks take two places.
        QCOMPARE(names(scores.requestEntries(QStringLiteral("game:gng"))).first(), QStringLiteral(" 1  17,200  *&?"));

        // Several tables: a folder each.
        const QVariant tgm2 = scores.requestEntries(QStringLiteral("game:tgm2"));
        QCOMPARE(names(tgm2), QStringList({ QStringLiteral("TOP SECTION TIMES"), QStringLiteral("GRANDMASTER TOP"),
                                            QStringLiteral("NORMAL TOP"), QStringLiteral("DOUBLES TOP") }));
        const QVariantMap grandmaster = tgm2.toList().at(1).toMap();
        QCOMPARE(grandmaster.value(QStringLiteral("path")).toString(), QStringLiteral("table:tgm2:1"));
        QCOMPARE(grandmaster.value(QStringLiteral("detail")).toString(), QStringLiteral("1 T.A S1 20:00:00"));
        const QVariant lines = scores.requestEntries(QStringLiteral("table:tgm2:1"));
        QCOMPARE(names(lines), QStringList({ QStringLiteral("1  T.A  S1  20:00:00"),
                                             QStringLiteral("2  CFJ  2   06:27:96  AC3 ST2 SK2 CO1"),
                                             QStringLiteral("3  CFG  3   05:06:06  AC2 ST1 SK1 RE1 CO1") }));
        QCOMPARE(lines.toList().first().toMap().value(QStringLiteral("detail")).toString(),
                 QStringLiteral("RANK 1\nGRANDMASTER NAME T.A\nGRANDMASTER CLASS S1\nGRANDMASTER TIME 20:00:00"));
        QVERIFY(scores.requestEntries(QStringLiteral("table:tgm2:9")).toList().isEmpty());
        QVERIFY(scores.requestEntries(QStringLiteral("table:tgm2:x")).toList().isEmpty());
        QVERIFY(scores.requestEntries(QStringLiteral("game:nothing")).toList().isEmpty());

        // No table, a best score alone: its fields, lined up.
        const QVariant solarfox = scores.requestEntries(QStringLiteral("game:solarfox"));
        QCOMPARE(names(solarfox), QStringList({ QStringLiteral("SCORE  15,430"), QStringLiteral("NAME   IVQ") }));
        QCOMPARE(solarfox.toList().first().toMap().value(QStringLiteral("detail")).toString(), QStringLiteral("SCORE 15,430"));
        QCOMPARE(entry(all, QStringLiteral("Solar Fox (upright)")).value(QStringLiteral("detail")).toString(),
                 QStringLiteral("SCORE 15,430\nNAME IVQ\nnvram/solarfox/nvram"));
    }

    // A game's best for the Arcade screen: the first table's first rows as
    // rank, score and name; a best score kept on its own as the first.
    void topEntries() {
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        refreshed(scores);
        const auto row = [](const char *rank, const char *score, const char *name) {
            return QVariant(QVariantMap{ { QStringLiteral("rank"), QLatin1String(rank) },
                                         { QStringLiteral("score"), QLatin1String(score) },
                                         { QStringLiteral("name"), QLatin1String(name) } });
        };
        QCOMPARE(scores.topEntries(QStringLiteral("ffight"), 3),
                 QVariantList({ row("1ST", "116,370", "ACD"), row("2ND", "89,760", ".Z"), row("3RD", "20,000", "NIN") }));
        QCOMPARE(scores.topEntries(QStringLiteral("ffight"), 9).size(), 5);
        QCOMPARE(scores.topEntries(QStringLiteral("gng"), 1), QVariantList({ row("1ST", "17,200", "*&?") }));
        QCOMPARE(scores.topEntries(QStringLiteral("solarfox"), 3), QVariantList({ row("1ST", "15,430", "IVQ") }));
        // None: a game with no table read, one that couldn't be, none asked.
        QVERIFY(scores.topEntries(QStringLiteral("nothing"), 3).isEmpty());
        QVERIFY(scores.topEntries(QStringLiteral("ffightu"), 3).isEmpty());
        QVERIFY(scores.topEntries(QStringLiteral("ffight"), 0).isEmpty());
    }

    // The files hi2txt can't read, and why; not NVRAM with settings in it.
    void notReadable() {
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        refreshed(scores);
        const QVariant unreadable = scores.requestEntries(QStringLiteral("unreadable"));
        QCOMPARE(names(unreadable), QStringList({ QStringLiteral("ffightu"), QStringLiteral("zzzunknown") }));
        const QVariantMap ffightu = unreadable.toList().first().toMap();
        QCOMPARE(ffightu.value(QStringLiteral("kind")).toString(), QStringLiteral("unreadable"));
        QVERIFY(!ffightu.value(QStringLiteral("isFolder")).toBool());
        QCOMPARE(ffightu.value(QStringLiteral("detail")).toString(),
                 QStringLiteral("The file isn't laid out as hi2txt knows this game's: another version of it?\nffightu.hi"));
        QCOMPARE(unreadable.toList().last().toMap().value(QStringLiteral("detail")).toString(),
                 QStringLiteral("hi2txt has no description of this game's high scores yet\nzzzunknown.hi"));
        const QStringList all = names(scores.requestEntries(QStringLiteral("all"))) + names(unreadable);
        QVERIFY(!all.contains(QStringLiteral("pacman")));
        QVERIFY(!all.contains(QStringLiteral("zzznvram")));
    }

    // The Arcade module's Recently Played, the games of it with a table;
    // none without it.
    void recentlyPlayed() {
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        refreshed(scores);
        QCOMPARE(names(scores.requestEntries(QStringLiteral("recent"))),
                 QStringList({ QStringLiteral("Final Fight (World, set 1)"),
                               QStringLiteral("Tetris the Absolute The Grand Master 2"),
                               QStringLiteral("Ghosts'n Goblins (World? set 1)") }));
        HighScoresBackend alone(QStringLiteral(APP_DIR), m_data.path());
        refreshed(alone);
        QVERIFY(!names(alone.requestEntries(QStringLiteral("top"))).contains(QStringLiteral("Recently Played")));
        QVERIFY(alone.requestEntries(QStringLiteral("recent")).toList().isEmpty());
    }

    // What was read is kept: a run that finds nothing changed reads nothing,
    // and a file is read again once it changes.
    void readAgainWhenChanged() {
        caughtUp();
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        QSignalSpy reading(&scores, &HighScoresBackend::readingChanged);
        QSignalSpy ready(&scores, &HighScoresBackend::entriesReady);
        QCOMPARE(names(scores.requestEntries(QStringLiteral("top"))).size(), 0);
        scores.refresh();
        QVERIFY(!scores.reading());
        QCOMPARE(reading.size(), 0);
        // The folder asked for is listed again all the same.
        QCOMPARE(ready.size(), 1);
        QCOMPARE(names(scores.requestEntries(QStringLiteral("game:ffight"))).first(), QStringLiteral("1  116,370  ACD"));

        // A new best score for Final Fight, its size and time as they were:
        // not looked at.
        const QString path = m_arcade + QStringLiteral("/hiscore/ffight.hi");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QByteArray data = file.readAll();
        file.close();
        data[29] = char(0x12);
        data[30] = char(0x34);
        data[31] = char(0x50);
        writeFile(path, data);
        setTime(path, m_time);
        scores.refresh();
        QVERIFY(!scores.reading());
        QCOMPARE(names(scores.requestEntries(QStringLiteral("game:ffight"))).first(), QStringLiteral("1  116,370  ACD"));

        // Played again: read again.
        setTime(path, m_time.addSecs(3600));
        refreshed(scores);
        QCOMPARE(reading.size(), 2);
        QCOMPARE(names(scores.requestEntries(QStringLiteral("game:ffight"))).first(), QStringLiteral("1  123,450  ACD"));

        // Back as it was.
        copySample(QStringLiteral("hiscore/ffight.hi"), path);
        setTime(path, m_time);
    }

    // A file new or gone reads a game again, whatever the times: Final
    // Fight's NVRAM newer than its table, then its table gone, which leaves
    // NVRAM it has no table in.
    void filesNewOrGone() {
        caughtUp();
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        const QString nvram = m_arcade + QStringLiteral("/nvram/ffight/nvram");
        writeFile(nvram, "settings");
        setTime(nvram, m_time.addSecs(7200));
        refreshed(scores);
        QVERIFY(names(scores.requestEntries(QStringLiteral("all"))).contains(QStringLiteral("Final Fight (World, set 1)")));
        QVERIFY(QFile::remove(m_arcade + QStringLiteral("/hiscore/ffight.hi")));
        refreshed(scores);
        QVERIFY(!names(scores.requestEntries(QStringLiteral("all"))).contains(QStringLiteral("Final Fight (World, set 1)")));
        QVERIFY(!names(scores.requestEntries(QStringLiteral("unreadable"))).contains(QStringLiteral("ffight")));
        // Back as it was.
        QVERIFY(QDir(m_arcade + QStringLiteral("/nvram/ffight")).removeRecursively());
        copySample(QStringLiteral("hiscore/ffight.hi"), m_arcade + QStringLiteral("/hiscore/ffight.hi"));
        setTime(m_arcade + QStringLiteral("/hiscore/ffight.hi"), m_time);
        refreshed(scores);
        QCOMPARE(names(scores.requestEntries(QStringLiteral("game:ffight"))).first(), QStringLiteral("1  116,370  ACD"));
    }

    // A refresh asked for while the files are read follows the read, and
    // reads what changed meanwhile.
    void refreshDuringRead() {
        caughtUp();
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        setTime(m_arcade + QStringLiteral("/nvram/tgm2/eeprom"), m_time.addSecs(60));
        scores.refresh();
        QVERIFY(scores.reading());
        const QString late = m_arcade + QStringLiteral("/hiscore/zzzlate.hi");
        writeFile(late, "junk");
        scores.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!scores.reading(), 10000);
        QVERIFY(names(scores.requestEntries(QStringLiteral("unreadable"))).contains(QStringLiteral("zzzlate")));
        QVERIFY(QFile::remove(late));
        refreshed(scores);
        QVERIFY(!names(scores.requestEntries(QStringLiteral("unreadable"))).contains(QStringLiteral("zzzlate")));
    }

    // What openhi2txt throws (for a link that loops, here where Donkey
    // Kong's table would be) is why the game can't be read, not the app's
    // end.
    void failingRead() {
        caughtUp();
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        const QString loop = m_arcade + QStringLiteral("/hiscore/dkong.hi");
        QVERIFY(QFile::link(loop, loop));
        writeFile(m_arcade + QStringLiteral("/nvram/dkong/nvram"), "settings");
        refreshed(scores);
        const QVariantMap dkong = entry(scores.requestEntries(QStringLiteral("unreadable")), QStringLiteral("dkong"));
        QVERIFY2(dkong.value(QStringLiteral("detail")).toString().startsWith(QStringLiteral("The high scores couldn't be read")),
                 qPrintable(dkong.value(QStringLiteral("detail")).toString()));
        QVERIFY(QFile::remove(loop));
        QVERIFY(QDir(m_arcade + QStringLiteral("/nvram/dkong")).removeRecursively());
        refreshed(scores);
        QVERIFY(!names(scores.requestEntries(QStringLiteral("unreadable"))).contains(QStringLiteral("dkong")));
    }

    // A folder where a link of openhi2txt's was (a backup restored through
    // it) is made the link again.
    void folderForLink() {
        caughtUp();
        const QString link = m_data.path() + QStringLiteral("/highscores/mame/hiscore");
        QVERIFY(QFile::remove(link));
        writeFile(link + QStringLiteral("/ffight.hi"), "stale");
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        refreshed(scores);
        QCOMPARE(QFileInfo(link).symLinkTarget(), m_arcade + QStringLiteral("/hiscore"));
    }

    // Another hiscore.dat (none, here): every game read again with it, what
    // was read shown meanwhile.
    void hiscoreDatChanged() {
        caughtUp();
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        QVERIFY(QFile::rename(m_dat, m_dat + QStringLiteral(".away")));
        scores.refresh();
        QVERIFY(scores.reading());
        QCOMPARE(names(scores.requestEntries(QStringLiteral("all"))), allGames());
        QTRY_VERIFY_WITH_TIMEOUT(!scores.reading(), 10000);
        QVERIFY(!QFileInfo::exists(m_data.path() + QStringLiteral("/highscores/mame/plugins/hiscore/hiscore.dat")));
        QCOMPARE(names(scores.requestEntries(QStringLiteral("game:gng"))).first(), QStringLiteral(" 1  10,000  Yuk"));
        QVERIFY(QFile::rename(m_dat + QStringLiteral(".away"), m_dat));
        refreshed(scores);
        QCOMPARE(names(scores.requestEntries(QStringLiteral("game:gng"))).first(), QStringLiteral(" 1  17,200  *&?"));
    }

    // The names follow the Arcade module's games.json.
    void gameNames() {
        caughtUp();
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        writeGamesJson(QStringLiteral("Makai-Mura (Japan)"));
        setTime(m_arcade + QStringLiteral("/games.json"), m_time.addSecs(60));
        refreshed(scores);
        QCOMPARE(names(scores.requestEntries(QStringLiteral("all"))),
                 QStringList({ QStringLiteral("Final Fight (World, set 1)"), QStringLiteral("Makai-Mura (Japan)"),
                               QStringLiteral("Solar Fox (upright)"),
                               QStringLiteral("Tetris the Absolute The Grand Master 2") }));
        writeGamesJson(QStringLiteral("Ghosts'n Goblins (World? set 1)"));
        setTime(m_arcade + QStringLiteral("/games.json"), m_time.addSecs(120));
    }

    // A game whose file is gone goes from the lists, and from what is kept.
    void fileGone() {
        caughtUp();
        HighScoresBackend scores(QStringLiteral(APP_DIR), m_data.path(), played());
        refreshed(scores);
        QVERIFY(QFile::remove(m_arcade + QStringLiteral("/hiscore/gng.hi")));
        QSignalSpy reading(&scores, &HighScoresBackend::readingChanged);
        scores.refresh();
        QCOMPARE(reading.size(), 0);
        QStringList left = allGames();
        left.removeOne(QStringLiteral("Ghosts'n Goblins (World? set 1)"));
        QCOMPARE(names(scores.requestEntries(QStringLiteral("all"))), left);
        HighScoresBackend again(QStringLiteral(APP_DIR), m_data.path(), played());
        again.refresh();
        QVERIFY(!again.reading());
        QCOMPARE(names(again.requestEntries(QStringLiteral("all"))), left);
        copySample(QStringLiteral("hiscore/gng.hi"), m_arcade + QStringLiteral("/hiscore/gng.hi"));
    }

    // Without the definitions there is nothing to read with: why, and no
    // tables.
    void noDefinitions() {
        QTemporaryDir app;
        HighScoresBackend scores(app.path(), m_data.path(), played());
        QVERIFY(scores.problem().startsWith(QStringLiteral("hi2txt's game definitions are missing")));
        scores.refresh();
        QVERIFY(!scores.reading());
        QVERIFY(scores.requestEntries(QStringLiteral("top")).toList().isEmpty());
    }
};

QTEST_GUILESS_MAIN(HighScoresTest)
#include "highscores_test.moc"
