#include "GameMode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

// Each product and each sum rounded on its own, as written: a compiler that
// folds a multiplication into the addition after it (as GCC does for arm64)
// rounds once where Switchres's figures, and the tests', were rounded twice,
// and a porch could come out a pixel off on one machine and not another.
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace modetest {

namespace {

bool fail(std::string *why, const std::string &text) {
    if (why)
        *why = text;
    return false;
}

// A plain decimal number at *p ("15625", "49.50", "0.064"), whatever the
// program's locale says a decimal point is; *p moved past it.
bool decimal(const char *&p, double *value) {
    while (*p == ' ' || *p == '\t')
        p++;
    double whole = 0;
    int digits = 0;
    for (; *p >= '0' && *p <= '9'; p++, digits++)
        whole = whole * 10 + (*p - '0');
    double fraction = 0, scale = 1;
    if (*p == '.') {
        p++;
        for (; *p >= '0' && *p <= '9'; p++, digits++) {
            fraction = fraction * 10 + (*p - '0');
            scale *= 10;
        }
    }
    if (digits == 0 || digits > 15)
        return false;
    *value = whole + fraction / scale;
    return true;
}

bool separator(const char *&p, char c) {
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != c)
        return false;
    p++;
    return true;
}

// Switchres's round_near().
int roundNear(double number) {
    return int(number < 0.0 ? std::ceil(number - 0.5) : std::floor(number + 0.5));
}

// The blanking a frame needs, in seconds (Switchres's vertical_blank).
double verticalBlank(const MonitorRange &range) {
    return range.vfrontPorch / 1000 + range.vsyncPulse / 1000 + range.vbackPorch / 1000;
}

// Switchres's max_vfreq_for_yres(): the highest refresh rate at which that
// many lines and the blanking fit under the monitor's highest line rate.
double maxRefreshFor(int lines, const MonitorRange &range) {
    return range.hfreqMax / (lines + roundNear(range.hfreqMax * verticalBlank(range)));
}

// Switchres's total_lines_for_yres(): the lines of a whole frame, the
// picture's and the blanking's, and more of them until the line rate is one
// the monitor takes.
int totalLinesFor(int lines, double refresh, const MonitorRange &range) {
    const double blank = verticalBlank(range);
    int total = std::max(lines + roundNear(refresh * lines / (1.0 - refresh * blank) * blank), 1);
    while (refresh * total < range.hfreqMin && refresh * (total + 1) < range.hfreqMax)
        total++;
    return total;
}

// Switchres's get_line_params(), a pixel at a time: the front porch, the
// sync and the back porch in pixels, each as near the time the range asks
// for as a whole number of them comes, and never under nine tenths of it.
void lineParams(int width, double lineRate, const MonitorRange &range, ModeLine *mode) {
    const double frontMin = range.hfrontPorch * .90;
    const double syncMin = range.hsyncPulse * .90;
    const double backMin = range.hbackPorch * .90;
    const double lineTime = 1 / lineRate * 1000000;
    int front = 1, sync = 1, back = 1;
    double pixelTime, newPixelTime;
    do {
        pixelTime = lineTime / (width + front + sync + back);
        if (front * pixelTime < frontMin
            || std::fabs((front + 1) * pixelTime - range.hfrontPorch) < std::fabs(front * pixelTime - range.hfrontPorch))
            front++;
        if (sync * pixelTime < syncMin
            || std::fabs((sync + 1) * pixelTime - range.hsyncPulse) < std::fabs(sync * pixelTime - range.hsyncPulse))
            sync++;
        if (back * pixelTime < backMin
            || std::fabs((back + 1) * pixelTime - range.hbackPorch) < std::fabs(back * pixelTime - range.hbackPorch))
            back++;
        newPixelTime = lineTime / (width + front + sync + back);
    } while (newPixelTime != pixelTime);
    mode->hdisplay = width;
    mode->hsyncStart = width + front;
    mode->hsyncEnd = width + front + sync;
    mode->htotal = width + front + sync + back;
}

} // namespace

bool parseMonitorRange(const std::string &specs, MonitorRange *range, std::string *why) {
    MonitorRange out;
    const char *p = specs.c_str();
    double polarity[2] = { 0, 0 };
    double lines[4] = { 0, 0, 0, 0 };
    const bool read = decimal(p, &out.hfreqMin) && separator(p, '-') && decimal(p, &out.hfreqMax)
                      && separator(p, ',') && decimal(p, &out.vfreqMin) && separator(p, '-') && decimal(p, &out.vfreqMax)
                      && separator(p, ',') && decimal(p, &out.hfrontPorch)
                      && separator(p, ',') && decimal(p, &out.hsyncPulse)
                      && separator(p, ',') && decimal(p, &out.hbackPorch)
                      && separator(p, ',') && decimal(p, &out.vfrontPorch)
                      && separator(p, ',') && decimal(p, &out.vsyncPulse)
                      && separator(p, ',') && decimal(p, &out.vbackPorch)
                      && separator(p, ',') && decimal(p, &polarity[0])
                      && separator(p, ',') && decimal(p, &polarity[1])
                      && separator(p, ',') && decimal(p, &lines[0])
                      && separator(p, ',') && decimal(p, &lines[1])
                      && separator(p, ',') && decimal(p, &lines[2])
                      && separator(p, ',') && decimal(p, &lines[3]);
    while (*p == ' ' || *p == '\t')
        p++;
    if (!read || *p != '\0')
        return fail(why, "a monitor's range is sixteen numbers: the line rates, the refresh rates, six porches and "
                         "syncs, two polarities and four line counts");
    for (double value : polarity) {
        if (value != 0 && value != 1)
            return fail(why, "a sync polarity is 0 or 1");
    }
    for (double value : lines) {
        if (value != std::floor(value) || value > 10000)
            return fail(why, "a line count is a whole number");
    }
    out.hsyncPositive = polarity[0] == 1;
    out.vsyncPositive = polarity[1] == 1;
    out.progressiveLinesMin = int(lines[0]);
    out.progressiveLinesMax = int(lines[1]);
    out.interlacedLinesMin = int(lines[2]);
    out.interlacedLinesMax = int(lines[3]);
    // Rates a CRT has (Switchres's own limits), the right way round.
    if (out.hfreqMin < 14000 || out.hfreqMax < out.hfreqMin || out.hfreqMax > 540672)
        return fail(why, "the line rates aren't a monitor's");
    if (out.vfreqMin < 40 || out.vfreqMax < out.vfreqMin || out.vfreqMax > 200)
        return fail(why, "the refresh rates aren't a monitor's");
    // Each porch and sync something, and shorter than the line or the frame
    // it is part of.
    const double lineTime = 1 / out.hfreqMax * 1000000;
    for (double value : { out.hfrontPorch, out.hsyncPulse, out.hbackPorch }) {
        if (value <= 0 || value > lineTime)
            return fail(why, "a horizontal porch or sync is no part of a line");
    }
    const double frameTime = 1 / out.vfreqMax * 1000;
    for (double value : { out.vfrontPorch, out.vsyncPulse, out.vbackPorch }) {
        if (value <= 0 || value > frameTime)
            return fail(why, "a vertical porch or sync is no part of a frame");
    }
    if (out.progressiveLinesMin < 0 || out.progressiveLinesMax < out.progressiveLinesMin
        || out.interlacedLinesMin < 0 || out.interlacedLinesMax < out.interlacedLinesMin)
        return fail(why, "the line counts are the wrong way round");
    *range = out;
    return true;
}

bool modeFor(int width, int lines, double refreshHz, const MonitorRange &range, ModeLine *mode, std::string *why) {
    char text[200];
    if (width < 64 || width > 4096)
        return fail(why, "a picture is 64 to 4096 pixels across");
    if (range.hfreqMax <= 0 || range.vfreqMax <= 0 || range.progressiveLinesMax <= 0)
        return fail(why, "the monitor takes no progressive picture");
    // As Switchres takes it, a float.
    const double refresh = double(float(refreshHz));
    if (!(lines >= range.progressiveLinesMin && lines <= range.progressiveLinesMax)) {
        std::snprintf(text, sizeof text, "its %d lines are outside the monitor's %d to %d", lines,
                      range.progressiveLinesMin, range.progressiveLinesMax);
        return fail(why, text);
    }
    if (!(refresh >= range.vfreqMin && refresh <= range.vfreqMax)) {
        std::snprintf(text, sizeof text, "its %.2f Hz is outside the monitor's %.2f to %.2f Hz", refreshHz,
                      range.vfreqMin, range.vfreqMax);
        return fail(why, text);
    }
    // Switchres would lower the refresh rate to what the lines leave room
    // for: not the game's own then, so no mode.
    if (refresh > maxRefreshFor(lines, range)) {
        std::snprintf(text, sizeof text, "its %d lines at %.2f Hz need a line rate above the monitor's %.3f kHz",
                      lines, refreshHz, range.hfreqMax / 1000.0);
        return fail(why, text);
    }

    ModeLine out;
    const int total = totalLinesFor(lines, refresh, range);
    const double lineRate = refresh * total;
    // Never outside the monitor's line rates, whatever the arithmetic: a
    // range too narrow to hold a whole number of lines at this rate has no
    // mode for it.
    if (lineRate < range.hfreqMin || lineRate > range.hfreqMax) {
        std::snprintf(text, sizeof text, "its %d lines at %.2f Hz come to %.3f kHz, outside the monitor's %.3f to %.3f kHz",
                      lines, refreshHz, lineRate / 1000.0, range.hfreqMin / 1000.0, range.hfreqMax / 1000.0);
        return fail(why, text);
    }
    lineParams(width, lineRate, range, &out);
    // In whole hertz, cut as Switchres cuts it.
    out.clockMHz = double(uint64_t(out.htotal * lineRate)) / 1000000.0;

    // The frame's blanking: the sync after a front porch of the range's own
    // and half of what the frame has over its least.
    const double blankLines = roundNear(lineRate * verticalBlank(range));
    const double margin = (total - lines - blankLines) / 2;
    const double frontPorch = margin + lineRate * (range.vfrontPorch / 1000);
    out.vdisplay = lines;
    out.vsyncStart = lines + std::max(roundNear(frontPorch), 1);
    out.vsyncEnd = out.vsyncStart + std::max(roundNear(lineRate * (range.vsyncPulse / 1000)), 1);
    out.vtotal = total;
    out.hsyncPositive = range.hsyncPositive;
    out.vsyncPositive = range.vsyncPositive;
    if (!(out.vsyncEnd <= out.vtotal && out.hsyncEnd <= out.htotal))
        return fail(why, "the frame has no room for its sync");
    std::snprintf(text, sizeof text, "%dx%d_%.2f", width, lines, out.refreshHz());
    out.name = text;
    *mode = out;
    return true;
}

} // namespace modetest
