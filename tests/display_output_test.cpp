// Settings → Display Output (src/display/DisplayOutput) against the image's
// own presets in a stand-in boot partition: the JAMMA preset is offered only
// where osdos-stop knows its code (launcher API 4), and is told apart from the
// others when it is in force. Switch and Power Off and At Next Start come with
// launcher API 5, the latter through a stand-in for osdos-display.socket. On a
// Pi 4, or the board OSDOS_BOARD_MODEL names (ctest's display_output_pi5 runs
// it as a Pi 5).
#include "display/DisplayOutput.h"
#include "util/Board.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QStringList ids(const QVariantList &options) {
    QStringList out;
    for (const QVariant &o : options)
        out << o.toMap().value(QStringLiteral("id")).toString();
    return out;
}

bool pi5() {
    return board::family() == board::Family::Pi5;
}

} // namespace

class DisplayOutputTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_boot;
    QTemporaryDir m_run;
    QTemporaryDir m_data;

    QString statePath() const { return m_data.filePath(QStringLiteral("display-output.json")); }
    QString atBootPath() const { return m_run.filePath(QStringLiteral("osdos-display-boot.txt")); }

    void inForce(const QString &id) {
        const QString path = m_boot.filePath(QStringLiteral("osdos-display.txt"));
        QFile::remove(path);
        QVERIFY(QFile::copy(m_boot.filePath(QStringLiteral("osdos-display-%1.txt").arg(id)), path));
        QFile::remove(atBootPath());
        QFile::remove(statePath());
    }

    QJsonObject state() const {
        QFile f(statePath());
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
    }

    // osdos-display.socket as the image has it, osdos-stop next-start behind
    // it: the output in force kept in /run once, the named preset written,
    // and the answer. With `answer` set, that instead, and nothing written;
    // `silent`, written and no answer, as when it comes too late.
    struct StopHelper {
        DisplayOutputTest *test;
        QLocalServer server;
        QByteArray answer;
        bool silent = false;
        QByteArrayList asked;

        explicit StopHelper(DisplayOutputTest *t) : test(t) {
            QLocalServer::removeServer(t->m_run.filePath(QStringLiteral("osdos-display.sock")));
            server.listen(t->m_run.filePath(QStringLiteral("osdos-display.sock")));
            QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
                QLocalSocket *c = server.nextPendingConnection();
                const auto take = [this, c] {
                    if (!c->canReadLine())
                        return;
                    const QByteArray id = c->readLine().trimmed();
                    asked << id;
                    if (answer.isEmpty()) {
                        const QString written = test->m_boot.filePath(QStringLiteral("osdos-display.txt"));
                        if (!QFile::exists(test->atBootPath()))
                            QFile::copy(written, test->atBootPath());
                        QFile::remove(written);
                        QFile::copy(test->m_boot.filePath(QStringLiteral("osdos-display-%1.txt")
                                                              .arg(QString::fromLatin1(id))), written);
                    }
                    if (!silent) {
                        c->write(answer.isEmpty() ? QByteArray("ok\n") : answer + '\n');
                        c->flush();
                    }
                    c->disconnectFromServer();
                };
                // The line may be there already, before readyRead could say so.
                QObject::connect(c, &QLocalSocket::readyRead, c, take);
                take();
            });
        }
    };

private slots:
    void initTestCase() {
        QVERIFY(m_boot.isValid() && m_data.isValid());
        const QDir presets(QStringLiteral(PRESETS_DIR));
        const QStringList files = presets.entryList({ QStringLiteral("osdos-display-*.txt") });
        QVERIFY(files.contains(QStringLiteral("osdos-display-jamma-240p.txt")));
        for (const QString &f : files)
            QVERIFY(QFile::copy(presets.filePath(f), m_boot.filePath(f)));
        QVERIFY(m_run.isValid());
        qputenv("OSDOS_BOOT_DIR", m_boot.path().toUtf8());
        qputenv("OSDOS_RUN_DIR", m_run.path().toUtf8());
        if (!qEnvironmentVariableIsSet("OSDOS_BOARD_MODEL"))
            qputenv("OSDOS_BOARD_MODEL", "Raspberry Pi 4 Model B Rev 1.5");
        QVERIFY(board::family() == board::Family::Pi4 || pi5());
        qputenv("OSDOS_AUTOSTART", "1");
    }

    void jammaNeedsLauncherApi4() {
        inForce(QStringLiteral("hdmi"));
        qputenv("OSDOS_LAUNCHER_API", "3");
        DisplayOutput api3(m_data.path());
        QVERIFY(api3.available());
        QVERIFY(ids(api3.options()).contains(QStringLiteral("scart-rgb-ntsc")));
        QVERIFY(!ids(api3.options()).contains(QStringLiteral("jamma-240p")));

        qputenv("OSDOS_LAUNCHER_API", "4");
        DisplayOutput api4(m_data.path());
        const QStringList offered = ids(api4.options());
        QCOMPARE(offered.last(), QStringLiteral("jamma-240p"));
        QCOMPARE(api4.options().last().toMap().value(QStringLiteral("label")).toString(),
                 QStringLiteral("JAMMA 240p"));
        // Beside the board's own: a Pi 5's GPIO composite, a Pi 4's 240p SCART.
        QCOMPARE(offered.contains(QStringLiteral("crt-gpio-ntsc")), pi5());
        QCOMPARE(offered.contains(QStringLiteral("scart-rgb-240p")), !pi5());
    }

    void jammaInForceIsKnown() {
        inForce(QStringLiteral("jamma-240p"));
        qputenv("OSDOS_LAUNCHER_API", "4");
        DisplayOutput output(m_data.path());
        QCOMPARE(output.current(), QStringLiteral("jamma-240p"));
        QCOMPARE(output.currentLabel(), QStringLiteral("JAMMA 240p"));
    }

    // These before any test that switches: QCoreApplication::exit(),
    // which apply() and applyAndPowerOff() end with, has every event loop
    // after it in this process return at once, QSignalSpy::wait()'s too.
    void atNextStartWaitsForTheNextStart() {
        inForce(QStringLiteral("hdmi"));
        qputenv("OSDOS_LAUNCHER_API", "5");
        StopHelper helper(this);
        DisplayOutput output(m_data.path());
        QVERIFY(output.nextStartAvailable());
        // Nothing to take back yet.
        QVERIFY(!output.applyAtNextStart(QStringLiteral("hdmi")));

        QSignalSpy finished(&output, &DisplayOutput::nextStartFinished);
        QVERIFY(output.applyAtNextStart(QStringLiteral("jamma-240p")));
        QVERIFY(output.busy());
        QVERIFY(!output.applyAtNextStart(QStringLiteral("crt-pal")));  // one at a time
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(0).toBool(), true);
        QCOMPARE(helper.asked, QByteArrayList{ "jamma-240p" });
        QVERIFY(!output.busy());
        QCOMPARE(output.current(), QStringLiteral("hdmi"));
        QCOMPARE(output.next(), QStringLiteral("jamma-240p"));
        QCOMPARE(output.nextLabel(), QStringLiteral("JAMMA 240p"));
        QCOMPARE(state().value(QStringLiteral("from")).toString(), QStringLiteral("HDMI"));
        QCOMPARE(state().value(QStringLiteral("to")).toString(), QStringLiteral("jamma-240p"));

        // OSD/OS started again in the same boot: still on HDMI, JAMMA still
        // waiting, nothing asked.
        DisplayOutput again(m_data.path());
        QCOMPARE(again.current(), QStringLiteral("hdmi"));
        QCOMPARE(again.next(), QStringLiteral("jamma-240p"));
        QVERIFY(!again.confirmPending() && again.notice().isEmpty());
        QVERIFY(QFile::exists(statePath()));

        // Taken back: HDMI at the next start too, nothing left to ask there.
        QSignalSpy back(&again, &DisplayOutput::nextStartFinished);
        QVERIFY(again.applyAtNextStart(QStringLiteral("hdmi")));
        QVERIFY(back.wait(5000));
        QCOMPARE(back.first().at(0).toBool(), true);
        QCOMPARE(again.next(), QString());
        QVERIFY(!QFile::exists(statePath()));

        // Written again, then the next start, /run empty again: the new
        // output asks to be kept, and names the one it would go back to.
        QVERIFY(again.applyAtNextStart(QStringLiteral("jamma-240p")));
        QVERIFY(back.wait(5000));
        QFile::remove(atBootPath());
        DisplayOutput nextStart(m_data.path());
        QCOMPARE(nextStart.current(), QStringLiteral("jamma-240p"));
        QCOMPARE(nextStart.next(), QString());
        QVERIFY(nextStart.confirmPending());
        QCOMPARE(nextStart.previousLabel(), QStringLiteral("HDMI"));
        nextStart.keep();
        QVERIFY(!QFile::exists(statePath()));
    }

    void atNextStartSaysWhyNot() {
        inForce(QStringLiteral("hdmi"));
        qputenv("OSDOS_LAUNCHER_API", "5");
        StopHelper helper(this);
        helper.answer = "the preset isn't on the boot partition";
        DisplayOutput output(m_data.path());
        QSignalSpy finished(&output, &DisplayOutput::nextStartFinished);
        QVERIFY(output.applyAtNextStart(QStringLiteral("jamma-240p")));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(0).toBool(), false);
        QCOMPARE(finished.first().at(1).toString(), QStringLiteral("the preset isn't on the boot partition"));
        QCOMPARE(output.next(), QString());
        QVERIFY(!output.busy());
        QVERIFY(!QFile::exists(statePath()));

        // The socket gone between: why, and nothing noted.
        helper.server.close();
        QVERIFY(output.applyAtNextStart(QStringLiteral("jamma-240p")));
        QVERIFY(finished.count() == 2 || finished.wait(5000));
        QCOMPARE(finished.last().at(0).toBool(), false);
        QVERIFY(!finished.last().at(1).toString().isEmpty());
        QVERIFY(!QFile::exists(statePath()));
    }

    void atNextStartWithoutAnAnswer() {
        // Written, and the answer lost: still asked about at the next start.
        inForce(QStringLiteral("hdmi"));
        qputenv("OSDOS_LAUNCHER_API", "5");
        StopHelper helper(this);
        helper.silent = true;
        DisplayOutput output(m_data.path());
        QSignalSpy finished(&output, &DisplayOutput::nextStartFinished);
        QVERIFY(output.applyAtNextStart(QStringLiteral("jamma-240p")));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(0).toBool(), false);
        QCOMPARE(output.next(), QStringLiteral("jamma-240p"));
        QCOMPARE(state().value(QStringLiteral("to")).toString(), QStringLiteral("jamma-240p"));
        QFile::remove(atBootPath());
        DisplayOutput nextStart(m_data.path());
        QVERIFY(nextStart.confirmPending());
        QCOMPARE(nextStart.previousLabel(), QStringLiteral("HDMI"));
    }

    void choosingJammaLeavesItsWayBack() {
        inForce(QStringLiteral("scart-rgb-ntsc"));
        qputenv("OSDOS_LAUNCHER_API", "4");
        DisplayOutput output(m_data.path());
        QVERIFY(output.apply(QStringLiteral("jamma-240p")));
        QFile state(m_data.filePath(QStringLiteral("display-output.json")));
        QVERIFY(state.open(QIODevice::ReadOnly));
        const QJsonObject o = QJsonDocument::fromJson(state.readAll()).object();
        QCOMPARE(o.value(QStringLiteral("from")).toString(), QStringLiteral("SCART RGB NTSC"));
        QCOMPARE(o.value(QStringLiteral("to")).toString(), QStringLiteral("jamma-240p"));
        state.close();
        QFile::remove(state.fileName());
        // Not with a launcher that wouldn't know the code.
        qputenv("OSDOS_LAUNCHER_API", "3");
        DisplayOutput older(m_data.path());
        QVERIFY(!older.apply(QStringLiteral("jamma-240p")));
    }

    void powerOffNeedsLauncherApi5() {
        inForce(QStringLiteral("hdmi"));
        qputenv("OSDOS_LAUNCHER_API", "4");
        DisplayOutput api4(m_data.path());
        QVERIFY(!api4.powerOffAvailable() && !api4.nextStartAvailable());
        QVERIFY(!api4.applyAndPowerOff(QStringLiteral("jamma-240p")));
        QVERIFY(!QFile::exists(statePath()));

        qputenv("OSDOS_LAUNCHER_API", "5");
        DisplayOutput api5(m_data.path());
        QVERIFY(api5.powerOffAvailable());
        // No socket: no At Next Start.
        QVERIFY(!api5.nextStartAvailable());
        QVERIFY(!api5.applyAtNextStart(QStringLiteral("jamma-240p")));
        QVERIFY(api5.applyAndPowerOff(QStringLiteral("jamma-240p")));
        QCOMPARE(state().value(QStringLiteral("from")).toString(), QStringLiteral("HDMI"));
        QCOMPARE(state().value(QStringLiteral("to")).toString(), QStringLiteral("jamma-240p"));
    }

    // A tube, whose pixels aren't square: every preset but HDMI's, whatever
    // its mode, on a screen of a tube's lines (not the HDMI screen a Pi 5
    // falls back to); without one, a TV's modes and not a small panel's.
    void tubes() {
        QVERIFY(DisplayOutput::isTube(QStringLiteral("jamma-240p"), QSize(720, 240)));
        QVERIFY(DisplayOutput::isTube(QStringLiteral("crt-pal"), QSize(720, 576)));
        QVERIFY(!DisplayOutput::isTube(QStringLiteral("crt-ntsc"), QSize(1920, 1080)));
        QVERIFY(!DisplayOutput::isTube(QStringLiteral("hdmi"), QSize(720, 480)));
        QVERIFY(!DisplayOutput::isTube(QStringLiteral("hdmi"), QSize(1920, 1080)));
        QVERIFY(DisplayOutput::isTube(QString(), QSize(720, 480)));
        QVERIFY(DisplayOutput::isTube(QString(), QSize(720, 576)));
        QVERIFY(DisplayOutput::isTube(QString(), QSize(720, 240)));
        QVERIFY(DisplayOutput::isTube(QString(), QSize(1920, 240)));
        QVERIFY(!DisplayOutput::isTube(QString(), QSize(480, 272)));
        QVERIFY(!DisplayOutput::isTube(QString(), QSize(800, 480)));
        QVERIFY(!DisplayOutput::isTube(QString(), QSize(1920, 1080)));
        QVERIFY(!DisplayOutput::isTube(QStringLiteral("jamma-240p"), QSize()));
    }
};

QTEST_GUILESS_MAIN(DisplayOutputTest)
#include "display_output_test.moc"
