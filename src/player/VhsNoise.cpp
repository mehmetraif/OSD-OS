#include "VhsNoise.h"
#include <QtMath>

namespace {
// About twenty frames a second: noise, not video.
constexpr int kFrameMs = 50;
// A streak's strength, from faint to full (alpha, 0-255).
constexpr int kAlpha[5] = { 26, 46, 77, 128, 217 };
}

// Its random numbers start from where it is: two of them on one screen don't
// draw the same noise.
VhsNoise::VhsNoise(QQuickItem *parent)
    : ArtPixelItem(kFrameMs, 0x9e3779b9u ^ quint32(reinterpret_cast<quintptr>(this) >> 4), parent) {
    fillLevels(m_levels, m_color);
    fillLevels(m_shades, m_shade);
}

void VhsNoise::setColor(const QColor &color) {
    if (color == m_color)
        return;
    m_color = color;
    fillLevels(m_levels, m_color);
    emit colorChanged();
}

void VhsNoise::setShade(const QColor &shade) {
    if (shade == m_shade)
        return;
    m_shade = shade;
    fillLevels(m_shades, m_shade);
    emit shadeChanged();
}

void VhsNoise::setStreaks(qreal streaks) {
    streaks = qBound(0.0, streaks, 4.0);
    if (qFuzzyCompare(streaks, m_streaks))
        return;
    m_streaks = streaks;
    emit streaksChanged();
}

void VhsNoise::setBands(bool bands) {
    if (bands == m_bands)
        return;
    m_bands = bands;
    emit bandsChanged();
}

void VhsNoise::fillLevels(QRgb *levels, const QColor &color) {
    for (int i = 0; i < 5; ++i) {
        const int alpha = color.alpha() * kAlpha[i] / 255;
        levels[i] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), alpha));
    }
}

void VhsNoise::streak(int x, int y, int length, int level, bool dark) {
    if (y < 0 || y >= m_frame.height())
        return;
    auto *line = reinterpret_cast<QRgb *>(m_frame.scanLine(y));
    const QRgb value = dark ? m_shades[level] : m_levels[level];
    const int end = qMin(m_frame.width(), x + length);
    for (int i = qMax(0, x); i < end; ++i)
        line[i] = value;
}

void VhsNoise::tear(int from, int to) {
    const int cols = m_frame.width();
    from = qMax(0, from);
    to = qMin(m_frame.height(), to);
    const bool shaded = m_shade.alpha() > 0;
    for (int y = from; y < to; ++y) {
        // The wash, not quite to the edges and never the same twice: its
        // length taken first, then where it starts, in that order whatever
        // the compiler.
        const int wash = cols - int(random() % 6);
        streak(int(random() % 6), y, wash, 1);
        // Long streaks, about one every third pixel, strong ones among them.
        const int count = cols / 3;
        for (int i = 0; i < count; ++i) {
            const quint32 r = random();
            const int length = 2 + int(r % 10) + int((r >> 8) % 3 == 0) * int((r >> 12) % 14);
            const quint32 l = (r >> 18) % 16;
            const int level = l < 5 ? 1 : l < 10 ? 2 : l < 14 ? 3 : 4;
            // Some of them the ground's colour: a dropout, a hole in the picture.
            const bool dark = shaded && (r >> 24) % 5 == 0;
            streak(int((r >> 4) % cols), y, length, dark ? 4 : level, dark);
        }
        // Now and then a whole line goes bright.
        const quint32 r = random();
        if (r % 9 == 0)
            streak(int((r >> 4) % (cols / 2 + 1)), y, cols / 3 + int((r >> 12) % (cols / 2 + 1)), 3);
    }
}

void VhsNoise::tick() {
    const int cols = qCeil(width() / pixel());
    const int rows = qCeil(height() / pixel());
    if (cols <= 0 || rows <= 0)
        return;
    if (m_frame.width() != cols || m_frame.height() != rows)
        m_frame = QImage(cols, rows, QImage::Format_ARGB32_Premultiplied);
    m_frame.fill(Qt::transparent);

    // The picture's streaks: at full `streaks`, about one starting in every
    // fifteenth pixel of a row, each row busier or quieter than the last.
    if (m_streaks > 0) {
        for (int y = 0; y < rows; ++y) {
            const int count = int(m_streaks * cols * (2 + random() % 11) / 100);
            for (int i = 0; i < count; ++i) {
                const quint32 r = random();
                // Mostly short and faint, a tint of the ground; now and then
                // a longer or a brighter one.
                const int length = 1 + int(r % 4) + int((r >> 8) % 4 == 0) * int((r >> 10) % 5);
                const quint32 l = (r >> 20) % 20;
                const int level = l < 11 ? 0 : l < 17 ? 1 : l < 19 ? 2 : 3;
                streak(int((r >> 4) % cols), y, length, level);
            }
        }
    }

    if (m_bands) {
        // The tracking band across the top, its edge ragged and never still.
        tear(0, int(rows * (0.08 + (random() % 5) / 100.0)));
        // Every few seconds it rolls down the picture, fast, and is gone.
        if (m_roll < 0) {
            if (--m_rest <= 0)
                m_roll = 0;
        } else {
            tear(int(m_roll), int(m_roll) + qMax(2, rows / 12));
            m_roll += rows / 30.0;
            if (m_roll >= rows) {
                m_roll = -1;
                m_rest = 60 + int(random() % 100);
            }
        }
        // The head-switching strip along the bottom.
        tear(rows - qMax(2, rows / 40), rows);
    }

    m_newFrame = true;
    update();
}

void VhsNoise::updateRunning() {
    const bool run = canRun();
    if (run && !m_timer.isActive()) {
        // A frame at once, not a tick later.
        tick();
        m_timer.start();
    } else if (!run && m_timer.isActive()) {
        m_timer.stop();
    }
}
