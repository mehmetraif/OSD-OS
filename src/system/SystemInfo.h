#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <functional>
#include <optional>

// Settings → About → System Information (views/SystemInfo.qml): what this
// device is and how the network knows it, as the context property
// "systemInfo".
//
// - Its name is the system's host name (util/DeviceName.h): what a router
//   lists it under, what others reach it at (<name>.local), and the name LAN
//   Play's games are hosted under unless Online Name says another. On the
//   OSD/OS image rename() sends a new one to osdos-name.socket, where
//   osdos-stop (scripts/install.sh) sets it as root (launcher API 8).
// - rows is the page: the device (its name, model, OS, processor, memory,
//   storage, temperature and time up), then each network connection's IP and
//   MAC addresses, as [{ key, label, value, description }], a heading as
//   { type: "section", label }. refresh() reads again what changes (what
//   doesn't is read once); the page asks every few seconds while it is up.
class SystemInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    // The name can be changed from here: osdos-name.socket is there.
    Q_PROPERTY(bool renameAvailable READ renameAvailable CONSTANT)
    // A new name on its way.
    Q_PROPERTY(bool renaming READ renaming NOTIFY renamingChanged)
    // How the last rename went, in a line, for the help line.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
public:
    // A network connection, as the rows show it: Wi-Fi or Ethernet, its
    // hardware address and IP addresses.
    struct Connection {
        QString name;   // eth0, wlan0
        bool wifi = false;
        QString mac;
        QStringList ipv4;
        QStringList ipv6;
    };
    // What the rows are made of, as read() finds it, or a test's: sizes in
    // bytes and times in seconds, -1 or none where they aren't known.
    struct Facts {
        QString name;
        QString model;
        QString serial;
        QString system;     // "Debian GNU/Linux 13 (trixie)"
        QString kernel;     // "Linux 6.12.47+rpt-rpi-2712"
        QString version;    // OSD/OS's
        QString cpu;        // "Cortex-A76", "" where it isn't known
        QString arch;       // "arm64"
        int cores = 0;
        qint64 memoryTotal = -1;
        qint64 memoryFree = -1;
        QString storagePath;
        qint64 storageTotal = -1;
        qint64 storageFree = -1;
        QString mediaPath;  // "" when the media folder is on the storage's drive
        qint64 mediaTotal = -1;
        qint64 mediaFree = -1;
        std::optional<double> temperature;  // °C
        qint64 uptime = -1;
        QList<Connection> connections;
    };

    // mediaRoot gives the films' folder, Local Files' (its drive shown when
    // it is another than the data folder's); none for no such row.
    explicit SystemInfo(const QString &dataRoot, std::function<QString()> mediaRoot = {},
                        QObject *parent = nullptr);

    QString deviceName() const { return m_name; }
    QVariantList rows() const { return m_rows; }
    bool renameAvailable() const { return m_renameAvailable; }
    bool renaming() const { return m_renaming; }
    QString message() const { return m_message; }

    Q_INVOKABLE void refresh();
    // The name typed, made one the network takes (hostnameFor()), set: false
    // when it can't be (the message says why) or can't be asked for now.
    Q_INVOKABLE bool rename(const QString &typed);
    Q_INVOKABLE void clearMessage();

    // The system as it is now, the data folder's and the media folder's
    // drives among it. Linux's files under OSDOS_SYSTEM_ROOT, for tests.
    static Facts read(const QString &dataRoot, const QString &mediaRoot);
    static QVariantList rowsOf(const Facts &facts);
    // A name the network takes of what was typed (RFC 1123: letters, digits
    // and hyphens, 63 at most, neither end a hyphen), in small letters, its
    // spaces as hyphens; "" when it can't be one, why saying so.
    static QString hostnameFor(const QString &typed, QString *why = nullptr);

signals:
    void changed();
    void renamingChanged();
    void messageChanged();

private:
    // What stays as it is while OSD/OS runs: the model, serial number, OS,
    // kernel, version, processor and the memory there is.
    static void readFixed(Facts *facts);
    // What changes: the name, the free memory and storage, the temperature,
    // the time up and the connections.
    static void readChanging(Facts *facts, const QString &dataRoot, const QString &mediaRoot);
    void setMessage(const QString &text);

    QString m_dataRoot;
    std::function<QString()> m_mediaRoot;
    std::optional<Facts> m_fixed;
    QString m_name;
    QVariantList m_rows;
    bool m_renameAvailable = false;
    bool m_renaming = false;
    QString m_message;
};
