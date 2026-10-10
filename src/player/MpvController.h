#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QPointer>
#include <QLocalSocket>
#include <QSize>
#include <QTimer>
#include <QJsonArray>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include "EmbeddedMpv.h"

class AppCore;
class DisplayHandoff;

class MpvController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int position    READ position    NOTIFY positionChanged)
    Q_PROPERTY(int duration    READ duration    NOTIFY durationChanged)
    Q_PROPERTY(int playlistPos READ playlistPos NOTIFY playlistPosChanged)
    // Transparent Background (see below): a session is playing inside the
    // app's own window, and VideoSurface shows its picture...
    Q_PROPERTY(bool videoActive READ videoActive NOTIFY videoActiveChanged)
    // ...and goes on behind the menus, back having returned to them.
    Q_PROPERTY(bool background READ background NOTIFY backgroundChanged)
    // What its player noted of the session behind the menus (noteSession), so
    // the main menu can offer it back; empty while there is none.
    Q_PROPERTY(QVariantMap backgroundNote READ backgroundNote NOTIFY backgroundChanged)
    // A session its player left with its menu's Browse while an mpv process
    // had the screen (leaveSession), for the main menu to offer back as it
    // does one behind the menus; empty while there is none.
    Q_PROPERTY(QVariantMap leftNote READ leftNote NOTIFY leftNoteChanged)

public:
    explicit MpvController(const QString &appRoot, const QString &dataRoot,
                           AppCore *appCore = nullptr,
                           DisplayHandoff *handoff = nullptr,
                           QObject *parent = nullptr);
    ~MpvController() override;

    int position()    const { return m_position;    }
    int duration()    const { return m_duration;    }
    int playlistPos() const { return m_playlistPos; }

    Q_INVOKABLE void loadAndPlay(const QString &url, float startSeconds,
                                  int audioTrack, int subTrack,
                                  const QStringList &subFiles = {},
                                  const QStringList &subLangs = {},
                                  bool loop = false,
                                  int playlistStart = -1,
                                  float transcodeOffsetSec = 0.0f,
                                  const QString &plexToken = {},
                                  bool muteAudio = false,
                                  const QString &oscMode = {},
                                  bool shuffle = false,
                                  const QStringList &subTitles = {},
                                  float imageDurationSec = 0.0f,
                                  bool imageContent = false,
                                  const QStringList &extraArgs = {},
                                  const QString &jellyfinToken = {},
                                  const QStringList &extraUrls = {});
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seekTo(int positionMs);
    Q_INVOKABLE void sendKey(const QString &key);
    Q_INVOKABLE void showOsdSkipPrompt();
    Q_INVOKABLE void clearOsdPrompt();

    // True only on devices whose smooth-playback decode path can't crop/zoom (the
    // Pi 3 DRM-overlay path). Settings uses this to show the "Smooth Playback"
    // toggle only where the smoothness-vs-crop trade-off actually exists.
    Q_INVOKABLE bool hasSmoothPlaybackTradeoff() const;

    // The module whose views are open (Main.qml, as its loader changes), for
    // the settings a module can override: its own Scaling.
    Q_INVOKABLE void setActiveModule(const QString &moduleId) { m_activeModule = moduleId; }

    // Transparent Background (app setting "transparent_background": how solid
    // the menus' ground is over the picture, 0 to 100 on Settings' TRANSPARENT
    // ... SOLID slider, or Off, the default): video is played inside the app's own
    // window (EmbeddedMpv) rather than by an mpv process over it, so the menus
    // can be drawn over the picture. Back from playback then returns to the
    // menus and leaves the video playing behind them: the module takes it as
    // stopped (it saves where it got to, and goes back), while the picture
    // and the sound go on. Choosing the same thing again brings it back full
    // screen where it is; playing anything else, Main.qml's STOP, a takeover
    // or the setting turned off ends it.
    //
    // Needs libmpv, opened at run time: embeddedAvailable() says whether it is
    // there (Settings offers the setting only then).
    Q_INVOKABLE bool embeddedAvailable() const;
    bool videoActive() const;
    bool background() const { return m_background; }
    // Ends a session playing behind the menus.
    Q_INVOKABLE void stopBackground();
    // Called by a player right after loadAndPlay(): how to take the session
    // back to full screen once it plays behind the menus, as
    // { module, title, params }, params being the player view's navParams.
    // Choosing the main menu's row for it opens the module's player with
    // them, which takes the session back (takeBack), or calls loadAndPlay()
    // with its very command line, which does the same. Every loadAndPlay()
    // clears it, so a player that notes nothing leaves no row.
    Q_INVOKABLE void noteSession(const QVariantMap &note);
    QVariantMap backgroundNote() const { return m_background ? m_sessionNote : QVariantMap(); }
    // Its player's menu left the session with Browse, an mpv process having
    // ended for the menu (Transparent Background off): it stopped there, where
    // its player saved it, and its note stays as leftNote until the next
    // loadAndPlay(). Its player, opened again with the note's params, starts
    // it where it was saved without asking.
    Q_INVOKABLE void leaveSession();
    QVariantMap leftNote() const { return m_leftNote; }
    // The session behind the menus, full screen again where it is, for the
    // player it belongs to (backgroundNote says whose it is): false when
    // there is none, and the player starts its video as it would any.
    // startSeconds is the player's resume point, which reattach() weighs.
    Q_INVOKABLE bool takeBack(float startSeconds);
    // A player with a playback menu of its own says so in its note (menu:
    // true): back then has it open the menu, the session staying its
    // player's, where any other player takes back as stopped and goes back
    // to its menus. Played inside the window (Transparent Background), the
    // video goes on under the menu: playerMenuRequested() opens it,
    // closePlayerMenu() takes the picture back to full screen, and
    // leavePlayerMenu() does what back always did, for the menu's way to its
    // module's browser: playbackEnded "stopped", the video playing on behind.
    // Played by an mpv process, which has the screen while it plays, the
    // video ends for the menu (backFromProcess): playbackEnded with reason
    // "menu", nothing playing under it, and the player starts it again where
    // it was as the menu closes.
    Q_INVOKABLE void closePlayerMenu();
    Q_INVOKABLE void leavePlayerMenu();
    // Sets a property of the session that plays, for a setting changed while
    // it plays: speed, panscan, loop-playlist and the like. Without a session
    // (a player's menu where its process ended) it does nothing: the next
    // one starts with the setting.
    Q_INVOKABLE void setVideoProperty(const QString &name, const QVariant &value);
    // Settings → Audio Output changed: a video playing behind the menus moves
    // to the card chosen at once (AudioOutput).
    void followAudioOutput();
    // The embedded session's newest picture, and the size to draw it at
    // (VideoSurface).
    QImage videoFrame() const;
    // The embedded session is drawn on the GPU, and its newest picture
    // there, taken as the scene graph shows it (VideoSurface).
    bool videoOnGpu() const;
    EmbeddedMpv::GpuFrame videoGpuFrame();
    void setVideoTargetSize(const QSize &size);

    // Which display fullscreen playback should open on, matching the UI's
    // app-level "display_index" (index into QGuiApplication::screens(), plus
    // that screen's QScreen::name()). main.cpp calls this once at startup;
    // index 0 (the default) adds no mpv args at all. Desktop launches only —
    // the headless VT-handoff path is unaffected.
    void setTargetDisplay(int index, const QString &screenName);

signals:
    void positionChanged(int ms);
    void durationChanged(int ms);
    void playlistPosChanged(int pos);
    // Emitted exactly once when mpv exits, with the reason it ended:
    //   "eof"     — file played to its natural end. (What a module does with this
    //               is its own concern.  as an example: Plex may autoplay the next episode)
    //   "stopped" — user quit/stopped before the end (also the safe default for a
    //               crash/kill with no end-file event).
    //   "failed"  — mpv exited with an error (code 2 — file could not be played;
    //               Up to the module as to when/how to use; for example Plex retries when transcoding).
    //   "menu"    — the process ended for its player's menu (back, for a player
    //               whose note says menu: true, see closePlayerMenu): the player
    //               opens it, and starts the video again where it was as it closes.
    // A single signal (rather than one per reason) is deliberate: a Player view
    // connects one handler and branches on `reason`, so it can never silently drop
    // a case the way an unhandled per-reason signal would.
    void playbackEnded(int finalPositionMs, int finalDurationMs, const QString &reason);

    void skipRequested();
    void videoActiveChanged();
    void backgroundChanged();
    void leftNoteChanged();
    void playerMenuRequested();
    void videoFrameReady();
    // The OSC's SUBTITLE button when the sub is burned into the stream and mpv
    // has nothing to cycle (see `sub-cycle` in scripts/mpv-osc.lua). The module
    // owns the change — typically stop, re-request the stream, relaunch.
    void subtitleCycleRequested();
    // The OSC's AUDIO button when the track is baked into the stream and mpv has
    // nothing to cycle (see `audio-cycle` in scripts/mpv-osc.lua). As above, the
    // module owns the change.
    void audioCycleRequested();

private slots:
    void onProcessFinished();
    void tryConnectIpc();
    void onIpcReadyRead();

private:
    // Hardware video-decode profile, detected once from /proc/device-tree/model.
    enum class VideoProfile { Pi3, Pi4, PiFullKms, Generic };

    void sendCommand(const QJsonArray &args);
    // The mpv options for a session, up to how its picture is shown; what
    // plays comes after them, behind a "--".
    QStringList sessionArgs(const QString &url, float startSeconds, int audioTrack, int subTrack,
                            const QStringList &subFiles, const QStringList &subLangs, bool loop,
                            int playlistStart, float transcodeOffsetSec, const QString &plexToken,
                            bool muteAudio, const QString &oscMode, bool shuffle,
                            const QStringList &subTitles, float imageDurationSec, bool imageContent,
                            const QStringList &extraArgs, const QString &jellyfinToken, bool embedded);
    // An mpv process for a session: args its options, media what plays.
    void startProcess(QStringList args, const QStringList &media);
    // The session loadAndPlay() asked for (serial), started once the screen
    // is free (screenBusy()).
    void launchAfterRetirement(int serial, const QStringList &args,
                               const QStringList &media, bool embedded);
    // stop() before that session started: its player is told it stopped once
    // the screen is free, and back with the app.
    void stopAfterRetirement(int serial, int positionMs);
    // True while the next session must wait: the process loadAndPlay()
    // retired for it is still on its way out, or the screen is being handed
    // back to the app (handBackScreen()). A retired process that outlives its
    // kill is given up on after a few seconds, so that the menus come back.
    bool screenBusy();
    // The screen back from the mpv process that had it, then `then`. Headless,
    // after DisplayHandoff's settle delay, as when a process ends; else at once.
    void handBackScreen(std::function<void()> then);
    // Transparent Background is on, and libmpv is there to play inside the app.
    bool transparentBackground() const;
    // The decode and drawing flags for a session played inside the app, as
    // it is drawn: on the GPU (gpu), or by the software renderer.
    void appendEmbeddedVideoArgs(QStringList &args, bool gpu) const;
    // The player inside the app, made when first needed.
    EmbeddedMpv *embeddedPlayer();
    void startEmbedded(QStringList args);
    // Ends an embedded session without a word to its module: as a process
    // replaced by the next is, or one left playing behind the menus.
    void endEmbedded();
    void onEmbeddedFinished(const QString &lastEndReason);
    // Back during an embedded session: the menus come back over the picture.
    void detachToMenus();
    // Back during a session in an mpv process: it ends, its player's menu
    // opening in its place if it has one.
    void backFromProcess();
    // The session behind the menus chosen again: full screen, where it is.
    void reattach(float startSeconds);
    VideoProfile detectVideoProfile() const;
    // Appends the profile-specific --vo/--gpu-context/--hwdec flags (honouring the
    // app-level "mpv_video_args" override) to a forming mpv argument list.
    void appendVideoArgs(QStringList &args) const;
    // App-level "smooth_playback" setting (default ON). On the Pi 3 this selects the
    // smooth zero-copy overlay path; turning it OFF restores the crop-capable scaler path.
    bool smoothPlaybackEnabled() const;
    // App-level "auto_crop" setting (default OFF), from before Scaling: its ON
    // reads as Pan & Scan while no Scaling has been chosen.
    bool autoCropEnabled() const;
    // The Scaling in force ("Letterbox", "14:9", "Pan & Scan", "Anamorphic"):
    // the active module's own "video_scaling", unless it is "Default", then the
    // app's.
    QString videoScaling() const;
    // True when the active decode path can't crop (Pi 3 overlay path with smooth
    // playback ON): --panscan blanks the video there. Gates auto-crop and tells
    // the OSC scripts to hide their CROP button.
    bool cropUnavailable() const;
    // App-level "video_output_levels" setting (default "Auto"). Returns the mpv
    // value for --video-output-levels ("limited"/"full"), or empty on Auto/unset.
    QString videoOutputLevels() const;

    // Owner token handed to DisplayHandoff, so the app can tell who has the screen.
    static constexpr const char *kHandoffOwner = "mpv";

    AppCore        *m_appCore      = nullptr;
    QString         m_activeModule;
    EmbeddedMpv    *m_embedded     = nullptr;
    bool            m_background   = false;
    int             m_detachPositionMs = 0;
    // The embedded session's command line, to know it when it is asked for again.
    QStringList     m_sessionArgs;
    QVariantMap     m_sessionNote;
    // The session left with Browse (leaveSession).
    QVariantMap     m_leftNote;
    // Back has its player's menu open over the picture (see closePlayerMenu).
    bool            m_playerMenu   = false;
    // The process is quitting for its player's menu (backFromProcess).
    bool            m_menuOnExit   = false;
    // loadAndPlay() starts its session a tick later (see there): the
    // session asked for last, and whether one is on its way, starting where.
    int             m_launchSerial = 0;
    bool            m_launchPending = false;
    int             m_pendingStartMs = 0;
    QSize           m_videoTargetSize { 640, 480 };
    QString         m_embeddedInputConfPath;
    DisplayHandoff *m_handoff      = nullptr;
    VideoProfile  m_videoProfile  = VideoProfile::Generic;
    QProcess     *m_process        = nullptr;
    QPointer<QProcess> m_retiringProcess;
    QElapsedTimer m_retireClock;           // since m_retiringProcess was told to quit
    bool          m_handingBack    = false; // handBackScreen() under way
    QLocalSocket *m_ipc            = nullptr;
    bool          m_ipcWanted     = false;
    QTimer       *m_connectTimer   = nullptr;
    QTimer       *m_watchdogTimer  = nullptr;
    qint64        m_lastIpcEventMs = 0;
    bool          m_paused         = false;  // mirrors mpv's pause property (watchdog exemption)
    QString       m_appRoot;
    QString       m_dataRoot;
    QString       m_socketPath;
    QString       m_inputConfPath;
    QString       m_logFilePath;
    QString       m_subInfoPath;       // JSON map: external sub URL -> friendly name (for the OSC)
    QString       m_logoOverlayPath;   // Settings → Logo Image read as mpv's raw overlay (mpv-logo.lua)
    QString       m_lastEndFileReason;  // mpv end-file "reason" for the current session
    // Set when this session passed --start; cleared once mpv has applied it. See
    // onIpcReadyRead's playback-restart handling for why the option can't just stay set.
    bool          m_pendingStartClear = false;
    int           m_position     = 0;
    int           m_duration     = 0;
    int           m_playlistPos  = -1;
    bool          m_headlessMode = false;
    int           m_displayIndex = 0;   // see setTargetDisplay()
    QString       m_displayScreenName;
    int           m_previousVt   = -1;
    bool          m_hasMpvOscScript     = false;
    bool          m_hasAmbientOscScript = false;
    bool          m_hasMediaKeysScript  = false;
};
