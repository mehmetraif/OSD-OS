#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A Raspberry Pi to JAMMA board's controls, as osdos-jamma reads them: two
// MCP23017 port expanders on I2C, player 1's at 0x20 and player 2's at 0x21,
// each switch closing a pin to ground. What each pin is (the joystick, the
// buttons, Start, Coin) is a mapping. The built-in one is the layout Crown
// Arcade's board is wired in, that of the driver it gives for it
// (mk_arcade_joystick_rpi): the joystick, Start and Coin, then the buttons in
// their order. A board wired otherwise takes a mapping file. Apart from the
// I2C and uinput side, so the tests can drive it.
namespace jamma {

// What a pin is. The joystick goes out as a digital stick (ABS_X, ABS_Y from
// -1 to 1), the rest as the gamepad buttons of the same names (BTN_A ...,
// Coin BTN_SELECT, Mode BTN_MODE).
enum class Input : uint8_t {
    None, Up, Down, Left, Right, Start, Coin,
    A, B, X, Y, TL, TR, TL2, TR2, C, Z, Mode,
};
constexpr int kInputCount = int(Input::Mode) + 1;

// The input a mapping file's name stands for: the names above in lower case,
// "select" for coin, "service" for mode, and b1-b6 for a, b, x, y, tl, tr.
bool inputFromName(const std::string &name, Input *out);
const char *inputName(Input input);
// The button an input goes out as (BTN_*); 0 for the joystick and None.
int buttonCode(Input input);
// Every button a pad has, whatever its pins are: SDL takes a device for a
// gamepad by its buttons, and a pin mapped later should find its button there.
std::vector<int> allButtons();

// One expander's 16 pins, GPA0-7 then GPB0-7.
struct ChipMap {
    uint8_t address = 0;
    Input pins[16] = {};
};

// The built-in mapping, the same on both expanders, in the order Crown
// Arcade's board has them: GPA0-3 the joystick (up, down, left, right), GPA4
// start, GPA5 coin, then the buttons from GPA6 up, button 1 to 6 on GPA6 to
// GPB3 (a, b, x, y, tl, tr), 7 and 8 on GPB4 and GPB5 (tl2, tr2), and GPB6 z,
// GPB7 mode.
ChipMap defaultMap(uint8_t address);
// "A0".."B7" (or "GPA0".."GPB7") -> 0..15; -1 for anything else.
int pinFromName(const std::string &name);
std::string pinName(int pin);

// A mapping file's lines over the maps, "<address> <pin> <input>" each
// ("0x21 B3 b6"; "none" takes a pin out), '#' to the end of a line a comment.
// A line for an address no map has is kept for nothing. Each line that can't
// be read is skipped and named in errors.
void applyMapFile(const std::string &text, std::vector<ChipMap> &maps,
                  std::vector<std::string> *errors);

// An event for the uinput device: EV_KEY or EV_ABS.
struct Event {
    uint16_t type;
    uint16_t code;
    int32_t value;
};

// One player's panel: its pins as last taken, and when each last changed. A
// pin's change is taken at once; its next one waits debounceMs, which lets a
// switch's bounce on closing or opening go by. An input is held while any pin
// that is it is, and goes out only as that changes.
class Panel {
public:
    explicit Panel(const ChipMap &map, int debounceMs = 5);

    // The pins read now, a set bit a closed switch (the port read inverted),
    // at nowMs; the events for what changed.
    std::vector<Event> update(uint16_t pressed, uint64_t nowMs);
    // Every pin let go at once, bounce or not: for an expander that stopped
    // answering, so nothing stays held.
    std::vector<Event> releaseAll(uint64_t nowMs);
    // The pins as last taken.
    uint16_t state() const { return m_state; }
    const ChipMap &map() const { return m_map; }

private:
    ChipMap m_map;
    int m_debounceMs;
    uint16_t m_state = 0;
    uint64_t m_changedAt[16] = {};
    bool m_changedOnce[16] = {};
    bool m_held[kInputCount] = {};
    int m_x = 0;
    int m_y = 0;
};

} // namespace jamma
