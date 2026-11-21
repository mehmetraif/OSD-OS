#pragma once

#include <QHostAddress>
#include <QString>
#include <QStringList>
#include <QtAlgorithms>
#include <QVariantMap>
#include <QtEndian>

#include <climits>

// What the link to the local network says of a game played over it: the
// band a Wi-Fi frequency is in, the interface the route to the network goes
// through, and the one line the Arcade's Host LAN Game and LAN Games show when
// that route is Wi-Fi at 2.4 GHz while a Bluetooth input device is connected.
// A Raspberry Pi has one radio for both: Bluetooth and 2.4 GHz Wi-Fi take
// turns on it, and a game's packets wait while the pads are served.
// Header-only, of Qt Core and Qt Network's QHostAddress alone: WifiManager
// (Settings → Network → Wi-Fi) and ArcadeBackend both read it.
namespace linkadvice {

// The line, as Host LAN Game and LAN Games show it.
inline const QString kTwoGhzWithBluetooth =
    QStringLiteral("Wi-Fi is on 2.4 GHz with Bluetooth pads: use 5 GHz or a cable");

// Below this, a Wi-Fi frequency in MHz is in the 2.4 GHz band.
constexpr uint kTwoGhzBelowMhz = 3000;

// The band of a frequency NetworkManager gives (an access point's
// Frequency, in MHz): "2.4 GHz" (2400-2500), "5 GHz" (5150-5895), "6 GHz"
// (5925-7125), "" for none known (0) or another band.
inline QString bandOf(uint mhz) {
    if (mhz >= 2400 && mhz < kTwoGhzBelowMhz)
        return QStringLiteral("2.4 GHz");
    if (mhz >= 4900 && mhz < 5925)
        return QStringLiteral("5 GHz");
    if (mhz >= 5925 && mhz <= 7125)
        return QStringLiteral("6 GHz");
    return QString();
}

// The bands of a network heard on several (one name, a router's 2.4 and
// 5 GHz radios), lowest first: "2.4/5 GHz"; one alone as bandOf() names it.
inline QString bandsOf(const QList<uint> &frequencies) {
    QStringList numbers;
    for (const char *band : { "2.4", "5", "6" }) {
        for (uint mhz : frequencies) {
            if (bandOf(mhz) == QLatin1String(band) + QStringLiteral(" GHz")) {
                numbers << QLatin1String(band);
                break;
            }
        }
    }
    return numbers.isEmpty() ? QString() : numbers.join(QLatin1Char('/')) + QStringLiteral(" GHz");
}

// The interface IPv4 traffic to peer goes through, from the kernel's routing
// table as Linux has it in /proc/net/route: the up route that matches peer
// with the longest mask, then the lowest metric. A null peer is "the local
// network" as a game hosted here meets it: the default route of the lowest
// metric (NetworkManager gives a cable's a lower one than Wi-Fi's), else,
// with no default route (two machines on a cable alone), the lowest-metric
// route to a private network. "" when nothing matches, or for a table that
// isn't one (another system than Linux).
inline QString routeInterface(const QString &table, const QHostAddress &peer = QHostAddress()) {
    bool ok = false;
    const quint32 to = peer.toIPv4Address(&ok);
    const bool anyPeer = !ok || peer.isNull();
    const auto ipOf = [](const QString &hex, bool *good) {
        const quint32 raw = hex.toUInt(good, 16);
        // The kernel prints the address's bytes, in the network's order, as
        // a number of this machine's.
        return qFromBigEndian<quint32>(&raw);
    };
    const auto isPrivate = [](quint32 ip) {
        return (ip >> 24) == 10 || (ip >> 20) == ((172u << 4) | 1u) || (ip >> 16) == ((192u << 8) | 168u);
    };
    QString best;
    int bestBits = -1;
    uint bestMetric = UINT_MAX;
    bool bestDefault = false;
    const QStringList lines = table.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = 1; i < lines.size(); ++i) {
        const QStringList f = lines.at(i).simplified().split(QLatin1Char(' '));
        if (f.size() < 8 || f.at(0) == QLatin1String("lo"))
            continue;
        bool g1 = false, g2 = false, g3 = false, g4 = false;
        const quint32 dest = ipOf(f.at(1), &g1);
        const uint flags = f.at(3).toUInt(&g2, 16);
        const uint metric = f.at(6).toUInt(&g3);
        const quint32 mask = ipOf(f.at(7), &g4);
        if (!g1 || !g2 || !g3 || !g4 || !(flags & 0x1))   // RTF_UP
            continue;
        const int bits = qPopulationCount(mask);
        if (anyPeer) {
            const bool isDefault = mask == 0;
            if (!isDefault && !isPrivate(dest))
                continue;
            // A default route before any other, then the lowest metric.
            if (best.isEmpty() || (isDefault && !bestDefault)
                || (isDefault == bestDefault && metric < bestMetric)) {
                best = f.at(0);
                bestDefault = isDefault;
                bestMetric = metric;
            }
            continue;
        }
        if ((to & mask) != (dest & mask))
            continue;
        if (bits > bestBits || (bits == bestBits && metric < bestMetric)) {
            best = f.at(0);
            bestBits = bits;
            bestMetric = metric;
        }
    }
    return best;
}

// The line for a game on the local network, or "": the route to it goes
// through the Wi-Fi device (routeIface is wifi's interface), on a frequency
// below 3000 MHz, while a Bluetooth input device is connected. wifi is
// WifiManager's state and current ({ interface, frequency, ... }).
inline QString warning(const QString &routeIface, const QString &wifiState, const QVariantMap &wifi,
                       bool bluetoothInput) {
    if (!bluetoothInput || routeIface.isEmpty() || wifiState != QLatin1String("connected"))
        return QString();
    const uint mhz = wifi.value(QStringLiteral("frequency")).toUInt();
    if (wifi.value(QStringLiteral("interface")).toString() != routeIface || mhz == 0 || mhz >= kTwoGhzBelowMhz)
        return QString();
    return kTwoGhzWithBluetooth;
}

} // namespace linkadvice
