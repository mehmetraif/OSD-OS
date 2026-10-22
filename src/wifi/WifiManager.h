#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#ifdef OSDOS_WIFI
#include <QDBusConnection>
#include <QDBusMessage>
class QDBusServiceWatcher;
#endif

// Settings → Wi-Fi: joins a Wi-Fi network through NetworkManager, which runs
// the Pi's network, on the system bus (org.freedesktop.NetworkManager). Exposed
// to QML as the context property "wifiManager" (views/Wifi.qml).
//
// - The device is NetworkManager's first Wi-Fi device (wlan0 on a Pi). enabled
//   turns Wi-Fi on and off (NetworkManager's radio switch).
// - startSearch() is the search mode: a scan, again every few seconds for a
//   minute, or until stopSearch(). What NetworkManager hears is in networks.
// - connectTo() joins a network: a saved one as it was saved, a new one with
//   the password typed for it. NetworkManager keeps a new one once it is
//   joined, and joins it by itself from then on, after a restart too; one
//   that couldn't be joined (a wrong password) is not kept, nor one given up
//   for another chosen while it was on its way. forget() deletes the saved
//   one.
// - The country: Raspberry Pi OS keeps Wi-Fi off until the country the Pi is
//   used in is set, for the radio to keep to that country's rules.
//   setCountry() sends it to osdos-wifi.socket, where osdos-stop
//   (scripts/install.sh) sets it as root (raspi-config's do_wifi_country).
//
// NetworkManager is only called once it is on the bus: on the OSD/OS image it
// starts after the app is on screen, and a call would start it early (D-Bus
// activation). The app's service has no login session, so NetworkManager lets
// it in through the image's polkit rule (50-osdos-networkmanager.rules).
// Built without Qt D-Bus (macOS), supported is false and nothing here does
// anything.
class WifiManager : public QObject {
    Q_OBJECT
    // Built with NetworkManager support and, run by the app's service, by a
    // launcher whose image lets it use NetworkManager (launcher API 7):
    // Settings offers the row.
    Q_PROPERTY(bool supported READ supported CONSTANT)
    // NetworkManager runs and has a Wi-Fi device.
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    // Wi-Fi is on.
    Q_PROPERTY(bool enabled READ enabled NOTIFY stateChanged)
    // What the device is doing: "off", "blocked" (a switch keeps the radio
    // off, rfkill's hard block), "starting" (on, not ready yet),
    // "disconnected", "connecting", "connected", "disconnecting"; "" without
    // a device.
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // The network joined, or being joined: { ssid, bars, address } (bars 0-4,
    // address its IPv4 address once it has one); empty otherwise.
    Q_PROPERTY(QVariantMap current READ current NOTIFY stateChanged)
    // The search mode is on.
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    // [{ ssid, bars, secured, security, known, heard, active, busy }]: the
    // networks heard, one line a name, and the saved ones out of reach: the
    // one joined first, then the saved ones, then the others, the strongest
    // first. bars is the signal, 0-4; security "open", "owe" (Enhanced Open),
    // "wpa", "sae" (WPA3), "wep" or "enterprise" (a user name too, which
    // can't be typed here), "" for one not heard; secured, a password
    // wanted; known, saved; busy "connecting", "disconnecting" or "".
    Q_PROPERTY(QVariantList networks READ networks NOTIFY networksChanged)
    // The country the radio keeps to the rules of (an ISO 3166 code, "" while
    // none is set), and its name.
    Q_PROPERTY(QString country READ country NOTIFY countryChanged)
    Q_PROPERTY(QString countryName READ countryName NOTIFY countryChanged)
    // The country can be set from here: osdos-wifi.socket is there (launcher
    // API 7).
    Q_PROPERTY(bool countryAvailable READ countryAvailable CONSTANT)
    // A country being set.
    Q_PROPERTY(bool countryBusy READ countryBusy NOTIFY countryChanged)
    // [{ code, name, letter }], by name; letter, the one it sorts under
    // ("A" for Åland), for the chooser's ◄ ►.
    Q_PROPERTY(QVariantList countries READ countries CONSTANT)
    // The country of the time zone the system is set to, for the chooser to
    // start on: "" for none (UTC).
    Q_PROPERTY(QString suggestedCountry READ suggestedCountry CONSTANT)
    // How the last connection, search or change went, in a line, for the
    // screen.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
public:
    explicit WifiManager(QObject *parent = nullptr);
#ifdef OSDOS_WIFI
    // On another bus than the system's (the tests' stand-in NetworkManager).
    explicit WifiManager(const QDBusConnection &bus, QObject *parent = nullptr);
#endif

    bool supported() const;
    bool available() const { return m_available; }
    bool enabled() const { return m_enabled; }
    QString state() const { return m_state; }
    QVariantMap current() const { return m_current; }
    bool searching() const { return m_searching; }
    QVariantList networks() const { return m_networks; }
    QString country() const { return m_country; }
    QString countryName() const;
    bool countryAvailable() const { return m_countryAvailable; }
    bool countryBusy() const { return m_countryBusy; }
    QVariantList countries() const;
    QString suggestedCountry() const;
    QString message() const { return m_message; }

    Q_INVOKABLE void setEnabled(bool on);
    Q_INVOKABLE void startSearch();
    Q_INVOKABLE void stopSearch();
    // Joins the network named ssid: as it was saved, without a password; with
    // the password, as a new one (a saved one's password changed, that one
    // is replaced once the new one is joined). "" for one without any.
    Q_INVOKABLE void connectTo(const QString &ssid, const QString &password = QString());
    // A network that doesn't say its name, joined by it: a WPA one with the
    // password, an open one without.
    Q_INVOKABLE void connectHidden(const QString &ssid, const QString &password);
    Q_INVOKABLE void disconnectNetwork();
    // Deletes what NetworkManager saved for the network: it isn't joined by
    // itself any more.
    Q_INVOKABLE void forget(const QString &ssid);
    // The country by its code: false when it can't be asked, else the
    // message says how it went.
    Q_INVOKABLE bool setCountry(const QString &code);
    Q_INVOKABLE void clearMessage();

signals:
    void stateChanged();
    void searchingChanged();
    void networksChanged();
    void countryChanged();
    void messageChanged();

#ifdef OSDOS_WIFI
private slots:
    void onServiceUnregistered();
    void onInterfacesAdded(const QDBusMessage &message);
    void onInterfacesRemoved(const QDBusMessage &message);
    void onPropertiesChanged(const QDBusMessage &message);
    void onDeviceStateChanged(const QDBusMessage &message);
    void onActiveStateChanged(const QDBusMessage &message);
    void onSavedUpdated(const QDBusMessage &message);

private:
    // An access point heard: the strongest of those with its name.
    struct Heard {
        QString path;
        int strength = 0;
        QString security;
    };
    // A connection on its way: the network, the active connection once
    // NetworkManager has answered, and the one added for it, deleted should
    // it not be joined, or the saved ones it replaces, deleted once it is.
    struct Attempt {
        QString name;
        QByteArray ssid;
        QString active;
        QString added;
        QStringList replaces;
        uint deviceReason = 0;
    };

    void load();
    void fetchSaved(const QString &path);
    void merge(const QString &path, const QString &interface, const QVariantMap &changed,
               const QStringList &invalidated = {});
    QVariantMap interfaceOf(const QString &path, const QString &interface) const;
    QStringList savedFor(const QByteArray &ssid) const;
    void scheduleRefresh();
    void refresh();
    void scan();
    void startAttempt(const QString &name, const QByteArray &ssid, const QStringList &replaces);
    void activated(int attempt, const QDBusMessage &reply, bool added);
    void checkAttempt();
    void attemptDone(bool joined, const QString &why);
    void addAndActivate(const QString &name, const QByteArray &ssid, const QString &security,
                        const QString &password, bool hidden, const QString &accessPoint);
    void call(const QString &path, const QString &interface, const QString &method,
              const QVariantList &args, int timeoutMs,
              std::function<void(const QDBusMessage &reply)> done = {});

    QDBusConnection m_bus;
    QDBusServiceWatcher *m_watcher = nullptr;
    // NetworkManager's objects as it has them: path -> interface -> its
    // properties (a QVariantMap in a QVariant), D-Bus types made plain.
    QHash<QString, QVariantMap> m_objects;
    // The saved Wi-Fi connections: path -> { ssid (bytes), hidden }.
    QHash<QString, QVariantMap> m_saved;
    QString m_device;
    QHash<QByteArray, Heard> m_heard;
    // A network's name as shown -> its SSID's bytes.
    QHash<QString, QByteArray> m_ssids;
    Attempt m_attempt;
    bool m_attempting = false;
    // Counts the attempts, so that the answer to one given up is let be.
    int m_attemptId = 0;
    QTimer m_attemptTimer;
    QTimer m_scanTimer;
    QTimer m_refreshTimer;
#endif

private:
    void setMessage(const QString &text);
    void readCountry();

    QString m_bootDir;
    bool m_available = false;
    bool m_enabled = false;
    QString m_state;
    QVariantMap m_current;
    QVariantList m_networks;
    bool m_searching = false;
    QTimer m_searchTimer;
    QString m_country;
    bool m_countryAvailable = false;
    bool m_countryBusy = false;
    QString m_message;
};
