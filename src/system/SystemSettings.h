#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>

#include <optional>

// Settings → System's Time Zone and Keyboard.
// - What the system has is read from its files (system/SystemLocale.h):
//   /etc/localtime's zone, and /etc/default/keyboard's layout, the console's,
//   in which a name and a password are typed at Exit to Terminal.
// - On the OSD/OS image a new one goes to osdos-system.socket, where
//   osdos-stop (scripts/install.sh) sets it as root (launcher API 10):
//   "time-zone <zone>" or "keyboard <layout>" on a line, "ok" or why not back.
//   Elsewhere the rows only show what the system has.
// - timeZones is every zone the system's time zone database lists by country
//   (zone.tab) and UTC, by name: [{ name, region }], the region what stands
//   before the first slash. keyboards is OSD/OS Setup's list of layouts
//   (schema-extra.json, built into the app), those the system has:
//   [{ name, code }].
class SystemSettings : public QObject {
    Q_OBJECT
    // The two can be changed from here: osdos-system.socket is there.
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString timeZone READ timeZone NOTIFY changed)
    Q_PROPERTY(QString keyboard READ keyboard NOTIFY changed)
    // The layout by its name in the list, else its code.
    Q_PROPERTY(QString keyboardName READ keyboardName NOTIFY changed)
    Q_PROPERTY(QVariantList timeZones READ timeZones CONSTANT)
    Q_PROPERTY(QVariantList keyboards READ keyboards CONSTANT)
    // A new zone or layout on its way.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // How the last change went, in a line, for the help line.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
public:
    explicit SystemSettings(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString timeZone() const { return m_timeZone; }
    QString keyboard() const { return m_keyboard; }
    QString keyboardName() const;
    QVariantList timeZones() const;
    QVariantList keyboards() const;
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }

    // What the system has now, read again.
    Q_INVOKABLE void refresh();
    // A zone or a layout of the lists, sent to the helper. False when it
    // isn't sent: not available, another on its way, not of the list, or the
    // one the system has already.
    Q_INVOKABLE bool setTimeZone(const QString &zone);
    Q_INVOKABLE bool setKeyboard(const QString &code);
    Q_INVOKABLE void clearMessage();

signals:
    void changed();
    void busyChanged();
    void messageChanged();

private:
    friend class SystemSettingsTest;
    bool send(const QString &what, const QString &value, const QString &shown);
    void setMessage(const QString &text);

    bool m_available;
    // OSD/OS Setup's layouts: the app's own copy, or a test's.
    QString m_keyboardsFile;
    QString m_timeZone;
    QString m_keyboard;
    bool m_busy = false;
    QString m_message;
    mutable std::optional<QVariantList> m_timeZones;
    mutable std::optional<QVariantList> m_keyboards;
};
