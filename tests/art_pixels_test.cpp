#include "fx/SelectorFx.h"
#include "player/VhsNoise.h"
#include <QCryptographicHash>
#include <QMetaProperty>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>

// The items that draw in art pixels: VhsNoise (LoadingScreen's tape noise)
// and SelectorFx (the selector effect round the selected line). Each draws a
// picture of its own a frame at a time, `pixel` screen pixels to the art
// pixel, and the screen shows it scaled up without smoothing.
//   - What each draws from a given seed, frame by frame, is what it drew when
//     this test was written: the noise's streaks, its bands and dropouts, and
//     the sparks round a box that moves. The other selector effects work out
//     sines and powers in the C library, which may round a last bit
//     differently from one system to the next: they are checked for drawing
//     something, and alike from the same start.
//   - On the screen each art pixel is a square block from the top left, the
//     last row and column cut short by the edge. (Drawn by Qt's software
//     renderer, which doesn't smooth this item's picture either way: what
//     is checked is where the frame goes, not its filter.)
//   - It runs only in a window, visible, with a size and `running`; the
//     selector effect only with an effect and a box to play round, and when
//     it stops what it drew goes.
//   - Their properties, as QML sets them.
class ArtPixelsTest : public QObject {
    Q_OBJECT

    // A frame's pixels, row by row, into the hash.
    static void add(QCryptographicHash &hash, const QImage &frame) {
        for (int y = 0; y < frame.height(); ++y)
            hash.addData(QByteArray::fromRawData(reinterpret_cast<const char *>(frame.constScanLine(y)),
                                                 frame.width() * 4));
    }

    // The noise's frames from a seed of the test's (its own takes in where
    // the item is in memory, so that two on one screen differ).
    static QByteArray noiseFrames(qreal streaks, bool bands, const QColor &shade, int frames) {
        VhsNoise noise;
        noise.setColor(QColor(80, 255, 160));
        noise.setShade(shade);
        noise.setStreaks(streaks);
        noise.setBands(bands);
        noise.setPixel(3);
        // 133⅓ by 100 art pixels: the last column cut short.
        noise.setSize(QSizeF(400, 300));
        noise.m_seed = 0x12345678u;
        QCryptographicHash hash(QCryptographicHash::Md5);
        for (int i = 0; i < frames; ++i) {
            noise.nextFrame();
            add(hash, noise.m_frame);
        }
        return hash.result().toHex();
    }

    // The effect's frames round a box on a 640×480 screen, at two screen
    // pixels to the art pixel; halfway the box moves up. `drawn`: how many
    // of them had something in them.
    static QByteArray selectorFrames(const QString &effect, int frames, int *drawn = nullptr) {
        QQuickItem screen;
        screen.setSize(QSizeF(640, 480));
        SelectorFx fx(&screen);
        fx.setSize(screen.size());
        QQuickItem box(&screen);
        box.setPosition(QPointF(101, 203));
        box.setSize(QSizeF(300, 25));
        fx.setTarget(&box);
        fx.setEffect(effect);
        fx.setInk(QColor(0, 200, 255));
        fx.setPaper(QColor(20, 0, 60));
        fx.setPixel(2);
        QCryptographicHash hash(QCryptographicHash::Md5);
        int count = 0;
        for (int i = 0; i < frames; ++i) {
            if (i == frames / 2)
                box.setY(150);
            fx.tick();
            add(hash, fx.m_frame);
            count += !fx.m_blank;
        }
        if (drawn)
            *drawn = count;
        return hash.result().toHex();
    }

    // The window as shown, each pixel against the item's frame scaled up
    // `pixel` times from the top left, drawn over the window's black.
    static int unlike(QQuickWindow &window, const QImage &frame, int pixel) {
        const QImage shown = window.grabWindow().convertToFormat(QImage::Format_RGB32);
        if (shown.size() != window.size())
            return -1;
        int count = 0;
        for (int y = 0; y < shown.height(); ++y) {
            for (int x = 0; x < shown.width(); ++x) {
                const QRgb art = frame.pixel(x / pixel, y / pixel);
                if (shown.pixel(x, y) != (0xff000000u | (art & 0xffffffu))) {
                    if (count == 0)
                        qWarning("first unlike at %d,%d: %08x shown, %08x drawn", x, y, shown.pixel(x, y), art);
                    ++count;
                }
            }
        }
        return count;
    }

    // An item's own properties, as "type name notify", sorted.
    static QStringList properties(const QMetaObject *meta) {
        QStringList list;
        for (int i = QQuickItem::staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
            const QMetaProperty p = meta->property(i);
            list << QStringLiteral("%1 %2 %3").arg(QString::fromLatin1(p.typeName()), QString::fromLatin1(p.name()),
                                                   QString::fromLatin1(p.notifySignal().name()));
        }
        list.sort();
        return list;
    }

private slots:
    void initTestCase() {
        qmlRegisterType<VhsNoise>("OSDOS.Video", 1, 0, "VhsNoise");
        qmlRegisterType<SelectorFx>("OSDOS.Video", 1, 0, "SelectorFx");
    }

    void noise_data() {
        QTest::addColumn<qreal>("streaks");
        QTest::addColumn<bool>("bands");
        QTest::addColumn<QColor>("shade");
        QTest::addColumn<QByteArray>("frames");
        // 150 frames: the tracking band rests 60, then rolls down the picture.
        QTest::newRow("streaks") << 1.0 << false << QColor(Qt::transparent)
                                 << QByteArray("59a31ccd8587563cda2e6968ab6812ae");
        QTest::newRow("bands with dropouts") << 0.0 << true << QColor(16, 32, 48)
                                             << QByteArray("d80815c59338322ec9cfbd6d274f5ff7");
        QTest::newRow("worn, bands") << 2.5 << true << QColor(Qt::transparent)
                                     << QByteArray("d07b85f38c0f2274b693a8db1f9ec18c");
    }
    void noise() {
        QFETCH(qreal, streaks);
        QFETCH(bool, bands);
        QFETCH(QColor, shade);
        QFETCH(QByteArray, frames);
        QCOMPARE(noiseFrames(streaks, bands, shade, 150), frames);
    }

    void selector_data() {
        QTest::addColumn<QString>("effect");
        // Empty: compared only with another item's from the same start.
        QTest::addColumn<QByteArray>("frames");
        QTest::newRow("Sparkles") << "Sparkles" << QByteArray("feaecaf84a8f81caee27806ab709ce74");
        QTest::newRow("Welding") << "Welding" << QByteArray();
        QTest::newRow("Lightning") << "Lightning" << QByteArray();
        QTest::newRow("Rainbow") << "Rainbow" << QByteArray();
        QTest::newRow("Snow") << "Snow" << QByteArray();
    }
    void selector() {
        QFETCH(QString, effect);
        QFETCH(QByteArray, frames);
        int drawn = 0;
        const QByteArray a = selectorFrames(effect, 90, &drawn);
        // In the log, to compare by hand on one machine.
        qInfo("%s: %s", QTest::currentDataTag(), a.constData());
        QVERIFY(drawn > 0);
        QCOMPARE(selectorFrames(effect, 90), a);
        if (!frames.isNull())
            QCOMPARE(a, frames);
    }

    // No effect: never a frame with anything in it.
    void selectorNone() {
        int drawn = -1;
        selectorFrames(QString(), 30, &drawn);
        QCOMPARE(drawn, 0);
    }

    void onScreen() {
        QQuickWindow window;
        window.setColor(Qt::black);
        // 75¼ by 50¾ art pixels of 4.
        window.resize(301, 203);
        window.show();

        VhsNoise noise;
        noise.setRunning(false);
        noise.setColor(QColor(80, 255, 160));
        noise.setBands(true);
        noise.setPixel(4);
        noise.setSize(QSizeF(301, 203));
        noise.setParentItem(window.contentItem());
        for (int i = 0; i < 3; ++i)
            noise.nextFrame();
        QCOMPARE(noise.m_frame.size(), QSize(76, 51));
        QCOMPARE(unlike(window, noise.m_frame, 4), 0);
        noise.setVisible(false);

        SelectorFx fx;
        fx.setRunning(false);
        fx.setPixel(4);
        fx.setSize(QSizeF(301, 203));
        fx.setParentItem(window.contentItem());
        QQuickItem box(window.contentItem());
        box.setPosition(QPointF(80, 90));
        box.setSize(QSizeF(140, 20));
        fx.setTarget(&box);
        fx.setEffect(QStringLiteral("Sparkles"));
        for (int i = 0; i < 12; ++i)
            fx.tick();
        QVERIFY(!fx.m_blank);
        QCOMPARE(fx.m_frame.size(), QSize(76, 51));
        QCOMPARE(unlike(window, fx.m_frame, 4), 0);
    }

    void runsWhileShown() {
        QQuickWindow window;
        window.resize(320, 240);

        VhsNoise noise;
        noise.setSize(QSizeF(320, 240));
        QVERIFY(!noise.m_timer.isActive());
        QVERIFY(noise.m_frame.isNull());
        // In a window: a frame at once, then on the timer.
        noise.setParentItem(window.contentItem());
        QVERIFY(noise.m_timer.isActive());
        QCOMPARE(noise.m_frame.size(), QSize(160, 120));
        noise.setVisible(false);
        QVERIFY(!noise.m_timer.isActive());
        noise.setVisible(true);
        QVERIFY(noise.m_timer.isActive());
        QSignalSpy running(&noise, &VhsNoise::runningChanged);
        noise.setRunning(false);
        QVERIFY(!noise.m_timer.isActive());
        noise.setRunning(false);
        QCOMPARE(running.count(), 1);
        noise.setRunning(true);
        QVERIFY(noise.m_timer.isActive());
        noise.setWidth(0);
        QVERIFY(!noise.m_timer.isActive());
        noise.setWidth(320);
        QVERIFY(noise.m_timer.isActive());
        noise.setParentItem(nullptr);
        QVERIFY(!noise.m_timer.isActive());
        // Stopped, it keeps the frame it has.
        QCOMPARE(noise.m_frame.size(), QSize(160, 120));

        SelectorFx fx;
        fx.setSize(QSizeF(320, 240));
        fx.setParentItem(window.contentItem());
        // Nothing to draw, and nothing to draw round.
        QVERIFY(!fx.m_timer.isActive());
        fx.setEffect(QStringLiteral("Sparkles"));
        QVERIFY(!fx.m_timer.isActive());
        QQuickItem box(window.contentItem());
        box.setPosition(QPointF(40, 60));
        box.setSize(QSizeF(200, 20));
        fx.setTarget(&box);
        QVERIFY(fx.m_timer.isActive());
        for (int i = 0; i < 10; ++i)
            fx.tick();
        QVERIFY(!fx.m_blank);
        QVERIFY(!fx.m_sparks.isEmpty());
        // Hidden, it stops, and what it drew goes.
        fx.setVisible(false);
        QVERIFY(!fx.m_timer.isActive());
        QVERIFY(fx.m_blank);
        QVERIFY(fx.m_sparks.isEmpty());
        QVERIFY(fx.m_newFrame);
        QCOMPARE(fx.m_frame.size(), QSize(160, 120));
        for (int y = 0; y < fx.m_frame.height(); ++y)
            for (int x = 0; x < fx.m_frame.width(); ++x)
                QCOMPARE(fx.m_frame.pixel(x, y), 0u);
        fx.setVisible(true);
        QVERIFY(fx.m_timer.isActive());
        fx.setRunning(false);
        QVERIFY(!fx.m_timer.isActive());
        fx.setRunning(true);
        fx.setHeight(0);
        QVERIFY(!fx.m_timer.isActive());
        fx.setHeight(240);
        QVERIFY(fx.m_timer.isActive());
        fx.setEffect(QString());
        QVERIFY(!fx.m_timer.isActive());
    }

    void pixel() {
        VhsNoise noise;
        QCOMPARE(noise.pixel(), 2);
        QSignalSpy noiseChanged(&noise, &VhsNoise::pixelChanged);
        noise.setPixel(0);
        QCOMPARE(noise.pixel(), 1);
        noise.setPixel(-3);
        QCOMPARE(noiseChanged.count(), 1);
        noise.setPixel(4);
        QCOMPARE(noise.pixel(), 4);
        QCOMPARE(noiseChanged.count(), 2);

        SelectorFx fx;
        QCOMPARE(fx.pixel(), 2);
        QSignalSpy fxChanged(&fx, &SelectorFx::pixelChanged);
        fx.setPixel(0);
        QCOMPARE(fx.pixel(), 1);
        fx.setPixel(1);
        QCOMPARE(fxChanged.count(), 1);
    }

    void inQml() {
        QCOMPARE(properties(&VhsNoise::staticMetaObject),
                 QStringList({ "QColor color colorChanged", "QColor shade shadeChanged", "bool bands bandsChanged",
                               "bool running runningChanged", "double streaks streaksChanged",
                               "int pixel pixelChanged" }));
        QCOMPARE(properties(&SelectorFx::staticMetaObject),
                 QStringList({ "QColor ink inkChanged", "QColor paper paperChanged", "QQuickItem* target targetChanged",
                               "QString effect effectChanged", "bool running runningChanged",
                               "int pixel pixelChanged" }));

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\n"
                          "import OSDOS.Video\n"
                          "Item {\n"
                          "    width: 320; height: 240\n"
                          "    VhsNoise { objectName: 'noise'; anchors.fill: parent; color: '#50ffa0'; shade: '#102030';\n"
                          "               pixel: 3; streaks: 0.5; bands: true; running: false }\n"
                          "    SelectorFx { objectName: 'fx'; anchors.fill: parent; target: parent; effect: 'Snow';\n"
                          "                 ink: '#ffffff'; paper: '#000000'; pixel: 0; running: false }\n"
                          "}\n",
                          QUrl());
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *noise = root->findChild<VhsNoise *>(QStringLiteral("noise"));
        QVERIFY(noise);
        QCOMPARE(noise->color(), QColor(0x50, 0xff, 0xa0));
        QCOMPARE(noise->shade(), QColor(0x10, 0x20, 0x30));
        QCOMPARE(noise->pixel(), 3);
        QCOMPARE(noise->streaks(), 0.5);
        QCOMPARE(noise->bands(), true);
        QCOMPARE(noise->running(), false);
        QCOMPARE(noise->size(), QSizeF(320, 240));
        auto *fx = root->findChild<SelectorFx *>(QStringLiteral("fx"));
        QVERIFY(fx);
        QCOMPARE(fx->target(), qobject_cast<QQuickItem *>(root.data()));
        QCOMPARE(fx->effect(), QStringLiteral("Snow"));
        QCOMPARE(fx->ink(), QColor(Qt::white));
        QCOMPARE(fx->paper(), QColor(Qt::black));
        QCOMPARE(fx->pixel(), 1);
        QCOMPARE(fx->running(), false);
    }
};

QTEST_MAIN(ArtPixelsTest)
#include "art_pixels_test.moc"
