// Settings → Wi-Fi's backend (src/wifi/WifiManager) against a stand-in for
// NetworkManager (fake_networkmanager) on a private D-Bus, a dbus-daemon of
// the test's own: NetworkManager away, starting later and stopping; the device
// and its state; the networks as the page lists them; Wi-Fi turned on and off
// and the search; joining a network (new, saved, saved with a new password,
// hidden, with a wrong password, in place of one on its way), leaving it and
// forgetting it; and the country, read from a stand-in boot partition and set
// through a stand-in for osdos-wifi.socket. Skipped without dbus-daemon.
#include "fake_networkmanager.h"
#include "wifi/WifiManager.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <memory>

using Security = FakeNetworkManager::Security;
using Hidden = FakeNetworkManager::Hidden;

namespace {

// A few lines of the time zone database's list of countries, the one
// raspi-config checks a code against, in its order (by code).
const char kIso3166[] =
    "# ISO 3166 alpha-2 country codes\n"
    "#\n"
    "# This file is in the public domain.\n"
    "#\n"
    "#country-\n"
    "#code\tname of country, territory, area, or subdivision\n"
    "AD\tAndorra\n"
    "AL\tAlbania\n"
    "AX\tÅland Islands\n"
    "CI\tCôte d'Ivoire\n"
    "CW\tCuraçao\n"
    "GB\tBritain (UK)\n"
    "TR\tTurkey\n"
    "US\tUnited States\n";

// A network as the page lists it: its name, bars and security ("-" for
// none), then what holds of it.
QString line(const QVariantMap &network) {
    const QString security = network.value(QStringLiteral("security")).toString();
    QString text = network.value(QStringLiteral("ssid")).toString() + QLatin1Char(' ')
                   + QString::number(network.value(QStringLiteral("bars")).toInt()) + QLatin1Char(' ')
                   + (security.isEmpty() ? QStringLiteral("-") : security);
    for (const char *flag : { "secured", "known", "heard", "active" }) {
        if (network.value(QLatin1String(flag)).toBool())
            text += QLatin1Char(' ') + QLatin1String(flag);
    }
    const QString busy = network.value(QStringLiteral("busy")).toString();
    if (!busy.isEmpty())
        text += QLatin1Char(' ') + busy;
    return text;
}

QStringList lines(const QVariantList &networks) {
    QStringList out;
    for (const QVariant &network : networks)
        out << line(network.toMap());
    return out;
}

// A map as its "key=value" pairs, by key.
QString describe(const QVariantMap &map) {
    QStringList pairs;
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        pairs << it.key() + QLatin1Char('=') + it.value().toString();
    return pairs.join(QStringLiteral(", "));
}

QStringList countryLines(const QVariantList &countries) {
    QStringList out;
    for (const QVariant &country : countries) {
        const QVariantMap c = country.toMap();
        out << c.value(QStringLiteral("code")).toString() + QLatin1Char(' ') + c.value(QStringLiteral("name")).toString()
                   + QLatin1Char(' ') + c.value(QStringLiteral("letter")).toString();
    }
    return out;
}

QStringList pathsOf(const QList<FakeNetworkManager::Call> &calls) {
    QStringList paths;
    for (const FakeNetworkManager::Call &call : calls)
        paths << call.path;
    return paths;
}

} // namespace

class WifiTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_busDir;
    QTemporaryDir m_boot;
    QTemporaryDir m_run;
    QTemporaryDir m_zoneinfo;
    QProcess m_daemon;
    QDBusConnection m_fakeBus{ QString() };
    QDBusConnection m_wifiBus{ QString() };
    std::unique_ptr<FakeNetworkManager> m_nm;
    bool m_hadTz = false;
    QByteArray m_tz;

    QString cmdlinePath() const { return m_boot.filePath(QStringLiteral("cmdline.txt")); }
    QString socketPath() const { return m_run.filePath(QStringLiteral("osdos-wifi.sock")); }

    // The boot partition's cmdline.txt, with the country raspi-config set
    // ("" for none).
    void writeCmdline(const QString &country) {
        QByteArray text = "console=serial0,115200 console=tty1 root=PARTUUID=4e639091-02 rootfstype=ext4 "
                          "fsck.repair=yes rootwait quiet";
        if (!country.isEmpty())
            text += " cfg80211.ieee80211_regdom=" + country.toLatin1();
        QFile file(cmdlinePath());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text + '\n');
    }

    void restoreTz() {
        if (m_hadTz)
            qputenv("TZ", m_tz);
        else
            qunsetenv("TZ");
    }

    std::unique_ptr<WifiManager> makeWifi() { return std::make_unique<WifiManager>(m_wifiBus); }

    // Whether WifiManager has read NetworkManager's objects, and the settings
    // of every saved connection: each Wi-Fi network saved (not a hotspot)
    // listed as known.
    bool loaded(const WifiManager &wifi) const {
        QSet<QString> saved;
        for (const QString &path : m_nm->savedConnections()) {
            const QVariantMap settings = m_nm->settings(path);
            const QVariantMap wireless = settings.value(QStringLiteral("802-11-wireless")).toMap();
            if (settings.value(QStringLiteral("connection")).toMap().value(QStringLiteral("type")).toString()
                    == QLatin1String("802-11-wireless")
                && wireless.value(QStringLiteral("mode")).toString() != QLatin1String("ap"))
                saved.insert(QString::fromUtf8(wireless.value(QStringLiteral("ssid")).toByteArray()));
        }
        return QTest::qWaitFor([&] {
            if (!wifi.available())
                return false;
            QSet<QString> known;
            for (const QVariant &network : wifi.networks()) {
                if (network.toMap().value(QStringLiteral("known")).toBool())
                    known.insert(network.toMap().value(QStringLiteral("ssid")).toString());
            }
            return known == saved;
        }, 5000);
    }

    // Every call WifiManager has made by now has reached the stand-in, and
    // every signal the stand-in sent before has reached WifiManager: a call
    // of the test's own on WifiManager's connection comes after the first,
    // and its answer after the second.
    bool sync() {
        QDBusMessage get = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.NetworkManager"),
                                                          QStringLiteral("/org/freedesktop/NetworkManager"),
                                                          QStringLiteral("org.freedesktop.DBus.Properties"),
                                                          QStringLiteral("Get"));
        get << QStringLiteral("org.freedesktop.NetworkManager") << QStringLiteral("Version");
        QDBusPendingCallWatcher watcher(m_wifiBus.asyncCall(get));
        QSignalSpy finished(&watcher, &QDBusPendingCallWatcher::finished);
        return finished.wait(5000) && !watcher.isError();
    }

    // osdos-wifi.socket as the image has it, osdos-stop wifi-country behind
    // it: the code on a line, written to cmdline.txt as raspi-config writes
    // it, and "ok" back. With refusal set, that instead, and nothing written.
    struct CountryHelper {
        WifiTest *test;
        QLocalServer server;
        QByteArray refusal;
        QByteArrayList asked;

        explicit CountryHelper(WifiTest *t) : test(t) {
            QLocalServer::removeServer(t->socketPath());
            server.listen(t->socketPath());
            QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
                QLocalSocket *c = server.nextPendingConnection();
                const auto take = [this, c] {
                    if (!c->canReadLine())
                        return;
                    const QByteArray code = c->readLine().trimmed();
                    asked << code;
                    if (refusal.isEmpty())
                        test->writeCmdline(QString::fromLatin1(code));
                    c->write(refusal.isEmpty() ? QByteArray("ok\n") : refusal + '\n');
                    c->flush();
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
        const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
        if (daemon.isEmpty())
            QSKIP("dbus-daemon isn't installed (Debian's dbus-daemon package)");
        QVERIFY(m_busDir.isValid() && m_boot.isValid() && m_run.isValid() && m_zoneinfo.isValid());

        // A bus of the test's own, with nothing on it to start.
        const QString config = m_busDir.filePath(QStringLiteral("bus.conf"));
        QFile conf(config);
        QVERIFY(conf.open(QIODevice::WriteOnly));
        conf.write(QStringLiteral(
            "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
            " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
            "<busconfig>\n"
            "  <type>session</type>\n"
            "  <listen>unix:path=%1</listen>\n"
            "  <auth>EXTERNAL</auth>\n"
            "  <policy context=\"default\">\n"
            "    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n"
            "    <allow eavesdrop=\"true\"/>\n"
            "    <allow own=\"*\"/>\n"
            "  </policy>\n"
            "</busconfig>\n").arg(m_busDir.filePath(QStringLiteral("bus"))).toUtf8());
        conf.close();
        m_daemon.start(daemon, { QStringLiteral("--config-file=") + config, QStringLiteral("--nofork"),
                                 QStringLiteral("--print-address=1") });
        QVERIFY2(m_daemon.waitForStarted(5000), qPrintable(m_daemon.errorString()));
        while (!m_daemon.canReadLine() && m_daemon.waitForReadyRead(5000)) {
        }
        const QString address = QString::fromUtf8(m_daemon.readLine()).trimmed();
        QVERIFY2(!address.isEmpty(), m_daemon.readAllStandardError().constData());

        // The stand-in on a connection of its own, WifiManager on another, as
        // two programs would be.
        m_fakeBus = QDBusConnection::connectToBus(address, QStringLiteral("fake"));
        m_wifiBus = QDBusConnection::connectToBus(address, QStringLiteral("wifi"));
        QVERIFY2(m_fakeBus.isConnected(), qPrintable(m_fakeBus.lastError().message()));
        QVERIFY2(m_wifiBus.isConnected(), qPrintable(m_wifiBus.lastError().message()));

        // The boot partition, /run and the time zone database's list, before
        // any WifiManager: the countries are read once in a process.
        QFile tab(m_zoneinfo.filePath(QStringLiteral("iso3166.tab")));
        QVERIFY(tab.open(QIODevice::WriteOnly));
        tab.write(kIso3166);
        tab.close();
        for (const char *name : { "MP240_BOOT_DIR", "MP240_RUN_DIR", "MP240_ZONEINFO_DIR" })
            qunsetenv(name);
        qputenv("OSDOS_BOOT_DIR", m_boot.path().toUtf8());
        qputenv("OSDOS_RUN_DIR", m_run.path().toUtf8());
        qputenv("OSDOS_ZONEINFO_DIR", m_zoneinfo.path().toUtf8());
        m_hadTz = qEnvironmentVariableIsSet("TZ");
        m_tz = qgetenv("TZ");
    }

    void cleanupTestCase() {
        m_nm.reset();
        QDBusConnection::disconnectFromBus(QStringLiteral("fake"));
        QDBusConnection::disconnectFromBus(QStringLiteral("wifi"));
        if (m_daemon.state() != QProcess::NotRunning) {
            m_daemon.terminate();
            m_daemon.waitForFinished(5000);
        }
    }

    // Each test with a NetworkManager of its own, started once its scenario
    // is set; run by hand (no OSDOS_AUTOSTART); the country GB.
    void init() {
        m_nm = std::make_unique<FakeNetworkManager>();
        for (const char *name : { "OSDOS_AUTOSTART", "OSDOS_LAUNCHER_API", "MP240_AUTOSTART", "MP240_LAUNCHER_API" })
            qunsetenv(name);
        restoreTz();
        writeCmdline(QStringLiteral("GB"));
        QLocalServer::removeServer(socketPath());
    }

    void cleanup() {
        m_nm.reset();
        restoreTz();
    }

    // NetworkManager isn't there: nothing is, and nothing is asked of it. It
    // starts after the app, as on the image, stops, and starts again.
    void networkManagerComesAndGoes() {
        m_nm->addAccessPoint(QStringLiteral("Cafe"), 70, Security::Wpa, QStringLiteral("espresso-42"));
        auto wifi = makeWifi();
        QVERIFY(!wifi->available());
        QVERIFY(!wifi->enabled());
        QCOMPARE(wifi->state(), QString());
        QVERIFY(wifi->current().isEmpty());
        QVERIFY(wifi->networks().isEmpty());
        wifi->setEnabled(false);
        wifi->startSearch();
        wifi->connectTo(QStringLiteral("Cafe"), QStringLiteral("espresso-42"));
        wifi->connectHidden(QStringLiteral("Attic"), QString());
        wifi->disconnectNetwork();
        wifi->forget(QStringLiteral("Cafe"));
        QVERIFY(!wifi->searching());
        QCOMPARE(wifi->message(), QString());

        QVERIFY(m_nm->start(m_fakeBus));
        QTRY_VERIFY(wifi->available());
        QVERIFY(wifi->enabled());
        QCOMPARE(wifi->state(), QStringLiteral("disconnected"));
        QCOMPARE(lines(wifi->networks()), QStringList{ "Cafe 3 wpa secured heard" });
        QCOMPARE(m_nm->calls().first().method, QStringLiteral("GetManagedObjects"));

        m_nm->stop();
        QTRY_VERIFY(!wifi->available());
        QVERIFY(!wifi->enabled());
        QCOMPARE(wifi->state(), QString());
        QVERIFY(wifi->networks().isEmpty());

        m_nm->clearCalls();
        QVERIFY(m_nm->start(m_fakeBus));
        QTRY_VERIFY(wifi->available());
        QCOMPARE(lines(wifi->networks()), QStringList{ "Cafe 3 wpa secured heard" });
        QCOMPARE(m_nm->calls().first().method, QStringLiteral("GetManagedObjects"));
    }

    // wlan0 is the device: not the loopback, Ethernet or Wi-Fi P2P one, nor
    // a dongle's wlan1. Its state: off by the radio switch, blocked by a
    // switch on the hardware whatever the radio switch says, starting until
    // the device is ready, disconnected, and connected to a network
    // NetworkManager joined by itself.
    void deviceAndState() {
        m_nm->addDongle();
        m_nm->addAccessPoint(QStringLiteral("Cafe"), 70, Security::Wpa, QStringLiteral("espresso-42"));
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        QCOMPARE(wifi->state(), QStringLiteral("disconnected"));
        QVERIFY(wifi->enabled());
        QVERIFY(wifi->current().isEmpty());
        wifi->startSearch();
        QTRY_COMPARE(m_nm->calls(QStringLiteral("RequestScan")).size(), 1);
        QCOMPARE(m_nm->calls(QStringLiteral("RequestScan")).first().path, m_nm->devicePath());
        wifi->stopSearch();

        m_nm->setWirelessEnabled(false);
        QTRY_COMPARE(wifi->state(), QStringLiteral("off"));
        QVERIFY(wifi->available());
        QVERIFY(!wifi->enabled());
        QTRY_VERIFY(wifi->networks().isEmpty());

        m_nm->setHeld(true);
        m_nm->setWirelessEnabled(true);
        QTRY_COMPARE(wifi->state(), QStringLiteral("starting"));
        QVERIFY(wifi->enabled());
        m_nm->setHeld(false);
        QTRY_COMPARE(wifi->state(), QStringLiteral("disconnected"));
        QTRY_COMPARE(lines(wifi->networks()), QStringList{ "Cafe 3 wpa secured heard" });

        m_nm->setHardwareBlocked(true);
        QTRY_COMPARE(wifi->state(), QStringLiteral("blocked"));
        QTRY_VERIFY(wifi->networks().isEmpty());
        m_nm->setWirelessEnabled(false);
        QTRY_VERIFY(!wifi->enabled());
        QCOMPARE(wifi->state(), QStringLiteral("blocked"));
        m_nm->setHardwareBlocked(false);
        QTRY_COMPARE(wifi->state(), QStringLiteral("off"));
        m_nm->setWirelessEnabled(true);
        QTRY_COMPARE(wifi->state(), QStringLiteral("disconnected"));

        m_nm->join(m_nm->addSaved(QStringLiteral("Cafe"), QStringLiteral("espresso-42")));
        QTRY_COMPARE(wifi->state(), QStringLiteral("connected"));
        QCOMPARE(describe(wifi->current()), QStringLiteral("address=192.168.1.23, bars=3, ssid=Cafe"));
        QTRY_COMPARE(lines(wifi->networks()), QStringList{ "Cafe 3 wpa secured known heard active" });
    }

    // The networks: a line a name, from its strongest access point; hidden
    // ones (an empty SSID, or zero bytes) left out; each kind of security;
    // the network joined first, then the saved ones, those out of reach after
    // those heard, then the others by bars, then by name. Saved connections
    // that aren't Wi-Fi networks joined (Ethernet's, a hotspot) aren't listed.
    void networkList() {
        const QVariantMap wired{ { QStringLiteral("id"), QStringLiteral("Wired connection 1") },
                                 { QStringLiteral("type"), QStringLiteral("802-3-ethernet") } };
        m_nm->addConnection({ { QStringLiteral("connection"), wired },
                              { QStringLiteral("802-3-ethernet"), QVariantMap() } });
        const QVariantMap hotspot{ { QStringLiteral("id"), QStringLiteral("Hotspot") },
                                   { QStringLiteral("type"), QStringLiteral("802-11-wireless") } };
        const QVariantMap hotspotWireless{ { QStringLiteral("ssid"), QByteArray("OSD-OS") },
                                           { QStringLiteral("mode"), QStringLiteral("ap") } };
        m_nm->addConnection({ { QStringLiteral("connection"), hotspot },
                              { QStringLiteral("802-11-wireless"), hotspotWireless } });
        m_nm->addAccessPoint(QStringLiteral("Home"), 50, Security::Wpa, QStringLiteral("home-password"));
        m_nm->addAccessPoint(QStringLiteral("Office"), 90, Security::Wpa, QStringLiteral("office-password"));
        m_nm->addAccessPoint(QStringLiteral("Cafe"), 40, Security::Wpa, QStringLiteral("espresso-42"));
        m_nm->addAccessPoint(QStringLiteral("Cafe"), 75, Security::Wpa, QStringLiteral("espresso-42"));
        m_nm->addAccessPoint(QStringLiteral("Library"), 75, Security::Open);
        m_nm->addAccessPoint(QStringLiteral("Zeta"), 60, Security::Sae, QStringLiteral("zeta"));
        const QString garden = m_nm->addAccessPoint(QStringLiteral("Garden"), 20, Security::Owe);
        m_nm->addAccessPoint(QStringLiteral("Old Router"), 35, Security::Wep, QStringLiteral("abcde"));
        m_nm->addAccessPoint(QStringLiteral("Campus"), 85, Security::Enterprise);
        m_nm->addAccessPoint(QStringLiteral("Attic"), 95, Security::Wpa, QStringLiteral("attic-pass"), Hidden::Empty);
        m_nm->addAccessPoint(QStringLiteral("Shed"), 95, Security::Wpa, QStringLiteral("shed-pass"), Hidden::Zeros);
        const QString home = m_nm->addSaved(QStringLiteral("Home"), QStringLiteral("home-password"));
        m_nm->addSaved(QStringLiteral("Office"), QStringLiteral("office-password"));
        m_nm->addSaved(QStringLiteral("Beach House"), QStringLiteral("beach-password"));
        m_nm->join(home);
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        QCOMPARE(wifi->state(), QStringLiteral("connected"));
        QCOMPARE(describe(wifi->current()), QStringLiteral("address=192.168.1.23, bars=2, ssid=Home"));
        QCOMPARE(lines(wifi->networks()), (QStringList{
            "Home 2 wpa secured known heard active",
            "Office 4 wpa secured known heard",
            "Beach House 0 - known",
            "Campus 4 enterprise secured heard",
            "Cafe 3 wpa secured heard",
            "Library 3 open heard",
            "Zeta 3 sae secured heard",
            "Old Router 2 wep secured heard",
            "Garden 1 owe heard",
        }));

        // Stronger by a bar, it goes up among the others.
        QSignalSpy changed(wifi.get(), &WifiManager::networksChanged);
        m_nm->setStrength(garden, 90);
        QTRY_COMPARE(lines(wifi->networks()), (QStringList{
            "Home 2 wpa secured known heard active",
            "Office 4 wpa secured known heard",
            "Beach House 0 - known",
            "Campus 4 enterprise secured heard",
            "Garden 4 owe heard",
            "Cafe 3 wpa secured heard",
            "Library 3 open heard",
            "Zeta 3 sae secured heard",
            "Old Router 2 wep secured heard",
        }));
        // A signal that moves within its bars changes nothing on the page.
        const int before = changed.count();
        m_nm->setStrength(garden, 95);
        QVERIFY(sync());
        QTest::qWait(100);
        QCOMPARE(changed.count(), before);
    }

    // A saved connection edited elsewhere (Updated): the list follows it.
    void savedConnectionEditedElsewhere() {
        const QString beach = m_nm->addSaved(QStringLiteral("Beach House"), QStringLiteral("beach-password"));
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        QCOMPARE(lines(wifi->networks()), QStringList{ "Beach House 0 - known" });
        m_nm->clearCalls();
        m_nm->renameSaved(beach, QStringLiteral("Cabin"));
        QTRY_COMPARE(lines(wifi->networks()), QStringList{ "Cabin 0 - known" });
        QCOMPARE(pathsOf(m_nm->calls(QStringLiteral("GetSettings"))), QStringList{ beach });
    }

    void turnOnAndOff() {
        m_nm->addAccessPoint(QStringLiteral("Cafe"), 70, Security::Wpa, QStringLiteral("espresso-42"));
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        m_nm->clearCalls();
        wifi->setEnabled(false);
        QTRY_COMPARE(wifi->state(), QStringLiteral("off"));
        QVERIFY(!m_nm->wirelessEnabled());
        const QList<FakeNetworkManager::Call> set = m_nm->calls(QStringLiteral("Set"));
        QCOMPARE(set.size(), 1);
        QCOMPARE(set.first().path, QStringLiteral("/org/freedesktop/NetworkManager"));
        QCOMPARE(set.first().interface, QStringLiteral("org.freedesktop.DBus.Properties"));
        QCOMPARE(set.first().args, (QVariantList{ QStringLiteral("org.freedesktop.NetworkManager"),
                                                  QStringLiteral("WirelessEnabled"), false }));
        QCOMPARE(set.first().error, QString());

        wifi->setEnabled(true);
        QTRY_COMPARE(wifi->state(), QStringLiteral("disconnected"));
        QVERIFY(m_nm->wirelessEnabled());
        QTRY_COMPARE(lines(wifi->networks()), QStringList{ "Cafe 3 wpa secured heard" });

        // Refused (the polkit rule missing): why.
        m_nm->failNextCall(QStringLiteral("Set"), QStringLiteral("org.freedesktop.NetworkManager.PermissionDenied"),
                           QStringLiteral("Not authorized to enable/disable Wi-Fi"));
        wifi->setEnabled(false);
        QTRY_COMPARE(wifi->message(), QStringLiteral("Couldn't turn Wi-Fi off: NetworkManager didn't allow it (polkit)"));
        QCOMPARE(wifi->state(), QStringLiteral("disconnected"));
        QVERIFY(m_nm->wirelessEnabled());
    }

    // The search: a scan of wlan0 at once. A scan turned down as too soon is
    // let be; another refusal is said. Wi-Fi off, there is none, and turning
    // it off ends one.
    void search() {
        m_nm->addAccessPoint(QStringLiteral("Cafe"), 70, Security::Wpa, QStringLiteral("espresso-42"));
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        m_nm->clearCalls();
        QSignalSpy searching(wifi.get(), &WifiManager::searchingChanged);
        wifi->startSearch();
        QVERIFY(wifi->searching());
        QCOMPARE(searching.count(), 1);
        QTRY_COMPARE(m_nm->calls(QStringLiteral("RequestScan")).size(), 1);
        const FakeNetworkManager::Call scan = m_nm->calls(QStringLiteral("RequestScan")).first();
        QCOMPARE(scan.path, m_nm->devicePath());
        QCOMPARE(scan.interface, QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless"));
        QCOMPARE(scan.args, QVariantList{ QVariantMap() });
        QCOMPARE(scan.error, QString());
        wifi->stopSearch();
        QVERIFY(!wifi->searching());
        QCOMPARE(searching.count(), 2);

        m_nm->failNextCall(QStringLiteral("RequestScan"), QStringLiteral("org.freedesktop.NetworkManager.Device.NotAllowed"),
                           QStringLiteral("Scanning not allowed immediately following previous scan"));
        wifi->startSearch();
        QTRY_COMPARE(m_nm->calls(QStringLiteral("RequestScan")).size(), 2);
        QVERIFY(sync());
        QCOMPARE(wifi->message(), QString());
        wifi->stopSearch();
        m_nm->failNextCall(QStringLiteral("RequestScan"), QStringLiteral("org.freedesktop.NetworkManager.PermissionDenied"),
                           QStringLiteral("Not authorized to request a Wi-Fi scan"));
        wifi->startSearch();
        QTRY_COMPARE(wifi->message(), QStringLiteral("Couldn't search: NetworkManager didn't allow it (polkit)"));

        m_nm->setWirelessEnabled(false);
        QTRY_COMPARE(wifi->state(), QStringLiteral("off"));
        QVERIFY(!wifi->searching());
        wifi->startSearch();
        QVERIFY(!wifi->searching());
    }

    // A new WPA network with its password: the settings sent, the network on
    // its way, then joined and kept.
    void joinNewNetwork() {
        m_nm->addAccessPoint(QStringLiteral("Cafe"), 40, Security::Wpa, QStringLiteral("espresso-42"));
        const QString strongest = m_nm->addAccessPoint(QStringLiteral("Cafe"), 70, Security::Wpa, QStringLiteral("espresso-42"));
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        m_nm->clearCalls();
        m_nm->setHeld(true);
        wifi->connectTo(QStringLiteral("Cafe"), QStringLiteral("espresso-42"));
        QTRY_COMPARE(m_nm->calls(QStringLiteral("AddAndActivateConnection")).size(), 1);
        const FakeNetworkManager::Call call = m_nm->calls(QStringLiteral("AddAndActivateConnection")).first();
        QCOMPARE(call.path, QStringLiteral("/org/freedesktop/NetworkManager"));
        QCOMPARE(call.interface, QStringLiteral("org.freedesktop.NetworkManager"));
        QCOMPARE(call.error, QString());
        QCOMPARE(call.args.size(), 3);
        const QVariantMap settings = call.args.at(0).toMap();
        const QVariantMap connection = settings.value(QStringLiteral("connection")).toMap();
        QCOMPARE(connection.value(QStringLiteral("id")).toString(), QStringLiteral("Cafe"));
        QCOMPARE(connection.value(QStringLiteral("type")).toString(), QStringLiteral("802-11-wireless"));
        const QVariantMap wireless = settings.value(QStringLiteral("802-11-wireless")).toMap();
        QCOMPARE(wireless.value(QStringLiteral("ssid")).userType(), int(QMetaType::QByteArray));
        QCOMPARE(wireless.value(QStringLiteral("ssid")).toByteArray(), QByteArray("Cafe"));
        QCOMPARE(wireless.value(QStringLiteral("mode")).toString(), QStringLiteral("infrastructure"));
        QVERIFY(!wireless.contains(QStringLiteral("hidden")));
        const QVariantMap security = settings.value(QStringLiteral("802-11-wireless-security")).toMap();
        QCOMPARE(security.value(QStringLiteral("key-mgmt")).toString(), QStringLiteral("wpa-psk"));
        QCOMPARE(security.value(QStringLiteral("psk")).toString(), QStringLiteral("espresso-42"));
        QCOMPARE(call.args.at(1).toString(), m_nm->devicePath());
        QCOMPARE(call.args.at(2).toString(), strongest);

        // Connecting as soon as it is asked for, before the device has moved.
        QTRY_VERIFY(m_nm->isWaiting());
        QCOMPARE(m_nm->deviceState(), 30u);
        QTRY_COMPARE(wifi->state(), QStringLiteral("connecting"));
        QCOMPARE(describe(wifi->current()), QStringLiteral("bars=3, ssid=Cafe"));
        QTRY_COMPARE(lines(wifi->networks()), QStringList{ "Cafe 3 wpa secured known heard connecting" });
        QCOMPARE(wifi->message(), QString());

        m_nm->setHeld(false);
        QTRY_COMPARE(wifi->state(), QStringLiteral("connected"));
        QCOMPARE(wifi->message(), QStringLiteral("Connected to Cafe"));
        QCOMPARE(describe(wifi->current()), QStringLiteral("address=192.168.1.23, bars=3, ssid=Cafe"));
        QTRY_COMPARE(lines(wifi->networks()), QStringList{ "Cafe 3 wpa secured known heard active" });
        const QString added = call.reply.value(0).toString();
        QCOMPARE(m_nm->savedConnections(), QStringList{ added });
        QCOMPARE(m_nm->settings(added).value(QStringLiteral("802-11-wireless-security")).toMap()
                     .value(QStringLiteral("psk")).toString(), QStringLiteral("espresso-42"));
        QVERIFY(m_nm->calls(QStringLiteral("Delete")).isEmpty());
    }

    // A wrong password: why, and what was added for it deleted, or
    // NetworkManager would try it again by itself. Another reason in its
    // words.
    void wrongPassword() {
        m_nm->addAccessPoint(QStringLiteral("Office"), 90, Security::Wpa, QStringLiteral("correct-horse"));
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        m_nm->clearCalls();
        wifi->connectTo(QStringLiteral("Office"), QStringLiteral("battery-staple"));
        QTRY_COMPARE(wifi->message(), QStringLiteral("Office: wrong password?"));
        const QList<FakeNetworkManager::Call> added = m_nm->calls(QStringLiteral("AddAndActivateConnection"));
        QCOMPARE(added.size(), 1);
        QTRY_COMPARE(pathsOf(m_nm->calls(QStringLiteral("Delete"))), QStringList{ added.first().reply.value(0).toString() });
        QTRY_VERIFY(m_nm->savedConnections().isEmpty());
        QTRY_COMPARE(wifi->state(), QStringLiteral("disconnected"));
        QVERIFY(wifi->current().isEmpty());
        QTRY_COMPARE(lines(wifi->networks()), QStringList{ "Office 4 wpa secured heard" });

        m_nm->failNextActivation(5, 3);
        wifi->connectTo(QStringLiteral("Office"), QStringLiteral("correct-horse"));
        QTRY_COMPARE(wifi->message(), QStringLiteral("Office: it gave no address (DHCP)"));
        QTRY_COMPARE(m_nm->calls(QStringLiteral("Delete")).size(), 2);
        QTRY_VERIFY(m_nm->savedConnections().isEmpty());
    }

    // What NetworkManager would turn down is said before it is asked: a WPA
    // password of fewer than 8 or more than 63 characters, a WPA3 one longer
    // than 63, a WEP key longer than 64, none for a secured network, and
    // WPA Enterprise, which wants a user name too. 64 hexadecimal digits are
    // a WPA key, and go.
    void passwordChecks() {
        m_nm->addAccessPoint(QStringLiteral("Cafe"), 70, Security::Wpa, QStringLiteral("espresso-42"));
        m_nm->addAccessPoint(QStringLiteral("Loft"), 60, Security::Sae, QStringLiteral("loft"));
        m_nm->addAccessPoint(QStringLiteral("Old Router"), 35, Security::Wep, QStringLiteral("abcde"));
        m_nm->addAccessPoint(QStringLiteral("Campus"), 85, Security::Enterprise);
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        m_nm->clearCalls();
        wifi->connectTo(QStringLiteral("Cafe"), QStringLiteral("short"));
        QCOMPARE(wifi->message(), QStringLiteral("A Wi-Fi password is 8 to 63 characters"));
        wifi->connectTo(QStringLiteral("Cafe"), QString(64, QLatin1Char('x')));
        QCOMPARE(wifi->message(), QStringLiteral("A Wi-Fi password is 8 to 63 characters"));
        wifi->connectTo(QStringLiteral("Cafe"));
        QCOMPARE(wifi->message(), QStringLiteral("Cafe wants its password"));
        wifi->connectTo(QStringLiteral("Loft"), QString(64, QLatin1Char('x')));
        QCOMPARE(wifi->message(), QStringLiteral("A Wi-Fi password is 1 to 63 characters"));
        wifi->connectTo(QStringLiteral("Old Router"), QString(65, QLatin1Char('k')));
        QCOMPARE(wifi->message(), QStringLiteral("A WEP key is 1 to 64 characters"));
        wifi->connectTo(QStringLiteral("Campus"), QStringLiteral("let-me-in-please"));
        QCOMPARE(wifi->message(),
                 QStringLiteral("Campus wants a user name too (WPA Enterprise), which can't be typed here"));
        QCOMPARE(wifi->state(), QStringLiteral("disconnected"));
        QVERIFY(sync());
        QVERIFY(m_nm->calls(QStringLiteral("AddAndActivateConnection")).isEmpty());
        QVERIFY(m_nm->calls(QStringLiteral("ActivateConnection")).isEmpty());

        const QString key(64, QLatin1Char('a'));
        wifi->connectTo(QStringLiteral("Cafe"), key);
        QTRY_COMPARE(m_nm->calls(QStringLiteral("AddAndActivateConnection")).size(), 1);
        const FakeNetworkManager::Call call = m_nm->calls(QStringLiteral("AddAndActivateConnection")).first();
        QCOMPARE(call.error, QString());
        QCOMPARE(call.args.at(0).toMap().value(QStringLiteral("802-11-wireless-security")).toMap()
                     .value(QStringLiteral("psk")).toString(), key);
        QTRY_COMPARE(wifi->message(), QStringLiteral("Cafe: wrong password?"));
    }

    // A saved network without a password: joined as it was saved
    // (ActivateConnection), nothing added. One out of reach is tried, and
    // not found; one whose password changed since is not joined, and stays
    // saved.
    void joinSaved() {
        m_nm->addAccessPoint(QStringLiteral("Home"), 50, Security::Wpa, QStringLiteral("home-password"));
        const QString home = m_nm->addSaved(QStringLiteral("Home"), QStringLiteral("home-password"));
        const QString beach = m_nm->addSaved(QStringLiteral("Beach House"), QStringLiteral("beach-password"));
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        QCOMPARE(lines(wifi->networks()), (QStringList{ "Home 2 wpa secured known heard", "Beach House 0 - known" }));
        m_nm->clearCalls();
        wifi->connectTo(QStringLiteral("Home"));
        QTRY_COMPARE(wifi->message(), QStringLiteral("Connected to Home"));
        QTRY_COMPARE(wifi->state(), QStringLiteral("connected"));
        const QList<FakeNetworkManager::Call> activate = m_nm->calls(QStringLiteral("ActivateConnection"));
        QCOMPARE(activate.size(), 1);
        QCOMPARE(activate.first().args, (QVariantList{ home, m_nm->devicePath(), QStringLiteral("/") }));
        QCOMPARE(activate.first().error, QString());
        QVERIFY(m_nm->calls(QStringLiteral("AddAndActivateConnection")).isEmpty());
        QCOMPARE(m_nm->savedConnections(), (QStringList{ home, beach }));

        wifi->connectTo(QStringLiteral("Beach House"));
        QTRY_COMPARE(wifi->message(), QStringLiteral("Beach House: it is out of reach"));
        QTRY_COMPARE(wifi->state(), QStringLiteral("disconnected"));

        m_nm->failNextActivation();
        wifi->connectTo(QStringLiteral("Home"));
        QTRY_COMPARE(wifi->message(), QStringLiteral("Home: wrong password?"));
        QVERIFY(sync());
        QVERIFY(m_nm->calls(QStringLiteral("Delete")).isEmpty());
        QCOMPARE(m_nm->savedConnections(), (QStringList{ home, beach }));
        QCOMPARE(m_nm->calls(QStringLiteral("ActivateConnection")).size(), 3);
    }

    // A saved network given a new password: a new connection, which replaces
    // the old one once it is joined, not before.
    void newPasswordReplacesSaved() {
        m_nm->addAccessPoint(QStringLiteral("Home"), 50, Security::Wpa, QStringLiteral("new-password"));
        const QString old = m_nm->addSaved(QStringLiteral("Home"), QStringLiteral("old-password"));
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        m_nm->clearCalls();
        m_nm->setHeld(true);
        wifi->connectTo(QStringLiteral("Home"), QStringLiteral("new-password"));
        QTRY_VERIFY(m_nm->isWaiting());
        QCOMPARE(m_nm->calls(QStringLiteral("AddAndActivateConnection")).size(), 1);
        QVERIFY(m_nm->calls(QStringLiteral("ActivateConnection")).isEmpty());
        const QString added = m_nm->calls(QStringLiteral("AddAndActivateConnection")).first().reply.value(0).toString();
        QVERIFY(sync());
        QCOMPARE(m_nm->savedConnections(), (QStringList{ old, added }));
        QVERIFY(m_nm->calls(QStringLiteral("Delete")).isEmpty());

        m_nm->setHeld(false);
        QTRY_COMPARE(wifi->message(), QStringLiteral("Connected to Home"));
        QTRY_COMPARE(pathsOf(m_nm->calls(QStringLiteral("Delete"))), QStringList{ old });
        QTRY_COMPARE(m_nm->savedConnections(), QStringList{ added });
        QCOMPARE(m_nm->settings(added).value(QStringLiteral("802-11-wireless-security")).toMap()
                     .value(QStringLiteral("psk")).toString(), QStringLiteral("new-password"));
        QTRY_COMPARE(lines(wifi->networks()), QStringList{ "Home 2 wpa secured known heard active" });
    }

    // A network that doesn't say its name, joined by it: saved as hidden,
    // with no access point named, and listed once joined.
    void joinHidden() {
        m_nm->addAccessPoint(QStringLiteral("Attic"), 65, Security::Wpa, QStringLiteral("attic-pass"), Hidden::Empty);
        m_nm->addAccessPoint(QStringLiteral("Cafe"), 70, Security::Wpa, QStringLiteral("espresso-42"));
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        QCOMPARE(lines(wifi->networks()), QStringList{ "Cafe 3 wpa secured heard" });
        m_nm->clearCalls();
        wifi->connectHidden(QStringLiteral("Attic"), QStringLiteral("attic-pass"));
        QTRY_COMPARE(wifi->message(), QStringLiteral("Connected to Attic"));
        const QList<FakeNetworkManager::Call> added = m_nm->calls(QStringLiteral("AddAndActivateConnection"));
        QCOMPARE(added.size(), 1);
        const QVariantMap settings = added.first().args.at(0).toMap();
        const QVariantMap wireless = settings.value(QStringLiteral("802-11-wireless")).toMap();
        QCOMPARE(wireless.value(QStringLiteral("ssid")).toByteArray(), QByteArray("Attic"));
        QCOMPARE(wireless.value(QStringLiteral("hidden")).userType(), int(QMetaType::Bool));
        QCOMPARE(wireless.value(QStringLiteral("hidden")).toBool(), true);
        const QVariantMap security = settings.value(QStringLiteral("802-11-wireless-security")).toMap();
        QCOMPARE(security.value(QStringLiteral("key-mgmt")).toString(), QStringLiteral("wpa-psk"));
        QCOMPARE(security.value(QStringLiteral("psk")).toString(), QStringLiteral("attic-pass"));
        QCOMPARE(added.first().args.at(2).toString(), QStringLiteral("/"));
        QTRY_COMPARE(wifi->state(), QStringLiteral("connected"));
        QCOMPARE(describe(wifi->current()), QStringLiteral("address=192.168.1.23, bars=3, ssid=Attic"));
        QTRY_COMPARE(lines(wifi->networks()),
                     (QStringList{ "Attic 3 wpa secured known heard active", "Cafe 3 wpa secured heard" }));
        QCOMPARE(m_nm->settings(m_nm->savedConnections().value(0)).value(QStringLiteral("802-11-wireless")).toMap()
                     .value(QStringLiteral("hidden")).toBool(), true);
    }

    // Another network joined while on one: NetworkManager leaves the first.
    void joinAnotherWhileConnected() {
        m_nm->addAccessPoint(QStringLiteral("Home"), 50, Security::Wpa, QStringLiteral("home-password"));
        m_nm->join(m_nm->addSaved(QStringLiteral("Home"), QStringLiteral("home-password")));
        m_nm->addAccessPoint(QStringLiteral("Cafe"), 70, Security::Wpa, QStringLiteral("espresso-42"));
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        QCOMPARE(lines(wifi->networks()), (QStringList{ "Home 2 wpa secured known heard active", "Cafe 3 wpa secured heard" }));
        wifi->connectTo(QStringLiteral("Cafe"), QStringLiteral("espresso-42"));
        QTRY_COMPARE(wifi->message(), QStringLiteral("Connected to Cafe"));
        QTRY_COMPARE(describe(wifi->current()), QStringLiteral("address=192.168.1.23, bars=3, ssid=Cafe"));
        QTRY_COMPARE(lines(wifi->networks()),
                     (QStringList{ "Cafe 3 wpa secured known heard active", "Home 2 wpa secured known heard" }));
    }

    // Another network chosen while one is on its way takes its place: it is
    // joined, and what was added for the one given up is deleted.
    void joinAnotherOnTheWay() {
        m_nm->addAccessPoint(QStringLiteral("Cafe"), 70, Security::Wpa, QStringLiteral("espresso-42"));
        m_nm->addAccessPoint(QStringLiteral("Office"), 90, Security::Wpa, QStringLiteral("correct-horse"));
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        m_nm->clearCalls();
        m_nm->setHeld(true);
        wifi->connectTo(QStringLiteral("Cafe"), QStringLiteral("espresso-42"));
        QTRY_VERIFY(m_nm->isWaiting());
        // NetworkManager's answer read: the connection added for Cafe known.
        QVERIFY(sync());
        const QString cafe = m_nm->calls(QStringLiteral("AddAndActivateConnection")).first().reply.value(0).toString();
        QVERIFY(m_nm->savedConnections().contains(cafe));
        QTRY_COMPARE(wifi->state(), QStringLiteral("connecting"));

        wifi->connectTo(QStringLiteral("Office"), QStringLiteral("correct-horse"));
        QTRY_COMPARE(m_nm->calls(QStringLiteral("AddAndActivateConnection")).size(), 2);
        QTRY_COMPARE(pathsOf(m_nm->calls(QStringLiteral("Delete"))), QStringList{ cafe });
        m_nm->setHeld(false);
        QTRY_COMPARE(wifi->message(), QStringLiteral("Connected to Office"));
        QTRY_COMPARE(lines(wifi->networks()),
                     (QStringList{ "Office 4 wpa secured known heard active", "Cafe 3 wpa secured heard" }));
        QCOMPARE(m_nm->savedConnections().size(), 1);
        QVERIFY(!m_nm->savedConnections().contains(cafe));
    }

    // Leaving the network joined: disconnecting, then disconnected. Leaving
    // one on its way gives it up, and what was added for it is deleted.
    void leaveAndGiveUp() {
        m_nm->addAccessPoint(QStringLiteral("Home"), 50, Security::Wpa, QStringLiteral("home-password"));
        const QString home = m_nm->addSaved(QStringLiteral("Home"), QStringLiteral("home-password"));
        m_nm->join(home);
        m_nm->addAccessPoint(QStringLiteral("Cafe"), 70, Security::Wpa, QStringLiteral("espresso-42"));
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        QCOMPARE(wifi->state(), QStringLiteral("connected"));
        m_nm->clearCalls();
        m_nm->setHeld(true);
        const QString active = m_nm->activeConnection();
        wifi->disconnectNetwork();
        QTRY_COMPARE(wifi->state(), QStringLiteral("disconnecting"));
        QCOMPARE(pathsOf(m_nm->calls(QStringLiteral("DeactivateConnection"))),
                 QStringList{ QStringLiteral("/org/freedesktop/NetworkManager") });
        QCOMPARE(m_nm->calls(QStringLiteral("DeactivateConnection")).first().args, QVariantList{ active });
        QTRY_COMPARE(lines(wifi->networks()).value(0), QStringLiteral("Home 2 wpa secured known heard active disconnecting"));
        QTRY_VERIFY(m_nm->isWaiting());
        m_nm->proceed();
        QTRY_COMPARE(wifi->state(), QStringLiteral("disconnected"));
        QVERIFY(wifi->current().isEmpty());
        QTRY_COMPARE(lines(wifi->networks()), (QStringList{ "Home 2 wpa secured known heard", "Cafe 3 wpa secured heard" }));
        QCOMPARE(m_nm->activeConnection(), QString());
        QCOMPARE(wifi->message(), QString());

        wifi->connectTo(QStringLiteral("Cafe"), QStringLiteral("espresso-42"));
        QTRY_VERIFY(m_nm->isWaiting());
        // The device on its way, its active connection what is left.
        m_nm->proceed();
        QCOMPARE(m_nm->deviceState(), 50u);
        QVERIFY(sync());
        QTRY_COMPARE(wifi->state(), QStringLiteral("connecting"));
        wifi->disconnectNetwork();
        QTRY_COMPARE(m_nm->calls(QStringLiteral("DeactivateConnection")).size(), 2);
        m_nm->proceed();
        QTRY_COMPARE(wifi->message(), QStringLiteral("Cafe: given up"));
        QTRY_COMPARE(m_nm->savedConnections(), QStringList{ home });
        QTRY_COMPARE(wifi->state(), QStringLiteral("disconnected"));
    }

    // Forgetting a network deletes every connection saved for it, the one
    // joined too, which NetworkManager then leaves; one out of reach goes from
    // the list. A refusal is said, and keeps it.
    void forget() {
        m_nm->addAccessPoint(QStringLiteral("Home"), 50, Security::Wpa, QStringLiteral("home-password"));
        const QString home = m_nm->addSaved(QStringLiteral("Home"), QStringLiteral("home-password"));
        const QString older = m_nm->addSaved(QStringLiteral("Home"), QStringLiteral("older-password"));
        const QString office = m_nm->addSaved(QStringLiteral("Office"), QStringLiteral("office-password"));
        const QString garage = m_nm->addSaved(QStringLiteral("Garage"), QStringLiteral("garage-password"));
        m_nm->join(home);
        QVERIFY(m_nm->start(m_fakeBus));
        auto wifi = makeWifi();
        QVERIFY(loaded(*wifi));
        QCOMPARE(lines(wifi->networks()),
                 (QStringList{ "Home 2 wpa secured known heard active", "Garage 0 - known", "Office 0 - known" }));
        QCOMPARE(m_nm->settings(older).value(QStringLiteral("connection")).toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("Home 1"));
        m_nm->clearCalls();
        wifi->forget(QStringLiteral("Home"));
        QTRY_COMPARE(wifi->message(), QStringLiteral("Home is forgotten"));
        QCOMPARE(pathsOf(m_nm->calls(QStringLiteral("Delete"))), (QStringList{ home, older }));
        QCOMPARE(m_nm->savedConnections(), (QStringList{ office, garage }));
        QTRY_COMPARE(wifi->state(), QStringLiteral("disconnected"));
        QTRY_COMPARE(lines(wifi->networks()),
                     (QStringList{ "Garage 0 - known", "Office 0 - known", "Home 2 wpa secured heard" }));

        wifi->forget(QStringLiteral("Office"));
        QTRY_COMPARE(wifi->message(), QStringLiteral("Office is forgotten"));
        QTRY_COMPARE(lines(wifi->networks()), (QStringList{ "Garage 0 - known", "Home 2 wpa secured heard" }));

        m_nm->failNextCall(QStringLiteral("Delete"), QStringLiteral("org.freedesktop.NetworkManager.PermissionDenied"),
                           QStringLiteral("Insufficient privileges"));
        wifi->forget(QStringLiteral("Garage"));
        QTRY_COMPARE(wifi->message(), QStringLiteral("Couldn't forget Garage: NetworkManager didn't allow it (polkit)"));
        QCOMPARE(m_nm->savedConnections(), QStringList{ garage });

        m_nm->clearCalls();
        wifi->forget(QStringLiteral("Nowhere"));
        QVERIFY(sync());
        QVERIFY(m_nm->calls(QStringLiteral("Delete")).isEmpty());
    }

    // The country raspi-config set, from the boot partition's cmdline.txt,
    // and its name from the time zone database's list; the list by name,
    // accents aside, each with the letter it is under. A code the list
    // doesn't have, none, and no cmdline.txt are no country.
    void country() {
        auto wifi = makeWifi();
        QCOMPARE(wifi->country(), QStringLiteral("GB"));
        QCOMPARE(wifi->countryName(), QStringLiteral("Britain (UK)"));
        QCOMPARE(countryLines(wifi->countries()), (QStringList{
            QStringLiteral("AX Åland Islands A"),
            QStringLiteral("AL Albania A"),
            QStringLiteral("AD Andorra A"),
            QStringLiteral("GB Britain (UK) B"),
            QStringLiteral("CI Côte d'Ivoire C"),
            QStringLiteral("CW Curaçao C"),
            QStringLiteral("TR Turkey T"),
            QStringLiteral("US United States U"),
        }));
        writeCmdline(QStringLiteral("XX"));
        QCOMPARE(makeWifi()->country(), QString());
        writeCmdline(QString());
        QCOMPARE(makeWifi()->country(), QString());
        QCOMPARE(makeWifi()->countryName(), QString());
        QVERIFY(QFile::remove(cmdlinePath()));
        QCOMPARE(makeWifi()->country(), QString());
    }

    // The country set through osdos-wifi.socket: busy meanwhile, one at a
    // time, then as the boot partition has it and the message says so. A
    // code that isn't a country is not sent; a refusal says why; the socket
    // gone too.
    void setCountry() {
        qputenv("OSDOS_LAUNCHER_API", "7");
        CountryHelper helper(this);
        QVERIFY(helper.server.isListening());
        auto wifi = makeWifi();
        QVERIFY(wifi->countryAvailable());
        QCOMPARE(wifi->country(), QStringLiteral("GB"));
        QSignalSpy changed(wifi.get(), &WifiManager::countryChanged);
        QVERIFY(!wifi->setCountry(QStringLiteral("XX")));
        QVERIFY(!wifi->setCountry(QStringLiteral("tr")));
        QCOMPARE(changed.count(), 0);

        QVERIFY(wifi->setCountry(QStringLiteral("TR")));
        QVERIFY(wifi->countryBusy());
        QVERIFY(!wifi->setCountry(QStringLiteral("US")));
        QTRY_VERIFY(!wifi->countryBusy());
        QCOMPARE(helper.asked, QByteArrayList{ "TR" });
        QCOMPARE(wifi->country(), QStringLiteral("TR"));
        QCOMPARE(wifi->countryName(), QStringLiteral("Turkey"));
        QCOMPARE(wifi->message(), QStringLiteral("Wi-Fi country set to Turkey"));
        QCOMPARE(changed.count(), 2);

        helper.refusal = "raspi-config isn't installed";
        QVERIFY(wifi->setCountry(QStringLiteral("US")));
        QTRY_VERIFY(!wifi->countryBusy());
        QCOMPARE(helper.asked, (QByteArrayList{ "TR", "US" }));
        QCOMPARE(wifi->message(), QStringLiteral("Couldn't set the country: raspi-config isn't installed"));
        QCOMPARE(wifi->country(), QStringLiteral("TR"));

        helper.server.close();
        QVERIFY(wifi->setCountry(QStringLiteral("US")));
        QTRY_VERIFY(!wifi->countryBusy());
        QCOMPARE(wifi->message(), QStringLiteral("Couldn't set the country: osdos-wifi.socket isn't listening"));
        QCOMPARE(wifi->country(), QStringLiteral("TR"));
    }

    // The country can be set with launcher API 7 and its socket there, not
    // with an older launcher, nor without the socket.
    void countryNeedsLauncherApi7AndItsSocket() {
        CountryHelper helper(this);
        QVERIFY(helper.server.isListening());
        qputenv("OSDOS_LAUNCHER_API", "6");
        auto api6 = makeWifi();
        QVERIFY(!api6->countryAvailable());
        QVERIFY(!api6->setCountry(QStringLiteral("TR")));
        qputenv("OSDOS_LAUNCHER_API", "7");
        QVERIFY(makeWifi()->countryAvailable());
        helper.server.close();
        auto noSocket = makeWifi();
        QVERIFY(!noSocket->countryAvailable());
        QVERIFY(!noSocket->setCountry(QStringLiteral("TR")));
        QVERIFY(helper.asked.isEmpty());
        QCOMPARE(api6->country(), QStringLiteral("GB"));
    }

    // Started by hand, the row is offered; by the app's service, only with
    // a launcher whose image lets the app use NetworkManager (API 7), under
    // the 240-MP names too.
    void supported() {
        auto wifi = makeWifi();
        QVERIFY(wifi->supported());
        qputenv("OSDOS_AUTOSTART", "1");
        QVERIFY(!wifi->supported());
        qputenv("OSDOS_LAUNCHER_API", "6");
        QVERIFY(!wifi->supported());
        qputenv("OSDOS_LAUNCHER_API", "7");
        QVERIFY(wifi->supported());
        qunsetenv("OSDOS_AUTOSTART");
        qunsetenv("OSDOS_LAUNCHER_API");
        qputenv("MP240_AUTOSTART", "1");
        qputenv("MP240_LAUNCHER_API", "6");
        QVERIFY(!wifi->supported());
        qputenv("MP240_LAUNCHER_API", "7");
        QVERIFY(wifi->supported());
    }

    // The chooser starts on the time zone's country, if the list has it, and
    // on none for UTC.
    void suggestedCountry() {
        if (QLocale::territoryToCode(QTimeZone(QByteArrayLiteral("Europe/Istanbul")).territory()) != QLatin1String("TR"))
            QSKIP("No time zone database here (tzdata) to place Europe/Istanbul");
        auto wifi = makeWifi();
        qputenv("TZ", "Europe/Istanbul");
        QCOMPARE(wifi->suggestedCountry(), QStringLiteral("TR"));
        qputenv("TZ", "UTC");
        QCOMPARE(wifi->suggestedCountry(), QString());
        qputenv("TZ", "Europe/Paris");
        QCOMPARE(wifi->suggestedCountry(), QString());
    }
};

QTEST_GUILESS_MAIN(WifiTest)
#include "wifi_test.moc"
