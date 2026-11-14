#include "SystemSettings.h"
#include "SystemLocale.h"
#include "util/LegacyNames.h"
#include "util/RootRequest.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVariantMap>

#include <algorithm>

namespace {

// Launcher API 10: osdos-stop sets the time zone and the keyboard, through
// osdos-system.socket.
constexpr int kSystemApi = 10;
// timedatectl, or raspi-config writing the console's keyboard.
constexpr int kTimeoutMs = 30 * 1000;

// The system's files, under OSDOS_SYSTEM_ROOT for tests.
QString systemRoot() {
    return legacy::env("SYSTEM_ROOT");
}

} // namespace

SystemSettings::SystemSettings(QObject *parent)
    : QObject(parent), m_available(rootrequest::available(QStringLiteral("system"), kSystemApi)),
      m_keyboardsFile(QStringLiteral(":/setup/schema-extra.json")) {
    refresh();
}

void SystemSettings::refresh() {
    const QString zone = systemlocale::timeZone(systemRoot());
    const QString layout = systemlocale::keyboard(systemRoot());
    if (zone == m_timeZone && layout == m_keyboard)
        return;
    m_timeZone = zone;
    m_keyboard = layout;
    emit changed();
}

QString SystemSettings::keyboardName() const {
    const QVariantList all = keyboards();
    for (const QVariant &v : all) {
        const QVariantMap layout = v.toMap();
        if (layout.value(QStringLiteral("code")).toString() == m_keyboard)
            return layout.value(QStringLiteral("name")).toString();
    }
    return m_keyboard;
}

// Each country's zones from the time zone database's zone.tab (its third
// column), and UTC where the system has it, by name.
QVariantList SystemSettings::timeZones() const {
    if (m_timeZones)
        return *m_timeZones;
    const QString folder = systemRoot() + QStringLiteral("/usr/share/zoneinfo");
    QStringList names;
    QFile table(folder + QStringLiteral("/zone.tab"));
    if (table.open(QIODevice::ReadOnly | QIODevice::Text)) {
        static const QRegularExpression kZone(QStringLiteral("^[A-Za-z0-9_+-]+(/[A-Za-z0-9_+-]+){0,2}$"));
        while (!table.atEnd()) {
            const QString line = QString::fromUtf8(table.readLine());
            if (line.startsWith(QLatin1Char('#')))
                continue;
            const QString name = line.section(QLatin1Char('\t'), 2, 2).trimmed();
            if (kZone.match(name).hasMatch() && !names.contains(name))
                names << name;
        }
    }
    if (QFileInfo(folder + QStringLiteral("/UTC")).isFile())
        names << QStringLiteral("UTC");
    names.sort(Qt::CaseInsensitive);
    QVariantList zones;
    for (const QString &name : std::as_const(names))
        zones << QVariantMap{ { QStringLiteral("name"), name },
                              { QStringLiteral("region"), name.section(QLatin1Char('/'), 0, 0) } };
    m_timeZones = zones;
    return zones;
}

// OSD/OS Setup's layouts, those with Latin letters ([[name, code], …]), that
// the system has a layout's file for.
QVariantList SystemSettings::keyboards() const {
    if (m_keyboards)
        return *m_keyboards;
    QVariantList layouts;
    QFile file(m_keyboardsFile);
    if (file.open(QIODevice::ReadOnly)) {
        const QString folder = systemRoot() + QStringLiteral("/usr/share/X11/xkb/symbols/");
        static const QRegularExpression kCode(QStringLiteral("^[a-z]{2,5}$"));
        const QJsonArray all = QJsonDocument::fromJson(file.readAll()).object()
                                   .value(QLatin1String("keyboards")).toArray();
        for (const QJsonValue &v : all) {
            const QJsonArray pair = v.toArray();
            const QString code = pair.at(1).toString();
            if (kCode.match(code).hasMatch() && QFileInfo(folder + code).isFile())
                layouts << QVariantMap{ { QStringLiteral("name"), pair.at(0).toString() },
                                        { QStringLiteral("code"), code } };
        }
    }
    m_keyboards = layouts;
    return layouts;
}

bool SystemSettings::setTimeZone(const QString &zone) {
    const QVariantList all = timeZones();
    const bool listed = std::any_of(all.cbegin(), all.cend(), [&zone](const QVariant &v) {
        return v.toMap().value(QStringLiteral("name")).toString() == zone;
    });
    if (!listed || zone == m_timeZone)
        return false;
    return send(QStringLiteral("time-zone"), zone, QStringLiteral("Time zone"));
}

bool SystemSettings::setKeyboard(const QString &code) {
    const QVariantList all = keyboards();
    const bool listed = std::any_of(all.cbegin(), all.cend(), [&code](const QVariant &v) {
        return v.toMap().value(QStringLiteral("code")).toString() == code;
    });
    if (!listed || code == m_keyboard)
        return false;
    return send(QStringLiteral("keyboard"), code, QStringLiteral("Keyboard"));
}

// osdos-stop system-setting, as root, behind the socket: which and its value
// on a line, "ok" or why not back.
bool SystemSettings::send(const QString &what, const QString &value, const QString &shown) {
    if (!m_available || m_busy)
        return false;
    clearMessage();
    m_busy = true;
    emit busyChanged();
    rootrequest::ask(this, QStringLiteral("system"), (what + QLatin1Char(' ') + value).toLatin1(), kTimeoutMs,
                     [this, value, shown](const rootrequest::Answer &answer) {
        m_busy = false;
        emit busyChanged();
        // As the system has it now, whatever the answer.
        refresh();
        if (answer.ok) {
            setMessage(shown + QStringLiteral(" set"));
        } else {
            qWarning("[SystemSettings] %s not set to %s: %s", qPrintable(shown), qPrintable(value),
                     qPrintable(answer.why));
            setMessage(shown + QStringLiteral(" not set: ") + answer.why);
        }
    });
    return true;
}

void SystemSettings::clearMessage() {
    setMessage(QString());
}

void SystemSettings::setMessage(const QString &text) {
    if (m_message == text)
        return;
    m_message = text;
    emit messageChanged();
}
