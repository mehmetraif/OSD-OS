#include "AppCore.h"
#include "input/ControllerProfile.h"
#include "modules/arcade/ArcadeBackend.h"
#include "modules/arcade/FbneoInstaller.h"
#include "modules/arcade/Libretro.h"
#include "modules/arcade/ProgramInstaller.h"
#include "modules/arcade/Mame.h"
#include "modules/arcade/RetroAchievements.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QtEndian>
#include <QtTest>

#include <functional>

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
	sf2b) echo '<machine name="sf2b" sourcefile="capcom/cps1.cpp" cloneof="sf2" romof="sf2"><description>Street Fighter II: The World Warrior (bootleg, set 1)</description><year>1991</year><manufacturer>bootleg</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	pacmanf) echo '<machine name="pacmanf" sourcefile="pacman/pacman.cpp" cloneof="puckman" romof="puckman"><description>Pac-Man (Midway, speedup hack)</description><year>1980</year><manufacturer>Namco (Midway license)</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	mspacman) echo '<machine name="mspacman" sourcefile="pacman/pacman.cpp"><description>Ms. Pac-Man</description><year>1981</year><manufacturer>Midway / General Computer Corporation</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
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
	echo "labels ${SDL_GAMECONTROLLER_USE_BUTTON_LABELS:-}"
	echo "mappings ${SDL_GAMECONTROLLERCONFIG:-}"
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
        // Paths answered late, by how many milliseconds.
        QHash<QByteArray, int> delays;
        // Paths answered with another status first: its line, with any
        // headers of its own after \r\n, and the body that goes with it ({}
        // unless statusBodies says), as many times as failTimes says (every
        // time for a path it leaves out).
        QHash<QByteArray, QByteArray> statuses;
        QHash<QByteArray, QByteArray> statusBodies;
        QHash<QByteArray, int> failTimes;
        // The Web API keys it takes.
        QStringList keys{ QStringLiteral("key1") };
        QStringList asked;
        // Told of each path as it is asked for.
        std::function<void(const QString &path)> onAsk;
        QUrl api() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/API/").arg(server.serverPort())); }
        QUrl media() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/media/").arg(server.serverPort())); }
        // ArcadeDB's scraper service, which the same stand-in plays.
        QUrl service() const {
            return QUrl(QStringLiteral("http://127.0.0.1:%1/service_scraper.php").arg(server.serverPort()));
        }
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
                // The Web API's and the scraper service's are told apart by
                // their queries; the scraper's may be answered for its path
                // alone, whatever it asks about.
                const bool withQuery = api || url.path() == QLatin1String("/service_scraper.php");
                const QByteArray path = (withQuery ? url.path() + QLatin1Char('?') + query.toString() : url.path()).toLatin1();
                const QByteArray bare = url.path().toLatin1();
                standIn.asked << QString::fromLatin1(path);
                if (standIn.onAsk)
                    standIn.onAsk(QString::fromLatin1(path));
                QByteArray status = "200 OK";
                const bool known = standIn.answers.contains(path) || (!api && standIn.answers.contains(bare));
                QByteArray body = standIn.answers.contains(path) ? standIn.answers.value(path) : standIn.answers.value(bare);
                const auto failing = [&standIn](const QByteArray &p) {
                    if (!standIn.statuses.contains(p))
                        return false;
                    if (!standIn.failTimes.contains(p))
                        return true;
                    if (standIn.failTimes[p] <= 0)
                        return false;
                    --standIn.failTimes[p];
                    return true;
                };
                if (api && !standIn.keys.contains(key))
                    status = "401 Unauthorized", body = "{}";
                else if (failing(path))
                    status = standIn.statuses.value(path), body = standIn.statusBodies.value(path, "{}");
                else if (!known)
                    status = "404 Not Found", body.clear();
                const QByteArray answer = "HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(body.size())
                                          + "\r\nConnection: close\r\n\r\n" + body;
                const auto send = [socket, answer] {
                    socket->write(answer);
                    socket->disconnectFromHost();
                };
                if (const int delay = standIn.delays.value(path); delay > 0)
                    QTimer::singleShot(delay, socket, send);
                else
                    send();
            });
        });
        return true;
    }
    // A picture as RetroAchievements' media server sends one: its first bytes.
    static QByteArray png(const QByteArray &mark) { return QByteArray("\x89PNG\r\n\x1a\n", 8) + mark; }
    static QByteArray jpeg(const QByteArray &mark) { return QByteArray("\xff\xd8\xff\xe0", 4) + mark; }
    static QByteArray hashOf(const char *file) { return libretro::achievementsHash(QLatin1String(file)).toLatin1(); }

    // A host's answer to RetroArch's query on the local network, as
    // RetroArch sends it (its struct ad_packet): "RANS", the game's CRC, the
    // port to join at and the passwords it asks for, big-endian, then each
    // string in a field of its own size, ended by a NUL within it.
    static QByteArray lanAnswer(const QByteArray &host, const QByteArray &content, quint32 port,
                                quint32 passwords = 0, const QByteArray &retroarch = "1.22.2", quint32 crc = 0,
                                const QByteArray &core = "FinalBurn Neo") {
        QByteArray packet("RANS");
        for (const quint32 number : { crc, port, passwords }) {
            char bytes[4];
            qToBigEndian(number, bytes);
            packet.append(bytes, 4);
        }
        const auto field = [&packet](const QByteArray &text, int size) {
            packet.append(text.left(size - 1).leftJustified(size, '\0'));
        };
        field(host, 32);
        field("unix x64", 32);
        field(core, 32);
        field("v1.0.0.03 6e7c2a1", 32);
        field(retroarch, 32);
        field(content, 256);
        field("N/A", 256);
        return packet;
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
        // No catver.ini downloaded but where a test asks for one, and no
        // ArcadeDB asked about the games' pictures but a stand-in a test
        // names (setPictureServer()): a test machine may be on line.
        qputenv("OSDOS_GENRES_URL", "");
        qputenv("OSDOS_ARCADEDB_URL", "");
        // The device's name, an invented one: LAN Play's games are hosted
        // under it.
        qputenv("OSDOS_DEVICE_NAME", "garage-cab");
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
        qunsetenv("OSDOS_ARCADEDB_URL");
        qunsetenv("MAME_LOG");
        qunsetenv("OSDOS_MEDIA_DIR");
        qunsetenv("OSDOS_DRM_DEVICE");
        qunsetenv("OSDOS_DEVICE_NAME");
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
        // A bootleg or a hack: said in the name's parentheses or by the
        // maker, in either case; not a game named after one.
        const auto unofficial = [](const char *description, const char *manufacturer) {
            mame::Machine machine;
            machine.description = QLatin1String(description);
            machine.manufacturer = QLatin1String(manufacturer);
            return mame::isBootlegOrHack(machine);
        };
        QVERIFY(unofficial("Galaga (bootleg)", "bootleg"));
        QVERIFY(unofficial("Ms. Pac-Man (bootleg, set 1)", "bootleg"));
        QVERIFY(unofficial("Pac-Man (Midway, speedup hack)", "Namco (Midway license)"));
        QVERIFY(unofficial("Puckman (Japan set 1, Probably Bootleg)", "Namco"));
        QVERIFY(unofficial("Mortal Kombat (Yawdim bootleg, set 1)", "bootleg (Yawdim)"));
        QVERIFY(unofficial("Pac-Man (Hearts)", "hack"));
        QVERIFY(unofficial("Street Fighter II' (Rainbow set 1)", "bootleg / hack"));
        QVERIFY(!unofficial("Galaga (Namco rev. B)", "Namco"));
        QVERIFY(!unofficial("Pac-Man (Midway)", "Namco (Midway license)"));
        QVERIFY(!unofficial("Hack Attack (US)", "Hackers Entertainment"));
        QVERIFY(!unofficial("Bootleg Sam", "Shackled Games"));
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
        // A player's Arcade Buttons: their buttons, coin and start in every
        // game, MAME's keys for them still beside them (players 1 to 4
        // have some), NONE for one with neither; and their Coin and Start
        // the way out. A player with none, or without a controller, keeps
        // MAME's own.
        const QStringList own{ QStringLiteral("BUTTON3"), QStringLiteral("BUTTON4"), QStringLiteral("BUTTON6"),
                               QStringLiteral("BUTTON1"), QStringLiteral("BUTTON2"), QStringLiteral("SLIDER2_NEG_SWITCH"),
                               QString(), QStringLiteral("ZAXIS_NEG_SWITCH"), QStringLiteral("BUTTON5"),
                               QStringLiteral("START") };
        const QString mapped = mame::controllerConfig({ QStringLiteral("aaaa"), QStringLiteral("bbbb"), QString(),
                                                        QString(), QStringLiteral("eeee") },
                                                      { own, {}, own, {}, own });
        const auto sequence = [&mapped](const QString &port) {
            const QRegularExpression re(QStringLiteral("<port type=\"%1\">\\s*<newseq type=\"standard\">([^<]*)</newseq>").arg(port));
            const QRegularExpressionMatch m = re.match(mapped);
            return m.hasMatch() ? m.captured(1) : QStringLiteral("<no port>");
        };
        QCOMPARE(sequence(QStringLiteral("P1_BUTTON1")),
                 QStringLiteral("KEYCODE_LCONTROL OR MOUSECODE_1_BUTTON1 OR GUNCODE_1_BUTTON1 OR JOYCODE_1_BUTTON3"));
        QCOMPARE(sequence(QStringLiteral("P1_BUTTON6")), QStringLiteral("KEYCODE_X OR JOYCODE_1_SLIDER2_NEG_SWITCH"));
        QCOMPARE(sequence(QStringLiteral("P1_BUTTON7")), QStringLiteral("KEYCODE_C"));
        QCOMPARE(sequence(QStringLiteral("P1_BUTTON8")), QStringLiteral("KEYCODE_V OR JOYCODE_1_ZAXIS_NEG_SWITCH"));
        QCOMPARE(sequence(QStringLiteral("COIN1")), QStringLiteral("KEYCODE_5 OR JOYCODE_1_BUTTON5"));
        QCOMPARE(sequence(QStringLiteral("START1")), QStringLiteral("KEYCODE_1 OR JOYCODE_1_START"));
        QCOMPARE(sequence(QStringLiteral("P2_BUTTON1")), QStringLiteral("<no port>"));
        QCOMPARE(sequence(QStringLiteral("P3_BUTTON1")), QStringLiteral("<no port>"));
        QCOMPARE(sequence(QStringLiteral("P5_BUTTON1")), QStringLiteral("JOYCODE_5_BUTTON3"));
        QCOMPARE(sequence(QStringLiteral("P5_BUTTON7")), QStringLiteral("NONE"));
        QCOMPARE(sequence(QStringLiteral("COIN5")), QStringLiteral("JOYCODE_5_BUTTON5"));
        QCOMPARE(sequence(QStringLiteral("START5")), QStringLiteral("JOYCODE_5_START"));
        QVERIFY(mapped.contains(QStringLiteral(">KEYCODE_ESC OR JOYCODE_1_BUTTON5 JOYCODE_1_START"
                                               " OR JOYCODE_2_SELECT JOYCODE_2_START"
                                               " OR JOYCODE_5_BUTTON5 JOYCODE_5_START<")));
        // MAME's keys elsewhere: player 2's, 3's and 4's.
        const QString others = mame::controllerConfig({ QStringLiteral("aaaa"), QStringLiteral("bbbb"),
                                                        QStringLiteral("cccc"), QStringLiteral("dddd") },
                                                      { {}, own, own, own });
        QVERIFY(others.contains(QStringLiteral(">KEYCODE_A OR MOUSECODE_2_BUTTON1 OR GUNCODE_2_BUTTON1 OR JOYCODE_2_BUTTON3<")));
        QVERIFY(others.contains(QStringLiteral(">KEYCODE_E OR JOYCODE_2_BUTTON2<")));
        QVERIFY(others.contains(QStringLiteral(">JOYCODE_2_SLIDER2_NEG_SWITCH<")));
        QVERIFY(others.contains(QStringLiteral(">KEYCODE_ENTER OR JOYCODE_3_BUTTON6<")));
        QVERIFY(others.contains(QStringLiteral(">KEYCODE_0PAD OR JOYCODE_4_BUTTON3<")));
        QVERIFY(others.contains(QStringLiteral(">KEYCODE_8 OR JOYCODE_4_BUTTON5<")));
        QVERIFY(others.contains(QStringLiteral(">KEYCODE_ESC OR JOYCODE_1_SELECT JOYCODE_1_START")));
        // No controller for player 1: MAME's first joystick's own way out.
        QVERIFY(mame::controllerConfig({ QString() }, { own })
                    .contains(QStringLiteral(">KEYCODE_ESC OR JOYCODE_1_SELECT JOYCODE_1_START<")));
        QVERIFY(!mame::controllerConfig({ QString() }, { own }).contains(QStringLiteral("P1_BUTTON1")));

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
        // Written off the GUI thread: a moment after the scan ended.
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(m_data.path() + QStringLiteral("/arcade/games.json")), 5000);
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
        // Every pad's mapping as OSD/OS's SDL has it goes to MAME's.
        controllers::setPadMappings({ QStringLiteral("03000000000000000000000000000000,Pad One,a:b0,b:b1") });
        const auto noMappings = qScopeGuard([] { controllers::setPadMappings({}); });
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
        QVERIFY(!log.contains(QStringLiteral("P1_BUTTON1")));
        // A pad's buttons by their places, as the menus have them.
        QCOMPARE(logLines(QStringLiteral("labels")), QStringList({ QStringLiteral("labels 0") }));
        QCOMPARE(logLines(QStringLiteral("mappings")),
                 QStringList({ QStringLiteral("mappings 03000000000000000000000000000000,Pad One,a:b0,b:b1") }));
        QVERIFY(QFileInfo(home + QStringLiteral("/cfg")).isDir());
        QVERIFY(QFileInfo(home + QStringLiteral("/nvram")).isDir());

        // Player 2's Arcade Buttons, as InputManager gives them: theirs in
        // the game, and their Coin and Start the way out.
        QStringList own(controllers::kArcadeInputs, QString());
        own[0] = QStringLiteral("BUTTON3");
        own[controllers::kArcadeCoin] = QStringLiteral("BUTTON6");
        own[controllers::kArcadeStart] = QStringLiteral("START");
        controllers::setPlayerArcade({ {}, { own, {} } });
        const auto noArcade = qScopeGuard([] { controllers::setPlayerArcade({}); });
        QFile::remove(m_log);
        QCOMPARE(played(arcade, QStringLiteral("pacman")).at(1).toString(), QStringLiteral("ok"));
        const QString withArcade = logLines().join(QLatin1Char('\n'));
        QVERIFY(withArcade.contains(QStringLiteral("<newseq type=\"standard\">KEYCODE_A OR MOUSECODE_2_BUTTON1"
                                                   " OR GUNCODE_2_BUTTON1 OR JOYCODE_2_BUTTON3</newseq>")));
        QVERIFY(withArcade.contains(QStringLiteral("<newseq type=\"standard\">KEYCODE_6 OR JOYCODE_2_BUTTON6</newseq>")));
        QVERIFY(withArcade.contains(QStringLiteral("KEYCODE_ESC OR JOYCODE_1_SELECT JOYCODE_1_START"
                                                   " OR JOYCODE_2_BUTTON6 JOYCODE_2_START OR JOYCODE_3_SELECT JOYCODE_3_START")));
        QVERIFY(!withArcade.contains(QStringLiteral("P1_BUTTON1")));
        controllers::setPlayerArcade({});

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
        QHash<QString, mame::Machine> machines;
        const QHash<QString, QString> games = libretro::readDat(dir.path() + QStringLiteral("/fbneo.dat"), &aspects,
                                                                &machines);
        QCOMPARE(games.size(), 7);
        // The pictures' shapes, where the list says.
        QCOMPARE(aspects.size(), 2);
        QVERIFY(!aspects.contains(QStringLiteral("1943")));
        QCOMPARE(aspects.value(QStringLiteral("sf2")), 4.0 / 3.0);
        QCOMPARE(aspects.value(QStringLiteral("galaga")), 3.0 / 4.0);
        QCOMPARE(games.value(QStringLiteral("sf2")), QStringLiteral("Street Fighter II - The World Warrior (World 910522)"));
        QVERIFY(games.contains(QStringLiteral("sf2ce")));
        QVERIFY(!games.contains(QStringLiteral("neogeo")));
        // Each game as MAME would describe it: its year and maker, a clone's
        // parent, its driver named as MAME's are, whether it works, and a
        // vertical one's rotation.
        QCOMPARE(machines.size(), 7);
        const mame::Machine kof98 = machines.value(QStringLiteral("kof98"));
        QCOMPARE(kof98.name, QStringLiteral("kof98"));
        QCOMPARE(kof98.description, QStringLiteral("The King of Fighters '98 - The Slugfest"));
        QCOMPARE(kof98.year, QStringLiteral("1998"));
        QCOMPARE(kof98.manufacturer, QStringLiteral("SNK"));
        QCOMPARE(kof98.sourceFile, QStringLiteral("neogeo.cpp"));
        QCOMPARE(mame::systemOf(kof98.sourceFile), QStringLiteral("Neo-Geo"));
        QCOMPARE(kof98.status, QStringLiteral("preliminary"));
        QVERIFY(kof98.cloneOf.isEmpty());
        QVERIFY(kof98.isGame());
        QCOMPARE(machines.value(QStringLiteral("sf2ce")).cloneOf, QStringLiteral("sf2"));
        QCOMPARE(machines.value(QStringLiteral("sf2ce")).sourceFile, QStringLiteral("capcom/cps1.cpp"));
        QCOMPARE(machines.value(QStringLiteral("galaga")).rotate, 90);
        QCOMPARE(machines.value(QStringLiteral("1943")).rotate, 90);
        QCOMPARE(machines.value(QStringLiteral("sf2")).rotate, 0);
        QCOMPARE(machines.value(QStringLiteral("sf2")).status, QStringLiteral("good"));
        QVERIFY(libretro::readDat(dir.path() + QStringLiteral("/none.dat"), nullptr, &machines).isEmpty());
        QVERIFY(machines.isEmpty());

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
        launch.infoDir = QStringLiteral("/ra/info");
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
                                  // A keyboard's coin and start as MAME has them, in
                                  // place of RetroArch's Right Shift and Enter.
                                  "input_player1_select = \"num5\"", "input_player1_start = \"num1\"",
                                  "input_player2_select = \"num6\"", "input_player2_start = \"num2\"",
                                  "input_player4_select = \"num8\"", "input_player4_start = \"num4\"",
                                  "menu_driver = \"rgui\"", "ui_companion_enable = \"false\"",
                                  "system_directory = \"/ra/system\"", "savefile_directory = \"/ra/saves\"",
                                  "input_player1_device_reservation_type = \"2\"",
                                  "input_player1_reserved_device = \"045e:028e Xbox 360 Controller\"",
                                  "input_player3_reserved_device = \"JAMMA P3\"",
                                  "cheevos_enable = \"true\"", "cheevos_username = \"player\"", "cheevos_token = \"\"",
                                  "cheevos_password = \"secret\"", "cheevos_hardcore_mode_enable = \"true\"",
                                  // The core known by its info file: among the cores of
                                  // its own folder, the file OSD/OS writes read afresh.
                                  "libretro_directory = \"/cores\"", "libretro_info_path = \"/ra/info\"",
                                  "core_info_cache_enable = \"false\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        // That file, by the core's name: its states deterministic, the same
        // on every machine, without which RetroArch neither hosts nor joins
        // a game with it.
        QCOMPARE(libretro::coreInfoPath(QStringLiteral("/ra/info")), QStringLiteral("/ra/info/fbneo_libretro.info"));
        const QStringList info = libretro::coreInfoText().split(QLatin1Char('\n'));
        for (const char *line : { "corename = \"FinalBurn Neo\"", "savestate = \"true\"",
                                  "savestate_features = \"deterministic\"" })
            QVERIFY2(info.contains(QLatin1String(line)), line);
        // The core's options file, where RetroArch keeps FinalBurn Neo's, and
        // the option hardcore wants in it: patched sets disallowed, which the
        // core's default allows; the rest of the file as it is, and the text
        // as it was when nothing is to change.
        QCOMPARE(libretro::coreOptionsPath(QStringLiteral("/ra/config")),
                 QStringLiteral("/ra/config/FinalBurn Neo/FinalBurn Neo.opt"));
        QCOMPARE(libretro::withHardcoreOptions(QString()), QStringLiteral("fbneo-allow-patched-romsets = \"disabled\"\n"));
        QCOMPARE(libretro::withHardcoreOptions(QStringLiteral("fbneo-cpu-speed-adjust = \"100\"\n"
                                                              "fbneo-allow-patched-romsets = \"enabled\"\n"
                                                              "fbneo-diagnostic-input = \"Start + L + R\"")),
                 QStringLiteral("fbneo-cpu-speed-adjust = \"100\"\n"
                                "fbneo-allow-patched-romsets = \"disabled\"\n"
                                "fbneo-diagnostic-input = \"Start + L + R\"\n"));
        const QString allowedOptions = QStringLiteral("fbneo-allow-patched-romsets-x = \"enabled\"\n"
                                                      "fbneo-allow-patched-romsets = \"disabled\"\n");
        QCOMPARE(libretro::withHardcoreOptions(allowedOptions), allowedOptions);
        QVERIFY(!config.contains(QStringLiteral("input_player2_reserved_device")));
        QVERIFY(!config.contains(QStringLiteral("input_player5_select = ")));
        QVERIFY(!config.contains(QStringLiteral("input_player7_")));
        QVERIFY(!config.contains(QStringLiteral("netplay_")));
        // A player on an arcade panel, a JAMMA board's stick: the cabinet's
        // two rows as FinalBurn Neo's 6-Button Panel has them (Y X L above,
        // B A R below), its 7 and 8 (the pad's triggers) where the panel
        // reads them (R2, L2), the joystick on the D-pad, and that controller
        // type in FinalBurn Neo's remap file, loaded without a word; the
        // other players as before.
        launch.panels = { false, false, true };
        launch.remapDir = QStringLiteral("/ra/remaps");
        config = libretro::configText(launch);
        for (const char *line : { "input_player3_y_btn = \"0\"", "input_player3_x_btn = \"1\"", "input_player3_l_btn = \"2\"",
                                  "input_player3_b_btn = \"3\"", "input_player3_a_btn = \"9\"", "input_player3_r_btn = \"10\"",
                                  "input_player3_select_btn = \"4\"", "input_player3_start_btn = \"6\"",
                                  "input_player3_r2_axis = \"+4\"", "input_player3_l2_axis = \"+5\"",
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
        // The file is OSD/OS's while there is none or it wrote it; one saved
        // from RetroArch's menu, or put there by hand, is the user's.
        const QString remapFile = libretro::coreRemapPath(dir.path() + QStringLiteral("/remaps"));
        QVERIFY(libretro::ownsRemap(remapFile));
        writeFile(remapFile, libretro::remapText(launch).toUtf8());
        QVERIFY(libretro::ownsRemap(remapFile));
        writeFile(remapFile, "input_libretro_device_p1 = \"1\"\ninput_remap_port_p1 = \"0\"\n");
        QVERIFY(!libretro::ownsRemap(remapFile));
        // Online, on the internet or the local network, every player plays
        // on the host's controller types. On the Classic layout, RetroArch's
        // own (a host without a panel, or one that isn't OSD/OS), a panel's
        // player has the cabinet's buttons by number (1 to 6 on B, A, Y, X,
        // R and L, 7 and 8 on the triggers), the joystick on the D-pad's
        // axes, and no remap file; the other players as before.
        using Netplay = libretro::Launch::Netplay;
        using Layout = libretro::Launch::Layout;
        for (const auto netplay : { Netplay::Join, Netplay::Host, Netplay::LanJoin, Netplay::LanHost }) {
            launch.netplay = netplay;
            const QStringList online = libretro::configText(launch).split(QLatin1Char('\n'));
            for (const char *line : { "input_player3_b_btn = \"0\"", "input_player3_a_btn = \"1\"",
                                      "input_player3_y_btn = \"2\"", "input_player3_x_btn = \"3\"",
                                      "input_player3_r_btn = \"9\"", "input_player3_l_btn = \"10\"",
                                      "input_player3_r2_axis = \"+4\"", "input_player3_l2_axis = \"+5\"",
                                      "input_player3_up_axis = \"-1\"", "input_player3_select_btn = \"4\"",
                                      "input_player1_b_btn = \"0\"", "input_player1_up_btn = \"11\"" })
                QVERIFY2(online.contains(QLatin1String(line)), line);
            QVERIFY(online.filter(QStringLiteral("input_player3_up_btn")).isEmpty());
            QCOMPARE(libretro::remapText(launch), QString());
        }
        // On the Panel layout, FinalBurn Neo's 6-Button Panel on every port
        // (a host with a panel's players), the panel's rows as off line; a
        // host's remap file sets every port, since a machine's players find
        // their pads only among ports of one kind, and a machine joining
        // takes the host's types without one.
        launch.layout = Layout::Panel;
        const QStringList everyPort{ QStringLiteral("input_libretro_device_p1 = \"261\""), QStringLiteral("input_libretro_device_p2 = \"261\""),
                                     QStringLiteral("input_libretro_device_p3 = \"261\""), QStringLiteral("input_libretro_device_p4 = \"261\""),
                                     QStringLiteral("input_libretro_device_p5 = \"261\""), QStringLiteral("input_libretro_device_p6 = \"261\"") };
        for (const auto netplay : { Netplay::LanHost, Netplay::Host }) {
            launch.netplay = netplay;
            config = libretro::configText(launch);
            for (const char *line : { "input_player3_y_btn = \"0\"", "input_player3_x_btn = \"1\"", "input_player3_l_btn = \"2\"",
                                      "input_player3_b_btn = \"3\"", "input_player3_up_axis = \"-1\"", "input_player1_b_btn = \"0\"" })
                QVERIFY2(has(QLatin1String(line)), line);
            QCOMPARE(libretro::remapText(launch).split(QLatin1Char('\n')).filter(QStringLiteral("input_libretro_device_p")), everyPort);
        }
        for (const auto netplay : { Netplay::LanJoin, Netplay::Join }) {
            launch.netplay = netplay;
            config = libretro::configText(launch);
            QVERIFY(has(QStringLiteral("input_player3_y_btn = \"0\"")));
            QVERIFY(has(QStringLiteral("input_player3_l_btn = \"2\"")));
            QCOMPARE(libretro::remapText(launch), QString());
        }
        launch.layout = Layout::Classic;
        launch.netplay = Netplay::None;
        // A player with Arcade Buttons, off line: each on the 6-Button
        // Panel's button for it (1 2 3 on Y X L, 4 5 6 on B A R, 7 on R2 and
        // 8 on L2, Coin and Start on Select and Start), a button's
        // number or an axis pushed and "nul" the other, each written once;
        // any other of the player's binds on an input those take let go; the
        // left stick a joystick beside the D-pad; and that player on the
        // panel in the remap file. A JAMMA stick's own over its panel's.
        // Online, as before.
        const auto binds = [](std::initializer_list<const char *> list) {
            QStringList out;
            for (const char *b : list)
                out << QLatin1String(b);
            return out;
        };
        launch.arcade = { {}, binds({ "2", "3", "10", "0", "1", "+5", "7", "-2", "4", "6" }) };
        config = libretro::configText(launch);
        for (const char *line : { "input_player2_y_btn = \"2\"", "input_player2_y_axis = \"nul\"",
                                  "input_player2_x_btn = \"3\"", "input_player2_l_btn = \"10\"",
                                  "input_player2_b_btn = \"0\"", "input_player2_a_btn = \"1\"",
                                  "input_player2_r_btn = \"nul\"", "input_player2_r_axis = \"+5\"",
                                  "input_player2_r2_btn = \"7\"", "input_player2_r2_axis = \"nul\"",
                                  "input_player2_l2_btn = \"nul\"", "input_player2_l2_axis = \"-2\"",
                                  "input_player2_select_btn = \"4\"", "input_player2_start_btn = \"6\"",
                                  "input_player2_l3_btn = \"nul\"", "input_player2_r_x_minus_axis = \"nul\"",
                                  "input_player2_r3_btn = \"8\"", "input_player2_up_btn = \"11\"",
                                  "input_player2_up_axis = \"-1\"", "input_player2_right_axis = \"+0\"",
                                  "input_player1_b_btn = \"0\"", "input_player1_l_btn = \"9\"",
                                  "input_player3_y_btn = \"0\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        QCOMPARE(config.count(QStringLiteral("input_player2_l2_axis =")), 1);
        QCOMPARE(config.count(QStringLiteral("input_player2_y_btn =")), 1);
        QVERIFY(!config.contains(QStringLiteral("input_player1_up_axis")));
        QCOMPARE(libretro::remapText(launch).split(QLatin1Char('\n')).filter(QStringLiteral("input_libretro_device_p")),
                 QStringList({ QStringLiteral("input_libretro_device_p2 = \"261\""),
                               QStringLiteral("input_libretro_device_p3 = \"261\"") }));
        launch.arcade = { {}, {}, binds({ "1", "0", "2", "3", "9", "10", "+4", "+5", "4", "6" }) };
        config = libretro::configText(launch);
        for (const char *line : { "input_player3_y_btn = \"1\"", "input_player3_x_btn = \"0\"",
                                  "input_player3_up_axis = \"-1\"", "input_player2_l_btn = \"9\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        launch.arcade = { {}, binds({ "2", "3", "10", "0", "1", "+5", "7", "-2", "4", "6" }) };
        launch.netplay = libretro::Launch::Netplay::Join;
        config = libretro::configText(launch);
        QVERIFY(has(QStringLiteral("input_player2_l_btn = \"9\"")));
        QVERIFY(has(QStringLiteral("input_player2_l3_btn = \"7\"")));
        QCOMPARE(libretro::remapText(launch), QString());
        // On the Panel layout the Arcade Buttons play online too, as the
        // panels do: joining without a remap file, hosting with every port.
        launch.layout = libretro::Launch::Layout::Panel;
        config = libretro::configText(launch);
        QVERIFY(has(QStringLiteral("input_player2_y_btn = \"2\"")));
        QVERIFY(has(QStringLiteral("input_player2_l3_btn = \"nul\"")));
        QVERIFY(has(QStringLiteral("input_player2_up_axis = \"-1\"")));
        QCOMPARE(libretro::remapText(launch), QString());
        launch.netplay = libretro::Launch::Netplay::LanHost;
        QCOMPARE(libretro::remapText(launch).split(QLatin1Char('\n')).filter(QStringLiteral("input_libretro_device_p")).size(), 6);
        launch.layout = libretro::Launch::Layout::Classic;
        launch.netplay = libretro::Launch::Netplay::None;
        launch.arcade.clear();
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
                                  "netplay_use_mitm_server = \"true\"", "netplay_mitm_server = \"madrid\"",
                                  "libretro_directory = \"/cores\"", "libretro_info_path = \"/ra/info\"" })
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
        // LAN Play's join: straight to the host on the local network, as a
        // lobby's room is joined.
        launch.netplay = libretro::Launch::Netplay::LanJoin;
        launch.joinAddress = QStringLiteral("192.168.1.20|55435");
        QCOMPARE(libretro::arguments(launch).mid(6),
                 QStringList({ QStringLiteral("--connect"), QStringLiteral("192.168.1.20|55435"),
                               QStringLiteral("/games/sf2.zip") }));
        config = libretro::configText(launch);
        QVERIFY(has(QStringLiteral("netplay_nickname = \"Mehmet\"")));
        QVERIFY(!config.contains(QStringLiteral("netplay_public_announce")));
        // LAN Play's host: on the local network alone, straight, where
        // RetroArch answers the queries made there; not in the lobby, nor
        // through a port opened on the router.
        launch.netplay = libretro::Launch::Netplay::LanHost;
        config = libretro::configText(launch);
        for (const char *line : { "netplay_nickname = \"Mehmet\"", "netplay_start_as_spectator = \"false\"",
                                  "netplay_public_announce = \"false\"", "netplay_use_mitm_server = \"false\"",
                                  "netplay_nat_traversal = \"false\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        QVERIFY(!config.contains(QStringLiteral("netplay_mitm_server ")));
        QCOMPARE(libretro::arguments(launch).mid(6),
                 QStringList({ QStringLiteral("--host"), QStringLiteral("/games/sf2.zip") }));

        // The game's ports this machine's players take, asked for by number:
        // up to the last player with a controller, one at least; none asked
        // for leaves RetroArch to its own, each port said so.
        QCOMPARE(libretro::localPorts({}), 1);
        QCOMPARE(libretro::localPorts({ QString(), QStringLiteral("0600bbbb") }), 2);
        QCOMPARE(libretro::localPorts({ QStringLiteral("a"), QString(), QStringLiteral("c"), QString() }), 3);
        const auto requested = [&config] {
            QStringList ports;
            for (int p = 1; p <= libretro::kMostPorts; ++p) {
                if (config.contains(QStringLiteral("netplay_request_device_p%1 = \"true\"").arg(p)))
                    ports << QString::number(p);
            }
            return ports.join(QLatin1Char(' '));
        };
        QVERIFY(has(QStringLiteral("netplay_request_device_p1 = \"false\"")));
        QVERIFY(has(QStringLiteral("netplay_request_device_p6 = \"false\"")));
        QVERIFY(!config.contains(QStringLiteral("netplay_request_device_p7")));
        QCOMPARE(requested(), QString());
        launch.portsFrom = 3;
        launch.ports = 2;
        config = libretro::configText(launch);
        QCOMPARE(requested(), QStringLiteral("3 4"));
        launch.portsFrom = 1;
        launch.ports = 6;
        config = libretro::configText(launch);
        QCOMPARE(requested(), QStringLiteral("1 2 3 4 5 6"));
        launch.netplay = libretro::Launch::Netplay::None;
        config = libretro::configText(launch);
        QVERIFY(!config.contains(QStringLiteral("netplay_")));
        launch.netplay = libretro::Launch::Netplay::LanHost;
        launch.portsFrom = 0;
        launch.ports = 0;
        // What a host's OSD/OS and a joiner's say to each other: a query for
        // so many ports, the first of them given, a release; nothing else
        // read.
        QCOMPARE(libretro::claimQuery(2), QByteArray("OSDOS PORTS? 2"));
        QCOMPARE(libretro::readClaimQuery(QByteArray("OSDOS PORTS? 2")).value_or(0), 2);
        QCOMPARE(libretro::readClaimQuery(QByteArray("OSDOS PORTS? 6")).value_or(0), 6);
        for (const char *packet : { "OSDOS PORTS? 0", "OSDOS PORTS? 7", "OSDOS PORTS? 2x", "OSDOS PORTS? 22",
                                    "OSDOS PORTS?", "OSDOS PORTS 2", "", "RANQ" })
            QVERIFY2(!libretro::readClaimQuery(QByteArray(packet)), packet);
        QCOMPARE(libretro::claimAnswer({ 3, 2 }), QByteArray("OSDOS PORTS 3 2"));
        QCOMPARE(libretro::claimAnswer({}), QByteArray("OSDOS PORTS 0 0"));
        const auto range = [](const char *packet) {
            const std::optional<libretro::PortRange> r = libretro::readClaimAnswer(QByteArray(packet));
            return r ? QStringLiteral("%1+%2").arg(r->first).arg(r->players) : QStringLiteral("none");
        };
        QCOMPARE(range("OSDOS PORTS 3 2"), QStringLiteral("3+2"));
        QCOMPARE(range("OSDOS PORTS 0 0"), QStringLiteral("0+0"));
        QCOMPARE(range("OSDOS PORTS 5 2"), QStringLiteral("5+2"));
        QCOMPARE(range("OSDOS PORTS 1 6"), QStringLiteral("1+6"));
        for (const char *packet : { "OSDOS PORTS 3", "OSDOS PORTS 6 2", "OSDOS PORTS 7 1", "OSDOS PORTS 3 0",
                                    "OSDOS PORTS 0 1", "OSDOS PORTS 3 2 1", "OSDOS PORTS? 3 1", "OSDOS PORTS 3  2",
                                    "OSDOS PORTS 3 2x", "OSDOS PORTS " })
            QCOMPARE(range(packet), QStringLiteral("none"));
        QCOMPARE(libretro::claimRelease(), QByteArray("OSDOS PORTS FREE"));
        QVERIFY(libretro::isClaimRelease(QByteArray("OSDOS PORTS FREE")));
        QVERIFY(!libretro::isClaimRelease(QByteArray("OSDOS PORTS FREE!")));
        // The host's layout, said before the ports: the panel's or Classic,
        // nothing else read, and never taken for the ports or a query.
        QCOMPARE(libretro::claimLayout(libretro::Launch::Layout::Panel), QByteArray("OSDOS LAYOUT panel"));
        QCOMPARE(libretro::claimLayout(libretro::Launch::Layout::Classic), QByteArray("OSDOS LAYOUT classic"));
        QVERIFY(libretro::readClaimLayout(QByteArray("OSDOS LAYOUT panel")) == libretro::Launch::Layout::Panel);
        QVERIFY(libretro::readClaimLayout(QByteArray("OSDOS LAYOUT classic")) == libretro::Launch::Layout::Classic);
        for (const char *packet : { "OSDOS LAYOUT modern", "OSDOS LAYOUT", "OSDOS LAYOUT panel x", "OSDOS LAYOUT  panel",
                                    "OSDOS PORTS 3 2", "OSDOS PORTS? 2", "" })
            QVERIFY2(!libretro::readClaimLayout(QByteArray(packet)), packet);
        QCOMPARE(range("OSDOS LAYOUT panel"), QStringLiteral("none"));
        QVERIFY(!libretro::readClaimQuery(QByteArray("OSDOS LAYOUT panel")));
        QVERIFY(!libretro::isClaimRelease(QByteArray("OSDOS LAYOUT panel")));
        // A host's layout by its players: the panel's with a JAMMA stick or
        // a pad's Arcade Buttons among them, else Classic.
        const auto layoutOf = [](const QList<bool> &panels, const QList<QStringList> &arcade) {
            return libretro::hostLayout(panels, arcade) == libretro::Launch::Layout::Panel ? QStringLiteral("panel")
                                                                                            : QStringLiteral("classic");
        };
        QCOMPARE(layoutOf({}, {}), QStringLiteral("classic"));
        QCOMPARE(layoutOf({ false, false }, { {}, {} }), QStringLiteral("classic"));
        QCOMPARE(layoutOf({ false, true }, {}), QStringLiteral("panel"));
        QCOMPARE(layoutOf({}, { {}, {}, { QStringLiteral("2"), QStringLiteral("3") } }), QStringLiteral("panel"));
        // The host's count: its own first, then each machine's by its
        // address, the first run of free ports that takes its players, else
        // the first free port and what follows it, within the game's ports;
        // one asking again given its own anew, one leaving giving them up.
        libretro::PortClaims claims;
        const auto given = [&claims](const char *address, int players) {
            const libretro::PortRange r = claims.claim(QLatin1String(address), players);
            return QStringLiteral("%1+%2").arg(r.first).arg(r.players);
        };
        claims.reset(2);
        QCOMPARE(given("192.168.1.30", 2), QStringLiteral("3+2"));
        QCOMPARE(given("192.168.1.31", 1), QStringLiteral("5+1"));
        QCOMPARE(given("192.168.1.32", 2), QStringLiteral("6+1"));
        QCOMPARE(given("192.168.1.32", 1), QStringLiteral("6+1"));
        QCOMPARE(given("192.168.1.30", 2), QStringLiteral("3+2"));
        QCOMPARE(given("192.168.1.30", 3), QStringLiteral("3+2"));
        QCOMPARE(given("192.168.1.30", 2), QStringLiteral("3+2"));
        claims.release(QStringLiteral("192.168.1.31"));
        QCOMPARE(given("192.168.1.33", 1), QStringLiteral("5+1"));
        claims.release(QStringLiteral("192.168.1.34"));
        QCOMPARE(given("192.168.1.35", 1), QStringLiteral("0+0"));
        claims.reset(1);
        QCOMPARE(given("a", 6), QStringLiteral("2+5"));
        QCOMPARE(given("b", 1), QStringLiteral("0+0"));
        QCOMPARE(given("b", 0), QStringLiteral("0+0"));
        claims.reset(6);
        QCOMPARE(given("a", 1), QStringLiteral("0+0"));
        claims.reset(0);
        QCOMPARE(given("a", 1), QStringLiteral("1+1"));
        // A two-player game: the host's one port and the joiner's the other,
        // whatever its players; a four-player one, two each.
        claims.reset(1, 2);
        QCOMPARE(given("a", 2), QStringLiteral("2+1"));
        QCOMPARE(given("b", 1), QStringLiteral("0+0"));
        claims.reset(1, 4);
        QCOMPARE(given("a", 2), QStringLiteral("2+2"));
        QCOMPARE(given("b", 2), QStringLiteral("4+1"));
        QCOMPARE(claims.most(), 4);
        claims.reset(3, 2);
        QCOMPARE(claims.own(), 2);
        QCOMPARE(given("a", 1), QStringLiteral("0+0"));

        // A host's answer to the query on the local network: its game, who
        // hosts it, what it plays in, and where it is joined, at the address
        // it answered from and the port it gives.
        QCOMPARE(libretro::lanQuery(), QByteArray("RANQ"));
        const QByteArray answer = lanAnswer("ann", "SF2", 55436, 0, "1.22.2", 0x1a2b3c4d);
        QCOMPARE(answer.size(), libretro::kLanAnswerSize);
        std::optional<libretro::Room> lan = libretro::readLanAnswer(answer, QStringLiteral("192.168.1.20"));
        QVERIFY(lan);
        QCOMPARE(lan->id, 0);
        QCOMPARE(lan->game, QStringLiteral("sf2"));
        QCOMPARE(lan->rom, QStringLiteral("SF2"));
        QCOMPARE(lan->crc, QStringLiteral("1A2B3C4D"));
        QCOMPARE(lan->host, QStringLiteral("ann"));
        QCOMPARE(lan->frontend, QStringLiteral("unix x64"));
        QCOMPARE(lan->core, QStringLiteral("FinalBurn Neo"));
        QCOMPARE(lan->coreVersion, QStringLiteral("v1.0.0.03 6e7c2a1"));
        QCOMPARE(lan->retroarch, QStringLiteral("1.22.2"));
        QCOMPARE(lan->subsystem, QString());
        QCOMPARE(lan->joinAddress, QStringLiteral("192.168.1.20|55436"));
        QVERIFY(!lan->password);
        QVERIFY(!lan->spectatePassword);
        QVERIFY(lan->joinable);
        // A field filled to its end, without its NUL: no more than it.
        QByteArray full = answer;
        full.replace(16, 32, QByteArray(32, 'x'));
        lan = libretro::readLanAnswer(full, QStringLiteral("192.168.1.20"));
        QCOMPARE(lan->host, QString(32, QLatin1Char('x')));
        QCOMPARE(lan->frontend, QStringLiteral("unix x64"));
        // Its passwords; another core's, and a host with no game, not joined;
        // names without markup; a port that isn't one, no address.
        lan = libretro::readLanAnswer(lanAnswer("b<o>b\x01", "N/A", 55435, 3, "1.22.2", 0, "Snes9x"),
                                      QStringLiteral("10.0.0.5"));
        QCOMPARE(lan->host, QStringLiteral("bob"));
        QVERIFY(lan->password);
        QVERIFY(lan->spectatePassword);
        QCOMPARE(lan->game, QString());
        QCOMPARE(lan->crc, QString());
        QVERIFY(!lan->joinable);
        QVERIFY(!libretro::readLanAnswer(lanAnswer("cy", "sf2", 55435, 0, "1.22.2", 0, "Snes9x"),
                                         QStringLiteral("10.0.0.5"))->joinable);
        QVERIFY(!libretro::readLanAnswer(lanAnswer("di", "N/A", 55435), QStringLiteral("10.0.0.5"))->joinable);
        lan = libretro::readLanAnswer(lanAnswer("eve", "sf2", 0), QStringLiteral("10.0.0.5"));
        QCOMPARE(lan->joinAddress, QString());
        QVERIFY(!lan->joinable);
        // What isn't an answer: another size, or another kind of packet.
        QVERIFY(!libretro::readLanAnswer(answer.chopped(1), QStringLiteral("192.168.1.20")));
        QVERIFY(!libretro::readLanAnswer(answer + 'x', QStringLiteral("192.168.1.20")));
        QVERIFY(!libretro::readLanAnswer("RANQ" + answer.mid(4), QStringLiteral("192.168.1.20")));
        // The networks RetroArch answers queries from: the private ones.
        const auto onLan = [](const char *address) {
            return libretro::isLanAddress(QHostAddress(QLatin1String(address)).toIPv4Address());
        };
        for (const char *address : { "10.1.2.3", "172.16.0.1", "172.31.255.254", "192.168.0.10" })
            QVERIFY2(onLan(address), address);
        for (const char *address : { "172.15.0.1", "172.32.0.1", "192.169.0.1", "11.0.0.1", "127.0.0.1", "100.64.0.1" })
            QVERIFY2(!onLan(address), address);
        // The answers taken: from those networks' hosts, and this machine;
        // not from the internet, a carrier's shared addresses, a link-local
        // one or IPv6.
        for (const char *address : { "192.168.1.20", "10.0.0.5", "172.16.0.1", "::ffff:192.168.1.20", "127.0.0.1" })
            QVERIFY2(libretro::isLanSender(QHostAddress(QLatin1String(address))), address);
        for (const char *address : { "8.8.8.8", "100.64.0.1", "169.254.1.1", "::1", "fe80::1", "fd00::1" })
            QVERIFY2(!libretro::isLanSender(QHostAddress(QLatin1String(address))), address);

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
        // Over the network, the first netplay error RetroArch plays on alone
        // after, its full stop dropped, passing over what lets the game go
        // on (a warning, input out of order); the last error line as before.
        writeFile(dir.path() + QStringLiteral("/joining.log"),
                  "[INFO] [Netplay] Connecting to netplay host 192.168.1.20:55435.\n"
                  "[WARN] [Netplay] WARNING: A netplay peer is running a different version of RetroArch. If problems occur, use the same version.\n"
                  "[ERROR] [Netplay] Netplay input out of order.\n"
                  "[ERROR] [Netplay] Failed to connect to host 192.168.1.20 on port 55435.\n"
                  "[ERROR] [Netplay] Failed to initialize netplay.\n"
                  "[libretro INFO] [FBNeo] Driver successfully started\n");
        run = libretro::readLog(dir.path() + QStringLiteral("/joining.log"));
        QVERIFY(run.started);
        QCOMPARE(run.netplayFailure, QStringLiteral("Failed to connect to host 192.168.1.20 on port 55435"));
        QCOMPARE(run.error, QStringLiteral("[Netplay] Failed to initialize netplay."));
        // A host turns clients away and plays on, and misses its port over
        // IPv6 before it binds it over IPv4: not a failure of its own, as a
        // port it couldn't bind at all is, the most telling line of the
        // start that failed.
        writeFile(dir.path() + QStringLiteral("/hosting.log"),
                  "[ERROR] [Netplay] Failed to bind port 55435.\n"
                  "[ERROR] [Netplay] Failed to receive header from client.\n"
                  "[ERROR] [Netplay] A netplay peer is running a different core. Cannot connect.\n"
                  "[libretro INFO] [FBNeo] Driver successfully started\n");
        QVERIFY(libretro::readLog(dir.path() + QStringLiteral("/hosting.log"), true).netplayFailure.isEmpty());
        writeFile(dir.path() + QStringLiteral("/port.log"),
                  "[ERROR] [Netplay] Failed to bind port 55435.\n"
                  "[ERROR] [Netplay] Failed to bind port 55435.\n"
                  "[ERROR] [Netplay] Failed to set up netplay sockets.\n"
                  "[ERROR] [Netplay] Failed to initialize netplay.\n");
        QCOMPARE(libretro::readLog(dir.path() + QStringLiteral("/port.log"), true).netplayFailure,
                 QStringLiteral("Failed to bind port 55435"));
        // The lines themselves: a netplay error's words, and what ends the
        // game of them, joining (the host turned this machine away, or
        // dropped it) and hosting (a start that failed, by its most telling
        // line).
        const QString refused = QStringLiteral("A netplay peer is running a different core. Cannot connect");
        QCOMPARE(libretro::netplayError(QStringLiteral("[ERROR] [Netplay] %1.").arg(refused)).value_or(QString()), refused);
        QVERIFY(!libretro::netplayError(QStringLiteral("[ERROR] [Content]: Something went wrong.")));
        QVERIFY(!libretro::netplayError(QStringLiteral("[INFO] [Netplay] Failed to initialize netplay.")));
        QCOMPARE(libretro::netplayFailure({ refused }, false), refused);
        QVERIFY(libretro::netplayFailure({ refused }, true).isEmpty());
        const QString ping = QStringLiteral("Your ping is too high for this host");
        QCOMPARE(libretro::netplayFailure({ ping, QStringLiteral("Failed to initialize netplay") }, false), ping);
        QVERIFY(libretro::netplayFailure({ QStringLiteral("Failed to set up netplay sockets") }, true).isEmpty());
        QCOMPARE(libretro::netplayFailure({ QStringLiteral("Failed to set up netplay sockets"),
                                            QStringLiteral("Failed to initialize netplay") }, true),
                 QStringLiteral("Failed to set up netplay sockets"));
        QCOMPARE(libretro::netplayFailure({ QStringLiteral("Failed to connect to relay server 1.2.3.4 on port 55435"),
                                            QStringLiteral("Failed to set up netplay sockets"),
                                            QStringLiteral("Failed to initialize netplay") }, true),
                 QStringLiteral("Failed to connect to relay server 1.2.3.4 on port 55435"));
        QCOMPARE(libretro::netplayFailure({ QStringLiteral("Failed to initialize netplay") }, false),
                 QStringLiteral("Failed to initialize netplay"));

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
        // The whole lobby: every room, newest first, as it can be shown; a
        // room FinalBurn Neo hosts that can be joined among them.
        const QList<libretro::Room> whole = libretro::readWholeLobby(lobby);
        QCOMPARE(whole.size(), 11);
        const libretro::Room &kart = whole.at(0);
        QCOMPARE(kart.id, 22);
        QCOMPARE(kart.rom, QStringLiteral("Super Mario Kart (USA)"));
        QVERIFY(kart.game.isEmpty());
        QCOMPARE(kart.crc, QStringLiteral("82993FA8"));
        QCOMPARE(kart.core, QStringLiteral("Snes9x"));
        QCOMPARE(kart.coreVersion, QStringLiteral("1.63 fae2fea"));
        QVERIFY(kart.subsystem.isEmpty());
        QCOMPARE(kart.retroarch, QStringLiteral("1.22.2"));
        QCOMPARE(kart.frontend, QStringLiteral("win32 x64"));
        QCOMPARE(kart.country, QStringLiteral("gb"));
        QCOMPARE(kart.relay, QStringLiteral("us-east1"));
        QCOMPARE(kart.players, 2);
        QCOMPARE(kart.spectators, 1);
        QVERIFY(kart.spectatePassword);
        QCOMPARE(kart.created.toUTC().toString(Qt::ISODate), QStringLiteral("2026-10-20T19:38:57Z"));
        QVERIFY(!kart.joinable);
        QStringList ids;
        for (const libretro::Room &room : whole)
            ids << QString::number(room.id) + (room.joinable ? QStringLiteral("+") : QString());
        QCOMPARE(ids, QStringList({ "22", "21", "20", "19", "18+", "17", "16", "15", "14+", "11+", "10" }));
        // What the lobby doesn't say, as such; the relay another runs by its
        // name; a name that can't be a set's none, the host's name as it is.
        QCOMPARE(whole.at(9).players, -1);
        QVERIFY(!whole.at(9).created.isValid());
        QVERIFY(whole.at(9).relay.isEmpty());
        QCOMPARE(whole.at(8).relay, QStringLiteral("198.51.100.7"));
        QVERIFY(whole.at(2).game.isEmpty());
        QCOMPARE(whole.at(2).rom, QStringLiteral("../../etc/passwd"));
        QVERIFY(!whole.at(7).connectable);
        QCOMPARE(whole.at(4).host, QStringLiteral("img src=http://192.0.2.99/a.pngz"));
        // An answer with more rooms than the lobby ever lists: the newest
        // thousand.
        QByteArray crowded = "[";
        for (int id = 1; id <= 1005; ++id)
            crowded += (id > 1 ? "," : "") + QByteArray("{\"fields\": {\"id\": ") + QByteArray::number(id) + "}}";
        crowded += "]";
        const QList<libretro::Room> kept = libretro::readWholeLobby(crowded);
        QCOMPARE(kept.size(), 1000);
        QCOMPARE(kept.first().id, 1005);
        QCOMPARE(kept.last().id, 6);
        // A core asked to describe itself; a file that isn't one, why.
        QString coreError;
        const libretro::CoreInfo testCore = libretro::loadCoreInfo(QStringLiteral(TEST_CORE), &coreError);
        QCOMPARE(testCore.name, QStringLiteral("OSD/OS Test Core"));
        QCOMPARE(testCore.version, QStringLiteral("v1.0.0.03 6e7c2a1"));
        QVERIFY(libretro::loadCoreInfo(QStringLiteral(TESTS_DIR "/data/arcade/lobby.json"), &coreError).version.isEmpty());
        QVERIFY(!coreError.isEmpty());
        // What the copy of the app printed of it, taken as the lobby's names
        // are; RetroArch's version, as a recent RetroArch prints it and as an
        // older one did.
        const libretro::CoreInfo printed = libretro::readCoreInfo("name=FinalBurn Neo\nversion=v1.0.0.03 <6e7c2a1>\n");
        QCOMPARE(printed.name, QStringLiteral("FinalBurn Neo"));
        QCOMPARE(printed.version, QStringLiteral("v1.0.0.03 6e7c2a1"));
        QCOMPARE(libretro::readVersion("RetroArch - Frontend for libretro\nVersion: 1.19.1 (Git 5a4f9b2) Oct 20 2024\n"),
                 QStringLiteral("1.19.1"));
        QCOMPARE(libretro::readVersion("RetroArch: Frontend for libretro -- v1.9.0 -- 6f4ae1f --\n"), QStringLiteral("1.9.0"));
        QVERIFY(libretro::readVersion("sh: retroarch: not found").isEmpty());
        // RetroArch.app's from its bundle, for a link to its executable too;
        // nothing for a program outside an app, "" for an Info.plist that
        // doesn't say (a binary one).
        QTemporaryDir apps;
        QVERIFY(apps.isValid());
        const QString contents = apps.path() + QStringLiteral("/RetroArch.app/Contents");
        writeFile(contents + QStringLiteral("/MacOS/RetroArch"));
        writeFile(contents + QStringLiteral("/Info.plist"),
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n"
                  "\t<key>CFBundleName</key>\n\t<string>RetroArch</string>\n"
                  "\t<key>CFBundleShortVersionString</key>\n\t<string>1.22.2</string>\n"
                  "\t<key>CFBundleVersion</key>\n\t<string>1</string>\n</dict>\n</plist>\n");
        QVERIFY(QFile::link(contents + QStringLiteral("/MacOS/RetroArch"), apps.path() + QStringLiteral("/retroarch")));
        writeFile(apps.path() + QStringLiteral("/bin/retroarch"));
        const auto bundled = [](const QString &program) {
            return libretro::bundleVersion(program).value_or(QStringLiteral("(none)"));
        };
        QCOMPARE(bundled(contents + QStringLiteral("/MacOS/RetroArch")), QStringLiteral("1.22.2"));
        QCOMPARE(bundled(apps.path() + QStringLiteral("/retroarch")), QStringLiteral("1.22.2"));
        QCOMPARE(bundled(apps.path() + QStringLiteral("/bin/retroarch")), QStringLiteral("(none)"));
        writeFile(contents + QStringLiteral("/Info.plist"), QByteArray("bplist00\xd1\x01\x02", 11));
        QCOMPARE(bundled(contents + QStringLiteral("/MacOS/RetroArch")), QString());

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
        // Without FinalBurn Neo at first: not where one is installed.
        if (!libretro::corePath(data.path()).isEmpty() || !libretro::datPath(data.path()).isEmpty())
            QSKIP("FinalBurn Neo is installed here");
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
        // Every pad's mapping as OSD/OS's SDL has it goes to RetroArch's.
        controllers::setPadMappings({ QStringLiteral("03000000000000000000000000000000,Pad One,a:b0,b:b1"),
                                      QStringLiteral("030000005e040000e002000003090000,Pad Two,a:b0") });
        const auto noMappings = qScopeGuard([] { controllers::setPadMappings({}); });
        // JAMMA 240p: Street Fighter II's 4:3 as wide as the screen's
        // pixels are narrow, its lines whole.
        arcade.setScreen(true, QSize(720, 240));
        // The core's options as the user left them, patched sets allowed as
        // FinalBurn Neo's default has it, over which RetroArch would pause
        // hardcore: the game given them with patched sets disallowed, the
        // rest as it was.
        const QString home = data.path() + QStringLiteral("/arcade/retroarch");
        const QString options = home + QStringLiteral("/config/FinalBurn Neo/FinalBurn Neo.opt");
        writeFile(options, "fbneo-cpu-speed-adjust = \"100\"\nfbneo-allow-patched-romsets = \"enabled\"\n");
        QVariantList end = played(arcade, QStringLiteral("sf2"));
        QCOMPARE(end.value(1).toString(), QStringLiteral("ok"));
        QCOMPARE(contents(options), QByteArray("fbneo-cpu-speed-adjust = \"100\"\nfbneo-allow-patched-romsets = \"disabled\"\n"));
        const QString line = logLines(QStringLiteral("retroarch ")).value(0);
        QCOMPARE(line, QStringLiteral("retroarch -L %1 --config %2/retroarch.cfg --verbose --log-file=%2/last.log %3/sf2.zip")
                           .arg(libretro::downloadedCore(data.path()), home, games));
        QVERIFY(logLines().contains(QStringLiteral("cwd ") + home));
        QVERIFY(logLines().contains(QStringLiteral("sdlconfig ") + data.path() + QStringLiteral("/gamecontrollerdb.txt")));
        // A pad's buttons by their places, as the menus have them.
        QVERIFY(logLines().contains(QStringLiteral("sdllabels 0")));
        QVERIFY(logLines().contains(QStringLiteral("sdlmappings 03000000000000000000000000000000,Pad One,a:b0,b:b1")));
        QVERIFY(logLines().contains(QStringLiteral("030000005e040000e002000003090000,Pad Two,a:b0")));
        // FinalBurn Neo's info file written for it, which has RetroArch take
        // its states for deterministic, as netplay needs them.
        QVERIFY(logLines().contains(QStringLiteral("coreinfo deterministic")));
        QVERIFY(contents(home + QStringLiteral("/info/fbneo_libretro.info")).contains("savestate_features = \"deterministic\""));
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
        // The gamepad with Arcade Buttons: theirs on the 6-Button Panel,
        // through the remap file too.
        const QStringList fighting{ QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("10"), QStringLiteral("0"),
                                    QStringLiteral("1"), QStringLiteral("+5"), QStringLiteral("9"), QStringLiteral("+4"),
                                    QStringLiteral("4"), QStringLiteral("6") };
        controllers::setPlayerArcade({ { QStringList(controllers::kArcadeInputs, QStringLiteral("BUTTON1")), fighting } });
        const auto noArcade = qScopeGuard([] { controllers::setPlayerArcade({}); });
        played(arcade, QStringLiteral("sf2"));
        config = QString::fromUtf8(contents(configCopy));
        for (const char *line : { "input_player1_y_btn = \"2\"", "input_player1_l_btn = \"10\"",
                                  "input_player1_r_axis = \"+5\"", "input_player1_r_btn = \"nul\"",
                                  "input_player1_r2_btn = \"9\"", "input_player1_l2_axis = \"+4\"",
                                  "input_player1_up_axis = \"-1\"" })
            QVERIFY2(config.contains(QLatin1String(line)), line);
        QCOMPARE(QString::fromUtf8(contents(remap)).split(QLatin1Char('\n')).filter(QStringLiteral("input_libretro_device_p")),
                 QStringList({ QStringLiteral("input_libretro_device_p1 = \"261\"") }));
        controllers::setPlayerArcade({});
        played(arcade, QStringLiteral("sf2"));
        QVERIFY(!QFileInfo::exists(remap));
        // One the user saved from RetroArch's menu is theirs: neither removed
        // without a panel nor written over with one.
        const QByteArray own("input_libretro_device_p1 = \"1\"\ninput_player1_btn_b = \"8\"\n");
        writeFile(remap, own);
        played(arcade, QStringLiteral("sf2"));
        QCOMPARE(contents(remap), own);
        controllers::setPlayerEnvironment({ { QStringLiteral("OSDOS_PLAYER1_NAME"), QStringLiteral("OSD/OS JAMMA P1") },
                                            { QStringLiteral("OSDOS_PLAYER1_GUID"),
                                              QStringLiteral("06002868010000000100000001000000") } });
        played(arcade, QStringLiteral("sf2"));
        QCOMPARE(contents(remap), own);
        QFile::remove(remap);
        controllers::setPlayerEnvironment({ { QStringLiteral("OSDOS_PLAYER1_NAME"), QStringLiteral("Xbox 360 Controller") },
                                            { QStringLiteral("OSDOS_PLAYER1_GUID"),
                                              QStringLiteral("030000005e0400008e02000014010000") } });
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
        // FinalBurn Neo's info file that can't be written (a file where its
        // folder goes): nothing started, as with RetroArch's settings.
        QVERIFY(QDir(home + QStringLiteral("/info")).removeRecursively());
        writeFile(home + QStringLiteral("/info"));
        QFile::remove(m_log);
        QVERIFY(!arcade.play(QStringLiteral("galaga")));
        QCOMPARE(arcade.lastError(), QStringLiteral("RetroArch's settings could not be written"));
        QVERIFY(logLines(QStringLiteral("retroarch ")).isEmpty());
        QVERIFY(QFile::remove(home + QStringLiteral("/info")));
        // MAME as before for the rest.
        QFile::remove(m_log);
        end = played(arcade, QStringLiteral("pacman"));
        QCOMPARE(end.value(1).toString(), QStringLiteral("ok"));
        QVERIFY(logLines(QStringLiteral("play pacman")).size() == 1);
    }

    // A set MAME doesn't know that FinalBurn Neo's list has is a game all
    // the same: listed by its description there, under its year and maker,
    // a clone with its parent's pictures, played in FinalBurn Neo whatever
    // engine was chosen for it, with no MAME to choose, and on Playable
    // Online and the RetroAchievements filter while FinalBurn Neo is there.
    // A vertical one is drawn 3:4 where the list gives no shape, and files
    // missing are said with nothing of MAME. While MAME is asked about every
    // set again (Read the Games Again, a restart in the middle of it, an
    // older games.json), what it said before holds. Not Recognized keeps the
    // sets neither has, and those FinalBurn Neo has as a folder; without
    // FinalBurn Neo's list they are all there.
    void fbneoOnlyGames() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        // Without FinalBurn Neo at first: not where one is installed.
        if (!libretro::corePath(data.path()).isEmpty() || !libretro::datPath(data.path()).isEmpty())
            QSKIP("FinalBurn Neo is installed here");
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        // MAME knows sf2 alone; FinalBurn Neo kof98, sf2ce and 1943 too, and
        // dino, kept as a folder; neither xyzzy.
        for (const char *set : { "sf2.zip", "kof98.zip", "sf2ce.7z", "1943.zip", "xyzzy.zip", "dino/cde_01.rom" })
            writeFile(games + QLatin1Char('/') + QLatin1String(set));
        writeFile(games + QStringLiteral("/snap/sf2.png"), "sf2 snap");
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        for (const char *program : { "/bin/mame", "/bin/retroarch" })
            QVERIFY(QFile::setPermissions(data.path() + QLatin1String(program),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        writeFile(data.path() + QStringLiteral("/gamecontrollerdb.txt"), "# none\n");
        const QString configCopy = data.path() + QStringLiteral("/ra.cfg");
        qputenv("RA_CONFIG_COPY", configCopy.toUtf8());
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qunsetenv("RA_CONFIG_COPY");
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        });
        const QString module = QStringLiteral("com.osdos.arcade");
        const auto setsOf = [](ArcadeBackend &arcade, const QString &filter) {
            QStringList sets;
            const QVariantList list = arcade.filterGames(filter, 0, 20);
            for (const QVariant &v : list)
                sets << v.toMap().value(QStringLiteral("set")).toString();
            return sets;
        };
        const auto labels = [](const QVariantList &choices) {
            QStringList list;
            for (const QVariant &c : choices)
                list << c.toMap().value(QStringLiteral("label")).toString()
                            + (c.toMap().value(QStringLiteral("current")).toBool() ? QStringLiteral(" *") : QString());
            return list;
        };

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        refreshed(arcade);
        // Without FinalBurn Neo's list: MAME's alone.
        QCOMPARE(setsOf(arcade, QStringLiteral("all")), QStringList({ QStringLiteral("sf2") }));
        QCOMPARE(setsOf(arcade, QStringLiteral("unknown")),
                 QStringList({ QStringLiteral("1943"), QStringLiteral("dino"), QStringLiteral("kof98"),
                               QStringLiteral("sf2ce"), QStringLiteral("xyzzy") }));
        QCOMPARE(arcade.gameDetails(QStringLiteral("xyzzy")).value(QStringLiteral("detail")).toString(),
                 QStringLiteral("MAME has no game called xyzzy\nIts set may be for another version of MAME"));

        // With it, the core there too: its games, by their descriptions.
        writeFile(libretro::downloadedCore(data.path()));
        writeFile(libretro::downloadedDat(data.path()), fixture("fbneo-arcade.dat"));
        refreshed(arcade);
        QVERIFY(arcade.fbneoReady());
        QCOMPARE(setsOf(arcade, QStringLiteral("all")),
                 QStringList({ QStringLiteral("1943"), QStringLiteral("sf2ce"), QStringLiteral("sf2"),
                               QStringLiteral("kof98") }));
        QCOMPARE(setsOf(arcade, QStringLiteral("unknown")), QStringList({ QStringLiteral("dino"), QStringLiteral("xyzzy") }));
        QCOMPARE(arcade.gameDetails(QStringLiteral("xyzzy")).value(QStringLiteral("detail")).toString(),
                 QStringLiteral("Neither MAME nor FinalBurn Neo has a game called xyzzy\n"
                                "Its set may be for another version of MAME"));
        QCOMPARE(arcade.gameDetails(QStringLiteral("dino")).value(QStringLiteral("detail")).toString(),
                 QStringLiteral("MAME has no game called dino\nFinalBurn Neo has it, but plays a zip or a 7z, not a folder"));
        QCOMPARE(arcade.filterGames(QStringLiteral("all"), 3, 1).value(0).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("The King of Fighters '98 - The Slugfest"));
        // What FinalBurn Neo's list says of it: maker, year, system; the
        // players it doesn't.
        QVariantMap details = arcade.gameDetails(QStringLiteral("kof98"));
        QCOMPARE(details.value(QStringLiteral("name")).toString(), QStringLiteral("The King of Fighters '98 - The Slugfest"));
        QCOMPARE(details.value(QStringLiteral("maker")).toString(), QStringLiteral("SNK"));
        QCOMPARE(details.value(QStringLiteral("year")).toString(), QStringLiteral("1998"));
        QCOMPARE(details.value(QStringLiteral("system")).toString(), QStringLiteral("Neo-Geo"));
        QCOMPARE(details.value(QStringLiteral("players")).toInt(), 0);
        QVERIFY(!details.value(QStringLiteral("unknown")).toBool());
        QCOMPARE(details.value(QStringLiteral("engine")).toString(), QStringLiteral("fbneo"));
        QVERIFY(setsOf(arcade, QStringLiteral("years/1990s")).contains(QStringLiteral("kof98")));
        QVERIFY(setsOf(arcade, QStringLiteral("makers/SNK")).contains(QStringLiteral("kof98")));
        // The tree's line of it, as well as FinalBurn Neo plays it.
        const QVariantList all = arcade.requestEntries(QStringLiteral("all")).toList();
        const auto entry = std::find_if(all.cbegin(), all.cend(), [](const QVariant &v) {
            return v.toMap().value(QStringLiteral("game")).toString() == QLatin1String("kof98");
        });
        QVERIFY(entry != all.cend());
        QCOMPARE(entry->toMap().value(QStringLiteral("detail")).toString(),
                 QStringLiteral("1998\nSNK\nNot working in FinalBurn Neo\nkof98.zip"));
        // A clone of FinalBurn Neo's: its parent's snap. A vertical game,
        // as the list says.
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2ce")).value(QStringLiteral("pictures")).toStringList(),
                 QStringList({ QUrl::fromLocalFile(games + QStringLiteral("/snap/sf2.png")).toString() }));
        QVERIFY(arcade.gameDetails(QStringLiteral("1943")).value(QStringLiteral("vertical")).toBool());
        QVERIFY(!arcade.gameDetails(QStringLiteral("kof98")).value(QStringLiteral("vertical")).toBool());

        // FinalBurn Neo's whatever engine was chosen, Online High Scores off;
        // no MAME offered, one chosen before taken for Auto.
        QCOMPARE(arcade.engineOf(QStringLiteral("kof98")), QStringLiteral("fbneo"));
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));
        QCOMPARE(labels(arcade.engineChoices(QStringLiteral("kof98"))),
                 QStringList({ QStringLiteral("Auto (FinalBurn Neo) *"), QStringLiteral("FinalBurn Neo") }));
        arcade.onSettingChanged(module, QStringLiteral("engines"), QVariantMap{ { QStringLiteral("kof98"), QStringLiteral("mame") } });
        QCOMPARE(arcade.engineOf(QStringLiteral("kof98")), QStringLiteral("fbneo"));
        QCOMPARE(labels(arcade.engineChoices(QStringLiteral("kof98"))),
                 QStringList({ QStringLiteral("Auto (FinalBurn Neo) *"), QStringLiteral("FinalBurn Neo") }));
        // Read the Games Again: while MAME is asked about every set again
        // (a_slowlist holding it a moment), what it said before holds, the
        // game FinalBurn Neo's still, and a set neither has still not
        // recognized.
        writeFile(games + QStringLiteral("/a_slowlist.zip"));
        arcade.rescan();
        QCOMPARE(arcade.engineOf(QStringLiteral("kof98")), QStringLiteral("fbneo"));
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.reading() && arcade.scanning(), 5000);
        QCOMPARE(arcade.engineOf(QStringLiteral("kof98")), QStringLiteral("fbneo"));
        QCOMPARE(labels(arcade.engineChoices(QStringLiteral("kof98"))),
                 QStringList({ QStringLiteral("Auto (FinalBurn Neo) *"), QStringLiteral("FinalBurn Neo") }));
        QVERIFY(arcade.gameDetails(QStringLiteral("xyzzy")).value(QStringLiteral("unknown")).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
        // OSD/OS stopped in the middle of it: games.json stays as MAME last
        // answered (what it is asked again goes in as it answers), so the
        // next start has every game without asking, kof98 FinalBurn Neo's.
        {
            ArcadeBackend stopped(data.path(), data.path(), nullptr, nullptr);
            refreshed(stopped);
            stopped.rescan();
            QTRY_VERIFY_WITH_TIMEOUT(!stopped.reading() && stopped.scanning(), 5000);
        }
        {
            QFile::remove(m_log);
            ArcadeBackend started(data.path(), data.path(), nullptr, nullptr);
            refreshed(started);
            QCOMPARE(started.engineOf(QStringLiteral("kof98")), QStringLiteral("fbneo"));
            QCOMPARE(started.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));
            QVERIFY(!started.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("unknown")).toBool());
            QVERIFY(logLines(QStringLiteral("listxml")).isEmpty());
        }
        // And so with a games.json of an older version, whose sets are all
        // asked about again, and asked before games.json was read.
        const auto engineWhileAsked = [&data](const QString &set, bool readAgain) {
            ArcadeBackend started(data.path(), data.path(), nullptr, nullptr);
            if (readAgain)
                started.rescan();
            else
                started.refresh();
            if (!QTest::qWaitFor([&started] { return !started.reading() && started.scanning(); }, 5000))
                return QStringLiteral("not asked");
            const QString engine = started.engineOf(set);
            if (!QTest::qWaitFor([&started] { return !started.scanning(); }, 10000))
                return QStringLiteral("not answered");
            return engine;
        };
        const QString kof98 = QStringLiteral("kof98");
        const QString cache = data.path() + QStringLiteral("/arcade/games.json");
        writeFile(cache, R"({"version": 1, "unknown": ["kof98"]})");
        QCOMPARE(engineWhileAsked(kof98, false), QStringLiteral("fbneo"));
        // One of another version that kept them apart too.
        writeFile(cache, R"({"version": 4, "unknownBefore": ["kof98"]})");
        QCOMPARE(engineWhileAsked(kof98, false), QStringLiteral("fbneo"));
        // Read the Games Again before the list was ever opened: games.json
        // read first.
        QCOMPARE(engineWhileAsked(kof98, true), QStringLiteral("fbneo"));
        // A set MAME has answered for since it last had none (a list it
        // didn't finish after that), asked about again: MAME's meanwhile.
        QJsonObject kept = QJsonDocument::fromJson(contents(cache)).object();
        QVERIFY(kept.value(QStringLiteral("machines")).toObject().contains(QStringLiteral("sf2")));
        kept.insert(QStringLiteral("unknownBefore"), QJsonArray({ QStringLiteral("sf2") }));
        writeFile(cache, QJsonDocument(kept).toJson());
        QCOMPARE(engineWhileAsked(QStringLiteral("sf2"), true), QStringLiteral("mame"));
        QFile::remove(games + QStringLiteral("/a_slowlist.zip"));
        refreshed(arcade);
        // Playable Online, with Online Play on.
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("On"));
        QVERIFY(setsOf(arcade, QStringLiteral("netplay")).contains(QStringLiteral("kof98")));
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("Off"));
        // Played in RetroArch with FinalBurn Neo.
        QFile::remove(m_log);
        const QVariantList end = played(arcade, QStringLiteral("kof98"));
        QCOMPARE(end.value(1).toString(), QStringLiteral("ok"));
        QVERIFY2(logLines(QStringLiteral("retroarch ")).value(0).endsWith(games + QStringLiteral("/kof98.zip")),
                 qPrintable(logLines().join(QLatin1Char('\n'))));
        QVERIFY(logLines(QStringLiteral("play kof98")).isEmpty());
        // A vertical one the list gives no shape: 3:4, as narrow on a tube.
        // Files missing, said with nothing of MAME, which hasn't the game.
        arcade.setScreen(true, QSize(720, 240));
        const QVariantList missing = played(arcade, QStringLiteral("1943"));
        QCOMPARE(missing.value(1).toString(), QStringLiteral("failed"));
        QCOMPARE(missing.value(2).toString(),
                 QStringLiteral("Missing bme01.12d: the set may be for another version of FinalBurn Neo\n"
                                "Or a BIOS set it needs isn't in the games folder"));
        QVERIFY(QString::fromUtf8(contents(configCopy)).contains(QStringLiteral("video_aspect_ratio = \"1.687500\"")));

        // The RetroAchievements filter, Online High Scores on it and a key
        // kept: its game, which RetroAchievements has achievements of.
        StandIn standIn;
        QVERIFY(serve(standIn));
        standIn.answers = {
            { "/API/API_GetGameList.php?i=27&h=1",
              "[{\"ID\": 1, \"NumAchievements\": 5, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("kof98") + "\"]}]" },
            { "/API/API_GetConsoleIDs.php?", R"([{"ID": 27, "Name": "Arcade"}])" },
        };
        arcade.setAchievementsServers(standIn.api(), standIn.media());
        arcade.onSettingChanged(module, QStringLiteral("online_high_scores"), QStringLiteral("RetroAchievements"));
        arcade.setAchievementsKey(QStringLiteral("key1"));
        QTRY_COMPARE_WITH_TIMEOUT(setsOf(arcade, QStringLiteral("achievements")), QStringList({ QStringLiteral("kof98") }), 5000);

        // Without the core: listed still, out of the RetroAchievements
        // filter, and its game says what is missing.
        QFile::remove(libretro::downloadedCore(data.path()));
        refreshed(arcade);
        QVERIFY(!arcade.fbneoReady());
        QVERIFY(setsOf(arcade, QStringLiteral("all")).contains(QStringLiteral("kof98")));
        QVERIFY(setsOf(arcade, QStringLiteral("achievements")).isEmpty());
        QCOMPARE(arcade.engineOf(QStringLiteral("kof98")), QStringLiteral("fbneo"));
        QVERIFY(!arcade.play(QStringLiteral("kof98")));
        QCOMPARE(arcade.lastError(), QStringLiteral("FinalBurn Neo is not installed: Settings → Arcade → FinalBurn Neo"));
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

    // OSD/OS Setup's account, which osdos-setup leaves for the app as the
    // card starts: taken once the event loop runs, the key kept and RetroArch
    // signing in with the password. RetroAchievements not reached at first
    // (the network not up yet), it is checked again a little later, the file
    // kept meanwhile; signed in, the file goes, and the password with it. A
    // password refused takes the file too, as does one RetroAchievements
    // took though RetroArch kept no token; a sign-in or a sign-out in
    // Settings takes its place, and its checks don't go on; checks that never
    // reach RetroAchievements stop after ten, the file left for the next
    // start; a file without a password brings its key alone, and one for the
    // account signed in already leaves its token as it is. Without a sign-in
    // core to check with, the file goes at once.
    void setupAccount() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/arcade")));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/retroarch"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString core = data.path() + QStringLiteral("/osdos_signin_libretro.so");
        writeFile(core);
        const QString online = data.path() + QStringLiteral("/online");
        qputenv("RA_CONFIG_COPY", (data.path() + QStringLiteral("/ra.cfg")).toUtf8());
        qputenv("RA_ONLINE", online.toUtf8());
        const auto restore = qScopeGuard([] {
            qunsetenv("RA_CONFIG_COPY");
            qunsetenv("RA_ONLINE");
        });
        const QString setup = data.path() + QStringLiteral("/arcade/retroachievements-setup.json");
        const QString kept = data.path() + QStringLiteral("/arcade/retroachievements.json");
        const auto account = [&kept] { return QJsonDocument::fromJson(contents(kept)).object(); };
        // The servers a stand-in's that never answers, the core a stand-in,
        // and the checks a twentieth of a second apart, so that a few of
        // those show none comes after; all before the event loop runs, as
        // the app has them.
        const auto prepare = [&](ArcadeBackend &arcade) {
            arcade.setAchievementsServers(QUrl(QStringLiteral("http://127.0.0.1:9/API/")),
                                          QUrl(QStringLiteral("http://127.0.0.1:9/")));
            arcade.setSignInCore(core);
            arcade.setSetupRetryInterval(50);
        };
        const auto checks = [this] { return logLines(QStringLiteral("retroarch ")).size(); };

        writeFile(setup, R"({"user": " player ", "password": "later", "key": "k3y"})");
        {
            QFile::remove(m_log);
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            QCOMPARE(arcade.achievementsState(), QStringLiteral("none"));
            QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("pending"), 10000);
            QVERIFY(arcade.achievementsKey());
            QVERIFY(QFileInfo::exists(setup));
            QCOMPARE(account().value(QStringLiteral("user")).toString(), QStringLiteral("player"));
            // Checked again while it waits, and signed in once the network is up.
            QTRY_VERIFY_WITH_TIMEOUT(checks() >= 2, 10000);
            writeFile(online);
            QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("signedIn"), 10000);
            QVERIFY(!QFileInfo::exists(setup));
            QCOMPARE(account().value(QStringLiteral("token")).toString(), QStringLiteral("tok456"));
            QCOMPARE(account().value(QStringLiteral("key")).toString(), QStringLiteral("k3y"));
            QVERIFY(!contents(kept).contains("later"));
            QVERIFY(!QFileInfo::exists(data.path() + QStringLiteral("/arcade/retroarch/sign-in")));
            // Nothing checked after.
            const int checked = checks();
            QTest::qWait(250);
            QCOMPARE(checks(), checked);
        }
        QFile::remove(online);

        // Another account's (the one signed in now keeps its token).
        writeFile(setup, R"({"user": "other", "password": "wrong"})");
        {
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("failed"), 10000);
            QVERIFY(!QFileInfo::exists(setup));
            QCOMPARE(account().value(QStringLiteral("key")).toString(), QStringLiteral("k3y"));
        }

        writeFile(setup, R"({"user": "player", "password": "offline"})");
        {
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("pending"), 10000);
            arcade.signInAchievements(QStringLiteral("player"), QStringLiteral("secret"));
            QVERIFY(!QFileInfo::exists(setup));
            QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("signedIn"), 10000);
            // The setup's check due meanwhile no longer is.
            QTest::qWait(250);
            QCOMPARE(arcade.achievementsState(), QStringLiteral("signedIn"));
            QCOMPARE(account().value(QStringLiteral("token")).toString(), QStringLiteral("tok789"));
        }

        writeFile(setup, R"({"user": "other", "password": "offline"})");
        {
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("pending"), 10000);
            arcade.signOutAchievements();
            QVERIFY(!QFileInfo::exists(setup));
            QTest::qWait(250);
            QCOMPARE(arcade.achievementsState(), QStringLiteral("none"));
        }

        writeFile(setup, R"({"user": "player", "password": "offline"})");
        {
            QFile::remove(m_log);
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            QTRY_COMPARE_WITH_TIMEOUT(checks(), 10, 20000);
            QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("pending"), 10000);
            QTest::qWait(250);
            QCOMPARE(checks(), 10);
            QVERIFY(QFileInfo::exists(setup));
        }

        writeFile(setup, R"({"key": " k4y "})");
        {
            QFile::remove(m_log);
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(setup), 5000);
            QCOMPARE(account().value(QStringLiteral("key")).toString(), QStringLiteral("k4y"));
            QTest::qWait(150);
            QCOMPARE(checks(), 0);
        }

        writeFile(setup, R"({"user": "player", "password": "offline"})");
        {
            QFile::remove(m_log);
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("pending"), 10000);
            arcade.signInAchievements(QStringLiteral("player"), QStringLiteral("offline"));
            QVERIFY(!QFileInfo::exists(setup));
            QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("pending"), 10000);
            const int checked = checks();
            QTest::qWait(250);
            QCOMPARE(checks(), checked);
        }

        writeFile(setup, R"({"user": "player", "password": "notoken"})");
        {
            QFile::remove(m_log);
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(setup), 10000);
            QCOMPARE(arcade.achievementsState(), QStringLiteral("pending"));
            QCOMPARE(arcade.achievementsMessage(),
                     QStringLiteral("RetroArch signed in but kept no token: RetroArch signs in at the next game"));
            QTest::qWait(250);
            QCOMPARE(checks(), 1);
        }

        writeFile(kept, R"({"user": "Player", "token": "tok999", "key": "k4y"})");
        writeFile(setup, R"({"user": "player", "password": "again"})");
        {
            QFile::remove(m_log);
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(setup), 5000);
            QCOMPARE(arcade.achievementsState(), QStringLiteral("signedIn"));
            QCOMPARE(account().value(QStringLiteral("token")).toString(), QStringLiteral("tok999"));
            QTest::qWait(150);
            QCOMPARE(checks(), 0);
        }

        QFile::remove(kept);
        writeFile(setup, R"({"user": "player", "password": "offline"})");
        {
            QFile::remove(m_log);
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            arcade.setSignInCore(data.path() + QStringLiteral("/none.so"));
            QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(setup), 5000);
            QCOMPARE(arcade.achievementsState(), QStringLiteral("pending"));
            QTest::qWait(150);
            QCOMPARE(checks(), 0);
        }
    }

    // OSD/OS Setup's account still waiting for RetroAchievements as a game
    // starts: the game's RetroArch has the password, no check of it runs
    // beside the game, and the file goes as the game ends. A game without
    // the password (Online High Scores off) has no check beside it either:
    // one due meanwhile waits for it to end, and runs once it has.
    void setupAccountBesideAGame() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        if (!libretro::corePath(data.path()).isEmpty() || !libretro::datPath(data.path()).isEmpty())
            QSKIP("FinalBurn Neo is installed here");
        writeFile(data.path() + QStringLiteral("/media/Games/Arcade/sf2.zip"));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/arcade")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        for (const char *program : { "/bin/mame", "/bin/retroarch" })
            QVERIFY(QFile::setPermissions(data.path() + QLatin1String(program),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString configCopy = data.path() + QStringLiteral("/ra.cfg");
        qputenv("RA_CONFIG_COPY", configCopy.toUtf8());
        // The game outlasts the checks' interval.
        qputenv("RA_SECONDS", "1");
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qunsetenv("RA_CONFIG_COPY");
            qunsetenv("RA_SECONDS");
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        });
        const QString core = data.path() + QStringLiteral("/osdos_signin_libretro.so");
        writeFile(core);
        writeFile(libretro::downloadedCore(data.path()));
        writeFile(libretro::downloadedDat(data.path()), fixture("fbneo-arcade.dat"));
        const auto prepare = [&core](ArcadeBackend &arcade) {
            arcade.setAchievementsServers(QUrl(QStringLiteral("http://127.0.0.1:9/API/")),
                                          QUrl(QStringLiteral("http://127.0.0.1:9/")));
            arcade.setSignInCore(core);
            arcade.setSetupRetryInterval(500);
            arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("online_high_scores"),
                                    QStringLiteral("RetroAchievements"));
        };
        // The games read once before, so that the account's checks have
        // hardly begun as the game starts.
        {
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            refreshed(arcade);
        }
        const QString setup = data.path() + QStringLiteral("/arcade/retroachievements-setup.json");
        writeFile(setup, R"({"user": "player", "password": "offline"})");
        const auto checks = [this, &core] { return logLines(QStringLiteral("retroarch -L ") + core).size(); };
        // The game's RetroArch config, written before the game starts.
        const QString gameConfig = data.path() + QStringLiteral("/arcade/retroarch/retroarch.cfg");

        {
            QFile::remove(m_log);
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            refreshed(arcade);
            QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("fbneo"));
            QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("pending"), 10000);
            QVERIFY(QFileInfo::exists(setup));
            QVERIFY2(checks() < 5, "the checks ran out before the game");

            QSignalSpy finished(&arcade, &ArcadeBackend::finished);
            QVERIFY(arcade.play(QStringLiteral("sf2")));
            const int checked = checks();
            QVERIFY(contents(gameConfig).contains("cheevos_password = \"offline\""));
            QVERIFY(finished.wait(10000));
            QCOMPARE(checks(), checked);
            QVERIFY(!QFileInfo::exists(setup));
            // Nor any after it: the game had the password.
            QTest::qWait(700);
            QCOMPARE(checks(), checked);
        }

        // The same game in FinalBurn Neo with Online High Scores off: its
        // RetroArch has no password, and the checks due while it plays wait.
        writeFile(setup, R"({"user": "player", "password": "offline"})");
        QFile::remove(m_log);
        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        prepare(arcade);
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("online_high_scores"),
                                QStringLiteral("Off"));
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("engines"),
                                QVariantMap{ { QStringLiteral("sf2"), QStringLiteral("fbneo") } });
        refreshed(arcade);
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("fbneo"));
        QTRY_COMPARE_WITH_TIMEOUT(arcade.achievementsState(), QStringLiteral("pending"), 10000);
        QVERIFY2(checks() < 5, "the checks ran out before the game");

        QSignalSpy finished(&arcade, &ArcadeBackend::finished);
        QVERIFY(arcade.play(QStringLiteral("sf2")));
        const int checked = checks();
        QVERIFY(!contents(gameConfig).contains("offline"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(checks(), checked);
        QVERIFY(QFileInfo::exists(setup));
        QTRY_VERIFY_WITH_TIMEOUT(checks() > checked, 5000);
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

        // Downloaded again, as an update is: the core, its list and its
        // licence replaced, and nothing else. The core's options and a game's, the
        // user's remap files, saves, states, a file of their own beside the
        // BIOS sets and the RetroAchievements account stay as they were.
        const QString home = data.path() + QStringLiteral("/arcade/retroarch");
        const QHash<QString, QByteArray> kept{
            { home + QStringLiteral("/config/FinalBurn Neo/FinalBurn Neo.opt"), "fbneo-diagnostic-input = \"Start + L + R\"\n" },
            { home + QStringLiteral("/config/FinalBurn Neo/sf2.cfg"), "video_scale_integer = \"true\"\n" },
            { home + QStringLiteral("/remaps/FinalBurn Neo/FinalBurn Neo.rmp"), "input_player1_btn_b = \"8\"\n" },
            { home + QStringLiteral("/remaps/FinalBurn Neo/sf2.rmp"), "input_player1_btn_a = \"0\"\n" },
            { home + QStringLiteral("/saves/fbneo/sf2.nv"), "nvram" },
            { home + QStringLiteral("/states/sf2.state1"), "state" },
            { home + QStringLiteral("/system/fbneo/hiscore.dat"), "sf2:\n" },
            { data.path() + QStringLiteral("/arcade/retroachievements.json"), "{\"user\": \"player\"}" },
        };
        for (auto file = kept.cbegin(); file != kept.cend(); ++file)
            writeFile(file.key(), file.value());
        installer.start(source("/core.zip", "/fbneo.dat"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(0).toBool(), true);
        for (auto file = kept.cbegin(); file != kept.cend(); ++file)
            QVERIFY2(contents(file.key()) == file.value(), qPrintable(file.key()));
        QVERIFY(noParts());

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

    // Settings → Arcade → FinalBurn Neo's download of RetroArch itself
    // (Windows) from a stand-in for libretro's build server: the version the
    // OSD/OS image has, its archive unpacked and RetroArch-Win64 put in
    // place, over one downloaded before; an archive the server hasn't, one
    // that isn't a 7z and one without retroarch.exe leave what was there,
    // and nothing of the download behind.
    void retroarchDownload() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QHash<QByteArray, QByteArray> files{ { "/stable/1.20.0/windows/x86_64/RetroArch.7z", fixture("retroarch.7z") },
                                             { "/other.7z", fixture("other.7z") },
                                             { "/license.txt", "You may freely use FB Neo.\n" } };
        QList<QByteArray> asked;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&server, &files, &asked] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &files, &asked] {
                const QByteArray request = socket->readAll();
                const QByteArray path = request.split(' ').value(1).split('?').first();
                asked.append(path);
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
        // libretro's source as on a 64-bit Windows, its archive on the
        // stand-in.
        const auto source = [&base, &data](const char *archive) {
            ProgramInstaller::Source s = ProgramInstaller::retroarch(data.path());
            s.available = true;
            s.archive = QUrl(base + QLatin1String(archive));
            return s;
        };
        const QString program = libretro::downloadedRetroArch(data.path());
        const QString home = QFileInfo(program).absolutePath();
        // What a download leaves behind: its archive and its folder, and the
        // RetroArch set aside for it.
        const auto leftovers = [&data] {
            QStringList found;
            QDirIterator parts(data.path(), { QStringLiteral("*.part"), QStringLiteral("*.old") },
                               QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (parts.hasNext())
                found.append(parts.next());
            return found;
        };

        ProgramInstaller installer;
        QSignalSpy finished(&installer, &ProgramInstaller::finished);
        QSignalSpy progressed(&installer, &ProgramInstaller::progressChanged);
        installer.start(source("/stable/1.20.0/windows/x86_64/RetroArch.7z"));
        QVERIFY(installer.running());
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().at(1).toString(), QString());
        QCOMPARE(finished.first().at(0).toBool(), true);
        QVERIFY(!installer.running());
        QCOMPARE(installer.progress(), 100);
        QVERIFY(progressed.count() >= 3);
        QVERIFY(contents(program).startsWith("MZ a stand-in for RetroArch for Windows"));
        QCOMPARE(contents(home + QStringLiteral("/retroarch.cfg")), QByteArray("# RetroArch's own settings, as it comes\n"));
        QCOMPARE(contents(home + QStringLiteral("/assets/readme.txt")), QByteArray("the assets' readme\n"));
        QVERIFY(QFileInfo::exists(home + QStringLiteral("/assets/empty.txt")));
        QCOMPARE(leftovers(), QStringList());
        // The archive alone is asked for: no list of versions to read first.
        QCOMPARE(asked, QList<QByteArray>({ "/stable/1.20.0/windows/x86_64/RetroArch.7z" }));

        // Again, over the one before: the new program is in place, and a
        // file of the user's own in the folder goes with the old one.
        writeFile(home + QStringLiteral("/mine.txt"), "mine");
        writeFile(program, "the one before");
        installer.start(source("/stable/1.20.0/windows/x86_64/RetroArch.7z"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(1).toString(), QString());
        QVERIFY(contents(program).startsWith("MZ a stand-in for RetroArch for Windows"));
        QVERIFY(!QFileInfo::exists(home + QStringLiteral("/mine.txt")));
        QCOMPARE(leftovers(), QStringList());

        // An archive the server hasn't, one that isn't a 7z, one without
        // retroarch.exe: why, the one before stays, nothing is left behind.
        writeFile(program, "the one before");
        installer.start(source("/stable/1.20.0/linux/x86_64/RetroArch.7z"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(0).toBool(), false);
        QCOMPARE(finished.last().at(1).toString(), QStringLiteral("127.0.0.1 answered 404"));
        QCOMPARE(contents(program), QByteArray("the one before"));
        QCOMPARE(leftovers(), QStringList());
        installer.start(source("/license.txt"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(1).toString(), QStringLiteral("The file is not a 7z archive"));
        installer.start(source("/other.7z"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(1).toString(), QStringLiteral("The download has no retroarch.exe"));
        QCOMPARE(contents(program), QByteArray("the one before"));
        QCOMPARE(leftovers(), QStringList());
        // A machine libretro builds none for: this one, as the source stands.
        installer.start(ProgramInstaller::retroarch(data.path()));
        QVERIFY2(finished.last().at(1).toString().startsWith(QStringLiteral("There is no RetroArch build for this machine")),
                 qPrintable(finished.last().at(1).toString()));

        // The source as it stands: the RetroArch the OSD/OS image has,
        // Debian trixie's 1.20.0, from the build server's stable builds, so
        // that a PC and a Pi play together on the same RetroArch.
        const ProgramInstaller::Source pinned = ProgramInstaller::retroarch(data.path());
        QCOMPARE(pinned.name, QStringLiteral("RetroArch"));
        QCOMPARE(pinned.version, QStringLiteral("1.20.0"));
        QCOMPARE(pinned.archive, QUrl(QStringLiteral("https://buildbot.libretro.com/stable/1.20.0/windows/x86_64/RetroArch.7z")));
        QCOMPARE(pinned.program, QStringLiteral("retroarch.exe"));
        QCOMPARE(pinned.target, home);
    }

    // Settings → Arcade → MAME's download (Windows) from a stand-in for
    // GitHub: the release of the version the OSD/OS image has, its 64-bit
    // Windows build a self-extracting archive with mame.exe at its top,
    // unpacked and put in place as <data>/arcade/mame with its plugins
    // beside it, over one downloaded before; an archive the server hasn't,
    // and one that isn't an archive, leaving what was there.
    void mameDownload() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QHash<QByteArray, QByteArray> files{ { "/mamedev/mame/releases/download/mame0276/mame0276b_64bit.exe", fixture("mame-sfx.bin") },
                                             { "/license.txt", "You may freely use FB Neo.\n" } };
        QList<QByteArray> asked;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&server, &files, &asked] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &files, &asked] {
                const QByteArray request = socket->readAll();
                const QByteArray path = request.split(' ').value(1).split('?').first();
                asked.append(path);
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
        // mamedev's source as on a 64-bit Windows, its archive on the
        // stand-in.
        const auto source = [&base, &data](const char *archive) {
            ProgramInstaller::Source s = ProgramInstaller::mame(data.path());
            s.available = true;
            s.archive = QUrl(base + QLatin1String(archive));
            return s;
        };
        const QString program = mame::downloadedMame(data.path());
        const QString home = QFileInfo(program).absolutePath();
        const auto leftovers = [&data] {
            QStringList found;
            QDirIterator parts(data.path(), { QStringLiteral("*.part"), QStringLiteral("*.old") },
                               QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (parts.hasNext())
                found.append(parts.next());
            return found;
        };

        ProgramInstaller installer;
        QSignalSpy finished(&installer, &ProgramInstaller::finished);
        installer.start(source("/mamedev/mame/releases/download/mame0276/mame0276b_64bit.exe"));
        QVERIFY(installer.running());
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().at(1).toString(), QString());
        QCOMPARE(finished.first().at(0).toBool(), true);
        QCOMPARE(installer.progress(), 100);
        QVERIFY(contents(program).startsWith("MZ a stand-in for MAME for Windows"));
        QCOMPARE(contents(home + QStringLiteral("/plugins/hiscore/init.lua")), QByteArray("-- the hiscore plugin\n"));
        QVERIFY(QFileInfo::exists(home + QStringLiteral("/bgfx/shaders/readme.txt")));
        QCOMPARE(mame::pluginsFolder(program), QDir::cleanPath(home + QStringLiteral("/plugins")));
        QCOMPARE(leftovers(), QStringList());
        // The archive alone is asked for: no release to read first.
        QCOMPARE(asked, QList<QByteArray>({ "/mamedev/mame/releases/download/mame0276/mame0276b_64bit.exe" }));

        // Again, over the one before: a file of the user's own in its
        // folder goes with it.
        writeFile(home + QStringLiteral("/mine.txt"), "mine");
        writeFile(program, "the one before");
        installer.start(source("/mamedev/mame/releases/download/mame0276/mame0276b_64bit.exe"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(1).toString(), QString());
        QVERIFY(contents(program).startsWith("MZ a stand-in for MAME for Windows"));
        QVERIFY(!QFileInfo::exists(home + QStringLiteral("/mine.txt")));
        QCOMPARE(leftovers(), QStringList());

        // An archive the server hasn't, and one that isn't an archive: why,
        // the one before stays, nothing is left behind.
        writeFile(program, "the one before");
        installer.start(source("/mamedev/mame/releases/download/mame0276/mame0276b_arm64.exe"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(0).toBool(), false);
        QCOMPARE(finished.last().at(1).toString(), QStringLiteral("127.0.0.1 answered 404"));
        QCOMPARE(contents(program), QByteArray("the one before"));
        QCOMPARE(leftovers(), QStringList());
        installer.start(source("/license.txt"));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(0).toBool(), false);
        QCOMPARE(finished.last().at(1).toString(), QStringLiteral("The file is not a 7z archive"));
        QCOMPARE(contents(program), QByteArray("the one before"));
        QCOMPARE(leftovers(), QStringList());
        // A machine mamedev builds none for: this one, as the source stands.
        installer.start(ProgramInstaller::mame(data.path()));
        QVERIFY(finished.last().at(1).toString().startsWith(QStringLiteral("There is no MAME build for this machine")));

        // The source as it stands: the MAME the OSD/OS image has, Debian
        // trixie's 0.276, from mamedev's releases, so that a PC and a Pi
        // play the same sets.
        const ProgramInstaller::Source pinned = ProgramInstaller::mame(data.path());
        QCOMPARE(pinned.name, QStringLiteral("MAME"));
        QCOMPARE(pinned.version, QStringLiteral("0.276"));
        QCOMPARE(pinned.archive, QUrl(QStringLiteral("https://github.com/mamedev/mame/releases/download/mame0276/mame0276b_64bit.exe")));
        QCOMPARE(pinned.program, QStringLiteral("mame.exe"));
        QCOMPARE(pinned.target, home);
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
        // galaga's achievements and no board; mslug's (its hash in capitals)
        // no achievements and a board of points, with no one on it yet;
        // pacman's neither: a game of RetroAchievements', for its pictures,
        // with nothing to earn in it.
        const QByteArray pacman = libretro::achievementsHash(QStringLiteral("pacman.zip")).toLatin1();
        const QHash<QByteArray, QByteArray> answers{
            { "/API/API_GetGameList.php?i=27&h=1",
              "[{\"ID\": 1, \"NumAchievements\": 20, \"NumLeaderboards\": 2, \"Hashes\": [\"" + sf2 + "\"]},"
              " {\"ID\": 2, \"NumAchievements\": 12, \"NumLeaderboards\": 0, \"Hashes\": [\"" + galaga + "\"]},"
              " {\"ID\": 3, \"NumAchievements\": 0, \"NumLeaderboards\": 1, \"Hashes\": [\"" + mslug.toUpper() + "\"]},"
              " {\"ID\": 4, \"NumAchievements\": 0, \"NumLeaderboards\": 0, \"Hashes\": [\"" + pacman + "\"]}]" },
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
        QCOMPARE(asked, QStringList({ QStringLiteral("/API/API_GetGameList.php?i=27&h=1 y=key1"),
                                      QStringLiteral("/API/API_GetGameLeaderboards.php?i=1 y=key1"),
                                      QStringLiteral("/API/API_GetLeaderboardEntries.php?i=12&c=3 y=key1") }));
        QVERIFY(contents(data.path() + QStringLiteral("/retroachievements-games.json")).contains(sf2));
        QVERIFY(contents(data.path() + QStringLiteral("/retroachievements-games.json")).contains(pacman));
        // The games with something to earn: those with achievements, or a
        // leaderboard alone; not one with neither, nor one it hasn't. Every
        // game it has known, one with neither too.
        QVERIFY(boards.hasGame(QStringLiteral("sf2.zip")));
        QVERIFY(boards.hasGame(QStringLiteral("galaga.zip")));
        QVERIFY(boards.hasGame(QStringLiteral("mslug.zip")));
        QVERIFY(!boards.hasGame(QStringLiteral("pacman.zip")));
        QVERIFY(!boards.hasGame(QStringLiteral("dkong.zip")));
        QVERIFY(boards.knowsGame(QStringLiteral("pacman.zip")));
        QVERIFY(boards.knowsGame(QStringLiteral("mslug.zip")));
        QVERIFY(!boards.knowsGame(QStringLiteral("dkong.zip")));
        // Wanted again while it is fresh: nothing read.
        asked.clear();
        boards.wantList();
        QTest::qWait(600);
        QVERIFY(asked.isEmpty());

        // A game whose leaderboards are none, and one with no one on its
        // board yet: the list isn't read again.
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

        // The list kept read from its file, wanted for itself: its games
        // known at once, nothing read. Of two games asked for at once, only
        // the last is read.
        asked.clear();
        RetroAchievements again(data.path(), api, media);
        QSignalSpy readyAgain(&again, &RetroAchievements::boardReady);
        QSignalSpy listAgain(&again, &RetroAchievements::listRead);
        again.setKey(QStringLiteral("key1"));
        again.wantList();
        QVERIFY(again.hasGame(QStringLiteral("mslug.zip")));
        QVERIFY(!again.hasGame(QStringLiteral("pacman.zip")));
        QVERIFY(again.knowsGame(QStringLiteral("pacman.zip")));
        QTest::qWait(600);
        QVERIFY(asked.isEmpty());
        QVERIFY(listAgain.isEmpty());
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
        QCOMPARE(asked.first(), QStringLiteral("/API/API_GetGameList.php?i=27&h=1 y=key1"));
        QVERIFY(contents(older.path() + QStringLiteral("/retroachievements-games.json")).contains("\"boards\""));

        // A list kept as 0.0.13 kept it, of the games with achievements
        // alone: its games known meanwhile, and, wanted, the list read again
        // with those that have a leaderboard alone.
        QTemporaryDir oldList;
        writeFile(oldList.path() + QStringLiteral("/retroachievements-games.json"),
                  "{\"read\": \"" + QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toLatin1()
                  + "\", \"games\": {\"" + sf2 + "\": 1}, \"boards\": [1]}");
        asked.clear();
        RetroAchievements keptAgain(oldList.path(), api, media);
        QSignalSpy listRead(&keptAgain, &RetroAchievements::listRead);
        keptAgain.setKey(QStringLiteral("key1"));
        keptAgain.wantList();
        QVERIFY(keptAgain.hasGame(QStringLiteral("sf2.zip")));
        QVERIFY(!keptAgain.hasGame(QStringLiteral("mslug.zip")));
        QVERIFY(listRead.wait(5000));
        QCOMPARE(asked, QStringList({ QStringLiteral("/API/API_GetGameList.php?i=27&h=1 y=key1") }));
        QVERIFY(keptAgain.hasGame(QStringLiteral("mslug.zip")));
        QVERIFY(contents(oldList.path() + QStringLiteral("/retroachievements-games.json")).contains("\"achievements\""));

        // A list kept without the games of neither achievements nor
        // leaderboards: its games as it says meanwhile, mslug's of a
        // leaderboard alone, and, wanted, the list read again with the rest.
        QTemporaryDir withoutNeither;
        writeFile(withoutNeither.path() + QStringLiteral("/retroachievements-games.json"),
                  "{\"read\": \"" + QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toLatin1()
                  + "\", \"games\": {\"" + sf2 + "\": 1, \"" + mslug + "\": 3}, \"achievements\": [1], \"boards\": [1, 3]}");
        asked.clear();
        RetroAchievements keptThird(withoutNeither.path(), api, media);
        QSignalSpy thirdRead(&keptThird, &RetroAchievements::listRead);
        keptThird.setKey(QStringLiteral("key1"));
        keptThird.wantList();
        QVERIFY(keptThird.listPending());
        QVERIFY(keptThird.hasGame(QStringLiteral("mslug.zip")));
        QCOMPARE(keptThird.achievements(QStringLiteral("mslug.zip")).value(QStringLiteral("state")).toString(),
                 QStringLiteral("loading"));
        QVERIFY(!keptThird.knowsGame(QStringLiteral("pacman.zip")));
        QVERIFY(thirdRead.wait(5000));
        QCOMPARE(asked.value(0), QStringLiteral("/API/API_GetGameList.php?i=27&h=1 y=key1"));
        QVERIFY(keptThird.knowsGame(QStringLiteral("pacman.zip")));
        QVERIFY(!keptThird.listPending());
        QVERIFY(contents(withoutNeither.path() + QStringLiteral("/retroachievements-games.json")).contains("\"version\":2"));

        // A list kept a week that can't be read again: the one kept used
        // meanwhile, and not wanted again until it is to be tried again, so
        // that what waits for it (Read the Games Again's pictures) goes on.
        QTemporaryDir stale;
        writeFile(stale.path() + QStringLiteral("/retroachievements-games.json"),
                  "{\"version\": 2, \"read\": \""
                  + QDateTime::currentDateTimeUtc().addDays(-8).toString(Qt::ISODate).toLatin1()
                  + "\", \"games\": {\"" + sf2 + "\": 1}, \"achievements\": [1], \"boards\": [1]}");
        asked.clear();
        RetroAchievements keptStale(stale.path(), QUrl(QStringLiteral("http://127.0.0.1:%1/gone/").arg(server.serverPort())),
                                    media);
        keptStale.setKey(QStringLiteral("key1"));
        keptStale.wantList();
        QVERIFY(keptStale.listPending());
        QVERIFY(keptStale.hasGame(QStringLiteral("sf2.zip")));
        QTRY_VERIFY_WITH_TIMEOUT(!keptStale.listPending(), 5000);
        QCOMPARE(asked, QStringList({ QStringLiteral("/gone/API_GetGameList.php?i=27&h=1 y=key1") }));
        keptStale.wantList();
        QVERIFY(!keptStale.listPending());
        QTest::qWait(600);
        QCOMPARE(asked.size(), 1);
        QVERIFY(keptStale.hasGame(QStringLiteral("sf2.zip")));

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
        // Puck Man's game (Pac-Man's too) has a leaderboard and no
        // achievements.
        standIn.answers = {
            { "/API/API_GetGameList.php?i=27&h=1",
              "[{\"ID\": 1, \"NumAchievements\": 3, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("sf2") + "\"]},"
              " {\"ID\": 4, \"NumAchievements\": 1, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("sf2ce") + "\"]},"
              " {\"ID\": 2, \"NumAchievements\": 0, \"NumLeaderboards\": 1, \"Hashes\": [\"" + hashOf("puckman") + "\", \"" + hashOf("pacman") + "\"]}]" },
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
        QCOMPARE(standIn.asked, QStringList({ QStringLiteral("/API/API_GetGameList.php?i=27&h=1"),
                                              QStringLiteral("/API/API_GetGameInfoAndUserProgress.php?g=1&u=Ryu") }));

        // A game with none, one with a leaderboard alone and one
        // RetroAchievements hasn't: the last two none at once, nothing asked.
        ra.achievements(QStringLiteral("sf2ce.zip"));
        QVERIFY(ready.wait(5000));
        QCOMPARE(state(ra.achievements(QStringLiteral("sf2ce.zip"))), QStringLiteral("none"));
        standIn.asked.clear();
        QCOMPARE(state(ra.achievements(QStringLiteral("pacman.zip"))), QStringLiteral("none"));
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

        // A list kept as 0.0.13 kept it, of the games with achievements,
        // that can't be read again: its games' achievements read meanwhile.
        QTemporaryDir oldList;
        writeFile(oldList.path() + QStringLiteral("/retroachievements-games.json"),
                  "{\"read\": \"" + QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toLatin1()
                  + "\", \"games\": {\"" + hashOf("sf2") + "\": 1}, \"boards\": []}");
        StandIn noList;
        QVERIFY(serve(noList));
        noList.answers = { { "/API/API_GetGameInfoAndUserProgress.php?g=1&u=Ryu", progress } };
        RetroAchievements kept(oldList.path(), noList.api(), noList.media());
        QSignalSpy readyKept(&kept, &RetroAchievements::achievementsReady);
        kept.setKey(QStringLiteral("key1"));
        kept.setUser(QStringLiteral("Ryu"));
        QCOMPARE(state(kept.achievements(QStringLiteral("sf2.zip"))), QStringLiteral("loading"));
        QVERIFY(readyKept.wait(5000));
        read = kept.achievements(QStringLiteral("sf2.zip"));
        QCOMPARE(state(read), QStringLiteral("ready"));
        QCOMPARE(read.value(QStringLiteral("achievements")).toList().size(), 3);
        QCOMPARE(noList.asked, QStringList({ QStringLiteral("/API/API_GetGameList.php?i=27&h=1"),
                                             QStringLiteral("/API/API_GetGameInfoAndUserProgress.php?g=1&u=Ryu") }));

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
        // Looked for before: said so at once, nothing asked.
        standIn.asked.clear();
        ra.findPictures({ QStringLiteral("pacman"), QStringLiteral("puckman") }, { QStringLiteral("puckman") });
        QVERIFY(found.wait(5000));
        QCOMPARE(found.last(), (QVariantList{ QStringLiteral("puckman"), QStringLiteral("looked"), QStringList() }));
        QVERIFY(standIn.asked.isEmpty());
        ra.findPictures({ QStringLiteral("galaga") }, {});
        QVERIFY(found.wait(5000));
        QCOMPARE(found.last(), (QVariantList{ QStringLiteral("galaga"), QStringLiteral("unknown"), QStringList() }));
        // A game's asked for by itself while one shown waits its moment: the
        // one shown first, neither taking the other's place.
        found.clear();
        ra.findPictures({ QStringLiteral("galaga") }, {});
        ra.findPicturesLater({ QStringLiteral("sf2") }, {});
        QTRY_COMPARE_WITH_TIMEOUT(found.size(), 2, 5000);
        QCOMPARE(found.at(0), (QVariantList{ QStringLiteral("galaga"), QStringLiteral("unknown"), QStringList() }));
        QCOMPARE(found.at(1), (QVariantList{ QStringLiteral("sf2"), QStringLiteral("found"),
                                             QStringList({ QStringLiteral("/Images/000101.png"),
                                                           QStringLiteral("/Images/000102.jpg") }) }));

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
            { "/API/API_GetGameList.php?i=27&h=1",
              "[{\"ID\": 1, \"NumAchievements\": 2, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("sf2") + "\"]},"
              " {\"ID\": 2, \"NumAchievements\": 4, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("puckman") + "\", \"" + hashOf("pacman") + "\"]},"
              " {\"ID\": 3, \"NumAchievements\": 0, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("kinst") + "\"]}]" },
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
        // RetroAchievements' pictures alone: none of progetto-SNAPS' here.
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("pictures_source"),
                                QStringLiteral("RetroAchievements"));
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
        // game not complete. Its game has neither achievements nor
        // leaderboards, and its pictures all the same.
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

    // Read the Games Again with a Web API key: once the games have been read
    // and MAME has answered, RetroAchievements' pictures of every game it has,
    // a game at a time, by name, as they are downloaded as a game is shown
    // (a clone's under its parent's name); none for a game looked for before
    // (its record) or one RetroAchievements hasn't. A game played meanwhile
    // holds the next back until it ends. Read again, nothing is asked but for
    // a game new in the folder, past a clone whose parent's were looked for;
    // a key changed while a game is read reads it again with the new one.
    // Nothing without a key.
    void retroAchievementsPicturesOnRescan() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        const QString snap = games + QStringLiteral("/snap");
        for (const char *set : { "sf2.zip", "mslug.zip", "galaga.zip", "pacman.zip", "kinst.zip" })
            writeFile(games + QLatin1Char('/') + QLatin1String(set));
        // Galaga's looked for before.
        writeFile(snap + QStringLiteral("/.retroachievements.json"),
                  R"({"galaga": {"saved": ["/Images/000301.png"], "complete": true}})");
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/mame"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
            qunsetenv("MAME_SECONDS");
        });
        // Kinst isn't RetroAchievements'; X-Men and Ace come into the folder
        // later. Metal Slug's game, with neither achievements nor
        // leaderboards, is answered for slowly, and Ace's.
        StandIn standIn;
        QVERIFY(serve(standIn));
        standIn.keys << QStringLiteral("key2");
        standIn.answers = {
            { "/API/API_GetGameList.php?i=27&h=1",
              "[{\"ID\": 1, \"NumAchievements\": 2, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("sf2") + "\"]},"
              " {\"ID\": 2, \"NumAchievements\": 0, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("mslug") + "\"]},"
              " {\"ID\": 3, \"NumAchievements\": 4, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("galaga") + "\"]},"
              " {\"ID\": 4, \"NumAchievements\": 5, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("puckman") + "\", \"" + hashOf("pacman") + "\"]},"
              " {\"ID\": 5, \"NumAchievements\": 6, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("xmen6p") + "\"]},"
              " {\"ID\": 6, \"NumAchievements\": 7, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("acefruit") + "\"]}]" },
            { "/API/API_GetGame.php?i=1", R"({"ImageTitle": "/Images/000101.png", "ImageBoxArt": "/Images/000102.jpg"})" },
            { "/API/API_GetGame.php?i=2", R"({"ImageTitle": "/Images/000201.png"})" },
            { "/API/API_GetGame.php?i=3", R"({"ImageTitle": "/Images/000301.png"})" },
            { "/API/API_GetGame.php?i=4", R"({"ImageTitle": "/Images/000401.png"})" },
            { "/API/API_GetGame.php?i=5", R"({"ImageTitle": "/Images/000501.png"})" },
            { "/API/API_GetGame.php?i=6", R"({"ImageTitle": "/Images/000601.png"})" },
            { "/media/Images/000101.png", png("sf2 title") },
            { "/media/Images/000102.jpg", jpeg("sf2 box") },
            { "/media/Images/000201.png", png("mslug title") },
            { "/media/Images/000301.png", png("galaga title") },
            { "/media/Images/000401.png", png("pacman title") },
            { "/media/Images/000501.png", png("xmen title") },
            { "/media/Images/000601.png", png("ace title") },
            { "/API/API_GetConsoleIDs.php?", R"([{"ID": 27, "Name": "Arcade"}])" },
        };
        standIn.delays = { { "/API/API_GetGame.php?i=2", 800 }, { "/API/API_GetGame.php?i=6", 800 } };
        const auto gamesAsked = [&standIn] {
            QStringList asked;
            for (const QString &path : std::as_const(standIn.asked)) {
                if (path.startsWith(QLatin1String("/API/API_GetGame.php")))
                    asked << path;
            }
            return asked;
        };
        const auto saved = [snap] {
            return QDir(snap).entryList({ QStringLiteral("*_*") }, QDir::Files, QDir::Name);
        };

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setAchievementsServers(standIn.api(), standIn.media());
        arcade.setPictureSweepInterval(50);
        // RetroAchievements' pictures alone: none of progetto-SNAPS' here.
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("pictures_source"),
                                QStringLiteral("RetroAchievements"));
        refreshed(arcade);
        // Read again without a key: nothing asked.
        arcade.rescan();
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
        QTest::qWait(300);
        QVERIFY(standIn.asked.isEmpty());
        QVERIFY(!arcade.sweepingPictures());
        // A key: nothing read but the key until the games are read again.
        arcade.setAchievementsKey(QStringLiteral("key1"));
        QTRY_VERIFY_WITH_TIMEOUT(arcade.achievementsKeyWorks(), 5000);
        QTest::qWait(300);
        QCOMPARE(standIn.asked, QStringList({ QStringLiteral("/API/API_GetConsoleIDs.php?") }));
        standIn.asked.clear();

        // Read again: Metal Slug's asked for first, and a game started as it
        // is; the next not asked for until the game has ended.
        QStringList askedWhilePlaying;
        bool started = false;
        standIn.onAsk = [&arcade, &askedWhilePlaying, &started](const QString &path) {
            if (path == QLatin1String("/API/API_GetGame.php?i=2") && !std::exchange(started, true)) {
                QVERIFY(arcade.play(QStringLiteral("galaga")));
                return;
            }
            if (arcade.running() && path.startsWith(QLatin1String("/API/")))
                askedWhilePlaying << path;
        };
        qputenv("MAME_SECONDS", "1.5");
        QSignalSpy finished(&arcade, &ArcadeBackend::finished);
        arcade.rescan();
        QTRY_VERIFY_WITH_TIMEOUT(started, 10000);
        QVERIFY(finished.wait(10000));
        QTRY_COMPARE_WITH_TIMEOUT(saved(), QStringList({ QStringLiteral("mslug_1.png"), QStringLiteral("puckman_1.png"),
                                                         QStringLiteral("sf2_1.png"), QStringLiteral("sf2_2.jpg") }),
                                  10000);
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.sweepingPictures(), 5000);
        standIn.onAsk = nullptr;
        QVERIFY2(askedWhilePlaying.isEmpty(), qPrintable(askedWhilePlaying.join(QLatin1Char(' '))));
        QCOMPARE(gamesAsked(), QStringList({ QStringLiteral("/API/API_GetGame.php?i=2"),
                                             QStringLiteral("/API/API_GetGame.php?i=4"),
                                             QStringLiteral("/API/API_GetGame.php?i=1") }));
        QCOMPARE(standIn.asked.count(QStringLiteral("/API/API_GetGameList.php?i=27&h=1")), 1);
        QCOMPARE(contents(snap + QStringLiteral("/sf2_2.jpg")), jpeg("sf2 box"));
        QCOMPARE(arcade.gameDetails(QStringLiteral("pacman")).value(QStringLiteral("pictures")).toStringList(),
                 QStringList({ QUrl::fromLocalFile(snap + QStringLiteral("/puckman_1.png")).toString() }));

        // X-Men in the folder, after Pac-Man, whose parent's pictures are in:
        // read again, X-Men's alone asked for, and the look ends.
        writeFile(games + QStringLiteral("/xmen6p.7z"));
        standIn.asked.clear();
        arcade.rescan();
        QTRY_VERIFY_WITH_TIMEOUT(saved().contains(QStringLiteral("xmen6p_1.png")), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.sweepingPictures(), 5000);
        QCOMPARE(gamesAsked(), QStringList({ QStringLiteral("/API/API_GetGame.php?i=5") }));

        // Ace in the folder, its game slow to answer: the key changed as it
        // is read, read again with the new one.
        writeFile(games + QStringLiteral("/acefruit.zip"));
        standIn.asked.clear();
        arcade.rescan();
        QTRY_VERIFY_WITH_TIMEOUT(gamesAsked().contains(QStringLiteral("/API/API_GetGame.php?i=6")), 10000);
        arcade.setAchievementsKey(QStringLiteral("key2"));
        QTRY_VERIFY_WITH_TIMEOUT(saved().contains(QStringLiteral("acefruit_1.png")), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.sweepingPictures(), 5000);
        QCOMPARE(gamesAsked(), QStringList({ QStringLiteral("/API/API_GetGame.php?i=6"),
                                             QStringLiteral("/API/API_GetGame.php?i=6") }));

        // Read again: every game looked for, nothing asked.
        standIn.asked.clear();
        arcade.rescan();
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.sweepingPictures(), 5000);
        QVERIFY(gamesAsked().isEmpty());
    }

    // progetto-SNAPS' pictures through ArcadeDB, for the games still without
    // any once RetroAchievements has been asked: Download Pictures goes
    // through the folder, RetroAchievements' pass first, then asks ArcadeDB
    // about the games with no picture in one request, saves each game's
    // screenshot as snap/<set>.png and its title screen as titles/<set>.png,
    // never over a file there, and notes in .arcadedb.json what ArcadeDB had
    // of each set, so that none is asked about again; a game shown is asked
    // about by itself, after RetroAchievements; Pictures From can keep
    // either source out; the look's marker goes once every picture is had.
    void picturesFromArcadeDb() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        const QString snap = games + QStringLiteral("/snap");
        const QString titles = games + QStringLiteral("/titles");
        // Street Fighter II's bootleg is a clone of it: its parent's pictures
        // are its own.
        for (const char *set : { "sf2.zip", "sf2b.zip", "galaga.zip", "kinst.zip", "pacman.zip" })
            writeFile(games + QLatin1Char('/') + QLatin1String(set));
        // Pac-Man's own snap is there already: ArcadeDB isn't asked about it.
        writeFile(snap + QStringLiteral("/pacman.png"), png("pacman mine"));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/mame"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        });
        // RetroAchievements has Street Fighter II alone; ArcadeDB has Galaga's
        // two pictures and Metal Slug's, and nothing of Killer Instinct.
        StandIn standIn;
        QVERIFY(serve(standIn));
        const QString base = standIn.service().toString().section(QLatin1Char('/'), 0, 2);
        const auto adbGame = [&base](const char *set) {
            return QStringLiteral("{\"game_name\": \"%1\", \"title\": \"%1\", \"url_image_ingame\": \"%2/media/adb/%1_ingame.png\","
                                  " \"url_image_title\": \"%2/media/adb/%1_title.png\"}")
                .arg(QLatin1String(set), base);
        };
        standIn.answers = {
            { "/API/API_GetGameList.php?i=27&h=1",
              "[{\"ID\": 1, \"NumAchievements\": 2, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("sf2") + "\"]}]" },
            { "/API/API_GetGame.php?i=1", R"({"ImageTitle": "/Images/000101.png"})" },
            { "/media/Images/000101.png", png("sf2 title") },
            { "/API/API_GetConsoleIDs.php?", R"([{"ID": 27, "Name": "Arcade"}])" },
            // Answered for its path alone, whatever it asks about.
            { "/service_scraper.php", ("{\"release\": \"0.289\", \"result\": [" + adbGame("galaga") + ", " + adbGame("mslug")
                                       + ", {\"game_name\": \"othergame\", \"url_image_ingame\": \"https://elsewhere.example/x.png\"}]}")
                                          .toUtf8() },
            { "/media/adb/galaga_ingame.png", png("galaga in play") },
            { "/media/adb/galaga_title.png", png("galaga title") },
            { "/media/adb/mslug_ingame.png", png("mslug in play") },
            { "/media/adb/mslug_title.png", png("mslug title") },
        };
        const auto asked = [&standIn](const char *prefix) {
            QStringList found;
            for (const QString &path : std::as_const(standIn.asked)) {
                if (path.startsWith(QLatin1String(prefix)))
                    found << path;
            }
            return found;
        };
        const auto files = [](const QString &folder) {
            return QDir(folder).entryList({ QStringLiteral("*.png") }, QDir::Files, QDir::Name);
        };
        const auto record = [snap] {
            QFile file(snap + QStringLiteral("/.arcadedb.json"));
            return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
        };
        const auto urls = [](const QStringList &paths) {
            QStringList list;
            for (const QString &path : paths)
                list << QUrl::fromLocalFile(path).toString();
            return list;
        };

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setAchievementsServers(standIn.api(), standIn.media());
        arcade.setPictureServer(standIn.service());
        arcade.setPictureSweepInterval(50);
        arcade.setAchievementsKey(QStringLiteral("key1"));
        refreshed(arcade);
        QTRY_VERIFY_WITH_TIMEOUT(arcade.achievementsKeyWorks(), 5000);
        standIn.asked.clear();

        // Download Pictures: RetroAchievements' pass saves Street Fighter II's,
        // and passes its bootleg, whose game is its parent's, looked for by
        // then, with no second spent asking after it; ArcadeDB is asked
        // about Galaga and Killer Instinct in one request (not Street
        // Fighter II or its bootleg, which have their picture by then, nor
        // Pac-Man, which has its own), and Galaga's two pictures are saved
        // where the packs put them. The marker stands while it goes on.
        arcade.downloadPictures();
        QTRY_VERIFY_WITH_TIMEOUT(arcade.sweepingPictures(), 5000);
        QVERIFY(QFile::exists(data.path() + QStringLiteral("/arcade/pictures-wanted")));
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.sweepingPictures(), 15000);
        QCOMPARE(files(snap), QStringList({ QStringLiteral("galaga.png"), QStringLiteral("pacman.png"), QStringLiteral("sf2_1.png") }));
        QCOMPARE(files(titles), QStringList({ QStringLiteral("galaga.png") }));
        QCOMPARE(contents(snap + QStringLiteral("/galaga.png")), png("galaga in play"));
        QCOMPARE(contents(titles + QStringLiteral("/galaga.png")), png("galaga title"));
        const QStringList queries = asked("/service_scraper.php?");
        QCOMPARE(queries.size(), 1);
        QVERIFY2(queries.first().contains(QLatin1String("galaga")) && queries.first().contains(QLatin1String("kinst"))
                     && !queries.first().contains(QLatin1String("sf2")) && !queries.first().contains(QLatin1String("pacman")),
                 qPrintable(queries.first()));
        // The service's sixth release dropped use_parent: not sent.
        QVERIFY(!queries.first().contains(QLatin1String("use_parent")));
        QCOMPARE(asked("/API/API_GetGame.php").size(), 1);
        QCOMPARE(arcade.gameDetails(QStringLiteral("galaga")).value(QStringLiteral("pictures")).toStringList(),
                 urls({ snap + QStringLiteral("/galaga.png"), titles + QStringLiteral("/galaga.png") }));
        QCOMPARE(arcade.gameDetails(QStringLiteral("pacman")).value(QStringLiteral("pictures")).toStringList(),
                 urls({ snap + QStringLiteral("/pacman.png") }));
        // What ArcadeDB had of each, noted (the record is written off the GUI
        // thread, Galaga's after Killer Instinct's); the marker gone, every
        // picture had.
        QTRY_VERIFY_WITH_TIMEOUT(record().value(QStringLiteral("galaga")).toObject().value(QStringLiteral("had")).toBool(),
                                 5000);
        QVERIFY(record().contains(QStringLiteral("kinst")));
        QCOMPARE(record().value(QStringLiteral("kinst")).toObject().value(QStringLiteral("had")).toBool(), false);
        QVERIFY(!record().contains(QStringLiteral("pacman")));
        QVERIFY(!QFile::exists(data.path() + QStringLiteral("/arcade/pictures-wanted")));
        const QVariantMap progress = arcade.picturesProgress();
        QCOMPARE(progress.value(QStringLiteral("active")).toBool(), false);
        QCOMPARE(progress.value(QStringLiteral("result")).toString(), QStringLiteral("Done"));
        QCOMPARE(progress.value(QStringLiteral("failed")).toInt(), 0);
        // Street Fighter II asked of RetroAchievements, Galaga and Killer
        // Instinct of ArcadeDB: not the bootleg.
        QCOMPARE(progress.value(QStringLiteral("asked")).toInt(), 3);
        const QVariantMap info = arcade.picturesInfo();
        QCOMPARE(info.value(QStringLiteral("games")).toInt(), 5);
        QCOMPARE(info.value(QStringLiteral("withPictures")).toInt(), 4);
        QCOMPARE(info.value(QStringLiteral("key")).toString(), QStringLiteral("works"));

        // Again: nothing asked of either, every set noted or with pictures,
        // and none counted as asked after, the bootleg among them.
        standIn.asked.clear();
        arcade.downloadPictures();
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.sweepingPictures(), 5000);
        QVERIFY2(standIn.asked.isEmpty(), qPrintable(standIn.asked.join(QLatin1Char(' '))));
        QCOMPARE(arcade.picturesProgress().value(QStringLiteral("asked")).toInt(), 0);

        // Metal Slug and Ms. Pac-Man come into the folder; Ms. Pac-Man is
        // shown, then Metal Slug in the same moment: RetroAchievements knows
        // neither, so ArcadeDB is asked, by itself, about the game still shown
        // alone, and its pictures come in as the game's (picturesChanged).
        writeFile(games + QStringLiteral("/mslug.zip"));
        writeFile(games + QStringLiteral("/mspacman.zip"));
        refreshed(arcade);
        standIn.asked.clear();
        QSignalSpy changed(&arcade, &ArcadeBackend::picturesChanged);
        arcade.wantPictures(QStringLiteral("mspacman"));
        arcade.wantPictures(QStringLiteral("mslug"));
        QTRY_VERIFY_WITH_TIMEOUT(changed.size() >= 2, 5000);
        QCOMPARE(asked("/service_scraper.php?").size(), 1);
        QVERIFY(asked("/service_scraper.php?").first().contains(QLatin1String("game_name=mslug")));
        QVERIFY(!asked("/service_scraper.php?").first().contains(QLatin1String("mspacman")));
        QCOMPARE(arcade.gameDetails(QStringLiteral("mslug")).value(QStringLiteral("pictures")).toStringList(),
                 urls({ snap + QStringLiteral("/mslug.png"), titles + QStringLiteral("/mslug.png") }));
        // Shown again: nothing more asked.
        standIn.asked.clear();
        arcade.wantPictures(QStringLiteral("mslug"));
        arcade.wantPictures(QStringLiteral("kinst"));
        QTest::qWait(200);
        QVERIFY(standIn.asked.isEmpty());

        // Pictures From RetroAchievements alone: ArcadeDB isn't asked about a
        // game still without a picture (one RetroAchievements hasn't); Off:
        // neither is.
        const QString module = QStringLiteral("com.osdos.arcade");
        arcade.onSettingChanged(module, QStringLiteral("pictures_source"), QStringLiteral("RetroAchievements"));
        standIn.asked.clear();
        arcade.wantPictures(QStringLiteral("mspacman"));
        QTest::qWait(300);
        QVERIFY(asked("/service_scraper.php?").isEmpty());
        arcade.onSettingChanged(module, QStringLiteral("pictures_source"), QStringLiteral("Off"));
        arcade.downloadPictures();
        QTest::qWait(300);
        QVERIFY(!arcade.sweepingPictures());
        QVERIFY(standIn.asked.isEmpty());
        arcade.onSettingChanged(module, QStringLiteral("pictures_source"), QStringLiteral("progetto-SNAPS"));
        arcade.downloadPictures();
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.sweepingPictures(), 5000);
        QCOMPARE(asked("/service_scraper.php?").size(), 1);
        QVERIFY(asked("/service_scraper.php?").first().contains(QLatin1String("mspacman")));
        QVERIFY(asked("/API/API_GetGame.php").isEmpty());

        // An answer that isn't the service's list of games: nothing is noted
        // of the set, which is asked about again later, not left for months.
        writeFile(games + QStringLiteral("/xmen6p.zip"));
        refreshed(arcade);
        standIn.answers.insert("/service_scraper.php", R"({"result": false})");
        standIn.asked.clear();
        arcade.wantPictures(QStringLiteral("xmen6p"));
        QTRY_VERIFY_WITH_TIMEOUT(!asked("/service_scraper.php?").isEmpty(), 5000);
        QTest::qWait(300);
        QVERIFY(!record().contains(QStringLiteral("xmen6p")));
    }

    // The key changed while RetroAchievements asks for a pause: the request
    // waiting out the pause was the old key's and isn't asked again; what is
    // wanted is read with the new key as it is asked for.
    void retroAchievementsKeyChangedInPause() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        const QString snap = games + QStringLiteral("/snap");
        writeFile(games + QStringLiteral("/sf2.zip"));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/mame"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        });
        StandIn standIn;
        QVERIFY(serve(standIn));
        standIn.keys << QStringLiteral("key2");
        standIn.answers = {
            { "/API/API_GetGameList.php?i=27&h=1",
              "[{\"ID\": 1, \"NumAchievements\": 2, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("sf2") + "\"]}]" },
            { "/API/API_GetGame.php?i=1", R"({"ImageTitle": "/Images/000101.png"})" },
            { "/media/Images/000101.png", png("sf2 title") },
            { "/API/API_GetConsoleIDs.php?", R"([{"ID": 27, "Name": "Arcade"}])" },
        };
        // The game's first ask: too many requests, two seconds' pause.
        standIn.statuses = { { "/API/API_GetGame.php?i=1", "429 Too Many Requests\r\nRetry-After: 2" } };
        standIn.failTimes = { { "/API/API_GetGame.php?i=1", 1 } };

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setAchievementsServers(standIn.api(), standIn.media());
        arcade.setPictureServer(standIn.service());
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("pictures_source"),
                                QStringLiteral("RetroAchievements"));
        arcade.setAchievementsKey(QStringLiteral("key1"));
        refreshed(arcade);
        QTRY_VERIFY_WITH_TIMEOUT(arcade.achievementsKeyWorks(), 5000);
        arcade.wantPictures(QStringLiteral("sf2"));
        QTRY_COMPARE_WITH_TIMEOUT(standIn.asked.count(QStringLiteral("/API/API_GetGame.php?i=1")), 1, 5000);
        // Another key in the pause: checked at once; the paused request isn't
        // asked again once the pause is over.
        arcade.setAchievementsKey(QStringLiteral("key2"));
        QTRY_VERIFY_WITH_TIMEOUT(arcade.achievementsKeyWorks(), 5000);
        QTest::qWait(2500);
        QCOMPARE(standIn.asked.count(QStringLiteral("/API/API_GetGame.php?i=1")), 1);
        QVERIFY(!QFile::exists(snap + QStringLiteral("/sf2_1.png")));
        // The game shown again: read with the new key, its picture saved.
        QSignalSpy changed(&arcade, &ArcadeBackend::picturesChanged);
        arcade.wantPictures(QStringLiteral("sf2"));
        QTRY_VERIFY_WITH_TIMEOUT(changed.size() >= 1, 10000);
        QCOMPARE(standIn.asked.count(QStringLiteral("/API/API_GetGame.php?i=1")), 2);
        QCOMPARE(contents(snap + QStringLiteral("/sf2_1.png")), png("sf2 title"));
    }

    // RetroAchievements asking for a pause (HTTP 429) isn't a refused key: the
    // request is asked again after the pause and the pictures come; nor is
    // its guard's 403 page; a 401, and a 403 in its own words, is.
    void retroAchievementsPause() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        const QString snap = games + QStringLiteral("/snap");
        writeFile(games + QStringLiteral("/sf2.zip"));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/mame"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        });
        StandIn standIn;
        QVERIFY(serve(standIn));
        standIn.keys << QStringLiteral("key2") << QStringLiteral("key3");
        standIn.answers = {
            { "/API/API_GetGameList.php?i=27&h=1",
              "[{\"ID\": 1, \"NumAchievements\": 2, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("sf2") + "\"]}]" },
            { "/API/API_GetGame.php?i=1", R"({"ImageTitle": "/Images/000101.png"})" },
            { "/media/Images/000101.png", png("sf2 title") },
            { "/API/API_GetConsoleIDs.php?", R"([{"ID": 27, "Name": "Arcade"}])" },
        };
        // The game's first ask and the picture's first download: too many
        // requests, a second's pause each.
        standIn.statuses = { { "/API/API_GetGame.php?i=1", "429 Too Many Requests\r\nRetry-After: 1" },
                             { "/media/Images/000101.png", "429 Too Many Requests\r\nRetry-After: 1" } };
        standIn.failTimes = { { "/API/API_GetGame.php?i=1", 1 }, { "/media/Images/000101.png", 1 } };

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setAchievementsServers(standIn.api(), standIn.media());
        arcade.setPictureServer(standIn.service());
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("pictures_source"),
                                QStringLiteral("RetroAchievements"));
        arcade.setAchievementsKey(QStringLiteral("key1"));
        refreshed(arcade);
        QTRY_VERIFY_WITH_TIMEOUT(arcade.achievementsKeyWorks(), 5000);
        QSignalSpy changed(&arcade, &ArcadeBackend::picturesChanged);
        arcade.wantPictures(QStringLiteral("sf2"));
        QTRY_VERIFY_WITH_TIMEOUT(changed.size() >= 1, 10000);
        QVERIFY(!arcade.achievementsKeyRefused());
        QCOMPARE(standIn.asked.count(QStringLiteral("/API/API_GetGame.php?i=1")), 2);
        QCOMPARE(standIn.asked.count(QStringLiteral("/media/Images/000101.png")), 2);
        QCOMPARE(contents(snap + QStringLiteral("/sf2_1.png")), png("sf2 title"));

        // The guard's page in a 403: no fault of the key's, which still works
        // once an answer comes with it.
        standIn.statuses = { { "/API/API_GetConsoleIDs.php?", "403 Forbidden" } };
        standIn.statusBodies = { { "/API/API_GetConsoleIDs.php?", "<html><body>Checking your browser</body></html>" } };
        standIn.failTimes = { { "/API/API_GetConsoleIDs.php?", 1 } };
        arcade.setAchievementsKey(QStringLiteral("key2"));
        QTest::qWait(500);
        QVERIFY(!arcade.achievementsKeyRefused());
        QVERIFY(!arcade.achievementsKeyWorks());
        arcade.checkAchievementsKey();
        QTRY_VERIFY_WITH_TIMEOUT(arcade.achievementsKeyWorks(), 5000);
        // RetroAchievements' own 403: refused; so is a key it doesn't know.
        standIn.statuses = { { "/API/API_GetConsoleIDs.php?", "403 Forbidden" } };
        standIn.statusBodies = { { "/API/API_GetConsoleIDs.php?", R"({"message": "This action is unauthorized."})" } };
        standIn.failTimes.clear();
        arcade.setAchievementsKey(QStringLiteral("key3"));
        QTRY_VERIFY_WITH_TIMEOUT(arcade.achievementsKeyRefused(), 5000);
        standIn.statuses.clear();
        standIn.statusBodies.clear();
        arcade.setAchievementsKey(QStringLiteral("nokey"));
        QTRY_VERIFY_WITH_TIMEOUT(arcade.achievementsKeyRefused(), 5000);
    }

    // A look for pictures that couldn't have every one is made again later,
    // and one cut short goes on at the next start: the marker left in the
    // data folder starts it once the games are read.
    void pictureSweepTriesAgain() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        const QString snap = games + QStringLiteral("/snap");
        writeFile(games + QStringLiteral("/sf2.zip"));
        writeFile(games + QStringLiteral("/galaga.zip"));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/mame"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        });
        StandIn standIn;
        QVERIFY(serve(standIn));
        // Street Fighter II's picture can't be downloaded at first.
        standIn.answers = {
            { "/API/API_GetGameList.php?i=27&h=1",
              "[{\"ID\": 1, \"NumAchievements\": 2, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("sf2") + "\"]}]" },
            { "/API/API_GetGame.php?i=1", R"({"ImageTitle": "/Images/000101.png"})" },
            { "/API/API_GetConsoleIDs.php?", R"([{"ID": 27, "Name": "Arcade"}])" },
        };
        // A look left wanting by the last run.
        writeFile(data.path() + QStringLiteral("/arcade/pictures-wanted"), "wanted\n");

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setAchievementsServers(standIn.api(), standIn.media());
        arcade.setPictureServer(standIn.service());
        arcade.setPictureSweepInterval(50);
        arcade.setPictureRetryInterval(500);
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("pictures_source"),
                                QStringLiteral("RetroAchievements"));
        arcade.setAchievementsKey(QStringLiteral("key1"));
        // The games read: the look goes on by itself (Download Pictures asked
        // meanwhile is wanted all the same, and says so at once), and ends
        // with a picture it couldn't have, to be tried again.
        arcade.refresh();
        QSignalSpy progressChanged(&arcade, &ArcadeBackend::picturesProgressChanged);
        arcade.downloadPictures();
        QCOMPARE(progressChanged.size(), 1);
        QVERIFY(arcade.picturesProgress().value(QStringLiteral("active")).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(standIn.asked.contains(QStringLiteral("/API/API_GetGame.php?i=1")), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(arcade.picturesProgress().value(QStringLiteral("again")).toBool(), 10000);
        QCOMPARE(arcade.picturesProgress().value(QStringLiteral("failed")).toInt(), 1);
        QVERIFY(QFile::exists(data.path() + QStringLiteral("/arcade/pictures-wanted")));
        // Had by the next look, which passes the game whose pictures are in
        // (the retry's ten minutes are a test's half second).
        standIn.answers.insert("/media/Images/000101.png", png("sf2 title"));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(snap + QStringLiteral("/sf2_1.png")), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(data.path() + QStringLiteral("/arcade/pictures-wanted")), 10000);
        QCOMPARE(arcade.picturesProgress().value(QStringLiteral("result")).toString(), QStringLiteral("Done"));
        QVERIFY(!arcade.picturesProgress().value(QStringLiteral("active")).toBool());
    }

    // The Arcade screen's RetroAchievements filter: with Online High Scores
    // on RetroAchievements and a key, the games it has achievements or a
    // leaderboard of, by their own file's hash, that play in FinalBurn Neo;
    // its list read once, as the filters are asked for, and kept for the
    // next start. None with Online High Scores off or without the key; a game
    // moved to MAME leaves it. Menu Filters lists it under Lists, after
    // Recently Played, and can hide it; Start Filter offers it.
    void retroAchievementsFilter() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        for (const char *set : { "sf2.zip", "mslug.zip", "galaga.zip", "pacman.zip", "kinst.zip" })
            writeFile(games + QLatin1Char('/') + QLatin1String(set));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        for (const char *program : { "/bin/mame", "/bin/retroarch" })
            QVERIFY(QFile::setPermissions(data.path() + QLatin1String(program),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        writeFile(libretro::downloadedCore(data.path()));
        writeFile(libretro::downloadedDat(data.path()), fixture("fbneo-arcade.dat"));
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        });
        // sf2's game has achievements and a leaderboard, mslug's a
        // leaderboard alone, galaga's neither; pacman's achievements, but
        // FinalBurn Neo hasn't it; kinst RetroAchievements hasn't.
        StandIn standIn;
        QVERIFY(serve(standIn));
        standIn.answers = {
            { "/API/API_GetGameList.php?i=27&h=1",
              "[{\"ID\": 1, \"NumAchievements\": 20, \"NumLeaderboards\": 1, \"Hashes\": [\"" + hashOf("sf2") + "\"]},"
              " {\"ID\": 2, \"NumAchievements\": 0, \"NumLeaderboards\": 3, \"Hashes\": [\"" + hashOf("mslug") + "\"]},"
              " {\"ID\": 3, \"NumAchievements\": 0, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("galaga") + "\"]},"
              " {\"ID\": 4, \"NumAchievements\": 8, \"NumLeaderboards\": 0, \"Hashes\": [\"" + hashOf("pacman") + "\"]}]" },
            { "/API/API_GetConsoleIDs.php?", R"([{"ID": 27, "Name": "Arcade"}])" },
        };
        const QString module = QStringLiteral("com.osdos.arcade");
        const QString list = QStringLiteral("/API/API_GetGameList.php?i=27&h=1");
        const auto filterOf = [](ArcadeBackend &arcade) {
            const QVariantList filters = arcade.filters();
            for (const QVariant &v : filters) {
                if (v.toMap().value(QStringLiteral("id")).toString() == QLatin1String("achievements"))
                    return v.toMap();
            }
            return QVariantMap();
        };
        const auto setsOf = [](ArcadeBackend &arcade) {
            QStringList sets;
            const QVariantList games = arcade.filterGames(QStringLiteral("achievements"), 0, 10);
            for (const QVariant &v : games)
                sets << v.toMap().value(QStringLiteral("set")).toString();
            return sets;
        };
        const auto labels = [](const QVariant &options) {
            QStringList list;
            const QVariantList all = options.toList();
            for (const QVariant &v : all)
                list << v.toMap().value(QStringLiteral("label")).toString();
            return list;
        };
        {
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            arcade.setAchievementsServers(standIn.api(), standIn.media());
            refreshed(arcade);
            QTRY_VERIFY_WITH_TIMEOUT(!arcade.filters().isEmpty(), 10000);
            QVERIFY(arcade.fbneoReady());
            // Neither Online High Scores on it nor a key: none, nothing asked.
            QVERIFY(filterOf(arcade).isEmpty());
            arcade.onSettingChanged(module, QStringLiteral("online_high_scores"), QStringLiteral("RetroAchievements"));
            QVERIFY(filterOf(arcade).isEmpty());
            QVERIFY(!arcade.filterPending(QStringLiteral("achievements")));
            QTest::qWait(600);
            QVERIFY(standIn.asked.isEmpty());

            // A key: the list read once, the filter waited for meanwhile and
            // there as it comes.
            QSignalSpy changed(&arcade, &ArcadeBackend::gamesChanged);
            arcade.setAchievementsKey(QStringLiteral("key1"));
            QVERIFY(!changed.isEmpty());
            QVERIFY(filterOf(arcade).isEmpty());
            QVERIFY(arcade.filterPending(QStringLiteral("achievements")));
            QVERIFY(!arcade.filterPending(QStringLiteral("favorites")));
            QTRY_VERIFY_WITH_TIMEOUT(!filterOf(arcade).isEmpty(), 5000);
            QVERIFY(!arcade.filterPending(QStringLiteral("achievements")));
            QCOMPARE(filterOf(arcade).value(QStringLiteral("name")).toString(), QStringLiteral("RetroAchievements"));
            QCOMPARE(filterOf(arcade).value(QStringLiteral("count")).toInt(), 2);
            QCOMPARE(setsOf(arcade), QStringList({ QStringLiteral("mslug"), QStringLiteral("sf2") }));
            QCOMPARE(arcade.filterIndex(QStringLiteral("achievements"), QStringLiteral("sf2")), 1);
            QCOMPARE(arcade.filterIndex(QStringLiteral("achievements"), QStringLiteral("galaga")), -1);
            QTest::qWait(600);
            QCOMPARE(standIn.asked.count(list), 1);

            // A game moved to MAME leaves it, and comes back with Auto.
            arcade.onSettingChanged(module, QStringLiteral("engines"),
                                    QVariantMap{ { QStringLiteral("sf2"), QStringLiteral("mame") } });
            QCOMPARE(setsOf(arcade), QStringList({ QStringLiteral("mslug") }));
            arcade.onSettingChanged(module, QStringLiteral("engines"), QVariantMap());
            QCOMPARE(setsOf(arcade).size(), 2);

            // Menu Filters: under Lists, after Recently Played; hidden there,
            // not among the filters. Start Filter offers it.
            QSignalSpy options(&arcade, &ArcadeBackend::dynamicOptionsReady);
            arcade.menuFilterOptions();
            QTRY_COMPARE_WITH_TIMEOUT(options.size(), 1, 5000);
            QCOMPARE(options.last().at(0).toString(), QStringLiteral("menu_filters"));
            QCOMPARE(labels(options.last().at(1)).mid(0, 6),
                     QStringList({ QStringLiteral("Lists"), QStringLiteral("Favorites"), QStringLiteral("Recently Played"),
                                   QStringLiteral("RetroAchievements"), QStringLiteral("Playable Online"),
                                   QStringLiteral("Not Recognized") }));
            arcade.startFilterOptions();
            QTRY_COMPARE_WITH_TIMEOUT(options.size(), 2, 5000);
            QCOMPARE(options.last().at(0).toString(), QStringLiteral("start_filter"));
            QVERIFY(labels(options.last().at(1)).contains(QStringLiteral("RetroAchievements")));
            arcade.onSettingChanged(module, QStringLiteral("menu_filters"),
                                    QVariantMap{ { QStringLiteral("achievements"), false } });
            QVERIFY(filterOf(arcade).isEmpty());
            arcade.onSettingChanged(module, QStringLiteral("menu_filters"), QVariantMap());
            QCOMPARE(filterOf(arcade).value(QStringLiteral("count")).toInt(), 2);

            // Online High Scores off, or no key: none, the games set to
            // FinalBurn Neo though they are.
            arcade.onSettingChanged(module, QStringLiteral("engines"),
                                    QVariantMap{ { QStringLiteral("sf2"), QStringLiteral("fbneo") },
                                                 { QStringLiteral("mslug"), QStringLiteral("fbneo") } });
            QCOMPARE(setsOf(arcade).size(), 2);
            arcade.onSettingChanged(module, QStringLiteral("online_high_scores"), QStringLiteral("Off"));
            QVERIFY(filterOf(arcade).isEmpty());
            QVERIFY(!arcade.filterPending(QStringLiteral("achievements")));
            arcade.onSettingChanged(module, QStringLiteral("online_high_scores"), QStringLiteral("RetroAchievements"));
            QCOMPARE(filterOf(arcade).value(QStringLiteral("count")).toInt(), 2);
            arcade.setAchievementsKey(QString());
            QVERIFY(filterOf(arcade).isEmpty());
            QVERIFY(!arcade.filterPending(QStringLiteral("achievements")));
            arcade.setAchievementsKey(QStringLiteral("key1"));
            QTRY_VERIFY_WITH_TIMEOUT(arcade.achievementsKeyWorks(), 5000);
        }

        // The next start: the list kept, the filter there at once, nothing
        // read.
        standIn.asked.clear();
        ArcadeBackend again(data.path(), data.path(), nullptr, nullptr);
        again.setAchievementsServers(standIn.api(), standIn.media());
        refreshed(again);
        QTRY_VERIFY_WITH_TIMEOUT(!again.filters().isEmpty(), 10000);
        again.onSettingChanged(module, QStringLiteral("online_high_scores"), QStringLiteral("RetroAchievements"));
        QCOMPARE(filterOf(again).value(QStringLiteral("count")).toInt(), 2);
        QTest::qWait(600);
        QVERIFY(standIn.asked.isEmpty());
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
        // big: an answer far bigger than a list of rooms. hold: the request
        // kept unanswered (held) until answer() is called for it.
        bool big = false;
        bool hold = false;
        QPointer<QTcpSocket> held;
        int asked = 0;
        const auto answer = [&lobby, &big](QTcpSocket *socket) {
            const QByteArray body = big ? QByteArray(5 * 1024 * 1024, ' ') : lobby;
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        };
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&server, &asked, &hold, &held, &answer] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &asked, &hold, &held, &answer] {
                socket->readAll();
                ++asked;
                if (hold)
                    held = socket;
                else
                    answer(socket);
            });
        });

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setLobbyUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/list").arg(server.serverPort())));
        // The copy of the app that says our core's version (--core-info),
        // for the core it is given.
        const QString coreInfo = data.path() + QStringLiteral("/bin/coreinfo");
        writeFile(coreInfo, "#!/bin/sh\n"
                            "case \"$2\" in *fbneo_libretro*) printf 'name=FinalBurn Neo\\nversion=v1.0.0.03 6e7c2a1\\n' ;;"
                            " *) exit 1 ;; esac\n");
        QVERIFY(QFile::setPermissions(coreInfo, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        arcade.setCoreInfoCommand({ coreInfo, QStringLiteral("--core-info") });
        refreshed(arcade);
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.filters().isEmpty(), 10000);
        // The Arcade screen's Playable Online: its games, and the count
        // filters() gives it (0 while it isn't among them).
        const auto playable = [&arcade] {
            QStringList sets;
            const QVariantList games = arcade.filterGames(QStringLiteral("netplay"), 0, 10);
            for (const QVariant &v : games)
                sets << v.toMap().value(QStringLiteral("set")).toString();
            return sets;
        };
        const auto listed = [&arcade] {
            const QVariantList filters = arcade.filters();
            for (const QVariant &v : filters) {
                const QVariantMap filter = v.toMap();
                if (filter.value(QStringLiteral("id")).toString() == QLatin1String("netplay")
                    && filter.value(QStringLiteral("name")).toString() == QLatin1String("Playable Online"))
                    return filter.value(QStringLiteral("count")).toInt();
            }
            return 0;
        };
        // Off: no lobby, no folder, nothing to host, no Playable Online.
        QCOMPARE(asked, 0);
        QVERIFY(arcade.rooms().isEmpty());
        QVERIFY(!names(arcade.requestEntries(QStringLiteral("top"))).contains(QStringLiteral("Online Games")));
        QVERIFY(!arcade.canHost(QStringLiteral("sf2")));
        QVERIFY(playable().isEmpty());
        QCOMPARE(listed(), 0);

        const QString module = QStringLiteral("com.osdos.arcade");
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("On"));
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("fbneo"));
        QVERIFY(arcade.canHost(QStringLiteral("sf2")));
        QVERIFY(!arcade.canHost(QStringLiteral("pacman")));
        // Playable Online: the games that can be hosted, by name, not one
        // FinalBurn Neo hasn't; one set to MAME too, as online it plays in
        // FinalBurn Neo. Menu Filters can hide it.
        QCOMPARE(listed(), 3);
        QCOMPARE(playable(), QStringList({ QStringLiteral("galaga"), QStringLiteral("mslug"), QStringLiteral("sf2") }));
        arcade.onSettingChanged(module, QStringLiteral("menu_filters"),
                                QVariantMap{ { QStringLiteral("netplay"), false } });
        QCOMPARE(listed(), 0);
        arcade.onSettingChanged(module, QStringLiteral("menu_filters"), QVariantMap());
        QCOMPARE(listed(), 3);
        QCOMPARE(arcade.filterIndex(QStringLiteral("netplay"), QStringLiteral("pacman")), -1);
        arcade.onSettingChanged(module, QStringLiteral("engines"),
                                QVariantMap{ { QStringLiteral("sf2"), QStringLiteral("mame") } });
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));
        QVERIFY(arcade.canHost(QStringLiteral("sf2")));
        QCOMPARE(playable().size(), 3);
        arcade.onSettingChanged(module, QStringLiteral("engines"), QVariantMap());
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("top"))).value(0), QStringLiteral("Online Games"));
        // Asked for before the lobby is read: read then, listed once it is.
        QSignalSpy roomsRead(&arcade, &ArcadeBackend::roomsChanged);
        QSignalSpy ready(&arcade, &ArcadeBackend::entriesReady);
        QVERIFY(!arcade.requestEntries(QStringLiteral("online")).isValid());
        QTRY_VERIFY_WITH_TIMEOUT(ready.contains(QVariantList{ QStringLiteral("online") }), 5000);
        QVERIFY(!roomsRead.isEmpty());
        QCOMPARE(asked, 1);
        QVERIFY(ready.contains(QVariantList{ QStringLiteral("online") }));
        const QVariantList rooms = arcade.rooms();
        QCOMPARE(rooms.size(), 1);
        const QVariantMap room = rooms.first().toMap();
        QCOMPARE(room.value(QStringLiteral("id")).toInt(), 11);
        QCOMPARE(room.value(QStringLiteral("set")).toString(), QStringLiteral("sf2"));
        QCOMPARE(room.value(QStringLiteral("host")).toString(), QStringLiteral("ann"));
        // With what the lobby says of it, as lobby() has it, for the Arcade
        // screen's box.
        const QVariantMap said = room.value(QStringLiteral("info")).toMap();
        QCOMPARE(said.value(QStringLiteral("id")).toInt(), 11);
        QCOMPARE(said.value(QStringLiteral("title")).toString(), QStringLiteral("Street Fighter II: The World Warrior"));
        QCOMPARE(said.value(QStringLiteral("rom")).toString(), QStringLiteral("SF2"));
        QCOMPARE(said.value(QStringLiteral("set")).toString(), QStringLiteral("sf2"));
        QCOMPARE(said.value(QStringLiteral("host")).toString(), QStringLiteral("ann"));
        QCOMPARE(said.value(QStringLiteral("country")).toString(), QStringLiteral("es"));
        QCOMPARE(said.value(QStringLiteral("retroarch")).toString(), QStringLiteral("1.20.0"));
        QCOMPARE(said.value(QStringLiteral("relay")).toString(), QString());
        QVERIFY(said.value(QStringLiteral("joinable")).toBool());
        const QVariantList entries = arcade.requestEntries(QStringLiteral("online")).toList();
        QCOMPARE(entries.size(), 2);
        const QVariantMap entry = entries.first().toMap();
        QCOMPARE(entry.value(QStringLiteral("kind")).toString(), QStringLiteral("room"));
        QCOMPARE(entry.value(QStringLiteral("room")).toInt(), 11);
        QCOMPARE(entry.value(QStringLiteral("game")).toString(), QStringLiteral("sf2"));
        QVERIFY(entry.value(QStringLiteral("name")).toString().endsWith(QStringLiteral(" (ann)")));
        QCOMPARE(entry.value(QStringLiteral("detail")).toString(), QStringLiteral("Hosted by ann in ES • sf2.zip"));
        // Then the whole lobby, every core's rooms (Lobby.qml).
        QCOMPARE(entries.last().toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("lobby"));
        QCOMPARE(entries.last().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("RetroArch Lobby"));
        // The whole lobby: FinalBurn Neo's rooms first, by title (a game of
        // the folder by its name there), then the others' by core; each
        // that can't be joined saying why; the one that can, the room
        // Online Games has.
        const QVariantMap whole = arcade.lobby();
        QCOMPARE(whole.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        const QVariantList lobbyRooms = whole.value(QStringLiteral("rooms")).toList();
        QStringList order;
        QHash<int, QVariantMap> byId;
        for (const QVariant &v : lobbyRooms) {
            const QVariantMap r = v.toMap();
            order << QString::number(r.value(QStringLiteral("id")).toInt());
            byId.insert(r.value(QStringLiteral("id")).toInt(), r);
        }
        QCOMPARE(order, QStringList({ "20", "15", "18", "14", "21", "19", "17", "11", "10", "16", "22" }));
        const auto why = [&byId](int id) { return byId.value(id).value(QStringLiteral("why")).toString(); };
        QVERIFY(byId.value(11).value(QStringLiteral("joinable")).toBool());
        QCOMPARE(why(11), QString());
        QCOMPARE(byId.value(11).value(QStringLiteral("title")).toString(), QStringLiteral("Street Fighter II: The World Warrior"));
        QCOMPARE(byId.value(11).value(QStringLiteral("set")).toString(), QStringLiteral("sf2"));
        QCOMPARE(byId.value(11).value(QStringLiteral("country")).toString(), QStringLiteral("es"));
        QCOMPARE(why(14), QStringLiteral("It asks for a password"));
        QCOMPARE(why(15), QStringLiteral("Its host's connection takes no players"));
        QCOMPARE(why(16), QStringLiteral("OSD/OS joins FinalBurn Neo's games alone: this one is Snes9x's"));
        QCOMPARE(why(10), QStringLiteral("OSD/OS joins FinalBurn Neo's games alone: this one is Beetle PSX HW's"));
        QCOMPARE(why(17), QStringLiteral("Its host's RetroArch can't be joined from here"));
        QCOMPARE(why(19), QStringLiteral("The lobby gives no address it can be joined at"));
        QCOMPARE(why(21), QStringLiteral("The lobby gives no address it can be joined at"));
        QCOMPARE(why(18), QStringLiteral("kof98 isn't in your games folder"));
        QCOMPARE(byId.value(18).value(QStringLiteral("title")).toString(), QStringLiteral("kof98.zip"));
        QVERIFY(byId.value(18).value(QStringLiteral("set")).toString().isEmpty());
        QCOMPARE(why(20), QStringLiteral("Its game isn't named as a set"));
        int joinable = 0;
        for (const QVariantMap &r : std::as_const(byId))
            joinable += r.value(QStringLiteral("joinable")).toBool() ? 1 : 0;
        QCOMPARE(joinable, 1);
        const QVariantMap kart = byId.value(22);
        QCOMPARE(kart.value(QStringLiteral("title")).toString(), QStringLiteral("Super Mario Kart (USA)"));
        QCOMPARE(kart.value(QStringLiteral("crc")).toString(), QStringLiteral("82993FA8"));
        QCOMPARE(kart.value(QStringLiteral("relay")).toString(), QStringLiteral("us-east1"));
        QCOMPARE(kart.value(QStringLiteral("players")).toInt(), 2);
        // The minutes since it was hosted, as the lobby says it was.
        const qint64 since = std::max<qint64>(0, QDateTime::fromString(QStringLiteral("2026-10-20T19:38:57Z"), Qt::ISODate)
                                                     .secsTo(QDateTime::currentDateTimeUtc()) / 60);
        QVERIFY2(qAbs(kart.value(QStringLiteral("minutes")).toLongLong() - since) <= 1,
                 qPrintable(kart.value(QStringLiteral("minutes")).toString()));
        QCOMPARE(byId.value(11).value(QStringLiteral("minutes")).toInt(), -1);
        // Each room matched against our own core's version and RetroArch's,
        // read as the lobby was: the same, another, or not to be known; none
        // of another core's.
        QTRY_COMPARE_WITH_TIMEOUT(arcade.lobby().value(QStringLiteral("yourCore")).toString(),
                                  QStringLiteral("v1.0.0.03 6e7c2a1"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(arcade.lobby().value(QStringLiteral("yourRetroArch")).toString(),
                                  QStringLiteral("1.22.2"), 5000);
        QCOMPARE(arcade.lobby().value(QStringLiteral("yourCoreState")).toString(), QStringLiteral("read"));
        QCOMPARE(arcade.lobby().value(QStringLiteral("yourRetroArchState")).toString(), QStringLiteral("read"));
        QHash<int, QVariantMap> matched;
        for (const QVariant &v : arcade.lobby().value(QStringLiteral("rooms")).toList())
            matched.insert(v.toMap().value(QStringLiteral("id")).toInt(), v.toMap());
        const auto matches = [&matched](int id) {
            return matched.value(id).value(QStringLiteral("coreMatch")).toString() + QLatin1Char('/')
                   + matched.value(id).value(QStringLiteral("retroarchMatch")).toString();
        };
        QCOMPARE(matches(11), QStringLiteral("same/other"));
        QCOMPARE(matches(14), QStringLiteral("other/same"));
        QCOMPARE(matches(15), QStringLiteral("unknown/same"));
        QCOMPARE(matches(16), QStringLiteral("/same"));
        // Online Play turned off while the lobby is read: the lobby
        // forgotten, and the read stopped, so that its answer, however late,
        // neither offers its rooms nor builds the screens anew. On again, the
        // lobby is read before a room is offered.
        hold = true;
        arcade.refreshRooms();
        QTRY_VERIFY_WITH_TIMEOUT(held, 5000);
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("Off"));
        QSignalSpy afterOff(&arcade, &ArcadeBackend::gamesChanged);
        const qsizetype readBefore = roomsRead.size();
        QVERIFY(held);
        answer(held);
        QTest::qWait(500);
        QCOMPARE(roomsRead.size(), readBefore);
        QVERIFY(afterOff.isEmpty());
        QVERIFY(arcade.rooms().isEmpty());
        QCOMPARE(arcade.lobby().value(QStringLiteral("state")).toString(), QStringLiteral("loading"));
        QVERIFY(arcade.lobby().value(QStringLiteral("rooms")).toList().isEmpty());
        hold = false;
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("On"));
        QVERIFY(!arcade.requestEntries(QStringLiteral("online")).isValid());
        QVERIFY(roomsRead.wait(5000));
        QCOMPARE(arcade.rooms().size(), 1);
        // The Arcade screen's filters: All Games, Favorites and Recently
        // Played with no game on them, then Online Games with its room, then
        // RetroArch Lobby, every room of the lobby.
        const QVariantList filters = arcade.filters();
        QStringList filterOrder;
        for (const QVariant &v : filters)
            filterOrder << v.toMap().value(QStringLiteral("id")).toString();
        QCOMPARE(filterOrder.mid(0, 5), QStringList({ QStringLiteral("all"), QStringLiteral("favorites"),
                                                      QStringLiteral("recent"), QStringLiteral("online"),
                                                      QStringLiteral("lobby") }));
        QCOMPARE(filters.value(1).toMap().value(QStringLiteral("count")).toInt(), 0);
        QCOMPARE(filters.value(3).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Online Games"));
        QCOMPARE(filters.value(3).toMap().value(QStringLiteral("count")).toInt(), 1);
        QCOMPARE(filters.value(4).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("RetroArch Lobby"));
        QCOMPARE(filters.value(4).toMap().value(QStringLiteral("count")).toInt(), 11);
        QCOMPARE(arcade.filterStatus(QStringLiteral("favorites")).value(QStringLiteral("name")).toString(),
                 QStringLiteral("No Favorites"));
        QCOMPARE(arcade.filterStatus(QStringLiteral("recent")).value(QStringLiteral("name")).toString(),
                 QStringLiteral("No Games Played Yet"));
        QVERIFY(arcade.filterStatus(QStringLiteral("all")).isEmpty());
        QVERIFY(arcade.filterStatus(QStringLiteral("lobby")).isEmpty());
        const QVariantMap row = arcade.filterGames(QStringLiteral("online"), 0, 7).value(0).toMap();
        QCOMPARE(row.value(QStringLiteral("set")).toString(), QStringLiteral("sf2"));
        QCOMPARE(row.value(QStringLiteral("room")).toInt(), 11);
        QCOMPARE(row.value(QStringLiteral("host")).toString(), QStringLiteral("ann"));
        // With the room as the whole lobby has it, matched against ours, for
        // the screen's box.
        const QVariantMap rowInfo = row.value(QStringLiteral("info")).toMap();
        QCOMPARE(rowInfo.value(QStringLiteral("coreMatch")).toString(), QStringLiteral("same"));
        QCOMPARE(rowInfo.value(QStringLiteral("retroarchMatch")).toString(), QStringLiteral("other"));
        QCOMPARE(rowInfo, matched.value(11));
        // A room's row, by its id; one gone, the game's first.
        QCOMPARE(arcade.filterIndex(QStringLiteral("online"), QStringLiteral("sf2"), 11), 0);
        QCOMPARE(arcade.filterIndex(QStringLiteral("online"), QStringLiteral("sf2"), 99), 0);
        QCOMPARE(arcade.filterIndex(QStringLiteral("online"), QStringLiteral("pacman"), 99), -1);
        // RetroArch Lobby's rows: the rooms as lobby() has them, each by the
        // name the lobby has for its game and its host, the folder's game by
        // its set, another's with none; a room's row by its id.
        const QVariantList lobbyRows = arcade.filterGames(QStringLiteral("lobby"), 0, 20);
        QStringList lobbyOrder;
        for (const QVariant &v : lobbyRows)
            lobbyOrder << QString::number(v.toMap().value(QStringLiteral("room")).toInt());
        QCOMPARE(lobbyOrder, order);
        const QVariantMap joinRow = lobbyRows.value(lobbyOrder.indexOf(QStringLiteral("11"))).toMap();
        QCOMPARE(joinRow.value(QStringLiteral("set")).toString(), QStringLiteral("sf2"));
        QCOMPARE(joinRow.value(QStringLiteral("name")).toString(), QStringLiteral("Street Fighter II: The World Warrior (ann)"));
        QCOMPARE(joinRow.value(QStringLiteral("host")).toString(), QStringLiteral("ann"));
        QVERIFY(joinRow.value(QStringLiteral("lobby")).toMap().value(QStringLiteral("joinable")).toBool());
        const QVariantMap kartRow = lobbyRows.value(lobbyOrder.indexOf(QStringLiteral("22"))).toMap();
        QCOMPARE(kartRow.value(QStringLiteral("set")).toString(), QString());
        QVERIFY(kartRow.value(QStringLiteral("name")).toString().startsWith(QStringLiteral("Super Mario Kart (USA) (")));
        QCOMPARE(kartRow.value(QStringLiteral("lobby")).toMap().value(QStringLiteral("crc")).toString(), QStringLiteral("82993FA8"));
        QCOMPARE(arcade.filterIndex(QStringLiteral("lobby"), QString(), 22), lobbyOrder.indexOf(QStringLiteral("22")));
        // One gone: the game's first room; none for one of no game here.
        QCOMPARE(arcade.filterIndex(QStringLiteral("lobby"), QString(), 99), -1);
        qsizetype firstSf2 = 0;
        while (firstSf2 < lobbyRows.size() && lobbyRows.at(firstSf2).toMap().value(QStringLiteral("set")) != QStringLiteral("sf2"))
            ++firstSf2;
        QVERIFY(firstSf2 < lobbyOrder.indexOf(QStringLiteral("11")));
        QCOMPARE(arcade.filterIndex(QStringLiteral("lobby"), QStringLiteral("sf2"), 99), int(firstSf2));
        // Start Filter can open on Online Games and on it, offered in the
        // screen's order.
        QSignalSpy startOptions(&arcade, &ArcadeBackend::dynamicOptionsReady);
        arcade.startFilterOptions();
        QTRY_COMPARE_WITH_TIMEOUT(startOptions.size(), 1, 5000);
        QStringList startIds;
        for (const QVariant &v : startOptions.first().at(1).toList())
            startIds << v.toMap().value(QStringLiteral("id")).toString();
        QCOMPARE(startIds.mid(0, 5), QStringList({ QStringLiteral("all"), QStringLiteral("favorites"), QStringLiteral("recent"),
                                                   QStringLiteral("online"), QStringLiteral("lobby") }));
        const auto offersLobby = [&startOptions] {
            for (const QVariant &v : startOptions.last().at(1).toList()) {
                if (v.toMap().value(QStringLiteral("id")).toString() == QLatin1String("lobby"))
                    return true;
            }
            return false;
        };
        // Online Play off: none of it, and Start Filter's choices sent again
        // without it; on, with it, once the lobby is read again.
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("Off"));
        QVERIFY(arcade.filterGames(QStringLiteral("lobby"), 0, 7).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(startOptions.size(), 2, 5000);
        QCOMPARE(startOptions.last().at(0).toString(), QStringLiteral("start_filter"));
        QVERIFY(!offersLobby());
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("On"));
        QTRY_COMPARE_WITH_TIMEOUT(startOptions.size(), 3, 5000);
        QVERIFY(offersLobby());
        arcade.refreshRooms();
        QVERIFY(roomsRead.wait(5000));
        QCOMPARE(arcade.rooms().size(), 1);

        // Joined: RetroArch connects to the room, under the default name, not
        // the device's, which stays off the internet. A room's host plays
        // Classic controller types, so the JAMMA board's sticks play their
        // buttons by number on them: no arcade panel, and a remap file left
        // by a game off line removed.
        controllers::setPlayerEnvironment({ { QStringLiteral("OSDOS_PLAYER1_NAME"), QStringLiteral("OSD/OS JAMMA P1") },
                                            { QStringLiteral("OSDOS_PLAYER1_GUID"),
                                              QStringLiteral("06002868010000000100000001000000") },
                                            { QStringLiteral("OSDOS_PLAYER2_NAME"), QStringLiteral("OSD/OS JAMMA P2") },
                                            { QStringLiteral("OSDOS_PLAYER2_GUID"),
                                              QStringLiteral("06006869010000000200000001000000") } });
        const QString remap = data.path() + QStringLiteral("/arcade/retroarch/remaps/FinalBurn Neo/FinalBurn Neo.rmp");
        libretro::Launch offLine;
        offLine.panels = { true };
        const QByteArray leftOver = libretro::remapText(offLine).toUtf8();
        writeFile(remap, leftOver);
        const auto binds = [](const QString &config, std::initializer_list<const char *> lines,
                              std::initializer_list<const char *> absent) {
            for (const char *line : lines) {
                if (!config.contains(QLatin1String(line)))
                    return false;
            }
            for (const char *line : absent) {
                if (config.contains(QLatin1String(line)))
                    return false;
            }
            return true;
        };
        const auto onClassic = [&binds](const QString &config) {
            return binds(config, { "input_player1_reserved_device = \"0001:0001 OSD/OS JAMMA P1\"",
                                   "input_player1_b_btn = \"0\"", "input_player1_y_btn = \"2\"",
                                   "input_player1_l_btn = \"10\"", "input_player1_up_axis = \"-1\"",
                                   "input_player2_b_btn = \"0\"" },
                         { "input_player1_up_btn", "input_player2_up_btn" });
        };
        // Hosting from the cabinet, the panel's layout: every port a 6-Button
        // Panel, the sticks bound to it.
        const auto onPanel = [&binds](const QString &config) {
            return binds(config, { "input_player1_reserved_device = \"0001:0001 OSD/OS JAMMA P1\"",
                                   "input_player1_y_btn = \"0\"", "input_player1_l_btn = \"2\"",
                                   "input_player1_b_btn = \"3\"", "input_player1_up_axis = \"-1\"",
                                   "input_player2_y_btn = \"0\"" },
                         { "input_player1_up_btn", "input_player2_up_btn" });
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
        QVERIFY2(onClassic(config), qPrintable(config));
        QVERIFY(!QFileInfo::exists(remap));
        // The core's states taken for deterministic: joined, not "Core does
        // not support netplay".
        QVERIFY(logLines().contains(QStringLiteral("coreinfo deterministic")));
        // Hosted: announced in the lobby through the relay chosen, under the
        // online name; the cabinet's layout on every port, the file left by
        // the game off line replaced by it.
        arcade.onSettingChanged(module, QStringLiteral("online_name"), QStringLiteral(" Mehmet "));
        arcade.onSettingChanged(module, QStringLiteral("relay_server"), QStringLiteral("Madrid"));
        QFile::remove(m_log);
        writeFile(remap, leftOver);
        QCOMPARE(ended([&arcade] { return arcade.hostGame(QStringLiteral("sf2")); }).value(1).toString(),
                 QStringLiteral("ok"));
        QVERIFY(logLines(QStringLiteral("retroarch ")).value(0).contains(QStringLiteral(" --host ")));
        config = QString::fromUtf8(contents(configCopy));
        for (const char *line : { "netplay_nickname = \"Mehmet\"", "netplay_public_announce = \"true\"",
                                  "netplay_use_mitm_server = \"true\"", "netplay_mitm_server = \"madrid\"" })
            QVERIFY2(config.contains(QLatin1String(line)), line);
        QVERIFY2(onPanel(config), qPrintable(config));
        QCOMPARE(QString::fromUtf8(contents(remap)).split(QLatin1Char('\n')).filter(QStringLiteral("input_libretro_device_p")).size(), 6);
        QVERIFY(logLines().contains(QStringLiteral("coreinfo deterministic")));
        // Files missing in a game hosted, which plays online in FinalBurn Neo
        // alone: nothing said of MAME.
        const QVariantList missing = ended([&arcade] { return arcade.hostGame(QStringLiteral("mslug")); });
        QCOMPARE(missing.value(1).toString(), QStringLiteral("failed"));
        QCOMPARE(missing.value(2).toString(),
                 QStringLiteral("Missing 201-p1.p1, 201-s1.s1: the set may be for another version of FinalBurn Neo\n"
                                "Or a BIOS set it needs isn't in the games folder"));
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
        // Online Games and RetroArch Lobby still there, with no room, for the
        // screen to say why: Online Games as the tree's line says it.
        const auto filterCount = [&arcade](const QString &id) {
            for (const QVariant &v : arcade.filters()) {
                if (v.toMap().value(QStringLiteral("id")).toString() == id)
                    return v.toMap().value(QStringLiteral("count")).toInt();
            }
            return -1;
        };
        QCOMPARE(filterCount(QStringLiteral("online")), 0);
        QCOMPARE(filterCount(QStringLiteral("lobby")), 0);
        QVERIFY(arcade.filterGames(QStringLiteral("lobby"), 0, 7).isEmpty());
        QVERIFY(arcade.filterGames(QStringLiteral("online"), 0, 7).isEmpty());
        const QVariantMap onlineSays = arcade.filterStatus(QStringLiteral("online"));
        QCOMPARE(onlineSays.value(QStringLiteral("name")).toString(), QStringLiteral("Lobby Not Reachable"));
        QCOMPARE(onlineSays.value(QStringLiteral("detail")).toString(), gone.value(QStringLiteral("detail")).toString());

        // FinalBurn Neo's core gone, and back: none can be played online
        // meanwhile. Its list gone: none either, as the folder is read again.
        const QString core = libretro::downloadedCore(data.path());
        QVERIFY(QFile::rename(core, core + QStringLiteral(".away")));
        refreshed(arcade);
        QVERIFY(playable().isEmpty());
        QCOMPARE(listed(), 0);
        QVERIFY(QFile::rename(core + QStringLiteral(".away"), core));
        refreshed(arcade);
        QCOMPARE(listed(), 3);
        QVERIFY(QFile::remove(libretro::downloadedDat(data.path())));
        refreshed(arcade);
        QVERIFY(playable().isEmpty());
        QCOMPARE(listed(), 0);
        writeFile(libretro::downloadedDat(data.path()), fixture("fbneo-arcade.dat"));
        refreshed(arcade);
        QCOMPARE(listed(), 3);

        // Off again: no Playable Online, no Online Games, no RetroArch Lobby.
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("Off"));
        QVERIFY(playable().isEmpty());
        QCOMPARE(listed(), 0);
        QCOMPARE(filterCount(QStringLiteral("online")), -1);
        QCOMPARE(filterCount(QStringLiteral("lobby")), -1);
        QVERIFY(arcade.filterGames(QStringLiteral("lobby"), 0, 7).isEmpty());
    }

    // LAN Play against a stand-in for the hosts on the local network, which
    // answer RetroArch's query as RetroArch does: the rooms of the folder's
    // games FinalBurn Neo plays (not one with a password, one not there,
    // another core's, nor what isn't an answer), in the tree's LAN Games and
    // the Arcade screen's filter, each with what its host plays in against
    // ours, its id kept while it is hosted; a room joined straight at its
    // host, and a game hosted on the network alone; a network that can't be
    // asked said so; nothing of it with LAN Play off.
    void lanPlay() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        for (const char *set : { "sf2.zip", "mslug.zip", "galaga.zip" })
            writeFile(games + QLatin1Char('/') + QLatin1String(set));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        // The copy of the app that says our core's version, once hold is
        // gone.
        const QString hold = data.path() + QStringLiteral("/hold");
        const QString coreInfo = data.path() + QStringLiteral("/bin/coreinfo");
        writeFile(coreInfo, QStringLiteral("#!/bin/sh\nwhile [ -e '%1' ]; do sleep 0.05; done\n"
                                           "printf 'name=FinalBurn Neo\\nversion=v1.0.0.03 6e7c2a1\\n'\n")
                                .arg(hold).toUtf8());
        for (const char *program : { "/bin/mame", "/bin/retroarch", "/bin/coreinfo" })
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
        });
        // The hosts: each query from this machine answered with what they
        // play.
        QList<QByteArray> answers;
        int asked = 0;
        QUdpSocket hosts;
        QVERIFY(hosts.bind(QHostAddress::LocalHost, 0));
        connect(&hosts, &QUdpSocket::readyRead, &hosts, [&hosts, &answers, &asked] {
            while (hosts.hasPendingDatagrams()) {
                const QNetworkDatagram query = hosts.receiveDatagram();
                if (query.data() != libretro::lanQuery())
                    continue;
                ++asked;
                for (const QByteArray &answer : std::as_const(answers))
                    hosts.writeDatagram(answer, query.senderAddress(), query.senderPort());
            }
        });
        const QByteArray ann = lanAnswer("ann", "sf2", 55435);
        const QByteArray eve = lanAnswer("eve", "galaga", 55436, 0, "1.21.0");
        answers = { ann, lanAnswer("bob", "mslug", 55437, 1), lanAnswer("cy", "kof98", 55438),
                    lanAnswer("di", "smw", 55439, 0, "1.22.2", 0, "Snes9x"), QByteArray("RANS, not an answer"),
                    ann, eve };

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setLanDiscovery(QStringLiteral("127.0.0.1"), hosts.localPort());
        arcade.setLanListenTime(150);
        arcade.setCoreInfoCommand({ coreInfo, QStringLiteral("--core-info") });
        refreshed(arcade);
        const QString module = QStringLiteral("com.osdos.arcade");
        // The rooms as "id set host", and a room's line.
        const auto rooms = [&arcade] {
            QStringList list;
            for (const QVariant &v : arcade.lanRooms()) {
                const QVariantMap room = v.toMap();
                list << QStringLiteral("%1 %2 %3").arg(room.value(QStringLiteral("id")).toString(),
                                                       room.value(QStringLiteral("set")).toString(),
                                                       room.value(QStringLiteral("host")).toString());
            }
            return list;
        };
        const auto detail = [&arcade](int at) {
            return arcade.lanRooms().value(at).toMap().value(QStringLiteral("detail")).toString();
        };
        // The Arcade screen's LAN Games: its count, -1 while it isn't there;
        // and what it says while it has no game.
        const auto lanFilter = [&arcade] {
            for (const QVariant &v : arcade.filters()) {
                if (v.toMap().value(QStringLiteral("id")).toString() == QLatin1String("lan"))
                    return v.toMap().value(QStringLiteral("count")).toInt();
            }
            return -1;
        };
        const auto lanSays = [&arcade] { return arcade.lanStatus().value(QStringLiteral("name")).toString(); };
        const auto ended = [&arcade](const std::function<bool()> &start) {
            QSignalSpy finished(&arcade, &ArcadeBackend::finished);
            if (!start())
                return QVariantList{ -1, QStringLiteral("not started"), arcade.lastError() };
            if (!finished.wait(10000) && finished.isEmpty())
                return QVariantList{ -1, QStringLiteral("no end"), QString() };
            return finished.first();
        };

        // Off: no folder, nothing to host, no icon; a game left to Auto in
        // MAME.
        arcade.refreshLanRooms();
        QVERIFY(!names(arcade.requestEntries(QStringLiteral("top"))).contains(QStringLiteral("LAN Games")));
        QCOMPARE(lanFilter(), -1);
        QVERIFY(!arcade.canHostLan(QStringLiteral("sf2")));
        QVERIFY(!arcade.statusIcons().contains(QStringLiteral("network")));
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));

        // On: LAN Games first, the games in FinalBurn Neo, its icon. The
        // Arcade screen's LAN Games at once, saying the network is asked.
        arcade.onSettingChanged(module, QStringLiteral("lan_play"), QStringLiteral("On"));
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("top"))).value(0), QStringLiteral("LAN Games"));
        QCOMPARE(lanFilter(), 0);
        QCOMPARE(lanSays(), QStringLiteral("Asking the Local Network…"));
        QVERIFY(arcade.canHostLan(QStringLiteral("sf2")));
        QVERIFY(!arcade.canHostLan(QStringLiteral("kof98")));
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("fbneo"));
        QVERIFY(arcade.statusIcons().contains(QStringLiteral("network")));

        // Asked for before the network has been asked: asked then (and
        // nothing while it was off), listed once the hosts have answered.
        // Our core's version not read yet: the lines without it.
        writeFile(hold);
        QSignalSpy read(&arcade, &ArcadeBackend::lanRoomsChanged);
        QSignalSpy ready(&arcade, &ArcadeBackend::entriesReady);
        QVERIFY(!arcade.requestEntries(QStringLiteral("lan")).isValid());
        QVERIFY(read.wait(5000));
        QCOMPARE(asked, 1);
        QVERIFY(ready.contains(QVariantList{ QStringLiteral("lan") }));
        QCOMPARE(rooms(), QStringList({ QStringLiteral("1 sf2 ann"), QStringLiteral("3 galaga eve") }));
        QCOMPARE(detail(0), QStringLiteral("Hosted by ann at 127.0.0.1 on this network • sf2.zip"));
        QTRY_COMPARE_WITH_TIMEOUT(detail(1), QStringLiteral("Hosted by eve at 127.0.0.1 on this network • galaga.zip • "
                                                            "Its RetroArch is 1.21.0, yours 1.22.2"), 5000);
        // Read: the lines say it, in the tree and on the Arcade screen.
        QSignalSpy changed(&arcade, &ArcadeBackend::gamesChanged);
        ready.clear();
        QVERIFY(QFile::remove(hold));
        QTRY_COMPARE_WITH_TIMEOUT(detail(0), QStringLiteral("Hosted by ann at 127.0.0.1 on this network • sf2.zip • "
                                                            "The same FinalBurn Neo and RetroArch as yours"), 5000);
        QCOMPARE(detail(1), QStringLiteral("Hosted by eve at 127.0.0.1 on this network • galaga.zip • Its RetroArch is 1.21.0, yours 1.22.2"));
        QCOMPARE(changed.size(), 1);
        QVERIFY(ready.contains(QVariantList{ QStringLiteral("lan") }));

        // In the tree: the rooms, by the game's title and who hosts it.
        const QVariantList entries = arcade.requestEntries(QStringLiteral("lan")).toList();
        QCOMPARE(names(entries), QStringList({ QStringLiteral("Street Fighter II: The World Warrior (ann)"),
                                               QStringLiteral("Galaga (eve)") }));
        const QVariantMap entry = entries.first().toMap();
        QCOMPARE(entry.value(QStringLiteral("kind")).toString(), QStringLiteral("room"));
        QCOMPARE(entry.value(QStringLiteral("path")).toString(), QStringLiteral("lan:1"));
        QCOMPARE(entry.value(QStringLiteral("game")).toString(), QStringLiteral("sf2"));
        QCOMPARE(entry.value(QStringLiteral("room")).toInt(), 1);
        QCOMPARE(entry.value(QStringLiteral("host")).toString(), QStringLiteral("ann"));
        QVERIFY(entry.value(QStringLiteral("lan")).toBool());
        QCOMPARE(entry.value(QStringLiteral("detail")).toString(), detail(0));
        // On the Arcade screen, after All Games and the two lists; a room's
        // row by its id.
        const QVariantMap filter = arcade.filters().value(3).toMap();
        QCOMPARE(filter.value(QStringLiteral("id")).toString(), QStringLiteral("lan"));
        QCOMPARE(filter.value(QStringLiteral("name")).toString(), QStringLiteral("LAN Games"));
        QCOMPARE(filter.value(QStringLiteral("count")).toInt(), 2);
        QCOMPARE(arcade.filterStatus(QStringLiteral("lan")), arcade.lanStatus());
        const QVariantMap row = arcade.filterGames(QStringLiteral("lan"), 1, 1).value(0).toMap();
        QCOMPARE(row.value(QStringLiteral("set")).toString(), QStringLiteral("galaga"));
        QCOMPARE(row.value(QStringLiteral("room")).toInt(), 3);
        QCOMPARE(row.value(QStringLiteral("host")).toString(), QStringLiteral("eve"));
        QVERIFY(row.value(QStringLiteral("lan")).toBool());
        QCOMPARE(row.value(QStringLiteral("detail")).toString(), detail(1));
        // With what its host answered with, as the lobby's rooms are told of
        // and matched against ours, and where it is joined: its address on
        // this network, and the game's file, for the screen's box.
        const QVariantMap info = row.value(QStringLiteral("info")).toMap();
        QCOMPARE(info, arcade.lanRooms().value(1).toMap().value(QStringLiteral("info")).toMap());
        QCOMPARE(info.value(QStringLiteral("id")).toInt(), 3);
        QCOMPARE(info.value(QStringLiteral("title")).toString(), QStringLiteral("Galaga"));
        QCOMPARE(info.value(QStringLiteral("set")).toString(), QStringLiteral("galaga"));
        QCOMPARE(info.value(QStringLiteral("rom")).toString(), QStringLiteral("galaga"));
        QCOMPARE(info.value(QStringLiteral("file")).toString(), QStringLiteral("galaga.zip"));
        QCOMPARE(info.value(QStringLiteral("address")).toString(), QStringLiteral("127.0.0.1"));
        QCOMPARE(info.value(QStringLiteral("host")).toString(), QStringLiteral("eve"));
        QCOMPARE(info.value(QStringLiteral("core")).toString(), QStringLiteral("FinalBurn Neo"));
        QCOMPARE(info.value(QStringLiteral("coreVersion")).toString(), QStringLiteral("v1.0.0.03 6e7c2a1"));
        QCOMPARE(info.value(QStringLiteral("coreMatch")).toString(), QStringLiteral("same"));
        QCOMPARE(info.value(QStringLiteral("retroarch")).toString(), QStringLiteral("1.21.0"));
        QCOMPARE(info.value(QStringLiteral("retroarchMatch")).toString(), QStringLiteral("other"));
        QCOMPARE(info.value(QStringLiteral("frontend")).toString(), QStringLiteral("unix x64"));
        QCOMPARE(info.value(QStringLiteral("crc")).toString(), QString());
        QCOMPARE(info.value(QStringLiteral("subsystem")).toString(), QString());
        QCOMPARE(info.value(QStringLiteral("relay")).toString(), QString());
        QCOMPARE(info.value(QStringLiteral("minutes")).toInt(), -1);
        QCOMPARE(info.value(QStringLiteral("players")).toInt(), -1);
        QVERIFY(!info.value(QStringLiteral("password")).toBool());
        QVERIFY(info.value(QStringLiteral("joinable")).toBool());
        QCOMPARE(info.value(QStringLiteral("why")).toString(), QString());
        QCOMPARE(arcade.filterIndex(QStringLiteral("lan"), QStringLiteral("galaga"), 3), 1);

        // Asked again, twice while the answers are listened for: asked once.
        // Each room keeps its id while its host plays the same game, the
        // Arcade screen not told of rooms as they were.
        changed.clear();
        answers = { eve, ann };
        arcade.refreshLanRooms();
        arcade.refreshLanRooms();
        QVERIFY(read.wait(5000));
        QCOMPARE(asked, 2);
        QCOMPARE(rooms(), QStringList({ QStringLiteral("1 sf2 ann"), QStringLiteral("3 galaga eve") }));
        QCOMPARE(changed.size(), 0);
        // Eve's answer lost on the way, among more answers of rooms that
        // aren't listed (with a password) than rooms are kept: her room kept
        // through that query, and Ann's heard.
        answers.clear();
        for (quint32 port = 56000; port < 56000 + 70; ++port)
            answers << lanAnswer("pat", "sf2", port, 1);
        answers << ann;
        arcade.refreshLanRooms();
        QVERIFY(read.wait(5000));
        QCOMPARE(rooms(), QStringList({ QStringLiteral("1 sf2 ann"), QStringLiteral("3 galaga eve") }));
        QCOMPARE(changed.size(), 0);

        // Joined: RetroArch connects straight to the host, at the port it
        // gave, under the device's name, this machine's two players (a JAMMA
        // stick and a pad) on the ports the host's OSD/OS gives them: asked
        // for at the claim port as the room is chosen, the game started on
        // the answer, on the layout the host said before it (the panel's:
        // the stick in the cabinet's rows, the pad as itself), and given
        // back as it ends.
        controllers::setPlayerEnvironment({ { QStringLiteral("OSDOS_PLAYER1_GUID"), QStringLiteral("0600aaaa") },
                                            { QStringLiteral("OSDOS_PLAYER1_NAME"), QStringLiteral("OSD/OS JAMMA P1") },
                                            { QStringLiteral("OSDOS_PLAYER2_GUID"), QStringLiteral("0600bbbb") },
                                            { QStringLiteral("OSDOS_PLAYER2_NAME"), QStringLiteral("Xbox 360 Controller") } });
        const auto noPlayers = qScopeGuard([] { controllers::setPlayerEnvironment({}); });
        QUdpSocket claims;
        QVERIFY(claims.bind(QHostAddress::LocalHost, 0));
        QList<QByteArray> claimed;
        // How the stand-in host answers: the layout then the ports, as OSD/OS
        // does; the ports first and the layout a moment later (the two are
        // datagrams of their own); or the ports alone, as an older OSD/OS.
        enum { LayoutThenPorts, PortsThenLayout, PortsAlone } answering = LayoutThenPorts;
        connect(&claims, &QUdpSocket::readyRead, &claims, [&claims, &claimed, &answering] {
            while (claims.hasPendingDatagrams()) {
                const QNetworkDatagram query = claims.receiveDatagram();
                claimed << query.data();
                if (!libretro::readClaimQuery(query.data()))
                    continue;
                const QHostAddress to = query.senderAddress();
                const quint16 port = query.senderPort();
                const auto layout = [&claims, to, port] {
                    claims.writeDatagram(libretro::claimLayout(libretro::Launch::Layout::Panel), to, port);
                };
                if (answering == LayoutThenPorts)
                    layout();
                claims.writeDatagram(libretro::claimAnswer({ 3, 2 }), to, port);
                if (answering == PortsThenLayout)
                    QTimer::singleShot(50, &claims, layout);
            }
        });
        arcade.setLanClaimPort(claims.localPort());
        const auto requested = [](const QString &config) {
            QStringList ports;
            for (int p = 1; p <= libretro::kMostPorts; ++p) {
                if (config.contains(QStringLiteral("netplay_request_device_p%1 = \"true\"").arg(p)))
                    ports << QString::number(p);
            }
            return ports.join(QLatin1Char(' '));
        };
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.joinLanRoom(1); }).value(1).toString(), QStringLiteral("ok"));
        const QString home = data.path() + QStringLiteral("/arcade/retroarch");
        QCOMPARE(logLines(QStringLiteral("retroarch ")).value(0),
                 QStringLiteral("retroarch -L %1 --config %2/retroarch.cfg --verbose --log-file=%2/last.log "
                                "--connect 127.0.0.1|55435 %3/sf2.zip").arg(libretro::downloadedCore(data.path()), home, games));
        QString config = QString::fromUtf8(contents(configCopy));
        QVERIFY2(config.contains(QStringLiteral("netplay_nickname = \"garage-cab\"")), qPrintable(config));
        QVERIFY(!config.contains(QStringLiteral("netplay_public_announce")));
        QCOMPARE(requested(config), QStringLiteral("3 4"));
        QVERIFY2(config.contains(QStringLiteral("input_player1_y_btn = \"0\"")), qPrintable(config));
        QVERIFY(config.contains(QStringLiteral("input_player1_l_btn = \"2\"")));
        QVERIFY(config.contains(QStringLiteral("input_player2_b_btn = \"0\"")));
        QVERIFY(!QFile::exists(libretro::coreRemapPath(home + QStringLiteral("/remaps"))));
        QVERIFY(logLines().contains(QStringLiteral("coreinfo deterministic")));
        QTRY_VERIFY(!claimed.isEmpty() && claimed.last() == libretro::claimRelease());
        QCOMPARE(claimed.first(), libretro::claimQuery(2));
        QVERIFY(!arcade.running());
        // The ports before the layout: the game waits a moment for it, and
        // starts on the panel's layout as before.
        answering = PortsThenLayout;
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.joinLanRoom(1); }).value(1).toString(), QStringLiteral("ok"));
        config = QString::fromUtf8(contents(configCopy));
        QCOMPARE(requested(config), QStringLiteral("3 4"));
        QVERIFY2(config.contains(QStringLiteral("input_player1_y_btn = \"0\"")), qPrintable(config));
        QTRY_VERIFY(!claimed.isEmpty() && claimed.last() == libretro::claimRelease());
        // The ports alone, from an older OSD/OS: joined with them a moment
        // later, on the Classic layout.
        answering = PortsAlone;
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.joinLanRoom(1); }).value(1).toString(), QStringLiteral("ok"));
        config = QString::fromUtf8(contents(configCopy));
        QCOMPARE(requested(config), QStringLiteral("3 4"));
        QVERIFY2(config.contains(QStringLiteral("input_player1_b_btn = \"0\"")), qPrintable(config));
        QVERIFY(config.contains(QStringLiteral("input_player1_y_btn = \"2\"")));
        QTRY_VERIFY(!claimed.isEmpty() && claimed.last() == libretro::claimRelease());
        // RetroArch that can't reach the host says so and plays the game
        // alone: stopped as its log says so, the ports given back, and the
        // end says why.
        qputenv("RA_NETPLAY_FAILS", "1");
        QFile::remove(m_log);
        const QVariantList alone = ended([&arcade] { return arcade.joinLanRoom(1); });
        qunsetenv("RA_NETPLAY_FAILS");
        QCOMPARE(alone.value(1).toString(), QStringLiteral("failed"));
        QCOMPARE(alone.value(2).toString(),
                 QStringLiteral("Could not join the game: Failed to connect to host 127.0.0.1 on port 55435\n"
                                "The host didn't answer: its game may have ended, or a firewall there keeps the port closed"));
        QTRY_VERIFY(!claimed.isEmpty() && claimed.last() == libretro::claimRelease());
        QVERIFY(!arcade.running());
        // A host that doesn't say, RetroArch alone or an older OSD/OS: asked
        // twice, then joined with the ports from 2, on the Classic layout,
        // the stick's buttons by number.
        claims.close();
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.joinLanRoom(1); }).value(1).toString(), QStringLiteral("ok"));
        config = QString::fromUtf8(contents(configCopy));
        QCOMPARE(requested(config), QStringLiteral("2 3"));
        QVERIFY2(config.contains(QStringLiteral("input_player1_b_btn = \"0\"")), qPrintable(config));
        QVERIFY(config.contains(QStringLiteral("input_player1_y_btn = \"2\"")));
        QVERIFY(config.contains(QStringLiteral("input_player1_l_btn = \"10\"")));
        // Hosted: on the network alone, straight, under the online name.
        // Street Fighter II takes two players: this machine's first on port
        // 1, whatever its second stick, so that the machine joining has the
        // other. It asks for its players' ports while the game runs and is
        // told the layout (the panel's, a JAMMA stick among this machine's
        // players: every port on it in the remap file) and given port 2
        // alone, whatever its players, its own anew as it asks again; none
        // once the game is over.
        arcade.onSettingChanged(module, QStringLiteral("online_name"), QStringLiteral("Mehmet"));
        quint16 claimPort = 0;
        {
            QUdpSocket probe;
            QVERIFY(probe.bind(QHostAddress::LocalHost, 0));
            claimPort = probe.localPort();
        }
        arcade.setLanClaimPort(claimPort);
        qputenv("RA_SECONDS", "3");
        const auto noSeconds = qScopeGuard([] { qunsetenv("RA_SECONDS"); });
        QFile::remove(m_log);
        QSignalSpy hostEnded(&arcade, &ArcadeBackend::finished);
        QVERIFY(arcade.hostLanGame(QStringLiteral("sf2")));
        QUdpSocket joiner;
        QVERIFY(joiner.bind(QHostAddress::LocalHost, 0));
        // What the host answers a query with, in order, within mostMs.
        const auto asking = [&joiner, claimPort](const QByteArray &packet, int mostMs) {
            joiner.writeDatagram(packet, QHostAddress::LocalHost, claimPort);
            QByteArrayList answers;
            for (int waited = 0; waited < mostMs && answers.size() < 2; waited += 20) {
                QTest::qWait(20);
                while (joiner.hasPendingDatagrams())
                    answers << joiner.receiveDatagram().data();
            }
            return answers.isEmpty() ? QByteArray("no answer") : answers.join(" / ");
        };
        const QByteArray panelThenPort2 = libretro::claimLayout(libretro::Launch::Layout::Panel) + " / "
                                          + libretro::claimAnswer({ 2, 1 });
        QCOMPARE(asking(libretro::claimQuery(2), 2000), panelThenPort2);
        QCOMPARE(asking(libretro::claimQuery(4), 2000), panelThenPort2);
        QCOMPARE(asking(libretro::claimQuery(1), 2000), panelThenPort2);
        const QString hostRemap = libretro::coreRemapPath(home + QStringLiteral("/remaps"));
        QCOMPARE(QString::fromUtf8(contents(hostRemap)).split(QLatin1Char('\n')).filter(QStringLiteral("input_libretro_device_p")).size(), 6);
        QVERIFY(hostEnded.wait(10000) || !hostEnded.isEmpty());
        QCOMPARE(hostEnded.first().value(1).toString(), QStringLiteral("ok"));
        QVERIFY(logLines(QStringLiteral("retroarch ")).value(0).contains(QStringLiteral(" --host ")));
        config = QString::fromUtf8(contents(configCopy));
        for (const char *line : { "netplay_nickname = \"Mehmet\"", "netplay_public_announce = \"false\"",
                                  "netplay_use_mitm_server = \"false\"", "netplay_nat_traversal = \"false\"",
                                  "input_player1_y_btn = \"0\"", "input_player2_b_btn = \"0\"" })
            QVERIFY2(config.contains(QLatin1String(line)), line);
        QCOMPARE(requested(config), QStringLiteral("1"));
        QVERIFY(logLines().contains(QStringLiteral("coreinfo deterministic")));
        QCOMPARE(asking(libretro::claimQuery(1), 300), QByteArray("no answer"));
        // Hosting with the port taken: RetroArch says so and plays alone;
        // stopped as its log says so, no ports given meanwhile, and the end
        // says why.
        qputenv("RA_NETPLAY_FAILS", "1");
        QFile::remove(m_log);
        hostEnded.clear();
        QVERIFY(arcade.hostLanGame(QStringLiteral("sf2")));
        QVERIFY(hostEnded.wait(10000) || !hostEnded.isEmpty());
        qunsetenv("RA_NETPLAY_FAILS");
        QCOMPARE(hostEnded.first().value(1).toString(), QStringLiteral("failed"));
        QCOMPARE(hostEnded.first().value(2).toString(), QStringLiteral("Could not host the game: Failed to bind port 55435"));
        QCOMPARE(asking(libretro::claimQuery(1), 300), QByteArray("no answer"));
        // A room no longer hosted.
        QVERIFY(!arcade.joinLanRoom(2));
        QCOMPARE(arcade.lastError(), QStringLiteral("The game is no longer hosted on this network"));

        // Ann plays another game: another room, the Arcade screen told.
        changed.clear();
        answers = { lanAnswer("ann", "mslug", 55435), eve };
        arcade.refreshLanRooms();
        QVERIFY(read.wait(5000));
        QCOMPARE(rooms(), QStringList({ QStringLiteral("3 galaga eve"), QStringLiteral("4 mslug ann") }));
        QCOMPARE(changed.size(), 1);
        // LAN Play turned off and on again: none of the rooms heard before
        // offered until the network has been asked again, each by its id.
        arcade.onSettingChanged(module, QStringLiteral("lan_play"), QStringLiteral("Off"));
        arcade.onSettingChanged(module, QStringLiteral("lan_play"), QStringLiteral("On"));
        QVERIFY(arcade.lanRooms().isEmpty());
        QVERIFY(!arcade.requestEntries(QStringLiteral("lan")).isValid());
        QVERIFY(read.wait(5000));
        QCOMPARE(rooms(), QStringList({ QStringLiteral("3 galaga eve"), QStringLiteral("4 mslug ann") }));
        // No one hosting: the rooms kept through one query they aren't heard
        // in, not a second; then said so in the tree, no filter.
        changed.clear();
        answers.clear();
        arcade.refreshLanRooms();
        QVERIFY(read.wait(5000));
        QCOMPARE(rooms(), QStringList({ QStringLiteral("3 galaga eve"), QStringLiteral("4 mslug ann") }));
        QCOMPARE(changed.size(), 0);
        arcade.refreshLanRooms();
        QVERIFY(read.wait(5000));
        QVERIFY(arcade.lanRooms().isEmpty());
        QCOMPARE(changed.size(), 1);
        QVariantMap message = arcade.requestEntries(QStringLiteral("lan")).toList().value(0).toMap();
        QCOMPARE(message.value(QStringLiteral("kind")).toString(), QStringLiteral("message"));
        QCOMPARE(message.value(QStringLiteral("name")).toString(), QStringLiteral("No LAN Games"));
        QCOMPARE(message.value(QStringLiteral("detail")).toString(),
                 QStringLiteral("No one on this network hosts a game you have: host one from a game's options"));
        // On the Arcade screen too, LAN Games with no game, saying the same.
        QCOMPARE(lanFilter(), 0);
        QCOMPARE(lanSays(), QStringLiteral("No LAN Games"));
        QCOMPARE(arcade.lanStatus().value(QStringLiteral("detail")), message.value(QStringLiteral("detail")));
        // And back: the room heard before, by its id.
        answers = { eve };
        arcade.refreshLanRooms();
        QVERIFY(read.wait(5000));
        QCOMPARE(rooms(), QStringList({ QStringLiteral("3 galaga eve") }));
        // A query that can't be sent: why, where the rooms were, none kept.
        arcade.setLanDiscovery(QStringLiteral("127.0.0.1"), 0);
        arcade.refreshLanRooms();
        QVERIFY(read.wait(5000));
        QVERIFY(arcade.lanRooms().isEmpty());
        message = arcade.requestEntries(QStringLiteral("lan")).toList().value(0).toMap();
        QCOMPARE(message.value(QStringLiteral("name")).toString(), QStringLiteral("Network Not Reachable"));
        QVERIFY2(message.value(QStringLiteral("detail")).toString().startsWith(QStringLiteral("The local network can't be reached: ")),
                 qPrintable(message.value(QStringLiteral("detail")).toString()));
        QCOMPARE(lanSays(), QStringLiteral("Network Not Reachable"));
        // FinalBurn Neo gone: that, first.
        const QString core = libretro::downloadedCore(data.path());
        QVERIFY(QFile::rename(core, core + QStringLiteral(".away")));
        QCOMPARE(lanSays(), QStringLiteral("FinalBurn Neo Not Installed"));
        QVERIFY(QFile::rename(core + QStringLiteral(".away"), core));

        // Off again: nothing of it.
        arcade.onSettingChanged(module, QStringLiteral("lan_play"), QStringLiteral("Off"));
        QVERIFY(arcade.lanRooms().isEmpty());
        QCOMPARE(lanFilter(), -1);
        QVERIFY(!names(arcade.requestEntries(QStringLiteral("top"))).contains(QStringLiteral("LAN Games")));
        QVERIFY(!arcade.statusIcons().contains(QStringLiteral("network")));
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));
    }

    // Our RetroArch's and core's versions, which the lobby's rooms are
    // matched against: still being read, then read; a read too slow asked
    // once more (a core loads faster the second time), and a file whose read
    // was too slow twice not asked again; none while a core that changed is
    // read; one that says its version but doesn't end read all the same; the
    // core not installed.
    void ownVersions() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        writeFile(libretro::downloadedCore(data.path()), "core");
        // The copy of the app that says our core's version: it notes each
        // time it is asked, takes its time before it says it while slow is
        // there, and after it while hang is.
        const QString asked = data.path() + QStringLiteral("/asked");
        const QString slow = data.path() + QStringLiteral("/slow");
        const QString hang = data.path() + QStringLiteral("/hang");
        const QString coreInfo = data.path() + QStringLiteral("/bin/coreinfo");
        writeFile(coreInfo, QStringLiteral("#!/bin/sh\necho >> '%1'\n[ -e '%2' ] && exec sleep 5\n"
                                           "printf 'name=FinalBurn Neo\\nversion=v1.0.0.03 6e7c2a1\\n'\n"
                                           "[ -e '%3' ] && exec sleep 5\nexit 0\n")
                                .arg(asked, slow, hang).toUtf8());
        for (const char *program : { "/bin/retroarch", "/bin/coreinfo" })
            QVERIFY(QFile::setPermissions(data.path() + QLatin1String(program),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        // No lobby to be reached: ours are what is looked at.
        arcade.setLobbyUrl(QUrl(QStringLiteral("http://127.0.0.1:9/list")));
        arcade.setCoreInfoCommand({ coreInfo, QStringLiteral("--core-info") });
        arcade.setVersionReadTimeout(1000);
        const auto ours = [&arcade](const char *key) { return arcade.lobby().value(QLatin1String(key)).toString(); };
        const auto times = [&asked] {
            QFile file(asked);
            return file.open(QIODevice::ReadOnly) ? file.readAll().count('\n') : 0;
        };

        // Too slow: still being read, then not; asked once more, read.
        writeFile(slow);
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("online_play"), QStringLiteral("On"));
        arcade.refreshRooms();
        QCOMPARE(ours("yourCoreState"), QStringLiteral("reading"));
        QTRY_COMPARE_WITH_TIMEOUT(ours("yourCoreState"), QStringLiteral("failed"), 5000);
        QCOMPARE(times(), 1);
        QCOMPARE(ours("yourCore"), QString());
        QTRY_COMPARE_WITH_TIMEOUT(ours("yourRetroArchState"), QStringLiteral("read"), 5000);
        QCOMPARE(ours("yourRetroArch"), QStringLiteral("1.22.2"));
        QVERIFY(QFile::remove(slow));
        arcade.refreshRooms();
        QTRY_COMPARE_WITH_TIMEOUT(ours("yourCoreState"), QStringLiteral("read"), 5000);
        QCOMPARE(ours("yourCore"), QStringLiteral("v1.0.0.03 6e7c2a1"));
        QCOMPARE(times(), 2);
        // Read: not asked again while the core is the same.
        arcade.refreshRooms();
        QTest::qWait(300);
        QCOMPARE(times(), 2);
        // No core: not installed, and none of its version kept.
        QVERIFY(QFile::remove(libretro::downloadedCore(data.path())));
        if (libretro::corePath(data.path()).isEmpty()) {
            QCOMPARE(ours("yourCoreState"), QStringLiteral("missing"));
            QCOMPARE(ours("yourCore"), QString());
        }
        // Another core, too slow twice: none of the one before's version
        // while it is read; asked once more, then no more.
        writeFile(libretro::downloadedCore(data.path()), "another core");
        writeFile(slow);
        arcade.refreshRooms();
        QCOMPARE(ours("yourCoreState"), QStringLiteral("reading"));
        QCOMPARE(ours("yourCore"), QString());
        QTRY_COMPARE_WITH_TIMEOUT(ours("yourCoreState"), QStringLiteral("failed"), 5000);
        QCOMPARE(times(), 3);
        arcade.refreshRooms();
        QCOMPARE(ours("yourCoreState"), QStringLiteral("reading"));
        QTRY_COMPARE_WITH_TIMEOUT(ours("yourCoreState"), QStringLiteral("failed"), 5000);
        QCOMPARE(times(), 4);
        arcade.refreshRooms();
        QTest::qWait(300);
        QCOMPARE(times(), 4);
        QCOMPARE(ours("yourCoreState"), QStringLiteral("failed"));
        // A core that says its version but doesn't end: stopped, read, and
        // not asked again.
        QVERIFY(QFile::remove(slow));
        writeFile(hang);
        writeFile(libretro::downloadedCore(data.path()), "the third core");
        arcade.refreshRooms();
        QTRY_COMPARE_WITH_TIMEOUT(ours("yourCoreState"), QStringLiteral("read"), 5000);
        QCOMPARE(ours("yourCore"), QStringLiteral("v1.0.0.03 6e7c2a1"));
        QCOMPARE(times(), 5);
        arcade.refreshRooms();
        QTest::qWait(300);
        QCOMPARE(times(), 5);
        // Another RetroArch: none of the one before's version while it is
        // read, then its own.
        writeFile(data.path() + QStringLiteral("/bin/retroarch"),
                  "#!/bin/sh\nsleep 0.3\necho 'Version: 1.22.3 (Git 0a1b2c3)'\n");
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/retroarch"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        arcade.refreshRooms();
        QCOMPARE(ours("yourRetroArchState"), QStringLiteral("reading"));
        QCOMPARE(ours("yourRetroArch"), QString());
        QTRY_COMPARE_WITH_TIMEOUT(ours("yourRetroArch"), QStringLiteral("1.22.3"), 5000);
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
        // No filter for the Arcade screen, RetroArch Lobby with Online Play on
        // included: it says why there are no games.
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("online_play"), QStringLiteral("On"));
        QVERIFY(arcade.filters().isEmpty());
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

        // With FinalBurn Neo: a game it has plays in it, left to Auto; MAME
        // chosen for it is MAME all the same, the game saying MAME is not
        // installed rather than playing in FinalBurn Neo against the
        // choice, and the choice saying so too.
        if (!libretro::corePath(data.path()).isEmpty() || !libretro::datPath(data.path()).isEmpty())
            QSKIP("FinalBurn Neo is installed here");
        writeFile(arcade.gamesFolder() + QStringLiteral("/sf2.zip"));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/retroarch"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        writeFile(libretro::downloadedCore(data.path()));
        writeFile(libretro::downloadedDat(data.path()), fixture("fbneo-arcade.dat"));
        refreshed(arcade);
        QVERIFY(arcade.fbneoReady());
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("fbneo"));
        const auto labels = [](const QVariantList &choices) {
            QStringList list;
            for (const QVariant &c : choices)
                list << c.toMap().value(QStringLiteral("label")).toString()
                            + (c.toMap().value(QStringLiteral("current")).toBool() ? QStringLiteral(" *") : QString());
            return list;
        };
        QCOMPARE(labels(arcade.engineChoices(QStringLiteral("sf2"))),
                 QStringList({ QStringLiteral("Auto (FinalBurn Neo) *"), QStringLiteral("MAME (Not Installed)"),
                               QStringLiteral("FinalBurn Neo") }));
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("engines"),
                                QVariantMap{ { QStringLiteral("sf2"), QStringLiteral("mame") } });
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));
        QCOMPARE(labels(arcade.engineChoices(QStringLiteral("sf2"))),
                 QStringList({ QStringLiteral("Auto (FinalBurn Neo)"), QStringLiteral("MAME (Not Installed) *"),
                               QStringLiteral("FinalBurn Neo") }));
        QVERIFY(!arcade.play(QStringLiteral("sf2")));
        QCOMPARE(arcade.lastError(), QStringLiteral("MAME is not installed"));
        // FinalBurn Neo chosen for one it hasn't falls to MAME, which says so.
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("engines"),
                                QVariantMap{ { QStringLiteral("pacman"), QStringLiteral("fbneo") } });
        QCOMPARE(arcade.engineOf(QStringLiteral("pacman")), QStringLiteral("mame"));
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
            QStringLiteral("years/1990s 1990s 4"), QStringLiteral("unknown Not Recognized 2"),
            QStringLiteral("search Search 8") }));

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

        // What a list says with no game on it, for the screen; the others say
        // nothing of their own.
        QCOMPARE(arcade.filterStatus(QStringLiteral("favorites")),
                 QVariantMap({ { QStringLiteral("name"), QStringLiteral("No Favorites") },
                               { QStringLiteral("detail"),
                                 QStringLiteral("Hold select on a game and choose Add to Favorites: it comes here") } }));
        QCOMPARE(arcade.filterStatus(QStringLiteral("recent")).value(QStringLiteral("name")).toString(),
                 QStringLiteral("No Games Played Yet"));
        QVERIFY(arcade.filterStatus(QStringLiteral("all")).isEmpty());
        QVERIFY(arcade.filterStatus(QStringLiteral("genres/Fighting")).isEmpty());

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
                                    QStringLiteral("achievements"), QStringLiteral("netplay"),
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
        QCOMPARE(arcade.filters().size(), 14);
    }

    // Search, the Arcade screen's last filter, as its words are typed: its
    // own row, then the games whose names or sets have every word in them,
    // case, accents, signs and spaces aside; those whose name starts with
    // the words, or that are the set typed, first, then those with a word
    // starting with the first, then the rest, each in the list's order.
    // Every game for no words. Its own row named after the words. The tree's
    // Search finds the same, and nothing for no words. A game gone from the
    // folder isn't found.
    void search() {
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);
        const auto found = [&arcade](const QString &words) {
            const int rows = arcade.setSearch(words);
            const QVariantList games = arcade.filterGames(QStringLiteral("search"), 0, 100);
            QStringList sets;
            for (qsizetype i = 1; i < games.size(); ++i)
                sets << games.at(i).toMap().value(QStringLiteral("set")).toString();
            return rows == games.size() ? sets
                                        : QStringList{ QStringLiteral("%1 rows, %2 listed").arg(rows).arg(games.size()) };
        };
        const QStringList every{ QStringLiteral("acefruit"), QStringLiteral("galaga"), QStringLiteral("kinst"),
                                 QStringLiteral("mslug"), QStringLiteral("pacman"), QStringLiteral("sf2"),
                                 QStringLiteral("xmen6p") };
        QCOMPARE(found(QString()), every);
        QCOMPARE(found(QStringLiteral("!!!")), every);
        // Its own row first, then the games: named after the words, Search
        // for none.
        const auto own = [&arcade] { return arcade.filterGames(QStringLiteral("search"), 0, 1).value(0).toMap(); };
        QVERIFY(own().value(QStringLiteral("search")).toBool());
        QCOMPARE(own().value(QStringLiteral("name")).toString(), QStringLiteral("!!!"));
        arcade.setSearch(QString());
        QCOMPARE(own().value(QStringLiteral("name")).toString(), QStringLiteral("Search"));
        // Street Fighter II's name starts with it, Metal Slug has a word that
        // does, Ace (Cassette), Killer Instinct and X-Men (6 Players) have it
        // in a word.
        QCOMPARE(found(QStringLiteral("s")), QStringList({ QStringLiteral("sf2"), QStringLiteral("mslug"),
                                                           QStringLiteral("acefruit"), QStringLiteral("kinst"),
                                                           QStringLiteral("xmen6p") }));
        QCOMPARE(arcade.search(), QStringLiteral("s"));
        QCOMPARE(arcade.filterIndex(QStringLiteral("search"), QStringLiteral("mslug")), 2);
        bool listed = false;
        for (const QVariant &v : arcade.filters()) {
            const QVariantMap filter = v.toMap();
            if (filter.value(QStringLiteral("id")).toString() == QLatin1String("search")) {
                QCOMPARE(filter.value(QStringLiteral("count")).toInt(), 6);
                listed = true;
            }
        }
        QVERIFY(listed);
        for (const char *words : { "PAC-MAN", "pac man", "pacman", "Pac" })
            QCOMPARE(found(QLatin1String(words)), QStringList({ QStringLiteral("pacman") }));
        QCOMPARE(found(QStringLiteral("fighter street")), QStringList({ QStringLiteral("sf2") }));
        QCOMPARE(found(QStringLiteral("fighter s")), QStringList({ QStringLiteral("sf2") }));
        QCOMPARE(found(QStringLiteral("SF2")), QStringList({ QStringLiteral("sf2") }));
        QCOMPARE(found(QStringLiteral("vehicle 001")), QStringList({ QStringLiteral("mslug") }));
        QCOMPARE(found(QStringLiteral("Ácé")), QStringList({ QStringLiteral("acefruit") }));
        QVERIFY(found(QStringLiteral("zzz")).isEmpty());
        // The tree's.
        QCOMPARE(names(arcade.requestEntries(QStringLiteral("search/fighter street"))),
                 QStringList({ QStringLiteral("Street Fighter II: The World Warrior") }));
        QVERIFY(arcade.requestEntries(QStringLiteral("search/!!!")).toList().isEmpty());
        // A game gone from the folder: the games as they are now.
        const auto restore = qScopeGuard([this] { writeFile(m_games + QStringLiteral("/acefruit.zip")); });
        QVERIFY(QFile::remove(m_games + QStringLiteral("/acefruit.zip")));
        refreshed(arcade);
        QCOMPARE(found(QStringLiteral("s")), QStringList({ QStringLiteral("sf2"), QStringLiteral("mslug"),
                                                           QStringLiteral("kinst"), QStringLiteral("xmen6p") }));
        // "pac man" run together is Pac-Man's name: it first, then Ms.
        // Pac-Man, with a word starting with "pac".
        const auto ms = qScopeGuard([this] { QFile::remove(m_games + QStringLiteral("/mspacman.zip")); });
        writeFile(m_games + QStringLiteral("/mspacman.zip"));
        refreshed(arcade);
        for (const char *words : { "pac man", "pacman" })
            QCOMPARE(found(QLatin1String(words)), QStringList({ QStringLiteral("pacman"), QStringLiteral("mspacman") }));
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

    // Settings → Arcade → Hide Hacks and Bootlegs: the sets MAME calls a
    // bootleg or a hack, in their name's parentheses or as their maker, leave
    // every list and filter, Favorites and Recently Played too, as it is
    // turned on, and are back as it is turned off; Not Recognized is as it
    // was, and a hidden set keeps its details.
    void hidesBootlegs() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        for (const char *set : { "sf2.zip", "sf2b.zip", "pacman.zip", "pacmanf.zip", "galaga.zip", "notagame.zip" })
            writeFile(games + QLatin1Char('/') + QLatin1String(set));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/mame"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        });
        const QString module = QStringLiteral("com.osdos.arcade");
        AppCore core(data.path(), data.path());
        core.add_to_list(module, QStringLiteral("favorites"), { { QStringLiteral("path"), QStringLiteral("game:sf2b") } });
        core.add_to_list(module, QStringLiteral("favorites"), { { QStringLiteral("path"), QStringLiteral("game:galaga") } });
        core.add_to_list(module, QStringLiteral("recent"), { { QStringLiteral("path"), QStringLiteral("game:pacmanf") } });

        ArcadeBackend arcade(data.path(), data.path(), &core, nullptr);
        connect(&core, &AppCore::moduleSettingChanged, &arcade, &ArcadeBackend::onSettingChanged);
        refreshed(arcade);
        const auto sets = [&arcade](const char *filter) {
            QStringList list;
            for (const QVariant &v : arcade.filterGames(QLatin1String(filter), 0, 50))
                list << v.toMap().value(QStringLiteral("set")).toString();
            return list;
        };
        const auto count = [&arcade](const char *id) {
            for (const QVariant &v : arcade.filters()) {
                if (v.toMap().value(QStringLiteral("id")).toString() == QLatin1String(id))
                    return v.toMap().value(QStringLiteral("count")).toInt();
            }
            return 0;
        };
        // The tree's lists, by their games' sets.
        const auto treeSets = [&arcade](const char *path) {
            QStringList list;
            for (const QVariant &v : arcade.requestEntries(QLatin1String(path)).toList())
                list << v.toMap().value(QStringLiteral("game")).toString();
            return list;
        };
        // Off: the bootleg and the hack are games like the others, by name.
        QCOMPARE(sets("all"), QStringList({ QStringLiteral("galaga"), QStringLiteral("pacman"), QStringLiteral("pacmanf"),
                                            QStringLiteral("sf2"), QStringLiteral("sf2b") }));
        QCOMPARE(sets("favorites"), QStringList({ QStringLiteral("galaga"), QStringLiteral("sf2b") }));
        QCOMPARE(sets("recent"), QStringList({ QStringLiteral("pacmanf") }));
        // The tree's Favorites as they were added, the latest first.
        QCOMPARE(treeSets("favorites"), QStringList({ QStringLiteral("galaga"), QStringLiteral("sf2b") }));
        QCOMPARE(treeSets("recent"), QStringList({ QStringLiteral("pacmanf") }));
        QCOMPARE(sets("makers/Namco"), QStringList({ QStringLiteral("galaga"), QStringLiteral("pacman"), QStringLiteral("pacmanf") }));
        QCOMPARE(count("unknown"), 1);

        // On: gone from every list and filter at once, Not Recognized as it
        // was; what the set is can still be asked.
        QSignalSpy changed(&arcade, &ArcadeBackend::gamesChanged);
        core.save_setting(module, QStringLiteral("hide_bootlegs"), QStringLiteral("ON"));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(sets("all"), QStringList({ QStringLiteral("galaga"), QStringLiteral("pacman"), QStringLiteral("sf2") }));
        QCOMPARE(count("all"), 3);
        QCOMPARE(sets("favorites"), QStringList({ QStringLiteral("galaga") }));
        QVERIFY(sets("recent").isEmpty());
        QCOMPARE(count("recent"), 0);
        QCOMPARE(treeSets("favorites"), QStringList({ QStringLiteral("galaga") }));
        QVERIFY(treeSets("recent").isEmpty());
        QCOMPARE(sets("makers/Namco"), QStringList({ QStringLiteral("galaga"), QStringLiteral("pacman") }));
        QCOMPARE(count("unknown"), 1);
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2b")).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Street Fighter II: The World Warrior (bootleg, set 1)"));

        // Off again: back where they were.
        core.save_setting(module, QStringLiteral("hide_bootlegs"), QStringLiteral("OFF"));
        QCOMPARE(changed.size(), 2);
        QCOMPARE(sets("all").size(), 5);
        QCOMPARE(sets("favorites"), QStringList({ QStringLiteral("galaga"), QStringLiteral("sf2b") }));
        QCOMPARE(sets("recent"), QStringList({ QStringLiteral("pacmanf") }));
        QCOMPARE(sets("makers/Namco").size(), 3);
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
            QStringLiteral("achievements=RetroAchievements"), QStringLiteral("netplay=Playable Online"),
            QStringLiteral("unknown=Not Recognized"),
            QStringLiteral("GENRES"), QStringLiteral("genres/Beat 'em Up=Beat 'em Up"),
            QStringLiteral("genres/Fighting=Fighting"), QStringLiteral("genres/Shmup=Shmup"),
            QStringLiteral("PLAYERS"), QStringLiteral("players/1=1 Player"), QStringLiteral("players/2=2 Players"),
            QStringLiteral("players/6=6 Players"),
            QStringLiteral("YEARS"), QStringLiteral("years/1980s=1980s"), QStringLiteral("years/1990s=1990s"),
            QStringLiteral("PUBLISHERS"), QStringLiteral("makers/Capcom=Capcom off"), QStringLiteral("makers/Konami=Konami off"),
            QStringLiteral("makers/Namco=Namco off"), QStringLiteral("makers/Nazca=Nazca off"),
            QStringLiteral("makers/Rare=Rare off") }));
        // Favorites and Recently Played with no game on them yet.
        QCOMPARE(ids(arcade.filters()), QStringList({
            QStringLiteral("all"), QStringLiteral("favorites"), QStringLiteral("recent"),
            QStringLiteral("genres/Beat 'em Up"), QStringLiteral("genres/Fighting"),
            QStringLiteral("genres/Shmup"), QStringLiteral("players/1"), QStringLiteral("players/2"),
            QStringLiteral("players/6"), QStringLiteral("years/1980s"), QStringLiteral("years/1990s"),
            QStringLiteral("unknown"), QStringLiteral("search") }));

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
            QStringLiteral("all"), QStringLiteral("favorites"), QStringLiteral("recent"),
            QStringLiteral("genres/Beat 'em Up"), QStringLiteral("genres/Shmup"),
            QStringLiteral("players/1"), QStringLiteral("players/2"), QStringLiteral("players/6"),
            QStringLiteral("years/1980s"), QStringLiteral("years/1990s"), QStringLiteral("makers/Namco"),
            QStringLiteral("search") }));
        QCOMPARE(arcade.filterGames(QStringLiteral("makers/Namco"), 0, 10).size(), 2);
        options.clear();
        arcade.startFilterOptions();
        QCOMPARE(ids(options.at(0).at(1).toList()), QStringList({
            QStringLiteral("all"), QStringLiteral("favorites"), QStringLiteral("recent"), QStringLiteral("achievements"),
            QStringLiteral("netplay"),
            QStringLiteral("genres/Beat 'em Up"), QStringLiteral("genres/Shmup"), QStringLiteral("players/1"),
            QStringLiteral("players/2"), QStringLiteral("players/6"), QStringLiteral("years/1980s"),
            QStringLiteral("years/1990s"), QStringLiteral("makers/Namco") }));

        // A heading's all at once: the players off, Favorites, Recently
        // Played, RetroAchievements and Playable Online too, Favorites off
        // with a game on it as without.
        for (const char *id : { "players/1", "players/2", "players/6", "favorites", "recent", "achievements", "netplay" })
            shown.insert(QLatin1String(id), false);
        core.save_setting(module, QStringLiteral("menu_filters"), shown);
        QCOMPARE(ids(arcade.filters()), QStringList({
            QStringLiteral("all"), QStringLiteral("genres/Beat 'em Up"), QStringLiteral("genres/Shmup"),
            QStringLiteral("years/1980s"), QStringLiteral("years/1990s"), QStringLiteral("makers/Namco"),
            QStringLiteral("search") }));
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
        // With Online Play on too: no RetroArch Lobby with no game to play.
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("On"));
        QCOMPARE(arcade.filters(), filters);
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
