#include "SelectorFx.h"
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QtMath>

namespace {
// About thirty frames a second: smooth enough for sparks, light on the Pi.
constexpr int kFrameMs = 33;
constexpr float kFrame = kFrameMs / 1000.0f;
// Its clock starts over every hour, as root.fxTime does: a float counting
// up for days stops counting (0.033 is lost on 2^20), and the bursts with it.
constexpr float kClockWraps = 3600;

// The rainbow's bands, top to bottom.
const QRgb kRainbow[6] = { qRgb(255, 48, 48), qRgb(255, 144, 0), qRgb(255, 232, 0),
                           qRgb(48, 224, 72), qRgb(48, 128, 255), qRgb(176, 72, 255) };

// The outward diagonal from each corner of a box: top left, top right,
// bottom left, bottom right.
const QPointF kOut[4] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };

QRgb mixed(QRgb a, QRgb b, float t) {
    return qRgb(int(qRed(a) + (qRed(b) - qRed(a)) * t), int(qGreen(a) + (qGreen(b) - qGreen(a)) * t),
                int(qBlue(a) + (qBlue(b) - qBlue(a)) * t));
}

// A smooth number from 0 to 1 along x: values at the whole numbers, eased
// between them.
float hash1(int i) {
    quint32 h = quint32(i) * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x85EBCA77u;
    h ^= h >> 13;
    return (h & 0xFFFFFF) / float(0x1000000);
}
float noise1(float x) {
    const int i = int(qFloor(x));
    const float f = x - i;
    const float s = f * f * (3 - 2 * f);
    return hash1(i) + (hash1(i + 1) - hash1(i)) * s;
}
float hash3(int x, int y, int z) {
    // Unsigned: the products overflow (a column past 30, a frame past 50), and
    // wrap only as unsigned; the same bits as the preview's Math.imul.
    return hash1(int(quint32(x) * 73856093u ^ quint32(y) * 19349663u ^ quint32(z) * 83492791u));
}
} // namespace

SelectorFx::SelectorFx(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    m_timer.setInterval(kFrameMs);
    connect(&m_timer, &QTimer::timeout, this, &SelectorFx::tick);
}

void SelectorFx::setTarget(QQuickItem *target) {
    if (target == m_target)
        return;
    m_target = target;
    updateRunning();
    emit targetChanged();
}

void SelectorFx::setEffect(const QString &effect) {
    if (effect == m_effect)
        return;
    m_effect = effect;
    const QString e = effect.toLower();
    m_kind = e == QLatin1String("sparkles") ? Kind::Sparkles
           : e == QLatin1String("welding") ? Kind::Welding
           : e == QLatin1String("lightning") ? Kind::Lightning
           : e == QLatin1String("rainbow") ? Kind::Rainbow
           : e == QLatin1String("snow") ? Kind::Snow
                                            : Kind::None;
    // What the last one left flies on only if this is the same kind of thing.
    m_sparks.clear();
    m_bolts.clear();
    updateRunning();
    emit effectChanged();
}

void SelectorFx::setInk(const QColor &ink) {
    if (ink == m_ink)
        return;
    m_ink = ink;
    emit inkChanged();
}

void SelectorFx::setPaper(const QColor &paper) {
    if (paper == m_paper)
        return;
    m_paper = paper;
    emit paperChanged();
}

void SelectorFx::setPixel(int pixel) {
    pixel = qMax(1, pixel);
    if (pixel == m_pixel)
        return;
    m_pixel = pixel;
    emit pixelChanged();
}

void SelectorFx::setRunning(bool running) {
    if (running == m_running)
        return;
    m_running = running;
    updateRunning();
    emit runningChanged();
}

quint32 SelectorFx::random() {
    quint32 x = m_seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return m_seed = x;
}

float SelectorFx::uniform(float from, float to) {
    return from + (to - from) * ((random() & 0xFFFFFF) / float(0x1000000));
}

QRectF SelectorFx::box() const {
    if (!m_target || !m_target->isVisible() || m_target->window() != window()
            || m_target->width() <= 0 || m_target->height() <= 0)
        return {};
    const QRectF r = m_target->mapRectToItem(this, m_target->boundingRect());
    return QRectF(r.x() / m_pixel, r.y() / m_pixel, r.width() / m_pixel, r.height() / m_pixel);
}

void SelectorFx::plot(int x, int y, QRgb color, float alpha) {
    if (x < 0 || y < 0 || x >= m_frame.width() || y >= m_frame.height() || alpha <= 0)
        return;
    alpha = qMin(alpha, 1.0f);
    QRgb &p = reinterpret_cast<QRgb *>(m_frame.scanLine(y))[x];
    // Over what is there, premultiplied.
    const float keep = 1 - alpha;
    p = qRgba(int(qRed(color) * alpha + qRed(p) * keep), int(qGreen(color) * alpha + qGreen(p) * keep),
              int(qBlue(color) * alpha + qBlue(p) * keep), int(255 * alpha + qAlpha(p) * keep));
}

void SelectorFx::line(QPointF a, QPointF b, QRgb color, float alpha) {
    const QPointF d = b - a;
    const int steps = qMax(1, int(qCeil(qMax(qAbs(d.x()), qAbs(d.y())))));
    for (int i = 0; i <= steps; ++i) {
        const QPointF p = a + d * (float(i) / steps);
        plot(int(qFloor(p.x())), int(qFloor(p.y())), color, alpha);
    }
}

QVector<QPointF> SelectorFx::bolt(QPointF from, QPointF to, float jag) {
    // Halved again and again, each middle pushed aside by less each time.
    QVector<QPointF> points = { from, to };
    for (int pass = 0; pass < 4; ++pass) {
        QVector<QPointF> next;
        for (int i = 0; i + 1 < points.size(); ++i) {
            const QPointF a = points[i], b = points[i + 1];
            const QPointF d = b - a;
            const QPointF normal(-d.y(), d.x());
            const qreal len = qMax<qreal>(1, qSqrt(normal.x() * normal.x() + normal.y() * normal.y()));
            next << a << (a + b) / 2 + normal / len * uniform(-jag, jag);
        }
        next << points.last();
        points = next;
        jag *= 0.55f;
    }
    return points;
}

void SelectorFx::emitSparkles(const QRectF &b) {
    const QPointF corners[4] = { b.topLeft(), b.topRight(), b.bottomLeft(), b.bottomRight() };
    for (int c = 0; c < 4; ++c) {
        if (uniform(0, 1) > 0.5f)
            continue;
        const float side = kOut[c].x(), up = kOut[c].y();
        Spark s;
        s.x = s.lastX = corners[c].x() + uniform(-0.5f, 0.5f);
        s.y = s.lastY = corners[c].y() + uniform(-0.5f, 0.5f);
        s.vx = side * uniform(25, 75);
        s.vy = up * uniform(2, 22) + uniform(-8, 8);
        s.age = 0;
        s.life = uniform(0.45f, 1.1f);
        s.cross = uniform(0, 1) < 0.3f;
        m_sparks << s;
    }
}

void SelectorFx::emitWelding(const QRectF &b) {
    if (m_time < m_nextBurst)
        return;
    m_nextBurst = m_time + uniform(0.05f, 0.2f);
    const QPointF corners[4] = { b.topLeft(), b.topRight(), b.bottomLeft(), b.bottomRight() };
    const int c = int(random() % 4);
    const qreal out = qAtan2(kOut[c].y(), kOut[c].x());
    const int count = 6 + int(random() % 10);
    for (int i = 0; i < count; ++i) {
        const qreal angle = out + uniform(-1.0f, 1.0f);
        const float speed = uniform(45, 165);
        Spark s;
        s.x = s.lastX = corners[c].x();
        s.y = s.lastY = corners[c].y();
        s.vx = float(qCos(angle)) * speed;
        s.vy = float(qSin(angle)) * speed;
        s.age = 0;
        s.life = uniform(0.3f, 0.85f);
        s.cross = false;
        m_sparks << s;
    }
    // The weld itself, flaring white for a moment.
    Spark flash = { float(corners[c].x()), float(corners[c].y()), 0, 0, 0, 0.07f,
                    float(corners[c].x()), float(corners[c].y()), true };
    m_sparks << flash;
}

void SelectorFx::emitLightning(const QRectF &b) {
    if (m_time < m_nextBurst)
        return;
    m_nextBurst = m_time + uniform(0.08f, 0.35f);
    const QPointF corners[4] = { b.topLeft(), b.topRight(), b.bottomLeft(), b.bottomRight() };
    Bolt main;
    main.lit = 0;
    main.frames = 2 + int(random() % 3);
    if (uniform(0, 1) < 0.22f) {
        // Along the box's top or bottom edge, corner to corner.
        const bool top = random() % 2;
        main.points = bolt(top ? b.topLeft() : b.bottomLeft(), top ? b.topRight() : b.bottomRight(),
                           qMin<float>(3, b.height() / 2));
        m_bolts << main;
        return;
    }
    const int c = int(random() % 4);
    const qreal angle = qAtan2(kOut[c].y(), kOut[c].x()) + uniform(-0.9f, 0.9f);
    const float length = uniform(16, 46);
    const QPointF end = corners[c] + QPointF(qCos(angle), qSin(angle)) * length;
    main.points = bolt(corners[c], end, length * 0.22f);
    m_bolts << main;
    // A branch off it, now and then.
    if (uniform(0, 1) < 0.6f) {
        const QPointF from = main.points[main.points.size() / 3 + int(random() % (main.points.size() / 3))];
        const qreal fork = angle + (random() % 2 ? 0.7 : -0.7);
        Bolt branch;
        branch.lit = 0;
        branch.frames = main.frames - 1;
        branch.points = bolt(from, from + QPointF(qCos(fork), qSin(fork)) * length * 0.45, length * 0.1f);
        m_bolts << branch;
    }
}

void SelectorFx::emitSnow(const QRectF &b) {
    // A flake a second for every seven art pixels of the box's width: a
    // steady fall off a menu's line, never a blizzard.
    m_snowDue += float(b.width()) / 7.0f * kFrame;
    while (m_snowDue >= 1) {
        m_snowDue -= 1;
        if (m_sparks.size() >= 400)
            continue;
        Spark s;
        // About one in four nearer: a small cross, falling faster.
        s.cross = uniform(0, 1) < 0.25f;
        s.x = s.lastX = uniform(float(b.left()), float(b.right()));
        s.y = s.lastY = float(b.top()) + uniform(-1.0f, 0.5f);
        s.vx = uniform(-2.5f, 2.5f);
        s.vy = s.cross ? uniform(14, 22) : uniform(7, 15);
        s.age = 0;
        s.life = uniform(1.6f, 2.8f);
        s.sway = uniform(0.4f, 1.4f);
        s.phase = uniform(0, 6.2832f);
        m_sparks << s;
    }
}

void SelectorFx::drawRainbow(const QRectF &b) {
    const int top = int(qCeil(b.bottom()));
    const int left = int(qFloor(b.left()));
    const int right = int(qCeil(b.right()));
    const int frame = int(m_time / kFrame);
    for (int x = left; x < right; ++x) {
        // Each column its own length, swelling and ebbing as it runs: about
        // a line of the menu at most, under the line below the selector.
        const int length = int(6 + 8 * noise1(x * 0.23f + m_time * 0.9f) + 6 * noise1(x * 0.07f - m_time * 0.45f));
        for (int k = 0; k < length; ++k) {
            const float along = float(k) / length;
            // Gone in specks toward the end.
            if (hash3(x, top + k, frame / 2) < along * along * 0.9f)
                continue;
            const int band = int(qFloor((k - m_time * 14) / 2)) % 6;
            // Never solid: the line it runs under stays legible.
            plot(x, top + k, kRainbow[(band + 6) % 6], 0.7f * (1.0f - qPow(along, 1.6f)));
        }
    }
}

void SelectorFx::tick() {
    m_time += kFrame;
    if (m_time >= kClockWraps) {
        m_time -= kClockWraps;
        m_nextBurst -= kClockWraps;
    }
    const int cols = qCeil(width() / m_pixel);
    const int rows = qCeil(height() / m_pixel);
    if (cols <= 0 || rows <= 0)
        return;
    if (m_frame.width() != cols || m_frame.height() != rows) {
        m_frame = QImage(cols, rows, QImage::Format_ARGB32_Premultiplied);
        m_blank = false;
    }
    const QRectF b = box();
    if (!b.isEmpty()) {
        switch (m_kind) {
        case Kind::Sparkles: emitSparkles(b); break;
        case Kind::Welding: emitWelding(b); break;
        case Kind::Lightning: emitLightning(b); break;
        case Kind::Snow: emitSnow(b); break;
        default: break;
        }
    }
    const bool drawing = (!b.isEmpty() && m_kind == Kind::Rainbow) || !m_sparks.isEmpty() || !m_bolts.isEmpty();
    if (!drawing && m_blank) {
        // Nothing then and nothing now: no frame to draw, and none to rest on
        // without a box either.
        if (!m_target)
            m_timer.stop();
        return;
    }
    m_frame.fill(Qt::transparent);
    if (!b.isEmpty() && m_kind == Kind::Rainbow)
        drawRainbow(b);

    const QRgb light = mixed(m_ink.rgb(), qRgb(255, 255, 255), 0.45f);
    // Snow in the scheme's colour, lighter, as the background's; over the
    // box, the box's own other colour.
    const QRgb flake = mixed(m_ink.rgb(), qRgb(255, 255, 255), 0.6f);
    const auto snowPlot = [&](int x, int y, float alpha) {
        const bool over = !b.isEmpty() && b.contains(QPointF(x + 0.5, y + 0.5));
        plot(x, y, over ? m_paper.rgb() : flake, alpha);
    };
    const int frame = int(m_time / kFrame);
    for (int i = m_sparks.size() - 1; i >= 0; --i) {
        Spark &s = m_sparks[i];
        s.age += kFrame;
        if (s.age >= s.life) {
            m_sparks.remove(i);
            continue;
        }
        s.lastX = s.x;
        s.lastY = s.y;
        const float f = s.age / s.life;
        if (m_kind == Kind::Welding) {
            if (s.life < 0.1f) {
                // The weld's flare: a white blob.
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        plot(int(s.x) + dx, int(s.y) + dy, qRgb(255, 255, 240), dx || dy ? 0.6f : 1.0f);
                continue;
            }
            s.vy += 170 * kFrame;
            s.vx *= 0.985f;
            s.x += s.vx * kFrame;
            s.y += s.vy * kFrame;
            // White hot, then yellow, orange and red as it cools.
            const QRgb color = f < 0.15f ? qRgb(255, 255, 235) : f < 0.4f ? qRgb(255, 226, 92)
                             : f < 0.7f ? qRgb(255, 140, 32) : qRgb(222, 54, 24);
            line(QPointF(s.lastX, s.lastY), QPointF(s.x, s.y), color, 1.0f - qPow(f, 1.5f));
        } else if (m_kind == Kind::Snow) {
            // Straight down where it is, whatever the box does now, swaying.
            s.x += s.vx * kFrame;
            s.y += s.vy * kFrame;
            const int x = int(qFloor(s.x + s.sway * qSin(s.phase + s.age * 2.6f)));
            const int y = int(qFloor(s.y));
            // Melting away over the last third of its fall.
            const float alpha = f < 0.66f ? 1.0f : 1.0f - (f - 0.66f) / 0.34f;
            if (s.cross) {
                snowPlot(x, y, alpha);
                snowPlot(x - 1, y, alpha * 0.6f);
                snowPlot(x + 1, y, alpha * 0.6f);
                snowPlot(x, y - 1, alpha * 0.6f);
                snowPlot(x, y + 1, alpha * 0.6f);
            } else {
                snowPlot(x, y, alpha * 0.85f);
            }
        } else {
            s.vx *= 0.955f;
            s.vy *= 0.955f;
            s.x += s.vx * kFrame;
            s.y += s.vy * kFrame;
            const float alpha = 1.0f - f * f;
            const int x = int(qFloor(s.x)), y = int(qFloor(s.y));
            // A streak behind it, as long as it is fast, then its head.
            line(QPointF(s.x - s.vx * 0.08f, s.y - s.vy * 0.08f), QPointF(s.x, s.y), light, alpha * 0.4f);
            plot(x, y, qRgb(255, 255, 255), alpha);
            // A cross that twinkles: its arms there half the time.
            if (s.cross && (frame + i) % 6 < 3) {
                for (int d = 1; d <= 2; ++d) {
                    const float arm = alpha * (d == 1 ? 0.8f : 0.45f);
                    plot(x - d, y, light, arm);
                    plot(x + d, y, light, arm);
                    plot(x, y - d, light, arm);
                    plot(x, y + d, light, arm);
                }
            }
        }
    }

    const QRgb core = mixed(m_ink.rgb(), qRgb(255, 255, 255), 0.75f);
    for (int i = m_bolts.size() - 1; i >= 0; --i) {
        Bolt &bolt = m_bolts[i];
        if (bolt.frames <= 0) {
            m_bolts.remove(i);
            continue;
        }
        // Brightest as it strikes, fainter for the frames it lingers.
        const float alpha = bolt.lit == 0 ? 1.0f : 0.65f;
        for (int k = 0; k + 1 < bolt.points.size(); ++k) {
            // A faint glow either side, then the bolt.
            line(bolt.points[k] + QPointF(0, 1), bolt.points[k + 1] + QPointF(0, 1), m_ink.rgb(), alpha * 0.3f);
            line(bolt.points[k] - QPointF(0, 1), bolt.points[k + 1] - QPointF(0, 1), m_ink.rgb(), alpha * 0.3f);
            line(bolt.points[k], bolt.points[k + 1], core, alpha);
        }
        ++bolt.lit;
        --bolt.frames;
    }

    m_blank = !drawing;
    m_newFrame = true;
    update();
}

void SelectorFx::updateRunning() {
    const bool run = m_running && m_kind != Kind::None && isVisible() && window()
                     && width() > 0 && height() > 0 && (m_target || !m_sparks.isEmpty() || !m_bolts.isEmpty());
    if (run && !m_timer.isActive()) {
        m_timer.start();
    } else if (!run && m_timer.isActive()) {
        m_timer.stop();
        m_sparks.clear();
        m_bolts.clear();
        if (!m_blank && !m_frame.isNull()) {
            m_frame.fill(Qt::transparent);
            m_blank = true;
            m_newFrame = true;
            update();
        }
    }
}

void SelectorFx::itemChange(ItemChange change, const ItemChangeData &value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged || change == ItemSceneChange)
        updateRunning();
}

void SelectorFx::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        updateRunning();
}

QSGNode *SelectorFx::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    auto *node = static_cast<QSGSimpleTextureNode *>(old);
    if (m_frame.isNull() || width() <= 0 || height() <= 0) {
        delete node;
        return nullptr;
    }
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        // Art pixels stay square blocks, as the menus' do.
        node->setFiltering(QSGTexture::Nearest);
    }
    if (m_newFrame || !node->texture()) {
        node->setTexture(window()->createTextureFromImage(m_frame));
        m_newFrame = false;
    }
    node->setRect(boundingRect());
    node->setSourceRect(QRectF(0, 0, width() / m_pixel, height() / m_pixel));
    return node;
}
