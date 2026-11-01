#pragma once
#include <QQuickImageProvider>

// A skin's picture of one of the window's parts (Settings → Look → Skin: its frame,
// the title and hint bars, the selected line) in the colour scheme's two
// colours, so a theme gives the window its shape and the scheme its colours:
//
//     image://osdskin/<rrggbb>/<rrggbb>/<url>
//
// draws the picture at <url> (a file) at its own size, in two colours: where
// it is light, the first (the scheme's colour); where it is dark, the second
// (its background); where it is clear, nothing. Like OsdIconProvider, it draws
// on every scene graph backend, the software one included.
class OsdSkinProvider : public QQuickImageProvider {
public:
    OsdSkinProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
