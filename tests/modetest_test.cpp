// osdos-modetest's part with no I/O (src/modetest/ModeLine): modelines read
// as Switchres and xrandr write them, the built-in modes' rates, which modes
// a screen may be given, and the picture drawn in each.
#include "modetest/ModeLine.h"

#include <QtTest>

#include <vector>

using namespace modetest;

class ModetestTest : public QObject {
    Q_OBJECT

private slots:
    // A modeline as Switchres prints it, its name in quotes with spaces in
    // it, and as xrandr takes it; and what isn't one.
    void modelinesRead() {
        ModeLine mode;
        std::string why;
        QVERIFY2(parseModeLine("Modeline \"320x224_59 15.684185KHz 59.185604Hz\" 6.524620 320 333 364 416 224 236 239 265   "
                               "-hsync -vsync", &mode, &why), why.c_str());
        QCOMPARE(QString::fromStdString(mode.name), QStringLiteral("320x224_59 15.684185KHz 59.185604Hz"));
        QCOMPARE(mode.clockMHz, 6.524620);
        QCOMPARE(mode.hdisplay, 320);
        QCOMPARE(mode.hsyncStart, 333);
        QCOMPARE(mode.hsyncEnd, 364);
        QCOMPARE(mode.htotal, 416);
        QCOMPARE(mode.vdisplay, 224);
        QCOMPARE(mode.vsyncStart, 236);
        QCOMPARE(mode.vsyncEnd, 239);
        QCOMPARE(mode.vtotal, 265);
        QVERIFY(!mode.hsyncPositive && !mode.vsyncPositive && !mode.interlace && !mode.csync);
        QVERIFY(qAbs(mode.lineRateHz() - 15684.18) < 0.1);
        QVERIFY(qAbs(mode.refreshHz() - 59.1856) < 0.001);

        // Without the word and the name; the flags in any case.
        QVERIFY2(parseModeLine("13.5 720 736 798 858 240 244 247 262 +HSync +vsync +csync", &mode, &why), why.c_str());
        QCOMPARE(QString::fromStdString(mode.name), QStringLiteral("720x240"));
        QVERIFY(mode.hsyncPositive && mode.vsyncPositive && mode.csync);
        QVERIFY(qAbs(mode.lineRateHz() - 15734.27) < 0.1);
        QVERIFY(qAbs(mode.refreshHz() - 60.05) < 0.01);

        // An interlaced one: two fields to a frame.
        QVERIFY2(parseModeLine("\"480i\" 13.5 720 739 801 858 480 488 494 525 interlace -hsync -vsync", &mode, &why),
                 why.c_str());
        QVERIFY(mode.interlace);
        QVERIFY(qAbs(mode.refreshHz() - 59.94) < 0.01);

        for (const char *bad : { "", "Modeline", "\"x\" 6.5 320 333 364",                     // too few numbers
                                 "\"x\" fast 320 333 364 416 224 236 239 265",                 // no clock
                                 "\"x\" 0 320 333 364 416 224 236 239 265",                    // no clock
                                 "\"x\" 6.5 320 333 364 416 224 236 239 265 -sync",            // no such flag
                                 "\"x\" 6.5 320 333.5 364 416 224 236 239 265",                // half a pixel
                                 "\"x\" 6.5 320 300 364 416 224 236 239 265",                  // sync inside the picture
                                 "\"x\" 6.5 320 333 364 320 224 236 239 265",                  // no blanking
                                 "\"x\" 6.5 320 333 364 416 224 236 230 265",                  // sync ends before it starts
                                 "\"x\" 6.5 320 333 364 416 224 236 239 224",                  // no blanking
                                 "\"x\" 6.5 32 33 36 41 224 236 239 265" }) {                  // no picture to speak of
            why.clear();
            QVERIFY2(!parseModeLine(bad, &mode, &why), bad);
            QVERIFY2(!why.empty(), bad);
        }
    }

    // The built-in modes: each a 15 kHz arcade monitor's, at the lines and
    // rate its name says, the first the menu's own for comparison.
    void theBuiltInModesAreArcadeModes() {
        const std::vector<ModeLine> &modes = builtInModes();
        QCOMPARE(int(modes.size()), 8);
        QCOMPARE(modes.front().vdisplay, 240);
        QVERIFY(qAbs(modes.front().refreshHz() - 60.0) < 0.01);
        int super = 0;
        for (const ModeLine &mode : modes) {
            // Switchres's arcade_15: 15.625 to 16.2 kHz, 49.5 to 65 Hz.
            QVERIFY2(mode.lineRateHz() >= 15624.0 && mode.lineRateHz() <= 16200.0, mode.name.c_str());
            QVERIFY2(mode.refreshHz() >= 49.5 && mode.refreshHz() <= 65.0, mode.name.c_str());
            QVERIFY2(!mode.hsyncPositive && !mode.vsyncPositive && !mode.interlace, mode.name.c_str());
            QVERIFY2(!mode.what.empty(), mode.name.c_str());
            // The name says the lines and the rate the mode has.
            const QString name = QString::fromStdString(mode.name);
            QCOMPARE(name.section('x', 1).section('_', 0, 0).toInt(), mode.vdisplay);
            QVERIFY2(qAbs(name.section('_', 1).toDouble() - mode.refreshHz()) < 0.01, mode.name.c_str());
            QCOMPARE(name.section('x', 0, 0).toInt(), mode.hdisplay);
            if (mode.hdisplay == 1920)
                super++;
            // Each may be shown on a screen on the JAMMA preset's 15.734 kHz,
            // and on none of another rate.
            std::string why;
            QVERIFY2(safeFor(mode, 15734.27, &why), why.c_str());
            QVERIFY2(!safeFor(mode, 31469.0, &why), mode.name.c_str());
            QVERIFY2(!safeFor(mode, 24960.0, &why), mode.name.c_str());
            QVERIFY2(!safeFor(mode, 0.0, &why), mode.name.c_str());
        }
        QCOMPARE(super, 6);
    }

    // What a screen is never given, whatever was asked for.
    void onlyModesOfTheScreensOwnRate() {
        ModeLine mode;
        std::string why;
        // A VGA mode on a 15 kHz screen.
        QVERIFY(parseModeLine("\"640x480\" 25.175 640 656 752 800 480 490 492 525 -hsync -vsync", &mode, &why));
        QVERIFY(!safeFor(mode, 15734.0, &why));
        QVERIFY2(QString::fromStdString(why).contains(QStringLiteral("31.469 kHz")), why.c_str());
        // The same on a screen that is on it.
        QVERIFY2(safeFor(mode, 31469.0, &why), why.c_str());
        // 5% is the edge: 15.0 kHz on a screen at 15.734 is inside, 14.9 outside.
        QVERIFY(parseModeLine("\"a\" 6.24 320 333 364 416 240 242 245 261", &mode, &why));   // 15.000 kHz
        QVERIFY2(safeFor(mode, 15734.0, &why), why.c_str());
        QVERIFY(parseModeLine("\"b\" 6.19 320 333 364 416 240 242 245 261", &mode, &why));   // 14.880 kHz
        QVERIFY(!safeFor(mode, 15734.0, &why));
        // Interlaced, however right its rate.
        QVERIFY(parseModeLine("\"480i\" 13.5 720 739 801 858 480 488 494 525 interlace", &mode, &why));
        QVERIFY(!safeFor(mode, 15734.0, &why));
        // Too few pictures a second, and too many.
        QVERIFY(parseModeLine("\"slow\" 6.5 320 333 364 416 288 300 303 360", &mode, &why)); // 43.4 Hz
        QVERIFY(!safeFor(mode, 15625.0, &why));
        QVERIFY(parseModeLine("\"fast\" 6.5 320 333 364 416 200 203 206 220", &mode, &why)); // 71.0 Hz
        QVERIFY(!safeFor(mode, 15625.0, &why));
        // A clock no Pi's DPI makes.
        QVERIFY(parseModeLine("\"thin\" 0.95 64 66 70 76 200 203 206 250", &mode, &why)); // 12.5 kHz, 50 Hz
        QVERIFY(!safeFor(mode, mode.lineRateHz(), &why));
        QVERIFY2(QString::fromStdString(why).contains(QStringLiteral("pixel clock")), why.c_str());
    }

    // The picture: inside its buffer whatever the stride, the edge white on
    // all four sides, as many squares as the mode's number, and the same
    // picture at 320 and at 1920 across.
    void thePictureSaysWhichModeItIs() {
        for (const ModeLine &mode : builtInModes()) {
            const int w = mode.hdisplay, h = mode.vdisplay;
            const int stride = w + 16;     // a row longer than the picture
            const int guard = 4;           // rows before and after it
            const uint32_t untouched = 0xDEADBEEF;
            std::vector<uint32_t> buffer(size_t(stride) * size_t(h + 2 * guard), untouched);
            uint32_t *pixels = buffer.data() + size_t(stride) * guard;
            const int index = 3;
            drawPattern(pixels, stride, w, h, index, mode);

            // Nothing outside the picture.
            for (int row = -guard; row < h + guard; row++) {
                for (int col = 0; col < stride; col++) {
                    const bool inside = row >= 0 && row < h && col < w;
                    const uint32_t value = pixels[row * stride + col];
                    if (!inside)
                        QVERIFY2(value == untouched, mode.name.c_str());
                    else
                        QVERIFY2(value != untouched, mode.name.c_str());
                }
            }
            // The four corners and the middle of each side: white.
            const auto at = [&](int x, int y) { return pixels[y * stride + x]; };
            for (const QPoint &p : { QPoint(0, 0), QPoint(w - 1, 0), QPoint(0, h - 1), QPoint(w - 1, h - 1),
                                     QPoint(w / 3, 0), QPoint(w / 3, h - 1), QPoint(0, h / 4), QPoint(w - 1, h / 4) })
                QVERIFY2(at(p.x(), p.y()) == 0x00FFFFFF, mode.name.c_str());
            // The squares: white runs on the row through their middle,
            // between the frame 5% in and the bars' left edge... counted
            // from 10% in, where they start.
            const int y = h / 10 + 4;
            int runs = 0;
            bool white = false;
            for (int x = w / 10; x < w / 2 - w / 320 - 1; x++) {
                const bool now = at(x, y) == 0x00FFFFFF;
                if (now && !white)
                    runs++;
                white = now;
            }
            QVERIFY2(runs == index, mode.name.c_str());
            // The bars' first colour and their last, a third down the bars.
            QCOMPARE(at(w / 10 + w / 100, h / 3 + h / 9), uint32_t(0x00FFFFFF));
            QCOMPARE(at(w - w / 10 - w / 100 - 1, h / 3 + h / 9), uint32_t(0x00404040));
        }

        // Nothing drawn, and nothing written, without a buffer or a size.
        drawPattern(nullptr, 320, 320, 224, 1, builtInModes().front());
        uint32_t one = 7;
        drawPattern(&one, 0, 320, 224, 1, builtInModes().front());
        QCOMPARE(one, uint32_t(7));
    }
};

QTEST_APPLESS_MAIN(ModetestTest)
#include "modetest_test.moc"
