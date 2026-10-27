#include "system/SystemInfo.h"
#include "util/DeviceName.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QNetworkInterface>
#include <QSignalSpy>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

// Settings → About → System Information (src/system/SystemInfo): its rows of
// what the device is, read from a stand-in for Linux's own files, the names
// the network takes of what is typed, and the device renamed through a
// stand-in for osdos-name.socket. Every name is an invented one.
class SystemInfoTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_root;   // OSDOS_SYSTEM_ROOT: /proc and /sys
    QTemporaryDir m_data;

    void writeFile(const QString &path, const QByteArray &data = QByteArray()) {
        QDir().mkpath(QFileInfo(m_root.path() + path).absolutePath());
        QFile file(m_root.path() + path);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
        file.write(data);
    }

    static QStringList keys(const QVariantList &rows) {
        QStringList keys;
        for (const QVariant &row : rows) {
            const QVariantMap map = row.toMap();
            keys << (map.value(QStringLiteral("type")).toString() == QLatin1String("section")
                         ? QStringLiteral("[") + map.value(QStringLiteral("label")).toString() + QLatin1Char(']')
                         : map.value(QStringLiteral("key")).toString());
        }
        return keys;
    }
    static QVariantMap rowOf(const QVariantList &rows, const QString &key) {
        for (const QVariant &row : rows) {
            if (row.toMap().value(QStringLiteral("key")).toString() == key)
                return row.toMap();
        }
        return {};
    }
    static QString valueOf(const QVariantList &rows, const QString &key) {
        return rowOf(rows, key).value(QStringLiteral("value")).toString();
    }
    static QString descriptionOf(const QVariantList &rows, const QString &key) {
        return rowOf(rows, key).value(QStringLiteral("description")).toString();
    }

private slots:
    void initTestCase() {
        QVERIFY(m_root.isValid());
        QVERIFY(m_data.isValid());
        qputenv("OSDOS_SYSTEM_ROOT", m_root.path().toUtf8());
        qputenv("OSDOS_DEVICE_NAME", "den-tv");
        qputenv("OSDOS_BOARD_MODEL", "Raspberry Pi 5 Model B Rev 1.0");
        qputenv("OSDOS_MEDIA_DIR", (m_data.path() + QStringLiteral("/media")).toUtf8());
        qunsetenv("OSDOS_LAUNCHER_API");
        // A Pi 5's: four Cortex-A76 cores, 8 GB, three days up, 51.6 °C.
        writeFile(QStringLiteral("/proc/cpuinfo"),
                  "processor\t: 0\nBogoMIPS\t: 108.00\nCPU implementer\t: 0x41\nCPU part\t: 0xd0b\n\n"
                  "processor\t: 1\nBogoMIPS\t: 108.00\nCPU implementer\t: 0x41\nCPU part\t: 0xd0b\n\n"
                  "Revision\t: d04170\nModel\t\t: Raspberry Pi 5 Model B Rev 1.0\n");
        writeFile(QStringLiteral("/proc/meminfo"),
                  "MemTotal:        8245100 kB\nMemFree:         1048576 kB\nMemAvailable:    6291456 kB\n");
        writeFile(QStringLiteral("/proc/uptime"), "274325.42 1000000.00\n");
        writeFile(QStringLiteral("/sys/class/thermal/thermal_zone0/temp"), "51600\n");
        writeFile(QStringLiteral("/proc/device-tree/serial-number"), QByteArray("a1b2c3d4e5f60718\0", 17));
        writeFile(QStringLiteral("/usr/lib/os-release"),
                  "PRETTY_NAME=\"Debian GNU/Linux 13 (trixie)\"\nNAME=\"Debian GNU/Linux\"\nVERSION_ID=\"13\"\n");
        writeFile(QStringLiteral("/proc/sys/kernel/osrelease"), "6.12.47+rpt-rpi-2712\n");
    }

    void cleanupTestCase() {
        for (const char *name : { "OSDOS_SYSTEM_ROOT", "OSDOS_DEVICE_NAME", "OSDOS_BOARD_MODEL", "OSDOS_MEDIA_DIR",
                                  "OSDOS_LAUNCHER_API", "OSDOS_RUN_DIR" })
            qunsetenv(name);
    }

    // The page's rows of what the device is, then each connection's IP and
    // MAC addresses under Network, a second of a kind as "Ethernet 2".
    void rows() {
        SystemInfo::Facts facts;
        facts.name = QStringLiteral("den-tv");
        facts.model = QStringLiteral("Raspberry Pi 5 Model B Rev 1.0");
        facts.serial = QStringLiteral("a1b2c3d4e5f60718");
        facts.system = QStringLiteral("Debian GNU/Linux 13 (trixie)");
        facts.kernel = QStringLiteral("Linux 6.12.47+rpt-rpi-2712");
        facts.version = QStringLiteral("0.0.10");
        facts.cpu = QStringLiteral("Cortex-A76");
        facts.arch = QStringLiteral("arm64");
        facts.cores = 4;
        facts.memoryTotal = 8245100LL * 1024;
        facts.memoryFree = 6LL << 30;
        facts.storagePath = QStringLiteral("/home/pi/.local/share/osdos");
        facts.storageTotal = 256LL << 30;
        facts.storageFree = (200LL << 30) + (300LL << 20);
        facts.mediaPath = QStringLiteral("/media/OSDOS");
        facts.mediaTotal = 512LL << 20;
        facts.mediaFree = 100LL << 20;
        facts.temperature = 51.6;
        facts.uptime = 3 * 86400 + 4 * 3600 + 12 * 60 + 59;
        facts.connections = {
            { QStringLiteral("eth0"), false, QStringLiteral("D8:3A:DD:00:00:01"),
              { QStringLiteral("192.168.1.20") }, { QStringLiteral("2001:db8::20") } },
            { QStringLiteral("eth1"), false, QStringLiteral("00:E0:4C:00:00:03"), {}, {} },
            { QStringLiteral("wlan0"), true, QStringLiteral("D8:3A:DD:00:00:02"),
              { QStringLiteral("192.168.1.21") }, {} },
        };
        QVariantList rows = SystemInfo::rowsOf(facts);
        QCOMPARE(keys(rows), QStringList({ "name", "model", "system", "processor", "memory", "storage", "media",
                                           "temperature", "uptime", "[Network]", "ip:eth0", "mac:eth0", "ip:eth1",
                                           "mac:eth1", "ip:wlan0", "mac:wlan0" }));
        QStringList labels;
        for (const QVariant &row : std::as_const(rows))
            labels << row.toMap().value(QStringLiteral("label")).toString();
        QCOMPARE(labels, QStringList({ "Device Name", "Model", "OS", "Processor", "Memory", "Storage",
                                       "Media Storage", "Temperature", "Up For", "Network", "Ethernet IP",
                                       "Ethernet MAC", "Ethernet 2 IP", "Ethernet 2 MAC", "Wi-Fi IP", "Wi-Fi MAC" }));
        QCOMPARE(valueOf(rows, "name"), QStringLiteral("den-tv"));
        QVERIFY(descriptionOf(rows, "name").contains(QStringLiteral(" as den-tv.local ")));
        // A Pi's revision in the help line alone.
        QCOMPARE(valueOf(rows, "model"), QStringLiteral("Raspberry Pi 5 Model B"));
        QCOMPARE(descriptionOf(rows, "model"), QStringLiteral("The board or computer OSD/OS runs on: Raspberry Pi 5 "
                                                              "Model B Rev 1.0 • Serial number a1b2c3d4e5f60718"));
        QCOMPARE(valueOf(rows, "system"), QStringLiteral("Debian GNU/Linux 13 (trixie)"));
        QCOMPARE(descriptionOf(rows, "system"), QStringLiteral("OSD/OS 0.0.10 • Linux 6.12.47+rpt-rpi-2712"));
        QCOMPARE(valueOf(rows, "processor"), QStringLiteral("Cortex-A76, 4 cores"));
        QCOMPARE(descriptionOf(rows, "processor"), QStringLiteral("Its kind and how many cores it has • arm64"));
        // Sizes: a tenth of a gigabyte, whole ones from a hundred, megabytes
        // under one.
        QCOMPARE(valueOf(rows, "memory"), QStringLiteral("7.9 GB"));
        QCOMPARE(descriptionOf(rows, "memory"), QStringLiteral("6.0 GB free now"));
        QCOMPARE(valueOf(rows, "storage"), QStringLiteral("200 GB free"));
        QCOMPARE(descriptionOf(rows, "storage"),
                 QStringLiteral("Of 256 GB, where OSD/OS keeps its settings and what it downloads "
                                "(/home/pi/.local/share/osdos)"));
        QCOMPARE(valueOf(rows, "media"), QStringLiteral("100 MB free"));
        QCOMPARE(descriptionOf(rows, "media"), QStringLiteral("Of 512 MB, where the films and games are (/media/OSDOS)"));
        QCOMPARE(valueOf(rows, "temperature"), QStringLiteral("52 °C"));
        QCOMPARE(valueOf(rows, "uptime"), QStringLiteral("3D 4H 12M"));
        // A connection's first IPv4 address, every one in its line; one
        // without an address not connected.
        QCOMPARE(valueOf(rows, "ip:eth0"), QStringLiteral("192.168.1.20"));
        QCOMPARE(descriptionOf(rows, "ip:eth0"), QStringLiteral("eth0's addresses: 192.168.1.20, 2001:db8::20"));
        QCOMPARE(valueOf(rows, "mac:eth0"), QStringLiteral("D8:3A:DD:00:00:01"));
        QCOMPARE(valueOf(rows, "ip:eth1"), QStringLiteral("Not Connected"));
        QCOMPARE(descriptionOf(rows, "ip:eth1"),
                 QStringLiteral("eth1 has no address: not connected, or not given one yet"));
        QCOMPARE(valueOf(rows, "ip:wlan0"), QStringLiteral("192.168.1.21"));
        QCOMPARE(valueOf(rows, "mac:wlan0"), QStringLiteral("D8:3A:DD:00:00:02"));

        // What isn't known left out: no model, memory, storage, temperature;
        // the processor by its architecture; an IPv6 address alone shown.
        SystemInfo::Facts bare;
        bare.name = QStringLiteral("den-tv");
        bare.system = QStringLiteral("Debian GNU/Linux 13 (trixie)");
        bare.arch = QStringLiteral("x86_64");
        bare.cores = 1;
        bare.uptime = 59 * 60 + 59;
        bare.connections = { { QStringLiteral("eth0"), false, QStringLiteral("02:00:00:00:00:04"), {},
                               { QStringLiteral("2001:db8::4") } } };
        rows = SystemInfo::rowsOf(bare);
        QCOMPARE(keys(rows), QStringList({ "name", "system", "processor", "uptime", "[Network]", "ip:eth0", "mac:eth0" }));
        QCOMPARE(valueOf(rows, "processor"), QStringLiteral("x86_64, 1 core"));
        QCOMPARE(valueOf(rows, "uptime"), QStringLiteral("59M"));
        QCOMPARE(rowOf(rows, "ip:eth0").value(QStringLiteral("label")).toString(), QStringLiteral("Ethernet IP"));
        QCOMPARE(valueOf(rows, "ip:eth0"), QStringLiteral("2001:db8::4"));
        bare.uptime = 3600;
        bare.connections.clear();
        bare.memoryTotal = 512LL << 20;
        rows = SystemInfo::rowsOf(bare);
        QCOMPARE(valueOf(rows, "uptime"), QStringLiteral("1H 0M"));
        QCOMPARE(descriptionOf(rows, "memory"), QStringLiteral("All the memory the system has"));
        QCOMPARE(keys(rows).mid(4), QStringList({ "uptime", "[Network]", "none" }));
        QCOMPARE(rowOf(rows, "none").value(QStringLiteral("label")).toString(), QStringLiteral("No Network"));
    }

    // The system as Linux's files say it, under the stand-in root; the
    // network's devices those with a device of their own in /sys (not a
    // bridge's, a tunnel's or a container's), Wi-Fi where it is wireless.
    void readsTheSystem() {
        const QString media = m_data.path() + QStringLiteral("/media");
        QVERIFY(QDir().mkpath(media));
        SystemInfo::Facts facts = SystemInfo::read(m_data.path(), media);
        QCOMPARE(facts.name, QStringLiteral("den-tv"));
        QCOMPARE(facts.model, QStringLiteral("Raspberry Pi 5 Model B Rev 1.0"));
        QCOMPARE(facts.serial, QStringLiteral("a1b2c3d4e5f60718"));
        QCOMPARE(facts.cpu, QStringLiteral("Cortex-A76"));
        QCOMPARE(facts.memoryTotal, 8245100LL * 1024);
        // As the Arcade module is told it, for its games over the network.
        QCOMPARE(SystemInfo::memoryTotal(), facts.memoryTotal);
        QCOMPARE(facts.memoryFree, 6291456LL * 1024);
        QCOMPARE(facts.uptime, 274325LL);
        QVERIFY(facts.temperature);
        QCOMPARE(*facts.temperature, 51.6);
        QVERIFY(facts.cores >= 1);
        // The system as os-release names it (where /etc has none, as /usr/lib
        // has it), and its kernel.
        QCOMPARE(facts.system, QStringLiteral("Debian GNU/Linux 13 (trixie)"));
        QCOMPARE(facts.kernel, QStringLiteral("Linux 6.12.47+rpt-rpi-2712"));
        // The data folder's drive; the media folder on the same one isn't
        // said twice.
        QCOMPARE(facts.storagePath, m_data.path());
        QVERIFY(facts.storageTotal > 0);
        QVERIFY(facts.storageFree >= 0 && facts.storageFree <= facts.storageTotal);
        QCOMPARE(facts.mediaPath, QString());
        QCOMPARE(facts.mediaTotal, -1LL);

        // None in /sys: none listed, whatever the machine has.
        QVERIFY(facts.connections.isEmpty());
        QStringList names;
        const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface &interface : interfaces) {
            if (!interface.flags().testFlag(QNetworkInterface::IsLoopBack))
                names << interface.name();
        }
        if (names.isEmpty()) {
            qInfo("No network devices on this machine: their rows not checked");
            return;
        }
        // The machine's first as an Ethernet device, its second as a
        // wireless one: Ethernet first in the rows, then Wi-Fi.
        writeFile(QStringLiteral("/sys/class/net/%1/device").arg(names.first()));
        if (names.size() > 1) {
            writeFile(QStringLiteral("/sys/class/net/%1/device").arg(names.at(1)));
            writeFile(QStringLiteral("/sys/class/net/%1/wireless").arg(names.at(1)));
        }
        facts = SystemInfo::read(m_data.path(), media);
        QCOMPARE(facts.connections.size(), qMin(2, int(names.size())));
        const QNetworkInterface first = QNetworkInterface::interfaceFromName(names.first());
        QCOMPARE(facts.connections.at(0).name, names.first());
        QVERIFY(!facts.connections.at(0).wifi);
        QCOMPARE(facts.connections.at(0).mac, first.hardwareAddress());
        // Its addresses while it is up: IPv4 and every IPv6 but link-local.
        QStringList ipv4;
        if (first.flags().testFlag(QNetworkInterface::IsUp) && first.flags().testFlag(QNetworkInterface::IsRunning)) {
            for (const QNetworkAddressEntry &entry : first.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                    ipv4 << entry.ip().toString();
            }
        }
        QCOMPARE(facts.connections.at(0).ipv4, ipv4);
        for (const QString &address : std::as_const(facts.connections.at(0).ipv6))
            QVERIFY2(!QHostAddress(address).isLinkLocal(), qPrintable(address));
        if (names.size() > 1) {
            QCOMPARE(facts.connections.at(1).name, names.at(1));
            QVERIFY(facts.connections.at(1).wifi);
        }
        QDir(m_root.path() + QStringLiteral("/sys/class/net")).removeRecursively();
    }

    // Read again for the page: what changes (the time up, the free memory),
    // while what doesn't (the processor, the memory there is) was read once;
    // the media folder's drive as the given folder's.
    void refresh() {
        SystemInfo info(m_data.path(), [this] { return m_data.path(); });
        QSignalSpy changed(&info, &SystemInfo::changed);
        info.refresh();
        QCOMPARE(changed.size(), 1);
        QCOMPARE(valueOf(info.rows(), "uptime"), QStringLiteral("3D 4H 12M"));
        QCOMPARE(valueOf(info.rows(), "processor").section(QLatin1Char(','), 0, 0), QStringLiteral("Cortex-A76"));
        QVERIFY(rowOf(info.rows(), "media").isEmpty());
        // Nothing new: nothing said.
        info.refresh();
        QCOMPARE(changed.size(), 1);
        writeFile(QStringLiteral("/proc/uptime"), "274400.00 1000000.00\n");
        writeFile(QStringLiteral("/proc/cpuinfo"), "CPU part\t: 0xd08\n");
        writeFile(QStringLiteral("/proc/meminfo"), "MemTotal:        4045824 kB\nMemAvailable:    1048576 kB\n");
        info.refresh();
        QCOMPARE(changed.size(), 2);
        QCOMPARE(valueOf(info.rows(), "uptime"), QStringLiteral("3D 4H 13M"));
        QCOMPARE(descriptionOf(info.rows(), "memory"), QStringLiteral("1.0 GB free now"));
        QCOMPARE(valueOf(info.rows(), "memory"), QStringLiteral("7.9 GB"));
        QCOMPARE(valueOf(info.rows(), "processor").section(QLatin1Char(','), 0, 0), QStringLiteral("Cortex-A76"));
        // A media folder on a drive of its own (a tmpfs here): its row.
        SystemInfo shm(m_data.path(), [] { return QStringLiteral("/dev/shm"); });
        shm.refresh();
        if (QStorageInfo(QStringLiteral("/dev/shm")).device() != QStorageInfo(m_data.path()).device())
            QVERIFY(descriptionOf(shm.rows(), "media").endsWith(QStringLiteral("(/dev/shm)")));
        writeFile(QStringLiteral("/proc/uptime"), "274325.42 1000000.00\n");
        writeFile(QStringLiteral("/proc/cpuinfo"), "CPU part\t: 0xd0b\n");
        writeFile(QStringLiteral("/proc/meminfo"), "MemTotal:        8245100 kB\nMemAvailable:    6291456 kB\n");
    }

    // A name the network takes of what was typed: in small letters, spaces
    // as hyphens; why not, for one it can't take.
    void names() {
        const auto name = [](const QString &typed) {
            QString why;
            const QString made = SystemInfo::hostnameFor(typed, &why);
            return made.isEmpty() ? QStringLiteral("! ") + why : made;
        };
        QCOMPARE(name(QStringLiteral("DEN TV")), QStringLiteral("den-tv"));
        QCOMPARE(name(QStringLiteral("  Garage_Cab  ")), QStringLiteral("garage-cab"));
        QCOMPARE(name(QStringLiteral("ARCADE  2")), QStringLiteral("arcade-2"));
        QCOMPARE(name(QStringLiteral("a")), QStringLiteral("a"));
        QCOMPARE(name(QString(63, QLatin1Char('A'))), QString(63, QLatin1Char('a')));
        QCOMPARE(name(QString()), QStringLiteral("! No name typed"));
        QCOMPARE(name(QStringLiteral("   ")), QStringLiteral("! No name typed"));
        QCOMPARE(name(QString(64, QLatin1Char('A'))), QStringLiteral("! A name has 63 letters at most"));
        for (const char *typed : { "-TV", "TV-", " -TV", "DEN.TV", "DEN'S TV", "TOM & ANN", "ÇAY", "OSD/OS" })
            QCOMPARE(name(QString::fromUtf8(typed)),
                     QStringLiteral("! Letters, digits and hyphens only, and no hyphen first or last"));
        QCOMPARE(SystemInfo::hostnameFor(QStringLiteral("-tv")), QString());
        // The device's own: a Mac's host name without its ".local".
        QCOMPARE(device::name(), QStringLiteral("den-tv"));
        qputenv("OSDOS_DEVICE_NAME", "den-tv.local");
        QCOMPARE(device::name(), QStringLiteral("den-tv"));
        qputenv("OSDOS_DEVICE_NAME", "den-tv");
    }

    // Renamed only where the image's helper is: launcher API 8 and its
    // socket there.
    void renameAvailable() {
        QTemporaryDir run;
        QVERIFY(run.isValid());
        qputenv("OSDOS_RUN_DIR", run.path().toUtf8());
        QLocalServer server;
        QVERIFY(server.listen(run.path() + QStringLiteral("/osdos-name.sock")));
        QVERIFY(!SystemInfo(m_data.path()).renameAvailable());
        qputenv("OSDOS_LAUNCHER_API", "7");
        QVERIFY(!SystemInfo(m_data.path()).renameAvailable());
        qputenv("OSDOS_LAUNCHER_API", "8");
        QVERIFY(SystemInfo(m_data.path()).renameAvailable());
        server.close();
        QVERIFY(!QFileInfo::exists(run.path() + QStringLiteral("/osdos-name.sock")));
        QVERIFY(!SystemInfo(m_data.path()).renameAvailable());
        // Without it: nothing asked, and nothing said.
        SystemInfo info(m_data.path());
        QVERIFY(!info.rename(QStringLiteral("loft")));
        QCOMPARE(info.message(), QString());
        QVERIFY(!info.renaming());
        qunsetenv("OSDOS_LAUNCHER_API");
        qunsetenv("OSDOS_RUN_DIR");
    }

    // The new name to a stand-in for osdos-name.socket, as osdos-stop takes
    // it: a line, "ok" or why not back. The page reads the system again
    // whatever the answer.
    void rename() {
        QTemporaryDir run;
        QVERIFY(run.isValid());
        qputenv("OSDOS_RUN_DIR", run.path().toUtf8());
        qputenv("OSDOS_LAUNCHER_API", "8");
        QLocalServer server;
        QVERIFY(server.listen(run.path() + QStringLiteral("/osdos-name.sock")));
        QByteArrayList asked;
        QByteArray reply = "ok\n";
        connect(&server, &QLocalServer::newConnection, &server, [&server, &asked, &reply] {
            QLocalSocket *socket = server.nextPendingConnection();
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, socket, [socket, &asked, &reply] {
                if (!socket->canReadLine())
                    return;
                asked << socket->readLine();
                // The system renamed, as hostnamectl would.
                if (reply == "ok\n")
                    qputenv("OSDOS_DEVICE_NAME", asked.last().trimmed());
                socket->write(reply);
                socket->flush();
                socket->disconnectFromServer();
            });
        });

        SystemInfo info(m_data.path());
        QVERIFY(info.renameAvailable());
        info.refresh();
        QCOMPARE(info.deviceName(), QStringLiteral("den-tv"));
        QCOMPARE(valueOf(info.rows(), "name"), QStringLiteral("den-tv"));

        // Typed as the keyboard types it, in capitals with a space: sent as
        // a host name, one at a time; the page under the new name.
        QSignalSpy renaming(&info, &SystemInfo::renamingChanged);
        QSignalSpy changed(&info, &SystemInfo::changed);
        QVERIFY(info.rename(QStringLiteral("ATTIC TV")));
        QVERIFY(info.renaming());
        QVERIFY(!info.rename(QStringLiteral("LOFT")));
        QTRY_VERIFY(!info.renaming());
        QCOMPARE(renaming.size(), 2);
        QCOMPARE(asked, QByteArrayList({ "attic-tv\n" }));
        QCOMPARE(info.deviceName(), QStringLiteral("attic-tv"));
        QCOMPARE(valueOf(info.rows(), "name"), QStringLiteral("attic-tv"));
        QVERIFY(!changed.isEmpty());
        QCOMPARE(info.message(), QStringLiteral("Now called attic-tv"));

        // Its own name, or one the network doesn't take: said, and nothing
        // sent.
        QVERIFY(!info.rename(QStringLiteral("Attic TV")));
        QCOMPARE(info.message(), QStringLiteral("It is called attic-tv already"));
        QVERIFY(!info.rename(QStringLiteral("-TV")));
        QCOMPARE(info.message(), QStringLiteral("Letters, digits and hyphens only, and no hyphen first or last"));
        QVERIFY(!info.renaming());
        QCOMPARE(asked.size(), 1);
        info.clearMessage();
        QCOMPARE(info.message(), QString());

        // Refused: why, under the name it had.
        reply = "hostnamectl couldn't set it\n";
        QVERIFY(info.rename(QStringLiteral("LOFT")));
        QTRY_VERIFY(!info.renaming());
        QCOMPARE(asked.last(), QByteArray("loft\n"));
        QCOMPARE(info.message(), QStringLiteral("Couldn't rename it: hostnamectl couldn't set it"));
        QCOMPARE(info.deviceName(), QStringLiteral("attic-tv"));
        // The helper ending without a word.
        reply.clear();
        QVERIFY(info.rename(QStringLiteral("LOFT")));
        QTRY_VERIFY(!info.renaming());
        QCOMPARE(info.message(), QStringLiteral("Couldn't rename it: no answer"));
        // No one listening any more.
        server.close();
        QVERIFY(info.rename(QStringLiteral("LOFT")));
        QTRY_VERIFY(!info.renaming());
        QCOMPARE(info.message(), QStringLiteral("Couldn't rename it: osdos-name.socket isn't listening"));
        QCOMPARE(info.deviceName(), QStringLiteral("attic-tv"));

        qputenv("OSDOS_DEVICE_NAME", "den-tv");
        qunsetenv("OSDOS_LAUNCHER_API");
        qunsetenv("OSDOS_RUN_DIR");
    }
};

QTEST_GUILESS_MAIN(SystemInfoTest)
#include "system_info_test.moc"
