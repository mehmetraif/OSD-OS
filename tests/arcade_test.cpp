#include "AppCore.h"
#include "audio/AudioOutput.h"
#include "input/ControllerProfile.h"
#include "modules/arcade/ArcadeBackend.h"
#include "modules/arcade/FbneoInstaller.h"
#include "modules/arcade/GameSystem.h"
#include "modules/arcade/Http.h"
#include "modules/arcade/LanRooms.h"
#include "modules/arcade/Libretro.h"
#include "modules/arcade/ProgramInstaller.h"
#include "modules/arcade/RecordFile.h"
#include "modules/arcade/Mame.h"
#include "modules/arcade/OwnMode.h"
#include "modules/arcade/RetroAchievements.h"
#include "modules/arcade/RomCheck.h"
#include "modetest/GameMode.h"
#include "util/DisplayHandoff.h"

#include <QCryptographicHash>
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

#include <cstring>

#include <functional>

#include <sys/inotify.h>
#include <unistd.h>

// What happens to a folder's files from here on, as the kernel tells it
// (inotify): the names that come into the folder, and those written to while
// they have that name. A file written whole and then given its name comes in
// without being written to; one written in place is written to under its
// own. fd is -1 where the folder can't be watched.
struct FolderWatch {
    int fd = -1;
    explicit FolderWatch(const QString &folder) : fd(inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) {
        if (fd >= 0 && inotify_add_watch(fd, QFile::encodeName(folder).constData(),
                                         IN_CREATE | IN_MOVED_TO | IN_MODIFY) < 0) {
            ::close(fd);
            fd = -1;
        }
    }
    ~FolderWatch() {
        if (fd >= 0)
            ::close(fd);
    }
    void read(QStringList &cameIn, QStringList &writtenTo) const {
        alignas(inotify_event) char buffer[16 * 1024];
        for (ssize_t size; (size = ::read(fd, buffer, sizeof buffer)) > 0;) {
            for (const char *at = buffer; at < buffer + size;) {
                const auto *event = reinterpret_cast<const inotify_event *>(at);
                const QString name = event->len ? QFile::decodeName(event->name) : QString();
                if (event->mask & (IN_CREATE | IN_MOVED_TO))
                    cameIn << name;
                if (event->mask & IN_MODIFY)
                    writtenTo << name;
                at += sizeof(inotify_event) + event->len;
            }
        }
    }
};

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
    // display card, preloaded library and controller file it notes. mslug and kinst lack files; sf2 can't open its picture;
    // slowgame takes a second, as does asking about a_slowlist half of one;
    // any other game takes $MAME_SECONDS, and -listxml exits $MAME_LIST_LINGER
    // seconds after its answer, -verifyroms $MAME_VERIFY_LINGER after its
    // verdicts.
    static constexpr const char *kFakeMame = R"SH(#!/bin/sh
machine() {
	case "$1" in
	pacman) echo '<machine name="pacman" sourcefile="pacman/pacman.cpp" cloneof="puckman" romof="puckman"><description>Pac-Man (Midway)</description><year>1980</year><manufacturer>Namco (Midway license)</manufacturer><rom name="pacman.6e" size="4096"/><display tag="screen" type="raster" rotate="90" width="288" height="224" refresh="60.606061"/><input players="2" coins="2"><control type="joy" player="1"/></input><dipswitch name="Coinage" tag="DSW1" mask="3"><dipvalue name="Free Play" value="0"/><dipvalue name="1C/1C" value="1" default="yes"/></dipswitch><dipswitch name="Difficulty" tag="DSW1" mask="64"><diplocation name="SW" number="7"/><dipvalue name="Normal" value="64" default="yes"/><dipvalue name="Hard" value="0"/></dipswitch><driver status="good"/></machine>' ;;
	galaga) echo '<machine name="galaga" sourcefile="namco/galaga.cpp"><description>Galaga (Namco rev. B)</description><year>1981</year><manufacturer>Namco</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	mslug) echo '<machine name="mslug" sourcefile="neogeo/neogeo.cpp" romof="neogeo"><description>Metal Slug - Super Vehicle-001</description><year>1996</year><manufacturer>Nazca</manufacturer><display tag="screen" type="raster" rotate="0" width="320" height="224" refresh="59.185606"/><input players="2"/><driver status="good"/></machine>' ;;
	neogeo) echo '<machine name="neogeo" isbios="yes"><description>Neo-Geo MV-6F</description><year>1990</year><manufacturer>SNK</manufacturer></machine>' ;;
	xmen6p) echo '<machine name="xmen6p" sourcefile="konami/xmen.cpp" cloneof="xmen"><description>X-Men (6 Players ver EAB)</description><year>1992</year><manufacturer>Konami</manufacturer><display tag="lscreen" rotate="270"/><display tag="rscreen" rotate="0"/><input players="6"/><driver status="imperfect"/></machine>' ;;
	kinst) echo '<machine name="kinst"><description>Killer Instinct (v1.5d)</description><year>1994</year><manufacturer>Rare / Nintendo</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	sf2) echo '<machine name="sf2" sourcefile="capcom/cps1.cpp"><description>Street Fighter II: The World Warrior</description><year>1991</year><manufacturer>Capcom</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	sf2b) echo '<machine name="sf2b" sourcefile="capcom/cps1.cpp" cloneof="sf2" romof="sf2"><description>Street Fighter II: The World Warrior (bootleg, set 1)</description><year>1991</year><manufacturer>bootleg</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	sf2ua) echo '<machine name="sf2ua" sourcefile="capcom/cps1.cpp" cloneof="sf2" romof="sf2"><description>Street Fighter II: The World Warrior (USA 910206)</description><year>1991</year><manufacturer>Capcom</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	sf2j) echo '<machine name="sf2j" sourcefile="capcom/cps1.cpp" cloneof="sf2" romof="sf2"><description>Street Fighter II: The World Warrior (Japan 911210)</description><year>1991</year><manufacturer>Capcom</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	s1945) echo '<machine name="s1945" sourcefile="psikyo/psikyo.cpp"><description>Strikers 1945 (Japan / World)</description><year>1995</year><manufacturer>Psikyo</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	pacmanf) echo '<machine name="pacmanf" sourcefile="pacman/pacman.cpp" cloneof="puckman" romof="puckman"><description>Pac-Man (Midway, speedup hack)</description><year>1980</year><manufacturer>Namco (Midway license)</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	mspacman) echo '<machine name="mspacman" sourcefile="pacman/pacman.cpp"><description>Ms. Pac-Man</description><year>1981</year><manufacturer>Midway / General Computer Corporation</manufacturer><input players="2"/><driver status="good"/></machine>' ;;
	acefruit) echo '<machine name="acefruit"><description>Ace (Cassette)</description><year>1982?</year><manufacturer>unknown</manufacturer><input players="1"/><driver status="preliminary"/></machine>' ;;
	*) return 1 ;;
	esac
}
# Check ROMs: -rompath, then -verifyroms and the sets. mslug lacks files
# (one with no good dump known, which doesn't count), sf2 has one wrong,
# galaga none of its files at all (MAME then says nothing of it), pacman
# is as good as known dumps make it, any other is good.
if [ "$1" = "-rompath" ] && [ "$3" = "-verifyroms" ]; then
	echo "verifyroms $2" >> "$MAME_LOG"
	shift 3
	echo "verifyroms $*" >> "$MAME_LOG"
	bad=0
	for name in "$@"; do
		case "$name" in
		mslug)
			echo "mslug       : sp-s2.sp1 (131072 bytes) - NOT FOUND"
			echo "mslug       : pal.bin - NOT FOUND - NO GOOD DUMP KNOWN"
			echo "mslug       : 000-lo.lo (131072 bytes) - NOT FOUND"
			echo "romset mslug [neogeo] is bad"
			bad=1 ;;
		sf2)
			echo "sf2         : sf2_30.11e (5 bytes) - INCORRECT CHECKSUM:"
			echo "EXPECTED: CRC(01234567) SHA1(0000000000000000000000000000000000000000)"
			echo "   FOUND: CRC(76543210) SHA1(1111111111111111111111111111111111111111)"
			echo "romset sf2 is bad"
			bad=1 ;;
		galaga) ;;
		pacman) echo "romset pacman [puckman] is best available" ;;
		*) echo "romset $name is good" ;;
		esac
	done
	echo "$# romsets found"
	[ -n "$MAME_VERIFY_LINGER" ] && sleep "$MAME_VERIFY_LINGER"
	[ "$bad" = 1 ] && exit 2
	exit 0
fi
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
	[ -n "$MAME_LIST_LINGER" ] && sleep "$MAME_LIST_LINGER"
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
	echo "alsa ${ALSA_CARD:-}"
	echo "preload ${LD_PRELOAD:-}"
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
leftover)
	# A helper it starts, as a sound library starts a sound server, left
	# running in its process group when it exits.
	sleep 60 &
	echo "helper $!" >> "$MAME_LOG"
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

    // The Arcade module's list of the games of five buttons or more, as it
    // ships in modules/arcade (scripts/make-five-button-sets.py): a stand-in
    // in the app's folder, before its ArcadeBackend first plays.
    void writeFiveButtonSets(const QString &appRoot, const QByteArray &sets) {
        writeFile(appRoot + QStringLiteral("/modules/arcade/five-button-sets.txt"), "# MAME 0.000: a stand-in\n" + sets);
    }
    // A player on Game Buttons' Auto, as InputManager gives one: Neo Geo
    // 4's row (A B X Y LB RB LT RT) and Fighting 6's (X Y RB over A B RT),
    // on a pad whose triggers are axes.
    static controllers::ArcadeBinds autoBinds() {
        controllers::ArcadeBinds binds;
        binds.automatic = true;
        binds.mame = QStringList({ "BUTTON1", "BUTTON2", "BUTTON3", "BUTTON4", "BUTTON5", "BUTTON6",
                                   "SLIDER1_NEG_SWITCH", "SLIDER2_NEG_SWITCH", "SELECT", "START" });
        binds.retroarch = QStringList({ "0", "1", "2", "3", "9", "10", "+4", "+5", "4", "6" });
        binds.mameMany = QStringList({ "BUTTON3", "BUTTON4", "BUTTON6", "BUTTON1", "BUTTON2",
                                       "SLIDER2_NEG_SWITCH", "BUTTON5", "SLIDER1_NEG_SWITCH", "SELECT", "START" });
        binds.retroarchMany = QStringList({ "2", "3", "10", "0", "1", "+5", "9", "+4", "4", "6" });
        return binds;
    }

    static QByteArray contents(const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    // A player's pad in RetroArch's config as this system's RetroArch is
    // given it: by its vendor and product where RetroArch reads them
    // (libretro::kPadsByVendor, Linux), else by its name.
    static QString reservedLine(int player, const QString &vendorAndName) {
        const QString device = libretro::kPadsByVendor ? vendorAndName : vendorAndName.section(QLatin1Char(' '), 1);
        return QStringLiteral("input_player%1_reserved_device = \"%2\"").arg(player).arg(device);
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

    // The folder read, or looked at and found as it was (lookAtNames()),
    // and MAME asked about what it hadn't been.
    static void refreshed(ArcadeBackend &arcade) {
        arcade.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.looking() && !arcade.scanning(), 10000);
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
    // The credits put in a FinalBurn Neo game: each coin held 100 ms and
    // let go 200 ms, from the first on; RetroArch's message for its network
    // RetroPad, struct remote_message as the machine lays it out.
    void coinFeed() {
        bool done = false;
        QVERIFY(libretro::coinDown(0, 2, &done) && !done);
        QVERIFY(libretro::coinDown(99, 2, &done) && !done);
        QVERIFY(!libretro::coinDown(100, 2, &done) && !done);
        QVERIFY(!libretro::coinDown(299, 2, &done) && !done);
        QVERIFY(libretro::coinDown(300, 2, &done) && !done);
        QVERIFY(!libretro::coinDown(400, 2, &done) && !done);
        QVERIFY(!libretro::coinDown(600, 2, &done) && done);
        QVERIFY(!libretro::coinDown(0, 0, &done) && done);
        for (const bool down : { true, false }) {
            const QByteArray message = libretro::remotePadMessage(libretro::kRetroPadSelect, down);
            QCOMPARE(message.size(), 20);
            qint32 fields[4];
            quint16 state = 7;
            std::memcpy(fields, message.constData(), sizeof fields);
            std::memcpy(&state, message.constData() + sizeof fields, sizeof state);
            QCOMPARE(fields[0], 0);
            QCOMPARE(fields[1], 1);   // RETRO_DEVICE_JOYPAD
            QCOMPARE(fields[2], 0);
            QCOMPARE(fields[3], 2);   // RETRO_DEVICE_ID_JOYPAD_SELECT
            QCOMPARE(state, quint16(down ? 1 : 0));
            QCOMPARE(message.mid(18), QByteArray(2, '\0'));
        }
    }

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
        // A monitor on its side: the picture turned against it, a vertical
        // game's lines (its height before the rotation) lying along the
        // screen's and drawn whole, a horizontal game's width across them.
        const QStringList left{ QStringLiteral("-rol") };
        const QStringList right{ QStringLiteral("-ror") };
        QCOMPARE(mame::screenArguments(vertical, 240, QStringLiteral("4:3"), true, false, QStringLiteral("Turned Right")),
                 left + shape + whole);
        QCOMPARE(mame::screenArguments(vertical, 240, QStringLiteral("4:3"), true, false, QStringLiteral("Turned Left")),
                 right + shape + whole);
        QCOMPARE(mame::screenArguments(wide, 240, QStringLiteral("4:3"), true, false, QStringLiteral("Turned Right")),
                 left + shape);
        QCOMPARE(mame::screenArguments(wide, 480, QStringLiteral("4:3"), true, false, QStringLiteral("Turned Right")),
                 left + shape + whole);
        QCOMPARE(mame::screenArguments(wide, 240, QStringLiteral("4:3"), true, true, QStringLiteral("Turned Left")),
                 right + QStringList({ QStringLiteral("-nokeepaspect") }));
        QCOMPARE(mame::screenArguments(vertical, 240, QStringLiteral("4:3"), true, false, QStringLiteral("Horizontal")),
                 shape);
        // Fit to Screen: stretched over the whole screen, whatever else.
        QCOMPARE(mame::screenArguments(wide, 240, QStringLiteral("4:3"), true, true),
                 QStringList({ QStringLiteral("-nokeepaspect") }));
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
            "<display tag=\"l\" type=\"raster\" rotate=\"270\" width=\"288\" height=\"224\" refresh=\"59.185606\" pixclock=\"6000000\""
            " htotal=\"384\" hbend=\"0\" hbstart=\"288\" vtotal=\"264\" vbend=\"16\" vbstart=\"240\"/>"
            "<display tag=\"r\" rotate=\"0\" width=\"320\" height=\"240\" refresh=\"60.000000\"/>"
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
            // Its first screen's refresh rate, as MAME writes it, and how
            // many screens it has.
            QCOMPARE(x.refresh, 59.185606);
            QCOMPARE(x.screens, 2);
            QCOMPARE(x.status, QStringLiteral("imperfect"));
            QVERIFY(x.isGame());
            QVERIFY(machines.at(1).bios && !machines.at(1).isGame());
            QVERIFY(machines.at(2).device && !machines.at(2).runnable);
            QCOMPARE(machines.at(1).refresh, 0.0);
            QCOMPARE(machines.at(1).screens, 0);
            // As the cache keeps it.
            const mame::Machine kept = mame::Machine::fromVariant(x.name, x.toVariant());
            QCOMPARE(kept.description, x.description);
            QCOMPARE(kept.sourceFile, x.sourceFile);
            QCOMPARE(kept.players, 6);
            QCOMPARE(kept.rotate, 270);
            QCOMPARE(kept.width, 288);
            QCOMPARE(kept.height, 224);
            QCOMPARE(kept.refresh, 59.185606);
            QCOMPARE(kept.screens, 2);
            const mame::Machine bios = mame::Machine::fromVariant(QStringLiteral("neogeo"), machines.at(1).toVariant());
            QVERIFY(bios.bios);
            QCOMPARE(bios.screens, 0);
            // One screen goes unsaid, and comes back as one: a rate kept
            // through the cache's JSON to the last digit MAME gave.
            mame::Machine one = x;
            one.screens = 1;
            QVERIFY(!one.toVariant().contains(QStringLiteral("screens")));
            const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(one.toVariant())).toJson(QJsonDocument::Compact);
            const mame::Machine read = mame::Machine::fromVariant(one.name, QJsonDocument::fromJson(json).object().toVariantMap());
            QCOMPARE(read.screens, 1);
            QCOMPARE(read.refresh, 59.185606);
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
        writeFiveButtonSets(m_data.path(), "sf2\nsf2ce\n");
        const auto noSets = qScopeGuard([this] { QFile::remove(m_data.path() + QStringLiteral("/modules/arcade/five-button-sets.txt")); });
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
                           " -ctrlrpath %2/ctrlr -ctrlr osdos -skip_gameinfo -comm_localhost 127.0.0.1"
                           " -samplepath %1/samples"
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
        // Player 2 on Auto: a game of up to four buttons (Pac-Man, not in
        // the module's list) on Neo Geo 4's row, Button 1 its A and Button 4
        // its Y; one of five or more (Street Fighter II, in it, which the
        // stand-in MAME doesn't play: its controller file is written all the
        // same) on Fighting 6's, Button 1 its X and Button 4 its A.
        controllers::setPlayerArcade({ {}, autoBinds() });
        const auto joycodes = [&home](const char *port) {
            const QRegularExpression re(QStringLiteral("<port type=\"%1\">\\s*<newseq type=\"standard\">[^<]*"
                                                       "(JOYCODE_2_[A-Z0-9_]+)</newseq>").arg(QLatin1String(port)));
            const QRegularExpressionMatch m = re.match(QString::fromUtf8(contents(home + QStringLiteral("/ctrlr/osdos.cfg"))));
            return m.hasMatch() ? m.captured(1) : QStringLiteral("<no port>");
        };
        QFile::remove(m_log);
        QCOMPARE(played(arcade, QStringLiteral("pacman")).at(1).toString(), QStringLiteral("ok"));
        QCOMPARE(joycodes("P2_BUTTON1"), QStringLiteral("JOYCODE_2_BUTTON1"));
        QCOMPARE(joycodes("P2_BUTTON4"), QStringLiteral("JOYCODE_2_BUTTON4"));
        QCOMPARE(joycodes("COIN2"), QStringLiteral("JOYCODE_2_SELECT"));
        QFile::remove(m_log);
        played(arcade, QStringLiteral("sf2"));
        QCOMPARE(joycodes("P2_BUTTON1"), QStringLiteral("JOYCODE_2_BUTTON3"));
        QCOMPARE(joycodes("P2_BUTTON4"), QStringLiteral("JOYCODE_2_BUTTON1"));
        QCOMPARE(joycodes("P2_BUTTON6"), QStringLiteral("JOYCODE_2_SLIDER2_NEG_SWITCH"));
        QCOMPARE(joycodes("COIN2"), QStringLiteral("JOYCODE_2_SELECT"));
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
        // MAME Vsync on Auto: each frame waits for the screen on a Pi 4 or 5
        // drawing through KMS (the test runs without a desktop), never
        // -syncrefresh; elsewhere as before, unless turned on; off where
        // turned off.
        const auto vsync = [&](board::Family family, const QVariant &choice) {
            arcade.setBoard(family);
            arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("mame_vsync"), choice);
            QFile::remove(m_log);
            played(arcade, QStringLiteral("galaga"));
            const QString line = logLines(QStringLiteral("play")).value(0);
            return !line.contains(QStringLiteral("-syncrefresh"))
                   && line.endsWith(QStringLiteral(" -video soft -waitvsync"));
        };
        QVERIFY(vsync(board::Family::Pi4, QVariant()));
        QVERIFY(vsync(board::Family::Pi5, QStringLiteral("Auto")));
        QVERIFY(!vsync(board::Family::Pi5, QStringLiteral("Off")));
        QVERIFY(!vsync(board::Family::Other, QStringLiteral("Auto")));
        QVERIFY(vsync(board::Family::Other, QStringLiteral("On")));
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("mame_vsync"), QStringLiteral("Off"));
        // A Pi on a 4K TV at 30 Hz whose HDMI lists 1920x1080: MAME asks for
        // it at 60 Hz, and says so in the log; on a 1080p60 screen, or off
        // the Pis, nothing.
        QTemporaryDir drm;
        QDir().mkpath(drm.path() + QStringLiteral("/card0-Composite-1"));
        QDir().mkpath(drm.path() + QStringLiteral("/card1-HDMI-A-1"));
        writeFile(drm.path() + QStringLiteral("/card0-Composite-1/status"), "disconnected\n");
        writeFile(drm.path() + QStringLiteral("/card0-Composite-1/modes"), "720x480i\n");
        writeFile(drm.path() + QStringLiteral("/card1-HDMI-A-1/status"), "connected\n");
        writeFile(drm.path() + QStringLiteral("/card1-HDMI-A-1/modes"), "3840x2160\n1920x1080\n1920x1080i\n");
        arcade.setDrmFolder(drm.path());
        const auto resolution = [&](board::Family family, const QSize &size, double hz) {
            arcade.setBoard(family);
            arcade.setScreen(false, size, hz);
            QFile::remove(m_log);
            played(arcade, QStringLiteral("galaga"));
            const QString line = logLines(QStringLiteral("play")).value(0);
            return line.section(QStringLiteral(" -resolution "), 1).section(QLatin1Char(' '), 0, 0);
        };
        {
            QTest::ignoreMessage(QtInfoMsg, "arcade: the screen is 3840x2160 at 30 Hz: the game asks for 1920x1080 at 60 Hz");
            QCOMPARE(resolution(board::Family::Pi4, QSize(3840, 2160), 30), QStringLiteral("1920x1080@60"));
        }
        QCOMPARE(resolution(board::Family::Pi5, QSize(1920, 1080), 60), QString());
        QCOMPARE(resolution(board::Family::Other, QSize(3840, 2160), 30), QString());
        writeFile(drm.path() + QStringLiteral("/card1-HDMI-A-1/modes"), "3840x2160\n");
        QCOMPARE(resolution(board::Family::Pi5, QSize(3840, 2160), 30), QString());
        arcade.setScreen(false, QSize());
        arcade.setBoard(board::Family::Other);
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("mame_vsync"), QVariant());
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

        // Vertical Games: a monitor turned for a vertical game alone, which
        // Pac-Man is turned against; Metal Slug, horizontal, as it is.
        arcade.onSettingChanged(module, QStringLiteral("vertical_monitor"), QStringLiteral("Turned Right"));
        QVERIFY(args("pacman").endsWith(QStringLiteral(" -rol")));
        line = args("mslug");
        QVERIFY2(!line.contains(QStringLiteral("-rol")) && !line.contains(QStringLiteral("-ror")), qPrintable(line));
        arcade.onSettingChanged(module, QStringLiteral("vertical_monitor"), QStringLiteral("Turned Left"));
        QVERIFY(args("pacman").endsWith(QStringLiteral(" -ror")));
        arcade.onSettingChanged(module, QStringLiteral("vertical_monitor"), QStringLiteral("Horizontal"));
        line = args("pacman");
        QVERIFY2(!line.contains(QStringLiteral("-rol")) && !line.contains(QStringLiteral("-ror")), qPrintable(line));
    }

    // Emulator Video → Game's Own Mode, the choice: a horizontal game of
    // one screen whose lines and refresh rate a 15 kHz arcade monitor takes
    // gets a mode of its own, 1920 across, the very modes osdos-modetest
    // showed on the device; every other game gets none, with why, and stays
    // on the mode the screen is on.
    void ownModeOfAGame() {
        const QString arcade15 = QString::fromLatin1(modetest::kArcade15);
        const auto machine = [](int width, int height, double refresh, int rotate = 0, int screens = 1) {
            mame::Machine m;
            m.width = width;
            m.height = height;
            m.refresh = refresh;
            m.rotate = rotate;
            m.screens = screens;
            return m;
        };
        const auto mode = [&arcade15](const mame::Machine &m, bool turned = false) {
            const mame::OwnMode own = mame::ownModeFor(m, arcade15, turned);
            return own.mode ? QString::fromStdString(modetest::describe(*own.mode)) + QStringLiteral(" | ") + own.why
                            : QStringLiteral("none: ") + own.why;
        };
        // A Neo Geo game, a CPS game, 240 lines at 57.55 Hz, R-Type, a
        // 60 Hz game of 240 lines, and Pac-Man's board with a horizontal
        // game on it: the tool's modes, as it named them on the device.
        QCOMPARE(mode(machine(320, 224, 59.185606)),
                 QStringLiteral("1920x224  15.684 kHz  59.19 Hz  39.132 MHz | its 320x224 at 59.185606 Hz"));
        QCOMPARE(mode(machine(384, 224, 59.637405)),
                 QStringLiteral("1920x224  15.625 kHz  59.64 Hz  38.953 MHz | its 384x224 at 59.637405 Hz"));
        QCOMPARE(mode(machine(320, 240, 57.55)),
                 QStringLiteral("1920x240  15.654 kHz  57.55 Hz  39.024 MHz | its 320x240 at 57.550000 Hz"));
        QCOMPARE(mode(machine(384, 256, 55.017606)),
                 QStringLiteral("1920x256  15.680 kHz  55.02 Hz  39.122 MHz | its 384x256 at 55.017606 Hz"));
        QCOMPARE(mode(machine(320, 240, 60.0)),
                 QStringLiteral("1920x240  15.660 kHz  60.00 Hz  39.040 MHz | its 320x240 at 60.000000 Hz"));
        QCOMPARE(mode(machine(288, 224, 60.606061)),
                 QStringLiteral("1920x224  15.636 kHz  60.61 Hz  38.981 MHz | its 288x224 at 60.606061 Hz"));
        // Its width changes nothing: the mode is 1920 across for all.
        QCOMPARE(mode(machine(256, 224, 59.185606)).section(QLatin1Char('|'), 0, 0),
                 mode(machine(512, 224, 59.185606)).section(QLatin1Char('|'), 0, 0));
        // A game flipped (rotate 180) is horizontal still; PAL's 288 lines
        // at 50 Hz fit.
        QVERIFY(mode(machine(320, 224, 59.185606, 180)).startsWith(QStringLiteral("1920x224 ")));
        QVERIFY2(mode(machine(384, 288, 50.0)).startsWith(QStringLiteral("1920x288  15.6")), qPrintable(mode(machine(384, 288, 50.0))));

        // A vertical game, either way up.
        QCOMPARE(mode(machine(288, 224, 60.606061, 90)), QStringLiteral("none: a vertical game"));
        QCOMPARE(mode(machine(288, 224, 60.606061, 270)), QStringLiteral("none: a vertical game"));
        // A monitor turned on its side for the game.
        QCOMPARE(mode(machine(320, 224, 59.185606), true), QStringLiteral("none: the monitor is turned on its side for it"));
        // No figures (a vector game, a set MAME gave no display of), and a
        // size without a rate (FinalBurn Neo's list alone).
        QCOMPARE(mode(machine(0, 0, 0)), QStringLiteral("none: no screen size and refresh rate is known for it"));
        QCOMPARE(mode(machine(0, 0, 60.0)), QStringLiteral("none: no screen size and refresh rate is known for it"));
        QCOMPARE(mode(machine(384, 224, 0)),
                 QStringLiteral("none: no refresh rate is known for it (MAME gives them, and has none for this set)"));
        // More than a screen.
        QCOMPARE(mode(machine(320, 224, 59.185606, 0, 2)), QStringLiteral("none: it has 2 screens"));
        QCOMPARE(mode(machine(288, 224, 60.0, 0, 3)), QStringLiteral("none: it has 3 screens"));
        // A 31 kHz game's 480 lines (and an interlaced game's), a 25 kHz
        // game's 384, and too few lines for the monitor.
        QCOMPARE(mode(machine(640, 480, 60.0)), QStringLiteral("none: its 480 lines are outside the monitor's 192 to 288"));
        QCOMPARE(mode(machine(512, 384, 60.0)), QStringLiteral("none: its 384 lines are outside the monitor's 192 to 288"));
        QCOMPARE(mode(machine(256, 160, 60.0)), QStringLiteral("none: its 160 lines are outside the monitor's 192 to 288"));
        // Lines and a rate that together need more than 16.2 kHz, and a
        // rate no such monitor holds.
        QCOMPARE(mode(machine(256, 256, 60.0)),
                 QStringLiteral("none: its 256 lines at 60.00 Hz need a line rate above the monitor's 16.200 kHz"));
        QCOMPARE(mode(machine(640, 240, 30.0)), QStringLiteral("none: its 30.00 Hz is outside the monitor's 49.50 to 65.00 Hz"));
        // No monitor named by the display output, or a line that isn't one.
        QCOMPARE(mame::ownModeFor(machine(320, 224, 59.185606), QString(), false).why,
                 QStringLiteral("the display output names no monitor's range"));
        QVERIFY(!mame::ownModeFor(machine(320, 224, 59.185606), QString(), false).mode);
        const mame::OwnMode bad = mame::ownModeFor(machine(320, 224, 59.185606), QStringLiteral("arcade_15"), false);
        QVERIFY(!bad.mode);
        QVERIFY2(bad.why.startsWith(QStringLiteral("the display output's monitor range isn't one (")), qPrintable(bad.why));

        // What MAME is told on such a mode: the screen's shape as a tube's,
        // the lines whole, no filter; Fit to Screen over the whole of it;
        // BGFX's own word for no filter.
        const mame::Machine mslug = machine(320, 224, 59.185606);
        const auto told = [&mslug](const char *shape, bool fill, const char *video) {
            return mame::ownModeArguments(mslug, 224, QLatin1String(shape), fill, QLatin1String(video)).join(QLatin1Char(' '));
        };
        QCOMPARE(told("4:3", false, ""), QStringLiteral("-aspect 4:3 -nounevenstretch -unevenstretchx -nofilter"));
        QCOMPARE(told("", false, "opengl"), QStringLiteral("-aspect 4:3 -nounevenstretch -unevenstretchx -nofilter"));
        QCOMPARE(told("16:9", false, "soft"), QStringLiteral("-aspect 16:9 -nounevenstretch -unevenstretchx -nofilter"));
        QCOMPARE(told("4:3", true, ""), QStringLiteral("-nokeepaspect -nofilter"));
        QCOMPARE(told("4:3", false, "bgfx"),
                 QStringLiteral("-aspect 4:3 -nounevenstretch -unevenstretchx -nofilter -bgfx_screen_chains unfiltered"));
        for (const char *never : { "-syncrefresh", "-switchres", "-resolution" })
            QVERIFY2(!told("4:3", false, "bgfx").contains(QLatin1String(never)), never);
    }

    // Game's Own Mode in MAME: off, and on where it can do nothing, a game
    // starts with the very arguments it has without it and the screen is
    // never asked for a mode; on a Raspberry Pi 5 with a 15 kHz arcade
    // monitor's display output, a horizontal game's mode is asked for, and
    // set, MAME is told of a screen of its lines, unfiltered, and preloaded
    // with osdos_keepmode.so; refused by the kernel, or for a game that
    // gets none, as without the setting. The log has a line each time.
    void ownModeInMame() {
        const QString module = QStringLiteral("com.osdos.arcade");
        DisplayHandoff handoff;
        QStringList asked;
        bool take = true;
        handoff.setModeSetter([&](const modetest::ModeLine &m, QString *why) {
            asked << QString::fromStdString(modetest::describe(m));
            if (!take)
                *why = QStringLiteral("the kernel refused it: Invalid argument");
            return take;
        });
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, &handoff);
        refreshed(arcade);
        // JAMMA 720x288, the preset the tool was run on.
        arcade.setScreen(true, QSize(720, 288), 50);
        arcade.setKeepModeLibrary(QStringLiteral(KEEPMODE_LIBRARY));
        const auto restore = qScopeGuard([&arcade, module] {
            arcade.onSettingChanged(module, QStringLiteral("own_video_mode"), QStringLiteral("OFF"));
            arcade.onSettingChanged(module, QStringLiteral("game_picture"), QStringLiteral("Original"));
        });
        // What MAME was started with, and what was preloaded into it.
        const auto run = [this, &arcade](const char *set) {
            QFile::remove(m_log);
            played(arcade, QLatin1String(set));
            return logLines(QStringLiteral("play ")).value(0) + QLatin1Char('\n') + logLines(QStringLiteral("preload ")).value(0);
        };
        const auto setting = [&arcade, module](bool on) {
            arcade.onSettingChanged(module, QStringLiteral("own_video_mode"), on ? QStringLiteral("ON") : QStringLiteral("OFF"));
        };
        const auto capabilities = [&arcade] {
            QSignalSpy options(&arcade, &ArcadeBackend::dynamicOptionsReady);
            arcade.machineCapabilities();
            return options.value(0).value(1).toStringList();
        };
        const QString arcade15 = QString::fromLatin1(modetest::kArcade15);
        const QByteArray line = "[Arcade] Game's own mode for mslug in MAME: ";

        // Where it can do nothing: any machine but a Pi 5, a display output
        // that names no monitor, and no osdos_keepmode.so. The row's
        // capability isn't there, the arguments are those without the
        // setting, and the screen is never asked.
        struct Where { board::Family family; QString monitor; QString library; const char *why; };
        const Where nowhere[] = {
            { board::Family::Other, arcade15, QStringLiteral(KEEPMODE_LIBRARY), "only on a Raspberry Pi 5" },
            { board::Family::Pi4, arcade15, QStringLiteral(KEEPMODE_LIBRARY), "only on a Raspberry Pi 5" },
            { board::Family::Pi5, QString(), QStringLiteral(KEEPMODE_LIBRARY),
              "only on a display output for a 15 kHz arcade monitor" },
            { board::Family::Pi5, arcade15, m_data.path() + QStringLiteral("/no-such.so"),
              "osdos_keepmode.so is not beside the app" },
        };
        for (const Where &where : nowhere) {
            arcade.setBoard(where.family);
            arcade.setMonitor(where.monitor);
            arcade.setKeepModeLibrary(where.library);
            QCOMPARE(arcade.ownModeUnavailable(), QLatin1String(where.why));
            QVERIFY(!capabilities().contains(QStringLiteral("own_video_mode")));
            setting(false);
            const QString off = run("mslug");
            setting(true);
            QTest::ignoreMessage(QtInfoMsg, (line + "none, " + where.why).constData());
            QCOMPARE(run("mslug"), off);
            QVERIFY(off.endsWith(QStringLiteral("\npreload ")));
        }
        QVERIFY(asked.isEmpty());

        // Where it can: a Pi 5 on a JAMMA preset's monitor.
        arcade.setBoard(board::Family::Pi5);
        arcade.setMonitor(arcade15);
        arcade.setKeepModeLibrary(QStringLiteral(KEEPMODE_LIBRARY));
        QCOMPARE(arcade.ownModeUnavailable(), QString());
        QCOMPARE(capabilities(), QStringList({ QStringLiteral("raspberry_pi"), QStringLiteral("own_video_mode") }));
        // Off: nothing asked, nothing said, nothing preloaded.
        setting(false);
        const QString off = run("mslug");
        QVERIFY2(off.endsWith(QStringLiteral(" -waitvsync -aspect 4:3 -nounevenstretch -unevenstretchx\npreload ")), qPrintable(off));
        QVERIFY(asked.isEmpty());

        // On: Metal Slug's 224 lines at 59.19 Hz.
        setting(true);
        QTest::ignoreMessage(QtInfoMsg, (line + "1920x224  15.684 kHz  59.19 Hz  39.132 MHz asked for, from its 320x224 "
                                                "at 59.185606 Hz").constData());
        const QString on = run("mslug");
        QCOMPARE(asked, QStringList({ QStringLiteral("1920x224  15.684 kHz  59.19 Hz  39.132 MHz") }));
        QCOMPARE(on, off.section(QLatin1Char('\n'), 0, 0) + QStringLiteral(" -nofilter\npreload ")
                         + QStringLiteral(KEEPMODE_LIBRARY));
        for (const char *never : { "-syncrefresh", "-switchres", "-resolution" })
            QVERIFY2(!on.contains(QLatin1String(never)), never);
        QVERIFY(!handoff.held() && !handoff.modeSet());

        // The kernel refuses the mode: the game all the same, as without.
        take = false;
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^\\[Arcade\\] Game's own mode for mslug in MAME: 1920x224 ")));
        QCOMPARE(run("mslug"), off);
        QCOMPARE(asked.size(), 2);
        take = true;

        // Fit to Screen: over the whole of the mode, unfiltered.
        arcade.onSettingChanged(module, QStringLiteral("game_picture"), QStringLiteral("Fit to Screen"));
        QVERIFY2(run("mslug").section(QLatin1Char('\n'), 0, 0).endsWith(QStringLiteral(" -waitvsync -nokeepaspect -nofilter")),
                 qPrintable(run("mslug")));
        arcade.onSettingChanged(module, QStringLiteral("game_picture"), QStringLiteral("Original"));
        asked.clear();

        // A vertical game, and one MAME gives no screen of: none, and why;
        // the screen isn't asked, and MAME starts as without the setting.
        setting(false);
        const QString pacmanOff = run("pacman");
        const QString galagaOff = run("galaga");
        setting(true);
        QTest::ignoreMessage(QtInfoMsg, "[Arcade] Game's own mode for pacman in MAME: none, a vertical game");
        QCOMPARE(run("pacman"), pacmanOff);
        QTest::ignoreMessage(QtInfoMsg, "[Arcade] Game's own mode for galaga in MAME: none, no screen size and refresh "
                                        "rate is known for it");
        QCOMPARE(run("galaga"), galagaOff);
        // Settings → Screen has no monitor turned here; one turned for the
        // game alone (its options' Monitor) keeps a vertical game off it
        // either way, and Vertical Games leaves a horizontal one its mode.
        arcade.onSettingChanged(module, QStringLiteral("vertical_monitor"), QStringLiteral("Turned Right"));
        QTest::ignoreMessage(QtInfoMsg, "[Arcade] Game's own mode for pacman in MAME: none, the monitor is turned on "
                                        "its side for it");
        QVERIFY(run("pacman").contains(QStringLiteral(" -rol")));
        QVERIFY(asked.isEmpty());
        QVERIFY(run("mslug").contains(QStringLiteral(" -nofilter")));
        QCOMPARE(asked.size(), 1);
        arcade.onSettingChanged(module, QStringLiteral("vertical_monitor"), QStringLiteral("Horizontal"));
        arcade.setBoard(board::Family::Other);
    }

    // Game's Own Mode in FinalBurn Neo: off, RetroArch is started with the
    // very config and arguments it has without it; on, a game whose mode is
    // set is started with that config untouched and one of the mode's laid
    // over it (the screen's refresh rate the mode's as the kernel makes it,
    // the picture's shape in the mode's pixels, nothing smoothed, no mode
    // asked for of RetroArch's own), preloaded with osdos_keepmode.so; a
    // mode refused, and a game that gets none, as without the setting. A
    // set MAME doesn't know takes its parent's refresh rate where MAME
    // knows the parent at the same size.
    void ownModeInFbneo() {
        // The config laid over RetroArch's, to the letter.
        modetest::MonitorRange range;
        modetest::ModeLine neogeo;
        std::string why;
        QVERIFY(modetest::parseMonitorRange(modetest::kArcade15, &range, &why));
        QVERIFY(modetest::modeFor(1920, 224, 59.185606, range, &neogeo, &why));
        // 39132 kHz over 2495 x 265: the rate the kernel makes of it.
        QCOMPARE(QString::number(libretro::kernelRefresh(neogeo), 'f', 6), QStringLiteral("59.185541"));
        QCOMPARE(libretro::kernelRefresh(modetest::ModeLine()), 0.0);
        const QString laidOver = QStringLiteral(
            "# Written by OSD/OS for a game on a video mode of its own, over retroarch.cfg: what is changed here is "
            "lost at the next.\n"
            "video_fullscreen_x = \"0\"\n"
            "video_fullscreen_y = \"0\"\n"
            "video_autoswitch_refresh_rate = \"3\"\n"
            "crt_switch_resolution = \"0\"\n"
            "video_refresh_rate = \"59.185541\"\n"
            "video_vsync = \"true\"\n"
            "vrr_runloop_enable = \"false\"\n"
            "video_smooth = \"false\"\n"
            "video_aspect_ratio_auto = \"false\"\n"
            "aspect_ratio_index = \"20\"\n"
            "video_aspect_ratio = \"8.571429\"\n"
            "video_scale_integer = \"true\"\n"
            "video_scale_integer_axis = \"0\"\n"
            "video_scale_integer_scaling = \"0\"\n");
        QCOMPARE(libretro::ownModeConfigText(libretro::kernelRefresh(neogeo),
                                             libretro::viewportAspect(4.0 / 3.0, QSize(1920, 224), 4.0 / 3.0), false),
                 laidOver);
        // Fit to Screen: the whole screen, no shape of the picture's own.
        const QString filled = libretro::ownModeConfigText(59.637213, 8.571429, true);
        QVERIFY2(filled.contains(QStringLiteral("aspect_ratio_index = \"24\"\nvideo_scale_integer = \"false\"\n")),
                 qPrintable(filled));
        QVERIFY(!filled.contains(QStringLiteral("video_aspect_ratio = ")));
        QVERIFY(filled.contains(QStringLiteral("video_refresh_rate = \"59.637213\"\n")));
        // RetroArch's arguments: the config laid over the other, before the game.
        libretro::Launch launch;
        launch.core = QStringLiteral("/c/fbneo_libretro.so");
        launch.config = QStringLiteral("/h/retroarch.cfg");
        launch.log = QStringLiteral("/h/last.log");
        launch.content = QStringLiteral("/g/mslug.zip");
        const QStringList plain = libretro::arguments(launch);
        QVERIFY(!plain.join(QLatin1Char(' ')).contains(QStringLiteral("appendconfig")));
        launch.ownModeConfig = QStringLiteral("/h/own-mode.cfg");
        QStringList withMode = libretro::arguments(launch);
        QCOMPARE(withMode.value(withMode.size() - 2), QStringLiteral("--appendconfig=/h/own-mode.cfg"));
        withMode.removeAt(withMode.size() - 2);
        QCOMPARE(withMode, plain);
        // And nothing of it in the config itself, whatever the launch says.
        QCOMPARE(libretro::configText(launch), [&launch] {
            libretro::Launch without = launch;
            without.ownModeConfig.clear();
            return libretro::configText(without);
        }());

        QTemporaryDir data;
        QVERIFY(data.isValid());
        if (!libretro::corePath(data.path()).isEmpty() || !libretro::datPath(data.path()).isEmpty())
            QSKIP("FinalBurn Neo is installed here");
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        for (const char *set : { "sf2.zip", "mslug.zip", "mslughb.zip", "galaga.zip", "bios/neogeo.zip" })
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
        });

        DisplayHandoff handoff;
        QStringList asked;
        bool take = true;
        handoff.setModeSetter([&](const modetest::ModeLine &m, QString *reason) {
            asked << QString::fromStdString(modetest::describe(m));
            if (!take)
                *reason = QStringLiteral("the kernel refused it: Invalid argument");
            return take;
        });
        ArcadeBackend arcade(data.path(), data.path(), nullptr, &handoff);
        refreshed(arcade);
        QVERIFY(arcade.fbneoReady());
        const QString module = QStringLiteral("com.osdos.arcade");
        arcade.onSettingChanged(module, QStringLiteral("engines"),
                                QVariantMap{ { QStringLiteral("mslug"), QStringLiteral("fbneo") },
                                             { QStringLiteral("sf2"), QStringLiteral("fbneo") },
                                             { QStringLiteral("galaga"), QStringLiteral("fbneo") } });
        QCOMPARE(arcade.engineOf(QStringLiteral("mslug")), QStringLiteral("fbneo"));
        QCOMPARE(arcade.engineOf(QStringLiteral("mslughb")), QStringLiteral("fbneo"));
        arcade.setScreen(true, QSize(720, 288), 50);
        arcade.setBoard(board::Family::Pi5);
        arcade.setMonitor(QString::fromLatin1(modetest::kArcade15));
        arcade.setKeepModeLibrary(QStringLiteral(KEEPMODE_LIBRARY));
        const QString home = data.path() + QStringLiteral("/arcade/retroarch");
        const QString ownConfig = home + QStringLiteral("/own-mode.cfg");
        // What RetroArch was started with, what was preloaded into it, and
        // the config it was given.
        struct Run { QString line, preload; QByteArray config; };
        const auto run = [&](const char *set) {
            QFile::remove(m_log);
            QFile::remove(configCopy);
            played(arcade, QLatin1String(set));
            return Run{ logLines(QStringLiteral("retroarch ")).value(0), logLines(QStringLiteral("rapreload ")).value(0),
                        contents(configCopy) };
        };
        const QString plainLine = QStringLiteral("retroarch -L %1 --config %2/retroarch.cfg --verbose --log-file=%2/last.log %3/")
                                      .arg(libretro::downloadedCore(data.path()), home, games);
        const QString modeLine = QStringLiteral("retroarch -L %1 --config %2/retroarch.cfg --verbose --log-file=%2/last.log "
                                                "--appendconfig=%2/own-mode.cfg %3/")
                                     .arg(libretro::downloadedCore(data.path()), home, games);

        // Off: as ever, and nothing written beside the config.
        const Run off = run("mslug");
        QCOMPARE(off.line, plainLine + QStringLiteral("mslug.zip"));
        QCOMPARE(off.preload, QStringLiteral("rapreload "));
        QVERIFY(!off.config.isEmpty());
        QVERIFY(!QFileInfo::exists(ownConfig));
        QVERIFY(asked.isEmpty());

        // On: Metal Slug's mode asked for and set; the config RetroArch is
        // given is byte for byte the one it has without the setting, and the
        // mode's is laid over it.
        arcade.onSettingChanged(module, QStringLiteral("own_video_mode"), QStringLiteral("ON"));
        QTest::ignoreMessage(QtInfoMsg, "[Arcade] Game's own mode for mslug in FinalBurn Neo: 1920x224  15.684 kHz  59.19 Hz  "
                                        "39.132 MHz asked for, from its 320x224 at 59.185606 Hz");
        const Run on = run("mslug");
        QCOMPARE(asked, QStringList({ QStringLiteral("1920x224  15.684 kHz  59.19 Hz  39.132 MHz") }));
        QCOMPARE(on.line, modeLine + QStringLiteral("mslug.zip"));
        QCOMPARE(on.preload, QStringLiteral("rapreload ") + QStringLiteral(KEEPMODE_LIBRARY));
        QCOMPARE(on.config, off.config);
        QCOMPARE(QString::fromUtf8(contents(ownConfig)), laidOver);
        QCOMPARE(QFile::permissions(ownConfig) & (QFile::ReadGroup | QFile::ReadOther), QFileDevice::Permissions());
        QVERIFY(!handoff.held() && !handoff.modeSet());

        // The kernel refuses the mode: RetroArch as without the setting.
        take = false;
        const Run refused = run("mslug");
        QCOMPARE(refused.line, off.line);
        QCOMPARE(refused.preload, off.preload);
        QCOMPARE(refused.config, off.config);
        QCOMPARE(asked.size(), 2);
        take = true;
        asked.clear();

        // A hack MAME doesn't know, of a game it does, at the same size:
        // its parent's refresh rate.
        QTest::ignoreMessage(QtInfoMsg, "[Arcade] Game's own mode for mslughb in FinalBurn Neo: 1920x224  15.684 kHz  59.19 Hz  "
                                        "39.132 MHz asked for, from its 320x224 at 59.185606 Hz");
        QCOMPARE(run("mslughb").line, modeLine + QStringLiteral("mslughb.zip"));
        QCOMPARE(asked.size(), 1);
        // Games MAME gives no screen of (its stand-in's sf2 and galaga),
        // though FinalBurn Neo's list has their sizes: no rate, no mode.
        QTest::ignoreMessage(QtInfoMsg, "[Arcade] Game's own mode for sf2 in FinalBurn Neo: none, no screen size and refresh "
                                        "rate is known for it");
        QCOMPARE(run("sf2").line, plainLine + QStringLiteral("sf2.zip"));
        QTest::ignoreMessage(QtInfoMsg, "[Arcade] Game's own mode for galaga in FinalBurn Neo: none, no screen size and "
                                        "refresh rate is known for it");
        QCOMPARE(run("galaga").preload, QStringLiteral("rapreload "));
        QCOMPARE(asked.size(), 1);

        // Off again: as at first.
        arcade.onSettingChanged(module, QStringLiteral("own_video_mode"), QStringLiteral("OFF"));
        const Run again = run("mslug");
        QCOMPARE(again.line, off.line);
        QCOMPARE(again.config, off.config);
        QCOMPARE(asked.size(), 1);
        arcade.setBoard(board::Family::Other);
    }

    // A game's options: its difficulty, where MAME plays it and its board has
    // a DIP switch for it (Pac-Man's, not its coinage's), set in MAME's own
    // cfg file for the game as it starts, the rest of the file kept, and kept
    // in games.json; and a vertical game's monitor (its options' Monitor),
    // which it plays with, Emulator Video → Vertical Games named as its
    // default, Settings → Screen → Rotation turned going before it.
    void difficultyAndMonitor() {
        const QString module = QStringLiteral("com.osdos.arcade");
        const QString cfg = m_data.path() + QStringLiteral("/arcade/cfg/pacman.cfg");
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);
        const auto restore = qScopeGuard([&arcade, &module, &cfg] {
            arcade.onSettingChanged(module, QStringLiteral("difficulties"), QVariantMap());
            arcade.onSettingChanged(module, QStringLiteral("monitors"), QVariantMap());
            arcade.onSettingChanged(module, QStringLiteral("vertical_monitor"), QStringLiteral("Horizontal"));
            QFile::remove(cfg);
        });
        const auto labels = [&arcade](const char *set) {
            QStringList out;
            for (const QVariant &v : arcade.difficultyChoices(QLatin1String(set))) {
                const QVariantMap choice = v.toMap();
                out << choice.value(QStringLiteral("label")).toString()
                           + (choice.value(QStringLiteral("current")).toBool() ? QStringLiteral("*") : QString());
            }
            return out;
        };
        QCOMPARE(labels("pacman"), QStringList({ QStringLiteral("Normal*"), QStringLiteral("Hard") }));
        QVERIFY(labels("galaga").isEmpty());
        // Never chosen: MAME's file as it was, none here.
        QFile::remove(cfg);
        played(arcade, QStringLiteral("pacman"));
        QVERIFY(!QFileInfo::exists(cfg));

        arcade.onSettingChanged(module, QStringLiteral("difficulties"), QVariantMap{ { QStringLiteral("pacman"), QStringLiteral("Hard") } });
        QCOMPARE(labels("pacman"), QStringList({ QStringLiteral("Normal"), QStringLiteral("Hard*") }));
        writeFile(cfg, R"(<?xml version="1.0"?>
<!-- This file is autogenerated; comments and unknown tags will be stripped -->
<mameconfig version="10">
    <system name="pacman">
        <input>
            <port tag=":IN0" type="P1_JOYSTICK_UP" mask="1" defvalue="1">
                <newseq type="standard">KEYCODE_W</newseq>
            </port>
            <port tag=":DSW1" type="DIPSWITCH" mask="64" defvalue="64" value="64" />
            <port tag=":DSW1" type="DIPSWITCH" mask="3" defvalue="1" value="0" />
        </input>
        <counters>
            <coins index="0" number="3" />
        </counters>
    </system>
</mameconfig>
)");
        // MAME's own file, with the keys and switches set in its menus: it
        // comes in whole under its name, never written to under it, where
        // the power going would leave it cut short and those lost.
        {
            const FolderWatch watch(QFileInfo(cfg).absolutePath());
            QCOMPARE(played(arcade, QStringLiteral("pacman")).value(1).toString(), QStringLiteral("ok"));
            if (watch.fd >= 0) {
                QStringList cameIn;
                QStringList writtenTo;
                watch.read(cameIn, writtenTo);
                QVERIFY2(cameIn.contains(QStringLiteral("pacman.cfg")), qPrintable(cameIn.join(QLatin1Char(' '))));
                QVERIFY2(!writtenTo.contains(QStringLiteral("pacman.cfg")), qPrintable(writtenTo.join(QLatin1Char(' '))));
            } else {
                qWarning("the cfg folder can't be watched here (inotify): that the file comes in whole isn't checked");
            }
        }
        QByteArray written = contents(cfg);
        QVERIFY2(written.contains(R"(<port tag=":DSW1" type="DIPSWITCH" mask="64" defvalue="64" value="0"/>)"), written.constData());
        QCOMPARE(written.count("mask=\"64\""), 1);
        // The rest kept: a button, the coinage, the counters.
        QVERIFY(written.contains("KEYCODE_W"));
        QVERIFY(written.contains(R"(mask="3" defvalue="1" value="0")"));
        QVERIFY(written.contains(R"(<coins index="0" number="3"/>)"));
        // As MAME writes it back when the game ends, the switch as chosen
        // in it already: left as it is, not written again at every start.
        const QByteArray asMame = R"(<?xml version="1.0"?>
<!-- This file is autogenerated; comments and unknown tags will be stripped -->
<mameconfig version="10">
    <system name="pacman">
        <input>
            <port tag=":IN0" type="P1_JOYSTICK_UP" mask="1" defvalue="1">
                <newseq type="standard">KEYCODE_W</newseq>
            </port>
            <port tag=":DSW1" type="DIPSWITCH" mask="64" defvalue="64" value="0" />
        </input>
    </system>
</mameconfig>
)";
        writeFile(cfg, asMame);
        played(arcade, QStringLiteral("pacman"));
        QCOMPARE(contents(cfg), asMame);
        // Back to the board's own.
        arcade.onSettingChanged(module, QStringLiteral("difficulties"), QVariantMap{ { QStringLiteral("pacman"), QStringLiteral("Normal") } });
        played(arcade, QStringLiteral("pacman"));
        QVERIFY(contents(cfg).contains(R"(mask="64" defvalue="64" value="64")"));
        QVERIFY(contents(cfg).contains("KEYCODE_W"));
        // The board's own in a file with no port of the switch, as MAME
        // keeps one left at its default: nothing to set, the file as it is;
        // and none made where MAME had none.
        QByteArray atDefault = asMame;
        atDefault.replace(R"(            <port tag=":DSW1" type="DIPSWITCH" mask="64" defvalue="64" value="0" />
)", "");
        QVERIFY(!atDefault.contains("DIPSWITCH"));
        writeFile(cfg, atDefault);
        played(arcade, QStringLiteral("pacman"));
        QCOMPARE(contents(cfg), atDefault);
        QFile::remove(cfg);
        played(arcade, QStringLiteral("pacman"));
        QVERIFY(!QFileInfo::exists(cfg));
        // A new file where MAME had none.
        QFile::remove(cfg);
        arcade.onSettingChanged(module, QStringLiteral("difficulties"), QVariantMap{ { QStringLiteral("pacman"), QStringLiteral("Hard") } });
        played(arcade, QStringLiteral("pacman"));
        written = contents(cfg);
        QVERIFY2(written.contains(R"(<system name="pacman">)") && written.contains(R"(value="0"/>)"), written.constData());

        // Kept with what MAME said: the next start has it without asking.
        {
            ArcadeBackend again(m_data.path(), m_data.path(), nullptr, nullptr);
            refreshed(again);
            QCOMPARE(again.difficultyChoices(QStringLiteral("pacman")).size(), 2);
        }

        // A vertical game's monitor: Vertical Games' first, named; the
        // game's own kept and played with; none for a horizontal game.
        const auto monitors = [&arcade](const char *set) {
            QStringList out;
            for (const QVariant &v : arcade.monitorChoices(QLatin1String(set))) {
                const QVariantMap choice = v.toMap();
                out << choice.value(QStringLiteral("label")).toString()
                           + (choice.value(QStringLiteral("current")).toBool() ? QStringLiteral("*") : QString());
            }
            return out;
        };
        QCOMPARE(monitors("pacman"), QStringList({ QStringLiteral("Default (Horizontal)*"), QStringLiteral("Turned Right"),
                                                   QStringLiteral("Turned Left") }));
        QVERIFY(monitors("mslug").isEmpty());
        arcade.onSettingChanged(module, QStringLiteral("vertical_monitor"), QStringLiteral("Turned Right"));
        QCOMPARE(monitors("pacman").first(), QStringLiteral("Default (Turned Right)*"));
        arcade.onSettingChanged(module, QStringLiteral("monitors"), QVariantMap{ { QStringLiteral("pacman"), QStringLiteral("Turned Left") } });
        QCOMPARE(monitors("pacman"), QStringList({ QStringLiteral("Default (Turned Right)"), QStringLiteral("Turned Right"),
                                                   QStringLiteral("Turned Left*") }));
        QFile::remove(m_log);
        played(arcade, QStringLiteral("pacman"));
        QVERIFY(logLines(QStringLiteral("play ")).value(0).contains(QStringLiteral(" -ror")));
        // Its own set back to the default: Vertical Games'.
        arcade.onSettingChanged(module, QStringLiteral("monitors"), QVariantMap());
        QFile::remove(m_log);
        played(arcade, QStringLiteral("pacman"));
        const QString line = logLines(QStringLiteral("play ")).value(0);
        QVERIFY2(line.contains(QStringLiteral(" -rol")) && !line.contains(QStringLiteral("-ror")), qPrintable(line));
    }

    // Settings → Arcade → Favorites: the favourites written to
    // osdos-favorites.txt in the games folder, a set a line with its title;
    // those such a file names added, by MAME's names alone, each once, with
    // their titles, as many as the list holds; and those OSD/OS Setup
    // brought, added once the games have been read, the file gone.
    void favoritesFile() {
        const QString module = QStringLiteral("com.osdos.arcade");
        const QString file = m_games + QStringLiteral("/osdos-favorites.txt");
        const QString setup = m_data.path() + QStringLiteral("/arcade/favorites-setup.json");
        const auto restore = qScopeGuard([&file, &setup, this] {
            QFile::remove(file);
            QFile::remove(setup);
            QFile::remove(m_data.path() + QStringLiteral("/lists.json"));
        });
        QFile::remove(m_data.path() + QStringLiteral("/lists.json"));
        AppCore core(m_data.path(), m_data.path());
        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        refreshed(arcade);
        QCOMPARE(arcade.favoritesFile(), file);
        QCOMPARE(arcade.favoritesCount(), 0);
        QVariantMap done = arcade.exportFavorites();
        QVERIFY(!done.value(QStringLiteral("ok")).toBool());
        QVERIFY(!QFileInfo::exists(file));

        const auto favorite = [](const QString &set, const QString &name) {
            return QVariantMap{ { QStringLiteral("name"), name }, { QStringLiteral("path"), QStringLiteral("game:") + set },
                                { QStringLiteral("isFolder"), false } };
        };
        core.add_to_list(module, QStringLiteral("favorites"), favorite(QStringLiteral("galaga"), QStringLiteral("Galaga")), 100);
        core.add_to_list(module, QStringLiteral("favorites"), favorite(QStringLiteral("pacman"), QStringLiteral("Pac-Man (Midway)")), 100);
        QCOMPARE(arcade.favoritesCount(), 2);
        done = arcade.exportFavorites();
        QVERIFY2(done.value(QStringLiteral("ok")).toBool(), qPrintable(done.value(QStringLiteral("message")).toString()));
        QCOMPARE(done.value(QStringLiteral("message")).toString(), QStringLiteral("2 favorites written to ") + file);
        const QString text = QString::fromUtf8(contents(file));
        QVERIFY2(text.contains(QStringLiteral("\npacman       # Pac-Man (Midway)\ngalaga       # Galaga")), qPrintable(text));
        QCOMPARE(ArcadeBackend::setsOfFavoritesText(text), QStringList({ QStringLiteral("pacman"), QStringLiteral("galaga") }));

        // A file of one's own: comments, a name in capitals, one there
        // already, one that is no set's name.
        writeFile(file, "# mine\nsf2   # Street Fighter II\n\tPACMAN\nnot a set!\n\nmslug\nsf2\n");
        done = arcade.importFavorites();
        QCOMPARE(done.value(QStringLiteral("message")).toString(),
                 QStringLiteral("From %1: 2 added, 1 there already").arg(file));
        QCOMPARE(arcade.favoriteSets(), QStringList({ QStringLiteral("sf2"), QStringLiteral("mslug"), QStringLiteral("pacman"),
                                                      QStringLiteral("galaga") }));
        // Added with their titles, as MAME has them.
        QCOMPARE(core.get_list(module, QStringLiteral("favorites")).first().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Street Fighter II: The World Warrior"));
        // None there.
        QFile::remove(file);
        QVERIFY(arcade.importFavorites().value(QStringLiteral("message")).toString().startsWith(QStringLiteral("No file to import")));

        // As many as the list holds, the rest said.
        QStringList many;
        for (int i = 0; i < 100; ++i)
            many << QStringLiteral("game%1").arg(i, 3, 10, QLatin1Char('0'));
        done = arcade.addFavorites(many);
        QCOMPARE(done.value(QStringLiteral("added")).toInt(), 96);
        QCOMPARE(done.value(QStringLiteral("full")).toInt(), 4);
        QCOMPARE(arcade.favoritesCount(), 100);
        QVERIFY(arcade.favoriteSets().contains(QStringLiteral("galaga")));

        // OSD/OS Setup's, once the games have been read: on a new card,
        // before MAME has said what they are, without titles, which are
        // MAME's once it has (Play at Startup's choices).
        QFile::remove(m_data.path() + QStringLiteral("/lists.json"));
        QFile::remove(m_data.path() + QStringLiteral("/arcade/games.json"));
        writeFile(setup, R"({"favorites": ["kinst", "sf2", "../x"]})");
        ArcadeBackend again(m_data.path(), m_data.path(), &core, nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(setup), 10000);
        QCOMPARE(again.favoriteSets(), QStringList({ QStringLiteral("kinst"), QStringLiteral("sf2") }));
        refreshed(again);
        QSignalSpy options(&again, &ArcadeBackend::dynamicOptionsReady);
        again.startupGameOptions();
        QStringList startup;
        for (const QList<QVariant> &args : std::as_const(options)) {
            if (args.value(0).toString() != QLatin1String("play_at_startup"))
                continue;
            for (const QVariant &choice : args.value(1).toList())
                startup << choice.toMap().value(QStringLiteral("label")).toString();
        }
        QVERIFY2(startup.contains(QStringLiteral("Killer Instinct")), qPrintable(startup.join(QStringLiteral(" | "))));

        // A list that couldn't be saved (a lists.json that isn't JSON is
        // never written over): the import says so, not how many it added,
        // and OSD/OS Setup's file is kept for the next start, when it is
        // taken.
        const QString lists = m_data.path() + QStringLiteral("/lists.json");
        writeFile(lists, "{ not JSON");
        writeFile(file, "galaga\nmslug\n");
        QSignalSpy changed(&again, &ArcadeBackend::gamesChanged);
        done = again.importFavorites();
        QVERIFY(!done.value(QStringLiteral("ok")).toBool());
        QCOMPARE(done.value(QStringLiteral("message")).toString(),
                 QStringLiteral("The favorites couldn't be saved: none added from ") + file);
        QVERIFY(changed.isEmpty());
        QCOMPARE(contents(lists), QByteArray("{ not JSON"));
        writeFile(setup, R"({"favorites": ["galaga"]})");
        again.afterReadTakeSetupFavorites();
        QVERIFY(QFileInfo::exists(setup));
        QVERIFY(QFile::remove(lists));
        again.afterReadTakeSetupFavorites();
        QVERIFY(!QFileInfo::exists(setup));
        QCOMPARE(again.favoriteSets(), QStringList({ QStringLiteral("galaga") }));
        // One that names only games there already has nothing to save.
        writeFile(lists, "{ not JSON");
        writeFile(setup, R"({"favorites": []})");
        again.afterReadTakeSetupFavorites();
        QVERIFY(!QFileInfo::exists(setup));
    }

    // MAME's cfg file with a DIP switch set: as MAME writes one, in place of
    // the switch's own, the rest kept; a new one for none, or for one that
    // isn't XML.
    void dipInCfg() {
        mame::Dip dip;
        dip.tag = QStringLiteral(":DSW");
        dip.mask = 12;
        dip.defaultValue = 12;
        dip.values = { { QStringLiteral("Easy"), 8 }, { QStringLiteral("Normal"), 12 }, { QStringLiteral("Hard"), 4 } };
        QCOMPARE(dip.valueOf(QStringLiteral("Hard")), 4);
        QCOMPARE(dip.valueOf(QStringLiteral("Gone")), 12);
        QCOMPARE(dip.defaultName(), QStringLiteral("Normal"));
        QVERIFY(mame::isDifficulty(QStringLiteral("Game Difficulty")));
        QVERIFY(!mame::isDifficulty(QStringLiteral("Bonus Life")));
        const QByteArray fresh = mame::withDip(QByteArray(), QStringLiteral("1942"), dip, 4);
        QVERIFY2(fresh.contains(R"(<mameconfig version="10">)") && fresh.contains(R"(<system name="1942">)")
                     && fresh.contains(R"(<port tag=":DSW" type="DIPSWITCH" mask="12" defvalue="12" value="4"/>)"),
                 fresh.constData());
        QCOMPARE(mame::withDip("not xml <", QStringLiteral("1942"), dip, 4), fresh);
        // Another system's file, and one without input: the port where it goes.
        const QByteArray other = mame::withDip(R"(<mameconfig version="10"><system name="1943"><input><port tag=":DSW" type="DIPSWITCH" mask="12" defvalue="12" value="8"/></input></system></mameconfig>)",
                                               QStringLiteral("1942"), dip, 8);
        QVERIFY(other.contains(R"(<system name="1943">)") && other.contains(R"(<system name="1942">)"));
        QCOMPARE(other.count("DIPSWITCH"), 2);
        const QByteArray noInput = mame::withDip(R"(<mameconfig version="10"><system name="1942"><counters><coins index="0" number="1"/></counters></system></mameconfig>)",
                                                 QStringLiteral("1942"), dip, 8);
        QVERIFY2(noInput.contains("<input>") && noInput.contains(R"(value="8"/>)") && noInput.contains("<coins"), noInput.constData());
        // Read back as XML, a single system.
        QXmlStreamReader check(noInput);
        int systems = 0;
        while (!check.atEnd()) {
            if (check.readNext() == QXmlStreamReader::StartElement && check.name() == QLatin1String("system"))
                ++systems;
        }
        QVERIFY(!check.hasError());
        QCOMPARE(systems, 1);
        // A file that says so already is given back as it is, byte for byte:
        // the switch's port with the value, as MAME lays its file out, or no
        // port of it for its default, which is how MAME keeps a switch left
        // alone (and no file at all then). One set otherwise is set, to the
        // default too.
        const QByteArray set4 = R"(<?xml version="1.0"?>
<mameconfig version="10">
    <system name="1942">
        <input>
            <port tag=":DSW" type="DIPSWITCH" mask="12" defvalue="12" value="4" />
            <port tag=":DSW" type="DIPSWITCH" mask="3" defvalue="3" value="1" />
        </input>
    </system>
</mameconfig>
)";
        QCOMPARE(mame::withDip(set4, QStringLiteral("1942"), dip, 4), set4);
        const QByteArray back = mame::withDip(set4, QStringLiteral("1942"), dip, 12);
        QVERIFY2(back.contains(R"(mask="12" defvalue="12" value="12"/>)") && back.contains(R"(mask="3" defvalue="3" value="1"/>)"),
                 back.constData());
        QCOMPARE(back.count("mask=\"12\""), 1);
        QByteArray unset = set4;
        unset.replace(R"(            <port tag=":DSW" type="DIPSWITCH" mask="12" defvalue="12" value="4" />
)", "");
        QCOMPARE(mame::withDip(unset, QStringLiteral("1942"), dip, 12), unset);
        QVERIFY(mame::withDip(unset, QStringLiteral("1942"), dip, 8).contains(R"(mask="12" defvalue="12" value="8"/>)"));
        QCOMPARE(mame::withDip(QByteArray(), QStringLiteral("1942"), dip, 12), QByteArray());
        // The switch of another system's alone doesn't count as this one's.
        QVERIFY(mame::withDip(R"(<mameconfig version="10"><system name="1943"><input><port tag=":DSW" type="DIPSWITCH" mask="12" defvalue="12" value="8"/></input></system></mameconfig>)",
                              QStringLiteral("1942"), dip, 8).contains(R"(<system name="1942">)"));
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
                                                        "Try another MAME Video in Settings → Arcade → Emulator Video"));

        // MAME killed by a signal says nothing of why.
        writeFile(m_games + QStringLiteral("/crash.zip"));
        arcade.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.scanning(), 10000);
        QCOMPARE(played(arcade, QStringLiteral("crash")),
                 QVariantList({ 11, QStringLiteral("failed"),
                                QStringLiteral("MAME closed unexpectedly (signal 11)\n"
                                               "Try another MAME Video in Settings → Arcade → Emulator Video") }));
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

    // MAME leaving a process of its own running as it exits (a sound server
    // its sound library started): the game ends as MAME does, and what it
    // left is ended, where the screen would wait for it for good, the game's
    // last frame up and nothing answering.
    void aGameLeavingAProcessBehindEnds() {
#ifndef Q_OS_LINUX
        QSKIP("Process groups are followed on Linux");
#else
        writeFile(m_games + QStringLiteral("/leftover.zip"));
        // Gone with the test whatever becomes of it: the games folder is
        // every test's.
        const auto removeGame = qScopeGuard([this] { QFile::remove(m_games + QStringLiteral("/leftover.zip")); });
        // The game takes the screen, as on a desktop (a hand-off with no
        // console to switch), and gives it back when its group has gone.
        const QByteArray display = qgetenv("DISPLAY");
        qputenv("DISPLAY", ":99");
        const auto restore = qScopeGuard([&display] {
            if (display.isEmpty())
                qunsetenv("DISPLAY");
            else
                qputenv("DISPLAY", display);
        });
        DisplayHandoff handoff;
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, &handoff);
        refreshed(arcade);
        QElapsedTimer took;
        took.start();
        QCOMPARE(played(arcade, QStringLiteral("leftover")).at(1).toString(), QStringLiteral("ok"));
        QVERIFY2(took.elapsed() < 5000, qPrintable(QStringLiteral("ended after %1 ms").arg(took.elapsed())));
        QVERIFY(!arcade.running());
        QVERIFY(!handoff.held());
        const QStringList helper = logLines(QStringLiteral("helper "));
        QCOMPARE(helper.size(), 1);
        const QString stat = QStringLiteral("/proc/") + helper.first().mid(7) + QStringLiteral("/stat");
        // Gone, or a zombie left for its new parent to reap.
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(stat)
                                     || contents(stat).mid(contents(stat).lastIndexOf(')') + 2, 1) == "Z", 3000);
#endif
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

    // A Pi 5's HDMI plays only while its port shows the picture: with both
    // off (the picture on a JAMMA board) and no other card, MAME plays none
    // too, as PortAudio, which it falls back to, would crash; with one
    // showing it, the game plays through it, and with a USB sound card
    // beside the two dark, through that: Audio Output on Auto names the card
    // ALSA's default can't be (ALSA_CARD). The log says what each was given,
    // the screen's DRM card too.
    void hdmiSoundOff() {
        QTemporaryDir asound;
        QTemporaryDir drm;
        QTemporaryDir runtime;
        QTemporaryDir data;
        QVERIFY(asound.isValid() && drm.isValid() && runtime.isValid() && data.isValid());
        const QByteArray hdmi = " 0 [vc4hdmi0       ]: vc4-hdmi - vc4-hdmi-0\n"
                                "                      vc4-hdmi-0\n"
                                " 1 [vc4hdmi1       ]: vc4-hdmi - vc4-hdmi-1\n"
                                "                      vc4-hdmi-1\n";
        writeFile(asound.path() + QStringLiteral("/cards"), hdmi);
        QVERIFY(QDir().mkpath(asound.path() + QStringLiteral("/card0/pcm0p")));
        QVERIFY(QDir().mkpath(asound.path() + QStringLiteral("/card1/pcm0p")));
        writeFile(drm.path() + QStringLiteral("/card1-HDMI-A-1/enabled"), "disabled\n");
        writeFile(drm.path() + QStringLiteral("/card1-HDMI-A-2/enabled"), "disabled\n");
        writeFile(drm.path() + QStringLiteral("/card1-DPI-1/enabled"), "enabled\n");
        const QByteArray pulse = qgetenv("PULSE_SERVER");
        const QByteArray xdg = qgetenv("XDG_RUNTIME_DIR");
        const QByteArray alsaCard = qgetenv("ALSA_CARD");
        qunsetenv("PULSE_SERVER");
        qunsetenv("ALSA_CARD");
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        qputenv("OSDOS_ASOUND_DIR", asound.path().toUtf8());
        qputenv("OSDOS_DRM_DIR", drm.path().toUtf8());
        // No ALSA configuration of the machine's sets the default.
        qputenv("OSDOS_ALSA_CONFIG", (asound.path() + QStringLiteral("/asound.conf")).toUtf8());
        // Audio Output on Auto, as the app has it.
        AppCore core(data.path(), data.path());
        AudioOutput audio(&core);
        // The game's command line, the card ALSA_CARD names it, and what the
        // log says it was given.
        static QStringList logged;
        const auto launch = [this]() -> QStringList {
            QFile::remove(m_log);
            ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
            refreshed(arcade);
            logged.clear();
            const QtMessageHandler previous = qInstallMessageHandler(
                [](QtMsgType, const QMessageLogContext &, const QString &message) { logged << message; });
            const QString reason = played(arcade, QStringLiteral("galaga")).at(1).toString();
            qInstallMessageHandler(previous);
            const QStringList lines = logLines(QStringLiteral("play"));
            if (reason != QLatin1String("ok") || lines.size() != 1)
                return { reason };
            const QStringList running = logged.filter(QStringLiteral("[Scripts] Running 'mame'"));
            return { lines.first(), logLines(QStringLiteral("alsa")).value(0), running.value(0) };
        };
        const QStringList off = launch();
        // HDMI1 shows the picture.
        writeFile(drm.path() + QStringLiteral("/card1-HDMI-A-2/enabled"), "enabled\n");
        const QStringList on = launch();
        // Both dark again, a USB sound card plugged in.
        writeFile(drm.path() + QStringLiteral("/card1-HDMI-A-2/enabled"), "disabled\n");
        writeFile(asound.path() + QStringLiteral("/cards"),
                  hdmi + " 2 [Device         ]: USB-Audio - USB Audio Device\n"
                         "                      GeneralPlus USB Audio Device at usb-xhci-hcd.0-1.3, full speed\n");
        QVERIFY(QDir().mkpath(asound.path() + QStringLiteral("/card2/pcm0p")));
        // The output's DRM card, as a Pi 5's launcher names it.
        qputenv("OSDOS_DRM_DEVICE", "/dev/dri/card1");
        const QStringList usb = launch();
        qunsetenv("OSDOS_DRM_DEVICE");
        qputenv("OSDOS_ASOUND_DIR", (m_data.path() + QStringLiteral("/asound")).toUtf8());
        qunsetenv("OSDOS_DRM_DIR");
        qunsetenv("OSDOS_ALSA_CONFIG");
        if (!pulse.isNull())
            qputenv("PULSE_SERVER", pulse);
        if (!alsaCard.isNull())
            qputenv("ALSA_CARD", alsaCard);
        if (xdg.isNull())
            qunsetenv("XDG_RUNTIME_DIR");
        else
            qputenv("XDG_RUNTIME_DIR", xdg);
        QVERIFY2(off.value(0).startsWith(QStringLiteral("play ")) && off.value(0).endsWith(QStringLiteral(" -sound none")),
                 qPrintable(off.join(QLatin1Char('|'))));
        QCOMPARE(off.value(1), QStringLiteral("alsa "));
        QVERIFY2(off.value(2).endsWith(QStringLiteral(", no sound card that can play)")), qPrintable(off.value(2)));
        QVERIFY2(on.value(0).startsWith(QStringLiteral("play ")) && !on.value(0).contains(QStringLiteral("-sound")),
                 qPrintable(on.join(QLatin1Char('|'))));
        QCOMPARE(on.value(1), QStringLiteral("alsa vc4hdmi1"));
        QVERIFY2(on.value(2).endsWith(QStringLiteral(", sound card vc4hdmi1)")), qPrintable(on.value(2)));
        QVERIFY2(usb.value(0).startsWith(QStringLiteral("play ")) && !usb.value(0).contains(QStringLiteral("-sound")),
                 qPrintable(usb.join(QLatin1Char('|'))));
        QCOMPARE(usb.value(1), QStringLiteral("alsa Device"));
        QVERIFY2(usb.value(2).endsWith(QStringLiteral(", sound card Device, DRM card 1)")), qPrintable(usb.value(2)));
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

    // Show Stats in FinalBurn Neo's config: off, none of it, the rest of
    // the text as it was; the frame rate, a second's frames at a time; the
    // machine's state too, by OSD/OS's own key. Played alone without
    // RetroAchievements, as RetroArch's message text top left, sized for
    // the screen's lines (the game's mode's where it has one); with
    // achievements or over the network, RetroArch's widgets kept.
    void statsConfig() {
        QCOMPARE(libretro::statsOf(QStringLiteral("Off")), libretro::Stats::Off);
        QCOMPARE(libretro::statsOf(QStringLiteral("FPS")), libretro::Stats::Fps);
        QCOMPARE(libretro::statsOf(QStringLiteral("FPS + System")), libretro::Stats::System);
        QCOMPARE(libretro::statsOf(QString()), libretro::Stats::Off);
        QCOMPARE(libretro::statsOf(QStringLiteral("fps")), libretro::Stats::Off);

        libretro::Launch launch;
        launch.core = QStringLiteral("/cores/fbneo_libretro.so");
        launch.content = QStringLiteral("/games/sf2.zip");
        launch.configDir = QStringLiteral("/ra/config");
        launch.infoDir = QStringLiteral("/ra/info");
        launch.screenSize = QSize(720, 240);
        const QStringList keys{ QStringLiteral("fps_show"), QStringLiteral("fps_update_interval"),
                                QStringLiteral("osdos_system_show"), QStringLiteral("menu_enable_widgets"),
                                QStringLiteral("video_font_enable"), QStringLiteral("video_font_size"),
                                QStringLiteral("video_message_pos_x"), QStringLiteral("video_message_pos_y") };
        // The lines on adds to off, the rest of on being off to the byte.
        const auto added = [&keys](const QString &off, const QString &on) {
            QStringList lines = on.split(QLatin1Char('\n'));
            QStringList extra;
            for (auto it = lines.begin(); it != lines.end();) {
                if (keys.contains(it->section(QLatin1Char(' '), 0, 0))) {
                    extra << *it;
                    it = lines.erase(it);
                } else {
                    ++it;
                }
            }
            return lines.join(QLatin1Char('\n')) == off ? extra : QStringList{ QStringLiteral("(changed)") };
        };
        const QStringList fps{ QStringLiteral("fps_show = \"true\""), QStringLiteral("fps_update_interval = \"60\"") };
        const QStringList topLeft{ QStringLiteral("menu_enable_widgets = \"false\""),
                                   QStringLiteral("video_font_enable = \"true\""),
                                   QStringLiteral("video_message_pos_x = \"0.05\""),
                                   QStringLiteral("video_font_size = \"16\""),
                                   QStringLiteral("video_message_pos_y = \"0.883\"") };
        const QStringList system{ QStringLiteral("osdos_system_show = \"true\"") };
        const QString off = libretro::configText(launch);
        for (const QString &key : keys)
            QVERIFY2(!off.contains(key + QStringLiteral(" = ")), qPrintable(key));
        launch.stats = libretro::Stats::Fps;
        QCOMPARE(added(off, libretro::configText(launch)), fps + topLeft);
        launch.stats = libretro::Stats::System;
        QCOMPARE(added(off, libretro::configText(launch)), fps + system + topLeft);
        // The text's size by the lines, and no more than its longest line
        // fits across: 20 on a 720x480 mode, 48 at most, 12 at least; a
        // game's mode over the screen's.
        const auto sizeAndPlace = [&launch] {
            const QStringList lines = libretro::configText(launch).split(QLatin1Char('\n'));
            return lines.filter(QStringLiteral("video_font_size")).join(QLatin1Char(' ')) + QLatin1Char(' ')
                   + lines.filter(QStringLiteral("video_message_pos_y")).join(QLatin1Char(' '));
        };
        launch.screenSize = QSize(720, 480);
        QCOMPARE(sizeAndPlace(), QStringLiteral("video_font_size = \"20\" video_message_pos_y = \"0.908\""));
        launch.screenSize = QSize(640, 480);
        QCOMPARE(sizeAndPlace(), QStringLiteral("video_font_size = \"17\" video_message_pos_y = \"0.915\""));
        launch.screenSize = QSize(320, 240);
        QCOMPARE(sizeAndPlace(), QStringLiteral("video_font_size = \"12\" video_message_pos_y = \"0.900\""));
        launch.screenSize = QSize(3840, 2160);
        QCOMPARE(sizeAndPlace(), QStringLiteral("video_font_size = \"48\" video_message_pos_y = \"0.928\""));
        launch.modeSize = QSize(1920, 1080);
        QCOMPARE(sizeAndPlace(), QStringLiteral("video_font_size = \"48\" video_message_pos_y = \"0.906\""));
        launch.modeSize = QSize();
        launch.screenSize = QSize();
        QCOMPARE(sizeAndPlace(), QStringLiteral(" video_message_pos_y = \"0.88\""));
        launch.screenSize = QSize(720, 240);
        // Over the network, or signed in to RetroAchievements: the widgets
        // stay, and the frame rate is in their box.
        for (const auto netplay : { libretro::Launch::Netplay::Host, libretro::Launch::Netplay::LanJoin }) {
            launch.netplay = netplay;
            launch.stats = libretro::Stats::Off;
            const QString alone = libretro::configText(launch);
            launch.stats = libretro::Stats::System;
            QCOMPARE(added(alone, libretro::configText(launch)), fps + system);
        }
        launch.netplay = libretro::Launch::Netplay::None;
        launch.achievementsUser = QStringLiteral("player");
        launch.achievementsToken = QStringLiteral("tok");
        launch.stats = libretro::Stats::Off;
        const QString signedIn = libretro::configText(launch);
        launch.stats = libretro::Stats::Fps;
        QCOMPARE(added(signedIn, libretro::configText(launch)), fps);
    }

    // What RetroArch with FinalBurn Neo is asked and what is made of what it
    // says: its list of games, the controllers kept for the players, its
    // config and arguments, its log, the token it saves, the netplay
    // lobby's rooms and RetroAchievements' hash of a game.
    void libretroHelpers() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.path() + QStringLiteral("/fbneo.dat"), fixture("fbneo-arcade.dat"));
        QHash<QString, mame::Machine> machines = libretro::readDat(dir.path() + QStringLiteral("/fbneo.dat"));
        QCOMPARE(machines.size(), 8);
        // The pictures' shapes, where the list says.
        QCOMPARE(std::count_if(machines.cbegin(), machines.cend(), [](const mame::Machine &m) { return m.aspect > 0; }), 3);
        QCOMPARE(machines.value(QStringLiteral("1943")).aspect, 0.0);
        QCOMPARE(machines.value(QStringLiteral("sf2")).aspect, 4.0 / 3.0);
        QCOMPARE(machines.value(QStringLiteral("galaga")).aspect, 3.0 / 4.0);
        QCOMPARE(machines.value(QStringLiteral("sf2")).description,
                 QStringLiteral("Street Fighter II - The World Warrior (World 910522)"));
        QVERIFY(machines.contains(QStringLiteral("sf2ce")));
        QVERIFY(!machines.contains(QStringLiteral("neogeo")));
        // Each game as MAME would describe it: its year and maker, a clone's
        // parent, its driver named as MAME's are, whether it works, and a
        // vertical one's rotation.
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
        // Its size as MAME gives one, before the rotation (the list has a
        // vertical game's as it stands on the glass), one screen, and no
        // refresh rate, which the list doesn't say.
        const auto size = [&machines](const char *set) {
            const mame::Machine m = machines.value(QLatin1String(set));
            return QStringLiteral("%1x%2 %3 %4").arg(m.width).arg(m.height).arg(m.screens).arg(m.refresh);
        };
        QCOMPARE(size("sf2"), QStringLiteral("384x224 1 0"));
        QCOMPARE(size("galaga"), QStringLiteral("288x224 1 0"));
        QCOMPARE(size("1943"), QStringLiteral("256x224 1 0"));
        QCOMPARE(size("kof98"), QStringLiteral("0x0 0 0"));
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
                                          QStringLiteral("Xbox 360 Controller"), true),
                 QStringLiteral("045e:028e Xbox 360 Controller"));
        QCOMPARE(libretro::reservedDevice(QStringLiteral("05000000546f6f6c6b697420506164"), QStringLiteral("Toolkit Pad"), true),
                 QStringLiteral("Toolkit Pad"));
        QCOMPARE(libretro::reservedDevice(QStringLiteral("19000000000000000000000000000000"), QStringLiteral("JAMMA P1"), true),
                 QStringLiteral("JAMMA P1"));
        // The JAMMA board's sticks by the vendor and the product osdos-jamma
        // gives them, the player's number.
        QCOMPARE(libretro::reservedDevice(QStringLiteral("06006869010000000200000001000000"),
                                          QStringLiteral("OSD/OS JAMMA P2"), true),
                 QStringLiteral("0001:0002 OSD/OS JAMMA P2"));
        // Where RetroArch reads no vendor or product (Windows, macOS, the
        // BSDs), by the name its SDL gives the pad: its mapping's, which
        // OSD/OS hands it, whatever the GUID's checksum; else its own.
        {
            controllers::setPadMappings({ QStringLiteral("030000005e040000130b000009057200,Xbox Series X Controller,a:b0") });
            const auto noMappings = qScopeGuard([] { controllers::setPadMappings({}); });
            QCOMPARE(libretro::reservedDevice(QStringLiteral("03001e8f5e040000130b000009057200"),
                                              QStringLiteral("Controller (Xbox One For Windows)"), false),
                     QStringLiteral("Xbox Series X Controller"));
            QCOMPARE(libretro::reservedDevice(QStringLiteral("030000005e0400008e02000014010000"),
                                              QStringLiteral("Xbox 360 Controller"), false),
                     QStringLiteral("Xbox 360 Controller"));
        }
        // On Windows SDL maps a pad its own drivers know (XInput's 'x', Raw
        // Input's 'r', HIDAPI's 'h') with a mapping it names "*", and names
        // the pad by the pad's own name: that name, never "*", which no pad
        // RetroArch sees is called, and which would leave a lone pad on the
        // first port kept for no one (player 2's, without its Game Buttons).
        {
            controllers::setPadMappings({ QStringLiteral("030000005e040000130b000009057200,*,a:b0,b:b1"),
                                          QStringLiteral("030000005e0400008e02000000007801,*,a:b0,b:b1") });
            const auto noMappings = qScopeGuard([] { controllers::setPadMappings({}); });
            QCOMPARE(libretro::reservedDevice(QStringLiteral("03001e8f5e040000130b000009057200"),
                                              QStringLiteral("Wireless Pad Pro"), false),
                     QStringLiteral("Wireless Pad Pro"));
            QCOMPARE(libretro::reservedDevice(QStringLiteral("0300a1b25e0400008e02000000007801"),
                                              QStringLiteral("Wired Pad"), false),
                     QStringLiteral("Wired Pad"));
        }

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
        for (const char *line : { "user_language = \"0\"", "video_driver = \"gl\"", "video_context_driver = \"kms\"",
                                  "input_driver = \"udev\"",
                                  "audio_driver = \"alsa\"", "audio_device = \"\"", "gamemode_enable = \"false\"",
                                  "input_joypad_driver = \"sdl2\"",
                                  // RetroArch's Standard Gamepad for every player, its own
                                  // look-up off: no "not configured" over the game.
                                  "input_autodetect_enable = \"false\"", "input_player1_b_btn = \"0\"",
                                  "input_player1_a_btn = \"1\"", "input_player1_start_btn = \"6\"",
                                  "input_player1_select_btn = \"4\"", "input_player2_up_btn = \"11\"",
                                  "input_player6_l_x_minus_axis = \"-0\"", "input_player6_r_y_plus_axis = \"-3\"",
                                  // The left stick a joystick beside the D-pad, as
                                  // its directions: online, the stick goes as these.
                                  "input_player1_up_axis = \"-1\"", "input_player1_down_axis = \"+1\"",
                                  "input_player1_left_axis = \"-0\"", "input_player6_right_axis = \"+0\"",
                                  "input_quit_gamepad_combo = \"4\"", "quit_press_twice = \"false\"",
                                  // A keyboard's coin and start as MAME has them, in
                                  // place of RetroArch's Right Shift and Enter.
                                  "input_player1_select = \"num5\"", "input_player1_start = \"num1\"",
                                  "input_player2_select = \"num6\"", "input_player2_start = \"num2\"",
                                  "input_player4_select = \"num8\"", "input_player4_start = \"num4\"",
                                  "menu_driver = \"rgui\"", "ui_companion_enable = \"false\"",
                                  "system_directory = \"/ra/system\"", "savefile_directory = \"/ra/saves\"",
                                  "input_player1_device_reservation_type = \"1\"",
                                  "input_player1_reserved_device = \"045e:028e Xbox 360 Controller\"",
                                  "input_player3_reserved_device = \"JAMMA P3\"",
                                  "cheevos_enable = \"true\"", "cheevos_username = \"player\"", "cheevos_token = \"\"",
                                  "cheevos_password = \"secret\"", "cheevos_hardcore_mode_enable = \"true\"",
                                  // The core known by its info file: among the cores of
                                  // its own folder, the file OSD/OS writes read afresh.
                                  "libretro_directory = \"/cores\"", "libretro_info_path = \"/ra/info\"",
                                  "core_info_cache_enable = \"false\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        // Preferred on every platform: a pad RetroArch doesn't match to its
        // player takes the first port free rather than leaving that one empty.
        launch.padsByVendor = false;
        QVERIFY(libretro::configText(launch).split(QLatin1Char('\n'))
                    .contains(QStringLiteral("input_player1_device_reservation_type = \"1\"")));
        launch.padsByVendor = true;
        QVERIFY(libretro::configText(launch).split(QLatin1Char('\n'))
                    .contains(QStringLiteral("input_player1_device_reservation_type = \"1\"")));
        // No card that can play: no audio driver at all, rather than ALSA
        // failing at every start ("failed_to_start_audio_driver").
        launch.sound = false;
        {
            const QStringList quiet = libretro::configText(launch).split(QLatin1Char('\n'));
            QVERIFY(quiet.contains(QStringLiteral("audio_driver = \"null\"")));
            QVERIFY(quiet.contains(QStringLiteral("audio_enable = \"false\"")));
            QVERIFY(!quiet.contains(QStringLiteral("audio_driver = \"alsa\"")));
        }
        launch.sound = true;
        // RetroArch's own display queue and sound buffer unless a machine's
        // are given: two frames queued on the console of a Pi 5 (Frame
        // Buffers on Auto), RetroArch's three on a Pi 4 and everywhere else,
        // two on any console asked for Two, and none of it on a desktop.
        QVERIFY(!config.contains(QStringLiteral("video_max_swapchain_images")));
        QVERIFY(!config.contains(QStringLiteral("audio_latency")));
        QVERIFY(!config.contains(QStringLiteral("audio_resampler_quality")));
        using board::Family;
        QCOMPARE(libretro::swapchainImages(Family::Pi5, true, QString()), 2);
        QCOMPARE(libretro::swapchainImages(Family::Pi5, true, QStringLiteral("Auto")), 2);
        QCOMPARE(libretro::swapchainImages(Family::Pi5, true, QStringLiteral("Three")), 0);
        QCOMPARE(libretro::swapchainImages(Family::Pi4, true, QStringLiteral("Auto")), 0);
        QCOMPARE(libretro::swapchainImages(Family::Pi4, true, QStringLiteral("Two")), 2);
        QCOMPARE(libretro::swapchainImages(Family::Other, true, QString()), 0);
        QCOMPARE(libretro::swapchainImages(Family::Pi5, false, QStringLiteral("Two")), 0);
        QCOMPARE(libretro::swapchainImages(Family::Other, false, QStringLiteral("Two")), 0);
        launch.swapchainImages = 2;
        QVERIFY(libretro::configText(launch).split(QLatin1Char('\n'))
                    .contains(QStringLiteral("video_max_swapchain_images = \"2\"")));
        launch.console = false;
        QVERIFY(!libretro::configText(launch).contains(QStringLiteral("video_max_swapchain_images")));
        launch.console = true;
        launch.swapchainImages = 0;
        // Sound Buffer on Auto: 48 ms on a Pi 5 and any other machine; on a
        // Pi 4 RetroArch's 64 ms and a cheaper resampler; RetroArch's own
        // for both where asked.
        const auto sound = [](Family family, const char *choice) {
            const libretro::SoundTuning t = libretro::soundTuning(family, QLatin1String(choice));
            return QStringLiteral("%1/%2").arg(t.latency).arg(t.resamplerQuality);
        };
        QCOMPARE(sound(Family::Pi5, ""), QStringLiteral("48/-1"));
        QCOMPARE(sound(Family::Other, "Auto"), QStringLiteral("48/-1"));
        QCOMPARE(sound(Family::Pi4, "Auto"), QStringLiteral("0/2"));
        QCOMPARE(sound(Family::Pi4, "RetroArch's"), QStringLiteral("0/-1"));
        QCOMPARE(sound(Family::Pi5, "RetroArch's"), QStringLiteral("0/-1"));
        launch.audioLatency = 48;
        launch.resamplerQuality = 2;
        for (bool console : { true, false }) {
            launch.console = console;
            const QStringList lines = libretro::configText(launch).split(QLatin1Char('\n'));
            QVERIFY(lines.contains(QStringLiteral("audio_latency = \"48\"")));
            QVERIFY(lines.contains(QStringLiteral("audio_resampler_quality = \"2\"")));
        }
        launch.console = true;
        launch.audioLatency = 0;
        launch.resamplerQuality = -1;
        // The relay nearest the time zone, of those RetroArch offers.
        for (const auto &[zone, relay] : std::initializer_list<std::pair<const char *, const char *>>{
                 { "Europe/Istanbul", "madrid" }, { "Africa/Lagos", "madrid" }, { "Atlantic/Canary", "madrid" },
                 { "America/Sao_Paulo", "saopaulo" }, { "America/Argentina/Cordoba", "saopaulo" },
                 { "America/Bogota", "saopaulo" }, { "America/Chicago", "nyc" }, { "America/Mexico_City", "nyc" },
                 { "America/Indiana/Indianapolis", "nyc" }, { "Asia/Seoul", "chuncheon" }, { "Asia/Tokyo", "chuncheon" },
                 { "Asia/Kolkata", "singapore" }, { "Asia/Dubai", "singapore" }, { "Australia/Perth", "singapore" },
                 { "Pacific/Auckland", "singapore" }, { "Pacific/Honolulu", "nyc" }, { "UTC", "nyc" },
                 { "Etc/UTC", "nyc" }, { "", "nyc" } })
            QCOMPARE(libretro::relayForTimeZone(QLatin1String(zone)), QLatin1String(relay));
        // A 4K screen, or a 1080p one at 30 Hz, whose connector has
        // 1920x1080: the game asks for it at 60 Hz. A tube's 240p and 480i,
        // a 1080p60 screen, 50 Hz, a connector without the mode, or only its
        // interlaced one: nothing.
        const QStringList tv{ QStringLiteral("3840x2160"), QStringLiteral("1920x1080"), QStringLiteral("1920x1080i"),
                              QStringLiteral("1280x720") };
        const auto mode = [](const QSize &screen, double hz, bool tube, const QStringList &modes) {
            const std::optional<libretro::GameMode> m = libretro::gameMode(screen, hz, tube, modes);
            return m ? QStringLiteral("%1x%2@%3").arg(m->size.width()).arg(m->size.height()).arg(m->refresh) : QString();
        };
        QCOMPARE(mode(QSize(3840, 2160), 30, false, tv), QStringLiteral("1920x1080@60"));
        QCOMPARE(mode(QSize(3840, 2160), 60, false, tv), QStringLiteral("1920x1080@60"));
        QCOMPARE(mode(QSize(1920, 1080), 30, false, tv), QStringLiteral("1920x1080@60"));
        QCOMPARE(mode(QSize(1920, 1080), 60, false, tv), QString());
        QCOMPARE(mode(QSize(1920, 1080), 50, false, tv), QString());
        QCOMPARE(mode(QSize(720, 240), 60, true, tv), QString());
        QCOMPARE(mode(QSize(720, 480), 30, true, tv), QString());
        QCOMPARE(mode(QSize(720, 480), 30, false, tv), QString());
        QCOMPARE(mode(QSize(720, 576), 25, true, { QStringLiteral("720x576i") }), QString());
        QCOMPARE(mode(QSize(3840, 2160), 30, false, { QStringLiteral("3840x2160"), QStringLiteral("1920x1080i") }),
                 QString());
        QCOMPARE(mode(QSize(), 30, false, tv), QString());
        // Written on the console alone, as RetroArch's KMS context takes it.
        QVERIFY(!config.contains(QStringLiteral("video_fullscreen_x")));
        QVERIFY(!config.contains(QStringLiteral("video_refresh_rate")));
        launch.modeSize = QSize(1920, 1080);
        launch.modeRefresh = 60;
        {
            const QStringList lines = libretro::configText(launch).split(QLatin1Char('\n'));
            for (const char *line : { "video_fullscreen_x = \"1920\"", "video_fullscreen_y = \"1080\"",
                                      "video_refresh_rate = \"60.000000\"" })
                QVERIFY2(lines.contains(QLatin1String(line)), line);
            launch.console = false;
            QVERIFY(!libretro::configText(launch).contains(QStringLiteral("video_fullscreen_x")));
            launch.console = true;
        }
        launch.modeSize = QSize();
        launch.modeRefresh = 0;
        // The ping on the screen only over the network, off unless asked.
        QVERIFY(!config.contains(QStringLiteral("netplay_ping_show")));
        // That file, by the core's name: its states deterministic, the same
        // on every machine, without which RetroArch neither hosts nor joins
        // a game with it.
        QCOMPARE(libretro::coreInfoPath(QStringLiteral("/ra/info")), QStringLiteral("/ra/info/fbneo_libretro.info"));
        const QStringList info = libretro::coreInfoText().split(QLatin1Char('\n'));
        for (const char *line : { "corename = \"FinalBurn Neo\"", "display_name = \"Arcade (FinalBurn Neo)\"", "savestate = \"true\"",
                                  "savestate_features = \"deterministic\"" })
            QVERIFY2(info.contains(QLatin1String(line)), line);
        // OSD/OS's own core for the local network: its info file and its
        // remap file by its own names, a remap file's folder one folder (no
        // slash in the name), and its name in RetroArch's menus.
        QCOMPARE(libretro::coreInfoPath(QStringLiteral("/ra/info"), true), QStringLiteral("/ra/info/osdos_lan_libretro.info"));
        const QStringList lanInfo = libretro::coreInfoText(true).split(QLatin1Char('\n'));
        for (const char *line : { "corename = \"OSD-OS LAN Core\"", "display_name = \"OSD/OS LAN Core (FinalBurn Neo)\"",
                                  "savestate = \"true\"", "savestate_features = \"deterministic\"" })
            QVERIFY2(lanInfo.contains(QLatin1String(line)), line);
        QCOMPARE(libretro::coreRemapPath(QStringLiteral("/ra/remaps")),
                 QStringLiteral("/ra/remaps/FinalBurn Neo/FinalBurn Neo.rmp"));
        QCOMPARE(libretro::coreRemapPath(QStringLiteral("/ra/remaps"), true),
                 QStringLiteral("/ra/remaps/OSD-OS LAN Core/OSD-OS LAN Core.rmp"));
        QVERIFY(!QString(libretro::kLanCoreName).contains(QLatin1Char('/')));
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
        // Over the network, the options both machines play with alike, in a
        // file of OSD/OS's own: every option that changes how a game runs at
        // FinalBurn Neo's default, high scores and patched sets off, whatever
        // the user's own say; the game's DIP switches, cheats, IPS patches
        // and ROM data left out, so at their defaults; of the user's, the
        // picture's alone.
        QCOMPARE(libretro::netplayOptionsPath(QStringLiteral("/ra/config")), QStringLiteral("/ra/config/netplay-options.cfg"));
        const QString alike = libretro::netplayOptionsText(QString());
        QVERIFY(alike.startsWith(QStringLiteral("# Written by OSD/OS")));
        QVERIFY(alike.endsWith(QLatin1Char('\n')));
        for (const char *line : { "fbneo-cpu-speed-adjust = \"100%\"", "fbneo-force-60hz = \"disabled\"",
                                  "fbneo-frameskip-type = \"disabled\"", "fbneo-fixed-frameskip = \"0\"",
                                  "fbneo-samplerate = \"48000\"", "fbneo-socd = \"3\"",
                                  "fbneo-diagnostic-input = \"Hold Start\"", "fbneo-hiscores = \"disabled\"",
                                  "fbneo-allow-patched-romsets = \"disabled\"", "fbneo-neogeo-mode = \"DIPSWITCH\"",
                                  "fbneo-memcard-mode = \"disabled\"", "fbneo-cyclone = \"disabled\"",
                                  "fbneo-analog-speed = \"100%\"" })
            QVERIFY2(alike.split(QLatin1Char('\n')).contains(QLatin1String(line)), line);
        // On OSD/OS's own core for the local network, the same but for the
        // diagnostic input, off there: held START counted in frames played
        // again would open a game's test menu on one machine alone.
        QString onLanCore = libretro::netplayOptionsText(QString(), true);
        QVERIFY2(onLanCore.contains(QStringLiteral("fbneo-diagnostic-input = \"None\"\n")), qPrintable(onLanCore));
        QCOMPARE(onLanCore.replace(QStringLiteral("\"None\""), QStringLiteral("\"Hold Start\"")), alike);
        const QString own = QStringLiteral("fbneo-cpu-speed-adjust = \"150%\"\n"
                                           "fbneo-vertical-mode = \"enabled\"\n"
                                           "  fbneo-vertical-mode = \"TATE\"\n"
                                           "fbneo-neogeo-mode = \"UNIBIOS\"\n"
                                           "fbneo-hiscores = \"enabled\"\n"
                                           "fbneo-allow-depth-32 = \"disabled\"\n"
                                           "fbneo-dipswitch-sf2-Difficulty = \"8 (Hardest)\"\n"
                                           "fbneo-cheat-3-sf2-Infinite_Energy = \"1 - Enabled\"\n"
                                           "fbneo-ips-sf2-0 = \"enabled\"\n"
                                           "fbneo-romdata-sf2-0 = \"enabled\"\n"
                                           "fbneo-debug-dip-1-1 = \"enabled\"\n"
                                           "fbneo-option-of-a-later-core = \"on\"");
        const QString theirs = libretro::netplayOptionsText(own);
        const QStringList theirLines = theirs.split(QLatin1Char('\n'));
        QVERIFY2(theirLines.contains(QStringLiteral("fbneo-vertical-mode = \"TATE\"")), qPrintable(theirs));
        QVERIFY(theirLines.contains(QStringLiteral("fbneo-allow-depth-32 = \"disabled\"")));
        QCOMPARE(theirs.count(QStringLiteral("fbneo-vertical-mode")), 1);
        for (const char *word : { "150%", "UNIBIOS", "dipswitch", "cheat", "ips-", "romdata", "debug", "later-core" })
            QVERIFY2(!theirs.contains(QLatin1String(word)), word);
        // The same lines for how the game runs, whatever this machine's.
        QStringList running = theirLines;
        running.removeAll(QStringLiteral("fbneo-vertical-mode = \"TATE\""));
        running.removeAll(QStringLiteral("fbneo-allow-depth-32 = \"disabled\""));
        QCOMPARE(running, alike.split(QLatin1Char('\n')));
        QVERIFY(!config.contains(QStringLiteral("input_player2_reserved_device")));
        QVERIFY(!config.contains(QStringLiteral("input_player5_select = ")));
        QVERIFY(!config.contains(QStringLiteral("input_player7_")));
        QVERIFY(!config.contains(QStringLiteral("netplay_")));
        // Off line, the user's own core options, as RetroArch keeps them.
        for (const char *key : { "global_core_options", "core_options_path", "game_specific_options" })
            QVERIFY2(!config.contains(QLatin1String(key)), key);
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
        // The file is OSD/OS's while there is none or it wrote it, what it
        // holds read with it; one saved from RetroArch's menu, or put there
        // by hand, is the user's.
        const QString remapFile = libretro::coreRemapPath(dir.path() + QStringLiteral("/remaps"));
        QString held = QStringLiteral("not read");
        QVERIFY(libretro::ownsRemap(remapFile, &held));
        QCOMPARE(held, QString());
        writeFile(remapFile, libretro::remapText(launch).toUtf8());
        QVERIFY(libretro::ownsRemap(remapFile));
        QVERIFY(libretro::ownsRemap(remapFile, &held));
        QCOMPARE(held, libretro::remapText(launch));
        writeFile(remapFile, "input_libretro_device_p1 = \"1\"\ninput_remap_port_p1 = \"0\"\n");
        QVERIFY(!libretro::ownsRemap(remapFile));
        QVERIFY(!libretro::ownsRemap(remapFile, &held));
        QCOMPARE(held, QString());
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
        QCOMPARE(config.count(QStringLiteral("input_player2_up_axis =")), 1);
        QVERIFY(has(QStringLiteral("input_player1_up_axis = \"-1\"")));
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
        // Fit to Screen: the whole screen (Full), its lines not kept whole,
        // whatever the shape and Integer Scale say.
        launch.fill = true;
        config = libretro::configText(launch);
        for (const char *line : { "aspect_ratio_index = \"24\"", "video_aspect_ratio_auto = \"false\"",
                                  "video_scale_integer = \"false\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        QVERIFY(!config.contains(QStringLiteral("video_aspect_ratio = ")));
        launch.fill = false;
        launch.aspect = 0;
        launch.integerScale = false;
        // Straight, unless the monitor is on its side (Settings → Screen →
        // Monitor): then turned a quarter against it, every time.
        QVERIFY(has(QStringLiteral("video_rotation = \"0\"")));
        launch.rotation = 1;
        config = libretro::configText(launch);
        QVERIFY(has(QStringLiteral("video_rotation = \"1\"")));
        launch.rotation = 0;
        // No credits to put in: no network RetroPad. Credits: player 1's,
        // on the port given.
        config = libretro::configText(launch);
        QVERIFY(has(QStringLiteral("network_remote_enable = \"false\"")));
        QVERIFY(!config.contains(QStringLiteral("network_remote_base_port")));
        launch.coinPort = 41234;
        config = libretro::configText(launch);
        for (const char *line : { "network_remote_enable = \"true\"", "network_remote_enable_user_p1 = \"true\"",
                                  "network_remote_base_port = \"41234\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        launch.coinPort = 0;
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
        // Through the relay, and no port asked of the router (UPnP).
        for (const char *line : { "netplay_nickname = \"Mehmet\"", "netplay_public_announce = \"true\"",
                                  "netplay_use_mitm_server = \"true\"", "netplay_mitm_server = \"madrid\"",
                                  "netplay_nat_traversal = \"false\"",
                                  "libretro_directory = \"/cores\"", "libretro_info_path = \"/ra/info\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        // The core's options from OSD/OS's file for the network alone, no
        // game's own over them.
        for (const char *line : { "global_core_options = \"true\"", "core_options_path = \"/ra/config/netplay-options.cfg\"",
                                  "game_specific_options = \"false\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        // Online, each machine plays ahead on a guess, as any RetroArch
        // does: hosted here, two frames of input latency to start with,
        // which the machines joining take, and up to four more for a machine
        // that can't play its frames again in time; the ping off the screen
        // unless asked for.
        for (const char *line : { "netplay_require_slaves = \"false\"", "netplay_input_latency_frames_min = \"2\"",
                                  "netplay_input_latency_frames_range = \"4\"", "netplay_ping_show = \"false\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        launch.pingShown = true;
        config = libretro::configText(launch);
        QVERIFY(has(QStringLiteral("netplay_ping_show = \"true\"")));
        launch.pingShown = false;
        launch.netplay = libretro::Launch::Netplay::Join;
        config = libretro::configText(launch);
        QVERIFY(has(QStringLiteral("netplay_input_latency_frames_min = \"0\"")));
        launch.netplay = libretro::Launch::Netplay::Host;
        config = libretro::configText(launch);

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
        // On the local network the machines follow their host unless told
        // otherwise (LanSync::Follow): nothing guessed, nothing played
        // twice, and no input latency on top. Joining says so too, though
        // it is its host's word that counts...
        for (const char *line : { "netplay_allow_slaves = \"true\"", "netplay_require_slaves = \"true\"",
                                  "netplay_input_latency_frames_min = \"0\"",
                                  "netplay_input_latency_frames_range = \"0\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        // LAN Play's host: on the local network alone, straight, where
        // RetroArch answers the queries made there; not in the lobby, nor
        // through a port opened on the router.
        launch.netplay = libretro::Launch::Netplay::LanHost;
        config = libretro::configText(launch);
        for (const char *line : { "netplay_nickname = \"Mehmet\"", "netplay_start_as_spectator = \"false\"",
                                  "netplay_public_announce = \"false\"", "netplay_use_mitm_server = \"false\"",
                                  "netplay_nat_traversal = \"false\"",
                                  // ...and hosting, whose word every machine joining takes.
                                  "netplay_require_slaves = \"true\"", "netplay_input_latency_frames_min = \"0\"",
                                  "netplay_input_latency_frames_range = \"0\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        QVERIFY(!config.contains(QStringLiteral("netplay_mitm_server ")));
        // Hosted to play ahead (LAN Sync's Responsive): a frame of input
        // latency for every machine, so that most presses are there before
        // their frame is played, and up to three more for one that still
        // can't keep up.
        launch.lanSync = libretro::Launch::LanSync::Predict;
        config = libretro::configText(launch);
        for (const char *line : { "netplay_require_slaves = \"false\"", "netplay_input_latency_frames_min = \"1\"",
                                  "netplay_input_latency_frames_range = \"3\"" })
            QVERIFY2(has(QLatin1String(line)), line);
        launch.lanSync = libretro::Launch::LanSync::Follow;
        config = libretro::configText(launch);
        // Paced by the screen, as an online game and a game alone: the sound
        // stretched and nudged to it as RetroArch has it.
        for (const char *key : { "audio_sync", "audio_max_timing_skew", "audio_rate_control", "vrr_runloop_enable" })
            QVERIFY2(!config.contains(QLatin1String(key)), key);
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
        // a pad's Arcade Buttons among them, else Classic; Classic too in a
        // game of five or six players, where one machine may bring four, one
        // more than RetroArch sends a machine's input for on the panel's. A
        // game whose players aren't known (0) keeps the panel.
        const auto layoutOf = [](const QList<bool> &panels, const QList<QStringList> &arcade, int players = 2) {
            return libretro::hostLayout(panels, arcade, players) == libretro::Launch::Layout::Panel
                       ? QStringLiteral("panel")
                       : QStringLiteral("classic");
        };
        QCOMPARE(layoutOf({}, {}), QStringLiteral("classic"));
        QCOMPARE(layoutOf({ false, false }, { {}, {} }), QStringLiteral("classic"));
        QCOMPARE(layoutOf({ false, true }, {}), QStringLiteral("panel"));
        QCOMPARE(layoutOf({}, { {}, {}, { QStringLiteral("2"), QStringLiteral("3") } }), QStringLiteral("panel"));
        QCOMPARE(layoutOf({ true }, {}, 4), QStringLiteral("panel"));
        QCOMPARE(layoutOf({ true }, {}, 0), QStringLiteral("panel"));
        QCOMPARE(layoutOf({ true }, {}, 5), QStringLiteral("classic"));
        QCOMPARE(layoutOf({}, { { QStringLiteral("2") } }, 6), QStringLiteral("classic"));
        QCOMPARE(layoutOf({}, {}, 6), QStringLiteral("classic"));
        QCOMPARE(libretro::mostPortsOn(libretro::Launch::Layout::Panel), 3);
        QCOMPARE(libretro::mostPortsOn(libretro::Launch::Layout::Classic), libretro::kMostPorts);
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
        // Each machine's players within each, the panel's three ports: one
        // asking for four given three, the next what is left.
        claims.reset(1, 6, libretro::kPanelPorts);
        QCOMPARE(claims.each(), 3);
        QCOMPARE(given("a", 4), QStringLiteral("2+3"));
        QCOMPARE(given("b", 4), QStringLiteral("5+2"));
        QCOMPARE(given("a", 1), QStringLiteral("2+1"));
        // One each, as OSD/OS hosts on the local network on FinalBurn Neo's
        // own core: one port whatever a machine asks for.
        claims.reset(1, 6, 1);
        QCOMPARE(given("a", 2), QStringLiteral("2+1"));
        QCOMPARE(given("b", 4), QStringLiteral("3+1"));
        QCOMPARE(given("c", 1), QStringLiteral("4+1"));
        claims.reset(1, 6, 0);
        QCOMPARE(claims.each(), 1);
        claims.reset(1);
        QCOMPARE(claims.each(), libretro::kMostPorts);
        // A claim kept while its address is connected, or until it is grace
        // old; dropped once its connection is gone, or never came. One
        // claimed again (a joiner rejoining from the same address) is given
        // its time anew.
        claims.reset(1, 6, 1);
        QCOMPARE(claims.claim(QStringLiteral("a"), 1, 0).first, 2);
        QCOMPARE(claims.claim(QStringLiteral("b"), 1, 0).first, 3);
        QVERIFY(claims.awaiting());
        QCOMPARE(claims.expire({ QStringLiteral("a") }, 1000, 5000), QStringList());
        QVERIFY(claims.awaiting());
        QCOMPARE(claims.expire({ QStringLiteral("a") }, 5000, 5000), QStringList({ QStringLiteral("b") }));
        QVERIFY(!claims.awaiting());
        QCOMPARE(claims.claim(QStringLiteral("c"), 1, 5000).first, 3);
        QCOMPARE(claims.expire({}, 6000, 5000), QStringList({ QStringLiteral("a") }));
        QCOMPARE(claims.claim(QStringLiteral("a"), 1, 6000).first, 2);
        QCOMPARE(claims.expire({ QStringLiteral("c") }, 7000, 5000), QStringList());
        QCOMPARE(claims.expire({ QStringLiteral("c") }, 10999, 5000), QStringList());
        QCOMPARE(claims.expire({ QStringLiteral("c") }, 11000, 5000), QStringList({ QStringLiteral("a") }));
        QVERIFY(!claims.empty());
        claims.release(QStringLiteral("c"));
        QVERIFY(claims.empty());
        // The connections to RetroArch's port as Linux's tables have them
        // (IPv4, and an IPv4 peer of an IPv6 socket), and as netstat does
        // on Windows, macOS and the BSDs: established ones alone.
        const QByteArray tcp =
            "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
            "   0: 00000000:D88B 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 1 1\n"
            "   1: 0501A8C0:D88B 1401A8C0:C35A 01 00000000:00000000 00:00000000 00000000  1000        0 2 1\n"
            "   2: 0501A8C0:D88B 1501A8C0:C35B 06 00000000:00000000 00:00000000 00000000  1000        0 3 1\n"
            "   3: 0501A8C0:1F90 1601A8C0:C35C 01 00000000:00000000 00:00000000 00000000  1000        0 4 1\n"
            "   4: 0000000000000000FFFF00000501A8C0:D88B 0000000000000000FFFF00001701A8C0:C35D 01 0:0 0:0 0 1000 0 5 1\n";
        QCOMPARE(libretro::procNetPeers(tcp, libretro::kLanPort),
                 QSet<QString>({ QStringLiteral("192.168.1.20"), QStringLiteral("192.168.1.23") }));
        const QByteArray windows =
            "  Proto  Local Address          Foreign Address        State\n"
            "  TCP    0.0.0.0:55435          0.0.0.0:0              LISTENING\n"
            "  TCP    192.168.1.5:55435      192.168.1.20:51234     ESTABLISHED\n"
            "  TCP    192.168.1.5:8080       192.168.1.22:51236     ESTABLISHED\n"
            "  TCP    [::ffff:192.168.1.5]:55435  [::ffff:192.168.1.21]:51235  ESTABLISHED\n";
        QCOMPARE(libretro::netstatPeers(windows, libretro::kLanPort),
                 QSet<QString>({ QStringLiteral("192.168.1.20"), QStringLiteral("192.168.1.21") }));
        const QByteArray bsd =
            "tcp4       0      0  192.168.1.5.55435      192.168.1.20.51234     ESTABLISHED\n"
            "tcp4       0      0  *.55435                *.*                    LISTEN\n"
            "tcp4       0      0  192.168.1.5.55435      192.168.1.24.51240     TIME_WAIT\n";
        QCOMPARE(libretro::netstatPeers(bsd, libretro::kLanPort), QSet<QString>({ QStringLiteral("192.168.1.20") }));
        // And as this machine's kernel writes them: a connection made here
        // found by its port, over IPv4 and over IPv6 to a dual-stack socket.
        for (const QHostAddress &listen : { QHostAddress(QHostAddress::LocalHost), QHostAddress(QHostAddress::Any) }) {
            QTcpServer server;
            QVERIFY(server.listen(listen, 0));
            QTcpSocket client;
            client.connectToHost(QHostAddress::LocalHost, server.serverPort());
            QVERIFY(client.waitForConnected(3000));
            QTRY_VERIFY(server.hasPendingConnections() || server.waitForNewConnection(100));
            QByteArray table;
            for (const char *path : { "/proc/net/tcp", "/proc/net/tcp6" }) {
                QFile file{ QString::fromLatin1(path) };
                if (file.open(QIODevice::ReadOnly))
                    table += file.readAll() + '\n';
            }
            QVERIFY2(libretro::procNetPeers(table, server.serverPort()).contains(QStringLiteral("127.0.0.1")),
                     table.constData());
        }
        // RetroArch's host's words for a client gone, named or not.
        QVERIFY(libretro::isClientHangup(QStringLiteral("[INFO] [Netplay] \"ann\" has disconnected")));
        QVERIFY(libretro::isClientHangup(QStringLiteral("[INFO] [Netplay] A netplay client has disconnected")));
        QVERIFY(!libretro::isClientHangup(QStringLiteral("[INFO] [Netplay] Netplay disconnected")));
        QVERIFY(!libretro::isClientHangup(QStringLiteral("\"ann\" has disconnected")));

        // A network game's summary, from RetroArch 1.20's own lines (and,
        // loosely, a later one's on stalls, catch-ups and savestates); the
        // account signed in, and hardcore paused or disabled as rcheevos and
        // RetroArch say it.
        {
            libretro::NetplaySummary summary;
            for (const char *line : {
                     "[INFO] [RCHEEVOS]: player logged in successfully",
                     "[INFO] [Netplay] Got connection from: \"ann\"",
                     "[INFO] [Netplay] ann has joined as player 2 (ping: 14 ms)",
                     "[INFO] [Netplay] bob (2) has joined with input devices 3, 4 (ping: 31 ms)",
                     "[INFO] [Netplay] eve has joined as player 5",
                     "[WARN] [Netplay] Netplay CRCs mismatch!",
                     "[ERROR] [Netplay] Netplay savestate loading failed: Prepare for desync!",
                     "[INFO] [Netplay] Netplay peer \"ann\" paused",
                     "[INFO] [Netplay] Stalled waiting for a peer, catching up",
                     "[INFO] [Netplay] \"ann\" has disconnected",
                     "[INFO] [Netplay] A netplay client has disconnected",
                     "[INFO] [RCHEEVOS]: Hardcore paused. Setting not allowed: fbneo-allow-patched-romsets=enabled",
                     "[INFO] [RCHEEVOS]: Game 123 loaded, hardcore disabled",
                     "[INFO] [RCHEEVOS]: Game 123 loaded, hardcore disabled",
                     "[INFO] [RCHEEVOS]: Hardcore enabled",
                     "[libretro INFO] [FBNeo] Driver successfully started" })
                summary.read(QLatin1String(line));
            QCOMPARE(summary.text(),
                     QStringLiteral("peers: ann 14 ms, bob (2) 31 ms, eve; left 2; stalls 1, catch-ups 1, savestates 1, "
                                    "CRC mismatches 1, desyncs 1, pauses 1, netplay errors 1; achievements: signed in, "
                                    "\"Hardcore paused. Setting not allowed: fbneo-allow-patched-romsets=enabled\", "
                                    "\"Game 123 loaded, hardcore disabled\""));
            libretro::NetplaySummary joined;
            for (const char *line : { "[INFO] [Netplay] You have joined with input devices 2, 3 (ping: 23 ms)",
                                      "[INFO] [RCHEEVOS]: RetroAchievements login failed: No response from server",
                                      "[INFO] [Netplay] Netplay disconnected" })
                joined.read(QLatin1String(line));
            QCOMPARE(joined.text(), QStringLiteral("peers: the host 23 ms; left 1; stalls 0, catch-ups 0, savestates 0, "
                                                   "CRC mismatches 0, desyncs 0, pauses 0, netplay errors 0; achievements: "
                                                   "not signed in (No response from server)"));
            QCOMPARE(libretro::NetplaySummary().text(),
                     QStringLiteral("peers: none logged; left 0; stalls 0, catch-ups 0, savestates 0, CRC mismatches 0, "
                                    "desyncs 0, pauses 0, netplay errors 0; achievements: nothing logged"));
        }

        // The host's OSD/OS: a claim never followed by a connection given up
        // once it is old enough, said in the log.
        {
            LanRooms lan;
            QUdpSocket probe;
            QVERIFY(probe.bind(QHostAddress::LocalHost, 0));
            const quint16 claimPort = probe.localPort();
            probe.close();
            lan.setClaimPort(claimPort);
            QSet<QString> connected;
            lan.setPeerSource([&connected] { return std::optional<QSet<QString>>(connected); });
            lan.setClaimGrace(300);
            lan.setClaimCheckInterval(50);
            QVERIFY(lan.startClaims(1, 6, 1, libretro::Launch::Layout::Classic));
            QUdpSocket joiner;
            QVERIFY(joiner.bind(QHostAddress::LocalHost, 0));
            const auto ask = [&] {
                joiner.writeDatagram(libretro::claimQuery(1), QHostAddress::LocalHost, claimPort);
                for (int waited = 0; waited < 3000; waited += 20) {
                    while (joiner.hasPendingDatagrams()) {
                        if (const auto r = libretro::readClaimAnswer(joiner.receiveDatagram().data()))
                            return r->first;
                    }
                    QTest::qWait(20);
                }
                return -1;
            };
            QCOMPARE(ask(), 2);
            // Connected: kept past the grace.
            connected = { QStringLiteral("127.0.0.1") };
            QTest::qWait(500);
            // Gone: freed at the next look, as at once after a client left.
            connected.clear();
            QTest::ignoreMessage(QtInfoMsg, "[Arcade] 127.0.0.1 has no connection to the game: its ports are free again");
            lan.clientLeft();
            // Rejoining from the same address: its port again, and its time.
            QCOMPARE(ask(), 2);
            QTest::ignoreMessage(QtInfoMsg, "[Arcade] 127.0.0.1 has no connection to the game: its ports are free again");
            QTest::qWait(600);
            lan.stopClaims();
        }

        // Joining: the host's answer taken from the local network alone, as
        // the rooms' answers and the claims are. Ports from another machine
        // there are given back to it; from the internet, a carrier's shared
        // addresses, a link-local address or IPv6 nothing is read, and
        // nothing sent back. The host's own layout and ports start the game.
        {
            LanRooms lan;
            QUdpSocket other;
            QVERIFY(other.bind(QHostAddress::LocalHost, 0));
            lan.setClaimPort(other.localPort());
            int answered = 0;
            std::optional<libretro::PortRange> given;
            libretro::Launch::Layout layout = libretro::Launch::Layout::Classic;
            connect(&lan, &LanRooms::portsAnswered, &lan,
                    [&](const std::optional<libretro::PortRange> &ports, libretro::Launch::Layout said) {
                        ++answered;
                        given = ports;
                        layout = said;
                    });
            // The host this machine joins: another address of this one.
            QCOMPARE(lan.askPort(), quint16(0));
            QVERIFY(lan.askForPorts(QStringLiteral("127.0.0.2"), 1));
            const quint16 asked = lan.askPort();
            QVERIFY(asked != 0);
            const auto from = [](const char *address, const QByteArray &data) {
                QNetworkDatagram datagram(data);
                datagram.setSender(QHostAddress(QLatin1String(address)), 55436);
                return datagram;
            };
            for (const char *forged : { "203.0.113.9", "8.8.8.8", "100.64.0.1", "169.254.1.1", "fd00::1", "fe80::1" }) {
                QVERIFY2(!lan.takePortsAnswer(from(forged, libretro::claimLayout(libretro::Launch::Layout::Panel))), forged);
                QVERIFY2(!lan.takePortsAnswer(from(forged, libretro::claimAnswer({ 2, 1 }))), forged);
                QVERIFY2(!lan.releasing(QLatin1String(forged)), forged);
            }
            QVERIFY(!lan.takePortsAnswer(from("127.0.0.1", libretro::claimAnswer({ 4, 1 }))));
            QVERIFY(lan.releasing(QStringLiteral("127.0.0.1")));
            QTRY_VERIFY(other.hasPendingDatagrams());
            QCOMPARE(other.receiveDatagram().data(), libretro::claimRelease());
            // Sent again a moment later, then its socket closed.
            QVERIFY(lan.releaseSocketOpen());
            QTRY_VERIFY(other.hasPendingDatagrams());
            QCOMPARE(other.receiveDatagram().data(), libretro::claimRelease());
            QTRY_VERIFY(!lan.releaseSocketOpen());
            QVERIFY(!lan.releasing(QStringLiteral("127.0.0.1")));
            QCOMPARE(answered, 0);
            QVERIFY(!lan.takePortsAnswer(from("127.0.0.2", libretro::claimLayout(libretro::Launch::Layout::Panel))));
            QTest::ignoreMessage(QtInfoMsg,
                                 "[Arcade] 127.0.0.2 gives this machine's 1 player(s) 1 port(s) from 3, on the panel layout");
            QVERIFY(lan.takePortsAnswer(from("127.0.0.2", libretro::claimAnswer({ 3, 1 }))));
            QCOMPARE(answered, 1);
            QVERIFY(given);
            QCOMPARE(given->first, 3);
            QCOMPARE(layout, libretro::Launch::Layout::Panel);
            // Answered: the ask's socket closed, its port free for anyone.
            QCOMPARE(lan.askPort(), quint16(0));
            {
                QUdpSocket again;
                QVERIFY2(again.bind(QHostAddress(QHostAddress::AnyIPv4), asked), qPrintable(again.errorString()));
            }
            // Never answered (asked three times, a quarter of a second
            // each): closed as it gives up, and the host told to give back
            // what an answer that comes later would have had given back.
            answered = 0;
            QVERIFY(lan.askForPorts(QStringLiteral("127.0.0.2"), 1));
            const quint16 unanswered = lan.askPort();
            QVERIFY(unanswered != 0);
            QTRY_COMPARE_WITH_TIMEOUT(answered, 1, 3000);
            QVERIFY(!given);
            QCOMPARE(lan.askPort(), quint16(0));
            QVERIFY(lan.releasing(QStringLiteral("127.0.0.2")));
            {
                QUdpSocket again;
                QVERIFY2(again.bind(QHostAddress(QHostAddress::AnyIPv4), unanswered), qPrintable(again.errorString()));
            }
            // A host that can't be asked at all: nothing left bound.
            QVERIFY(!lan.askForPorts(QStringLiteral("not an address"), 1));
            QCOMPARE(lan.askPort(), quint16(0));
        }

        // The LAN query's socket: bound for the second its answers are
        // listened for, closed once they have been (its port free for
        // anyone), and bound afresh for the next query; closed too as LAN
        // Play is turned off with a query under way. An answer to it is
        // still heard.
        {
            QUdpSocket hosts;
            QVERIFY(hosts.bind(QHostAddress::LocalHost, 0));
            connect(&hosts, &QUdpSocket::readyRead, &hosts, [&hosts] {
                while (hosts.hasPendingDatagrams()) {
                    const QNetworkDatagram query = hosts.receiveDatagram();
                    if (query.data() == libretro::lanQuery())
                        hosts.writeDatagram(lanAnswer("ann", "sf2", 55435), query.senderAddress(), query.senderPort());
                }
            });
            LanRooms lan;
            lan.setDiscovery(QStringLiteral("127.0.0.1"), hosts.localPort());
            lan.setListenTime(200);
            QSignalSpy read(&lan, &LanRooms::roomsRead);
            QCOMPARE(lan.queryPort(), quint16(0));
            lan.query();
            const quint16 first = lan.queryPort();
            QVERIFY(first != 0);
            QVERIFY(read.wait(3000));
            QCOMPARE(lan.rooms().size(), qsizetype(1));
            QCOMPARE(lan.queryPort(), quint16(0));
            {
                QUdpSocket again;
                QVERIFY2(again.bind(QHostAddress(QHostAddress::AnyIPv4), first), qPrintable(again.errorString()));
            }
            lan.query();
            QVERIFY(lan.queryPort() != 0);
            QVERIFY(read.wait(3000));
            QCOMPARE(lan.rooms().size(), qsizetype(1));
            QCOMPARE(lan.queryPort(), quint16(0));
            lan.query();
            QVERIFY(lan.queryPort() != 0);
            lan.forget();
            QCOMPARE(lan.queryPort(), quint16(0));
        }

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
        // OSD/OS's LAN core: joined, with it.
        QVERIFY(libretro::readLanAnswer(lanAnswer("cy", "akatana", 55435, 0, "1.22.2", 0, "OSD-OS LAN Core"),
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
        // Joining, the log read a line at a time: connected to the host, its
        // errors then end nothing, even one that would have turned the join
        // down before; the connection ending then is the host gone. Ended
        // before it was connected, the join failed, why the last error
        // before it, or the disconnection itself. A host's log never says
        // so of itself, and its start failing ends its game as before.
        const auto readLines = [](libretro::NetplayLog log, const QStringList &lines) {
            for (const QString &line : lines)
                log.read(line);
            return log;
        };
        const QString connected = QStringLiteral("[INFO] [Netplay] Connected to: \"ann\"");
        const QString disconnected = QStringLiteral("[INFO] [Netplay] Netplay disconnected");
        libretro::NetplayLog joined = readLines(libretro::NetplayLog(),
                                                { QStringLiteral("[INFO] [Netplay] Connecting to netplay host 192.168.1.20:55435."),
                                                  connected,
                                                  QStringLiteral("[ERROR] [Netplay] Failed to receive header from host."),
                                                  QStringLiteral("[ERROR] [Netplay] NETPLAY_CMD_INPUT received an unexpected payload size.") });
        QVERIFY(!joined.hostLeft());
        QVERIFY(joined.failure().isEmpty());
        joined = readLines(joined, { disconnected, QStringLiteral("[ERROR] [Netplay] Failed to initialize netplay.") });
        QVERIFY(joined.hostLeft());
        QVERIFY(joined.failure().isEmpty());
        joined = readLines(libretro::NetplayLog(), { QStringLiteral("[ERROR] [Netplay] Netplay input out of order."), disconnected });
        QVERIFY(!joined.hostLeft());
        QCOMPARE(joined.failure(), QStringLiteral("Netplay input out of order"));
        QCOMPARE(readLines(libretro::NetplayLog(), { disconnected }).failure(), QStringLiteral("Netplay disconnected"));
        QCOMPARE(readLines(libretro::NetplayLog(), { QStringLiteral("[ERROR] [Netplay] Incorrect password."), connected,
                                                     disconnected }).failure(),
                 QStringLiteral("Incorrect password"));
        const libretro::NetplayLog hosted = readLines(libretro::NetplayLog(true), { connected, disconnected });
        QVERIFY(!hosted.hostLeft());
        QVERIFY(hosted.failure().isEmpty());
        QCOMPARE(readLines(libretro::NetplayLog(true), { QStringLiteral("[ERROR] [Netplay] Failed to bind port 55435."),
                                                        QStringLiteral("[ERROR] [Netplay] Failed to initialize netplay.") })
                     .failure(),
                 QStringLiteral("Failed to bind port 55435"));
        writeFile(dir.path() + QStringLiteral("/left.log"),
                  "[INFO] [Netplay] Connected to: \"ann\"\n"
                  "[libretro INFO] [FBNeo] Driver successfully started\n"
                  "[ERROR] [Netplay] NETPLAY_CMD_INPUT received an unexpected payload size.\n"
                  "[INFO] [Netplay] Netplay disconnected\n");
        run = libretro::readLog(dir.path() + QStringLiteral("/left.log"));
        QVERIFY(run.hostLeft);
        QVERIFY(run.netplayFailure.isEmpty());
        QVERIFY(!libretro::readLog(dir.path() + QStringLiteral("/left.log"), true).hostLeft);
        QVERIFY(!libretro::readLog(dir.path() + QStringLiteral("/joining.log")).hostLeft);

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
        QCOMPARE(whole.size(), 12);
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
        QCOMPARE(ids, QStringList({ "22", "21", "20", "19", "18+", "17", "16", "15", "14+", "13", "11+", "10" }));
        // What the lobby doesn't say, as such; the relay another runs by its
        // name; a name that can't be a set's none, the host's name as it is.
        QCOMPARE(whole.at(10).players, -1);
        QVERIFY(!whole.at(10).created.isValid());
        QVERIFY(whole.at(10).relay.isEmpty());
        QCOMPARE(whole.at(8).relay, QStringLiteral("198.51.100.7"));
        QVERIFY(whole.at(2).game.isEmpty());
        QCOMPARE(whole.at(2).rom, QStringLiteral("../../etc/passwd"));
        QVERIFY(!whole.at(7).connectable);
        QCOMPARE(whole.at(4).host, QStringLiteral("img src=http://192.0.2.99/a.pngz"));
        // A room reached straight at an address inside a network (here a
        // home network's) isn't joined from a public list, and says so.
        QCOMPARE(whole.at(9).id, 13);
        QVERIFY(whole.at(9).insideAddress);
        QVERIFY(whole.at(9).joinAddress.isEmpty());
        QVERIFY(!whole.at(9).joinable);
        QVERIFY(!whole.at(1).insideAddress);
        // Each such address, and a name, refused straight; an address on the
        // internet joined; a relayed room joined at its relay whatever its
        // host's address.
        const auto straight = [](const char *ip, int hostMethod = 0, const char *relay = "") {
            return libretro::readWholeLobby(QByteArray("[{\"fields\": {\"id\": 1, \"game_name\": \"sf2\", "
                                                       "\"core_name\": \"FinalBurn Neo\", \"retroarch_version\": \"1.22.2\", "
                                                       "\"port\": 55435, \"mitm_port\": 55435, \"mitm_session\": \"c2Vzc2lvbg==\", \"ip\": \"")
                                            + ip + "\", \"host_method\": " + QByteArray::number(hostMethod)
                                            + ", \"mitm_ip\": \"" + relay + "\"}}]")
                .value(0);
        };
        for (const char *inside : { "192.168.1.40", "10.0.0.2", "172.20.1.1", "127.0.0.1", "127.8.8.8", "169.254.3.4",
                                    "100.64.1.1", "0.0.0.0", "224.0.0.251", "255.255.255.255", "::1", "::", "fe80::1",
                                    "fd00::2", "fc12::1", "ff02::1", "::ffff:192.168.1.40", "localhost",
                                    "router.lan" }) {
            const libretro::Room room = straight(inside);
            QVERIFY2(!room.joinable && room.joinAddress.isEmpty() && room.insideAddress, inside);
        }
        for (const char *outside : { "203.0.113.7", "8.8.8.8", "172.32.0.1", "100.128.0.1", "2001:db8::7" }) {
            const libretro::Room room = straight(outside);
            QVERIFY2(room.joinable && !room.insideAddress, outside);
            QCOMPARE(room.joinAddress, QLatin1String(outside) + QStringLiteral("|55435"));
        }
        const libretro::Room relayed = straight("192.168.1.40", 3, "us-east1.relay.retroarch.com");
        QVERIFY(relayed.joinable);
        QVERIFY(!relayed.insideAddress);
        QCOMPARE(relayed.joinAddress, QStringLiteral("us-east1.relay.retroarch.com|55435|c2Vzc2lvbg=="));
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

    // The Arcade module's list of the games of five buttons or more, as it
    // ships (modules/arcade/five-button-sets.txt, made from MAME's -listxml
    // by scripts/make-five-button-sets.py): the MAME it was made from named
    // first, then the sets one a line, sorted, each once; the six-button
    // fighters in it, parents and clones, and the games of four buttons or
    // fewer not.
    void fiveButtonSets() {
        QFile file(QStringLiteral(TESTS_DIR "/../modules/arcade/five-button-sets.txt"));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        QVERIFY(lines.takeFirst().startsWith(QStringLiteral("# MAME 0.")));
        QCOMPARE(lines.takeLast(), QString());
        QVERIFY(lines.size() > 1000);
        QStringList sorted = lines;
        sorted.sort();
        sorted.removeDuplicates();
        QCOMPARE(lines, sorted);
        for (const QString &set : std::as_const(lines))
            QVERIFY2(!set.isEmpty() && set == set.trimmed() && !set.startsWith(QLatin1Char('#')), qPrintable(set));
        for (const char *set : { "sf2", "sf2ce", "ssf2t", "mk", "mk2", "kinst", "sfa3" })
            QVERIFY2(lines.contains(QLatin1String(set)), set);
        for (const char *set : { "pacman", "mslug", "kof98", "galaga", "dino" })
            QVERIFY2(!lines.contains(QLatin1String(set)), set);
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
        writeFiveButtonSets(data.path(), "sf2\n");

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
        QVERIFY(config.contains(reservedLine(1, QStringLiteral("045e:028e Xbox 360 Controller"))));
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
        // Show Stats, from the next game: off unless set, then the frame
        // rate and OSD/OS's key for the machine's state; signed in to
        // RetroAchievements, in RetroArch's widgets.
        QVERIFY(!config.contains(QStringLiteral("fps_show")));
        arcade.onSettingChanged(module, QStringLiteral("show_stats"), QStringLiteral("FPS + System"));
        played(arcade, QStringLiteral("sf2"));
        config = QString::fromUtf8(contents(configCopy));
        QVERIFY(config.contains(QStringLiteral("fps_show = \"true\"")));
        QVERIFY(config.contains(QStringLiteral("osdos_system_show = \"true\"")));
        QVERIFY(!config.contains(QStringLiteral("menu_enable_widgets")));
        arcade.onSettingChanged(module, QStringLiteral("show_stats"), QVariant());
        played(arcade, QStringLiteral("sf2"));
        QVERIFY(!contents(configCopy).contains("fps_show"));
        controllers::setPlayerEnvironment({ { QStringLiteral("OSDOS_PLAYER1_NAME"), QStringLiteral("OSD/OS JAMMA P1") },
                                            { QStringLiteral("OSDOS_PLAYER1_GUID"),
                                              QStringLiteral("06002868010000000100000001000000") },
                                            { QStringLiteral("OSDOS_PLAYER2_NAME"), QStringLiteral("OSD/OS JAMMA P2") },
                                            { QStringLiteral("OSDOS_PLAYER2_GUID"),
                                              QStringLiteral("06006869010000000200000001000000") } });
        played(arcade, QStringLiteral("sf2"));
        config = QString::fromUtf8(contents(configCopy));
        QVERIFY(config.contains(reservedLine(1, QStringLiteral("0001:0001 OSD/OS JAMMA P1"))));
        QVERIFY(config.contains(reservedLine(2, QStringLiteral("0001:0002 OSD/OS JAMMA P2"))));
        for (const char *line : { "input_player1_y_btn = \"0\"", "input_player2_b_btn = \"3\"",
                                  "input_player3_b_btn = \"0\"" })
            QVERIFY2(config.contains(QLatin1String(line)), line);
        QCOMPARE(QString::fromUtf8(contents(remap)).split(QLatin1Char('\n')).filter(QStringLiteral("input_libretro_device_p")),
                 QStringList({ QStringLiteral("input_libretro_device_p1 = \"261\""),
                               QStringLiteral("input_libretro_device_p2 = \"261\"") }));
        // Started again with the same players, the file as it is to be is
        // left alone: not written anew (and flushed to the card) at every
        // game.
        {
            const QByteArray before = contents(remap);
            const FolderWatch watch(QFileInfo(remap).path());
            played(arcade, QStringLiteral("sf2"));
            QCOMPARE(contents(remap), before);
            if (watch.fd >= 0) {
                QStringList cameIn;
                QStringList writtenTo;
                watch.read(cameIn, writtenTo);
                QVERIFY2(cameIn.isEmpty(), qPrintable(cameIn.join(QLatin1Char(' '))));
                QVERIFY2(writtenTo.isEmpty(), qPrintable(writtenTo.join(QLatin1Char(' '))));
            } else {
                qWarning("the remap folder can't be watched here (inotify): that it isn't written again isn't checked");
            }
        }
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
        // On Auto: Street Fighter II, in the module's list of the games of
        // five buttons or more, on Fighting 6's row (X Y RB over A B RT);
        // Galaga, not in it, on Neo Geo 4's (A B X Y on Button 1 to 4: the
        // 6-Button Panel's Y X L B slots).
        controllers::setPlayerArcade({ autoBinds() });
        played(arcade, QStringLiteral("sf2"));
        config = QString::fromUtf8(contents(configCopy));
        for (const char *line : { "input_player1_y_btn = \"2\"", "input_player1_l_btn = \"10\"",
                                  "input_player1_b_btn = \"0\"", "input_player1_r_axis = \"+5\"" })
            QVERIFY2(config.contains(QLatin1String(line)), line);
        QCOMPARE(arcade.engineOf(QStringLiteral("galaga")), QStringLiteral("fbneo"));
        played(arcade, QStringLiteral("galaga"));
        config = QString::fromUtf8(contents(configCopy));
        for (const char *line : { "input_player1_y_btn = \"0\"", "input_player1_x_btn = \"1\"",
                                  "input_player1_l_btn = \"2\"", "input_player1_b_btn = \"3\"",
                                  "input_player1_r_btn = \"10\"", "input_player1_r_axis = \"nul\"" })
            QVERIFY2(config.contains(QLatin1String(line)), line);
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
        QVERIFY(config.contains(QStringLiteral("video_rotation = \"0\"")));
        // Vertical Games: a monitor turned for it, Galaga turned against it,
        // in its shape turned.
        arcade.onSettingChanged(module, QStringLiteral("vertical_monitor"), QStringLiteral("Turned Left"));
        played(arcade, QStringLiteral("galaga"));
        arcade.onSettingChanged(module, QStringLiteral("vertical_monitor"), QStringLiteral("Horizontal"));
        config = QString::fromUtf8(contents(configCopy));
        QVERIFY2(config.contains(QStringLiteral("video_rotation = \"3\"")), qPrintable(config));
        QVERIFY(config.contains(QStringLiteral("video_aspect_ratio = \"1.333333\"")));
        // The game's own monitor (its options' Monitor), as FinalBurn Neo
        // plays and hosts it.
        arcade.onSettingChanged(module, QStringLiteral("monitors"), QVariantMap{ { QStringLiteral("galaga"), QStringLiteral("Turned Right") } });
        played(arcade, QStringLiteral("galaga"));
        arcade.onSettingChanged(module, QStringLiteral("monitors"), QVariantMap());
        QVERIFY(QString::fromUtf8(contents(configCopy)).contains(QStringLiteral("video_rotation = \"1\"")));
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
        // Vertical Games: the games whose screen stood upright, the one a
        // monitor turned on its side is for.
        QVERIFY(setsOf(arcade, QStringLiteral("vertical")).contains(QStringLiteral("1943")));
        QVERIFY(!setsOf(arcade, QStringLiteral("vertical")).contains(QStringLiteral("kof98")));

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
        writeFile(cache, R"({"version": 6, "unknownBefore": ["kof98"]})");
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

        // A device's sign-in (Export to OSD/OS Setup), its token in the
        // password's place: taken as it is, signed in with no check, the
        // file gone.
        QFile::remove(kept);
        writeFile(setup, R"({"user": " carried ", "token": "tok789", "key": "k5y"})");
        {
            QFile::remove(m_log);
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            prepare(arcade);
            QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(setup), 5000);
            QCOMPARE(arcade.achievementsState(), QStringLiteral("signedIn"));
            QCOMPARE(account().value(QStringLiteral("user")).toString(), QStringLiteral("carried"));
            QCOMPARE(account().value(QStringLiteral("token")).toString(), QStringLiteral("tok789"));
            QCOMPARE(account().value(QStringLiteral("key")).toString(), QStringLiteral("k5y"));
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

    // A file's CRC-32, as a zip's table gives it.
    static quint32 crc32Of(const QByteArray &data) {
        quint32 crc = 0xFFFFFFFFu;
        for (const char byte : data) {
            crc ^= quint8(byte);
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }

    // A zip of those files, stored as they are.
    static QByteArray zipOf(const QList<std::pair<QByteArray, QByteArray>> &files) {
        QByteArray out;
        QByteArray central;
        const auto le16 = [](QByteArray &b, quint32 v) {
            b.append(char(v & 0xFF));
            b.append(char((v >> 8) & 0xFF));
        };
        const auto le32 = [&le16](QByteArray &b, quint32 v) {
            le16(b, v & 0xFFFF);
            le16(b, v >> 16);
        };
        for (const auto &[name, data] : files) {
            const quint32 crc = crc32Of(data);
            const quint32 offset = quint32(out.size());
            le32(out, 0x04034b50);
            for (int i = 0; i < 5; ++i)   // version, flags, method, time, date
                le16(out, i == 0 ? 20 : 0);
            le32(out, crc);
            le32(out, quint32(data.size()));
            le32(out, quint32(data.size()));
            le16(out, quint32(name.size()));
            le16(out, 0);
            out += name;
            out += data;
            le32(central, 0x02014b50);
            for (int i = 0; i < 6; ++i)   // made by, version, flags, method, time, date
                le16(central, i < 2 ? 20 : 0);
            le32(central, crc);
            le32(central, quint32(data.size()));
            le32(central, quint32(data.size()));
            le16(central, quint32(name.size()));
            for (int i = 0; i < 4; ++i)   // extra, comment, disk, internal
                le16(central, 0);
            le32(central, 0);
            le32(central, offset);
            central += name;
        }
        const quint32 centralAt = quint32(out.size());
        out += central;
        le32(out, 0x06054b50);
        le16(out, 0);
        le16(out, 0);
        le16(out, quint32(files.size()));
        le16(out, quint32(files.size()));
        le32(out, quint32(central.size()));
        le32(out, centralAt);
        le16(out, 0);
        return out;
    }

    // MAME's -verifyroms read as it comes, a line cut anywhere.
    void verifyReader() {
        romcheck::VerifyReader reader;
        reader.addData("mslug       : sp-s2.sp1 (131072 bytes) - NOT FOUND\nmslug       : pal.bin - NOT FOUND - NO GOOD DUMP KNOWN\r\n");
        reader.addData("romset mslug [neogeo] is b");
        QVERIFY(reader.take().isEmpty());
        reader.addData("ad\r\nromset pacman [puckman] is best available\nromset galaga is good");
        QHash<QString, romcheck::Verdict> verdicts = reader.take();
        QCOMPARE(verdicts.size(), 2);
        QVERIFY(!verdicts.value(QStringLiteral("mslug")).good);
        QCOMPARE(verdicts.value(QStringLiteral("mslug")).missing, QStringList({ QStringLiteral("sp-s2.sp1") }));
        QVERIFY(verdicts.value(QStringLiteral("pacman")).good);
        reader.finish();
        verdicts = reader.take();
        QVERIFY(verdicts.value(QStringLiteral("galaga")).good);
    }

    // A check held (a game starting) as MAME has answered for the last of
    // its sets and has only to exit, then let go: done, with what MAME said.
    // MAME isn't asked again with no set to check, which would have it check
    // every machine it knows.
    void romCheckLetGoWithNothingLeft() {
        RomCheck::Job job;
        job.mame = mamePath();
        job.rompath = m_games;
        job.workDir = m_data.path();
        job.mameFrom = QStringLiteral("a mame");
        job.mameSets = QStringList{ QStringLiteral("pacman"), QStringLiteral("sf2") };
        qputenv("MAME_VERIFY_LINGER", "5");
        const auto linger = qScopeGuard([] { qunsetenv("MAME_VERIFY_LINGER"); });
        QFile::remove(m_log);
        RomCheck check;
        QSignalSpy done(&check, &RomCheck::done);
        check.start(job, false);
        const auto progress = [&check](const char *key) { return check.progress().value(QLatin1String(key)); };
        QTRY_VERIFY_WITH_TIMEOUT(progress("stage").toString() == QLatin1String("MAME") && progress("done").toInt() == 2,
                                 10000);
        QVERIFY(check.active());
        check.pause();
        QVERIFY(check.active());
        QVERIFY(progress("held").toBool());
        QVERIFY(done.isEmpty());
        check.resume();
        QVERIFY(!check.active());
        QCOMPARE(progress("result").toString(), QStringLiteral("Done"));
        QCOMPARE(done.size(), 1);
        const romcheck::Results results = done.first().first().value<romcheck::Results>();
        QCOMPARE(results.sets.value(QStringLiteral("pacman")).mame, 1);
        QCOMPARE(results.sets.value(QStringLiteral("sf2")).mame, -1);
        QCOMPARE(logLines(QStringLiteral("verifyroms")),
                 QStringList({ QStringLiteral("verifyroms ") + m_games, QStringLiteral("verifyroms pacman sf2") }));
    }

    // The pictures' records (RecordFile): read by folder, written whole off
    // the GUI thread, their names in order, only once they have changed and
    // only where there is a folder.
    void recordFile() {
        struct Entry {
            int n = 0;
            static Entry fromJson(const QJsonObject &json) { return { json.value(QStringLiteral("n")).toInt() }; }
            QJsonObject toJson() const { return { { QStringLiteral("n"), n } }; }
        };
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.path() + QStringLiteral("/.record.json");
        QThreadPool saves;
        saves.setMaxThreadCount(1);
        RecordFile<Entry> record(QStringLiteral(".record.json"));
        QVERIFY(record.folder().isEmpty());
        record.load(dir.path());
        QCOMPARE(record.folder(), dir.path());
        QVERIFY(record.entries().isEmpty());
        record.entries().insert(QStringLiteral("sf2"), { 2 });
        record.entries().insert(QStringLiteral("pacman"), { 1 });
        // Unchanged as far as it knows: nothing written.
        record.flush(&saves);
        saves.waitForDone();
        QVERIFY(!QFileInfo::exists(file));
        record.markChanged();
        record.flush(&saves);
        saves.waitForDone();
        QFile written(file);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.readAll(), QByteArray(R"({"pacman":{"n":1},"sf2":{"n":2}})"));
        written.close();
        // Written once for a change.
        QVERIFY(QFile::remove(file));
        record.flush(&saves);
        saves.waitForDone();
        QVERIFY(!QFileInfo::exists(file));
        record.markChanged();
        record.flush(&saves);
        saves.waitForDone();
        // Read back, in another folder none, and with none nothing written.
        RecordFile<Entry> again(QStringLiteral(".record.json"));
        again.load(dir.path());
        QCOMPARE(again.entries().size(), 2);
        QCOMPARE(again.entries().value(QStringLiteral("sf2")).n, 2);
        QTemporaryDir other;
        QVERIFY(other.isValid());
        again.load(other.path());
        QVERIFY(again.entries().isEmpty());
        again.load(QString());
        again.entries().insert(QStringLiteral("galaga"), { 3 });
        again.markChanged();
        again.flush(&saves);
        saves.waitForDone();
        QVERIFY(QDir(other.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }

    // The installers' download (arcadehttp::downloadToFile()) from a local
    // server: a file whole, its progress said; and why one isn't: the
    // server's answer, too big, or a file that can't be written.
    void downloadToFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray body;
        for (int i = 0; body.size() < 1024 * 1024; ++i)
            body += QByteArray::number(i) + '\n';
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&server, &body] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &body] {
                const QByteArray path = socket->readAll().split(' ').value(1);
                if (path == "/file")
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size())
                                  + "\r\nConnection: close\r\n\r\n" + body);
                else
                    socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
        });
        const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        QNetworkAccessManager nam;
        QObject context;
        const auto download = [&](const QString &path, const QString &file, qint64 maxBytes, qint64 *received) {
            QString result = QStringLiteral("not done");
            bool done = false;
            QNetworkReply *reply = arcadehttp::downloadToFile(
                &nam, QUrl(base + path), file, 10000, maxBytes, QStringLiteral("the test's file"), &context,
                [received](qint64 got, qint64) {
                    if (received)
                        *received = got;
                },
                [&](const QString &error) {
                    result = error;
                    done = true;
                });
            if (reply && !QTest::qWaitFor([&done] { return done; }, 10000))
                result = QStringLiteral("timed out");
            return result;
        };
        const QString file = dir.path() + QStringLiteral("/file.part");
        qint64 received = 0;
        QCOMPARE(download(QStringLiteral("/file"), file, 4 * 1024 * 1024, &received), QString());
        QCOMPARE(contents(file), body);
        QCOMPARE(received, body.size());
        QCOMPARE(download(QStringLiteral("/none"), file, 4 * 1024 * 1024, nullptr),
                 QStringLiteral("127.0.0.1 answered 404"));
        QCOMPARE(download(QStringLiteral("/file"), file, 1024, nullptr),
                 QStringLiteral("127.0.0.1 sent more than the test's file can be"));
        // A file that can't be written: said at once, nothing asked for.
        const QString nowhere = dir.path() + QStringLiteral("/no folder/file.part");
        QVERIFY(download(QStringLiteral("/file"), nowhere, 4 * 1024 * 1024, nullptr)
                    .startsWith(nowhere + QStringLiteral(" could not be written: ")));
        // A disk that fills up on the way (/dev/full): failed, and why is
        // the file's, not the download's stopping.
        QFile full(QStringLiteral("/dev/full"));
        if (!full.open(QIODevice::WriteOnly))
            QSKIP("no /dev/full here");
        QVERIFY(full.write(body) < 0 || !full.flush());
        const QString diskFull = full.errorString();
        full.close();
        QCOMPARE(download(QStringLiteral("/file"), QStringLiteral("/dev/full"), 4 * 1024 * 1024, nullptr), diskFull);
    }

    // Settings → Arcade → Check ROMs, started by the user: FinalBurn Neo's
    // list's CRCs against the archives' tables (a BIOS set's too), MAME's
    // -verifyroms with the folder and its bios folder; what it found picks
    // the engine that has a game's files where the other lacks them, says so
    // in its details, leaves it out of online play where FinalBurn Neo's are
    // missing, and of every list with Hide Incomplete Games; kept in
    // romcheck.json, and holding while the archives are as they were.
    void checkRoms() {
#ifndef OSDOS_MINIZIP
        QSKIP("minizip isn't in this build");
#endif
        QTemporaryDir data;
        QVERIFY(data.isValid());
        if (!libretro::corePath(data.path()).isEmpty() || !libretro::datPath(data.path()).isEmpty())
            QSKIP("FinalBurn Neo is installed here");
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        const QByteArray sf2Rom("SF2-A");
        const QByteArray galagaRom("GAL1");
        const QByteArray s1945Rom("1945");
        const QByteArray mslugRom("MSL1");
        const QByteArray biosRom("BIOS");
        const auto crc = [](const QByteArray &rom) { return QString::number(crc32Of(rom), 16).rightJustified(8, QLatin1Char('0')); };
        // sf2 and galaga whole, s1945 and mslug with a ROM wrong (mslug's
        // BIOS ROM in the bios folder's neogeo), pacman FinalBurn Neo hasn't.
        writeFile(games + QStringLiteral("/sf2.zip"), zipOf({ { "sf2_30.11e", sf2Rom } }));
        writeFile(games + QStringLiteral("/galaga.zip"), zipOf({ { "gg1_1b.3p", galagaRom } }));
        writeFile(games + QStringLiteral("/s1945.zip"), zipOf({ { "2s.u40", "XXXX" } }));
        writeFile(games + QStringLiteral("/mslug.zip"), zipOf({ { "201-p1.p1", "BAD!" } }));
        writeFile(games + QStringLiteral("/bios/neogeo.zip"), zipOf({ { "sp-s2.sp1", biosRom } }));
        writeFile(games + QStringLiteral("/pacman.zip"), zipOf({ { "pacman.6e", "PAC1" } }));
        const QString dat = QStringLiteral(
            "<?xml version=\"1.0\"?><datafile>"
            "<game name=\"sf2\"><description>Street Fighter II</description><rom name=\"sf2_30.11e\" size=\"5\" crc=\"%1\"/>"
            "<rom name=\"sf2_pal.bin\" size=\"5\" status=\"nodump\"/></game>"
            "<game name=\"galaga\"><description>Galaga</description><rom name=\"gg1_1b.3p\" size=\"4\" crc=\"%2\"/></game>"
            "<game name=\"s1945\"><description>Strikers 1945</description><rom name=\"2s.u40\" size=\"4\" crc=\"%3\"/></game>"
            "<game name=\"mslug\" romof=\"neogeo\"><description>Metal Slug</description><rom name=\"201-p1.p1\" size=\"4\" crc=\"%4\"/>"
            "<rom name=\"sp-s2.sp1\" merge=\"sp-s2.sp1\" size=\"4\" crc=\"%5\"/></game>"
            "<game name=\"neogeo\" isbios=\"yes\"><description>Neo-Geo</description><rom name=\"sp-s2.sp1\" size=\"4\" crc=\"%5\"/></game>"
            "</datafile>")
            .arg(crc(sf2Rom), crc(galagaRom), crc(s1945Rom), crc(mslugRom), crc(biosRom));
        writeFile(libretro::downloadedCore(data.path()));
        writeFile(libretro::downloadedDat(data.path()), dat.toUtf8());
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        for (const char *program : { "/bin/mame", "/bin/retroarch" })
            QVERIFY(QFile::setPermissions(data.path() + QLatin1String(program),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        });
        QFile::remove(m_log);

        const auto romsOf = [](ArcadeBackend &arcade, const char *set) {
            return arcade.gameDetails(QLatin1String(set)).value(QStringLiteral("roms")).toString();
        };
        const auto infoOf = [](ArcadeBackend &arcade) {
            const QVariantMap info = arcade.romCheckInfo();
            return QList<int>{ info.value(QStringLiteral("games")).toInt(), info.value(QStringLiteral("complete")).toInt(),
                               info.value(QStringLiteral("fbneoOnly")).toInt(), info.value(QStringLiteral("mameOnly")).toInt(),
                               info.value(QStringLiteral("incomplete")).toInt(), info.value(QStringLiteral("unchecked")).toInt() };
        };
        const auto setsIn = [](ArcadeBackend &arcade, const char *filter) {
            QStringList sets;
            const QVariantList rows = arcade.filterGames(QLatin1String(filter), 0, 1000);
            for (const QVariant &row : rows)
                sets << row.toMap().value(QStringLiteral("set")).toString();
            return sets;
        };
        const QString module = QStringLiteral("com.osdos.arcade");
        {
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            refreshed(arcade);
            QVERIFY(arcade.fbneoReady());
            // Nothing checked until asked: nothing said, the engines as
            // they are, nothing hidden.
            QCOMPARE(infoOf(arcade), QList<int>({ 5, 0, 0, 0, 0, 5 }));
            QVERIFY(arcade.romCheckInfo().value(QStringLiteral("checked")).toString().isEmpty());
            QVERIFY(logLines(QStringLiteral("verifyroms")).isEmpty());
            QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));

            QSignalSpy changed(&arcade, &ArcadeBackend::romCheckChanged);
            arcade.checkRoms();
            QVERIFY(arcade.romCheckProgress().value(QStringLiteral("active")).toBool());
            QTRY_VERIFY_WITH_TIMEOUT(!arcade.romCheckProgress().value(QStringLiteral("active")).toBool(), 10000);
            QCOMPARE(arcade.romCheckProgress().value(QStringLiteral("result")).toString(), QStringLiteral("Done"));
            QVERIFY(!changed.isEmpty());
            // MAME asked once, with the games folder and its bios folder,
            // about the games it knows.
            QCOMPARE(logLines(QStringLiteral("verifyroms")),
                     QStringList({ QStringLiteral("verifyroms %1;%1/bios").arg(games),
                                   QStringLiteral("verifyroms galaga mslug pacman s1945 sf2") }));

            QCOMPARE(infoOf(arcade), QList<int>({ 5, 1, 2, 1, 1, 0 }));
            QVERIFY(!arcade.romCheckInfo().value(QStringLiteral("checked")).toString().isEmpty());
            QCOMPARE(romsOf(arcade, "pacman"), QString());
            QCOMPARE(romsOf(arcade, "sf2"), QStringLiteral("1 File Missing for MAME"));
            QCOMPARE(romsOf(arcade, "galaga"), QStringLiteral("Files Missing for MAME"));
            QCOMPARE(romsOf(arcade, "s1945"), QStringLiteral("1 File Missing for FinalBurn Neo"));
            QCOMPARE(romsOf(arcade, "mslug"), QStringLiteral("2 Files Missing"));
            // Auto plays a game where its files are whole.
            QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("fbneo"));
            QCOMPARE(arcade.engineOf(QStringLiteral("galaga")), QStringLiteral("fbneo"));
            QCOMPARE(arcade.engineOf(QStringLiteral("s1945")), QStringLiteral("mame"));
            arcade.onSettingChanged(module, QStringLiteral("online_high_scores"), QStringLiteral("RetroAchievements"));
            QCOMPARE(arcade.engineOf(QStringLiteral("s1945")), QStringLiteral("mame"));
            QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("fbneo"));
            // Chosen for a game, an engine is played whatever was found.
            arcade.onSettingChanged(module, QStringLiteral("engines"),
                                    QVariantMap{ { QStringLiteral("sf2"), QStringLiteral("mame") } });
            QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));
            arcade.onSettingChanged(module, QStringLiteral("engines"), QVariantMap());
            arcade.onSettingChanged(module, QStringLiteral("online_high_scores"), QStringLiteral("Off"));
            // Online, nothing FinalBurn Neo's files are missing of.
            arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("On"));
            const QStringList online = setsIn(arcade, "netplay");
            QVERIFY(online.contains(QStringLiteral("galaga")));
            QVERIFY(!online.contains(QStringLiteral("s1945")));
            QVERIFY(!online.contains(QStringLiteral("mslug")));
            // Nor hosted, online or on the local network: a game's options
            // offer neither, and hosting it says why before RetroArch starts.
            arcade.onSettingChanged(module, QStringLiteral("lan_play"), QStringLiteral("On"));
            QVERIFY(arcade.canHost(QStringLiteral("galaga")));
            QVERIFY(arcade.canHostLan(QStringLiteral("galaga")));
            for (const QString &set : { QStringLiteral("s1945"), QStringLiteral("mslug") }) {
                QVERIFY2(!arcade.canHost(set), qPrintable(set));
                QVERIFY2(!arcade.canHostLan(set), qPrintable(set));
            }
            const QString incomplete =
                QStringLiteral("Files of it are missing for FinalBurn Neo, as Settings → Arcade → Check ROMs found");
            QFile::remove(m_log);
            QVERIFY(!arcade.hostGame(QStringLiteral("s1945")));
            QCOMPARE(arcade.lastError(), incomplete);
            QVERIFY(!arcade.hostLanGame(QStringLiteral("s1945")));
            QCOMPARE(arcade.lastError(), incomplete);
            QVERIFY(logLines(QStringLiteral("retroarch")).isEmpty());
            arcade.onSettingChanged(module, QStringLiteral("lan_play"), QStringLiteral("Off"));
            arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("Off"));

            // Hide Incomplete Games: mslug alone, whole in neither.
            QVERIFY(setsIn(arcade, "all").contains(QStringLiteral("mslug")));
            arcade.onSettingChanged(module, QStringLiteral("hide_incomplete"), QStringLiteral("ON"));
            QStringList shown = setsIn(arcade, "all");
            QVERIFY(!shown.contains(QStringLiteral("mslug")));
            QVERIFY(shown.contains(QStringLiteral("s1945")));
            QVERIFY(shown.contains(QStringLiteral("sf2")));
            QCOMPARE(arcade.romCheckInfo().value(QStringLiteral("incomplete")).toInt(), 1);
            QTRY_VERIFY(QFileInfo::exists(data.path() + QStringLiteral("/arcade/romcheck.json")));
        }
        // Kept for the next start, Hide Incomplete Games with it as it was
        // saved; a game's archive changed, or its BIOS set's, and what was
        // found of it no longer holds.
        QThreadPool::globalInstance()->waitForDone();
        {
            AppCore core(data.path(), data.path());
            QVERIFY(core.save_setting(module, QStringLiteral("hide_incomplete"), QStringLiteral("ON")));
            ArcadeBackend arcade(data.path(), data.path(), &core, nullptr);
            refreshed(arcade);
            QCOMPARE(infoOf(arcade), QList<int>({ 5, 1, 2, 1, 1, 0 }));
            QVERIFY(!setsIn(arcade, "all").contains(QStringLiteral("mslug")));
            QCOMPARE(romsOf(arcade, "sf2"), QStringLiteral("1 File Missing for MAME"));

            QTest::qWait(20);
            writeFile(games + QStringLiteral("/bios/neogeo.zip"), zipOf({ { "sp-s2.sp1", biosRom }, { "000-lo.lo", "LO" } }));
            writeFile(games + QStringLiteral("/galaga.zip"), zipOf({ { "gg1_1b.3p", galagaRom }, { "extra", "X" } }));
            // An archive replaced under its own name is not seen by a look at
            // the folder's names (refresh()): by the next read, as the app
            // starts, after a game, or from Read the Games Again.
            arcade.markChanged();
            refreshed(arcade);
            QCOMPARE(infoOf(arcade), QList<int>({ 5, 1, 1, 1, 0, 2 }));
            QVERIFY(setsIn(arcade, "all").contains(QStringLiteral("mslug")));
            QCOMPARE(romsOf(arcade, "mslug"), QString());
            QCOMPARE(arcade.engineOf(QStringLiteral("galaga")), QStringLiteral("mame"));
        }
        // Started while a game plays: MAME isn't asked beside it, but once
        // it has ended.
        {
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            refreshed(arcade);
            QFile::remove(m_log);
            qputenv("MAME_SECONDS", "1");
            const auto seconds = qScopeGuard([] { qunsetenv("MAME_SECONDS"); });
            QSignalSpy finished(&arcade, &ArcadeBackend::finished);
            QVERIFY(arcade.play(QStringLiteral("pacman")));
            arcade.checkRoms();
            QTRY_VERIFY(arcade.romCheckProgress().value(QStringLiteral("stage")).toString() == QLatin1String("MAME"));
            QVERIFY(arcade.romCheckProgress().value(QStringLiteral("held")).toBool());
            QVERIFY(arcade.romCheckProgress().value(QStringLiteral("active")).toBool());
            QVERIFY(logLines(QStringLiteral("verifyroms")).isEmpty());
            QVERIFY(finished.wait(10000));
            QTRY_VERIFY_WITH_TIMEOUT(!arcade.romCheckProgress().value(QStringLiteral("active")).toBool(), 10000);
            QCOMPARE(arcade.romCheckProgress().value(QStringLiteral("result")).toString(), QStringLiteral("Done"));
            const QStringList lines = logLines();
            const qsizetype ended = lines.indexOf(QStringLiteral("end pacman"));
            QVERIFY(ended >= 0);
            // indexOf() takes an exact match.
            QVERIFY(lines.indexOf(QRegularExpression(QStringLiteral("verifyroms .*"))) > ended);
        }
        // Started before the folder has been read, or while MAME is still
        // asked about its sets (slow to answer for a_slowlist, with no
        // games.json to know them from): it waits for both, saying so, and
        // then checks every game, not the part of them known as it was asked
        // for.
        writeFile(games + QStringLiteral("/a_slowlist.zip"));
        for (const bool beforeRead : { true, false }) {
            QThreadPool::globalInstance()->waitForDone();
            QFile::remove(data.path() + QStringLiteral("/arcade/games.json"));
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            const auto progress = [&arcade](const char *key) {
                return arcade.romCheckProgress().value(QLatin1String(key));
            };
            QFile::remove(m_log);
            arcade.refresh();
            if (!beforeRead)
                QTRY_COMPARE_WITH_TIMEOUT(logLines(QStringLiteral("listxml")).size(), 1, 5000);
            QSignalSpy changed(&arcade, &ArcadeBackend::romCheckChanged);
            arcade.checkRoms();
            QVERIFY(!changed.isEmpty());
            QVERIFY(progress("active").toBool());
            QVERIFY(progress("held").toBool());
            QVERIFY(progress("result").toString().isEmpty());
            QTRY_COMPARE_WITH_TIMEOUT(logLines(QStringLiteral("listxml")).size(), 1, 5000);
            QVERIFY(arcade.scanning());
            QVERIFY(progress("active").toBool());
            QVERIFY(progress("held").toBool());
            QVERIFY(logLines(QStringLiteral("verifyroms")).isEmpty());
            // Asked for again meanwhile, it is the one check.
            arcade.checkRoms();
            QTRY_VERIFY_WITH_TIMEOUT(!progress("active").toBool(), 10000);
            QCOMPARE(progress("result").toString(), QStringLiteral("Done"));
            QVERIFY(!arcade.scanning());
            const QStringList lines = logLines();
            QCOMPARE(lines.filter(QRegularExpression(QStringLiteral("^(listxml |verifyroms |answered$)"))),
                     QStringList({ QStringLiteral("listxml a_slowlist galaga mslug pacman s1945 sf2"),
                                   QStringLiteral("answered"),
                                   QStringLiteral("verifyroms %1;%1/bios").arg(games),
                                   QStringLiteral("verifyroms galaga mslug pacman s1945 sf2") }));
            QCOMPARE(arcade.romCheckInfo().value(QStringLiteral("unchecked")).toInt(), 0);
        }
        QVERIFY(QFile::remove(games + QStringLiteral("/a_slowlist.zip")));
        // A clone takes its BIOS set's ROMs through its parent, and what was
        // found of it is held against that set too: found without a BIOS
        // ROM, it is no longer so once the bios folder's set has changed.
        {
            QThreadPool::globalInstance()->waitForDone();
            const QByteArray cloneRom("MSLX");
            const QByteArray sfixRom("SFIX");
            writeFile(games + QStringLiteral("/mslugx.zip"), zipOf({ { "x-p1.p1", cloneRom } }));
            QString withClone = dat;
            withClone.replace(QStringLiteral("</datafile>"),
                              QStringLiteral("<game name=\"mslugx\" cloneof=\"mslug\" romof=\"mslug\">"
                                             "<description>Metal Slug (clone)</description>"
                                             "<rom name=\"x-p1.p1\" size=\"4\" crc=\"%1\"/>"
                                             "<rom name=\"sfix.sfix\" merge=\"sfix.sfix\" size=\"4\" crc=\"%2\"/></game>"
                                             "</datafile>").arg(crc(cloneRom), crc(sfixRom)));
            writeFile(libretro::downloadedDat(data.path()), withClone.toUtf8());
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            refreshed(arcade);
            arcade.checkRoms();
            QTRY_VERIFY_WITH_TIMEOUT(!arcade.romCheckProgress().value(QStringLiteral("active")).toBool(), 10000);
            QCOMPARE(romsOf(arcade, "mslugx"), QStringLiteral("1 File Missing"));
            QCOMPARE(arcade.romCheckInfo().value(QStringLiteral("unchecked")).toInt(), 0);
            QTest::qWait(20);
            writeFile(games + QStringLiteral("/bios/neogeo.zip"),
                      zipOf({ { "sp-s2.sp1", biosRom }, { "sfix.sfix", sfixRom } }));
            arcade.markChanged();
            refreshed(arcade);
            // mslug, and its clone.
            QCOMPARE(arcade.romCheckInfo().value(QStringLiteral("unchecked")).toInt(), 2);
            QCOMPARE(romsOf(arcade, "mslugx"), QString());
            arcade.checkRoms();
            QTRY_VERIFY_WITH_TIMEOUT(!arcade.romCheckProgress().value(QStringLiteral("active")).toBool(), 10000);
            QCOMPARE(arcade.romCheckInfo().value(QStringLiteral("unchecked")).toInt(), 0);
            QCOMPARE(romsOf(arcade, "mslugx"), QString());
        }
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

        // What a download stopped by the app closing left goes when the app
        // starts again, before anything is downloaded.
        for (const QString &path : { core + QStringLiteral(".zip.part"), libretro::downloadedDat(data.path()) + QStringLiteral(".part"),
                                     libretro::downloadedLicense(data.path()) + QStringLiteral(".part") })
            writeFile(path, "half a download");
        QVERIFY(!noParts());
        FbneoInstaller installer(data.path());
        QVERIFY(noParts());
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
        // stand-in, with the SHA-256 of what the stand-in serves.
        const auto source = [&base, &data, &files](const char *archive) {
            ProgramInstaller::Source s = ProgramInstaller::retroarch(data.path());
            s.available = true;
            s.archive = QUrl(base + QLatin1String(archive));
            s.sha256 = QString::fromLatin1(
                QCryptographicHash::hash(files.value(archive), QCryptographicHash::Sha256).toHex());
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
        // The archive whole but not the file expected, its SHA-256 another:
        // refused before any of it is unpacked, the one before kept.
        ProgramInstaller::Source wrong = source("/stable/1.20.0/windows/x86_64/RetroArch.7z");
        const QString archiveSha = wrong.sha256;
        wrong.sha256 = QString(64, QLatin1Char('0'));
        installer.start(wrong);
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(0).toBool(), false);
        QCOMPARE(finished.last().at(1).toString(),
                 QStringLiteral("The download is not the file expected: its SHA-256 is %1, not %2")
                     .arg(archiveSha, wrong.sha256));
        QCOMPARE(contents(program), QByteArray("the one before"));
        QCOMPARE(leftovers(), QStringList());
        // Its SHA-256 given in capitals, as some lists have it: the same.
        ProgramInstaller::Source capitals = source("/stable/1.20.0/windows/x86_64/RetroArch.7z");
        capitals.sha256 = capitals.sha256.toUpper();
        installer.start(capitals);
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(1).toString(), QString());
        QVERIFY(contents(program).startsWith("MZ a stand-in for RetroArch for Windows"));
        // None known, as for libretro's builds: installed unchecked, and
        // said so in the log.
        writeFile(program, "the one before");
        ProgramInstaller::Source unchecked = source("/stable/1.20.0/windows/x86_64/RetroArch.7z");
        unchecked.sha256.clear();
        QTest::ignoreMessage(QtWarningMsg,
                             "[Arcade] RetroArch's download is not checked: no SHA-256 is known for RetroArch.7z");
        installer.start(unchecked);
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(1).toString(), QString());
        QCOMPARE(finished.last().at(0).toBool(), true);
        QVERIFY(contents(program).startsWith("MZ a stand-in for RetroArch for Windows"));
        QCOMPARE(leftovers(), QStringList());
        writeFile(program, "the one before");
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
        // libretro publishes no SHA-256 for it (scripts/engines.env).
        QCOMPARE(pinned.sha256, QString());
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
        // stand-in, with the SHA-256 of what the stand-in serves.
        const auto source = [&base, &data, &files](const char *archive) {
            ProgramInstaller::Source s = ProgramInstaller::mame(data.path());
            s.available = true;
            s.archive = QUrl(base + QLatin1String(archive));
            s.sha256 = QString::fromLatin1(
                QCryptographicHash::hash(files.value(archive), QCryptographicHash::Sha256).toHex());
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
        // The release's file with the SHA-256 engines.env has for 0.276's
        // (scripts/engines.env), which the stand-in isn't: refused before
        // anything of it is unpacked or run, the one before kept.
        ProgramInstaller::Source wrong = source("/mamedev/mame/releases/download/mame0276/mame0276b_64bit.exe");
        const QString archiveSha = wrong.sha256;
        wrong.sha256 = ProgramInstaller::mame(data.path()).sha256;
        installer.start(wrong);
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(0).toBool(), false);
        QCOMPARE(finished.last().at(1).toString(),
                 QStringLiteral("The download is not the file expected: its SHA-256 is %1, not %2")
                     .arg(archiveSha, wrong.sha256));
        QCOMPARE(contents(program), QByteArray("the one before"));
        QCOMPARE(leftovers(), QStringList());
        // None known: installed unchecked, and said so in the log.
        ProgramInstaller::Source unchecked = source("/mamedev/mame/releases/download/mame0276/mame0276b_64bit.exe");
        unchecked.sha256.clear();
        QTest::ignoreMessage(QtWarningMsg,
                             "[Arcade] MAME's download is not checked: no SHA-256 is known for mame0276b_64bit.exe");
        installer.start(unchecked);
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.last().at(1).toString(), QString());
        QVERIFY(contents(program).startsWith("MZ a stand-in for MAME for Windows"));
        QCOMPARE(leftovers(), QStringList());
        writeFile(program, "the one before");
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
        // The Windows build's name by version: b_64bit.exe up to 0.280,
        // b_x64.exe from 0.281, as engines.yml has it too.
        QCOMPARE(ProgramInstaller::mameWindowsFile(QStringLiteral("0.276")), QStringLiteral("mame0276b_64bit.exe"));
        QCOMPARE(ProgramInstaller::mameWindowsFile(QStringLiteral("0.280")), QStringLiteral("mame0280b_64bit.exe"));
        QCOMPARE(ProgramInstaller::mameWindowsFile(QStringLiteral("0.281")), QStringLiteral("mame0281b_x64.exe"));
        QCOMPARE(ProgramInstaller::mameWindowsFile(QStringLiteral("0.289")), QStringLiteral("mame0289b_x64.exe"));
        // The SHA-256 of that file in the release's SHA256SUMS.
        QCOMPARE(pinned.sha256, QStringLiteral("12499709a4cfea351b997762c2e6399f7edc70855ec68efbc419024b51f9ffcc"));
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

    // The numbered pictures read with the folder: a set's in turn from 1,
    // past a few numbers the user deleted and those that are sets of the
    // folder, but not another machine's snap a pack names <set>_<n> far
    // past them (Data East's TMNT pinball, tmnt_101 to tmnt_104, beside
    // Konami's tmnt), with pictures of the set's own before it or not.
    void numberedPicturesInTurn() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString snap = dir.path() + QStringLiteral("/snap");
        for (const char *name : { "tmnt.png", "tmnt_101.png", "tmnt_103.png", "tmnt_104.png", "sf2_2.png",
                                  "sf2_4.png", "sf2_8.jpg", "sf2_8.png", "sf2_13.png", "mk_1.png", "mk_101.png" })
            writeFile(snap + QLatin1Char('/') + QLatin1String(name), png(name));
        const QHash<QString, QString> sets{ { QStringLiteral("tmnt"), QStringLiteral("tmnt.zip") },
                                            { QStringLiteral("sf2"), QStringLiteral("sf2.zip") },
                                            { QStringLiteral("sf2_6"), QStringLiteral("sf2_6.zip") },
                                            { QStringLiteral("mk"), QStringLiteral("mk.zip") } };
        const auto in = [&snap](std::initializer_list<const char *> names) {
            QStringList paths;
            for (const char *name : names)
                paths << snap + QLatin1Char('/') + QLatin1String(name);
            return paths;
        };
        ArcadePictures::Found found = ArcadePictures::read(snap, dir.path() + QStringLiteral("/titles"),
                                                           dir.path() + QStringLiteral("/shots"), sets);
        QVERIFY2(!found.numbered.contains(QStringLiteral("tmnt")), qPrintable(found.numbered.value("tmnt").join(' ')));
        QCOMPARE(found.snaps.value(QStringLiteral("tmnt")), snap + QStringLiteral("/tmnt.png"));
        // 1 and 3 deleted, 5 and 7 missing and 6 a set's; 9 to 12 missing.
        QCOMPARE(found.numbered.value(QStringLiteral("sf2")), in({ "sf2_2.png", "sf2_4.png", "sf2_8.png", "sf2_8.jpg" }));
        QCOMPARE(found.numbered.value(QStringLiteral("mk")), in({ "mk_1.png" }));
    }

    // RetroAchievements' pictures of a game as it is shown, with a key kept:
    // saved in the games folder's snap folder as <set>_<n> in the first
    // numbers free, never over the user's own (whatever its suffix's case)
    // nor as a set's name, a clone's under its parent's name when it is its
    // parent's game and shown before the clone's own snap, listed among its
    // pictures (by their numbers, the snap after them) and kept through the
    // folder read again; each
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
                 urls({ snap + QStringLiteral("/sf2_1.png"), snap + QStringLiteral("/sf2_4.PNG"),
                        snap + QStringLiteral("/sf2.png") }));
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
        // Shown by their numbers, the user's among them, the snap after.
        const QStringList shown{ snap + QStringLiteral("/sf2_1.png"), saved.at(0), snap + QStringLiteral("/sf2_4.PNG"),
                                 saved.at(1), saved.at(2), snap + QStringLiteral("/sf2.png") };
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("pictures")), urls(shown));
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("picture")).toString(),
                 QUrl::fromLocalFile(snap + QStringLiteral("/sf2_1.png")).toString());
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

        // Pac-Man is its parent's game: saved as Puck Man's, shown before its
        // own snap. Galaga isn't RetroAchievements': nothing read, asked
        // again or not.
        arcade.wantPictures(QStringLiteral("pacman"));
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 4, 10000);
        QCOMPARE(changed.last().at(0).toString(), QStringLiteral("puckman"));
        QCOMPARE(arcade.gameDetails(QStringLiteral("pacman")).value(QStringLiteral("pictures")),
                 urls({ snap + QStringLiteral("/puckman_1.png"), snap + QStringLiteral("/pacman.png") }));
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
    // never over a file there and each whole as it comes into the folder
    // (RetroAchievements' too), and notes in .arcadedb.json what ArcadeDB had
    // of each set, so that none is asked about again; a game shown is asked
    // about by itself, after RetroAchievements; Game Pictures' Source can keep
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
        const FolderWatch watch(snap);
        arcade.downloadPictures();
        QTRY_VERIFY_WITH_TIMEOUT(arcade.sweepingPictures(), 5000);
        QVERIFY(QFile::exists(data.path() + QStringLiteral("/arcade/pictures-wanted")));
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.sweepingPictures(), 15000);
        QCOMPARE(files(snap), QStringList({ QStringLiteral("galaga.png"), QStringLiteral("pacman.png"), QStringLiteral("sf2_1.png") }));
        QCOMPARE(files(titles), QStringList({ QStringLiteral("galaga.png") }));
        QCOMPARE(contents(snap + QStringLiteral("/galaga.png")), png("galaga in play"));
        QCOMPARE(contents(titles + QStringLiteral("/galaga.png")), png("galaga title"));
        // Each came into the folder whole: written beside its name and given
        // it once on the card, never written to under it, where the power
        // going would leave a picture cut short that is then the game's.
        if (watch.fd >= 0) {
            QStringList cameIn;
            QStringList writtenTo;
            watch.read(cameIn, writtenTo);
            for (const char *picture : { "galaga.png", "sf2_1.png" }) {
                QVERIFY2(cameIn.contains(QLatin1String(picture)), qPrintable(cameIn.join(QLatin1Char(' '))));
                QVERIFY2(!writtenTo.contains(QLatin1String(picture)), picture);
            }
        } else {
            qWarning("the snap folder can't be watched here (inotify): how its pictures are written isn't checked");
        }
        const QStringList queries = asked("/service_scraper.php?");
        QCOMPARE(queries.size(), 1);
        QVERIFY2(queries.first().contains(QLatin1String("galaga")) && queries.first().contains(QLatin1String("kinst"))
                     && !queries.first().contains(QLatin1String("sf2")) && !queries.first().contains(QLatin1String("pacman")),
                 qPrintable(queries.first()));
        // The service's sixth release dropped use_parent: not sent.
        QVERIFY(!queries.first().contains(QLatin1String("use_parent")));
        QCOMPARE(asked("/API/API_GetGame.php").size(), 1);
        QCOMPARE(arcade.gameDetails(QStringLiteral("galaga")).value(QStringLiteral("pictures")).toStringList(),
                 urls({ titles + QStringLiteral("/galaga.png"), snap + QStringLiteral("/galaga.png") }));
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
                 urls({ titles + QStringLiteral("/mslug.png"), snap + QStringLiteral("/mslug.png") }));
        // Shown again: nothing more asked.
        standIn.asked.clear();
        arcade.wantPictures(QStringLiteral("mslug"));
        arcade.wantPictures(QStringLiteral("kinst"));
        QTest::qWait(200);
        QVERIFY(standIn.asked.isEmpty());

        // Game Pictures' Source on RetroAchievements alone: ArcadeDB isn't asked about a
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
            QCOMPARE(labels(options.last().at(1)).mid(0, 7),
                     QStringList({ QStringLiteral("Lists"), QStringLiteral("Favorites"), QStringLiteral("Recently Played"),
                                   QStringLiteral("RetroAchievements"), QStringLiteral("Playable Online"),
                                   QStringLiteral("Vertical Games"), QStringLiteral("Not Recognized") }));
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
        // big: an answer far bigger than a list of rooms. none: a lobby with
        // no room. hold: the request kept unanswered (held) until answer()
        // is called for it.
        bool big = false;
        bool none = false;
        bool hold = false;
        QPointer<QTcpSocket> held;
        int asked = 0;
        const auto answer = [&lobby, &big, &none](QTcpSocket *socket) {
            const QByteArray body = big ? QByteArray(5 * 1024 * 1024, ' ') : none ? QByteArray("[]") : lobby;
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
        QCOMPARE(order, QStringList({ "20", "15", "18", "14", "21", "19", "17", "13", "11", "10", "16", "22" }));
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
        QCOMPARE(why(13), QStringLiteral("The lobby gives an address inside a network, not on the internet"));
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
        QCOMPARE(filters.value(4).toMap().value(QStringLiteral("count")).toInt(), 12);
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
        const QString stick = reservedLine(1, QStringLiteral("0001:0001 OSD/OS JAMMA P1"));
        const auto onClassic = [&binds, &stick](const QString &config) {
            return config.contains(stick)
                   && binds(config, { "input_player1_b_btn = \"0\"", "input_player1_y_btn = \"2\"",
                                      "input_player1_l_btn = \"10\"", "input_player1_up_axis = \"-1\"",
                                      "input_player2_b_btn = \"0\"" },
                            { "input_player1_up_btn", "input_player2_up_btn" });
        };
        // Hosting from the cabinet, the panel's layout: every port a 6-Button
        // Panel, the sticks bound to it.
        const auto onPanel = [&binds, &stick](const QString &config) {
            return config.contains(stick)
                   && binds(config, { "input_player1_y_btn = \"0\"", "input_player1_l_btn = \"2\"",
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
        // The user's own core options: a faster CPU and a harder game here,
        // a picture turned for a cabinet's screen on its side.
        const QString home = data.path() + QStringLiteral("/arcade/retroarch");
        const QString ownOptions = home + QStringLiteral("/config/FinalBurn Neo/FinalBurn Neo.opt");
        const QByteArray own("fbneo-cpu-speed-adjust = \"150%\"\n"
                             "fbneo-vertical-mode = \"TATE\"\n"
                             "fbneo-dipswitch-sf2-Difficulty = \"8 (Hardest)\"\n");
        writeFile(ownOptions, own);
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.joinRoom(11); }).value(1).toString(), QStringLiteral("ok"));
        QCOMPARE(logLines(QStringLiteral("retroarch ")).value(0),
                 QStringLiteral("retroarch -L %1 --config %2/retroarch.cfg --verbose --log-file=%2/last.log "
                                "--connect 203.0.113.5|55435 %3/sf2.zip").arg(libretro::downloadedCore(data.path()), home, games));
        QString config = QString::fromUtf8(contents(configCopy));
        // Joined with the options every machine plays with alike, from
        // OSD/OS's own file: the CPU at its speed, the game's DIP switches at
        // their defaults, the picture as this machine has it; the user's
        // own file as it was.
        const QString netplayOptions = home + QStringLiteral("/config/netplay-options.cfg");
        QVERIFY2(config.contains(QStringLiteral("core_options_path = \"%1\"").arg(netplayOptions)), qPrintable(config));
        QVERIFY(config.contains(QStringLiteral("global_core_options = \"true\"")));
        const QString alike = QString::fromUtf8(contents(netplayOptions));
        QCOMPARE(alike, libretro::netplayOptionsText(QString::fromUtf8(own)));
        QVERIFY(alike.contains(QStringLiteral("fbneo-cpu-speed-adjust = \"100%\"")));
        QVERIFY(alike.contains(QStringLiteral("fbneo-vertical-mode = \"TATE\"")));
        QVERIFY(!alike.contains(QStringLiteral("dipswitch")));
        QCOMPARE(contents(ownOptions), own);
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
        // Relay Server with nothing saved: the nearest to the system's time
        // zone, as the row shows it; one saved kept, whatever the zone.
        arcade.setTimeZone(QStringLiteral("Asia/Tokyo"));
        QCOMPARE(arcade.relayServer(), QStringLiteral("Chuncheon"));
        arcade.setTimeZone(QStringLiteral("America/Argentina/Buenos_Aires"));
        QCOMPARE(arcade.relayServer(), QStringLiteral("São Paulo"));
        arcade.setTimeZone(QString());
        QCOMPARE(arcade.relayServer(), QStringLiteral("New York"));
        arcade.setTimeZone(QStringLiteral("Europe/Istanbul"));
        QCOMPARE(arcade.relayServer(), QStringLiteral("Madrid"));
        arcade.onSettingChanged(module, QStringLiteral("relay_server"), QStringLiteral("Singapore"));
        QCOMPARE(arcade.relayServer(), QStringLiteral("Singapore"));
        arcade.onSettingChanged(module, QStringLiteral("relay_server"), QVariant());
        QCOMPARE(arcade.relayServer(), QStringLiteral("Madrid"));
        arcade.setTimeZone(QStringLiteral("Australia/Sydney"));
        arcade.onSettingChanged(module, QStringLiteral("relay_server"), QStringLiteral("Madrid"));
        // Host LAN Game's advice under Smooth: a Pi 4, or a machine on
        // Wi-Fi, told the fastest machine on a cable should host; a Pi 5 or
        // a PC on a cable, and Responsive, nothing.
        const QString fastest = QStringLiteral("The fastest machine on a cable should host");
        arcade.setOverWifi(false);
        arcade.setBoard(board::Family::Pi4);
        QCOMPARE(arcade.hostAdvice(), fastest);
        arcade.setBoard(board::Family::Pi5);
        QCOMPARE(arcade.hostAdvice(), QString());
        arcade.setOverWifi(true);
        QCOMPARE(arcade.hostAdvice(), fastest);
        arcade.setBoard(board::Family::Other);
        QCOMPARE(arcade.hostAdvice(), fastest);
        arcade.onSettingChanged(module, QStringLiteral("lan_sync"), QStringLiteral("Responsive"));
        QCOMPARE(arcade.hostAdvice(), QString());
        arcade.onSettingChanged(module, QStringLiteral("lan_sync"), QVariant());
        arcade.setOverWifi(false);
        QCOMPARE(arcade.hostAdvice(), QString());
        // This machine's own network read, whatever it is.
        arcade.setOverWifi(std::nullopt);
        QVERIFY(arcade.hostAdvice().isEmpty() || arcade.hostAdvice() == fastest);
        QCOMPARE(libretro::hostAdvice(true, board::Family::Pi4, false), fastest);
        QCOMPARE(libretro::hostAdvice(false, board::Family::Pi4, true), QString());
        // A Pi 5's console (the test runs without a desktop): two frames
        // queued for the screen, the 48 ms sound buffer, and the ping shown
        // where asked.
        arcade.setBoard(board::Family::Pi5);
        arcade.onSettingChanged(module, QStringLiteral("show_ping"), QStringLiteral("ON"));
        QFile::remove(m_log);
        writeFile(remap, leftOver);
        QCOMPARE(ended([&arcade] { return arcade.hostGame(QStringLiteral("sf2")); }).value(1).toString(),
                 QStringLiteral("ok"));
        QVERIFY(logLines(QStringLiteral("retroarch ")).value(0).contains(QStringLiteral(" --host ")));
        config = QString::fromUtf8(contents(configCopy));
        for (const char *line : { "netplay_nickname = \"Mehmet\"", "netplay_public_announce = \"true\"",
                                  "netplay_use_mitm_server = \"true\"", "netplay_mitm_server = \"madrid\"",
                                  "netplay_nat_traversal = \"false\"",
                                  "netplay_input_latency_frames_min = \"2\"", "netplay_ping_show = \"true\"",
                                  "video_max_swapchain_images = \"2\"", "audio_latency = \"48\"" })
            QVERIFY2(config.contains(QLatin1String(line)), line);
        QVERIFY(!config.contains(QStringLiteral("audio_resampler_quality")));
        arcade.setBoard(board::Family::Other);
        arcade.onSettingChanged(module, QStringLiteral("show_ping"), QVariant());
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

        // A lobby read for the first time that has no room to join: the
        // Online Games folder, asked for before it was read, is told so and
        // lists that no one hosts a game, rather than wait on for rooms.
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("Off"));
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("On"));
        none = true;
        ready.clear();
        QVERIFY(!arcade.requestEntries(QStringLiteral("online")).isValid());
        QVERIFY(roomsRead.wait(5000));
        none = false;
        QVERIFY(arcade.rooms().isEmpty());
        QCOMPARE(ready.count(QVariantList{ QStringLiteral("online") }), 1);
        const QVariantList noRooms = arcade.requestEntries(QStringLiteral("online")).toList();
        QCOMPARE(names(noRooms), QStringList({ QStringLiteral("No Games Online"), QStringLiteral("RetroArch Lobby") }));
        // Read again and found the same: nothing listed anew.
        none = true;
        arcade.refreshRooms();
        QVERIFY(roomsRead.wait(5000));
        none = false;
        QCOMPARE(ready.count(QVariantList{ QStringLiteral("online") }), 1);

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
        for (const char *set : { "sf2.zip", "mslug.zip", "galaga.zip", "xmen6p.zip", "dino.zip" })
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
        // FinalBurn Neo's list, with a six-player game (X-Men's, whose
        // players MAME knows) beside one whose players aren't known
        // (Cadillacs and Dinosaurs, which MAME doesn't know).
        writeFile(libretro::downloadedDat(data.path()),
                  fixture("fbneo-arcade.dat")
                      .replace("</datafile>", "\t<game name=\"xmen6p\" sourcefile=\"konami/d_xmen.cpp\"><description>"
                                              "X-Men (6 Players ver EAB)</description></game>\n</datafile>"));
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

        writeFiveButtonSets(data.path(), "sf2\n");
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
        // gave, under the device's name, this machine's player 1 (a JAMMA
        // stick, a pad as player 2) on the one port the host's OSD/OS gives
        // it: asked for at the claim port as the room is chosen, the game
        // started on the answer, on the layout the host said before it (the
        // panel's: the stick in the cabinet's rows, the pad as itself), and
        // given back as it ends.
        using Players = QList<QPair<QString, QString>>;
        const Players twoPlayers = { { QStringLiteral("OSDOS_PLAYER1_GUID"), QStringLiteral("0600aaaa") },
                                     { QStringLiteral("OSDOS_PLAYER1_NAME"), QStringLiteral("OSD/OS JAMMA P1") },
                                     { QStringLiteral("OSDOS_PLAYER2_GUID"), QStringLiteral("0600bbbb") },
                                     { QStringLiteral("OSDOS_PLAYER2_NAME"), QStringLiteral("Xbox 360 Controller") } };
        controllers::setPlayerEnvironment(twoPlayers);
        const auto noPlayers = qScopeGuard([] { controllers::setPlayerEnvironment({}); });
        QUdpSocket claims;
        QVERIFY(claims.bind(QHostAddress::LocalHost, 0));
        QList<QByteArray> claimed;
        // How the stand-in host answers: the layout then the ports, as OSD/OS
        // does; the ports first and the layout a moment later (the two are
        // datagrams of their own); the ports alone, as an older OSD/OS; or
        // first that it plays at the game's own rate, as OSD/OS 0.0.28 to
        // 0.0.32 did. It plays on the panel's layout and gives port 3, and 4
        // after it as an older OSD/OS gave a machine of two players, unless
        // said otherwise: one taken, port 3.
        // Or the ports once more a moment later: asked twice, it answers twice.
        enum { LayoutThenPorts, PortsThenLayout, PortsAlone, PacedFirst, PortsAgainLater } answering = LayoutThenPorts;
        libretro::Launch::Layout saidLayout = libretro::Launch::Layout::Panel;
        libretro::PortRange portsGiven = { 3, 2 };
        connect(&claims, &QUdpSocket::readyRead, &claims, [&claims, &claimed, &answering, &saidLayout, &portsGiven] {
            while (claims.hasPendingDatagrams()) {
                const QNetworkDatagram query = claims.receiveDatagram();
                claimed << query.data();
                if (!libretro::readClaimQuery(query.data()))
                    continue;
                const QHostAddress to = query.senderAddress();
                const quint16 port = query.senderPort();
                const auto layout = [&claims, said = saidLayout, to, port] {
                    claims.writeDatagram(libretro::claimLayout(said), to, port);
                };
                if (answering == PacedFirst)
                    claims.writeDatagram(QByteArray("OSDOS PACE game"), to, port);
                if (answering == LayoutThenPorts || answering == PacedFirst || answering == PortsAgainLater)
                    layout();
                claims.writeDatagram(libretro::claimAnswer(portsGiven), to, port);
                if (answering == PortsThenLayout)
                    QTimer::singleShot(50, &claims, layout);
                if (answering == PortsAgainLater) {
                    QTimer::singleShot(300, &claims, [&claims, given = portsGiven, to, port] {
                        claims.writeDatagram(libretro::claimAnswer(given), to, port);
                    });
                }
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
        QCOMPARE(requested(config), QStringLiteral("3"));
        QVERIFY2(config.contains(QStringLiteral("input_player1_y_btn = \"0\"")), qPrintable(config));
        QVERIFY(config.contains(QStringLiteral("input_player1_l_btn = \"2\"")));
        QVERIFY(config.contains(QStringLiteral("input_player2_b_btn = \"0\"")));
        QVERIFY(!QFile::exists(libretro::coreRemapPath(home + QStringLiteral("/remaps"))));
        QVERIFY(logLines().contains(QStringLiteral("coreinfo deterministic")));
        // Paced by the screen, as online: the sound as RetroArch has it.
        for (const char *key : { "audio_sync", "audio_max_timing_skew", "audio_rate_control" })
            QVERIFY2(!config.contains(QLatin1String(key)), key);
        QTRY_VERIFY(!claimed.isEmpty() && claimed.last() == libretro::claimRelease());
        QCOMPARE(claimed.first(), libretro::claimQuery(1));
        QVERIFY(!arcade.running());
        // Given back twice, a moment apart, in case one is lost.
        QTRY_COMPARE(claimed.count(libretro::claimRelease()), qsizetype(2));
        QTest::qWait(LanRooms::kReleaseAgainMs + 100);
        QCOMPARE(claimed.count(libretro::claimRelease()), qsizetype(2));
        // The host's answer once more, late (asked twice, it answers twice),
        // with the game already started on the first: the ports this machine
        // plays on stay its until its game ends, and go back then.
        answering = PortsAgainLater;
        qputenv("RA_SECONDS", "1.5");
        claimed.clear();
        QFile::remove(m_log);
        {
            QSignalSpy lateEnded(&arcade, &ArcadeBackend::finished);
            QVERIFY(arcade.joinLanRoom(1));
            QTRY_VERIFY_WITH_TIMEOUT(arcade.running(), 3000);
            QTest::qWait(700);
            QVERIFY(arcade.running());
            QVERIFY2(!claimed.contains(libretro::claimRelease()), "the ports being played on were given back");
            QVERIFY(lateEnded.wait(10000) || !lateEnded.isEmpty());
        }
        qunsetenv("RA_SECONDS");
        QTRY_VERIFY(claimed.contains(libretro::claimRelease()));
        answering = LayoutThenPorts;
        // The ports before the layout: the game waits a moment for it, and
        // starts on the panel's layout as before.
        answering = PortsThenLayout;
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.joinLanRoom(1); }).value(1).toString(), QStringLiteral("ok"));
        config = QString::fromUtf8(contents(configCopy));
        QCOMPARE(requested(config), QStringLiteral("3"));
        QVERIFY2(config.contains(QStringLiteral("input_player1_y_btn = \"0\"")), qPrintable(config));
        QTRY_VERIFY(!claimed.isEmpty() && claimed.last() == libretro::claimRelease());
        // The ports alone, from an older OSD/OS: joined with them a moment
        // later, on the Classic layout.
        answering = PortsAlone;
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.joinLanRoom(1); }).value(1).toString(), QStringLiteral("ok"));
        config = QString::fromUtf8(contents(configCopy));
        QCOMPARE(requested(config), QStringLiteral("3"));
        QVERIFY2(config.contains(QStringLiteral("input_player1_b_btn = \"0\"")), qPrintable(config));
        QVERIFY(config.contains(QStringLiteral("input_player1_y_btn = \"2\"")));
        QTRY_VERIFY(!claimed.isEmpty() && claimed.last() == libretro::claimRelease());
        // The word on the game's rate from OSD/OS 0.0.28 to 0.0.32, passed
        // over: joined on the panel's layout, paced by the screen.
        answering = PacedFirst;
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.joinLanRoom(1); }).value(1).toString(), QStringLiteral("ok"));
        config = QString::fromUtf8(contents(configCopy));
        QCOMPARE(requested(config), QStringLiteral("3"));
        QVERIFY2(config.contains(QStringLiteral("input_player1_y_btn = \"0\"")), qPrintable(config));
        QVERIFY(!config.contains(QStringLiteral("audio_max_timing_skew")));
        QTRY_VERIFY(!claimed.isEmpty() && claimed.last() == libretro::claimRelease());
        // Four players here, given four ports by an older OSD/OS: one taken,
        // the first, player 1's, on either layout; the others watch.
        const Players fourPlayers = twoPlayers
                                    + Players{ { QStringLiteral("OSDOS_PLAYER3_GUID"), QStringLiteral("0600cccc") },
                                               { QStringLiteral("OSDOS_PLAYER3_NAME"), QStringLiteral("Xbox 360 Controller") },
                                               { QStringLiteral("OSDOS_PLAYER4_GUID"), QStringLiteral("0600dddd") },
                                               { QStringLiteral("OSDOS_PLAYER4_NAME"), QStringLiteral("Xbox 360 Controller") } };
        controllers::setPlayerEnvironment(fourPlayers);
        answering = LayoutThenPorts;
        portsGiven = { 2, 4 };
        claimed.clear();
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.joinLanRoom(1); }).value(1).toString(), QStringLiteral("ok"));
        config = QString::fromUtf8(contents(configCopy));
        QCOMPARE(requested(config), QStringLiteral("2"));
        QTRY_VERIFY(!claimed.isEmpty() && claimed.last() == libretro::claimRelease());
        QCOMPARE(claimed.first(), libretro::claimQuery(1));
        saidLayout = libretro::Launch::Layout::Classic;
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.joinLanRoom(1); }).value(1).toString(), QStringLiteral("ok"));
        config = QString::fromUtf8(contents(configCopy));
        QCOMPARE(requested(config), QStringLiteral("2"));
        QTRY_VERIFY(!claimed.isEmpty() && claimed.last() == libretro::claimRelease());
        controllers::setPlayerEnvironment(twoPlayers);
        saidLayout = libretro::Launch::Layout::Panel;
        portsGiven = { 3, 2 };
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
        // Connected, then the host gone a second later: RetroArch plays on
        // alone, unthrottled had it been catching up; stopped as its log says
        // so, the ports given back, the game played, and the end says the
        // host left. An error while connected ended nothing.
        const qint64 playedBefore = arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("playTime")).toLongLong();
        qputenv("RA_HOST_LEAVES", "1");
        QFile::remove(m_log);
        static QStringList logged;
        logged.clear();
        const QtMessageHandler previous = qInstallMessageHandler(
            [](QtMsgType, const QMessageLogContext &, const QString &message) { logged << message; });
        const QVariantList left = ended([&arcade] { return arcade.joinLanRoom(1); });
        qInstallMessageHandler(previous);
        qunsetenv("RA_HOST_LEAVES");
        // And the app's log says how it went, in one line, from RetroArch's.
        const QStringList summary = logged.filter(QStringLiteral("arcade: network game: "));
        QCOMPARE(summary.size(), 1);
        QVERIFY2(summary.first().startsWith(QStringLiteral("arcade: network game: sf2, joined on the LAN, 0 min ")),
                 qPrintable(summary.first()));
        QVERIFY2(summary.first().endsWith(QStringLiteral(
                     "; peers: the host 12 ms; left 1; stalls 0, catch-ups 0, savestates 0, CRC mismatches 1, desyncs 0, "
                     "pauses 0, netplay errors 1; achievements: nothing logged")),
                 qPrintable(summary.first()));
        QCOMPARE(left.value(1).toString(), QStringLiteral("stopped"));
        QCOMPARE(left.value(2).toString(), QStringLiteral("The host left the game"));
        QTRY_VERIFY(!claimed.isEmpty() && claimed.last() == libretro::claimRelease());
        QVERIFY(!arcade.running());
        QVERIFY(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("playTime")).toLongLong() > playedBefore);
        // A host that doesn't say, RetroArch alone or an older OSD/OS: asked
        // twice, then joined on RetroArch's own terms (the first free port),
        // on the Classic layout, the stick's buttons by number.
        claims.close();
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.joinLanRoom(1); }).value(1).toString(), QStringLiteral("ok"));
        config = QString::fromUtf8(contents(configCopy));
        QCOMPARE(requested(config), QString());
        QVERIFY2(config.contains(QStringLiteral("input_player1_b_btn = \"0\"")), qPrintable(config));
        QVERIFY(config.contains(QStringLiteral("input_player1_y_btn = \"2\"")));
        QVERIFY(config.contains(QStringLiteral("input_player1_l_btn = \"10\"")));
        // Hosted: on the network alone, straight, under the online name.
        // Street Fighter II takes two players: this machine's first on port
        // 1, whatever its second stick, and the machine joining the other.
        // It asks for its port while the game runs and is told the layout
        // (the panel's, a JAMMA stick among this machine's players: every
        // port on it in the remap file) and given port 2 alone, whatever its
        // players, its own anew as it asks again; none once the game is
        // over.
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
        // LAN Sync unset: Smooth, the machines joining follow this one.
        QVERIFY2(config.contains(QStringLiteral("netplay_require_slaves = \"true\"")), qPrintable(config));
        QVERIFY(config.contains(QStringLiteral("netplay_input_latency_frames_min = \"0\"")));
        QVERIFY(logLines().contains(QStringLiteral("coreinfo deterministic")));
        QCOMPARE(asking(libretro::claimQuery(1), 300), QByteArray("no answer"));
        // X-Men's six players, hosted with the same two: on Classic, the
        // stick's buttons by number and no remap file. On the local network,
        // on FinalBurn Neo's own core, one port a machine, so that the players
        // take the ports in the order their machines come in: this machine's
        // first on port 1 alone, and a machine joining with four given port 2
        // alone.
        QCOMPARE(arcade.onlineLayout(QStringLiteral("xmen6p"), true), QStringLiteral("classic"));
        QCOMPARE(arcade.onlineLayout(QStringLiteral("sf2"), true), QStringLiteral("panel"));
        QCOMPARE(arcade.onlineLayout(QStringLiteral("dino"), true), QStringLiteral("panel"));
        QCOMPARE(arcade.onlineLayout(QStringLiteral("sf2"), false), QStringLiteral("classic"));
        QFile::remove(m_log);
        hostEnded.clear();
        QVERIFY(arcade.hostLanGame(QStringLiteral("xmen6p")));
        QVERIFY(!QFile::exists(hostRemap));
        QCOMPARE(asking(libretro::claimQuery(4), 2000),
                 libretro::claimLayout(libretro::Launch::Layout::Classic) + " / " + libretro::claimAnswer({ 2, 1 }));
        QVERIFY(hostEnded.wait(10000) || !hostEnded.isEmpty());
        QCOMPARE(hostEnded.first().value(1).toString(), QStringLiteral("ok"));
        config = QString::fromUtf8(contents(configCopy));
        QCOMPARE(requested(config), QStringLiteral("1"));
        QVERIFY2(config.contains(QStringLiteral("input_player1_b_btn = \"0\"")), qPrintable(config));
        // Two pads on Game Buttons' Auto, no stick among them: the host's
        // layout is what it is with both on Engine's, Classic, which it
        // tells the machines joining, and its players play RetroArch's own
        // binds, not Auto's rows (the remap file none); a layout chosen by
        // hand makes it the panel's, as before.
        const Players twoPads = { { QStringLiteral("OSDOS_PLAYER1_GUID"), QStringLiteral("0600bbbb") },
                                  { QStringLiteral("OSDOS_PLAYER1_NAME"), QStringLiteral("Xbox 360 Controller") },
                                  { QStringLiteral("OSDOS_PLAYER2_GUID"), QStringLiteral("0600cccc") },
                                  { QStringLiteral("OSDOS_PLAYER2_NAME"), QStringLiteral("Xbox 360 Controller") } };
        controllers::setPlayerEnvironment(twoPads);
        controllers::setPlayerArcade({});
        QCOMPARE(arcade.onlineLayout(QStringLiteral("sf2"), true), QStringLiteral("classic"));
        controllers::setPlayerArcade({ autoBinds(), autoBinds() });
        const auto noArcade = qScopeGuard([] { controllers::setPlayerArcade({}); });
        QCOMPARE(arcade.onlineLayout(QStringLiteral("sf2"), true), QStringLiteral("classic"));
        QCOMPARE(arcade.onlineLayout(QStringLiteral("galaga"), true), QStringLiteral("classic"));
        QFile::remove(m_log);
        hostEnded.clear();
        QVERIFY(arcade.hostLanGame(QStringLiteral("sf2")));
        QCOMPARE(asking(libretro::claimQuery(1), 2000),
                 libretro::claimLayout(libretro::Launch::Layout::Classic) + " / " + libretro::claimAnswer({ 2, 1 }));
        QVERIFY(hostEnded.wait(10000) || !hostEnded.isEmpty());
        QCOMPARE(hostEnded.first().value(1).toString(), QStringLiteral("ok"));
        QVERIFY(!QFile::exists(hostRemap));
        config = QString::fromUtf8(contents(configCopy));
        QVERIFY2(!config.contains(QStringLiteral("input_player1_r_axis = \"+5\"")), qPrintable(config));
        QVERIFY(!config.contains(QStringLiteral("input_player1_y_btn = \"0\"")));
        controllers::ArcadeBinds chosen = autoBinds();
        chosen.automatic = false;
        controllers::setPlayerArcade({ chosen });
        QCOMPARE(arcade.onlineLayout(QStringLiteral("sf2"), true), QStringLiteral("panel"));
        controllers::setPlayerArcade({});
        controllers::setPlayerEnvironment(twoPlayers);

        // Cadillacs and Dinosaurs, whose players aren't known: the panel's
        // layout kept, this machine on port 1 alone, and a machine joining
        // with four given port 2 alone. Hosted with LAN Sync on Responsive:
        // every machine plays ahead, with a frame of input latency.
        arcade.onSettingChanged(module, QStringLiteral("lan_sync"), QStringLiteral("Responsive"));
        QFile::remove(m_log);
        hostEnded.clear();
        QVERIFY(arcade.hostLanGame(QStringLiteral("dino")));
        QCOMPARE(asking(libretro::claimQuery(4), 2000),
                 libretro::claimLayout(libretro::Launch::Layout::Panel) + " / " + libretro::claimAnswer({ 2, 1 }));
        QVERIFY(hostEnded.wait(10000) || !hostEnded.isEmpty());
        QCOMPARE(hostEnded.first().value(1).toString(), QStringLiteral("ok"));
        config = QString::fromUtf8(contents(configCopy));
        QCOMPARE(requested(config), QStringLiteral("1"));
        QVERIFY2(config.contains(QStringLiteral("netplay_require_slaves = \"false\"")), qPrintable(config));
        QVERIFY(config.contains(QStringLiteral("netplay_input_latency_frames_min = \"1\"")));
        QVERIFY(config.contains(QStringLiteral("netplay_input_latency_frames_range = \"3\"")));
        arcade.onSettingChanged(module, QStringLiteral("lan_sync"), QStringLiteral("Smooth"));
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
        // Within a second or so: where the emulators are is kept that long
        // between looks (ArcadeBackend::programs()).
        QTRY_COMPARE_WITH_TIMEOUT(lanSays(), QStringLiteral("FinalBurn Neo Not Installed"), 3000);
        QVERIFY(QFile::rename(core + QStringLiteral(".away"), core));
        QTRY_COMPARE_WITH_TIMEOUT(lanSays(), QStringLiteral("Network Not Reachable"), 3000);

        // Off again: nothing of it.
        arcade.onSettingChanged(module, QStringLiteral("lan_play"), QStringLiteral("Off"));
        QVERIFY(arcade.lanRooms().isEmpty());
        QCOMPARE(lanFilter(), -1);
        QVERIFY(!names(arcade.requestEntries(QStringLiteral("top"))).contains(QStringLiteral("LAN Games")));
        QVERIFY(!arcade.statusIcons().contains(QStringLiteral("network")));
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("mame"));
    }

    // OSD/OS's LAN core: every game hosted on the local network with it, and
    // a room hosted so joined with it, matched against it, every controller
    // here on a port of its own; a room hosted with FinalBurn Neo's joined
    // with that, and a game hosted online with it. Without OSD/OS's core, a
    // game hosted with FinalBurn Neo's, but a CV1000 game (FinalBurn Neo's
    // cave/d_cv1k.cpp) neither hosted nor joined, saying why.
    void lanPlayCv1000() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        for (const char *set : { "sf2.zip", "akatana.zip" })
            writeFile(games + QLatin1Char('/') + QLatin1String(set));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        // The copy of the app that says a core's version: the rooms' for
        // OSD/OS's own, another for FinalBurn Neo's.
        const QString coreInfo = data.path() + QStringLiteral("/bin/coreinfo");
        writeFile(coreInfo, "#!/bin/sh\ncase \"$2\" in\n"
                            "*osdos_lan_libretro*) printf 'name=OSD-OS LAN Core\\nversion=v1.0.0.03 6e7c2a1\\n' ;;\n"
                            "*) printf 'name=FinalBurn Neo\\nversion=v1.0.0.03 0000000\\n' ;;\nesac\n");
        for (const char *program : { "/bin/mame", "/bin/retroarch", "/bin/coreinfo" })
            QVERIFY(QFile::setPermissions(data.path() + QLatin1String(program),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString core = libretro::downloadedCore(data.path());
        writeFile(core);
        QCOMPARE(libretro::lanCorePath(data.path()), QString());
        const QString lanCore = QFileInfo(core).path() + QStringLiteral("/osdos_lan_libretro.so");
        writeFile(lanCore);
        QCOMPARE(libretro::lanCorePath(data.path()), lanCore);
        writeFile(libretro::downloadedDat(data.path()),
                  fixture("fbneo-arcade.dat")
                      .replace("</datafile>", "\t<game name=\"akatana\" sourcefile=\"cave/d_cv1k.cpp\"><description>"
                                              "Akai Katana (2010/ 8/13 MASTER VER.)</description></game>\n</datafile>"));
        qputenv("RA_CONFIG_COPY", (data.path() + QStringLiteral("/ra.cfg")).toUtf8());
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qunsetenv("RA_CONFIG_COPY");
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        });
        // The hosts: Akai Katana on OSD/OS's core, Street Fighter II on
        // FinalBurn Neo's.
        QUdpSocket hosts;
        QVERIFY(hosts.bind(QHostAddress::LocalHost, 0));
        connect(&hosts, &QUdpSocket::readyRead, &hosts, [&hosts] {
            while (hosts.hasPendingDatagrams()) {
                const QNetworkDatagram query = hosts.receiveDatagram();
                if (query.data() != libretro::lanQuery())
                    continue;
                for (const QByteArray &answer : { lanAnswer("ann", "akatana", 55435, 0, "1.22.2", 0, "OSD-OS LAN Core"),
                                                  lanAnswer("bob", "sf2", 55436) })
                    hosts.writeDatagram(answer, query.senderAddress(), query.senderPort());
            }
        });

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setLanDiscovery(QStringLiteral("127.0.0.1"), hosts.localPort());
        arcade.setLanListenTime(150);
        arcade.setCoreInfoCommand({ coreInfo, QStringLiteral("--core-info") });
        arcade.setLobbyUrl(QUrl(QStringLiteral("http://127.0.0.1:9/list")));
        // No host's OSD/OS to give a machine joining its ports: asked, then
        // joined on RetroArch's own terms.
        quint16 claimPort = 0;
        {
            QUdpSocket probe;
            QVERIFY(probe.bind(QHostAddress::LocalHost, 0));
            claimPort = probe.localPort();
        }
        arcade.setLanClaimPort(claimPort);
        refreshed(arcade);
        const QString module = QStringLiteral("com.osdos.arcade");
        arcade.onSettingChanged(module, QStringLiteral("lan_play"), QStringLiteral("On"));
        const QString home = data.path() + QStringLiteral("/arcade/retroarch");
        const auto ended = [&arcade](const std::function<bool()> &start) {
            QSignalSpy finished(&arcade, &ArcadeBackend::finished);
            if (!start())
                return QVariantList{ -1, QStringLiteral("not started"), arcade.lastError() };
            if (!finished.wait(10000) && finished.isEmpty())
                return QVariantList{ -1, QStringLiteral("no end"), QString() };
            return finished.first();
        };
        const auto startedWith = [this] { return logLines(QStringLiteral("retroarch ")).value(0).section(QLatin1Char(' '), 2, 2); };

        // Akai Katana hosted: on OSD/OS's core, with its own info file, which
        // RetroArch takes its states for deterministic by.
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.hostLanGame(QStringLiteral("akatana")); }).value(1).toString(),
                 QStringLiteral("ok"));
        QCOMPARE(startedWith(), lanCore);
        QVERIFY(logLines(QStringLiteral("retroarch ")).value(0).contains(QStringLiteral(" --host ")));
        QVERIFY(logLines().contains(QStringLiteral("coreinfo deterministic")));
        QVERIFY(contents(libretro::coreInfoPath(home + QStringLiteral("/info"), true))
                    .contains("corename = \"OSD-OS LAN Core\""));
        // Street Fighter II hosted on OSD/OS's core too, with a JAMMA stick
        // and a pad here: a port each, 1 and 2, whatever the game's two
        // players, as the core seats a player only as they first press COIN
        // or START; a machine joining with two given the next two, of the
        // six, three at most on the panel's layout.
        controllers::setPlayerEnvironment({ { QStringLiteral("OSDOS_PLAYER1_GUID"), QStringLiteral("0600aaaa") },
                                            { QStringLiteral("OSDOS_PLAYER1_NAME"), QStringLiteral("OSD/OS JAMMA P1") },
                                            { QStringLiteral("OSDOS_PLAYER2_GUID"), QStringLiteral("0600bbbb") },
                                            { QStringLiteral("OSDOS_PLAYER2_NAME"), QStringLiteral("Xbox 360 Controller") } });
        const auto noPlayers = qScopeGuard([] { controllers::setPlayerEnvironment({}); });
        qputenv("RA_SECONDS", "3");
        QFile::remove(m_log);
        QSignalSpy sf2Ended(&arcade, &ArcadeBackend::finished);
        QVERIFY(arcade.hostLanGame(QStringLiteral("sf2")));
        QUdpSocket joiner;
        QVERIFY(joiner.bind(QHostAddress::LocalHost, 0));
        joiner.writeDatagram(libretro::claimQuery(4), QHostAddress::LocalHost, claimPort);
        QByteArrayList answers;
        QTRY_VERIFY_WITH_TIMEOUT(([&joiner, &answers] {
            while (joiner.hasPendingDatagrams())
                answers << joiner.receiveDatagram().data();
            return answers.size() == 2;
        }()), 2000);
        QCOMPARE(answers, QByteArrayList({ libretro::claimLayout(libretro::Launch::Layout::Panel),
                                           libretro::claimAnswer({ 3, 3 }) }));
        QVERIFY(sf2Ended.wait(10000) || !sf2Ended.isEmpty());
        qunsetenv("RA_SECONDS");
        QCOMPARE(startedWith(), lanCore);
        const QString config = QString::fromUtf8(contents(data.path() + QStringLiteral("/ra.cfg")));
        for (const char *line : { "netplay_request_device_p1 = \"true\"", "netplay_request_device_p2 = \"true\"",
                                  "netplay_request_device_p3 = \"false\"" })
            QVERIFY2(config.contains(QLatin1String(line)), line);
        // Akai Katana online: FinalBurn Neo's.
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("On"));
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.hostGame(QStringLiteral("akatana")); }).value(1).toString(),
                 QStringLiteral("ok"));
        QCOMPARE(startedWith(), core);
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("Off"));

        // The rooms: Akai Katana's matched against OSD/OS's core, Street
        // Fighter II's against FinalBurn Neo's.
        QSignalSpy read(&arcade, &ArcadeBackend::lanRoomsChanged);
        arcade.refreshLanRooms();
        QVERIFY(read.wait(5000));
        const auto room = [&arcade](const QString &set) {
            for (const QVariant &v : arcade.lanRooms()) {
                if (v.toMap().value(QStringLiteral("set")).toString() == set)
                    return v.toMap();
            }
            return QVariantMap();
        };
        QTRY_COMPARE_WITH_TIMEOUT(room(QStringLiteral("akatana")).value(QStringLiteral("info")).toMap()
                                      .value(QStringLiteral("coreMatch")).toString(), QStringLiteral("same"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(room(QStringLiteral("sf2")).value(QStringLiteral("info")).toMap()
                                      .value(QStringLiteral("coreMatch")).toString(), QStringLiteral("other"), 5000);
        QVariantMap info = room(QStringLiteral("akatana")).value(QStringLiteral("info")).toMap();
        QCOMPARE(info.value(QStringLiteral("core")).toString(), QStringLiteral("OSD-OS LAN Core"));
        QVERIFY(info.value(QStringLiteral("coreHere")).toBool());
        QVERIFY(info.value(QStringLiteral("joinable")).toBool());
        QVERIFY(room(QStringLiteral("akatana")).value(QStringLiteral("detail")).toString()
                    .endsWith(QStringLiteral("The same FinalBurn Neo and RetroArch as yours")));
        QVERIFY(room(QStringLiteral("sf2")).value(QStringLiteral("detail")).toString()
                    .contains(QStringLiteral("Its FinalBurn Neo is v1.0.0.03 6e7c2a1, yours v1.0.0.03 0000000")));
        // Joined: Akai Katana's room on OSD/OS's core, Street Fighter II's on
        // FinalBurn Neo's.
        const int akatana = room(QStringLiteral("akatana")).value(QStringLiteral("id")).toInt();
        const int sf2 = room(QStringLiteral("sf2")).value(QStringLiteral("id")).toInt();
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade, akatana] { return arcade.joinLanRoom(akatana); }).value(1).toString(), QStringLiteral("ok"));
        QCOMPARE(startedWith(), lanCore);
        QVERIFY(logLines(QStringLiteral("retroarch ")).value(0).contains(QStringLiteral(" --connect 127.0.0.1|55435 ")));
        QVERIFY(logLines().contains(QStringLiteral("coreinfo deterministic")));
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade, sf2] { return arcade.joinLanRoom(sf2); }).value(1).toString(), QStringLiteral("ok"));
        QCOMPARE(startedWith(), core);

        // Without OSD/OS's core: Akai Katana neither hosted on the network
        // nor its room joined, saying why; Street Fighter II hosted on
        // FinalBurn Neo's.
        QVERIFY(QFile::remove(lanCore));
        const QString noCore = QStringLiteral("This OSD/OS has no core of its own for the local network: "
                                              "OSD/OS's packages bring it");
        QVERIFY(!arcade.hostLanGame(QStringLiteral("akatana")));
        QCOMPARE(arcade.lastError(), noCore);
        info = room(QStringLiteral("akatana")).value(QStringLiteral("info")).toMap();
        QVERIFY(!info.value(QStringLiteral("coreHere")).toBool());
        QVERIFY(!info.value(QStringLiteral("joinable")).toBool());
        QCOMPARE(info.value(QStringLiteral("why")).toString(), noCore);
        QCOMPARE(ended([&arcade, akatana] { return arcade.joinLanRoom(akatana); }).value(2).toString(), noCore);
        QFile::remove(m_log);
        QCOMPARE(ended([&arcade] { return arcade.hostLanGame(QStringLiteral("sf2")); }).value(1).toString(),
                 QStringLiteral("ok"));
        QCOMPARE(startedWith(), core);
    }

    // Over the network, only the games whose states RetroArch has the memory
    // for on this machine: on a Raspberry Pi of 8 GB, Akai Katana (CV1000)
    // neither hosted online nor its room joined, saying why, but on the local
    // network on OSD/OS's own core; on a machine of 2 GB not there either; on
    // one of 32 GB everywhere. Street Fighter II on all of them.
    void netplayMemory() {
        const qint64 gib = qint64(1) << 30;
        QCOMPARE(libretro::netplayStateSize(QStringLiteral("cave/cv1k.cpp"), false), qint64(151780188));
        QCOMPARE(libretro::netplayStateSize(QStringLiteral("cave/cv1k.cpp"), true), qint64(17562464));
        QCOMPARE(libretro::netplayStateSize(QStringLiteral("capcom/cps1.cpp"), false), qint64(0));
        QCOMPARE(libretro::netplayMemory(0, true), qint64(0));
        QCOMPARE(libretro::netplayMemory(151780188, false), 64 * qint64(151780188) + gib);
        QCOMPARE(libretro::netplayMemory(151780188, true), 126 * qint64(151780188) + gib);
        // Twice its states where that is less than 1 GiB more.
        QCOMPARE(libretro::netplayMemory(1 << 20, true), 2 * 126 * qint64(1 << 20));

        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        for (const char *set : { "sf2.zip", "akatana.zip" })
            writeFile(games + QLatin1Char('/') + QLatin1String(set));
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        const QString coreInfo = data.path() + QStringLiteral("/bin/coreinfo");
        writeFile(coreInfo, "#!/bin/sh\ncase \"$2\" in\n"
                            "*osdos_lan_libretro*) printf 'name=OSD-OS LAN Core\\nversion=v1.0.0.03 6e7c2a1\\n' ;;\n"
                            "*) printf 'name=FinalBurn Neo\\nversion=v1.0.0.03 6e7c2a1\\n' ;;\nesac\n");
        for (const char *program : { "/bin/mame", "/bin/retroarch", "/bin/coreinfo" })
            QVERIFY(QFile::setPermissions(data.path() + QLatin1String(program),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString core = libretro::downloadedCore(data.path());
        writeFile(core);
        writeFile(QFileInfo(core).path() + QStringLiteral("/osdos_lan_libretro.so"));
        writeFile(libretro::downloadedDat(data.path()),
                  fixture("fbneo-arcade.dat")
                      .replace("</datafile>", "\t<game name=\"akatana\" sourcefile=\"cave/d_cv1k.cpp\"><description>"
                                              "Akai Katana (2010/ 8/13 MASTER VER.)</description></game>\n</datafile>"));
        qputenv("RA_CONFIG_COPY", (data.path() + QStringLiteral("/ra.cfg")).toUtf8());
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qunsetenv("RA_CONFIG_COPY");
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        });
        // The lobby, with a room of each, and the local network's hosts:
        // Akai Katana on OSD/OS's core, Street Fighter II on FinalBurn Neo's.
        const QByteArray lobby =
            "[{\"fields\": {\"id\": 11, \"username\": \"ann\", \"game_name\": \"SF2\", \"core_name\": \"FinalBurn Neo\","
            " \"ip\": \"203.0.113.5\", \"port\": 55435, \"host_method\": 0, \"connectable\": true,"
            " \"has_password\": false, \"retroarch_version\": \"1.20.0\"}},"
            " {\"fields\": {\"id\": 12, \"username\": \"bob\", \"game_name\": \"akatana\", \"core_name\": \"FinalBurn Neo\","
            " \"ip\": \"203.0.113.6\", \"port\": 55435, \"host_method\": 0, \"connectable\": true,"
            " \"has_password\": false, \"retroarch_version\": \"1.20.0\"}}]";
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&server, &lobby] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &lobby] {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(lobby.size()) + "\r\nConnection: close\r\n\r\n" + lobby);
                socket->disconnectFromHost();
            });
        });
        QUdpSocket hosts;
        QVERIFY(hosts.bind(QHostAddress::LocalHost, 0));
        connect(&hosts, &QUdpSocket::readyRead, &hosts, [&hosts] {
            while (hosts.hasPendingDatagrams()) {
                const QNetworkDatagram query = hosts.receiveDatagram();
                if (query.data() != libretro::lanQuery())
                    continue;
                for (const QByteArray &answer : { lanAnswer("ann", "akatana", 55435, 0, "1.22.2", 0, "OSD-OS LAN Core"),
                                                  lanAnswer("bob", "sf2", 55436) })
                    hosts.writeDatagram(answer, query.senderAddress(), query.senderPort());
            }
        });

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setLobbyUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/list").arg(server.serverPort())));
        arcade.setLanDiscovery(QStringLiteral("127.0.0.1"), hosts.localPort());
        arcade.setLanListenTime(150);
        arcade.setCoreInfoCommand({ coreInfo, QStringLiteral("--core-info") });
        // A Raspberry Pi 5 of 8 GB, as its /proc/meminfo has it: 7.9 GB.
        const qint64 pi = 8245100LL * 1024;
        arcade.setMemory(pi);
        refreshed(arcade);
        const QString module = QStringLiteral("com.osdos.arcade");
        arcade.onSettingChanged(module, QStringLiteral("online_play"), QStringLiteral("On"));
        arcade.onSettingChanged(module, QStringLiteral("lan_play"), QStringLiteral("On"));
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.filters().isEmpty(), 10000);
        const auto playable = [&arcade] {
            QStringList sets;
            for (const QVariant &v : arcade.filterGames(QStringLiteral("netplay"), 0, 10))
                sets << v.toMap().value(QStringLiteral("set")).toString();
            return sets;
        };
        const auto setsOf = [](const QVariantList &rooms) {
            QStringList sets;
            for (const QVariant &v : rooms)
                sets << v.toMap().value(QStringLiteral("set")).toString();
            sets.sort();
            return sets;
        };
        QSignalSpy ready(&arcade, &ArcadeBackend::entriesReady);
        QVERIFY(!arcade.requestEntries(QStringLiteral("online")).isValid());
        QTRY_VERIFY_WITH_TIMEOUT(ready.contains(QVariantList{ QStringLiteral("online") }), 5000);
        QSignalSpy lanRead(&arcade, &ArcadeBackend::lanRoomsChanged);
        arcade.refreshLanRooms();
        QVERIFY(lanRead.wait(5000));
        const auto lobbyWhy = [&arcade](int id) {
            for (const QVariant &v : arcade.lobby().value(QStringLiteral("rooms")).toList()) {
                if (v.toMap().value(QStringLiteral("id")).toInt() == id)
                    return v.toMap().value(QStringLiteral("why")).toString();
            }
            return QStringLiteral("not listed");
        };

        // Online: Street Fighter II hosted and its room listed; Akai Katana
        // neither, the lobby saying why of its room, and either said why of
        // when started.
        QVERIFY(arcade.canHost(QStringLiteral("sf2")));
        QVERIFY(!arcade.canHost(QStringLiteral("akatana")));
        QCOMPARE(playable(), QStringList{ QStringLiteral("sf2") });
        QCOMPARE(setsOf(arcade.rooms()), QStringList{ QStringLiteral("sf2") });
        const QString joining = QStringLiteral("Joining it over the network needs 10.0 GB of memory, and this machine has 7.9 GB");
        QCOMPARE(lobbyWhy(11), QString());
        QCOMPARE(lobbyWhy(12), joining);
        QVERIFY(!arcade.hostGame(QStringLiteral("akatana")));
        QCOMPARE(arcade.lastError(),
                 QStringLiteral("Hosting it over the network needs 18.8 GB of memory, and this machine has 7.9 GB"));
        QVERIFY(!arcade.joinRoom(12));
        QCOMPARE(arcade.lastError(), joining);
        // On the local network, on OSD/OS's own core: both.
        QVERIFY(arcade.canHostLan(QStringLiteral("akatana")));
        QVERIFY(arcade.canHostLan(QStringLiteral("sf2")));
        QCOMPARE(setsOf(arcade.lanRooms()), QStringList({ QStringLiteral("akatana"), QStringLiteral("sf2") }));

        // A machine of 2 GB: Akai Katana not on the local network either.
        const qint64 small = 1948000LL * 1024;
        arcade.setMemory(small);
        QVERIFY(!arcade.canHostLan(QStringLiteral("akatana")));
        QVERIFY(arcade.canHostLan(QStringLiteral("sf2")));
        QCOMPARE(setsOf(arcade.lanRooms()), QStringList{ QStringLiteral("sf2") });
        QVERIFY(!arcade.hostLanGame(QStringLiteral("akatana")));
        QCOMPARE(arcade.lastError(),
                 QStringLiteral("Hosting it over the network needs 3.1 GB of memory, and this machine has 1.9 GB"));

        // One of 32 GB: Akai Katana everywhere.
        arcade.setMemory(32 * gib);
        QVERIFY(arcade.canHost(QStringLiteral("akatana")));
        QVERIFY(arcade.canHostLan(QStringLiteral("akatana")));
        QCOMPARE(playable(), QStringList({ QStringLiteral("akatana"), QStringLiteral("sf2") }));
        QCOMPARE(setsOf(arcade.rooms()), QStringList({ QStringLiteral("akatana"), QStringLiteral("sf2") }));
        QCOMPARE(lobbyWhy(12), QString());
        QCOMPARE(setsOf(arcade.lanRooms()), QStringList({ QStringLiteral("akatana"), QStringLiteral("sf2") }));
    }

    // The RetroArch a package brings (the Linux arm64 one's, built with
    // TLS): played with before the system's, beside FinalBurn Neo's cores;
    // not without the system's certificates, which it would end without at
    // its first request, nor once it was left.
    void packagedRetroArch() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString program = dir.path() + QStringLiteral("/retroarch");
        QCOMPARE(libretro::packagedProgramToPlay(program, true, false), QString());
        writeFile(program, "#!/bin/sh\n");
        // A file that can't be run is no program.
        QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner));
        QCOMPARE(libretro::packagedProgramToPlay(program, true, false), QString());
        QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QCOMPARE(libretro::packagedProgramToPlay(program, true, false), program);
        QCOMPARE(libretro::packagedProgramToPlay(program, false, false), QString());
        QCOMPARE(libretro::packagedProgramToPlay(program, true, true), QString());
        QCOMPARE(libretro::packagedProgramToPlay(QString(), true, false), QString());
        QCOMPARE(QFileInfo(libretro::packagedProgram()).path(), QFileInfo(libretro::packagedCore()).path());
        QCOMPARE(QFileInfo(libretro::packagedProgram()).fileName(), QStringLiteral("retroarch"));
    }

    // A package's RetroArch that doesn't start (a library it needs isn't on
    // the system): the system's is played with instead, and its version is
    // the one read.
    void packagedRetroArchThatDoesNotStart() {
        if (!QFileInfo::exists(QLatin1String(libretro::kSystemCertificates)))
            QSKIP("The system has no certificates file: the package's RetroArch is never played with");
        QTemporaryDir data;
        QTemporaryDir prefix;
        QTemporaryDir system;
        QVERIFY(data.isValid() && prefix.isValid() && system.isValid());
        const QByteArray path = qgetenv("PATH");
        const auto restore = qScopeGuard([path] {
            qputenv("PATH", path);
            libretro::setPackagePrefix(QString());
        });
        libretro::setPackagePrefix(prefix.path());
        const QString packaged = libretro::packagedProgram();
        QCOMPARE(packaged, prefix.path() + QStringLiteral("/lib/osdos/retroarch"));
        writeFile(packaged, "#!/bin/sh\necho 'error while loading shared libraries: libSDL2-2.0.so.0' >&2\nexit 127\n");
        writeFile(system.path() + QStringLiteral("/retroarch"), fixture("retroarch"));
        for (const QString &program : { packaged, system.path() + QStringLiteral("/retroarch") })
            QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("PATH", system.path().toLocal8Bit() + ':' + path);
        // The package's first, then the user's own over it.
        QCOMPARE(libretro::programPath(data.path()), packaged);

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setLobbyUrl(QUrl(QStringLiteral("http://127.0.0.1:9/list")));
        arcade.setVersionReadTimeout(3000);
        const auto ours = [&arcade](const char *key) { return arcade.lobby().value(QLatin1String(key)).toString(); };
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("online_play"), QStringLiteral("On"));
        arcade.refreshRooms();
        QTRY_COMPARE_WITH_TIMEOUT(ours("yourRetroArch"), QStringLiteral("1.22.2"), 8000);
        QCOMPARE(libretro::programPath(data.path()), system.path() + QStringLiteral("/retroarch"));
        // Asked for again, it is the package's once more: left only until
        // the app starts again.
        libretro::setPackagePrefix(prefix.path());
        QCOMPARE(libretro::programPath(data.path()), packaged);
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/retroarch"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QCOMPARE(libretro::programPath(data.path()), data.path() + QStringLiteral("/bin/retroarch"));
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

    // The versions read are kept in the arcade folder (engine-versions.json)
    // by the stamp of the file each was read from: the next run asks none
    // of them, its RetroArch's and cores' as soon as online play looks;
    // a file changed since, of a stamp it doesn't keep, is asked again, the
    // rest kept; and a kept file that can't be read counts for none.
    void ownVersionsAreKeptForTheNextRun() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString askedRetroArch = data.path() + QStringLiteral("/asked-retroarch");
        const QString askedCore = data.path() + QStringLiteral("/asked-core");
        writeFile(data.path() + QStringLiteral("/bin/retroarch"),
                  QStringLiteral("#!/bin/sh\necho >> '%1'\necho 'Version: 1.22.2 (Git 0a1b2c3)'\n")
                      .arg(askedRetroArch).toUtf8());
        writeFile(libretro::downloadedCore(data.path()), "core");
        const QString coreInfo = data.path() + QStringLiteral("/bin/coreinfo");
        writeFile(coreInfo, QStringLiteral("#!/bin/sh\necho \"$2\" >> '%1'\n"
                                           "printf 'name=FinalBurn Neo\\nversion=v1.0.0.03 6e7c2a1\\n'\n")
                                .arg(askedCore).toUtf8());
        for (const char *program : { "/bin/retroarch", "/bin/coreinfo" })
            QVERIFY(QFile::setPermissions(data.path() + QLatin1String(program),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const auto lines = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll().count('\n') : 0;
        };
        const QString kept = data.path() + QStringLiteral("/arcade/engine-versions.json");
        // A run: online play on, the lobby (not reached) looked at.
        const auto run = [&](const std::function<void(ArcadeBackend &)> &then) {
            ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
            arcade.setLobbyUrl(QUrl(QStringLiteral("http://127.0.0.1:9/list")));
            arcade.setCoreInfoCommand({ coreInfo, QStringLiteral("--core-info") });
            arcade.setVersionReadTimeout(3000);
            arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("online_play"), QStringLiteral("On"));
            arcade.refreshRooms();
            then(arcade);
            // Its file written (off the app's thread) before the next run.
            QThreadPool::globalInstance()->waitForDone();
        };
        const auto ours = [](ArcadeBackend &arcade, const char *key) {
            return arcade.lobby().value(QLatin1String(key)).toString();
        };

        run([&](ArcadeBackend &arcade) {
            QTRY_COMPARE_WITH_TIMEOUT(ours(arcade, "yourRetroArchState"), QStringLiteral("read"), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(ours(arcade, "yourCoreState"), QStringLiteral("read"), 5000);
        });
        QCOMPARE(lines(askedRetroArch), 1);
        QCOMPARE(lines(askedCore), 1);
        QVERIFY(QFileInfo::exists(kept));

        // The next run: read at once, nothing asked.
        run([&](ArcadeBackend &arcade) {
            QCOMPARE(ours(arcade, "yourRetroArchState"), QStringLiteral("read"));
            QCOMPARE(ours(arcade, "yourRetroArch"), QStringLiteral("1.22.2"));
            QCOMPARE(ours(arcade, "yourCoreState"), QStringLiteral("read"));
            QCOMPARE(ours(arcade, "yourCore"), QStringLiteral("v1.0.0.03 6e7c2a1"));
            QTest::qWait(300);
        });
        QCOMPARE(lines(askedRetroArch), 1);
        QCOMPARE(lines(askedCore), 1);

        // The core replaced (another size): asked again; RetroArch kept.
        writeFile(libretro::downloadedCore(data.path()), "a newer core");
        run([&](ArcadeBackend &arcade) {
            QCOMPARE(ours(arcade, "yourRetroArch"), QStringLiteral("1.22.2"));
            QCOMPARE(ours(arcade, "yourCoreState"), QStringLiteral("reading"));
            QTRY_COMPARE_WITH_TIMEOUT(ours(arcade, "yourCoreState"), QStringLiteral("read"), 5000);
        });
        QCOMPARE(lines(askedRetroArch), 1);
        QCOMPARE(lines(askedCore), 2);
        run([](ArcadeBackend &) {});
        QCOMPARE(lines(askedCore), 2);

        // A kept file that can't be read: everything asked again.
        writeFile(kept, "{ \"version\": 1, \"retroarch\": ");
        run([&](ArcadeBackend &arcade) {
            QCOMPARE(ours(arcade, "yourRetroArchState"), QStringLiteral("reading"));
            QTRY_COMPARE_WITH_TIMEOUT(ours(arcade, "yourRetroArchState"), QStringLiteral("read"), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(ours(arcade, "yourCoreState"), QStringLiteral("read"), 5000);
        });
        QCOMPARE(lines(askedRetroArch), 2);
        QCOMPARE(lines(askedCore), 3);
        // And written whole again.
        QCOMPARE(QJsonDocument::fromJson([&] { QFile f(kept); f.open(QIODevice::ReadOnly); return f.readAll(); }())
                     .object().value(QStringLiteral("retroarch")).toObject().value(QStringLiteral("version")).toString(),
                 QStringLiteral("1.22.2"));
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

    // A set that comes into the folder while MAME is asked about the others
    // is asked about as that asking ends, not left for the next time the
    // list opens.
    void scanAsksSetsNewToTheFolder() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        QVERIFY(QFile::setPermissions(data.path() + QStringLiteral("/bin/mame"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        writeFile(games + QStringLiteral("/pacman.zip"));
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        // MAME answers at once and takes its time to exit: the asking is
        // still under way while the folder is read again.
        qputenv("MAME_LIST_LINGER", "3");
        const auto linger = qScopeGuard([] { qunsetenv("MAME_LIST_LINGER"); });
        QFile::remove(m_log);
        arcade.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(logLines(QStringLiteral("answered")).size(), 1, 5000);
        QVERIFY(arcade.scanning());
        writeFile(games + QStringLiteral("/galaga.zip"));
        QSignalSpy reading(&arcade, &ArcadeBackend::readingChanged);
        arcade.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(reading.size() >= 2 && !arcade.reading(), 5000);
        QVERIFY(arcade.scanning());
        QCOMPARE(logLines(QStringLiteral("listxml")), QStringList({ QStringLiteral("listxml pacman") }));
        qunsetenv("MAME_LIST_LINGER");
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.looking() && !arcade.scanning(), 10000);
        QCOMPARE(logLines(QStringLiteral("listxml")),
                 QStringList({ QStringLiteral("listxml pacman"), QStringLiteral("listxml galaga") }));
        QVERIFY(arcade.gameDetails(QStringLiteral("galaga")).contains(QStringLiteral("year")));
    }

    // Nor is MAME run beside its own download, which puts its MAME in the
    // folder it runs from (Windows renames no folder a program runs from):
    // Check ROMs started meanwhile is held, a FinalBurn Neo game that
    // ends meanwhile starts neither it nor the asking the download stopped,
    // a game of MAME's is refused, and both go on once the download has
    // ended. A game started while a
    // check waits only for an asking that has answered, but not exited,
    // doesn't let the check go for the moment before it starts.
    void mameNotBesideItsDownload() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/bin")));
        QVERIFY(QFile::copy(mamePath(), data.path() + QStringLiteral("/bin/mame")));
        writeFile(data.path() + QStringLiteral("/bin/retroarch"), fixture("retroarch"));
        for (const char *program : { "/bin/mame", "/bin/retroarch" })
            QVERIFY(QFile::setPermissions(data.path() + QLatin1String(program),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        writeFile(libretro::downloadedCore(data.path()));
        writeFile(libretro::downloadedDat(data.path()), fixture("fbneo-arcade.dat"));
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        for (const char *set : { "pacman.zip", "galaga.zip", "sf2.zip" })
            writeFile(games + QLatin1Char('/') + QLatin1String(set));
        qputenv("OSDOS_MEDIA_DIR", (data.path() + QStringLiteral("/media")).toUtf8());
        qputenv("RA_CONFIG_COPY", (data.path() + QStringLiteral("/ra.cfg")).toUtf8());
        const auto restore = qScopeGuard([this] {
            qunsetenv("RA_CONFIG_COPY");
            qunsetenv("MAME_LIST_LINGER");
            qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        });
        // MAME's archive from a stand-in that answers only once told to,
        // without it: a download that fails, and leaves MAME as it was.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QPointer<QTcpSocket> asked;
        connect(&server, &QTcpServer::newConnection, &server, [&server, &asked] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &asked] {
                socket->readAll();
                asked = socket;
            });
        });
        // The order MAME was run in, and RetroArch.
        const auto runs = [this] {
            QStringList order;
            for (const QString &line : logLines()) {
                const QString word = line.section(QLatin1Char(' '), 0, 0);
                if (word == QLatin1String("listxml") || word == QLatin1String("answered")
                    || word == QLatin1String("verifyroms") || word == QLatin1String("retroarch")
                    || word == QLatin1String("play") || word == QLatin1String("end"))
                    order << word;
            }
            return order;
        };
        const auto progress = [](ArcadeBackend &arcade, const char *key) {
            return arcade.romCheckProgress().value(QLatin1String(key));
        };

        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        arcade.setMameArchive(QUrl(QStringLiteral("http://127.0.0.1:%1/mame.exe").arg(server.serverPort())));
        refreshed(arcade);
        arcade.onSettingChanged(QStringLiteral("com.osdos.arcade"), QStringLiteral("engines"),
                                QVariantMap{ { QStringLiteral("sf2"), QStringLiteral("fbneo") } });
        QCOMPARE(arcade.engineOf(QStringLiteral("sf2")), QStringLiteral("fbneo"));

        // A set new to the folder, slow to be answered for: MAME is asked
        // about it, and stopped as the download starts.
        writeFile(games + QStringLiteral("/a_slowlist.zip"));
        QFile::remove(m_log);
        arcade.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(logLines(QStringLiteral("listxml")).size(), 1, 5000);
        arcade.downloadMame();
        QVERIFY(arcade.mameDownloading());
        QTRY_VERIFY_WITH_TIMEOUT(asked, 5000);
        QVERIFY(arcade.scanning());

        // Check ROMs: held, MAME having still to answer for a set.
        arcade.checkRoms();
        QVERIFY(progress(arcade, "active").toBool());
        QVERIFY(progress(arcade, "held").toBool());

        // A game played in FinalBurn Neo meanwhile: as it ends, MAME is
        // asked nothing, and the check's part stays held.
        QSignalSpy finished(&arcade, &ArcadeBackend::finished);
        QVERIFY2(arcade.play(QStringLiteral("sf2")), qPrintable(arcade.lastError()));
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().at(1).toString(), QStringLiteral("ok"));
        QTest::qWait(500);
        QCOMPARE(runs(), QStringList({ QStringLiteral("listxml"), QStringLiteral("retroarch") }));
        QVERIFY(progress(arcade, "held").toBool());
        QVERIFY(arcade.mameDownloading());
        // A game of MAME's is refused meanwhile, saying why: its MAME would
        // run from the folder the download replaces.
        finished.clear();
        QVERIFY(!arcade.play(QStringLiteral("pacman")));
        QCOMPARE(arcade.lastError(), QStringLiteral("MAME is still downloading"));
        QVERIFY(!arcade.running());
        QTest::qWait(200);
        QVERIFY(finished.isEmpty());
        QCOMPARE(runs(), QStringList({ QStringLiteral("listxml"), QStringLiteral("retroarch") }));

        // The download ends: the set asked about again, then the check's
        // part.
        asked->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        asked->disconnectFromHost();
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.mameDownloading(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!progress(arcade, "active").toBool(), 10000);
        QCOMPARE(progress(arcade, "result").toString(), QStringLiteral("Done"));
        QVERIFY(!arcade.scanning());
        QCOMPARE(runs(), QStringList({ QStringLiteral("listxml"), QStringLiteral("retroarch"), QStringLiteral("listxml"),
                                       QStringLiteral("answered"), QStringLiteral("verifyroms"),
                                       QStringLiteral("verifyroms") }));

        // A game started while a check waits for an asking that has answered
        // for its set and has still to exit: the asking stopped, the check
        // held from then until the game has ended, never let go between.
        writeFile(games + QStringLiteral("/mspacman.zip"));
        qputenv("MAME_LIST_LINGER", "5");
        QFile::remove(m_log);
        arcade.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(arcade.gameDetails(QStringLiteral("mspacman")).contains(QStringLiteral("year")), 5000);
        QVERIFY(arcade.scanning());
        arcade.checkRoms();
        QVERIFY(progress(arcade, "active").toBool());
        QVERIFY(progress(arcade, "held").toBool());
        bool letGo = false;
        const QMetaObject::Connection watched =
            connect(&arcade, &ArcadeBackend::romCheckChanged, this, [&arcade, &letGo, &progress] {
                if (progress(arcade, "active").toBool() && !progress(arcade, "held").toBool())
                    letGo = true;
            });
        finished.clear();
        QVERIFY(arcade.play(QStringLiteral("pacman")));
        disconnect(watched);
        QVERIFY(!letGo);
        QVERIFY(progress(arcade, "held").toBool());
        QVERIFY(finished.wait(10000));
        QTRY_VERIFY_WITH_TIMEOUT(!progress(arcade, "active").toBool(), 10000);
        QCOMPARE(progress(arcade, "result").toString(), QStringLiteral("Done"));
        QCOMPARE(runs(), QStringList({ QStringLiteral("listxml"), QStringLiteral("answered"), QStringLiteral("play"),
                                       QStringLiteral("end"), QStringLiteral("verifyroms"),
                                       QStringLiteral("verifyroms") }));
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

        // One script for the credits and Show Stats: the credits alone as
        // they were, nothing for neither, Show Stats' line first (the
        // credits' part returns from the script at a reset), the machine's
        // state read only where asked.
        QCOMPARE(mame::autobootScript(3, false, false), script);
        QCOMPARE(mame::autobootScript(3, false, true), script);
        QCOMPARE(mame::autobootScript(0, false, true), QString());
        QCOMPARE(mame::autobootScript(-1, false, false), QString());
        const QString fps = mame::autobootScript(0, true, false);
        QCOMPARE(fps, mame::statsScript(false));
        QVERIFY(fps.contains(QStringLiteral("if not osdos_stats_run then\n  osdos_stats_run = true\n")));
        QVERIFY(fps.contains(QStringLiteral("  local system = false\n")));
        QVERIFY(fps.endsWith(QStringLiteral("  end)\nend\n")));
        QVERIFY(!fps.contains(QStringLiteral("osdos_coins_run")));
        QVERIFY(fps.contains(QStringLiteral("emu.register_frame_done(function()")));
        QVERIFY(fps.contains(QStringLiteral("manager.machine.render.ui_container:draw_text(0.05, 0.05, line,")));
        QVERIFY(mame::statsScript(true).contains(QStringLiteral("  local system = true\n")));
        QCOMPARE(mame::autobootScript(3, true, true), mame::statsScript(true) + script);
        // Only calls both MAMEs it runs with (0.276, and Homebrew's and the
        // BSDs' 0.289) have: a name MAME doesn't know stops the script.
        static const QRegularExpression calls(QStringLiteral("\\b(emu|manager)((?:[.:][a-z_]+)+)"));
        QSet<QString> used;
        for (auto it = calls.globalMatch(mame::autobootScript(3, true, true)); it.hasNext();)
            used.insert(it.next().captured(0));
        QCOMPARE(used, (QSet<QString>{ QStringLiteral("emu.osd_ticks_per_second"), QStringLiteral("emu.osd_ticks"),
                                       QStringLiteral("manager.machine.screens"),
                                       QStringLiteral("emu.register_frame_done"),
                                       QStringLiteral("emu.add_machine_frame_notifier"),
                                       QStringLiteral("manager.machine.video.speed_percent"),
                                       QStringLiteral("manager.machine.render.ui_container:draw_text"),
                                       QStringLiteral("manager.machine.ioport:token_to_input_type"),
                                       QStringLiteral("manager.machine.ioport.ports"),
                                       QStringLiteral("manager.machine.time:as_double") }));
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
            QStringLiteral("recent Recently Played 1"), QStringLiteral("vertical Vertical Games 2"),
            QStringLiteral("genres/Beat 'em Up Beat 'em Up 1"),
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
                                    QStringLiteral("achievements"), QStringLiteral("netplay"), QStringLiteral("vertical"),
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
        QCOMPARE(arcade.filters().size(), 15);
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

    // Settings → Arcade → Hide Bootlegs: the sets MAME calls a
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

    // Settings → Arcade → Regions' choices and the regions a set's name gives
    // in its parentheses, as MAME and FinalBurn Neo write them: several in
    // one name, a country as its region, one the list doesn't name as Others,
    // and none in a title, a maker, a version or a longer word. OSD/OS Setup
    // offers the same choices.
    // Settings → Arcade → Online Name's value on its row: the name the
    // others see (nickname()), on the local network while LAN Play is on;
    // sent again as a name is typed and as LAN Play changes, so the row
    // follows its page.
    void onlineNameRow() {
        const QString module = QStringLiteral("com.osdos.arcade");
        AppCore core(m_data.path(), m_data.path());
        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        QSignalSpy options(&arcade, &ArcadeBackend::dynamicOptionsReady);
        const auto sent = [&options](int at) {
            QStringList got{ options.at(at).at(0).toString() };
            for (const QVariant &v : options.at(at).at(1).toList())
                got << v.toMap().value(QStringLiteral("id")).toString() + QLatin1Char('=')
                           + v.toMap().value(QStringLiteral("label")).toString();
            return got;
        };
        arcade.onlineNameOptions();
        QCOMPARE(options.size(), 1);
        QCOMPARE(sent(0), QStringList({ "online_name", "OSD-OS=OSD-OS" }));
        arcade.onSettingChanged(module, QStringLiteral("lan_play"), QStringLiteral("On"));
        QCOMPARE(sent(options.size() - 1), QStringList({ "online_name", "garage-cab=garage-cab" }));
        arcade.onSettingChanged(module, QStringLiteral("online_name"), QStringLiteral(" Ann "));
        QCOMPARE(sent(options.size() - 1), QStringList({ "online_name", "Ann=Ann" }));
        arcade.onSettingChanged(module, QStringLiteral("online_name"), QString());
        arcade.onSettingChanged(module, QStringLiteral("lan_play"), QStringLiteral("Off"));
        QCOMPARE(sent(options.size() - 1), QStringList({ "online_name", "OSD-OS=OSD-OS" }));
    }

    // Settings → Arcade → Relay Server's choices: the five relays in the
    // row's order, saved by their names, the one nearest the time zone
    // marked as the row's while nothing is saved, whatever is saved. OSD/OS
    // Setup lists the same.
    void relayServerRow() {
        const QString module = QStringLiteral("com.osdos.arcade");
        AppCore core(m_data.path(), m_data.path());
        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        QSignalSpy options(&arcade, &ArcadeBackend::dynamicOptionsReady);
        const auto sent = [&options] {
            QStringList got{ options.last().at(0).toString() };
            for (const QVariant &v : options.last().at(1).toList()) {
                const QVariantMap choice = v.toMap();
                // Shown by the name it is saved under.
                if (choice.value(QStringLiteral("id")) != choice.value(QStringLiteral("label")))
                    got << QStringLiteral("label differs");
                got << choice.value(QStringLiteral("id")).toString()
                           + (choice.value(QStringLiteral("default")).toBool() ? QStringLiteral(" *") : QString());
            }
            return got;
        };
        arcade.setTimeZone(QStringLiteral("Asia/Tokyo"));
        arcade.relayServerOptions();
        QCOMPARE(sent(), QStringList({ "relay_server", "New York", "Madrid", "São Paulo", "Singapore",
                                       "Chuncheon *" }));
        arcade.setTimeZone(QStringLiteral("Europe/Istanbul"));
        arcade.onSettingChanged(module, QStringLiteral("relay_server"), QStringLiteral("Singapore"));
        arcade.relayServerOptions();
        QCOMPARE(sent(), QStringList({ "relay_server", "New York", "Madrid *", "São Paulo", "Singapore",
                                       "Chuncheon" }));
        QCOMPARE(arcade.relayServer(), QStringLiteral("Singapore"));
        arcade.setTimeZone(QString());
        arcade.relayServerOptions();
        QCOMPARE(sent().filter(QStringLiteral(" *")), QStringList({ "New York *" }));

        QFile extra(QStringLiteral(TESTS_DIR "/../site/setup/schema-extra.json"));
        QVERIFY(extra.open(QIODevice::ReadOnly));
        const QJsonArray listed = QJsonDocument::fromJson(extra.readAll()).object().value(QLatin1String("modules"))
                                      .toObject().value(module).toObject().value(QLatin1String("options"))
                                      .toObject().value(QLatin1String("relay_server")).toArray();
        QCOMPARE(listed.toVariantList(), QVariantList({ "New York", "Madrid", "São Paulo", "Singapore", "Chuncheon" }));
    }

    // What the machine is, for Settings → Arcade's rows that do something
    // only on a Raspberry Pi (CPU at Full Speed, Frame Buffers): the
    // manifest's slot for them answers with the capability on any Pi and
    // none elsewhere, and is the one those rows name.
    void machineCapabilities() {
        AppCore core(m_data.path(), m_data.path());
        ArcadeBackend arcade(m_data.path(), m_data.path(), &core, nullptr);
        QSignalSpy options(&arcade, &ArcadeBackend::dynamicOptionsReady);
        for (const auto &[family, expected] : { std::pair{ board::Family::Pi3, QStringList{ "raspberry_pi" } },
                                                std::pair{ board::Family::Pi4, QStringList{ "raspberry_pi" } },
                                                std::pair{ board::Family::Pi5, QStringList{ "raspberry_pi" } },
                                                std::pair{ board::Family::Other, QStringList{} } }) {
            arcade.setBoard(family);
            options.clear();
            QVERIFY(QMetaObject::invokeMethod(&arcade, "machineCapabilities"));
            QCOMPARE(options.size(), 1);
            QCOMPARE(options.at(0).at(0).toString(), QStringLiteral("_capabilities"));
            QCOMPARE(options.at(0).at(1).toStringList(), expected);
        }
        QFile file(QStringLiteral(TESTS_DIR "/../modules/arcade/manifest.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QStringList named;
        std::function<void(const QJsonArray &)> walk = [&](const QJsonArray &rows) {
            for (const QJsonValue &v : rows) {
                const QJsonObject row = v.toObject();
                const QJsonObject need = row.value(QLatin1String("requires_capability")).toObject();
                if (need.value(QLatin1String("name")).toString() == QLatin1String("raspberry_pi")) {
                    QCOMPARE(need.value(QLatin1String("slot")).toString(), QStringLiteral("machineCapabilities"));
                    QCOMPARE(need.value(QLatin1String("reason")).toString(), QStringLiteral("Only on a Raspberry Pi"));
                    named << row.value(QLatin1String("key")).toString();
                }
                walk(row.value(QLatin1String("settings")).toArray());
            }
        };
        walk(QJsonDocument::fromJson(file.readAll()).object().value(QLatin1String("settings")).toArray());
        QCOMPARE(named, QStringList({ "cpu_full_speed", "frame_buffers" }));
        QCOMPARE(QString::fromLatin1(GameSystem::kFullSpeedKey), QStringLiteral("cpu_full_speed"));
    }

    void regionsOfNames() {
        QStringList ids, names;
        for (const mame::Region &region : mame::regions()) {
            ids << region.id;
            names << region.name;
        }
        QCOMPARE(names, QStringList({ "World", "USA", "Europe", "Asia", "Japan", "Korea", "China", "Hong Kong",
                                      "Taiwan", "Hispanic", "Brazil", "Oceania", "Others" }));
        const auto of = [](const char *name) { return mame::regionsOf(QString::fromUtf8(name)); };
        QCOMPARE(of("Street Fighter II: The World Warrior (USA 910206)"), QStringList({ "usa" }));
        QCOMPARE(of("Street Fighter II: The World Warrior (910522 World)"), QStringList({ "world" }));
        QCOMPARE(of("Donkey Kong (US set 1)"), QStringList({ "usa" }));
        QCOMPARE(of("Street Fighter Alpha 3 (Euro 980904)"), QStringList({ "europe" }));
        QCOMPARE(of("Raiden II (Germany)"), QStringList({ "europe" }));
        QCOMPARE(of("Marvel Vs. Capcom (Hong Kong 980123)"), QStringList({ "hong_kong" }));
        QCOMPARE(of("Street Fighter Alpha 3 (Hispanic 980904)"), QStringList({ "hispanic" }));
        QCOMPARE(of("Street Fighter Alpha 3 (Brazil 980629)"), QStringList({ "brazil" }));
        QCOMPARE(of("Cool Boarders Arcade Jam (Export)"), QStringList({ "world" }));
        QCOMPARE(of("Strikers 1945 (Japan / World)"), QStringList({ "world", "japan" }));
        QCOMPARE(of("Block Out (Europe and Oceania)"), QStringList({ "europe", "oceania" }));
        QCOMPARE(of("Aero Fighters (World / USA + Canada / Korea / Hong Kong / Taiwan) (newer hardware)"),
                 QStringList({ "world", "usa", "korea", "hong_kong", "taiwan" }));
        // A region the list doesn't name is Others'.
        QCOMPARE(of("Queen Bee (Israel, Ver. 100)"), QStringList({ "others" }));
        QVERIFY(of("Street Fighter II: The World Warrior").isEmpty());
        QVERIFY(of("Galaga (Namco rev. B)").isEmpty());
        QVERIFY(of("Ms. Pac-Man").isEmpty());
        QVERIFY(of("Mortal Kombat II (rev L3.1, European)").isEmpty());
        QVERIFY(of("Racing Game (Russian, Indiana rev 2)").isEmpty());

        QFile setup(QStringLiteral(TESTS_DIR "/../site/setup/schema-extra.json"));
        QVERIFY(setup.open(QIODevice::ReadOnly));
        QStringList offered;
        const QJsonArray choices = QJsonDocument::fromJson(setup.readAll()).object()
                                       .value(QStringLiteral("modules")).toObject()
                                       .value(QStringLiteral("com.osdos.arcade")).toObject()
                                       .value(QStringLiteral("options")).toObject()
                                       .value(QStringLiteral("regions")).toArray();
        for (const QJsonValue &choice : choices)
            offered << choice.toArray().at(1).toString();
        QCOMPARE(offered, ids);
    }

    // Settings → Arcade → Regions: every region on its page whatever the
    // folder holds. A region turned off (the row saved whole, as its page
    // saves it) leaves every list and filter, Favorites and Recently Played
    // too; a set of several regions goes once they all are off, one whose
    // name gives none never. As the module starts, what was saved holds.
    void hidesRegions() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString games = data.path() + QStringLiteral("/media/Games/Arcade");
        for (const char *set : { "sf2.zip", "sf2ua.zip", "sf2j.zip", "s1945.zip", "galaga.zip" })
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
        core.add_to_list(module, QStringLiteral("favorites"), { { QStringLiteral("path"), QStringLiteral("game:sf2j") } });
        core.add_to_list(module, QStringLiteral("recent"), { { QStringLiteral("path"), QStringLiteral("game:s1945") } });

        ArcadeBackend arcade(data.path(), data.path(), &core, nullptr);
        connect(&core, &AppCore::moduleSettingChanged, &arcade, &ArcadeBackend::onSettingChanged);
        QSignalSpy options(&arcade, &ArcadeBackend::dynamicOptionsReady);
        arcade.regionOptions();
        QCOMPARE(options.size(), 1);
        QCOMPARE(options.first().at(0).toString(), QStringLiteral("regions"));
        QCOMPARE(options.first().at(1).toList().size(), mame::regions().size());
        QCOMPARE(options.first().at(1).toList().at(4).toMap(),
                 QVariantMap({ { QStringLiteral("id"), QStringLiteral("japan") }, { QStringLiteral("label"), QStringLiteral("Japan") } }));
        refreshed(arcade);
        const auto setsOf = [](ArcadeBackend &backend, const char *filter) {
            QStringList list;
            for (const QVariant &v : backend.filterGames(QLatin1String(filter), 0, 50))
                list << v.toMap().value(QStringLiteral("set")).toString();
            return list;
        };
        const auto sets = [&](const char *filter) { return setsOf(arcade, filter); };
        const auto treeSets = [&arcade](const char *path) {
            QStringList list;
            for (const QVariant &v : arcade.requestEntries(QLatin1String(path)).toList())
                list << v.toMap().value(QStringLiteral("game")).toString();
            return list;
        };
        // All on: every game, by name.
        const QStringList all{ QStringLiteral("galaga"), QStringLiteral("sf2"), QStringLiteral("sf2j"),
                               QStringLiteral("sf2ua"), QStringLiteral("s1945") };
        QCOMPARE(sets("all"), all);
        QCOMPARE(sets("favorites"), QStringList({ QStringLiteral("sf2j") }));
        QCOMPARE(sets("recent"), QStringList({ QStringLiteral("s1945") }));

        // Japan off: its Street Fighter II gone, Favorites with it; Strikers
        // 1945 is World's too.
        QSignalSpy changed(&arcade, &ArcadeBackend::gamesChanged);
        core.save_setting(module, QStringLiteral("regions"), QVariantMap{ { QStringLiteral("japan"), false } });
        QCOMPARE(changed.size(), 1);
        QCOMPARE(sets("all"), QStringList({ QStringLiteral("galaga"), QStringLiteral("sf2"), QStringLiteral("sf2ua"),
                                            QStringLiteral("s1945") }));
        QVERIFY(sets("favorites").isEmpty());
        QVERIFY(treeSets("favorites").isEmpty());
        QCOMPARE(sets("recent"), QStringList({ QStringLiteral("s1945") }));
        QCOMPARE(sets("makers/Capcom"), QStringList({ QStringLiteral("sf2"), QStringLiteral("sf2ua") }));

        // World off as well: Strikers 1945 goes; Street Fighter II, whose
        // name gives none, and Galaga stay.
        core.save_setting(module, QStringLiteral("regions"),
                          QVariantMap{ { QStringLiteral("japan"), false }, { QStringLiteral("world"), false } });
        QCOMPARE(changed.size(), 2);
        QCOMPARE(sets("all"), QStringList({ QStringLiteral("galaga"), QStringLiteral("sf2"), QStringLiteral("sf2ua") }));
        QVERIFY(sets("recent").isEmpty());
        QVERIFY(treeSets("recent").isEmpty());

        // As the module starts again, what was saved holds.
        ArcadeBackend again(data.path(), data.path(), &core, nullptr);
        refreshed(again);
        QCOMPARE(setsOf(again, "all"), QStringList({ QStringLiteral("galaga"), QStringLiteral("sf2"), QStringLiteral("sf2ua") }));

        // Both on again: back where they were.
        core.save_setting(module, QStringLiteral("regions"),
                          QVariantMap{ { QStringLiteral("japan"), true }, { QStringLiteral("world"), true } });
        QCOMPARE(changed.size(), 3);
        QCOMPARE(sets("all"), all);
        QCOMPARE(sets("favorites"), QStringList({ QStringLiteral("sf2j") }));
        QCOMPARE(treeSets("recent"), QStringList({ QStringLiteral("s1945") }));
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
            QStringLiteral("vertical=Vertical Games"), QStringLiteral("unknown=Not Recognized"),
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
            QStringLiteral("all"), QStringLiteral("favorites"), QStringLiteral("recent"), QStringLiteral("vertical"),
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
            QStringLiteral("all"), QStringLiteral("favorites"), QStringLiteral("recent"), QStringLiteral("vertical"),
            QStringLiteral("genres/Beat 'em Up"), QStringLiteral("genres/Shmup"),
            QStringLiteral("players/1"), QStringLiteral("players/2"), QStringLiteral("players/6"),
            QStringLiteral("years/1980s"), QStringLiteral("years/1990s"), QStringLiteral("makers/Namco"),
            QStringLiteral("search") }));
        QCOMPARE(arcade.filterGames(QStringLiteral("makers/Namco"), 0, 10).size(), 2);
        options.clear();
        arcade.startFilterOptions();
        QCOMPARE(ids(options.at(0).at(1).toList()), QStringList({
            QStringLiteral("all"), QStringLiteral("favorites"), QStringLiteral("recent"), QStringLiteral("achievements"),
            QStringLiteral("netplay"), QStringLiteral("vertical"),
            QStringLiteral("genres/Beat 'em Up"), QStringLiteral("genres/Shmup"), QStringLiteral("players/1"),
            QStringLiteral("players/2"), QStringLiteral("players/6"), QStringLiteral("years/1980s"),
            QStringLiteral("years/1990s"), QStringLiteral("makers/Namco") }));

        // A heading's all at once: the players off, Favorites, Recently
        // Played, RetroAchievements, Playable Online and Vertical Games too,
        // Favorites off with a game on it as without.
        for (const char *id : { "players/1", "players/2", "players/6", "favorites", "recent", "achievements", "netplay",
                                "vertical" })
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
        // Show Stats: the script with its line alone, then with the credits
        // after it; off again, the arguments as they were.
        const QString module = QStringLiteral("com.osdos.arcade");
        const QString before = logLines(QStringLiteral("play")).last();
        arcade.onSettingChanged(module, QStringLiteral("show_stats"), QStringLiteral("FPS + System"));
        QVERIFY(arcade.play(QStringLiteral("galaga")));
        QVERIFY(finished.wait(10000));
        QCOMPARE(logLines(QStringLiteral("play")).last(), before + QStringLiteral(" -autoboot_script ") + script);
#ifdef Q_OS_LINUX
        QCOMPARE(coins(), mame::statsScript(true));
#else
        QCOMPARE(coins(), mame::statsScript(false));
#endif
        arcade.onSettingChanged(module, QStringLiteral("show_stats"), QStringLiteral("FPS"));
        QVERIFY(arcade.play(QStringLiteral("galaga"), 2));
        QVERIFY(finished.wait(10000));
        QCOMPARE(coins(), mame::statsScript(false) + mame::coinScript(2));
        arcade.onSettingChanged(module, QStringLiteral("show_stats"), QStringLiteral("Off"));
        QVERIFY(arcade.play(QStringLiteral("galaga")));
        QVERIFY(finished.wait(10000));
        QCOMPARE(logLines(QStringLiteral("play")).last(), before);
        QVERIFY(arcade.play(QStringLiteral("galaga"), 2));
        QVERIFY(finished.wait(10000));
        QCOMPARE(coins(), mame::coinScript(2));
    }

    // The module opened again reads nothing while the games folder is as it
    // was: a look at its names says so, and the list stays as it is. A game
    // put in it, or taken out, is found by that look and the folder read.
    void folderLookedAtBeforeItIsRead() {
        const auto restore = qScopeGuard([this] { QFile::remove(m_games + QStringLiteral("/1944.zip")); });
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);                                  // the first: read
        const QVariant before = arcade.requestEntries(QStringLiteral("all"));
        QVERIFY(before.isValid());
        QSignalSpy reading(&arcade, &ArcadeBackend::readingChanged);
        QSignalSpy looked(&arcade, &ArcadeBackend::lookedAtNames);
        arcade.refresh();
        // On the screen at once, the folder not being read.
        QVERIFY(!arcade.reading());
        QVERIFY(arcade.requestEntries(QStringLiteral("all")).isValid());
        QTRY_COMPARE_WITH_TIMEOUT(looked.size(), 1, 10000);
        QCOMPARE(reading.size(), 0);
        QCOMPARE(arcade.requestEntries(QStringLiteral("all")), before);

        writeFile(m_games + QStringLiteral("/1944.zip"));
        arcade.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(reading.size() >= 2, 10000);   // read, and done
        QTRY_VERIFY_WITH_TIMEOUT(!arcade.looking() && !arcade.scanning(), 10000);
        QCOMPARE(looked.size(), 1);
        QVERIFY(names(arcade.requestEntries(QStringLiteral("unknown"))).contains(QStringLiteral("1944.zip")));

        QVERIFY(QFile::remove(m_games + QStringLiteral("/1944.zip")));
        refreshed(arcade);
        QVERIFY(!names(arcade.requestEntries(QStringLiteral("unknown"))).contains(QStringLiteral("1944.zip")));
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
        QCOMPARE(cache.value(QStringLiteral("version")).toInt(), 5);
        cache.remove(QStringLiteral("version"));
        writeFile(path, QJsonDocument(cache).toJson());
        QFile::remove(m_log);
        ArcadeBackend arcade(m_data.path(), m_data.path(), nullptr, nullptr);
        refreshed(arcade);
        QCOMPARE(logLines(QStringLiteral("listxml")),
                 QStringList({ QStringLiteral("listxml acefruit galaga kinst mslug neogeo notagame pacman sf2 xmen6p") }));
        QCOMPARE(arcade.gameDetails(QStringLiteral("sf2")).value(QStringLiteral("system")).toString(), QStringLiteral("CPS-1"));
    }

    // lanLinkWarning(): Host LAN Game's line of the link, against stand-ins
    // for Settings → Network's Wi-Fi and Bluetooth (objects with their
    // properties) and for /proc/net/route (OSDOS_PROC_NET_DIR). The line
    // with the route through Wi-Fi at 2.4 GHz and a Bluetooth input device
    // connected, written to the log once; nothing at 5 GHz, through a cable,
    // without the pads, without the sources, or for a room no longer there.
    void lanLinkWarning() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const auto hex = [](const char *address) {
            quint32 raw = 0;
            qToBigEndian(QHostAddress(QLatin1String(address)).toIPv4Address(), &raw);
            return QStringLiteral("%1").arg(raw, 8, 16, QLatin1Char('0')).toUpper();
        };
        const auto routes = [&](const char *iface) {
            writeFile(data.path() + QStringLiteral("/route"),
                      QStringLiteral("Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n"
                                     "%1\t00000000\t%2\t0003\t0\t0\t600\t00000000\t0\t0\t0\n")
                          .arg(QLatin1String(iface), hex("192.168.1.1")).toUtf8());
        };
        routes("wlan0");
        qputenv("OSDOS_PROC_NET_DIR", data.path().toUtf8());
        const auto restore = qScopeGuard([] { qunsetenv("OSDOS_PROC_NET_DIR"); });
        ArcadeBackend arcade(data.path(), data.path(), nullptr, nullptr);
        QCOMPARE(arcade.lanLinkWarning(), QString());

        QObject wifi;
        QObject bluetooth;
        wifi.setProperty("state", QStringLiteral("connected"));
        wifi.setProperty("current", QVariantMap{ { QStringLiteral("interface"), QStringLiteral("wlan0") },
                                                 { QStringLiteral("frequency"), 2437u },
                                                 { QStringLiteral("band"), QStringLiteral("2.4 GHz") } });
        bluetooth.setProperty("inputConnected", true);
        arcade.setLinkSources(&wifi, &bluetooth);
        const QString line = QStringLiteral("Wi-Fi is on 2.4 GHz with Bluetooth pads: use 5 GHz or a cable");
        QTest::ignoreMessage(QtInfoMsg, qPrintable(QStringLiteral("[Arcade] LAN game over wlan0 at 2437 MHz with a "
                                                                  "Bluetooth input device connected: ") + line));
        QCOMPARE(arcade.lanLinkWarning(), line);
        QCOMPARE(arcade.lanLinkWarning(), line);   // logged once
        QCOMPARE(arcade.lanLinkWarning(7), QString());   // no such room

        bluetooth.setProperty("inputConnected", false);
        QCOMPARE(arcade.lanLinkWarning(), QString());
        bluetooth.setProperty("inputConnected", true);
        wifi.setProperty("current", QVariantMap{ { QStringLiteral("interface"), QStringLiteral("wlan0") },
                                                 { QStringLiteral("frequency"), 5180u } });
        QCOMPARE(arcade.lanLinkWarning(), QString());
        wifi.setProperty("current", QVariantMap{ { QStringLiteral("interface"), QStringLiteral("wlan0") },
                                                 { QStringLiteral("frequency"), 2412u } });
        routes("eth0");
        QCOMPARE(arcade.lanLinkWarning(), QString());
        QFile::remove(data.path() + QStringLiteral("/route"));
        QCOMPARE(arcade.lanLinkWarning(), QString());
    }
};


QTEST_GUILESS_MAIN(ArcadeTest)
#include "arcade_test.moc"
