#include "OsdIconProvider.h"
#include <QCache>
#include <QColor>
#include <QImageReader>
#include <QMutex>
#include <QUrl>
#include <cmath>

namespace {

// What each file gave at each size asked for, by "<path>\n<width>x<height>":
// its drawing, trimmed and not yet coloured, or a null image for a file that
// gave none. Costed in kilobytes and held to kKeptKb, the oldest going first;
// a request may come from the scene graph's image thread, hence the mutex.
constexpr int kKeptKb = 8 * 1024;
QMutex g_keptMutex;
QCache<QString, QImage> g_kept(kKeptKb);

QString keptKey(const QString &path, const QSize &size) {
    return path + QLatin1Char('\n') + QString::number(size.width()) + QLatin1Char('x')
           + QString::number(size.height());
}

bool kept(const QString &key, QImage *drawing) {
    QMutexLocker lock(&g_keptMutex);
    const QImage *found = g_kept.object(key);
    if (!found)
        return false;
    *drawing = *found;
    return true;
}

void keep(const QString &key, const QImage &drawing) {
    QMutexLocker lock(&g_keptMutex);
    // One too large to keep is deleted by the cache, and drawn again next time.
    g_kept.insert(key, new QImage(drawing), qMax(1, int(drawing.sizeInBytes() / 1024)));
}

// The source rendered at a given size, as straight (not premultiplied) ARGB:
// a vector drawn at that size, a picture of pixels (a skin's icon) scaled, its
// pixels kept square and sharp as it grows.
QImage render(const QString &path, const QSize &size) {
    QImageReader reader(path);
    if (!reader.format().startsWith("svg")) {
        const QImage image = reader.read();
        const bool grows = size.width() >= image.width() && size.height() >= image.height();
        return image.scaled(size, Qt::IgnoreAspectRatio, grows ? Qt::FastTransformation : Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_ARGB32);
    }
    reader.setScaledSize(size);
    return reader.read().convertToFormat(QImage::Format_ARGB32);
}

// The drawing a file gives at the size asked for, trimmed of the margin
// around it, its own colours still in it; null when the file gives none.
QImage drawingOf(const QString &path, const QSize &requestedSize) {
    const QSize natural = QImageReader(path).size();
    if (natural.isEmpty())
        return {};
    const int height = requestedSize.height() > 0 ? requestedSize.height() : natural.height();

    // Where the drawing is, found on a large rendering so the margin around it
    // is measured to within a fraction of a final pixel.
    const int probeHeight = qMax(height * 8, 64);
    const QImage probe = render(path, QSize(qMax(1, qRound(double(natural.width()) * probeHeight
                                                              / natural.height())), probeHeight));
    int left = probe.width(), right = -1, top = probe.height(), bottom = -1;
    for (int y = 0; y < probe.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(probe.constScanLine(y));
        for (int x = 0; x < probe.width(); ++x) {
            if (qAlpha(line[x]) == 0)
                continue;
            left = qMin(left, x);
            right = qMax(right, x + 1);
            top = qMin(top, y);
            bottom = qMax(bottom, y + 1);
        }
    }
    if (right < 0)
        return {};

    // Drawn again from the original at the scale that makes the drawing itself
    // the height asked for, then cut to it. A width asked for as well
    // stretches it to that width: a screen whose pixels are not square
    // (720×480 on a 4:3 tube) keeps the drawing's shape on the glass.
    const double scale = double(height) / (bottom - top);
    const double scaleX = requestedSize.width() > 0 ? double(requestedSize.width()) / (right - left) : scale;
    const QImage full = render(path, QSize(qMax(1, qRound(probe.width() * scaleX)),
                                           qMax(1, qRound(probe.height() * scale))));
    const int x0 = int(std::floor(left * scaleX));
    const int y0 = int(std::floor(top * scale));
    return full.copy(x0, y0,
                     qMax(1, int(std::ceil(right * scaleX)) - x0),
                     qMax(1, int(std::ceil(bottom * scale)) - y0));
}

} // namespace

QImage OsdIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    // <rrggbb>/<url>: the url keeps its own slashes.
    const int colorEnd = id.indexOf(QLatin1Char('/'));
    if (colorEnd < 0)
        return {};
    const QColor color(QLatin1Char('#') + id.left(colorEnd));
    if (!color.isValid())
        return {};
    const QUrl url(id.mid(colorEnd + 1));
    const QString path = url.isLocalFile() ? url.toLocalFile()
                       : url.scheme() == QLatin1String("qrc") ? QLatin1Char(':') + url.path()
                                                              : url.toString();

    // As kept from the last time this file was asked for at this size, or
    // read and drawn now, and kept: the file is looked for once.
    const QString key = keptKey(path, requestedSize);
    QImage out;
    if (!kept(key, &out)) {
        out = drawingOf(path, requestedSize);
        keep(key, out);
    }
    if (out.isNull())
        return {};

    // In the colour, with the drawing's own edges.
    const QRgb rgb = color.rgb() & 0x00ffffff;
    for (int y = 0; y < out.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x)
            line[x] = (QRgb(qAlpha(line[x])) << 24) | rgb;
    }
    if (size)
        *size = out.size();
    return out;
}

void OsdIconProvider::forget() {
    QMutexLocker lock(&g_keptMutex);
    g_kept.clear();
}

bool OsdIconProvider::lookChanged(const QString &key) {
    if (key != QLatin1String("osd_theme") && key != QLatin1String("skin"))
        return false;
    forget();
    return true;
}
