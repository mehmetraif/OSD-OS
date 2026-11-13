#include "ModeLine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace modetest {

namespace {

// Text cut at spaces, a part in quotes kept whole (without them).
std::vector<std::string> words(const std::string &text) {
    std::vector<std::string> out;
    std::string word;
    bool quoted = false;
    bool any = false;
    for (char c : text) {
        if (c == '"') {
            quoted = !quoted;
            any = true;
        } else if (!quoted && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
            if (any)
                out.push_back(word);
            word.clear();
            any = false;
        } else {
            word += c;
            any = true;
        }
    }
    if (any)
        out.push_back(word);
    return out;
}

bool number(const std::string &text, double *value) {
    if (text.empty())
        return false;
    char *end = nullptr;
    *value = std::strtod(text.c_str(), &end);
    return end && *end == '\0' && std::isfinite(*value);
}

bool whole(const std::string &text, int *value) {
    double d = 0;
    if (!number(text, &d) || d != std::floor(d) || d < 0 || d > 100000)
        return false;
    *value = int(d);
    return true;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

bool fail(std::string *why, const std::string &text) {
    if (why)
        *why = text;
    return false;
}

ModeLine made(const char *what, const char *line) {
    ModeLine mode;
    std::string why;
    if (!parseModeLine(line, &mode, &why))
        std::abort(); // a built-in mode that isn't one: caught by the test
    mode.what = what;
    return mode;
}

// --- the picture ---

constexpr uint32_t kBlack = 0x00000000;
constexpr uint32_t kWhite = 0x00FFFFFF;
constexpr uint32_t kGreen = 0x0000C000;
constexpr uint32_t kGrey  = 0x00808080;

struct Canvas {
    uint32_t *pixels;
    int stride, width, height;

    void rect(int x, int y, int w, int h, uint32_t colour) const {
        const int x0 = std::max(0, x), y0 = std::max(0, y);
        const int x1 = std::min(width, x + w), y1 = std::min(height, y + h);
        for (int row = y0; row < y1; row++) {
            uint32_t *p = pixels + size_t(row) * size_t(stride);
            for (int col = x0; col < x1; col++)
                p[col] = colour;
        }
    }
    void frame(int x, int y, int w, int h, int tx, int ty, uint32_t colour) const {
        rect(x, y, w, ty, colour);
        rect(x, y + h - ty, w, ty, colour);
        rect(x, y, tx, h, colour);
        rect(x + w - tx, y, tx, h, colour);
    }
};

// A digit of seven bars, 10 units across and 18 lines high, the bars two
// thick: which of a (top), b, c (right), d (bottom), e, f (left), g (middle).
constexpr uint8_t kSegments[10] = {
    0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110,
    0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111,
};

int drawText(const Canvas &c, int x, int y, int u, const std::string &text) {
    for (char ch : text) {
        if (ch == '.') {
            c.rect(x, y + 16, 2 * u, 2, kWhite);
            x += 5 * u;
            continue;
        }
        if (ch < '0' || ch > '9') {
            x += 8 * u;
            continue;
        }
        const uint8_t s = kSegments[ch - '0'];
        if (s & 0b0000001) c.rect(x, y, 10 * u, 2, kWhite);                 // a
        if (s & 0b0000010) c.rect(x + 8 * u, y, 2 * u, 10, kWhite);         // b
        if (s & 0b0000100) c.rect(x + 8 * u, y + 8, 2 * u, 10, kWhite);     // c
        if (s & 0b0001000) c.rect(x, y + 16, 10 * u, 2, kWhite);            // d
        if (s & 0b0010000) c.rect(x, y + 8, 2 * u, 10, kWhite);             // e
        if (s & 0b0100000) c.rect(x, y, 2 * u, 10, kWhite);                 // f
        if (s & 0b1000000) c.rect(x, y + 8, 10 * u, 2, kWhite);             // g
        x += 14 * u;
    }
    return x;
}

} // namespace

double ModeLine::lineRateHz() const {
    return htotal > 0 ? clockMHz * 1e6 / htotal : 0;
}

double ModeLine::refreshHz() const {
    if (vtotal <= 0)
        return 0;
    double rate = lineRateHz() / vtotal;
    if (interlace)
        rate *= 2;
    if (doublescan)
        rate /= 2;
    return rate;
}

bool parseModeLine(const std::string &text, ModeLine *mode, std::string *why) {
    std::vector<std::string> parts = words(text);
    if (!parts.empty() && lower(parts.front()) == "modeline")
        parts.erase(parts.begin());
    ModeLine out;
    // A name: whatever comes first that isn't the clock.
    double clock = 0;
    if (!parts.empty() && !number(parts.front(), &clock)) {
        out.name = parts.front();
        parts.erase(parts.begin());
    }
    if (parts.size() < 9)
        return fail(why, "a modeline has a pixel clock in MHz and eight numbers after it");
    if (!number(parts[0], &out.clockMHz) || out.clockMHz <= 0)
        return fail(why, "the pixel clock isn't a number of MHz: " + parts[0]);
    int *fields[8] = { &out.hdisplay, &out.hsyncStart, &out.hsyncEnd, &out.htotal,
                       &out.vdisplay, &out.vsyncStart, &out.vsyncEnd, &out.vtotal };
    for (int i = 0; i < 8; i++) {
        if (!whole(parts[size_t(i) + 1], fields[i]))
            return fail(why, "not a whole number: " + parts[size_t(i) + 1]);
    }
    for (size_t i = 9; i < parts.size(); i++) {
        const std::string flag = lower(parts[i]);
        if (flag == "+hsync")           out.hsyncPositive = true;
        else if (flag == "-hsync")      out.hsyncPositive = false;
        else if (flag == "+vsync")      out.vsyncPositive = true;
        else if (flag == "-vsync")      out.vsyncPositive = false;
        else if (flag == "interlace")   out.interlace = true;
        else if (flag == "doublescan")  out.doublescan = true;
        else if (flag == "+csync" || flag == "composite") out.csync = true;
        else if (flag == "-csync")      out.csync = false;
        else
            return fail(why, "not a flag of a modeline: " + parts[i]);
    }
    if (out.hdisplay < 64 || out.vdisplay < 64)
        return fail(why, "a picture of under 64 pixels or lines");
    if (!(out.hdisplay <= out.hsyncStart && out.hsyncStart < out.hsyncEnd && out.hsyncEnd <= out.htotal
          && out.hdisplay < out.htotal))
        return fail(why, "the horizontal sync isn't in the blanking after the picture");
    if (!(out.vdisplay <= out.vsyncStart && out.vsyncStart < out.vsyncEnd && out.vsyncEnd <= out.vtotal
          && out.vdisplay < out.vtotal))
        return fail(why, "the vertical sync isn't in the blanking after the picture");
    if (out.name.empty()) {
        char name[64];
        std::snprintf(name, sizeof name, "%dx%d", out.hdisplay, out.vdisplay);
        out.name = name;
    }
    *mode = out;
    return true;
}

std::string describe(const ModeLine &mode) {
    char text[128];
    std::snprintf(text, sizeof text, "%dx%d%s  %.3f kHz  %.2f Hz  %.3f MHz", mode.hdisplay, mode.vdisplay,
                  mode.interlace ? "i" : "", mode.lineRateHz() / 1000.0, mode.refreshHz(), mode.clockMHz);
    return text;
}

const std::vector<ModeLine> &builtInModes() {
    static const std::vector<ModeLine> modes = {
        made("the menu's 240 lines at 60 Hz, for comparison",
             "\"1920x240_60\" 39.040380 1920 1998 2181 2493 240 242 245 261 -hsync -vsync"),
        made("224 lines at 59.19 Hz, a Neo Geo game's",
             "\"1920x224_59.19\" 39.132041 1920 1998 2182 2495 224 236 239 265 -hsync -vsync"),
        made("224 lines at 59.64 Hz, a CPS game's",
             "\"1920x224_59.64\" 38.953125 1920 1998 2181 2493 224 234 237 262 -hsync -vsync"),
        made("240 lines at 57.55 Hz, a Cave game's",
             "\"1920x240_57.55\" 39.024424 1920 1998 2181 2493 240 247 250 272 -hsync -vsync"),
        made("256 lines at 55.02 Hz, R-Type's",
             "\"1920x256_55.02\" 39.121643 1920 1998 2182 2495 256 262 265 285 -hsync -vsync"),
        made("224 lines at 60.61 Hz, Pac-Man's",
             "\"1920x224_60.61\" 38.981454 1920 1998 2181 2493 224 232 235 258 -hsync -vsync"),
        made("the Neo Geo mode at the game's own 320 across",
             "\"320x224_59.19\" 6.524620 320 333 364 416 224 236 239 265 -hsync -vsync"),
        made("the CPS mode at the game's own 384 across",
             "\"384x224_59.64\" 7.796875 384 400 437 499 224 234 237 262 -hsync -vsync"),
    };
    return modes;
}

bool safeFor(const ModeLine &mode, double currentLineRateHz, std::string *why) {
    char text[160];
    if (mode.interlace || mode.doublescan)
        return fail(why, "interlaced and doublescan modes aren't tried");
    if (currentLineRateHz <= 0)
        return fail(why, "the screen is on no mode to compare its line rate with");
    const double rate = mode.lineRateHz();
    if (rate < currentLineRateHz * 0.95 || rate > currentLineRateHz * 1.05) {
        std::snprintf(text, sizeof text, "its %.3f kHz is not within 5%% of the %.3f kHz the screen is on",
                      rate / 1000.0, currentLineRateHz / 1000.0);
        return fail(why, text);
    }
    const double refresh = mode.refreshHz();
    if (refresh < 47.0 || refresh > 66.0) {
        std::snprintf(text, sizeof text, "its %.2f Hz is outside 47 to 66 Hz", refresh);
        return fail(why, text);
    }
    if (mode.clockMHz < 1.0 || mode.clockMHz > 200.0) {
        std::snprintf(text, sizeof text, "its pixel clock of %.3f MHz is outside 1 to 200 MHz", mode.clockMHz);
        return fail(why, text);
    }
    return true;
}

void drawPattern(uint32_t *pixels, int stride, int width, int height, int index, const ModeLine &mode) {
    if (!pixels || width <= 0 || height <= 0 || stride < width)
        return;
    const Canvas c { pixels, stride, width, height };
    // One pixel of a picture 320 across, in this mode's pixels.
    const int u = std::max(1, width / 320);
    c.rect(0, 0, width, height, kBlack);

    // Eight bars across the middle third, 10% in from each side.
    static const uint32_t bars[8] = { 0x00FFFFFF, 0x00FFFF00, 0x0000FFFF, 0x0000FF00,
                                      0x00FF00FF, 0x00FF0000, 0x000000FF, 0x00404040 };
    const int barsLeft = width / 10, barsWidth = width - 2 * barsLeft;
    for (int i = 0; i < 8; i++)
        c.rect(barsLeft + barsWidth * i / 8, height / 3, barsWidth * (i + 1) / 8 - barsWidth * i / 8, height / 3,
               bars[i]);

    // A cross through the middle, over the bars.
    c.rect(width / 2 - u / 2, 0, u, height, kGrey);
    c.rect(0, height / 2, width, 1, kGrey);

    // The frame 5% in, and the mode's very edge.
    c.frame(width / 20, height / 20, width - 2 * (width / 20), height - 2 * (height / 20), u, 1, kGreen);
    c.frame(0, 0, width, height, u, 1, kWhite);

    // Which mode this is: that many squares, 10% in from the top left.
    for (int i = 0; i < index; i++)
        c.rect(width / 10 + i * 12 * u, height / 10, 8 * u, 8, kWhite);

    // Its lines and its refresh rate, 10% in from the bottom left.
    char text[32];
    std::snprintf(text, sizeof text, "%d %.2f", mode.vdisplay, mode.refreshHz());
    drawText(c, width / 10, height - height / 10 - 18, u, text);
}

} // namespace modetest
