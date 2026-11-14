// What happens when a video is asked for while another still plays in an mpv
// process (an NFC card tapped mid-film, say): MpvController against a
// stand-in for mpv, a shell script first on PATH that writes down when it
// starts, is told to quit (SIGTERM) and exits, and whether an earlier one was
// still alive as it started. And what becomes of a video its menu's Browse
// left as the next is asked for; and how a tube's shape, whose pixels aren't
// square, reaches mpv.
#include "player/MpvController.h"
#include "player/EmbeddedMpv.h"
#include "player/VideoSurface.h"
#include "audio/MenuMusic.h"
#include "util/DisplayHandoff.h"
#include "AppCore.h"
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

static const char kStandIn[] = R"(#!/bin/sh
log="$FAKE_MPV_LOG"
media=""
for a in "$@"; do media="$a"; done
media=$(basename "$media" .mp4)
if [ -f "$log.pids" ]; then
  while read -r pid name; do
    if [ -r "/proc/$pid/stat" ] && ! grep -q '^[0-9]* ([^)]*) Z' "/proc/$pid/stat"; then
      echo "overlap $media $name" >> "$log"
    fi
  done < "$log.pids"
fi
echo "$$ $media" >> "$log.pids"
printf '%s\n' "$@" > "$log.args"
printf '%s\n' "$FONTCONFIG_FILE" > "$log.fonts"
echo "start $media" >> "$log"
if [ "$FAKE_MPV_MODE" = fail ]; then
  echo "[ffmpeg/video] h264: error while decoding MB 1 2"
  echo "[ffmpeg] https: HTTP error 404 Not Found"
  echo "Failed to open $media.mp4."
  echo 'Exiting... (Errors when loading file)'
  exit 2
fi
if [ "$FAKE_MPV_MODE" = noaudio ]; then
  echo "Could not open/initialize audio device -> no sound."
  echo "VO: [null] 640x480 yuv420p"
  sleep 2
  echo 'Exiting... (Interrupted by error)'
  exit 2
fi
if [ "$FAKE_MPV_MODE" = plays ]; then
  echo "VO: [null] 640x480 yuv420p"
fi
if [ "$FAKE_MPV_MODE" = ignore ]; then
  trap 'echo "term $media" >> "$log"' TERM
else
  trap 'echo "term $media" >> "$log"; sleep "$FAKE_MPV_TERM_DELAY"; echo "exit $media" >> "$log"; exit 0' TERM
fi
i=0
while [ $i -lt 600 ]; do sleep 0.05; i=$((i+1)); done
)";

// The stand-in takes this long to quit when told to: the time the app would
// stand still if it waited for it.
static const char kTermDelay[] = "0.8";

class PlaybackRetireTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_log;
    MpvController *m_mpv = nullptr;

    QStringList events() const {
        QFile f(m_log);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    bool happened(const QString &event) const { return events().contains(event); }
    bool before(const QString &first, const QString &then) const {
        const QStringList all = events();
        return all.contains(first) && all.contains(then) && all.indexOf(first) < all.indexOf(then);
    }
    bool overlapped() const { return events().join(QLatin1Char('\n')).contains(QLatin1String("overlap")); }
    // What the last stand-in started was given.
    QStringList arguments() const {
        QFile f(m_log + QStringLiteral(".args"));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    void play(const char *name, float startSeconds = 0) {
        m_mpv->loadAndPlay(m_dir.path() + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".mp4"),
                           startSeconds, -1, -1);
    }
    // The warnings written to the log while `what` runs, each as it is
    // written.
    static inline QStringList *s_logged = nullptr;
    static void keepWarning(QtMsgType type, const QMessageLogContext &, const QString &message) {
        if (type == QtWarningMsg && s_logged)
            s_logged->append(message);
    }
    template <typename What> static QStringList logged(What what) {
        QStringList lines;
        s_logged = &lines;
        const QtMessageHandler previous = qInstallMessageHandler(keepWarning);
        what();
        qInstallMessageHandler(previous);
        s_logged = nullptr;
        return lines;
    }
    // The run-time folder the controller's files go in (XDG_RUNTIME_DIR): the
    // test's own, and apart from its temp folder, which they used to be in.
    QString runDir() const { return m_dir.path() + QStringLiteral("/run"); }
    // The player's control connection, stood in for by the test: mpv's
    // socket, which the controller connects to once it listens.
    QString socketPath() const { return runDir() + QStringLiteral("/osdos-mpv.sock"); }
    bool listen(QLocalServer &control) {
        QLocalServer::removeServer(socketPath());
        return control.listen(socketPath());
    }
    QVariantMap menuNote(const char *title) const {
        return {{QStringLiteral("module"), QStringLiteral("com.osdos.local_files")},
                {QStringLiteral("title"), QLatin1String(title)}, {QStringLiteral("menu"), true}};
    }
    // A controller with Transparent Background on: its videos play inside
    // the window (libmpv), with `handoff` as the screen's.
    void useEmbedded(AppCore &core, const QString &data, DisplayHandoff *handoff = nullptr) {
        core.save_setting(QString(), QStringLiteral("transparent_background"), QStringLiteral("40"));
        delete m_mpv;
        m_mpv = new MpvController(m_dir.path(), data, &core, handoff);
    }
    // A picture mpv makes itself (lavfi), without a file or sound.
    void playPicture(const char *source) {
        m_mpv->loadAndPlay(QStringLiteral("av://lavfi:") + QLatin1String(source), 0, -1, -1, {}, {}, false, -1, 0,
                           QString(), true);
    }
    static bool write(const QString &path, const QByteArray &text) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
    }
    // A JPEG of this picture as stored, with an EXIF orientation of 6 (turned
    // a quarter clockwise as it is shown), as a camera holding it on its side
    // writes it: Qt writes no orientation of its own.
    static QByteArray onItsSide(const QImage &stored) {
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        stored.save(&buffer, "JPG");
        static const char exif[] = "\xFF\xE1\x00\x22" "Exif\0\0" "MM\0\x2A\0\0\0\x08"
                                   "\0\x01" "\x01\x12\0\x03\0\0\0\x01\0\x06\0\0" "\0\0\0\0";
        jpeg.insert(2, QByteArray(exif, sizeof exif - 1));  // after the JPEG's start
        return jpeg;
    }
    // The machine's own mpv.conf kept out of what mpv is told: the test's
    // own, empty, in its place. False where the system's may give the
    // screen's shape, which would stand as it would for mpv.
    bool ownMpvConf() {
        const QString home = m_dir.path() + QStringLiteral("/mpv-home");
        QFile::remove(home + QStringLiteral("/mpv.conf"));
        if (!QDir().mkpath(home))
            return false;
        qputenv("MPV_HOME", home.toUtf8());
        QFile system(QStringLiteral("/etc/mpv/mpv.conf"));
        return !(system.open(QIODevice::ReadOnly) && system.readAll().contains("aspect"));
    }

private slots:
    void threeFilmsWithoutStopKeepControlsOnNewest() {
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        for (const char *film : {"A", "B", "C"}) {
            play(film);
            QTRY_VERIFY_WITH_TIMEOUT(happened(QStringLiteral("start ") + film), 5000);
            QLocalServer control;
            const QString path = socketPath();
            QLocalServer::removeServer(path);
            QVERIFY(control.listen(path));
            QTRY_VERIFY_WITH_TIMEOUT(control.hasPendingConnections(), 5000);
            QScopedPointer<QLocalSocket> socket(control.nextPendingConnection());
            QByteArray commands;
            connect(socket.data(), &QLocalSocket::readyRead, this, [&]() { commands += socket->readAll(); });
            m_mpv->sendKey(QStringLiteral("UP"));
            m_mpv->seekTo(20000);
            QTRY_VERIFY_WITH_TIMEOUT(commands.contains("keypress") && commands.contains("seek"), 3000);
            QVERIFY(commands.contains("UP"));
            QVERIFY(commands.contains("absolute+exact"));
            // The picture can keep playing after its control socket drops.
            // Re-establish IPC instead of leaving every key except stop inert.
            socket.reset();
            QTRY_VERIFY_WITH_TIMEOUT(control.hasPendingConnections(), 5000);
            socket.reset(control.nextPendingConnection());
            commands.clear();
            connect(socket.data(), &QLocalSocket::readyRead, this, [&]() { commands += socket->readAll(); });
            m_mpv->sendKey(QStringLiteral("DOWN"));
            m_mpv->seekTo(10000);
            QTRY_VERIFY_WITH_TIMEOUT(commands.contains("DOWN") && commands.contains("seek"), 3000);
        }
        QVERIFY(before("exit A", "start B"));
        QVERIFY(before("exit B", "start C"));
        QVERIFY(!overlapped());
        QCOMPARE(ended.count(), 0); // old exits must not stop the current view
    }

    void initTestCase() {
        QVERIFY(m_dir.isValid());
        const QString bin = m_dir.path() + QStringLiteral("/bin");
        QVERIFY(QDir().mkpath(bin));
        QFile mpv(bin + QStringLiteral("/mpv"));
        QVERIFY(mpv.open(QIODevice::WriteOnly));
        mpv.write(kStandIn);
        mpv.close();
        QVERIFY(mpv.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        qputenv("PATH", bin.toUtf8() + ':' + qgetenv("PATH"));
        // The controller's socket, logs and input.conf, away from a running
        // app's: in a run-time folder of the test's own, the user's alone as
        // a login session's is.
        qputenv("TMPDIR", m_dir.path().toUtf8());
        QVERIFY(QDir().mkpath(runDir()));
        QVERIFY(QFile::setPermissions(runDir(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        qputenv("XDG_RUNTIME_DIR", runDir().toUtf8());
        m_log = m_dir.path() + QStringLiteral("/mpv.log");
        qputenv("FAKE_MPV_LOG", m_log.toUtf8());
        qputenv("FAKE_MPV_TERM_DELAY", kTermDelay);
    }

    void init() {
        QFile::remove(m_log);
        QFile::remove(m_log + QStringLiteral(".pids"));
        qputenv("FAKE_MPV_MODE", "quit");
        m_mpv = new MpvController(m_dir.path(), m_dir.path());
    }

    void cleanup() {
        delete m_mpv;  // ends what still plays, killing it if it must
        m_mpv = nullptr;
    }

    // What mpv is handed for a session (its control socket, log, key
    // bindings and fontconfig file) is in the user's run-time folder, not
    // under a fixed name in the temp folder, which other accounts write in.
    // The bindings' file is the app's own, whole and its owner's alone: made
    // again when it is gone or holds something else, and not named to mpv at
    // all when it can't be written.
    // mpv is given every OpenGL function as the context has it, but
    // glFenceSync: one that makes no fence. mpv 0.40 leaves a fence behind
    // for every picture it draws for the render API, and on a Pi 4 each is a
    // file descriptor: a film's length of them used up all the app may open.
    // Where the context has no fences, mpv is given none.
    void mpvIsGivenNoFences() {
        int marker = 0;
        void *real = &marker;
        QCOMPARE(EmbeddedMpv::glFunctionFor("glClear", real), real);
        QCOMPARE(EmbeddedMpv::glFunctionFor("glDeleteSync", real), real);
        QCOMPARE(EmbeddedMpv::glFunctionFor("glClientWaitSync", real), real);
        QCOMPARE(EmbeddedMpv::glFunctionFor("glFenceSyncAPPLE", real), real);
        QCOMPARE(EmbeddedMpv::glFunctionFor("glFenceSync", nullptr), static_cast<void *>(nullptr));
        void *given = EmbeddedMpv::glFunctionFor("glFenceSync", real);
        QVERIFY(given != nullptr);
        QVERIFY(given != real);
        const quint64 before = EmbeddedMpv::fencesNotMade();
#if defined(Q_OS_WIN) && !defined(Q_OS_WIN64)
        using FenceSync = void *(__stdcall *)(unsigned int, unsigned int);
#else
        using FenceSync = void *(*)(unsigned int, unsigned int);
#endif
        // GL_SYNC_GPU_COMMANDS_COMPLETE, as mpv asks.
        QCOMPARE(reinterpret_cast<FenceSync>(given)(0x9117, 0), static_cast<void *>(nullptr));
        QCOMPARE(EmbeddedMpv::fencesNotMade(), before + 1);
    }

    void runFilesAreTheUsersOwn() {
        const auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        const QString conf = runDir() + QStringLiteral("/osdos-input.conf");
        const QString named = QStringLiteral("--input-conf=") + conf;
        const QByteArray bindings = "ESC script-message osdos-menu\nBS script-message osdos-menu\nENTER cycle pause\n";
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QVERIFY2(arguments().contains(QStringLiteral("--input-ipc-server=") + socketPath()),
                 qPrintable(arguments().join(QLatin1Char(' '))));
        QVERIFY(arguments().contains(QStringLiteral("--log-file=") + runDir() + QStringLiteral("/osdos-mpv.log")));
        QVERIFY(arguments().contains(named));
        QCOMPARE(read(conf), bindings);
        QVERIFY(!(QFile::permissions(conf) & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
                                             | QFileDevice::ReadOther | QFileDevice::WriteOther)));
        QCOMPARE(read(m_log + QStringLiteral(".fonts")).trimmed(),
                 (runDir() + QStringLiteral("/osdos-fonts.conf")).toUtf8());
        QVERIFY(read(runDir() + QStringLiteral("/osdos-fonts.conf")).contains("/assets/fonts</dir>"));
        // Nothing of it in the temp folder.
        QVERIFY(QDir(m_dir.path()).entryList({ QStringLiteral("osdos-*") }, QDir::AllEntries | QDir::System).isEmpty());

        QVERIFY(write(conf, "ESC quit\n"));
        play("B");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start B"), 5000);
        QVERIFY(arguments().contains(named));
        QCOMPARE(read(conf), bindings);

        QVERIFY(QFile::remove(conf));
        play("C");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start C"), 5000);
        QVERIFY(arguments().contains(named));
        QCOMPARE(read(conf), bindings);

        // A folder has its name: it can't be written.
        QVERIFY(QFile::remove(conf));
        QVERIFY(QDir().mkpath(conf + QStringLiteral("/held")));
        const auto free = qScopeGuard([conf] { QDir(conf).removeRecursively(); });
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(
                                               "^\\[MpvController\\] The keys' file for mpv couldn't be written")));
        play("D");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start D"), 5000);
        QVERIFY2(arguments().filter(QStringLiteral("--input-conf")).isEmpty(),
                 qPrintable(arguments().join(QLatin1Char(' '))));
        QVERIFY(!arguments().filter(QStringLiteral("--input-ar-delay=")).isEmpty());
    }

    // On a tube mpv is told the screen is 4:3 whatever its pixels; on a
    // screen of square ones, nothing. A shape of the user's own in mpv.conf
    // stands, under a profile too (one in use would lose to the command
    // line); one commented out doesn't.
    void aTubeIsToldItsShape() {
        const auto unset = qScopeGuard([] { qunsetenv("MPV_HOME"); });
        if (!ownMpvConf())
            QSKIP("/etc/mpv/mpv.conf may give the screen's shape");
        m_mpv->setScreen(true, QSize(720, 240));
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QVERIFY(arguments().contains(QStringLiteral("--monitoraspect=4:3")));
        m_mpv->setScreen(false, QSize(1920, 1080));
        play("B");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start B"), 5000);
        QVERIFY(!arguments().join(QLatin1Char(' ')).contains(QLatin1String("monitoraspect")));
        const QString conf = qEnvironmentVariable("MPV_HOME") + QStringLiteral("/mpv.conf");
        QVERIFY(write(conf, "sub-scale=1.2\n# monitorpixelaspect=0.95\n"));
        m_mpv->setScreen(true, QSize(720, 240));
        play("C");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start C"), 5000);
        QVERIFY(arguments().contains(QStringLiteral("--monitoraspect=4:3")));
        QVERIFY(write(conf, "sub-scale=1.2\nmonitorpixelaspect=0.95\n"));
        play("D");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start D"), 5000);
        QVERIFY(!arguments().join(QLatin1Char(' ')).contains(QLatin1String("monitoraspect")));
        QVERIFY(write(conf, "[crt]\nmonitoraspect=1.36\n[default]\nprofile=crt\n"));
        play("E");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start E"), 5000);
        QVERIFY(!arguments().join(QLatin1Char(' ')).contains(QLatin1String("monitoraspect")));
    }

    // On a tube, Logo Image is made as much wider as its pixels are narrower
    // than tall, and a camera's picture stored on its side as it is shown. A
    // shape of the user's own in mpv_video_args stands in place of 4:3.
    void aTubesLogoKeepsItsShape() {
        const auto unset = qScopeGuard([] { qunsetenv("MPV_HOME"); });
        if (!ownMpvConf())
            QSKIP("/etc/mpv/mpv.conf may give the screen's shape");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QVERIFY(QDir().mkpath(m_dir.path() + QStringLiteral("/scripts")));
        QVERIFY(write(m_dir.path() + QStringLiteral("/scripts/mpv-logo.lua"), "-- stand-in\n"));
        // 300×200 as shown: stored 200×300, its EXIF turning it.
        const QString photo = data.path() + QStringLiteral("/photo.jpg");
        QImage stored(200, 300, QImage::Format_RGB32);
        stored.fill(Qt::red);
        QVERIFY(write(photo, onItsSide(stored)));
        QCOMPARE(int(QImageReader(photo).transformation()), int(QImageIOHandler::TransformationRotate90));
        AppCore core(data.path(), data.path());
        QVERIFY(core.save_setting(QString(), QStringLiteral("video_logo"), QStringLiteral("tr")));
        QVERIFY(core.save_setting(QString(), QStringLiteral("video_logo_image"), photo));
        delete m_mpv;
        m_mpv = new MpvController(m_dir.path(), data.path(), &core);
        const auto end = qScopeGuard([this] { delete m_mpv; m_mpv = nullptr; });
        m_mpv->setScreen(true, QSize(720, 240));
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        // 7% of 240 lines is 17, and 300×200 at that is 25.5 wide on square
        // pixels: 57 on pixels 4/9 as wide as they are tall.
        const QString opts = arguments().filter(QStringLiteral("--script-opts=")).value(0);
        QVERIFY2(opts.contains(QStringLiteral("logo-width=57,logo-height=17")), qPrintable(opts));
        QVERIFY(arguments().contains(QStringLiteral("--monitoraspect=4:3")));
        QVERIFY(core.save_setting(QString(), QStringLiteral("mpv_video_args"),
                                  QStringLiteral("--vo=drm --monitorpixelaspect=0.5")));
        play("B");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start B"), 5000);
        QVERIFY(!arguments().contains(QStringLiteral("--monitoraspect=4:3")));
        QVERIFY(arguments().contains(QStringLiteral("--monitorpixelaspect=0.5")));
    }

    // The window's own session draws on square pixels in the tube's shape:
    // a JAMMA board's 720×240 and composite's 720×480 on 640×480, the lines
    // doubled on 240; PAL's 720×576, wider pixels, on 768×576. Square ones
    // stay as they are.
    void aTubeDrawsInItsShape() {
        const auto on43 = [](int width, int height) {
            return VideoSurface::drawingSize(QSize(width, height), (4.0 / 3) * height / width);
        };
        QCOMPARE(on43(720, 240), QSize(640, 480));
        QCOMPARE(on43(720, 480), QSize(640, 480));
        QCOMPARE(on43(720, 576), QSize(768, 576));
        QCOMPARE(on43(1920, 240), QSize(640, 480));
        QCOMPARE(on43(320, 240), QSize(320, 240));
        QCOMPARE(VideoSurface::drawingSize(QSize(1920, 1080), 1), QSize(1920, 1080));
    }

    // Back from the player's view used to go to mpv as a key and come back
    // over the control connection as osdos-menu; it is acted on here now,
    // at once, unless mpv's own deck menu or screen saver is up, which say
    // so and take it. Without the connection it comes all the same.
    void backActsAtOnce() {
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QLocalServer control;
        const QString path = socketPath();
        QLocalServer::removeServer(path);
        QVERIFY(control.listen(path));
        QTRY_VERIFY_WITH_TIMEOUT(control.hasPendingConnections(), 5000);
        QScopedPointer<QLocalSocket> socket(control.nextPendingConnection());
        QByteArray commands;
        connect(socket.data(), &QLocalSocket::readyRead, this, [&]() { commands += socket->readAll(); });
        // The deck's menu up: back goes to mpv, whose menu closes on it.
        socket->write("{\"event\":\"client-message\",\"args\":[\"osdos-osd-menu\",\"shown\"]}\n");
        socket->flush();
        QTest::qWait(250);
        m_mpv->sendKey(QStringLiteral("ESC"));
        QTRY_VERIFY_WITH_TIMEOUT(commands.contains("keypress"), 3000);
        QVERIFY(commands.contains("ESC"));
        QVERIFY(!commands.contains("quit"));
        commands.clear();
        // The menu gone: back acts here, telling the process to quit.
        socket->write("{\"event\":\"client-message\",\"args\":[\"osdos-osd-menu\",\"hidden\"]}\n");
        socket->flush();
        QTest::qWait(250);
        m_mpv->sendKey(QStringLiteral("BS"));
        QTRY_VERIFY_WITH_TIMEOUT(commands.contains("quit"), 3000);
        QVERIFY(!commands.contains("keypress"));
        QCOMPARE(ended.count(), 0);  // the stand-in takes no quit: still playing
        // Without the connection (none made yet, or being made again), back
        // ends the process all the same, and its player, with a menu of its
        // own, is told the video ended for it.
        socket.reset();
        control.close();
        QLocalServer::removeServer(path);
        play("B");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start B"), 5000);
        m_mpv->noteSession({{QStringLiteral("module"), QStringLiteral("com.osdos.local_files")},
                            {QStringLiteral("title"), QStringLiteral("B")}, {QStringLiteral("menu"), true}});
        m_mpv->sendKey(QStringLiteral("ESC"));
        QTRY_VERIFY_WITH_TIMEOUT(happened("exit B"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 3000);
        QCOMPARE(ended.first().at(2).toString(), QStringLiteral("menu"));
    }

    // Transparent Background: back leaves the video playing behind the
    // menus at once, without a word to mpv and back; and a picture no one
    // sees (its surface hidden behind a solid ground) is drawn a few pixels
    // wide until it shows again.
    void backLeavesAnEmbeddedVideoBehindTheMenus() {
        if (!EmbeddedMpv::available())
            QSKIP("libmpv isn't installed here");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(data.path(), data.path());
        QVERIFY(core.save_setting(QString(), QStringLiteral("transparent_background"), QStringLiteral("40")));
        delete m_mpv;
        m_mpv = new MpvController(m_dir.path(), data.path(), &core);
        const auto end = qScopeGuard([this] { delete m_mpv; m_mpv = nullptr; });
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        QSignalSpy background(m_mpv, &MpvController::backgroundChanged);
        // A picture mpv makes itself, without a file or sound.
        m_mpv->loadAndPlay(QStringLiteral("av://lavfi:color=c=gray:s=64x48:r=10"), 0, -1, -1, {}, {}, false, -1, 0,
                           QString(), true);
        QTRY_VERIFY_WITH_TIMEOUT(m_mpv->videoActive(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_mpv->videoFrame().isNull(), 10000);
        QCOMPARE(m_mpv->videoFrame().size(), QSize(640, 480));
        QVERIFY(!m_mpv->background());
        m_mpv->sendKey(QStringLiteral("ESC"));
        // Before any event is handled.
        QVERIFY(m_mpv->background());
        QCOMPARE(background.count(), 1);
        QCOMPARE(ended.count(), 1);
        QCOMPARE(ended.first().at(2).toString(), QStringLiteral("stopped"));
        QVERIFY(m_mpv->videoActive());
        // Hidden: the pictures a few pixels wide; shown: whole again.
        m_mpv->setVideoShown(false);
        QTRY_COMPARE_WITH_TIMEOUT(m_mpv->videoFrame().size(), QSize(16, 16), 5000);
        m_mpv->setVideoShown(true);
        QTRY_COMPARE_WITH_TIMEOUT(m_mpv->videoFrame().size(), QSize(640, 480), 5000);
        // Behind the menus back does nothing more, and STOP ends it without
        // a word: its module took it as stopped already.
        m_mpv->sendKey(QStringLiteral("BS"));
        QCOMPARE(ended.count(), 1);
        m_mpv->stopBackground();
        QTRY_VERIFY_WITH_TIMEOUT(!m_mpv->videoActive(), 5000);
        QCOMPARE(ended.count(), 1);
    }

    // Transparent Background: Settings → TV / Movie → Scaling and Video
    // Levels reach a video playing behind the menus at once, as a session
    // starts with them (mpv asked what it has over its own socket, as another
    // client), and the video chosen again carries on, the same session. A
    // video of a module with a Scaling of its own keeps that.
    void scalingAndLevelsReachAVideoBehindTheMenus() {
        if (!EmbeddedMpv::available())
            QSKIP("libmpv isn't installed here");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(data.path(), data.path());
        useEmbedded(core, data.path());
        const auto end = qScopeGuard([this] { delete m_mpv; m_mpv = nullptr; });
        const QString module = QStringLiteral("com.osdos.local_files");
        m_mpv->setActiveModule(module);
        playPicture("color=c=gray:s=64x48:r=10");
        QTRY_VERIFY_WITH_TIMEOUT(m_mpv->videoActive(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_mpv->videoFrame().isNull(), 10000);
        m_mpv->sendKey(QStringLiteral("ESC"));
        QVERIFY(m_mpv->background());
        // Settings open over it.
        m_mpv->setActiveModule(QString());

        QLocalSocket probe;
        QTRY_VERIFY_WITH_TIMEOUT((probe.state() == QLocalSocket::ConnectedState
                                  || (probe.connectToServer(socketPath()), probe.waitForConnected(500))), 10000);
        int asked = 0;
        auto property = [&probe, &asked](const char *name) {
            const int id = ++asked;
            probe.write(QJsonDocument(QJsonObject{ { "command", QJsonArray{ "get_property", name } },
                                                   { "request_id", id } }).toJson(QJsonDocument::Compact) + '\n');
            QElapsedTimer waited;
            waited.start();
            while (waited.elapsed() < 3000) {
                if (!probe.canReadLine() && !probe.waitForReadyRead(200))
                    continue;
                const QJsonObject answer = QJsonDocument::fromJson(probe.readLine()).object();
                if (answer.value("request_id").toInt() == id)
                    return answer.value("data").toVariant();
            }
            return QVariant(QStringLiteral("no answer"));
        };
        QCOMPARE(property("panscan").toDouble(), 0.0);
        QCOMPARE(property("keepaspect").toBool(), true);
        QCOMPARE(property("video-output-levels").toString(), QStringLiteral("auto"));

        QVERIFY(core.save_setting(QString(), QStringLiteral("video_scaling"), QStringLiteral("Pan & Scan")));
        QTRY_COMPARE_WITH_TIMEOUT(property("panscan").toDouble(), 1.0, 5000);
        QVERIFY(core.save_setting(QString(), QStringLiteral("video_scaling"), QStringLiteral("14:9")));
        QTRY_COMPARE_WITH_TIMEOUT(property("panscan").toDouble(), 0.43, 5000);
        QVERIFY(core.save_setting(QString(), QStringLiteral("video_scaling"), QStringLiteral("Anamorphic")));
        QTRY_COMPARE_WITH_TIMEOUT(property("keepaspect").toBool(), false, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(property("panscan").toDouble(), 0.0, 5000);
        QVERIFY(core.save_setting(QString(), QStringLiteral("video_scaling"), QStringLiteral("Letterbox")));
        QTRY_COMPARE_WITH_TIMEOUT(property("keepaspect").toBool(), true, 5000);
        QVERIFY(core.save_setting(QString(), QStringLiteral("video_output_levels"), QStringLiteral("Limited")));
        QTRY_COMPARE_WITH_TIMEOUT(property("video-output-levels").toString(), QStringLiteral("limited"), 5000);
        QVERIFY(core.save_setting(QString(), QStringLiteral("video_output_levels"), QStringLiteral("Full")));
        QTRY_COMPARE_WITH_TIMEOUT(property("video-output-levels").toString(), QStringLiteral("full"), 5000);

        // Chosen again in its module: the session carries on, as it is.
        m_mpv->setActiveModule(module);
        playPicture("color=c=gray:s=64x48:r=10");
        QVERIFY(!m_mpv->background());
        QCOMPARE(property("video-output-levels").toString(), QStringLiteral("full"));
        QCOMPARE(probe.state(), QLocalSocket::ConnectedState);
        m_mpv->sendKey(QStringLiteral("ESC"));
        QVERIFY(m_mpv->background());
        m_mpv->setActiveModule(QString());

        // Its module's own Scaling stays its video's, whatever Settings' is;
        // back on Default it is Settings' again, taken as it is chosen again.
        QVERIFY(core.save_setting(module, QStringLiteral("video_scaling"), QStringLiteral("14:9")));
        QVERIFY(core.save_setting(QString(), QStringLiteral("video_scaling"), QStringLiteral("Pan & Scan")));
        QVERIFY(core.save_setting(QString(), QStringLiteral("video_output_levels"), QStringLiteral("Auto")));
        QTRY_COMPARE_WITH_TIMEOUT(property("video-output-levels").toString(), QStringLiteral("auto"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(property("panscan").toDouble(), 0.43, 5000);
        QVERIFY(core.save_setting(module, QStringLiteral("video_scaling"), QStringLiteral("Default")));
        m_mpv->setActiveModule(module);
        playPicture("color=c=gray:s=64x48:r=10");
        QVERIFY(!m_mpv->background());
        QTRY_COMPARE_WITH_TIMEOUT(property("panscan").toDouble(), 1.0, 5000);
        QCOMPARE(probe.state(), QLocalSocket::ConnectedState);
    }

    // Transparent Background: a video asked for while the last one's player
    // has its menu open over the picture (a card tapped, a favourite chosen)
    // used to hear, inside its own loadAndPlay(), that playback had ended:
    // the last session's end, which its view took for its own and left on.
    // The end of a session replaced is no one's to hear.
    void aVideoReplacedUnderItsMenuTellsNoOne() {
        if (!EmbeddedMpv::available())
            QSKIP("libmpv isn't installed here");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(data.path(), data.path());
        useEmbedded(core, data.path());
        const auto end = qScopeGuard([this] { delete m_mpv; m_mpv = nullptr; });
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        QSignalSpy menu(m_mpv, &MpvController::playerMenuRequested);
        playPicture("color=c=gray:s=64x48:r=10");
        QTRY_VERIFY_WITH_TIMEOUT(!m_mpv->videoFrame().isNull(), 10000);
        m_mpv->noteSession(menuNote("gray"));
        m_mpv->sendKey(QStringLiteral("ESC"));
        QCOMPARE(menu.count(), 1);
        QVERIFY(m_mpv->background());
        QCOMPARE(ended.count(), 0);
        playPicture("color=c=blue:s=64x48:r=10");
        QCOMPARE(ended.count(), 0);  // not inside the call
        QVERIFY(!m_mpv->background());
        QTRY_VERIFY_WITH_TIMEOUT(m_mpv->videoActive() && !m_mpv->videoFrame().isNull(), 10000);
        QTest::qWait(300);
        QCOMPARE(ended.count(), 0);  // nor after: the end was the old session's
    }

    // Something else taking the screen (a game, a takeover script) while a
    // video plays inside the window full screen: the video ends and its
    // player is told, a tick later, where before only a video behind the
    // menus ended and one full screen played on, unseen, under the game.
    void aHandOffEndsAFullScreenVideo() {
        if (!EmbeddedMpv::available())
            QSKIP("libmpv isn't installed here");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(data.path(), data.path());
        DisplayHandoff handoff;
        useEmbedded(core, data.path(), &handoff);
        const auto end = qScopeGuard([this] { delete m_mpv; m_mpv = nullptr; });
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        playPicture("color=c=gray:s=64x48:r=10");
        QTRY_VERIFY_WITH_TIMEOUT(!m_mpv->videoFrame().isNull(), 10000);
        emit handoff.handingOff(QStringLiteral("arcade"));
        QVERIFY(!m_mpv->videoActive());
        QCOMPARE(ended.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 3000);
        QCOMPARE(ended.first().at(2).toString(), QStringLiteral("stopped"));
    }

    // Replacing a video played inside the window takes its core down, which
    // joins its decoder, sound output and scripts: on a thread of its own
    // now, the next starting once it has gone, where before the GUI thread
    // stood still for it with the loading screen on it.
    void replacingAnEmbeddedVideoDoesNotHoldTheApp() {
        if (!EmbeddedMpv::available())
            QSKIP("libmpv isn't installed here");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(data.path(), data.path());
        useEmbedded(core, data.path());
        const auto end = qScopeGuard([this] { delete m_mpv; m_mpv = nullptr; });
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        playPicture("testsrc=s=640x480:r=30");
        QTRY_VERIFY_WITH_TIMEOUT(!m_mpv->videoFrame().isNull(), 10000);
        QSignalSpy positions(m_mpv, &MpvController::positionChanged);
        QElapsedTimer held;
        held.start();
        playPicture("color=c=blue:s=64x48:r=10");
        const qint64 ms = held.elapsed();
        // Freeing the render context alone took a quarter of a second here
        // with the picture playing; a render thread's last picture, at most,
        // now.
        QVERIFY2(ms < 150, qPrintable(QStringLiteral("loadAndPlay held the app %1 ms").arg(ms)));
        QVERIFY(m_mpv->videoFrame().isNull());  // the old picture let go at once
        QTRY_VERIFY_WITH_TIMEOUT(m_mpv->videoActive() && !m_mpv->videoFrame().isNull(), 10000);
        QCOMPARE(m_mpv->videoFrame().size(), QSize(640, 480));
        QTRY_VERIFY_WITH_TIMEOUT(positions.count() > 0, 10000);
        QCOMPARE(ended.count(), 0);
    }

    // The app goes on while the old player quits, and the new one starts
    // once it has.
    void replacingDoesNotHoldTheApp() {
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QElapsedTimer held;
        held.start();
        play("B");
        QVERIFY2(held.elapsed() < 400, qPrintable(QStringLiteral("loadAndPlay held the app %1 ms").arg(held.elapsed())));
        QTRY_VERIFY_WITH_TIMEOUT(happened("start B"), 5000);
        QVERIFY(before("exit A", "start B"));
        QVERIFY(!overlapped());
    }

    // Three asked for in a row: only the last plays.
    void onlyTheLastRequestPlays() {
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        play("B");
        play("C");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start C"), 5000);
        QVERIFY(!happened("start B"));
        QVERIFY(before("exit A", "start C"));
    }

    // Stopped while it waits for the old player: nothing starts, and its
    // player hears it stopped where it was to start, once.
    void stopWhileWaiting() {
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        play("B", 7);
        QTest::qWait(100);  // past the launch tick: B waits for A
        m_mpv->stop();
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
        QCOMPARE(ended.first().at(0).toInt(), 7000);
        QCOMPARE(ended.first().at(2).toString(), QStringLiteral("stopped"));
        QTest::qWait(500);
        QCOMPARE(ended.count(), 1);
        QVERIFY(!happened("start B"));
    }

    // Menu music playing as a video is asked for: it is gone before the
    // video's player starts, so the sound card is free for it.
    void menuMusicStopsFirst() {
        const QString tune = m_dir.path() + QStringLiteral("/tune.ogg");
        QFile(tune).open(QIODevice::WriteOnly);
        MenuMusic music;
        music.setSource(tune);
        music.setWanted(true);
        QTRY_VERIFY_WITH_TIMEOUT(happened("start tune.ogg"), 5000);
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QVERIFY(!overlapped());
        QVERIFY(!music.playing());
        // It stays off while the video plays.
        QTest::qWait(1200);
        QCOMPARE(events().filter(QStringLiteral("start tune.ogg")).size(), 1);
    }

    // Stopped, then another asked for straight away: that one plays, and
    // the stop isn't told, as its player would take it for the new one's end.
    void stopThenAnother() {
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        play("B", 7);
        m_mpv->stop();
        play("C");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start C"), 5000);
        QVERIFY(!happened("start B"));
        QTest::qWait(300);
        QCOMPARE(ended.count(), 0);
    }

    // A player that ignores being told to quit is killed a second on, and
    // the next starts once it is gone, never beside it.
    void stubbornPlayerIsKilled() {
        qputenv("FAKE_MPV_MODE", "ignore");
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        qputenv("FAKE_MPV_MODE", "quit");
        QElapsedTimer since;
        since.start();
        play("B");
        QVERIFY(since.elapsed() < 400);
        QTRY_VERIFY_WITH_TIMEOUT(happened("start B"), 5000);
        QVERIFY2(since.elapsed() >= 900, qPrintable(QStringLiteral("B started after %1 ms").arg(since.elapsed())));
        QVERIFY(happened("term A"));
        QVERIFY(!overlapped());
    }

    // Keys pressed while the next player is on its way (loadAndPlay()'s tick,
    // the old player quitting) used to be dropped, its loading screen up
    // all the while: they wait for it now, as keys wait for its control
    // connection, and reach it in their order once it listens.
    void keysInTheLaunchGapReachTheNextPlayer() {
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        play("B");
        m_mpv->sendKey(QStringLiteral("UP"));    // within the tick
        QTest::qWait(60);                        // past it: B waits for A to quit
        m_mpv->sendKey(QStringLiteral("DOWN"));
        QTRY_VERIFY_WITH_TIMEOUT(happened("start B"), 5000);
        QLocalServer control;
        QVERIFY(listen(control));
        QTRY_VERIFY_WITH_TIMEOUT(control.hasPendingConnections(), 5000);
        QScopedPointer<QLocalSocket> socket(control.nextPendingConnection());
        QByteArray commands;
        connect(socket.data(), &QLocalSocket::readyRead, this, [&]() { commands += socket->readAll(); });
        // What came as it connected, before anything was listening for it.
        commands += socket->readAll();
        QTRY_VERIFY2_WITH_TIMEOUT(commands.contains("DOWN"), commands.constData(), 3000);
        QVERIFY(commands.contains("UP"));
        QVERIFY(commands.indexOf("observe_property") < commands.indexOf("UP"));
        QVERIFY(commands.indexOf("UP") < commands.indexOf("DOWN"));
    }

    // A key held: its press goes as one, its repeats hold it down in mpv,
    // which repeats it at its own pace (the session's --input-ar-* options)
    // until it is let go (releaseKeys), or until the repeats stop coming.
    // Sent as a press each, they queued up in a busy player and went on
    // after the finger had come off.
    void aHeldKeyIsHeldDownInMpv() {
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QVERIFY(arguments().contains(QStringLiteral("--input-ar-delay=150")));
        QVERIFY(arguments().contains(QStringLiteral("--input-ar-rate=6")));
        QLocalServer control;
        QVERIFY(listen(control));
        QTRY_VERIFY_WITH_TIMEOUT(control.hasPendingConnections(), 5000);
        QScopedPointer<QLocalSocket> socket(control.nextPendingConnection());
        QByteArray commands;
        connect(socket.data(), &QLocalSocket::readyRead, this, [&]() { commands += socket->readAll(); });
        commands += socket->readAll();  // what came as it connected
        QTRY_VERIFY_WITH_TIMEOUT(commands.contains("observe_property"), 3000);
        QTest::qWait(50);
        commands.clear();
        const auto lines = [&commands]() {
            QStringList out;
            for (const QByteArray &line : commands.split('\n'))
                if (!line.isEmpty())
                    out << QString::fromUtf8(line);
            return out;
        };
        m_mpv->sendKey(QStringLiteral("RIGHT"));
        m_mpv->sendKey(QStringLiteral("RIGHT"), true);
        m_mpv->sendKey(QStringLiteral("RIGHT"), true);
        m_mpv->sendKey(QStringLiteral("RIGHT"), true);
        QTRY_COMPARE_WITH_TIMEOUT(lines().size(), 2, 3000);
        QVERIFY(lines().at(0).contains(QStringLiteral("\"keypress\",\"RIGHT\"")));
        QVERIFY(lines().at(1).contains(QStringLiteral("\"keydown\",\"RIGHT\"")));
        QTest::qWait(150);
        QCOMPARE(lines().size(), 2);  // the repeats hold it, nothing more goes
        m_mpv->releaseKeys();
        QTRY_COMPARE_WITH_TIMEOUT(lines().size(), 3, 3000);
        QVERIFY(lines().at(2).contains(QStringLiteral("\"keyup\",\"RIGHT\"")));
        m_mpv->releaseKeys();
        QTest::qWait(100);
        QCOMPARE(lines().size(), 3);  // nothing held, nothing let go
        // Repeats that stop coming (a release that went astray) let it go by
        // themselves; another key's press lets a held one go first.
        commands.clear();
        m_mpv->sendKey(QStringLiteral("LEFT"), true);
        QTRY_COMPARE_WITH_TIMEOUT(lines().size(), 1, 3000);
        QVERIFY(lines().at(0).contains(QStringLiteral("\"keydown\",\"LEFT\"")));
        QTRY_COMPARE_WITH_TIMEOUT(lines().size(), 2, 3000);
        QVERIFY(lines().at(1).contains(QStringLiteral("\"keyup\",\"LEFT\"")));
        commands.clear();
        m_mpv->sendKey(QStringLiteral("VOLUME_UP"), true);
        m_mpv->sendKey(QStringLiteral("UP"));
        QTRY_COMPARE_WITH_TIMEOUT(lines().size(), 3, 3000);
        QVERIFY(lines().at(0).contains(QStringLiteral("\"keydown\",\"VOLUME_UP\"")));
        QVERIFY(lines().at(1).contains(QStringLiteral("\"keyup\",\"VOLUME_UP\"")));
        QVERIFY(lines().at(2).contains(QStringLiteral("\"keypress\",\"UP\"")));
        // Back's repeats are nobody's.
        commands.clear();
        m_mpv->sendKey(QStringLiteral("ESC"), true);
        QTest::qWait(100);
        QCOMPARE(lines().size(), 0);
    }

    // Back while the next player is on its way, its view up and its menu
    // noted: the video ends for the menu, as back during it would, where it
    // was to start. A player without a menu hears it stopped.
    void backInTheLaunchGapOpensTheMenu() {
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        play("A", 12);
        m_mpv->noteSession(menuNote("A"));
        m_mpv->sendKey(QStringLiteral("ESC"));
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 3000);
        QCOMPARE(ended.first().at(0).toInt(), 12000);
        QCOMPARE(ended.first().at(2).toString(), QStringLiteral("menu"));
        QTest::qWait(200);
        QVERIFY(!happened("start A"));
        play("B", 3);
        m_mpv->noteSession({{QStringLiteral("module"), QStringLiteral("com.osdos.youtube")},
                            {QStringLiteral("title"), QStringLiteral("B")}});
        m_mpv->sendKey(QStringLiteral("BS"));
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 2, 3000);
        QCOMPARE(ended.last().at(0).toInt(), 3000);
        QCOMPARE(ended.last().at(2).toString(), QStringLiteral("stopped"));
        QTest::qWait(200);
        QVERIFY(!happened("start B"));
    }

    // Back on a player that never acts on its quit (an mpv stuck in a
    // driver's call reads neither that nor a signal): after a moment's grace
    // it is terminated, then killed, and its player is told, where before
    // nothing more happened and the screen stayed the player's for good.
    void aFrozenPlayerIsEndedByBack() {
        qputenv("FAKE_MPV_MODE", "ignore");   // takes no SIGTERM either
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QLocalServer control;
        QVERIFY(listen(control));
        QTRY_VERIFY_WITH_TIMEOUT(control.hasPendingConnections(), 5000);
        QScopedPointer<QLocalSocket> socket(control.nextPendingConnection());
        QByteArray commands;
        connect(socket.data(), &QLocalSocket::readyRead, this, [&]() { commands += socket->readAll(); });
        m_mpv->noteSession(menuNote("A"));
        QElapsedTimer since;
        since.start();
        m_mpv->sendKey(QStringLiteral("ESC"));
        QTRY_VERIFY_WITH_TIMEOUT(commands.contains("quit"), 3000);
        QCOMPARE(ended.count(), 0);
        // The quit read by no one, the player is terminated after its
        // grace, not before...
        QTRY_VERIFY_WITH_TIMEOUT(happened("term A"), 5000);
        QVERIFY2(since.elapsed() >= 1200, qPrintable(QStringLiteral("terminated after %1 ms").arg(since.elapsed())));
        // ...and killed when the signal does nothing either: its player
        // hears the end back asked for.
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
        QCOMPARE(ended.first().at(2).toString(), QStringLiteral("menu"));
        QVERIFY(!happened("exit A"));
    }

    // No mpv to play with: its player hears the video failed, why, with how
    // to install mpv, and that it was the player's own failure, which no
    // transcode would mend.
    void aMissingMpvSaysSo() {
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        const QByteArray path = qgetenv("PATH");
        const QString empty = m_dir.path() + QStringLiteral("/no-mpv");
        QVERIFY(QDir().mkpath(empty));
        qputenv("PATH", empty.toUtf8());
        const auto restore = qScopeGuard([path] { qputenv("PATH", path); });
        play("A");
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
        QCOMPARE(ended.first().at(2).toString(), QStringLiteral("failed"));
        QCOMPARE(m_mpv->lastError(), QStringLiteral("mpv is not installed"));
        // How to install it, on this system.
        QVERIFY2(m_mpv->lastErrorDetail().contains(QLatin1String("mpv")), qPrintable(m_mpv->lastErrorDetail()));
        QVERIFY(m_mpv->playerFailed());
        // The next video asked for starts with none.
        qputenv("PATH", path);
        play("B");
        QVERIFY(m_mpv->lastError().isEmpty());
        QVERIFY(m_mpv->lastErrorDetail().isEmpty());
        QVERIFY(!m_mpv->playerFailed());
        QTRY_VERIFY_WITH_TIMEOUT(happened("start B"), 5000);
    }

    // A video mpv couldn't play: why, in mpv's own first words on it, past a
    // decoder's complaint and without the token in its address; the video's
    // failure, not the player's.
    void aVideoMpvCouldNotPlaySaysWhy() {
        qputenv("FAKE_MPV_MODE", "fail");
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        play("A");
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
        QCOMPARE(ended.first().at(2).toString(), QStringLiteral("failed"));
        QCOMPARE(m_mpv->lastError(), QStringLiteral("mpv could not play it"));
        QCOMPARE(m_mpv->lastErrorDetail(), QStringLiteral("[ffmpeg] https: HTTP error 404 Not Found"));
        QVERIFY(!m_mpv->playerFailed());
    }

    // A player that goes back without a word when its video failed has it
    // written to the log (logFailure), one line a failure: which player,
    // what it played and why, mpv's own first words on it unless the player
    // has a server's. Whatever it is handed, no token or password is
    // written, and of an address no more than its host.
    void aFailureIsLoggedInOneLine() {
        // Nothing failed yet: said so, rather than an empty reason.
        QCOMPARE(logged([this] { m_mpv->logFailure(QStringLiteral("AmbientMode Player"), QStringLiteral("Fireplace.mp4")); }),
                 QStringList{ QStringLiteral("[AmbientMode Player] Fireplace.mp4: playback failed: mpv gave no reason") });

        qputenv("FAKE_MPV_MODE", "fail");
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        play("A");
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
        QCOMPARE(logged([this] { m_mpv->logFailure(QStringLiteral("YouTube Player"), QStringLiteral("\"A Talk\" (abc123)")); }),
                 QStringList{ QStringLiteral("[YouTube Player] \"A Talk\" (abc123): playback failed: "
                                             "mpv could not play it: [ffmpeg] https: HTTP error 404 Not Found") });
        // A card's stream, named by its address: the host alone, without the
        // account's name, password and token.
        QCOMPARE(logged([this] {
                     m_mpv->logFailure(QStringLiteral("NfcReader Player"),
                                       QStringLiteral("https://ann:made-up-password@nas.example:8096/Videos/7/stream"
                                                      "?static=true&api_key=made-up-key"));
                 }),
                 QStringList{ QStringLiteral("[NfcReader Player] https://nas.example:8096: playback failed: "
                                             "mpv could not play it: [ffmpeg] https: HTTP error 404 Not Found") });
        // A server's words in place of mpv's, the address asked in them and
        // a token beside it, on two lines: one line, the host, no token.
        QCOMPARE(logged([this] {
                     m_mpv->logFailure(QStringLiteral("LivePlayer"), QStringLiteral("channel 5 News"),
                                       QStringLiteral("TUNE FAILED: Error transferring https://plex.example:32400/livetv/dvrs/1"
                                                      "/channels/5/tune?X-Plex-Token=made-up-token - server replied: Forbidden\n"
                                                      "  asked with X-Plex-Token:made-up-token"));
                 }),
                 QStringList{ QStringLiteral("[LivePlayer] channel 5 News: playback failed: TUNE FAILED: Error transferring "
                                             "https://plex.example:32400 - server replied: Forbidden; "
                                             "asked with X-Plex-Token=REDACTED") });
    }

    // An mpv that starts and never shows a picture (a hardware decoder that
    // hands no frame back): its player no longer stands on a black screen
    // for good. With no word from it, another mpv takes its place, started
    // without the hardware decoder; that one showing nothing either, the
    // video ends as failed, saying it did not start.
    void aVideoThatShowsNothingIsStartedAgainOnTheCpu() {
        qputenv("OSDOS_START_TIMEOUT_MS", "700");
        const auto restore = qScopeGuard([] { qunsetenv("OSDOS_START_TIMEOUT_MS"); });
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QVERIFY2(!arguments().contains(QStringLiteral("--hwdec=no")), qPrintable(arguments().join(QLatin1Char(' '))));
        QTRY_COMPARE_WITH_TIMEOUT(events().count(QStringLiteral("start A")), 2, 8000);
        QVERIFY2(arguments().contains(QStringLiteral("--hwdec=no")), qPrintable(arguments().join(QLatin1Char(' '))));
        // After the device's own decoders: the last word on the command line.
        QVERIFY(arguments().lastIndexOf(QStringLiteral("--hwdec=no"))
                > arguments().indexOf(QRegularExpression(QStringLiteral("^--hwdec=(?!no$).*"))));
        QVERIFY(before(QStringLiteral("term A"), QStringLiteral("exit A")));
        QVERIFY(!overlapped());
        QCOMPARE(ended.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 8000);
        QCOMPARE(ended.first().at(2).toString(), QStringLiteral("failed"));
        QCOMPARE(m_mpv->lastError(), QStringLiteral("The video did not start"));
        QVERIFY2(m_mpv->lastErrorDetail().startsWith(QStringLiteral("mpv showed no picture in ")),
                 qPrintable(m_mpv->lastErrorDetail()));
        QVERIFY(!m_mpv->playerFailed());
        QCOMPARE(events().count(QStringLiteral("start A")), 2);
        // Tried again by its player (RETRY), it gets the hardware decoder
        // again: the next one may answer.
        play("A");
        QTRY_COMPARE_WITH_TIMEOUT(events().count(QStringLiteral("start A")), 3, 5000);
        QVERIFY(!arguments().contains(QStringLiteral("--hwdec=no")));
        QVERIFY(m_mpv->lastError().isEmpty());
    }

    // The same mpv, with its control connection made: told to decode on the
    // CPU where it is first, which an mpv that still listens does as it
    // plays. Playing after that, it is left alone.
    void aVideoThatShowsNothingIsToldToDecodeOnTheCpuFirst() {
        qputenv("OSDOS_START_TIMEOUT_MS", "700");
        const auto restore = qScopeGuard([] { qunsetenv("OSDOS_START_TIMEOUT_MS"); });
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QLocalServer control;
        QVERIFY(listen(control));
        QTRY_VERIFY_WITH_TIMEOUT(control.hasPendingConnections(), 5000);
        QScopedPointer<QLocalSocket> socket(control.nextPendingConnection());
        QByteArray commands;
        connect(socket.data(), &QLocalSocket::readyRead, this, [&]() { commands += socket->readAll(); });
        QTRY_VERIFY_WITH_TIMEOUT(commands.contains("\"core-idle\""), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(commands.contains("[\"set_property\",\"hwdec\",\"no\"]"), 5000);
        QCOMPARE(events().count(QStringLiteral("start A")), 1);
        socket->write("{\"event\":\"property-change\",\"id\":5,\"name\":\"core-idle\",\"data\":false}\n");
        socket->flush();
        QTest::qWait(1800);
        QCOMPARE(events().count(QStringLiteral("start A")), 1);
        QVERIFY(!happened("term A"));
        QCOMPARE(ended.count(), 0);
    }

    // A video that plays is left alone: mpv's own word that its picture is
    // set up counts, whatever becomes of the control connection.
    void aVideoThatPlaysIsLeftAlone() {
        qputenv("OSDOS_START_TIMEOUT_MS", "700");
        qputenv("FAKE_MPV_MODE", "plays");
        const auto restore = qScopeGuard([] { qunsetenv("OSDOS_START_TIMEOUT_MS"); });
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QTest::qWait(2200);
        QCOMPARE(events().count(QStringLiteral("start A")), 1);
        QVERIFY(!happened("term A"));
        QCOMPARE(ended.count(), 0);
    }

    // A video whose sound card couldn't be opened plays without sound, and
    // mpv calls the file failed as it ends (exit code 2). It played: its
    // player hears it ended, with no failure to show.
    void aVideoWithoutSoundEndsAsItPlayed() {
        qputenv("FAKE_MPV_MODE", "noaudio");
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QLocalServer control;
        QVERIFY(listen(control));
        QTRY_VERIFY_WITH_TIMEOUT(control.hasPendingConnections(), 5000);
        QScopedPointer<QLocalSocket> socket(control.nextPendingConnection());
        socket->write("{\"event\":\"property-change\",\"id\":2,\"name\":\"duration\",\"data\":6.0}\n"
                      "{\"event\":\"property-change\",\"id\":1,\"name\":\"time-pos\",\"data\":5.96}\n"
                      "{\"event\":\"end-file\",\"reason\":\"error\","
                      "\"file_error\":\"audio output initialization failed\"}\n");
        socket->flush();
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 8000);
        QCOMPARE(ended.first().at(2).toString(), QStringLiteral("eof"));
        QVERIFY(m_mpv->lastError().isEmpty());
    }

    // A video its menu's Browse left (an mpv process having ended for the
    // menu): the main menu offers it back until another video is asked for.
    void aLeftVideoIsOfferedUntilTheNext() {
        QSignalSpy left(m_mpv, &MpvController::leftNoteChanged);
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        const QVariantMap note{{QStringLiteral("module"), QStringLiteral("com.osdos.local_files")},
                               {QStringLiteral("title"), QStringLiteral("A")},
                               {QStringLiteral("menu"), true}};
        m_mpv->noteSession(note);
        QVERIFY(m_mpv->leftNote().isEmpty());
        m_mpv->leaveSession();
        QCOMPARE(m_mpv->leftNote(), note);
        m_mpv->leaveSession();
        QCOMPARE(left.count(), 1);
        play("B");
        QVERIFY(m_mpv->leftNote().isEmpty());
        QCOMPARE(left.count(), 2);
        // B noted nothing, so leaving it leaves no row.
        m_mpv->leaveSession();
        QVERIFY(m_mpv->leftNote().isEmpty());
        QCOMPARE(left.count(), 2);
    }
};

int main(int argc, char **argv) {
    // No display is needed: nothing is drawn.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    PlaybackRetireTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "playback_retire_test.moc"
