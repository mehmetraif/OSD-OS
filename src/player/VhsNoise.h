#pragma once
#include <QColor>
#include <QImage>
#include <QQuickItem>
#include <QTimer>

// A VHS tape's noise, drawn afresh at every frame, the way a deck shows a tape
// it can't play yet: short horizontal streaks in `color` at random strengths,
// one art pixel tall, on a clear ground. `streaks` says how many cover the
// picture (0 none, 1 a worn tape's worth). `bands` adds the two strips a VHS
// picture breaks up in: the tracking band, a heavy strip across the top that
// now and then rolls down the picture, and the head-switching strip along the
// bottom; with a `shade` (the ground's colour), streaks of it there cut into
// whatever lies under the noise, as dropouts eat a tape's picture. Drawn at
// the art pixel (`pixel` screen pixels each), so the streaks are the size of
// the menus' own pixels. It runs only while visible and `running`; stopped, it
// holds the frame it has.
// LoadingScreen (views/Components) lays it under and over its text.
//
//     VhsNoise { anchors.fill: parent; color: root.primaryColor; pixel: root.px }
class VhsNoise : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(QColor shade READ shade WRITE setShade NOTIFY shadeChanged)
    Q_PROPERTY(int pixel READ pixel WRITE setPixel NOTIFY pixelChanged)
    Q_PROPERTY(qreal streaks READ streaks WRITE setStreaks NOTIFY streaksChanged)
    Q_PROPERTY(bool bands READ bands WRITE setBands NOTIFY bandsChanged)
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
public:
    explicit VhsNoise(QQuickItem *parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    QColor shade() const { return m_shade; }
    void setShade(const QColor &shade);
    int pixel() const { return m_pixel; }
    void setPixel(int pixel);
    qreal streaks() const { return m_streaks; }
    void setStreaks(qreal streaks);
    bool bands() const { return m_bands; }
    void setBands(bool bands);
    bool running() const { return m_running; }
    void setRunning(bool running);

signals:
    void colorChanged();
    void shadeChanged();
    void pixelChanged();
    void streaksChanged();
    void bandsChanged();
    void runningChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    friend class ArtPixelsTest;
    void nextFrame();
    void updateRunning();
    quint32 random();
    // A streak `length` long at (x, y), at strength `level` (0 faint … 4
    // full), of the colour or, `dark`, of the shade.
    void streak(int x, int y, int length, int level, bool dark = false);
    static void fillLevels(QRgb *levels, const QColor &color);
    // A strip of rows torn up the way a band is: a wash of the colour, then
    // streaks far denser and longer than the picture's.
    void tear(int from, int to);

    QColor m_color = Qt::white;
    QColor m_shade = Qt::transparent;
    int m_pixel = 2;
    qreal m_streaks = 1.0;
    bool m_bands = false;
    bool m_running = true;

    QTimer m_timer;
    QImage m_frame;
    bool m_newFrame = false;
    quint32 m_seed = 0x9e3779b9u;
    QRgb m_levels[5] = {};
    QRgb m_shades[5] = {};
    // The rolling band's top row while it rolls; frames to wait otherwise.
    double m_roll = -1.0;
    int m_rest = 60;
};
