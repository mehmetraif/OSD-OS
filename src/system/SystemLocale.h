#pragma once
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>

// The system's time zone and keyboard layout, as the files say: read by
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

} // namespace systemlocale
