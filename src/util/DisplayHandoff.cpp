#include "DisplayHandoff.h"
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QDebug>

#ifdef Q_OS_LINUX
#include <QElapsedTimer>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/vt.h>
#include <cstring>
// DRM master ioctls (also provided by xf86drm.h, but define as fallback).
#ifndef DRM_IOCTL_SET_MASTER
#define DRM_IOCTL_SET_MASTER   _IO('d', 0x1e)
#define DRM_IOCTL_DROP_MASTER  _IO('d', 0x1f)
#endif
#endif

DisplayHandoff::DisplayHandoff(QObject *parent)
    : QObject(parent)
{
    // A member timer rather than QTimer::singleShot so releaseNow() can cancel a
    // pending release during shutdown — the stored callback captures an object
    // that is being destroyed.
    m_releaseTimer = new QTimer(this);
    m_releaseTimer->setSingleShot(true);
    connect(m_releaseTimer, &QTimer::timeout, this, [this] {
        auto cb = m_onRestored;
        m_onRestored = nullptr;
        doRestore();
        if (cb) cb();
    });
    // While a child has the screen, whether its own picture is up yet: the
    // cover is blacked out then (checkCover()).
    m_coverTimer = new QTimer(this);
    m_coverTimer->setInterval(250);
#ifdef Q_OS_LINUX
    connect(m_coverTimer, &QTimer::timeout, this, &DisplayHandoff::checkCover);
#endif
}

DisplayHandoff::~DisplayHandoff() {
    // Restore unconditionally: leaving a Pi on a blank free VT with DRM master
    // dropped means a black screen and a text console the user never asked for.
    if (!m_owner.isEmpty()) {
        m_releaseTimer->stop();
        m_onRestored = nullptr;
        // Not heldChanged: QML outlives this object, and has nothing to redraw.
        blockSignals(true);
        // Qt's own frame, not the cover: nothing is drawn after this.
        doRestore(false);
    }
#ifdef Q_OS_LINUX
    // The cover's mapping; the buffer itself goes with the app's DRM file,
    // which may be on the screen still.
    if (m_cover.pixels)
        ::munmap(m_cover.pixels, m_cover.size);
#endif
}

bool DisplayHandoff::isHeadless() {
#ifdef Q_OS_LINUX
    return qgetenv("DISPLAY").isEmpty() && qgetenv("WAYLAND_DISPLAY").isEmpty();
#else
    return false;
#endif
}

bool DisplayHandoff::isHeldBy(const QString &owner) const {
    return !m_owner.isEmpty() && m_owner == owner;
}

bool DisplayHandoff::savedStateValid() const {
#ifdef Q_OS_LINUX
    return m_savedDrm.valid;
#else
    return false;
#endif
}

int DisplayHandoff::acquire(const QString &owner) {
    // On every platform, before anything else: what plays inside the app's
    // window makes way (MpvController), whether or not the screen changes hands.
    emit handingOff(owner);
    if (!m_owner.isEmpty() && m_owner != owner) {
        qWarning("[DisplayHandoff] %s requested the screen but %s holds it",
                 qPrintable(owner), qPrintable(m_owner));
        return -1;
    }
    // A same-owner re-acquire while held is refused too: proceeding would
    // overwrite m_previousVt with the free VT we are currently on, losing the
    // real VT to return to. Callers that relaunch a child use isHeldBy() and
    // skip acquire() entirely (see MpvController).
    if (isHeldBy(owner)) {
        qWarning("[DisplayHandoff] %s tried to acquire the screen it already holds",
                 qPrintable(owner));
        return -1;
    }
    // Owner is tracked on every platform: held/heldChanged describe a logical
    // hand-off too (InputManager releases its SDL pads for RetroArch/MAME),
    // not only a DRM master drop. Headless Linux continues below with the
    // VT/DRM work; elsewhere the child just opens a window over ours.
    m_owner = owner;
    if (!isHeadless()) {
        emit heldChanged();
        return 0;
    }

    m_previousVt = getActiveVt();
    m_qtDrmFd    = -1;

#ifdef Q_OS_LINUX
    // This hand-off's own saved state, or none: the last one's could be left
    // valid (its restore skipped for a DRM master still held), and a takeover
    // would then be allowed that cannot be restored.
    m_savedDrm = {};
    // VT switch first — suspends Qt's render thread via the kernel's VT switch
    // signal before DRM master is dropped, which is what keeps the
    // "Failed to commit atomic request (code=-13)" (EACCES) noise down. Expect a
    // handful of those lines anyway: Qt can still have commits in flight between
    // the switch completing and the master drop. Pi-observed as 1-4 lines per
    // hand-off, with the display recovering correctly. A continuous stream of them
    // instead means the VT switch silently failed — see the warning below.
    const int freeVt = findFreeVt(m_previousVt);
    m_switchedVt = m_previousVt > 0 && switchToVt(freeVt);
    if (m_previousVt <= 0) {
        // A switch with no VT to come back to would leave the app on the free
        // one, and each hand-off after it a VT further on: the lesser harm is
        // the renderer drawing on, as below, until the child is done.
        qWarning("[DisplayHandoff] The active VT is unknown, so the VT is not "
                 "switched (it could not be switched back). Qt's renderer will "
                 "keep drawing while DRM master is dropped.");
    } else if (!m_switchedVt) {
        // This is not cosmetic. Without the switch, Qt's renderer keeps drawing
        // while DRM master is dropped below it: expect a stream of
        // "Failed to commit atomic request (code=-13)" (EACCES), and a child that
        // draws nothing will leave the OSD/OS UI on screen rather than blanking,
        // so a "takeover" doesn't visibly take over. It usually still recovers,
        // which is exactly what makes it easy to miss — so say so plainly.
        qWarning("[DisplayHandoff] Could not switch VT, so Qt's renderer will keep "
                 "drawing while DRM master is dropped. This process needs "
                 "CAP_SYS_TTY_CONFIG or membership of the 'tty' group, and "
                 "/dev/tty0 must be group-writable. The installed systemd service "
                 "grants both (see scripts/install.sh); a hand-launched dev build "
                 "may not.");
    }

    m_qtDrmFd = findQtDrmFd();
    if (m_qtDrmFd < 0) {
        qWarning("[DisplayHandoff] Could not find Qt DRM fd");
    } else {
        qDebug("[DisplayHandoff] DRM master dropped (fd %d)", m_qtDrmFd);
        // Save the current CRTC state so we can restore it exactly after the
        // child exits. An atomic cleanup disables the CRTC (CRTC_ACTIVE=0);
        // without this restore, Qt EGLFS gets EINVAL on its next page flip.
        saveDrmCrtcState(m_qtDrmFd);
        // The app's last frame on a frame buffer of our own: what the child
        // saves, and puts back as it ends, blacked out by then.
        if (m_savedDrm.valid && m_coverPainter)
            showCover(m_qtDrmFd);
    }
    emit heldChanged();
    return freeVt;
#else
    return 0;
#endif
}

void DisplayHandoff::releaseDeferred(const QString &owner,
                                     std::function<void()> onRestored,
                                     int delayMs) {
    if (!m_owner.isEmpty() && m_owner != owner) {
        // Not ours to release. Still run the callback — the caller needs it to
        // report that its process ended.
        qWarning("[DisplayHandoff] %s tried to release a hand-off held by %s",
                 qPrintable(owner), qPrintable(m_owner));
        if (onRestored) onRestored();
        return;
    }
    m_onRestored = std::move(onRestored);
    m_releaseTimer->start(delayMs);
}

void DisplayHandoff::releaseNow(const QString &owner) {
    if (m_owner.isEmpty() || m_owner != owner)
        return;
    m_releaseTimer->stop();
    m_onRestored = nullptr;
    doRestore();
}

void DisplayHandoff::doRestore(bool useCover) {
    m_coverTimer->stop();
#ifdef Q_OS_LINUX
    if (m_qtDrmFd >= 0) {
        if (::ioctl(m_qtDrmFd, DRM_IOCTL_SET_MASTER, 0) < 0) {
            qWarning("[DisplayHandoff] drmSetMaster failed: %s", strerror(errno));
        } else {
            qDebug("[DisplayHandoff] DRM master restored (fd %d)", m_qtDrmFd);
            // Restore the CRTC to its pre-hand-off state using legacy
            // drmModeSetCrtc. This re-enables the CRTC with the original mode and
            // the cover, black, or Qt's last framebuffer, so EGLFS's first
            // atomic page flip succeeds instead of getting EINVAL from a
            // disabled CRTC.
            restoreDrmCrtcState(m_qtDrmFd, useCover);
        }
        m_qtDrmFd = -1;
    }
    m_cover.shown = false;
#else
    Q_UNUSED(useCover)
#endif
    // Only switch back if we actually switched away — otherwise this just logs a
    // second, confusing permission error on a VT we never left.
    if (m_previousVt > 0 && m_switchedVt) {
        qDebug("[DisplayHandoff] Switching back to VT %d", m_previousVt);
        switchToVt(m_previousVt);
    }
    m_previousVt = -1;
    m_switchedVt = false;
    if (!m_owner.isEmpty()) {
        m_owner.clear();
        emit heldChanged();
    }
}

int DisplayHandoff::getActiveVt() const {
#ifdef Q_OS_LINUX
    QFile f("/sys/class/tty/tty0/active");
    if (f.open(QIODevice::ReadOnly)) {
        const QString name = QString::fromLatin1(f.readAll()).trimmed();
        bool ok;
        const int n = name.mid(3).toInt(&ok);
        if (ok && n > 0)
            return n;
    }
    // Without sysfs (a sandboxed service, a kernel built without it): the
    // console's own answer. Write-only, as the tty group may only write it.
    const int fd = ::open("/dev/tty0", O_WRONLY);
    if (fd < 0)
        return -1;
    struct vt_stat state {};
    const bool ok = ::ioctl(fd, VT_GETSTATE, &state) == 0;
    ::close(fd);
    return ok && state.v_active > 0 ? state.v_active : -1;
#else
    return -1;
#endif
}

int DisplayHandoff::findFreeVt(int activeVt) const {
#ifdef Q_OS_LINUX
    int n = -1;
    const int fd = ::open("/dev/tty0", O_WRONLY);
    if (fd >= 0) {
        ::ioctl(fd, VT_OPENQRY, &n);
        ::close(fd);
    }
    if (n <= 0 || n > 63) n = 7;

    // VT_OPENQRY reports the lowest VT that no process currently has OPEN, which
    // is NOT the same as "a VT other than the one we are displaying on". Under
    // the installed service getty@tty1 and autovt@ are masked and osdos.service
    // opens no tty of its own, so nothing holds /dev/tty1 open and this can hand
    // back the very VT Qt is on.
    //
    // Switching to the VT we are already on is a silent no-op: the kernel sends
    // no VT-switch signal, so Qt's renderer never suspends, and we then drop DRM
    // master out from under a still-drawing Qt. The symptoms are a continuous
    // stream of "Failed to commit atomic request (code=-13)" and a takeover that
    // doesn't visibly take over — with m_switchedVt true, so nothing warns.
    //
    // Any other VT will do. A VT does not have to be free to be activated; being
    // unused is merely preferable, because then no getty repaints over the child.
    if (n == activeVt) n = (activeVt < 63) ? activeVt + 1 : activeVt - 1;
    return n;
#else
    Q_UNUSED(activeVt)
    return -1;
#endif
}

#ifdef Q_OS_LINUX
// Opens a tty suitable for issuing VT_ACTIVATE.
//
// Deliberately prefers OUR OWN console over /dev/tty0. /dev/tty0 resolves to
// whichever VT is currently in the foreground, and the kernel permits VT_ACTIVATE
// only when the opened tty is the caller's controlling terminal or the caller has
// CAP_SYS_TTY_CONFIG. So with /dev/tty0 the switch AWAY succeeds (we are still the
// foreground console) but the switch BACK fails with EPERM — the app is left
// stranded on the VT it moved to, and the next hand-off starts from there,
// walking up a VT each time. Opening /dev/tty<our own VT> keeps the check
// satisfied in both directions, since that tty stays our controlling terminal.
//
// The installed systemd service has CAP_SYS_TTY_CONFIG and no controlling
// terminal at all, so either path works there; this is what makes a hand-launched
// dev build behave the same as the service.
int DisplayHandoff::openVtControlFd() const {
    if (m_previousVt > 0) {
        const QByteArray own = QByteArray("/dev/tty") + QByteArray::number(m_previousVt);
        const int fd = ::open(own.constData(), O_WRONLY);
        if (fd >= 0)
            return fd;
    }
    return ::open("/dev/tty0", O_WRONLY);
}
#endif

bool DisplayHandoff::switchToVt(int vt) {
#ifdef Q_OS_LINUX
    int fd = openVtControlFd();
    if (fd < 0) {
        qWarning("[DisplayHandoff] switchToVt %d: cannot open a control tty: %s",
                 vt, strerror(errno));
        return false;
    }
    if (::ioctl(fd, VT_ACTIVATE, vt) < 0) {
        qWarning("[DisplayHandoff] VT_ACTIVATE %d failed: %s", vt, strerror(errno));
        ::close(fd);
        return false;
    }
    // Asked for, the switch happens whatever the wait says: switched, so the
    // VT is switched back. A wait a signal cuts short is waited again.
    int waited;
    int tries = 0;
    do {
        waited = ::ioctl(fd, VT_WAITACTIVE, vt);
    } while (waited < 0 && errno == EINTR && ++tries < 10);
    if (waited < 0)
        qWarning("[DisplayHandoff] VT_WAITACTIVE %d failed: %s", vt, strerror(errno));
    ::close(fd);
    return true;
#else
    Q_UNUSED(vt)
    return false;
#endif
}

int DisplayHandoff::findQtDrmFd() const {
#ifdef Q_OS_LINUX
    // Scan the process's open file descriptors for Qt's DRM primary card
    // device. DRM primary nodes have major=226, minor 0-63 (card0, card1…).
    // We try DRM_IOCTL_DROP_MASTER on each candidate — it succeeds only on
    // the fd that currently holds DRM master, which tells us it's Qt's fd.
    QDir fdDir("/proc/self/fd");
    const QStringList entries = fdDir.entryList(QDir::Files | QDir::System);
    for (const QString &entry : entries) {
        bool ok;
        int fd = entry.toInt(&ok);
        if (!ok) continue;
        struct stat st;
        if (::fstat(fd, &st) < 0) continue;
        if (!S_ISCHR(st.st_mode)) continue;
        if (major(st.st_rdev) != 226) continue;   // not a DRM device
        if (minor(st.st_rdev) >= 64) continue;    // render node, not primary card
        // Found a DRM primary fd — try to drop master; if it works, this is it.
        if (::ioctl(fd, DRM_IOCTL_DROP_MASTER, 0) == 0)
            return fd;
    }
    return -1;
#else
    return -1;
#endif
}

#ifdef Q_OS_LINUX
void DisplayHandoff::saveDrmCrtcState(int fd) {
    m_savedDrm = {};

    drmModeResPtr res = drmModeGetResources(fd);
    if (!res) {
        qWarning("[DisplayHandoff] saveDrmCrtcState: drmModeGetResources failed");
        return;
    }

    for (int i = 0; i < res->count_crtcs && !m_savedDrm.valid; ++i) {
        drmModeCrtcPtr crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        if (!crtc) continue;

        if (crtc->mode_valid) {
            m_savedDrm.crtcId = crtc->crtc_id;
            m_savedDrm.fbId   = crtc->buffer_id;
            m_savedDrm.x      = crtc->x;
            m_savedDrm.y      = crtc->y;
            m_savedDrm.mode   = crtc->mode;

            // Find the connector whose encoder is driving this CRTC
            for (int j = 0; j < res->count_connectors; ++j) {
                drmModeConnectorPtr conn = drmModeGetConnector(fd, res->connectors[j]);
                if (!conn) continue;
                if (conn->encoder_id) {
                    drmModeEncoderPtr enc = drmModeGetEncoder(fd, conn->encoder_id);
                    if (enc) {
                        if (enc->crtc_id == m_savedDrm.crtcId) {
                            m_savedDrm.connectorId = conn->connector_id;
                            m_savedDrm.valid = true;
                        }
                        drmModeFreeEncoder(enc);
                    }
                }
                drmModeFreeConnector(conn);
                if (m_savedDrm.valid) break;
            }
        }
        drmModeFreeCrtc(crtc);
    }
    drmModeFreeResources(res);

    if (m_savedDrm.valid)
        qDebug("[DisplayHandoff] Saved CRTC %u connector %u mode %dx%d@%d",
               m_savedDrm.crtcId, m_savedDrm.connectorId,
               m_savedDrm.mode.hdisplay, m_savedDrm.mode.vdisplay,
               m_savedDrm.mode.vrefresh);
    else
        qWarning("[DisplayHandoff] Could not save CRTC state");
}

void DisplayHandoff::restoreDrmCrtcState(int fd, bool useCover) {
    if (!m_savedDrm.valid) return;

    // The cover, black, where this hand-off showed one: the app's last frame
    // (the loading screen it handed the screen over from) never comes back
    // on the screen, which shows black until the app's next frame. Should
    // the cover not be taken, Qt's own frame buffer, as before covers.
    bool restored = false;
    if (useCover && m_cover.shown && m_cover.fd == fd) {
        blackenCover();
        if (drmModeSetCrtc(fd, m_savedDrm.crtcId, m_cover.fbId, 0, 0,
                           &m_savedDrm.connectorId, 1, &m_savedDrm.mode) == 0)
            restored = true;
        else
            qWarning("[DisplayHandoff] The cover could not be put back (%s): the app's last frame is",
                     strerror(errno));
    }
    int ret = 0;
    if (!restored)
        ret = drmModeSetCrtc(fd,
                             m_savedDrm.crtcId,
                             m_savedDrm.fbId,
                             m_savedDrm.x, m_savedDrm.y,
                             &m_savedDrm.connectorId, 1,
                             &m_savedDrm.mode);
    if (ret < 0)
        qWarning("[DisplayHandoff] drmModeSetCrtc restore failed: %s", strerror(errno));
    else
        qDebug("[DisplayHandoff] CRTC restored (mode %dx%d@%d%s)",
               m_savedDrm.mode.hdisplay, m_savedDrm.mode.vdisplay,
               m_savedDrm.mode.vrefresh, restored ? ", black" : "");

    // A child that drew a mouse pointer (cage does, when there is a mouse) can
    // leave it on the CRTC's cursor plane, over the menus. The app draws none.
    if (drmModeSetCursor(fd, m_savedDrm.crtcId, 0, 0, 0) < 0)
        qDebug("[DisplayHandoff] Clearing the cursor failed: %s", strerror(errno));

    m_savedDrm.valid = false;
}

// A dumb buffer of width x height, 32-bit XRGB, mapped, as the cover; the one
// there kept when it is of that size. Never called while the cover is on the
// screen: removing a frame buffer that is shown turns its CRTC off.
bool DisplayHandoff::ensureCover(int fd, uint32_t width, uint32_t height) {
    if (m_cover.fbId && m_cover.fd == fd && m_cover.width == width && m_cover.height == height)
        return true;
    if (m_cover.fbId) {
        if (m_cover.pixels)
            ::munmap(m_cover.pixels, m_cover.size);
        if (m_cover.fd >= 0 && ::fcntl(m_cover.fd, F_GETFD) != -1) {
            drmModeRmFB(m_cover.fd, m_cover.fbId);
            drm_mode_destroy_dumb destroy {};
            destroy.handle = m_cover.handle;
            drmIoctl(m_cover.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        }
        m_cover = {};
    }
    drm_mode_create_dumb create {};
    create.width = width;
    create.height = height;
    create.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
        qWarning("[DisplayHandoff] No cover for the screen (%ux%u): %s", width, height, strerror(errno));
        return false;
    }
    const auto destroy = [fd, &create] {
        drm_mode_destroy_dumb gone {};
        gone.handle = create.handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &gone);
    };
    uint32_t fbId = 0;
    if (drmModeAddFB(fd, width, height, 24, 32, create.pitch, create.handle, &fbId) != 0) {
        qWarning("[DisplayHandoff] No cover for the screen (a frame buffer of %ux%u): %s",
                 width, height, strerror(errno));
        destroy();
        return false;
    }
    drm_mode_map_dumb map {};
    map.handle = create.handle;
    void *pixels = MAP_FAILED;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) == 0)
        pixels = ::mmap(nullptr, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, off_t(map.offset));
    if (pixels == MAP_FAILED) {
        qWarning("[DisplayHandoff] No cover for the screen (its memory): %s", strerror(errno));
        drmModeRmFB(fd, fbId);
        destroy();
        return false;
    }
    m_cover.fd = fd;
    m_cover.handle = create.handle;
    m_cover.fbId = fbId;
    m_cover.width = width;
    m_cover.height = height;
    m_cover.pitch = create.pitch;
    m_cover.size = create.size;
    m_cover.pixels = static_cast<unsigned char *>(pixels);
    return true;
}

void DisplayHandoff::showCover(int fd) {
    const uint32_t width = m_savedDrm.mode.hdisplay;
    const uint32_t height = m_savedDrm.mode.vdisplay;
    if (!width || !height || !ensureCover(fd, width, height))
        return;
    QElapsedTimer took;
    took.start();
    if (!m_coverPainter(int(width), int(height), m_cover.pixels, int(m_cover.pitch))) {
        qWarning("[DisplayHandoff] The app's frame could not be painted for the cover: none this time");
        return;
    }
    m_cover.black = false;
    // Master again for the moment it takes, as before the drop: no one
    // else has taken it, the child not started yet.
    if (::ioctl(fd, DRM_IOCTL_SET_MASTER, 0) < 0) {
        qWarning("[DisplayHandoff] No cover: drmSetMaster failed: %s", strerror(errno));
        return;
    }
    const bool shown = drmModeSetCrtc(fd, m_savedDrm.crtcId, m_cover.fbId, 0, 0,
                                      &m_savedDrm.connectorId, 1, &m_savedDrm.mode) == 0;
    const int setError = errno;
    if (::ioctl(fd, DRM_IOCTL_DROP_MASTER, 0) < 0)
        qWarning("[DisplayHandoff] DRM master could not be dropped again after the cover: %s",
                 strerror(errno));
    if (!shown) {
        qWarning("[DisplayHandoff] The cover could not be shown (%s): the child finds the app's own frame",
                 strerror(setError));
        return;
    }
    m_cover.shown = true;
    m_coverTimer->start();
    qDebug("[DisplayHandoff] Cover %ux%u shown, painted in %lld ms", width, height, qint64(took.elapsed()));
}

void DisplayHandoff::blackenCover() {
    if (!m_cover.pixels || m_cover.black)
        return;
    std::memset(m_cover.pixels, 0, m_cover.size);
    m_cover.black = true;
}

void DisplayHandoff::checkCover() {
    if (!m_cover.shown || m_qtDrmFd < 0) {
        m_coverTimer->stop();
        return;
    }
    drmModeCrtcPtr crtc = drmModeGetCrtc(m_qtDrmFd, m_savedDrm.crtcId);
    const uint32_t onScreen = crtc ? crtc->buffer_id : m_cover.fbId;
    if (crtc)
        drmModeFreeCrtc(crtc);
    // The child's own picture up (or the screen off): what it puts back as
    // it ends is black.
    if (onScreen != m_cover.fbId) {
        blackenCover();
        m_coverTimer->stop();
    }
}
#endif
