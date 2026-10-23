#include "VideoSurface.h"
#include "MpvController.h"
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QtQuick/qsgtexture_platform.h>

VideoSurface::VideoSurface(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
}

QObject *VideoSurface::controller() const {
    return m_controller;
}

void VideoSurface::setController(QObject *controller) {
    auto *mpv = qobject_cast<MpvController *>(controller);
    if (mpv == m_controller)
        return;
    if (m_controller)
        disconnect(m_controller, nullptr, this, nullptr);
    m_controller = mpv;
    if (m_controller)
        connect(m_controller, &MpvController::videoFrameReady, this, &VideoSurface::onFrame);
    reportSize();
    reportShown();
    update();
    emit controllerChanged();
}

void VideoSurface::onFrame() {
    if (isVisible())
        update();
}

void VideoSurface::reportShown() {
    if (m_controller)
        m_controller->setVideoShown(isVisible());
}

void VideoSurface::setPixelAspect(qreal aspect) {
    if (aspect <= 0 || qFuzzyCompare(aspect, m_pixelAspect))
        return;
    m_pixelAspect = aspect;
    reportSize();
    emit pixelAspectChanged();
}

QSize VideoSurface::drawingSize(const QSize &pixels, qreal pixelAspect) {
    if (pixels.isEmpty() || pixelAspect <= 0 || qFuzzyCompare(pixelAspect, 1.0))
        return pixels;
    const int lines = pixels.height() * (pixels.height() <= 288 ? 2 : 1);
    return QSize(qRound(lines * pixels.width() * pixelAspect / pixels.height()), lines);
}

QSGNode *VideoSurface::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    // The GUI thread waits while this runs, so the controller can be asked.
    auto *node = static_cast<QSGSimpleTextureNode *>(old);
    const bool onGpu = m_controller && m_controller->videoOnGpu();
    // A GPU picture is taken here, as it is shown: its texture stays as it is
    // while the scene graph may draw it (EmbeddedMpv::gpuFrame()).
    const EmbeddedMpv::GpuFrame gpu = onGpu ? m_controller->videoGpuFrame() : EmbeddedMpv::GpuFrame();
    const QImage frame = !onGpu && m_controller ? m_controller->videoFrame() : QImage();
    if ((onGpu ? !gpu.texture : frame.isNull()) || width() <= 0 || height() <= 0) {
        delete node;
        m_shownKey = 0;
        return nullptr;
    }
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }
    const qint64 key = onGpu ? qint64(gpu.serial) : frame.cacheKey();
    if (key != m_shownKey || onGpu != m_shownOnGpu || !node->texture()) {
        // The GPU's texture as it is, in the scene graph's context, which
        // shares its objects; the wrapper doesn't own it.
        QSGTexture *texture = onGpu
            ? QNativeInterface::QSGOpenGLTexture::fromNative(gpu.texture, window(), gpu.size,
                                                             QQuickWindow::TextureIsOpaque)
            : window()->createTextureFromImage(frame, QQuickWindow::TextureIsOpaque);
        if (!texture) {
            delete node;
            m_shownKey = 0;
            return nullptr;
        }
        node->setTexture(texture);
        m_shownKey = key;
        m_shownOnGpu = onGpu;
    }
    node->setRect(boundingRect());
    return node;
}

void VideoSurface::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        reportSize();
}

void VideoSurface::itemChange(ItemChange change, const ItemChangeData &value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged)
        reportSize();
    if (change == ItemVisibleHasChanged) {
        reportShown();
        // Shown again: the latest picture, drawn whole for it.
        if (value.boolValue)
            update();
    }
}

void VideoSurface::reportSize() {
    if (!m_controller || !window() || width() <= 0 || height() <= 0)
        return;
    const qreal dpr = window()->effectiveDevicePixelRatio();
    m_controller->setVideoTargetSize(drawingSize(QSize(qRound(width() * dpr), qRound(height() * dpr)),
                                                 m_pixelAspect));
}
