#include "MpvController.h"
#include "EmbeddedMpv.h"
#include "../AppCore.h"
#include "../audio/AudioOutput.h"
#include "../util/YtDlpLocator.h"
#include "../util/MpvLocator.h"
#include "../util/DisplayHandoff.h"
#include "../util/FontconfigOverride.h"
#include "../util/AtomicFile.h"
#include "../audio/MenuMusic.h"
#include "../util/Board.h"
#include "../util/LegacyNames.h"
#include "../util/Diagnostics.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QImageReader>
#include <QScreen>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDateTime>
#include <QRegularExpression>
#include <QDebug>
#include <algorithm>
#include <climits>
#include <cmath>

namespace {

// A process retired for the next session (loadAndPlay) is told to quit, is
// killed if it is still there a second later, and is given up on
// (screenBusy) if even that hasn't ended it a few seconds on.
constexpr int kRetireKillMs   = 1000;
constexpr int kRetireGiveUpMs = 5000;
// Commands kept for the player's control connection to come (sendCommand()).
constexpr int kMostCommandsWaiting = 32;
// A key held down in mpv for its repeats (sendKey()) is let go when no
// repeat has come for this long: a keyboard's repeats come every few dozen
// milliseconds, a pad's every 100 ms, so a release that went astray (the
// view lost its focus meanwhile) can't leave a key down for good.
constexpr int kHeldKeyMs = 400;
// How mpv repeats a key held down (the --input-ar-* options every session
// gets): after a moment, six times a second, each repeat run as the player
// gets to it and no faster, and none once the key is up. A TV remote's
// volume key takes the range in three seconds or so at that rate, and a
// held ◄ ► scans at a minute a second.
constexpr int kMpvRepeatDelayMs = 150;
constexpr int kMpvRepeatsPerSecond = 6;

// A channel logo of the user's own (Settings → TV / Movie → Logo Image), read
// for mpv-logo.lua to lay over the picture with mpv's overlay-add: at the
// height OSD/OS's logo stands (7% of the output's, `outputHeight` pixels), its
// shape kept on pixels `pixelAspect` as wide as they are tall (a tube's aren't
// square), as raw premultiplied BGRA, mpv's overlay format, in `out`. mpv
// before 0.38 can't scale an overlay, so it is made at the size it is shown.
// False, and a line in the log, for a picture that can't be read.
bool writeLogoOverlay(const QString &image, const QString &out, int outputHeight, double pixelAspect,
                      int *width, int *height) {
    QImageReader reader(image);
    reader.setAutoTransform(true);
    // A camera's picture may be stored on its side and turned as it is read,
    // after it is scaled: its size as stored, and the size it is scaled to,
    // are turned too.
    const bool turned = reader.transformation() & QImageIOHandler::TransformationRotate90;
    const QSize natural = turned ? reader.size().transposed() : reader.size();
    const int h = qMax(8, qRound(outputHeight * 0.07));
    const auto widthFor = [h, pixelAspect](const QSize &size) {
        return qMax(1, qRound(double(size.width()) * h / size.height() / pixelAspect));
    };
    if (natural.isValid() && !natural.isEmpty()) {
        const QSize shown(widthFor(natural), h);
        reader.setScaledSize(turned ? shown.transposed() : shown);
    }
    QImage picture = reader.read();
    if (picture.isNull()) {
        qWarning("[MpvController] logo image %s: %s", qPrintable(image), qPrintable(reader.errorString()));
        return false;
    }
    if (picture.height() != h)
        picture = picture.scaled(widthFor(picture.size()), h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    picture = picture.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QByteArray raw;
    raw.reserve(picture.width() * picture.height() * 4);
    for (int y = 0; y < picture.height(); ++y)
        raw.append(reinterpret_cast<const char *>(picture.constScanLine(y)), picture.width() * 4);
    // Replaced whole, not rewritten in place: an mpv started just before (one
    // being retired, or libmpv inside the app) may still be reading the last.
    if (!writeFileAtomically(out, raw))
        return false;
    *width = picture.width();
    *height = picture.height();
    return true;
}

// Whether the user gives mpv the screen's shape themselves (monitoraspect or
// monitorpixelaspect, tuned against a test pattern for a tube whose picture
// isn't quite 4:3), in the mpv_video_args setting (`videoArgs`) or mpv.conf:
// then theirs stands, in place of a tube's 4:3. mpv reads $MPV_HOME, else
// $XDG_CONFIG_HOME/mpv (~/.config/mpv while that is unset or empty), and
// /etc/mpv. A line under a [profile] counts too: the profile may be in use
// (profile=, profile-cond), and the 4:3 on the command line would win over
// it.
bool userSetsScreenShape(const QString &videoArgs) {
    static const QRegularExpression key(QStringLiteral("^(--)?monitor(pixel)?aspect\\s*="));
    const QStringList args = videoArgs.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (std::any_of(args.cbegin(), args.cend(), [](const QString &a) { return key.match(a).hasMatch(); }))
        return true;
    const QString home = qEnvironmentVariable("MPV_HOME");
    QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (config.isEmpty())
        config = QDir::homePath() + QStringLiteral("/.config");
    for (const QString &path : { home.isEmpty() ? config + QStringLiteral("/mpv/mpv.conf")
                                                : home + QStringLiteral("/mpv.conf"),
                                 QStringLiteral("/etc/mpv/mpv.conf") }) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        if (std::any_of(lines.cbegin(), lines.cend(), [](const QString &line) { return key.match(line.trimmed()).hasMatch(); }))
            return true;
    }
    return false;
}

// Text for the log with every token the modules hand mpv blanked out: a
// server's in a URL's query (Jellyfin's ApiKey, Emby's api_key), Plex's in
// its header, Jellyfin's in its Authorization header.
QString redactSecrets(QString text) {
    static const QRegularExpression kApiKey(QStringLiteral("Api[_-]?Key=[^&\\s]+"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kPlexToken(QStringLiteral("X-Plex-Token[=:][^&\\s]+"));
    static const QRegularExpression kQuotedToken(QStringLiteral("Token=\"[^\"]+\""));
    text.replace(kApiKey, QStringLiteral("ApiKey=REDACTED"));
    text.replace(kPlexToken, QStringLiteral("X-Plex-Token=REDACTED"));
    text.replace(kQuotedToken, QStringLiteral("Token=\"REDACTED\""));
    return text;
}

} // namespace

MpvController::MpvController(const QString &appRoot, const QString &dataRoot,
                             AppCore *appCore, DisplayHandoff *handoff,
                             QObject *parent)
    : QObject(parent)
    , m_appCore(appCore)
    , m_handoff(handoff)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
#ifdef Q_OS_WIN
    // mpv's control connection on Windows is a named pipe, which QLocalSocket
    // opens by the same name.
    , m_socketPath(QStringLiteral("\\\\.\\pipe\\osdos-mpv-") + QString::number(QCoreApplication::applicationPid()))
#else
    , m_socketPath(QDir::tempPath() + "/osdos-mpv.sock")
#endif
    , m_inputConfPath(QDir::tempPath() + "/osdos-input.conf")
    , m_logFilePath(QDir::tempPath() + "/osdos-mpv.log")
    , m_subInfoPath(QDir::tempPath() + "/osdos-mpv-subinfo.json")
    , m_logoOverlayPath(QDir::tempPath() + "/osdos-logo.bgra")
{
    m_videoProfile = detectVideoProfile();
    qInfo("[MpvController] video profile: %s",
          m_videoProfile == VideoProfile::Pi4       ? "Pi 4 — drm + drm-copy,v4l2m2m-copy"
        : m_videoProfile == VideoProfile::Pi3       ? "Pi 3 — gpu/drm + v4l2m2m (zero-copy)"
        : m_videoProfile == VideoProfile::PiFullKms ? "Pi 5 (Full KMS) — drm + auto-safe"
                                                    : "generic");

    // Back ends the video (backFromProcess), and opens its player's menu if
    // it has one; with Transparent Background it returns to the menus and
    // leaves the video playing (detachToMenus). Which is decided here, not in
    // mpv: one file for a process and an embedded session alike. The deck's
    // own menu still takes back first while it is open (its bindings are
    // forced).
    QFile f(m_inputConfPath);
    if (f.open(QFile::WriteOnly | QFile::Text)) {
        f.write("ESC script-message osdos-menu\n");
        f.write("BS script-message osdos-menu\n");
        f.write("ENTER cycle pause\n");
        f.close();
    }
    // A video left playing behind the menus ends when anything else takes the
    // screen (a takeover script, a web player), or the setting is turned off.
    if (m_handoff) {
        connect(m_handoff, &DisplayHandoff::handingOff, this, [this](const QString &owner) {
            if (owner != QLatin1String(kHandoffOwner) && m_background)
                endEmbedded();
        });
    }
    if (m_appCore) {
        connect(m_appCore, &AppCore::appSettingChanged, this, [this](const QString &key, const QString &) {
            if (key == QLatin1String("transparent_background") && !transparentBackground())
                stopBackground();
        });
    }

    m_hasMpvOscScript     = QFile::exists(m_appRoot + "/scripts/mpv-osc.lua");
    m_hasAmbientOscScript = QFile::exists(m_appRoot + "/scripts/mpv-osc-ambient.lua");
    m_hasMediaKeysScript  = QFile::exists(m_appRoot + "/scripts/mpv-media-keys.lua");

    m_ipc = new QLocalSocket(this);
    connect(m_ipc, &QLocalSocket::connected, this, [this] {
        m_connectTimer->stop();
        if (m_reconnecting) {
            m_reconnecting = false;
            qWarning("[MpvController] Control connection to the player made again");
        }
        m_lastIpcEventMs = QDateTime::currentMSecsSinceEpoch();
        m_watchdogTimer->start();
        sendCommand({"observe_property", 1, "time-pos"});
        sendCommand({"observe_property", 2, "duration"});
        sendCommand({"observe_property", 3, "playlist-pos"});
        sendCommand({"observe_property", 4, "pause"});
        // What was asked before the connection came, in its order: a key's
        // keydown and keyup among it (sendKey()), so a key held down across a
        // break is let go as it was.
        const QList<QJsonArray> waiting = std::exchange(m_commandsWaiting, {});
        for (const QJsonArray &command : waiting)
            sendCommand(command);
    });

    // A key held down in mpv (sendKey()) let go when its repeats stop coming.
    m_keyUpTimer = new QTimer(this);
    m_keyUpTimer->setSingleShot(true);
    m_keyUpTimer->setInterval(kHeldKeyMs);
    connect(m_keyUpTimer, &QTimer::timeout, this, &MpvController::releaseKeys);
    connect(m_ipc, &QLocalSocket::readyRead, this, &MpvController::onIpcReadyRead);

    m_connectTimer = new QTimer(this);
    m_connectTimer->setInterval(100);
    connect(m_connectTimer, &QTimer::timeout, this, &MpvController::tryConnectIpc);
    // The player's control connection gone while it is wanted: made again.
    // An mpv ending closes it too, a moment before its process has gone, so
    // that alone isn't worth a line in the log; one that still plays a few
    // seconds on without it is.
    connect(m_ipc, &QLocalSocket::disconnected, this, [this]() {
        if (!m_ipcWanted) return;
        m_reconnecting = true;
        m_watchdogTimer->stop();
        m_connectTimer->start();
        const int serial = m_launchSerial;
        QTimer::singleShot(3000, this, [this, serial]() {
            if (m_reconnecting && serial == m_launchSerial
                    && ((m_process && m_process->state() == QProcess::Running) || videoActive()))
                qWarning("[MpvController] Control connection to the playing player lost; making it again");
        });
    });

    // Watchdog: fires every 10 s; logs a warning if no IPC time-pos event has
    // arrived for 30 s while connected — strong indicator of a playback freeze.
    // Exempt while paused: time-pos is legitimately silent then (a long pause is
    // a normal state now that the screen saver runs over it), and the unpause
    // property-change event refreshes m_lastIpcEventMs so the 30 s window
    // restarts fresh on resume.
    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setInterval(10000);
    connect(m_watchdogTimer, &QTimer::timeout, this, [this] {
        if (m_ipc->state() != QLocalSocket::ConnectedState || m_paused) return;
        qint64 silenceMs = QDateTime::currentMSecsSinceEpoch() - m_lastIpcEventMs;
        if (silenceMs > 30000) {
            qWarning("[MpvController] WATCHDOG: no IPC time-pos event for %lld s — possible freeze",
                     silenceMs / 1000);
        }
    });
}

MpvController::~MpvController() {
    m_ipcWanted = false;
    // An embedded session ends first, while everything it reports to is still here.
    if (m_embedded)
        m_embedded->stop();
    // Shutdown may occur while a replacement is waiting for the old player.
    // Both processes must release the display before the handoff is restored.
    for (QProcess *process : {m_process, m_retiringProcess.data()}) {
        if (process && process->state() != QProcess::NotRunning) {
            process->disconnect();
            process->terminate();
            if (!process->waitForFinished(2000)) {
                process->kill();
                process->waitForFinished(1000);
            }
        }
    }
    // Quitting mid-playback on a headless Pi used to leave the VT switched away
    // and DRM master dropped — a black screen or a stray text console. Restore
    // synchronously here: terminate() above has already released mpv's hold on
    // the device, and we're exiting, so there's no point deferring 200 ms.
    // A no-op if mpv wasn't holding the screen (any platform, any mode).
    if (m_handoff)
        m_handoff->releaseNow(QLatin1String(kHandoffOwner));
}

QStringList MpvController::sessionArgs(const QString &url, float startSeconds,
                                       int audioTrack, int subTrack,
                                       const QStringList &subFiles,
                                       const QStringList &subLangs, bool loop,
                                       int playlistStart, float transcodeOffsetSec,
                                       const QString &plexToken, bool muteAudio,
                                       const QString &oscMode, bool shuffle,
                                       const QStringList &subTitles, float imageDurationSec,
                                       bool imageContent, const QStringList &extraArgs,
                                       const QString &jellyfinToken, bool embedded) {
    // The Pi 3 overlay plane can't crop; a picture drawn inside the app can.
    const bool noCrop = !embedded && cropUnavailable();
    const bool hasOscScript = (oscMode == "ambient") ? m_hasAmbientOscScript : m_hasMpvOscScript;
    const QString oscScript = m_appRoot + "/scripts/" + ((oscMode == "ambient") ? "mpv-osc-ambient.lua" : "mpv-osc.lua");

    QStringList args;
    args << QString("--input-ipc-server=%1").arg(m_socketPath)
         << QString("--log-file=%1").arg(m_logFilePath)
         << (hasOscScript ? "--osc=no" : "--osc=yes")
         << "--osd-level=0";

    if (hasOscScript)
        args << QString("--script=%1").arg(oscScript);

    // Media-key handling + themed volume bar — loaded for every mode so HID
    // media keys work anytime mpv is playing, not just inside a given module.
    if (m_hasMediaKeysScript)
        args << QString("--script=%1").arg(m_appRoot + "/scripts/mpv-media-keys.lua");

    // Screen saver Lua script — only loaded when the user has opted in via the
    // screensaver_timeout setting (a positive number of seconds; "OFF" parses
    // to 0 and disables). The timeout reaches the script via scriptOpts below.
    int screensaverTimeout = 0;
    if (m_appCore) {
        const int n = m_appCore->get_setting(QString(), "screensaver_timeout").toString().toInt();
        const QString ssScript = m_appRoot + "/scripts/mpv-screensaver.lua";
        if (n > 0 && QFile::exists(ssScript)) {
            screensaverTimeout = n;
            args << QString("--script=%1").arg(ssScript);
        }
    }

    // Settings' CHANNEL LOGO: OSD/OS's logo in a corner of the picture, drawn
    // by mpv itself (scripts/mpv-logo.lua), so it is in the picture in both
    // modes and under whatever the app draws over it. The corner reaches the
    // script with the script options below; unset is the top right.
    // Settings → TV / Movie → Logo Image puts a picture of the user's own
    // there instead, made for the script as an overlay of the size it is
    // shown at.
    QString logoCorner;
    int logoWidth = 0, logoHeight = 0;
    if (m_appCore) {
        const QString corner = m_appCore->get_setting(QString(), "video_logo").toString();
        const QString logoScript = m_appRoot + "/scripts/mpv-logo.lua";
        if (corner != QLatin1String("off") && QFile::exists(logoScript)) {
            logoCorner = (corner == QLatin1String("tl") || corner == QLatin1String("bl")
                          || corner == QLatin1String("br") || corner == QLatin1String("all"))
                         ? corner : QStringLiteral("tr");
            args << QString("--script=%1").arg(logoScript);
            const QString image = m_appCore->get_setting(QString(), "video_logo_image").toString();
            // What mpv draws on: the window's own session the size
            // VideoSurface asked for, in square pixels; a process the UI's
            // screen (setScreen()), whose pixels a tube's mode has narrower or
            // wider.
            QSize output = m_videoTargetSize;
            double pixelAspect = 1;
            if (!embedded) {
                output = m_screenSize;
                if (output.isEmpty()) {
                    const QScreen *screen = QGuiApplication::primaryScreen();
                    output = screen ? screen->size() * screen->devicePixelRatio() : QSize(640, 480);
                }
                if (m_tubeScreen && !output.isEmpty())
                    pixelAspect = (4.0 / 3) * output.height() / output.width();
            }
            if (!image.isEmpty()) {
                // Made once for a picture at a size, and kept for the
                // sessions after at it: reading and scaling it is work the
                // player waits on at every start.
                const QFileInfo info(image);
                const QString madeFor = image + QLatin1Char('|') + QString::number(info.size()) + QLatin1Char('|')
                                        + QString::number(info.lastModified().toMSecsSinceEpoch()) + QLatin1Char('|')
                                        + QString::number(output.height()) + QLatin1Char('|') + QString::number(pixelAspect);
                if (madeFor == m_logoOverlayFor && QFile::exists(m_logoOverlayPath)) {
                    logoWidth = m_logoOverlayWidth;
                    logoHeight = m_logoOverlayHeight;
                } else if (writeLogoOverlay(image, m_logoOverlayPath, output.height(), pixelAspect, &logoWidth, &logoHeight)) {
                    m_logoOverlayFor = madeFor;
                    m_logoOverlayWidth = logoWidth;
                    m_logoOverlayHeight = logoHeight;
                } else {
                    logoWidth = logoHeight = 0;
                    m_logoOverlayFor.clear();
                }
            }
        }
    }

    // Still-image playback only: mpv's KMS output (--vo=drm) won't repaint the
    // primary plane between two consecutive same-size/format stills, so a photo
    // playlist freezes on the first frame while the clock advances. This script
    // nudges a render-affecting property on each playlist advance to force a
    // page-flip. Loaded only for image content, so video playback is untouched.
    if (imageContent) {
        const QString slideshowScript = m_appRoot + "/scripts/mpv-slideshow-redraw.lua";
        if (QFile::exists(slideshowScript))
            args << QString("--script=%1").arg(slideshowScript);
    }

    if (playlistStart >= 0)
        args << QString("--playlist-start=%1").arg(playlistStart);
    if (startSeconds > 0.5f)
        args << QString("--start=%1").arg(double(startSeconds), 0, 'f', 3);
    if (audioTrack > 0)
        args << QString("--aid=%1").arg(audioTrack);
    for (const QString &sf : subFiles)
        args << QString("--sub-file=%1").arg(sf);
    if (subTrack > 0)
        args << QString("--sid=%1").arg(subTrack);
    else if (subTrack < -1)
        // subs disabled or provided via transcode
        args << QStringLiteral("--sid=no");
    else if (subTrack == -1)
        // forced subs only
        args << QStringLiteral("--subs-with-matching-audio=forced") << QStringLiteral("--subs-fallback-forced=always");
    else if (subTrack == 0) {
        // Always display subs, even if the audio and subtitle languages match
        args << QStringLiteral("--subs-with-matching-audio=yes") << QStringLiteral("--subs-fallback=yes");
        if (subFiles.isEmpty())
            // use embedded or auto-matched sub
            args << QStringLiteral("--sid=auto");
    }
    // else: external sub(s) loaded, subTrack==0 → mpv auto-selects first loaded sub
    if (!subLangs.isEmpty())
        args << QString("--slang=%1").arg(subLangs.join(QStringLiteral(",")));

    // yt-dlp hook intercepts HTTP media URLs and can break Plex/Jellyfin
    // playback with spurious 401/400 errors — disabled unless the caller
    // explicitly opts in via extraArgs (e.g. YouTube passes --ytdl=yes).
    bool ytdlEnabled = false;
    for (const QString &a : extraArgs) {
        if (a == QLatin1String("--ytdl") || a.startsWith(QLatin1String("--ytdl=")))
            ytdlEnabled = true;
    }

    QStringList scriptOpts;
    if (transcodeOffsetSec > 0.5f)
        scriptOpts << QString("transcode-offset=%1").arg(double(transcodeOffsetSec), 0, 'f', 3);
    if (screensaverTimeout > 0)
        scriptOpts << QString("screensaver_timeout=%1").arg(screensaverTimeout);
    if (!logoCorner.isEmpty())
        scriptOpts << QString("logo-corner=%1").arg(logoCorner);
    if (logoWidth > 0)
        scriptOpts << QString("logo-image=%1").arg(m_logoOverlayPath)
                   << QString("logo-width=%1").arg(logoWidth) << QString("logo-height=%1").arg(logoHeight);
    // Tell the OSC scripts to hide their CROP button on decode paths where
    // --panscan would blank the video (Pi 3 overlay path, 1080p Playback ON).
    if (noCrop)
        scriptOpts << QStringLiteral("hide-crop=1");

    // Hand the OSC a map of external sub-file URL -> friendly track name so it can show
    // the real subtitle name. mpv otherwise titles an external sub from its URL basename
    // (e.g. "Stream.srt" for Jellyfin sidecars). Purely cosmetic — it does not affect
    // which sub mpv loads or selects.
    QFile::remove(m_subInfoPath);
    if (!subTitles.isEmpty() && subTitles.size() == subFiles.size()) {
        QJsonObject info;
        for (int i = 0; i < subFiles.size(); ++i) {
            if (!subTitles[i].isEmpty())
                info.insert(subFiles[i], subTitles[i]);
        }
        QFile sf(m_subInfoPath);
        if (!info.isEmpty() && sf.open(QFile::WriteOnly | QFile::Truncate)) {
            sf.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            sf.write(QJsonDocument(info).toJson(QJsonDocument::Compact));
            sf.close();
            // Path is comma- and space-free, so it is safe in the script-opts list.
            scriptOpts << QString("subinfo-file=%1").arg(m_subInfoPath);
        }
    }
    // Point mpv's ytdl_hook at the same user-updatable yt-dlp the app resolves,
    // so both agree on one copy even when it isn't on the global PATH (the
    // SteamOS story). Merged into the single --script-opts below — a second
    // --script-opts flag would replace, not append, clobbering the entries above.
    // The comma guard mirrors the subinfo-file constraint (a path with a comma
    // would break the join); realistically never hit.
    if (ytdlEnabled) {
        const QString ytdlPath = ytdlp::locate(m_dataRoot);
        if (!ytdlPath.isEmpty() && !ytdlPath.contains(QLatin1Char(',')))
            scriptOpts << QString("ytdl_hook-ytdl_path=%1").arg(ytdlPath);
    }
    if (!scriptOpts.isEmpty())
        args << QString("--script-opts=%1").arg(scriptOpts.join(QStringLiteral(",")));

    if (loop)
        args << QStringLiteral("--loop-playlist=inf");
    if (shuffle)
        args << QStringLiteral("--shuffle");
    // How long a still image is shown before mpv advances (or EOFs back to the
    // menu). Global for the launch, so it covers every image in a mixed playlist;
    // mpv ignores it for video and animated formats.
    if (imageDurationSec > 0.0f)
        args << QString("--image-display-duration=%1").arg(double(imageDurationSec), 0, 'f', 1);
    if (muteAudio)
        args << QStringLiteral("--no-audio");
    // Settings → Audio Output: the sound card chosen, while it is plugged in;
    // inside the app, Auto's too, which a process has as ALSA's default
    // instead (its environment, startProcess()).
    args << AudioOutput::mpvArgs(embedded);
    // See ytdlEnabled above: default the hook off unless the caller opted in.
    if (!ytdlEnabled)
        args << QStringLiteral("--ytdl=no");
    args << extraArgs;
    if (!plexToken.isEmpty()) {
        args << QString("--http-header-fields=X-Plex-Token:%1").arg(plexToken);
    }
    if (!jellyfinToken.isEmpty()) {
        args << QString("--http-header-fields=Authorization:MediaBrowser Token=\"%1\"").arg(jellyfinToken);
    }

    // plex.direct certs are Let's Encrypt-signed but ffmpeg's bundled CA bundle
    // may not trust the full chain (same reason Qt needs ignoreSslErrors for these
    // hosts). Disable TLS verification only for plex.direct playback URLs.
    if (QUrl(url).host().endsWith(QStringLiteral(".plex.direct")))
        args << QStringLiteral("--tls-verify=no");

    // Scaling: how a picture of another shape fills the screen, a 16:9 film on
    // a 4:3 tube above all. 14:9 crops a little of the sides (panscan 0.43 is
    // 14:9 for a 16:9 picture), Pan & Scan all the bars' worth, and Anamorphic
    // squeezes the picture to fill the screen, for a TV set to 16:9. The
    // cropping ones need panscan, which the Pi3 overlay (smooth) path blanks
    // video under, so it keeps the whole picture there, matching the 1080p
    // Playback trade-off. The OSC's CROP button steps through all four live.
    const QString scaling = videoScaling();
    if (scaling == QLatin1String("Anamorphic")) {
        args << QStringLiteral("--keepaspect=no");
    } else if (!noCrop) {
        if (scaling == QLatin1String("Pan & Scan"))
            args << QStringLiteral("--panscan=1");
        else if (scaling == QLatin1String("14:9"))
            args << QStringLiteral("--panscan=0.43");
    }
    // A tube's pixels aren't square (720×240 from a JAMMA board fills a 4:3
    // screen), and mpv takes them as square unless told the screen's shape:
    // 4:3, or the user's own in mpv.conf or mpv_video_args. The window's own
    // session is drawn on square ones in that shape instead (VideoSurface):
    // libmpv's render API leaves the screen's shape out.
    if (m_tubeScreen && !embedded
            && !userSetsScreenShape(m_appCore ? m_appCore->get_setting(QString(), "mpv_video_args").toString()
                                              : QString()))
        args << QStringLiteral("--monitoraspect=4:3");

    // Video Levels: the RGB range mpv converts YUV into. Emitted only when the
    // user has overridden it, so "Auto" leaves both mpv's own default and
    // anything in their mpv.conf untouched. Sits before appendVideoArgs so an
    // explicit --video-output-levels inside the mpv_video_args override still
    // wins (later on the command line).
    const QString outputLevels = videoOutputLevels();
    if (!outputLevels.isEmpty())
        args << QStringLiteral("--video-output-levels=%1").arg(outputLevels);

    return args;
}

void MpvController::loadAndPlay(const QString &url, float startSeconds,
                                 int audioTrack, int subTrack,
                                 const QStringList &subFiles,
                                 const QStringList &subLangs, bool loop,
                                 int playlistStart, float transcodeOffsetSec,
                                 const QString &plexToken, bool muteAudio,
                                 const QString &oscMode, bool shuffle,
                                 const QStringList &subTitles, float imageDurationSec,
                                 bool imageContent, const QStringList &extraArgs, const QString &jellyfinToken,
                                 const QStringList &extraUrls) {
    // The menu music stops before mpv opens the sound card; it plays again
    // once the session has ended (main.cpp) and the menus want it.
    MenuMusic::hold(QStringLiteral("video"));
    // Its player notes the new session afresh (noteSession), or leaves none,
    // and a session left with Browse is gone: this one takes its place.
    m_sessionNote.clear();
    if (!m_leftNote.isEmpty()) {
        m_leftNote.clear();
        emit leftNoteChanged();
    }
    m_menuOnExit = false;
    // The new session's mpv has neither up yet.
    m_oscMenuShown = false;
    m_saverShown = false;
    // Transparent Background plays inside the app's own window, where the
    // menus can lie over the picture.
    const bool embedded = transparentBackground();
    QStringList args = sessionArgs(url, startSeconds, audioTrack, subTrack, subFiles, subLangs, loop,
                                   playlistStart, transcodeOffsetSec, plexToken, muteAudio, oscMode,
                                   shuffle, subTitles, imageDurationSec, imageContent, extraArgs,
                                   jellyfinToken, embedded);
    // What plays, after the options and a "--", so that a name beginning
    // with "-" is never taken for an option: mpv builds its playlist from
    // every word there. Absolute paths or URLs, extraUrls too; mpv options go
    // in extraArgs.
    const QStringList media = QStringList(url) + extraUrls;
    // The deck's keys, how a held one repeats (sendKey()), and audio as the
    // clock; ahead of the video arguments, so a setting's mpv_video_args
    // still has the last word.
    args << QString("--input-conf=%1").arg(m_inputConfPath)
         << QStringLiteral("--input-ar-delay=%1").arg(kMpvRepeatDelayMs)
         << QStringLiteral("--input-ar-rate=%1").arg(kMpvRepeatsPerSecond)
         << QStringLiteral("--video-sync=audio");
    if (embedded) {
        appendEmbeddedVideoArgs(args, embeddedPlayer()->gpuAvailable());
        args << QStringLiteral("--") << media;
        // Chosen again while it plays behind the menus: the same session goes
        // on, full screen again (as takeBack() has it). Its start is the
        // module's resume point, which reattach() weighs, its sound card
        // follows Settings as it plays (followAudioOutput()), and its decoding
        // and drawing flags follow where it is drawn, which a GPU that failed
        // to take it changes since.
        QStringList drawing;
        appendEmbeddedVideoArgs(drawing, true);
        appendEmbeddedVideoArgs(drawing, false);
        auto comparable = [&drawing](QStringList a) {
            a.erase(std::remove_if(a.begin(), a.end(), [&drawing](const QString &x) {
                        return x.startsWith(QLatin1String("--start="))
                            || x.startsWith(QLatin1String("--audio-device="))
                            || drawing.contains(x);
                    }), a.end());
            return a;
        };
        if (m_background && m_embedded && m_embedded->running()
                && comparable(args) == comparable(m_sessionArgs)) {
            reattach(startSeconds);
            return;
        }
    }

    // Retire the old process without waiting on the UI thread. Keep it alive
    // until it exits: deleting a running QProcess can itself block.
    if (m_process) {
        QProcess *old = m_process;
        old->disconnect();
        if (old->state() != QProcess::NotRunning) {
            m_retiringProcess = old;
            m_retireClock.start();
            connect(old, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    old, &QObject::deleteLater);
            connect(old, &QProcess::errorOccurred, old, [old](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart)
                    old->deleteLater();
            });
            connect(old, &QProcess::started, old, [old]() { old->terminate(); });
            old->terminate();
            QTimer::singleShot(kRetireKillMs, old, [old]() {
                if (old->state() != QProcess::NotRunning)
                    old->kill();
            });
        } else {
            old->deleteLater();
        }
        m_process = nullptr;
    }
    m_ipcWanted = false;
    endEmbedded();
    closeIpc(false, nullptr, nullptr);
    m_playlistPos = -1;
    m_paused      = false;
    m_lastEndFileReason.clear();
    m_pendingStartClear = startSeconds > 0.5f;

    // Stamp the log file so each session is identifiable when tailing over SSH.
    // Owner-only perms: mpv logs its command line (incl. auth headers) at verbose
    // level into --log-file, and it truncates rather than recreates the file — so
    // permissions set here survive the mpv session.
    {
        QFile lf(m_logFilePath);
        if (lf.open(QFile::Append | QFile::Text)) {
            lf.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            lf.write(QString("\n=== OSD/OS session start %1 ===\n    url: %2\n\n")
                         .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                         .arg(redactSecrets(url))
                         .toUtf8());
        }
    }
    // How much memory there was, video by video (util/Diagnostics): a Pi
    // running short shows here before it stalls.
    const QString memory = diagnostics::memoryLine();
    if (!memory.isEmpty())
        qInfo("[MpvController] memory as a video starts: %s", qPrintable(memory));

    // The session starts a tick later, so that its player's loading screen
    // is drawn first: starting mpv runs synchronously and, on the Pi,
    // switches the VT at once, suspending Qt's render thread before the
    // frame can paint. Another loadAndPlay(), or stop(), in the meantime
    // supersedes it.
    m_launchPending = true;
    m_pendingStartMs = int(startSeconds * 1000.0f);
    const int serial = ++m_launchSerial;
    QTimer::singleShot(50, this, [this, serial, args, media, embedded]() {
        launchAfterRetirement(serial, args, media, embedded);
    });
}

void MpvController::launchAfterRetirement(int serial, const QStringList &args,
                                         const QStringList &media, bool embedded) {
    if (serial != m_launchSerial)
        return;
    if (screenBusy()) {
        QTimer::singleShot(25, this, [this, serial, args, media, embedded]() {
            launchAfterRetirement(serial, args, media, embedded);
        });
        return;
    }
    // Played inside the app, the picture needs the screen back first, from
    // the process retired for it.
    if (embedded && m_headlessMode) {
        handBackScreen([this, serial, args, media]() {
            launchAfterRetirement(serial, args, media, true);
        });
        return;
    }
    QFile::remove(m_socketPath);
    m_launchPending = false;
    if (embedded)
        startEmbedded(args);
    else
        startProcess(args, media);
}

void MpvController::stopAfterRetirement(int serial, int positionMs) {
    if (serial != m_launchSerial)
        return;
    if (screenBusy()) {
        QTimer::singleShot(25, this, [this, serial, positionMs]() {
            stopAfterRetirement(serial, positionMs);
        });
        return;
    }
    QFile::remove(m_socketPath);
    // Unless another session was asked for meanwhile: it is that one's now.
    handBackScreen([this, serial, positionMs]() {
        if (serial == m_launchSerial)
            emit playbackEnded(positionMs, 0, QStringLiteral("stopped"));
    });
}

bool MpvController::screenBusy() {
    if (m_handingBack)
        return true;
    if (!m_retiringProcess || m_retiringProcess->state() == QProcess::NotRunning)
        return false;
    if (m_retireClock.elapsed() < kRetireGiveUpMs)
        return true;
    // Stuck where even a kill can't reach it (in a driver, or on a network
    // mount gone away). It still deletes itself if it ever ends; unparented,
    // so that the app quitting doesn't wait on it either.
    qWarning("[MpvController] mpv (pid %lld) is still there %d s after it was told to quit; "
             "going on without it", qint64(m_retiringProcess->processId()), kRetireGiveUpMs / 1000);
    m_retiringProcess->setParent(nullptr);
    m_retiringProcess = nullptr;
    return false;
}

// Ends a session that never got going as if mpv had just exited: the screen
// back first (should it have been handed over, or the process retired for
// this one have had it), then the report, and only for the launch still
// current (another may have started since).
void MpvController::endAtOnce(const QString &reason) {
    const int serial = m_launchSerial;
    QTimer::singleShot(0, this, [this, serial, reason]() {
        handBackScreen([this, serial, reason]() {
            if (serial == m_launchSerial)
                emit playbackEnded(0, 0, reason);
        });
    });
}

// Takes the IPC down after a session: no reconnecting, the timers off, what
// mpv last sent read (drain: readyRead and the end of the process are
// separate events, so its final end-file may still sit unread), the socket
// file gone, and the position and duration it got to handed out and zeroed.
void MpvController::closeIpc(bool drain, int *pos, int *dur) {
    m_ipcWanted = false;
    m_reconnecting = false;
    m_commandsWaiting.clear();
    // Nothing is held down in a player that is over.
    m_keyDown.clear();
    m_keyUpTimer->stop();
    m_connectTimer->stop();
    m_watchdogTimer->stop();
    if (drain && m_ipc->state() == QLocalSocket::ConnectedState)
        onIpcReadyRead();
    m_ipc->abort();
    QFile::remove(m_socketPath);
    if (pos) *pos = m_position;
    if (dur) *dur = m_duration;
    m_position = 0;
    m_duration = 0;
}

void MpvController::handBackScreen(std::function<void()> then) {
    if (!m_headlessMode || !m_handoff) {
        m_headlessMode = false;
        then();
        return;
    }
    // DisplayHandoff defers the DRM restore and VT switch (200 ms by default)
    // because mpv's last KMS atomic commit may still be pending in the vc4
    // driver at the moment the process exits. The next session waits for it
    // (screenBusy), so that the restore can't take the screen from it.
    m_handingBack = true;
    m_handoff->releaseDeferred(QLatin1String(kHandoffOwner), [this, then]() {
        m_handingBack = false;
        m_headlessMode = false;
        then();
    });
}

void MpvController::startProcess(QStringList args, const QStringList &media) {
    // Bundled sibling first, then PATH — see util/MpvLocator.h. Shared with the
    // audio-only spawners so a bundled-mpv or user-drop-in change lands in one
    // place.
    const QString bin = mpvbin::locate();
    if (bin.isEmpty()) {
        qWarning("[MpvController] mpv not found (no bundled sibling, none on PATH)");
        endAtOnce(QStringLiteral("stopped"));
        return;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MpvController::onProcessFinished);
    // An mpv that is there but can't run (a wrong architecture, a noexec
    // mount) never finishes: ended as a missing one is, with the screen back,
    // or a headless Pi would sit black, its keys going to a player that isn't.
    connect(m_process, &QProcess::errorOccurred, this, [this, proc = m_process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || proc != m_process)
            return;
        qWarning("[MpvController] mpv could not be started: %s", qPrintable(proc->errorString()));
        m_ipcWanted = false;
        m_connectTimer->stop();
        proc->deleteLater();
        m_process = nullptr;
        endAtOnce(QStringLiteral("failed"));
    });
    // mpv's own output, to the journal: what it plays among it, so without
    // the tokens.
    connect(m_process, &QProcess::readyRead, this, [this]() {
        const QByteArray out = m_process->readAll();
        if (!out.isEmpty())
            qWarning("[mpv] %s", qPrintable(redactSecrets(QString::fromUtf8(out).trimmed())));
    });
    auto launch = [this, bin, media](QStringList options) {
        options << QStringLiteral("--") << media;
        qDebug("[MpvController] launch: mpv %s", qPrintable(redactSecrets(options.join(QLatin1Char(' ')))));
        m_process->start(bin, options);
        // A start that failed at once (no process to fork: errorOccurred came
        // synchronously, and the session is over) has no socket to wait for.
        if (m_process && m_process->state() != QProcess::NotRunning) {
            m_ipcWanted = true;
            m_connectTimer->start();
        }
    };

    m_headlessMode = DisplayHandoff::isHeadless();
    if (m_headlessMode) {
        {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("APP_ROOT", m_appRoot);
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
            const QString fcConf = fcoverride::write(m_appRoot + "/assets/fonts");
            if (!fcConf.isEmpty())
                env.insert("FONTCONFIG_FILE", fcConf);
#endif
            // Settings → Audio Output's card as ALSA's default.
            AudioOutput::applyTo(env);
            m_process->setProcessEnvironment(env);
        }

        if (m_handoff && m_handoff->isHeldBy(QLatin1String(kHandoffOwner))) {
            // Already in headless mode (the last session ended for this one
            // without releasing the screen). The hand-off already holds Qt's
            // real VT — do NOT acquire again, which would overwrite it with the
            // free VT we are currently on. The old mpv was terminated already;
            // just launch the replacement directly.
            appendVideoArgs(args);
            args << "--no-input-terminal";
            launch(args);
            return;
        }

        // First entry into headless mode: hand the display to mpv. The VT/DRM
        // ordering that makes this work lives in DisplayHandoff::acquire().
        //
        // mpv deliberately does not check savedStateValid() here — playback has
        // always proceeded even when the CRTC state couldn't be captured.
        //
        // A refusal (-1) means another subsystem already owns the screen — e.g. a
        // takeover script is running and an NFC tap asked for playback. Launching
        // anyway would put mpv on a display it does not own, and its later release
        // would be rejected as an owner mismatch. Bail out the same way a missing
        // mpv binary does, with a deferred synthetic end so the caller's Player
        // view doesn't sit there waiting for a signal that never comes.
        if (m_handoff && m_handoff->acquire(QLatin1String(kHandoffOwner)) < 0) {
            qWarning("[MpvController] Cannot start playback: %s has the screen",
                     qPrintable(m_handoff->currentOwner()));
            m_headlessMode = false;
            m_process->deleteLater();
            m_process = nullptr;
            endAtOnce(QStringLiteral("failed"));
            return;
        }

        appendVideoArgs(args);
        args << "--no-input-terminal";
        launch(args);
    } else {
        // Desktop: X11 or Wayland compositor present.
        // Prefer X11/Xwayland for mpv — the Wayland VO stalls waiting for
        // wl_surface frame-done callbacks from labwc (the Pi compositor). But
        // only strip WAYLAND_DISPLAY when there is a DISPLAY to fall back to:
        // on a pure-Wayland session with no Xwayland DISPLAY exported to us
        // (e.g. the Steam Deck's KDE session launched from the file manager),
        // removing it would leave mpv with no output at all and it exits
        // instantly. In that case keep Wayland so mpv can open a window.
        // --no-native-fs avoids macOS Space-transition delays that can
        // prevent early OSD renders from appearing.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("APP_ROOT", m_appRoot);
        if (!qEnvironmentVariable("DISPLAY").trimmed().isEmpty())
            env.remove("WAYLAND_DISPLAY");
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
        // Linux and the BSDs: mpv's OSD in the app's VCR font, through fontconfig.
        const QString fcConf = fcoverride::write(m_appRoot + "/assets/fonts");
        if (!fcConf.isEmpty())
            env.insert("FONTCONFIG_FILE", fcConf);
#endif
        AudioOutput::applyTo(env);
        m_process->setProcessEnvironment(env);
        args << "--fullscreen" << "--no-native-fs";
        // Playback follows the UI's display (app-level "display_index"). Only
        // when a non-default display is configured, so the default command
        // line is untouched. Which form of the option works depends on the
        // windowing system mpv itself ends up on:
        //  - macOS: numeric --fs-screen indexes NSScreen.screens, the same
        //    ordering Qt's screen list (and display_index) already relies on.
        //  - X11 / native Wayland: --fs-screen-name matched against the RandR
        //    output / wl_output name, which is exactly QScreen::name() there.
        //    (Numeric would mean Xinerama order, not guaranteed to match Qt's.)
        //  - Qt on Wayland but DISPLAY set: the env block above stripped
        //    WAYLAND_DISPLAY, so mpv runs on Xwayland where RandR outputs are
        //    named "XWAYLAND0..." and can't match QScreen::name(). Fall back
        //    to numeric — Xwayland's screen order follows wl_output
        //    announcement order like Qt's list does. Best effort; on a
        //    mismatch mpv warns and uses the current screen.
        if (m_displayIndex > 0) {
#ifdef Q_OS_MACOS
            args << QString("--fs-screen=%1").arg(m_displayIndex);
#else
            const bool mpvOnXwayland =
                QGuiApplication::platformName() == QLatin1String("wayland")
                && !qEnvironmentVariable("DISPLAY").trimmed().isEmpty();
            if (mpvOnXwayland || m_displayScreenName.isEmpty())
                args << QString("--fs-screen=%1").arg(m_displayIndex);
            else
                args << QString("--fs-screen-name=%1").arg(m_displayScreenName);
#endif
        }
        appendVideoArgs(args);
#ifdef Q_OS_MACOS
        // mpv runs as a separate process and can't see the app-bundle font via
        // FontLoader. This will load the bundled VCR OSD Mono directly into the OSD libass
        // instance (used by the OSC scripts) so users don't need a system install.
        // macOS libass uses the coretext provider, so the Linux FONTCONFIG_FILE
        // approach doesn't apply here; --osd-fonts-dir is provider-independent.
        args << QString("--osd-fonts-dir=%1").arg(m_appRoot + "/assets/fonts");
#endif
        launch(args);
    }
}

void MpvController::stop() {
    // Not started yet (the tick loadAndPlay() waits): it never does, and for
    // its player it stopped where it was to start. Told as a process's exit
    // is, not from inside the player's own call.
    if (m_launchPending) {
        const int serial = ++m_launchSerial;
        m_launchPending = false;
        const int pos = m_pendingStartMs;
        QTimer::singleShot(0, this, [this, serial, pos]() {
            stopAfterRetirement(serial, pos);
        });
        return;
    }
    if (m_embedded && m_embedded->running()) {
        // finished() follows, as a process's exit does.
        m_embedded->quit();
        return;
    }
    if (m_ipc->state() == QLocalSocket::ConnectedState) {
        sendCommand({"quit"});
    } else if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
    }
}

void MpvController::seekTo(int positionMs) {
    sendCommand({"seek", positionMs / 1000.0, "absolute+exact"});
}

void MpvController::sendKey(const QString &key, bool autoRepeat) {
    if (key == QLatin1String("ESC") || key == QLatin1String("BS")) {
        // Once per press: a held back has nothing more to do.
        if (!autoRepeat)
            backKey(key);
        return;
    }
    if (!autoRepeat) {
        // A new press ends the hold of any other key.
        releaseKeys();
        sendCommand({"keypress", key});
        return;
    }
    // A key held: down in mpv, which repeats it at its own pace (the
    // --input-ar-* options, sessionArgs()) for as long as it is, so that a
    // player slow to act on them is never left a queue of presses to work
    // through once the key is up. Sent as a press each, a held ◄ went on
    // seeking, and a held volume key on turning, after the finger had come
    // off. Each repeat that comes keeps it down (kHeldKeyMs); the key's
    // release (releaseKeys()) or the end of its repeats lets it go. A key
    // whose binding doesn't repeat (the deck menu's ▲ ▼) is held for nothing.
    if (m_keyDown != key) {
        if (!m_keyDown.isEmpty())
            sendCommand({"keyup", m_keyDown});
        m_keyDown = key;
        sendCommand({"keydown", key});
    }
    m_keyUpTimer->start();
}

void MpvController::releaseKeys() {
    m_keyUpTimer->stop();
    if (m_keyDown.isEmpty())
        return;
    // By name: a keyup without one, which would let every key go, is a
    // crash in mpv 0.37 over the control connection.
    const QString key = std::exchange(m_keyDown, {});
    sendCommand({"keyup", key});
}

// Back used to go to mpv as any key, whose input.conf maps it to osdos-menu,
// which came back over the control connection to detachToMenus() or
// backFromProcess(): a round trip through mpv's input queue and the socket,
// and nothing at all while the connection was down or being made again. The
// deck's own menu and the screen saver still take it while they are up
// (their bindings are forced, and they say when they show); otherwise it is
// acted on here, at once. A keyboard's own Esc to an mpv process still comes
// the old way.
void MpvController::backKey(const QString &key) {
    if ((m_oscMenuShown || m_saverShown) && m_ipc->state() == QLocalSocket::ConnectedState) {
        sendCommand({"keypress", key});
        return;
    }
    if (videoActive()) {
        // Behind the menus, or under its player's menu, nothing is left to
        // do for it.
        if (!m_background)
            detachToMenus();
        return;
    }
    if ((m_process && m_process->state() != QProcess::NotRunning) || m_launchPending) {
        backFromProcess();
        return;
    }
    qWarning("[MpvController] back with nothing playing, dropped");
}

void MpvController::showOsdSkipPrompt() {
    sendCommand({"script-message", "skip-overlay-state", "1"});
    sendCommand({"keypress", "DOWN"});
}

void MpvController::clearOsdPrompt() {
    sendCommand({"script-message", "skip-overlay-state", "0"});
}

void MpvController::tryConnectIpc() {
    if (!m_ipcWanted) { m_connectTimer->stop(); m_reconnecting = false; return; }
    if (m_ipc->state() == QLocalSocket::ConnectedState ||
        m_ipc->state() == QLocalSocket::ConnectingState)
        return;
    m_ipc->connectToServer(m_socketPath);
}

void MpvController::onIpcReadyRead() {
    while (m_ipc->canReadLine()) {
        const QByteArray line = m_ipc->readLine().trimmed();
        const QJsonObject obj = QJsonDocument::fromJson(line).object();
        if (obj.isEmpty()) continue;
        const QString event = obj["event"].toString();
        // property-change is the hot path (fires many times per second), so test
        // it first; only other events pay for the end-file check below.
        if (event != "property-change") {
            // mpv reports why playback ended: "eof" (played to the end),
            // "quit"/"stop" (user exited), "error", etc. Remember the last one
            // so onProcessFinished can distinguish a natural finish from a quit.
            if (event == "end-file") {
                m_lastEndFileReason = obj["reason"].toString();
            } else if (event == "playback-restart" && m_pendingStartClear) {
                // --start is a global option that mpv re-applies on *every* file load,
                // and --loop-playlist reloads its entries on wrap. Left set, a resumed
                // file would restart at the resume offset on every loop lap (and every
                // later playlist entry would start there too) instead of at the
                // beginning. mpv only reads `start` when it loads a file, and
                // playback-restart means the initial seek is already done — so clearing
                // it here leaves the current playback alone while every subsequent load
                // begins at 0. Guarded so it fires once per session, not on every seek.
                m_pendingStartClear = false;
                sendCommand({"set_property", "start", "none"});
            } else if (event == "client-message") {
                const QJsonArray args = obj["args"].toArray();
                if (args.size() > 0) {
                    const QString msg = args[0].toString();
                    if (msg == "skip-segment")
                        emit skipRequested();
                    else if (msg == "osdos-menu")
                        videoActive() ? detachToMenus() : backFromProcess();
                    else if (msg == "cycle-sub")
                        emit subtitleCycleRequested();
                    else if (msg == "cycle-audio")
                        emit audioCycleRequested();
                    else if (msg == "osdos-osd-menu")
                        m_oscMenuShown = args.size() > 1 && args[1].toString() == QLatin1String("shown");
                    else if (msg == "osdos-screensaver")
                        m_saverShown = args.size() > 1 && args[1].toString() == QLatin1String("shown");
                }
            }
            continue;
        }

        m_lastIpcEventMs = QDateTime::currentMSecsSinceEpoch();

        const QString     name = obj["name"].toString();
        const QJsonValue  data = obj["data"];
        if (data.isNull() || data.isUndefined()) continue; // property unavailable during shutdown
        if (name == "pause") {
            m_paused = data.toBool();
            continue;
        }
        const double val = data.toDouble();
        // Behind the menus the session is no module's any more: it goes on
        // being followed, unreported, should it be chosen again. Under its
        // player's own menu it is still that player's.
        const bool report = !m_background || m_playerMenu;
        // In milliseconds, within an int: a corrupt file can claim a length
        // beyond it, which converted unbounded is undefined (negative on x86).
        const auto ms = [](double seconds) { return int(qBound(0.0, seconds * 1000.0, double(INT_MAX))); };
        if (name == "time-pos") {
            m_position = ms(val);
            if (report)
                emit positionChanged(m_position);
        } else if (name == "duration") {
            m_duration = ms(val);
            if (report)
                emit durationChanged(m_duration);
        } else if (name == "playlist-pos") {
            m_playlistPos = int(val);
            if (report)
                emit playlistPosChanged(m_playlistPos);
        }
    }
}

void MpvController::onProcessFinished() {
    int exitCode = m_process ? m_process->exitCode() : -1;
    if (m_process) {
        const QByteArray remaining = m_process->readAll();
        if (!remaining.isEmpty())
            qWarning("[mpv] %s", qPrintable(redactSecrets(QString::fromUtf8(remaining).trimmed())));
    }
    if (exitCode != 0)
        qWarning("[MpvController] mpv exited with code %d", exitCode);
    // Drained: m_lastEndFileReason must be accurate, so a natural EOF reliably
    // triggers autoplay-next.
    int pos = 0, dur = 0;
    closeIpc(true, &pos, &dur);
    m_oscMenuShown = false;
    m_saverShown = false;

    // Classify why mpv exited, once, so both the headless and desktop paths emit
    // the same playbackEnded reason:
    //   exit code 2          -> "failed"  (file could not be played; up to the module as to what to do. As an example: Plex attemps a retry in this case)
    //   end-file reason "eof"-> "eof"     (natural end; up to the module as to what to do. As an example: Plex autoplays next)
    //   anything else        -> "stopped" (user quit/stop, crash, or kill; a safe default)
    //   quit for its player's menu (backFromProcess) -> "menu", once the
    //   screen is the app's again; a video that ended on its own meanwhile
    //   ends as any.
    QString reason;
    if (exitCode == 2)                    reason = QStringLiteral("failed");
    else if (m_lastEndFileReason == "eof") reason = QStringLiteral("eof");
    else if (m_menuOnExit)                 reason = QStringLiteral("menu");
    else                                   reason = QStringLiteral("stopped");
    m_menuOnExit = false;

    // Told with the screen back, unless another session was asked for
    // meanwhile: the end isn't its.
    const int serial = m_launchSerial;
    handBackScreen([this, serial, pos, dur, reason]() {
        if (serial == m_launchSerial)
            emit playbackEnded(pos, dur, reason);
    });
}

void MpvController::sendCommand(const QJsonArray &args) {
    if (m_ipc->state() != QLocalSocket::ConnectedState) {
        // A session's connection takes a moment to come (and comes again
        // after a break): what is asked meanwhile waits for it, rather than
        // being lost, a few dozen commands at most. With no session wanted
        // there is nothing to wait for.
        if (m_ipcWanted && m_commandsWaiting.size() < kMostCommandsWaiting) {
            m_commandsWaiting << args;
            return;
        }
        qWarning("[MpvController] IPC not connected, dropping: %s",
                 QJsonDocument(QJsonObject{{"command", args}}).toJson(QJsonDocument::Compact).constData());
        return;
    }
    QJsonObject cmd;
    cmd["command"] = args;
    m_ipc->write(QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n");
}

MpvController::VideoProfile MpvController::detectVideoProfile() const {
#ifdef Q_OS_LINUX
    // By the board's model (util/Board). Pi 3 and Pi 4 both boot Fake KMS but
    // have different CPU budgets, so they get different decode paths; Pi 5
    // boots Full KMS and direct-renders with --vo=drm.
    switch (board::family()) {
    case board::Family::Pi5: return VideoProfile::PiFullKms;
    case board::Family::Pi4: return VideoProfile::Pi4;
    case board::Family::Pi3: return VideoProfile::Pi3;
    case board::Family::Other: break;
    }
#endif
    return VideoProfile::Generic;
}

void MpvController::appendVideoArgs(QStringList &args) const {
    // App-level "mpv_video_args" override replaces the auto-detected vo/hwdec
    // flags verbatim. Read here (not cached) so edits to config.json take effect
    // on the next playback without a rebuild — handy for per-device HW tuning.
    if (m_appCore) {
        const QString override =
            m_appCore->get_setting(QString(), "mpv_video_args").toString().trimmed();
        if (!override.isEmpty()) {
            args << override.split(' ', Qt::SkipEmptyParts);
            return;
        }
    }

    if (m_headlessMode) {
        if (m_videoProfile == VideoProfile::Pi4) {
            // Pi 4B: native --vo=drm draws on the primary plane with precise KMS
            // page-flip timing (smooth cadence). drm-copy (HEVC, on rpivid) and
            // v4l2m2m-copy (H.264) keep decode on the hardware blocks but copy
            // frames back to RAM so they land on that
            // primary plane instead of the drmprime *overlay* plane — the overlay
            // path (vo=gpu zero-copy) decodes just as cheaply but its presentation
            // jitters into visible 24p judder. The copy + zimg downscale costs more
            // CPU (~50-70% across 4 cores) but the Pi4 has the headroom, and crop
            // (--panscan) works because frames go through the normal scaler.
            args << "--vo=drm" << "--hwdec=drm-copy,v4l2m2m-copy";
        } else if (m_videoProfile == VideoProfile::Pi3) {
            // Pi 3B/3B+: too weak for the copy + software-scale path above (it pegs
            // all four cores and gets choppy). Zero-copy v4l2m2m hands decoded frames
            // straight to a DRM overlay plane for the lowest possible CPU (~15%) with
            // smooth playback. The one trade-off: the overlay plane can't zoom/crop,
            // so mpv's --panscan (the OSC crop button) blanks the video on this path.
            // The "smooth_playback" setting (default ON) lets the user opt out: when
            // OFF we fall back to the crop-capable scaler path (--vo=drm) at the cost
            // of higher CPU and less smooth cadence.
            if (smoothPlaybackEnabled())
                args << "--vo=gpu" << "--gpu-context=drm" << "--hwdec=v4l2m2m";
            else
                args << "--vo=drm" << "--hwdec=v4l2m2m-copy";
        } else {
            // Pi 5 (Full KMS) and a safe fallback for unknown headless Linux for now.
            // Note: Sometimes on Pi5+composite CRTs the Pi5 reports its composite raster 
            // inconsistently (sometimes a narrow 704×432 instead of the standard 720×480i)
            // this can be accomodated for in mpv.conf via a monitorpixelaspect property or
            // in cmdline.txt/config.txt at the OS level vs hardcoding something here.
            args << "--vo=drm" << "--hwdec=auto-safe";
        }
        // The output the display preset names, which the launcher found for
        // the app's own screen (OSDOS_DRM_*): mpv plays on it too, in its mode,
        // where the board has more than one output on (a Pi 5 keeps HDMI on
        // beside a CRT, and the mode's lines pick its PAL or NTSC).
        const QString drmDevice = legacy::env("DRM_DEVICE");
        const QString drmConnector = legacy::env("DRM_CONNECTOR");
        if (!drmDevice.isEmpty() && !drmConnector.isEmpty()) {
            args << "--drm-device=" + drmDevice << "--drm-connector=" + drmConnector;
            const QString drmMode = legacy::env("DRM_MODE");
            if (!drmMode.isEmpty())
                args << "--drm-mode=" + drmMode;
        }
    } else {
#if defined(Q_OS_MACOS)
        // Apple Silicon: enable VideoToolbox HW decode (mpv's default is none).
        args << "--hwdec=videotoolbox";
#elif defined(Q_OS_WIN)
        // Windows: the GPU's decoder (D3D11) where mpv finds it safe.
        args << "--hwdec=auto-safe";
#elif defined(Q_OS_BSD4)
        // The BSDs: VA-API through Mesa where there is one, as mpv finds it safe.
        args << "--hwdec=auto-safe";
#elif defined(Q_OS_LINUX)
        // Desktop compositor (SteamDeck gamescope / KDE, x86_64 Intel/AMD): mpv
        // sets no hwdec by default, so decode falls back to software. This is an
        // explicit priority list rather than auto-safe because auto-safe also
        // considers Vulkan video decode, and on a host where neither NVDEC nor
        // VA-API can initialise it walks all the way down to that: on Batocera
        // with an NVIDIA GPU (no usable nvidia_drv_video.so, "Could not create
        // device" for cuda) mpv picked h264-vulkan, presented exactly one frame
        // and then deadlocked with the demuxer still buffering — a hard freeze
        // needing SIGKILL. The Vulkan *output* path is fine and stays in use; only
        // Vulkan decoding is excluded. VA-API still wins on the Deck's AMD APU and
        // on Intel/AMD, NVDEC covers NVIDIA where it actually initialises, the
        // -copy variants catch hosts whose VO interop is unavailable, and the
        // trailing "no" degrades to software instead of hanging.
        // Fully overridable via the mpv_video_args setting handled above.
        args << "--hwdec=vaapi,nvdec,vaapi-copy,nvdec-copy,no";
#endif
        // Any other desktop: leave mpv's defaults untouched.
    }
}

bool MpvController::smoothPlaybackEnabled() const {
    // Default ON: only an explicit "Off" opts out. Stored by Settings as a string
    // ("On"/"Off") via the list_single row, so compare on the string form.
    if (!m_appCore)
        return true;
    const QVariant v = m_appCore->get_setting(QString(), "smooth_playback");
    if (!v.isValid() || v.toString().isEmpty())
        return true;
    return v.toString().compare(QStringLiteral("Off"), Qt::CaseInsensitive) != 0;
}

void MpvController::setTargetDisplay(int index, const QString &screenName) {
    m_displayIndex      = index;
    m_displayScreenName = screenName;
}

void MpvController::setScreen(bool tube, const QSize &size) {
    m_tubeScreen = tube;
    m_screenSize = size;
}

bool MpvController::autoCropEnabled() const {
    // Default OFF: only an explicit "On" opts in. Stored by Settings as a string
    // ("On"/"Off") via the list_single row, so compare on the string form.
    if (!m_appCore)
        return false;
    const QVariant v = m_appCore->get_setting(QString(), "auto_crop");
    return v.toString().compare(QStringLiteral("On"), Qt::CaseInsensitive) == 0;
}

QString MpvController::videoScaling() const {
    if (!m_appCore)
        return QStringLiteral("Letterbox");
    if (!m_activeModule.isEmpty()) {
        const QString own = m_appCore->get_setting(m_activeModule, "video_scaling").toString();
        if (!own.isEmpty() && own.compare(QStringLiteral("Default"), Qt::CaseInsensitive) != 0)
            return own;
    }
    const QString app = m_appCore->get_setting(QString(), "video_scaling").toString();
    if (!app.isEmpty())
        return app;
    return autoCropEnabled() ? QStringLiteral("Pan & Scan") : QStringLiteral("Letterbox");
}

QString MpvController::videoOutputLevels() const {
    // Default "Auto" → no flag at all, leaving mpv's own default (full-range RGB
    // out) and anything the user set in mpv.conf in place. Stored by Settings as
    // a string ("Auto"/"Limited"/"Full") via the list_single row, so compare on
    // the string form.
    if (!m_appCore)
        return {};
    const QString v = m_appCore->get_setting(QString(), "video_output_levels").toString();
    if (v.compare(QStringLiteral("Limited"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("limited");
    if (v.compare(QStringLiteral("Full"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("full");
    return {};
}

bool MpvController::cropUnavailable() const {
    // The Pi 3 smooth (overlay-plane) path is the one decode path that can't
    // crop/zoom: --panscan blanks the video there. (Ignores the mpv_video_args
    // override, same as the auto-crop gate — the setting still reflects intent.)
    return m_videoProfile == VideoProfile::Pi3 && smoothPlaybackEnabled();
}

bool MpvController::hasSmoothPlaybackTradeoff() const {
    // Only the Pi 3 overlay path sacrifices crop/zoom for smoothness. Every other
    // profile (Pi 4 copy path, Pi 5/generic --vo=drm, desktop) can already crop, so
    // the toggle would be a no-op there and is hidden.
    return m_videoProfile == VideoProfile::Pi3;
}

// ---------------------------------------------------------------------------
// Transparent Background: sessions played inside the app's own window
// ---------------------------------------------------------------------------

bool MpvController::embeddedAvailable() const {
    return EmbeddedMpv::available();
}

bool MpvController::transparentBackground() const {
    if (!m_appCore)
        return false;
    // How solid the menus' ground is over the picture, 0 to 100, Settings'
    // TRANSPARENT … SOLID slider (SOLID hides the picture, which plays on), or
    // Off, the default when unset. Its first values were words: On (clear)
    // and Dim (60). Main.qml's backgroundOn() reads it the same way.
    const QString v = m_appCore->get_setting(QString(), "transparent_background").toString().trimmed();
    bool on = v.compare(QStringLiteral("On"), Qt::CaseInsensitive) == 0
              || v.compare(QStringLiteral("Dim"), Qt::CaseInsensitive) == 0;
    if (!on)
        v.toInt(&on);
    return on && EmbeddedMpv::available();
}

bool MpvController::videoActive() const {
    return m_embedded && m_embedded->running();
}

QImage MpvController::videoFrame() const {
    return m_embedded ? m_embedded->frame() : QImage();
}

bool MpvController::videoOnGpu() const {
    return m_embedded && m_embedded->gpuRendering();
}

EmbeddedMpv::GpuFrame MpvController::videoGpuFrame() {
    return m_embedded ? m_embedded->gpuFrame() : EmbeddedMpv::GpuFrame();
}

void MpvController::setVideoTargetSize(const QSize &size) {
    m_videoTargetSize = size;
    if (m_embedded)
        m_embedded->setTargetSize(size);
}

void MpvController::setVideoShown(bool shown) {
    m_videoShown = shown;
    if (m_embedded)
        m_embedded->setShown(shown);
}

void MpvController::stopBackground() {
    if (m_background)
        stop();
}

void MpvController::noteSession(const QVariantMap &note) {
    // Only a session played inside the window is left behind the menus
    // (backgroundNote), but a process's says whether back opens a menu.
    m_sessionNote = note;
}

void MpvController::leaveSession() {
    if (m_sessionNote.isEmpty() || m_leftNote == m_sessionNote)
        return;
    m_leftNote = m_sessionNote;
    emit leftNoteChanged();
}

void MpvController::appendEmbeddedVideoArgs(QStringList &args, bool gpu) const {
    // The mpv_video_args override is for mpv's own outputs, so not used here.
    // Drawn on the GPU, the decoders' frames go to it as they are: drm (HEVC)
    // and v4l2m2m (H.264) on a Pi 4 or 3, NVDEC on a PC, each ahead of its
    // copy-back mode, which mpv falls back to where it can't hand frames over
    // (and the software renderer, should the GPU fail, takes). VA-API only as
    // a copy: its frames would need the window system's display, which mpv
    // isn't given. mpv's passes between render into 8-bit textures rather
    // than half floats: half the memory traffic for the Pi's GPU. On a Pi the
    // GPU enlarges with mpv's fast profile (bilinear), but shrinks a picture
    // with hermite widened to the scale, as mpv does by default, so a 1080p
    // or 4K one brought down to a CRT's lines doesn't shimmer, and dithers as
    // the software renderer does, so gradients don't band.
    switch (m_videoProfile) {
    case VideoProfile::Pi4:
        args << (gpu ? QStringLiteral("--hwdec=drm,v4l2m2m,drm-copy,v4l2m2m-copy")
                     : QStringLiteral("--hwdec=drm-copy,v4l2m2m-copy"));
        break;
    case VideoProfile::Pi3:
        args << (gpu ? QStringLiteral("--hwdec=v4l2m2m,v4l2m2m-copy")
                     : QStringLiteral("--hwdec=v4l2m2m-copy"));
        break;
    case VideoProfile::PiFullKms:
        // A Pi 5's working decoder for H.264 and HEVC is FFmpeg's Vulkan one
        // (vulkan-copy, which auto-copy-safe picks there), whose frames
        // reach mpv's OpenGL renderer only as copies.
        args << QStringLiteral("--hwdec=auto-copy-safe");
        break;
    case VideoProfile::Generic:
#if defined(Q_OS_MACOS)
        args << QStringLiteral("--hwdec=videotoolbox-copy")
             << QString("--osd-fonts-dir=%1").arg(m_appRoot + "/assets/fonts");
#elif defined(Q_OS_WIN)
        args << QStringLiteral("--hwdec=auto-copy-safe")
             << QString("--osd-fonts-dir=%1").arg(m_appRoot + "/assets/fonts");
#elif defined(Q_OS_BSD4)
        args << QStringLiteral("--hwdec=auto-copy-safe");
#elif defined(Q_OS_LINUX)
        args << (gpu ? QStringLiteral("--hwdec=nvdec,vaapi-copy,nvdec-copy,no")
                     : QStringLiteral("--hwdec=vaapi-copy,nvdec-copy,no"));
#endif
        break;
    }
    if (gpu)
        args << QStringLiteral("--fbo-format=rgba8");
    if (gpu && m_videoProfile != VideoProfile::Generic)
        args << QStringLiteral("--profile=fast") << QStringLiteral("--dscale=hermite")
             << QStringLiteral("--correct-downscaling=yes") << QStringLiteral("--dither=fruit");
}

EmbeddedMpv *MpvController::embeddedPlayer() {
    if (!m_embedded) {
        m_embedded = new EmbeddedMpv(this);
        m_embedded->setTargetSize(m_videoTargetSize);
        m_embedded->setShown(m_videoShown);
        connect(m_embedded, &EmbeddedMpv::frameReady, this, &MpvController::videoFrameReady);
        connect(m_embedded, &EmbeddedMpv::finished, this, &MpvController::onEmbeddedFinished);
    }
    return m_embedded;
}

void MpvController::startEmbedded(QStringList args) {
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
    // mpv draws its OSD (the deck's menu) in the app's VCR font: libass finds
    // it through fontconfig, set up as for an mpv process of its own. Only
    // fontconfig set up afterwards reads this; Qt's own already is.
    const QString fcConf = fcoverride::write(m_appRoot + "/assets/fonts");
    if (!fcConf.isEmpty())
        qputenv("FONTCONFIG_FILE", fcConf.toUtf8());
#endif
    // The screen stays the app's, even headless: nothing to hand over.
    m_headlessMode = false;
    qDebug("[MpvController] embedded launch: mpv %s", qPrintable(redactSecrets(args.join(QLatin1Char(' ')))));
    if (!embeddedPlayer()->start(args)) {
        qWarning("[MpvController] Cannot start playback: libmpv could not be set up");
        endAtOnce(QStringLiteral("failed"));
        return;
    }
    m_sessionArgs = args;
    emit videoActiveChanged();
    m_ipcWanted = true;
    m_connectTimer->start();
}

void MpvController::endEmbedded() {
    if (!m_embedded || !m_embedded->running())
        return;
    m_embedded->stop();
    // Its control connection with it: left, it would be made again, to a
    // socket of a session that is over.
    int pos = 0, dur = 0;
    closeIpc(false, &pos, &dur);
    m_sessionArgs.clear();
    m_sessionNote.clear();
    m_oscMenuShown = false;
    m_saverShown = false;
    const bool wasMenu = m_playerMenu;
    m_playerMenu = false;
    if (m_background) {
        m_background = false;
        emit backgroundChanged();
    }
    emit videoActiveChanged();
    // Its player had its menu open over it (something else took the screen):
    // for it, playback stopped.
    if (wasMenu)
        emit playbackEnded(pos, dur, QStringLiteral("stopped"));
}

void MpvController::onEmbeddedFinished(const QString &lastEndReason) {
    int pos = 0, dur = 0;
    closeIpc(true, &pos, &dur);
    m_sessionArgs.clear();
    m_sessionNote.clear();
    m_oscMenuShown = false;
    m_saverShown = false;
    const bool wasBackground = m_background;
    const bool wasMenu = m_playerMenu;
    m_playerMenu = false;
    if (wasBackground) {
        m_background = false;
        emit backgroundChanged();
    }
    emit videoActiveChanged();
    // Its module took it as stopped already, when back left it behind the
    // menus; not so a player with its menu open over it.
    if (wasBackground && !wasMenu)
        return;
    // The same reasons as a process's: a file mpv couldn't play is "failed",
    // the playlist played out "eof", anything else (quit) "stopped".
    QString reason;
    if (lastEndReason == QLatin1String("error"))    reason = QStringLiteral("failed");
    else if (lastEndReason == QLatin1String("eof")) reason = QStringLiteral("eof");
    else                                            reason = QStringLiteral("stopped");
    emit playbackEnded(pos, dur, reason);
}

void MpvController::detachToMenus() {
    if (!m_embedded || !m_embedded->running() || m_background)
        return;
    m_background = true;
    m_detachPositionMs = m_position;
    m_playerMenu = m_sessionNote.value(QStringLiteral("menu")).toBool();
    emit backgroundChanged();
    // The deck's own menu, if it is open, goes with the full-screen view.
    sendCommand({"script-message", "osdos-osd-menu-hide"});
    // A player with a menu of its own opens it over the picture, and the
    // session stays its.
    if (m_playerMenu) {
        emit playerMenuRequested();
        return;
    }
    // For any other module, playback stopped here: it saves where it got to
    // and goes back to the menus, which now lie over the picture.
    emit playbackEnded(m_position, m_duration, QStringLiteral("stopped"));
}

void MpvController::backFromProcess() {
    // mpv has the screen while it plays, so nothing of the app's can lie over
    // the picture: the video ends, and its player's menu, if it has one,
    // opens in its place (onProcessFinished). Its player starts it again where
    // it was as the menu closes.
    m_menuOnExit = m_sessionNote.value(QStringLiteral("menu")).toBool();
    stop();
}

void MpvController::closePlayerMenu() {
    if (!m_playerMenu)
        return;
    m_playerMenu = false;
    m_background = false;
    emit backgroundChanged();
}

void MpvController::leavePlayerMenu() {
    if (!m_playerMenu)
        return;
    m_playerMenu = false;
    // As back without a menu: its player takes it as stopped, saves where it
    // is now, and goes back to its module's menus, the picture going on
    // behind them. Chosen again from there, it carries on (takeBack()).
    m_detachPositionMs = m_position;
    emit playbackEnded(m_position, m_duration, QStringLiteral("stopped"));
}

void MpvController::followAudioOutput() {
    // A video playing inside the app, behind the menus, goes on through the
    // card chosen, or Auto's. An mpv process can't be playing while Settings
    // is up; the next one is started with it.
    if (!m_embedded || !m_embedded->running())
        return;
    const QString device = AudioOutput::mpvDevice();
    setVideoProperty(QStringLiteral("audio-device"), device.isEmpty() ? QStringLiteral("auto") : device);
}

void MpvController::setVideoProperty(const QString &name, const QVariant &value) {
    if (m_ipc->state() != QLocalSocket::ConnectedState)
        return;
    sendCommand({"set_property", name, QJsonValue::fromVariant(value)});
}

void MpvController::reattach(float startSeconds) {
    m_background = false;
    emit backgroundChanged();
    // The module asks to resume where back left it (the point it saved then):
    // that is carrying on where it is now. Anything else, from the start say,
    // it gets.
    const double start = startSeconds > 0.5f ? double(startSeconds) : 0.0;
    if (std::abs(start * 1000.0 - m_detachPositionMs) > 1500.0)
        sendCommand({"seek", start, "absolute"});
    emit durationChanged(m_duration);
    if (m_playlistPos >= 0)
        emit playlistPosChanged(m_playlistPos);
    emit positionChanged(m_position);
}

bool MpvController::takeBack(float startSeconds) {
    if (!m_background || !videoActive())
        return false;
    reattach(startSeconds);
    return true;
}
