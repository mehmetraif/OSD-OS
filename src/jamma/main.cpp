// osdos-jamma: a Raspberry Pi to JAMMA board's controls as two gamepads.
//
// The board (RGB-Pi's JAMMA, Crown Arcade's "Raspberry Pi TO JAMMA") reads the
// cabinet's joysticks and buttons through two MCP23017 port expanders on the
// Pi's I2C0, GPIO 0 and 1 (/dev/i2c-0 on a Pi 4 and a Pi 5 alike): player 1's
// at 0x20, player 2's at 0x21. This sets their pins as inputs with pull-ups,
// reads both every 2 ms and plays what changes on a uinput gamepad each,
// "OSD/OS JAMMA P1" and "OSD/OS JAMMA P2", which OSD/OS, MAME and RetroArch
// take as any other pad: the joystick a digital stick, the buttons gamepad
// buttons (JammaPanel.h). Each has a vendor and a product of its own (1, and
// the player's number), so that SDL tells them apart. An expander that
// doesn't answer is asked again every half second, so a player whose
// expander comes late still gets a pad.
//
// The JAMMA display preset (osdos-display-jamma-240p.txt) turns I2C0 on and
// says so ("# osdos-controls: jamma"): with it in force, the expanders are
// waited for a while as the system comes up. Without it, or with neither
// expander on the bus by then, there is nothing to read: it says so and exits
// 0.
//
//   osdos-jamma [--bus /dev/i2c-N] [--map FILE] [--test]
//
// --test prints each pin as it changes, with what the mapping makes of it, and
// plays nothing: the way to find out a board's wiring. It watches any other
// expander on the bus too (0x22 to 0x27), as player "0", should a board wire
// some of its controls to a third. The mapping file,
// /boot/firmware/osdos-jamma.txt when there is one, moves pins, a line each:
// "<address> <pin> <input>", e.g. "0x21 B3 b6".

#include "JammaPanel.h"

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr uint8_t kAddresses[] = { 0x20, 0x21 };  // player 1, player 2
// The pads' vendor, as mk_arcade_joystick_rpi gives its joysticks; their
// product is the player's number. Without either, SDL knows a pad by the
// first eleven letters of its name, the same for both ("OSD/OS JAMM"), and
// gives the second the first's mapping, name and all: RetroArch, which takes
// a pad's name from that, would then have two P1s and no P2.
constexpr uint16_t kVendor = 0x0001;
// The other addresses an MCP23017 can have, watched by --test.
constexpr uint8_t kOtherAddresses[] = { 0x22, 0x23, 0x24, 0x25, 0x26, 0x27 };
constexpr const char *kDefaultBus = "/dev/i2c-0";
constexpr const char *kDefaultMap = "/boot/firmware/osdos-jamma.txt";
constexpr const char *kDisplayPreset = "/boot/firmware/osdos-display.txt";
constexpr long kPeriodNs = 2'000'000;  // a read of both every 2 ms
constexpr uint64_t kLostAfterMs = 50;  // without a good read: let go of it all
constexpr uint64_t kRetryMs = 500;     // asking a silent expander again
constexpr int kWaitForBusS = 15;       // with the JAMMA preset in force

// MCP23017 registers, with IOCON.BANK = 0 (the power-on layout): the A and B
// registers of each kind side by side, so one read from GPIOA has both ports.
constexpr uint8_t kIodirA = 0x00, kIodirB = 0x01, kIpolA = 0x02, kIpolB = 0x03;
constexpr uint8_t kIoconBank1 = 0x05, kIocon = 0x0A, kGppuA = 0x0C, kGppuB = 0x0D;
constexpr uint8_t kGpioA = 0x12;

volatile sig_atomic_t g_stop = 0;
bool g_test = false;

void onSignal(int) {
    g_stop = 1;
}

void say(const char *format, ...) {
    va_list args;
    va_start(args, format);
    std::fputs("osdos-jamma: ", stderr);
    std::vfprintf(stderr, format, args);
    std::fputc('\n', stderr);
    va_end(args);
}

uint64_t nowMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000 + uint64_t(ts.tv_nsec) / 1'000'000;
}

bool readText(const std::string &path, std::string *text) {
    std::ifstream f(path);
    if (!f)
        return false;
    std::ostringstream s;
    s << f.rdbuf();
    *text = s.str();
    return true;
}

// ── I2C ──────────────────────────────────────────────────────────────────────

bool writeReg(int bus, uint8_t address, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { reg, value };
    i2c_msg msg = { address, 0, 2, buf };
    i2c_rdwr_ioctl_data data = { &msg, 1 };
    return ioctl(bus, I2C_RDWR, &data) == 1;
}

bool readRegs(int bus, uint8_t address, uint8_t reg, uint8_t *out, uint16_t len) {
    i2c_msg msgs[2] = { { address, 0, 1, &reg }, { address, I2C_M_RD, len, out } };
    i2c_rdwr_ioctl_data data = { msgs, 2 };
    return ioctl(bus, I2C_RDWR, &data) == 2;
}

// Whether anything answers at an address: a read alone, which changes nothing
// on whatever is there.
bool answers(int bus, uint8_t address) {
    uint8_t byte = 0;
    i2c_msg msg = { address, I2C_M_RD, 1, &byte };
    i2c_rdwr_ioctl_data data = { &msg, 1 };
    return ioctl(bus, I2C_RDWR, &data) == 1;
}

// Every pin an input, with its pull-up, read as it is: the switches close to
// ground, so a pin reads 0 while its switch is held. IOCON goes first, at both
// the addresses it can have: 0x05 is IOCON with BANK = 1 (and GPINTENB, off
// anyway, with BANK = 0), and zero there puts the power-on layout back.
bool setUp(int bus, uint8_t address) {
    const uint8_t writes[][2] = {
        { kIoconBank1, 0x00 }, { kIocon, 0x00 },
        { kIodirA, 0xFF }, { kIodirB, 0xFF },
        { kIpolA, 0x00 }, { kIpolB, 0x00 },
        { kGppuA, 0xFF }, { kGppuB, 0xFF },
    };
    for (const auto &w : writes) {
        if (!writeReg(bus, address, w[0], w[1]))
            return false;
    }
    uint8_t dir[2] = {};
    return readRegs(bus, address, kIodirA, dir, 2) && dir[0] == 0xFF && dir[1] == 0xFF;
}

// Both ports, a set bit a closed switch.
bool readPressed(int bus, uint8_t address, uint16_t *pressed) {
    uint8_t port[2];
    if (!readRegs(bus, address, kGpioA, port, 2))
        return false;
    *pressed = uint16_t(~(port[0] | (port[1] << 8)));
    return true;
}

// ── uinput ───────────────────────────────────────────────────────────────────

// Player number's gamepad, with every button there is (jamma::allButtons())
// and a digital stick, -1 to 1 on each axis, as RGB-Pi's driver makes its
// joysticks.
int openPad(int number) {
    const int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return -1;
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0;
    for (const int code : jamma::allButtons())
        ok = ok && ioctl(fd, UI_SET_KEYBIT, code) == 0;
    for (const int code : { ABS_X, ABS_Y }) {
        uinput_abs_setup abs = {};
        abs.code = uint16_t(code);
        abs.absinfo.minimum = -1;
        abs.absinfo.maximum = 1;
        ok = ok && ioctl(fd, UI_SET_ABSBIT, code) == 0 && ioctl(fd, UI_ABS_SETUP, &abs) == 0;
    }
    uinput_setup setup = {};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = kVendor;
    setup.id.product = uint16_t(number);
    setup.id.version = 1;
    std::snprintf(setup.name, sizeof setup.name, "OSD/OS JAMMA P%d", number);
    ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

bool send(int pad, const std::vector<jamma::Event> &events) {
    if (events.empty())
        return true;
    std::vector<input_event> out;
    for (const jamma::Event &e : events) {
        input_event ie = {};
        ie.type = e.type;
        ie.code = e.code;
        ie.value = e.value;
        out.push_back(ie);
    }
    input_event syn = {};
    syn.type = EV_SYN;
    syn.code = SYN_REPORT;
    out.push_back(syn);
    const size_t bytes = out.size() * sizeof(input_event);
    return write(pad, out.data(), bytes) == ssize_t(bytes);
}

// ── The players ──────────────────────────────────────────────────────────────

struct Player {
    int number;             // 1, 2; 0 for another expander --test watches
    uint8_t address;
    jamma::Panel panel;
    int pad = -1;
    bool found = false;     // has answered at least once
    bool lost = true;       // not answering now: asked again at retryAt
    uint64_t retryAt = 0;
    uint64_t lastGood = 0;  // the last good read
};

// --test: each pin that changed, with what it is.
void printChanges(const Player &p, uint16_t before, uint16_t after) {
    for (int pin = 0; pin < 16; ++pin) {
        const uint16_t bit = uint16_t(1u << pin);
        if ((before ^ after) & bit) {
            std::printf("P%d 0x%02x %s %-8s %s\n", p.number, p.address, jamma::pinName(pin).c_str(),
                        (after & bit) ? "pressed" : "released",
                        jamma::inputName(p.panel.map().pins[pin]));
        }
    }
    std::fflush(stdout);
}

void deliver(Player &p, const std::vector<jamma::Event> &events) {
    if (p.pad >= 0 && !send(p.pad, events))
        say("P%d: the gamepad took no events: %s", p.number, std::strerror(errno));
}

// Whether the display preset in force is the JAMMA one, which turns I2C0 on.
bool jammaPresetInForce() {
    std::string text;
    return readText(kDisplayPreset, &text) && text.find("# osdos-controls: jamma") != std::string::npos;
}

int usage(int code) {
    std::fprintf(code ? stderr : stdout,
                 "usage: osdos-jamma [--bus /dev/i2c-N] [--map FILE] [--test]\n"
                 "Reads a Raspberry Pi to JAMMA board's two MCP23017s (0x20 player 1, 0x21\n"
                 "player 2) and plays them on two gamepads. --test prints each pin as it\n"
                 "changes instead. The mapping file (default %s, when\n"
                 "there is one) moves pins: \"<address> <pin> <input>\" a line, e.g. \"0x21 B3 b6\".\n",
                 kDefaultMap);
    return code;
}

// The player's expander answers: set up, and given its pad on the first time.
// False only when no pad can be made (no /dev/uinput).
bool takeUp(Player &p, uint64_t now) {
    p.lost = false;
    p.lastGood = now;
    if (!p.found) {
        p.found = true;
        if (p.number)
            say("player %d at 0x%02x", p.number, p.address);
        else
            say("another expander at 0x%02x", p.address);
    } else {
        say("the expander at 0x%02x answers again", p.address);
    }
    if (g_test || p.pad >= 0)
        return true;
    p.pad = openPad(p.number);
    if (p.pad < 0) {
        say("no gamepad for player %d (/dev/uinput): %s", p.number, std::strerror(errno));
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    std::string busPath = kDefaultBus;
    bool busAsked = false;
    std::string mapPath;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--test") {
            g_test = true;
        } else if (arg == "--bus" && i + 1 < argc) {
            busPath = argv[++i];
            busAsked = true;
        } else if (arg == "--map" && i + 1 < argc) {
            mapPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            return usage(0);
        } else {
            return usage(2);
        }
    }

    // The mapping: the built-in one, with the file's lines over it.
    std::vector<jamma::ChipMap> maps;
    for (const uint8_t a : kAddresses)
        maps.push_back(jamma::defaultMap(a));
    const bool mapAsked = !mapPath.empty();
    if (!mapAsked && access(kDefaultMap, F_OK) == 0)
        mapPath = kDefaultMap;
    if (!mapPath.empty()) {
        std::string text;
        if (!readText(mapPath, &text)) {
            say("%s: %s", mapPath.c_str(), std::strerror(errno));
            if (mapAsked)
                return 2;
        } else {
            std::vector<std::string> errors;
            jamma::applyMapFile(text, maps, &errors);
            for (const std::string &e : errors)
                say("%s, %s: left as it was", mapPath.c_str(), e.c_str());
            say("pins as %s has them", mapPath.c_str());
        }
    }

    struct sigaction sa = {};
    sa.sa_handler = onSignal;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // Both players from the start, neither answering yet.
    std::vector<Player> players;
    for (size_t i = 0; i < maps.size(); ++i)
        players.push_back({ int(i) + 1, kAddresses[i], jamma::Panel(maps[i]) });

    // The bus, and an expander on it. With the JAMMA preset in force they are
    // waited for while the system comes up (i2c-dev, the bus's driver).
    // Without it, as the service runs it, nothing on the bus is touched: what
    // answers at 0x20 or 0x21 there is something else's. Run by hand (--test,
    // --bus), it is looked at once.
    const bool expected = jammaPresetInForce();
    if (!expected && !g_test && !busAsked) {
        say("the JAMMA display preset isn't in force: nothing to read");
        return 0;
    }
    const uint64_t giveUpAt = nowMs() + (expected ? kWaitForBusS * 1000 : 0);
    int bus = -1;
    int openError = 0;
    bool any = false;
    for (;;) {
        if (bus < 0) {
            bus = open(busPath.c_str(), O_RDWR | O_CLOEXEC);
            openError = errno;
        }
        if (bus >= 0) {
            for (Player &p : players) {
                if (!p.found && setUp(bus, p.address)) {
                    if (!takeUp(p, nowMs()))
                        return 1;
                    any = true;
                }
            }
        }
        if (any || g_stop || nowMs() >= giveUpAt)
            break;
        usleep(250'000);
    }
    // --test: any other expander on the bus too, its pins without a mapping.
    // Only where something answers is anything written.
    if (g_test && bus >= 0) {
        for (const uint8_t a : kOtherAddresses) {
            if (!answers(bus, a) || !setUp(bus, a))
                continue;
            jamma::ChipMap none;
            none.address = a;
            players.push_back({ 0, a, jamma::Panel(none) });
            takeUp(players.back(), nowMs());
            any = true;
        }
    }
    if (!any) {
        if (bus < 0)
            say("%s: %s: nothing to read%s", busPath.c_str(), std::strerror(openError),
                expected ? "" : " (the JAMMA display preset isn't in force)");
        else
            say("no MCP23017 at 0x20 or 0x21 on %s: nothing to read", busPath.c_str());
        return 0;
    }
    if (g_test)
        std::printf("Press each control; Ctrl+C ends.\n");

    timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!g_stop) {
        const uint64_t now = nowMs();
        for (Player &p : players) {
            if (p.lost) {
                if (now < p.retryAt)
                    continue;
                if (!setUp(bus, p.address)) {
                    p.retryAt = now + kRetryMs;
                    continue;
                }
                if (!takeUp(p, now))
                    return 1;
            }
            uint16_t pressed;
            if (!readPressed(bus, p.address, &pressed)) {
                // Lost once no read has been good for a while, however long
                // each failed one took: nothing is left held meanwhile.
                if (nowMs() - p.lastGood >= kLostAfterMs) {
                    say("the expander at 0x%02x stopped answering: %s", p.address, std::strerror(errno));
                    p.lost = true;
                    p.retryAt = now + kRetryMs;
                    deliver(p, p.panel.releaseAll(now));
                }
                continue;
            }
            p.lastGood = now;
            const uint16_t before = p.panel.state();
            const std::vector<jamma::Event> events = p.panel.update(pressed, now);
            if (g_test)
                printChanges(p, before, p.panel.state());
            else
                deliver(p, events);
        }
        next.tv_nsec += kPeriodNs;
        if (next.tv_nsec >= 1'000'000'000) {
            next.tv_nsec -= 1'000'000'000;
            ++next.tv_sec;
        }
        // Behind by more than a period (a slow bus, a suspended system): start
        // the beat again from now rather than reading back to back.
        timespec current;
        clock_gettime(CLOCK_MONOTONIC, &current);
        const int64_t behindNs = (int64_t(current.tv_sec) - next.tv_sec) * 1'000'000'000
                                 + (current.tv_nsec - next.tv_nsec);
        if (behindNs > kPeriodNs)
            next = current;
        while (!g_stop && clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr) == EINTR) {
        }
    }

    for (Player &p : players) {
        if (p.pad >= 0) {
            ioctl(p.pad, UI_DEV_DESTROY);
            close(p.pad);
        }
    }
    close(bus);
    return 0;
}
