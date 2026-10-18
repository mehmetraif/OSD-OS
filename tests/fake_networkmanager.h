#pragma once

#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMap>
#include <QPair>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

// A stand-in for NetworkManager on a D-Bus of the tests' own: wifi_test's
// private bus, or the one the fake_networkmanager program is started on for
// screenshots. It speaks what WifiManager (src/wifi) uses of NetworkManager's
// D-Bus API, with NetworkManager's own object paths, interfaces, property
// names, D-Bus types and state numbers:
//
// - its objects under /org/freedesktop, listed by the ObjectManager there
//   (GetManagedObjects, InterfacesAdded, InterfacesRemoved), each with
//   org.freedesktop.DBus.Properties (Get, GetAll, Set and PropertiesChanged);
// - the manager, with WirelessEnabled (which Set turns on and off) and
//   WirelessHardwareEnabled, ActivateConnection, AddAndActivateConnection and
//   DeactivateConnection;
// - the devices: wlan0, the Wi-Fi device, beside a loopback, an Ethernet and
//   a Wi-Fi P2P one; wlan0's access points and RequestScan, its StateChanged
//   signal, its active connection (and that one's StateChanged) and its
//   IP4Config;
// - the saved connections (Settings.Connection), with GetSettings, Delete and
//   the Updated signal.
//
// The scenario is set from C++: the access points, each with the password
// that joins it, the saved connections, Wi-Fi turned off or blocked by a
// switch, and calls or an activation that are to fail. Joining goes as it
// does with NetworkManager, a step at a time, with the signals NetworkManager
// sends: the device through its states to activated with an address, or to
// failed and back to disconnected, the active connection removed. Every call
// is written down, with its answer, for the test to check.
class FakeNetworkManager : public QDBusVirtualObject {
    Q_OBJECT
public:
    // How an access point is joined, as its flags say.
    enum class Security { Open, Owe, Wep, Wpa, Sae, Enterprise };
    // How an access point that doesn't say its name broadcasts it: as an
    // empty SSID, or as zero bytes of its name's length, as some routers do.
    enum class Hidden { No, Empty, Zeros };

    // A method called, its arguments and its answer made plain: an object
    // path a string, a map a QVariantMap, an array a QVariantList, bytes a
    // QByteArray. error is the D-Bus error's name, "" for an answer.
    struct Call {
        QString path;
        QString interface;
        QString method;
        QVariantList args;
        QVariantList reply;
        QString error;
    };

    explicit FakeNetworkManager(QObject *parent = nullptr);
    ~FakeNetworkManager() override;

    // On the bus: the objects under /org/freedesktop, then the name
    // org.freedesktop.NetworkManager, as NetworkManager starting. stop() gives
    // the name up, as NetworkManager stopping; start() again brings it back
    // with what it had.
    bool start(const QDBusConnection &bus);
    void stop();

    // --- The scenario ---

    // An access point named ssid, its signal strength 0-100, joined with
    // password (a WEP one's key). Returns its object path.
    QString addAccessPoint(const QString &ssid, int strength, Security security,
                           const QString &password = QString(), Hidden hidden = Hidden::No);
    void setStrength(const QString &accessPoint, int strength);
    void removeAccessPoint(const QString &accessPoint);
    // A connection saved for ssid, joined as the access point of that name
    // says (a WPA one without it, given a password; open without one).
    // Returns its object path. A second one for the same name is called
    // "<name> 1", as NetworkManager calls it.
    QString addSaved(const QString &ssid, const QString &password = QString(), bool hidden = false);
    // A connection saved with these settings, of any kind (an Ethernet one,
    // a hotspot), completed as NetworkManager completes a connection it
    // saves. Returns its object path.
    QString addConnection(const QMap<QString, QVariantMap> &settings);
    // A saved connection edited elsewhere (with nmcli, say): its network
    // renamed, and its Updated signal sent.
    void renameSaved(const QString &saved, const QString &ssid);
    // The device on the network saved at that path, as though it had joined
    // it before the test: the steps at once.
    void join(const QString &saved);
    // Wi-Fi turned on or off: NetworkManager's radio switch.
    void setWirelessEnabled(bool on);
    // A switch that keeps the radio off (rfkill's hard block).
    void setHardwareBlocked(bool blocked);
    // The next activation fails for these reasons (NMDeviceStateReason,
    // NMActiveConnectionStateReason), whatever the password: by default a
    // password NetworkManager can't join with.
    void failNextActivation(uint deviceReason = 7, uint activeReason = 9);
    // The next call of method answers with this D-Bus error.
    void failNextCall(const QString &method, const QString &errorName, const QString &errorText);
    // How long NetworkManager takes from one step of joining or leaving a
    // network to the next, and to turn the radio on (100 ms).
    void setDelay(int ms);
    // Held, the steps of joining and leaving wait for proceed(), each call
    // running those waiting: a test can look at a network on its way. A
    // network being joined waits twice: before the device's first steps,
    // and before the outcome.
    void setHeld(bool held);
    bool isWaiting() const { return !m_waiting.isEmpty(); }
    void proceed();
    // The IPv4 address the device gets (192.168.1.23).
    void setAddress(const QString &address);
    // A second Wi-Fi device, a USB dongle (wlan1): no access points, never
    // the one WifiManager uses. Returns its object path.
    QString addDongle();

    // --- What it has ---

    QString devicePath() const { return m_device; }
    uint deviceState() const { return m_deviceState; }
    bool wirelessEnabled() const { return m_wirelessEnabled; }
    QString address() const { return m_address; }
    // The active connection's object path, "" for none.
    QString activeConnection() const { return m_active; }
    QStringList savedConnections() const { return m_settings.keys(); }
    // A saved connection's settings with their secrets, made plain.
    QVariantMap settings(const QString &saved) const;
    QList<Call> calls() const { return m_calls; }
    QList<Call> calls(const QString &method) const;
    void clearCalls() { m_calls.clear(); }

    // QDBusVirtualObject: what is under /org/freedesktop.
    QString introspect(const QString &path) const override;
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;

signals:
    // A call answered.
    void called(const QString &method);

private:
    using Settings = QMap<QString, QVariantMap>;

    struct AccessPoint {
        QByteArray ssid;
        int strength = 0;
        Security security = Security::Open;
        QString password;
        Hidden hidden = Hidden::No;
        // A hidden one's name, learned as the device joined it.
        bool revealed = false;
        QString bssid;
        uint frequency = 2437;
    };

    QDBusMessage answer(const QDBusMessage &message);
    QDBusMessage properties(const QDBusMessage &message);
    QDBusMessage activateConnection(const QDBusMessage &message);
    QDBusMessage addAndActivateConnection(const QDBusMessage &message);
    QDBusMessage deactivateConnection(const QDBusMessage &message);
    QDBusMessage requestScan(const QDBusMessage &message);
    QDBusMessage getSettings(const QDBusMessage &message);
    QDBusMessage deleteSaved(const QDBusMessage &message);
    QString deviceProblem(const QString &device) const;

    void exportObject(const QString &path, const Settings &interfaces);
    void unexportObject(const QString &path);
    void change(const QString &path, const QString &interface, const QVariantMap &changed);
    void emitSignal(const QString &path, const QString &interface, const QString &name, const QVariantList &args);

    bool radioOn() const { return m_wirelessEnabled && !m_blocked; }
    void radioChanged();
    void showAccessPoint(const QString &path);
    void hideAccessPoint(const QString &path);
    void accessPointsChanged();
    QVariantMap accessPointProperties(const AccessPoint &ap) const;
    QString accessPointFor(const QString &saved, const QString &specific) const;

    QString save(const Settings &settings);
    void removeSaved(const QString &path);
    void savedChanged();
    QString uniqueId(const QString &name) const;

    QString startActivation(const QString &saved, const QString &specific);
    void prepare(const QString &active, const QString &specific);
    void conclude(const QString &active);
    void succeed(const QString &active);
    void fail(const QString &active, uint deviceReason, uint activeReason);
    void beginLeaving(uint deviceReason, uint activeReason);
    void endLeaving(uint deviceState, uint deviceReason, uint activeReason);
    void setDeviceState(uint state, uint reason, const QVariantMap &also = QVariantMap());
    void setActiveState(const QString &active, uint state, uint reason);
    void setManagerState(uint state);
    void soon(std::function<void()> step);
    void later(std::function<void()> step);

    QDBusConnection m_bus;
    bool m_started = false;
    // What is on the bus: path -> interface -> property -> value, each value
    // of the D-Bus type NetworkManager gives it.
    QMap<QString, Settings> m_objects;
    // The access points, heard or not (Wi-Fi off): path -> it, in the order
    // they were added.
    QHash<QString, AccessPoint> m_aps;
    QStringList m_apOrder;
    // The saved connections: path -> their settings, secrets too.
    QMap<QString, Settings> m_settings;
    QStringList m_devices;
    QString m_device;
    uint m_deviceState = 0;
    bool m_wirelessEnabled = true;
    bool m_blocked = false;
    // The active connection, the connection it is of, its state, the access
    // point joined and the IP4Config.
    QString m_active;
    QString m_activeSaved;
    uint m_activeState = 0;
    QString m_activeAp;
    QString m_ip4;
    QString m_address = QStringLiteral("192.168.1.23");
    int m_nextAccessPoint = 1;
    int m_nextActive = 1;
    int m_nextIp4 = 1;
    int m_nextSaved = 1;
    int m_nextDevice = 1;
    int m_delay = 100;
    bool m_held = false;
    QList<std::function<void()>> m_waiting;
    bool m_failActivation = false;
    uint m_failDeviceReason = 0;
    uint m_failActiveReason = 0;
    QHash<QString, QPair<QString, QString>> m_failCalls;
    QList<Call> m_calls;
    QElapsedTimer m_clock;
};
