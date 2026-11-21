// What happens when a video is asked for while another still plays in an mpv
// process (an NFC card tapped mid-film, say): MpvController against a
// stand-in for mpv, a shell script first on PATH that writes down when it
// starts, is told to quit (SIGTERM) and exits, and whether an earlier one was
// still alive as it started. And what becomes of a video its menu's Browse
// left as the next is asked for; how a tube's shape, whose pixels aren't
// square, reaches mpv; what of an address mpv prints reaches the log and
// the Playback failed notice: none of its sign-in, nor a server's token; and
// what of a session reaches mpv's command line, which any account can read:
// no token either, which goes in files of the user's own.
#include "player/MpvController.h"
#include "player/EmbeddedMpv.h"
#include "player/VideoSurface.h"
#include "audio/MenuMusic.h"
#include "util/DisplayHandoff.h"
#include "util/RedactSecrets.h"
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
#include <QMutex>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>

static const char kStandIn[] = R"(#!/bin/sh
log="$FAKE_MPV_LOG"
media=""
for a in "$@"; do
  case "$a" in
    --playlist=*) media=$(grep -v '^#' "${a#--playlist=}" | tail -n 1) ;;
    --*) ;;
    *) media="$a" ;;
  esac
done
address="$media"
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
  printf 'Failed to open %s.\n' "$address"
  echo 'Exiting... (Errors when loading file)'
  exit 2
fi
if [ "$FAKE_MPV_MODE" = noprotocol ]; then
  echo "[ffmpeg] Protocol not found. Make sure ffmpeg/Libav is compiled with networking support."
  printf 'Failed to open %s.\n' "$address"
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

// What the app logs while this is there, as its log file and the journal get
// it, from whatever thread; the test's own log gets it too.
class LogCapture {
public:
    LogCapture() {
        QMutexLocker lock(&s_mutex);
        s_lines = &m_lines;
        s_previous = qInstallMessageHandler(handle);
    }
    ~LogCapture() {
        QMutexLocker lock(&s_mutex);
        qInstallMessageHandler(s_previous);
        s_lines = nullptr;
    }
    QString text() const {
        QMutexLocker lock(&s_mutex);
        return m_lines.join(QLatin1Char('\n'));
    }

private:
    static void handle(QtMsgType type, const QMessageLogContext &context, const QString &message) {
        QtMessageHandler previous = nullptr;
        {
            QMutexLocker lock(&s_mutex);
            if (s_lines)
                s_lines->append(message);
            previous = s_previous;
        }
        if (previous)
            previous(type, context, message);
    }
    QStringList m_lines;
    static inline QMutex s_mutex;
    static inline QStringList *s_lines = nullptr;
    static inline QtMessageHandler s_previous = nullptr;
};

// A stand-in for a media server, on this machine: it writes down each
// request it is asked, headers and all, and answers a small picture for a
// stream (/Videos/<id>/stream) and a line of subtitles for anything else.
class ServerStandIn : public QTcpServer {
public:
    ServerStandIn() {
        QImage picture(16, 16, QImage::Format_RGB32);
        picture.fill(Qt::gray);
        QBuffer buffer(&m_picture);
        buffer.open(QIODevice::WriteOnly);
        picture.save(&buffer, "PNG");
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() { read(socket); });
            }
        });
    }
    QString address() const { return QStringLiteral("http://127.0.0.1:%1").arg(serverPort()); }
    QList<QByteArray> asked;

private:
    void read(QTcpSocket *socket) {
        const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n") || socket->property("answered").toBool())
            return;
        socket->setProperty("answered", true);
        asked << request;
        const bool stream = request.contains("/stream");
        const QByteArray body = stream ? m_picture : QByteArray("1\n00:00:00,000 --> 00:00:01,000\nline\n");
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + QByteArray(stream ? "image/png" : "text/plain")
                      + "\r\nContent-Length: " + QByteArray::number(body.size())
                      + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }
    QByteArray m_picture;
};

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
    // The files a process is handed what must not be on its command line in.
    QString privateOptions() const { return runDir() + QStringLiteral("/osdos-mpv-private.conf"); }
    QString privateMedia() const { return runDir() + QStringLiteral("/osdos-mpv-private.m3u"); }
    static QByteArray contents(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    // Its owner's alone to read and write (0600).
    static bool ownersAlone(const QString &path) {
        struct stat info;
        return ::stat(QFile::encodeName(path).constData(), &info) == 0 && (info.st_mode & 0777) == 0600;
    }
    // A line of an mpv config file, its value quoted by its length.
    static QByteArray optionLine(const char *option, const QByteArray &value) {
        return QByteArray(option) + "=%" + QByteArray::number(value.size()) + '%' + value + '\n';
    }
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

    // A media server's token never reaches mpv's command line, which any
    // account on the machine can read while the video plays (ps,
    // /proc/<pid>/cmdline): Plex's and Jellyfin's headers go in a config file
    // mpv is given with --include, and so do the subtitle files of a server
    // whose addresses hold the token (Emby's api_key), in their order, whose
    // stream goes in a playlist given with --playlist. Both files are in the
    // run-time folder, their owner's alone, and gone once the session is
    // over. What holds no secret stays on the command line as it was.
    void aServersTokenIsNotOnTheCommandLine() {
        const auto commandLine = [this] { return arguments().join(QLatin1Char('\n')); };
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);

        // Plex: its token is a header, its addresses hold none.
        const QString plexSub = QStringLiteral("https://plex.example:32400/library/streams/9");
        m_mpv->loadAndPlay(QStringLiteral("https://plex.example:32400/library/parts/7/film.mp4"), 0, 1, 0, { plexSub },
                           {}, false, -1, 0, QStringLiteral("made-up-plex-token"));
        QTRY_VERIFY_WITH_TIMEOUT(happened("start film"), 5000);
        QVERIFY2(!commandLine().contains(QLatin1String("made-up")), qPrintable(commandLine()));
        QVERIFY2(!commandLine().contains(QLatin1String("http-header-fields")), qPrintable(commandLine()));
        QVERIFY(arguments().contains(QStringLiteral("--include=") + privateOptions()));
        QVERIFY(arguments().contains(QStringLiteral("--sub-file=") + plexSub));
        QCOMPARE(arguments().last(), QStringLiteral("https://plex.example:32400/library/parts/7/film.mp4"));
        QCOMPARE(contents(privateOptions()),
                 optionLine("http-header-fields-append", "X-Plex-Token:made-up-plex-token"));
        QVERIFY(ownersAlone(privateOptions()));
        QVERIFY(!QFile::exists(privateMedia()));
        // Nothing of it in the temp folder.
        QVERIFY(QDir(m_dir.path()).entryList({ QStringLiteral("osdos-*") }, QDir::AllEntries | QDir::System).isEmpty());
        // Over, its file is gone.
        m_mpv->stop();
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
        QVERIFY(!QFile::exists(privateOptions()));

        // Jellyfin: the same, its header with quotes of its own.
        m_mpv->loadAndPlay(QStringLiteral("https://jellyfin.example/Videos/7/stream.mp4"), 0, 1, -2, {}, {}, false, -1, 0,
                           QString(), false, QString(), false, {}, 0, false, {}, QStringLiteral("made-up-jellyfin-token"));
        QTRY_VERIFY_WITH_TIMEOUT(happened("start stream"), 5000);
        QVERIFY2(!commandLine().contains(QLatin1String("made-up")), qPrintable(commandLine()));
        QCOMPARE(contents(privateOptions()),
                 optionLine("http-header-fields-append", "Authorization:MediaBrowser Token=\"made-up-jellyfin-token\""));
        QVERIFY(ownersAlone(privateOptions()));

        // Emby: the token is in its stream's address and in its subtitles'
        // too. The session before is replaced, and its file with it.
        const QString embyStream = QStringLiteral("http://emby.example:8096/Videos/7/stream?static=true&mediaSourceId=7"
                                                  "&api_key=made-up-emby-key");
        const QString embySubs[] = { QStringLiteral("http://emby.example:8096/Videos/7/7/Subtitles/3/Stream.srt"
                                                    "?api_key=made-up-emby-key"),
                                     QStringLiteral("http://emby.example:8096/Videos/7/7/Subtitles/2/Stream.ass"
                                                    "?api_key=made-up-emby-key") };
        m_mpv->loadAndPlay(embyStream, 90, 1, 0, { embySubs[0], embySubs[1] }, {}, false, -1, 0, QString(), false,
                           QString(), false, {}, 0, false, {}, QStringLiteral("made-up-emby-key"));
        QTRY_VERIFY_WITH_TIMEOUT(happened("start stream?static=true&mediaSourceId=7&api_key=made-up-emby-key"), 5000);
        QVERIFY2(!commandLine().contains(QLatin1String("made-up")), qPrintable(commandLine()));
        QVERIFY2(!commandLine().contains(QLatin1String("emby.example")), qPrintable(commandLine()));
        QVERIFY(arguments().contains(QStringLiteral("--include=") + privateOptions()));
        QVERIFY(arguments().contains(QStringLiteral("--playlist=") + privateMedia()));
        QVERIFY(arguments().contains(QStringLiteral("--start=90.000")));
        QVERIFY(!arguments().contains(QStringLiteral("--")));
        QCOMPARE(contents(privateOptions()),
                 optionLine("sub-files-append", embySubs[0].toUtf8())
                     + optionLine("sub-files-append", embySubs[1].toUtf8())
                     + optionLine("http-header-fields-append", "Authorization:MediaBrowser Token=\"made-up-emby-key\""));
        QCOMPARE(contents(privateMedia()), QByteArray("#EXTM3U\n") + embyStream.toUtf8() + '\n');
        QVERIFY(ownersAlone(privateOptions()));
        QVERIFY(ownersAlone(privateMedia()));
        QVERIFY(QDir(m_dir.path()).entryList({ QStringLiteral("osdos-*") }, QDir::AllEntries | QDir::System).isEmpty());
        m_mpv->stop();
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 2, 5000);
        QVERIFY(!QFile::exists(privateOptions()));
        QVERIFY(!QFile::exists(privateMedia()));

        // A video with nothing to hide is handed no file.
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QVERIFY2(!commandLine().contains(QLatin1String("--include=")), qPrintable(commandLine()));
        QVERIFY2(!commandLine().contains(QLatin1String("--playlist=")), qPrintable(commandLine()));
        QVERIFY(!QFile::exists(privateOptions()));
        QVERIFY(!QFile::exists(privateMedia()));
    }

    // With no file to put the token in (a folder has its name), nothing is
    // started with it on the command line after all: the session ends as
    // one mpv could not be started for, the player's own failure.
    void withoutAFileForItATokenIsNotHandedOver() {
        QVERIFY(QDir().mkpath(privateOptions() + QStringLiteral("/held")));
        const auto free = qScopeGuard([this] { QDir(privateOptions()).removeRecursively(); });
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^\\[AtomicFile\\] Could not write ")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(
                                               "^\\[MpvController\\] Cannot start playback: what mpv is handed in a file")));
        m_mpv->loadAndPlay(QStringLiteral("https://plex.example:32400/library/parts/7/film.mp4"), 0, 1, 0, {}, {}, false,
                           -1, 0, QStringLiteral("made-up-plex-token"));
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
        QCOMPARE(ended.first().at(2).toString(), QStringLiteral("failed"));
        QCOMPARE(m_mpv->lastError(), QStringLiteral("mpv could not be started"));
        QVERIFY2(m_mpv->lastErrorDetail().contains(runDir()), qPrintable(m_mpv->lastErrorDetail()));
        QVERIFY(m_mpv->playerFailed());
        QVERIFY(!happened("start film"));
    }

    // The files as the controller writes them, read by the mpv that is
    // installed (skipped without one): asked of a stand-in for a server, the
    // stream is asked for by the address in the playlist, its token in it,
    // and it and each subtitle file, in their order, with the header.
    void theInstalledMpvReadsTheFiles() {
        QStringList folders = qEnvironmentVariable("PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);
        folders.removeAll(m_dir.path() + QStringLiteral("/bin"));
        const QString mpv = QStandardPaths::findExecutable(QStringLiteral("mpv"), folders);
        if (mpv.isEmpty())
            QSKIP("mpv isn't installed here");

        ServerStandIn server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QString stream = server.address() + QStringLiteral("/Videos/7/stream?static=true&api_key=made-up-emby-key");
        const QString subs[] = {
            server.address() + QStringLiteral("/Videos/7/7/Subtitles/3/Stream.srt?api_key=made-up-emby-key"),
            server.address() + QStringLiteral("/Videos/7/7/Subtitles/2/Stream.srt?api_key=made-up-emby-key") };
        m_mpv->loadAndPlay(stream, 0, 1, 0, { subs[0], subs[1] }, {}, false, -1, 0, QString(), false, QString(), false,
                           {}, 0, false, {}, QStringLiteral("made-up-emby-key"));
        QTRY_VERIFY_WITH_TIMEOUT(happened("start stream?static=true&api_key=made-up-emby-key"), 5000);
        QVERIFY(QFile::exists(privateOptions()));
        QVERIFY(QFile::exists(privateMedia()));

        QProcess real;
        real.setProcessChannelMode(QProcess::MergedChannels);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        // Straight to the stand-in, whatever proxy the machine has.
        for (const char *name : { "http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY", "all_proxy", "ALL_PROXY" })
            env.remove(QLatin1String(name));
        real.setProcessEnvironment(env);
        QByteArray said;
        connect(&real, &QProcess::readyRead, this, [&]() { said += real.readAll(); });
        real.start(mpv, { QStringLiteral("--no-config"), QStringLiteral("--vo=null"), QStringLiteral("--ao=null"),
                          QStringLiteral("--no-input-terminal"), QStringLiteral("--image-display-duration=0.2"),
                          QStringLiteral("--include=") + privateOptions(),
                          QStringLiteral("--playlist=") + privateMedia() });
        QVERIFY2(real.waitForStarted(5000), qPrintable(real.errorString()));
        // What it asked for, each address once, in the order it first did.
        const auto paths = [&server]() {
            QList<QByteArray> seen;
            for (const QByteArray &request : server.asked) {
                const QByteArray path = request.mid(4, request.indexOf(' ', 4) - 4);
                if (!seen.contains(path))
                    seen << path;
            }
            return seen;
        };
        QTRY_VERIFY2_WITH_TIMEOUT(paths().size() >= 3 || real.state() == QProcess::NotRunning, said.constData(), 20000);
        real.kill();
        real.waitForFinished(5000);
        const qsizetype host = server.address().size();
        QCOMPARE(paths(), (QList<QByteArray>{ stream.mid(host).toUtf8(), subs[0].mid(host).toUtf8(),
                                              subs[1].mid(host).toUtf8() }));
        for (const QByteArray &request : std::as_const(server.asked))
            QVERIFY2(request.contains("\r\nAuthorization:MediaBrowser Token=\"made-up-emby-key\"\r\n"),
                     request.constData());
    }

    // Inside the window (Transparent Background) a session has no command
    // line: libmpv is given the header as it is, which the server is asked
    // with, and no file is written for it.
    void aVideoInsideTheWindowIsHandedItsTokenAsItIs() {
        if (!EmbeddedMpv::available())
            QSKIP("libmpv isn't installed here");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(data.path(), data.path());
        useEmbedded(core, data.path());
        const auto end = qScopeGuard([this] { delete m_mpv; m_mpv = nullptr; });
        ServerStandIn server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        m_mpv->loadAndPlay(server.address() + QStringLiteral("/Videos/7/stream?static=true&api_key=made-up-emby-key"),
                           0, -1, -2, {}, {}, false, -1, 0, QStringLiteral("made-up-plex-token"), true, QString(), false,
                           {}, 0, false, {}, QStringLiteral("made-up-emby-key"));
        QTRY_VERIFY_WITH_TIMEOUT(!server.asked.isEmpty(), 15000);
        const QByteArray request = server.asked.first();
        QVERIFY2(request.startsWith("GET /Videos/7/stream?static=true&api_key=made-up-emby-key HTTP/"),
                 request.constData());
        QVERIFY2(request.contains("\r\nX-Plex-Token:made-up-plex-token\r\n"), request.constData());
        QVERIFY2(request.contains("\r\nAuthorization:MediaBrowser Token=\"made-up-emby-key\"\r\n"), request.constData());
        QVERIFY(!QFile::exists(privateOptions()));
        QVERIFY(!QFile::exists(privateMedia()));
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
        // Nothing playing: back is the views'. A session on its way, and an
        // mpv process playing: the controller's (takesBack()).
        QVERIFY(!m_mpv->takesBack());
        play("A");
        QVERIFY(m_mpv->takesBack());
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QVERIFY(m_mpv->takesBack());
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
        // Ended, with its player's menu up: back is the menu's.
        QTRY_VERIFY_WITH_TIMEOUT(!m_mpv->takesBack(), 3000);
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
        // In front of the menus back is the controller's; behind them, the
        // menus' (takesBack()).
        QVERIFY(m_mpv->takesBack());
        m_mpv->sendKey(QStringLiteral("ESC"));
        // Before any event is handled.
        QVERIFY(m_mpv->background());
        QVERIFY(!m_mpv->takesBack());
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

    // Transparent Background: a video whose sound card can't be opened (none
    // on the machine, as on these tests' runners) plays without sound, and
    // mpv calls the whole file an error as it ends. Its player is told the
    // end it had, never that it failed, whether or not the word of it came
    // over the control connection before the core went.
    void anEmbeddedVideoWithoutSoundEndsAsItPlayed() {
        if (!EmbeddedMpv::available())
            QSKIP("libmpv isn't installed here");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(data.path(), data.path());
        QVERIFY(core.save_setting(QString(), QStringLiteral("transparent_background"), QStringLiteral("On")));
        delete m_mpv;
        m_mpv = new MpvController(m_dir.path(), data.path(), &core);
        const auto end = qScopeGuard([this] { delete m_mpv; m_mpv = nullptr; });
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        // A second of picture and of sound that mpv makes itself.
        m_mpv->loadAndPlay(QStringLiteral("av://lavfi:testsrc=size=64x48:rate=10:duration=1[out0];sine=duration=1[out1]"),
                           0, -1, -1, {}, {}, false, -1, 0, QString(), true);
        QTRY_VERIFY_WITH_TIMEOUT(m_mpv->videoActive(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 20000);
        QVERIFY2(ended.first().at(2).toString() != QLatin1String("failed"), "a video that played was called failed");
        QVERIFY(m_mpv->lastError().isEmpty());
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

    // Transparent Background: a video behind the menus whose screen saver is
    // up, chosen again, carries on with the saver over it; back then goes to
    // the saver, as it did before, and the video stays in front.
    void aVideoChosenAgainKeepsItsScreenSaver() {
        if (!EmbeddedMpv::available())
            QSKIP("libmpv isn't installed here");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(data.path(), data.path());
        useEmbedded(core, data.path());
        const auto end = qScopeGuard([this] { delete m_mpv; m_mpv = nullptr; });
        playPicture("color=c=gray:s=64x48:r=10");
        QTRY_VERIFY_WITH_TIMEOUT(!m_mpv->videoFrame().isNull(), 10000);
        m_mpv->sendKey(QStringLiteral("ESC"));
        QVERIFY(m_mpv->background());
        // The saver says it is up, as mpv-screensaver.lua does, to every
        // client of mpv's socket, the controller's among them once it has
        // said what it had to as the video went behind the menus.
        QLocalSocket probe;
        QTRY_VERIFY_WITH_TIMEOUT((probe.state() == QLocalSocket::ConnectedState
                                  || (probe.connectToServer(socketPath()), probe.waitForConnected(500))), 10000);
        QByteArray heard;
        connect(&probe, &QLocalSocket::readyRead, this, [&]() { heard += probe.readAll(); });
        QTRY_VERIFY_WITH_TIMEOUT(heard.contains("osdos-osd-menu-hide"), 10000);
        probe.write("{\"command\":[\"script-message\",\"osdos-screensaver\",\"shown\"]}\n");
        probe.flush();
        QTRY_VERIFY_WITH_TIMEOUT(heard.contains("\"osdos-screensaver\",\"shown\""), 5000);
        QTest::qWait(200);
        playPicture("color=c=gray:s=64x48:r=10");
        QVERIFY(!m_mpv->background());
        m_mpv->sendKey(QStringLiteral("ESC"));
        QVERIFY(!m_mpv->background());
    }

    // A Scaling chosen in a player's menu reaches the video under it as a
    // session starts with it: Default is Settings', and Settings' unset is
    // the old Auto Crop's (ON reading as Pan & Scan), as for the session's
    // command line. The menu used to make Default Letterbox there.
    void aPlayersMenuScalingIsTheSessions() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(data.path(), data.path());
        QVERIFY(core.save_setting(QString(), QStringLiteral("auto_crop"), QStringLiteral("On")));
        const QString module = QStringLiteral("com.osdos.local_files");
        QVERIFY(core.save_setting(module, QStringLiteral("video_scaling"), QStringLiteral("Default")));
        delete m_mpv;
        m_mpv = new MpvController(m_dir.path(), data.path(), &core);
        const auto end = qScopeGuard([this] { delete m_mpv; m_mpv = nullptr; });
        m_mpv->setActiveModule(module);
        // An mpv process: on its command line.
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QVERIFY2(arguments().contains(QStringLiteral("--panscan=1")), qPrintable(arguments().join(QLatin1Char(' '))));
        m_mpv->stop();
        if (!EmbeddedMpv::available())
            QSKIP("libmpv isn't installed here");

        // Inside the window, its player's menu open over it.
        useEmbedded(core, data.path());
        m_mpv->setActiveModule(module);
        QSignalSpy menu(m_mpv, &MpvController::playerMenuRequested);
        playPicture("color=c=gray:s=64x48:r=10");
        QTRY_VERIFY_WITH_TIMEOUT(!m_mpv->videoFrame().isNull(), 10000);
        m_mpv->noteSession(menuNote("gray"));
        m_mpv->sendKey(QStringLiteral("ESC"));
        QCOMPARE(menu.count(), 1);

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
        // Chosen (again, until the controller's own connection is up), and
        // what mpv has then.
        auto chosen = [this, &property](const char *scaling, const char *name) {
            m_mpv->applyScaling(QLatin1String(scaling));
            return property(name);
        };
        QTRY_COMPARE_WITH_TIMEOUT(property("panscan").toDouble(), 1.0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(chosen("Letterbox", "panscan").toDouble(), 0.0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(chosen("Default", "panscan").toDouble(), 1.0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(chosen("14:9", "panscan").toDouble(), 0.43, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(chosen("Anamorphic", "keepaspect").toBool(), false, 5000);
        QCOMPARE(property("panscan").toDouble(), 0.0);
        // Settings' own, once there is one.
        QVERIFY(core.save_setting(QString(), QStringLiteral("video_scaling"), QStringLiteral("14:9")));
        QTRY_COMPARE_WITH_TIMEOUT(chosen("Default", "panscan").toDouble(), 0.43, 5000);
        QCOMPARE(property("keepaspect").toBool(), true);
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

    // A sign-in in an address (scheme://user:password@host, or in its query:
    // ?username=...&password=..., as a card's tag file, an .m3u or a playlist
    // may give one): mpv prints the address it could not open as it was
    // given, and neither the log nor the Playback failed notice gets the
    // sign-in, the host and the rest of the query kept. In the notice where the
    // address is mpv's first word on the failure, and cut to the notice's
    // length only once the sign-in is out of it: a password that long (a
    // token used as one) was cut before its "@", and so shown.
    void aSignInInAnAddressReachesNeitherTheLogNorTheNotice_data() {
        QTest::addColumn<QByteArray>("mode");
        QTest::addColumn<QString>("address");
        QTest::addColumn<QString>("logged");
        QTest::addColumn<QString>("detail");
        QTest::newRow("a server's stream, with its token")
            << QByteArray("fail")
            << QStringLiteral("https://ann:made-up-password@nas.example:8096/Videos/7/stream?static=true&ApiKey=made-up-key")
            << QStringLiteral("Failed to open https://REDACTED@nas.example:8096/Videos/7/stream?static=true&ApiKey=REDACTED")
            << QStringLiteral("[ffmpeg] https: HTTP error 404 Not Found");
        QTest::newRow("an address mpv has no protocol for")
            << QByteArray("noprotocol") << QStringLiteral("smb://ann:made-up-password@nas.example/films/a.mkv")
            << QStringLiteral("Failed to open smb://REDACTED@nas.example/films/a.mkv.")
            << QStringLiteral("Failed to open smb://REDACTED@nas.example/films/a.mkv.");
        QTest::newRow("a password longer than the notice")
            << QByteArray("noprotocol")
            << QStringLiteral("smb://ann:made-up-password-") + QString(200, QLatin1Char('x'))
                   + QStringLiteral("@nas.example/films/a.mkv")
            << QStringLiteral("Failed to open smb://REDACTED@nas.example/films/a.mkv.")
            << QStringLiteral("Failed to open smb://REDACTED@nas.example/films/a.mkv.");
        // The sign-in in the query, as a television provider's addresses
        // have it.
        QTest::newRow("a name and a password in the query")
            << QByteArray("noprotocol")
            << QStringLiteral("http://tv.example:8080/get.php?username=made-up-name&password=made-up-password&type=m3u_plus")
            << QStringLiteral("Failed to open http://tv.example:8080/get.php?username=REDACTED&password=REDACTED&type=m3u_plus.")
            << QStringLiteral("Failed to open http://tv.example:8080/get.php?username=REDACTED&password=REDACTED&type=m3u_plus.");
    }
    void aSignInInAnAddressReachesNeitherTheLogNorTheNotice() {
        QFETCH(QByteArray, mode);
        QFETCH(QString, address);
        QFETCH(QString, logged);
        QFETCH(QString, detail);
        qputenv("FAKE_MPV_MODE", mode);
        LogCapture log;
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        m_mpv->loadAndPlay(address, 0, -1, -1);
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
        QCOMPARE(ended.first().at(2).toString(), QStringLiteral("failed"));
        // The line mpv printed, among the "[mpv]" ones.
        const QString text = log.text();
        const qsizetype mpvSaid = text.indexOf(QLatin1String("[mpv] "));
        QVERIFY2(mpvSaid >= 0, qPrintable(text));
        QVERIFY2(text.mid(mpvSaid).contains(logged), qPrintable(text));
        // Nowhere in the log, the launch line among it, nor in the notice.
        for (const char *secret : {"made-up-password", "ann:", "made-up-key", "made-up-name"}) {
            QVERIFY2(!text.contains(QLatin1String(secret)), qPrintable(text));
            QVERIFY2(!m_mpv->lastErrorDetail().contains(QLatin1String(secret)), qPrintable(m_mpv->lastErrorDetail()));
        }
        QCOMPARE(m_mpv->lastErrorDetail(), detail);
    }

    // What is blanked of an address and what is left (redactSecrets()): the
    // sign-in before the host in each way one is written, and the one in
    // the query, the host and the rest kept; and nothing of an address
    // without one, whose "@" is in its path or its query, or whose keys only
    // look like a sign-in's. The tokens as before.
    void anAddressLosesItsSignInAndKeepsItsHost_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("blanked");
        auto row = [](const char *name, const char *text, const char *blanked) {
            QTest::newRow(name) << QString::fromUtf8(text) << QString::fromUtf8(blanked);
        };
        auto kept = [&row](const char *name, const char *text) { row(name, text, text); };
        row("a name and a password", "Failed to open https://ann:made-up@nas.example:8096/Videos/7/stream.",
            "Failed to open https://REDACTED@nas.example:8096/Videos/7/stream.");
        row("a name alone, a token perhaps", "rtsp://made-up-token@cam.example/live",
            "rtsp://REDACTED@cam.example/live");
        row("a name and no password", "ftp://ann:@nas.example/a.mkv", "ftp://REDACTED@nas.example/a.mkv");
        row("a password of digits", "ftp://ann:5550100@nas.example:21/a.mkv", "ftp://REDACTED@nas.example:21/a.mkv");
        row("percent-encoded", "https://ann%40home:made%2Fup@nas.example/a.mkv", "https://REDACTED@nas.example/a.mkv");
        // A password's own "@", "/", "?" or "#" written as it is (the last
        // three make an address no player can open, which mpv then prints):
        // none of the password is left.
        row("an @ in the password", "https://ann:made@up@nas.example/a.mkv", "https://REDACTED@nas.example/a.mkv");
        row("a / in the password", "https://ann:made/up@nas.example/a.mkv", "https://REDACTED@nas.example/a.mkv");
        row("an @ and a / in the password", "https://ann:ma@de/up@nas.example/a.mkv",
            "https://REDACTED@nas.example/a.mkv");
        row("a ? and a # in the password", "https://ann:made?up#1@nas.example/a.mkv",
            "https://REDACTED@nas.example/a.mkv");
        row("an IPv6 host", "http://ann:made-up@[fe80::1]:8096/a.mkv", "http://REDACTED@[fe80::1]:8096/a.mkv");
        row("in quotes", "Opening 'https://ann:made-up@nas.example/a.ts' for reading",
            "Opening 'https://REDACTED@nas.example/a.ts' for reading");
        row("two on a line", "--sub-file=https://ann:made-up@nas.example/a.srt -- smb://bob:made-up-too@nas.example/a.mkv",
            "--sub-file=https://REDACTED@nas.example/a.srt -- smb://REDACTED@nas.example/a.mkv");
        row("a name alone, an @ in the query", "https://made-up-token@nas.example/a?mail=me@home.example",
            "https://REDACTED@nas.example/a?mail=me@home.example");
        // The price of a password's own "@" or "/" never showing: with a
        // password, an "@" further on in the address takes the host with it.
        row("a password, and an @ further on", "https://ann:made-up@nas.example/a?mail=me@home.example",
            "https://REDACTED@home.example");
        kept("blanked already", "https://REDACTED@nas.example:8096/a.mkv");
        // In the query: the value of a key that is, or ends in, "password",
        // "passwd", "pwd" or "token", or is "pass", "pw", "username", "user"
        // or "login", in any case, up to the next "&".
        row("a name and a password in the query",
            "http://tv.example:8080/get.php?username=made-up&password=made-up&type=m3u_plus",
            "http://tv.example:8080/get.php?username=REDACTED&password=REDACTED&type=m3u_plus");
        row("the short keys, the last in the query", "Failed to open http://tv.example/live.m3u8?user=made-up&pass=made-up.",
            "Failed to open http://tv.example/live.m3u8?user=REDACTED&pass=REDACTED");
        row("the other keys for them", "http://tv.example/a?login=made-up&passwd=made-up&pwd=made-up&pw=made-up&x=1",
            "http://tv.example/a?login=REDACTED&passwd=REDACTED&pwd=REDACTED&pw=REDACTED&x=1");
        row("in capitals, the key kept as written", "http://tv.example/a?USERNAME=made-up&Password=made-up",
            "http://tv.example/a?USERNAME=REDACTED&Password=REDACTED");
        row("a key ending in it", "http://tv.example/a?stream_password=made-up&userPwd=made-up&id=7",
            "http://tv.example/a?stream_password=REDACTED&userPwd=REDACTED&id=7");
        row("a token in the query", "http://tv.example/a.m3u8?token=made-up&access_token=made-up&x=1",
            "http://tv.example/a.m3u8?token=REDACTED&access_token=REDACTED&x=1");
        row("a password with a # and a ; of its own", "http://tv.example/a?password=made#up;made-up&x=1",
            "http://tv.example/a?password=REDACTED&x=1");
        row("a mail address for a name, behind a port", "http://tv.example:8080/a?username=made-up@home.example&password=made@up",
            "http://tv.example:8080/a?username=REDACTED&password=REDACTED");
        row("a sign-in before the host and one in the query", "http://ann:made-up@tv.example/a?password=made-up",
            "http://REDACTED@tv.example/a?password=REDACTED");
        kept("an empty name and password", "http://tv.example/get.php?username=&password=&type=m3u");
        kept("keys that only look like them",
             "http://tv.example/a?bypass=1&compass=n&userid=7&passes=2&password_hint=x&page=2&q=password");
        kept("no sign-in", "https://nas.example:8096/Videos/7/stream?static=true");
        kept("no sign-in, an @ in the path", "https://www.youtube.com/@channel/videos");
        kept("no sign-in, a port, an @ in the query", "http://nas.example:8096/Items?searchTerm=a@b");
        kept("no sign-in, an IPv6 host, an @ in the path", "http://[fe80::1]:8096/Users/@me");
        kept("a file's address", "file:///media/films/a@b.mkv");
        kept("a file's name", "Failed to open /media/films/a@b: c.mkv.");
        kept("a picture mpv makes", "av://lavfi:color=c=gray:s=64x48:r=10");
        row("Jellyfin's key", "https://nas.example/Videos/7/stream?static=true&ApiKey=made-up&x=1",
            "https://nas.example/Videos/7/stream?static=true&ApiKey=REDACTED&x=1");
        row("Emby's key", "https://nas.example/Videos/7/stream?api_key=made-up",
            "https://nas.example/Videos/7/stream?ApiKey=REDACTED");
        row("Plex's token", "--http-header-fields=X-Plex-Token:made-up https://nas.example:32400/a?X-Plex-Token=made-up",
            "--http-header-fields=X-Plex-Token=REDACTED https://nas.example:32400/a?X-Plex-Token=REDACTED");
        row("Jellyfin's header", "--http-header-fields=Authorization:MediaBrowser Token=\"made-up\"",
            "--http-header-fields=Authorization:MediaBrowser Token=\"REDACTED\"");
    }
    void anAddressLosesItsSignInAndKeepsItsHost() {
        QFETCH(QString, text);
        QFETCH(QString, blanked);
        QCOMPARE(redactSecrets(text), blanked);
    }

    // The same of a video played inside the window (Transparent Background),
    // whose messages mpv hands the app one by one: a playlist's address it
    // could not open, as it goes on to the next, without its sign-in and
    // without the server's token.
    void aSignInInAnAddressStaysOutOfTheLogInsideTheWindowToo() {
        if (!EmbeddedMpv::available())
            QSKIP("libmpv isn't installed here");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(data.path(), data.path());
        useEmbedded(core, data.path());
        const auto end = qScopeGuard([this] { delete m_mpv; m_mpv = nullptr; });
        LogCapture log;
        // A scheme no mpv has a protocol for (nothing is looked up or
        // reached), then a picture mpv makes itself.
        m_mpv->loadAndPlay(QStringLiteral("nosuch://ann:made-up-password@nas.example/films/a.mkv?ApiKey=made-up-key"),
                           0, -1, -1, {}, {}, false, -1, 0, QString(), true, QString(), false, {}, 0, false, {},
                           QString(), {QStringLiteral("av://lavfi:color=c=gray:s=64x48:r=10")});
        // mpv's own line on it, not the launch line's.
        static const QRegularExpression mpvSaid(QStringLiteral("\\[mpv\\] [^\\n]*nosuch://[^\\n]*"));
        QTRY_VERIFY2_WITH_TIMEOUT(mpvSaid.match(log.text()).hasMatch(), qPrintable(log.text()), 10000);
        const QString text = log.text();
        QVERIFY2(mpvSaid.match(text).captured().contains(
                     QLatin1String("nosuch://REDACTED@nas.example/films/a.mkv?ApiKey=REDACTED")),
                 qPrintable(text));
        for (const char *secret : {"made-up-password", "ann:", "made-up-key"})
            QVERIFY2(!text.contains(QLatin1String(secret)), qPrintable(text));
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

    // How long a session's mpv has to open what it plays and show its first
    // picture: a file's while for a file and for a playlist of files, a
    // stream's for an address, for a playlist of addresses on the device
    // (a Jellyfin video on one of Playlists') and for what yt-dlp is to find
    // (YouTube videos on one).
    void aPlaylistOfStreamsHasAStreamsWhileToStart() {
        qunsetenv("OSDOS_START_TIMEOUT_MS");
        const QString files = m_dir.path() + QStringLiteral("/files.m3u");
        const QString streams = m_dir.path() + QStringLiteral("/streams.m3u");
        QVERIFY(write(files, "#EXTM3U\n#EXTINF:-1,A\n/media/A.mp4\nfile:///media/B.mp4\n"));
        QVERIFY(write(streams, "#EXTM3U\n/media/A.mp4\n#EXTINF:-1,B\nhttps://jellyfin.example/Videos/7/stream\n"));
        const QStringList none{ QStringLiteral("--ytdl=no") };
        const QStringList ytdl{ QStringLiteral("--ytdl=no"), QStringLiteral("--ytdl=yes") };
        QCOMPARE(MpvController::startTimeoutMs({ QStringLiteral("/media/A.mp4") }, none, false), 25000);
        QCOMPARE(MpvController::startTimeoutMs({ QStringLiteral("/media/A.mp4") }, none, true), 10000);
        QCOMPARE(MpvController::startTimeoutMs({ files }, none, false), 25000);
        QCOMPARE(MpvController::startTimeoutMs({ QStringLiteral("https://plex.example/film.mp4") }, none, false), 90000);
        QCOMPARE(MpvController::startTimeoutMs({ QStringLiteral("https://plex.example/film.mp4") }, none, true), 45000);
        QCOMPARE(MpvController::startTimeoutMs({ streams }, none, false), 90000);
        QCOMPARE(MpvController::startTimeoutMs({ QUrl::fromLocalFile(streams).toString() }, none, true), 45000);
        QCOMPARE(MpvController::startTimeoutMs({ files }, ytdl, false), 90000);
        QCOMPARE(MpvController::startTimeoutMs({ files }, ytdl, true), 45000);
    }

    // A playlist gone on to its next video ends before that one has a
    // position of its own (mpv has none for it until it plays): its player
    // is handed none, not the last video's. The same video told again (a
    // control connection made anew) keeps where it is.
    void aPlaylistsNextVideoStartsAtNothing() {
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        play("A");
        QTRY_VERIFY_WITH_TIMEOUT(happened("start A"), 5000);
        QLocalServer control;
        QVERIFY(listen(control));
        QTRY_VERIFY_WITH_TIMEOUT(control.hasPendingConnections(), 5000);
        QScopedPointer<QLocalSocket> socket(control.nextPendingConnection());
        QSignalSpy moved(m_mpv, &MpvController::playlistPosChanged);
        socket->write("{\"event\":\"property-change\",\"id\":3,\"name\":\"playlist-pos\",\"data\":0}\n"
                      "{\"event\":\"property-change\",\"id\":2,\"name\":\"duration\",\"data\":60.0}\n"
                      "{\"event\":\"property-change\",\"id\":1,\"name\":\"time-pos\",\"data\":40.0}\n"
                      "{\"event\":\"property-change\",\"id\":3,\"name\":\"playlist-pos\",\"data\":0}\n");
        socket->flush();
        QTRY_COMPARE_WITH_TIMEOUT(moved.count(), 2, 5000);
        QCOMPARE(m_mpv->position(), 40000);
        socket->write("{\"event\":\"property-change\",\"id\":3,\"name\":\"playlist-pos\",\"data\":1}\n"
                      "{\"event\":\"property-change\",\"id\":1,\"name\":\"time-pos\"}\n"
                      "{\"event\":\"property-change\",\"id\":2,\"name\":\"duration\"}\n");
        socket->flush();
        QTRY_COMPARE_WITH_TIMEOUT(moved.count(), 3, 5000);
        m_mpv->stop();
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
        QCOMPARE(ended.first().at(0).toInt(), 0);
        QCOMPARE(ended.first().at(1).toInt(), 0);
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
