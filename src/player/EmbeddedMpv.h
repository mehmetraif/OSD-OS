#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSize>
#include <QStringList>
#include <atomic>

class QOffscreenSurface;
class QOpenGLContext;
class QThread;
struct mpv_handle;
struct mpv_render_context;

// mpv played inside OSD/OS's own window rather than as a process of its own,
// for the Transparent Background setting: the menus can then be drawn over the
// picture while it plays (MpvController, VideoSurface).
//
// libmpv is opened at run time (libmpv.so.2 / libmpv.2.dylib), so the app
// still starts where it isn't installed; available() says whether it is.
//
// The picture is drawn on the GPU where Qt Quick draws with OpenGL: on a
// thread of its own, with a context that shares its objects with the scene
// graph's (Qt::AA_ShareOpenGLContexts), mpv converts and scales into one of a
// few textures, which VideoSurface shows as they are, and the hardware decoder
// hands it frames without a copy. Elsewhere (Metal on macOS, the software
// scene graph, OpenGL drawn on the CPU), or should that fail, libmpv's
// software renderer draws it on the CPU, on a thread of its own, at the size
// the VideoSurface asks for.
//
// A session takes an mpv command line (start(args), the one MpvController
// builds) and turns it into options and a playlist. It ends when mpv quits,
// or its playlist has played out: finished(lastEndReason), with the reason the
// last file ended ("eof", "quit", "stop", "error", "redirect").
//
// The GPU draws the menus too. Pictures that take long to draw (a 4K film
// brought down to a CRT's lines) hold their frames up, so the GPU renderer
// times each one and, when they take too long for a while, turns mpv's
// scaling down a step, twice at most, saying so in the journal; the sessions
// after start at that step (GpuRenderer::noteDrawTime()).
class EmbeddedMpv : public QObject {
    Q_OBJECT
public:
    // A picture drawn on the GPU: an OpenGL texture the scene graph's context
    // can sample, its size, and its number, new for each picture.
    struct GpuFrame {
        unsigned texture = 0;
        QSize size;
        quint64 serial = 0;
    };

    // libmpv can be loaded, with everything this needs from it.
    static bool available();

    explicit EmbeddedMpv(QObject *parent = nullptr);
    ~EmbeddedMpv() override;

    // Sessions are drawn on the GPU: Qt Quick draws with OpenGL, its contexts
    // share their objects, and a GPU draws them, not the CPU (Mesa's llvmpipe,
    // unless OSDOS_EMBEDDED_RENDER=gpu; OSDOS_EMBEDDED_RENDER=sw says no to
    // any). Decided when first asked, before the first session, so that its
    // command line is made for where it is drawn; no from then on should
    // drawing there fail.
    bool gpuAvailable();

    // Starts playing what an mpv command line asks for. False if libmpv
    // could not be set up (finished() does not follow then). A core still
    // retiring (below) is waited for first.
    bool start(const QStringList &args);
    // Ends the session at once, without finished(): its core is gone, its
    // threads joined and its devices closed, before this returns.
    void stop();
    // Ends the session as stop() does, but its core goes on a thread of its
    // own: destroying it joins the demuxer, the decoder, the sound output,
    // the scripts and the control connection, which takes a film on a Pi a
    // while, and the GUI thread stood still for it (a loading screen with
    // it). retiring() says so until it has gone, then retired(). The sound
    // card and the decoder are the core's until then, so the next session
    // waits for it (MpvController::screenBusy()); start() and stop() wait
    // for it themselves.
    void retire();
    bool retiring() const { return m_retiring.load(); }
    // How long the core has been retiring, in milliseconds; 0 with none.
    int retiringMs() const;
    // Gives up on a retiring core that won't go (stuck in a driver's call,
    // or on a mount gone away): left to finish by itself, should it ever,
    // and retiring() is false from here, so that the next session can
    // start. stop() gives up by itself after kRetireWaitMs.
    void abandonRetirement();
    static constexpr int kRetireWaitMs = 5000;
    // Asks mpv to quit, as its own quit command does: finished() follows.
    void quit();
    bool running() const { return m_mpv != nullptr; }
    // This session is drawn on the GPU (gpuFrame()), not into frame().
    bool gpuRendering() const { return m_gpu; }

    // The picture's size in device pixels; it is drawn again at a new size.
    void setTargetSize(const QSize &size);
    // Whether the picture is on screen. Hidden (the VideoSurface behind a
    // solid ground, say), each picture is drawn a few pixels wide, so that
    // neither mpv's renderer nor the GPU works for no one, while mpv plays
    // on; shown again, the latest is drawn whole at once.
    void setShown(bool shown);
    // The newest picture, or a null image before the first.
    QImage frame() const;
    // The newest picture drawn on the GPU, none before the first. Asked on
    // the scene graph's render thread as it is shown: the texture stays as it
    // is while it may be on screen, until two more have been asked for.
    GpuFrame gpuFrame();

signals:
    void frameReady();
    void finished(const QString &lastEndReason);
    // A retiring core has gone (retire()).
    void retired();

private:
    class Renderer;
    class GpuRenderer;

    static void onWakeup(void *self);
    void drainEvents();
    // The session's core taken down before this returns: retire(), waited
    // for.
    void teardown();
    // A retiring core gone before this returns.
    void waitForRetirement();
    // The retiring core numbered serial has gone.
    void retirementDone(int serial);
    // On the GUI thread, from the GPU renderer: the scaling turned down to
    // its level-th step, the pictures having taken tookMs each.
    void drawCheaper(int level, double tookMs);
    // The GPU's thread and context, made by the first gpuAvailable() and
    // kept to the end: false, with none, where sessions aren't drawn there.
    bool setUpGpu();
    void shutdownGpu();

    mpv_handle         *m_mpv    = nullptr;
    mpv_render_context *m_render = nullptr;
    QThread            *m_thread   = nullptr;
    Renderer           *m_renderer = nullptr;
    std::atomic<bool>   m_drainQueued { false };
    QString             m_lastEndReason;
    // The thread destroying a retired core (retire()), none between; each
    // numbered, so a late word from an earlier one counts for nothing.
    std::atomic<bool>   m_retiring { false };
    QThread            *m_retireThread = nullptr;
    int                 m_retireSerial = 0;
    QElapsedTimer       m_retiringSince;

    // The GPU's: kept from one session to the next.
    QThread           *m_gpuThread   = nullptr;
    GpuRenderer       *m_gpuRenderer = nullptr;
    QOpenGLContext    *m_gpuContext  = nullptr;
    QOffscreenSurface *m_gpuSurface  = nullptr;
    // This session is drawn on the GPU; none are (declined, or it failed).
    bool m_gpu = false;
    bool m_gpuOff = false;
    // The GPU didn't take a hardware decoder's frames, so sessions decode
    // with the copy-back modes in their --hwdec; this session's, to turn to
    // should it not.
    bool m_gpuCopyBack = false;
    QString m_copyBackHwdec;

    mutable QMutex m_frameLock;
    QImage         m_frame;
    QSize          m_targetSize { 640, 480 };
    bool           m_shown = true;
    // The size the renderers draw at: m_targetSize, or a few pixels while
    // the picture is hidden. Under m_frameLock.
    QSize drawSize() const;
    // Under m_frameLock: the GPU's newest picture and its texture's place,
    // and the places of the two the scene graph took last, kept as they are.
    GpuFrame m_gpuPublished;
    int      m_gpuPublishedIndex = -1;
    int      m_gpuShownIndex     = -1;
    int      m_gpuPreviousIndex  = -1;
};
