#include "DisplayOutput.h"
#include "../util/Board.h"
#include "../util/LegacyNames.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace {

// A mode with a horizontal frequency above a standard arcade monitor's
// 15 kHz: said before it is chosen, for a monitor that can't take it may be
// damaged by it.
constexpr const char *k25kHz =
    "For a medium resolution (25 kHz) or tri-sync monitor only: too high a scan rate can damage a 15 kHz one.";
constexpr const char *k31kHz =
    "For a 31 kHz (VGA) or tri-sync monitor only: too high a scan rate can damage a 15 kHz one.";

// The presets, in the menu's order, with the exit code osdos-stop
// (scripts/install.sh, STOP_HELPER) writes each on, then reboots: keep the two
// in step. kPowerOff more, it writes it and powers off. A preset is offered
// from the launcher API whose osdos-stop knows its code.
struct Preset {
    const char *id;     // osdos-display-<id>.txt
    const char *label;
    int exitCode;
    int launcherApi;
    const char *warning = nullptr;  // said as it is chosen
};
const Preset kPresets[] = {
    { "hdmi",           "HDMI",                20, 3 },
    { "crt-ntsc",       "Composite NTSC",      21, 3 },
    { "crt-pal",        "Composite PAL",       22, 3 },
    { "crt-gpio-ntsc",  "GPIO Composite NTSC", 23, 3 },
    { "crt-gpio-pal",   "GPIO Composite PAL",  24, 3 },
    { "scart-rgb-ntsc", "SCART RGB NTSC",      25, 3 },
    { "scart-rgb-pal",  "SCART RGB PAL",       26, 3 },
    { "scart-rgb-240p", "SCART RGB 240p",      27, 3 },
    { "scart-rgb-288p", "SCART RGB 288p",      28, 3 },
    { "jamma-240p",     "JAMMA 240p",          30, 4 },
    // The JAMMA board's other modes: 320 across (the games' own pixels),
    // super resolutions 1920 across, PAL's 288 lines, interlaced 480 and 576
    // lines (a Pi 4's alone), and the 25 and 31 kHz monitors'.
    { "jamma-240p-native", "JAMMA 240p Native", 31, 6 },
    { "jamma-240p-super",  "JAMMA 240p Super",  32, 6 },
    { "jamma-288p",        "JAMMA 288p",        33, 6 },
    { "jamma-288p-super",  "JAMMA 288p Super",  34, 6 },
    { "jamma-480i",        "JAMMA 480i",        35, 6 },
    { "jamma-576i",        "JAMMA 576i",        36, 6 },
    { "jamma-25k",         "JAMMA 25 kHz",      37, 6, k25kHz },
    { "jamma-31k",         "JAMMA 31 kHz",      38, 6, k31kHz },
};
// osdos-stop's for the output before the last one chosen.
constexpr int kRevertExitCode = 29;
// Added to a preset's code: written, then off (40-58).
constexpr int kPowerOff = 20;
// The launcher whose osdos-stop powers off after a preset, and writes one for
// the next start through osdos-display.socket.
constexpr int kNextStartApi = 5;
// For osdos-stop's answer to At Next Start: a few copies and a sync on the card.
constexpr int kNextStartTimeoutMs = 15000;

const Preset *presetById(const QString &id) {
    for (const Preset &p : kPresets) {
        if (id == QLatin1String(p.id))
            return &p;
    }
    return nullptr;
}

QString labelOf(const QString &id) {
    const Preset *p = presetById(id);
    return p ? QString::fromLatin1(p->label) : QStringLiteral("Custom");
}

// Whether the board has that output.
bool boardHas(const QString &id) {
    const board::Family family = board::family();
    const bool pi3 = family == board::Family::Pi3;
    const bool pi4 = family == board::Family::Pi4;
    const bool pi5 = family == board::Family::Pi5;
    if (id == QLatin1String("hdmi"))
        return pi3 || pi4 || pi5;
    // A Pi 5's composite on GPIO 4-11 (vec-gpio-pi5).
    if (id.startsWith(QLatin1String("crt-gpio-")))
        return pi5;
    // The AV jack (Pi 3, Pi 4), the TV pads (Pi 5); none on a keyboard model.
    if (id.startsWith(QLatin1String("crt-")))
        return (pi3 || pi4 || pi5) && !board::isKeyboard();
    // RGB on the GPIO pins (DPI): 480i and 576i on both, and on the Pi 4,
    // whose interlace is the firmware's, 240p and 288p should it not hold.
    if (id == QLatin1String("scart-rgb-ntsc") || id == QLatin1String("scart-rgb-pal"))
        return pi4 || pi5;
    if (id.startsWith(QLatin1String("scart-rgb-")))
        return pi4;
    // A Raspberry Pi to JAMMA board in RGB-Pi's layout: the Pi 4's firmware
    // DPI, its sound on GPIO 18 and 19; the Pi 5's kernel DPI, its sound on a
    // USB card, which interlaces only with GPIO 1, the board's I2C. Not a
    // Pi 3's.
    if (id == QLatin1String("jamma-480i") || id == QLatin1String("jamma-576i"))
        return pi4;
    if (id.startsWith(QLatin1String("jamma-")))
        return pi4 || pi5;
    return false;
}

QByteArray readFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

DisplayOutput::DisplayOutput(const QString &dataRoot, QObject *parent)
    : QObject(parent),
      m_dataRoot(dataRoot),
      m_bootDir(legacy::env("BOOT_DIR", QStringLiteral("/boot/firmware"))),
      m_runDir(legacy::env("RUN_DIR", QStringLiteral("/run"))) {
    // Not the OSD/OS image: nothing to offer.
    const QByteArray written = readFile(m_bootDir + QStringLiteral("/osdos-display.txt"));
    if (written.isEmpty())
        return;
    // Once a preset has been written for the next start, osdos-stop keeps the
    // one this boot started on in /run: that one is in force.
    const QByteArray atBoot = readFile(atBootPath());
    const QByteArray inForce = atBoot.isEmpty() ? written : atBoot;
    const int launcherApi = legacy::envInt("LAUNCHER_API");
    for (const Preset &p : kPresets) {
        const QString id = QString::fromLatin1(p.id);
        const QByteArray preset = readFile(presetPath(id));
        if (preset.isEmpty())
            continue;
        if (preset == inForce)
            m_current = id;
        else if (preset == written)
            m_next = id;
        if (boardHas(id) && launcherApi >= p.launcherApi)
            m_options.append(QVariantMap{ { QStringLiteral("id"), id },
                                          { QStringLiteral("label"), labelOf(id) },
                                          { QStringLiteral("warning"), QString::fromUtf8(p.warning ? p.warning : "") } });
    }
    m_available = legacy::envIsSet("AUTOSTART") && launcherApi >= 3 && m_options.size() > 1;
    m_powerOffAvailable = m_available && launcherApi >= kNextStartApi;
    m_nextStartAvailable = m_powerOffAvailable && QFileInfo::exists(socketPath());

    // What became of the last change.
    const QJsonObject state = QJsonDocument::fromJson(readFile(statePath())).object();
    if (state.isEmpty())
        return;
    const QString to = state.value(QStringLiteral("to")).toString();
    const QString reverted = state.value(QStringLiteral("reverted")).toString();
    if (!atBoot.isEmpty()) {
        // This boot's change, for the next start: asked about there.
        if (to.isEmpty() || to != m_next)
            QFile::remove(statePath());
        return;
    }
    if (!to.isEmpty() && to == m_current && m_available) {
        // On the new output: it stays only when said so (keep(), revert()).
        m_confirmPending = true;
        m_previousLabel = state.value(QStringLiteral("from")).toString();
        return;
    }
    if (!to.isEmpty() && to != m_current) {
        m_notice = QStringLiteral("Still on %1").arg(currentLabel());
        m_noticeDetail = QStringLiteral("The display output couldn't be switched to %1").arg(labelOf(to));
    } else if (!reverted.isEmpty()) {
        m_notice = QStringLiteral("Back to %1").arg(currentLabel());
        m_noticeDetail = QStringLiteral("%1 was not kept").arg(reverted);
    }
    QFile::remove(statePath());
}

QString DisplayOutput::boardModel() const {
    return board::model();
}

QString DisplayOutput::currentLabel() const {
    return labelOf(m_current);
}

QString DisplayOutput::nextLabel() const {
    return m_next.isEmpty() ? QString() : labelOf(m_next);
}

bool DisplayOutput::apply(const QString &id) {
    return switchTo(id, false);
}

bool DisplayOutput::applyAndPowerOff(const QString &id) {
    return switchTo(id, true);
}

bool DisplayOutput::switchTo(const QString &id, bool powerOff) {
    const Preset *p = presetById(id);
    if (!m_available || !p || id == m_current || !offered(id) || m_busy
        || (powerOff && !m_powerOffAvailable))
        return false;
    if (!writeState({ { QStringLiteral("from"), currentLabel() }, { QStringLiteral("to"), id } }))
        return false;
    const int code = p->exitCode + (powerOff ? kPowerOff : 0);
    qInfo("[DisplayOutput] %s -> %s%s: exit %d for osdos-stop", qPrintable(m_current), qPrintable(id),
          powerOff ? ", then off" : "", code);
    QCoreApplication::exit(code);
    return true;
}

bool DisplayOutput::applyAtNextStart(const QString &id) {
    // The one in force takes back what waits; another is written over it.
    const QString waiting = m_next.isEmpty() ? m_current : m_next;
    if (!m_nextStartAvailable || m_busy || !presetById(id) || id == waiting
        || (id != m_current && !offered(id)))
        return false;
    // Noted first, as for a switch: what osdos-stop writes is asked about at
    // the next start even should its answer never come back.
    if (id != m_current
        && !writeState({ { QStringLiteral("from"), currentLabel() }, { QStringLiteral("to"), id } }))
        return false;
    m_busy = true;
    emit changed();

    // osdos-stop next-start, as root, behind the socket: the name on a line,
    // "ok" or why not back. The connections are this object's, so that none
    // is left to call on it as it goes: its socket goes after them.
    auto *socket = new QLocalSocket(this);
    auto reply = std::make_shared<QByteArray>();
    auto sent = std::make_shared<bool>(false);
    auto done = std::make_shared<bool>(false);
    const auto finish = [this, socket, reply, sent, done, id](const QString &failure) {
        if (*done)
            return;
        *done = true;
        if (socket->isOpen())
            reply->append(socket->readAll());
        const QByteArray line = reply->left(reply->indexOf('\n')).trimmed();
        socket->deleteLater();
        if (line == "ok")
            nextStartDone(id, true, QString(), false);
        else
            nextStartDone(id, false, line.isEmpty() ? failure : QString::fromUtf8(line),
                          line.isEmpty() && *sent);
    };
    connect(socket, &QLocalSocket::connected, this, [socket, sent, id] {
        socket->write(id.toLatin1() + '\n');
        *sent = true;
    });
    connect(socket, &QLocalSocket::readyRead, this, [socket, reply, finish] {
        reply->append(socket->readAll());
        if (reply->contains('\n'))
            finish(QString());
    });
    connect(socket, &QLocalSocket::disconnected, this, [finish] {
        finish(QStringLiteral("no answer"));
    });
    connect(socket, &QLocalSocket::errorOccurred, this, [socket, finish](QLocalSocket::LocalSocketError error) {
        const bool away = error == QLocalSocket::ServerNotFoundError || error == QLocalSocket::ConnectionRefusedError;
        finish(away ? QStringLiteral("osdos-display.socket isn't listening") : socket->errorString());
    });
    QTimer::singleShot(kNextStartTimeoutMs, this, [finish] {
        finish(QStringLiteral("no answer in time"));
    });
    socket->connectToServer(socketPath());
    return true;
}

void DisplayOutput::nextStartDone(const QString &id, bool ok, const QString &reason, bool unanswered) {
    m_busy = false;
    // As the boot partition has it now, whatever the answer: osdos-stop may
    // have written it all the same.
    const QByteArray written = readFile(m_bootDir + QStringLiteral("/osdos-display.txt"));
    const QByteArray atBoot = readFile(atBootPath());
    m_next = atBoot.isEmpty() || written == atBoot ? QString() : presetMatching(written);
    if (!m_next.isEmpty()) {
        // What waits, to keep or go back on at the next start.
        if (!writeState({ { QStringLiteral("from"), currentLabel() }, { QStringLiteral("to"), m_next } }))
            qWarning("[DisplayOutput] %s can't be noted: no question at the next start",
                     qPrintable(statePath()));
    } else if (!unanswered || id == m_current) {
        // Nothing waits. Asked and not answered, osdos-stop may yet write
        // it: the note stays for the next start.
        QFile::remove(statePath());
    }
    if (ok)
        qInfo("[DisplayOutput] at the next start: %s", qPrintable(m_next.isEmpty() ? m_current : m_next));
    else
        qWarning("[DisplayOutput] %s not written for the next start: %s", qPrintable(id), qPrintable(reason));
    emit changed();
    emit nextStartFinished(ok, reason);
}

void DisplayOutput::keep() {
    if (!m_confirmPending)
        return;
    QFile::remove(statePath());
    m_confirmPending = false;
    emit changed();
}

void DisplayOutput::revert() {
    if (!m_confirmPending)
        return;
    writeState({ { QStringLiteral("reverted"), currentLabel() } });
    qInfo("[DisplayOutput] %s not kept: exit %d for osdos-stop", qPrintable(m_current), kRevertExitCode);
    QCoreApplication::exit(kRevertExitCode);
}

void DisplayOutput::dismissNotice() {
    if (m_notice.isEmpty())
        return;
    m_notice.clear();
    m_noticeDetail.clear();
    emit changed();
}

bool DisplayOutput::isTube(const QString &preset, const QSize &size) {
    if (size.isEmpty() || size.height() > 576)
        return false;
    if (!preset.isEmpty())
        return preset != QLatin1String("hdmi");
    return (size.height() <= 288 && size.width() >= 2 * size.height())
        || (size.width() == 720 && (size.height() == 480 || size.height() == 576));
}

bool DisplayOutput::offered(const QString &id) const {
    return std::any_of(m_options.cbegin(), m_options.cend(), [&](const QVariant &o) {
        return o.toMap().value(QStringLiteral("id")).toString() == id;
    });
}

QString DisplayOutput::presetMatching(const QByteArray &bytes) const {
    for (const Preset &p : kPresets) {
        const QString id = QString::fromLatin1(p.id);
        if (readFile(presetPath(id)) == bytes)
            return id;
    }
    return QString();
}

QString DisplayOutput::presetPath(const QString &id) const {
    return m_bootDir + QStringLiteral("/osdos-display-") + id + QStringLiteral(".txt");
}

QString DisplayOutput::atBootPath() const {
    return m_runDir + QStringLiteral("/osdos-display-boot.txt");
}

QString DisplayOutput::socketPath() const {
    return m_runDir + QStringLiteral("/osdos-display.sock");
}

QString DisplayOutput::statePath() const {
    return m_dataRoot + QStringLiteral("/display-output.json");
}

bool DisplayOutput::writeState(const QVariantMap &state) const {
    QSaveFile f(statePath());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(QJsonObject::fromVariantMap(state)).toJson(QJsonDocument::Compact));
    return f.commit();
}
