#include "boot/BootProgress.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

// The OSD/OS image's boot screen (src/boot/BootProgress): the services it
// lists from a unit list as systemd reports them, through a stand-in for
// systemctl on PATH. An optional step (OSD/OS Setup's SETTINGS) is listed
// only once its unit starts, and counts for nothing until then.
class BootProgressTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;   // the stand-in systemctl, its units' states, the unit list

    QString path(const QString &name) const { return m_dir.path() + QLatin1Char('/') + name; }

    void write(const QString &name, const QByteArray &data) {
        QFile file(path(name));
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(name));
        file.write(data);
    }

    // A unit as systemctl show has it: inactive and not yet looked at unless
    // said otherwise; skipped, its condition checked and failed.
    void setUnit(const QString &unit, const char *activeState, bool skipped = false) {
        write(QStringLiteral("units/") + unit,
              QByteArray("LoadState=loaded\nActiveState=") + activeState
                  + (skipped ? "\nConditionResult=no\nConditionTimestampMonotonic=123\n"
                             : "\nConditionResult=yes\nConditionTimestampMonotonic=0\n"));
    }

    static QStringList shown(const BootProgress &boot) {
        QStringList lines;
        for (const QVariant &step : boot.steps()) {
            const QVariantMap map = step.toMap();
            lines << map.value(QStringLiteral("label")).toString() + QLatin1Char(' ')
                         + map.value(QStringLiteral("state")).toString();
        }
        return lines;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        QVERIFY(QDir(m_dir.path()).mkpath(QStringLiteral("units")));
        // is-system-running: the line in system-state. show: a block a unit,
        // in the order asked, from units/<unit>.
        write(QStringLiteral("systemctl"), R"SH(#!/bin/sh
dir="$(dirname "$0")"
case "$1" in
is-system-running) cat "$dir/system-state" ;;
show)
    while [ "$1" != "--" ]; do shift; done
    shift
    first=1
    for unit in "$@"; do
        [ "$first" = 1 ] || echo
        first=0
        echo "Id=$unit"
        cat "$dir/units/$unit"
    done ;;
*) exit 1 ;;
esac
)SH");
        QFile::setPermissions(path(QStringLiteral("systemctl")),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("PATH", m_dir.path().toLocal8Bit() + ':' + qgetenv("PATH"));
        qputenv("OSDOS_BOOT_UNITS_FILE", path(QStringLiteral("boot-units")).toLocal8Bit());
        write(QStringLiteral("boot-units"),
              "# As the image writes it.\n"
              "osdos-setup.service|SETTINGS|optional\n"
              "NetworkManager.service|WI-FI\n"
              "avahi-daemon.service|LOCAL NETWORK\n");
    }

    void init() {
        write(QStringLiteral("system-state"), "starting\n");
        for (const QString &unit : {QStringLiteral("osdos-setup.service"), QStringLiteral("NetworkManager.service"),
                                    QStringLiteral("avahi-daemon.service")})
            setUnit(unit, "inactive");
    }

    // A start with OSD/OS Setup's file: SETTINGS comes in as it starts, first,
    // and stays with its result.
    void optionalListedOnceItStarts() {
        BootProgress boot;
        QVERIFY(boot.active());
        QCOMPARE(shown(boot), (QStringList{"WI-FI pending", "LOCAL NETWORK pending"}));
        QCOMPARE(boot.currentLabel(), QStringLiteral("WI-FI"));
        QCOMPARE(boot.progress(), 0.0);

        setUnit(QStringLiteral("osdos-setup.service"), "activating");
        QTRY_COMPARE(shown(boot), (QStringList{"SETTINGS running", "WI-FI pending", "LOCAL NETWORK pending"}));
        QCOMPARE(boot.currentLabel(), QStringLiteral("SETTINGS"));

        setUnit(QStringLiteral("osdos-setup.service"), "failed");
        QTRY_COMPARE(shown(boot), (QStringList{"SETTINGS failed", "WI-FI pending", "LOCAL NETWORK pending"}));
        QCOMPARE(boot.progress(), 1.0 / 3);

        setUnit(QStringLiteral("NetworkManager.service"), "active");
        setUnit(QStringLiteral("avahi-daemon.service"), "active");
        QTRY_COMPARE(shown(boot), (QStringList{"SETTINGS failed", "WI-FI done", "LOCAL NETWORK done"}));
        QCOMPARE(boot.progress(), 1.0);
        QTRY_VERIFY(!boot.active());
    }

    // Any other start: its condition fails, and it is never listed nor
    // counted.
    void optionalSkippedNeverListed() {
        setUnit(QStringLiteral("osdos-setup.service"), "inactive", true);
        BootProgress boot;
        QCOMPARE(shown(boot), (QStringList{"WI-FI pending", "LOCAL NETWORK pending"}));
        setUnit(QStringLiteral("NetworkManager.service"), "active");
        QTRY_COMPARE(boot.progress(), 0.5);
        setUnit(QStringLiteral("avahi-daemon.service"), "active");
        QTRY_COMPARE(boot.progress(), 1.0);
        QCOMPARE(shown(boot), (QStringList{"WI-FI done", "LOCAL NETWORK done"}));
        QTRY_VERIFY(!boot.active());
    }

    // Already starting as the app does: in the first frame, read before it.
    void firstFrameListsWhatHasStarted() {
        setUnit(QStringLiteral("osdos-setup.service"), "activating");
        BootProgress boot;
        QCOMPARE(shown(boot), (QStringList{"SETTINGS running", "WI-FI pending", "LOCAL NETWORK pending"}));
        QCOMPARE(boot.currentLabel(), QStringLiteral("SETTINGS"));
    }

    // Listed late, it doesn't take the bar back; the screen waits for it.
    void listedLateNeverTakesTheBarBack() {
        BootProgress boot;
        setUnit(QStringLiteral("NetworkManager.service"), "active");
        setUnit(QStringLiteral("avahi-daemon.service"), "active");
        QTRY_COMPARE(boot.progress(), 1.0);
        setUnit(QStringLiteral("osdos-setup.service"), "activating");
        QTRY_COMPARE(shown(boot).value(0), QStringLiteral("SETTINGS running"));
        QCOMPARE(boot.progress(), 1.0);
        QTest::qWait(1600);
        QVERIFY2(boot.active(), "closed with a step still starting");
        setUnit(QStringLiteral("osdos-setup.service"), "active");
        QTRY_VERIFY(!boot.active());
    }

    // A flag it doesn't know: said, and the step listed as any other.
    void unknownFlag() {
        write(QStringLiteral("boot-units"), "NetworkManager.service|WI-FI|later\n");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("unknown flag .*: later")));
        BootProgress boot;
        QCOMPARE(shown(boot), (QStringList{"WI-FI pending"}));
        write(QStringLiteral("boot-units"),
              "osdos-setup.service|SETTINGS|optional\n"
              "NetworkManager.service|WI-FI\n"
              "avahi-daemon.service|LOCAL NETWORK\n");
    }

    // Once the system has started, a start of the app (an update, Exit to
    // Terminal and back) shows no boot screen.
    void notWhenStarted() {
        write(QStringLiteral("system-state"), "running\n");
        BootProgress boot;
        QVERIFY(!boot.active());
    }
};

QTEST_GUILESS_MAIN(BootProgressTest)
#include "boot_progress_test.moc"
