#pragma once
#include <QPointer>
#include <QQuickItem>

class MpvController;

// The picture of an mpv session played inside OSD/OS's own window (the
// Transparent Background setting, see EmbeddedMpv), drawn as an item so that
// the menus can lie over it. It fills its area, mpv having fitted the picture
// to it (bars, Scaling), and tells the controller how many pixels that is:
// the size mpv draws at. A picture drawn on the GPU is shown as the texture
// it is, one drawn on the CPU uploaded into one.
//
// mpv takes the pixels it draws on as square, as they aren't on a tube (a
// JAMMA board's 720×240 fills a 4:3 screen): pixelAspect, a pixel's width
// over its height, has it draw on square ones in the area's own shape, the
// picture then stretched over the item's.
//
//     VideoSurface { anchors.fill: parent; controller: mpvController }
class VideoSurface : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(qreal pixelAspect READ pixelAspect WRITE setPixelAspect NOTIFY pixelAspectChanged)
public:
    explicit VideoSurface(QQuickItem *parent = nullptr);

    QObject *controller() const;
    void setController(QObject *controller);
    qreal pixelAspect() const { return m_pixelAspect; }
    void setPixelAspect(qreal aspect);

    // The size mpv draws at for an area of this many pixels, each pixelAspect
    // as wide as it is tall: the area's own, or with pixels that aren't
    // square, square ones in its shape, as many lines again on a screen of
    // 288 or fewer (240p, 288p) for its finer detail.
    static QSize drawingSize(const QSize &pixels, qreal pixelAspect);

signals:
    void controllerChanged();
    void pixelAspectChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void reportSize();

    QPointer<MpvController> m_controller;
    qreal m_pixelAspect = 1;
    // The picture shown: a QImage's cacheKey, or a GPU picture's serial.
    qint64 m_shownKey = 0;
    bool m_shownOnGpu = false;
};
