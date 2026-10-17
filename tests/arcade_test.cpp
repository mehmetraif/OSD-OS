#include "AppCore.h"
#include "input/ControllerProfile.h"
#include "modules/arcade/ArcadeBackend.h"
#include "modules/arcade/Mame.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

// The Arcade module against a stand-in for MAME (a shell script in the data
// folder's bin, where the module looks first): what MAME's -listxml says of
// the sets in the games folder, read as it comes and kept until MAME changes,
// the original screen's tree and the Arcade screen's filters made of it, a
// game's details, and a game started with its folders, the high scores
// plugin, the players' controllers in its controller file, a way out and the
// credits bought, how long it was played, and what a game that couldn't
// start lacked.
class ArcadeTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_data;
    QString m_games;
    QString m_log;

    // MAME as far as the module asks it: -listxml for the machines below
    // (exit 5 for a name it has none of, after the XML of those it has),
    // and a game, whose arguments, folder, display card and controller file
    // it notes. mslug and kinst lack files; sf2 can't open its picture;
    // slowgame takes a second, as does asking about a_slowlist half of one;
    // any other game takes $MAME_SECONDS.
    static constexpr const char *kFakeMame = R"SH(#!/bin/sh
machine() {
	case "$1" in
	pacman) echo '<machine name="pacman" sourcefile="pacman/pacman.cpp" cloneof="puckman" romof="puckman"><description>Pac-Man (Midway)</description><year>1980</year><manufacturer>Namco (Midway license)</manufacturer><rom name="pacman.6e" size="4096"/><display tag="screen" type="raster" rotate="90"/><input players="2" coins="2"><control type="joy" player="1"/></input><driver status="good"/></machine>' ;;
	galaga) echo '<machine name="galaga" sourcefile="namco/galaga.cpp"><description>Galaga (Namco rev. B)</description><year>1981</year><manufacturer>Namco</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	mslug) echo '<machine name="mslug" sourcefile="neogeo/neogeo.cpp" romof="neogeo"><description>Metal Slug - Super Vehicle-001</description><year>1996</year><manufacturer>Nazca</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	neogeo) echo '<machine name="neogeo" isbios="yes"><description>Neo-Geo MV-6F</description><year>1990</year><manufacturer>SNK</manufacturer></machine>' ;;
	xmen6p) echo '<machine name="xmen6p" sourcefile="konami/xmen.cpp" cloneof="xmen"><description>X-Men (6 Players ver EAB)</description><year>1992</year><manufacturer>Konami</manufacturer><display tag="lscreen" rotate="270"/><display tag="rscreen" rotate="0"/><input players="6"/><driver status="imperfect"/></machine>' ;;
	kinst) echo '<machine name="kinst"><description>Killer Instinct (v1.5d)</description><year>1994</year><manufacturer>Rare / Nintendo</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	sf2) echo '<machine name="sf2" sourcefile="capcom/cps1.cpp"><description>Street Fighter II: The World Warrior</description><year>1991</year><manufacturer>Capcom</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	acefruit) echo '<machine name="acefruit"><description>Ace (Cassette)</description><year>1982?</year><manufacturer>unknown</manufacturer><input players="1"/><driver status="preliminary"/></machine>' ;;
	*) return 1 ;;
	esac
}
case "$1" in
-listxml)
	shift
	[ "$1" = "-nodtd" ] && shift
	echo "listxml $*" >> "$MAME_LOG"
	case " $* " in *" a_slowlist "*) sleep 0.5 ;; esac
	echo '<?xml version="1.0"?>'
	echo '<mame build="0.276 (test)" debug="no" mameconfig="10">'
	missing=""
	for name in "$@"; do
		machine "$name" || missing="$name"
	done
	echo '<machine name="z80" isdevice="yes" runnable="no"><description>Zilog Z80</description></machine>'
	echo '</mame>'
	if [ -n "$missing" ]; then
		echo "No matching machines found for '$missing'" >&2
		exit 5
	fi
	exit 0
	;;
esac
{
	echo "play $*"
	echo "cwd $(pwd)"
	echo "kms ${SDL_KMSDRM_DEVICE_INDEX:-}"
} >> "$MAME_LOG"
prev=""
for a in "$@"; do
	[ "$prev" = "-ctrlrpath" ] && cat "$a/osdos.cfg" >> "$MAME_LOG"
	prev="$a"
done
case "$1" in
mslug)
	echo "sp-s2.sp1 NOT FOUND (tried in mslug neogeo)"
	echo "OPTIONAL sm1.bin NOT FOUND (tried in mslug)"
	echo "pal.bin NOT FOUND (NO GOOD DUMP KNOWN) (tried in mslug)"
	echo "000-lo.lo NOT FOUND (tried in mslug neogeo)"
	echo "Required files are missing, the machine cannot be run."
	exit 2
	;;
kinst)
	echo "kinst.u98 NOT FOUND (tried in kinst kinstp)"
	exit 2
	;;
sf2)
	echo "ALSA lib conf.c:5731:(snd_config_expand) Evaluate error: No such file or directory"
	echo "Fatal error: Could not create OpenGL context"
	exit 3
	;;
crash)
	echo "ALSA lib pcm.c:2721:(snd_pcm_open_noupdate) Unknown PCM dmix"
	kill -SEGV $$
	;;
slowgame)
	sleep 1
	;;
*)
	[ -n "$MAME_SECONDS" ] && sleep "$MAME_SECONDS"
	;;
esac
echo "end $1" >> "$MAME_LOG"
exit 0
)SH";

    QString mamePath() const { return m_data.path() + QStringLiteral("/bin/mame"); }

    void writeFile(const QString &path, const QByteArray &data = QByteArray()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
        f.write(data);
    }

    QStringList logLines() const {
        QFile f(m_log);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    QStringList logLines(const QString &prefix) const {
        QStringList lines = logLines();
        lines.erase(std::remove_if(lines.begin(), lines.end(),
                                   [&prefix](const QString &l) { return !l.startsWith(prefix); }),
                    lines.end());
        return lines;
    }

    static QStringList names(const QVariant &entries) {
        QStringList list;
        const QVariantList all = entries.toList();
        for (const QVariant &v : all)
            list << v.toMap().value(QStringLiteral("name")).toString();
        return list;
    }

    // Asked about the folder, and done asking MAME.
    static void refreshed(ArcadeBackend &arcade) {
        arcade.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
    }

    // A game played to its end: (exit code, reason, notice).
    static QVariantList played(ArcadeBackend &arcade, const QString &game) {
        QSignalSpy finished(&arcade, &ArcadeBackend::finished);
        if (!arcade.play(game))
            return { -1, QStringLiteral("not started"), arcade.lastError() };
        if (!finished.wait(10000) && finished.isEmpty())
            return { -1, QStringLiteral("no end"), QString() };
        return finished.first();
    }

private slots:
    void initTestCase() {
        QVERIFY(m_data.isValid());
        m_log = m_data.path() + QStringLiteral("/mame.log");
        qputenv("MAME_LOG", m_log.toUtf8());
        qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        qunsetenv("OSDOS_DRM_DEVICE");
        // A card ALSA can play through, whatever this machine has.
        writeFile(m_data.path() + QStringLiteral("/asound/cards"),
                  " 0 [Headphones     ]: bcm2835_headpho - bcm2835 Headphones\n"
                  "                      bcm2835 Headphones\n");
        QVERIFY(QDir().mkpath(m_data.path() + QStringLiteral("/asound/card0/pcm0p")));
        qputenv("OSDOS_ASOUND_DIR", (m_data.path() + QStringLiteral("/asound")).toUtf8());
        writeFile(mamePath(), kFakeMame);
        QVERIFY(QFile::setPermissions(mamePath(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        // MAME's hiscore plugin, where Debian's is beside /usr/games.
        writeFile(m_data.path() + QStringLiteral("/share/games/mame/plugins/hiscore/init.lua"));

        m_games = m_data.path() + QStringLiteral("/media/Games/Arcade");
        for (const char *set : { "pacman.zip", "galaga.zip", "mslug.zip", "neogeo.zip", "xmen6p.7z",
                                 "sf2.zip", "acefruit.zip", "notagame.zip", "Street Fighter.zip", "readme.txt" })
            writeFile(m_games + QLatin1Char('/') + QLatin1String(set));
        // A game in a folder of its own, a folder beside a game's zip (where
        // its CHD would be: the zip is the set), and the folders that aren't
        // games.
        QVERIFY(QDir().mkpath(m_games + QStringLiteral("/kinst")));
        QVERIFY(QDir().mkpath(m_games + QStringLiteral("/galaga")));
        QVERIFY(QDir().mkpath(m_games + QStringLiteral("/bios")));
        QVERIFY(QDir().mkpath(m_games + QStringLiteral("/samples")));
    }

    void cleanupTestCase() {
        qunsetenv("OSDOS_ASOUND_DIR");
        qunsetenv("MAME_LOG");
        qunsetenv("OSDOS_MEDIA_DIR");
        qunsetenv("OSDOS_DRM_DEVICE");
    }

    void helpers() {
        QVERIFY(mame::isShortName(QStringLiteral("pacman")));
        QVERIFY(mame::isShortName(QStringLiteral("1942")));
        QVERIFY(mame::isShortName(QStringLiteral("ddp2_c")));
        QVERIFY(!mame::isShortName(QStringLiteral("street fighter")));
        QVERIFY(!mame::isShortName(QStringLiteral("pac*")));
        QVERIFY(!mame::isShortName(QStringLiteral("PACMAN")));
        QVERIFY(!mame::isShortName(QString()));

        QCOMPARE(mame::makerOf(QStringLiteral("Namco (Midway license)")), QStringLiteral("Namco"));
        QCOMPARE(mame::makerOf(QStringLiteral("Capcom / Mitchell")), QStringLiteral("Capcom"));
        QCOMPARE(mame::makerOf(QStringLiteral("<unknown>")), QString());
        QCOMPARE(mame::makerOf(QStringLiteral("unknown")), QString());
        QCOMPARE(mame::makerOf(QStringLiteral("bootleg")), QStringLiteral("bootleg"));

        QCOMPARE(mame::decadeOf(QStringLiteral("1981")), QStringLiteral("1980s"));
        QCOMPARE(mame::decadeOf(QStringLiteral("198?")), QStringLiteral("1980s"));
        QCOMPARE(mame::decadeOf(QStringLiteral("1982?")), QStringLiteral("1980s"));
        QCOMPARE(mame::decadeOf(QStringLiteral("19??")), QString());
        QCOMPARE(mame::decadeOf(QString()), QString());

        // Each player's controller as MAME's joystick of the same number;
        // one a player before them has goes where MAME's order puts it.
        const QString config = mame::controllerConfig({ QStringLiteral("aaaa"), QStringLiteral("bbbb"),
                                                        QStringLiteral("aaaa"), QString(),
                                                        QStringLiteral("cccc") });
        QVERIFY(config.contains(QStringLiteral("<mapdevice device=\"aaaa\" controller=\"JOYCODE_1\" />")));
        QVERIFY(config.contains(QStringLiteral("<mapdevice device=\"bbbb\" controller=\"JOYCODE_2\" />")));
        QVERIFY(config.contains(QStringLiteral("<mapdevice device=\"cccc\" controller=\"JOYCODE_5\" />")));
        QCOMPARE(config.count(QStringLiteral("aaaa")), 1);
        QVERIFY(!config.contains(QStringLiteral("JOYCODE_3\"")));
        QVERIFY(config.contains(QStringLiteral("<port type=\"UI_CANCEL\">")));
        QVERIFY(config.contains(QStringLiteral("KEYCODE_ESC OR JOYCODE_1_SELECT JOYCODE_1_START")));
        QVERIFY(!mame::controllerConfig({}).contains(QStringLiteral("mapdevice")));

        // Only what MAME can't go without: not an optional file, nor one
        // with no good dump known.
        const mame::Missing missing = mame::missingFiles(QStringLiteral(
            "sp-s2.sp1 NOT FOUND (tried in mslug neogeo)\n"
            "OPTIONAL sm1.bin NOT FOUND (tried in mslug)\n"
            "pal.bin NOT FOUND (NO GOOD DUMP KNOWN) (tried in mslug)\n"
            "000-lo.lo NOT FOUND (tried in mslug neogeo)\n"
            "disk.chd NOT FOUND\n"
            "Required files are missing, the machine cannot be run.\n"));
        QCOMPARE(missing.files, QStringList({ QStringLiteral("sp-s2.sp1"), QStringLiteral("000-lo.lo"),
                                              QStringLiteral("disk.chd") }));
        QCOMPARE(missing.sets, QStringList({ QStringLiteral("mslug"), QStringLiteral("neogeo") }));

        QCOMPARE(mame::lastError(QStringLiteral("Loading\nFatal error: no screen\nBye\n")),
                 QStringLiteral("Fatal error: no screen"));
        QCOMPARE(mame::lastError(QStringLiteral("one\ntwo\n\n")), QStringLiteral("two"));
        // Not ALSA's complaints about a missing sound card, error or not.
        QCOMPARE(mame::lastError(QStringLiteral("ALSA lib conf.c:5731: Evaluate error\nLoading\n"
                                                "Fatal error: no screen\nALSA lib pcm.c: error\n")),
                 QStringLiteral("Fatal error: no screen"));
        QCOMPARE(mame::lastError(QStringLiteral("Initializing\nALSA lib pcm.c: Unknown PCM dmix error\n")),
                 QStringLiteral("Initializing"));
    }

    // -listxml read as it comes, a byte at a time as well as at once.
    void listXmlInChunks() {
        const QByteArray xml =
            "<?xml version=\"1.0\"?>\n<mame build=\"0.276 (test)\" debug=\"no\" mameconfig=\"10\">\n"
            "\t<machine name=\"xmen6p\" sourcefile=\"konami/xmen.cpp\" cloneof=\"xmen\"><description>X-Men &amp; Friends</description>"
            "<year>1992</year><manufacturer>Konami</manufacturer><rom name=\"a\"/>"
            "<display tag=\"l\" rotate=\"270\"/><display tag=\"r\" rotate=\"0\"/>"
            "<input players=\"6\"><control type=\"joy\"/></input><dipswitch name=\"x\"><dipvalue name=\"y\"/></dipswitch>"
            "<driver status=\"imperfect\"/></machine>\n"
            "\t<machine name=\"neogeo\" isbios=\"yes\"><description>Neo-Geo</description></machine>\n"
            "\t<machine name=\"z80\" isdevice=\"yes\" runnable=\"no\"><description>Zilog Z80</description></machine>\n"
            "</mame>\n";
        for (const int step : { 1, 7, int(xml.size()) }) {
            mame::ListXmlReader reader;
            QList<mame::Machine> machines;
            for (int i = 0; i < xml.size(); i += step) {
                reader.addData(xml.mid(i, step));
                machines += reader.take();
            }
            QVERIFY(!reader.failed());
            QCOMPARE(reader.build(), QStringLiteral("0.276 (test)"));
            QCOMPARE(machines.size(), 3);
            const mame::Machine &x = machines.at(0);
            QCOMPARE(x.name, QStringLiteral("xmen6p"));
            QCOMPARE(x.description, QStringLiteral("X-Men & Friends"));
            QCOMPARE(x.year, QStringLiteral("1992"));
            QCOMPARE(x.manufacturer, QStringLiteral("Konami"));
            QCOMPARE(x.cloneOf, QStringLiteral("xmen"));
            QCOMPARE(x.sourceFile, QStringLiteral("konami/xmen.cpp"));
            QCOMPARE(x.players, 6);
            QCOMPARE(x.rotate, 270);
            QCOMPARE(x.status, QStringLiteral("imperfect"));
            QVERIFY(x.isGame());
            QVERIFY(machines.at(1).bios && !machines.at(1).isGame());
            QVERIFY(machines.at(2).device && !machines.at(2).runnable);
            // As the cache keeps it.
            const mame::Machine kept = mame::Machine::fromVariant(x.name, x.toVariant());
            QCOMPARE(kept.description, x.description);
            QCOMPARE(kept.sourceFile, x.sourceFile);
            QCOMPARE(kept.players, 6);
            QCOMPARE(kept.rotate, 270);
            QVERIFY(mame::Machine::fromVariant(QStringLiteral("neogeo"), machines.at(1).toVariant()).bios);
        }
        mame::ListXmlReader broken;
        broken.addData("<mame><machine name=\"x\"><description>a</year></mame>");
        QVERIFY(broken.failed());
    }

    // The folder's sets as MAME says they are, in the tree's folders.
    void tree() {
        QFile::remove(m_log);
        AppCore core(m_data.path(), m_data.path());
        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        QCOMPARE(arcade.gamesFolder(), m_games);
        QCOMPARE(arcade.mamePath(), mamePath());

        QSignalSpy ready(&arcade, &ArcadeBackend::entriesReady);
        arcade.refresh();
        ready.clear();
        QVERIFY(arcade.scanning());
        // While MAME is asked: the lists at once, the rest once it has
        // answered, and the top again then.
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("top"))),
                 QStringList({ QStringLiteral("Recently Played"), QStringLiteral("Favorites") }));
        QVERIFY(!arcade.requestEntries(QStringLiteral("all")).isValid());
        QVERIFY(ready.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
        QStringList readyPaths;
        for (const QList<QVariant> &args : std::as_const(ready))
            readyPaths << args.at(0).toString();
        readyPaths.sort();
        QCOMPARE(readyPaths, QStringList({ QStringLiteral("all"), QStringLiteral("top") }));
        QCOMPARE(arcade.problem(), QString());
        QVERIFY(arcade.folderReadable());
        // Asked about once, every set whose name can be one, the folder
        // game among them; not the folders that aren't games.
        QCOMPARE(logLines(QStringLiteral("listxml")),
                 QStringList({ QStringLiteral("listxml acefruit galaga kinst mslug neogeo notagame pacman sf2 xmen6p") }));

        QCOMPARE(names(arcade.requestEntries(QStringLiteral("top"))),
                 QStringList({ QStringLiteral("Recently Played"), QStringLiteral("Favorites"),
                               QStringLiteral("Search"), QStringLiteral("All Games"),
                               QStringLiteral("Players"), QStringLiteral("Years"),
                               QStringLiteral("Makers"), QStringLiteral("Not Recognized") }));
        // By their names; no BIOS set.
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("all"))),
                 QStringList({ QStringLiteral("Ace (Cassette)"),
                               QStringLiteral("Galaga (Namco rev. B)"), QStringLiteral("Killer Instinct (v1.5d)"),
                               QStringLiteral("Metal Slug - Super Vehicle-001"), QStringLiteral("Pac-Man (Midway)"),
                               QStringLiteral("Street Fighter II: The World Warrior"),
                               QStringLiteral("X-Men (6 Players ver EAB)") }));
        const QVariantMap ace = arcade.requestEntries(QStringLiteral("all")).toList().first().toMap();
        QCOMPARE(ace.value(QStringLiteral("detail")).toString(),
                 QStringLiteral("1982?\nunknown\n1 player\nNot working in MAME\nacefruit.zip"));
        const QVariantMap galaga = arcade.requestEntries(QStringLiteral("all")).toList().at(1).toMap();
        QCOMPARE(galaga.value(QStringLiteral("path")).toString(), QStringLiteral("game:galaga"));
        QCOMPARE(galaga.value(QStringLiteral("game")).toString(), QStringLiteral("galaga"));
        QCOMPARE(galaga.value(QStringLiteral("isFolder")).toBool(), false);
        QCOMPARE(galaga.value(QStringLiteral("detail")).toString(),
                 QStringLiteral("1981\nNamco\n2 players\ngalaga.zip"));
        const QVariantMap xmen = arcade.requestEntries(QStringLiteral("all")).toList().last().toMap();
        QCOMPARE(xmen.value(QStringLiteral("detail")).toString(),
                 QStringLiteral("1992\nKonami\n6 players\nImperfect in MAME\nxmen6p.7z"));

        QCOMPARE(names(arcade.requestEntries(QStringLiteral("players"))),
                 QStringList({ QStringLiteral("1 Player"), QStringLiteral("2 Players"), QStringLiteral("6 Players") }));
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("players/6"))),
                 QStringList({ QStringLiteral("X-Men (6 Players ver EAB)") }));
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("years"))),
                 QStringList({ QStringLiteral("1980s"), QStringLiteral("1990s") }));
        // A year MAME isn't sure of in its decade; a maker it calls unknown
        // in none.
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("years/1980s"))),
                 QStringList({ QStringLiteral("Ace (Cassette)"), QStringLiteral("Galaga (Namco rev. B)"),
                               QStringLiteral("Pac-Man (Midway)") }));
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("makers"))),
                 QStringList({ QStringLiteral("Capcom"), QStringLiteral("Konami"), QStringLiteral("Namco"),
                               QStringLiteral("Nazca"), QStringLiteral("Rare") }));
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("makers/Namco"))),
                 QStringList({ QStringLiteral("Galaga (Namco rev. B)"), QStringLiteral("Pac-Man (Midway)") }));
        // By name, or by the set's.
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("search/PAC"))),
                 QStringList({ QStringLiteral("Pac-Man (Midway)") }));
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("search/sf2"))),
                 QStringList({ QStringLiteral("Street Fighter II: The World Warrior") }));
        // What MAME has no game of, and what can't be a set's name.
        const QVariantList unknown = arcade.requestEntries(QStringLiteral("unknown")).toList();
        QCOMPARE(names(unknown), QStringList({ QStringLiteral("notagame.zip"), QStringLiteral("Street Fighter.zip") }));
        QCOMPARE(unknown.first().toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("unknown"));
        QCOMPARE(unknown.first().toMap().value(QStringLiteral("path")).toString(), QStringLiteral("unknown:notagame"));
        QCOMPARE(unknown.first().toMap().value(QStringLiteral("detail")).toString(),
                 QStringLiteral("MAME has no game called notagame\nIts set may be for another version of MAME"));
        QCOMPARE(unknown.last().toMap().value(QStringLiteral("detail")).toString(),
                 QStringLiteral("Not the name of a set MAME knows\nA set is named as MAME names its game: pacman.zip"));
        QVERIFY(QFileInfo::exists(m_data.path() + QStringLiteral("/arcade/games.json")));
    }

    // What MAME said is kept: asked again only about a set new in the
    // folder, and about every one once MAME itself has changed.
    void cache() {
        {
            // MAME asked once, whatever ran before.
            ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
            refreshed(arcade);
        }
        QFile::remove(m_log);
        {
            ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
            arcade.refresh();
            QVERIFY(!arcade.scanning());
            QCOMPARE(names(arcade.requestEntries(QStringLiteral("all"))).size(), 7);
        }
        QVERIFY(logLines(QStringLiteral("listxml")).isEmpty());

        writeFile(m_games + QStringLiteral("/1942.zip"));
        {
            ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
            refreshed(arcade);
            QCOMPARE(logLines(QStringLiteral("listxml")), QStringList({ QStringLiteral("listxml 1942") }));
            QCOMPARE(names(arcade.requestEntries(QStringLiteral("unknown"))).first(), QStringLiteral("1942.zip"));
        }

        QFile fake(mamePath());
        QVERIFY(fake.open(QIODevice::Append));
        fake.write("# a newer MAME\n");
        fake.close();
        QFile::remove(m_log);
        {
            ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
            refreshed(arcade);
            QCOMPARE(logLines(QStringLiteral("listxml")),
                     QStringList({ QStringLiteral("listxml 1942 acefruit galaga kinst mslug neogeo notagame pacman sf2 xmen6p") }));
        }
        QFile::remove(m_games + QStringLiteral("/1942.zip"));
    }

    // A game started as MAME is to have it, and back.
    void play() {
        QFile::remove(m_log);
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);
        // Players 1 and 2 on two pads, player 3 on one of player 1's model.
        controllers::setPlayerEnvironment({ { QStringLiteral("OSDOS_PLAYER1_GUID"), QStringLiteral("0600aaaa") },
                                            { QStringLiteral("OSDOS_PLAYER2_GUID"), QStringLiteral("0600bbbb") },
                                            { QStringLiteral("OSDOS_PLAYER3_GUID"), QStringLiteral("0600aaaa") } });
        qputenv("OSDOS_DRM_DEVICE", "/dev/dri/card1");
        QVERIFY(!arcade.played());
        QCOMPARE(played(arcade, QStringLiteral("pacman")),
                 QVariantList({ 0, QStringLiteral("ok"), QString() }));
        qunsetenv("OSDOS_DRM_DEVICE");
        QVERIFY(arcade.played());
        QVERIFY(!arcade.running());

        const QString home = m_data.path() + QStringLiteral("/arcade");
        const QString plugins = m_data.path() + QStringLiteral("/share/games/mame/plugins");
        QCOMPARE(logLines(QStringLiteral("play")), QStringList({
            QStringLiteral("play pacman -rompath %1;%1/bios -homepath %2 -cfg_directory %2/cfg"
                           " -nvram_directory %2/nvram -input_directory %2/inp -state_directory %2/sta"
                           " -snapshot_directory %2/snap -diff_directory %2/diff"
                           " -ctrlrpath %2/ctrlr -ctrlr osdos -skip_gameinfo -samplepath %1/samples"
                           " -pluginspath %3 -plugin hiscore").arg(m_games, home, plugins) }));
        QCOMPARE(logLines(QStringLiteral("cwd")), QStringList({ QStringLiteral("cwd ") + home }));
        // The output's card, for SDL, on a Pi 5.
        QCOMPARE(logLines(QStringLiteral("kms")), QStringList({ QStringLiteral("kms 1") }));
        const QString log = logLines().join(QLatin1Char('\n'));
        QVERIFY(log.contains(QStringLiteral("<mapdevice device=\"0600aaaa\" controller=\"JOYCODE_1\" />")));
        QVERIFY(log.contains(QStringLiteral("<mapdevice device=\"0600bbbb\" controller=\"JOYCODE_2\" />")));
        QVERIFY(!log.contains(QStringLiteral("JOYCODE_3\"")));
        QVERIFY(log.contains(QStringLiteral("KEYCODE_ESC OR JOYCODE_1_SELECT JOYCODE_1_START")));
        QVERIFY(QFileInfo(home + QStringLiteral("/cfg")).isDir());
        QVERIFY(QFileInfo(home + QStringLiteral("/nvram")).isDir());

        // The module's settings: MAME's picture drawn another way, and no
        // high scores kept; and no players.
        controllers::setPlayerEnvironment({});
        QFile::remove(m_log);
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("video"), QStringLiteral("Software"));
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("high_scores"), QStringLiteral("OFF"));
        arcade.onSettingChanged(QStringLiteral("com.osdos.other"), QStringLiteral("video"), QStringLiteral("BGFX"));
        QCOMPARE(played(arcade, QStringLiteral("galaga")).at(1).toString(), QStringLiteral("ok"));
        const QStringList second = logLines(QStringLiteral("play"));
        QCOMPARE(second.size(), 1);
        QVERIFY(second.first().endsWith(QStringLiteral(" -samplepath %1/samples -video soft").arg(m_games)));
        QVERIFY(!second.first().contains(QStringLiteral("hiscore")));
        // No players: no controller of theirs, still a way out.
        QVERIFY(!logLines().join(QLatin1Char('\n')).contains(QStringLiteral("mapdevice")));
        QVERIFY(logLines().join(QLatin1Char('\n')).contains(QStringLiteral("UI_CANCEL")));
    }

    // What a game that didn't play says.
    void notices() {
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);
        QCOMPARE(played(arcade, QStringLiteral("mslug")),
                 QVariantList({ 2, QStringLiteral("failed"),
                                QStringLiteral("Missing sp-s2.sp1, 000-lo.lo, looked for in mslug.zip, neogeo.zip") }));
        QVERIFY(arcade.output().contains(QStringLiteral("Required files are missing")));
        // A set as it is in the folder (a folder of files), one that isn't
        // there as a zip.
        QCOMPARE(played(arcade, QStringLiteral("kinst")).at(2).toString(),
                 QStringLiteral("Missing kinst.u98, looked for in kinst/, kinstp.zip"));
        const QVariantList fatal = played(arcade, QStringLiteral("sf2"));
        QCOMPARE(fatal.at(0).toInt(), 3);
        QCOMPARE(fatal.at(2).toString(), QStringLiteral("MAME stopped: Fatal error: Could not create OpenGL context\n"
                                                        "Try another Video in the Arcade module's settings"));

        // MAME killed by a signal says nothing of why.
        writeFile(m_games + QStringLiteral("/crash.zip"));
        arcade.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
        QCOMPARE(played(arcade, QStringLiteral("crash")),
                 QVariantList({ 11, QStringLiteral("failed"),
                                QStringLiteral("MAME closed unexpectedly (signal 11)\n"
                                               "Try another Video in the Arcade module's settings") }));
        QFile::remove(m_games + QStringLiteral("/crash.zip"));

        QVERIFY(!arcade.play(QStringLiteral("gone")));
        QCOMPARE(arcade.lastError(), QStringLiteral("The game is no longer in the games folder"));
        // A file in the folder whose name isn't a set's, nor an option's.
        writeFile(m_games + QStringLiteral("/-listxml.zip"));
        arcade.refresh();
        QVERIFY(!arcade.play(QStringLiteral("-listxml")));
        QCOMPARE(arcade.lastError(), QStringLiteral("MAME has no game called -listxml"));
        QVERIFY(!arcade.play(QStringLiteral("street fighter")));
        QFile::remove(m_games + QStringLiteral("/-listxml.zip"));
        // One at a time.
        QSignalSpy finished(&arcade, &ArcadeBackend::finished);
        QVERIFY(arcade.play(QStringLiteral("pacman")));
        QVERIFY(arcade.running());
        QVERIFY(!arcade.play(QStringLiteral("galaga")));
        QCOMPARE(arcade.lastError(), QStringLiteral("A game is still running"));
        QVERIFY(finished.wait(10000));
    }

    // With no sound card ALSA can play through, MAME is told to play none:
    // it may not start at all otherwise.
    void noSoundCard() {
        QTemporaryDir asound;
        QTemporaryDir runtime;
        QVERIFY(asound.isValid() && runtime.isValid());
        writeFile(asound.path() + QStringLiteral("/cards"), "--- no soundcards ---\n");
        const QByteArray pulse = qgetenv("PULSE_SERVER");
        const QByteArray xdg = qgetenv("XDG_RUNTIME_DIR");
        qunsetenv("PULSE_SERVER");
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        qputenv("OSDOS_ASOUND_DIR", asound.path().toUtf8());
        QFile::remove(m_log);
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);
        QCOMPARE(played(arcade, QStringLiteral("galaga")).at(1).toString(), QStringLiteral("ok"));
        qputenv("OSDOS_ASOUND_DIR", (m_data.path() + QStringLiteral("/asound")).toUtf8());
        if (!pulse.isNull())
            qputenv("PULSE_SERVER", pulse);
        if (xdg.isNull())
            qunsetenv("XDG_RUNTIME_DIR");
        else
            qputenv("XDG_RUNTIME_DIR", xdg);
        const QStringList lines = logLines(QStringLiteral("play"));
        QCOMPARE(lines.size(), 1);
        QVERIFY2(lines.first().endsWith(QStringLiteral(" -sound none")), qPrintable(lines.first()));
    }

    // Recently Played and Favorites: the games still in the folder, as MAME
    // says they are.
    void lists() {
        AppCore core(m_data.path(), m_data.path());
        core.add_to_list(QStringLiteral("com.osdos.arcade"), QStringLiteral("recent"),
                         { { QStringLiteral("name"), QStringLiteral("Old Name") },
                           { QStringLiteral("path"), QStringLiteral("game:pacman") } });
        core.add_to_list(QStringLiteral("com.osdos.arcade"), QStringLiteral("recent"),
                         { { QStringLiteral("name"), QStringLiteral("Gone") },
                           { QStringLiteral("path"), QStringLiteral("game:gone") } });
        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        refreshed(arcade);
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("recent"))),
                 QStringList({ QStringLiteral("Pac-Man (Midway)") }));
        QVERIFY(arcade.requestEntries(QStringLiteral("favorites")).toList().isEmpty());
        // As High Scores has them: every set played, newest first.
        QCOMPARE(arcade.recentSets(), QStringList({ QStringLiteral("gone"), QStringLiteral("pacman") }));
    }

    // A set whose file MAME can't open by its name: in upper case, where the
    // filesystem tells cases apart (not the card's exFAT).
    void misnamed() {
        writeFile(m_data.path() + QStringLiteral("/Case"));
        const bool tellsCases = !QFileInfo::exists(m_data.path() + QStringLiteral("/case"));
        QFile::remove(m_data.path() + QStringLiteral("/Case"));
        if (!tellsCases)
            QSKIP("this filesystem doesn't tell cases apart");
        // The folder as the other tests have it again, whatever happens here.
        const auto restore = qScopeGuard([this] {
            QFile::remove(m_games + QStringLiteral("/DKONG.ZIP"));
            QFile::remove(m_games + QStringLiteral("/Pac-Man.zip"));
            QDir(m_games).rename(QStringLiteral("BIOS"), QStringLiteral("bios"));
            QDir(m_games).rename(QStringLiteral("Samples"), QStringLiteral("samples"));
        });
        writeFile(m_games + QStringLiteral("/DKONG.ZIP"));
        writeFile(m_games + QStringLiteral("/Pac-Man.zip"));
        QVERIFY(QDir(m_games).rename(QStringLiteral("bios"), QStringLiteral("BIOS")));
        QVERIFY(QDir(m_games).rename(QStringLiteral("samples"), QStringLiteral("Samples")));
        QFile::remove(m_log);
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);
        // Not asked about, and under Not Recognized with what to do.
        QVERIFY(!logLines(QStringLiteral("listxml")).join(QLatin1Char(' ')).contains(QStringLiteral("dkong")));
        const QVariantList unknown = arcade.requestEntries(QStringLiteral("unknown")).toList();
        QCOMPARE(names(unknown).first(), QStringLiteral("DKONG.ZIP"));
        QCOMPARE(unknown.first().toMap().value(QStringLiteral("detail")).toString(),
                 QStringLiteral("MAME looks for dkong.zip, in lower case\nRename the file to dkong.zip"));
        QVERIFY(!arcade.play(QStringLiteral("dkong")));
        QCOMPARE(arcade.lastError(), QStringLiteral("MAME looks for dkong.zip, in lower case: rename the file to dkong.zip"));
        // A name that can't be a set's whatever its case isn't told to be
        // renamed, and the BIOS and Samples folders aren't sets.
        QCOMPARE(names(unknown), QStringList({ QStringLiteral("DKONG.ZIP"), QStringLiteral("notagame.zip"),
                                               QStringLiteral("Pac-Man.zip"), QStringLiteral("Street Fighter.zip") }));
        QVERIFY(unknown.at(2).toMap().value(QStringLiteral("detail")).toString().startsWith(
            QStringLiteral("Not the name of a set MAME knows")));
        // MAME is given them as they are named.
        QCOMPARE(played(arcade, QStringLiteral("galaga")).at(1).toString(), QStringLiteral("ok"));
        const QString line = logLines(QStringLiteral("play")).first();
        QVERIFY2(line.contains(QStringLiteral(" -rompath %1;%1/BIOS ").arg(m_games)), qPrintable(line));
        QVERIFY2(line.contains(QStringLiteral(" -samplepath %1/Samples ").arg(m_games)), qPrintable(line));
    }

    // A game started while MAME is asked about the sets holds the rest of
    // the asking until it has ended: MAME isn't run beside a game.
    void scanWaitsForGame() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/mame"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        // More sets than a batch, the first batch slow to answer.
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        writeFile(games + QStringLiteral("/a_slowlist.zip"));
        for (int i = 0; i < 300; ++i)
            writeFile(games + QStringLiteral("/g%1.zip").arg(i, 3, 10, QLatin1Char('0')));
        writeFile(games + QStringLiteral("/slowgame.zip"));
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        QFile::remove(m_log);
        QSignalSpy finished(&arcade, &ArcadeBackend::finished);
        arcade.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(logLines(QStringLiteral("listxml")).size(), 1, 5000);
        QVERIFY(arcade.play(QStringLiteral("slowgame")));
        QVERIFY(finished.wait(10000));
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
        // The first batch answered while the game played; the second only
        // once it had ended.
        QStringList order;
        const QStringList lines = logLines();
        for (const QString &line : lines) {
            const QString word = line.section(QLatin1Char(' '), 0, 0);
            if (word == QLatin1String("listxml") || word == QLatin1String("play") || word == QLatin1String("end"))
                order << word;
        }
        QCOMPARE(order, QStringList({ QStringLiteral("listxml"), QStringLiteral("play"), QStringLiteral("end"),
                                      QStringLiteral("listxml") }));
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("unknown"))).size(), 302);
    }

    // A MAME with its own files beside it runs in its folder, to find them;
    // what it writes still goes to the module's.
    void ownFolder() {
        const QString bgfx = m_data.path() + QStringLiteral("/bin/bgfx");
        const auto restore = qScopeGuard([&bgfx] { QDir(bgfx).removeRecursively(); });
        QVERIFY(QDir().mkpath(bgfx));
        QFile::remove(m_log);
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);
        QCOMPARE(played(arcade, QStringLiteral("galaga")).at(1).toString(), QStringLiteral("ok"));
        QCOMPARE(logLines(QStringLiteral("cwd")), QStringList({ QStringLiteral("cwd %1/bin").arg(m_data.path()) }));
        QVERIFY(logLines(QStringLiteral("play")).first().contains(
            QStringLiteral(" -cfg_directory %1/arcade/cfg ").arg(m_data.path())));
    }

    // A games folder that isn't there and can't be made (a drive not
    // plugged in) says so, and has nothing in it.
    void folderProblem() {
        writeFile(m_data.path() + QStringLiteral("/not-a-folder"));
        AppCore core(m_data.path(), m_data.path());
        const auto restore = qScopeGuard([this, &core] {
            core.save_setting(QStringLiteral("com.osdos.arcade"), QStringLiteral("games_directory"), QString());
            QFile::remove(m_data.path() + QStringLiteral("/not-a-folder"));
        });
        core.save_setting(QStringLiteral("com.osdos.arcade"), QStringLiteral("games_directory"),
                          m_data.path() + QStringLiteral("/not-a-folder/MAME"));
        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        QSignalSpy problem(&arcade, &ArcadeBackend::problemChanged);
        arcade.refresh();
        QVERIFY(!arcade.scanning());
        QVERIFY(!arcade.folderReadable());
        QCOMPARE(arcade.problem(), QStringLiteral("The games folder can't be read: %1/not-a-folder/MAME")
                                       .arg(m_data.path()));
        QCOMPARE(problem.size(), 1);
        QVERIFY(arcade.requestEntries(QStringLiteral("top")).toList().isEmpty());
    }

    // Without MAME the folder's sets are still listed, by their names, and
    // the list says what is missing.
    void withoutMame() {
        if (!QStandardPaths::findExecutable(QStringLiteral("mame")).isEmpty()
            || QFileInfo::exists(QStringLiteral("/usr/games/mame")))
            QSKIP("a MAME is installed here");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        writeFile(arcade.gamesFolder() + QStringLiteral("/pacman.zip"));
        arcade.refresh();
        QVERIFY(!arcade.scanning());
        QCOMPARE(arcade.problem(), QStringLiteral("MAME is not installed"));
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("all"))), QStringList({ QStringLiteral("pacman") }));
        QVERIFY(!arcade.play(QStringLiteral("pacman")));
        QCOMPARE(arcade.lastError(), QStringLiteral("MAME is not installed"));
    }
    // ── The Arcade screen ────────────────────────────────────────────────

    // What the Arcade screen makes of a game's description and its driver,
    // catver.ini's genres, and the credits' script.
    void screenHelpers() {
        QCOMPARE(mame::titleOf(QStringLiteral("Galaga (Namco rev. B)")), QStringLiteral("Galaga"));
        QCOMPARE(mame::titleOf(QStringLiteral("Truxton / Tatsujin")), QStringLiteral("Truxton"));
        QCOMPARE(mame::titleOf(QStringLiteral("Samurai Shodown II / Shin Samurai Spirits (NGM-063 ~ NGH-063)")),
                 QStringLiteral("Samurai Shodown II"));
        QCOMPARE(mame::titleOf(QStringLiteral("Metal Slug - Super Vehicle-001")),
                 QStringLiteral("Metal Slug - Super Vehicle-001"));
        QCOMPARE(mame::titleOf(QStringLiteral("(prototype)")), QStringLiteral("(prototype)"));

        QCOMPARE(mame::systemOf(QStringLiteral("capcom/cps1.cpp")), QStringLiteral("CPS-1"));
        QCOMPARE(mame::systemOf(QStringLiteral("cps2.cpp")), QStringLiteral("CPS-2"));
        QCOMPARE(mame::systemOf(QStringLiteral("neogeo/neogeo.cpp")), QStringLiteral("Neo-Geo"));
        QCOMPARE(mame::systemOf(QStringLiteral("sega/naomi.cpp")), QStringLiteral("NAOMI"));
        QCOMPARE(mame::systemOf(QStringLiteral("nintendo/dkong.cpp")), QStringLiteral("DKONG"));
        QCOMPARE(mame::systemOf(QString()), QString());

        // The genre arcade players know a game by, from its category.
        const std::pair<const char *, const char *> kGenres[] = {
            { "Fighter / Versus", "Fighting" },                  // Street Fighter II
            { "Fighter / Versus * Mature *", "Fighting" },       // Mortal Kombat
            { "Fighter / 3D", "Fighting" },
            { "Fighter / 2.5D", "Beat 'em Up" },                 // Final Fight
            { "Fighter / 2D", "Beat 'em Up" },                   // Kung-Fu Master
            { "Platform / Fighter", "Beat 'em Up" },             // China Gate
            { "Platform / Shooter Scrolling", "Run and Gun" },   // Metal Slug
            { "Shooter / Walking", "Run and Gun" },              // Commando
            { "Shooter / Flying Vertical", "Shmup" },            // 1942
            { "Shooter / Gallery", "Shmup" },                    // Galaga
            { "Shooter / Misc. Horizontal", "Shmup" },
            { "Shooter / Gun", "Light Gun" },                    // Operation Wolf
            { "Shooter / Field", "Shooter" },
            { "Platform / Run Jump", "Platform" },               // Donkey Kong
            { "Platform / Fighter Scrolling", "Platform" },      // Shinobi
            { "Driving / Race (chase view)", "Racing" },         // Out Run
            { "Climbing / Building", "Climbing" },               // Crazy Climber
            { "Maze / Collect", "Maze" },
            { "Puzzle", "Puzzle" },
        };
        for (const auto &[category, genre] : kGenres)
            QCOMPARE(mame::genreOf(QString::fromUtf8(category)), QString::fromUtf8(genre));

        // [Category]'s lines only, each set's genre by its category.
        const QString catver = m_data.path() + QStringLiteral("/catver-test.ini");
        writeFile(catver, "[FOLDER_SETTINGS]\nRootFolderIcon mame\n\n[ROOT_FOLDER]\n\n[Category]\n"
                          "sf2=Fighter / Versus\nmk=Fighter / Versus * Mature *\npuckman=Maze / Collect\n"
                          "tetris=Puzzle\n;note=Not a game\n\n[VerAdded]\nsf2=0.30\n");
        const QHash<QString, QString> genres = mame::readGenres(catver);
        QCOMPARE(genres.size(), 4);
        QCOMPARE(genres.value(QStringLiteral("sf2")), QStringLiteral("Fighting"));
        QCOMPARE(genres.value(QStringLiteral("mk")), QStringLiteral("Fighting"));
        QCOMPARE(genres.value(QStringLiteral("puckman")), QStringLiteral("Maze"));
        QCOMPARE(genres.value(QStringLiteral("tetris")), QStringLiteral("Puzzle"));
        QFile::remove(catver);
        QVERIFY(mame::readGenres(catver).isEmpty());

        // Player 1's coin pressed that many times, from five seconds in, once
        // whatever resets the game (MAME runs the script again at each).
        const QString script = mame::coinScript(3);
        QVERIFY(script.contains(QStringLiteral("if osdos_coins_run then return end\nosdos_coins_run = true\n")));
        QVERIFY(script.contains(QStringLiteral("local left = 3\n")));
        QVERIFY(script.contains(QStringLiteral("local nextAt, hold, gap = 5, 0.1, 0.2\n")));
        QVERIFY(script.contains(QStringLiteral("token_to_input_type(\"COIN1\")")));
    }

    // The filters: All Games, the lists, the genres catver.ini gives (a
    // clone's its parent's), players and decades, Not Recognized; a page of
    // one, where a game is in it, what a game is, its picture (looked for
    // once a list), and the settings' choices. While MAME is asked, only the
    // lists.
    void filters() {
        const QString module = QStringLiteral("com.osdos.arcade");
        AppCore core(m_data.path(), m_data.path());
        const auto restore = qScopeGuard([this] {
            QFile::remove(m_games + QStringLiteral("/catver.ini"));
            QDir(m_games + QStringLiteral("/snap")).removeRecursively();
            QDir(m_data.path() + QStringLiteral("/arcade/snap/galaga")).removeRecursively();
            QFile::remove(m_data.path() + QStringLiteral("/lists.json"));
        });
        writeFile(m_games + QStringLiteral("/catver.ini"),
                  "[Category]\nsf2=Fighter / Versus\nkinst=Fighter / Versus\nxmen6p=Fighter / 2.5D\n"
                  "puckman=Maze / Collect\ngalaga=Shooter / Flying Vertical\n");
        // A picture beside the games, Pac-Man's parent's, and one MAME took.
        writeFile(m_games + QStringLiteral("/snap/puckman.png"), "png");
        writeFile(m_data.path() + QStringLiteral("/arcade/snap/galaga/0000.png"), "png");
        QFile::remove(m_data.path() + QStringLiteral("/lists.json"));
        core.add_to_list(module, QStringLiteral("favorites"),
                         { { QStringLiteral("name"), QStringLiteral("Galaga (Namco rev. B)") },
                           { QStringLiteral("path"), QStringLiteral("game:galaga") } });
        core.add_to_list(module, QStringLiteral("favorites"),
                         { { QStringLiteral("name"), QStringLiteral("Street Fighter II") },
                           { QStringLiteral("path"), QStringLiteral("game:sf2") } });
        core.add_to_list(module, QStringLiteral("recent"), { { QStringLiteral("path"), QStringLiteral("game:pacman") } });
        core.add_to_list(module, QStringLiteral("recent"), { { QStringLiteral("path"), QStringLiteral("game:gone") } });

        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        refreshed(arcade);
        const auto described = [](const QVariantList &filters) {
            QStringList list;
            for (const QVariant &v : filters) {
                const QVariantMap f = v.toMap();
                list << QStringLiteral("%1 %2 %3").arg(f.value(QStringLiteral("id")).toString(),
                                                      f.value(QStringLiteral("name")).toString())
                            .arg(f.value(QStringLiteral("count")).toInt());
            }
            return list;
        };
        QCOMPARE(described(arcade.filters()), QStringList({
            QStringLiteral("all All Games 7"), QStringLiteral("favorites Favorites 2"),
            QStringLiteral("recent Recently Played 1"), QStringLiteral("genres/Beat 'em Up Beat 'em Up 1"),
            QStringLiteral("genres/Fighting Fighting 2"), QStringLiteral("genres/Maze Maze 1"),
            QStringLiteral("genres/Shmup Shmup 1"),
            QStringLiteral("players/1 1 Player 1"), QStringLiteral("players/2 2 Players 5"),
            QStringLiteral("players/6 6 Players 1"), QStringLiteral("years/1980s 1980s 3"),
            QStringLiteral("years/1990s 1990s 4"), QStringLiteral("unknown Not Recognized 2") }));

        // A page: the games' titles, the favourites marked.
        const QVariantList first = arcade.filterGames(QStringLiteral("all"), 0, 3);
        QCOMPARE(names(first), QStringList({ QStringLiteral("Ace"), QStringLiteral("Galaga"),
                                             QStringLiteral("Killer Instinct") }));
        QCOMPARE(first.at(1).toMap().value(QStringLiteral("set")).toString(), QStringLiteral("galaga"));
        QVERIFY(first.at(1).toMap().value(QStringLiteral("favorite")).toBool());
        QVERIFY(!first.at(0).toMap().value(QStringLiteral("favorite")).toBool());
        QCOMPARE(names(arcade.filterGames(QStringLiteral("all"), 5, 10)),
                 QStringList({ QStringLiteral("Street Fighter II: The World Warrior"), QStringLiteral("X-Men") }));
        QVERIFY(arcade.filterGames(QStringLiteral("all"), 7, 3).isEmpty());
        QVERIFY(arcade.filterGames(QStringLiteral("all"), -1, 3).isEmpty());
        QCOMPARE(names(arcade.filterGames(QStringLiteral("favorites"), 0, 10)),
                 QStringList({ QStringLiteral("Galaga"), QStringLiteral("Street Fighter II: The World Warrior") }));
        QCOMPARE(names(arcade.filterGames(QStringLiteral("recent"), 0, 10)), QStringList({ QStringLiteral("Pac-Man") }));
        QCOMPARE(names(arcade.filterGames(QStringLiteral("genres/Maze"), 0, 10)), QStringList({ QStringLiteral("Pac-Man") }));
        const QVariantList unknown = arcade.filterGames(QStringLiteral("unknown"), 0, 10);
        QCOMPARE(names(unknown), QStringList({ QStringLiteral("notagame.zip"), QStringLiteral("Street Fighter.zip") }));
        QVERIFY(unknown.first().toMap().value(QStringLiteral("unknown")).toBool());
        QVERIFY(arcade.filterGames(QStringLiteral("genres/Nothing"), 0, 10).isEmpty());

        QCOMPARE(arcade.filterIndex(QStringLiteral("all"), QStringLiteral("pacman")), 4);
        QCOMPARE(arcade.filterIndex(QStringLiteral("genres/Fighting"), QStringLiteral("sf2")), 1);
        QCOMPARE(arcade.filterIndex(QStringLiteral("favorites"), QStringLiteral("pacman")), -1);

        // What a game is.
        const QVariantMap sf2 = arcade.gameDetails(QStringLiteral("sf2"));
        QCOMPARE(sf2.value(QStringLiteral("name")).toString(), QStringLiteral("Street Fighter II: The World Warrior"));
        QCOMPARE(sf2.value(QStringLiteral("system")).toString(), QStringLiteral("CPS-1"));
        QCOMPARE(sf2.value(QStringLiteral("maker")).toString(), QStringLiteral("Capcom"));
        QCOMPARE(sf2.value(QStringLiteral("year")).toString(), QStringLiteral("1991"));
        QCOMPARE(sf2.value(QStringLiteral("players")).toInt(), 2);
        QCOMPARE(sf2.value(QStringLiteral("vertical")).toBool(), false);
        QCOMPARE(sf2.value(QStringLiteral("picture")).toString(), QString());
        const QVariantMap pacman = arcade.gameDetails(QStringLiteral("pacman"));
        QCOMPARE(pacman.value(QStringLiteral("system")).toString(), QStringLiteral("PACMAN"));
        QCOMPARE(pacman.value(QStringLiteral("maker")).toString(), QStringLiteral("Namco"));
        QVERIFY(pacman.value(QStringLiteral("vertical")).toBool());
        QCOMPARE(pacman.value(QStringLiteral("picture")).toString(),
                 QUrl::fromLocalFile(m_games + QStringLiteral("/snap/puckman.png")).toString());
        QCOMPARE(arcade.gameDetails(QStringLiteral("galaga")).value(QStringLiteral("picture")).toString(),
                 QUrl::fromLocalFile(m_data.path() + QStringLiteral("/arcade/snap/galaga/0000.png")).toString());
        // A picture is looked for once, till the list opens again.
        writeFile(m_games + QStringLiteral("/snap/sf2.png"), "png");
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("picture")).toString(), QString());
        refreshed(arcade);
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("picture")).toString(),
                 QUrl::fromLocalFile(m_games + QStringLiteral("/snap/sf2.png")).toString());
        const QVariantMap notagame = arcade.gameDetails(QStringLiteral("notagame"));
        QVERIFY(notagame.value(QStringLiteral("unknown")).toBool());
        QCOMPARE(notagame.value(QStringLiteral("name")).toString(), QStringLiteral("notagame.zip"));
        QCOMPARE(notagame.value(QStringLiteral("detail")).toString(),
                 QStringLiteral("MAME has no game called notagame\nIts set may be for another version of MAME"));
        QVERIFY(arcade.gameDetails(QStringLiteral("gone")).isEmpty());

        // Settings → Arcade's choices: Start Filter's, the lists whether or
        // not they hold a game, and Play at Startup's, the favourites.
        QSignalSpy options(&arcade, &ArcadeBackend::dynamicOptionsReady);
        arcade.startFilterOptions();
        arcade.startupGameOptions();
        QCOMPARE(options.size(), 2);
        QStringList ids;
        for (const QVariant &v : options.at(0).at(1).toList())
            ids << v.toMap().value(QStringLiteral("id")).toString();
        QCOMPARE(options.at(0).at(0).toString(), QStringLiteral("start_filter"));
        QCOMPARE(ids, QStringList({ QStringLiteral("all"), QStringLiteral("favorites"), QStringLiteral("recent"),
                                    QStringLiteral("genres/Beat 'em Up"), QStringLiteral("genres/Fighting"),
                                    QStringLiteral("genres/Maze"), QStringLiteral("genres/Shmup"),
                                    QStringLiteral("players/1"),
                                    QStringLiteral("players/2"), QStringLiteral("players/6"),
                                    QStringLiteral("years/1980s"), QStringLiteral("years/1990s") }));
        QCOMPARE(options.at(1).at(0).toString(), QStringLiteral("play_at_startup"));
        QStringList games;
        for (const QVariant &v : options.at(1).at(1).toList())
            games << v.toMap().value(QStringLiteral("id")).toString() + QLatin1Char('=') + v.toMap().value(QStringLiteral("label")).toString();
        QCOMPARE(games, QStringList({ QStringLiteral("=None"), QStringLiteral("galaga=Galaga"),
                                      QStringLiteral("sf2=Street Fighter II") }));

        // Asked again: only the lists, then all of them once MAME has
        // answered.
        QSignalSpy changed(&arcade, &ArcadeBackend::gamesChanged);
        arcade.rescan();
        QVERIFY(arcade.scanning());
        QCOMPARE(described(arcade.filters()), QStringList({ QStringLiteral("favorites Favorites 2"),
                                                            QStringLiteral("recent Recently Played 1") }));
        QVERIFY(arcade.filterGames(QStringLiteral("all"), 0, 3).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
        QVERIFY(!changed.isEmpty());
        QCOMPARE(arcade.filters().size(), 13);
    }

    // Settings → Arcade → Menu Filters: its page, the lists and the places
    // under their headings, a publisher's off; the filters the Arcade screen
    // goes round and Start Filter offers, as turned on or off there (the row
    // saved whole each time, as its page saves it), a publisher's id with
    // dots in it among them. A Start Filter saved under one of catver.ini's
    // own genres is the genre it became.
    void menuFilters() {
        const QString module = QStringLiteral("com.osdos.arcade");
        AppCore core(m_data.path(), m_data.path());
        const auto restore = qScopeGuard([this, &core, &module] {
            QFile::remove(m_games + QStringLiteral("/catver.ini"));
            QFile::remove(m_data.path() + QStringLiteral("/lists.json"));
            core.save_setting(module, QStringLiteral("menu_filters"), QVariantMap());
            core.save_setting(module, QStringLiteral("start_filter"), QString());
        });
        QFile::remove(m_data.path() + QStringLiteral("/lists.json"));
        writeFile(m_games + QStringLiteral("/catver.ini"),
                  "[Category]\nsf2=Fighter / Versus\nxmen6p=Fighter / 2.5D\ngalaga=Shooter / Gallery\n");
        core.save_setting(module, QStringLiteral("start_filter"), QStringLiteral("genres/Fighter"));

        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        QCOMPARE(core.get_setting(module, QStringLiteral("start_filter")).toString(), QStringLiteral("genres/Fighting"));
        // As main.cpp's registerModule() wires it.
        connect(&core, &AppCore::moduleSettingChanged, &arcade, &ArcadeBackend::onSettingChanged);
        refreshed(arcade);
        const auto ids = [](const QVariantList &list) {
            QStringList all;
            for (const QVariant &v : list)
                all << v.toMap().value(QStringLiteral("id")).toString();
            return all;
        };

        QSignalSpy options(&arcade, &ArcadeBackend::dynamicOptionsReady);
        arcade.menuFilterOptions();
        QCOMPARE(options.size(), 1);
        QCOMPARE(options.at(0).at(0).toString(), QStringLiteral("menu_filters"));
        QStringList page;
        for (const QVariant &v : options.at(0).at(1).toList()) {
            const QVariantMap o = v.toMap();
            if (o.value(QStringLiteral("heading")).toBool())
                page << o.value(QStringLiteral("label")).toString().toUpper();
            else
                page << o.value(QStringLiteral("id")).toString() + QLatin1Char('=') + o.value(QStringLiteral("label")).toString()
                            + (o.value(QStringLiteral("default")).toBool() ? QString() : QStringLiteral(" off"));
        }
        QCOMPARE(page, QStringList({
            QStringLiteral("LISTS"), QStringLiteral("favorites=Favorites"), QStringLiteral("recent=Recently Played"),
            QStringLiteral("unknown=Not Recognized"),
            QStringLiteral("GENRES"), QStringLiteral("genres/Beat 'em Up=Beat 'em Up"),
            QStringLiteral("genres/Fighting=Fighting"), QStringLiteral("genres/Shmup=Shmup"),
            QStringLiteral("PLAYERS"), QStringLiteral("players/1=1 Player"), QStringLiteral("players/2=2 Players"),
            QStringLiteral("players/6=6 Players"),
            QStringLiteral("YEARS"), QStringLiteral("years/1980s=1980s"), QStringLiteral("years/1990s=1990s"),
            QStringLiteral("PUBLISHERS"), QStringLiteral("makers/Capcom=Capcom off"), QStringLiteral("makers/Konami=Konami off"),
            QStringLiteral("makers/Namco=Namco off"), QStringLiteral("makers/Nazca=Nazca off"),
            QStringLiteral("makers/Rare=Rare off") }));
        QCOMPARE(ids(arcade.filters()), QStringList({
            QStringLiteral("all"), QStringLiteral("genres/Beat 'em Up"), QStringLiteral("genres/Fighting"),
            QStringLiteral("genres/Shmup"), QStringLiteral("players/1"), QStringLiteral("players/2"),
            QStringLiteral("players/6"), QStringLiteral("years/1980s"), QStringLiteral("years/1990s"),
            QStringLiteral("unknown") }));

        // A publisher on, a genre and Not Recognized off. Start Filter offers
        // what is shown.
        QSignalSpy changed(&arcade, &ArcadeBackend::gamesChanged);
        QVariantMap shown{ { QStringLiteral("makers/Namco"), true } };
        core.save_setting(module, QStringLiteral("menu_filters"), shown);
        shown.insert(QStringLiteral("genres/Fighting"), false);
        shown.insert(QStringLiteral("unknown"), false);
        core.save_setting(module, QStringLiteral("menu_filters"), shown);
        QCOMPARE(changed.size(), 2);
        QCOMPARE(ids(arcade.filters()), QStringList({
            QStringLiteral("all"), QStringLiteral("genres/Beat 'em Up"), QStringLiteral("genres/Shmup"),
            QStringLiteral("players/1"), QStringLiteral("players/2"), QStringLiteral("players/6"),
            QStringLiteral("years/1980s"), QStringLiteral("years/1990s"), QStringLiteral("makers/Namco") }));
        QCOMPARE(arcade.filterGames(QStringLiteral("makers/Namco"), 0, 10).size(), 2);
        options.clear();
        arcade.startFilterOptions();
        QCOMPARE(ids(options.at(0).at(1).toList()), QStringList({
            QStringLiteral("all"), QStringLiteral("favorites"), QStringLiteral("recent"),
            QStringLiteral("genres/Beat 'em Up"), QStringLiteral("genres/Shmup"), QStringLiteral("players/1"),
            QStringLiteral("players/2"), QStringLiteral("players/6"), QStringLiteral("years/1980s"),
            QStringLiteral("years/1990s"), QStringLiteral("makers/Namco") }));

        // A heading's all at once: the players off, Favorites and Recently
        // Played too.
        for (const char *id : { "players/1", "players/2", "players/6", "favorites", "recent" })
            shown.insert(QLatin1String(id), false);
        core.save_setting(module, QStringLiteral("menu_filters"), shown);
        QCOMPARE(ids(arcade.filters()), QStringList({
            QStringLiteral("all"), QStringLiteral("genres/Beat 'em Up"), QStringLiteral("genres/Shmup"),
            QStringLiteral("years/1980s"), QStringLiteral("years/1990s"), QStringLiteral("makers/Namco") }));
        core.add_to_list(module, QStringLiteral("favorites"), { { QStringLiteral("path"), QStringLiteral("game:galaga") } });
        QVERIFY(!ids(arcade.filters()).contains(QStringLiteral("favorites")));
        options.clear();
        arcade.startFilterOptions();
        QCOMPARE(ids(options.at(0).at(1).toList()).mid(0, 2),
                 QStringList({ QStringLiteral("all"), QStringLiteral("genres/Beat 'em Up") }));

        // A publisher with dots in its name, saved and read back as it is.
        shown.insert(QStringLiteral("makers/U.S. Games"), true);
        core.save_setting(module, QStringLiteral("menu_filters"), shown);
        QVERIFY(core.get_setting(module, QStringLiteral("menu_filters")).toMap()
                    .value(QStringLiteral("makers/U.S. Games")).toBool());
        AppCore again(m_data.path(), m_data.path());
        ArcadeBackend reread(m_data.path(), m_data.path(), &again, nullptr);
        refreshed(reread);
        QVERIFY(ids(reread.filters()).contains(QStringLiteral("makers/Namco")));
        QVERIFY(!ids(reread.filters()).contains(QStringLiteral("genres/Fighting")));
    }

    // A folder of sets MAME knows none of has Not Recognized in All Games'
    // place, though Menu Filters hides it: the screen then says why.
    void onlyUnrecognized() {
        const QString module = QStringLiteral("com.osdos.arcade");
        const QString folder = m_data.path() + QStringLiteral("/unrecognized");
        AppCore core(m_data.path(), m_data.path());
        const auto restore = qScopeGuard([&core, &module, &folder] {
            core.save_setting(module, QStringLiteral("games_directory"), QString());
            core.save_setting(module, QStringLiteral("menu_filters"), QVariantMap());
            QDir(folder).removeRecursively();
        });
        writeFile(folder + QStringLiteral("/notagame.zip"));
        writeFile(folder + QStringLiteral("/nothing.zip"));
        core.save_setting(module, QStringLiteral("games_directory"), folder);
        core.save_setting(module, QStringLiteral("menu_filters"), QVariantMap{ { QStringLiteral("unknown"), false } });
        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        refreshed(arcade);
        const QVariantList filters = arcade.filters();
        QCOMPARE(filters.size(), 1);
        QCOMPARE(filters.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("unknown"));
        QCOMPARE(filters.first().toMap().value(QStringLiteral("count")).toInt(), 2);
    }

    // The credits bought go in as the game starts, nine at most: MAME runs
    // the script that puts them in.
    void credits() {
        QFile::remove(m_log);
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);
        const QString script = m_data.path() + QStringLiteral("/arcade/coins.lua");
        const auto coins = [&script] {
            QFile f(script);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        QSignalSpy finished(&arcade, &ArcadeBackend::finished);
        QVERIFY(arcade.play(QStringLiteral("galaga"), 3));
        QVERIFY(finished.wait(10000));
        QVERIFY2(logLines(QStringLiteral("play")).last().endsWith(QStringLiteral(" -autoboot_script ") + script),
                 qPrintable(logLines(QStringLiteral("play")).last()));
        QVERIFY(coins().contains(QStringLiteral("local left = 3\n")));
        QVERIFY(arcade.play(QStringLiteral("galaga"), 20));
        QVERIFY(finished.wait(10000));
        QVERIFY(coins().contains(QStringLiteral("local left = 9\n")));
        QVERIFY(arcade.play(QStringLiteral("galaga")));
        QVERIFY(finished.wait(10000));
        QVERIFY(!logLines(QStringLiteral("play")).last().contains(QStringLiteral("autoboot")));
    }

    // How long each game has been played, kept for the next start; not a
    // game MAME refused.
    void playTime() {
        QFile::remove(m_data.path() + QStringLiteral("/arcade/playtime.json"));
        {
            ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
            refreshed(arcade);
            QCOMPARE(arcade.gameDetails(QStringLiteral("galaga")).value(QStringLiteral("playTime")).toLongLong(), 0);
            qputenv("MAME_SECONDS", "1.2");
            QCOMPARE(played(arcade, QStringLiteral("galaga")).at(1).toString(), QStringLiteral("ok"));
            qunsetenv("MAME_SECONDS");
            const qint64 seconds = arcade.gameDetails(QStringLiteral("galaga")).value(QStringLiteral("playTime")).toLongLong();
            QVERIFY2(seconds >= 1 && seconds <= 2, qPrintable(QString::number(seconds)));
            QCOMPARE(played(arcade, QStringLiteral("mslug")).at(0).toInt(), 2);
            QCOMPARE(arcade.gameDetails(QStringLiteral("mslug")).value(QStringLiteral("playTime")).toLongLong(), 0);
        }
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);
        QVERIFY(arcade.gameDetails(QStringLiteral("galaga")).value(QStringLiteral("playTime")).toLongLong() >= 1);
    }

    // Settings → Arcade → Play at Startup is the app's startup favourite
    // while it is one of the module's games, whichever screen set it.
    void startupGame() {
        const QString module = QStringLiteral("com.osdos.arcade");
        AppCore core(m_data.path(), m_data.path());
        const auto restore = qScopeGuard([this, &core, &module] {
            core.save_setting(QString(), QStringLiteral("startup_favorite"), QString());
            core.save_setting(module, QStringLiteral("play_at_startup"), QString());
            QFile::remove(m_data.path() + QStringLiteral("/lists.json"));
        });
        QFile::remove(m_data.path() + QStringLiteral("/lists.json"));
        core.add_to_list(module, QStringLiteral("favorites"),
                         { { QStringLiteral("name"), QStringLiteral("Galaga (Namco rev. B)") },
                           { QStringLiteral("path"), QStringLiteral("game:galaga") } });
        core.save_setting(QString(), QStringLiteral("startup_favorite"), QString());
        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);

        // Chosen here: the app's, as a game's options make it.
        arcade.onSettingChanged(module, QStringLiteral("play_at_startup"), QStringLiteral("galaga"));
        const QVariantMap startup = core.get_setting(QString(), QStringLiteral("startup_favorite")).toMap();
        QCOMPARE(startup.value(QStringLiteral("module")).toString(), module);
        QCOMPARE(startup.value(QStringLiteral("path")).toString(), QStringLiteral("game:galaga"));
        QCOMPARE(startup.value(QStringLiteral("name")).toString(), QStringLiteral("Galaga (Namco rev. B)"));
        QCOMPARE(core.get_setting(module, QStringLiteral("play_at_startup")).toString(), QStringLiteral("galaga"));

        // Another module's favourite chosen: none here, and none chosen here
        // leaves it.
        core.save_setting(QString(), QStringLiteral("startup_favorite"),
                          QVariantMap{ { QStringLiteral("module"), QStringLiteral("com.osdos.local_files") },
                                       { QStringLiteral("path"), QStringLiteral("/media/film.mkv") },
                                       { QStringLiteral("name"), QStringLiteral("Film") } });
        QCOMPARE(core.get_setting(module, QStringLiteral("play_at_startup")).toString(), QString());
        arcade.onSettingChanged(module, QStringLiteral("play_at_startup"), QString());
        QCOMPARE(core.get_setting(QString(), QStringLiteral("startup_favorite")).toMap().value(QStringLiteral("module")).toString(),
                 QStringLiteral("com.osdos.local_files"));

        // One of this module's, then none: no startup favourite at all.
        arcade.onSettingChanged(module, QStringLiteral("play_at_startup"), QStringLiteral("galaga"));
        arcade.onSettingChanged(module, QStringLiteral("play_at_startup"), QString());
        QCOMPARE(core.get_setting(QString(), QStringLiteral("startup_favorite")).toString(), QString());

        // Set while the module wasn't there to see it: shown as it is from
        // the start.
        core.save_setting(module, QStringLiteral("play_at_startup"), QStringLiteral("pacman"));
        QVariantMap sf2{ { QStringLiteral("module"), module }, { QStringLiteral("path"), QStringLiteral("game:sf2") },
                         { QStringLiteral("name"), QStringLiteral("Street Fighter II") } };
        QJsonObject config;
        {
            QFile f(m_data.path() + QStringLiteral("/config.json"));
            QVERIFY(f.open(QIODevice::ReadOnly));
            config = QJsonDocument::fromJson(f.readAll()).object();
        }
        QJsonObject app = config.value(QStringLiteral("app")).toObject();
        app.insert(QStringLiteral("startup_favorite"), QJsonObject::fromVariantMap(sf2));
        config.insert(QStringLiteral("app"), app);
        writeFile(m_data.path() + QStringLiteral("/config.json"), QJsonDocument(config).toJson());
        ArcadeBackend again(m_data.path(), m_data.path(), &core, nullptr);
        QCOMPARE(core.get_setting(module, QStringLiteral("play_at_startup")).toString(), QStringLiteral("sf2"));
    }

    // games.json from before it kept each machine's source file is read as
    // none: MAME is asked about every set again.
    void oldCache() {
        {
            ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
            refreshed(arcade);
        }
        const QString path = m_data.path() + QStringLiteral("/arcade/games.json");
        QJsonObject cache;
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            cache = QJsonDocument::fromJson(f.readAll()).object();
        }
        QCOMPARE(cache.value(QStringLiteral("version")).toInt(), 2);
        cache.remove(QStringLiteral("version"));
        writeFile(path, QJsonDocument(cache).toJson());
        QFile::remove(m_log);
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);
        QCOMPARE(logLines(QStringLiteral("listxml")),
                 QStringList({ QStringLiteral("listxml acefruit galaga kinst mslug neogeo notagame pacman sf2 xmen6p") }));
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("system")).toString(), QStringLiteral("CPS-1"));
    }
};


QTEST_GUILESS_MAIN(ArcadeTest)
#include "arcade_test.moc"
