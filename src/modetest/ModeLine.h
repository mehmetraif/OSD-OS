#pragma once
#include <cstdint>
#include <string>
#include <vector>

// osdos-modetest's part with no I/O (tests/modetest_test.cpp drives it): a
// video mode as a modeline, the modes the tool tries by itself, which of them
// a screen may be given, and the picture drawn in each.
//
// The tool answers one question on the device: does this output, with this
// board and this monitor, hold a mode made for one game (its own lines and
// refresh rate), set while the system runs? docs/TODO-display-modes.md says
// what hangs on the answer.
namespace modetest {

// A mode in XFree86's modeline terms, as Switchres and xrandr write them.
struct ModeLine {
    std::string name;
    double clockMHz = 0;
    int hdisplay = 0, hsyncStart = 0, hsyncEnd = 0, htotal = 0;
    int vdisplay = 0, vsyncStart = 0, vsyncEnd = 0, vtotal = 0;
    bool hsyncPositive = false;
    bool vsyncPositive = false;
    bool interlace = false;
    bool doublescan = false;
    bool csync = false;
    // Of a built-in mode: what plays in it.
    std::string what;

    // Lines a second, and pictures a second (a progressive mode's).
    double lineRateHz() const;
    double refreshHz() const;
};

// A modeline read from text: `Modeline "name" clock hdisplay hsync-start
// hsync-end htotal vdisplay vsync-start vsync-end vtotal [flags]`, the word
// Modeline and the name left out or not, the name in quotes where it has
// spaces (Switchres's has). False with why where it isn't one, or describes
// no picture (a sync outside its blanking, totals under the picture).
bool parseModeLine(const std::string &text, ModeLine *mode, std::string *why);

// "1920x224  15.684 kHz  59.19 Hz  39.132 MHz".
std::string describe(const ModeLine &mode);

// The modes tried when none is given: what Switchres 2.2.2 (antonioginer/
// switchres at 10618ef, `switchres <w> <h> <hz> -m arcade_15 -c`) works out
// for a 15 kHz arcade monitor for the commonest kinds of game, the menu's own
// 240 lines at 60 Hz first for comparison; at 1920 across (a "super
// resolution", whose pixel clock every driver takes), and two at the games'
// own widths, whose clock is a sixth of that.
const std::vector<ModeLine> &builtInModes();

// Whether a mode may be put on a screen that is now on a mode of
// currentLineRateHz. A monitor made for one line rate can be damaged by
// another, so a mode is only ever tried within 5% of the rate the screen is
// on already, progressive, between 47 and 66 pictures a second, and with a
// pixel clock a Pi's DPI can make (1 to 200 MHz). There is no way around it.
bool safeFor(const ModeLine &mode, double currentLineRateHz, std::string *why);

// The picture for a mode, into width x height pixels of XRGB8888, stride
// pixels a row: the mode's edge in white (a picture that fits shows all four
// sides), a frame 5% in, a cross through the middle, eight colour bars,
// `index` squares at the top left (which of the modes this is), and the
// mode's lines and refresh rate in digits at the bottom left. Everything is
// as wide as it would be on a picture 320 across, so it looks the same at
// 1920.
void drawPattern(uint32_t *pixels, int stride, int width, int height, int index, const ModeLine &mode);

} // namespace modetest
