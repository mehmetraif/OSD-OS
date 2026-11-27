#include "OwnMode.h"
#include "../../modetest/GameMode.h"

namespace mame {

OwnMode ownModeFor(const Machine &machine, const QString &monitor, bool turned) {
    OwnMode out;
    modetest::MonitorRange range;
    std::string why;
    if (monitor.isEmpty()) {
        out.why = QStringLiteral("the display output names no monitor's range");
        return out;
    }
    if (!modetest::parseMonitorRange(monitor.toStdString(), &range, &why)) {
        out.why = QStringLiteral("the display output's monitor range isn't one (%1)").arg(QString::fromStdString(why));
        return out;
    }
    if (turned) {
        out.why = QStringLiteral("the monitor is turned on its side for it");
        return out;
    }
    if (machine.rotate == 90 || machine.rotate == 270) {
        out.why = QStringLiteral("a vertical game");
        return out;
    }
    if (machine.width <= 0 || machine.height <= 0) {
        out.why = QStringLiteral("no screen size and refresh rate is known for it");
        return out;
    }
    if (machine.refresh <= 0) {
        out.why = QStringLiteral("no refresh rate is known for it (MAME gives them, and has none for this set)");
        return out;
    }
    if (machine.screens != 1) {
        out.why = QStringLiteral("it has %1 screens").arg(machine.screens);
        return out;
    }
    modetest::ModeLine mode;
    if (!modetest::modeFor(kOwnModeWidth, machine.height, machine.refresh, range, &mode, &why)) {
        out.why = QString::fromStdString(why);
        return out;
    }
    out.mode = mode;
    out.why = QStringLiteral("its %1x%2 at %3 Hz").arg(machine.width).arg(machine.height)
                  .arg(machine.refresh, 0, 'f', 6);
    return out;
}

QStringList ownModeArguments(const Machine &machine, int lines, const QString &shape, bool fill, const QString &video) {
    QStringList args = screenArguments(machine, lines, shape.isEmpty() ? QStringLiteral("4:3") : shape, true, fill);
    args << QStringLiteral("-nofilter");
    if (video == QLatin1String("bgfx"))
        args << QStringLiteral("-bgfx_screen_chains") << QStringLiteral("unfiltered");
    return args;
}

} // namespace mame
