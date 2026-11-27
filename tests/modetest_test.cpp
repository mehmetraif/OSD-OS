// osdos-modetest's part with no I/O (src/modetest/ModeLine): modelines read
// as Switchres and xrandr write them, the built-in modes' rates, which modes
// a screen may be given, and the picture drawn in each. And a game's own mode
// (src/modetest/GameMode), which the app works out too: a monitor's range
// read, the tool's eight modes made again from their lines and rates, and no
// mode where the monitor can't show the game as it is.
#include "modetest/GameMode.h"
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

    // A monitor's range as Switchres writes one, and what isn't one.
    void aMonitorsRangeIsRead() {
        MonitorRange range;
        std::string why;
        QVERIFY2(parseMonitorRange(kArcade15, &range, &why), why.c_str());
        QCOMPARE(range.hfreqMin, 15625.0);
        QCOMPARE(range.hfreqMax, 16200.0);
        QCOMPARE(range.vfreqMin, 49.5);
        QCOMPARE(range.vfreqMax, 65.0);
        QCOMPARE(range.hfrontPorch, 2.0);
        QCOMPARE(range.hsyncPulse, 4.7);
        QCOMPARE(range.hbackPorch, 8.0);
        QCOMPARE(range.vfrontPorch, 0.064);
        QCOMPARE(range.vsyncPulse, 0.192);
        QCOMPARE(range.vbackPorch, 1.024);
        QVERIFY(!range.hsyncPositive && !range.vsyncPositive);
        QCOMPARE(range.progressiveLinesMin, 192);
        QCOMPARE(range.progressiveLinesMax, 288);
        QCOMPARE(range.interlacedLinesMin, 448);
        QCOMPARE(range.interlacedLinesMax, 576);
        // Without the spaces, and with positive syncs.
        QVERIFY2(parseMonitorRange("15250-16500,40-80,2.187,4.688,6.719,0.190,0.191,1.018,1,1,192,288,448,576", &range, &why),
                 why.c_str());
        QVERIFY(range.hsyncPositive && range.vsyncPositive);
        QCOMPARE(range.vfreqMax, 80.0);

        const MonitorRange before = range;
        for (const char *bad : { "", "arcade_15", "15625-16200",
                                 "15625-16200, 49.50-65.00, 2.000, 4.700, 8.000, 0.064, 0.192, 1.024, 0, 0, 192, 288, 448",       // a number short
                                 "15625-16200, 49.50-65.00, 2.000, 4.700, 8.000, 0.064, 0.192, 1.024, 0, 0, 192, 288, 448, 576, 1", // one over
                                 "15625-16200, 49,50-65,00, 2.000, 4.700, 8.000, 0.064, 0.192, 1.024, 0, 0, 192, 288, 448, 576",    // a comma for a point
                                 "16200-15625, 49.50-65.00, 2.000, 4.700, 8.000, 0.064, 0.192, 1.024, 0, 0, 192, 288, 448, 576",    // the wrong way round
                                 "15625-16200, 65.00-49.50, 2.000, 4.700, 8.000, 0.064, 0.192, 1.024, 0, 0, 192, 288, 448, 576",
                                 "1562-1620, 49.50-65.00, 2.000, 4.700, 8.000, 0.064, 0.192, 1.024, 0, 0, 192, 288, 448, 576",      // no CRT's line rate
                                 "15625-16200, 49.50-65.00, 0, 4.700, 8.000, 0.064, 0.192, 1.024, 0, 0, 192, 288, 448, 576",        // no front porch
                                 "15625-16200, 49.50-65.00, 2.000, 4.700, 80.000, 0.064, 0.192, 1.024, 0, 0, 192, 288, 448, 576",   // longer than a line
                                 "15625-16200, 49.50-65.00, 2.000, 4.700, 8.000, 0.064, 0.192, 0, 0, 0, 192, 288, 448, 576",        // no back porch
                                 "15625-16200, 49.50-65.00, 2.000, 4.700, 8.000, 0.064, 0.192, 1.024, 2, 0, 192, 288, 448, 576",    // no such polarity
                                 "15625-16200, 49.50-65.00, 2.000, 4.700, 8.000, 0.064, 0.192, 1.024, 0, 0, 288, 192, 448, 576",    // the wrong way round
                                 "15625-16200, 49.50-65.00, 2.000, 4.700, 8.000, 0.064, 0.192, 1.024, 0, 0, 192.5, 288, 448, 576" }) {
            why.clear();
            QVERIFY2(!parseMonitorRange(bad, &range, &why), bad);
            QVERIFY2(!why.empty(), bad);
        }
        // What was there stays, where a line isn't read.
        QCOMPARE(range.hfreqMax, before.hfreqMax);
    }

    // The tool's eight modes, worked out with Switchres for its arcade_15,
    // come out of modeFor() again from their widths, lines and refresh
    // rates, to the pixel, the line and the hertz of the pixel clock.
    void aGamesOwnModeIsSwitchress() {
        MonitorRange range;
        std::string why;
        QVERIFY2(parseMonitorRange(kArcade15, &range, &why), why.c_str());
        // Each built-in mode's game, in the tool's order: the menu's 60 Hz,
        // a Neo Geo game (6 MHz / 384 / 264), a CPS game (8 MHz / 512 / 262),
        // 57.55 Hz, R-Type (8 MHz / 512 / 284) and Pac-Man (6.144 MHz / 384 /
        // 264), as MAME's -listxml gives their refresh rates.
        const struct { int width, lines; double refresh; } games[] = {
            { 1920, 240, 60.0 }, { 1920, 224, 59.185606 }, { 1920, 224, 59.637405 }, { 1920, 240, 57.55 },
            { 1920, 256, 55.017606 }, { 1920, 224, 60.606061 }, { 320, 224, 59.185606 }, { 384, 224, 59.637405 },
        };
        const std::vector<ModeLine> &built = builtInModes();
        QCOMPARE(int(built.size()), int(std::size(games)));
        for (size_t i = 0; i < built.size(); i++) {
            const ModeLine &want = built[i];
            ModeLine got;
            QVERIFY2(modeFor(games[i].width, games[i].lines, games[i].refresh, range, &got, &why), why.c_str());
            // Named as the tool's are, by its size and rate ("1920x224_59.19").
            const QString name = QString::fromStdString(got.name);
            QCOMPARE(name.section('_', 0, 0), QString::fromStdString(want.name).section('_', 0, 0));
            QVERIFY2(qAbs(name.section('_', 1).toDouble() - want.refreshHz()) < 0.01, got.name.c_str());
            // In whole hertz, as the modeline's six decimals of a MHz are.
            QCOMPARE(qRound64(got.clockMHz * 1e6), qRound64(want.clockMHz * 1e6));
            QCOMPARE(got.hdisplay, want.hdisplay);
            QCOMPARE(got.hsyncStart, want.hsyncStart);
            QCOMPARE(got.hsyncEnd, want.hsyncEnd);
            QCOMPARE(got.htotal, want.htotal);
            QCOMPARE(got.vdisplay, want.vdisplay);
            QCOMPARE(got.vsyncStart, want.vsyncStart);
            QCOMPARE(got.vsyncEnd, want.vsyncEnd);
            QCOMPARE(got.vtotal, want.vtotal);
            QCOMPARE(got.hsyncPositive, want.hsyncPositive);
            QCOMPARE(got.vsyncPositive, want.vsyncPositive);
            QVERIFY(!got.interlace && !got.doublescan && !got.csync);
            // The game's own rate, to a thousandth of a hertz.
            QVERIFY2(qAbs(got.refreshHz() - games[i].refresh) < 0.001, got.name.c_str());
        }
    }

    // No mode where the monitor can't show the game as it is: its lines one
    // for one, at its own refresh rate, within the monitor's line rates.
    void noModeOutsideTheMonitorsRange() {
        MonitorRange range;
        std::string why;
        QVERIFY(parseMonitorRange(kArcade15, &range, &why));
        ModeLine mode;
        mode.name = "untouched";
        const auto refused = [&](int width, int lines, double refresh, const char *says) {
            why.clear();
            if (modeFor(width, lines, refresh, range, &mode, &why))
                return false;
            return QString::fromStdString(why).contains(QLatin1String(says)) && mode.name == "untouched";
        };
        // A 31 kHz game's 480 lines, a 25 kHz game's 384, and too few.
        QVERIFY2(refused(1920, 480, 60.0, "its 480 lines are outside the monitor's 192 to 288"), why.c_str());
        QVERIFY2(refused(1920, 384, 60.0, "its 384 lines are outside"), why.c_str());
        QVERIFY2(refused(1920, 191, 60.0, "its 191 lines are outside"), why.c_str());
        QVERIFY2(refused(1920, 289, 50.0, "its 289 lines are outside"), why.c_str());
        // An interlaced game's 30 frames, and a rate no monitor of this
        // range holds.
        QVERIFY2(refused(1920, 240, 30.0, "its 30.00 Hz is outside the monitor's 49.50 to 65.00 Hz"), why.c_str());
        QVERIFY2(refused(1920, 224, 70.0, "its 70.00 Hz is outside"), why.c_str());
        QVERIFY2(refused(1920, 224, 0.0, "is outside"), why.c_str());
        // Lines and a rate that each fit, and together need more than 16.2
        // kHz: 256 lines at 60 Hz, 288 at 55. Switchres would slow the
        // refresh down for them; here there is no mode.
        QVERIFY2(refused(1920, 256, 60.0, "its 256 lines at 60.00 Hz need a line rate above the monitor's 16.200 kHz"),
                 why.c_str());
        QVERIFY2(refused(1920, 288, 55.0, "need a line rate above"), why.c_str());
        // The edges that do fit: 288 lines at 50 Hz, 192 at 65.
        QVERIFY2(modeFor(1920, 288, 50.0, range, &mode, &why), why.c_str());
        QVERIFY2(modeFor(1920, 192, 65.0, range, &mode, &why), why.c_str());
        mode.name = "untouched";
        // No picture to speak of, and one no output has the width for.
        QVERIFY2(refused(32, 224, 60.0, "pixels across"), why.c_str());
        QVERIFY2(refused(8000, 224, 60.0, "pixels across"), why.c_str());
        // A monitor of one line rate alone (Switchres's pal: 15.625 kHz, 50
        // Hz) has a mode only for what lands on it: 288 lines at 50 Hz with
        // 312 or 313 lines to a frame come to 15.6 and 15.65 kHz, neither
        // its own.
        MonitorRange pal;
        QVERIFY2(parseMonitorRange("15625.00-15625.00, 50.00-50.00, 1.500, 4.700, 5.800, 0.064, 0.160, 1.056, 0, 0, 192, 288, 448, 576",
                                   &pal, &why), why.c_str());
        why.clear();
        QVERIFY(!modeFor(720, 288, 50.0, pal, &mode, &why));
        QVERIFY2(QString::fromStdString(why).contains(QStringLiteral("outside the monitor's 15.625 to 15.625 kHz")), why.c_str());
        // And none at all for a range that was never read.
        QVERIFY(!modeFor(1920, 224, 60.0, MonitorRange(), &mode, &why));
        QCOMPARE(QString::fromStdString(mode.name), QStringLiteral("untouched"));
    }

    // Every mode modeFor() makes for an arcade monitor, over all its lines
    // and rates and at the widths games have: inside the monitor's range,
    // the game's lines and rate, a picture with its sync in its blanking and
    // the porches the range asks for, and one a screen on any of the 15 kHz
    // presets may be given (safeFor()).
    void everyModeMadeIsTheMonitorsOwn() {
        MonitorRange range;
        std::string why;
        QVERIFY(parseMonitorRange(kArcade15, &range, &why));
        int made = 0, none = 0;
        for (int width : { 256, 320, 384, 720, 1920 }) {
            for (int lines = 192; lines <= 288; lines++) {
                for (double refresh = 49.5; refresh <= 65.0; refresh += 0.137) {
                    ModeLine mode;
                    if (!modeFor(width, lines, refresh, range, &mode, &why)) {
                        // Only for want of line rate: too many lines for the refresh.
                        QVERIFY2(QString::fromStdString(why).contains(QStringLiteral("need a line rate above")), why.c_str());
                        QVERIFY2(refresh * (lines + 20) > 16000.0, why.c_str());
                        none++;
                        continue;
                    }
                    made++;
                    const QByteArray name = QByteArray::fromStdString(mode.name);
                    QVERIFY2(mode.lineRateHz() >= 15624.9 && mode.lineRateHz() <= 16200.1, name.constData());
                    QVERIFY2(qAbs(mode.refreshHz() - refresh) < 0.001, name.constData());
                    QCOMPARE(mode.hdisplay, width);
                    QCOMPARE(mode.vdisplay, lines);
                    QVERIFY2(mode.hdisplay < mode.hsyncStart && mode.hsyncStart < mode.hsyncEnd
                             && mode.hsyncEnd < mode.htotal, name.constData());
                    QVERIFY2(mode.vdisplay < mode.vsyncStart && mode.vsyncStart < mode.vsyncEnd
                             && mode.vsyncEnd < mode.vtotal, name.constData());
                    // The porches and the sync in time, within a pixel and a
                    // tenth of what the range asks for.
                    const double pixel = 1e6 / (mode.clockMHz * 1e6);
                    const auto near = [&](int pixels, double wanted) {
                        return pixels * pixel > wanted * 0.89 && pixels * pixel < wanted * 1.1 + pixel;
                    };
                    QVERIFY2(near(mode.hsyncStart - mode.hdisplay, 2.0), name.constData());
                    QVERIFY2(near(mode.hsyncEnd - mode.hsyncStart, 4.7), name.constData());
                    QVERIFY2(near(mode.htotal - mode.hsyncEnd, 8.0), name.constData());
                    // The frame's sync three lines or so (0.192 ms), and at
                    // least 1.024 ms of back porch after it.
                    QVERIFY2(mode.vsyncEnd - mode.vsyncStart >= 3 && mode.vsyncEnd - mode.vsyncStart <= 4, name.constData());
                    QVERIFY2((mode.vtotal - mode.vsyncEnd) / mode.lineRateHz() >= 0.00095, name.constData());
                    // The JAMMA presets' own line rates: 720x240's, 320x240's
                    // and 1920x240's, 720x288's and 1920x288's.
                    for (double screen : { 15734.27, 15659.06, 15660.0, 15625.0, 15648.4 })
                        QVERIFY2(safeFor(mode, screen, &why), why.c_str());
                    QVERIFY2(!safeFor(mode, 24960.0, &why), name.constData());
                    QVERIFY2(!safeFor(mode, 31469.0, &why), name.constData());
                }
            }
        }
        QVERIFY(made > 30000);
        QVERIFY(none > 1000);
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
