#pragma once
#include <QObject>
#include <QString>
#include <functional>

class QTimer;

#ifdef Q_OS_LINUX
#include <xf86drm.h>
#include <xf86drmMode.h>

struct DrmSavedState {
    uint32_t crtcId      = 0;
    uint32_t connectorId = 0;
    uint32_t fbId        = 0;
    int      x           = 0;
    int      y           = 0;
    drmModeModeInfo mode = {};
    bool     valid       = false;
};

// A frame buffer of the hand-off's own (a dumb buffer, XRGB8888, mapped), the
// cover: what the screen shows while the child starts and what the child
// puts back as it ends (see acquire()).
struct DrmCover {
    int      fd     = -1;
    uint32_t handle = 0;
    uint32_t fbId   = 0;
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t pitch  = 0;
    uint64_t size   = 0;
    unsigned char *pixels = nullptr;
    bool     shown  = false;    // on the CRTC as this hand-off began
    bool     black  = false;
};
#endif

// Brackets a fullscreen external process on headless Linux (RPi Lite / EGLFS),
// where Qt owns the DRM device and a child that wants the screen can't simply
// open a window over ours.
//
// One instance owns the hand-off for the whole app, so two subsystems can never
// both believe they hold the screen: acquire() takes an owner token and refuses
// if a different owner already holds it.
//
// The ORDER HERE IS LOAD-BEARING and was established against real Pi hardware —
// do not reorder it, and do not re-implement these ioctls in a new caller:
//
//   acquire():  VT switch  ->  drmDropMaster  ->  save CRTC state
//     The VT switch goes FIRST because it suspends Qt's render thread via the
//     kernel's VT-switch signal before DRM master is dropped. On kernels 5.8+
//     drmSetMaster() returns EACCES for non-root while any other process holds
//     master, and Qt EGLFS runs VT_AUTO and never calls drmDropMaster() itself.
//
//     Then the cover (with a cover painter set): master taken again for a
//     moment, the CRTC set to a frame buffer of our own holding the app's
//     last frame, master dropped. A frame Qt draws without master is never
//     shown (its commit fails with EACCES, and Qt 6.8 waits for no flip
//     after a failed commit).
//
//   release():  drmSetMaster  ->  restore CRTC (and clear the cursor)  ->  VT switch back
//     Exactly the inverse. The CRTC restore uses LEGACY drmModeSetCrtc, not an
//     atomic commit: the child's atomic cleanup leaves CRTC_ACTIVE=0, and EGLFS
//     then gets EINVAL on its first page flip. It is restored with the cover,
//     black by then, where there is one, and with Qt's last frame buffer
//     otherwise. Before that SetCrtc, every other plane still attached to the
//     CRTC is disabled: RetroArch and SDL's KMS backends leave overlay planes
//     armed, and EGLFS's next atomic commit then fails with EINVAL forever —
//     a black screen after Coin+Start on a Pi 5 JAMMA DPI cabinet.
//     The default 200 ms defer lets the child's last pending KMS commit clear in
//     the vc4 driver; commit too early and EGLFS gets EBUSY repeatedly, drops its
//     DRM pipeline, and the kernel falls back to the text console on Qt's VT.
//     200 ms is more than three VSync periods at 60 Hz. On a Pi 5 DPI output
//     (drm-rp1-dpi, OSDOS_DRM_CONNECTOR naming DPI) the pipe disable/enable at
//     child exit needs longer — releaseDeferred waits at least 500 ms there
//     unless OSDOS_HANDOFF_SETTLE_MS sets the wait.
//
//   The cover: a child that sets a mode of its own (SDL's KMS/DRM backend,
//     RetroArch's, mpv's) saves the CRTC as it found it and sets it back as it
//     ends, a moment before it exits. That was Qt's last frame: the loading
//     screen the game or the video started behind, put back on the screen
//     at the end and shown until the app drew again. With the cover it finds
//     a frame buffer of ours instead, which shows the same picture while it
//     starts and is blacked out once its own picture is up, so that what it
//     puts back as it ends is black. main.cpp then releaseResources()+update()
//     so EGLFS rebuilds against the restored CRTC.
//
// On macOS and on desktop Linux (X11/Wayland present) there is no DRM/VT work
// — the child just opens a window over ours — so acquire() still returns 0,
// but it still records the owner so held/heldChanged track a logical hand-off
// (InputManager can release its SDL pads for the child).
class DisplayHandoff : public QObject {
    Q_OBJECT
    // True while a child has the screen (a hand-off is held). On headless
    // Linux nothing Qt draws meanwhile reaches it; everywhere, QML reads it as
    // root.screenHandedOff (Main.qml), so that animations rest until it is
    // back, leaving the CPU to the child (LoadingScreen).
    Q_PROPERTY(bool held READ held NOTIFY heldChanged)
public:
    explicit DisplayHandoff(QObject *parent = nullptr);
    ~DisplayHandoff() override;

    bool held() const { return !m_owner.isEmpty(); }

    // True when Qt owns the display directly (EGLFS/DRM) rather than through a
    // compositor, which is the only case that needs a hand-off.
    static bool isHeadless();

    // True while this owner holds a hand-off, whether or not it found a VT to
    // return to. Callers use it as a re-entrancy guard: acquiring twice without
    // releasing would overwrite the saved VT with the free one we just switched
    // to, and look for Qt's DRM fd after its master is already dropped.
    bool    isHeldBy(const QString &owner) const;
    QString currentOwner() const { return m_owner; }

    // False when the CRTC state captured by acquire() is unusable, meaning the
    // post-exit restore will NOT be able to put the display back. A caller that
    // can decline (a script launcher, which would otherwise strand the user on a
    // black screen with no way home) should check this and refuse; mpv
    // deliberately does not, because playback has always proceeded in this case.
    //
    // ONLY MEANINGFUL WHEN acquire() RETURNED > 0. It is also false on macOS and
    // on desktop Linux, where there is no CRTC to save and nothing to restore —
    // treating that as "refuse" would break every non-headless target.
    bool    savedStateValid() const;

    // Returns the VT the child now has to itself (>0), or 0 when no hand-off was
    // needed (non-Linux, or a compositor is present). Returns -1 when refused
    // because a different owner already holds the screen.
    int  acquire(const QString &owner);

    // Restores the display after delayMs (raised for a Pi 5 DPI output — see
    // the header comment on release), then invokes onRestored. The callback
    // always runs — even if this owner never held the screen — because callers
    // use it to emit their "finished" signal, and dropping it would leave a view
    // focused over a defunct process with the app looking frozen.
    void releaseDeferred(const QString &owner, std::function<void()> onRestored,
                         int delayMs = 200);

    // Synchronous restore with no callback, for shutdown paths. Cancels any
    // pending deferred release (discarding its callback, since the object that
    // would receive it is being torn down).
    void releaseNow(const QString &owner);

    // Paints the app's frame as it is now into a cover of width x height
    // (32-bit XRGB pixels, pitch bytes a line), for a hand-off on headless
    // Linux (see the cover above); false when it can't, the hand-off then
    // going on without a cover. main.cpp sets it from the QML window.
    using CoverPainter = std::function<bool(int width, int height, unsigned char *pixels, int pitch)>;
    void setCoverPainter(CoverPainter painter) { m_coverPainter = std::move(painter); }

signals:
    // At the start of every acquire(), on every platform (where nothing is
    // handed over too): something is about to take the screen. A video left
    // playing behind the menus ends then (MpvController).
    void handingOff(const QString &owner);
    void heldChanged();

private:
    int  getActiveVt() const;
    // Never returns activeVt — switching to the VT we are already on is a silent
    // no-op that leaves Qt drawing with DRM master dropped. See the definition.
    int  findFreeVt(int activeVt) const;
    int  findQtDrmFd() const;
#ifdef Q_OS_LINUX
    // Our own console in preference to /dev/tty0 — see the comment on the
    // definition; using /dev/tty0 makes the switch BACK fail with EPERM.
    int  openVtControlFd() const;
#endif
    // False when the switch could not be performed (typically no permission on
    // /dev/tty0). The caller must know, because a hand-off without the VT switch
    // leaves Qt's renderer running while DRM master is dropped.
    bool switchToVt(int vt);
#ifdef Q_OS_LINUX
    void saveDrmCrtcState(int fd);
    // useCover: the CRTC set to the cover (blacked out first) where one was
    // shown, else, or should that fail, to Qt's last frame buffer.
    void restoreDrmCrtcState(int fd, bool useCover);
    // Disable every plane still on crtcId except one whose FB is keepFbId
    // (0: disable all). Leaves EGLFS's atomic commit able to flip again.
    void disableExtraPlanes(int fd, uint32_t crtcId, uint32_t keepFbId);
    // Mode from the connector that matches the saved one, else the saved mode.
    drmModeModeInfo connectorMode(int fd) const;
    // The CRTC set to a cover painted with the app's frame, master taken
    // for it and dropped again (acquire()).
    void showCover(int fd);
    bool ensureCover(int fd, uint32_t width, uint32_t height);
    void blackenCover();
    // Blacked out once the child's own picture is up (m_coverTimer).
    void checkCover();
#endif
    // How long releaseDeferred actually waits: at least delayMs, longer on
    // drm-rp1-dpi, or OSDOS_HANDOFF_SETTLE_MS when set.
    int settleDelayMs(int delayMs) const;
    // useCover false for shutdown: Qt's own last frame buffer put back.
    void doRestore(bool useCover = true);

    QString m_owner;                    // empty when not handed off
    int     m_previousVt   = -1;
    bool    m_switchedVt   = false;     // did the VT switch actually succeed?
    int     m_qtDrmFd      = -1;
    QTimer *m_releaseTimer = nullptr;
    QTimer *m_coverTimer   = nullptr;
    std::function<void()> m_onRestored;
    CoverPainter m_coverPainter;
#ifdef Q_OS_LINUX
    DrmSavedState m_savedDrm = {};
    DrmCover      m_cover    = {};
#endif
};
