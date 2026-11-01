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
    void defaultMapIsCrowns() {
        // The layout Crown Arcade's board is wired in (mk_arcade_joystick_rpi's),
        // the same on both chips: the joystick, Start and Coin, then the
        // buttons in their order from GPA6.
        const ChipMap p1 = defaultMap(0x20);
        QCOMPARE(int(p1.address), 0x20);
        const Input expected[16] = {
            Input::Up, Input::Down, Input::Left, Input::Right,
            Input::Start, Input::Coin, Input::A, Input::B,
            Input::X, Input::Y, Input::TL, Input::TR,
            Input::TL2, Input::TR2, Input::Z, Input::Mode,
        };
        for (int pin = 0; pin < 16; ++pin)
            QCOMPARE(int(p1.pins[pin]), int(expected[pin]));
        // Button 1 on GPA6 and button 6 on GPB3, as on both chips.
        QCOMPARE(int(defaultMap(0x21).pins[pinFromName("A6")]), int(Input::A));
        QCOMPARE(int(defaultMap(0x21).pins[pinFromName("B3")]), int(Input::TR));
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
        std::vector<Event> e = panel.update(bit("A6"), 1000);  // button 1
        QCOMPARE(int(e.size()), 1);
        QVERIFY(has(e, EV_KEY, BTN_A, 1));
        e = panel.update(bit("A6") | bit("A4"), 1010);  // and Start
        QVERIFY(has(e, EV_KEY, BTN_START, 1));
        QCOMPARE(int(e.size()), 1);
        e = panel.update(bit("A5"), 1020);  // Coin; button 1 and Start let go
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
        QVERIFY(has(panel.update(bit("A6"), 1000), EV_KEY, BTN_A, 1));
        QVERIFY(panel.update(bit("A6") | bit("B2"), 1010).empty());
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
        QVERIFY(has(panel.update(bit("A7"), 1000), EV_KEY, BTN_B, 1));
        // Opens and closes again within 5 ms: nothing goes out.
        QVERIFY(panel.update(0, 1001).empty());
        QVERIFY(panel.update(bit("A7"), 1002).empty());
        QVERIFY(panel.update(0, 1004).empty());
        // Still open once the 5 ms are up: the release goes out then.
        QVERIFY(has(panel.update(0, 1005), EV_KEY, BTN_B, 0));
        // Another pin isn't held back by this one's change.
        QVERIFY(has(panel.update(bit("A6"), 1006), EV_KEY, BTN_A, 1));
    }

    void sharedCoinGoesToPlayerOneAlone() {
        // Without player 2's Start held, the coin button is player 1's, and
        // everything else goes through as it came.
        SharedCoin coin;
        std::vector<Event> p1 = { { EV_KEY, BTN_SELECT, 1 }, { EV_KEY, BTN_A, 1 } };
        std::vector<Event> p2 = { { EV_ABS, ABS_X, 1 } };
        coin.route(p1, p2);
        QVERIFY(has(p1, EV_KEY, BTN_SELECT, 1));
        QVERIFY(has(p1, EV_KEY, BTN_A, 1));
        QCOMPARE(int(p2.size()), 1);
        QVERIFY(has(p2, EV_ABS, ABS_X, 1));
        QVERIFY(!coin.routed());
        // Player 2's Start pressed after the coin: the coin stays player 1's,
        // and the Start waits for it to come out, since RetroArch takes a
        // Coin on one pad and a Start on another for its way out.
        p1 = {};
        p2 = { { EV_KEY, BTN_START, 1 } };
        coin.route(p1, p2);
        QVERIFY(p1.empty());
        QVERIFY(p2.empty());
        p1 = { { EV_KEY, BTN_SELECT, 0 } };
        p2 = {};
        coin.route(p1, p2);
        QCOMPARE(int(p1.size()), 1);
        QVERIFY(has(p1, EV_KEY, BTN_SELECT, 0));
        QCOMPARE(int(p2.size()), 1);
        QVERIFY(has(p2, EV_KEY, BTN_START, 1));
        QVERIFY(!coin.routed());
    }

    void sharedCoinHoldsPlayerOnesStartForPlayerTwosCoin() {
        // Player 2's own Coin in: player 1's Start waits for it, and is let
        // go while it goes in if held already.
        SharedCoin coin;
        std::vector<Event> p1 = { { EV_KEY, BTN_START, 1 } }, p2;
        coin.route(p1, p2);
        QVERIFY(has(p1, EV_KEY, BTN_START, 1));
        p1 = {};
        p2 = { { EV_KEY, BTN_SELECT, 1 } };
        coin.route(p1, p2);
        QCOMPARE(int(p1.size()), 1);
        QVERIFY(has(p1, EV_KEY, BTN_START, 0));
        QCOMPARE(int(p2.size()), 1);
        QVERIFY(has(p2, EV_KEY, BTN_SELECT, 1));
        p1 = {};
        p2 = { { EV_KEY, BTN_SELECT, 0 } };
        coin.route(p1, p2);
        QVERIFY(has(p2, EV_KEY, BTN_SELECT, 0));
        QVERIFY(has(p1, EV_KEY, BTN_START, 1));
        // A player's own Coin and Start, held together: their way out.
        p1 = { { EV_KEY, BTN_SELECT, 1 } };
        p2 = {};
        coin.route(p1, p2);
        QVERIFY(has(p1, EV_KEY, BTN_SELECT, 1));
    }

    void sharedCoinWithStartIsPlayerTwos() {
        SharedCoin coin;
        std::vector<Event> p1, p2 = { { EV_KEY, BTN_START, 1 } };
        coin.route(p1, p2);
        QVERIFY(has(p2, EV_KEY, BTN_START, 1));
        // The coin pressed with Start held: player 2's, its Start let go
        // first, so that the pad never has Coin and Start together (the way
        // out of a game in MAME and RetroArch).
        p1 = { { EV_KEY, BTN_SELECT, 1 }, { EV_KEY, BTN_B, 1 } };
        p2 = {};
        coin.route(p1, p2);
        QCOMPARE(int(p1.size()), 1);
        QVERIFY(has(p1, EV_KEY, BTN_B, 1));
        QCOMPARE(int(p2.size()), 2);
        QCOMPARE(int(p2[0].code), BTN_START);
        QCOMPARE(p2[0].value, 0);
        QCOMPARE(int(p2[1].code), BTN_SELECT);
        QCOMPARE(p2[1].value, 1);
        QVERIFY(coin.routed());
        // Start let go and pressed again while the coin is in: held back.
        p1 = {};
        p2 = { { EV_KEY, BTN_START, 0 } };
        coin.route(p1, p2);
        QVERIFY(p2.empty());
        p2 = { { EV_KEY, BTN_START, 1 } };
        coin.route(p1, p2);
        QVERIFY(p2.empty());
        // The coin out: player 2's, and the Start still held goes in again
        // after it, which starts player 2.
        p1 = { { EV_KEY, BTN_SELECT, 0 } };
        p2 = {};
        coin.route(p1, p2);
        QVERIFY(p1.empty());
        QCOMPARE(int(p2.size()), 2);
        QCOMPARE(int(p2[0].code), BTN_SELECT);
        QCOMPARE(p2[0].value, 0);
        QCOMPARE(int(p2[1].code), BTN_START);
        QCOMPARE(p2[1].value, 1);
        QVERIFY(!coin.routed());
        // Start let go before the coin: nothing goes in again.
        p2 = { { EV_KEY, BTN_START, 0 } };
        p1 = {};
        coin.route(p1, p2);
        QVERIFY(has(p2, EV_KEY, BTN_START, 0));
        p2 = { { EV_KEY, BTN_START, 1 } };
        coin.route(p1, p2);
        p1 = { { EV_KEY, BTN_SELECT, 1 } };
        p2 = {};
        coin.route(p1, p2);
        p1 = {};
        p2 = { { EV_KEY, BTN_START, 0 } };
        coin.route(p1, p2);
        QVERIFY(p2.empty());
        p1 = { { EV_KEY, BTN_SELECT, 0 } };
        coin.route(p1, p2);
        QCOMPARE(int(p2.size()), 1);
        QVERIFY(has(p2, EV_KEY, BTN_SELECT, 0));
    }

    void sharedCoinKeepsPlayerTwosOwnCoin() {
        // A cabinet with both coin buttons: player 2's own Coin is as it was,
        // and the shared one held over it keeps the pad's Coin in.
        SharedCoin coin;
        std::vector<Event> p1, p2 = { { EV_KEY, BTN_SELECT, 1 } };
        coin.route(p1, p2);
        QVERIFY(has(p2, EV_KEY, BTN_SELECT, 1));
        p2 = { { EV_KEY, BTN_SELECT, 0 } };
        coin.route(p1, p2);
        QVERIFY(has(p2, EV_KEY, BTN_SELECT, 0));
        p2 = { { EV_KEY, BTN_START, 1 } };
        coin.route(p1, p2);
        p1 = { { EV_KEY, BTN_SELECT, 1 } };
        p2 = {};
        coin.route(p1, p2);
        QVERIFY(has(p2, EV_KEY, BTN_SELECT, 1));
        p1 = {};
        p2 = { { EV_KEY, BTN_SELECT, 1 } };
        coin.route(p1, p2);
        QVERIFY(p2.empty());
        p2 = { { EV_KEY, BTN_SELECT, 0 } };
        coin.route(p1, p2);
        QVERIFY(p2.empty());  // the shared coin still in
    }

    void sharedCoinNeverCoinAndStartTogether() {
        // Every order of the four controls pressed and let go, read by read:
        // no Coin is in on either pad while a Start is in on either (how
        // RetroArch sees a machine's pads, all at once) but where one
        // player's own two are held (the way out a player asks for), and
        // player 1's coin goes to one pad only.
        const int codes[4][2] = { { 1, BTN_SELECT }, { 2, BTN_SELECT }, { 2, BTN_START }, { 1, BTN_START } };
        for (int order = 0; order < 65536; ++order) {
            SharedCoin coin;
            bool held[4] = {};
            bool pad1Coin = false, pad1Start = false, pad2Coin = false, pad2Start = false;
            int steps = order;
            for (int step = 0; step < 8; ++step) {
                const int which = steps % 4;
                steps /= 4;
                held[which] = !held[which];
                std::vector<Event> p1, p2;
                (codes[which][0] == 1 ? p1 : p2).push_back({ EV_KEY, uint16_t(codes[which][1]), held[which] ? 1 : 0 });
                coin.route(p1, p2);
                for (const Event &e : p1)
                    (e.code == BTN_SELECT ? pad1Coin : pad1Start) = e.value != 0;
                for (const Event &e : p2)
                    (e.code == BTN_SELECT ? pad2Coin : pad2Start) = e.value != 0;
                const bool own1 = held[0] && held[3] && pad1Coin && pad1Start;
                const bool own2 = held[1] && held[2] && pad2Coin && pad2Start;
                const QString where = QString("order %1 step %2").arg(order).arg(step);
                QVERIFY2(!((pad1Coin || pad2Coin) && (pad1Start || pad2Start)) || own1 || own2, qPrintable(where));
                QVERIFY2(pad1Coin == (held[0] && !coin.routed()), qPrintable(where));
                QVERIFY2(pad2Coin == (held[1] || coin.routed()), qPrintable(where));
                QVERIFY2(!coin.routed() || held[0], qPrintable(where));
                // A Start not held never goes in.
                QVERIFY2(!pad1Start || held[3], qPrintable(where));
                QVERIFY2(!pad2Start || held[2], qPrintable(where));
            }
            // Everything let go: so are the pads.
            for (int which = 0; which < 4; ++which) {
                if (!held[which])
                    continue;
                held[which] = false;
                std::vector<Event> p1, p2;
                (codes[which][0] == 1 ? p1 : p2).push_back({ EV_KEY, uint16_t(codes[which][1]), 0 });
                coin.route(p1, p2);
                for (const Event &e : p1)
                    (e.code == BTN_SELECT ? pad1Coin : pad1Start) = e.value != 0;
                for (const Event &e : p2)
                    (e.code == BTN_SELECT ? pad2Coin : pad2Start) = e.value != 0;
            }
            QVERIFY2(!pad1Coin && !pad1Start && !pad2Coin && !pad2Start, qPrintable(QString("order %1").arg(order)));
        }
    }

    void releaseAllLetsGoAtOnce() {
        Panel panel(defaultMap(0x20), 5);
        panel.update(bit("A6") | bit("A2"), 1000);
        const std::vector<Event> e = panel.releaseAll(1001);
        QVERIFY(has(e, EV_KEY, BTN_A, 0));
        QVERIFY(has(e, EV_ABS, ABS_X, 0));
        QCOMPARE(int(panel.state()), 0);
    }
};

QTEST_GUILESS_MAIN(JammaTest)
#include "jamma_test.moc"
