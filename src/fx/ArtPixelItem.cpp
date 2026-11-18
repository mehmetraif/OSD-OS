#include "ArtPixelItem.h"
#include <QQuickWindow>
#include <QSGSimpleTextureNode>

ArtPixelItem::ArtPixelItem(int frameMs, quint32 seed, QQuickItem *parent) : QQuickItem(parent), m_seed(seed) {
    setFlag(ItemHasContents, true);
    m_timer.setInterval(frameMs);
    connect(&m_timer, &QTimer::timeout, this, &ArtPixelItem::tick);
    // From 0, xorshift never leaves it.
    if (m_seed == 0)
        m_seed = 1;
}

void ArtPixelItem::setPixel(int pixel) {
    pixel = qMax(1, pixel);
    if (pixel == m_pixel)
        return;
    m_pixel = pixel;
    emit pixelChanged();
}

void ArtPixelItem::setRunning(bool running) {
    if (running == m_running)
        return;
    m_running = running;
    updateRunning();
    emit runningChanged();
}

bool ArtPixelItem::canRun() const {
    return m_running && isVisible() && window() && width() > 0 && height() > 0;
}

quint32 ArtPixelItem::random() {
    // xorshift: plenty for noise, and cheap enough for thousands a frame.
    // SelectorFx's numbers are OSD/OS Setup's live preview's too
    // (site/setup/js/preview/selector.mjs): a change here is made there.
    quint32 x = m_seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return m_seed = x;
}

void ArtPixelItem::itemChange(ItemChange change, const ItemChangeData &value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged || change == ItemSceneChange)
        updateRunning();
}

void ArtPixelItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        updateRunning();
}

QSGNode *ArtPixelItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    // The GUI thread waits while this runs, so the frame can be read.
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
    // Whole art pixels from the top left, the last row and column cut short
    // by the edge.
    node->setRect(boundingRect());
    node->setSourceRect(QRectF(0, 0, width() / m_pixel, height() / m_pixel));
    return node;
}
