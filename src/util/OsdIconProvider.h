#pragma once
#include <QQuickImageProvider>

// An image (a module's logo.svg) drawn in one colour, the way a deck's
// on-screen display draws its symbols:
//
//     image://osdicon/<rrggbb>/<url>
//
// renders what <url> draws, trimmed of the margin around it, from the original
// (a vector is drawn at the requested height, not scaled from a bitmap; a
// picture of pixels, a skin's icon, grows with its pixels sharp), and colours
// it, keeping its smooth edges. Unlike a shader effect it draws on every scene
// graph backend, the software one included.
//
// What a file gave at a size is kept (its drawing, or that it gave none): the
// same icon asked for again, in another colour too, is coloured from that,
// its file neither looked for nor read again. A view made again asks for its
// title bar's logo again, and Camcorder's column for a module's logo in the
// other colour at every move.
class OsdIconProvider : public QQuickImageProvider {
public:
    OsdIconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    // What was kept is forgotten: the next request reads its file.
    static void forget();
    // An app setting changed (AppCore::appSettingChanged): Settings' Theme
    // or Skin brings other icons, or the same folder's as they are now, so
    // what was kept is forgotten. True when the key was one of those.
    static bool lookChanged(const QString &key);
};
