#include "AppCore.h"
#include "input/ControllerProfile.h"
#include "modules/arcade/ArcadeBackend.h"
#include "modules/arcade/FbneoInstaller.h"
#include "modules/arcade/Libretro.h"
#include "modules/arcade/Mame.h"
#include "modules/arcade/RetroAchievements.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QUrl>
#include <QUrlQuery>
#include <QtTest>

// The Arcade module against a stand-in for MAME (a shell script in the data
// folder's bin, where the module looks first): the games folder read off the
// GUI thread, what MAME's -listxml says of its sets, read as it comes and
// kept until MAME changes,
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
    // noted as asked and as answered, and a game, whose arguments, folder,
    // display card and controller file it notes. mslug and kinst lack files; sf2 can't open its picture;
    // slowgame takes a second, as does asking about a_slowlist half of one;
    // any other game takes $MAME_SECONDS.
    static constexpr const char *kFakeMame = R"SH(#!/bin/sh
machine() {
	case "$1" in
	pacman) echo '<machine name="pacman" sourcefile="pacman/pacman.cpp" cloneof="puckman" romof="puckman"><description>Pac-Man (Midway)</description><year>1980</year><manufacturer>Namco (Midway license)</manufacturer><rom name="pacman.6e" size="4096"/><display tag="screen" type="raster" rotate="90" width="288" height="224"/><input players="2" coins="2"><control type="joy" player="1"/></input><driver status="good"/></machine>' ;;
	galaga) echo '<machine name="galaga" sourcefile="namco/galaga.cpp"><description>Galaga (Namco rev. B)</description><year>1981</year><manufacturer>Namco</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	mslug) echo '<machine name="mslug" sourcefile="neogeo/neogeo.cpp" romof="neogeo"><description>Metal Slug - Super Vehicle-001</description><year>1996</year><manufacturer>Nazca</manufacturer><display tag="screen" type="raster" rotate="0" width="320" height="224"/><input players="2"/><driver status="good"/></machine>' ;;
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
	echo "answered" >> "$MAME_LOG"
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

    // A file of data/arcade: a stand-in for RetroArch (retroarch),
    // FinalBurn Neo's list of games (fbneo-arcade.dat) and the netplay
    // lobby's answer (lobby.json).
    static QByteArray fixture(const char *name) {
        QFile f(QStringLiteral(TESTS_DIR "/data/arcade/") + QLatin1String(name));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    QString mamePath() const { return m_data.path() + QStringLiteral("/bin/mame"); }

    void writeFile(const QString &path, const QByteArray &data = QByteArray()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
        f.write(data);
    }

    static QByteArray contents(const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
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

    // A stand-in for RetroAchievements' Web API and media server: the API's
    // answers by their path and query, the key left out (key1 the only one
    // it takes), the media server's by their path; every request noted.
    struct StandIn {
        QTcpServer server;
        QHash<QByteArray, QByteArray> answers;
        QStringList asked;
        QUrl api() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/API/").arg(server.serverPort())); }
        QUrl media() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/media/").arg(server.serverPort())); }
    };
    static bool serve(StandIn &standIn) {
        if (!standIn.server.listen(QHostAddress::LocalHost))
            return false;
        connect(&standIn.server, &QTcpServer::newConnection, &standIn.server, [&standIn] {
            QTcpSocket *socket = standIn.server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &standIn] {
                const QUrl url(QString::fromLatin1(socket->readAll().split(' ').value(1)));
                QUrlQuery query(url);
                const QString key = query.queryItemValue(QStringLiteral("y"));
                query.removeQueryItem(QStringLiteral("y"));
                const bool api = url.path().startsWith(QLatin1String("/API/"));
                const QByteArray path = (api ? url.path() + QLatin1Char('?') + query.toString() : url.path()).toLatin1();
                standIn.asked << QString::fromLatin1(path);
                QByteArray status = "200 OK";
                QByteArray body = standIn.answers.value(path);
                if (api && key != QLatin1String("key1"))
                    status = "401 Unauthorized", body = "{}";
                else if (!standIn.answers.contains(path))
                    status = "404 Not Found", body.clear();
                socket->write("HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(body.size())
                              + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        return true;
    }
    // A picture as RetroAchievements' media server sends one: its first bytes.
    static QByteArray png(const QByteArray &mark) { return QByteArray("\x89PNG\r\n\x1a\n", 8) + mark; }
    static QByteArray jpeg(const QByteArray &mark) { return QByteArray("\xff\xd8\xff\xe0", 4) + mark; }
    static QByteArray hashOf(const char *file) { return libretro::achievementsHash(QLatin1String(file)).toLatin1(); }

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
        // No catver.ini downloaded but where a test asks for one.
        qputenv("OSDOS_GENRES_URL", "");
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
        qunsetenv("OSDOS_GENRES_URL");
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
        // A company under its own name, whichever of its arms, mergers and
        // legal forms made the game.
        QCOMPARE(mame::makerOf(QStringLiteral("Nintendo of America")), QStringLiteral("Nintendo"));
        QCOMPARE(mame::makerOf(QStringLiteral("SNK Playmore")), QStringLiteral("SNK"));
        QCOMPARE(mame::makerOf(QStringLiteral("SNK Corporation of America")), QStringLiteral("SNK"));
        QCOMPARE(mame::makerOf(QStringLiteral("Taito America Corporation (Romstar license)")), QStringLiteral("Taito"));
        QCOMPARE(mame::makerOf(QStringLiteral("Taito Corporation Japan")), QStringLiteral("Taito"));
        QCOMPARE(mame::makerOf(QStringLiteral("Data East USA, Inc.")), QStringLiteral("Data East"));
        QCOMPARE(mame::makerOf(QStringLiteral("Capcom U.S.A.")), QStringLiteral("Capcom"));
        QCOMPARE(mame::makerOf(QStringLiteral("Video System Co.")), QStringLiteral("Video System"));
        QCOMPARE(mame::makerOf(QStringLiteral("Sega Amusements Europe")), QStringLiteral("Sega"));
        QCOMPARE(mame::makerOf(QStringLiteral("Konami Industry Co. Ltd.")), QStringLiteral("Konami"));
        QCOMPARE(mame::makerOf(QStringLiteral("Bally Midway")), QStringLiteral("Midway"));
        QCOMPARE(mame::makerOf(QStringLiteral("Midway Manufacturing Co.")), QStringLiteral("Midway"));
        QCOMPARE(mame::makerOf(QStringLiteral("Atari Games")), QStringLiteral("Atari"));
        QCOMPARE(mame::makerOf(QStringLiteral("Technos Japan")), QStringLiteral("Technos"));
        QCOMPARE(mame::makerOf(QStringLiteral("American Sammy")), QStringLiteral("Sammy"));
        // A name that is only an ending, or that only begins like a company
        // or ends like one, as it is.
        QCOMPARE(mame::makerOf(QStringLiteral("Universal")), QStringLiteral("Universal"));
        QCOMPARE(mame::makerOf(QStringLiteral("Segasa")), QStringLiteral("Segasa"));
        QCOMPARE(mame::makerOf(QStringLiteral("American Laser Games")), QStringLiteral("American Laser Games"));
        QCOMPARE(mame::makerOf(QStringLiteral("U.S. Games")), QStringLiteral("U.S. Games"));

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
        // Coin and Start leave the game on every player's controller, so
        // a cabinet whose player 1 pad is unplugged still has a way out.
        QVERIFY(config.contains(QStringLiteral(">KEYCODE_ESC OR JOYCODE_1_SELECT JOYCODE_1_START"
                                               " OR JOYCODE_2_SELECT JOYCODE_2_START"
                                               " OR JOYCODE_3_SELECT JOYCODE_3_START"
                                               " OR JOYCODE_5_SELECT JOYCODE_5_START<")));
        QVERIFY(mame::controllerConfig({ QString(), QStringLiteral("bbbb") })
                    .contains(QStringLiteral(">KEYCODE_ESC OR JOYCODE_1_SELECT JOYCODE_1_START"
                                             " OR JOYCODE_2_SELECT JOYCODE_2_START<")));
        QVERIFY(!mame::controllerConfig({}).contains(QStringLiteral("mapdevice")));
        QVERIFY(mame::controllerConfig({}).contains(QStringLiteral(">KEYCODE_ESC OR JOYCODE_1_SELECT JOYCODE_1_START<")));

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

        // The screen's shape, where its pixels aren't square, and a game's
        // lines whole only where one of each fits: a vertical game's lines
        // are its width before the rotation.
        const QStringList shape{ QStringLiteral("-aspect"), QStringLiteral("4:3") };
        const QStringList whole{ QStringLiteral("-nounevenstretch"), QStringLiteral("-unevenstretchx") };
        mame::Machine wide;
        wide.width = 384;
        wide.height = 224;
        mame::Machine tall = wide;
        tall.height = 256;
        mame::Machine vertical;
        vertical.rotate = 90;
        vertical.width = 288;
        vertical.height = 224;
        QCOMPARE(mame::screenArguments(wide, 240, QStringLiteral("4:3"), true), shape + whole);
        QCOMPARE(mame::screenArguments(tall, 240, QStringLiteral("4:3"), true), shape);
        QCOMPARE(mame::screenArguments(tall, 480, QStringLiteral("4:3"), true), shape + whole);
        QCOMPARE(mame::screenArguments(vertical, 240, QStringLiteral("4:3"), true), shape);
        QCOMPARE(mame::screenArguments(vertical, 1080, QString(), true), whole);
        QCOMPARE(mame::screenArguments(mame::Machine(), 240, QStringLiteral("4:3"), true), shape);
        QCOMPARE(mame::screenArguments(wide, 240, QStringLiteral("16:9"), false),
                 QStringList({ QStringLiteral("-aspect"), QStringLiteral("16:9") }));
        QVERIFY(mame::screenArguments(wide, 1080, QString(), false).isEmpty());
    }

    // -listxml read as it comes, a byte at a time as well as at once.
    void listXmlInChunks() {
        const QByteArray xml =
            "<?xml version=\"1.0\"?>\n<mame build=\"0.276 (test)\" debug=\"no\" mameconfig=\"10\">\n"
            "\t<machine name=\"xmen6p\" sourcefile=\"konami/xmen.cpp\" cloneof=\"xmen\"><description>X-Men &amp; Friends</description>"
            "<year>1992</year><manufacturer>Konami</manufacturer><rom name=\"a\"/>"
            "<display tag=\"l\" rotate=\"270\" width=\"288\" height=\"224\"/><display tag=\"r\" rotate=\"0\" width=\"320\" height=\"240\"/>"
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
            QCOMPARE(x.width, 288);
            QCOMPARE(x.height, 224);
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
            QCOMPARE(kept.width, 288);
            QCOMPARE(kept.height, 224);
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
        QSignalSpy reading(&arcade, &ArcadeBackend::readingChanged);
        arcade.refresh();
        QVERIFY(arcade.scanning());
        // The folder read, and MAME asked about its sets (the read's end is
        // waited for alone, so MAME can't have answered yet).
        QVERIFY(reading.wait(10000));
        QVERIFY(!arcade.reading());
        QVERIFY(arcade.scanning());
        ready.clear();
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
            refreshed(arcade);
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

    // The folder is read off the GUI thread: refresh() returns at once, the
    // list waiting for the read (scanning meanwhile); a refresh while it
    // reads reads again, and another folder chosen meanwhile is the one
    // read. Before the folder has been read a game plays all the same, and
    // the settings' choices come once it has; a snapshot MAME took in a game
    // is its picture as the game ends.
    void readOffThread() {
        const QString module = QStringLiteral("com.osdos.arcade");
        const QString other = m_data.path() + QStringLiteral("/other-games");
        const QString shots = m_data.path() + QStringLiteral("/arcade/snap/galaga");
        AppCore core(m_data.path(), m_data.path());
        const auto restore = qScopeGuard([this, &core, &module, &other, &shots] {
            core.save_setting(module, QStringLiteral("games_directory"), QString());
            QDir(other).removeRecursively();
            QDir(shots).removeRecursively();
            QFile::remove(m_games + QStringLiteral("/1943.zip"));
        });
        {
            ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
            QSignalSpy reading(&arcade, &ArcadeBackend::readingChanged);
            arcade.refresh();
            QVERIFY(arcade.reading());
            QVERIFY(arcade.scanning());
            QVERIFY(!arcade.requestEntries(QStringLiteral("all")).isValid());
            // The read done but not yet taken, which waits for the event
            // loop: a set put in the folder now is only in a read after it.
            QThreadPool::globalInstance()->waitForDone();
            QVERIFY(arcade.reading());
            writeFile(m_games + QStringLiteral("/1943.zip"));
            arcade.refresh();
            QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
            QVERIFY(!arcade.reading());
            QCOMPARE(reading.size(), 2);
            QVERIFY(names(arcade.requestEntries(QStringLiteral("unknown"))).contains(QStringLiteral("1943.zip")));
        }
        writeFile(other + QStringLiteral("/galaga.zip"));
        {
            ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
            connect(&core, &AppCore::moduleSettingChanged, &arcade, &ArcadeBackend::onSettingChanged);
            arcade.refresh();
            core.save_setting(module, QStringLiteral("games_directory"), other);
            QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
            QCOMPARE(names(arcade.requestEntries(QStringLiteral("all"))),
                     QStringList({ QStringLiteral("Galaga (Namco rev. B)") }));
            core.save_setting(module, QStringLiteral("games_directory"), QString());
        }
        QFile::remove(m_log);
        {
            ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
            QCOMPARE(played(arcade, QStringLiteral("galaga")).at(1).toString(), QStringLiteral("ok"));
            QVERIFY(!arcade.play(QStringLiteral("gone")));
            QCOMPARE(arcade.lastError(), QStringLiteral("The game is no longer in the games folder"));
            QSignalSpy options(&arcade, &ArcadeBackend::dynamicOptionsReady);
            arcade.startFilterOptions();
            QVERIFY(arcade.reading());
            QVERIFY(options.isEmpty());
            QTRY_COMPARE_WITH_TIMEOUT(options.size(), 1, 10000);
            QCOMPARE(options.first().at(0).toString(), QStringLiteral("start_filter"));
            QVERIFY(options.first().at(1).toList().size() > 1);
            // MAME isn't asked from the settings: that waits for the list.
            QVERIFY(logLines(QStringLiteral("listxml")).isEmpty());
        }
        {
            // Read the Games Again while the settings' read goes on: MAME
            // asked about every set once the folder has been read.
            ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
            arcade.startFilterOptions();
            QVERIFY(arcade.reading());
            arcade.rescan();
            QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
            QCOMPARE(logLines(QStringLiteral("listxml")),
                     QStringList({ QStringLiteral("listxml 1943 acefruit galaga kinst mslug neogeo notagame pacman sf2 xmen6p") }));
            QFile::remove(m_log);
        }
        {
            ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
            refreshed(arcade);
            QCOMPARE(arcade.gameDetails(QStringLiteral("galaga")).value(QStringLiteral("picture")).toString(), QString());
            writeFile(shots + QStringLiteral("/0001.png"), "png");
            QCOMPARE(played(arcade, QStringLiteral("galaga")).at(1).toString(), QStringLiteral("ok"));
            QCOMPARE(arcade.gameDetails(QStringLiteral("galaga")).value(QStringLiteral("picture")).toString(),
                     QUrl::fromLocalFile(shots + QStringLiteral("/0001.png")).toString());
        }
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

    // Settings → Arcade → Emulator Video and the screen OSD/OS is on: on a
    // tube (a CRT's display preset, or off the image a TV's lines) MAME is
    // told it is 4:3 and draws a horizontal game's lines whole; on HDMI
    // nothing; the settings over either.
    void screen() {
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);
        const QString module = QStringLiteral("com.osdos.arcade");
        const auto args = [this, &arcade](const char *set) {
            QFile::remove(m_log);
            played(arcade, QLatin1String(set));
            return logLines(QStringLiteral("play ")).value(0);
        };
        const auto restore = qScopeGuard([&arcade, module] {
            arcade.onSettingChanged(module, QStringLiteral("screen_shape"), QStringLiteral("Auto"));
            arcade.onSettingChanged(module, QStringLiteral("integer_scale"), QStringLiteral("Auto"));
        });

        // JAMMA 240p: Metal Slug's 224 lines once each, Pac-Man's 288
        // (a vertical game's width) spread over the screen's 240.
        arcade.setScreen(true, QSize(720, 240));
        QVERIFY(args("mslug").endsWith(QStringLiteral(" -aspect 4:3 -nounevenstretch -unevenstretchx")));
        QVERIFY(args("pacman").endsWith(QStringLiteral(" -aspect 4:3")));
        // A game MAME gives no size of: the shape only.
        QVERIFY(args("galaga").endsWith(QStringLiteral(" -aspect 4:3")));
        arcade.onSettingChanged(module, QStringLiteral("integer_scale"), QStringLiteral("Off"));
        QVERIFY(args("mslug").endsWith(QStringLiteral(" -aspect 4:3")));

        // HDMI: square pixels, the picture spread; the settings over it.
        arcade.onSettingChanged(module, QStringLiteral("integer_scale"), QStringLiteral("Auto"));
        arcade.setScreen(false, QSize(1920, 1080));
        QString line = args("pacman");
        QVERIFY2(!line.contains(QStringLiteral("-aspect")) && !line.contains(QStringLiteral("stretch")), qPrintable(line));
        arcade.onSettingChanged(module, QStringLiteral("integer_scale"), QStringLiteral("On"));
        arcade.onSettingChanged(module, QStringLiteral("screen_shape"), QStringLiteral("16:9"));
        QVERIFY(args("pacman").endsWith(QStringLiteral(" -aspect 16:9 -nounevenstretch -unevenstretchx")));

        // Composite's tube, 480 lines, and Square Pixels saying otherwise.
        arcade.onSettingChanged(module, QStringLiteral("integer_scale"), QStringLiteral("Auto"));
        arcade.onSettingChanged(module, QStringLiteral("screen_shape"), QStringLiteral("Auto"));
        arcade.setScreen(true, QSize(720, 480));
        QVERIFY(args("mslug").endsWith(QStringLiteral(" -aspect 4:3 -nounevenstretch -unevenstretchx")));
        arcade.onSettingChanged(module, QStringLiteral("screen_shape"), QStringLiteral("Square Pixels"));
        QVERIFY(args("mslug").endsWith(QStringLiteral(" -nounevenstretch -unevenstretchx")));
        QVERIFY(!args("mslug").contains(QStringLiteral("-aspect")));
        arcade.setScreen(false, QSize(1920, 1080));
        arcade.onSettingChanged(module, QStringLiteral("screen_shape"), QStringLiteral("Auto"));
        line = args("mslug");
        QVERIFY2(!line.contains(QStringLiteral("-aspect")) && !line.contains(QStringLiteral("stretch")), qPrintable(line));
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
        refreshed(arcade);
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

    // What RetroArch with FinalBurn Neo is asked and what is made of what it
    // says: its list of games, the controllers kept for the players, its
    // config and arguments, its log, the token it saves, the netplay
    // lobby's rooms and RetroAchievements' hash of a game.
    void libretroHelpers() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.path() + QStringLiteral("/fbneo.dat"), fixture("fbneo-arcade.dat"));
        QHash<QString, double> aspects;
        const QHash<QString, QString> games = libretro::readDat(dir.path() + QStringLiteral("/fbneo.dat"), &aspects);
        QCOMPARE(games.size(), 5);
        // The pictures' shapes, where the list says.
        QCOMPARE(aspects.size(), 2);
        QCOMPARE(aspects.value(QStringLiteral("sf2")), 4.0 / 3.0);
        QCOMPARE(aspects.value(QStringLiteral("galaga")), 3.0 / 4.0);
        QCOMPARE(games.value(QStringLiteral("sf2")), QStringLiteral("Street Fighter II - The World Warrior (World 910522)"));
        QVERIFY(games.contains(QStringLiteral("sf2ce")));
        QVERIFY(!games.contains(QStringLiteral("neogeo")));
        QVERIFY(libretro::readDat(dir.path() + QStringLiteral("/none.dat")).isEmpty());

        // A sign-in RetroAchievements turned down, and one that didn't reach it.
        for (const char *refused : { "Invalid user/token combination.", "Invalid token",
                                     "Invalid User/Password combination. Please try again",
                                     "The access token has expired. Please log in again." })
            QVERIFY2(libretro::loginRefused(QLatin1String(refused)), refused);
        for (const char *unreached : { "No response from server", "Unexpected API failure with no error message",
                                       "502 Bad Gateway", "" })
            QVERIFY2(!libretro::loginRefused(QLatin1String(unreached)), unreached);

        // A USB pad's vendor and product (SDL's GUID, little-endian), as
        // RetroArch's menu writes them; a device without them by its name.
        QCOMPARE(libretro::reservedDevice(QStringLiteral("030000005e0400008e02000014010000"),
                                          QStringLiteral("Xbox 360 Controller")),
                 QStringLiteral("045e:028e Xbox 360 Controller"));
        QCOMPARE(libretro::reservedDevice(QStringLiteral("05000000546f6f6c6b697420506164"), QStringLiteral("Toolkit Pad")),
                 QStringLiteral("Toolkit Pad"));
        QCOMPARE(libretro::reservedDevice(QStringLiteral("19000000000000000000000000000000"), QStringLiteral("JAMMA P1")),
                 QStringLiteral("JAMMA P1"));
        // The JAMMA board's sticks by the vendor and the product osdos-jamma
        // gives them, the player's number.
        QCOMPARE(libretro::reservedDevice(QStringLiteral("06006869010000000200000001000000"),
                                          QStringLiteral("OSD/OS JAMMA P2")),
                 QStringLiteral("0001:0002 OSD/OS JAMMA P2"));

        libretro::Launch launch;
        launch.core = QStringLiteral("/cores/fbneo_libretro.so");
        launch.content = QStringLiteral("/games/sf2.zip");
        launch.config = QStringLiteral("/ra/retroarch.cfg");
        launch.log = QStringLiteral("/ra/last.log");
        launch.systemDir = QStringLiteral("/ra/system");
        launch.saveDir = QStringLiteral("/ra/saves");
        launch.stateDir = QStringLiteral("/ra/states");
        launch.configDir = QStringLiteral("/ra/config");
        launch.pads = { QStringLiteral("045e:028e Xbox 360 Controller"), QString(), QStringLiteral("JAMMA \"P3\"") };
        launch.achievementsUser = QStringLiteral("player");
        launch.achievementsPassword = QStringLiteral("secret");
        QString config = libretro::configText(launch);
        const auto has = [&config](const QString &line) { return config.split(QLatin1Char('\n')).contains(line); };
        QVERIFY2(has(QStringLiteral("config_save_on_exit = \"true\"")), qPrintable(config));
        for (const char *line : { "video_driver = \"gl\"", "video_context_driver = \"kms\"", "input_driver = \"udev\"",
                                  "audio_driver = \"alsa\"", "audio_device = \"\"", "input_joypad_driver = \"sdl2\"",
                                  // RetroArch's Standard Gamepad for every player, its own
                                  // look-up off: no "not configured" over the game.
                                  "input_autodetect_enable = \"false\"", "input_player1_b_btn = \"0\"",
                                  "input_player1_a_btn = \"1\"", "input_player1_start_btn = \"6\"",
                                  "input_player1_select_btn = \"4\"", "input_player2_up_btn = \"11\"",
                                  "input_player6_l_x_minus_axis = \"-0\"", "input_player6_r_y_plus_axis = \"-3\"",
                                  "input_quit_gamepad_combo = \"4\"", "quit_press_twice = \"false\"",
                                  "menu_driver = \"rgui\"", "ui_companion_enable = \"false\"",
                                  "system_directory = \"/ra/system\"", "savefile_directory = \"/ra/saves\"",
                                  "input_player1_device_reservation_type = \"2\"",
                                  "input_player1_reserved_device = \"045e:028e Xbox 360 Controller\"",
                                  "input_player3_reserved_device = \"JAMMA P3\"",
                                  "cheevos_enable = \"true\"", "cheevos_username = \"player\"", "cheevos_token = \"\"",
                                  "cheevos_password = \"secret\"", "cheevos_hardcore_mode_enable = \"true\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        QVERIFY(!config.contains(QStringLiteral("input_player2_reserved_device")));
        QVERIFY(!config.contains(QStringLiteral("input_player7_")));
        QVERIFY(!config.contains(QStringLiteral("netplay_")));
        // A player on an arcade panel, a JAMMA board's stick: the cabinet's
        // two rows as FinalBurn Neo's 6-Button Panel has them (Y X L above,
        // B A R below), the joystick on the D-pad, and that controller type
        // in FinalBurn Neo's remap file, loaded without a word; the other
        // players as before.
        launch.panels = { false, false, true };
        launch.remapDir = QStringLiteral("/ra/remaps");
        config = libretro::configText(launch);
        for (const char *line : { "input_player3_y_btn = \"0\"", "input_player3_x_btn = \"1\"", "input_player3_l_btn = \"2\"",
                                  "input_player3_b_btn = \"3\"", "input_player3_a_btn = \"9\"", "input_player3_r_btn = \"10\"",
                                  "input_player3_select_btn = \"4\"", "input_player3_start_btn = \"6\"",
                                  "input_player3_up_axis = \"-1\"", "input_player3_down_axis = \"+1\"",
                                  "input_player3_left_axis = \"-0\"", "input_player3_right_axis = \"+0\"",
                                  "input_player1_b_btn = \"0\"", "input_player1_up_btn = \"11\"",
                                  "input_remapping_directory = \"/ra/remaps\"", "auto_remaps_enable = \"true\"",
                                  "remap_save_on_exit = \"false\"", "notification_show_remap_load = \"false\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        QVERIFY(!config.contains(QStringLiteral("input_player3_up_btn")));
        QCOMPARE(libretro::coreRemapPath(QStringLiteral("/ra/remaps")),
                 QStringLiteral("/ra/remaps/FinalBurn Neo/FinalBurn Neo.rmp"));
        const QStringList remap = libretro::remapText(launch).split(QLatin1Char('\n'));
        QVERIFY(remap.contains(QStringLiteral("input_libretro_device_p3 = \"261\"")));
        QVERIFY(!remap.filter(QStringLiteral("input_libretro_device_p1")).size());
        // Online, every player on the host's controller types: no panel.
        for (const auto netplay : { libretro::Launch::Netplay::Join, libretro::Launch::Netplay::Host }) {
            launch.netplay = netplay;
            const QStringList online = libretro::configText(launch).split(QLatin1Char('\n'));
            QVERIFY(online.contains(QStringLiteral("input_player3_b_btn = \"0\"")));
            QVERIFY(online.contains(QStringLiteral("input_player3_up_btn = \"11\"")));
            QVERIFY(online.filter(QStringLiteral("input_player3_up_axis")).isEmpty());
            QCOMPARE(libretro::remapText(launch), QString());
        }
        launch.netplay = libretro::Launch::Netplay::None;
        launch.panels.clear();
        QCOMPARE(libretro::remapText(launch), QString());
        // Square pixels and no integer scale: the core's shape.
        QVERIFY(has(QStringLiteral("video_scale_integer = \"false\"")));
        QVERIFY(!config.contains(QStringLiteral("aspect_ratio_index")));
        // A 4:3 tube at 720×240: a picture of its shape as wide as the
        // screen's pixels are narrow, its lines whole.
        QCOMPARE(libretro::viewportAspect(4.0 / 3.0, QSize(720, 240), 4.0 / 3.0), 3.0);
        QCOMPARE(libretro::viewportAspect(3.0 / 4.0, QSize(720, 240), 4.0 / 3.0), 1.6875);
        QCOMPARE(libretro::viewportAspect(4.0 / 3.0, QSize(720, 576), 4.0 / 3.0), 1.25);
        QCOMPARE(libretro::viewportAspect(4.0 / 3.0, QSize(720, 480), 16.0 / 9.0), 1.125);
        QCOMPARE(libretro::viewportAspect(4.0 / 3.0, QSize(), 4.0 / 3.0), 0.0);
        launch.aspect = 3.0;
        launch.integerScale = true;
        config = libretro::configText(launch);
        for (const char *line : { "aspect_ratio_index = \"20\"", "video_aspect_ratio = \"3.000000\"",
                                  "video_aspect_ratio_auto = \"false\"", "video_scale_integer = \"true\"",
                                  "video_scale_integer_axis = \"0\"", "video_scale_integer_scaling = \"0\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        launch.aspect = 0;
        launch.integerScale = false;
        // With a token: the password is no longer RetroArch's, nor saved.
        launch.achievementsToken = QStringLiteral("tok123");
        config = libretro::configText(launch);
        QVERIFY(has(QStringLiteral("cheevos_token = \"tok123\"")));
        QVERIFY(has(QStringLiteral("cheevos_password = \"\"")));
        QVERIFY(has(QStringLiteral("config_save_on_exit = \"false\"")));
        // Without an account, no achievements; a desktop's own drivers.
        launch.achievementsUser.clear();
        launch.console = false;
        launch.netplay = libretro::Launch::Netplay::Host;
        launch.nickname = QStringLiteral("Mehmet");
        launch.relay = QStringLiteral("madrid");
        config = libretro::configText(launch);
        QVERIFY(has(QStringLiteral("cheevos_enable = \"false\"")));
        QVERIFY(!config.contains(QStringLiteral("cheevos_username")));
        QVERIFY(!config.contains(QStringLiteral("video_driver")));
        QVERIFY(!config.contains(QStringLiteral("input_driver ")));
        for (const char *line : { "netplay_nickname = \"Mehmet\"", "netplay_public_announce = \"true\"",
                                  "netplay_use_mitm_server = \"true\"", "netplay_mitm_server = \"madrid\"" })
            QVERIFY2(has(QLatin1String(line)), line);

        QCOMPARE(libretro::arguments(launch),
                 QStringList({ QStringLiteral("-L"), QStringLiteral("/cores/fbneo_libretro.so"), QStringLiteral("--config"),
                               QStringLiteral("/ra/retroarch.cfg"), QStringLiteral("--verbose"),
                               QStringLiteral("--log-file=/ra/last.log"), QStringLiteral("--host"),
                               QStringLiteral("/games/sf2.zip") }));
        launch.netplay = libretro::Launch::Netplay::Join;
        launch.joinAddress = QStringLiteral("198.51.100.7|55435|c2Vzc2lvbg==");
        QCOMPARE(libretro::arguments(launch).mid(6),
                 QStringList({ QStringLiteral("--connect"), QStringLiteral("198.51.100.7|55435|c2Vzc2lvbg=="),
                               QStringLiteral("/games/sf2.zip") }));

        writeFile(dir.path() + QStringLiteral("/missing.log"),
                  "[INFO] RetroArch 1.20.0\n"
                  "[libretro ERROR] [FBNeo] ROM at index 1 with name 201-p1.p1 and CRC 0x12345678 is required\n"
                  "[libretro ERROR] [FBNeo] ROM at index 1 with name 201-p1.p1 and CRC 0x12345678 is required\n"
                  "[libretro ERROR] [FBNeo] ROM at index 4 with name 201-s1.s1 and CRC 0x2345678a is required\n"
                  "[INFO] [RCHEEVOS]: RetroAchievements login failed: Invalid User/Password combination. Please try again\n"
                  "[ERROR] [Content]: Something went wrong.\n");
        libretro::Run run = libretro::readLog(dir.path() + QStringLiteral("/missing.log"));
        QVERIFY(run.logged);
        QVERIFY(!run.started);
        QCOMPARE(run.missing, QStringList({ QStringLiteral("201-p1.p1"), QStringLiteral("201-s1.s1") }));
        QCOMPARE(run.loginFailure, QStringLiteral("Invalid User/Password combination. Please try again"));
        QCOMPARE(run.error, QStringLiteral("[Content]: Something went wrong."));
        writeFile(dir.path() + QStringLiteral("/started.log"), "[libretro INFO] [FBNeo] Driver successfully started\n");
        run = libretro::readLog(dir.path() + QStringLiteral("/started.log"));
        QVERIFY(run.started);
        QVERIFY(run.missing.isEmpty());
        QVERIFY(!libretro::readLog(dir.path() + QStringLiteral("/none.log")).logged);

        writeFile(dir.path() + QStringLiteral("/saved.cfg"),
                  "cheevos_enable = \"true\"\ncheevos_password = \"\"\ncheevos_token = \"tok456\"\n");
        QCOMPARE(libretro::savedToken(dir.path() + QStringLiteral("/saved.cfg")), QStringLiteral("tok456"));
        QCOMPARE(libretro::savedToken(dir.path() + QStringLiteral("/none.cfg")), QString());

        // The lobby: FinalBurn Neo's rooms that can be joined, newest
        // first, a relayed one through its relay; not another core's, one
        // that can't be called (behind a home router, unrelayed) or an old
        // RetroArch's. What hosts write is taken as it can be used: a name
        // without its markup, a set's name without its extension, and no
        // room whose game isn't a set's name or whose address isn't one.
        const QByteArray lobby = fixture("lobby.json");
        const QList<libretro::Room> rooms = libretro::readLobby(lobby);
        QCOMPARE(rooms.size(), 3);
        QCOMPARE(rooms.at(0).id, 18);
        QCOMPARE(rooms.at(0).game, QStringLiteral("kof98"));
        // Without its markup, 32 characters at most.
        QCOMPARE(rooms.at(0).host, QStringLiteral("img src=http://192.0.2.99/a.pngz"));
        QCOMPARE(rooms.at(0).country, QString());
        QCOMPARE(rooms.at(1).id, 14);
        QCOMPARE(rooms.at(1).game, QStringLiteral("mslug"));
        QCOMPARE(rooms.at(1).host, QStringLiteral("bob"));
        QCOMPARE(rooms.at(1).joinAddress, QStringLiteral("198.51.100.7|55435|c2Vzc2lvbg=="));
        QVERIFY(rooms.at(1).password);
        QCOMPARE(rooms.at(2).game, QStringLiteral("sf2"));
        QCOMPARE(rooms.at(2).country, QStringLiteral("es"));
        QCOMPARE(rooms.at(2).joinAddress, QStringLiteral("203.0.113.5|55435"));
        QVERIFY(libretro::readLobby("<html>busy</html>").isEmpty());

        // rcheevos' arcade hash: the file's name without its extension, as
        // it is written.
        QCOMPARE(libretro::achievementsHash(QStringLiteral("sf2.zip")), QStringLiteral("a7946d9b269b67f7a6b1148d3f5d8155"));
        QCOMPARE(libretro::achievementsHash(QStringLiteral("sf2.7z")), libretro::achievementsHash(QStringLiteral("sf2.zip")));
        QVERIFY(libretro::achievementsHash(QStringLiteral("SF2.zip")) != libretro::achievementsHash(QStringLiteral("sf2.zip")));
    }

    // The second engine: a set FinalBurn Neo has plays in it while Online
    // High Scores or Online Play is on, or as its options choose, with the
    // players' controllers kept for them and the BIOS sets linked where it
    // looks; RetroAchievements' password goes to RetroArch once, for the
    // token it saves, and never stays; and what its log says of a game that
    // didn't start.
    void fbneoEngine() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        // kof98 a folder of files, which FinalBurn Neo doesn't open.
        for (const char *set : { "sf2.zip", "mslug.zip", "galaga.zip", "pacman.zip", "bios/neogeo.zip",
                                 "kof98/242-p1.p1" })
            writeFile(games + QLatin1Char('/') + QLatin1String(set));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        for (const char *program : { "/bin/mame", "/bin/retroarch" })
            QVERIFY(QFile::setPermissions(data.path() + QLatin1String(program),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString configCopy = data.path() + QStringLiteral("/ra.cfg");
        qputenv("RA_CONFIG_COPY", configCopy.toUtf8());
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qunsetenv("RA_CONFIG_COPY");
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
            controllers::setPlayerEnvironment({});
        });
        controllers::setPlayerEnvironment({ { QStringLiteral("OSDOS_PLAYER1_NAME"), QStringLiteral("Xbox 360 Controller") },
                                            { QStringLiteral("OSDOS_PLAYER1_GUID"),
                                              QStringLiteral("030000005e0400008e02000014010000") } });
        writeFile(data.path() + QStringLiteral("/gamecontrollerdb.txt"), "# none\n");

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        refreshed(arcade);
        QVERIFY(!arcade.fbneoReady());
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));
        // The core and its list, where Settings → Arcade → FinalBurn Neo
        // downloads them.
        writeFile(libretro::downloadedCore(data.path()));
        writeFile(libretro::downloadedDat(data.path()), fixture("fbneo-arcade.dat"));
        refreshed(arcade);
        QVERIFY(arcade.fbneoReady());
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));

        const QString module = QStringLiteral("com.osdos.arcade");
        arcade.onSettingChanged(module, QStringLiteral("online_high_scores"), QStringLiteral("RetroAchievements"));
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("fbneo"));
        QCOMPARE(arcade.engineOf(QStringLiteral("pacman")), QStringLiteral("mame"));
        // A game of its list kept as a folder: MAME's, and not offered.
        QCOMPARE(arcade.engineOf(QStringLiteral("kof98")), QStringLiteral("mame"));
        QCOMPARE(arcade.engineChoices(QStringLiteral("kof98")).size(), 2);
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("engine")).toString(), QStringLiteral("fbneo"));
        // Its leaderboard for the Arcade screen once a key is kept, and only
        // for a game FinalBurn Neo plays; the key forgotten before anything
        // is asked, so no test reaches RetroAchievements.
        const auto boardState = [&arcade](const char *set) {
            return arcade.leaderboard(QLatin1String(set)).value(QStringLiteral("state")).toString();
        };
        QCOMPARE(boardState("sf2"), QStringLiteral("off"));
        // The key, asked about as it is set, asked of no server.
        arcade.setAchievementsServers(QUrl(QStringLiteral("http://127.0.0.1:9/API/")),
                                      QUrl(QStringLiteral("http://127.0.0.1:9/media/")));
        arcade.setAchievementsKey(QStringLiteral(" key1 "));
        QVERIFY(arcade.achievementsKey());
        QCOMPARE(QJsonDocument::fromJson(contents(data.path() + QStringLiteral("/arcade/retroachievements.json")))
                     .object().value(QStringLiteral("key")).toString(), QStringLiteral("key1"));
        QCOMPARE(boardState("sf2"), QStringLiteral("loading"));
        QCOMPARE(boardState("pacman"), QStringLiteral("off"));
        QCOMPARE(boardState("kof98"), QStringLiteral("off"));
        arcade.setAchievementsKey(QString());
        QVERIFY(!arcade.achievementsKey());
        QCOMPARE(boardState("sf2"), QStringLiteral("off"));
        const auto labels = [](const QVariantList &choices) {
            QStringList list;
            for (const QVariant &c : choices)
                list << c.toMap().value(QStringLiteral("label")).toString()
                            + (c.toMap().value(QStringLiteral("current")).toBool() ? QStringLiteral(" *") : QString());
            return list;
        };
        QCOMPARE(labels(arcade.engineChoices(QStringLiteral("sf2"))),
                 QStringList({ QStringLiteral("Auto (FinalBurn Neo) *"), QStringLiteral("MAME"), QStringLiteral("FinalBurn Neo") }));
        QCOMPARE(labels(arcade.engineChoices(QStringLiteral("pacman"))),
                 QStringList({ QStringLiteral("Auto (MAME) *"), QStringLiteral("MAME") }));
        arcade.onSettingChanged(module, QStringLiteral("engines"), QVariantMap{ { QStringLiteral("sf2"), QStringLiteral("mame") } });
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));
        QCOMPARE(labels(arcade.engineChoices(QStringLiteral("sf2"))),
                 QStringList({ QStringLiteral("Auto (FinalBurn Neo)"), QStringLiteral("MAME *"), QStringLiteral("FinalBurn Neo") }));
        arcade.onSettingChanged(module, QStringLiteral("online_high_scores"), QStringLiteral("Off"));
        arcade.onSettingChanged(module, QStringLiteral("engines"), QVariantMap{ { QStringLiteral("galaga"), QStringLiteral("fbneo") } });
        QCOMPARE(arcade.engineOf(QStringLiteral("galaga")), QStringLiteral("fbneo"));
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));
        arcade.onSettingChanged(module, QStringLiteral("online_high_scores"), QStringLiteral("RetroAchievements"));

        // Signed in: the password for RetroArch at the next game, kept only
        // in memory and that game's config.
        QSignalSpy account(&arcade, &ArcadeBackend::achievementsChanged);
        arcade.signInAchievements(QStringLiteral(" player "), QStringLiteral("secret"));
        QCOMPARE(arcade.achievementsUser(), QStringLiteral("player"));
        QCOMPARE(arcade.achievementsState(), QStringLiteral("pending"));
        const QString kept = data.path() + QStringLiteral("/arcade/retroachievements.json");
        QVERIFY(!QFile(kept).open(QIODevice::ReadOnly) || !contents(kept).contains("secret"));
        QCOMPARE(QFile::permissions(kept) & (QFile::ReadGroup | QFile::ReadOther), QFileDevice::Permissions());

        QFile::remove(m_log);
        // JAMMA 240p: Street Fighter II's 4:3 as wide as the screen's
        // pixels are narrow, its lines whole.
        arcade.setScreen(true, QSize(720, 240));
        QVariantList end = played(arcade, QStringLiteral("sf2"));
        QCOMPARE(end.value(1).toString(), QStringLiteral("ok"));
        const QString home = data.path() + QStringLiteral("/arcade/retroarch");
        const QString line = logLines(QStringLiteral("retroarch ")).value(0);
        QCOMPARE(line, QStringLiteral("retroarch -L %1 --config %2/retroarch.cfg --verbose --log-file=%2/last.log %3/sf2.zip")
                           .arg(libretro::downloadedCore(data.path()), home, games));
        QVERIFY(logLines().contains(QStringLiteral("cwd ") + home));
        QVERIFY(logLines().contains(QStringLiteral("sdlconfig ") + data.path() + QStringLiteral("/gamecontrollerdb.txt")));
        QString config = QString::fromUtf8(contents(configCopy));
        QVERIFY2(config.contains(QStringLiteral("cheevos_password = \"secret\"")), qPrintable(config));
        QVERIFY(config.contains(QStringLiteral("cheevos_username = \"player\"")));
        QVERIFY(config.contains(QStringLiteral("config_save_on_exit = \"true\"")));
        QVERIFY(config.contains(QStringLiteral("input_player1_reserved_device = \"045e:028e Xbox 360 Controller\"")));
        QVERIFY(config.contains(QStringLiteral("system_directory = \"%1/system\"").arg(home)));
        QVERIFY(config.contains(QStringLiteral("aspect_ratio_index = \"20\"")));
        QVERIFY(config.contains(QStringLiteral("video_aspect_ratio = \"3.000000\"")));
        QVERIFY(config.contains(QStringLiteral("video_scale_integer = \"true\"")));
        // The token RetroArch saved taken, the password gone with the
        // config it saved.
        QCOMPARE(arcade.achievementsState(), QStringLiteral("signedIn"));
        QVERIFY(!QFile::exists(home + QStringLiteral("/retroarch.cfg")));
        QCOMPARE(QJsonDocument::fromJson(contents(kept)).object().value(QStringLiteral("token")).toString(), QStringLiteral("tok123"));
        QVERIFY(!contents(kept).contains("secret"));
        QCOMPARE(QFileInfo(home).permissions() & (QFile::ReadGroup | QFile::ReadOther | QFile::ExeOther),
                 QFileDevice::Permissions());
        // The BIOS sets where FinalBurn Neo looks.
        QCOMPARE(QFileInfo(home + QStringLiteral("/system/fbneo/neogeo.zip")).symLinkTarget(),
                 QFileInfo(games + QStringLiteral("/bios/neogeo.zip")).canonicalFilePath());

        // The token from then on, and no password; one RetroAchievements no
        // longer takes signs out.
        QFile::remove(m_log);
        end = played(arcade, QStringLiteral("sf2"));
        config = QString::fromUtf8(contents(configCopy));
        QVERIFY(config.contains(QStringLiteral("cheevos_token = \"tok123\"")));
        QVERIFY(config.contains(QStringLiteral("cheevos_password = \"\"")));
        QVERIFY(config.contains(QStringLiteral("config_save_on_exit = \"false\"")));
        QCOMPARE(arcade.achievementsState(), QStringLiteral("signedIn"));
        // The config, the token in it, goes with the game.
        QVERIFY(!QFile::exists(home + QStringLiteral("/retroarch.cfg")));
        // No arcade panel: no remap file. The JAMMA board's sticks: each
        // player's by its vendor and product, on FinalBurn Neo's 6-Button
        // Panel through its remap file; a gamepad again, none.
        const QString remap = home + QStringLiteral("/remaps/FinalBurn Neo/FinalBurn Neo.rmp");
        QVERIFY(!QFileInfo::exists(remap));
        QVERIFY(config.contains(QStringLiteral("input_remapping_directory = \"%1/remaps\"").arg(home)));
        controllers::setPlayerEnvironment({ { QStringLiteral("OSDOS_PLAYER1_NAME"), QStringLiteral("OSD/OS JAMMA P1") },
                                            { QStringLiteral("OSDOS_PLAYER1_GUID"),
                                              QStringLiteral("06002868010000000100000001000000") },
                                            { QStringLiteral("OSDOS_PLAYER2_NAME"), QStringLiteral("OSD/OS JAMMA P2") },
                                            { QStringLiteral("OSDOS_PLAYER2_GUID"),
                                              QStringLiteral("06006869010000000200000001000000") } });
        played(arcade, QStringLiteral("sf2"));
        config = QString::fromUtf8(contents(configCopy));
        for (const char *line : { "input_player1_reserved_device = \"0001:0001 OSD/OS JAMMA P1\"",
                                  "input_player2_reserved_device = \"0001:0002 OSD/OS JAMMA P2\"",
                                  "input_player1_y_btn = \"0\"", "input_player2_b_btn = \"3\"",
                                  "input_player3_b_btn = \"0\"" })
            QVERIFY2(config.contains(QLatin1String(line)), line);
        QCOMPARE(QString::fromUtf8(contents(remap)).split(QLatin1Char('\n')).filter(QStringLiteral("input_libretro_device_p")),
                 QStringList({ QStringLiteral("input_libretro_device_p1 = \"261\""),
                               QStringLiteral("input_libretro_device_p2 = \"261\"") }));
        controllers::setPlayerEnvironment({ { QStringLiteral("OSDOS_PLAYER1_NAME"), QStringLiteral("Xbox 360 Controller") },
                                            { QStringLiteral("OSDOS_PLAYER1_GUID"),
                                              QStringLiteral("030000005e0400008e02000014010000") } });
        played(arcade, QStringLiteral("sf2"));
        QVERIFY(!QFileInfo::exists(remap));
        // RetroAchievements not reached: still signed in, the token kept.
        writeFile(kept, "{\"user\": \"player\", \"token\": \"offline\"}");
        {
            ArcadeBackend offline(data.path(), data.path(), nullptr, nullptr);
            refreshed(offline);
            offline.onSettingChanged(module, QStringLiteral("online_high_scores"), QStringLiteral("RetroAchievements"));
            played(offline, QStringLiteral("sf2"));
            QCOMPARE(offline.achievementsState(), QStringLiteral("signedIn"));
            QCOMPARE(QJsonDocument::fromJson(contents(kept)).object().value(QStringLiteral("token")).toString(),
                     QStringLiteral("offline"));
        }
        writeFile(kept, "{\"user\": \"player\", \"token\": \"expired\"}");
        // A config left by a game the app didn't see end goes as it starts.
        writeFile(home + QStringLiteral("/retroarch.cfg"), "cheevos_password = \"secret\"\n");
        ArcadeBackend again(data.path(), data.path(), nullptr, nullptr);
        QVERIFY(!QFile::exists(home + QStringLiteral("/retroarch.cfg")));
        refreshed(again);
        again.onSettingChanged(module, QStringLiteral("online_high_scores"), QStringLiteral("RetroAchievements"));
        QCOMPARE(again.achievementsState(), QStringLiteral("signedIn"));
        played(again, QStringLiteral("sf2"));
        QCOMPARE(again.achievementsState(), QStringLiteral("failed"));
        QCOMPARE(again.achievementsMessage(), QStringLiteral("Invalid token"));
        // A password refused: said why, and not kept.
        again.signInAchievements(QStringLiteral("player"), QStringLiteral("wrong"));
        played(again, QStringLiteral("sf2"));
        QCOMPARE(again.achievementsState(), QStringLiteral("failed"));
        QCOMPARE(again.achievementsMessage(), QStringLiteral("Invalid User/Password combination. Please try again"));
        again.signOutAchievements();
        QCOMPARE(again.achievementsState(), QStringLiteral("none"));
        QCOMPARE(QJsonDocument::fromJson(contents(kept)).object().value(QStringLiteral("user")).toString(), QString());

        // Files missing: what FinalBurn Neo said, as a game that didn't
        // play, with no play time, however long RetroArch took; a game that
        // played has its time.
        qputenv("RA_SECONDS", "1.2");
        end = played(arcade, QStringLiteral("mslug"));
        QCOMPARE(end.value(1).toString(), QStringLiteral("failed"));
        QVERIFY2(end.value(2).toString().startsWith(QStringLiteral("Missing 201-p1.p1, 201-s1.s1: FinalBurn Neo's sets")),
                 qPrintable(end.value(2).toString()));
        QCOMPARE(arcade.gameDetails(QStringLiteral("mslug")).value(QStringLiteral("playTime")).toLongLong(), 0);
        const qint64 before = arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("playTime")).toLongLong();
        played(arcade, QStringLiteral("sf2"));
        qunsetenv("RA_SECONDS");
        QVERIFY(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("playTime")).toLongLong() >= before + 1);
        // A set it doesn't know starts nothing, and RetroArch says nothing.
        arcade.onSettingChanged(module, QStringLiteral("engines"), QVariantMap{ { QStringLiteral("galaga"), QStringLiteral("fbneo") } });
        end = played(arcade, QStringLiteral("galaga"));
        QCOMPARE(end.value(1).toString(), QStringLiteral("failed"));
        QVERIFY2(end.value(2).toString().startsWith(QStringLiteral("FinalBurn Neo could not start the game")),
                 qPrintable(end.value(2).toString()));
        // Galaga is vertical (3:4): its picture as narrow on the tube.
        QVERIFY(QString::fromUtf8(contents(configCopy)).contains(QStringLiteral("video_aspect_ratio = \"1.687500\"")));
        // On HDMI, the core's shape on square pixels.
        arcade.setScreen(false, QSize(1920, 1080));
        played(arcade, QStringLiteral("galaga"));
        config = QString::fromUtf8(contents(configCopy));
        QVERIFY(!config.contains(QStringLiteral("aspect_ratio_index")));
        QVERIFY(config.contains(QStringLiteral("video_scale_integer = \"false\"")));
        // MAME as before for the rest.
        QFile::remove(m_log);
        end = played(arcade, QStringLiteral("pacman"));
        QCOMPARE(end.value(1).toString(), QStringLiteral("ok"));
        QVERIFY(logLines(QStringLiteral("play pacman")).size() == 1);
    }

    // A sign-in checked as it is made: RetroArch, with no picture, sound or
    // controllers, signs in with the sign-in core and is stopped as it says
    // how it went. The token it saved is kept and nothing of the check is
    // left, its config having held the password; a password refused says
    // why and isn't kept; RetroAchievements not reached leaves the password
    // for the next game, saying so; signed out while it runs, it is stopped.
    // Without the core, the next game signs in, as before.
    void signInChecked() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/retroarch"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString core = data.path() + QStringLiteral("/osdos_signin_libretro.so");
        writeFile(core);
        const QString configCopy = data.path() + QStringLiteral("/ra.cfg");
        qputenv("RA_CONFIG_COPY", configCopy.toUtf8());
        const auto restore = qScopeGuard([] { qunsetenv("RA_CONFIG_COPY"); });
        const QString dir = data.path() + QStringLiteral("/arcade/retroarch/sign-in");
        const QString kept = data.path() + QStringLiteral("/arcade/retroachievements.json");

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setSignInCore(data.path() + QStringLiteral("/none.so"));
        arcade.signInAchievements(QStringLiteral("player"), QStringLiteral("secret"));
        QCOMPARE(arcade.achievementsState(), QStringLiteral("pending"));
        QCOMPARE(arcade.achievementsMessage(), QString());

        arcade.setSignInCore(core);
        QFile::remove(m_log);
        arcade.signInAchievements(QStringLiteral(" player "), QStringLiteral("secret"));
        QCOMPARE(arcade.achievementsState(), QStringLiteral("checking"));
        QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("signedIn"), 10000);
        QCOMPARE(logLines(QStringLiteral("retroarch ")).value(0),
                 QStringLiteral("retroarch -L %1 --config %2/retroarch.cfg --verbose %2/sign-in.osdos").arg(core, dir));
        QVERIFY(logLines().contains(QStringLiteral("cwd ") + dir));
        QVERIFY(logLines().contains(QStringLiteral("xdg ") + dir + QStringLiteral("/config")));
        const QString config = QString::fromUtf8(contents(configCopy));
        for (const char *line : { "config_save_on_exit = \"true\"", "load_dummy_on_core_shutdown = \"false\"",
                                  "video_driver = \"null\"", "audio_driver = \"null\"",
                                  "input_driver = \"null\"", "input_joypad_driver = \"null\"", "cheevos_enable = \"true\"",
                                  "cheevos_username = \"player\"", "cheevos_password = \"secret\"", "cheevos_token = \"\"",
                                  "cheevos_hardcore_mode_enable = \"false\"" })
            QVERIFY2(config.contains(QLatin1String(line)), line);
        QCOMPARE(QJsonDocument::fromJson(contents(kept)).object().value(QStringLiteral("token")).toString(),
                 QStringLiteral("tok789"));
        QVERIFY(!contents(kept).contains("secret"));
        QVERIFY(!QFileInfo::exists(dir));

        arcade.signInAchievements(QStringLiteral("player"), QStringLiteral("wrong"));
        QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("failed"), 10000);
        QCOMPARE(arcade.achievementsMessage(), QStringLiteral("Invalid User/Password combination. Please try again"));
        QCOMPARE(QJsonDocument::fromJson(contents(kept)).object().value(QStringLiteral("token")).toString(), QString());
        QVERIFY(!QFileInfo::exists(dir));

        arcade.signInAchievements(QStringLiteral("player"), QStringLiteral("offline"));
        QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("pending"), 10000);
        QCOMPARE(arcade.achievementsMessage(), QStringLiteral("No response from server: RetroArch signs in at the next game"));
        QVERIFY(!QFileInfo::exists(dir));

        arcade.signInAchievements(QStringLiteral("player"), QStringLiteral("silent"));
        QCOMPARE(arcade.achievementsState(), QStringLiteral("checking"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(configCopy) && contents(configCopy).contains("silent"), 10000);
        arcade.signOutAchievements();
        QCOMPARE(arcade.achievementsState(), QStringLiteral("none"));
        QVERIFY(!QFileInfo::exists(dir));
        // Nothing comes of the RetroArch stopped.
        QTest::qWait(300);
        QCOMPARE(arcade.achievementsState(), QStringLiteral("none"));
    }

    // Settings → Arcade → FinalBurn Neo's download from a stand-in server:
    // the core out of its zip, the list of games and the licence put in
    // place once all three are whole; a file the server hasn't, or a list
    // that isn't FinalBurn Neo's, leaves what was there.
    void fbneoDownload() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        // FinalBurn Neo's list has some 8,400 sets: one of a thousand.
        QByteArray dat = "<?xml version=\"1.0\"?>\n<datafile>\n";
        for (int i = 0; i < 1000; ++i)
            dat += "<game name=\"game" + QByteArray::number(i) + "\"><description>Game</description></game>\n";
        dat += "</datafile>\n";
        QHash<QByteArray, QByteArray> files{ { "/core.zip", fixture("fbneo-core.zip") }, { "/fbneo.dat", dat },
                                             { "/small.dat", fixture("fbneo-arcade.dat") },
                                             { "/license.txt", "You may freely use FB Neo.\n" } };
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&server, &files] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &files] {
                const QByteArray request = socket->readAll();
                const QByteArray path = request.split(' ').value(1);
                if (files.contains(path)) {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(files.value(path).size())
                                  + "\r\nConnection: close\r\n\r\n" + files.value(path));
                } else {
                    socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                }
                socket->disconnectFromHost();
            });
        });
        const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        const auto source = [&base](const char *core, const char *list) {
            return FbneoInstaller::Source{ QUrl(base + QLatin1String(core)), QUrl(base + QLatin1String(list)),
                                           QUrl(base + QStringLiteral("/license.txt")) };
        };
        const QString core = libretro::downloadedCore(data.path());
        const auto noParts = [&data] {
            QDirIterator parts(data.path(), { QStringLiteral("*.part") }, QDir::Files, QDirIterator::Subdirectories);
            return !parts.hasNext();
        };

        FbneoInstaller installer(data.path());
        QSignalSpy finished(&installer, &FbneoInstaller::finished);
        installer.start(source("/core.zip", "/fbneo.dat"));
        QVERIFY(installer.running());
        QVERIFY(finished.wait(10000));
#ifdef OSDOS_MINIZIP
        QCOMPARE(finished.first().at(0).toBool(), true);
        QCOMPARE(installer.progress(), 100);
        QVERIFY(contents(core).startsWith("\x7f" "ELF a stand-in for FinalBurn Neo's core"));
        QCOMPARE(contents(libretro::downloadedDat(data.path())), dat);
        QCOMPARE(contents(libretro::downloadedLicense(data.path())), QByteArray("You may freely use FB Neo.\n"));
        QVERIFY(noParts());
        QVERIFY(!QFile::exists(core + QStringLiteral(".zip")));
        QCOMPARE(libretro::corePath(data.path()), core);
        QCOMPARE(libretro::datPath(data.path()), libretro::downloadedDat(data.path()));

        // A list the server hasn't, then one that isn't FinalBurn Neo's:
        // why, and what was there stays.
        writeFile(core, "the core before");
        installer.start(source("/core.zip", "/gone.dat"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(0).toBool(), false);
        QCOMPARE(finished.last().at(1).toString(), QStringLiteral("127.0.0.1 answered 404"));
        // The core downloaded before it goes too.
        QVERIFY(noParts());
        installer.start(source("/core.zip", "/small.dat"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(1).toString(), QStringLiteral("The list of games downloaded isn't FinalBurn Neo's"));
        installer.start(source("/license.txt", "/fbneo.dat"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(1).toString(), QStringLiteral("The core's download isn't a zip"));
        QCOMPARE(contents(core), QByteArray("the core before"));
        QCOMPARE(contents(libretro::downloadedDat(data.path())), dat);
        QVERIFY(noParts());
#else
        QCOMPARE(finished.first().at(0).toBool(), false);
        QVERIFY(finished.first().at(1).toString().contains(QStringLiteral("can't unpack the core")));
        QVERIFY(!QFile::exists(core));
#endif
        // A machine libretro builds none for.
        installer.start(FbneoInstaller::Source{ QUrl(), QUrl(base), QUrl(base) });
        QCOMPARE(finished.last().at(1).toString().left(40), QStringLiteral("libretro builds no FinalBurn Neo for thi"));
    }

    // The Arcade screen's leaderboards from a stand-in for RetroAchievements'
    // Web API: the arcade games' list read once and kept, a game found by
    // its file's hash, its leaderboard of scores chosen and its first three
    // read; only the game last asked for is read; a game with no leaderboard,
    // a key refused and a server gone said so.
    void retroAchievementsBoards() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QByteArray sf2 = libretro::achievementsHash(QStringLiteral("sf2.zip")).toLatin1();
        const QByteArray mslug = libretro::achievementsHash(QStringLiteral("mslug.zip")).toLatin1();
        const QByteArray galaga = libretro::achievementsHash(QStringLiteral("galaga.zip")).toLatin1();
        // sf2's game has a board of times first, then one of scores;
        // galaga's none; mslug's (its hash in capitals) one of points, with
        // no one on it yet.
        const QHash<QByteArray, QByteArray> answers{
            { "/API/API_GetGameList.php?i=27&f=1&h=1",
              "[{\"ID\": 1, \"NumLeaderboards\": 2, \"Hashes\": [\"" + sf2 + "\"]},"
              " {\"ID\": 2, \"NumLeaderboards\": 0, \"Hashes\": [\"" + galaga + "\"]},"
              " {\"ID\": 3, \"NumLeaderboards\": 1, \"Hashes\": [\"" + mslug.toUpper() + "\"]}]" },
            { "/API/API_GetGameLeaderboards.php?i=1",
              "{\"Count\": 2, \"Results\": [{\"ID\": 11, \"RankAsc\": true, \"Title\": \"Fastest Clear\","
              " \"Description\": \"Fewest seconds\", \"Format\": \"TIME\"}, {\"ID\": 12, \"RankAsc\": false,"
              " \"Title\": \" Arcade Mode\", \"Description\": \"The highest score possible\", \"Format\": \"VALUE\"}]}" },
            { "/API/API_GetLeaderboardEntries.php?i=12&c=3",
              "{\"Count\": 3, \"Total\": 40, \"Results\": [{\"Rank\": 1, \"User\": \"Ryu\", \"Score\": 1234560,"
              " \"FormattedScore\": \"1,234,560\"}, {\"Rank\": 2, \"User\": \"Ken\", \"Score\": 990000,"
              " \"FormattedScore\": \"990,000\"}, {\"Rank\": 2, \"User\": \"Chun\", \"Score\": 990000}]}" },
            { "/API/API_GetGameLeaderboards.php?i=3",
              "{\"Results\": [{\"ID\": 31, \"RankAsc\": false, \"Title\": \"Mission 1\","
              " \"Description\": \"Most points\", \"Format\": \"SCORE\"}]}" },
            { "/API/API_GetLeaderboardEntries.php?i=31&c=3", "{\"Count\": 0, \"Total\": 0, \"Results\": []}" },
            { "/API/API_GetConsoleIDs.php?", "[{\"ID\": 27, \"Name\": \"Arcade\"}]" },
        };
        QStringList asked;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&server, &answers, &asked] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &answers, &asked] {
                const QUrl url(QString::fromLatin1(socket->readAll().split(' ').value(1)));
                QUrlQuery query(url);
                const QString key = query.queryItemValue(QStringLiteral("y"));
                query.removeQueryItem(QStringLiteral("y"));
                const QByteArray path = (url.path() + QLatin1Char('?') + query.toString()).toLatin1();
                asked << QString::fromLatin1(path) + QStringLiteral(" y=") + key;
                QByteArray status = "200 OK";
                QByteArray body = answers.value(path);
                if (key != QLatin1String("key1"))
                    status = "401 Unauthorized", body = "{\"message\": \"Unauthenticated.\"}";
                else if (!answers.contains(path))
                    status = "404 Not Found";
                socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        const QUrl api(QStringLiteral("http://127.0.0.1:%1/API/").arg(server.serverPort()));
        const QUrl media(QStringLiteral("http://127.0.0.1:%1/media/").arg(server.serverPort()));
        const auto state = [](const QVariantMap &board) { return board.value(QStringLiteral("state")).toString(); };

        RetroAchievements boards(data.path(), api, media);
        QSignalSpy ready(&boards, &RetroAchievements::boardReady);
        QCOMPARE(state(boards.board(QStringLiteral("sf2.zip"))), QStringLiteral("failed"));
        boards.setKey(QStringLiteral("key1"));
        QCOMPARE(state(boards.board(QStringLiteral("sf2.zip"))), QStringLiteral("loading"));
        QVERIFY(ready.wait(5000));
        QCOMPARE(ready.last().at(0).toString(), QStringLiteral("sf2.zip"));
        QVariantMap board = boards.board(QStringLiteral("sf2.zip"));
        QCOMPARE(state(board), QStringLiteral("ready"));
        QCOMPARE(board.value(QStringLiteral("title")).toString(), QStringLiteral("Arcade Mode"));
        QVariantList entries = board.value(QStringLiteral("entries")).toList();
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries.at(0).toMap(), (QVariantMap{ { QStringLiteral("rank"), QStringLiteral("1ST") },
                                                      { QStringLiteral("score"), QStringLiteral("1,234,560") },
                                                      { QStringLiteral("name"), QStringLiteral("Ryu") } }));
        QCOMPARE(entries.at(2).toMap().value(QStringLiteral("rank")).toString(), QStringLiteral("2ND"));
        QCOMPARE(entries.at(2).toMap().value(QStringLiteral("score")).toString(), QStringLiteral("990000"));
        QCOMPARE(asked, QStringList({ QStringLiteral("/API/API_GetGameList.php?i=27&f=1&h=1 y=key1"),
                                      QStringLiteral("/API/API_GetGameLeaderboards.php?i=1 y=key1"),
                                      QStringLiteral("/API/API_GetLeaderboardEntries.php?i=12&c=3 y=key1") }));
        QVERIFY(contents(data.path() + QStringLiteral("/retroachievements-games.json")).contains(sf2));

        // A game whose leaderboards are none, and one with no one on its
        // board yet: the list isn't read again.
        asked.clear();
        QCOMPARE(state(boards.board(QStringLiteral("galaga.zip"))), QStringLiteral("loading"));
        QVERIFY(ready.wait(5000));
        QCOMPARE(state(boards.board(QStringLiteral("galaga.zip"))), QStringLiteral("none"));
        boards.board(QStringLiteral("mslug.zip"));
        QVERIFY(ready.wait(5000));
        board = boards.board(QStringLiteral("mslug.zip"));
        QCOMPARE(state(board), QStringLiteral("ready"));
        QCOMPARE(board.value(QStringLiteral("title")).toString(), QStringLiteral("Mission 1"));
        QVERIFY(board.value(QStringLiteral("entries")).toList().isEmpty());
        QCOMPARE(asked, QStringList({ QStringLiteral("/API/API_GetGameLeaderboards.php?i=3 y=key1"),
                                      QStringLiteral("/API/API_GetLeaderboardEntries.php?i=31&c=3 y=key1") }));

        // The list kept read from its file; of two games asked for at once,
        // only the last is read.
        asked.clear();
        RetroAchievements again(data.path(), api, media);
        QSignalSpy readyAgain(&again, &RetroAchievements::boardReady);
        again.setKey(QStringLiteral("key1"));
        again.board(QStringLiteral("sf2.zip"));
        again.board(QStringLiteral("mslug.zip"));
        QVERIFY(readyAgain.wait(5000));
        QCOMPARE(readyAgain.size(), 1);
        QCOMPARE(readyAgain.last().at(0).toString(), QStringLiteral("mslug.zip"));
        QCOMPARE(asked.size(), 2);
        QVERIFY(asked.first().startsWith(QStringLiteral("/API/API_GetGameLeaderboards.php?i=3 ")));

        // A list kept as 0.0.9 kept it, of the games with leaderboards alone:
        // its boards read meanwhile, and the list read again with the rest.
        QTemporaryDir older;
        writeFile(older.path() + QStringLiteral("/retroachievements-games.json"),
                  "{\"read\": \"" + QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toLatin1()
                  + "\", \"games\": {\"" + sf2 + "\": 1}}");
        asked.clear();
        RetroAchievements kept(older.path(), api, media);
        QSignalSpy readyKept(&kept, &RetroAchievements::boardReady);
        kept.setKey(QStringLiteral("key1"));
        kept.board(QStringLiteral("sf2.zip"));
        QVERIFY(readyKept.wait(5000));
        QCOMPARE(state(kept.board(QStringLiteral("sf2.zip"))), QStringLiteral("ready"));
        QCOMPARE(asked.first(), QStringLiteral("/API/API_GetGameList.php?i=27&f=1&h=1 y=key1"));
        QVERIFY(contents(older.path() + QStringLiteral("/retroachievements-games.json")).contains("\"boards\""));

        // A key RetroAchievements refuses.
        QSignalSpy refused(&again, &RetroAchievements::keyRefused);
        again.setKey(QStringLiteral("bad"));
        QCOMPARE(state(again.board(QStringLiteral("sf2.zip"))), QStringLiteral("loading"));
        QVERIFY(readyAgain.wait(5000));
        QCOMPARE(refused.size(), 1);
        QCOMPARE(state(again.board(QStringLiteral("sf2.zip"))), QStringLiteral("failed"));

        // A key refused before any list was read, then the right one: the
        // list read at once, not a minute later.
        QTemporaryDir fresh;
        RetroAchievements corrected(fresh.path(), api, media);
        QSignalSpy readyCorrected(&corrected, &RetroAchievements::boardReady);
        corrected.setKey(QStringLiteral("bad"));
        corrected.board(QStringLiteral("sf2.zip"));
        QVERIFY(readyCorrected.wait(5000));
        QCOMPARE(state(corrected.board(QStringLiteral("sf2.zip"))), QStringLiteral("failed"));
        corrected.setKey(QStringLiteral("key1"));
        QCOMPARE(state(corrected.board(QStringLiteral("sf2.zip"))), QStringLiteral("loading"));
        QVERIFY(readyCorrected.wait(5000));
        QCOMPARE(state(corrected.board(QStringLiteral("sf2.zip"))), QStringLiteral("ready"));

        // A key asked about at once, with the smallest answer, and taken;
        // asked again, not, as an answer came; another refused. An answer to
        // anything else takes a key too.
        QTemporaryDir checked;
        RetroAchievements checker(checked.path(), api, media);
        QSignalSpy accepted(&checker, &RetroAchievements::keyAccepted);
        QSignalSpy refusedKey(&checker, &RetroAchievements::keyRefused);
        asked.clear();
        checker.setKey(QStringLiteral("key1"));
        checker.checkKey();
        QVERIFY(accepted.wait(5000));
        QCOMPARE(asked, QStringList({ QStringLiteral("/API/API_GetConsoleIDs.php? y=key1") }));
        checker.checkKey();
        QTest::qWait(200);
        QCOMPARE(asked.size(), 1);
        checker.setKey(QStringLiteral("bad"));
        checker.checkKey();
        QVERIFY(refusedKey.wait(5000));
        QCOMPARE(accepted.size(), 1);
        QSignalSpy acceptedAgain(&again, &RetroAchievements::keyAccepted);
        again.setKey(QStringLiteral("key1"));
        again.board(QStringLiteral("sf2.zip"));
        QVERIFY(acceptedAgain.wait(5000));

        // No server, and no list kept: the board can't be read. What is
        // logged names what wasn't read, never the key.
        server.close();
        QTemporaryDir empty;
        RetroAchievements gone(empty.path(), api, media);
        QSignalSpy readyGone(&gone, &RetroAchievements::boardReady);
        static QStringList logged;
        logged.clear();
        const QtMessageHandler previous = qInstallMessageHandler(
            [](QtMsgType, const QMessageLogContext &, const QString &message) { logged << message; });
        gone.setKey(QStringLiteral("key1"));
        gone.board(QStringLiteral("sf2.zip"));
        const bool readyInTime = readyGone.wait(5000);
        qInstallMessageHandler(previous);
        QVERIFY(readyInTime);
        QCOMPARE(state(gone.board(QStringLiteral("sf2.zip"))), QStringLiteral("failed"));
        QVERIFY(logged.join(QLatin1Char('\n')).contains(QStringLiteral("API_GetGameList.php could not be read")));
        for (const QString &message : std::as_const(logged))
            QVERIFY2(!message.contains(QStringLiteral("key1")), qPrintable(message));
    }

    // A game's achievements and pictures from the stand-in: the user's
    // earned ones (in hardcore, or not), in RetroAchievements' order, a badge
    // name that isn't one dropped; with no user, as they are; a game with
    // none; one RetroAchievements hasn't, none at once; kept a while and read
    // again once forgotten. Its title screen and box art, not its
    // placeholder nor a path off the media server; a clone of the same game
    // as its parent's going by the parent; one looked for left be; one
    // RetroAchievements hasn't, unknown. A file downloaded only when it is a
    // picture, from a path of the media server's, and no bigger than its
    // kind's; a badge, locked or not.
    void retroAchievementsGameData() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        StandIn standIn;
        QVERIFY(serve(standIn));
        const QByteArray progress = R"({"ID": 1, "Title": "Street Fighter II", "Achievements": {
            "12": {"ID": 12, "Title": "World Warrior", "Description": "Beat the game", "Points": 25,
                   "BadgeName": "250336", "DisplayOrder": 2, "DateEarned": "2026-03-14 10:00:00",
                   "DateEarnedHardcore": "2026-03-14 10:00:00"},
            "11": {"ID": 11, "Title": " Hadouken ", "Description": "Throw a fireball", "Points": 5,
                   "BadgeName": "250337", "DisplayOrder": 1},
            "13": {"ID": 13, "Title": "Bonus Stage", "Description": "Wreck the car", "Points": 10,
                   "BadgeName": "../250338", "DisplayOrder": 2, "DateEarned": "2025-01-02 08:30:00"}}})";
        standIn.answers = {
            { "/API/API_GetGameList.php?i=27&f=1&h=1",
              "[{\"ID\": 1, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("sf2") + "\"]},"
              " {\"ID\": 4, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("sf2ce") + "\"]},"
              " {\"ID\": 2, \"NumLeaderboards\": 1, \"Hashes\": [\"" + hashOf("puckman") + "\", \"" + hashOf("pacman") + "\"]}]" },
            { "/API/API_GetGameInfoAndUserProgress.php?g=1&u=Ryu", progress },
            { "/API/API_GetGameExtended.php?i=1", R"({"ID": 1, "Achievements": {"11": {"ID": 11, "Title": "Hadouken",
                                                    "Points": 5, "BadgeName": "250337", "DisplayOrder": 1}}})" },
            { "/API/API_GetGameInfoAndUserProgress.php?g=4&u=Ryu", R"({"ID": 4, "Achievements": []})" },
            { "/API/API_GetGame.php?i=1", R"({"Title": "Street Fighter II", "ImageIcon": "/Images/000100.png",
                                            "ImageTitle": "/Images/000101.png", "ImageIngame": "/Images/000002.png",
                                            "ImageBoxArt": "/Images/000102.jpg"})" },
            { "/API/API_GetGame.php?i=2", R"({"Title": "Pac-Man", "ImageTitle": "/Images/000201.png",
                                            "ImageIngame": "/Images/000201.png", "ImageBoxArt": "/Images/../../x.png"})" },
            { "/media/Images/000101.png", png("title") },
            { "/media/Images/000102.jpg", jpeg("box") },
            { "/media/Images/000201.png", "<html>not a picture</html>" },
            { "/media/Badge/250336.png", png("badge") },
            { "/media/Badge/250337_lock.png", png("locked") },
            { "/media/Badge/big.png", png(QByteArray(600 * 1024, 'x')) },
        };
        const auto state = [](const QVariantMap &read) { return read.value(QStringLiteral("state")).toString(); };

        RetroAchievements ra(data.path(), standIn.api(), standIn.media());
        QSignalSpy ready(&ra, &RetroAchievements::achievementsReady);
        QCOMPARE(state(ra.achievements(QStringLiteral("sf2.zip"))), QStringLiteral("failed"));
        ra.setKey(QStringLiteral("key1"));
        ra.setUser(QStringLiteral("Ryu"));
        QCOMPARE(state(ra.achievements(QStringLiteral("sf2.zip"))), QStringLiteral("loading"));
        QVERIFY(ready.wait(5000));
        QCOMPARE(ready.last().at(0).toString(), QStringLiteral("sf2.zip"));
        QVariantMap read = ra.achievements(QStringLiteral("sf2.zip"));
        QCOMPARE(state(read), QStringLiteral("ready"));
        QVERIFY(read.value(QStringLiteral("progress")).toBool());
        const QVariantList achievements = read.value(QStringLiteral("achievements")).toList();
        QCOMPARE(achievements.size(), 3);
        QCOMPARE(achievements.at(0).toMap(), (QVariantMap{
            { QStringLiteral("id"), 11 }, { QStringLiteral("title"), QStringLiteral("Hadouken") },
            { QStringLiteral("description"), QStringLiteral("Throw a fireball") }, { QStringLiteral("points"), 5 },
            { QStringLiteral("badge"), QStringLiteral("250337") }, { QStringLiteral("earned"), QString() },
            { QStringLiteral("hardcore"), false } }));
        QCOMPARE(achievements.at(1).toMap().value(QStringLiteral("earned")).toString(), QStringLiteral("2026-03-14"));
        QVERIFY(achievements.at(1).toMap().value(QStringLiteral("hardcore")).toBool());
        QCOMPARE(achievements.at(2).toMap().value(QStringLiteral("earned")).toString(), QStringLiteral("2025-01-02"));
        QVERIFY(!achievements.at(2).toMap().value(QStringLiteral("hardcore")).toBool());
        QCOMPARE(achievements.at(2).toMap().value(QStringLiteral("badge")).toString(), QString());
        QCOMPARE(standIn.asked, QStringList({ QStringLiteral("/API/API_GetGameList.php?i=27&f=1&h=1"),
                                              QStringLiteral("/API/API_GetGameInfoAndUserProgress.php?g=1&u=Ryu") }));

        // A game with none, and one RetroAchievements hasn't: none at once,
        // nothing asked.
        ra.achievements(QStringLiteral("sf2ce.zip"));
        QVERIFY(ready.wait(5000));
        QCOMPARE(state(ra.achievements(QStringLiteral("sf2ce.zip"))), QStringLiteral("none"));
        standIn.asked.clear();
        QCOMPARE(state(ra.achievements(QStringLiteral("galaga.zip"))), QStringLiteral("none"));
        QTest::qWait(600);
        QVERIFY(standIn.asked.isEmpty());

        // Kept a while; read again once forgotten (the game played); with no
        // user, as they are.
        QCOMPARE(state(ra.achievements(QStringLiteral("sf2.zip"))), QStringLiteral("ready"));
        QTest::qWait(600);
        QVERIFY(standIn.asked.isEmpty());
        ra.forgetAchievements(QStringLiteral("sf2.zip"));
        QCOMPARE(state(ra.achievements(QStringLiteral("sf2.zip"))), QStringLiteral("loading"));
        QVERIFY(ready.wait(5000));
        QCOMPARE(standIn.asked, QStringList({ QStringLiteral("/API/API_GetGameInfoAndUserProgress.php?g=1&u=Ryu") }));
        ra.setUser(QString());
        QCOMPARE(state(ra.achievements(QStringLiteral("sf2.zip"))), QStringLiteral("loading"));
        QVERIFY(ready.wait(5000));
        read = ra.achievements(QStringLiteral("sf2.zip"));
        QVERIFY(!read.value(QStringLiteral("progress")).toBool());
        QCOMPARE(read.value(QStringLiteral("achievements")).toList().size(), 1);
        QCOMPARE(standIn.asked.last(), QStringLiteral("/API/API_GetGameExtended.php?i=1"));

        // The pictures' paths.
        QSignalSpy found(&ra, &RetroAchievements::picturesFound);
        ra.findPictures({ QStringLiteral("sf2") }, {});
        QVERIFY(found.wait(5000));
        QCOMPARE(found.last(), (QVariantList{ QStringLiteral("sf2"), QStringLiteral("found"),
                                              QStringList({ QStringLiteral("/Images/000101.png"),
                                                            QStringLiteral("/Images/000102.jpg") }) }));
        ra.findPictures({ QStringLiteral("pacman"), QStringLiteral("puckman") }, {});
        QVERIFY(found.wait(5000));
        QCOMPARE(found.last(), (QVariantList{ QStringLiteral("puckman"), QStringLiteral("found"),
                                              QStringList({ QStringLiteral("/Images/000201.png") }) }));
        standIn.asked.clear();
        ra.findPictures({ QStringLiteral("pacman"), QStringLiteral("puckman") }, { QStringLiteral("puckman") });
        QVERIFY(!found.wait(800));
        QVERIFY(standIn.asked.isEmpty());
        ra.findPictures({ QStringLiteral("galaga") }, {});
        QVERIFY(found.wait(5000));
        QCOMPARE(found.last(), (QVariantList{ QStringLiteral("galaga"), QStringLiteral("unknown"), QStringList() }));

        // Files of the media server's.
        const auto download = [&ra](const QString &path) {
            QByteArray got = "none yet";
            bool done = false;
            ra.media(path, [&got, &done](const QByteArray &data) { got = data; done = true; });
            [&done] { QTRY_VERIFY_WITH_TIMEOUT(done, 5000); }();
            return got;
        };
        QCOMPARE(download(QStringLiteral("/Images/000101.png")), png("title"));
        QCOMPARE(download(QStringLiteral("/Images/000102.jpg")), jpeg("box"));
        QCOMPARE(download(QStringLiteral("/Images/000201.png")), QByteArray());
        QCOMPARE(download(QStringLiteral("/Images/404.png")), QByteArray());
        QCOMPARE(download(QStringLiteral("/Badge/big.png")), QByteArray());
        standIn.asked.clear();
        QCOMPARE(download(QStringLiteral("/Images/../../etc/passwd")), QByteArray());
        QCOMPARE(download(QStringLiteral("http://elsewhere/Images/1.png")), QByteArray());
        QVERIFY(standIn.asked.isEmpty());

        // Badges, in colour and grey.
        QCOMPARE(download(QStringLiteral("/Badge/250336.png")), png("badge"));
        QCOMPARE(download(QStringLiteral("/Badge/250337_lock.png")), png("locked"));
    }

    // RetroAchievements' pictures of a game as it is shown, with a key kept:
    // saved in the games folder's snap folder as <set>_<n> in the first
    // numbers free, never over the user's own (whatever its suffix's case)
    // nor as a set's name, a clone's under its parent's name when it is its
    // parent's game and shown after the clone's own snap, listed among its
    // pictures (the snap first) and kept through the folder read again; each
    // game's looked for once, this run and the next (its record, beside
    // them); one that failed not again at once; none with no key, nor for a
    // game RetroAchievements hasn't. Its achievements counted for the Arcade
    // screen's box, and their badges saved beside its pictures.
    void retroAchievementsPictures() {
        const QString snap = m_games + QStringLiteral("/snap");
        const QString record = snap + QStringLiteral("/.retroachievements.json");
        const auto restore = qScopeGuard([this, snap, record] {
            QDir(snap).removeRecursively();
            QFile::remove(record);
            QFile::remove(m_data.path() + QStringLiteral("/arcade/retroachievements.json"));
            QFile::remove(m_data.path() + QStringLiteral("/arcade/retroachievements-games.json"));
            QFile::remove(m_games + QStringLiteral("/sf2_3.zip"));
        });
        // The user's own: the snap pack's sf2.png and pacman.png, an sf2_1.png
        // and an sf2_4.PNG; and a set named sf2_3 (its own picture's name).
        writeFile(snap + QStringLiteral("/sf2.png"), png("pack"));
        writeFile(snap + QStringLiteral("/sf2_1.png"), png("mine"));
        writeFile(snap + QStringLiteral("/sf2_4.PNG"), png("shouting"));
        writeFile(snap + QStringLiteral("/pacman.png"), png("pac pack"));
        writeFile(m_games + QStringLiteral("/sf2_3.zip"));
        StandIn standIn;
        QVERIFY(serve(standIn));
        standIn.answers = {
            { "/API/API_GetGameList.php?i=27&f=1&h=1",
              "[{\"ID\": 1, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("sf2") + "\"]},"
              " {\"ID\": 2, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("puckman") + "\", \"" + hashOf("pacman") + "\"]},"
              " {\"ID\": 3, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("kinst") + "\"]}]" },
            { "/API/API_GetGame.php?i=1", R"({"ImageTitle": "/Images/000101.png", "ImageIngame": "/Images/000103.png",
                                            "ImageBoxArt": "/Images/000102.jpg"})" },
            { "/API/API_GetGame.php?i=2", R"({"ImageTitle": "/Images/000201.png", "ImageIngame": "/Images/000002.png",
                                            "ImageBoxArt": "/Images/000002.png"})" },
            { "/API/API_GetGame.php?i=3", R"({"ImageTitle": "/Images/000301.png", "ImageIngame": "/Images/000302.png"})" },
            { "/media/Images/000101.png", png("title") },
            { "/media/Images/000103.png", png("ingame") },
            { "/media/Images/000102.jpg", jpeg("box") },
            { "/media/Images/000201.png", png("pacman") },
            { "/media/Images/000301.png", png("kinst") },
            { "/API/API_GetGameExtended.php?i=1", R"({"ID": 1, "Achievements": {
                "11": {"ID": 11, "Title": "Hadouken", "Points": 5, "BadgeName": "250337", "DisplayOrder": 1},
                "12": {"ID": 12, "Title": "World Warrior", "Points": 25, "BadgeName": "250336", "DisplayOrder": 2}}})" },
            { "/media/Badge/250336.png", png("badge") },
            { "/media/Badge/250337_lock.png", png("locked") },
            { "/API/API_GetConsoleIDs.php?", R"([{"ID": 27, "Name": "Arcade"}])" },
        };
        const auto urls = [](const QStringList &files) {
            QStringList list;
            for (const QString &file : files)
                list << QUrl::fromLocalFile(file).toString();
            return QVariant(list);
        };

        AppCore core(m_data.path(), m_data.path());
        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        arcade.setAchievementsServers(standIn.api(), standIn.media());
        refreshed(arcade);
        // No key: nothing asked.
        arcade.wantPictures(QStringLiteral("sf2"));
        QTest::qWait(600);
        QVERIFY(standIn.asked.isEmpty());

        // A key: asked about at once, and taken; nothing else read until a
        // game's pictures are wanted.
        QSignalSpy changed(&arcade, &ArcadeBackend::picturesChanged);
        arcade.setAchievementsKey(QStringLiteral("key1"));
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("pictures")),
                 urls({ snap + QStringLiteral("/sf2.png"), snap + QStringLiteral("/sf2_1.png"),
                        snap + QStringLiteral("/sf2_4.PNG") }));
        QTRY_VERIFY_WITH_TIMEOUT(arcade.achievementsKeyWorks(), 5000);
        QTest::qWait(600);
        QCOMPARE(standIn.asked, QStringList({ QStringLiteral("/API/API_GetConsoleIDs.php?") }));
        standIn.asked.clear();
        arcade.wantPictures(QStringLiteral("sf2"));
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 3, 10000);
        QCOMPARE(changed.last().at(0).toString(), QStringLiteral("sf2"));
        // The user's untouched; RetroAchievements' in 2, 5 and 6, 3 being a
        // set's name and 4 the user's, each named as it is a PNG or a JPEG.
        QCOMPARE(contents(snap + QStringLiteral("/sf2.png")), png("pack"));
        QCOMPARE(contents(snap + QStringLiteral("/sf2_1.png")), png("mine"));
        QCOMPARE(contents(snap + QStringLiteral("/sf2_4.PNG")), png("shouting"));
        QStringList files = QDir(snap).entryList({ QStringLiteral("sf2_*") }, QDir::Files, QDir::Name);
        QVERIFY(files.removeOne(QStringLiteral("sf2_1.png")));
        QVERIFY(files.removeOne(QStringLiteral("sf2_4.PNG")));
        QStringList stems;
        QStringList saved;
        QList<QByteArray> bytes;
        for (const QString &file : std::as_const(files)) {
            const QByteArray data = contents(snap + QLatin1Char('/') + file);
            stems << QFileInfo(file).completeBaseName();
            saved << snap + QLatin1Char('/') + file;
            bytes << data;
            QCOMPARE(QFileInfo(file).suffix(), data.startsWith("\xff\xd8") ? QStringLiteral("jpg") : QStringLiteral("png"));
        }
        QCOMPARE(stems, QStringList({ QStringLiteral("sf2_2"), QStringLiteral("sf2_5"), QStringLiteral("sf2_6") }));
        // In the order they came: the title screen, the screenshot, the box.
        QCOMPARE(bytes, QList<QByteArray>({ png("title"), png("ingame"), jpeg("box") }));
        // Shown by their numbers, the user's among them.
        const QStringList shown{ snap + QStringLiteral("/sf2.png"), snap + QStringLiteral("/sf2_1.png"), saved.at(0),
                                 snap + QStringLiteral("/sf2_4.PNG"), saved.at(1), saved.at(2) };
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("pictures")), urls(shown));
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("picture")).toString(),
                 QUrl::fromLocalFile(snap + QStringLiteral("/sf2.png")).toString());
        // The record, written after the pictures off the GUI thread.
        const auto kept = [record](const char *name) {
            return QJsonDocument::fromJson(contents(record)).object().value(QLatin1String(name)).toObject();
        };
        QTRY_VERIFY_WITH_TIMEOUT(kept("sf2").value(QStringLiteral("complete")).toBool(), 5000);
        QCOMPARE(kept("sf2").value(QStringLiteral("saved")).toArray().size(), 3);

        // Looked for once: asked for again, nothing read.
        standIn.asked.clear();
        arcade.wantPictures(QStringLiteral("sf2"));
        QTest::qWait(700);
        QVERIFY(standIn.asked.isEmpty());

        // Pac-Man is its parent's game: saved as Puck Man's, shown after its
        // own snap. Galaga isn't RetroAchievements': nothing read, asked
        // again or not.
        arcade.wantPictures(QStringLiteral("pacman"));
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 4, 10000);
        QCOMPARE(changed.last().at(0).toString(), QStringLiteral("puckman"));
        QCOMPARE(arcade.gameDetails(QStringLiteral("pacman")).value(QStringLiteral("pictures")),
                 urls({ snap + QStringLiteral("/pacman.png"), snap + QStringLiteral("/puckman_1.png") }));
        standIn.asked.clear();
        arcade.wantPictures(QStringLiteral("galaga"));
        QTest::qWait(700);
        arcade.wantPictures(QStringLiteral("galaga"));
        QTest::qWait(700);
        QVERIFY(standIn.asked.isEmpty());

        // A picture that can't be downloaded: those that could be kept, the
        // game not complete.
        arcade.wantPictures(QStringLiteral("kinst"));
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 5, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(standIn.asked.contains(QStringLiteral("/media/Images/000302.png")), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(kept("kinst").value(QStringLiteral("saved")).toArray().size(), 1, 5000);
        QVERIFY(!kept("kinst").value(QStringLiteral("complete")).toBool());
        // Its job ended: not asked about again at once.
        standIn.asked.clear();
        arcade.wantPictures(QStringLiteral("kinst"));
        QTest::qWait(700);
        QVERIFY(standIn.asked.isEmpty());

        // The folder read again: the pictures still there, in turn.
        refreshed(arcade);
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("pictures")), urls(shown));

        // The next run: looked for already, as its record says.
        standIn.asked.clear();
        ArcadeBackend next(m_data.path(), m_data.path(), &core, nullptr);
        next.setAchievementsServers(standIn.api(), standIn.media());
        refreshed(next);
        next.wantPictures(QStringLiteral("sf2"));
        next.wantPictures(QStringLiteral("pacman"));
        QTest::qWait(700);
        QVERIFY(standIn.asked.isEmpty());

        // Its achievements counted, with no account to say which were
        // earned; none at once for a game the list hasn't.
        QSignalSpy counted(&arcade, &ArcadeBackend::gameAchievementsReady);
        QCOMPARE(arcade.achievementCount(QStringLiteral("sf2")).value(QStringLiteral("state")).toString(),
                 QStringLiteral("loading"));
        QVERIFY(counted.wait(5000));
        QCOMPARE(arcade.achievementCount(QStringLiteral("sf2")),
                 (QVariantMap{ { QStringLiteral("state"), QStringLiteral("ready") }, { QStringLiteral("earned"), 0 },
                               { QStringLiteral("total"), 2 }, { QStringLiteral("progress"), false } }));
        QCOMPARE(arcade.achievementCount(QStringLiteral("galaga")).value(QStringLiteral("state")).toString(),
                 QStringLiteral("none"));

        // An achievement's badge, as it is first shown: saved beside the
        // pictures as achieve_<set>_<id> (the grey one _lock), downloaded
        // once, and no picture of the game's; one that couldn't be
        // downloaded not asked for again at once; nothing for a name that
        // isn't a badge's, nor for a set not in the folder.
        QSignalSpy badges(&arcade, &ArcadeBackend::badgesReady);
        standIn.asked.clear();
        QCOMPARE(arcade.achievementBadge(QStringLiteral("sf2"), 12, QStringLiteral("250336"), true), QString());
        QCOMPARE(arcade.achievementBadge(QStringLiteral("sf2"), 12, QStringLiteral("250336"), true), QString());
        QVERIFY(badges.wait(5000));
        const QString earned = snap + QStringLiteral("/achieve_sf2_12.png");
        QCOMPARE(arcade.achievementBadge(QStringLiteral("sf2"), 12, QStringLiteral("250336"), true),
                 QUrl::fromLocalFile(earned).toString());
        QCOMPARE(contents(earned), png("badge"));
        QCOMPARE(standIn.asked.count(QStringLiteral("/media/Badge/250336.png")), 1);
        QCOMPARE(arcade.achievementBadge(QStringLiteral("sf2"), 11, QStringLiteral("250337"), false), QString());
        QVERIFY(badges.wait(5000));
        QCOMPARE(contents(snap + QStringLiteral("/achieve_sf2_11_lock.png")), png("locked"));
        refreshed(arcade);
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("pictures")), urls(shown));
        standIn.asked.clear();
        QCOMPARE(arcade.achievementBadge(QStringLiteral("sf2"), 13, QStringLiteral("999"), true), QString());
        QTRY_COMPARE_WITH_TIMEOUT(standIn.asked.size(), 1, 5000);
        QTest::qWait(300);
        QCOMPARE(arcade.achievementBadge(QStringLiteral("sf2"), 13, QStringLiteral("999"), true), QString());
        QCOMPARE(arcade.achievementBadge(QStringLiteral("sf2"), 14, QStringLiteral("../999"), true), QString());
        QCOMPARE(arcade.achievementBadge(QStringLiteral("nosuchset"), 15, QStringLiteral("250336"), true), QString());
        QTest::qWait(300);
        QCOMPARE(standIn.asked.size(), 1);
    }

    // The status icons the module adds, as its settings and account stand,
    // and AppCore's of every enabled module's, read again as they change and
    // as a module is turned on or off.
    void statusIcons() {
        const QString module = QStringLiteral("com.osdos.arcade");
        QTemporaryDir app;
        QVERIFY(app.isValid());
        writeFile(app.path() + QStringLiteral("/modules/arcade/manifest.json"),
                  R"({"id": "com.osdos.arcade", "name": "Arcade", "entry_point_qml": "views/Root.qml",
                      "settings": [{"key": "enabled", "type": "toggle", "default": "OFF"}]})");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(app.path(), data.path());
        ArcadeBackend arcade(m_data.path(), data.path(), &core, nullptr);
        core.registerModule(module, QStringLiteral("arcadeBackend"), &arcade, nullptr);
        QSignalSpy own(&arcade, &ArcadeBackend::statusIconsChanged);
        QSignalSpy all(&core, &AppCore::statusIconsChanged);

        // Save High Scores is on as it comes; the module isn't.
        QCOMPARE(arcade.statusIcons(), QStringList({ QStringLiteral("hiscore") }));
        QVERIFY(core.statusIcons().isEmpty());
        QVERIFY(core.save_setting(module, QStringLiteral("enabled"), true));
        QCOMPARE(all.size(), 1);
        QCOMPARE(core.statusIcons(), QStringList({ QStringLiteral("hiscore") }));

        QVERIFY(core.save_setting(module, QStringLiteral("online_play"), QStringLiteral("On")));
        QCOMPARE(core.statusIcons(), QStringList({ QStringLiteral("hiscore"), QStringLiteral("globe") }));
        // RetroAchievements: its trophy while its account is signed in, or
        // signs in at the next game.
        QVERIFY(core.save_setting(module, QStringLiteral("online_high_scores"), QStringLiteral("RetroAchievements")));
        QCOMPARE(core.statusIcons(), QStringList({ QStringLiteral("hiscore"), QStringLiteral("globe") }));
        arcade.signInAchievements(QStringLiteral("Ryu"), QStringLiteral("secret"));
        QCOMPARE(core.statusIcons(), QStringList({ QStringLiteral("hiscore"), QStringLiteral("trophy"),
                                                   QStringLiteral("globe") }));
        QVERIFY(core.save_setting(module, QStringLiteral("high_scores"), false));
        QCOMPARE(core.statusIcons(), QStringList({ QStringLiteral("trophy"), QStringLiteral("globe") }));
        arcade.signOutAchievements();
        QCOMPARE(core.statusIcons(), QStringList({ QStringLiteral("globe") }));
        // AppCore's changed only as they did: not for Online High Scores
        // alone, whose trophy waited for the account.
        QVERIFY(own.size() >= 5);
        QCOMPARE(all.size(), 5);

        QVERIFY(core.save_setting(module, QStringLiteral("enabled"), false));
        QVERIFY(core.statusIcons().isEmpty());
        QCOMPARE(all.size(), 6);
    }

    // Online Play against a stand-in for RetroArch's lobby: the rooms of the
    // folder's games FinalBurn Neo plays (not a room with a password, nor
    // one of a game not there), in the tree's Online Games and the Arcade
    // screen's filter; a room joined and a game hosted through the relay
    // chosen, with the online name; a lobby that can't be reached said so;
    // nothing of it with Online Play off.
    void onlinePlay() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        for (const char *set : { "sf2.zip", "mslug.zip", "galaga.zip", "pacman.zip" })
            writeFile(games + QLatin1Char('/') + QLatin1String(set));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        for (const char *program : { "/bin/mame", "/bin/retroarch" })
            QVERIFY(QFile::setPermissions(data.path() + QLatin1String(program),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        writeFile(libretro::downloadedCore(data.path()));
        writeFile(libretro::downloadedDat(data.path()), fixture("fbneo-arcade.dat"));
        const QString configCopy = data.path() + QStringLiteral("/ra.cfg");
        qputenv("RA_CONFIG_COPY", configCopy.toUtf8());
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qunsetenv("RA_CONFIG_COPY");
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
            controllers::setPlayerEnvironment({});
        });
        const QByteArray lobby = fixture("lobby.json");
        // big: an answer far bigger than a list of rooms.
        bool big = false;
        int asked = 0;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&server, &lobby, &asked, &big] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &lobby, &asked, &big] {
                socket->readAll();
                ++asked;
                const QByteArray body = big ? QByteArray(5 * 1024 * 1024, ' ') : lobby;
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setLobbyUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/list").arg(server.serverPort())));
        refreshed(arcade);
        // Off: no lobby, no folder, nothing to host.
        QCOMPARE(asked, 0);
        QVERIFY(arcade.rooms().isEmpty());
        QVERIFY(!names(arcade.requestEntries(QStringLiteral("top"))).contains(QStringLiteral("Online Games")));
        QVERIFY(!arcade.canHost(QStringLiteral("sf2")));

        const QString module = QStringLiteral("com.osdos.arcade");
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("On"));
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("fbneo"));
        QVERIFY(arcade.canHost(QStringLiteral("sf2")));
        QVERIFY(!arcade.canHost(QStringLiteral("pacman")));
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("top"))).value(0), QStringLiteral("Online Games"));
        // Asked for before the lobby is read: read then, listed once it is.
        QSignalSpy roomsRead(&arcade, &ArcadeBackend::roomsChanged);
        QSignalSpy ready(&arcade, &ArcadeBackend::entriesReady);
        QVERIFY(!arcade.requestEntries(QStringLiteral("online")).isValid());
        QVERIFY(roomsRead.wait(5000));
        QCOMPARE(asked, 1);
        QVERIFY(ready.contains(QVariantList{ QStringLiteral("online") }));
        const QVariantList rooms = arcade.rooms();
        QCOMPARE(rooms.size(), 1);
        const QVariantMap room = rooms.first().toMap();
        QCOMPARE(room.value(QStringLiteral("id")).toInt(), 11);
        QCOMPARE(room.value(QStringLiteral("set")).toString(), QStringLiteral("sf2"));
        QCOMPARE(room.value(QStringLiteral("host")).toString(), QStringLiteral("ann"));
        const QVariantList entries = arcade.requestEntries(QStringLiteral("online")).toList();
        QCOMPARE(entries.size(), 1);
        const QVariantMap entry = entries.first().toMap();
        QCOMPARE(entry.value(QStringLiteral("kind")).toString(), QStringLiteral("room"));
        QCOMPARE(entry.value(QStringLiteral("room")).toInt(), 11);
        QCOMPARE(entry.value(QStringLiteral("game")).toString(), QStringLiteral("sf2"));
        QVERIFY(entry.value(QStringLiteral("name")).toString().endsWith(QStringLiteral(" (ann)")));
        QCOMPARE(entry.value(QStringLiteral("detail")).toString(), QStringLiteral("Hosted by ann in ES • sf2.zip"));
        // The Arcade screen's filter, after All Games.
        const QVariantList filters = arcade.filters();
        QCOMPARE(filters.value(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("online"));
        QCOMPARE(filters.value(1).toMap().value(QStringLiteral("count")).toInt(), 1);
        const QVariantMap row = arcade.filterGames(QStringLiteral("online"), 0, 7).value(0).toMap();
        QCOMPARE(row.value(QStringLiteral("set")).toString(), QStringLiteral("sf2"));
        QCOMPARE(row.value(QStringLiteral("room")).toInt(), 11);
        QCOMPARE(row.value(QStringLiteral("host")).toString(), QStringLiteral("ann"));
        // A room's row, by its id; one gone, the game's first.
        QCOMPARE(arcade.filterIndex(QStringLiteral("online"), QStringLiteral("sf2"), 11), 0);
        QCOMPARE(arcade.filterIndex(QStringLiteral("online"), QStringLiteral("sf2"), 99), 0);
        QCOMPARE(arcade.filterIndex(QStringLiteral("online"), QStringLiteral("pacman"), 99), -1);

        // Joined: RetroArch connects to the room, under the default name. The
        // JAMMA board's sticks play on the host's controller types, as any
        // pad: no arcade panel, and a remap file left by a game off line
        // removed.
        controllers::setPlayerEnvironment({ { QStringLiteral("OSDOS_PLAYER1_NAME"), QStringLiteral("OSD/OS JAMMA P1") },
                                            { QStringLiteral("OSDOS_PLAYER1_GUID"),
                                              QStringLiteral("06002868010000000100000001000000") },
                                            { QStringLiteral("OSDOS_PLAYER2_NAME"), QStringLiteral("OSD/OS JAMMA P2") },
                                            { QStringLiteral("OSDOS_PLAYER2_GUID"),
                                              QStringLiteral("06006869010000000200000001000000") } });
        const QString remap = data.path() + QStringLiteral("/arcade/retroarch/remaps/FinalBurn Neo/FinalBurn Neo.rmp");
        writeFile(remap, "input_libretro_device_p1 = \"261\"\n");
        const auto onPads = [](const QString &config) {
            for (const char *line : { "input_player1_reserved_device = \"0001:0001 OSD/OS JAMMA P1\"",
                                      "input_player1_b_btn = \"0\"", "input_player2_up_btn = \"11\"" }) {
                if (!config.contains(QLatin1String(line)))
                    return false;
            }
            return !config.contains(QStringLiteral("input_player1_up_axis"));
        };
        const auto ended = [&arcade](const std::function<bool()> &start) {
            QSignalSpy finished(&arcade, &ArcadeBackend::finished);
            if (!start())
                return QVariantList{ -1, QStringLiteral("not started"), arcade.lastError() };
            if (!finished.wait(10000) && finished.isEmpty())
                return QVariantList{ -1, QStringLiteral("no end"), QString() };
            return finished.first();
        };
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.joinRoom(11); }).value(1).toString(), QStringLiteral("ok"));
        const QString home = data.path() + QStringLiteral("/arcade/retroarch");
        QCOMPARE(logLines(QStringLiteral("retroarch ")).value(0),
                 QStringLiteral("retroarch -L %1 --config %2/retroarch.cfg --verbose --log-file=%2/last.log "
                                "--connect 203.0.113.5|55435 %3/sf2.zip").arg(libretro::downloadedCore(data.path()), home, games));
        QString config = QString::fromUtf8(contents(configCopy));
        QVERIFY2(config.contains(QStringLiteral("netplay_nickname = \"OSD-OS\"")), qPrintable(config));
        QVERIFY(config.contains(QStringLiteral("netplay_start_as_spectator = \"false\"")));
        QVERIFY(!config.contains(QStringLiteral("netplay_public_announce")));
        QVERIFY(onPads(config));
        QVERIFY(!QFileInfo::exists(remap));
        // Hosted: announced in the lobby through the relay chosen, under the
        // online name.
        arcade.onSettingChanged(module, QStringLiteral("online_name"), QStringLiteral(" Mehmet "));
        arcade.onSettingChanged(module, QStringLiteral("relay_server"), QStringLiteral("Madrid"));
        QFile::remove(m_log);
        writeFile(remap, "input_libretro_device_p1 = \"261\"\n");
        QCOMPARE(ended([&arcade] { return arcade.hostGame(QStringLiteral("sf2")); }).value(1).toString(),
                 QStringLiteral("ok"));
        QVERIFY(logLines(QStringLiteral("retroarch ")).value(0).contains(QStringLiteral(" --host ")));
        config = QString::fromUtf8(contents(configCopy));
        for (const char *line : { "netplay_nickname = \"Mehmet\"", "netplay_public_announce = \"true\"",
                                  "netplay_use_mitm_server = \"true\"", "netplay_mitm_server = \"madrid\"" })
            QVERIFY2(config.contains(QLatin1String(line)), line);
        QVERIFY(onPads(config));
        QVERIFY(!QFileInfo::exists(remap));
        // A room gone from the lobby, and a game that can't be hosted.
        QVERIFY(!arcade.joinRoom(99));
        QCOMPARE(arcade.lastError(), QStringLiteral("The game is no longer in RetroArch's lobby"));
        QVERIFY(!arcade.hostGame(QStringLiteral("pacman")));

        // An answer far bigger than a list of rooms isn't read whole.
        big = true;
        arcade.refreshRooms();
        QVERIFY(roomsRead.wait(5000));
        big = false;
        QVERIFY(arcade.rooms().isEmpty());
        QCOMPARE(arcade.requestEntries(QStringLiteral("online")).toList().value(0).toMap().value(QStringLiteral("detail")).toString(),
                 QStringLiteral("RetroArch's lobby can't be reached: its answer is too big"));

        // The lobby gone: said so where the rooms were.
        server.close();
        arcade.refreshRooms();
        QVERIFY(roomsRead.wait(5000));
        QVERIFY(arcade.rooms().isEmpty());
        const QVariantMap gone = arcade.requestEntries(QStringLiteral("online")).toList().value(0).toMap();
        QCOMPARE(gone.value(QStringLiteral("kind")).toString(), QStringLiteral("message"));
        QCOMPARE(gone.value(QStringLiteral("name")).toString(), QStringLiteral("Lobby Not Reachable"));
        QVERIFY(gone.value(QStringLiteral("detail")).toString().startsWith(QStringLiteral("RetroArch's lobby can't be reached")));
        QCOMPARE(arcade.filters().value(1).toMap().value(QStringLiteral("id")).toString() == QLatin1String("online"), false);
    }

    // A game started while MAME is asked about the sets stops that MAME and
    // holds the asking until it has ended: MAME isn't run beside a game. The
    // sets the stopped MAME hadn't answered for are asked about first.
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
        QVERIFY(arcade.scanning());
        QVERIFY(finished.wait(10000));
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
        // The first batch never answered: stopped as the game started, and
        // asked about again once it had ended, before the second.
        QStringList order;
        const QStringList lines = logLines();
        for (const QString &line : lines) {
            const QString word = line.section(QLatin1Char(' '), 0, 0);
            if (word == QLatin1String("listxml") || word == QLatin1String("answered")
                || word == QLatin1String("play") || word == QLatin1String("end"))
                order << word;
        }
        QCOMPARE(order, QStringList({ QStringLiteral("listxml"), QStringLiteral("play"), QStringLiteral("end"),
                                      QStringLiteral("listxml"), QStringLiteral("answered"),
                                      QStringLiteral("listxml"), QStringLiteral("answered") }));
        const QStringList asked = logLines(QStringLiteral("listxml"));
        QCOMPARE(asked.at(1), asked.at(0));
        QVERIFY(asked.at(0).startsWith(QStringLiteral("listxml a_slowlist g000 ")));
        QCOMPARE(asked.at(2), QStringLiteral("listxml g299 slowgame"));
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("unknown"))).size(), 302);

        // A game started while the folder is read: the sets new to it are
        // asked about once the game has ended, not beside it.
        writeFile(games + QStringLiteral("/newgame.zip"));
        QFile::remove(m_log);
        finished.clear();
        arcade.refresh();
        QVERIFY(arcade.play(QStringLiteral("slowgame")));
        QVERIFY(finished.wait(10000));
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
        order.clear();
        for (const QString &line : logLines()) {
            const QString word = line.section(QLatin1Char(' '), 0, 0);
            if (word == QLatin1String("listxml") || word == QLatin1String("play") || word == QLatin1String("end"))
                order << word;
        }
        QCOMPARE(order, QStringList({ QStringLiteral("play"), QStringLiteral("end"), QStringLiteral("listxml") }));
        QCOMPARE(logLines(QStringLiteral("listxml")), QStringList({ QStringLiteral("listxml newgame") }));
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
        refreshed(arcade);
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
        refreshed(arcade);
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
            // A TTL game's kind, as a CPU's game's.
            { "TTL * Driving / Race", "Racing" },                // Gran Trak 10
            { "TTL * Shooter / Gun", "Light Gun" },
            { "TTL * Ball & Paddle / Pong", "Ball & Paddle" },   // Pong
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

        // Asked again: only the lists while MAME is asked (once the folder
        // has been read), then all of them once it has answered.
        QSignalSpy changed(&arcade, &ArcadeBackend::gamesChanged);
        QSignalSpy reading(&arcade, &ArcadeBackend::readingChanged);
        arcade.rescan();
        QVERIFY(arcade.scanning());
        QVERIFY(reading.wait(10000));
        QVERIFY(!arcade.reading());
        QVERIFY(arcade.scanning());
        QCOMPARE(described(arcade.filters()), QStringList({ QStringLiteral("favorites Favorites 2"),
                                                            QStringLiteral("recent Recently Played 1") }));
        QVERIFY(arcade.filterGames(QStringLiteral("all"), 0, 3).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
        QVERIFY(!changed.isEmpty());
        QCOMPARE(arcade.filters().size(), 13);
    }

    // No catver.ini beside the games nor kept: one downloaded into the
    // module's folder once the folder is read, and read with it, the genres
    // then among the filters; not again once there. An answer that isn't
    // one, not kept, and not asked for again at once.
    void genresDownloaded() {
        const QString kept = m_data.path() + QStringLiteral("/arcade/catver.ini");
        const auto restore = qScopeGuard([kept] { QFile::remove(kept); });
        QFile::remove(m_games + QStringLiteral("/catver.ini"));
        QFile::remove(kept);
        StandIn standIn;
        QVERIFY(serve(standIn));
        standIn.answers = {
            { "/media/catver.ini", "[FOLDER_SETTINGS]\nRootFolderIcon mame\n\n[Category]\nsf2=Fighter / Versus\r\n"
                                   "galaga=Shooter / Flying Vertical\r\n" },
            { "/media/page.html", "<html>Not Found</html>" },
        };
        const auto genres = [](ArcadeBackend &arcade) {
            QStringList ids;
            for (const QVariant &v : arcade.filters()) {
                const QString id = v.toMap().value(QStringLiteral("id")).toString();
                if (id.startsWith(QLatin1String("genres/")))
                    ids << id;
            }
            return ids;
        };

        AppCore core(m_data.path(), m_data.path());
        {
            ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
            arcade.setGenresUrl(standIn.media().resolved(QUrl(QStringLiteral("page.html"))));
            refreshed(arcade);
            QTRY_VERIFY_WITH_TIMEOUT(standIn.asked.contains(QStringLiteral("/media/page.html")), 5000);
            QTest::qWait(300);
            QVERIFY(!QFileInfo::exists(kept));
            QVERIFY(genres(arcade).isEmpty());
            standIn.asked.clear();
            refreshed(arcade);
            QTest::qWait(300);
            QVERIFY(standIn.asked.isEmpty());
        }

        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        arcade.setGenresUrl(standIn.media().resolved(QUrl(QStringLiteral("catver.ini"))));
        standIn.asked.clear();
        refreshed(arcade);
        QTRY_COMPARE_WITH_TIMEOUT(genres(arcade), QStringList({ QStringLiteral("genres/Fighting"),
                                                                QStringLiteral("genres/Shmup") }), 10000);
        QCOMPARE(standIn.asked, QStringList({ QStringLiteral("/media/catver.ini") }));
        QCOMPARE(contents(kept), standIn.answers.value("/media/catver.ini"));
        refreshed(arcade);
        QTest::qWait(300);
        QCOMPARE(standIn.asked.size(), 1);
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

    // A Start Filter and Menu Filters' publishers saved under a company's
    // other names, before they were merged (Nintendo of America, SNK
    // Playmore), are saved again under the company, on where any of its
    // names was.
    void publishersMerged() {
        const QString module = QStringLiteral("com.osdos.arcade");
        AppCore core(m_data.path(), m_data.path());
        const auto restore = qScopeGuard([&core, &module] {
            core.save_setting(module, QStringLiteral("menu_filters"), QVariantMap());
            core.save_setting(module, QStringLiteral("start_filter"), QString());
        });
        core.save_setting(module, QStringLiteral("start_filter"), QStringLiteral("makers/Nintendo of America"));
        core.save_setting(module, QStringLiteral("menu_filters"), QVariantMap{
            { QStringLiteral("makers/SNK Playmore"), true }, { QStringLiteral("makers/SNK"), false },
            { QStringLiteral("makers/Capcom U.S.A."), false }, { QStringLiteral("makers/Namco"), true },
            { QStringLiteral("genres/Fighting"), false } });
        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        QCOMPARE(core.get_setting(module, QStringLiteral("start_filter")).toString(), QStringLiteral("makers/Nintendo"));
        QCOMPARE(core.get_setting(module, QStringLiteral("menu_filters")).toMap(), (QVariantMap{
            { QStringLiteral("makers/SNK"), true }, { QStringLiteral("makers/Capcom"), false },
            { QStringLiteral("makers/Namco"), true }, { QStringLiteral("genres/Fighting"), false } }));
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
        QCOMPARE(cache.value(QStringLiteral("version")).toInt(), 3);
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
