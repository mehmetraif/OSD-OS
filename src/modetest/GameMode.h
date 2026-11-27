#pragma once
#include "ModeLine.h"

#include <string>

// A video mode made for one game: from its lines and its refresh rate, and
// the ranges of the monitor it is shown on. No I/O, and no Qt: the app works
// each game's mode out with it (Settings → Arcade → Emulator Video → Game's
// Own Mode), osdos-modetest tries one by hand (--game), and
// tests/modetest_test.cpp holds it to the eight modes the tool carries.
//
// The arithmetic is Switchres's (antonioginer/switchres, GPL-2.0 or later, at
// 10618ef: modeline_create(), get_line_params(), total_lines_for_yres() and
// max_vfreq_for_yres() in its modeline.cpp), written again here for the one
// case OSD/OS needs: a progressive mode whose lines are the game's own, one
// for one. What Switchres does besides (interlaced and doublescan modes,
// lines doubled or stretched into a range, a refresh rate bent to fit) is
// left out on purpose: such a mode would not be the game's own, so none is
// made and the game stays on the mode the screen is on.
namespace modetest {

// What a monitor takes, as Switchres writes a range of one: the line rates
// and the refresh rates it holds, the horizontal porches and sync it wants
// (in microseconds: the same on the glass whatever the mode's width), the
// vertical ones (in milliseconds), the sync polarities, and the lines of a
// progressive and of an interlaced picture.
struct MonitorRange {
    double hfreqMin = 0, hfreqMax = 0;                          // Hz
    double vfreqMin = 0, vfreqMax = 0;                          // Hz
    double hfrontPorch = 0, hsyncPulse = 0, hbackPorch = 0;     // µs
    double vfrontPorch = 0, vsyncPulse = 0, vbackPorch = 0;     // ms
    bool hsyncPositive = false, vsyncPositive = false;
    int progressiveLinesMin = 0, progressiveLinesMax = 0;
    int interlacedLinesMin = 0, interlacedLinesMax = 0;
};

// Switchres's range for a standard resolution (15 kHz) arcade monitor, its
// arcade_15: 15.625 to 16.2 kHz, 49.5 to 65 Hz, 192 to 288 lines. The
// Display Output presets for such a monitor name it
// (os/stage-osdos/03-boot/files/display-presets.json, "monitor").
inline constexpr char kArcade15[] =
    "15625-16200, 49.50-65.00, 2.000, 4.700, 8.000, 0.064, 0.192, 1.024, 0, 0, 192, 288, 448, 576";

// A range read from Switchres's line for one (its switchres.ini's
// crt_range, monitor.cpp's presets): "hfreq_min-hfreq_max,
// vfreq_min-vfreq_max, hfront_porch, hsync_pulse, hback_porch,
// vfront_porch, vsync_pulse, vback_porch, hsync_polarity, vsync_polarity,
// progressive_lines_min, progressive_lines_max, interlaced_lines_min,
// interlaced_lines_max". False with why for a line that isn't one, or
// that describes no monitor (a rate or a porch of nothing, limits the wrong
// way round).
bool parseMonitorRange(const std::string &specs, MonitorRange *range, std::string *why);

// The mode for a game of that many lines at that refresh rate, width pixels
// across, on a monitor of that range: the lines the game's own, the refresh
// rate its own (to the hertz the pixel clock is counted in), the line rate
// the lowest the monitor takes at which they fit, the blanking and the sync
// as the range asks. False with why, and no mode, where the monitor can't
// show it so: lines outside its progressive ones, a refresh rate outside its
// range, or the two together needing a line rate above its highest. Never a
// mode outside the range.
//
// The refresh rate is taken as Switchres takes one, in single precision, so
// that the mode is Switchres's own to the hertz.
bool modeFor(int width, int lines, double refreshHz, const MonitorRange &range, ModeLine *mode, std::string *why);

} // namespace modetest
