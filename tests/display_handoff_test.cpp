// A video mode of a child's own through the hand-off of the screen
// (DisplayHandoff::setMode(), ScriptEntry::mode), as far as it goes without
// a screen: who may ask for one and when, what the hand-off says of a mode
// it doesn't set, and what a program is started with on its mode and
// without it. The DRM calls themselves (the mode set on a frame of its own
// size, the app's put back) need a Raspberry Pi's console: tests/README.md,
// "On a Raspberry Pi".
#include "modetest/GameMode.h"
#include "modules/scripts/ScriptLauncher.h"
#include "util/DisplayHandoff.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class DisplayHandoffTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QByteArray m_display;

    // A Neo Geo game's mode on a 15 kHz arcade monitor.
    static modetest::ModeLine gameMode() {
        modetest::MonitorRange range;
        modetest::ModeLine mode;
        std::string why;
        if (!modetest::parseMonitorRange(modetest::kArcade15, &range, &why)
            || !modetest::modeFor(1920, 224, 59.185606, range, &mode, &why))
            qFatal("no mode: %s", why.c_str());
        return mode;
    }

    // A script that says what it was started with.
    QString script() {
        const QString path = m_dir.filePath(QStringLiteral("says.sh"));
        if (!QFileInfo::exists(path)) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                qFatal("no script");
            file.write("#!/bin/sh\necho \"args=$*\"\necho \"keep=$OSDOS_TEST_KEEP\"\necho \"preload=$LD_PRELOAD\"\n");
            file.close();
            file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
        }
        return path;
    }

    ScriptEntry entry() {
        ScriptEntry e;
        e.path = script();
        e.basename = QStringLiteral("says.sh");
        e.meta.mode = QStringLiteral("takeover");
        e.argv = { QStringLiteral("as"), QStringLiteral("ever") };
        e.environment = { { QStringLiteral("OSDOS_TEST_KEEP"), QStringLiteral("no") } };
        e.modeArgv = { QStringLiteral("on"), QStringLiteral("its"), QStringLiteral("mode") };
        e.modeEnvironment = { { QStringLiteral("OSDOS_TEST_KEEP"), QStringLiteral("yes") },
                              { QStringLiteral("LD_PRELOAD"), QStringLiteral("libdl.so.2") } };
        return e;
    }

    // The run's own lines, without what anything else printed.
    static QString said(const ScriptLauncher &launcher, const char *key) {
        const QString start = QLatin1String(key) + QLatin1Char('=');
        for (const QString &line : launcher.outputText().split(QLatin1Char('\n'))) {
            if (line.startsWith(start))
                return line.mid(start.size());
        }
        return QStringLiteral("(not said)");
    }

private slots:
    // A hand-off as a desktop's: no console, so nothing of the VT or DRM,
    // and a takeover that isn't turned down for want of a screen to save.
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        m_display = qgetenv("DISPLAY");
        qputenv("DISPLAY", ":99");
        // A library the session preloads already, one every system has.
        qputenv("LD_PRELOAD", "libm.so.6");
    }
    void cleanupTestCase() {
        if (m_display.isEmpty())
            qunsetenv("DISPLAY");
        else
            qputenv("DISPLAY", m_display);
        qunsetenv("LD_PRELOAD");
    }

    // A mode is set only for the owner that holds the screen, and only where
    // a console's screen was handed over: never on a desktop.
    void aModeNeedsAConsoleHandedOver() {
        DisplayHandoff handoff;
        const modetest::ModeLine mode = gameMode();
        QString why;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(
            "^\\[DisplayHandoff\\] arcade's mode 1920x224  15\\.684 kHz  59\\.19 Hz  39\\.132 MHz not set "
            "\\(the screen isn't handed over to it\\): it plays on the mode the screen is on$")));
        QVERIFY(!handoff.setMode(QStringLiteral("arcade"), mode, &why));
        QCOMPARE(why, QStringLiteral("the screen isn't handed over to it"));
        QVERIFY(!handoff.modeSet());

        QCOMPARE(handoff.acquire(QStringLiteral("arcade")), 0);
        QVERIFY(!handoff.setMode(QStringLiteral("scripts"), mode, &why));
        QCOMPARE(why, QStringLiteral("the screen isn't handed over to it"));
        QVERIFY(!handoff.setMode(QStringLiteral("arcade"), mode, &why));
        QCOMPARE(why, QStringLiteral("no console's screen was handed over"));
        QVERIFY(!handoff.modeSet());
        handoff.releaseNow(QStringLiteral("arcade"));
        QVERIFY(!handoff.held());
    }

    // Tests have no screen: a stand-in answers for the kernel. The mode it
    // takes is the hand-off's until the screen is back; one it refuses
    // leaves none, with its reason.
    void aModeIsTheHandOffsUntilTheScreenIsBack() {
        DisplayHandoff handoff;
        const modetest::ModeLine mode = gameMode();
        QStringList asked;
        bool take = true;
        handoff.setModeSetter([&](const modetest::ModeLine &m, QString *why) {
            asked << QString::fromStdString(modetest::describe(m));
            if (!take)
                *why = QStringLiteral("the kernel refused it: Invalid argument");
            return take;
        });
        // Not before the screen is handed over, stand-in or not.
        QVERIFY(!handoff.setMode(QStringLiteral("arcade"), mode));
        QVERIFY(asked.isEmpty());

        QCOMPARE(handoff.acquire(QStringLiteral("arcade")), 0);
        QTest::ignoreMessage(QtInfoMsg,
                             "[DisplayHandoff] arcade's mode set: 1920x224  15.684 kHz  59.19 Hz  39.132 MHz");
        QVERIFY(handoff.setMode(QStringLiteral("arcade"), mode));
        QVERIFY(handoff.modeSet());
        QCOMPARE(asked, QStringList({ QStringLiteral("1920x224  15.684 kHz  59.19 Hz  39.132 MHz") }));
        // One mode to a hand-off.
        QString why;
        QVERIFY(!handoff.setMode(QStringLiteral("arcade"), mode, &why));
        QCOMPARE(why, QStringLiteral("a mode is set for this hand-off already"));
        QCOMPARE(asked.size(), 1);
        QVERIFY(handoff.modeSet());
        handoff.releaseNow(QStringLiteral("arcade"));
        QVERIFY(!handoff.modeSet());

        // Refused: no mode, the reason passed on, and the next hand-off free
        // to ask again.
        take = false;
        QCOMPARE(handoff.acquire(QStringLiteral("arcade")), 0);
        QTest::ignoreMessage(QtWarningMsg,
                             "[DisplayHandoff] arcade's mode 1920x224  15.684 kHz  59.19 Hz  39.132 MHz not set "
                             "(the kernel refused it: Invalid argument): it plays on the mode the screen is on");
        QVERIFY(!handoff.setMode(QStringLiteral("arcade"), mode, &why));
        QCOMPARE(why, QStringLiteral("the kernel refused it: Invalid argument"));
        QVERIFY(!handoff.modeSet());
        QCOMPARE(asked.size(), 2);
        handoff.releaseNow(QStringLiteral("arcade"));

        // The hand-back by the timer puts it back as well.
        take = true;
        QCOMPARE(handoff.acquire(QStringLiteral("arcade")), 0);
        QVERIFY(handoff.setMode(QStringLiteral("arcade"), mode));
        bool back = false;
        handoff.releaseDeferred(QStringLiteral("arcade"), [&back] { back = true; }, 10);
        QTRY_VERIFY(back);
        QVERIFY(!handoff.modeSet());
        QVERIFY(!handoff.held());
    }

    // A program started with a mode of its own: on it, with what keeps it
    // there (its other arguments, its variables over the rest, a library
    // preloaded after any the session preloads); where the mode wasn't set,
    // and for a run that asks for none, exactly as ever.
    void aRunOnItsOwnMode() {
        DisplayHandoff handoff;
        int asked = 0;
        bool take = true;
        handoff.setModeSetter([&](const modetest::ModeLine &, QString *why) {
            ++asked;
            if (!take)
                *why = QStringLiteral("the kernel refused it: Invalid argument");
            return take;
        });
        const auto run = [&](const ScriptEntry &e, ScriptLauncher &launcher) {
            QSignalSpy finished(&launcher, &ScriptLauncher::finished);
            QString error;
            if (!launcher.start(e, &error))
                return error;
            // While it runs the mode is the hand-off's, where it was set.
            const bool held = handoff.modeSet();
            if (!finished.wait(10000))
                return QStringLiteral("never finished");
            if (handoff.held() || handoff.modeSet())
                return QStringLiteral("the screen isn't back");
            return QStringLiteral("%1 %2").arg(finished.at(0).at(1).toString(), held ? "mode" : "no mode");
        };
        ScriptLauncher launcher(m_dir.path(), m_dir.path(), &handoff);
        launcher.setHandoffOwner(QStringLiteral("arcade"));

        // On its mode.
        ScriptEntry e = entry();
        e.mode = gameMode();
        QCOMPARE(run(e, launcher), QStringLiteral("ok mode"));
        QCOMPARE(asked, 1);
        QVERIFY(launcher.modeSet());
        QCOMPARE(said(launcher, "args"), QStringLiteral("on its mode"));
        QCOMPARE(said(launcher, "keep"), QStringLiteral("yes"));
        QCOMPARE(said(launcher, "preload"), QStringLiteral("libm.so.6:libdl.so.2"));

        // The mode refused: the run all the same, as one without a mode.
        take = false;
        QCOMPARE(run(e, launcher), QStringLiteral("ok no mode"));
        QCOMPARE(asked, 2);
        QVERIFY(!launcher.modeSet());
        QCOMPARE(said(launcher, "args"), QStringLiteral("as ever"));
        QCOMPARE(said(launcher, "keep"), QStringLiteral("no"));
        QCOMPARE(said(launcher, "preload"), QStringLiteral("libm.so.6"));

        // A run that asks for none: the screen is never asked.
        take = true;
        e.mode.reset();
        QCOMPARE(run(e, launcher), QStringLiteral("ok no mode"));
        QCOMPARE(asked, 2);
        QCOMPARE(said(launcher, "args"), QStringLiteral("as ever"));
        QCOMPARE(said(launcher, "preload"), QStringLiteral("libm.so.6"));

        // Nor for one that keeps to the app's own screen (console).
        e = entry();
        e.mode = gameMode();
        e.meta.mode = QStringLiteral("console");
        QCOMPARE(run(e, launcher), QStringLiteral("ok no mode"));
        QCOMPARE(asked, 2);
        QCOMPARE(said(launcher, "args"), QStringLiteral("as ever"));

        // And with no hand-off at all to ask.
        ScriptLauncher alone(m_dir.path(), m_dir.path(), nullptr);
        e.meta.mode = QStringLiteral("takeover");
        QSignalSpy finished(&alone, &ScriptLauncher::finished);
        QString error;
        QVERIFY2(alone.start(e, &error), qPrintable(error));
        QVERIFY(finished.wait(10000));
        QVERIFY(!alone.modeSet());
        QCOMPARE(said(alone, "args"), QStringLiteral("as ever"));
    }
};

QTEST_GUILESS_MAIN(DisplayHandoffTest)
#include "display_handoff_test.moc"
