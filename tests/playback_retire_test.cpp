// What happens when a video is asked for while another still plays in an mpv
// process (an NFC card tapped mid-film, say): MpvController against a
// stand-in for mpv, a shell script first on PATH that writes down when it
// starts, is told to quit (SIGTERM) and exits, and whether an earlier one was
// still alive as it started. And what becomes of a video its menu's Browse
// left as the next is asked for.
#include "player/MpvController.h"
#include "audio/MenuMusic.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QLocalServer>
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
echo "start $media" >> "$log"
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
    void play(const char *name, float startSeconds = 0) {
        m_mpv->loadAndPlay(m_dir.path() + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".mp4"),
                           startSeconds, -1, -1);
    }

private slots:
    void threeFilmsWithoutStopKeepControlsOnNewest() {
        QSignalSpy ended(m_mpv, &MpvController::playbackEnded);
        for (const char *film : {"A", "B", "C"}) {
            play(film);
            QTRY_VERIFY_WITH_TIMEOUT(happened(QStringLiteral("start ") + film), 5000);
            QLocalServer control;
            const QString path = m_dir.path() + QStringLiteral("/osdos-mpv.sock");
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
        // The controller's socket, logs and input.conf, away from a running app's.
        qputenv("TMPDIR", m_dir.path().toUtf8());
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
