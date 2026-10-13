#include "JammaPanel.h"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace jamma {

namespace {

struct InputInfo {
    Input input;
    const char *name;
    int code;
};
// Every input with its name and button, None first.
const InputInfo kInputs[] = {
    { Input::None,  "none",  0 },
    { Input::Up,    "up",    0 },
    { Input::Down,  "down",  0 },
    { Input::Left,  "left",  0 },
    { Input::Right, "right", 0 },
    { Input::Start, "start", BTN_START },
    { Input::Coin,  "coin",  BTN_SELECT },
    { Input::A,     "a",     BTN_A },
    { Input::B,     "b",     BTN_B },
    { Input::X,     "x",     BTN_X },
    { Input::Y,     "y",     BTN_Y },
    { Input::TL,    "tl",    BTN_TL },
    { Input::TR,    "tr",    BTN_TR },
    { Input::TL2,   "tl2",   BTN_TL2 },
    { Input::TR2,   "tr2",   BTN_TR2 },
    { Input::C,     "c",     BTN_C },
    { Input::Z,     "z",     BTN_Z },
    { Input::Mode,  "mode",  BTN_MODE },
};
// Other names for some: what a cabinet calls them.
struct Alias {
    const char *name;
    Input input;
};
const Alias kAliases[] = {
    { "select", Input::Coin }, { "service", Input::Mode },
    { "b1", Input::A }, { "b2", Input::B }, { "b3", Input::X },
    { "b4", Input::Y }, { "b5", Input::TL }, { "b6", Input::TR },
};

const InputInfo &infoOf(Input input) {
    for (const InputInfo &i : kInputs) {
        if (i.input == input)
            return i;
    }
    return kInputs[0];
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The hat's value from two opposite directions: -1, 0 (neither, or both) or 1.
int axis(bool negative, bool positive) {
    return (positive ? 1 : 0) - (negative ? 1 : 0);
}

} // namespace

bool inputFromName(const std::string &name, Input *out) {
    const std::string n = lower(name);
    for (const InputInfo &i : kInputs) {
        if (n == i.name) {
            *out = i.input;
            return true;
        }
    }
    for (const Alias &a : kAliases) {
        if (n == a.name) {
            *out = a.input;
            return true;
        }
    }
    return false;
}

const char *inputName(Input input) {
    return infoOf(input).name;
}

int buttonCode(Input input) {
    return infoOf(input).code;
}

std::vector<int> allButtons() {
    std::vector<int> codes;
    for (const InputInfo &i : kInputs) {
        if (i.code)
            codes.push_back(i.code);
    }
    return codes;
}

ChipMap defaultMap(uint8_t address) {
    ChipMap map;
    map.address = address;
    const Input pins[16] = {
        // GPA0-7
        Input::Up, Input::Down, Input::Left, Input::Right,
        Input::Mode, Input::Start, Input::Coin, Input::Z,
        // GPB0-7: the buttons from GPB7 down
        Input::TR2, Input::TL2, Input::TR, Input::TL,
        Input::Y, Input::X, Input::B, Input::A,
    };
    std::copy(std::begin(pins), std::end(pins), map.pins);
    return map;
}

int pinFromName(const std::string &name) {
    std::string n = lower(name);
    if (n.rfind("gp", 0) == 0)
        n = n.substr(2);
    if (n.size() != 2 || (n[0] != 'a' && n[0] != 'b') || n[1] < '0' || n[1] > '7')
        return -1;
    return (n[0] == 'b' ? 8 : 0) + (n[1] - '0');
}

std::string pinName(int pin) {
    if (pin < 0 || pin > 15)
        return "?";
    return std::string(1, pin < 8 ? 'A' : 'B') + static_cast<char>('0' + pin % 8);
}

void applyMapFile(const std::string &text, std::vector<ChipMap> &maps,
                  std::vector<std::string> *errors) {
    std::istringstream lines(text);
    std::string line;
    int number = 0;
    while (std::getline(lines, line)) {
        ++number;
        const size_t hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        std::istringstream words(line);
        std::string address, pin, input, extra;
        if (!(words >> address))
            continue;
        const auto fail = [&](const std::string &why) {
            if (errors)
                errors->push_back("line " + std::to_string(number) + ": " + why);
        };
        if (!(words >> pin >> input) || (words >> extra)) {
            fail("not <address> <pin> <input>");
            continue;
        }
        char *end = nullptr;
        const long a = std::strtol(address.c_str(), &end, 0);
        if (!end || *end || a < 0x03 || a > 0x77) {
            fail("no I2C address: " + address);
            continue;
        }
        const int p = pinFromName(pin);
        if (p < 0) {
            fail("no pin: " + pin + " (A0-A7, B0-B7)");
            continue;
        }
        Input in;
        if (!inputFromName(input, &in)) {
            fail("no input: " + input);
            continue;
        }
        for (ChipMap &m : maps) {
            if (m.address == a)
                m.pins[p] = in;
        }
    }
}

Panel::Panel(const ChipMap &map, int debounceMs)
    : m_map(map), m_debounceMs(debounceMs) {}

std::vector<Event> Panel::update(uint16_t pressed, uint64_t nowMs) {
    const uint16_t changed = pressed ^ m_state;
    for (int pin = 0; pin < 16; ++pin) {
        const uint16_t bit = uint16_t(1u << pin);
        if (!(changed & bit))
            continue;
        if (m_changedOnce[pin] && nowMs - m_changedAt[pin] < uint64_t(m_debounceMs))
            continue;
        m_state ^= bit;
        m_changedAt[pin] = nowMs;
        m_changedOnce[pin] = true;
    }
    // Each input held while any pin that is it is, however many there are.
    bool held[kInputCount] = {};
    for (int pin = 0; pin < 16; ++pin) {
        if (m_state & (1u << pin))
            held[int(m_map.pins[pin])] = true;
    }
    std::vector<Event> events;
    for (int i = int(Input::Start); i < kInputCount; ++i) {
        if (held[i] != m_held[i])
            events.push_back({ EV_KEY, uint16_t(buttonCode(Input(i))), held[i] ? 1 : 0 });
    }
    std::copy(std::begin(held), std::end(held), m_held);
    // The joystick, a digital stick, as RGB-Pi's driver has it.
    const int x = axis(held[int(Input::Left)], held[int(Input::Right)]);
    const int y = axis(held[int(Input::Up)], held[int(Input::Down)]);
    if (x != m_x)
        events.push_back({ EV_ABS, ABS_X, x });
    if (y != m_y)
        events.push_back({ EV_ABS, ABS_Y, y });
    m_x = x;
    m_y = y;
    return events;
}

std::vector<Event> Panel::releaseAll(uint64_t nowMs) {
    std::fill(std::begin(m_changedOnce), std::end(m_changedOnce), false);
    return update(0, nowMs);
}

} // namespace jamma
