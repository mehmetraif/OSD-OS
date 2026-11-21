#include "DisplayOutput.h"
#include "../util/AtomicFile.h"
#include "../util/Board.h"
#include "../util/DurableFile.h"
#include "../util/ExitCodes.h"
#include "../util/FileText.h"
#include "../util/LegacyNames.h"
#include "../util/RootRequest.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace {

// The presets, in the menu's order, as the image has them
// (os/stage-osdos/03-boot/files/display-presets.json, built in: it says what
// each field is, and where else it is read).
struct Preset {
    QString id;     // osdos-display-<id>.txt
    QString label;
    int exitCode = 0;
    int launcherApi = 0;
    QStringList boards;
    bool keyboard = true;
    QString help;     // its line in the chooser
    QString warning;  // said as it is chosen
};

const QList<Preset> &presets() {
    static const QList<Preset> list = [] {
        QList<Preset> out;
        QFile file(QStringLiteral(":/display/display-presets.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning("[DisplayOutput] %s: %s", qPrintable(file.fileName()), qPrintable(file.errorString()));
            return out;
        }
        const QJsonArray array =
            QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("presets")).toArray();
        for (const QJsonValue &v : array) {
            const QJsonObject o = v.toObject();
            Preset p;
            p.id = o.value(QStringLiteral("id")).toString();
            p.label = o.value(QStringLiteral("label")).toString();
            p.exitCode = o.value(QStringLiteral("exitCode")).toInt();
            p.launcherApi = o.value(QStringLiteral("launcherApi")).toInt();
            for (const QJsonValue &b : o.value(QStringLiteral("boards")).toArray())
                p.boards << b.toString();
            p.keyboard = o.value(QStringLiteral("keyboard")).toBool(true);
            p.help = o.value(QStringLiteral("help")).toString();
            p.warning = o.value(QStringLiteral("warning")).toString();
            out << p;
        }
        return out;
    }();
    return list;
}

// The launcher whose osdos-stop powers off after a preset, and writes one for
// the next start through osdos-display.socket.
constexpr int kNextStartApi = 5;
// For osdos-stop's answer to At Next Start: a few copies and a sync on the card.
constexpr int kNextStartTimeoutMs = 15000;

const Preset *presetById(const QString &id) {
    for (const Preset &p : presets()) {
        if (id == p.id)
            return &p;
    }
    return nullptr;
}

QString labelOf(const QString &id) {
    const Preset *p = presetById(id);
    return p ? p->label : QStringLiteral("Custom");
}

// Whether the board has that output.
bool boardHas(const Preset &p) {
    const board::Family family = board::family();
    const QLatin1String name = family == board::Family::Pi3   ? QLatin1String("pi3")
                               : family == board::Family::Pi4 ? QLatin1String("pi4")
                               : family == board::Family::Pi5 ? QLatin1String("pi5")
                                                              : QLatin1String();
    return !name.isEmpty() && p.boards.contains(name) && (p.keyboard || !board::isKeyboard());
}

} // namespace

DisplayOutput::DisplayOutput(const QString &dataRoot, QObject *parent)
    : QObject(parent),
      m_dataRoot(dataRoot),
      m_bootDir(legacy::env("BOOT_DIR", QStringLiteral("/boot/firmware"))),
      m_runDir(legacy::env("RUN_DIR", QStringLiteral("/run"))) {
    // Not the OSD/OS image: nothing to offer.
    const QByteArray written = filetext::readText(m_bootDir + QStringLiteral("/osdos-display.txt"));
    if (written.isEmpty())
        return;
    // Once a preset has been written for the next start, osdos-stop keeps the
    // one this boot started on in /run: that one is in force.
    const QByteArray atBoot = filetext::readText(atBootPath());
    const QByteArray inForce = atBoot.isEmpty() ? written : atBoot;
    const int launcherApi = legacy::envInt("LAUNCHER_API");
    for (const Preset &p : presets()) {
        const QString &id = p.id;
        const QByteArray preset = filetext::readText(presetPath(id));
        if (preset.isEmpty())
            continue;
        if (preset == inForce)
            m_current = id;
        else if (preset == written)
            m_next = id;
        if (boardHas(p) && launcherApi >= p.launcherApi)
            m_options.append(QVariantMap{ { QStringLiteral("id"), id },
                                          { QStringLiteral("label"), p.label },
                                          { QStringLiteral("help"), p.help },
                                          { QStringLiteral("warning"), p.warning } });
    }
    m_available = legacy::envIsSet("AUTOSTART") && launcherApi >= 3 && m_options.size() > 1;
    m_powerOffAvailable = m_available && launcherApi >= kNextStartApi;
    m_nextStartAvailable = m_powerOffAvailable && rootrequest::available(QStringLiteral("display"), kNextStartApi);

    // What became of the last change.
    QJsonObject state;
    QString why;
    if (!filetext::readJsonObject(statePath(), &state, &why) && !why.isEmpty())
        qWarning("[DisplayOutput] %s: %s", qPrintable(statePath()), qPrintable(why));
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
    const int code = p->exitCode + (powerOff ? exitcode::kDisplayThenPowerOff : 0);
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
    // "ok" or why not back.
    rootrequest::ask(this, QStringLiteral("display"), id.toLatin1(), kNextStartTimeoutMs,
                     [this, id](const rootrequest::Answer &answer) {
        nextStartDone(id, answer.ok, answer.why, !answer.answered && answer.sent);
    });
    return true;
}

void DisplayOutput::nextStartDone(const QString &id, bool ok, const QString &reason, bool unanswered) {
    m_busy = false;
    // As the boot partition has it now, whatever the answer: osdos-stop may
    // have written it all the same.
    const QByteArray written = filetext::readText(m_bootDir + QStringLiteral("/osdos-display.txt"));
    const QByteArray atBoot = filetext::readText(atBootPath());
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
    // On the disk before the answer counts: a cabinet switched off at the wall
    // a moment after KEEP would find the note again, and switch back.
    syncPath(m_dataRoot, true);
    m_confirmPending = false;
    emit changed();
}

void DisplayOutput::revert() {
    if (!m_confirmPending)
        return;
    if (!writeState({ { QStringLiteral("reverted"), currentLabel() } }))
        qWarning("[DisplayOutput] the note of the switch back could not be written");
    qInfo("[DisplayOutput] %s not kept: exit %d for osdos-stop", qPrintable(m_current), exitcode::kDisplayRevert);
    QCoreApplication::exit(exitcode::kDisplayRevert);
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
    for (const Preset &p : presets()) {
        if (filetext::readText(presetPath(p.id)) == bytes)
            return p.id;
    }
    return QString();
}

QString DisplayOutput::presetPath(const QString &id) const {
    return m_bootDir + QStringLiteral("/osdos-display-") + id + QStringLiteral(".txt");
}

QString DisplayOutput::atBootPath() const {
    return m_runDir + QStringLiteral("/osdos-display-boot.txt");
}

QString DisplayOutput::statePath() const {
    return m_dataRoot + QStringLiteral("/display-output.json");
}

// Whole or not at all (writeFileAtomically): on a full card a note written
// empty would let the switch go ahead with nothing to ask KEEP or SWITCH BACK
// from at the next start, on an output that may show no picture.
bool DisplayOutput::writeState(const QVariantMap &state) const {
    return writeFileAtomically(statePath(),
                               QJsonDocument(QJsonObject::fromVariantMap(state)).toJson(QJsonDocument::Compact));
}
