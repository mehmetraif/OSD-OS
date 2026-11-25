#pragma once
#include "util/FileText.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

// The system's time zone, keyboard layout, SSH and firewall, as the files say: read by
// Settings → System's rows (SystemSettings) and by the export to OSD/OS Setup
// (util/SetupExport). `root` stands before every path, for tests.
namespace systemlocale {

// The time zone: /etc/localtime's link into zoneinfo, as timedatectl sets
// it, else /etc/timezone. Empty where neither names one.
inline QString timeZone(const QString &root = QString()) {
    static const QRegularExpression kZone(QStringLiteral("^[A-Za-z0-9_+-]+(/[A-Za-z0-9_+-]+)*$"));
    const QString link = QFileInfo(root + QStringLiteral("/etc/localtime")).symLinkTarget();
    const qsizetype at = link.indexOf(QLatin1String("/zoneinfo/"));
    QString zone = at >= 0 ? link.mid(at + 10) : QString();
    if (zone.isEmpty()) {
        QFile file(root + QStringLiteral("/etc/timezone"));
        if (file.open(QIODevice::ReadOnly))
            zone = QString::fromUtf8(file.read(256)).trimmed();
    }
    return kZone.match(zone).hasMatch() && !zone.contains(QLatin1String("..")) ? zone : QString();
}

// The keyboard: the first of XKBLAYOUT's layouts in /etc/default/keyboard,
// as raspi-config sets it. Empty where the file names none.
inline QString keyboard(const QString &root = QString()) {
    static const QRegularExpression kLayout(QStringLiteral("^\\s*XKBLAYOUT\\s*=\\s*\"?([a-z]{2,5})"),
                                            QRegularExpression::MultilineOption);
    QFile file(root + QStringLiteral("/etc/default/keyboard"));
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QRegularExpressionMatch layout = kLayout.match(QString::fromUtf8(file.read(64 * 1024)));
    return layout.hasMatch() ? layout.captured(1) : QString();
}

// SSH: -1 where it isn't there to be turned on (no ssh.service), else 1
// when systemd starts it (enabled: its link in multi-user.target.wants, as
// systemctl enable makes it), 0 when it doesn't.
inline int ssh(const QString &root = QString()) {
    const bool installed = QFileInfo::exists(root + QStringLiteral("/usr/lib/systemd/system/ssh.service"))
                           || QFileInfo::exists(root + QStringLiteral("/lib/systemd/system/ssh.service"));
    if (!installed)
        return -1;
    const QFileInfo wanted(root + QStringLiteral("/etc/systemd/system/multi-user.target.wants/ssh.service"));
    return wanted.exists() || wanted.isSymLink() ? 1 : 0;
}

// The firewall (osdos-firewall.service, which lets only the home network's
// addresses connect): -1 where it isn't there to be turned on (no unit, as on
// a system set up before it, or no nft to load its rules), else 1 when
// systemd starts it (its link in multi-user.target.wants), 0 when it doesn't.
inline int firewall(const QString &root = QString()) {
    if (!QFileInfo::exists(root + QStringLiteral("/etc/systemd/system/osdos-firewall.service"))
        || !QFileInfo::exists(root + QStringLiteral("/usr/sbin/nft")))
        return -1;
    const QFileInfo wanted(root + QStringLiteral("/etc/systemd/system/multi-user.target.wants/osdos-firewall.service"));
    return wanted.exists() || wanted.isSymLink() ? 1 : 0;
}

// Whether its rules are in force now: /run/osdos-firewall, its unit's
// RuntimeDirectory=, is there from its start until it stops or fails.
inline bool firewallLoaded(const QString &root = QString()) {
    return QFileInfo(root + QStringLiteral("/run/osdos-firewall")).isDir();
}

// The public keys in home's ~/.ssh/authorized_keys that log in, each as its
// line has it, once: a type, then its key, as the card's setup takes one
// (public_key() in osdos-setup).
inline QStringList sshKeys(const QString &home) {
    static const QRegularExpression kKey(
        QStringLiteral("^(ssh-(rsa|dss|ed25519)|ecdsa-sha2-nistp(256|384|521)|sk-(ssh-ed25519|ecdsa-sha2-nistp256)@openssh\\.com)"
                       "\\s+[A-Za-z0-9+/]+={0,3}(\\s.*)?$"));
    QStringList keys;
    const QStringList lines =
        QString::fromUtf8(filetext::readText(home + QStringLiteral("/.ssh/authorized_keys"), 256 * 1024))
            .split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString key = line.trimmed();
        if (kKey.match(key).hasMatch() && !keys.contains(key))
            keys << key;
    }
    return keys;
}

} // namespace systemlocale
