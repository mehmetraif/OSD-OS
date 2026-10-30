#include "WifiManager.h"
#include "../util/LegacyNames.h"
#include "../util/RootRequest.h"

#include <QFile>
#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>
#include <memory>

#ifdef OSDOS_WIFI
#include "../util/DBusCall.h"
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QSet>
#endif

namespace {
// How long the search mode looks for networks before it stops by itself, and
// how often it asks for a scan meanwhile: NetworkManager turns down one asked
// for too soon after the one before.
constexpr int kSearchMs = 60 * 1000;
constexpr int kScanEveryMs = 10 * 1000;
// The launcher whose image lets the app's user use NetworkManager (polkit)
// and set the country (osdos-wifi.socket).
constexpr int kWifiApi = 7;
// For osdos-stop's answer: raspi-config sets the country, then turns Wi-Fi on.
constexpr int kCountryTimeoutMs = 20 * 1000;

struct Country {
    QString code;
    QString name;
};

// A name as it sorts: without its accents, in one case ("Åland" among the A's).
QString sortKey(const QString &name) {
    QString key;
    for (const QChar c : name.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing)
            key += c.toCaseFolded();
    }
    return key;
}

// The countries, by name, from the time zone database's list (iso3166.tab:
// "<code>\t<name>"), the one raspi-config checks a code against.
const QList<Country> &countryList() {
    static const QList<Country> list = [] {
        QList<Country> countries;
        QFile file(legacy::env("ZONEINFO_DIR", QStringLiteral("/usr/share/zoneinfo"))
                   + QStringLiteral("/iso3166.tab"));
        if (!file.open(QIODevice::ReadOnly))
            return countries;
        static const QRegularExpression line(QStringLiteral("^([A-Z]{2})\\t(.+)$"));
        for (const QString &text : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
            const QRegularExpressionMatch m = line.match(text.trimmed());
            if (m.hasMatch())
                countries.append({ m.captured(1), m.captured(2).trimmed() });
        }
        std::sort(countries.begin(), countries.end(), [](const Country &a, const Country &b) {
            return sortKey(a.name) < sortKey(b.name);
        });
        return countries;
    }();
    return list;
}

const Country *countryByCode(const QString &code) {
    for (const Country &c : countryList()) {
        if (c.code == code)
            return &c;
    }
    return nullptr;
}

#ifdef OSDOS_WIFI
const QString kService       = QStringLiteral("org.freedesktop.NetworkManager");
const QString kRootPath      = QStringLiteral("/org/freedesktop");
const QString kManagerPath   = QStringLiteral("/org/freedesktop/NetworkManager");
const QString kManager       = QStringLiteral("org.freedesktop.NetworkManager");
const QString kDevice        = QStringLiteral("org.freedesktop.NetworkManager.Device");
const QString kWireless      = QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless");
const QString kAccessPoint   = QStringLiteral("org.freedesktop.NetworkManager.AccessPoint");
const QString kActive        = QStringLiteral("org.freedesktop.NetworkManager.Connection.Active");
const QString kIp4Config     = QStringLiteral("org.freedesktop.NetworkManager.IP4Config");
const QString kSaved         = QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection");
const QString kProperties    = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kObjectManager = QStringLiteral("org.freedesktop.DBus.ObjectManager");

constexpr int kCallTimeoutMs = 10 * 1000;
// Long enough for NetworkManager's own tries, whose reasons are better than
// this: the supplicant's (25 s by default), then DHCP's (45 s).
constexpr int kConnectTimeoutMs = 90 * 1000;
// A device's type (NMDeviceType) and states (NMDeviceState).
constexpr uint kWifiDevice = 2;
constexpr uint kStateUnavailable = 20;
constexpr uint kStatePrepare = 40;
constexpr uint kStateActivated = 100;
constexpr uint kStateDeactivating = 110;
constexpr uint kStateFailed = 120;
// An active connection's states (NMActiveConnectionState).
constexpr uint kActiveActivated = 2;
constexpr uint kActiveDeactivated = 4;
// What an access point says of its security (NM80211ApFlags,
// NM80211ApSecurityFlags).
constexpr uint kApPrivacy = 0x1;
constexpr uint kKeyPsk = 0x100;
constexpr uint kKey8021x = 0x200;
constexpr uint kKeySae = 0x400;
constexpr uint kKeyOwe = 0x800;
constexpr uint kKeyOweTransition = 0x1000;
constexpr uint kKeyEapSuiteB = 0x2000;

QVariant plain(const QVariant &value);

// A D-Bus value with what QtDBus leaves wrapped (a container stays a
// QDBusArgument, to be read once) made plain Qt: a map a QVariantMap, an
// array or a structure a QVariantList, an object path a string. Bytes stay
// bytes (an SSID) and strings a QStringList.
QVariant plainArgument(const QDBusArgument &arg) {
    switch (arg.currentType()) {
    case QDBusArgument::BasicType:
    case QDBusArgument::VariantType:
        return plain(arg.asVariant());
    case QDBusArgument::ArrayType: {
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
    return value;
}

// An object path, "" for none ("/").
QString pathOf(const QVariant &value) {
    const QString path = value.toString();
    return path == QLatin1String("/") ? QString() : path;
}

QStringList pathsOf(const QVariant &value) {
    QStringList paths;
    for (const QVariant &v : value.toList()) {
        const QString path = pathOf(v);
        if (!path.isEmpty())
            paths.append(path);
    }
    return paths;
}

// The signal in bars, as nmcli draws it: four over 80, three over 55, two
// over 30, one over 5.
int barsOf(int strength) {
    return strength > 80 ? 4 : strength > 55 ? 3 : strength > 30 ? 2 : strength > 5 ? 1 : 0;
}

// How a network is joined, from what its access point says.
QString securityOf(const QVariantMap &ap) {
    const uint keys = ap.value(QStringLiteral("WpaFlags")).toUInt() | ap.value(QStringLiteral("RsnFlags")).toUInt();
    if (keys & (kKey8021x | kKeyEapSuiteB))
        return QStringLiteral("enterprise");
    if (keys & kKeyPsk)
        return QStringLiteral("wpa");
    if (keys & kKeySae)
        return QStringLiteral("sae");
    if (keys & kKeyOwe)
        return QStringLiteral("owe");
    // The open half of an Enhanced Open pair, or open.
    if (keys & kKeyOweTransition)
        return QStringLiteral("open");
    if (ap.value(QStringLiteral("Flags")).toUInt() & kApPrivacy)
        return QStringLiteral("wep");
    return QStringLiteral("open");
}

bool needsPassword(const QString &security) {
    return security == QLatin1String("wpa") || security == QLatin1String("sae")
        || security == QLatin1String("wep") || security == QLatin1String("enterprise");
}

// Why a password can't be the network's, "" when it can: what NetworkManager
// would refuse, said before it is asked.
QString passwordProblem(const QString &security, const QString &password) {
    static const QRegularExpression hexKey(QStringLiteral("^[0-9A-Fa-f]{64}$"));
    if (security == QLatin1String("wpa") && !hexKey.match(password).hasMatch()
        && (password.size() < 8 || password.size() > 63))
        return QStringLiteral("A Wi-Fi password is 8 to 63 characters");
    if (security == QLatin1String("sae") && (password.isEmpty() || password.size() > 63))
        return QStringLiteral("A Wi-Fi password is 1 to 63 characters");
    if (security == QLatin1String("wep") && (password.isEmpty() || password.size() > 64))
        return QStringLiteral("A WEP key is 1 to 64 characters");
    return QString();
}

// A new connection's settings for NetworkManager (a{sa{sv}}): a network, how
// it is joined and with what.
QMap<QString, QVariantMap> newConnection(const QByteArray &ssid, const QString &name, const QString &security,
                                         const QString &password, bool hidden) {
    QMap<QString, QVariantMap> settings;
    settings.insert(QStringLiteral("connection"), {
        { QStringLiteral("id"), name },
        { QStringLiteral("type"), QStringLiteral("802-11-wireless") },
    });
    QVariantMap wireless{
        { QStringLiteral("ssid"), ssid },
        { QStringLiteral("mode"), QStringLiteral("infrastructure") },
        // Without power saving (NM_SETTING_WIRELESS_POWERSAVE_DISABLE): the
        // radio sleeps between packets otherwise and wakes for each, a lag a
        // LAN game or a stream feels. The OSD/OS image says so for every
        // connection (osdos-wifi-powersave.conf); said here too, so that a
        // connection OSD/OS makes shows it (nmcli) and has it anywhere.
        { QStringLiteral("powersave"), 2u },
    };
    if (hidden)
        wireless.insert(QStringLiteral("hidden"), true);
    settings.insert(QStringLiteral("802-11-wireless"), wireless);
    QVariantMap secured;
    if (security == QLatin1String("wpa") || security == QLatin1String("sae")) {
        secured.insert(QStringLiteral("key-mgmt"), security == QLatin1String("sae") ? QStringLiteral("sae")
                                                                                     : QStringLiteral("wpa-psk"));
        secured.insert(QStringLiteral("psk"), password);
    } else if (security == QLatin1String("owe")) {
        secured.insert(QStringLiteral("key-mgmt"), QStringLiteral("owe"));
    } else if (security == QLatin1String("wep")) {
        // A key (5 or 13 characters, 10 or 26 hexadecimal digits), else a
        // passphrase (NMWepKeyType 1, 2).
        static const QRegularExpression key(QStringLiteral("^(.{5}|.{13}|[0-9A-Fa-f]{10}|[0-9A-Fa-f]{26})$"));
        secured.insert(QStringLiteral("key-mgmt"), QStringLiteral("none"));
        secured.insert(QStringLiteral("wep-key0"), password);
        secured.insert(QStringLiteral("wep-key-type"), key.match(password).hasMatch() ? 1u : 2u);
    }
    if (!secured.isEmpty())
        settings.insert(QStringLiteral("802-11-wireless-security"), secured);
    return settings;
}

// Why a network wasn't joined, in a few words: the device's reason as it
// failed (NMDeviceStateReason) where it says more, else the connection's
// (NMActiveConnectionStateReason).
QString failureOf(uint deviceReason, uint activeReason) {
    switch (deviceReason) {
    case 7:  // NO_SECRETS
    case 8:  // SUPPLICANT_DISCONNECT, a 4-way handshake that failed
        return QStringLiteral("wrong password?");
    case 9:  // SUPPLICANT_CONFIG_FAILED
        return QStringLiteral("it turned down how it was asked");
    case 11: // SUPPLICANT_TIMEOUT
        return QStringLiteral("no answer from it");
    case 5:  // IP_CONFIG_UNAVAILABLE
    case 6:  // IP_CONFIG_EXPIRED
        return QStringLiteral("it gave no address (DHCP)");
    case 53: // SSID_NOT_FOUND
        return QStringLiteral("it is out of reach");
    default:
        break;
    }
    switch (activeReason) {
    case 9:  // NO_SECRETS
    case 10: // LOGIN_FAILED
        return QStringLiteral("wrong password?");
    case 6:  // CONNECT_TIMEOUT
        return QStringLiteral("no answer from it");
    case 5:  // IP_CONFIG_INVALID
        return QStringLiteral("it gave no address (DHCP)");
    case 2:  // USER_DISCONNECTED
        return QStringLiteral("given up");
    default:
        return QStringLiteral("it couldn't be joined");
    }
}

// A failed call, in a few words for the screen.
QString describe(const QDBusMessage &reply) {
    const QString name = reply.errorName();
    const QString text = reply.errorMessage();
    if (name.endsWith(QLatin1String(".PermissionDenied")) || name.endsWith(QLatin1String(".AccessDenied")))
        return QStringLiteral("NetworkManager didn't allow it (polkit)");
    if (name == QLatin1String("org.freedesktop.DBus.Error.NoReply")
        || name == QLatin1String("org.freedesktop.DBus.Error.Timeout"))
        return QStringLiteral("no answer from NetworkManager");
    if (name == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown"))
        return QStringLiteral("NetworkManager isn't running");
    return text.isEmpty() ? name : text;
}
#endif
} // namespace

WifiManager::WifiManager(QObject *parent)
#ifdef OSDOS_WIFI
    : WifiManager(QDBusConnection::systemBus(), parent) {}

WifiManager::WifiManager(const QDBusConnection &bus, QObject *parent)
    : QObject(parent), m_bus(bus),
#else
    : QObject(parent),
#endif
      m_bootDir(legacy::env("BOOT_DIR", QStringLiteral("/boot/firmware"))) {
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(kSearchMs);
    connect(&m_searchTimer, &QTimer::timeout, this, &WifiManager::stopSearch);
    m_countryAvailable = rootrequest::available(QStringLiteral("wifi"), kWifiApi);
    readCountry();

#ifdef OSDOS_WIFI
    qDBusRegisterMetaType<QMap<QString, QVariantMap>>();
    m_scanTimer.setInterval(kScanEveryMs);
    connect(&m_scanTimer, &QTimer::timeout, this, &WifiManager::scan);
    m_attemptTimer.setSingleShot(true);
    m_attemptTimer.setInterval(kConnectTimeoutMs);
    connect(&m_attemptTimer, &QTimer::timeout, this, [this] {
        if (!m_attempt.active.isEmpty())
            call(kManagerPath, kManager, QStringLiteral("DeactivateConnection"),
                 { QVariant::fromValue(QDBusObjectPath(m_attempt.active)) }, kCallTimeoutMs);
        attemptDone(false, QStringLiteral("no answer from it"));
    });
    // A scan's answers come by the dozen: the lists are worked out once
    // they have.
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(50);
    connect(&m_refreshTimer, &QTimer::timeout, this, &WifiManager::refresh);

    if (!m_bus.isConnected()) {
        qWarning("[Wi-Fi] No system bus: %s", qPrintable(m_bus.lastError().message()));
        return;
    }
    m_watcher = new QDBusServiceWatcher(kService, m_bus,
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &WifiManager::load);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &WifiManager::onServiceUnregistered);

    m_bus.connect(kService, kRootPath, kObjectManager, QStringLiteral("InterfacesAdded"),
                  this, SLOT(onInterfacesAdded(QDBusMessage)));
    m_bus.connect(kService, kRootPath, kObjectManager, QStringLiteral("InterfacesRemoved"),
                  this, SLOT(onInterfacesRemoved(QDBusMessage)));
    m_bus.connect(kService, QString(), kProperties, QStringLiteral("PropertiesChanged"),
                  this, SLOT(onPropertiesChanged(QDBusMessage)));
    m_bus.connect(kService, QString(), kDevice, QStringLiteral("StateChanged"),
                  this, SLOT(onDeviceStateChanged(QDBusMessage)));
    m_bus.connect(kService, QString(), kActive, QStringLiteral("StateChanged"),
                  this, SLOT(onActiveStateChanged(QDBusMessage)));
    m_bus.connect(kService, QString(), kSaved, QStringLiteral("Updated"),
                  this, SLOT(onSavedUpdated(QDBusMessage)));

    // Running already (a desktop, or the app started again): taken now.
    // Otherwise the watcher says when it comes; asking for it before would
    // start it (D-Bus activation).
    if (m_bus.interface() && m_bus.interface()->isServiceRegistered(kService))
        load();
#endif
}

bool WifiManager::supported() const {
#ifdef OSDOS_WIFI
    return !legacy::envIsSet("AUTOSTART") || legacy::envInt("LAUNCHER_API") >= kWifiApi;
#else
    return false;
#endif
}

QString WifiManager::countryName() const {
    const Country *c = countryByCode(m_country);
    return c ? c->name : QString();
}

QString WifiManager::suggestedCountry() const {
    const QString code = QLocale::territoryToCode(QTimeZone::systemTimeZone().territory());
    return countryByCode(code) ? code : QString();
}

QVariantList WifiManager::countries() const {
    QVariantList list;
    for (const Country &c : countryList())
        list.append(QVariantMap{ { QStringLiteral("code"), c.code }, { QStringLiteral("name"), c.name },
                                 { QStringLiteral("letter"), sortKey(c.name).left(1).toUpper() } });
    return list;
}

void WifiManager::setEnabled(bool on) {
#ifdef OSDOS_WIFI
    if (!m_available)
        return;
    clearMessage();
    if (!on)
        stopSearch();
    call(kManagerPath, kProperties, QStringLiteral("Set"),
         { kManager, QStringLiteral("WirelessEnabled"), QVariant::fromValue(QDBusVariant(on)) }, kCallTimeoutMs,
         [this, on](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage)
                 setMessage((on ? QStringLiteral("Couldn't turn Wi-Fi on: ") : QStringLiteral("Couldn't turn Wi-Fi off: "))
                            + describe(reply));
         });
#else
    Q_UNUSED(on)
#endif
}

void WifiManager::startSearch() {
#ifdef OSDOS_WIFI
    if (!m_enabled)
        return;
    clearMessage();
    if (!m_searching) {
        m_searching = true;
        emit searchingChanged();
    }
    scan();
    m_scanTimer.start();
    m_searchTimer.start();
#endif
}

void WifiManager::stopSearch() {
    m_searchTimer.stop();
#ifdef OSDOS_WIFI
    m_scanTimer.stop();
#endif
    if (m_searching) {
        m_searching = false;
        emit searchingChanged();
    }
}

void WifiManager::connectTo(const QString &ssid, const QString &password) {
#ifdef OSDOS_WIFI
    if (m_device.isEmpty() || !m_enabled || ssid.isEmpty())
        return;
    clearMessage();
    const QByteArray bytes = m_ssids.value(ssid, ssid.toUtf8());
    const QStringList saved = savedFor(bytes);
    const Heard heard = m_heard.value(bytes);
    if (password.isEmpty() && !saved.isEmpty()) {
        // As it was saved. NetworkManager takes the access point it hears
        // best for it.
        startAttempt(ssid, bytes, {});
        const int attempt = m_attemptId;
        call(kManagerPath, kManager, QStringLiteral("ActivateConnection"),
             { QVariant::fromValue(QDBusObjectPath(saved.constFirst())), QVariant::fromValue(QDBusObjectPath(m_device)),
               QVariant::fromValue(QDBusObjectPath(QStringLiteral("/"))) },
             kCallTimeoutMs, [this, attempt](const QDBusMessage &reply) { activated(attempt, reply, false); });
        return;
    }
    const QString security = heard.path.isEmpty() ? (password.isEmpty() ? QStringLiteral("open") : QStringLiteral("wpa"))
                                                  : heard.security;
    if (security == QLatin1String("enterprise")) {
        setMessage(ssid + QStringLiteral(" wants a user name too (WPA Enterprise), which can't be typed here"));
        return;
    }
    if (needsPassword(security) && password.isEmpty()) {
        setMessage(ssid + QStringLiteral(" wants its password"));
        return;
    }
    addAndActivate(ssid, bytes, security, password, false, heard.path);
#else
    Q_UNUSED(ssid)
    Q_UNUSED(password)
#endif
}

void WifiManager::connectHidden(const QString &ssid, const QString &password) {
#ifdef OSDOS_WIFI
    if (m_device.isEmpty() || !m_enabled || ssid.isEmpty())
        return;
    clearMessage();
    addAndActivate(ssid, ssid.toUtf8(), password.isEmpty() ? QStringLiteral("open") : QStringLiteral("wpa"),
                   password, true, QString());
#else
    Q_UNUSED(ssid)
    Q_UNUSED(password)
#endif
}

void WifiManager::disconnectNetwork() {
#ifdef OSDOS_WIFI
    const QString active = pathOf(interfaceOf(m_device, kDevice).value(QStringLiteral("ActiveConnection")));
    if (active.isEmpty())
        return;
    clearMessage();
    call(kManagerPath, kManager, QStringLiteral("DeactivateConnection"), { QVariant::fromValue(QDBusObjectPath(active)) },
         kCallTimeoutMs, [this](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage)
                 setMessage(QStringLiteral("Couldn't disconnect: ") + describe(reply));
         });
#endif
}

void WifiManager::forget(const QString &ssid) {
#ifdef OSDOS_WIFI
    const QStringList saved = savedFor(m_ssids.value(ssid, ssid.toUtf8()));
    if (saved.isEmpty())
        return;
    clearMessage();
    // Every connection saved for it, and a line once all have answered.
    auto left = std::make_shared<int>(int(saved.size()));
    auto failure = std::make_shared<QString>();
    for (const QString &path : saved) {
        call(path, kSaved, QStringLiteral("Delete"), {}, kCallTimeoutMs,
             [this, ssid, left, failure](const QDBusMessage &reply) {
                 if (reply.type() == QDBusMessage::ErrorMessage && failure->isEmpty())
                     *failure = describe(reply);
                 if (--*left > 0)
                     return;
                 setMessage(failure->isEmpty() ? ssid + QStringLiteral(" is forgotten")
                                               : QStringLiteral("Couldn't forget ") + ssid + QStringLiteral(": ") + *failure);
             });
    }
#else
    Q_UNUSED(ssid)
#endif
}

bool WifiManager::setCountry(const QString &code) {
    if (!m_countryAvailable || m_countryBusy || !countryByCode(code))
        return false;
    clearMessage();
    m_countryBusy = true;
    emit countryChanged();

    // osdos-stop wifi-country, as root, behind the socket: the code on a
    // line, "ok" or why not back.
    rootrequest::ask(this, QStringLiteral("wifi"), code.toLatin1(), kCountryTimeoutMs,
                     [this, code](const rootrequest::Answer &answer) {
        m_countryBusy = false;
        // As the boot partition has it now, whatever the answer.
        readCountry();
        emit countryChanged();
        if (answer.ok) {
            setMessage(QStringLiteral("Wi-Fi country set to ") + countryName());
        } else {
            qWarning("[Wi-Fi] The country wasn't set to %s: %s", qPrintable(code), qPrintable(answer.why));
            setMessage(QStringLiteral("Couldn't set the country: ") + answer.why);
        }
    });
    return true;
}

void WifiManager::clearMessage() {
    if (m_message.isEmpty())
        return;
    m_message.clear();
    emit messageChanged();
}

void WifiManager::setMessage(const QString &text) {
    if (m_message == text)
        return;
    m_message = text;
    emit messageChanged();
}

// The country raspi-config has set: the kernel's regulatory domain on the
// command line it starts with (cfg80211.ieee80211_regdom=<code>).
void WifiManager::readCountry() {
    QFile file(m_bootDir + QStringLiteral("/cmdline.txt"));
    QString code;
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression regdom(QStringLiteral("(?:^|\\s)cfg80211\\.ieee80211_regdom=(\\S+)"));
        code = regdom.match(QString::fromUtf8(file.readAll())).captured(1);
    }
    m_country = countryByCode(code) ? code : QString();
}

#ifdef OSDOS_WIFI

// --- NetworkManager's objects ---

void WifiManager::onServiceUnregistered() {
    stopSearch();
    m_objects.clear();
    m_saved.clear();
    if (m_attempting) {
        m_attemptTimer.stop();
        m_attempting = false;
        m_attempt = Attempt();
        ++m_attemptId;
    }
    refresh();
}

void WifiManager::load() {
    call(kRootPath, kObjectManager, QStringLiteral("GetManagedObjects"), {}, kCallTimeoutMs,
         [this](const QDBusMessage &reply) {
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qWarning("[Wi-Fi] Couldn't list NetworkManager's objects: %s", qPrintable(reply.errorMessage()));
            return;
        }
        const QVariantMap objects = plain(reply.arguments().constFirst()).toMap();
        m_objects.clear();
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            m_objects.insert(it.key(), it.value().toMap());
            if (m_objects.value(it.key()).contains(kSaved))
                fetchSaved(it.key());
        }
        refresh();
    });
}

// A saved connection's settings: kept if it is a Wi-Fi network's.
void WifiManager::fetchSaved(const QString &path) {
    call(path, kSaved, QStringLiteral("GetSettings"), {}, kCallTimeoutMs, [this, path](const QDBusMessage &reply) {
        // Deleted meanwhile.
        if (!m_objects.value(path).contains(kSaved))
            return;
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qWarning("[Wi-Fi] Couldn't read the connection %s: %s", qPrintable(path), qPrintable(reply.errorMessage()));
            return;
        }
        const QVariantMap settings = plain(reply.arguments().constFirst()).toMap();
        const QVariantMap connection = settings.value(QStringLiteral("connection")).toMap();
        const QVariantMap wireless = settings.value(QStringLiteral("802-11-wireless")).toMap();
        const QString mode = wireless.value(QStringLiteral("mode")).toString();
        const QByteArray ssid = wireless.value(QStringLiteral("ssid")).toByteArray();
        // A network joined, not one the Pi makes (a hotspot).
        if (connection.value(QStringLiteral("type")).toString() == QLatin1String("802-11-wireless") && !ssid.isEmpty()
            && (mode.isEmpty() || mode == QLatin1String("infrastructure"))) {
            m_saved.insert(path, { { QStringLiteral("ssid"), ssid },
                                   { QStringLiteral("hidden"), wireless.value(QStringLiteral("hidden")).toBool() } });
        } else {
            m_saved.remove(path);
        }
        scheduleRefresh();
    });
}

void WifiManager::onInterfacesAdded(const QDBusMessage &message) {
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2)
        return;
    const QString path = plain(args.at(0)).toString();
    const QVariantMap interfaces = plain(args.at(1)).toMap();
    for (auto it = interfaces.cbegin(); it != interfaces.cend(); ++it)
        merge(path, it.key(), it.value().toMap());
    if (interfaces.contains(kSaved))
        fetchSaved(path);
    scheduleRefresh();
}

void WifiManager::onInterfacesRemoved(const QDBusMessage &message) {
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2)
        return;
    const QString path = plain(args.at(0)).toString();
    const QStringList interfaces = plain(args.at(1)).toStringList();
    auto object = m_objects.find(path);
    if (object == m_objects.end())
        return;
    for (const QString &interface : interfaces)
        object->remove(interface);
    if (object->isEmpty())
        m_objects.erase(object);
    if (interfaces.contains(kSaved))
        m_saved.remove(path);
    // The connection on its way gone without a word of how it ended.
    if (m_attempting && path == m_attempt.active && interfaces.contains(kActive))
        attemptDone(false, failureOf(m_attempt.deviceReason, 0));
    scheduleRefresh();
}

void WifiManager::onPropertiesChanged(const QDBusMessage &message) {
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2 || !m_objects.contains(message.path()))
        return;
    merge(message.path(), args.at(0).toString(), plain(args.at(1)).toMap(),
          args.size() > 2 ? plain(args.at(2)).toStringList() : QStringList());
    scheduleRefresh();
}

// A device's state as it changes, with why: kept for the connection on its
// way, should it fail.
void WifiManager::onDeviceStateChanged(const QDBusMessage &message) {
    const QList<QVariant> args = message.arguments();
    if (args.size() < 3 || message.path() != m_device || !m_attempting)
        return;
    if (args.at(0).toUInt() == kStateFailed)
        m_attempt.deviceReason = args.at(2).toUInt();
}

void WifiManager::onActiveStateChanged(const QDBusMessage &message) {
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2 || !m_attempting || message.path() != m_attempt.active)
        return;
    const uint state = args.at(0).toUInt();
    if (state == kActiveActivated)
        attemptDone(true, QString());
    else if (state == kActiveDeactivated)
        attemptDone(false, failureOf(m_attempt.deviceReason, args.at(1).toUInt()));
}

void WifiManager::onSavedUpdated(const QDBusMessage &message) {
    if (m_objects.value(message.path()).contains(kSaved))
        fetchSaved(message.path());
}

// --- Connecting ---

void WifiManager::addAndActivate(const QString &name, const QByteArray &ssid, const QString &security,
                                 const QString &password, bool hidden, const QString &accessPoint) {
    const QString problem = needsPassword(security) ? passwordProblem(security, password) : QString();
    if (!problem.isEmpty()) {
        setMessage(problem);
        return;
    }
    // Saved before with another password, or as another network: replaced
    // once this one is joined.
    startAttempt(name, ssid, savedFor(ssid));
    const int attempt = m_attemptId;
    call(kManagerPath, kManager, QStringLiteral("AddAndActivateConnection"),
         { QVariant::fromValue(newConnection(ssid, name, security, password, hidden)),
           QVariant::fromValue(QDBusObjectPath(m_device)),
           QVariant::fromValue(QDBusObjectPath(accessPoint.isEmpty() ? QStringLiteral("/") : accessPoint)) },
         kCallTimeoutMs, [this, attempt](const QDBusMessage &reply) { activated(attempt, reply, true); });
}

void WifiManager::startAttempt(const QString &name, const QByteArray &ssid, const QStringList &replaces) {
    // One on its way is given up for this one, which NetworkManager starts
    // in its place: what was added for it goes now, or as NetworkManager's
    // answer for it comes (activated()).
    if (m_attempting && !m_attempt.added.isEmpty())
        call(m_attempt.added, kSaved, QStringLiteral("Delete"), {}, kCallTimeoutMs);
    // Scans get in the way of joining: the search waits while it lasts.
    m_attempting = true;
    m_attempt = Attempt();
    m_attempt.name = name;
    m_attempt.ssid = ssid;
    m_attempt.replaces = replaces;
    ++m_attemptId;
    m_attemptTimer.start();
    scheduleRefresh();
}

// NetworkManager's answer to ActivateConnection (o) or
// AddAndActivateConnection (o, o): the connection's on its way.
void WifiManager::activated(int attempt, const QDBusMessage &reply, bool added) {
    const QList<QVariant> args = reply.arguments();
    if (attempt != m_attemptId || !m_attempting) {
        // Given up meanwhile: what was added for it goes.
        if (added && reply.type() == QDBusMessage::ReplyMessage && !args.isEmpty())
            call(plain(args.at(0)).toString(), kSaved, QStringLiteral("Delete"), {}, kCallTimeoutMs);
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage || args.size() < (added ? 2 : 1)) {
        attemptDone(false, describe(reply));
        return;
    }
    if (added)
        m_attempt.added = plain(args.at(0)).toString();
    m_attempt.active = plain(args.at(added ? 1 : 0)).toString();
    checkAttempt();
}

// Where the connection has got to already, should its state have changed
// before the answer came.
void WifiManager::checkAttempt() {
    if (!m_attempting || m_attempt.active.isEmpty() || !m_objects.contains(m_attempt.active))
        return;
    const uint state = interfaceOf(m_attempt.active, kActive).value(QStringLiteral("State")).toUInt();
    if (state == kActiveActivated)
        attemptDone(true, QString());
    else if (state == kActiveDeactivated)
        attemptDone(false, failureOf(m_attempt.deviceReason, 0));
}

void WifiManager::attemptDone(bool joined, const QString &why) {
    if (!m_attempting)
        return;
    m_attemptTimer.stop();
    const Attempt attempt = m_attempt;
    m_attempting = false;
    m_attempt = Attempt();
    ++m_attemptId;
    if (joined) {
        for (const QString &path : attempt.replaces)
            call(path, kSaved, QStringLiteral("Delete"), {}, kCallTimeoutMs);
        setMessage(QStringLiteral("Connected to ") + attempt.name);
    } else {
        // Not kept: NetworkManager would only try it again, with the
        // password that didn't do.
        if (!attempt.added.isEmpty())
            call(attempt.added, kSaved, QStringLiteral("Delete"), {}, kCallTimeoutMs);
        qWarning("[Wi-Fi] %s wasn't joined: %s", qPrintable(attempt.name), qPrintable(why));
        setMessage(attempt.name + QStringLiteral(": ") + why);
    }
    scheduleRefresh();
}

void WifiManager::scan() {
    if (m_device.isEmpty() || !m_enabled || m_attempting)
        return;
    call(m_device, kWireless, QStringLiteral("RequestScan"), { QVariant::fromValue(QVariantMap()) }, kCallTimeoutMs,
         [this](const QDBusMessage &reply) {
             // One asked for too soon, or while the device isn't ready, is
             // only turned down: the next is asked for in a moment.
             if (reply.type() == QDBusMessage::ErrorMessage && !reply.errorName().endsWith(QLatin1String(".NotAllowed")))
                 setMessage(QStringLiteral("Couldn't search: ") + describe(reply));
         });
}

// --- What the page shows ---

void WifiManager::merge(const QString &path, const QString &interface, const QVariantMap &changed,
                        const QStringList &invalidated) {
    QVariantMap &object = m_objects[path];
    QVariantMap properties = object.value(interface).toMap();
    properties.insert(changed);
    for (const QString &key : invalidated)
        properties.remove(key);
    object.insert(interface, properties);
}

QVariantMap WifiManager::interfaceOf(const QString &path, const QString &interface) const {
    return path.isEmpty() ? QVariantMap() : m_objects.value(path).value(interface).toMap();
}

QStringList WifiManager::savedFor(const QByteArray &ssid) const {
    QStringList paths;
    for (auto it = m_saved.cbegin(); it != m_saved.cend(); ++it) {
        if (it.value().value(QStringLiteral("ssid")).toByteArray() == ssid)
            paths.append(it.key());
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

void WifiManager::scheduleRefresh() {
    m_refreshTimer.start();
}

void WifiManager::refresh() {
    m_refreshTimer.stop();
    // The device: the Wi-Fi one NetworkManager manages, the first by its
    // interface's name (wlan0 before a dongle's wlan1).
    QString device;
    QString deviceName;
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        const QVariantMap properties = it.value().value(kDevice).toMap();
        if (properties.value(QStringLiteral("DeviceType")).toUInt() != kWifiDevice || !it.value().contains(kWireless)
            || !properties.value(QStringLiteral("Managed"), true).toBool())
            continue;
        const QString name = properties.value(QStringLiteral("Interface")).toString();
        if (device.isEmpty() || name < deviceName) {
            device = it.key();
            deviceName = name;
        }
    }
    m_device = device;
    const QVariantMap manager = interfaceOf(kManagerPath, kManager);
    const QVariantMap deviceProperties = interfaceOf(device, kDevice);
    const QVariantMap wireless = interfaceOf(device, kWireless);
    const bool available = !device.isEmpty();
    const bool enabled = available && manager.value(QStringLiteral("WirelessEnabled")).toBool();
    const bool blocked = available && !manager.value(QStringLiteral("WirelessHardwareEnabled"), true).toBool();
    const uint deviceState = deviceProperties.value(QStringLiteral("State")).toUInt();

    // The networks heard: the strongest access point of each name. One that
    // doesn't say its name is left out.
    QHash<QByteArray, Heard> heard;
    for (const QString &path : pathsOf(wireless.value(QStringLiteral("AccessPoints")))) {
        const QVariantMap ap = interfaceOf(path, kAccessPoint);
        const QByteArray ssid = ap.value(QStringLiteral("Ssid")).toByteArray();
        if (ssid.isEmpty() || ssid.count('\0') == ssid.size())
            continue;
        const int strength = ap.value(QStringLiteral("Strength")).toInt();
        auto found = heard.find(ssid);
        if (found == heard.end() || strength > found->strength)
            heard.insert(ssid, { path, strength, securityOf(ap) });
    }
    m_heard = heard;

    // The network the device is on, or on its way to: its saved
    // connection's, else its access point's.
    const QVariantMap active = interfaceOf(pathOf(deviceProperties.value(QStringLiteral("ActiveConnection"))), kActive);
    const QVariantMap activeAp = interfaceOf(pathOf(wireless.value(QStringLiteral("ActiveAccessPoint"))), kAccessPoint);
    QByteArray activeSsid = m_saved.value(pathOf(active.value(QStringLiteral("Connection"))))
                                .value(QStringLiteral("ssid")).toByteArray();
    if (activeSsid.isEmpty())
        activeSsid = activeAp.value(QStringLiteral("Ssid")).toByteArray();

    QString state;
    if (!available)
        state = QString();
    else if (blocked)
        state = QStringLiteral("blocked");
    else if (!enabled)
        state = QStringLiteral("off");
    else if (m_attempting || (deviceState >= kStatePrepare && deviceState < kStateActivated))
        state = QStringLiteral("connecting");
    else if (deviceState <= kStateUnavailable)
        state = QStringLiteral("starting");
    else if (deviceState == kStateActivated)
        state = QStringLiteral("connected");
    else if (deviceState == kStateDeactivating)
        state = QStringLiteral("disconnecting");
    else
        state = QStringLiteral("disconnected");

    QVariantMap current;
    const QByteArray currentSsid = m_attempting ? m_attempt.ssid : activeSsid;
    if ((state == QLatin1String("connected") || state == QLatin1String("connecting")
         || state == QLatin1String("disconnecting")) && !currentSsid.isEmpty()) {
        current.insert(QStringLiteral("ssid"), QString::fromUtf8(currentSsid));
        const int strength = currentSsid == activeSsid && !activeAp.isEmpty()
                                 ? activeAp.value(QStringLiteral("Strength")).toInt()
                                 : heard.value(currentSsid).strength;
        current.insert(QStringLiteral("bars"), barsOf(strength));
        if (state == QLatin1String("connected")) {
            const QVariantMap ip4 = interfaceOf(pathOf(deviceProperties.value(QStringLiteral("Ip4Config"))), kIp4Config);
            current.insert(QStringLiteral("address"), ip4.value(QStringLiteral("AddressData")).toList().value(0)
                                                          .toMap().value(QStringLiteral("address")).toString());
        }
    }

    // A line a name. The one joined is listed even when it isn't heard by
    // name (a hidden one).
    QSet<QByteArray> known;
    m_ssids.clear();
    for (auto it = m_saved.cbegin(); it != m_saved.cend(); ++it) {
        const QByteArray ssid = it.value().value(QStringLiteral("ssid")).toByteArray();
        known.insert(ssid);
        m_ssids.insert(QString::fromUtf8(ssid), ssid);
    }
    const bool joined = state == QLatin1String("connected") || state == QLatin1String("disconnecting");
    if (joined && !activeSsid.isEmpty() && !heard.contains(activeSsid))
        heard.insert(activeSsid, { QString(), activeAp.value(QStringLiteral("Strength")).toInt(),
                                   activeAp.isEmpty() ? QStringLiteral("wpa") : securityOf(activeAp) });
    struct Line {
        QVariantMap entry;
        int rank;
        int bars;
        QString key;
    };
    QList<Line> lines;
    for (auto it = heard.cbegin(); it != heard.cend(); ++it) {
        const QByteArray &ssid = it.key();
        const QString name = QString::fromUtf8(ssid);
        m_ssids.insert(name, ssid);
        const bool isActive = joined && ssid == activeSsid;
        QString busy;
        if (m_attempting && ssid == m_attempt.ssid)
            busy = QStringLiteral("connecting");
        else if (isActive && state == QLatin1String("disconnecting"))
            busy = QStringLiteral("disconnecting");
        const int bars = barsOf(it->strength);
        QVariantMap entry{
            { QStringLiteral("ssid"), name },
            { QStringLiteral("bars"), bars },
            { QStringLiteral("security"), it->security },
            { QStringLiteral("secured"), needsPassword(it->security) },
            { QStringLiteral("known"), known.contains(ssid) },
            { QStringLiteral("heard"), true },
            { QStringLiteral("active"), isActive },
            { QStringLiteral("busy"), busy },
        };
        lines.append({ entry, isActive ? 0 : known.contains(ssid) ? 1 : 2, bars, sortKey(name) });
    }
    // The saved ones out of reach, after those heard: to be forgotten, or
    // tried all the same (a network that doesn't say its name).
    for (const QByteArray &ssid : std::as_const(known)) {
        if (heard.contains(ssid))
            continue;
        const QString name = QString::fromUtf8(ssid);
        QVariantMap entry{
            { QStringLiteral("ssid"), name },
            { QStringLiteral("bars"), 0 },
            { QStringLiteral("security"), QString() },
            { QStringLiteral("secured"), false },
            { QStringLiteral("known"), true },
            { QStringLiteral("heard"), false },
            { QStringLiteral("active"), false },
            { QStringLiteral("busy"), m_attempting && ssid == m_attempt.ssid ? QStringLiteral("connecting") : QString() },
        };
        lines.append({ entry, 1, -1, sortKey(name) });
    }
    // By bars rather than the signal itself, which moves at every scan: the
    // lines stay where they are.
    std::sort(lines.begin(), lines.end(), [](const Line &a, const Line &b) {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        if (a.bars != b.bars)
            return a.bars > b.bars;
        return a.key < b.key;
    });
    QVariantList networks;
    for (const Line &line : lines)
        networks.append(line.entry);

    const bool stateDiffers = available != m_available || enabled != m_enabled || state != m_state
                              || current != m_current;
    m_available = available;
    m_enabled = enabled;
    m_state = state;
    m_current = current;
    if (!enabled)
        stopSearch();
    if (stateDiffers)
        emit stateChanged();
    if (networks != m_networks) {
        m_networks = networks;
        emit networksChanged();
    }
}

void WifiManager::call(const QString &path, const QString &interface, const QString &method,
                       const QVariantList &args, int timeoutMs,
                       std::function<void(const QDBusMessage &reply)> done) {
    dbuscall::call(m_bus, kService, path, interface, method, args, timeoutMs, this, std::move(done));
}

#endif
