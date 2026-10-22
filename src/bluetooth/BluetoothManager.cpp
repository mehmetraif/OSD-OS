#include "BluetoothManager.h"

#ifdef OSDOS_BLUETOOTH
#include "BluetoothAgent.h"
#include "../util/DBusCall.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QRandomGenerator>
#include <algorithm>
#endif

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>

namespace {
// How long the search mode looks for devices before it stops by itself.
constexpr int kSearchMs = 60 * 1000;

// An rfkill switch for Bluetooth, which keeps the radio off below BlueZ: one
// per adapter, named after it ("hci0").
struct RfkillSwitch {
    QString name;
    bool soft = false;   // blocked by software (rfkill block, the boot default)
    bool hard = false;   // blocked by a physical switch
};

// Bluetooth's rfkill switches, from sysfs, which all can read.
QList<RfkillSwitch> bluetoothSwitches() {
    QList<RfkillSwitch> switches;
    const QDir rfkill(QStringLiteral("/sys/class/rfkill"));
    for (const QString &entry : rfkill.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        auto read = [&](const char *name) {
            QFile file(rfkill.filePath(entry + QLatin1Char('/') + QLatin1String(name)));
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
        };
        if (read("type") == QLatin1String("bluetooth"))
            switches.append({ read("name"), read("soft") == QLatin1String("1"), read("hard") == QLatin1String("1") });
    }
    return switches;
}

#ifdef OSDOS_BLUETOOTH
const QString kBluez         = QStringLiteral("org.bluez");
const QString kAdapter       = QStringLiteral("org.bluez.Adapter1");
const QString kDevice        = QStringLiteral("org.bluez.Device1");
const QString kAgentManager  = QStringLiteral("org.bluez.AgentManager1");
const QString kProperties    = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kObjectManager = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString kAgentPath     = QStringLiteral("/com/osdos/BluetoothAgent");
const QString kRejected      = QStringLiteral("org.bluez.Error.Rejected");

// Long enough to type a code on a keyboard.
constexpr int kPairTimeoutMs = 120 * 1000;
constexpr int kConnectTimeoutMs = 30 * 1000;
constexpr int kCallTimeoutMs = 10 * 1000;

// The device properties the list shows: a change to any other (RSSI comes
// with every answer while searching) leaves it as it is.
const QStringList kShownProperties = {
    QStringLiteral("Name"), QStringLiteral("Alias"), QStringLiteral("Paired"),
    QStringLiteral("Connected"), QStringLiteral("Icon"), QStringLiteral("Class"),
};

QDBusConnection bus() { return QDBusConnection::systemBus(); }

// A D-Bus value as T, demarshalled already or still a QDBusArgument (what a
// container inside a signal's arguments stays).
template <typename T>
T unwrap(const QVariant &value) {
    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        T out;
        value.value<QDBusArgument>() >> out;
        return out;
    }
    return value.value<T>();
}

// What a device says it is: its Class of Device for the classic ones (a
// keyboard with a touchpad is a "combo", for which BlueZ has no icon), else
// the icon BlueZ gives it.
QString kindOf(const QVariantMap &device) {
    const uint cls = device.value(QStringLiteral("Class")).toUInt();
    if (((cls >> 8) & 0x1f) == 0x05) {   // peripheral
        switch ((cls & 0xc0) >> 6) {
        case 1: return QStringLiteral("Keyboard");
        case 2: return QStringLiteral("Mouse");
        case 3: return QStringLiteral("Keyboard+Mouse");
        default:
            switch ((cls & 0x1e) >> 2) {
            case 1: case 2: return QStringLiteral("Gamepad");
            case 3: return QStringLiteral("Remote");
            default: break;
            }
        }
    }
    const QString icon = device.value(QStringLiteral("Icon")).toString();
    if (icon == QLatin1String("input-keyboard")) return QStringLiteral("Keyboard");
    if (icon == QLatin1String("input-mouse"))    return QStringLiteral("Mouse");
    if (icon == QLatin1String("input-gaming"))   return QStringLiteral("Gamepad");
    if (icon == QLatin1String("input-tablet"))   return QStringLiteral("Tablet");
    if (icon.startsWith(QLatin1String("audio-"))) return QStringLiteral("Audio");
    if (icon == QLatin1String("phone"))          return QStringLiteral("Phone");
    if (icon == QLatin1String("computer"))       return QStringLiteral("Computer");
    return QString();
}

// A failed call, in a few words for the screen.
QString describe(const QDBusMessage &reply) {
    const QString name = reply.errorName();
    const QString text = reply.errorMessage();
    if (name.endsWith(QLatin1String(".AuthenticationCanceled")))
        return QStringLiteral("pairing canceled");
    if (name.endsWith(QLatin1String(".AuthenticationRejected")))
        return QStringLiteral("pairing declined");
    if (name.endsWith(QLatin1String(".AuthenticationTimeout"))
        || name == QLatin1String("org.freedesktop.DBus.Error.NoReply"))
        return QStringLiteral("pairing took too long");
    if (name.endsWith(QLatin1String(".AuthenticationFailed")))
        return QStringLiteral("pairing failed, was the code typed right?");
    if (name.endsWith(QLatin1String(".ConnectionAttemptFailed"))
        || text.contains(QLatin1String("Page Timeout"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("Host is down"), Qt::CaseInsensitive))
        return QStringLiteral("no answer, is it on, close by and in pairing mode?");
    if (text.contains(QLatin1String("rfkill"), Qt::CaseInsensitive))
        return QStringLiteral("Bluetooth is blocked (rfkill)");
    if (name.endsWith(QLatin1String(".NotReady")))
        return QStringLiteral("Bluetooth is off");
    if (name.endsWith(QLatin1String(".DoesNotExist")))
        return QStringLiteral("it has gone out of reach");
    return text.isEmpty() ? name : text;
}

// What of rfkill's keeps the adapter at path off, in words; empty if nothing.
QString rfkillBlock(const QString &adapterPath) {
    const QString name = adapterPath.section(QLatin1Char('/'), -1);
    for (const RfkillSwitch &rfkillSwitch : bluetoothSwitches()) {
        if (rfkillSwitch.name != name)
            continue;
        if (rfkillSwitch.hard)
            return QStringLiteral("a switch blocks it (rfkill)");
        if (rfkillSwitch.soft)
            return QStringLiteral("rfkill blocks it");
    }
    return QString();
}

// Errors that mean the thing asked for is so already, or about to be.
bool alreadySo(const QDBusMessage &reply) {
    const QString name = reply.errorName();
    return name.endsWith(QLatin1String(".AlreadyConnected"))
        || name.endsWith(QLatin1String(".AlreadyExists"))
        || name.endsWith(QLatin1String(".InProgress"));
}
#endif
}

BluetoothManager::BluetoothManager(QObject *parent) : QObject(parent) {
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(kSearchMs);
    connect(&m_searchTimer, &QTimer::timeout, this, &BluetoothManager::stopSearch);

#ifdef OSDOS_BLUETOOTH
    if (!bus().isConnected()) {
        qWarning("[Bluetooth] No system bus: %s", qPrintable(bus().lastError().message()));
        return;
    }
    // The agent BlueZ asks while pairing.
    auto *agentObject = new QObject(this);
    new BluetoothAgent(agentObject, this);
    if (!bus().registerObject(kAgentPath, agentObject, QDBusConnection::ExportAdaptors))
        qWarning("[Bluetooth] Could not export the pairing agent");

    m_watcher = new QDBusServiceWatcher(kBluez, bus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &BluetoothManager::load);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &BluetoothManager::onServiceUnregistered);

    bus().connect(kBluez, QString(), kObjectManager, QStringLiteral("InterfacesAdded"),
                  this, SLOT(onInterfacesAdded(QDBusMessage)));
    bus().connect(kBluez, QString(), kObjectManager, QStringLiteral("InterfacesRemoved"),
                  this, SLOT(onInterfacesRemoved(QDBusMessage)));
    bus().connect(kBluez, QString(), kProperties, QStringLiteral("PropertiesChanged"),
                  this, SLOT(onPropertiesChanged(QDBusMessage)));

    // Running already (a desktop, or the app started again): take it now.
    // Otherwise the watcher says when it comes; asking for it before would
    // start it (D-Bus activation).
    if (bus().interface() && bus().interface()->isServiceRegistered(kBluez))
        load();
#endif
}

BluetoothManager::~BluetoothManager() {
#ifdef OSDOS_BLUETOOTH
    // A search left running would go on after the app.
    if (searching()) {
        QDBusMessage stop = QDBusMessage::createMethodCall(kBluez, m_adapterPath, kAdapter,
                                                           QStringLiteral("StopDiscovery"));
        bus().call(stop, QDBus::NoBlock);
    }
#endif
}

bool BluetoothManager::supported() const {
#ifdef OSDOS_BLUETOOTH
    return true;
#else
    return false;
#endif
}

QString BluetoothManager::adapterName() const {
#ifdef OSDOS_BLUETOOTH
    return m_adapters.value(m_adapterPath).value(QStringLiteral("Alias")).toString();
#else
    return QString();
#endif
}

bool BluetoothManager::powered() const {
#ifdef OSDOS_BLUETOOTH
    return m_adapters.value(m_adapterPath).value(QStringLiteral("Powered")).toBool();
#else
    return false;
#endif
}

bool BluetoothManager::searching() const {
#ifdef OSDOS_BLUETOOTH
    return m_adapters.value(m_adapterPath).value(QStringLiteral("Discovering")).toBool();
#else
    return false;
#endif
}

QVariantList BluetoothManager::devices() const {
    QVariantList list;
#ifdef OSDOS_BLUETOOTH
    if (m_adapterPath.isEmpty())
        return list;
    QVariantList found;
    for (const QString &path : m_seen) {
        if (!path.startsWith(m_adapterPath + QLatin1Char('/')))
            continue;
        const QVariantMap &device = m_devices.value(path);
        const bool paired = device.value(QStringLiteral("Paired")).toBool();
        // Found ones only once they have told their name: a keyboard in
        // pairing mode does, the anonymous beacons around don't.
        if (!paired && !device.contains(QStringLiteral("Name")))
            continue;
        QVariantMap entry;
        entry[QStringLiteral("path")] = path;
        entry[QStringLiteral("name")] = device.value(QStringLiteral("Alias"),
                                                     device.value(QStringLiteral("Name"))).toString();
        entry[QStringLiteral("address")] = device.value(QStringLiteral("Address")).toString();
        entry[QStringLiteral("kind")] = kindOf(device);
        entry[QStringLiteral("paired")] = paired;
        entry[QStringLiteral("connected")] = device.value(QStringLiteral("Connected")).toBool();
        entry[QStringLiteral("busy")] = m_busy.value(path);
        (paired ? list : found).append(entry);
    }
    std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString()
                   .compare(b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    list += found;
#endif
    return list;
}

void BluetoothManager::setPowered(bool on) {
#ifdef OSDOS_BLUETOOTH
    if (m_adapterPath.isEmpty())
        return;
    clearMessage();
    if (on) {
        powerOn({});
        return;
    }
    stopSearch();
    call(m_adapterPath, kProperties, QStringLiteral("Set"),
         { kAdapter, QStringLiteral("Powered"), QVariant::fromValue(QDBusVariant(false)) }, kCallTimeoutMs,
         [this](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage)
                 setMessage(QStringLiteral("Couldn't turn Bluetooth off: ") + describe(reply));
         });
#else
    Q_UNUSED(on)
#endif
}

void BluetoothManager::startSearch() {
#ifdef OSDOS_BLUETOOTH
    if (m_adapterPath.isEmpty())
        return;
    clearMessage();
    m_searchWanted = true;
    const QString adapter = m_adapterPath;
    auto discover = [this, adapter]() {
        // Stopped while the adapter was being turned on: not started after all.
        if (!m_searchWanted)
            return;
        call(adapter, kAdapter, QStringLiteral("StartDiscovery"), {}, kCallTimeoutMs,
             [this](const QDBusMessage &reply) {
                 if (reply.type() == QDBusMessage::ErrorMessage && !alreadySo(reply)) {
                     m_searchTimer.stop();
                     setMessage(QStringLiteral("Couldn't search: ") + describe(reply));
                 }
             });
        m_searchTimer.start();
    };
    if (powered()) {
        discover();
        return;
    }
    // Off: on first, then the search.
    powerOn(discover);
#endif
}

void BluetoothManager::stopSearch() {
    m_searchTimer.stop();
    m_searchWanted = false;
#ifdef OSDOS_BLUETOOTH
    // A search BlueZ hasn't confirmed yet is stopped as it is (onPropertiesChanged).
    if (!m_adapterPath.isEmpty() && searching())
        call(m_adapterPath, kAdapter, QStringLiteral("StopDiscovery"), {}, kCallTimeoutMs);
#endif
}

void BluetoothManager::pair(const QString &path) {
#ifdef OSDOS_BLUETOOTH
    if (!m_devices.contains(path) || !m_pairingPath.isEmpty())
        return;
    // A search under way gets in the way of pairing.
    stopSearch();
    clearMessage();
    m_pairingPath = path;
    setBusy(path, QStringLiteral("pairing"));
    call(path, kDevice, QStringLiteral("Pair"), {}, kPairTimeoutMs, [this, path](const QDBusMessage &reply) {
        m_pairingPath.clear();
        replyToPending(false);
        setPrompt({});
        if (reply.type() == QDBusMessage::ErrorMessage && !alreadySo(reply)) {
            setBusy(path, QString());
            setMessage(nameOf(path) + QStringLiteral(": ") + describe(reply));
            return;
        }
        // Trusted, it comes back by itself after a restart (a keyboard at
        // its first key press) without asking.
        setDeviceProperty(path, QStringLiteral("Trusted"), true);
        connectDevice(path);
    });
#else
    Q_UNUSED(path)
#endif
}

void BluetoothManager::connectDevice(const QString &path) {
#ifdef OSDOS_BLUETOOTH
    if (!m_devices.contains(path))
        return;
    setBusy(path, QStringLiteral("connecting"));
    call(path, kDevice, QStringLiteral("Connect"), {}, kConnectTimeoutMs, [this, path](const QDBusMessage &reply) {
        setBusy(path, QString());
        if (reply.type() == QDBusMessage::ErrorMessage && !alreadySo(reply))
            setMessage(nameOf(path) + QStringLiteral(": ") + describe(reply));
        else
            setMessage(nameOf(path) + QStringLiteral(" is connected"));
    });
#else
    Q_UNUSED(path)
#endif
}

void BluetoothManager::disconnectDevice(const QString &path) {
#ifdef OSDOS_BLUETOOTH
    if (!m_devices.contains(path))
        return;
    clearMessage();
    setBusy(path, QStringLiteral("disconnecting"));
    call(path, kDevice, QStringLiteral("Disconnect"), {}, kCallTimeoutMs, [this, path](const QDBusMessage &reply) {
        setBusy(path, QString());
        if (reply.type() == QDBusMessage::ErrorMessage)
            setMessage(nameOf(path) + QStringLiteral(": ") + describe(reply));
    });
#else
    Q_UNUSED(path)
#endif
}

void BluetoothManager::forget(const QString &path) {
#ifdef OSDOS_BLUETOOTH
    if (!m_devices.contains(path) || m_adapterPath.isEmpty())
        return;
    clearMessage();
    const QString name = nameOf(path);
    call(m_adapterPath, kAdapter, QStringLiteral("RemoveDevice"), { QVariant::fromValue(QDBusObjectPath(path)) },
         kCallTimeoutMs, [this, name](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage)
                 setMessage(name + QStringLiteral(": ") + describe(reply));
             else
                 setMessage(name + QStringLiteral(" is forgotten"));
         });
#else
    Q_UNUSED(path)
#endif
}

void BluetoothManager::answerPrompt(bool accept) {
#ifdef OSDOS_BLUETOOTH
    replyToPending(accept);
    setPrompt({});
#else
    Q_UNUSED(accept)
#endif
}

void BluetoothManager::cancelPairing() {
#ifdef OSDOS_BLUETOOTH
    replyToPending(false);
    setPrompt({});
    if (!m_pairingPath.isEmpty())
        call(m_pairingPath, kDevice, QStringLiteral("CancelPairing"), {}, kCallTimeoutMs);
#endif
}

void BluetoothManager::clearMessage() {
    if (m_message.isEmpty())
        return;
    m_message.clear();
    emit messageChanged();
}

void BluetoothManager::collectDetails() {
    QStringList lines;
#ifdef OSDOS_BLUETOOTH
    if (m_adapterPath.isEmpty()) {
        lines << QStringLiteral("BlueZ: no adapter");
    } else {
        const QVariantMap adapter = m_adapters.value(m_adapterPath);
        lines << QStringLiteral("%1 %2: powered %3, %4")
                     .arg(m_adapterPath.section(QLatin1Char('/'), -1),
                          adapter.value(QStringLiteral("Address")).toString(),
                          adapter.value(QStringLiteral("Powered")).toBool() ? QStringLiteral("yes")
                                                                             : QStringLiteral("no"),
                          adapter.value(QStringLiteral("PowerState"), QStringLiteral("?")).toString());
    }
#endif
    // A switch that keeps the radio off: rfkill's.
    for (const RfkillSwitch &rfkillSwitch : bluetoothSwitches())
        lines << QStringLiteral("rfkill %1: soft %2, hard %3")
                     .arg(rfkillSwitch.name,
                          rfkillSwitch.soft ? QStringLiteral("blocked") : QStringLiteral("no"),
                          rfkillSwitch.hard ? QStringLiteral("blocked") : QStringLiteral("no"));
    m_details = lines.join(QLatin1Char('\n'));
    emit detailsChanged();

    // The system log's last Bluetooth lines: the kernel's, bluetoothd's,
    // rfkill's and this app's. The app's user reads it as a member of adm.
    auto *journal = new QProcess(this);
    auto finish = [this, journal, lines](bool ran) {
        journal->deleteLater();
        static const QRegularExpression relevant(
            QStringLiteral("bluetooth|hci\\d|rfkill|bthelper|bcm|brcm"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression stamp(QStringLiteral("^\\[[^\\]]*\\]\\s*"));
        // Each line once, where it came last, with how many times it came:
        // a refusal at every try would otherwise push out what led to it.
        QStringList order;               // the lines, timestamp aside
        QHash<QString, QString> last;    // line -> its last time, in full
        QHash<QString, int> times;
        if (ran) {
            for (const QString &line : QString::fromUtf8(journal->readAllStandardOutput()).split(QLatin1Char('\n'))) {
                if (!relevant.match(line).hasMatch())
                    continue;
                const QString text = line.trimmed().remove(stamp);
                order.removeOne(text);
                order.append(text);
                last.insert(text, line.trimmed());
                ++times[text];
            }
        }
        QStringList out = lines;
        if (order.isEmpty())
            out << QStringLiteral("(no Bluetooth lines in the system log)");
        for (const QString &text : order.mid(qMax(0, int(order.size()) - 20)))
            out << last.value(text) + (times.value(text) > 1 ? QStringLiteral(" (x%1)").arg(times.value(text))
                                                             : QString());
        m_details = out.join(QLatin1Char('\n'));
        emit detailsChanged();
    };
    connect(journal, &QProcess::finished, this, [finish]() { finish(true); });
    connect(journal, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(false);
    });
    journal->start(QStringLiteral("journalctl"),
                   { QStringLiteral("-b"), QStringLiteral("--no-pager"), QStringLiteral("--no-hostname"),
                     QStringLiteral("-o"), QStringLiteral("short-monotonic"), QStringLiteral("-n"),
                     QStringLiteral("600") });
}

#ifdef OSDOS_BLUETOOTH

// --- What BlueZ asks of the agent ---

// What the pairing screen shows: a code, for a device, to type (pin), to
// watch being typed (passkey, with how many digits are in) or to compare
// (confirm).
static QVariantMap pairingPrompt(const QString &kind, const QString &name, const QString &code,
                                 int entered = 0) {
    return { { QStringLiteral("kind"), kind }, { QStringLiteral("name"), name },
             { QStringLiteral("code"), code }, { QStringLiteral("entered"), entered } };
}

// A passkey as the other device shows it: six digits.
static QString passkeyText(uint passkey) {
    return QStringLiteral("%1").arg(passkey, 6, 10, QLatin1Char('0'));
}

QString BluetoothManager::agentRequestPinCode(const QString &device) {
    // An older device that takes any code typed on it, then its Enter: six
    // digits, shown to be typed.
    const QString pin = passkeyText(QRandomGenerator::global()->bounded(1000000));
    setPrompt(pairingPrompt(QStringLiteral("pin"), nameOf(device), pin));
    return pin;
}

void BluetoothManager::agentDisplayPinCode(const QString &device, const QString &pinCode) {
    setPrompt(pairingPrompt(QStringLiteral("pin"), nameOf(device), pinCode));
}

void BluetoothManager::agentDisplayPasskey(const QString &device, uint passkey, int entered) {
    setPrompt(pairingPrompt(QStringLiteral("passkey"), nameOf(device), passkeyText(passkey), entered));
}

void BluetoothManager::agentRequestConfirmation(const QString &device, uint passkey, const QDBusMessage &call) {
    replyToPending(false);
    m_pendingCall = call;
    setPrompt(pairingPrompt(QStringLiteral("confirm"), nameOf(device), passkeyText(passkey)));
}

// Pairing with nothing to show or type, which BlueZ checks with the agent:
// yes for the device being paired here, no for anything else that asks.
void BluetoothManager::agentRequestAuthorization(const QString &device, const QDBusMessage &call) {
    bus().send(device == m_pairingPath
               ? call.createReply()
               : call.createErrorReply(kRejected, QStringLiteral("Pair it from Settings → Bluetooth")));
}

// A device not trusted yet that wants a service: yes if it is paired.
void BluetoothManager::agentAuthorizeService(const QString &device, const QDBusMessage &call) {
    const bool ours = device == m_pairingPath
                      || m_devices.value(device).value(QStringLiteral("Paired")).toBool();
    bus().send(ours ? call.createReply()
                    : call.createErrorReply(kRejected, QStringLiteral("Not paired")));
}

void BluetoothManager::agentCancel() {
    m_pendingCall = QDBusMessage();
    setPrompt({});
}

// --- BlueZ's objects ---

void BluetoothManager::onServiceUnregistered() {
    const bool had = !m_adapterPath.isEmpty() || !m_devices.isEmpty();
    m_searchTimer.stop();
    m_adapters.clear();
    m_devices.clear();
    m_seen.clear();
    m_busy.clear();
    m_pairingPath.clear();
    m_pendingCall = QDBusMessage();
    m_agentRegistered = false;
    m_adapterPath.clear();
    m_powerFailed = false;
    setPrompt({});
    if (had) {
        emit adapterChanged();
        emit devicesChanged();
    }
}

void BluetoothManager::load() {
    call(QStringLiteral("/"), kObjectManager, QStringLiteral("GetManagedObjects"), {}, kCallTimeoutMs,
         [this](const QDBusMessage &reply) {
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qWarning("[Bluetooth] Couldn't list BlueZ's objects: %s", qPrintable(reply.errorMessage()));
            return;
        }
        const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
        arg.beginMap();
        while (!arg.atEnd()) {
            QDBusObjectPath path;
            QMap<QString, QVariantMap> interfaces;
            arg.beginMapEntry();
            arg >> path >> interfaces;
            arg.endMapEntry();
            addObject(path.path(), interfaces);
        }
        arg.endMap();
        chooseAdapter();
        emit adapterChanged();
        emit devicesChanged();
        registerAgent();
    });
}

void BluetoothManager::registerAgent() {
    if (m_agentRegistered)
        return;
    call(QStringLiteral("/org/bluez"), kAgentManager, QStringLiteral("RegisterAgent"),
         { QVariant::fromValue(QDBusObjectPath(kAgentPath)), QStringLiteral("DisplayYesNo") }, kCallTimeoutMs,
         [this](const QDBusMessage &reply) {
        if (reply.type() == QDBusMessage::ErrorMessage && !alreadySo(reply)) {
            qWarning("[Bluetooth] Couldn't register the pairing agent: %s", qPrintable(reply.errorMessage()));
            return;
        }
        m_agentRegistered = true;
        // The default one, so a device asking to pair or to connect asks it.
        call(QStringLiteral("/org/bluez"), kAgentManager, QStringLiteral("RequestDefaultAgent"),
             { QVariant::fromValue(QDBusObjectPath(kAgentPath)) }, kCallTimeoutMs);
    });
}

void BluetoothManager::addObject(const QString &path, const QMap<QString, QVariantMap> &interfaces) {
    if (interfaces.contains(kAdapter))
        m_adapters[path].insert(interfaces.value(kAdapter));
    if (interfaces.contains(kDevice)) {
        m_devices[path].insert(interfaces.value(kDevice));
        if (!m_seen.contains(path))
            m_seen.append(path);
    }
}

// The first adapter, hci0 on a Pi.
void BluetoothManager::chooseAdapter() {
    if (m_adapters.contains(m_adapterPath))
        return;
    QStringList paths = m_adapters.keys();
    std::sort(paths.begin(), paths.end());
    m_adapterPath = paths.value(0);
}

void BluetoothManager::onInterfacesAdded(const QDBusMessage &message) {
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2)
        return;
    const QString path = unwrap<QDBusObjectPath>(args.at(0)).path();
    const auto interfaces = unwrap<QMap<QString, QVariantMap>>(args.at(1));
    const QString before = m_adapterPath;
    addObject(path, interfaces);
    if (interfaces.contains(kAdapter)) {
        chooseAdapter();
        emit adapterChanged();
        if (before.isEmpty())
            registerAgent();
    }
    if (interfaces.contains(kDevice))
        emit devicesChanged();
}

void BluetoothManager::onInterfacesRemoved(const QDBusMessage &message) {
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2)
        return;
    const QString path = unwrap<QDBusObjectPath>(args.at(0)).path();
    const QStringList interfaces = unwrap<QStringList>(args.at(1));
    if (interfaces.contains(kDevice) && m_devices.remove(path)) {
        m_seen.removeAll(path);
        m_busy.remove(path);
        if (m_pairingPath == path)
            cancelPairing();
        emit devicesChanged();
    }
    if (interfaces.contains(kAdapter) && m_adapters.remove(path)) {
        if (m_adapterPath == path) {
            m_adapterPath.clear();
            chooseAdapter();
            emit devicesChanged();
        }
        emit adapterChanged();
    }
}

void BluetoothManager::onPropertiesChanged(const QDBusMessage &message) {
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2)
        return;
    const QString path = message.path();
    const QString interface = args.at(0).toString();
    const QVariantMap changed = unwrap<QVariantMap>(args.at(1));
    const QStringList invalidated = args.size() > 2 ? unwrap<QStringList>(args.at(2)) : QStringList();

    if (interface == kAdapter && m_adapters.contains(path)) {
        QVariantMap &adapter = m_adapters[path];
        adapter.insert(changed);
        for (const QString &key : invalidated)
            adapter.remove(key);
        if (path == m_adapterPath) {
            if (changed.contains(QStringLiteral("Discovering"))) {
                if (!changed.value(QStringLiteral("Discovering")).toBool()) {
                    // Discovery ended by itself, or by another client.
                    m_searchTimer.stop();
                } else if (!m_searchWanted) {
                    // Confirmed after the search was stopped (Back before
                    // BlueZ answered): ended now, or it would run on with
                    // nobody looking.
                    call(path, kAdapter, QStringLiteral("StopDiscovery"), {}, kCallTimeoutMs);
                }
            }
            emit adapterChanged();
        }
    } else if (interface == kDevice && m_devices.contains(path)) {
        QVariantMap &device = m_devices[path];
        bool shown = false;
        for (auto it = changed.cbegin(); it != changed.cend(); ++it) {
            device.insert(it.key(), it.value());
            shown = shown || kShownProperties.contains(it.key());
        }
        for (const QString &key : invalidated) {
            device.remove(key);
            shown = shown || kShownProperties.contains(key);
        }
        if (shown)
            emit devicesChanged();
    }
}

// --- Helpers ---

void BluetoothManager::setBusy(const QString &path, const QString &state) {
    if (m_busy.value(path) == state)
        return;
    if (state.isEmpty())
        m_busy.remove(path);
    else
        m_busy.insert(path, state);
    emit devicesChanged();
}

void BluetoothManager::setMessage(const QString &text) {
    if (m_message == text)
        return;
    m_message = text;
    emit messageChanged();
}

void BluetoothManager::setPrompt(const QVariantMap &prompt) {
    if (m_prompt == prompt)
        return;
    m_prompt = prompt;
    emit promptChanged();
}

void BluetoothManager::replyToPending(bool accept) {
    if (m_pendingCall.type() != QDBusMessage::MethodCallMessage)
        return;
    bus().send(accept ? m_pendingCall.createReply()
                      : m_pendingCall.createErrorReply(kRejected, QStringLiteral("Declined")));
    m_pendingCall = QDBusMessage();
}

// Turns the adapter on, then then(). A refusal is tried once more a moment
// later (bluetoothd may still be setting the adapter up); a second one is
// said, and the page then offers the details. rfkill's block, which BlueZ only
// calls "Failed", is said at once: trying again wouldn't lift it.
void BluetoothManager::powerOn(std::function<void()> then, bool retry) {
    const QString adapter = m_adapterPath;
    call(adapter, kProperties, QStringLiteral("Set"),
         { kAdapter, QStringLiteral("Powered"), QVariant::fromValue(QDBusVariant(true)) }, kCallTimeoutMs,
         [this, then, retry, adapter](const QDBusMessage &reply) {
             if (reply.type() != QDBusMessage::ErrorMessage) {
                 if (m_powerFailed) {
                     m_powerFailed = false;
                     emit adapterChanged();
                 }
                 if (then)
                     then();
                 return;
             }
             qWarning("[Bluetooth] Powering %s on failed: %s %s", qPrintable(adapter),
                      qPrintable(reply.errorName()), qPrintable(reply.errorMessage()));
             const QString blocked = rfkillBlock(adapter);
             if (blocked.isEmpty() && retry && adapter == m_adapterPath) {
                 QTimer::singleShot(2000, this, [this, then, adapter]() {
                     if (adapter == m_adapterPath)
                         powerOn(then, false);
                 });
                 return;
             }
             setMessage(QStringLiteral("Couldn't turn Bluetooth on: ")
                        + (blocked.isEmpty() ? describe(reply) : blocked));
             if (!m_powerFailed) {
                 m_powerFailed = true;
                 emit adapterChanged();
             }
         });
}

void BluetoothManager::setDeviceProperty(const QString &path, const QString &name, const QVariant &value) {
    call(path, kProperties, QStringLiteral("Set"), { kDevice, name, QVariant::fromValue(QDBusVariant(value)) },
         kCallTimeoutMs);
}

QString BluetoothManager::nameOf(const QString &path) const {
    const QVariantMap device = m_devices.value(path);
    const QString name = device.value(QStringLiteral("Alias"), device.value(QStringLiteral("Name"))).toString();
    return name.isEmpty() ? QStringLiteral("The device") : name;
}

void BluetoothManager::call(const QString &path, const QString &interface, const QString &method,
                            const QVariantList &args, int timeoutMs,
                            std::function<void(const QDBusMessage &reply)> done) {
    dbuscall::call(bus(), kBluez, path, interface, method, args, timeoutMs, this, std::move(done));
}

#endif
