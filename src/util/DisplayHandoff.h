#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>

class QTimer;

#ifdef Q_OS_LINUX
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <string>
#include <vector>

// One settable property of a KMS object (the connector, the CRTC or one of
// its planes) as Qt had it when a hand-off began.
struct DrmPropValue {
    uint32_t    object = 0;
    uint32_t    prop   = 0;
    uint64_t    value  = 0;
    std::string name;
};

struct DrmSavedState {
    uint32_t crtcId      = 0;
    uint32_t connectorId = 0;
    uint32_t fbId        = 0;
    int      x           = 0;
    int      y           = 0;
    drmModeModeInfo mode = {};
    bool     valid       = false;
    // With Qt on atomic KMS (QT_QPA_EGLFS_KMS_ATOMIC): the plane showing
    // Qt's frames, the CRTC's MODE_ID property, and every settable property
    // of the connector, the CRTC and its planes as Qt had them, which the
    // hand-back commits again whole (restoreAtomic()).
    uint32_t planeId     = 0;
    uint32_t modeProp    = 0;
    std::vector<DrmPropValue> props;
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
//     Exactly the inverse. The child's cleanup leaves the CRTC off
//     (CRTC_ACTIVE=0) or as the child had it, and EGLFS, which sets the mode
//     only on its first frame and from then on only flips a frame buffer onto
//     its plane, gets EINVAL on every flip after. With Qt on atomic KMS the
//     restore is one atomic commit of what Qt had as the hand-off began
//     (saved by acquire()): the mode, the CRTC on, and every settable property
//     of the connector, the CRTC and its planes; LEGACY drmModeSetCrtc when
//     that is refused, and with Qt on legacy KMS. Either restores the cover,
//     black by then, where there is one, and Qt's last frame buffer otherwise.
//
//   After the hand-back: Qt's flips counted for a few seconds (its own
//     "Failed to commit atomic request" warnings against the frames it
//     draws). When every frame it draws is refused, the screen is lost to it
//     for good, as a Pi 5 on its DPI output showed (EINVAL on every flip after
//     a game): the state of the screen goes to the log, and screenLost()
//     starts the app again (main.cpp), which sets its mode afresh.
//     The default 200 ms defer lets the child's last pending KMS commit clear in
//     the vc4 driver; commit too early and EGLFS gets EBUSY repeatedly, drops its
//     DRM pipeline, and the kernel falls back to the text console on Qt's VT.
//     200 ms is more than three VSync periods at 60 Hz.
//
//   The cover: a child that sets a mode of its own (SDL's KMS/DRM backend,
//     RetroArch's, mpv's) saves the CRTC as it found it and sets it back as it
//     ends, a moment before it exits. That was Qt's last frame: the loading
//     screen the game or the video started behind, put back on the screen
//     at the end and shown until the app drew again. With the cover it finds
//     a frame buffer of ours instead, which shows the same picture while it
//     starts and is blacked out once its own picture is up, so that what it
//     puts back as it ends is black.
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

    // Restores the display after delayMs, then invokes onRestored. The callback
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

    // A frame Qt has drawn and handed to the screen, from any thread (the
    // render thread's QQuickWindow::frameSwapped, main.cpp): what Qt's
    // refused flips are counted against after a hand-back.
    void noteFrame() { m_frames.fetch_add(1, std::memory_order_relaxed); }

signals:
    // At the start of every acquire(), on every platform (where nothing is
    // handed over too): something is about to take the screen. A video left
    // playing behind the menus ends then (MpvController).
    void handingOff(const QString &owner);
    void heldChanged();
    // After a hand-back, every frame Qt drew was refused by the screen: the
    // menus run on unseen, and only a fresh start of the app sets its mode
    // again (main.cpp starts it again in place).
    void screenLost();

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
    // Qt's own values of every settable property of object (connector, CRTC
    // or plane), for restoreAtomic().
    void saveProps(int fd, uint32_t object, uint32_t type);
    // useCover: the CRTC set to the cover (blacked out first) where one was
    // shown, else, or should that fail, to Qt's last frame buffer.
    void restoreDrmCrtcState(int fd, bool useCover);
    // What Qt had as the hand-off began, committed again whole with fbId on
    // its plane; false (nothing changed) when there is nothing saved or the
    // commit is refused.
    bool restoreAtomic(int fd, uint32_t fbId);
    // Qt's flips since the hand-back: screenLost() when every frame it drew
    // was refused (m_verifyTimer, for a few seconds after a hand-back).
    void checkFramesShown();
    // The CRTC, Qt's plane and its frame buffers as the kernel has them, and
    // whether it would take a flip of each, into the log.
    void logScreenState(int fd) const;
    // The CRTC set to a cover painted with the app's frame, master taken
    // for it and dropped again (acquire()).
    void showCover(int fd);
    bool ensureCover(int fd, uint32_t width, uint32_t height);
    void blackenCover();
    // Blacked out once the child's own picture is up (m_coverTimer).
    void checkCover();
#endif
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
    std::atomic<quint64> m_frames { 0 };
#ifdef Q_OS_LINUX
    DrmSavedState m_savedDrm = {};
    DrmCover      m_cover    = {};
    // The hand-back being watched (checkFramesShown()): Qt's DRM file and
    // CRTC, the frames drawn and the flips refused before it, the checks left.
    QTimer  *m_verifyTimer   = nullptr;
    int      m_verifyFd      = -1;
    uint32_t m_verifyCrtc    = 0;
    quint64  m_framesBefore  = 0;
    quint64  m_refusedBefore = 0;
    int      m_checksLeft    = 0;
#endif
};
