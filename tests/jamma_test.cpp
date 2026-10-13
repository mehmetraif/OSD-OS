// A Raspberry Pi to JAMMA board's pins as gamepad events (src/jamma,
// osdos-jamma): the built-in mapping, a mapping file over it, the joystick as
// a digital stick, a button held across pins, and a switch's bounce let go by.
#include "jamma/JammaPanel.h"

#include <linux/input-event-codes.h>

#include <QtTest>

#include <algorithm>

using namespace jamma;

namespace {

uint16_t bit(const char *pin) {
    return uint16_t(1u << pinFromName(pin));
}

bool has(const std::vector<Event> &events, uint16_t type, uint16_t code, int32_t value) {
    for (const Event &e : events) {
        if (e.type == type && e.code == code && e.value == value)
            return true;
    }
    return false;
}

} // namespace

class JammaTest : public QObject {
    Q_OBJECT

private slots:
    void defaultMapIsRgbPis() {
        // The layout RGB-Pi's JAMMA driver (joypi) reads, the same on both
        // chips: the joystick and Start, Coin on GPA, the buttons from GPB7 down.
        const ChipMap p1 = defaultMap(0x20);
        QCOMPARE(int(p1.address), 0x20);
        const Input expected[16] = {
            Input::Up, Input::Down, Input::Left, Input::Right,
            Input::Mode, Input::Start, Input::Coin, Input::Z,
            Input::TR2, Input::TL2, Input::TR, Input::TL,
            Input::Y, Input::X, Input::B, Input::A,
        };
        for (int pin = 0; pin < 16; ++pin)
            QCOMPARE(int(p1.pins[pin]), int(expected[pin]));
        // Button 1 on GPB7, as on both chips.
        QCOMPARE(int(defaultMap(0x21).pins[pinFromName("B7")]), int(Input::A));
    }

    void pinAndInputNames() {
        QCOMPARE(pinFromName("A0"), 0);
        QCOMPARE(pinFromName("b7"), 15);
        QCOMPARE(pinFromName("GPB3"), 11);
        QCOMPARE(pinFromName("C1"), -1);
        QCOMPARE(pinFromName("A8"), -1);
        QCOMPARE(pinName(11), std::string("B3"));
        Input in;
        QVERIFY(inputFromName("B6", &in));
        QCOMPARE(int(in), int(Input::TR));
        QVERIFY(inputFromName("select", &in));
        QCOMPARE(int(in), int(Input::Coin));
        QVERIFY(!inputFromName("b7", &in));
        QCOMPARE(buttonCode(Input::Coin), BTN_SELECT);
        QCOMPARE(buttonCode(Input::Up), 0);
        // A gamepad by SDL's reckoning (BTN_A), with every button a pin can be.
        const std::vector<int> buttons = allButtons();
        QVERIFY(std::find(buttons.begin(), buttons.end(), BTN_A) != buttons.end());
        QCOMPARE(int(buttons.size()), 13);
    }

    void mapFileMovesPins() {
        std::vector<ChipMap> maps = { defaultMap(0x20), defaultMap(0x21) };
        std::vector<std::string> errors;
        applyMapFile("# player 2's sixth button\n"
                     "0x21 B0 b6\n"
                     "0x20 A7 none   # not wired\n"
                     "0x20 B9 a\n"
                     "0x22 A0 up\n"
                     "0x20 A0\n"
                     "0x20 A1 jump\n",
                     maps, &errors);
        QCOMPARE(int(maps[1].pins[pinFromName("B0")]), int(Input::TR));
        QCOMPARE(int(maps[0].pins[pinFromName("A7")]), int(Input::None));
        QCOMPARE(int(maps[0].pins[0]), int(Input::Up));   // the bad lines change nothing
        QCOMPARE(int(maps[0].pins[1]), int(Input::Down));
        QCOMPARE(int(errors.size()), 3);
        QVERIFY(QString::fromStdString(errors[0]).startsWith("line 4: no pin"));
        QVERIFY(QString::fromStdString(errors[1]).startsWith("line 6: not <address>"));
        QVERIFY(QString::fromStdString(errors[2]).startsWith("line 7: no input"));
    }

    void buttonsGoOutAsTheirCodes() {
        Panel panel(defaultMap(0x20));
        std::vector<Event> e = panel.update(bit("B7"), 1000);  // button 1
        QCOMPARE(int(e.size()), 1);
        QVERIFY(has(e, EV_KEY, BTN_A, 1));
        e = panel.update(bit("B7") | bit("A5"), 1010);  // and Start
        QVERIFY(has(e, EV_KEY, BTN_START, 1));
        QCOMPARE(int(e.size()), 1);
        e = panel.update(bit("A6"), 1020);  // Coin; button 1 and Start let go
        QVERIFY(has(e, EV_KEY, BTN_SELECT, 1));
        QVERIFY(has(e, EV_KEY, BTN_A, 0));
        QVERIFY(has(e, EV_KEY, BTN_START, 0));
        // A pin out of the map changes the state, and sends nothing.
        std::vector<ChipMap> maps = { defaultMap(0x20) };
        applyMapFile("0x20 B0 none", maps, nullptr);
        Panel unmapped(maps[0]);
        QVERIFY(unmapped.update(bit("B0"), 1000).empty());
        QCOMPARE(int(unmapped.state()), int(bit("B0")));
    }

    void buttonHeldAcrossPins() {
        // Two pins that are the same button: it is let go only with both.
        std::vector<ChipMap> maps = { defaultMap(0x20) };
        applyMapFile("0x20 B2 a", maps, nullptr);
        Panel panel(maps[0]);
        QVERIFY(has(panel.update(bit("B7"), 1000), EV_KEY, BTN_A, 1));
        QVERIFY(panel.update(bit("B7") | bit("B2"), 1010).empty());
        QVERIFY(panel.update(bit("B2"), 1020).empty());
        QVERIFY(has(panel.update(0, 1030), EV_KEY, BTN_A, 0));
    }

    void joystickIsADigitalStick() {
        Panel panel(defaultMap(0x21));
        std::vector<Event> e = panel.update(bit("A0"), 1000);  // up
        QCOMPARE(int(e.size()), 1);
        QVERIFY(has(e, EV_ABS, ABS_Y, -1));
        e = panel.update(bit("A0") | bit("A3"), 1010);  // up and right
        QCOMPARE(int(e.size()), 1);
        QVERIFY(has(e, EV_ABS, ABS_X, 1));
        e = panel.update(bit("A0") | bit("A1") | bit("A3"), 1020);  // up and down: neither
        QVERIFY(has(e, EV_ABS, ABS_Y, 0));
        e = panel.update(0, 1030);
        QVERIFY(has(e, EV_ABS, ABS_X, 0));
        QVERIFY(!has(e, EV_ABS, ABS_Y, 0));  // it was 0 already
    }

    void bounceIsLetGoBy() {
        Panel panel(defaultMap(0x20), 5);
        QVERIFY(has(panel.update(bit("B6"), 1000), EV_KEY, BTN_B, 1));
        // Opens and closes again within 5 ms: nothing goes out.
        QVERIFY(panel.update(0, 1001).empty());
        QVERIFY(panel.update(bit("B6"), 1002).empty());
        QVERIFY(panel.update(0, 1004).empty());
        // Still open once the 5 ms are up: the release goes out then.
        QVERIFY(has(panel.update(0, 1005), EV_KEY, BTN_B, 0));
        // Another pin isn't held back by this one's change.
        QVERIFY(has(panel.update(bit("B7"), 1006), EV_KEY, BTN_A, 1));
    }

    void releaseAllLetsGoAtOnce() {
        Panel panel(defaultMap(0x20), 5);
        panel.update(bit("B7") | bit("A2"), 1000);
        const std::vector<Event> e = panel.releaseAll(1001);
        QVERIFY(has(e, EV_KEY, BTN_A, 0));
        QVERIFY(has(e, EV_ABS, ABS_X, 0));
        QCOMPARE(int(panel.state()), 0);
    }
};

QTEST_GUILESS_MAIN(JammaTest)
#include "jamma_test.moc"
