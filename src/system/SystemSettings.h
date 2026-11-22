#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>

#include <optional>

// Settings → System's Time Zone, Keyboard and SSH.
// - What the system has is read from its files (system/SystemLocale.h):
//   /etc/localtime's zone, /etc/default/keyboard's layout, the console's,
//   in which a name and a password are typed at Exit to Terminal, and
//   whether systemd starts ssh.service.
// - On the OSD/OS image a new one goes to osdos-system.socket, where
//   osdos-stop (scripts/install.sh) sets it as root (launcher API 10):
//   "time-zone <zone>", "keyboard <layout>", "ssh on" or "ssh off" on a
//   line, "ok" or why not back. SSH only from launcher API 11: the helper
//   is the image's (or install.sh's), not the app's, so an app updated in
//   place over an older image has one that doesn't know it; there the row
//   shows what systemd has and says why it can't be changed (sshOff).
//   Elsewhere the rows only show what the system has, and SSH is off.
// - timeZones is every zone the system's time zone database lists by country
//   (zone.tab) and UTC, by name: [{ name, region }], the region what stands
//   before the first slash. keyboards is OSD/OS Setup's list of layouts
//   (schema-extra.json, built into the app), those the system has:
//   [{ name, code }].
// - SSH only runs or doesn't from here: the password, the keys and whether a
//   password logs in are OSD/OS Setup's alone.
class SystemSettings : public QObject {
    Q_OBJECT
    // They can be changed from here: osdos-system.socket is there.
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString timeZone READ timeZone NOTIFY changed)
    Q_PROPERTY(QString keyboard READ keyboard NOTIFY changed)
    // The layout by its name in the list, else its code.
    Q_PROPERTY(QString keyboardName READ keyboardName NOTIFY changed)
    Q_PROPERTY(QVariantList timeZones READ timeZones CONSTANT)
    Q_PROPERTY(QVariantList keyboards READ keyboards CONSTANT)
    // The SSH service enabled (on the image only).
    Q_PROPERTY(bool ssh READ ssh NOTIFY changed)
    // Why SSH can't be turned on or off from here although the rest can, in
    // a line, or empty: the helper is older than SSH (launcher API under
    // 11), or the system has no ssh.service. Empty off the image too, where
    // nothing here can be set (available).
    Q_PROPERTY(QString sshOff READ sshOff CONSTANT)
    // Why nobody could sign in yet although SSH is on, in a line, or empty:
    // the app's user has no key in ~/.ssh/authorized_keys and, as the helper
    // said when SSH was turned on here, no password.
    Q_PROPERTY(QString sshNote READ sshNote NOTIFY changed)
    // A change on its way.
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
    bool ssh() const { return m_ssh; }
    QString sshOff() const { return m_sshOff; }
    QString sshNote() const { return m_sshNote; }
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }

    // What the system has now, read again.
    Q_INVOKABLE void refresh();
    // A zone or a layout of the lists, sent to the helper. False when it
    // isn't sent: not available, another on its way, not of the list, or the
    // one the system has already.
    Q_INVOKABLE bool setTimeZone(const QString &zone);
    Q_INVOKABLE bool setKeyboard(const QString &code);
    // SSH on (enabled and started) or off (disabled and stopped), sent to
    // the helper. False when it isn't sent: not available (or sshOff),
    // another on its way, or as the system has it already.
    Q_INVOKABLE bool setSsh(bool on);
    Q_INVOKABLE void clearMessage();

signals:
    void changed();
    void busyChanged();
    void messageChanged();

private:
    friend class SystemSettingsTest;
    // done and notDone head the message: "Time zone set", "Time zone not
    // set: <why>".
    bool send(const QString &what, const QString &value, const QString &done, const QString &notDone);
    void setMessage(const QString &text);

    bool m_available;
    // OSD/OS Setup's layouts: the app's own copy, or a test's.
    QString m_keyboardsFile;
    QString m_timeZone;
    QString m_keyboard;
    bool m_ssh = false;
    QString m_sshOff;
    QString m_sshNote;
    // The helper said, as SSH was last turned on here, that the app's user
    // has no password to log in with.
    bool m_sshNoPassword = false;
    bool m_busy = false;
    QString m_message;
    mutable std::optional<QVariantList> m_timeZones;
    mutable std::optional<QVariantList> m_keyboards;
};
