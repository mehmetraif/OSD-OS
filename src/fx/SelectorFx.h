#pragma once
#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QVector>

// Settings → Look → Effects → Selector Effect, or a theme's: something always going on round
// the selected line, drawn in art pixels over the menus. `target` is the
// selection's box (Main.qml's root.selector, whichever is in front), followed
// wherever it goes; the effect plays out from it, across the whole item, so
// nothing of it is cut off where a list's box ends:
//   Sparkles   sparks streaking off the box's four corners, left and right,
//              some of them twinkling crosses, in the scheme's colour;
//   Welding    a welder's sparks bursting from the corners, scattering outward
//              and falling, white hot, then yellow, orange and red;
//   Lightning  jagged bolts crackling out of the corners, now and then along
//              the box's edge;
//   Rainbow    a pixel rainbow running down from under the box and fading
//              away, every column its own length;
//   Snow       flakes falling from the box's top edge, swaying as they fall
//              and melting away a line or two below it. They fall where they
//              are: the box moving down leaves them above it, moving up
//              below, and new ones fall from where it has gone.
// Anything else is none. It runs only while visible and `running`, and once
// the last spark is out with no box to play round, it rests.
//
//     SelectorFx { anchors.fill: parent; target: root.selector; effect: "Sparkles" }
class SelectorFx : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(QString effect READ effect WRITE setEffect NOTIFY effectChanged)
    Q_PROPERTY(QColor ink READ ink WRITE setInk NOTIFY inkChanged)
    // The scheme's other colour: snow passing over the box takes it, as the
    // box's label does.
    Q_PROPERTY(QColor paper READ paper WRITE setPaper NOTIFY paperChanged)
    Q_PROPERTY(int pixel READ pixel WRITE setPixel NOTIFY pixelChanged)
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
public:
    explicit SelectorFx(QQuickItem *parent = nullptr);

    QQuickItem *target() const { return m_target; }
    void setTarget(QQuickItem *target);
    QString effect() const { return m_effect; }
    void setEffect(const QString &effect);
    QColor ink() const { return m_ink; }
    void setInk(const QColor &ink);
    QColor paper() const { return m_paper; }
    void setPaper(const QColor &paper);
    int pixel() const { return m_pixel; }
    void setPixel(int pixel);
    bool running() const { return m_running; }
    void setRunning(bool running);

signals:
    void targetChanged();
    void effectChanged();
    void inkChanged();
    void paperChanged();
    void pixelChanged();
    void runningChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    enum class Kind { None, Sparkles, Welding, Lightning, Rainbow, Snow };
    struct Spark {
        float x, y, vx, vy;     // art pixels, art pixels a second
        float age, life;        // seconds
        float lastX, lastY;     // where it was a frame ago, for a streak
        bool cross;             // drawn as a twinkling cross, not a dot
        float sway = 0;         // a flake's: how far it sways either side,
        float phase = 0;        // and where in its sway it starts
    };
    struct Bolt {
        QVector<QPointF> points;
        int frames;             // left to show
        int lit;                // frames it has shown
    };

    void tick();
    void updateRunning();
    quint32 random();
    float uniform(float from, float to);
    // The target's box in art pixels, in this item's coordinates; empty when
    // there is none to play round.
    QRectF box() const;
    void emitSparkles(const QRectF &b);
    void emitWelding(const QRectF &b);
    void emitLightning(const QRectF &b);
    void emitSnow(const QRectF &b);
    void drawRainbow(const QRectF &b);
    QVector<QPointF> bolt(QPointF from, QPointF to, float jag);
    // Drawing, in premultiplied colour, over what is there.
    void plot(int x, int y, QRgb color, float alpha);
    void line(QPointF a, QPointF b, QRgb color, float alpha);

    QPointer<QQuickItem> m_target;
    QString m_effect;
    Kind m_kind = Kind::None;
    QColor m_ink = Qt::white;
    QColor m_paper = Qt::black;
    int m_pixel = 2;
    bool m_running = true;

    QTimer m_timer;
    QImage m_frame;
    bool m_newFrame = false;
    bool m_blank = true;
    quint32 m_seed = 0x2545F491u;
    float m_time = 0;
    float m_nextBurst = 0;
    // Flakes due but not yet fallen, a fraction of one.
    float m_snowDue = 0;
    QVector<Spark> m_sparks;
    QVector<Bolt> m_bolts;
};
