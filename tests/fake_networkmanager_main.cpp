// fake_networkmanager: the stand-in for NetworkManager (fake_networkmanager.h)
// on its own, for screenshots of Settings → Wi-Fi without a Raspberry Pi. It
// registers on the bus whose address is its argument, else the one
// DBUS_SYSTEM_BUS_ADDRESS names, with five networks heard: Home, saved and
// joined; Workshop, saved; Arcade Hall (WPA3), Corner Cafe (open) and
// Neighbours (WPA), each with its own number of bars. It runs until it is
// killed. The app finds it with DBUS_SYSTEM_BUS_ADDRESS set to the same
// address (tests/README.md).
#include "fake_networkmanager.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>

#include <cstdio>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    const QString address = args.size() > 1 ? args.at(1) : qEnvironmentVariable("DBUS_SYSTEM_BUS_ADDRESS");
    if (address.isEmpty()) {
        std::fprintf(stderr, "Usage: fake_networkmanager [bus address]\n"
                             "Without an address, the bus DBUS_SYSTEM_BUS_ADDRESS names.\n");
        return 2;
    }
    QDBusConnection bus = QDBusConnection::connectToBus(address, QStringLiteral("fake_networkmanager"));
    if (!bus.isConnected()) {
        std::fprintf(stderr, "fake_networkmanager: can't connect to %s: %s\n", qPrintable(address),
                     qPrintable(bus.lastError().message()));
        return 1;
    }

    using Security = FakeNetworkManager::Security;
    FakeNetworkManager nm;
    // Slow enough for a network on its way to be seen on the screen.
    nm.setDelay(1500);
    nm.addAccessPoint(QStringLiteral("Home"), 82, Security::Wpa, QStringLiteral("home-sweet-home"));
    nm.addAccessPoint(QStringLiteral("Workshop"), 58, Security::Wpa, QStringLiteral("solder-fumes"));
    nm.addAccessPoint(QStringLiteral("Arcade Hall"), 66, Security::Sae, QStringLiteral("insert-coin"));
    nm.addAccessPoint(QStringLiteral("Corner Cafe"), 45, Security::Open);
    nm.addAccessPoint(QStringLiteral("Neighbours"), 24, Security::Wpa, QStringLiteral("not-yours-42"));
    nm.join(nm.addSaved(QStringLiteral("Home"), QStringLiteral("home-sweet-home")));
    nm.addSaved(QStringLiteral("Workshop"), QStringLiteral("solder-fumes"));
    if (!nm.start(bus)) {
        std::fprintf(stderr, "fake_networkmanager: can't register org.freedesktop.NetworkManager on %s: %s\n",
                     qPrintable(address), qPrintable(bus.lastError().message()));
        return 1;
    }
    std::printf("fake_networkmanager: on %s\n"
                "  Home         4 bars, WPA,  saved and joined (192.168.1.23)\n"
                "  Workshop     3 bars, WPA,  saved\n"
                "  Arcade Hall  3 bars, WPA3, password insert-coin\n"
                "  Corner Cafe  2 bars, open\n"
                "  Neighbours   1 bar,  WPA,  password not-yours-42\n",
                qPrintable(address));
    std::fflush(stdout);
    return app.exec();
}
