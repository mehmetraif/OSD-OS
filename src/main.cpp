#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QDir>
#include <QStandardPaths>
#include <QCursor>
#include <QDebug>
#include <QWindow>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <locale.h>
#include <csignal>

#include "AppCore.h"
#include "modules/local_files/LocalFilesBackend.h"
#include "modules/plex/PlexBackend.h"
#include "modules/jellyfin/JellyfinBackend.h"
#include "modules/emby/EmbyBackend.h"
#include "modules/ambient_mode/AmbientModeBackend.h"
#include "modules/nfc_reader/NfcReaderBackend.h"
#include "modules/youtube/YouTubeBackend.h"
#include "modules/weather/WeatherBackend.h"
#include "modules/scripts/ScriptsBackend.h"
#include "modules/web_player/WebPlayerBackend.h"
#include "modules/arcade/ArcadeBackend.h"
#include "modules/playlists/PlaylistsBackend.h"
#include "player/MpvController.h"
#include "player/VideoSurface.h"
#include "player/VhsNoise.h"
#include "fx/SelectorFx.h"
#include "input/InputManager.h"
#include "input/IdleTracker.h"
#include "update/UpdateManager.h"
#include "boot/BootProgress.h"
#include "display/DisplayOutput.h"
#include "audio/AudioOutput.h"
#include "audio/MenuMusic.h"
#include "bluetooth/BluetoothManager.h"
#include "util/ExecPath.h"
#include "util/DisplayHandoff.h"
#include "util/OsdIconProvider.h"
#include "util/OsdSkinProvider.h"
#include "util/LegacyNames.h"
#include "util/Diagnostics.h"
#ifdef Q_OS_MAC
#include "util/MacosUtils.h"
#endif

static QString resolveAppRoot() {
    QString envRoot = qEnvironmentVariable("APP_ROOT");
    if (!envRoot.isEmpty())
        return QDir(envRoot).canonicalPath();

    QString appDir = QCoreApplication::applicationDirPath();

    if (QCoreApplication::applicationFilePath().contains(".app/Contents/MacOS/"))
        return QDir(appDir + "/../Resources").canonicalPath();

    QDir fhsData(appDir + "/../share/osdos");
    if (fhsData.exists())
        return fhsData.canonicalPath();

    return QDir(appDir + "/..").canonicalPath();
}

// The data folder: DATA_ROOT, else the platform's for OSD-OS. Before anything
// reads it, what an install from 240-MP's days left is brought over: its data
// folder, moved here, and the module ids in it (util/LegacyNames).
static QString resolveDataRoot() {
    QString envRoot = qEnvironmentVariable("DATA_ROOT");
    if (!envRoot.isEmpty()) {
        const QString root = QDir(envRoot).canonicalPath();
        legacy::migrateModuleIds(root);
        return root;
    }

    QString path = legacy::migrateDataFolder(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QDir().mkpath(path);
    legacy::migrateModuleIds(path);
    return path;
}

// Terminating signals must unwind normally rather than killing the process where
// it stands. Without this, `systemctl stop`, `pkill`, or a shutdown can leave mpv
// or another process running and with the scripts module (takeover specifically), it
// could also leave a headless Pi on a blank VT with DRM master dropped, because
// ~DisplayHandoff never got the chance to put the display back.
//
// Async-signal-safe: only records the signal. A 100 ms timer in main() polls it and
// exits from the event loop, where destructors run properly.
static volatile std::sig_atomic_t g_termSignal = 0;
extern "C" void osdosHandleTerm(int sig) { g_termSignal = sig; }

int main(int argc, char *argv[]) {
    // Before any thread exists, Qt's or a libmpv core's: fewer malloc arenas
    // (util/Diagnostics), or every film played inside the window leaves tens
    // of MB behind.
    diagnostics::limitMallocArenas();
    // On a headless screen (EGLFS) Qt draws the mouse pointer itself, as a
    // hardware cursor, and the BlankCursor below doesn't always keep it hidden:
    // it came back over the menus after a web player session. The app has no
    // use for one.
    if (qEnvironmentVariableIsEmpty("QT_QPA_EGLFS_HIDECURSOR"))
        qputenv("QT_QPA_EGLFS_HIDECURSOR", "1");
    // OpenGL contexts share their objects, the scene graph's among them, so a
    // video played inside the window is drawn on the GPU into textures it
    // shows as they are (EmbeddedMpv). Before the application exists.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QGuiApplication app(argc, argv);
    app.setApplicationName("OSD-OS");
    app.setApplicationVersion(QStringLiteral(APP_VERSION));

    // Hide cursor — OSD/OS is keyboard/gamepad-only so the cursor serves no
    // purpose. Hidden on all of Linux: headless EGLFS and desktop compositors
    // (Steam Deck / RPi desktop) alike, since the app runs fullscreen kiosk-style.
#ifdef Q_OS_LINUX
    QGuiApplication::setOverrideCursor(Qt::BlankCursor);
#endif
#ifdef Q_OS_MAC
    QGuiApplication::setOverrideCursor(Qt::BlankCursor);
    hideMacOSMenuBar();
#endif

    setlocale(LC_NUMERIC, "C");

    std::signal(SIGTERM, osdosHandleTerm);
    std::signal(SIGINT,  osdosHandleTerm);
    std::signal(SIGHUP,  osdosHandleTerm);
    QTimer termPoll;
    termPoll.setInterval(100);
    QObject::connect(&termPoll, &QTimer::timeout, &app, [&app]() {
        if (g_termSignal) {
            qInfo("[main] Termination signal received — shutting down cleanly");
            // 128 + signal number, the shell convention for "ended by a signal".
            // It lets the autostart service's stop helper (osdos-stop) tell a
            // `systemctl stop`/`restart` apart from the user choosing Quit
            // (exit 0), the only exit that powers the Pi off, and the service
            // count it as no failure (SuccessExitStatus=), not one to start
            // the app again after.
            app.exit(128 + g_termSignal);
        }
    });
    termPoll.start();

    // Once, before anything looks for or spawns mpv / yt-dlp: the locators are
    // pure queries and deliberately do not touch the environment themselves.
    execpath::primeSystemPath();

    const QString appRoot  = resolveAppRoot();
    const QString dataRoot = resolveDataRoot();
    qDebug("[main] appRoot  = %s", qPrintable(appRoot));
    qDebug("[main] dataRoot = %s", qPrintable(dataRoot));
    // A crash writes where it was to the journal and to crash-report.txt in
    // the data folder, which outlives a reboot where the Pi's journal doesn't.
    diagnostics::installCrashHandlers(dataRoot);

    // Log every attached display so a user can discover which index is their
    // target (e.g. a CRT) and set the app-level "display_index" in config.json
    // accordingly. Name is the localized display name on macOS, the RandR
    // output name on X11, the wl_output name on Wayland, and the single DRM
    // output on EGLFS.
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        const QRect g = screens.at(i)->geometry();
        qInfo("[main] display index %d: \"%s\" %dx%d at (%d,%d)",
              i, qPrintable(screens.at(i)->name()),
              g.width(), g.height(), g.x(), g.y());
    }

    QQmlApplicationEngine engine;

    AppCore             appCore(appRoot, dataRoot);

    // Which physical display the UI launches on. App-level "display_index"
    // (0 = primary screen, the previous hardcoded behaviour). Lets the UI open
    // on a secondary display without making it the OS primary. Applied on
    // macOS and desktop Linux (xcb/wayland); on single-display platforms
    // (Pi EGLFS, Steam Deck gaming mode under gamescope) the clamp below
    // resolves to 0 and everything stays a no-op.
    // On macOS, Qt's screen list and AppKit's NSScreen.screens share ordering,
    // so this index is valid for both the QML geometry below and the native
    // forceWindowFullScreenOnScreen() call after load.
    int displayIndex = appCore.get_setting(QString(), "display_index").toInt();
    if (displayIndex < 0 || displayIndex >= screens.size()) {
        if (displayIndex != 0)
            qWarning("[main] display_index %d is out of range (%lld displays attached) — falling back to display 0",
                     displayIndex, (long long)screens.size());
        displayIndex = 0;
    }
    QScreen *targetScreen = screens.value(displayIndex, QGuiApplication::primaryScreen());
    const QRect screenGeo = targetScreen ? targetScreen->geometry() : QRect(0, 0, 1920, 1080);
    qInfo("[main] UI target display index %d -> %dx%d at (%d,%d)",
          displayIndex, screenGeo.width(), screenGeo.height(), screenGeo.x(), screenGeo.y());

    LocalFilesBackend   localFiles(appRoot, dataRoot, &appCore);
    PlexBackend         plexBackend(appRoot, dataRoot);
    JellyfinBackend     jellyfinBackend(appRoot, dataRoot);
    EmbyBackend         embyBackend(appRoot, dataRoot);
    AmbientModeBackend  ambientMode(dataRoot);
    NfcReaderBackend    nfcReader(appRoot, dataRoot, &appCore);
    // Ahead of everything that takes the screen through it, so that all of
    // them are destroyed before it is.
    DisplayHandoff      displayHandoff;
    YouTubeBackend      youtubeBackend(appRoot, dataRoot, &appCore, &displayHandoff);
    WeatherBackend      weatherBackend(appRoot, dataRoot);
    ScriptsBackend      scriptsBackend(appRoot, dataRoot, &displayHandoff);
    // The games in MAME, which takes the screen as a script does.
    ArcadeBackend       arcadeBackend(appRoot, dataRoot, &appCore, &displayHandoff);
    // The streaming services opened as their own web players. TMDB's
    // provider ids are fallbacks for when its name lookup misses; the
    // Wikidata properties hold each service's id for a title.
    WebPlayerBackend    netflixBackend({
        QStringLiteral("netflix"), QStringLiteral("https://www.netflix.com/browse"),
        QStringLiteral("https://www.netflix.com/login"),
        { QStringLiteral("Netflix"), 8, QStringLiteral("Netflix Home"),
          QStringLiteral("https://www.netflix.com/watch/%1"),
          QStringLiteral("https://www.netflix.com/search?q=%1"), QStringLiteral("P1874") } },
        appRoot, dataRoot, &displayHandoff);
    WebPlayerBackend    primeVideoBackend({
        QStringLiteral("prime_video"), QStringLiteral("https://www.primevideo.com"),
        QStringLiteral("https://www.primevideo.com/auth-redirect?signin=1&returnUrl=%2F"),
        { QStringLiteral("Amazon Prime Video"), 119, QStringLiteral("Prime Video Home"),
          QStringLiteral("https://www.primevideo.com/detail/%1"),
          QStringLiteral("https://www.primevideo.com/search/ref=atv_nb_sr?phrase=%1"),
          QStringLiteral("P14440") } },
        appRoot, dataRoot, &displayHandoff);
    // Lists of videos from the modules above, downloaded through them for
    // the offline ones.
    PlaylistsBackend    playlistsBackend(dataRoot, &appCore, &localFiles, &youtubeBackend,
                                         {{QStringLiteral("com.osdos.jellyfin"), &jellyfinBackend},
                                          {QStringLiteral("com.osdos.emby"), &embyBackend}});
    MpvController       mpvController(appRoot, dataRoot, &appCore, &displayHandoff);
    InputManager        inputManager(dataRoot, &appCore);
    IdleTracker         idleTracker(60);   // disabled until Main.qml applies the saved setting
    UpdateManager       updateManager(appRoot, dataRoot);
    BootProgress        bootProgress;      // inert outside the OSD/OS image (os/)
    DisplayOutput       displayOutput(dataRoot); // Settings → Display Output, on the image
    AudioOutput         audioOutput(&appCore);   // Settings → Audio Output (ALSA)
    MenuMusic           menuMusic(dataRoot);     // Settings → Menu Music, or a theme's
    BluetoothManager    bluetoothManager;  // Settings → Bluetooth (BlueZ on Linux)

    // Playback follows the UI's display: mpv gets a --fs-screen* arg derived
    // from this on macOS / desktop Linux (no-op at index 0 and on headless).
    mpvController.setTargetDisplay(displayIndex, targetScreen ? targetScreen->name() : QString());

    // When the Qt window is inactive (fullscreen mpv has OS focus on macOS),
    // gamepad actions bypass QML and drive mpv directly over IPC.
    QObject::connect(&inputManager, &InputManager::mpvKeyRequested,
                     &mpvController, &MpvController::sendKey);
    QObject::connect(&audioOutput, &AudioOutput::cardChanged,
                     &mpvController, &MpvController::followAudioOutput);
    QObject::connect(&audioOutput, &AudioOutput::cardChanged, &menuMusic, &MenuMusic::restart);
    // A video over, the menu music may play again: MpvController holds it off
    // as one starts.
    QObject::connect(&mpvController, &MpvController::playbackEnded, &menuMusic,
                     []() { MenuMusic::release(QStringLiteral("video")); });

    // Each module backend is wired in one call: stored for action routing, exposed to QML
    // under its context-property name, and its optional signals/slots connected by
    // introspection. The module ID lives in exactly one place per module.
    QQmlContext *ctx = engine.rootContext();
    appCore.registerModule("com.osdos.local_files",  "localFilesBackend",  &localFiles,  ctx);
    appCore.registerModule("com.osdos.plex",         "plexBackend",        &plexBackend, ctx);
    appCore.registerModule("com.osdos.jellyfin",     "jellyfinBackend",    &jellyfinBackend, ctx);
    appCore.registerModule("com.osdos.emby",         "embyBackend",        &embyBackend, ctx);
    appCore.registerModule("com.osdos.ambient_mode", "ambientModeBackend", &ambientMode, ctx);
    appCore.registerModule("com.osdos.nfc_reader",   "nfcReaderBackend",   &nfcReader,   ctx);
    appCore.registerModule("com.osdos.youtube",      "youtubeBackend",     &youtubeBackend, ctx);
    appCore.registerModule("com.osdos.weather",      "weatherBackend",     &weatherBackend, ctx);
    appCore.registerModule("com.osdos.scripts",      "scriptsBackend",     &scriptsBackend, ctx);
    appCore.registerModule("com.osdos.netflix",      "netflixBackend",     &netflixBackend, ctx);
    appCore.registerModule("com.osdos.prime_video",  "primeVideoBackend",  &primeVideoBackend, ctx);
    appCore.registerModule("com.osdos.playlists",    "playlistsBackend",   &playlistsBackend, ctx);
    appCore.registerModule("com.osdos.arcade",       "arcadeBackend",      &arcadeBackend, ctx);

    ctx->setContextProperty("idleTracker",   &idleTracker);
    ctx->setContextProperty("appCore",       &appCore);
    ctx->setContextProperty("mpvController", &mpvController);
    ctx->setContextProperty("inputManager",  &inputManager);
    ctx->setContextProperty("updateManager", &updateManager);
    ctx->setContextProperty("bootProgress",  &bootProgress);
    ctx->setContextProperty("displayOutput", &displayOutput);
    ctx->setContextProperty("audioOutput",   &audioOutput);
    ctx->setContextProperty("menuMusic",     &menuMusic);
    ctx->setContextProperty("bluetoothManager", &bluetoothManager);
    // Whether a child has the screen (Main.qml: root.screenHandedOff).
    ctx->setContextProperty("displayHandoff", &displayHandoff);
#ifdef Q_OS_MAC
    // Target display geometry in Qt coordinates (top-left origin), so the QML
    // Window bindings position onto the chosen screen. The native fullscreen
    // call after load then nails the exact frame in AppKit coordinates.
    // QVariant(...) not a literal — a bare 0 is a null pointer constant and
    // resolves to the QObject* overload, handing QML null instead of an int.
    engine.rootContext()->setContextProperty("macScreenX",      QVariant(screenGeo.x()));
    engine.rootContext()->setContextProperty("macScreenY",      QVariant(screenGeo.y()));
    engine.rootContext()->setContextProperty("macScreenWidth",  QVariant(screenGeo.width()));
    engine.rootContext()->setContextProperty("macScreenHeight", QVariant(screenGeo.height()));
#endif

    engine.addImportPath(appRoot + "/views");
    // The title bar's logos, in the theme's colour on the art-pixel grid
    // (image://osdicon/…). The engine owns it.
    engine.addImageProvider(QStringLiteral("osdicon"), new OsdIconProvider);
    engine.addImageProvider(QStringLiteral("osdskin"), new OsdSkinProvider);
    // The picture of a video played inside this window (Transparent Background),
    // and a tape's noise, for the screen a video loads behind (LoadingScreen).
    qmlRegisterType<VideoSurface>("OSDOS.Video", 1, 0, "VideoSurface");
    qmlRegisterType<VhsNoise>("OSDOS.Video", 1, 0, "VhsNoise");
    // Settings → Selector Effect's sparks round the selected line.
    qmlRegisterType<SelectorFx>("OSDOS.Video", 1, 0, "SelectorFx");

    engine.load(QUrl::fromLocalFile(appRoot + "/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        qCritical("[main] QML engine failed to load Main.qml");
        return 1;
    }

    // Gamepad key events are posted straight to the root window so they reach
    // the QML focus item even when another window (mpv) holds OS focus.
    QQuickWindow *rootWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    inputManager.setTargetWindow(rootWindow);

    // On the OS image the services held back for the app start once its first
    // frame is on screen. The timer is a backstop for a platform whose window
    // never reports a swap; markReady() only acts once.
    if (rootWindow)
        QObject::connect(rootWindow, &QQuickWindow::frameSwapped, &bootProgress,
                         &BootProgress::markReady, Qt::SingleShotConnection);
    QTimer::singleShot(5000, &bootProgress, &BootProgress::markReady);

    // The main thread watched once the event loop runs (util/Diagnostics):
    // stuck 5 s, where it is goes to the journal and stall-report.txt. Under
    // the autostart service, still stuck at 20 s, the app exits and the
    // service starts it again, so a frozen menu comes back by itself.
    // OSDOS_WATCHDOG=0 leaves it off.
    if (legacy::env("WATCHDOG") != QLatin1String("0")) {
        const int restartMs = legacy::envIsSet("AUTOSTART") ? 20000 : 0;
        QTimer::singleShot(0, &app, [dataRoot, restartMs]() {
            diagnostics::startStallWatchdog(dataRoot, 5000, restartMs);
        });
        // Its shutdown may take a while (an mpv to see off): not a freeze.
        QObject::connect(&app, &QCoreApplication::aboutToQuit, []() { diagnostics::stopStallWatchdog(); });
    }

#ifdef Q_OS_MAC
    if (QWindow *win = qobject_cast<QWindow *>(engine.rootObjects().first())) {
        // Move the window onto the target screen before forcing fullscreen, so
        // both Qt's and AppKit's notion of the window's screen agree.
        if (targetScreen)
            win->setScreen(targetScreen);
        win->setGeometry(screenGeo);
        win->winId(); // ensure native NSWindow is created
        forceWindowFullScreenOnScreen(reinterpret_cast<void *>(win->winId()), displayIndex);
    }
#elif defined(Q_OS_LINUX)
    // Desktop Linux (Steam Deck desktop mode, generic x86_64) only: EGLFS has
    // a single screen so displayIndex is already clamped to 0 there, but gate
    // on the platform name anyway so this can never disturb the Pi path.
    // Wayland ignores setGeometry (clients can't self-position); re-issuing
    // FullScreen after setScreen makes QtWayland send
    // xdg_toplevel.set_fullscreen(output) for the target.  On xcb the geometry
    // move plus the fullscreen re-request lands it the same way.
    if (displayIndex > 0 && targetScreen
        && (QGuiApplication::platformName() == QLatin1String("xcb")
            || QGuiApplication::platformName() == QLatin1String("wayland"))) {
        if (QWindow *win = qobject_cast<QWindow *>(engine.rootObjects().first())) {
            win->setScreen(targetScreen);
            win->setGeometry(screenGeo);
            win->setVisibility(QWindow::FullScreen);
        }
    }
#endif

    return app.exec();
}
