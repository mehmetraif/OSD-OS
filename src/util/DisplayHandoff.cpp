#include "DisplayHandoff.h"
#include <QDir>
#include <QFile>
#include <QStringList>
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

#ifdef Q_OS_LINUX
namespace {

// Qt's own word of a flip the kernel refused, counted from its warnings: the
// atomic path's "Failed to commit atomic request (code=...)" and the legacy
// one's "Could not queue DRM page flip". Qt reports nothing else of it.
std::atomic<quint64> g_refusedFlips { 0 };
QtMessageHandler g_nextHandler = nullptr;

void countRefusedFlips(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    if (type == QtWarningMsg
            && (message.startsWith(QLatin1String("Failed to commit atomic request"))
                || message.startsWith(QLatin1String("Could not queue DRM page flip"))))
        g_refusedFlips.fetch_add(1, std::memory_order_relaxed);
    if (g_nextHandler)
        g_nextHandler(type, context, message);
}

bool qtUsesAtomic() {
    return qEnvironmentVariableIntValue("QT_QPA_EGLFS_KMS_ATOMIC") != 0;
}

// A property a hand-back puts back: settable, and not a blob (one may be gone
// by then; the mode goes back from the saved mode instead), nor one an
// atomic commit refuses or that is not state (fences, damage, DPMS, which
// only the legacy call takes, and link-status, which only goes to "Good").
bool restorable(const drmModePropertyRes *p) {
    if (p->flags & (DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_BLOB))
        return false;
    static const char *const skipped[] = { "DPMS", "link-status", "IN_FENCE_FD", "OUT_FENCE_PTR",
                                           "HOTSPOT_X", "HOTSPOT_Y" };
    for (const char *name : skipped) {
        if (std::strcmp(p->name, name) == 0)
            return false;
    }
    return true;
}

// The value of object's property name, as the kernel has it now.
bool propValue(int fd, uint32_t object, uint32_t type, const char *name, uint64_t *value, uint32_t *id = nullptr) {
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, object, type);
    if (!props)
        return false;
    bool found = false;
    for (uint32_t i = 0; i < props->count_props && !found; ++i) {
        drmModePropertyPtr p = drmModeGetProperty(fd, props->props[i]);
        if (!p)
            continue;
        if (std::strcmp(p->name, name) == 0) {
            found = true;
            if (value)
                *value = props->prop_values[i];
            if (id)
                *id = p->prop_id;
        }
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
    return found;
}

} // namespace
#endif

DisplayHandoff::DisplayHandoff(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_LINUX
    // Qt's refused flips counted (checkFramesShown()), every message passed
    // on to the handler before (util/LogFile's, then Qt's own).
    if (isHeadless())
        g_nextHandler = qInstallMessageHandler(countRefusedFlips);
    m_verifyTimer = new QTimer(this);
    m_verifyTimer->setInterval(1000);
    connect(m_verifyTimer, &QTimer::timeout, this, &DisplayHandoff::checkFramesShown);
#endif
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
    // A hand-back still being watched is over: the screen goes again.
    m_verifyTimer->stop();
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
            const uint32_t crtcId = m_savedDrm.crtcId;
            const bool restored = m_savedDrm.valid;
            restoreDrmCrtcState(m_qtDrmFd, useCover);
            // Whether Qt's frames reach the screen now, watched for a few
            // seconds (checkFramesShown()); not at shutdown, with nothing
            // drawn after.
            if (restored && useCover) {
                m_verifyFd = m_qtDrmFd;
                m_verifyCrtc = crtcId;
                m_framesBefore = m_frames.load(std::memory_order_relaxed);
                m_refusedBefore = g_refusedFlips.load(std::memory_order_relaxed);
                m_checksLeft = 6;
                m_verifyTimer->start();
            }
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

    // With Qt on atomic KMS, all it had: the connector's, the CRTC's and its
    // planes' settable properties (a child may leave any of them changed),
    // the cursor planes aside (Qt's legacy cursor, cleared at the hand-back).
    if (m_savedDrm.valid && qtUsesAtomic()) {
        int crtcIndex = -1;
        for (int i = 0; i < res->count_crtcs; ++i) {
            if (res->crtcs[i] == m_savedDrm.crtcId)
                crtcIndex = i;
        }
        saveProps(fd, m_savedDrm.connectorId, DRM_MODE_OBJECT_CONNECTOR);
        saveProps(fd, m_savedDrm.crtcId, DRM_MODE_OBJECT_CRTC);
        propValue(fd, m_savedDrm.crtcId, DRM_MODE_OBJECT_CRTC, "MODE_ID", nullptr, &m_savedDrm.modeProp);
        drmModePlaneResPtr planes = drmModeGetPlaneResources(fd);
        for (uint32_t i = 0; planes && crtcIndex >= 0 && i < planes->count_planes; ++i) {
            drmModePlanePtr plane = drmModeGetPlane(fd, planes->planes[i]);
            if (!plane)
                continue;
            uint64_t type = 0;
            if ((plane->possible_crtcs & (1u << crtcIndex))
                    && propValue(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, "type", &type)
                    && type != DRM_PLANE_TYPE_CURSOR) {
                saveProps(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
                if (type == DRM_PLANE_TYPE_PRIMARY && plane->crtc_id == m_savedDrm.crtcId)
                    m_savedDrm.planeId = plane->plane_id;
            }
            drmModeFreePlane(plane);
        }
        if (planes)
            drmModeFreePlaneResources(planes);
    }
    drmModeFreeResources(res);

    if (m_savedDrm.valid)
        qDebug("[DisplayHandoff] Saved CRTC %u connector %u mode %dx%d@%d, %zu properties",
               m_savedDrm.crtcId, m_savedDrm.connectorId,
               m_savedDrm.mode.hdisplay, m_savedDrm.mode.vdisplay,
               m_savedDrm.mode.vrefresh, m_savedDrm.props.size());
    else
        qWarning("[DisplayHandoff] Could not save CRTC state");
}

void DisplayHandoff::saveProps(int fd, uint32_t object, uint32_t type) {
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, object, type);
    if (!props)
        return;
    for (uint32_t i = 0; i < props->count_props; ++i) {
        drmModePropertyPtr p = drmModeGetProperty(fd, props->props[i]);
        if (!p)
            continue;
        if (restorable(p))
            m_savedDrm.props.push_back({ object, p->prop_id, props->prop_values[i], p->name });
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
}

void DisplayHandoff::restoreDrmCrtcState(int fd, bool useCover) {
    if (!m_savedDrm.valid) return;

    // The cover, black, where this hand-off showed one: the app's last frame
    // (the loading screen it handed the screen over from) never comes back
    // on the screen, which shows black until the app's next frame. Should
    // the cover not be taken, Qt's own frame buffer, as before covers.
    const bool cover = useCover && m_cover.shown && m_cover.fd == fd;
    if (cover)
        blackenCover();
    // What Qt had, whole, where it is on atomic KMS; the CRTC alone by the
    // legacy call otherwise, or should the commit be refused.
    const char *how = nullptr;
    if (restoreAtomic(fd, cover ? m_cover.fbId : m_savedDrm.fbId))
        how = "Qt's display state committed whole";
    if (!how && cover) {
        if (drmModeSetCrtc(fd, m_savedDrm.crtcId, m_cover.fbId, 0, 0,
                           &m_savedDrm.connectorId, 1, &m_savedDrm.mode) == 0)
            how = "the CRTC set to the cover";
        else
            qWarning("[DisplayHandoff] The cover could not be put back (%s): the app's last frame is",
                     strerror(errno));
    }
    if (!how) {
        if (drmModeSetCrtc(fd, m_savedDrm.crtcId, m_savedDrm.fbId, m_savedDrm.x, m_savedDrm.y,
                           &m_savedDrm.connectorId, 1, &m_savedDrm.mode) == 0)
            how = "the CRTC set to the app's last frame";
        else
            qWarning("[DisplayHandoff] drmModeSetCrtc restore failed: %s", strerror(errno));
    }
    if (how)
        qInfo("[DisplayHandoff] Screen back (mode %dx%d@%d): %s",
              m_savedDrm.mode.hdisplay, m_savedDrm.mode.vdisplay, m_savedDrm.mode.vrefresh, how);

    // A child that drew a mouse pointer (cage does, when there is a mouse) can
    // leave it on the CRTC's cursor plane, over the menus. The app draws none.
    if (drmModeSetCursor(fd, m_savedDrm.crtcId, 0, 0, 0) < 0)
        qDebug("[DisplayHandoff] Clearing the cursor failed: %s", strerror(errno));

    m_savedDrm.valid = false;
}

bool DisplayHandoff::restoreAtomic(int fd, uint32_t fbId) {
    if (m_savedDrm.props.empty() || !m_savedDrm.planeId || !m_savedDrm.modeProp)
        return false;
    // The mode as a blob of our own: the one the CRTC had may be gone with
    // the child's state.
    uint32_t blob = 0;
    if (drmModeCreatePropertyBlob(fd, &m_savedDrm.mode, sizeof m_savedDrm.mode, &blob) != 0) {
        qWarning("[DisplayHandoff] Qt's display state can't be committed whole (no mode blob: %s)",
                 strerror(errno));
        return false;
    }
    drmModeAtomicReqPtr request = drmModeAtomicAlloc();
    if (!request) {
        drmModeDestroyPropertyBlob(fd, blob);
        return false;
    }
    for (const DrmPropValue &p : m_savedDrm.props) {
        const bool frame = p.object == m_savedDrm.planeId && p.name == "FB_ID";
        drmModeAtomicAddProperty(request, p.object, p.prop, frame ? fbId : p.value);
    }
    drmModeAtomicAddProperty(request, m_savedDrm.crtcId, m_savedDrm.modeProp, blob);
    const int ret = drmModeAtomicCommit(fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
    drmModeAtomicFree(request);
    // The CRTC keeps the mode it was given; the blob's id is ours to let go.
    drmModeDestroyPropertyBlob(fd, blob);
    if (ret != 0) {
        qWarning("[DisplayHandoff] Qt's display state could not be committed whole (%s)", strerror(-ret));
        return false;
    }
    return true;
}

void DisplayHandoff::checkFramesShown() {
    if (!m_owner.isEmpty() || m_verifyFd < 0) {
        m_verifyTimer->stop();
        return;
    }
    const quint64 drawn = m_frames.load(std::memory_order_relaxed) - m_framesBefore;
    const quint64 refused = g_refusedFlips.load(std::memory_order_relaxed) - m_refusedBefore;
    // Frames getting through (a refusal or two as the hand-back settles
    // aside): the screen is the app's again.
    if (drawn >= refused + 2) {
        qInfo("[DisplayHandoff] Screen back and confirmed: the app's frames reach it "
              "(%llu drawn, %llu refused since the hand-back)", drawn, refused);
        m_verifyTimer->stop();
        return;
    }
    // Every frame drawn refused, enough of them to tell: the menus run on
    // unseen, and only a fresh start sets the mode again.
    if (refused >= 3 && refused + 1 >= drawn) {
        qWarning("[DisplayHandoff] Screen lost: the kernel refused the %llu frames the app drew since "
                 "the hand-back; the screen as it is:", refused);
        logScreenState(m_verifyFd);
        m_verifyTimer->stop();
        emit screenLost();
        return;
    }
    if (--m_checksLeft <= 0) {
        qInfo("[DisplayHandoff] Screen back, not confirmed: %llu frames drawn, %llu refused since the hand-back",
              drawn, refused);
        m_verifyTimer->stop();
    }
}

void DisplayHandoff::logScreenState(int fd) const {
    uint64_t active = 0;
    propValue(fd, m_verifyCrtc, DRM_MODE_OBJECT_CRTC, "ACTIVE", &active);
    if (drmModeCrtcPtr crtc = drmModeGetCrtc(fd, m_verifyCrtc)) {
        qWarning("[DisplayHandoff]   CRTC %u: %s, mode %s %ux%u@%u, frame buffer %u", crtc->crtc_id,
                 active ? "active" : "not active", crtc->mode_valid ? crtc->mode.name : "(none)",
                 crtc->mode.hdisplay, crtc->mode.vdisplay, crtc->mode.vrefresh, crtc->buffer_id);
        drmModeFreeCrtc(crtc);
    }
    // The primary plane on that CRTC, every property of it.
    uint32_t planeId = 0;
    if (drmModePlaneResPtr planes = drmModeGetPlaneResources(fd)) {
        for (uint32_t i = 0; i < planes->count_planes && !planeId; ++i) {
            uint64_t type = 0, crtcId = 0;
            if (propValue(fd, planes->planes[i], DRM_MODE_OBJECT_PLANE, "type", &type)
                    && propValue(fd, planes->planes[i], DRM_MODE_OBJECT_PLANE, "CRTC_ID", &crtcId)
                    && type == DRM_PLANE_TYPE_PRIMARY && crtcId == m_verifyCrtc)
                planeId = planes->planes[i];
        }
        drmModeFreePlaneResources(planes);
    }
    if (drmModeObjectPropertiesPtr props = planeId ? drmModeObjectGetProperties(fd, planeId, DRM_MODE_OBJECT_PLANE)
                                                   : nullptr) {
        QStringList values;
        for (uint32_t i = 0; i < props->count_props; ++i) {
            if (drmModePropertyPtr p = drmModeGetProperty(fd, props->props[i])) {
                values << QStringLiteral("%1=%2").arg(QLatin1String(p->name)).arg(props->prop_values[i]);
                drmModeFreeProperty(p);
            }
        }
        drmModeFreeObjectProperties(props);
        qWarning("[DisplayHandoff]   plane %u: %s", planeId, qPrintable(values.join(QLatin1Char(' '))));
    } else {
        qWarning("[DisplayHandoff]   no primary plane on the CRTC");
    }
    // The app's frame buffers (its DRM file's), and whether the kernel would
    // take a flip of each onto that plane as Qt asks for one.
    uint32_t fbProp = 0, crtcProp = 0;
    const bool canTest = qtUsesAtomic() && planeId
                         && propValue(fd, planeId, DRM_MODE_OBJECT_PLANE, "FB_ID", nullptr, &fbProp)
                         && propValue(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_ID", nullptr, &crtcProp);
    drmModeResPtr res = drmModeGetResources(fd);
    for (int i = 0; res && i < res->count_fbs; ++i) {
        const uint32_t id = res->fbs[i];
        QString line = QStringLiteral("frame buffer %1").arg(id);
        if (drmModeFBPtr fb = drmModeGetFB(fd, id)) {
            line += QStringLiteral(": %1x%2, %3 bits a pixel, pitch %4").arg(fb->width).arg(fb->height)
                        .arg(fb->bpp).arg(fb->pitch);
            drmModeFreeFB(fb);
        }
        if (id == m_cover.fbId)
            line += QStringLiteral(" (the cover)");
        if (canTest) {
            drmModeAtomicReqPtr test = drmModeAtomicAlloc();
            drmModeAtomicAddProperty(test, planeId, fbProp, id);
            drmModeAtomicAddProperty(test, planeId, crtcProp, m_verifyCrtc);
            const int ret = drmModeAtomicCommit(fd, test,
                                                DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
            drmModeAtomicFree(test);
            line += ret == 0 ? QStringLiteral(", a flip of it would be taken")
                             : QStringLiteral(", a flip of it would be refused: %1").arg(QString::fromLocal8Bit(strerror(-ret)));
        }
        qWarning("[DisplayHandoff]   %s", qPrintable(line));
    }
    if (res)
        drmModeFreeResources(res);
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
