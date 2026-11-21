// The system around an arcade game (GameSystem) against a stand-in for
// osdos-game.socket, as osdos-stop game takes it: the line a game's start
// sends, from the Arcade module's setting and whether it plays over the
// network, held for the whole game, and "end" as it ends; nothing asked
// without the helper (an older launcher, no socket).
#include "AppCore.h"
#include "modules/arcade/GameSystem.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace {

// A stand-in for osdos-game.socket: what each connection sent, whether it
// has closed, and the answers it gives, a line for each line it is sent.
struct Helper {
    QLocalServer server;
    QLocalSocket *connection = nullptr;
    QByteArray sent;
    bool closed = false;
    QByteArrayList answers;

    bool listen(const QString &dir) {
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            connection = server.nextPendingConnection();
            sent.clear();
            closed = false;
            QObject::connect(connection, &QLocalSocket::readyRead, connection, [this] {
                const QByteArray got = connection->readAll();
                sent += got;
                for (int lines = got.count('\n'); lines > 0 && !answers.isEmpty(); --lines)
                    connection->write(answers.takeFirst());
            });
            QObject::connect(connection, &QLocalSocket::disconnected, connection, [this] { closed = true; });
        });
        return server.listen(dir + QStringLiteral("/osdos-game.sock"));
    }
};

const QString kArcade = QStringLiteral("com.osdos.arcade");

} // namespace

class GameSystemTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_data;
    QTemporaryDir m_run;

private slots:
    void initTestCase() {
        QVERIFY(m_data.isValid());
        QVERIFY(m_run.isValid());
        qputenv("OSDOS_RUN_DIR", m_run.path().toUtf8());
        qputenv("OSDOS_LAUNCHER_API", "11");
    }

    // A game's start sends what it wants and holds the connection; its end
    // sends "end", then closes it.
    void heldForTheGame() {
        AppCore core(m_data.path(), m_data.path());
        Helper helper;
        QVERIFY(helper.listen(m_run.path()));
        helper.answers << "ok performance\n";
        GameSystem system(&core);
        QVERIFY(system.fullSpeedWanted());
        QTest::ignoreMessage(QtInfoMsg, "[Arcade] CPU governor for the game: performance");
        system.started(false);
        QTRY_COMPARE(helper.sent, QByteArray("start full-speed\n"));
        QTest::qWait(100);
        QVERIFY(!helper.closed);
        system.ended();
        QTRY_VERIFY(helper.closed);
        QCOMPARE(helper.sent, QByteArray("start full-speed\nend\n"));
    }

    // Over the network, and with the setting off: the helper is told, and
    // what it kept is logged.
    void networkAndSettingOff() {
        AppCore core(m_data.path(), m_data.path());
        QVERIFY(core.save_setting(kArcade, QLatin1String(GameSystem::kFullSpeedKey), QStringLiteral("Off")));
        Helper helper;
        QVERIFY(helper.listen(m_run.path()));
        helper.answers << "ok kept ondemand, as Settings asks\n";
        GameSystem system(&core);
        QVERIFY(!system.fullSpeedWanted());
        QTest::ignoreMessage(QtInfoMsg, "[Arcade] CPU governor for the game kept at ondemand, as Settings asks");
        system.started(true);
        QTRY_COMPARE(helper.sent, QByteArray("start as-is network\n"));
        QTest::qWait(100);
        system.ended();
        QTRY_VERIFY(helper.closed);
        QVERIFY(core.save_setting(kArcade, QLatin1String(GameSystem::kFullSpeedKey), QStringLiteral("On")));
        QVERIFY(system.fullSpeedWanted());
    }

    // The app gone mid-game (here the object, as the app quits): the
    // connection closes, which the helper takes as the game's end.
    void closedWithTheApp() {
        Helper helper;
        QVERIFY(helper.listen(m_run.path()));
        auto system = std::make_unique<GameSystem>(nullptr);
        system->started(false);
        QTRY_COMPARE(helper.sent, QByteArray("start full-speed\n"));
        system.reset();
        QTRY_VERIFY(helper.closed);
        QCOMPARE(helper.sent, QByteArray("start full-speed\n"));
    }

    // A refusal is logged, and the game goes on.
    void refused() {
        Helper helper;
        QVERIFY(helper.listen(m_run.path()));
        helper.answers << "nothing this does\n";
        GameSystem system(nullptr);
        QTest::ignoreMessage(QtWarningMsg,
                             "[Arcade] CPU governor for the game: osdos-game.socket said \"nothing this does\"");
        system.started(false);
        QTRY_VERIFY(!helper.sent.isEmpty());
        QTest::qWait(100);
        system.ended();
    }

    // Without the helper (a launcher before level 11, or no socket), nothing
    // is asked.
    void withoutTheHelper() {
        Helper helper;
        QVERIFY(helper.listen(m_run.path()));
        qputenv("OSDOS_LAUNCHER_API", "10");
        GameSystem system(nullptr);
        system.started(true);
        QTest::qWait(100);
        QVERIFY(!helper.connection);
        system.ended();
        qputenv("OSDOS_LAUNCHER_API", "11");
        helper.server.close();
        GameSystem alone(nullptr);
        alone.started(false);
        alone.ended();
    }
};

QTEST_GUILESS_MAIN(GameSystemTest)
#include "game_system_test.moc"
