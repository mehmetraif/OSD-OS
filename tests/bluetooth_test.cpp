// Settings → Network → Bluetooth's DETAILS (BluetoothManager::collectDetails())
// against a stand-in for journalctl: a shell script first on PATH that writes
// down that it started, writes two lines of a system log, one of them
// Bluetooth's, and stays as long as it is told (FAKE_JOURNAL_STAY).
#include "bluetooth/BluetoothManager.h"
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

static const char kJournal[] = R"(#!/bin/sh
echo "start $$" >> "$FAKE_JOURNAL_LOG"
echo "[   12.000001] kernel: Bluetooth: hci0: BCM: firmware Patch file not found"
echo "[   13.000002] systemd[1]: Started Network Time Synchronization."
if [ -n "$FAKE_JOURNAL_STAY" ]; then exec sleep "$FAKE_JOURNAL_STAY"; fi
)";

class BluetoothTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_log;

    // The stand-in's starts, each its process id.
    QStringList starts() const {
        QFile f(m_log);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)
            .filter(QRegularExpression(QStringLiteral("^start "))).replaceInStrings(QStringLiteral("start "), QString());
    }
    static bool alive(const QString &pid) {
        QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
        return stat.open(QIODevice::ReadOnly) && !QString::fromLatin1(stat.readAll()).contains(QStringLiteral(") Z "));
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        const QString bin = m_dir.path() + QStringLiteral("/bin");
        QVERIFY(QDir().mkpath(bin));
        QFile script(bin + QStringLiteral("/journalctl"));
        QVERIFY(script.open(QIODevice::WriteOnly));
        QVERIFY(script.write(kJournal) > 0);
        script.close();
        QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        qputenv("PATH", bin.toUtf8() + ':' + qgetenv("PATH"));
        m_log = m_dir.path() + QStringLiteral("/journal.log");
        qputenv("FAKE_JOURNAL_LOG", m_log.toUtf8());
    }

    void init() {
        QFile::remove(m_log);
        qputenv("FAKE_JOURNAL_STAY", "");
    }

    // The log read: its Bluetooth lines, the others left out.
    void logRead() {
        BluetoothManager manager;
        manager.collectDetails();
        QTRY_VERIFY_WITH_TIMEOUT(manager.details().contains(QStringLiteral("hci0: BCM: firmware Patch file not found")),
                                 3000);
        QVERIFY(!manager.details().contains(QStringLiteral("Network Time")));
        QVERIFY(!manager.details().contains(QStringLiteral("stopped reading")));
        QCOMPARE(starts().size(), 1);
    }

    // A log that takes too long (a large journal on a slow card): a press
    // while it is read starts no second one; at the time limit what it wrote
    // by then is shown, with why there is no more, and it is ended. A press
    // after that reads it again.
    void slowLogStopped() {
        qputenv("FAKE_JOURNAL_STAY", "30");
        QTest::ignoreMessage(QtWarningMsg, "[Bluetooth] journalctl still reading after 5 s: stopped");
        BluetoothManager manager;
        manager.collectDetails();
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 1, 3000);
        const QString pid = starts().first();
        manager.collectDetails();
        manager.collectDetails();
        QTest::qWait(500);
        QCOMPARE(starts().size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(manager.details().contains(QStringLiteral("stopped reading the system log")), 8000);
        QVERIFY(manager.details().contains(QStringLiteral("hci0: BCM: firmware Patch file not found")));
        QTRY_VERIFY_WITH_TIMEOUT(!alive(pid), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(manager.findChildren<QProcess *>().isEmpty(), 3000);

        qputenv("FAKE_JOURNAL_STAY", "");
        manager.collectDetails();
        QTRY_COMPARE_WITH_TIMEOUT(starts().size(), 2, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(manager.details().contains(QStringLiteral("hci0: BCM: firmware Patch file not found")),
                                 3000);
        QVERIFY(!manager.details().contains(QStringLiteral("stopped reading")));
    }
};

QTEST_GUILESS_MAIN(BluetoothTest)
#include "bluetooth_test.moc"
