#include "fake_networkmanager.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QtEndian>

#include <utility>

// A device's StateReason property, (uu): its state and why it came to it.
struct NmDeviceStateReason {
    uint state = 0;
    uint reason = 0;
    bool operator==(const NmDeviceStateReason &other) const {
        return state == other.state && reason == other.reason;
    }
};

QDBusArgument &operator<<(QDBusArgument &arg, const NmDeviceStateReason &value) {
    arg.beginStructure();
    arg << value.state << value.reason;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, NmDeviceStateReason &value) {
    arg.beginStructure();
    arg >> value.state >> value.reason;
    arg.endStructure();
    return arg;
}

// GetManagedObjects' answer, a{oa{sa{sv}}}.
using NmManagedObjects = QMap<QDBusObjectPath, QMap<QString, QVariantMap>>;

namespace {

const QString kService         = QStringLiteral("org.freedesktop.NetworkManager");
const QString kRootPath        = QStringLiteral("/org/freedesktop");
const QString kManagerPath     = QStringLiteral("/org/freedesktop/NetworkManager");
const QString kSettingsPath    = QStringLiteral("/org/freedesktop/NetworkManager/Settings");
const QString kManager         = QStringLiteral("org.freedesktop.NetworkManager");
const QString kSettings        = QStringLiteral("org.freedesktop.NetworkManager.Settings");
const QString kSaved           = QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection");
const QString kDevice          = QStringLiteral("org.freedesktop.NetworkManager.Device");
const QString kWireless        = QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless");
const QString kWired           = QStringLiteral("org.freedesktop.NetworkManager.Device.Wired");
const QString kWifiP2P         = QStringLiteral("org.freedesktop.NetworkManager.Device.WifiP2P");
const QString kLoopback        = QStringLiteral("org.freedesktop.NetworkManager.Device.Loopback");
const QString kAccessPoint     = QStringLiteral("org.freedesktop.NetworkManager.AccessPoint");
const QString kActive          = QStringLiteral("org.freedesktop.NetworkManager.Connection.Active");
const QString kIp4Config       = QStringLiteral("org.freedesktop.NetworkManager.IP4Config");
const QString kAgentManager    = QStringLiteral("org.freedesktop.NetworkManager.AgentManager");
const QString kDnsManager      = QStringLiteral("org.freedesktop.NetworkManager.DnsManager");
const QString kProperties      = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kObjectManager   = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString kIntrospectable  = QStringLiteral("org.freedesktop.DBus.Introspectable");

// A device's states (NMDeviceState) and the reasons it changes state
// (NMDeviceStateReason).
constexpr uint kUnmanaged = 10;
constexpr uint kUnavailable = 20;
constexpr uint kDisconnected = 30;
constexpr uint kPrepare = 40;
constexpr uint kConfig = 50;
constexpr uint kNeedAuth = 60;
constexpr uint kIpConfig = 70;
constexpr uint kIpCheck = 80;
constexpr uint kSecondaries = 90;
constexpr uint kActivated = 100;
constexpr uint kDeactivating = 110;
constexpr uint kFailed = 120;
constexpr uint kReasonNone = 0;
constexpr uint kReasonNoSecrets = 7;
constexpr uint kReasonSupplicantConfigFailed = 9;
constexpr uint kReasonConnectionRemoved = 38;
constexpr uint kReasonUserRequested = 39;
constexpr uint kReasonSupplicantAvailable = 42;
constexpr uint kReasonSsidNotFound = 53;
constexpr uint kReasonNewActivation = 60;
// An active connection's states (NMActiveConnectionState) and reasons
// (NMActiveConnectionStateReason).
constexpr uint kActivating = 1;
constexpr uint kActiveActivated = 2;
constexpr uint kActiveDeactivating = 3;
constexpr uint kActiveDeactivated = 4;
constexpr uint kActiveReasonNone = 1;
constexpr uint kActiveReasonUserDisconnected = 2;
constexpr uint kActiveReasonDeviceDisconnected = 3;
constexpr uint kActiveReasonNoSecrets = 9;
constexpr uint kActiveReasonConnectionRemoved = 11;
// NetworkManager's own state (NMState) and connectivity (NMConnectivityState).
constexpr uint kNmDisconnected = 20;
constexpr uint kNmConnecting = 40;
constexpr uint kNmConnectedGlobal = 70;
constexpr uint kConnectivityNone = 1;
constexpr uint kConnectivityFull = 4;
// Device types (NMDeviceType), and Wi-Fi's infrastructure mode (NM80211Mode).
constexpr uint kTypeEthernet = 1;
constexpr uint kTypeWifi = 2;
constexpr uint kTypeWifiP2P = 30;
constexpr uint kTypeLoopback = 32;
constexpr uint kModeInfra = 2;
// What an access point says of its security (NM80211ApFlags,
// NM80211ApSecurityFlags): CCMP for pairs and groups, and the key management.
constexpr uint kApPrivacy = 0x1;
constexpr uint kPairCcmp = 0x8;
constexpr uint kGroupCcmp = 0x80;
constexpr uint kKeyPsk = 0x100;
constexpr uint kKey8021x = 0x200;
constexpr uint kKeySae = 0x400;
constexpr uint kKeyOwe = 0x800;

QDBusObjectPath objectPath(const QString &path) {
    return QDBusObjectPath(path.isEmpty() ? QStringLiteral("/") : path);
}

// An object path property's value, "/" for none.
QVariant pathValue(const QString &path) {
    return QVariant::fromValue(objectPath(path));
}

QVariant pathsValue(const QStringList &paths) {
    QList<QDBusObjectPath> list;
    for (const QString &path : paths)
        list.append(QDBusObjectPath(path));
    return QVariant::fromValue(list);
}

// "/" as an argument means none.
QString pathArgument(const QVariant &value) {
    const QString path = value.value<QDBusObjectPath>().path();
    return path == QLatin1String("/") ? QString() : path;
}

QVariant plain(const QVariant &value);

// What QtDBus leaves wrapped made plain Qt, as the test reads it.
QVariant plainArgument(const QDBusArgument &arg) {
    switch (arg.currentType()) {
    case QDBusArgument::BasicType:
    case QDBusArgument::VariantType:
        return plain(arg.asVariant());
    case QDBusArgument::ArrayType: {
        const QString signature = arg.currentSignature();
        if (signature == QLatin1String("ay") || signature == QLatin1String("as"))
            return arg.asVariant();
        QVariantList list;
        arg.beginArray();
        while (!arg.atEnd())
            list.append(plainArgument(arg));
        arg.endArray();
        return list;
    }
    case QDBusArgument::StructureType: {
        QVariantList fields;
        arg.beginStructure();
        while (!arg.atEnd())
            fields.append(plainArgument(arg));
        arg.endStructure();
        return fields;
    }
    case QDBusArgument::MapType: {
        QVariantMap map;
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            const QString key = plainArgument(arg).toString();
            map.insert(key, plainArgument(arg));
            arg.endMapEntry();
        }
        arg.endMap();
        return map;
    }
    default:
        return QVariant();
    }
}

QVariant plain(const QVariant &value) {
    const int type = value.userType();
    if (type == qMetaTypeId<QDBusArgument>())
        return plainArgument(value.value<QDBusArgument>());
    if (type == qMetaTypeId<QDBusVariant>())
        return plain(value.value<QDBusVariant>().variant());
    if (type == qMetaTypeId<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    if (type == qMetaTypeId<QList<QDBusObjectPath>>()) {
        QVariantList list;
        for (const QDBusObjectPath &path : value.value<QList<QDBusObjectPath>>())
            list.append(path.path());
        return list;
    }
    if (type == QMetaType::QVariantMap) {
        QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            *it = plain(*it);
        return map;
    }
    if (type == QMetaType::QVariantList) {
        QVariantList list = value.toList();
        for (QVariant &item : list)
            item = plain(item);
        return list;
    }
    return value;
}

// A value's D-Bus type.
QString signatureOf(const QVariant &value) {
    if (value.userType() == qMetaTypeId<QDBusArgument>())
        return value.value<QDBusArgument>().currentSignature();
    if (value.userType() == qMetaTypeId<QDBusVariant>())
        return QStringLiteral("v");
    const char *signature = QDBusMetaType::typeToSignature(value.metaType());
    return signature ? QString::fromLatin1(signature) : QStringLiteral("?");
}

QDBusMessage invalidArgs(const QDBusMessage &message, const QString &expected) {
    return message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"),
                                    QStringLiteral("Type of message, '(%1)', does not match expected type '(%2)'")
                                        .arg(message.signature(), expected));
}

QDBusMessage unknownMethod(const QDBusMessage &message) {
    return message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
                                    QStringLiteral("Method '%1' on interface '%2' doesn't exist")
                                        .arg(message.member(), message.interface()));
}

// An IPv4 address as NetworkManager's older properties have it: a uint in
// network byte order.
uint networkOrder(const QString &address) {
    quint32 value = 0;
    for (const QString &part : address.split(QLatin1Char('.')))
        value = value << 8 | part.toUInt();
    return qToBigEndian(value);
}

QVariantMap deviceProperties(const QString &name, uint type, const QString &driver, const QString &hwAddress,
                             uint state, bool managed, uint capabilities) {
    return {
        { QStringLiteral("Udi"), QStringLiteral("/sys/devices/virtual/net/") + name },
        { QStringLiteral("Path"), QString() },
        { QStringLiteral("Interface"), name },
        { QStringLiteral("IpInterface"), QString() },
        { QStringLiteral("Driver"), driver },
        { QStringLiteral("DriverVersion"), QString() },
        { QStringLiteral("FirmwareVersion"), QString() },
        { QStringLiteral("Capabilities"), capabilities },
        { QStringLiteral("State"), state },
        { QStringLiteral("StateReason"), QVariant::fromValue(NmDeviceStateReason{ state, kReasonNone }) },
        { QStringLiteral("ActiveConnection"), pathValue(QString()) },
        { QStringLiteral("Ip4Config"), pathValue(QString()) },
        { QStringLiteral("Dhcp4Config"), pathValue(QString()) },
        { QStringLiteral("Ip6Config"), pathValue(QString()) },
        { QStringLiteral("Dhcp6Config"), pathValue(QString()) },
        { QStringLiteral("Managed"), managed },
        { QStringLiteral("Autoconnect"), true },
        { QStringLiteral("FirmwareMissing"), false },
        { QStringLiteral("NmPluginMissing"), false },
        { QStringLiteral("DeviceType"), type },
        { QStringLiteral("AvailableConnections"), pathsValue({}) },
        { QStringLiteral("PhysicalPortId"), QString() },
        { QStringLiteral("Mtu"), 1500u },
        { QStringLiteral("Metered"), 0u },
        { QStringLiteral("Real"), true },
        { QStringLiteral("Ip4Connectivity"), 0u },
        { QStringLiteral("Ip6Connectivity"), 0u },
        { QStringLiteral("InterfaceFlags"), 0u },
        { QStringLiteral("HwAddress"), hwAddress },
    };
}

QVariantMap wirelessProperties(const QString &hwAddress) {
    return {
        { QStringLiteral("HwAddress"), hwAddress },
        { QStringLiteral("PermHwAddress"), hwAddress },
        { QStringLiteral("Mode"), kModeInfra },
        { QStringLiteral("Bitrate"), 0u },
        { QStringLiteral("AccessPoints"), pathsValue({}) },
        { QStringLiteral("ActiveAccessPoint"), pathValue(QString()) },
        { QStringLiteral("WirelessCapabilities"), 0x7ffu },
        { QStringLiteral("LastScan"), QVariant::fromValue(qlonglong(-1)) },
    };
}

QVariantMap ip4Properties(const QString &address) {
    const QString network = address.section(QLatin1Char('.'), 0, 2);
    const QString gateway = network + QStringLiteral(".1");
    QVariantMap addressData{ { QStringLiteral("address"), address }, { QStringLiteral("prefix"), 24u } };
    QVariantMap localRoute{ { QStringLiteral("dest"), network + QStringLiteral(".0") },
                            { QStringLiteral("prefix"), 24u },
                            { QStringLiteral("metric"), 600u } };
    QVariantMap defaultRoute{ { QStringLiteral("dest"), QStringLiteral("0.0.0.0") },
                              { QStringLiteral("prefix"), 0u },
                              { QStringLiteral("next-hop"), gateway },
                              { QStringLiteral("metric"), 600u } };
    QVariantMap nameserver{ { QStringLiteral("address"), gateway } };
    QList<uint> oldAddress{ networkOrder(address), 24u, networkOrder(gateway) };
    return {
        { QStringLiteral("Addresses"), QVariant::fromValue(QList<QList<uint>>{ oldAddress }) },
        { QStringLiteral("AddressData"), QVariant::fromValue(QList<QVariantMap>{ addressData }) },
        { QStringLiteral("Gateway"), gateway },
        { QStringLiteral("Routes"), QVariant::fromValue(QList<QList<uint>>()) },
        { QStringLiteral("RouteData"), QVariant::fromValue(QList<QVariantMap>{ localRoute, defaultRoute }) },
        { QStringLiteral("NameserverData"), QVariant::fromValue(QList<QVariantMap>{ nameserver }) },
        { QStringLiteral("Nameservers"), QVariant::fromValue(QList<uint>{ networkOrder(gateway) }) },
        { QStringLiteral("Domains"), QStringList{ QStringLiteral("lan") } },
        { QStringLiteral("Searches"), QStringList() },
        { QStringLiteral("DnsOptions"), QStringList() },
        { QStringLiteral("DnsPriority"), 600 },
        { QStringLiteral("WinsServerData"), QStringList() },
    };
}

// What NetworkManager adds to a connection's settings as it saves it.
void complete(QMap<QString, QVariantMap> &settings) {
    QVariantMap &connection = settings[QStringLiteral("connection")];
    if (!connection.contains(QStringLiteral("type")))
        connection.insert(QStringLiteral("type"), QStringLiteral("802-11-wireless"));
    if (connection.value(QStringLiteral("type")).toString() == QLatin1String("802-11-wireless")) {
        QVariantMap &wireless = settings[QStringLiteral("802-11-wireless")];
        if (!connection.contains(QStringLiteral("id")))
            connection.insert(QStringLiteral("id"), QString::fromUtf8(wireless.value(QStringLiteral("ssid")).toByteArray()));
        if (!wireless.contains(QStringLiteral("mode")))
            wireless.insert(QStringLiteral("mode"), QStringLiteral("infrastructure"));
        if (!wireless.contains(QStringLiteral("seen-bssids")))
            wireless.insert(QStringLiteral("seen-bssids"), QStringList());
    }
    if (!connection.contains(QStringLiteral("uuid")))
        connection.insert(QStringLiteral("uuid"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!connection.contains(QStringLiteral("timestamp")))
        connection.insert(QStringLiteral("timestamp"), QVariant::fromValue(qulonglong(QDateTime::currentSecsSinceEpoch())));
    if (!connection.contains(QStringLiteral("permissions")))
        connection.insert(QStringLiteral("permissions"), QStringList());
    if (!settings.contains(QStringLiteral("ipv4"))) {
        settings.insert(QStringLiteral("ipv4"), {
            { QStringLiteral("method"), QStringLiteral("auto") },
            { QStringLiteral("address-data"), QVariant::fromValue(QList<QVariantMap>()) },
            { QStringLiteral("addresses"), QVariant::fromValue(QList<QList<uint>>()) },
            { QStringLiteral("dns"), QVariant::fromValue(QList<uint>()) },
            { QStringLiteral("dns-search"), QStringList() },
            { QStringLiteral("route-data"), QVariant::fromValue(QList<QVariantMap>()) },
            { QStringLiteral("routes"), QVariant::fromValue(QList<QList<uint>>()) },
        });
    }
    if (!settings.contains(QStringLiteral("ipv6"))) {
        settings.insert(QStringLiteral("ipv6"), {
            { QStringLiteral("method"), QStringLiteral("auto") },
            { QStringLiteral("addr-gen-mode"), 1 },
            { QStringLiteral("address-data"), QVariant::fromValue(QList<QVariantMap>()) },
            { QStringLiteral("dns-search"), QStringList() },
        });
    }
    if (!settings.contains(QStringLiteral("proxy")))
        settings.insert(QStringLiteral("proxy"), QVariantMap());
}

// Why NetworkManager would turn a new connection's settings down, "" when it
// wouldn't: the D-Bus type of each property WifiManager may send, and the
// values NetworkManager checks, in its words.
QString invalidSetting(const QMap<QString, QVariantMap> &settings) {
    static const QHash<QString, QString> types{
        { QStringLiteral("connection.id"), QStringLiteral("s") },
        { QStringLiteral("connection.uuid"), QStringLiteral("s") },
        { QStringLiteral("connection.type"), QStringLiteral("s") },
        { QStringLiteral("connection.autoconnect"), QStringLiteral("b") },
        { QStringLiteral("connection.interface-name"), QStringLiteral("s") },
        { QStringLiteral("802-11-wireless.ssid"), QStringLiteral("ay") },
        { QStringLiteral("802-11-wireless.mode"), QStringLiteral("s") },
        { QStringLiteral("802-11-wireless.hidden"), QStringLiteral("b") },
        { QStringLiteral("802-11-wireless-security.key-mgmt"), QStringLiteral("s") },
        { QStringLiteral("802-11-wireless-security.auth-alg"), QStringLiteral("s") },
        { QStringLiteral("802-11-wireless-security.psk"), QStringLiteral("s") },
        { QStringLiteral("802-11-wireless-security.wep-key0"), QStringLiteral("s") },
        { QStringLiteral("802-11-wireless-security.wep-key-type"), QStringLiteral("u") },
        { QStringLiteral("ipv4.method"), QStringLiteral("s") },
        { QStringLiteral("ipv6.method"), QStringLiteral("s") },
    };
    for (auto setting = settings.cbegin(); setting != settings.cend(); ++setting) {
        for (auto property = setting->cbegin(); property != setting->cend(); ++property) {
            const QString name = setting.key() + QLatin1Char('.') + property.key();
            const QString expected = types.value(name);
            const QString actual = signatureOf(property.value());
            if (!expected.isEmpty() && actual != expected)
                return QStringLiteral("%1: can't set property of type '%2' from value of type '%3'")
                    .arg(name, expected, actual);
        }
    }
    const QVariantMap connection = settings.value(QStringLiteral("connection"));
    if (connection.contains(QStringLiteral("type"))
        && connection.value(QStringLiteral("type")).toString() != QLatin1String("802-11-wireless"))
        return QStringLiteral("connection.type: property is invalid");
    const QVariantMap wireless = settings.value(QStringLiteral("802-11-wireless"));
    if (!wireless.contains(QStringLiteral("ssid")))
        return QStringLiteral("802-11-wireless.ssid: property is missing");
    const QByteArray ssid = wireless.value(QStringLiteral("ssid")).toByteArray();
    if (ssid.isEmpty() || ssid.size() > 32)
        return QStringLiteral("802-11-wireless.ssid: SSID length is out of range <1-32> bytes");
    static const QStringList modes{ QStringLiteral("infrastructure"), QStringLiteral("adhoc"), QStringLiteral("ap"),
                                    QStringLiteral("mesh") };
    if (wireless.contains(QStringLiteral("mode")) && !modes.contains(wireless.value(QStringLiteral("mode")).toString()))
        return QStringLiteral("802-11-wireless.mode: property is invalid");
    if (!settings.contains(QStringLiteral("802-11-wireless-security")))
        return QString();
    const QVariantMap security = settings.value(QStringLiteral("802-11-wireless-security"));
    const QString keyMgmt = security.value(QStringLiteral("key-mgmt")).toString();
    static const QStringList keyMgmts{ QStringLiteral("none"), QStringLiteral("ieee8021x"), QStringLiteral("owe"),
                                       QStringLiteral("wpa-psk"), QStringLiteral("sae"), QStringLiteral("wpa-eap"),
                                       QStringLiteral("wpa-eap-suite-b-192") };
    if (!keyMgmts.contains(keyMgmt))
        return QStringLiteral("802-11-wireless-security.key-mgmt: property is invalid");
    const bool hasPsk = security.contains(QStringLiteral("psk"));
    const QString psk = security.value(QStringLiteral("psk")).toString();
    static const QRegularExpression hexKey(QStringLiteral("^[0-9A-Fa-f]{64}$"));
    if (keyMgmt == QLatin1String("wpa-psk") && hasPsk && (psk.size() < 8 || psk.size() > 63)
        && !hexKey.match(psk).hasMatch())
        return QStringLiteral("802-11-wireless-security.psk: property is invalid");
    if (keyMgmt == QLatin1String("sae") && hasPsk && psk.isEmpty())
        return QStringLiteral("802-11-wireless-security.psk: property is invalid");
    if ((keyMgmt == QLatin1String("wpa-eap") || keyMgmt == QLatin1String("ieee8021x")
         || keyMgmt == QLatin1String("wpa-eap-suite-b-192"))
        && !settings.contains(QStringLiteral("802-1x")))
        return QStringLiteral("802-11-wireless-security.key-mgmt: '%1' security requires '802-1x' setting presence")
            .arg(keyMgmt);
    if (keyMgmt == QLatin1String("none")) {
        // A WEP key (1) of 5 or 13 characters, 10 or 26 hexadecimal digits,
        // or a passphrase (2).
        const uint keyType = security.value(QStringLiteral("wep-key-type")).toUInt();
        const QString key = security.value(QStringLiteral("wep-key0")).toString();
        static const QRegularExpression wepKey(QStringLiteral("^(.{5}|.{13}|[0-9A-Fa-f]{10}|[0-9A-Fa-f]{26})$"));
        if (keyType > 2)
            return QStringLiteral("802-11-wireless-security.wep-key-type: property is invalid");
        if (security.contains(QStringLiteral("wep-key0"))
            && ((keyType == 1 && !wepKey.match(key).hasMatch()) || (keyType == 2 && (key.isEmpty() || key.size() > 64))))
            return QStringLiteral("802-11-wireless-security.wep-key0: property is invalid");
    }
    return QString();
}

// What each interface has besides its properties, for introspection.
const QHash<QString, QString> &interfaceMembers() {
    static const QHash<QString, QString> members{
        { kObjectManager, QStringLiteral(
              "<method name=\"GetManagedObjects\"><arg name=\"objects\" type=\"a{oa{sa{sv}}}\" direction=\"out\"/></method>"
              "<signal name=\"InterfacesAdded\"><arg name=\"object_path\" type=\"o\"/>"
              "<arg name=\"interfaces_and_properties\" type=\"a{sa{sv}}\"/></signal>"
              "<signal name=\"InterfacesRemoved\"><arg name=\"object_path\" type=\"o\"/>"
              "<arg name=\"interfaces\" type=\"as\"/></signal>") },
        { kManager, QStringLiteral(
              "<method name=\"ActivateConnection\"><arg name=\"connection\" type=\"o\" direction=\"in\"/>"
              "<arg name=\"device\" type=\"o\" direction=\"in\"/><arg name=\"specific_object\" type=\"o\" direction=\"in\"/>"
              "<arg name=\"active_connection\" type=\"o\" direction=\"out\"/></method>"
              "<method name=\"AddAndActivateConnection\"><arg name=\"connection\" type=\"a{sa{sv}}\" direction=\"in\"/>"
              "<arg name=\"device\" type=\"o\" direction=\"in\"/><arg name=\"specific_object\" type=\"o\" direction=\"in\"/>"
              "<arg name=\"path\" type=\"o\" direction=\"out\"/><arg name=\"active_connection\" type=\"o\" direction=\"out\"/>"
              "</method>"
              "<method name=\"DeactivateConnection\"><arg name=\"active_connection\" type=\"o\" direction=\"in\"/></method>"
              "<signal name=\"StateChanged\"><arg name=\"state\" type=\"u\"/></signal>"
              "<signal name=\"DeviceAdded\"><arg name=\"device_path\" type=\"o\"/></signal>") },
        { kSettings, QStringLiteral(
              "<signal name=\"NewConnection\"><arg name=\"connection\" type=\"o\"/></signal>"
              "<signal name=\"ConnectionRemoved\"><arg name=\"connection\" type=\"o\"/></signal>") },
        { kSaved, QStringLiteral(
              "<method name=\"GetSettings\"><arg name=\"settings\" type=\"a{sa{sv}}\" direction=\"out\"/></method>"
              "<method name=\"Delete\"/><signal name=\"Updated\"/><signal name=\"Removed\"/>") },
        { kDevice, QStringLiteral(
              "<signal name=\"StateChanged\"><arg name=\"new_state\" type=\"u\"/><arg name=\"old_state\" type=\"u\"/>"
              "<arg name=\"reason\" type=\"u\"/></signal>") },
        { kWireless, QStringLiteral(
              "<method name=\"RequestScan\"><arg name=\"options\" type=\"a{sv}\" direction=\"in\"/></method>"
              "<signal name=\"AccessPointAdded\"><arg name=\"access_point\" type=\"o\"/></signal>"
              "<signal name=\"AccessPointRemoved\"><arg name=\"access_point\" type=\"o\"/></signal>") },
        { kActive, QStringLiteral(
              "<signal name=\"StateChanged\"><arg name=\"state\" type=\"u\"/><arg name=\"reason\" type=\"u\"/></signal>") },
    };
    return members;
}

} // namespace

FakeNetworkManager::FakeNetworkManager(QObject *parent) : QDBusVirtualObject(parent), m_bus(QString()) {
    qDBusRegisterMetaType<NmDeviceStateReason>();
    qDBusRegisterMetaType<QList<QVariantMap>>();
    qDBusRegisterMetaType<QList<QList<uint>>>();
    qDBusRegisterMetaType<Settings>();
    qDBusRegisterMetaType<NmManagedObjects>();
    m_clock.start();

    // The devices as on a Raspberry Pi: the loopback one NetworkManager leaves
    // alone, Ethernet without a cable, wlan0, and the Wi-Fi P2P device
    // wpa_supplicant makes beside it.
    const auto nextDevice = [this] { return QStringLiteral("/org/freedesktop/NetworkManager/Devices/%1").arg(m_nextDevice++); };
    const QString lo = nextDevice();
    exportObject(lo, {
        { kDevice, deviceProperties(QStringLiteral("lo"), kTypeLoopback, QString(), QStringLiteral("00:00:00:00:00:00"),
                                    kUnmanaged, false, 0x5u) },
        { kLoopback, QVariantMap() },
    });
    const QString eth0 = nextDevice();
    const QString ethAddress = QStringLiteral("DC:A6:32:00:00:01");
    exportObject(eth0, {
        { kDevice, deviceProperties(QStringLiteral("eth0"), kTypeEthernet, QStringLiteral("bcmgenet"), ethAddress,
                                    kUnavailable, true, 0x3u) },
        { kWired, { { QStringLiteral("HwAddress"), ethAddress },
                    { QStringLiteral("PermHwAddress"), ethAddress },
                    { QStringLiteral("Speed"), 0u },
                    { QStringLiteral("S390Subchannels"), QStringList() },
                    { QStringLiteral("Carrier"), false } } },
    });
    m_device = nextDevice();
    m_deviceState = radioOn() ? kDisconnected : kUnavailable;
    const QString wifiAddress = QStringLiteral("DC:A6:32:00:00:02");
    exportObject(m_device, {
        { kDevice, deviceProperties(QStringLiteral("wlan0"), kTypeWifi, QStringLiteral("brcmfmac"), wifiAddress,
                                    m_deviceState, true, 0x1u) },
        { kWireless, wirelessProperties(wifiAddress) },
    });
    const QString p2p = nextDevice();
    exportObject(p2p, {
        { kDevice, deviceProperties(QStringLiteral("p2p-dev-wlan0"), kTypeWifiP2P, QString(),
                                    QStringLiteral("DE:A6:32:00:00:02"), kDisconnected, true, 0x1u) },
        { kWifiP2P, { { QStringLiteral("HwAddress"), QStringLiteral("DE:A6:32:00:00:02") },
                      { QStringLiteral("Peers"), pathsValue({}) } } },
    });
    m_devices = { lo, eth0, m_device, p2p };

    exportObject(kManagerPath, { { kManager, {
        { QStringLiteral("Devices"), pathsValue(m_devices) },
        { QStringLiteral("AllDevices"), pathsValue(m_devices) },
        { QStringLiteral("Checkpoints"), pathsValue({}) },
        { QStringLiteral("NetworkingEnabled"), true },
        { QStringLiteral("WirelessEnabled"), m_wirelessEnabled },
        { QStringLiteral("WirelessHardwareEnabled"), !m_blocked },
        { QStringLiteral("WwanEnabled"), true },
        { QStringLiteral("WwanHardwareEnabled"), false },
        { QStringLiteral("ActiveConnections"), pathsValue({}) },
        { QStringLiteral("PrimaryConnection"), pathValue(QString()) },
        { QStringLiteral("PrimaryConnectionType"), QString() },
        { QStringLiteral("Metered"), 4u },
        { QStringLiteral("ActivatingConnection"), pathValue(QString()) },
        { QStringLiteral("Startup"), false },
        { QStringLiteral("Version"), QStringLiteral("1.52.0") },
        { QStringLiteral("Capabilities"), QVariant::fromValue(QList<uint>()) },
        { QStringLiteral("State"), kNmDisconnected },
        { QStringLiteral("Connectivity"), kConnectivityNone },
        { QStringLiteral("ConnectivityCheckAvailable"), false },
        { QStringLiteral("ConnectivityCheckEnabled"), false },
        { QStringLiteral("ConnectivityCheckUri"), QString() },
        { QStringLiteral("GlobalDnsConfiguration"), QVariantMap() },
    } } });
    exportObject(kSettingsPath, { { kSettings, {
        { QStringLiteral("Connections"), pathsValue({}) },
        { QStringLiteral("Hostname"), QStringLiteral("osdos") },
        { QStringLiteral("CanModify"), true },
        { QStringLiteral("VersionId"), QVariant::fromValue(qulonglong(1)) },
    } } });
    exportObject(QStringLiteral("/org/freedesktop/NetworkManager/AgentManager"), { { kAgentManager, QVariantMap() } });
    exportObject(QStringLiteral("/org/freedesktop/NetworkManager/DnsManager"), { { kDnsManager, {
        { QStringLiteral("Mode"), QStringLiteral("default") },
        { QStringLiteral("RcManager"), QStringLiteral("symlink") },
        { QStringLiteral("Configuration"), QVariant::fromValue(QList<QVariantMap>()) },
    } } });
}

FakeNetworkManager::~FakeNetworkManager() {
    stop();
}

bool FakeNetworkManager::start(const QDBusConnection &bus) {
    if (m_started)
        return true;
    m_bus = bus;
    if (!m_bus.isConnected() || !m_bus.registerVirtualObject(kRootPath, this, QDBusConnection::SubPath))
        return false;
    m_started = true;
    if (!m_bus.registerService(kService)) {
        m_bus.unregisterObject(kRootPath, QDBusConnection::UnregisterTree);
        m_started = false;
        return false;
    }
    return true;
}

void FakeNetworkManager::stop() {
    if (!m_started)
        return;
    m_bus.unregisterService(kService);
    m_bus.unregisterObject(kRootPath, QDBusConnection::UnregisterTree);
    m_started = false;
}

// --- The scenario ---

QString FakeNetworkManager::addAccessPoint(const QString &ssid, int strength, Security security,
                                           const QString &password, Hidden hidden) {
    const int n = m_nextAccessPoint++;
    const QString path = QStringLiteral("/org/freedesktop/NetworkManager/AccessPoint/%1").arg(n);
    AccessPoint ap;
    ap.ssid = ssid.toUtf8();
    ap.strength = strength;
    ap.security = security;
    ap.password = password;
    ap.hidden = hidden;
    // A made-up BSSID, and channel 1, 6 or 11 at 2.4 GHz, or one at 5 GHz.
    ap.bssid = QStringLiteral("0A:00:00:00:00:%1").arg(n, 2, 16, QLatin1Char('0')).toUpper();
    ap.frequency = n % 2 ? 2412 + 25 * uint(n % 3) : 5180 + 20 * uint(n % 4);
    m_aps.insert(path, ap);
    m_apOrder.append(path);
    if (radioOn()) {
        showAccessPoint(path);
        accessPointsChanged();
    }
    return path;
}

void FakeNetworkManager::setStrength(const QString &accessPoint, int strength) {
    const auto ap = m_aps.find(accessPoint);
    if (ap == m_aps.end())
        return;
    ap->strength = strength;
    change(accessPoint, kAccessPoint, { { QStringLiteral("Strength"), QVariant::fromValue(uchar(qBound(0, strength, 100))) } });
}

void FakeNetworkManager::removeAccessPoint(const QString &accessPoint) {
    if (!m_aps.contains(accessPoint))
        return;
    hideAccessPoint(accessPoint);
    m_aps.remove(accessPoint);
    m_apOrder.removeAll(accessPoint);
    accessPointsChanged();
}

QString FakeNetworkManager::addSaved(const QString &ssid, const QString &password, bool hidden) {
    const QByteArray bytes = ssid.toUtf8();
    Security security = password.isEmpty() ? Security::Open : Security::Wpa;
    for (const QString &path : std::as_const(m_apOrder)) {
        if (m_aps.value(path).ssid == bytes) {
            security = m_aps.value(path).security;
            break;
        }
    }
    Settings settings;
    settings.insert(QStringLiteral("connection"), { { QStringLiteral("id"), uniqueId(ssid) } });
    QVariantMap wireless{ { QStringLiteral("ssid"), bytes } };
    if (hidden)
        wireless.insert(QStringLiteral("hidden"), true);
    settings.insert(QStringLiteral("802-11-wireless"), wireless);
    const QString securityKey = QStringLiteral("802-11-wireless-security");
    switch (security) {
    case Security::Open:
        break;
    case Security::Owe:
        settings.insert(securityKey, { { QStringLiteral("key-mgmt"), QStringLiteral("owe") } });
        break;
    case Security::Wep: {
        static const QRegularExpression key(QStringLiteral("^(.{5}|.{13}|[0-9A-Fa-f]{10}|[0-9A-Fa-f]{26})$"));
        settings.insert(securityKey, { { QStringLiteral("key-mgmt"), QStringLiteral("none") },
                                       { QStringLiteral("wep-key0"), password },
                                       { QStringLiteral("wep-key-type"), key.match(password).hasMatch() ? 1u : 2u } });
        break;
    }
    case Security::Wpa:
    case Security::Sae:
        settings.insert(securityKey, { { QStringLiteral("key-mgmt"), security == Security::Sae ? QStringLiteral("sae")
                                                                                              : QStringLiteral("wpa-psk") },
                                       { QStringLiteral("psk"), password } });
        break;
    case Security::Enterprise:
        settings.insert(securityKey, { { QStringLiteral("key-mgmt"), QStringLiteral("wpa-eap") } });
        settings.insert(QStringLiteral("802-1x"), { { QStringLiteral("eap"), QStringList{ QStringLiteral("peap") } },
                                                    { QStringLiteral("identity"), QStringLiteral("student") },
                                                    { QStringLiteral("phase2-auth"), QStringLiteral("mschapv2") },
                                                    { QStringLiteral("password"), password } });
        break;
    }
    complete(settings);
    return save(settings);
}

QString FakeNetworkManager::addConnection(const QMap<QString, QVariantMap> &settings) {
    Settings completed = settings;
    complete(completed);
    return save(completed);
}

void FakeNetworkManager::renameSaved(const QString &saved, const QString &ssid) {
    const auto settings = m_settings.find(saved);
    if (settings == m_settings.end())
        return;
    (*settings)[QStringLiteral("802-11-wireless")].insert(QStringLiteral("ssid"), ssid.toUtf8());
    emitSignal(saved, kSaved, QStringLiteral("Updated"), {});
}

void FakeNetworkManager::join(const QString &saved) {
    if (!m_settings.contains(saved) || !radioOn())
        return;
    const QString active = startActivation(saved, QString());
    prepare(active, QString());
    succeed(active);
}

void FakeNetworkManager::setWirelessEnabled(bool on) {
    if (m_wirelessEnabled == on)
        return;
    m_wirelessEnabled = on;
    change(kManagerPath, kManager, { { QStringLiteral("WirelessEnabled"), on } });
    radioChanged();
}

void FakeNetworkManager::setHardwareBlocked(bool blocked) {
    if (m_blocked == blocked)
        return;
    m_blocked = blocked;
    change(kManagerPath, kManager, { { QStringLiteral("WirelessHardwareEnabled"), !blocked } });
    radioChanged();
}

void FakeNetworkManager::failNextActivation(uint deviceReason, uint activeReason) {
    m_failActivation = true;
    m_failDeviceReason = deviceReason;
    m_failActiveReason = activeReason;
}

void FakeNetworkManager::failNextCall(const QString &method, const QString &errorName, const QString &errorText) {
    m_failCalls.insert(method, { errorName, errorText });
}

void FakeNetworkManager::setDelay(int ms) {
    m_delay = ms;
}

void FakeNetworkManager::setHeld(bool held) {
    m_held = held;
    if (!held)
        proceed();
}

void FakeNetworkManager::proceed() {
    const QList<std::function<void()>> steps = std::exchange(m_waiting, {});
    for (const std::function<void()> &step : steps)
        step();
}

void FakeNetworkManager::setAddress(const QString &address) {
    m_address = address;
}

QString FakeNetworkManager::addDongle() {
    const QString path = QStringLiteral("/org/freedesktop/NetworkManager/Devices/%1").arg(m_nextDevice++);
    const QString hwAddress = QStringLiteral("00:13:EF:00:00:01");
    exportObject(path, {
        { kDevice, deviceProperties(QStringLiteral("wlan1"), kTypeWifi, QStringLiteral("rtl8xxxu"), hwAddress,
                                    radioOn() ? kDisconnected : kUnavailable, true, 0x1u) },
        { kWireless, wirelessProperties(hwAddress) },
    });
    m_devices.append(path);
    change(kManagerPath, kManager, { { QStringLiteral("Devices"), pathsValue(m_devices) },
                                     { QStringLiteral("AllDevices"), pathsValue(m_devices) } });
    emitSignal(kManagerPath, kManager, QStringLiteral("DeviceAdded"), { pathValue(path) });
    return path;
}

QVariantMap FakeNetworkManager::settings(const QString &saved) const {
    QVariantMap out;
    const Settings settings = m_settings.value(saved);
    for (auto it = settings.cbegin(); it != settings.cend(); ++it)
        out.insert(it.key(), plain(QVariant(it.value())));
    return out;
}

QList<FakeNetworkManager::Call> FakeNetworkManager::calls(const QString &method) const {
    QList<Call> out;
    for (const Call &call : m_calls) {
        if (call.method == method)
            out.append(call);
    }
    return out;
}

// --- On the bus ---

QString FakeNetworkManager::introspect(const QString &path) const {
    QString xml;
    if (path == kRootPath) {
        xml += QStringLiteral("<interface name=\"%1\">%2</interface>\n")
                   .arg(kObjectManager, interfaceMembers().value(kObjectManager));
    }
    const Settings interfaces = m_objects.value(path);
    for (auto it = interfaces.cbegin(); it != interfaces.cend(); ++it) {
        xml += QStringLiteral("<interface name=\"%1\">").arg(it.key()) + interfaceMembers().value(it.key());
        for (auto property = it->cbegin(); property != it->cend(); ++property) {
            const bool writable = it.key() == kManager && property.key() == QLatin1String("WirelessEnabled");
            xml += QStringLiteral("<property name=\"%1\" type=\"%2\" access=\"%3\"/>")
                       .arg(property.key(), signatureOf(property.value()),
                            writable ? QStringLiteral("readwrite") : QStringLiteral("read"));
        }
        xml += QStringLiteral("</interface>\n");
    }
    // The nodes under it.
    QStringList children;
    const QString prefix = path + QLatin1Char('/');
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        if (!it.key().startsWith(prefix))
            continue;
        const QString child = it.key().mid(prefix.size()).section(QLatin1Char('/'), 0, 0);
        if (!children.contains(child))
            children.append(child);
    }
    for (const QString &child : std::as_const(children))
        xml += QStringLiteral("<node name=\"%1\"/>\n").arg(child);
    return xml;
}

bool FakeNetworkManager::handleMessage(const QDBusMessage &message, const QDBusConnection &connection) {
    // Introspection is Qt's, with introspect()'s part in it.
    if (message.interface() == kIntrospectable
        || (message.interface().isEmpty() && message.member() == QLatin1String("Introspect")))
        return false;
    Call call;
    call.path = message.path();
    call.interface = message.interface();
    call.method = message.member();
    for (const QVariant &arg : message.arguments())
        call.args.append(plain(arg));
    const qsizetype index = m_calls.size();
    m_calls.append(call);
    const QDBusMessage reply = answer(message);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_calls[index].error = reply.errorName();
    } else {
        for (const QVariant &value : reply.arguments())
            m_calls[index].reply.append(plain(value));
    }
    connection.send(reply);
    emit called(message.member());
    return true;
}

QDBusMessage FakeNetworkManager::answer(const QDBusMessage &message) {
    const QString method = message.member();
    const auto failure = m_failCalls.find(method);
    if (failure != m_failCalls.end()) {
        const QPair<QString, QString> error = failure.value();
        m_failCalls.erase(failure);
        return message.createErrorReply(error.first, error.second);
    }
    const QString path = message.path();
    const QString interface = message.interface();
    if (path == kRootPath) {
        if (interface != kObjectManager || method != QLatin1String("GetManagedObjects"))
            return unknownMethod(message);
        if (!message.signature().isEmpty())
            return invalidArgs(message, QString());
        NmManagedObjects objects;
        for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it)
            objects.insert(QDBusObjectPath(it.key()), it.value());
        return message.createReply(QVariant::fromValue(objects));
    }
    if (!m_objects.contains(path)) {
        return message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"),
                                        QStringLiteral("No such object path '%1'").arg(path));
    }
    if (interface == kProperties)
        return properties(message);
    if (!m_objects.value(path).contains(interface)) {
        return message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
                                        QStringLiteral("No such interface '%1' on object at path %2").arg(interface, path));
    }
    if (interface == kManager) {
        if (method == QLatin1String("ActivateConnection"))
            return activateConnection(message);
        if (method == QLatin1String("AddAndActivateConnection"))
            return addAndActivateConnection(message);
        if (method == QLatin1String("DeactivateConnection"))
            return deactivateConnection(message);
    } else if (interface == kWireless && method == QLatin1String("RequestScan")) {
        return requestScan(message);
    } else if (interface == kSaved) {
        if (method == QLatin1String("GetSettings"))
            return getSettings(message);
        if (method == QLatin1String("Delete"))
            return deleteSaved(message);
    }
    return unknownMethod(message);
}

QDBusMessage FakeNetworkManager::properties(const QDBusMessage &message) {
    const QString method = message.member();
    const QVariantList args = message.arguments();
    const Settings object = m_objects.value(message.path());
    const auto unknownInterface = [&message](const QString &interface) {
        return message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownInterface"),
                                        QStringLiteral("No such interface '%1'").arg(interface));
    };
    const auto unknownProperty = [&message](const QString &name) {
        return message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownProperty"),
                                        QStringLiteral("No such property '%1'").arg(name));
    };
    if (method == QLatin1String("GetAll")) {
        if (message.signature() != QLatin1String("s"))
            return invalidArgs(message, QStringLiteral("s"));
        const QString interface = args.at(0).toString();
        if (!object.contains(interface))
            return unknownInterface(interface);
        return message.createReply(QVariant(object.value(interface)));
    }
    if (method == QLatin1String("Get") || method == QLatin1String("Set")) {
        const QString expected = method == QLatin1String("Get") ? QStringLiteral("ss") : QStringLiteral("ssv");
        if (message.signature() != expected)
            return invalidArgs(message, expected);
        const QString interface = args.at(0).toString();
        const QString name = args.at(1).toString();
        if (!object.contains(interface))
            return unknownInterface(interface);
        if (!object.value(interface).contains(name))
            return unknownProperty(name);
        if (method == QLatin1String("Get"))
            return message.createReply(QVariant::fromValue(QDBusVariant(object.value(interface).value(name))));
        if (message.path() != kManagerPath || interface != kManager || name != QLatin1String("WirelessEnabled")) {
            return message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.PropertyReadOnly"),
                                            QStringLiteral("Property '%1' is not writable").arg(name));
        }
        const QVariant value = args.at(2).value<QDBusVariant>().variant();
        if (value.userType() != QMetaType::Bool) {
            return message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"),
                                            QStringLiteral("Error setting property 'WirelessEnabled': "
                                                           "Expected type 'b' but got '%1'").arg(signatureOf(value)));
        }
        setWirelessEnabled(value.toBool());
        return message.createReply();
    }
    return unknownMethod(message);
}

// Why the device asked for can't take a connection, "" when it can. "/" is
// whichever NetworkManager picks: wlan0.
QString FakeNetworkManager::deviceProblem(const QString &device) const {
    if (device != QLatin1String("/") && device != m_device)
        return QStringLiteral("Device '%1' can't take a Wi-Fi connection here").arg(device);
    if (m_deviceState <= kUnavailable) {
        return QStringLiteral("No suitable device found for this connection "
                              "(device wlan0 not available because device is not available).");
    }
    return QString();
}

QDBusMessage FakeNetworkManager::activateConnection(const QDBusMessage &message) {
    if (message.signature() != QLatin1String("ooo"))
        return invalidArgs(message, QStringLiteral("ooo"));
    const QVariantList args = message.arguments();
    const QString saved = pathArgument(args.at(0));
    const QString specific = pathArgument(args.at(2));
    if (!m_settings.contains(saved)) {
        return message.createErrorReply(QStringLiteral("org.freedesktop.NetworkManager.UnknownConnection"),
                                        QStringLiteral("Connection '%1' is not available").arg(saved));
    }
    const QString problem = deviceProblem(args.at(1).value<QDBusObjectPath>().path());
    if (!problem.isEmpty())
        return message.createErrorReply(QStringLiteral("org.freedesktop.NetworkManager.UnknownDevice"), problem);
    if (!specific.isEmpty() && !(m_aps.contains(specific) && m_objects.contains(specific))) {
        return message.createErrorReply(QStringLiteral("org.freedesktop.NetworkManager.UnknownConnection"),
                                        QStringLiteral("The access point '%1' isn't in the scan list").arg(specific));
    }
    const QString active = startActivation(saved, specific);
    // The device's steps come after the answer, as NetworkManager's do.
    soon([this, active, specific] {
        prepare(active, specific);
        later([this, active] { conclude(active); });
    });
    return message.createReply(pathValue(active));
}

QDBusMessage FakeNetworkManager::addAndActivateConnection(const QDBusMessage &message) {
    if (message.signature() != QLatin1String("a{sa{sv}}oo"))
        return invalidArgs(message, QStringLiteral("a{sa{sv}}oo"));
    const QVariantList args = message.arguments();
    Settings settings = qdbus_cast<Settings>(args.at(0));
    const QString specific = pathArgument(args.at(2));
    const QString problem = deviceProblem(args.at(1).value<QDBusObjectPath>().path());
    if (!problem.isEmpty())
        return message.createErrorReply(QStringLiteral("org.freedesktop.NetworkManager.UnknownDevice"), problem);
    if (!specific.isEmpty() && !(m_aps.contains(specific) && m_objects.contains(specific))) {
        return message.createErrorReply(QStringLiteral("org.freedesktop.NetworkManager.UnknownConnection"),
                                        QStringLiteral("The access point '%1' isn't in the scan list").arg(specific));
    }
    // An SSID left out is the access point's.
    QVariantMap &wireless = settings[QStringLiteral("802-11-wireless")];
    if (!specific.isEmpty() && !wireless.contains(QStringLiteral("ssid")))
        wireless.insert(QStringLiteral("ssid"), m_aps.value(specific).ssid);
    const QString invalid = invalidSetting(settings);
    if (!invalid.isEmpty()) {
        return message.createErrorReply(
            QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection.InvalidProperty"), invalid);
    }
    complete(settings);
    const QString saved = save(settings);
    const QString active = startActivation(saved, specific);
    soon([this, active, specific] {
        prepare(active, specific);
        later([this, active] { conclude(active); });
    });
    return message.createReply(QVariantList{ pathValue(saved), pathValue(active) });
}

QDBusMessage FakeNetworkManager::deactivateConnection(const QDBusMessage &message) {
    if (message.signature() != QLatin1String("o"))
        return invalidArgs(message, QStringLiteral("o"));
    const QString active = pathArgument(message.arguments().at(0));
    if (active.isEmpty() || active != m_active || m_activeState >= kActiveDeactivating) {
        return message.createErrorReply(QStringLiteral("org.freedesktop.NetworkManager.ConnectionNotActive"),
                                        QStringLiteral("The connection was not active."));
    }
    beginLeaving(kReasonUserRequested, kActiveReasonUserDisconnected);
    later([this, active] {
        if (m_active == active)
            endLeaving(kDisconnected, kReasonUserRequested, kActiveReasonUserDisconnected);
    });
    return message.createReply();
}

QDBusMessage FakeNetworkManager::requestScan(const QDBusMessage &message) {
    if (message.signature() != QLatin1String("a{sv}"))
        return invalidArgs(message, QStringLiteral("a{sv}"));
    // The dongle hears nothing.
    if (message.path() != m_device)
        return message.createReply();
    if (m_deviceState <= kUnavailable) {
        return message.createErrorReply(QStringLiteral("org.freedesktop.NetworkManager.Device.NotAllowed"),
                                        QStringLiteral("Scanning not allowed while unavailable"));
    }
    // The scan's results a moment later: the access points seen again.
    QTimer::singleShot(m_delay, this, [this] {
        const qint64 now = m_clock.elapsed();
        change(m_device, kWireless, { { QStringLiteral("LastScan"), QVariant::fromValue(qlonglong(now + 3600 * 1000)) } });
        for (const QString &path : std::as_const(m_apOrder))
            change(path, kAccessPoint, { { QStringLiteral("LastSeen"), int(now / 1000 + 3600) } });
    });
    return message.createReply();
}

QDBusMessage FakeNetworkManager::getSettings(const QDBusMessage &message) {
    if (!message.signature().isEmpty())
        return invalidArgs(message, QString());
    // As NetworkManager gives them: without the secrets, which GetSecrets has.
    Settings settings = m_settings.value(message.path());
    static const QStringList secrets{ QStringLiteral("psk"), QStringLiteral("wep-key0"), QStringLiteral("wep-key1"),
                                      QStringLiteral("wep-key2"), QStringLiteral("wep-key3"),
                                      QStringLiteral("leap-password"), QStringLiteral("password") };
    for (auto it = settings.begin(); it != settings.end(); ++it) {
        for (const QString &secret : secrets)
            it->remove(secret);
    }
    return message.createReply(QVariant::fromValue(settings));
}

QDBusMessage FakeNetworkManager::deleteSaved(const QDBusMessage &message) {
    if (!message.signature().isEmpty())
        return invalidArgs(message, QString());
    removeSaved(message.path());
    return message.createReply();
}

// --- NetworkManager's objects ---

void FakeNetworkManager::exportObject(const QString &path, const Settings &interfaces) {
    m_objects.insert(path, interfaces);
    emitSignal(kRootPath, kObjectManager, QStringLiteral("InterfacesAdded"),
               { QVariant::fromValue(objectPath(path)), QVariant::fromValue(interfaces) });
}

void FakeNetworkManager::unexportObject(const QString &path) {
    const auto object = m_objects.find(path);
    if (object == m_objects.end())
        return;
    const QStringList interfaces = object->keys();
    m_objects.erase(object);
    emitSignal(kRootPath, kObjectManager, QStringLiteral("InterfacesRemoved"),
               { QVariant::fromValue(objectPath(path)), interfaces });
}

// The properties set, and PropertiesChanged with those that did change.
void FakeNetworkManager::change(const QString &path, const QString &interface, const QVariantMap &changed) {
    const auto object = m_objects.find(path);
    if (object == m_objects.end() || !object->contains(interface))
        return;
    QVariantMap &properties = (*object)[interface];
    QVariantMap really;
    for (auto it = changed.cbegin(); it != changed.cend(); ++it) {
        const auto old = properties.constFind(it.key());
        if (old != properties.cend() && old->userType() == it.value().userType() && *old == it.value())
            continue;
        properties.insert(it.key(), it.value());
        really.insert(it.key(), it.value());
    }
    if (!really.isEmpty())
        emitSignal(path, kProperties, QStringLiteral("PropertiesChanged"), { interface, really, QStringList() });
}

void FakeNetworkManager::emitSignal(const QString &path, const QString &interface, const QString &name,
                                    const QVariantList &args) {
    if (!m_started)
        return;
    QDBusMessage signal = QDBusMessage::createSignal(path, interface, name);
    signal.setArguments(args);
    m_bus.send(signal);
}

// The radio on or off as its switches have it. Off, the device is
// unavailable, off its network, and hears nothing; on, it is ready a moment
// later and hears the access points again.
void FakeNetworkManager::radioChanged() {
    if (!radioOn()) {
        if (m_deviceState <= kUnavailable)
            return;
        if (!m_active.isEmpty()) {
            beginLeaving(kReasonNone, kActiveReasonDeviceDisconnected);
            endLeaving(kUnavailable, kReasonNone, kActiveReasonDeviceDisconnected);
        } else {
            setDeviceState(kUnavailable, kReasonNone);
        }
        for (const QString &path : std::as_const(m_apOrder))
            hideAccessPoint(path);
        accessPointsChanged();
        return;
    }
    const auto ready = [this] {
        if (!radioOn() || m_deviceState > kUnavailable)
            return;
        setDeviceState(kDisconnected, kReasonSupplicantAvailable);
        for (const QString &path : std::as_const(m_apOrder))
            showAccessPoint(path);
        accessPointsChanged();
    };
    if (m_started)
        later(ready);
    else
        ready();
}

void FakeNetworkManager::showAccessPoint(const QString &path) {
    if (m_objects.contains(path) || !m_aps.contains(path))
        return;
    exportObject(path, { { kAccessPoint, accessPointProperties(m_aps.value(path)) } });
    emitSignal(m_device, kWireless, QStringLiteral("AccessPointAdded"), { pathValue(path) });
}

void FakeNetworkManager::hideAccessPoint(const QString &path) {
    if (!m_objects.contains(path))
        return;
    emitSignal(m_device, kWireless, QStringLiteral("AccessPointRemoved"), { pathValue(path) });
    unexportObject(path);
}

// The device's AccessPoints: those heard, in the order they were added.
void FakeNetworkManager::accessPointsChanged() {
    QStringList heard;
    for (const QString &path : std::as_const(m_apOrder)) {
        if (m_objects.contains(path))
            heard.append(path);
    }
    change(m_device, kWireless, { { QStringLiteral("AccessPoints"), pathsValue(heard) } });
}

QVariantMap FakeNetworkManager::accessPointProperties(const AccessPoint &ap) const {
    uint flags = kApPrivacy;
    uint rsn = kPairCcmp | kGroupCcmp;
    switch (ap.security) {
    case Security::Open:
        flags = 0;
        rsn = 0;
        break;
    case Security::Wep:
        rsn = 0;
        break;
    case Security::Owe:
        rsn |= kKeyOwe;
        break;
    case Security::Wpa:
        rsn |= kKeyPsk;
        break;
    case Security::Sae:
        rsn |= kKeySae;
        break;
    case Security::Enterprise:
        rsn |= kKey8021x;
        break;
    }
    QByteArray ssid = ap.ssid;
    if (!ap.revealed && ap.hidden == Hidden::Empty)
        ssid.clear();
    else if (!ap.revealed && ap.hidden == Hidden::Zeros)
        ssid.fill('\0');
    const bool fiveGhz = ap.frequency > 5000;
    return {
        { QStringLiteral("Flags"), flags },
        { QStringLiteral("WpaFlags"), 0u },
        { QStringLiteral("RsnFlags"), rsn },
        { QStringLiteral("Ssid"), ssid },
        { QStringLiteral("Frequency"), ap.frequency },
        { QStringLiteral("HwAddress"), ap.bssid },
        { QStringLiteral("Mode"), kModeInfra },
        { QStringLiteral("MaxBitrate"), fiveGhz ? 433300u : 144400u },
        { QStringLiteral("Bandwidth"), fiveGhz ? 80u : 20u },
        { QStringLiteral("Strength"), QVariant::fromValue(uchar(qBound(0, ap.strength, 100))) },
        { QStringLiteral("LastSeen"), int(m_clock.elapsed() / 1000 + 3600) },
    };
}

// The access point a connection joins: the one asked for, else the strongest
// of its name heard, a hidden one only for a connection saved as hidden.
QString FakeNetworkManager::accessPointFor(const QString &saved, const QString &specific) const {
    if (!specific.isEmpty() && m_aps.contains(specific) && m_objects.contains(specific))
        return specific;
    const QVariantMap wireless = m_settings.value(saved).value(QStringLiteral("802-11-wireless"));
    const QByteArray ssid = wireless.value(QStringLiteral("ssid")).toByteArray();
    const bool hidden = wireless.value(QStringLiteral("hidden")).toBool();
    QString best;
    for (const QString &path : m_apOrder) {
        const AccessPoint ap = m_aps.value(path);
        if (!m_objects.contains(path) || ap.ssid != ssid || (ap.hidden != Hidden::No && !ap.revealed && !hidden))
            continue;
        if (best.isEmpty() || ap.strength > m_aps.value(best).strength)
            best = path;
    }
    return best;
}

// --- Saved connections ---

QString FakeNetworkManager::save(const Settings &settings) {
    const QString path = QStringLiteral("/org/freedesktop/NetworkManager/Settings/%1").arg(m_nextSaved++);
    m_settings.insert(path, settings);
    const QString id = settings.value(QStringLiteral("connection")).value(QStringLiteral("id")).toString();
    exportObject(path, { { kSaved, {
        { QStringLiteral("Unsaved"), false },
        { QStringLiteral("Flags"), 0u },
        { QStringLiteral("Filename"), QStringLiteral("/etc/NetworkManager/system-connections/%1.nmconnection").arg(id) },
    } } });
    emitSignal(kSettingsPath, kSettings, QStringLiteral("NewConnection"), { pathValue(path) });
    savedChanged();
    return path;
}

void FakeNetworkManager::removeSaved(const QString &path) {
    // The network the device is on goes with it: NetworkManager leaves it.
    if (!m_active.isEmpty() && m_activeSaved == path) {
        beginLeaving(kReasonConnectionRemoved, kActiveReasonConnectionRemoved);
        endLeaving(kDisconnected, kReasonConnectionRemoved, kActiveReasonConnectionRemoved);
    }
    emitSignal(path, kSaved, QStringLiteral("Removed"), {});
    m_settings.remove(path);
    unexportObject(path);
    emitSignal(kSettingsPath, kSettings, QStringLiteral("ConnectionRemoved"), { pathValue(path) });
    savedChanged();
}

void FakeNetworkManager::savedChanged() {
    const QVariant saved = pathsValue(m_settings.keys());
    change(kSettingsPath, kSettings, { { QStringLiteral("Connections"), saved } });
    change(m_device, kDevice, { { QStringLiteral("AvailableConnections"), saved } });
}

QString FakeNetworkManager::uniqueId(const QString &name) const {
    QSet<QString> ids;
    for (const Settings &settings : m_settings)
        ids.insert(settings.value(QStringLiteral("connection")).value(QStringLiteral("id")).toString());
    QString id = name;
    for (int n = 1; ids.contains(id); ++n)
        id = QStringLiteral("%1 %2").arg(name).arg(n);
    return id;
}

// --- Joining and leaving ---

// The active connection, as NetworkManager has it before it answers: the
// device off the network it was on first.
QString FakeNetworkManager::startActivation(const QString &saved, const QString &specific) {
    if (!m_active.isEmpty()) {
        beginLeaving(kReasonNewActivation, kActiveReasonDeviceDisconnected);
        endLeaving(kDisconnected, kReasonNewActivation, kActiveReasonDeviceDisconnected);
    }
    const QVariantMap connection = m_settings.value(saved).value(QStringLiteral("connection"));
    const QString active = QStringLiteral("/org/freedesktop/NetworkManager/ActiveConnection/%1").arg(m_nextActive++);
    m_active = active;
    m_activeSaved = saved;
    m_activeState = kActivating;
    m_activeAp.clear();
    exportObject(active, { { kActive, {
        { QStringLiteral("Connection"), pathValue(saved) },
        { QStringLiteral("SpecificObject"), pathValue(specific) },
        { QStringLiteral("Id"), connection.value(QStringLiteral("id")).toString() },
        { QStringLiteral("Uuid"), connection.value(QStringLiteral("uuid")).toString() },
        { QStringLiteral("Type"), connection.value(QStringLiteral("type")).toString() },
        { QStringLiteral("Devices"), pathsValue({ m_device }) },
        { QStringLiteral("State"), kActivating },
        { QStringLiteral("StateFlags"), 0u },
        { QStringLiteral("Default"), false },
        { QStringLiteral("Ip4Config"), pathValue(QString()) },
        { QStringLiteral("Dhcp4Config"), pathValue(QString()) },
        { QStringLiteral("Default6"), false },
        { QStringLiteral("Ip6Config"), pathValue(QString()) },
        { QStringLiteral("Dhcp6Config"), pathValue(QString()) },
        { QStringLiteral("Vpn"), false },
        { QStringLiteral("Master"), pathValue(QString()) },
    } } });
    change(kManagerPath, kManager, { { QStringLiteral("ActiveConnections"), pathsValue({ active }) },
                                     { QStringLiteral("ActivatingConnection"), pathValue(active) } });
    return active;
}

// The first steps: the device prepares, and finds the access point.
void FakeNetworkManager::prepare(const QString &active, const QString &specific) {
    if (active != m_active || m_activeState != kActivating)
        return;
    setDeviceState(kPrepare, kReasonNone, { { QStringLiteral("ActiveConnection"), pathValue(active) } });
    setManagerState(kNmConnecting);
    m_activeAp = accessPointFor(m_activeSaved, specific);
    setDeviceState(kConfig, kReasonNone);
    if (m_activeAp.isEmpty())
        return;
    AccessPoint &ap = m_aps[m_activeAp];
    if (ap.hidden != Hidden::No && !ap.revealed) {
        // The supplicant learns a hidden network's name as it joins it.
        ap.revealed = true;
        change(m_activeAp, kAccessPoint, { { QStringLiteral("Ssid"), ap.ssid } });
    }
    change(m_device, kWireless, { { QStringLiteral("ActiveAccessPoint"), pathValue(m_activeAp) } });
    change(active, kActive, { { QStringLiteral("SpecificObject"), pathValue(m_activeAp) } });
}

// How it ends: joined, unless the scenario says otherwise, no access point
// of its name is heard, or the connection doesn't have what the access point
// wants.
void FakeNetworkManager::conclude(const QString &active) {
    if (active != m_active || m_activeState != kActivating)
        return;
    bool failed = false;
    uint deviceReason = kReasonNone;
    uint activeReason = kActiveReasonNone;
    if (m_failActivation) {
        m_failActivation = false;
        failed = true;
        deviceReason = m_failDeviceReason;
        activeReason = m_failActiveReason;
    } else if (m_activeAp.isEmpty()) {
        failed = true;
        deviceReason = kReasonSsidNotFound;
        activeReason = kActiveReasonDeviceDisconnected;
    } else {
        const Settings settings = m_settings.value(m_activeSaved);
        const QVariantMap security = settings.value(QStringLiteral("802-11-wireless-security"));
        const AccessPoint ap = m_aps.value(m_activeAp);
        QString wanted;
        QString secret;
        switch (ap.security) {
        case Security::Open:
            break;
        case Security::Owe:
            wanted = QStringLiteral("owe");
            break;
        case Security::Wep:
            wanted = QStringLiteral("none");
            secret = security.value(QStringLiteral("wep-key0")).toString();
            break;
        case Security::Wpa:
            wanted = QStringLiteral("wpa-psk");
            secret = security.value(QStringLiteral("psk")).toString();
            break;
        case Security::Sae:
            wanted = QStringLiteral("sae");
            secret = security.value(QStringLiteral("psk")).toString();
            break;
        case Security::Enterprise:
            wanted = QStringLiteral("wpa-eap");
            secret = settings.value(QStringLiteral("802-1x")).value(QStringLiteral("password")).toString();
            break;
        }
        if (security.value(QStringLiteral("key-mgmt")).toString() != wanted) {
            failed = true;
            deviceReason = kReasonSupplicantConfigFailed;
            activeReason = kActiveReasonDeviceDisconnected;
        } else if (secret != ap.password) {
            failed = true;
            deviceReason = kReasonNoSecrets;
            activeReason = kActiveReasonNoSecrets;
        }
    }
    if (failed)
        fail(active, deviceReason, activeReason);
    else
        succeed(active);
}

void FakeNetworkManager::succeed(const QString &active) {
    m_ip4 = QStringLiteral("/org/freedesktop/NetworkManager/IP4Config/%1").arg(m_nextIp4++);
    exportObject(m_ip4, { { kIp4Config, ip4Properties(m_address) } });
    setDeviceState(kIpConfig, kReasonNone, { { QStringLiteral("Ip4Config"), pathValue(m_ip4) },
                                             { QStringLiteral("IpInterface"), QStringLiteral("wlan0") } });
    change(active, kActive, { { QStringLiteral("Ip4Config"), pathValue(m_ip4) } });
    setDeviceState(kIpCheck, kReasonNone);
    setDeviceState(kSecondaries, kReasonNone);
    setDeviceState(kActivated, kReasonNone);
    change(m_device, kWireless, { { QStringLiteral("Bitrate"), 144400u } });
    change(active, kActive, { { QStringLiteral("Default"), true } });
    setActiveState(active, kActiveActivated, kActiveReasonNone);
    change(kManagerPath, kManager, { { QStringLiteral("PrimaryConnection"), pathValue(active) },
                                     { QStringLiteral("PrimaryConnectionType"), QStringLiteral("802-11-wireless") },
                                     { QStringLiteral("ActivatingConnection"), pathValue(QString()) },
                                     { QStringLiteral("Connectivity"), kConnectivityFull } });
    setManagerState(kNmConnectedGlobal);
}

void FakeNetworkManager::fail(const QString &active, uint deviceReason, uint activeReason) {
    // Wanting secrets, NetworkManager asks for them, and fails once no agent
    // has any.
    if (deviceReason == kReasonNoSecrets)
        setDeviceState(kNeedAuth, kReasonNone);
    setDeviceState(kFailed, deviceReason);
    setActiveState(active, kActiveDeactivated, activeReason);
    // Then at once disconnected, the active connection gone.
    endLeaving(kDisconnected, kReasonNone, activeReason);
}

void FakeNetworkManager::beginLeaving(uint deviceReason, uint activeReason) {
    if (m_active.isEmpty())
        return;
    setDeviceState(kDeactivating, deviceReason);
    setActiveState(m_active, kActiveDeactivating, activeReason);
}

// Off the network: the device in that state, the active connection
// deactivated (unless it is already) and removed, its IP4Config too.
void FakeNetworkManager::endLeaving(uint deviceState, uint deviceReason, uint activeReason) {
    if (m_active.isEmpty())
        return;
    const QString active = std::exchange(m_active, QString());
    const QString ip4 = std::exchange(m_ip4, QString());
    m_activeSaved.clear();
    m_activeAp.clear();
    setDeviceState(deviceState, deviceReason, { { QStringLiteral("ActiveConnection"), pathValue(QString()) },
                                                { QStringLiteral("Ip4Config"), pathValue(QString()) },
                                                { QStringLiteral("IpInterface"), QString() } });
    change(m_device, kWireless, { { QStringLiteral("ActiveAccessPoint"), pathValue(QString()) },
                                  { QStringLiteral("Bitrate"), 0u } });
    if (m_activeState != kActiveDeactivated)
        setActiveState(active, kActiveDeactivated, activeReason);
    m_activeState = 0;
    unexportObject(active);
    if (!ip4.isEmpty())
        unexportObject(ip4);
    change(kManagerPath, kManager, { { QStringLiteral("ActiveConnections"), pathsValue({}) },
                                     { QStringLiteral("PrimaryConnection"), pathValue(QString()) },
                                     { QStringLiteral("PrimaryConnectionType"), QString() },
                                     { QStringLiteral("ActivatingConnection"), pathValue(QString()) },
                                     { QStringLiteral("Connectivity"), kConnectivityNone } });
    setManagerState(kNmDisconnected);
}

// The device's State and StateReason, then StateChanged, as NetworkManager
// sends them.
void FakeNetworkManager::setDeviceState(uint state, uint reason, const QVariantMap &also) {
    const uint old = m_deviceState;
    m_deviceState = state;
    QVariantMap changed = also;
    changed.insert(QStringLiteral("State"), state);
    changed.insert(QStringLiteral("StateReason"), QVariant::fromValue(NmDeviceStateReason{ state, reason }));
    change(m_device, kDevice, changed);
    if (state != old)
        emitSignal(m_device, kDevice, QStringLiteral("StateChanged"), { state, old, reason });
}

void FakeNetworkManager::setActiveState(const QString &active, uint state, uint reason) {
    m_activeState = state;
    change(active, kActive, { { QStringLiteral("State"), state } });
    emitSignal(active, kActive, QStringLiteral("StateChanged"), { state, reason });
}

void FakeNetworkManager::setManagerState(uint state) {
    if (m_objects.value(kManagerPath).value(kManager).value(QStringLiteral("State")).toUInt() == state)
        return;
    change(kManagerPath, kManager, { { QStringLiteral("State"), state } });
    emitSignal(kManagerPath, kManager, QStringLiteral("StateChanged"), { state });
}

// A step as soon as the answer has gone, or once proceed() is called while
// held.
void FakeNetworkManager::soon(std::function<void()> step) {
    if (m_held) {
        m_waiting.append(std::move(step));
        return;
    }
    QTimer::singleShot(0, this, [step] { step(); });
}

// A step after the delay, or once proceed() is called while held.
void FakeNetworkManager::later(std::function<void()> step) {
    if (m_held) {
        m_waiting.append(std::move(step));
        return;
    }
    QTimer::singleShot(m_delay, this, [step] { step(); });
}
