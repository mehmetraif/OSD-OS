#include "system/SystemSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

// Settings → System's Time Zone and Keyboard (SystemSettings), on a stand-in
// for the system's files (OSDOS_SYSTEM_ROOT) and for osdos-system.socket: what
// the system has, the zones and layouts offered, and a new one sent as
// osdos-stop takes it.
class SystemSettingsTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_root;

    void write(const QString &path, const QByteArray &text) {
        const QString full = m_root.path() + path;
        QVERIFY(QDir().mkpath(QFileInfo(full).path()));
        QFile file(full);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text);
    }
    // /etc/localtime linked into zoneinfo, as timedatectl leaves it.
    void zone(const QString &name) {
        const QString link = m_root.path() + QStringLiteral("/etc/localtime");
        QFile::remove(link);
        QVERIFY(QFile::link(m_root.path() + QStringLiteral("/usr/share/zoneinfo/") + name, link));
    }
    void layout(const QByteArray &code) {
        write(QStringLiteral("/etc/default/keyboard"),
              "# KEYBOARD CONFIGURATION FILE\nXKBMODEL=\"pc105\"\nXKBLAYOUT=\"" + code + "\"\n");
    }
    static QStringList named(const QVariantList &list, const char *key) {
        QStringList out;
        for (const QVariant &v : list)
            out << v.toMap().value(QLatin1String(key)).toString();
        return out;
    }
    // The page's layouts as the source has them, in place of the app's copy.
    static void useSourceKeyboards(SystemSettings &settings) {
        settings.m_keyboardsFile = QStringLiteral(SOURCE_DIR "/site/setup/schema-extra.json");
        settings.m_keyboards.reset();
    }

private slots:
    void initTestCase() {
        QVERIFY(m_root.isValid());
        qputenv("OSDOS_SYSTEM_ROOT", m_root.path().toUtf8());
        // The time zone database, in short: zone.tab's lines and the zones'
        // files, with one of tzdata's older names that no country lists.
        write(QStringLiteral("/usr/share/zoneinfo/zone.tab"),
              "# tzdb timezone descriptions (deprecated version)\n"
              "#country-\n#code\tcoordinates\tTZ\tcomments\n"
              "TR\t+4101+02858\tEurope/Istanbul\n"
              "AR\t-3436-05827\tAmerica/Argentina/Buenos_Aires\tBuenos Aires (BA, CF)\n"
              "DE\t+5230+01322\tEurope/Berlin\tmost of Germany\n"
              "JP\t+353916+1394441\tAsia/Tokyo\n"
              "XX\t+0000+00000\t../etc/shadow\n");
        for (const char *name : { "Europe/Istanbul", "America/Argentina/Buenos_Aires", "Europe/Berlin", "Asia/Tokyo",
                                  "UTC", "Turkey" })
            write(QStringLiteral("/usr/share/zoneinfo/") + QLatin1String(name), "TZif");
        // XKB's layouts: three of the page's, and one it doesn't list.
        for (const char *code : { "us", "tr", "de", "ara" })
            write(QStringLiteral("/usr/share/X11/xkb/symbols/") + QLatin1String(code), "xkb_symbols");
    }
    void cleanupTestCase() { qunsetenv("OSDOS_SYSTEM_ROOT"); }

    // What the system has: /etc/localtime's zone, else /etc/timezone's, and
    // the first of XKBLAYOUT's layouts, by its name in the page's list.
    void reads() {
        QFile::remove(m_root.path() + QStringLiteral("/etc/localtime"));
        QFile::remove(m_root.path() + QStringLiteral("/etc/timezone"));
        QFile::remove(m_root.path() + QStringLiteral("/etc/default/keyboard"));
        SystemSettings none;
        QCOMPARE(none.timeZone(), QString());
        QCOMPARE(none.keyboard(), QString());
        QCOMPARE(none.keyboardName(), QString());

        write(QStringLiteral("/etc/timezone"), "Asia/Tokyo\n");
        layout("tr,us");
        SystemSettings settings;
        useSourceKeyboards(settings);
        QCOMPARE(settings.timeZone(), QStringLiteral("Asia/Tokyo"));
        QCOMPARE(settings.keyboard(), QStringLiteral("tr"));
        QCOMPARE(settings.keyboardName(), QStringLiteral("Turkish (Q)"));
        // The link, where there is one, and read again as it changes.
        QSignalSpy changed(&settings, &SystemSettings::changed);
        zone(QStringLiteral("Europe/Istanbul"));
        settings.refresh();
        QCOMPARE(settings.timeZone(), QStringLiteral("Europe/Istanbul"));
        QCOMPARE(changed.size(), 1);
        settings.refresh();
        QCOMPARE(changed.size(), 1);
        // A layout the page doesn't list: its code.
        layout("ara");
        settings.refresh();
        QCOMPARE(settings.keyboardName(), QStringLiteral("ara"));
    }

    // The zones each country has and UTC, by name, each with its region; the
    // page's layouts that the system has.
    void lists() {
        SystemSettings settings;
        useSourceKeyboards(settings);
        QCOMPARE(named(settings.timeZones(), "name"),
                 QStringList({ QStringLiteral("America/Argentina/Buenos_Aires"), QStringLiteral("Asia/Tokyo"),
                               QStringLiteral("Europe/Berlin"), QStringLiteral("Europe/Istanbul"),
                               QStringLiteral("UTC") }));
        QCOMPARE(named(settings.timeZones(), "region"),
                 QStringList({ QStringLiteral("America"), QStringLiteral("Asia"), QStringLiteral("Europe"),
                               QStringLiteral("Europe"), QStringLiteral("UTC") }));
        QCOMPARE(named(settings.keyboards(), "code"),
                 QStringList({ QStringLiteral("us"), QStringLiteral("tr"), QStringLiteral("de") }));
        QCOMPARE(named(settings.keyboards(), "name").value(1), QStringLiteral("Turkish (Q)"));
    }

    // Changed only where the image's helper is: launcher API 10 and its
    // socket there.
    void available() {
        QTemporaryDir run;
        QVERIFY(run.isValid());
        qputenv("OSDOS_RUN_DIR", run.path().toUtf8());
        QLocalServer server;
        QVERIFY(server.listen(run.path() + QStringLiteral("/osdos-system.sock")));
        QVERIFY(!SystemSettings().available());
        qputenv("OSDOS_LAUNCHER_API", "9");
        QVERIFY(!SystemSettings().available());
        qputenv("OSDOS_LAUNCHER_API", "10");
        QVERIFY(SystemSettings().available());
        server.close();
        QVERIFY(!SystemSettings().available());
        // Without it: nothing asked, and nothing said.
        SystemSettings settings;
        QVERIFY(!settings.setTimeZone(QStringLiteral("Europe/Berlin")));
        QCOMPARE(settings.message(), QString());
        QVERIFY(!settings.busy());
        qunsetenv("OSDOS_LAUNCHER_API");
        qunsetenv("OSDOS_RUN_DIR");
    }

    // A zone or a layout of the lists to a stand-in for osdos-system.socket,
    // as osdos-stop takes it: which and its value on a line, "ok" or why not
    // back. The system is read again whatever the answer.
    void sets() {
        QTemporaryDir run;
        QVERIFY(run.isValid());
        qputenv("OSDOS_RUN_DIR", run.path().toUtf8());
        qputenv("OSDOS_LAUNCHER_API", "10");
        zone(QStringLiteral("Europe/Istanbul"));
        layout("us");
        QLocalServer server;
        QVERIFY(server.listen(run.path() + QStringLiteral("/osdos-system.sock")));
        QByteArrayList asked;
        QByteArray reply = "ok\n";
        connect(&server, &QLocalServer::newConnection, &server, [this, &server, &asked, &reply] {
            QLocalSocket *socket = server.nextPendingConnection();
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, socket, [this, socket, &asked, &reply] {
                if (!socket->canReadLine())
                    return;
                asked << socket->readLine();
                // The system changed, as timedatectl and raspi-config would.
                const QByteArray line = asked.last().trimmed();
                if (reply == "ok\n" && line.startsWith("time-zone "))
                    zone(QString::fromLatin1(line.mid(10)));
                if (reply == "ok\n" && line.startsWith("keyboard "))
                    layout(line.mid(9));
                socket->write(reply);
                socket->flush();
                socket->disconnectFromServer();
            });
        });

        SystemSettings settings;
        useSourceKeyboards(settings);
        QVERIFY(settings.available());
        QSignalSpy busy(&settings, &SystemSettings::busyChanged);
        QVERIFY(settings.setTimeZone(QStringLiteral("Asia/Tokyo")));
        QVERIFY(settings.busy());
        // One at a time.
        QVERIFY(!settings.setKeyboard(QStringLiteral("tr")));
        QTRY_VERIFY(!settings.busy());
        QCOMPARE(busy.size(), 2);
        QCOMPARE(asked, QByteArrayList({ "time-zone Asia/Tokyo\n" }));
        QCOMPARE(settings.timeZone(), QStringLiteral("Asia/Tokyo"));
        QCOMPARE(settings.message(), QStringLiteral("Time zone set"));

        QVERIFY(settings.setKeyboard(QStringLiteral("tr")));
        QTRY_VERIFY(!settings.busy());
        QCOMPARE(asked.last(), QByteArray("keyboard tr\n"));
        QCOMPARE(settings.keyboard(), QStringLiteral("tr"));
        QCOMPARE(settings.message(), QStringLiteral("Keyboard set"));

        // The one it has already, or one that is not of the lists (an older
        // name of the zone's, a layout the page doesn't offer, a command):
        // nothing sent.
        QVERIFY(!settings.setTimeZone(QStringLiteral("Asia/Tokyo")));
        QVERIFY(!settings.setTimeZone(QStringLiteral("Turkey")));
        QVERIFY(!settings.setTimeZone(QStringLiteral("Europe/Berlin; reboot")));
        QVERIFY(!settings.setTimeZone(QStringLiteral("../etc/shadow")));
        QVERIFY(!settings.setKeyboard(QStringLiteral("tr")));
        QVERIFY(!settings.setKeyboard(QStringLiteral("ara")));
        QVERIFY(!settings.setKeyboard(QStringLiteral("us\nkeyboard de")));
        QCOMPARE(asked.size(), 2);

        // Refused: why, on what it had.
        reply = "timedatectl couldn't set it\n";
        QVERIFY(settings.setTimeZone(QStringLiteral("Europe/Berlin")));
        QTRY_VERIFY(!settings.busy());
        QCOMPARE(asked.last(), QByteArray("time-zone Europe/Berlin\n"));
        QCOMPARE(settings.message(), QStringLiteral("Time zone not set: timedatectl couldn't set it"));
        QCOMPARE(settings.timeZone(), QStringLiteral("Asia/Tokyo"));
        settings.clearMessage();
        QCOMPARE(settings.message(), QString());
        // No one listening any more.
        server.close();
        QVERIFY(settings.setKeyboard(QStringLiteral("de")));
        QTRY_VERIFY(!settings.busy());
        QCOMPARE(settings.message(), QStringLiteral("Keyboard not set: osdos-system.socket isn't listening"));
        QCOMPARE(settings.keyboard(), QStringLiteral("tr"));

        qunsetenv("OSDOS_LAUNCHER_API");
        qunsetenv("OSDOS_RUN_DIR");
    }
};

QTEST_GUILESS_MAIN(SystemSettingsTest)
#include "system_settings_test.moc"
