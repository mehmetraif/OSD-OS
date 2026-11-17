#pragma once
#include <QFileInfo>
#include <QHostAddress>
#include <QList>
#include <QNetworkInterface>
#include <QString>

#ifdef Q_OS_LINUX
#include <QFile>
#include <algorithm>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

// The machine's network interfaces, as QNetworkInterface lists them, but on
// Linux without its Wi-Fi probe. Qt asks each Ethernet-like interface whether
// it is wireless with SIOCGIWMODE, one of the old wireless extensions, and the
// kernel writes to the journal that a program using them ("osdos uses wireless
// extensions which will stop working for Wi-Fi 7 hardware; use nl80211"); on
// such hardware the question goes unanswered and a Wi-Fi interface reads as
// Ethernet. Linux's own list (getifaddrs()) and sysfs tell the same without
// it: a wireless interface has /sys/class/net/<name>/wireless (or phy80211).
// Elsewhere Qt's list as it is.
namespace netif {

enum class Kind { Loopback, Ethernet, Wifi, Virtual, Other };

struct Address {
    QHostAddress ip;
    QHostAddress broadcast;   // an IPv4 network's, where it has one
};

struct Interface {
    QString name;
    Kind kind = Kind::Other;
    bool up = false;
    bool running = false;
    bool canBroadcast = false;
    QString hardwareAddress;   // "AA:BB:CC:DD:EE:FF", "" for none
    QList<Address> addresses;
};

#ifdef Q_OS_LINUX
namespace detail {
inline QString sysText(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.read(64)).trimmed() : QString();
}

// What an interface is, by its link type (/sys/class/net/<name>/type, the
// kernel's ARPHRD_*): 1 an Ethernet-like one, wireless where sysfs says so;
// 772 the loopback; a tunnel's, a bridge-less virtual link's and the like
// virtual; PPP and the rest other.
inline Kind kindOf(const QString &name, unsigned flags) {
    if (flags & IFF_LOOPBACK)
        return Kind::Loopback;
    const QString sys = QStringLiteral("/sys/class/net/") + name;
    bool ok = false;
    const int type = sysText(sys + QStringLiteral("/type")).toInt(&ok);
    if (!ok)
        return Kind::Other;
    switch (type) {
    case 1:     // ARPHRD_ETHER
        return QFileInfo::exists(sys + QStringLiteral("/wireless")) || QFileInfo::exists(sys + QStringLiteral("/phy80211"))
                   ? Kind::Wifi : Kind::Ethernet;
    case 772:   // ARPHRD_LOOPBACK
        return Kind::Loopback;
    case 512:   // ARPHRD_PPP
    case 801:   // ARPHRD_IEEE80211
    case 803:   // ARPHRD_IEEE80211_RADIOTAP
        return Kind::Other;
    default:    // tunnels (768, 769, 776, 778), ARPHRD_NONE (65534), ...
        return Kind::Virtual;
    }
}
} // namespace detail
#endif

inline QList<Interface> list() {
    QList<Interface> found;
#ifdef Q_OS_LINUX
    struct ifaddrs *all = nullptr;
    if (::getifaddrs(&all) != 0)
        return found;
    for (const struct ifaddrs *entry = all; entry; entry = entry->ifa_next) {
        if (!entry->ifa_name)
            continue;
        const QString name = QString::fromLocal8Bit(entry->ifa_name);
        auto it = std::find_if(found.begin(), found.end(), [&name](const Interface &i) { return i.name == name; });
        if (it == found.end()) {
            Interface iface;
            iface.name = name;
            iface.kind = detail::kindOf(name, entry->ifa_flags);
            iface.up = entry->ifa_flags & IFF_UP;
            iface.running = entry->ifa_flags & IFF_RUNNING;
            iface.canBroadcast = entry->ifa_flags & IFF_BROADCAST;
            const QString mac = detail::sysText(QStringLiteral("/sys/class/net/") + name + QStringLiteral("/address"));
            iface.hardwareAddress = mac == QLatin1String("00:00:00:00:00:00") ? QString() : mac.toUpper();
            found.append(iface);
            it = found.end() - 1;
        }
        if (!entry->ifa_addr || (entry->ifa_addr->sa_family != AF_INET && entry->ifa_addr->sa_family != AF_INET6))
            continue;
        Address address;
        address.ip.setAddress(entry->ifa_addr);
        if (entry->ifa_addr->sa_family == AF_INET && (entry->ifa_flags & IFF_BROADCAST) && entry->ifa_broadaddr)
            address.broadcast.setAddress(entry->ifa_broadaddr);
        it->addresses.append(address);
    }
    ::freeifaddrs(all);
#else
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &qt : interfaces) {
        Interface iface;
        iface.name = qt.name();
        const QNetworkInterface::InterfaceFlags flags = qt.flags();
        iface.kind = flags.testFlag(QNetworkInterface::IsLoopBack) || qt.type() == QNetworkInterface::Loopback
                         ? Kind::Loopback
                     : qt.type() == QNetworkInterface::Ethernet ? Kind::Ethernet
                     : qt.type() == QNetworkInterface::Wifi     ? Kind::Wifi
                     : qt.type() == QNetworkInterface::Virtual  ? Kind::Virtual
                                                                : Kind::Other;
        iface.up = flags.testFlag(QNetworkInterface::IsUp);
        iface.running = flags.testFlag(QNetworkInterface::IsRunning);
        iface.canBroadcast = flags.testFlag(QNetworkInterface::CanBroadcast);
        iface.hardwareAddress = qt.hardwareAddress();
        const QList<QNetworkAddressEntry> entries = qt.addressEntries();
        for (const QNetworkAddressEntry &entry : entries)
            iface.addresses.append({ entry.ip(), entry.broadcast() });
        found.append(iface);
    }
#endif
    return found;
}

} // namespace netif
