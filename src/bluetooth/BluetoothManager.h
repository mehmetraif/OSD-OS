#pragma once

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#ifdef OSDOS_BLUETOOTH
#include <QDBusMessage>
class QDBusServiceWatcher;
#endif

// Settings → Bluetooth: finds, pairs and connects Bluetooth devices (a
// keyboard, a gamepad, a remote) through BlueZ, the Linux Bluetooth daemon, on
// the system bus (org.bluez). Exposed to QML as the context property
// "bluetoothManager" (views/Bluetooth.qml).
//
// - The adapter is the first BlueZ has (hci0 on a Pi). powered turns it on and
//   off.
// - startSearch() is the search mode: discovery for a minute, or until
//   stopSearch(). What answers with a name is listed in devices as it is found,
//   after the paired ones.
// - pair() pairs a found device, then trusts it, so it comes back by itself
//   after a restart, and connects it. Pairing asks this app's agent
//   (BluetoothAgent, org.bluez.Agent1) for what the device needs: a keyboard's
//   is a code to type on it, which prompt holds while it is wanted.
//
// BlueZ is only called once it is on the bus. On the OSD/OS image bluetooth.service
// starts after the app is on screen, and a call would start it early (D-Bus
// activation). Built without Qt D-Bus (macOS), supported is false and nothing
// here does anything.
class BluetoothManager : public QObject {
    Q_OBJECT
    // Built with BlueZ support: Settings offers the row.
    Q_PROPERTY(bool supported READ supported CONSTANT)
    // BlueZ runs and has an adapter.
    Q_PROPERTY(bool available READ available NOTIFY adapterChanged)
    Q_PROPERTY(QString adapterName READ adapterName NOTIFY adapterChanged)
    Q_PROPERTY(bool powered READ powered NOTIFY adapterChanged)
    // Discovery is on (searching for devices).
    Q_PROPERTY(bool searching READ searching NOTIFY adapterChanged)
    // [{ path, name, address, kind, paired, connected, busy }]: the paired
    // devices by name, then those found, in the order they were. kind is what
    // the device says it is ("Keyboard", "Keyboard+Mouse", "Gamepad"…, or "");
    // busy is "pairing", "connecting", "disconnecting" or "".
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    // What pairing needs shown, while it does: { kind, name, code, entered }.
    // kind "passkey": type code on the device, then its Enter (entered digits
    // so far); "pin": the same, for an older device; "confirm": does the
    // device show code? (answerPrompt). Empty otherwise.
    Q_PROPERTY(QVariantMap prompt READ prompt NOTIFY promptChanged)
    // How the last pairing or connection went, in a line, for the screen.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    // Turning the adapter on failed, twice (once when rfkill blocks it): the
    // page offers its details.
    Q_PROPERTY(bool powerFailed READ powerFailed NOTIFY adapterChanged)
    // What the system says about Bluetooth, for when it won't turn on
    // (collectDetails()): the adapter as BlueZ has it, rfkill, and the
    // system log's last Bluetooth lines. A line each.
    Q_PROPERTY(QString details READ details NOTIFY detailsChanged)
public:
    explicit BluetoothManager(QObject *parent = nullptr);
    ~BluetoothManager() override;

    bool supported() const;
    bool available() const { return !m_adapterPath.isEmpty(); }
    QString adapterName() const;
    bool powered() const;
    bool searching() const;
    QVariantList devices() const;
    QVariantMap prompt() const { return m_prompt; }
    QString message() const { return m_message; }
    bool powerFailed() const { return m_powerFailed; }
    QString details() const { return m_details; }

    Q_INVOKABLE void setPowered(bool on);
    Q_INVOKABLE void startSearch();
    Q_INVOKABLE void stopSearch();
    Q_INVOKABLE void pair(const QString &path);
    Q_INVOKABLE void connectDevice(const QString &path);
    Q_INVOKABLE void disconnectDevice(const QString &path);
    // Unpairs it: BlueZ forgets it.
    Q_INVOKABLE void forget(const QString &path);
    // The answer to a "confirm" prompt.
    Q_INVOKABLE void answerPrompt(bool accept);
    // Gives up the pairing under way.
    Q_INVOKABLE void cancelPairing();
    Q_INVOKABLE void clearMessage();
    Q_INVOKABLE void collectDetails();

#ifdef OSDOS_BLUETOOTH
    // BluetoothAgent's calls: what BlueZ asks of the agent.
    QString agentRequestPinCode(const QString &device);
    void agentDisplayPinCode(const QString &device, const QString &pinCode);
    void agentDisplayPasskey(const QString &device, uint passkey, int entered);
    void agentRequestConfirmation(const QString &device, uint passkey, const QDBusMessage &call);
    void agentRequestAuthorization(const QString &device, const QDBusMessage &call);
    void agentAuthorizeService(const QString &device, const QDBusMessage &call);
    void agentCancel();
#endif

signals:
    void adapterChanged();
    void devicesChanged();
    void promptChanged();
    void messageChanged();
    void detailsChanged();

#ifdef OSDOS_BLUETOOTH
private slots:
    void onServiceUnregistered();
    void onInterfacesAdded(const QDBusMessage &message);
    void onInterfacesRemoved(const QDBusMessage &message);
    void onPropertiesChanged(const QDBusMessage &message);

private:
    void load();
    void registerAgent();
    void addObject(const QString &path, const QMap<QString, QVariantMap> &interfaces);
    void chooseAdapter();
    void setBusy(const QString &path, const QString &state);
    void setMessage(const QString &text);
    void setPrompt(const QVariantMap &prompt);
    void replyToPending(bool accept);
    void setDeviceProperty(const QString &path, const QString &name, const QVariant &value);
    void powerOn(std::function<void()> then, bool retry = true);
    QString nameOf(const QString &path) const;
    void call(const QString &path, const QString &interface, const QString &method,
              const QVariantList &args, int timeoutMs,
              std::function<void(const QDBusMessage &reply)> done = {});

    QDBusServiceWatcher *m_watcher = nullptr;
    QHash<QString, QVariantMap> m_adapters;   // path -> Adapter1 properties
    QHash<QString, QVariantMap> m_devices;    // path -> Device1 properties
    QStringList m_seen;                       // device paths, in the order found
    QHash<QString, QString> m_busy;           // path -> what is under way
    QString m_pairingPath;
    // A RequestConfirmation waiting on answerPrompt().
    QDBusMessage m_pendingCall;
    bool m_agentRegistered = false;
#endif

private:
    QString m_adapterPath;
    QVariantMap m_prompt;
    QString m_message;
    bool m_powerFailed = false;
    QString m_details;
    QTimer m_searchTimer;
    // A search asked for and not stopped since: what a discovery that starts
    // late (the adapter was off, BlueZ was slow to confirm) is checked against.
    bool m_searchWanted = false;
};
