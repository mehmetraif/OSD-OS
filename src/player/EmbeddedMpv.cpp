#include "EmbeddedMpv.h"
#include "../util/Diagnostics.h"
#include "../util/LegacyNames.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QLibrary>
#include <QMap>
#include <QMutexLocker>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QSemaphore>
#include <QThread>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <vector>

#ifdef OSDOS_EMBEDDED_MPV
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

// OpenGL ES 2's headers don't name it.
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

namespace {

// libmpv's entry points, resolved once from whichever copy is installed. It is
// opened rather than linked so the app runs where it is missing, and never
// unloaded: mpv's threads can outlive a session's last call into it.
struct MpvApi {
    decltype(&mpv_create)                             create = nullptr;
    decltype(&mpv_initialize)                         initialize = nullptr;
    decltype(&mpv_set_option_string)                  setOptionString = nullptr;
    decltype(&mpv_set_option)                         setOption = nullptr;
    decltype(&mpv_command)                            command = nullptr;
    decltype(&mpv_command_async)                      commandAsync = nullptr;
    decltype(&mpv_wait_event)                         waitEvent = nullptr;
    decltype(&mpv_set_wakeup_callback)                setWakeupCallback = nullptr;
    decltype(&mpv_request_log_messages)               requestLogMessages = nullptr;
    decltype(&mpv_terminate_destroy)                  terminateDestroy = nullptr;
    decltype(&mpv_error_string)                       errorString = nullptr;
    decltype(&mpv_render_context_create)              renderCreate = nullptr;
    decltype(&mpv_render_context_set_update_callback) renderSetUpdateCallback = nullptr;
    decltype(&mpv_render_context_update)              renderUpdate = nullptr;
    decltype(&mpv_render_context_render)              renderRender = nullptr;
    decltype(&mpv_render_context_free)                renderFree = nullptr;
    bool ok = false;
};

QStringList libraryCandidates() {
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    // Homebrew's prefixes aren't on the loader's default path.
    return { appDir + QStringLiteral("/../Frameworks/libmpv.2.dylib"),
             QStringLiteral("/opt/homebrew/lib/libmpv.2.dylib"),
             QStringLiteral("/usr/local/lib/libmpv.2.dylib"),
             QStringLiteral("libmpv.2.dylib") };
#elif defined(Q_OS_WIN)
    // Beside the app, as mpv's own builds name it, or on the PATH.
    return { appDir + QStringLiteral("/libmpv-2.dll"), appDir + QStringLiteral("/mpv/libmpv-2.dll"),
             QStringLiteral("libmpv-2.dll"), QStringLiteral("mpv-2.dll") };
#elif defined(Q_OS_BSD4)
    // The package's library, in /usr/local/lib, which the loader doesn't look
    // in by itself on OpenBSD, and which OpenBSD numbers in its own way
    // (libmpv.so.3.0 for libmpv 2): the newest of what is there, then the
    // soname mpv's own build gives it.
    const QDir lib(QStringLiteral("/usr/local/lib"));
    QStringList candidates;
    const QStringList names = lib.entryList({ QStringLiteral("libmpv.so.*") }, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QString &name : names)
        candidates << lib.filePath(name);
    return candidates << QStringLiteral("libmpv.so.2");
#else
    // A copy bundled beside the binary (the AppImage's lib/) first.
    return { appDir + QStringLiteral("/../lib/libmpv.so.2"),
             QStringLiteral("libmpv.so.2"), QStringLiteral("libmpv.so.1") };
#endif
}

const MpvApi &api() {
    static const MpvApi loaded = [] {
        MpvApi a;
        static QLibrary lib;
        for (const QString &name : libraryCandidates()) {
            lib.setFileName(name);
            if (lib.load())
                break;
        }
        if (!lib.isLoaded()) {
            qInfo("[EmbeddedMpv] libmpv not found: Transparent Background is unavailable");
            return a;
        }
        bool all = true;
        auto resolve = [&all](auto &fn, const char *name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(lib.resolve(name));
            if (!fn) {
                qWarning("[EmbeddedMpv] %s has no %s", qPrintable(lib.fileName()), name);
                all = false;
            }
        };
        resolve(a.create, "mpv_create");
        resolve(a.initialize, "mpv_initialize");
        resolve(a.setOptionString, "mpv_set_option_string");
        resolve(a.setOption, "mpv_set_option");
        resolve(a.command, "mpv_command");
        resolve(a.commandAsync, "mpv_command_async");
        resolve(a.waitEvent, "mpv_wait_event");
        resolve(a.setWakeupCallback, "mpv_set_wakeup_callback");
        resolve(a.requestLogMessages, "mpv_request_log_messages");
        resolve(a.terminateDestroy, "mpv_terminate_destroy");
        resolve(a.errorString, "mpv_error_string");
        resolve(a.renderCreate, "mpv_render_context_create");
        resolve(a.renderSetUpdateCallback, "mpv_render_context_set_update_callback");
        resolve(a.renderUpdate, "mpv_render_context_update");
        resolve(a.renderRender, "mpv_render_context_render");
        resolve(a.renderFree, "mpv_render_context_free");
        a.ok = all;
        if (all)
            qInfo("[EmbeddedMpv] using %s", qPrintable(lib.fileName()));
        return a;
    }();
    return loaded;
}

QString endReasonName(int reason) {
    switch (reason) {
    case MPV_END_FILE_REASON_EOF:      return QStringLiteral("eof");
    case MPV_END_FILE_REASON_STOP:     return QStringLiteral("stop");
    case MPV_END_FILE_REASON_QUIT:     return QStringLiteral("quit");
    case MPV_END_FILE_REASON_ERROR:    return QStringLiteral("error");
    case MPV_END_FILE_REASON_REDIRECT: return QStringLiteral("redirect");
    }
    return {};
}

void freePixels(void *pixels) {
    std::free(pixels);
}

// The copy-back modes in an --hwdec list (auto-copy and auto-copy-safe among
// them), and "no": decoders that hand their frames over as pictures in
// memory, never as the GPU's own.
QString copyBackModes(const QString &hwdec) {
    QStringList modes;
    for (const QString &mode : hwdec.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        if (mode.endsWith(QLatin1String("-copy")) || mode.startsWith(QLatin1String("auto-copy"))
                || mode == QLatin1String("no"))
            modes << mode;
    }
    return modes.join(QLatin1Char(','));
}

// The scaling turned down a step, for pictures that take too long to draw
// (GpuRenderer::noteDrawTime()): first the downscaler no wider than the
// picture's pixels, where correct-downscaling widens it by the scale (a 4K
// picture brought down to 480 lines: forty times the fetches a pixel), and
// no pass through linear light; then bilinear scaling with no dithering,
// mpv's cheapest. Each is an option mpv takes while it plays.
std::vector<std::pair<const char *, const char *>> cheaperScaling(int level) {
    if (level <= 1)
        return { { "correct-downscaling", "no" }, { "linear-downscaling", "no" } };
    return { { "dscale", "bilinear" }, { "scale", "bilinear" }, { "cscale", "bilinear" }, { "dither", "no" } };
}

} // namespace

// Draws the pictures, on a thread of its own: the software renderer converts
// and scales on the CPU, which would hold the menus still on the GUI thread.
// Only it calls into the render context while it runs (libmpv's rule: one
// mpv_render_* call at a time, none from inside its callbacks).
class EmbeddedMpv::Renderer : public QObject {
public:
    explicit Renderer(EmbeddedMpv *owner) : m_owner(owner) {}

    // Set before the thread starts, and only looked at on it.
    mpv_render_context *context = nullptr;

    // From any thread, mpv's included: draw on the render thread, once however
    // often this is asked before it gets there. redraw draws the last picture
    // again even when mpv has no new one (a new size).
    void schedule(bool redraw = false) {
        if (redraw)
            m_redraw = true;
        if (!m_scheduled.exchange(true))
            QMetaObject::invokeMethod(this, [this] { render(); }, Qt::QueuedConnection);
    }

    static void onUpdate(void *self) { static_cast<Renderer *>(self)->schedule(); }

private:
    void render() {
        m_scheduled = false;
        if (!context)
            return;
        const bool newFrame = api().renderUpdate(context) & MPV_RENDER_UPDATE_FRAME;
        const bool redraw = m_redraw.exchange(false) && m_drawn;
        if (!newFrame && !redraw)
            return;
        QSize size;
        {
            QMutexLocker lock(&m_owner->m_frameLock);
            size = m_owner->drawSize();
        }
        if (size.isEmpty())
            return;
        // Lines and start on 64 bytes, for mpv's SIMD paths. Each picture has
        // memory of its own, freed with the last QImage holding it, so one
        // still on its way to the screen is never drawn over.
        const size_t stride = (size_t(size.width()) * 4 + 63) & ~size_t(63);
        void *pixels = nullptr;
        if (posix_memalign(&pixels, 64, stride * size_t(size.height())) != 0)
            return;
        int surface[2] = { size.width(), size.height() };
        char format[] = "rgb0";
        size_t strideParam = stride;
        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_SW_SIZE, surface },
            { MPV_RENDER_PARAM_SW_FORMAT, format },
            { MPV_RENDER_PARAM_SW_STRIDE, &strideParam },
            { MPV_RENDER_PARAM_SW_POINTER, pixels },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };
        if (api().renderRender(context, params) < 0) {
            std::free(pixels);
            return;
        }
        m_drawn = true;
        const QImage picture(static_cast<uchar *>(pixels), size.width(), size.height(),
                             qsizetype(stride), QImage::Format_RGBX8888, freePixels, pixels);
        {
            QMutexLocker lock(&m_owner->m_frameLock);
            m_owner->m_frame = picture;
        }
        emit m_owner->frameReady();
    }

    EmbeddedMpv *m_owner;
    std::atomic<bool> m_scheduled { false };
    std::atomic<bool> m_redraw { false };
    bool m_drawn = false;
};

// Draws the pictures on the GPU, on a thread of its own, where an OpenGL
// context sharing its objects with the scene graph's is current from init()
// to cleanup(). mpv draws each picture into one of kBuffers textures, never
// one the scene graph may still be showing (gpuFrame()), and it is finished
// (glFinish) before it is handed over, so the other context reads it whole.
// The textures stay from one session to the next, so the scene graph is never
// left with one that is gone; a free one is made again at a new size.
class EmbeddedMpv::GpuRenderer : public QObject {
public:
    GpuRenderer(EmbeddedMpv *owner, QOpenGLContext *context, QOffscreenSurface *surface)
        : m_owner(owner), m_context(context), m_surface(surface) {}

    // These four on its thread, the GUI thread waiting for them.
    bool init() {
        if (!m_context->makeCurrent(m_surface)) {
            qWarning("[EmbeddedMpv] mpv's OpenGL context can't be used: pictures drawn on the CPU");
            return false;
        }
        m_gl = m_context->functions();
        // A sized format where the context takes one (OpenGL ES 2 doesn't).
        m_internalFormat = m_context->isOpenGLES() && m_context->format().majorVersion() < 3
                           ? 0 : GL_RGBA8;
        const QByteArray renderer(reinterpret_cast<const char *>(m_gl->glGetString(GL_RENDERER)));
        // OpenGL drawn on the CPU (Mesa's llvmpipe, in a VM say) gains
        // nothing over mpv's own software renderer, unless asked for (tests).
        if ((renderer.contains("llvmpipe") || renderer.contains("softpipe")
             || renderer.contains("Software Rasterizer"))
                && legacy::env("EMBEDDED_RENDER") != QLatin1String("gpu")) {
            qInfo("[EmbeddedMpv] OpenGL here is drawn on the CPU (%s): pictures drawn by mpv's software renderer",
                  renderer.constData());
            return false;
        }
        qInfo("[EmbeddedMpv] pictures drawn on the GPU: %s", renderer.constData());
        return true;
    }

    // A session's core: mpv draws through this context.
    bool attach(mpv_handle *mpv) {
        mpv_opengl_init_params gl { &GpuRenderer::procAddress, m_context };
        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL) },
            { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };
        if (api().renderCreate(&m_render, mpv, params) < 0) {
            m_render = nullptr;
            return false;
        }
        m_drawn = false;
        // This session's pictures measured afresh (noteDrawTime()).
        m_drawMs = -1;
        m_slowPictures = 0;
        api().renderSetUpdateCallback(m_render, &GpuRenderer::onUpdate, this);
        return true;
    }

    // Before the core goes (libmpv's order).
    void detach() {
        if (!m_render)
            return;
        api().renderSetUpdateCallback(m_render, nullptr, nullptr);
        api().renderFree(m_render);
        m_render = nullptr;
    }

    void cleanup() {
        detach();
        if (m_gl) {
            for (Buffer &b : m_buffers)
                release(b);
        }
        m_context->doneCurrent();
        // Back to the GUI thread, which deletes it once this one has ended.
        m_context->moveToThread(QCoreApplication::instance()->thread());
    }

    // As the software renderer's (Renderer above).
    void schedule(bool redraw = false) {
        if (redraw)
            m_redraw = true;
        if (!m_scheduled.exchange(true))
            QMetaObject::invokeMethod(this, [this] { render(); }, Qt::QueuedConnection);
    }

    static void onUpdate(void *self) { static_cast<GpuRenderer *>(self)->schedule(); }

    // How many steps the scaling has been turned down (noteDrawTime()), for
    // the sessions after to start that way. From any thread.
    int cheaper() const { return m_cheaper; }

private:
    struct Buffer {
        GLuint texture = 0;
        GLuint fbo = 0;
        QSize size;
    };
    // The newest picture and the two the scene graph took last are taken, so
    // one is always free.
    static constexpr int kBuffers = 4;
    // A picture taking this long, on average, for this many pictures in a
    // row, is too long: see noteDrawTime().
    static constexpr double kSlowMs = 12.0;
    static constexpr int kSlowPictures = 48;
    static constexpr int kCheapest = 2;

    static void *procAddress(void *context, const char *name) {
        return reinterpret_cast<void *>(static_cast<QOpenGLContext *>(context)->getProcAddress(name));
    }

    bool allocate(Buffer &b, const QSize &size) {
        release(b);
        m_gl->glGenTextures(1, &b.texture);
        m_gl->glBindTexture(GL_TEXTURE_2D, b.texture);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_gl->glTexImage2D(GL_TEXTURE_2D, 0, m_internalFormat ? GLint(m_internalFormat) : GL_RGBA,
                           size.width(), size.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_gl->glBindTexture(GL_TEXTURE_2D, 0);
        m_gl->glGenFramebuffers(1, &b.fbo);
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, b.fbo);
        m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, b.texture, 0);
        const bool complete = m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        m_gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!complete) {
            release(b);
            return false;
        }
        b.size = size;
        return true;
    }

    void release(Buffer &b) {
        if (b.fbo)
            m_gl->glDeleteFramebuffers(1, &b.fbo);
        if (b.texture)
            m_gl->glDeleteTextures(1, &b.texture);
        b = Buffer();
    }

    void render() {
        m_scheduled = false;
        if (!m_render)
            return;
        const bool newFrame = api().renderUpdate(m_render) & MPV_RENDER_UPDATE_FRAME;
        const bool redraw = m_redraw.exchange(false) && m_drawn;
        if (!newFrame && !redraw)
            return;
        QSize size;
        int taken[3];
        {
            QMutexLocker lock(&m_owner->m_frameLock);
            size = m_owner->drawSize();
            taken[0] = m_owner->m_gpuPublishedIndex;
            taken[1] = m_owner->m_gpuShownIndex;
            taken[2] = m_owner->m_gpuPreviousIndex;
        }
        if (size.isEmpty())
            return;
        // A free texture, one of the right size if there is one.
        int index = -1;
        for (int i = 0; i < kBuffers; ++i) {
            if (i == taken[0] || i == taken[1] || i == taken[2])
                continue;
            if (m_buffers[i].size == size) {
                index = i;
                break;
            }
            if (index < 0)
                index = i;
        }
        Buffer &b = m_buffers[index];
        if (b.size != size && !allocate(b, size)) {
            qWarning("[EmbeddedMpv] no %dx%d texture to draw into", size.width(), size.height());
            return;
        }
        mpv_opengl_fbo fbo { int(b.fbo), size.width(), size.height(), int(m_internalFormat) };
        int flipY = 0;
        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
            { MPV_RENDER_PARAM_FLIP_Y, &flipY },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };
        QElapsedTimer clock;
        clock.start();
        if (api().renderRender(m_render, params) < 0)
            return;
        m_gl->glFinish();
        noteDrawTime(clock.nsecsElapsed() / 1e6);
        m_drawn = true;
        {
            QMutexLocker lock(&m_owner->m_frameLock);
            m_owner->m_gpuPublishedIndex = index;
            m_owner->m_gpuPublished = { b.texture, size, ++m_serial };
        }
        emit m_owner->frameReady();
    }

    // The GPU is the menus' as much as mpv's: a picture that takes long to
    // draw (a 1080p or 4K one brought down to a CRT's lines with the
    // downscaler widened to the scale, hundreds of fetches a pixel) keeps
    // their frames waiting behind it, and the menus answer late. Pictures
    // taking kSlowMs or more each, on average, for kSlowPictures in a row
    // (two seconds of a film; counted in pictures, not time, so a pause or
    // the gap to the next session counts for nothing, and measured afresh
    // for each session, whose first few pictures compile mpv's shaders and
    // pass) turn the scaling down a step (cheaperScaling()), kCheapest steps
    // at most, and the sessions after start at that step: the GPU that was
    // too slow for a picture is the same for the next.
    void noteDrawTime(double ms) {
        m_drawMs = m_drawMs < 0 ? ms : m_drawMs * 0.875 + ms * 0.125;
        if (m_drawMs < kSlowMs || m_cheaper >= kCheapest) {
            m_slowPictures = 0;
            return;
        }
        if (++m_slowPictures < kSlowPictures)
            return;
        m_slowPictures = 0;
        const int level = ++m_cheaper;
        const double took = m_drawMs;
        // Measured afresh at the new step.
        m_drawMs = -1;
        QMetaObject::invokeMethod(m_owner, [owner = m_owner, level, took] { owner->drawCheaper(level, took); },
                                  Qt::QueuedConnection);
    }

    EmbeddedMpv *m_owner;
    QOpenGLContext *m_context;
    QOffscreenSurface *m_surface;
    QOpenGLFunctions *m_gl = nullptr;
    GLenum m_internalFormat = 0;
    mpv_render_context *m_render = nullptr;
    Buffer m_buffers[kBuffers];
    quint64 m_serial = 0;
    std::atomic<bool> m_scheduled { false };
    std::atomic<bool> m_redraw { false };
    bool m_drawn = false;
    // A picture's draw time, averaged over the last few; how many pictures in
    // a row it has been over kSlowMs; the steps taken.
    double m_drawMs = -1;
    int m_slowPictures = 0;
    std::atomic<int> m_cheaper { 0 };
};

bool EmbeddedMpv::available() {
    return api().ok;
}

EmbeddedMpv::EmbeddedMpv(QObject *parent) : QObject(parent) {}

EmbeddedMpv::~EmbeddedMpv() {
    teardown();
    shutdownGpu();
}

bool EmbeddedMpv::gpuAvailable() {
    if (!m_gpuOff && !m_gpuRenderer && !setUpGpu())
        m_gpuOff = true;
    return !m_gpuOff;
}

bool EmbeddedMpv::setUpGpu() {
    if (legacy::env("EMBEDDED_RENDER") == QLatin1String("sw")) {
        qInfo("[EmbeddedMpv] pictures drawn by mpv's software renderer (OSDOS_EMBEDDED_RENDER=sw)");
        return false;
    }
    // Qt Quick's own renderer (no other backend, as QT_QUICK_BACKEND=software
    // asks for) on OpenGL.
    QOpenGLContext *share = QOpenGLContext::globalShareContext();
    if (!QQuickWindow::sceneGraphBackend().isEmpty()
            || QQuickWindow::graphicsApi() != QSGRendererInterface::OpenGL || !share) {
        qInfo("[EmbeddedMpv] Qt Quick doesn't draw with OpenGL here: pictures drawn by mpv's software renderer");
        return false;
    }
    // Both made here, on the GUI thread; the context is current on the GPU's.
    auto *surface = new QOffscreenSurface;
    surface->setFormat(share->format());
    surface->create();
    auto *context = new QOpenGLContext;
    context->setFormat(share->format());
    context->setShareContext(share);
    if (!surface->isValid() || !context->create()) {
        qWarning("[EmbeddedMpv] no OpenGL context for mpv: pictures drawn on the CPU");
        delete context;
        delete surface;
        return false;
    }
    m_gpuSurface = surface;
    m_gpuContext = context;
    m_gpuThread = new QThread;
    m_gpuThread->setObjectName(QStringLiteral("mpv-gpu"));
    m_gpuRenderer = new GpuRenderer(this, context, surface);
    context->moveToThread(m_gpuThread);
    m_gpuRenderer->moveToThread(m_gpuThread);
    m_gpuThread->start();
    bool ok = false;
    QMetaObject::invokeMethod(m_gpuRenderer, [this, &ok] { ok = m_gpuRenderer->init(); },
                              Qt::BlockingQueuedConnection);
    if (!ok) {
        shutdownGpu();
        return false;
    }
    return true;
}

void EmbeddedMpv::shutdownGpu() {
    if (!m_gpuRenderer)
        return;
    QMetaObject::invokeMethod(m_gpuRenderer, [this] { m_gpuRenderer->cleanup(); },
                              Qt::BlockingQueuedConnection);
    m_gpuThread->quit();
    m_gpuThread->wait();
    delete m_gpuRenderer;
    m_gpuRenderer = nullptr;
    delete m_gpuThread;
    m_gpuThread = nullptr;
    delete m_gpuContext;
    m_gpuContext = nullptr;
    delete m_gpuSurface;
    m_gpuSurface = nullptr;
}

bool EmbeddedMpv::start(const QStringList &args) {
    stop();
    const MpvApi &a = api();
    if (!a.ok)
        return false;
    mpv_handle *mpv = a.create();
    if (!mpv) {
        qWarning("[EmbeddedMpv] mpv_create failed");
        return false;
    }

    // An option mpv doesn't know (one newer than this libmpv, say) is left
    // out, where the mpv command line would refuse to start.
    auto set = [&](const QString &name, const QString &value) {
        const int err = a.setOptionString(mpv, name.toUtf8().constData(), value.toUtf8().constData());
        if (err < 0)
            qWarning("[EmbeddedMpv] --%s=%s: %s", qPrintable(name), qPrintable(value), a.errorString(err));
    };
    // What libmpv leaves off and the mpv command line has on. Idle "once"
    // waits for the playlist below, and quits once it has played out.
    set(QStringLiteral("vo"), QStringLiteral("libmpv"));
    set(QStringLiteral("idle"), QStringLiteral("once"));
    set(QStringLiteral("input-default-bindings"), QStringLiteral("yes"));

    // A list option given more than once on the command line (--script,
    // --sub-file, ...) adds to its list there. Here the lists are gathered and
    // given whole, as lists rather than joined strings, so an item keeps any
    // character: a subtitle URL's ':' and ','.
    QMap<QString, QStringList> lists;
    auto setList = [&](const QString &name, const QStringList &values) {
        std::vector<QByteArray> utf8;
        utf8.reserve(size_t(values.size()));
        std::vector<mpv_node> items(size_t(values.size()));
        for (int i = 0; i < values.size(); ++i) {
            utf8.push_back(values[i].toUtf8());
            items[size_t(i)].format = MPV_FORMAT_STRING;
            items[size_t(i)].u.string = utf8.back().data();
        }
        mpv_node_list list { int(items.size()), items.data(), nullptr };
        mpv_node root;
        root.format = MPV_FORMAT_NODE_ARRAY;
        root.u.list = &list;
        const int err = a.setOption(mpv, name.toUtf8().constData(), MPV_FORMAT_NODE, &root);
        if (err < 0)
            qWarning("[EmbeddedMpv] --%s: %s", qPrintable(name), a.errorString(err));
    };

    // The command line as options, its other words, and every word after a
    // "--", as the playlist.
    QStringList urls;
    QString hwdec;
    int playlistStart = -1;
    bool shuffle = false;
    bool files = false;
    for (const QString &arg : args) {
        if (files || !arg.startsWith(QLatin1String("--"))) {
            urls << arg;
            continue;
        }
        if (arg == QLatin1String("--")) {
            files = true;
            continue;
        }
        QString name = arg.mid(2);
        QString value = QStringLiteral("yes");
        const int eq = name.indexOf(QLatin1Char('='));
        if (eq >= 0) {
            value = name.mid(eq + 1);
            name = name.left(eq);
        } else if (name.startsWith(QLatin1String("no-"))) {
            name = name.mid(3);
            value = QStringLiteral("no");
        }
        if (name == QLatin1String("script")) {
            lists[QStringLiteral("scripts")] << value;
            continue;
        }
        if (name == QLatin1String("sub-file")) {
            lists[QStringLiteral("sub-files")] << value;
            continue;
        }
        if (name == QLatin1String("audio-file")) {
            lists[QStringLiteral("audio-files")] << value;
            continue;
        }
        if (name == QLatin1String("http-header-fields")) {
            lists[name] << value;
            continue;
        }
        if (name == QLatin1String("playlist-start")) {
            playlistStart = value.toInt();
        } else if (name == QLatin1String("shuffle")) {
            shuffle = value == QLatin1String("yes");
        } else if (name == QLatin1String("hwdec")) {
            hwdec = value;
            // The GPU didn't take a hardware decoder's frames before.
            if (m_gpuCopyBack && !copyBackModes(value).isEmpty())
                value = copyBackModes(value);
        }
        set(name, value);
    }
    for (auto it = lists.cbegin(); it != lists.cend(); ++it)
        setList(it.key(), it.value());
    // The scaling as the sessions before left it: turned down where their
    // pictures took too long to draw (GpuRenderer::noteDrawTime()).
    if (m_gpuRenderer) {
        for (int level = 1; level <= m_gpuRenderer->cheaper(); ++level) {
            for (const auto &[name, value] : cheaperScaling(level))
                set(QLatin1String(name), QLatin1String(value));
        }
    }

    if (a.initialize(mpv) < 0) {
        qWarning("[EmbeddedMpv] mpv_initialize failed");
        a.terminateDestroy(mpv);
        return false;
    }
    a.requestLogMessages(mpv, "warn");

    // On the GPU where it can be; on the CPU otherwise, and from then on
    // should the GPU fail.
    m_gpu = false;
    if (gpuAvailable()) {
        bool attached = false;
        QMetaObject::invokeMethod(m_gpuRenderer, [this, mpv, &attached] {
            attached = m_gpuRenderer->attach(mpv);
        }, Qt::BlockingQueuedConnection);
        m_gpu = attached;
        if (!attached) {
            m_gpuOff = true;
            qWarning("[EmbeddedMpv] mpv can't draw with OpenGL here: pictures drawn on the CPU");
        }
    }
    if (!m_gpu) {
        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW) },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };
        mpv_render_context *render = nullptr;
        if (a.renderCreate(&render, mpv, params) < 0) {
            qWarning("[EmbeddedMpv] the software renderer could not be set up");
            a.terminateDestroy(mpv);
            return false;
        }
        m_render = render;
        m_thread = new QThread;
        m_thread->setObjectName(QStringLiteral("mpv-render"));
        m_renderer = new Renderer(this);
        m_renderer->context = render;
        m_renderer->moveToThread(m_thread);
        m_thread->start();
        a.renderSetUpdateCallback(render, &Renderer::onUpdate, m_renderer);
    }

    // Should the GPU not take a hardware decoder's frames, the session's
    // copy-back modes take over (drainEvents()).
    m_copyBackHwdec = m_gpu && !m_gpuCopyBack && copyBackModes(hwdec) != hwdec
                      ? copyBackModes(hwdec) : QString();

    m_mpv = mpv;
    m_lastEndReason.clear();
    a.setWakeupCallback(mpv, &EmbeddedMpv::onWakeup, this);

    // The playlist, mixed (shuffle) and started (playlist-start) as the
    // command line would. A lone playlist file (m3u) mpv mixes and starts
    // where it opens it, by the same options.
    for (const QString &url : urls) {
        const QByteArray u = url.toUtf8();
        const char *load[] = { "loadfile", u.constData(), "append", nullptr };
        a.command(mpv, load);
    }
    if (urls.size() > 1 && shuffle) {
        const char *mix[] = { "playlist-shuffle", nullptr };
        a.command(mpv, mix);
    }
    const QByteArray first = QByteArray::number(urls.size() > 1 && playlistStart > 0 ? playlistStart : 0);
    const char *play[] = { "playlist-play-index", first.constData(), nullptr };
    if (a.command(mpv, play) < 0) {
        // Before mpv 0.33.
        const char *pos[] = { "set", "playlist-pos", first.constData(), nullptr };
        a.command(mpv, pos);
    }
    return true;
}

void EmbeddedMpv::stop() {
    teardown();
}

void EmbeddedMpv::retire() {
    if (!m_mpv)
        return;
    // Never two at once: the one before has gone first (start() and
    // MpvController wait for it, so this is for form).
    waitForRetirement();
    const MpvApi *a = &api();
    a->setWakeupCallback(m_mpv, nullptr, nullptr);
    mpv_handle *core = m_mpv;
    m_mpv = nullptr;
    // Told to quit first, without waiting for it: a core still playing holds
    // the freeing of its render context a quarter of a second, for a picture
    // no one will draw; one quitting lets it go at once. The video of a
    // core quitting ends anyway (the destroy follows).
    const char *quit[] = { "quit", nullptr };
    a->commandAsync(core, 0, quit);
    // The render context goes before the core (libmpv's order), and freeing
    // it disables the video forcefully, which waits on the core's playloop:
    // a few hundred milliseconds with a film playing. The GPU's is freed on
    // the GPU's thread, where its context is current, told here and waited
    // for by the retiring thread; the software renderer's on the retiring
    // thread itself, once the render thread has finished (a picture at
    // most). Neither on the GUI thread.
    auto renderDown = std::make_shared<QSemaphore>();
    mpv_render_context *render = nullptr;
    if (m_gpu) {
        GpuRenderer *renderer = m_gpuRenderer;
        QMetaObject::invokeMethod(renderer, [renderer, renderDown] {
            renderer->detach();
            renderDown->release();
        }, Qt::QueuedConnection);
        m_gpu = false;
    } else {
        m_thread->quit();
        m_thread->wait();
        // The core asks the renderer for no more pictures before the
        // renderer goes: the callback off first (nothing else calls into
        // the context now that its thread has finished), the object after.
        a->renderSetUpdateCallback(m_render, nullptr, nullptr);
        delete m_renderer;
        m_renderer = nullptr;
        delete m_thread;
        m_thread = nullptr;
        render = m_render;
        m_render = nullptr;
        renderDown->release();
    }
    {
        QMutexLocker lock(&m_frameLock);
        m_frame = QImage();
        m_gpuPublished = GpuFrame();
        m_gpuPublishedIndex = -1;
    }
    m_retiring = true;
    m_retiringSince.start();
    const int serial = ++m_retireSerial;
    QThread *thread = QThread::create([a, core, render, renderDown] {
        renderDown->acquire();
        if (render)
            a->renderFree(render);
        a->terminateDestroy(core);
        // What the core freed goes back to the system, film by film
        // (util/Diagnostics), rather than staying with the app.
        diagnostics::releaseFreedMemory();
    });
    thread->setObjectName(QStringLiteral("mpv-retire"));
    m_retireThread = thread;
    connect(thread, &QThread::finished, this, [this, serial] { retirementDone(serial); });
    thread->start();
}

void EmbeddedMpv::retirementDone(int serial) {
    if (serial != m_retireSerial || !m_retireThread)
        return;
    m_retireThread->deleteLater();
    m_retireThread = nullptr;
    m_retiring = false;
    emit retired();
}

void EmbeddedMpv::waitForRetirement() {
    if (!m_retireThread)
        return;
    if (!m_retireThread->wait(kRetireWaitMs))
        abandonRetirement();
    else
        retirementDone(m_retireSerial);
}

int EmbeddedMpv::retiringMs() const {
    return m_retiring ? int(m_retiringSince.elapsed()) : 0;
}

void EmbeddedMpv::abandonRetirement() {
    if (!m_retireThread)
        return;
    // Stuck where even its destroy can't reach it (a read off a mount gone
    // away, a decoder's call that never returns). Left to finish by itself,
    // should it ever; its word of that counts for nothing (the serial), and
    // it deletes itself. The next session starts beside it: its sound card
    // may be busy still, which a core can't be left to hold for good.
    qWarning("[EmbeddedMpv] a retired core is still there %d s after it was let go of; going on without it",
             int(m_retiringSince.elapsed() / 1000));
    QThread *thread = m_retireThread;
    m_retireThread = nullptr;
    disconnect(thread, nullptr, this, nullptr);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    ++m_retireSerial;
    m_retiring = false;
    emit retired();
}

// Commands that need no answer go to the core without waiting for it
// (mpv_command_async): mpv_command waits for the playloop, which, busy with
// a slow seek or a read off a slow mount, would hold the GUI thread with it.
void EmbeddedMpv::quit() {
    if (!m_mpv)
        return;
    const char *cmd[] = { "quit", nullptr };
    api().commandAsync(m_mpv, 0, cmd);
}

void EmbeddedMpv::drawCheaper(int level, double tookMs) {
    if (!m_mpv || !m_gpu)
        return;
    qWarning("[EmbeddedMpv] the pictures took %.0f ms each to draw, the menus' frames waiting behind them: %s",
             tookMs, level <= 1 ? "the downscaler is no wider than the picture's pixels now"
                                : "the picture is scaled bilinearly now, without dithering");
    for (const auto &[name, value] : cheaperScaling(level)) {
        const char *cmd[] = { "set", name, value, nullptr };
        api().commandAsync(m_mpv, 0, cmd);
    }
}

void EmbeddedMpv::setTargetSize(const QSize &size) {
    {
        QMutexLocker lock(&m_frameLock);
        if (size == m_targetSize || size.isEmpty())
            return;
        m_targetSize = size;
    }
    if (m_renderer)
        m_renderer->schedule(true);
    if (m_gpu)
        m_gpuRenderer->schedule(true);
}

void EmbeddedMpv::setShown(bool shown) {
    {
        QMutexLocker lock(&m_frameLock);
        if (shown == m_shown)
            return;
        m_shown = shown;
    }
    // Shown again: the latest picture whole, before the next comes.
    if (m_renderer)
        m_renderer->schedule(true);
    if (m_gpu)
        m_gpuRenderer->schedule(true);
}

QSize EmbeddedMpv::drawSize() const {
    // mpv still converts and scales each picture, but into a few pixels:
    // the decoder's frames are taken as they come, and the picture is there
    // to be drawn whole again at once.
    return m_shown ? m_targetSize : QSize(16, 16);
}

QImage EmbeddedMpv::frame() const {
    QMutexLocker lock(&m_frameLock);
    return m_frame;
}

EmbeddedMpv::GpuFrame EmbeddedMpv::gpuFrame() {
    QMutexLocker lock(&m_frameLock);
    if (m_gpuPublishedIndex != m_gpuShownIndex) {
        m_gpuPreviousIndex = m_gpuShownIndex;
        m_gpuShownIndex = m_gpuPublishedIndex;
    }
    return m_gpuShownIndex >= 0 ? m_gpuPublished : GpuFrame();
}

void EmbeddedMpv::onWakeup(void *self) {
    // From one of mpv's threads: the events are read on the GUI thread.
    auto *embedded = static_cast<EmbeddedMpv *>(self);
    if (!embedded->m_drainQueued.exchange(true))
        QMetaObject::invokeMethod(embedded, [embedded] { embedded->drainEvents(); }, Qt::QueuedConnection);
}

void EmbeddedMpv::drainEvents() {
    m_drainQueued = false;
    while (m_mpv) {
        mpv_event *event = api().waitEvent(m_mpv, 0);
        switch (event->event_id) {
        case MPV_EVENT_NONE:
            return;
        case MPV_EVENT_LOG_MESSAGE: {
            const auto *msg = static_cast<mpv_event_log_message *>(event->data);
            const QByteArray text = QByteArray(msg->text).trimmed();
            qWarning("[mpv] %s: %s", msg->prefix, text.constData());
            // mpv draws nothing where the GPU can't take a hardware decoder's
            // frames, and doesn't fall back by itself: the copy-back modes
            // hand them over in memory, for this session and the next.
            if (!m_copyBackHwdec.isEmpty()
                    && (text.startsWith("Mapping hardware decoded surface failed")
                        || text.startsWith("Initializing texture for hardware decoding failed"))) {
                qWarning("[EmbeddedMpv] the GPU can't take the decoder's frames: --hwdec=%s",
                         qPrintable(m_copyBackHwdec));
                const QByteArray modes = m_copyBackHwdec.toUtf8();
                const char *cmd[] = { "set", "hwdec", modes.constData(), nullptr };
                api().commandAsync(m_mpv, 0, cmd);
                m_copyBackHwdec.clear();
                m_gpuCopyBack = true;
            }
            break;
        }
        case MPV_EVENT_END_FILE:
            m_lastEndReason = endReasonName(static_cast<mpv_event_end_file *>(event->data)->reason);
            break;
        case MPV_EVENT_SHUTDOWN: {
            const QString reason = m_lastEndReason;
            // Gone on a thread of its own, as a replaced core goes: what is
            // left to destroy after the quit is the core's own threads.
            retire();
            emit finished(reason);
            return;
        }
        default:
            break;
        }
    }
}

void EmbeddedMpv::teardown() {
    retire();
    waitForRetirement();
}

#else // !OSDOS_EMBEDDED_MPV — built without libmpv's headers: never available.

class EmbeddedMpv::Renderer {};
class EmbeddedMpv::GpuRenderer {};

bool EmbeddedMpv::available() { return false; }
EmbeddedMpv::EmbeddedMpv(QObject *parent) : QObject(parent) {}
EmbeddedMpv::~EmbeddedMpv() = default;
bool EmbeddedMpv::start(const QStringList &) { return false; }
void EmbeddedMpv::stop() {}
void EmbeddedMpv::retire() {}
int EmbeddedMpv::retiringMs() const { return 0; }
void EmbeddedMpv::abandonRetirement() {}
void EmbeddedMpv::quit() {}
void EmbeddedMpv::drawCheaper(int, double) {}
void EmbeddedMpv::waitForRetirement() {}
void EmbeddedMpv::retirementDone(int) {}
void EmbeddedMpv::setTargetSize(const QSize &size) {
    QMutexLocker lock(&m_frameLock);
    m_targetSize = size;
}
void EmbeddedMpv::setShown(bool shown) {
    QMutexLocker lock(&m_frameLock);
    m_shown = shown;
}
QSize EmbeddedMpv::drawSize() const { return m_targetSize; }
QImage EmbeddedMpv::frame() const { return {}; }
EmbeddedMpv::GpuFrame EmbeddedMpv::gpuFrame() { return {}; }
bool EmbeddedMpv::gpuAvailable() { return false; }
bool EmbeddedMpv::setUpGpu() { return false; }
void EmbeddedMpv::shutdownGpu() {}
void EmbeddedMpv::onWakeup(void *) {}
void EmbeddedMpv::drainEvents() {}
void EmbeddedMpv::teardown() {}

#endif
