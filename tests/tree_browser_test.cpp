#include "util/OsdIconProvider.h"
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

// The views' pixel symbols (PixelIcon) are pictures one of the app's image
// providers makes, so the runner's engine has that provider, as main.cpp's
// has. The logos' (osdicon) stays out, as it always has been here: with it
// every title bar would have its logo, and less room for the titles the tests
// measure.
class Setup : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine *engine) {
        engine->addImageProvider(QStringLiteral("osdpixel"), new OsdPixelProvider);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(tree_browser, Setup)
#include "tree_browser_test.moc"
