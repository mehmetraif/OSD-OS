#pragma once
#include "../../modetest/ModeLine.h"
#include "Mame.h"
#include <QString>
#include <QStringList>
#include <optional>

// A video mode of a game's own (Settings → Arcade → Emulator Video → Game's
// Own Mode): the screen put on the game's lines and refresh rate while it
// plays, so that each of its lines is one of the screen's and each of its
// frames one of the screen's, on a 15 kHz monitor driven from a Raspberry Pi
// 5's DPI output. What mode a game gets and what MAME is told on it, with no
// process and no screen of its own, so tests/arcade_test.cpp can drive it;
// RetroArch's side is libretro::ownModeConfigText(). The arithmetic is
// modetest::modeFor() (src/modetest/GameMode), the screen's change of mode
// DisplayHandoff::setMode().
namespace mame {

// How many pixels across such a mode is: a "super resolution", which a game
// of any width fills with whole columns or nearly (six to a pixel of a
// 320-wide game, five of a 384-wide one), and which a game that changes its
// width as it plays, or has more to show than its first screen says, fills
// as well. The picture is as wide on the glass whatever the number: the
// porches are times, not pixels.
inline constexpr int kOwnModeWidth = 1920;

struct OwnMode {
    std::optional<modetest::ModeLine> mode;
    // For a mode, what it was made from ("its 320x224 at 59.185606 Hz"); for
    // none, why not ("a vertical game", "its 480 lines are outside the
    // monitor's 192 to 288").
    QString why;
};

// The mode for a machine (its figures: its first screen's size, rotation and
// refresh rate, and how many screens it has) on a monitor of that range
// (monitor: Switchres's line for one, as the display preset in force names
// it; modetest::parseMonitorRange()). Horizontal games alone, on a monitor
// that isn't turned on its side for them (turned: ArcadeBackend::
// monitorFor()): a vertical game, one whose figures aren't known, one with
// more than a screen, and one the monitor's range has no mode for (too many
// lines, as an interlaced or a 25 or 31 kHz game has, a refresh rate outside
// it, or the two needing a line rate above its highest) get none, and stay
// on the mode the screen is on. Never a mode outside the monitor's range.
OwnMode ownModeFor(const Machine &machine, const QString &monitor, bool turned);

// What MAME is told of the screen on such a mode, in place of
// screenArguments(): the same for a screen of the mode's lines, whose shape
// is a tube's (shape, "4:3" where Screen Shape says nothing or square
// pixels: a mode 1920 across has none), the game's lines whole whatever
// Integer Scale says, as they are the screen's own; and no filter between
// the game's pixels, which the mode's columns are not one for one with
// (-nofilter: MAME's own default smooths them; with MAME Video on BGFX,
// video "bgfx", its unfiltered chain, as BGFX doesn't go by -nofilter).
// Never -syncrefresh nor -switchres: MAME keeps the game's own speed by its
// clock whatever mode the screen turns out to be on, and waits for the
// screen's refresh as MAME Vsync has it.
QStringList ownModeArguments(const Machine &machine, int lines, const QString &shape, bool fill, const QString &video);

} // namespace mame
