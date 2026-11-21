// The system around an arcade game (GameSystem) against a stand-in for
// osdos-game.socket, as osdos-stop game takes it: the line a game's start
// sends, from the Arcade module's setting and whether it plays over the
// network, held for the whole game, and "end" as it ends; the helper's line
// of what the Pi did logged, and a sentence for the player once each when
// it ran short of power or slowed down; nothing asked without the helper (an
// older launcher, no socket), and said so only on a Pi (OSDOS_BOARD_MODEL,
// which the test is run with once and once without).
#include "AppCore.h"
#include "modules/arcade/GameSystem.h"

#include <QElapsedTimer>
#include <QLocalServer>
#include <QRegularExpression>
#include <QLocalSocket>
#include <QMutex>
#include <QThread>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace {

// A stand-in for osdos-game.socket, on a thread of its own as the helper is
// a process of its own: GameSystem waits for its line at a game's end. What
// each connection sent, whether it has closed, and the answers it gives, a
// line for each line it is sent.
class Helper {
public:
    explicit Helper(const QString &dir) {
        m_thread.start();
        m_server = new QLocalServer;
        m_server->moveToThread(&m_thread);
        QMetaObject::invokeMethod(m_server, [this, dir] {
            QObject::connect(m_server, &QLocalServer::newConnection, m_server, [this] {
                QLocalSocket *connection = m_server->nextPendingConnection();
                {
                    QMutexLocker lock(&m_mutex);
                    m_sent.clear();
                    m_closed = false;
                    m_connected = true;
                }
                QObject::connect(connection, &QLocalSocket::readyRead, connection, [this, connection] {
                    const QByteArray got = connection->readAll();
                    QMutexLocker lock(&m_mutex);
                    m_sent += got;
                    for (int lines = int(got.count('\n')); lines > 0 && !m_answers.isEmpty(); --lines)
                        connection->write(m_answers.takeFirst());
                });
                QObject::connect(connection, &QLocalSocket::disconnected, connection, [this, connection] {
                    QMutexLocker lock(&m_mutex);
                    m_closed = true;
                    connection->deleteLater();
                });
            });
            m_listening = m_server->listen(dir + QStringLiteral("/osdos-game.sock"));
        }, Qt::BlockingQueuedConnection);
    }
    ~Helper() {
        stop();
        m_thread.quit();
        m_thread.wait();
    }
    bool listening() const { return m_listening; }
    // The socket gone, as with no helper.
    void stop() {
        if (!m_server)
            return;
        QLocalServer *server = m_server;
        m_server = nullptr;
        QMetaObject::invokeMethod(server, [server] { delete server; }, Qt::BlockingQueuedConnection);
    }
    void answer(const QByteArrayList &lines) {
        QMutexLocker lock(&m_mutex);
        m_answers += lines;
    }
    QByteArray sent() const {
        QMutexLocker lock(&m_mutex);
        return m_sent;
    }
    bool closed() const {
        QMutexLocker lock(&m_mutex);
        return m_closed;
    }
    bool connected() const {
        QMutexLocker lock(&m_mutex);
        return m_connected;
    }

private:
    QThread m_thread;
    QLocalServer *m_server = nullptr;
    bool m_listening = false;
    mutable QMutex m_mutex;
    QByteArray m_sent;
    bool m_closed = false;
    bool m_connected = false;
    QByteArrayList m_answers;
};

const QString kArcade = QStringLiteral("com.osdos.arcade");
const QByteArray kSummary = "governor performance, clock 1800 MHz, throttling none, highest 61.2 °C";

bool onPi() {
    return qgetenv("OSDOS_BOARD_MODEL").startsWith("Raspberry Pi");
}

} // namespace

class GameSystemTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_data;
    QTemporaryDir m_run;

private slots:
    void initTestCase() {
        // Every warning a test doesn't expect is a failure.
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
        QVERIFY(m_data.isValid());
        QVERIFY(m_run.isValid());
        qputenv("OSDOS_RUN_DIR", m_run.path().toUtf8());
        qputenv("OSDOS_LAUNCHER_API", "11");
    }

    // A game's start sends what it wants and holds the connection; its end
    // sends "end", then closes it.
    void heldForTheGame() {
        AppCore core(m_data.path(), m_data.path());
        Helper helper(m_run.path());
        QVERIFY(helper.listening());
        helper.answer({ "ok performance\n", "notice=none " + kSummary + "\n" });
        GameSystem system(&core);
        QVERIFY(system.fullSpeedWanted());
        QTest::ignoreMessage(QtInfoMsg, "[Arcade] CPU governor for the game: performance");
        system.started(false);
        QTRY_COMPARE(helper.sent(), QByteArray("start full-speed\n"));
        QTest::qWait(100);
        QVERIFY(!helper.closed());
        QTest::ignoreMessage(QtInfoMsg, QByteArray("arcade: system during the game: " + kSummary).constData());
        QCOMPARE(system.ended(), QString());
        QTRY_VERIFY(helper.closed());
        QCOMPARE(helper.sent(), QByteArray("start full-speed\nend\n"));
    }

    // Over the network, and with the setting off: the helper is told, and
    // what it kept is logged.
    void networkAndSettingOff() {
        AppCore core(m_data.path(), m_data.path());
        QVERIFY(core.save_setting(kArcade, QLatin1String(GameSystem::kFullSpeedKey), QStringLiteral("Off")));
        Helper helper(m_run.path());
        QVERIFY(helper.listening());
        helper.answer({ "ok kept ondemand, as Settings asks\n" });
        GameSystem system(&core);
        QVERIFY(!system.fullSpeedWanted());
        QTest::ignoreMessage(QtInfoMsg, "[Arcade] CPU governor for the game kept at ondemand, as Settings asks");
        system.started(true);
        QTRY_COMPARE(helper.sent(), QByteArray("start as-is network\n"));
        QTest::qWait(100);
        QTest::ignoreMessage(QtInfoMsg,
                             "arcade: system during the game: not measured (osdos-game.socket didn't answer in time)");
        QElapsedTimer waited;
        waited.start();
        QCOMPARE(system.ended(), QString());
        // Waited for, but not past kEndWaitMs.
        QVERIFY(waited.elapsed() < GameSystem::kEndWaitMs + 500);
        QTRY_VERIFY(helper.closed());
        QVERIFY(core.save_setting(kArcade, QLatin1String(GameSystem::kFullSpeedKey), QStringLiteral("On")));
        QVERIFY(system.fullSpeedWanted());
    }

    // The Pi short of power or slowed down: said on screen, once each this
    // run, and logged every time.
    void notices() {
        Helper helper(m_run.path());
        QVERIFY(helper.listening());
        GameSystem system(nullptr);
        const auto game = [&](const QByteArray &flags) {
            helper.answer({ "ok performance\n", "notice=" + flags + " " + kSummary + "\n" });
            QTest::ignoreMessage(QtInfoMsg, "[Arcade] CPU governor for the game: performance");
            system.started(false);
            [&] { QTRY_VERIFY(helper.sent().contains('\n')); }();
            QTest::qWait(50);
            QTest::ignoreMessage(QtInfoMsg, QByteArray("arcade: system during the game: " + kSummary).constData());
            return system.ended();
        };
        QCOMPARE(game("throttled"), GameSystem::noticeFor(QStringLiteral("throttled")));
        QVERIFY(GameSystem::noticeFor(QStringLiteral("throttled")).contains(QLatin1String("slowed itself down")));
        QCOMPARE(game("throttled"), QString());
        QCOMPARE(game("power"), GameSystem::noticeFor(QStringLiteral("power")));
        QVERIFY(GameSystem::noticeFor(QStringLiteral("power")).contains(QLatin1String("short of power")));
        QCOMPARE(game("power,throttled"), GameSystem::noticeFor(QStringLiteral("power,throttled")));
        QCOMPARE(game("none"), QString());
        QCOMPARE(GameSystem::noticeFor(QStringLiteral("none")), QString());
        QCOMPARE(GameSystem::joined(QString(), QStringLiteral("b")), QStringLiteral("b"));
        QCOMPARE(GameSystem::joined(QStringLiteral("a"), QString()), QStringLiteral("a"));
        QCOMPARE(GameSystem::joined(QStringLiteral("a."), QStringLiteral("b")), QStringLiteral("a. b"));
    }

    // The app gone mid-game (here the object, as the app quits): the
    // connection closes, which the helper takes as the game's end.
    void closedWithTheApp() {
        Helper helper(m_run.path());
        QVERIFY(helper.listening());
        auto system = std::make_unique<GameSystem>(nullptr);
        system->started(false);
        QTRY_COMPARE(helper.sent(), QByteArray("start full-speed\n"));
        system.reset();
        QTRY_VERIFY(helper.closed());
        QCOMPARE(helper.sent(), QByteArray("start full-speed\n"));
    }

    // A refusal is logged, and the game goes on.
    void refused() {
        Helper helper(m_run.path());
        QVERIFY(helper.listening());
        helper.answer({ "nothing this does\n" });
        GameSystem system(nullptr);
        QTest::ignoreMessage(QtWarningMsg,
                             "[Arcade] CPU governor for the game: osdos-game.socket said \"nothing this does\"");
        system.started(false);
        QTRY_VERIFY(!helper.sent().isEmpty());
        QTest::qWait(100);
        if (onPi())
            QTest::ignoreMessage(QtInfoMsg,
                                 "arcade: system during the game: not measured (osdos-game.socket said \"nothing this does\")");
        QCOMPARE(system.ended(), QString());
    }

    // Without the helper (a launcher before level 11, or no socket), nothing
    // is asked.
    void withoutTheHelper() {
        Helper helper(m_run.path());
        QVERIFY(helper.listening());
        qputenv("OSDOS_LAUNCHER_API", "10");
        GameSystem system(nullptr);
        system.started(true);
        QTest::qWait(100);
        QVERIFY(!helper.connected());
        if (onPi())
            QTest::ignoreMessage(QtInfoMsg, "arcade: system during the game: not measured (the launcher is below level 11)");
        QCOMPARE(system.ended(), QString());
        qputenv("OSDOS_LAUNCHER_API", "11");
        helper.stop();
        GameSystem alone(nullptr);
        alone.started(false);
        if (onPi())
            QTest::ignoreMessage(QtInfoMsg, "arcade: system during the game: not measured (no osdos-game.socket)");
        QCOMPARE(alone.ended(), QString());
    }
};

QTEST_GUILESS_MAIN(GameSystemTest)
#include "game_system_test.moc"
