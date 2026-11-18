#pragma once
#include <QImage>
#include <QQuickItem>
#include <QTimer>

// An item that draws in art pixels (`pixel` screen pixels each), so that what
// it draws is the size of the menus' own pixels: a picture of its own,
// m_frame, drawn afresh a frame at a time on a timer (tick()) and shown
// scaled up without smoothing. Each kind says when it runs (updateRunning()),
// never unless `running`, visible, in a window and with a size (canRun()).
// VhsNoise and SelectorFx draw on it.
class ArtPixelItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int pixel READ pixel WRITE setPixel NOTIFY pixelChanged)
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
public:
    int pixel() const { return m_pixel; }
    void setPixel(int pixel);
    bool running() const { return m_running; }
    void setRunning(bool running);

signals:
    void pixelChanged();
    void runningChanged();

protected:
    // A frame every `frameMs` milliseconds while it runs; random() starts
    // from `seed`.
    ArtPixelItem(int frameMs, quint32 seed, QQuickItem *parent);

    // The next frame, drawn into m_frame; then m_newFrame and update().
    virtual void tick() = 0;
    // The timer started or stopped for what the item is now. Called as it is
    // shown or hidden, put in a window or taken out, sized, or set running.
    virtual void updateRunning() = 0;
    bool canRun() const;
    quint32 random();

    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

    QTimer m_timer;
    QImage m_frame;
    bool m_newFrame = false;

private:
    friend class ArtPixelsTest;
    int m_pixel = 2;
    bool m_running = true;
    quint32 m_seed;
};
