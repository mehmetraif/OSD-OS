#include "RemovableDrives.h"
#include "util/LegacyNames.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>
#include <algorithm>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#elif defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#include <QStorageInfo>
#endif
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// `path` is `dir` or in it.
bool within(const QString &path, const QString &dir) {
    if (dir.isEmpty())
        return false;
    return path == dir || path.startsWith(dir.endsWith(QLatin1Char('/')) ? dir : dir + QLatin1Char('/'));
}

QString cleanAbsolute(const QString &path) {
    return path.isEmpty() ? QString() : QDir::cleanPath(QDir(path).absolutePath());
}

#ifdef Q_OS_LINUX
QString mountinfoPath() {
    return legacy::env("MOUNTINFO", QStringLiteral("/proc/self/mountinfo"));
}

bool isOctal(char c) {
    return c >= '0' && c <= '7';
}

// A mount table's field with its octal escapes (\040 for a space) undone.
QString unescape(const QByteArray &field) {
    QByteArray out;
    out.reserve(field.size());
    for (int i = 0; i < field.size(); ++i) {
        const char c = field.at(i);
        if (c == '\\' && i + 3 < field.size() && isOctal(field.at(i + 1))
                && isOctal(field.at(i + 2)) && isOctal(field.at(i + 3))) {
            out.append(char(((field.at(i + 1) - '0') << 6) | ((field.at(i + 2) - '0') << 3)
                            | (field.at(i + 3) - '0')));
            i += 3;
        } else {
            out.append(c);
        }
    }
    return QString::fromUtf8(out);
}

// Where /etc/fstab mounts things: the system's own, never taken out.
QSet<QString> fstabMountPoints() {
    QSet<QString> points;
    QFile f(QStringLiteral("/etc/fstab"));
    if (!f.open(QIODevice::ReadOnly))
        return points;
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.simplified();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QList<QByteArray> fields = line.split(' ');
        if (fields.size() >= 2)
            points.insert(QDir::cleanPath(unescape(fields.at(1))));
    }
    return points;
}
#endif

} // namespace

RemovableDrives::RemovableDrives(QObject *parent) : QObject(parent) {
    // A drive with two partitions mounts twice in a moment: one look for both.
    m_settle = new QTimer(this);
    m_settle->setSingleShot(true);
    m_settle->setInterval(300);
    connect(m_settle, &QTimer::timeout, this, &RemovableDrives::rescan);
#ifdef Q_OS_LINUX
    // The kernel flags its mount table (POLLPRI) at every mount and unmount;
    // a file standing in for it (tests) is watched instead.
    const QString path = mountinfoPath();
    m_fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    if (m_fd >= 0) {
        m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Exception, this);
        connect(m_notifier, &QSocketNotifier::activated, m_settle, qOverload<>(&QTimer::start));
    }
    if (!path.startsWith(QLatin1String("/proc/"))) {
        m_watcher = new QFileSystemWatcher(QStringList(path), this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, m_settle, qOverload<>(&QTimer::start));
    }
#elif defined(Q_OS_MACOS)
    // A volume's folder in /Volumes comes as it mounts and goes as it is
    // ejected; the settle gives the mount its moment.
    m_watcher = new QFileSystemWatcher(QStringList(QStringLiteral("/Volumes")), this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, m_settle, qOverload<>(&QTimer::start));
#elif defined(Q_OS_WIN)
    // Nothing says when a drive comes or goes: the volumes are looked at
    // every few seconds instead.
    auto *poll = new QTimer(this);
    poll->setInterval(3000);
    connect(poll, &QTimer::timeout, this, &RemovableDrives::rescan);
    poll->start();
#endif
    m_drives = scan();
}

RemovableDrives::~RemovableDrives() {
    // The notifier goes before the descriptor it watches.
    delete m_notifier;
    m_notifier = nullptr;
#ifdef Q_OS_UNIX
    if (m_fd >= 0)
        ::close(m_fd);
#endif
}

bool RemovableDrives::holds(const QString &path) const {
    const QString clean = cleanAbsolute(path);
    return std::any_of(m_drives.cbegin(), m_drives.cend(),
                       [&clean](const Drive &d) { return within(clean, d.path); });
}

void RemovableDrives::setMediaRoot(const QString &path) {
    m_mediaRoot = cleanAbsolute(path);
    rescan();
}

QList<RemovableDrives::Drive> RemovableDrives::scan() const {
    QList<Drive> found;
#ifdef Q_OS_LINUX
    QFile f(mountinfoPath());
    if (f.open(QIODevice::ReadOnly)) {
        const QSet<QString> fstab = fstabMountPoints();
        const QList<QByteArray> lines = f.readAll().split('\n');
        for (const QByteArray &line : lines) {
            // 36 35 98:0 /mnt1 /mnt2 rw,noatime master:1 - ext3 /dev/root rw
            // (mount point fifth, file system type and source after the "-").
            const QList<QByteArray> fields = line.split(' ');
            const int dash = int(fields.indexOf(QByteArray("-")));
            if (dash < 6 || dash + 2 >= fields.size())
                continue;
            const QString point = QDir::cleanPath(unescape(fields.at(4)));
            if (!fields.at(dash + 2).startsWith("/dev/") || fstab.contains(point))
                continue;
            if (point.startsWith(QLatin1String("/media/")) || point.startsWith(QLatin1String("/run/media/")))
                found.append({ QFileInfo(point).fileName(), point });
        }
    }
#elif defined(Q_OS_MACOS)
    const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &volume : volumes) {
        const QString point = QDir::cleanPath(volume.rootPath());
        if (volume.isValid() && volume.isReady() && point.startsWith(QLatin1String("/Volumes/"))
                && volume.device().startsWith("/dev/"))
            found.append({ QFileInfo(point).fileName(), point });
    }
#elif defined(Q_OS_WIN)
    // The removable drives (USB sticks, card readers) among the volumes, by
    // their label, else their letter.
    const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &volume : volumes) {
        if (!volume.isValid() || !volume.isReady())
            continue;
        const QString root = QDir::toNativeSeparators(volume.rootPath()); // "E:\"
        if (::GetDriveTypeW(reinterpret_cast<const wchar_t *>(root.utf16())) != DRIVE_REMOVABLE)
            continue;
        const QString point = QDir::cleanPath(volume.rootPath());         // "E:/"
        QString name = volume.name().trimmed();
        if (name.isEmpty())
            name = point.left(2);
        found.append({ name, point });
    }
#endif
    QList<Drive> drives;
    for (const Drive &d : found) {
        // Local Files shows the media folder, and what it holds, already.
        if (d.name.isEmpty() || within(d.path, m_mediaRoot) || within(m_mediaRoot, d.path))
            continue;
        // One mounted over another, or twice: once.
        if (std::none_of(drives.cbegin(), drives.cend(), [&d](const Drive &o) { return o.path == d.path; }))
            drives.append(d);
    }
    std::sort(drives.begin(), drives.end(), [](const Drive &a, const Drive &b) {
        const int byName = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.path < b.path;
    });
    return drives;
}

void RemovableDrives::rescan() {
#ifdef Q_OS_LINUX
    // A stand-in file replaced rather than written over is no longer watched.
    if (m_watcher && m_watcher->files().isEmpty() && QFileInfo::exists(mountinfoPath()))
        m_watcher->addPath(mountinfoPath());
#endif
    const QList<Drive> now = scan();
    if (now == m_drives)
        return;
    QStringList gone;
    for (const Drive &d : std::as_const(m_drives)) {
        if (std::none_of(now.cbegin(), now.cend(), [&d](const Drive &n) { return n.path == d.path; })) {
            gone << d.path;
            qInfo("[LocalFiles] drive out: %s", qPrintable(d.path));
        }
    }
    for (const Drive &d : now) {
        if (std::none_of(m_drives.cbegin(), m_drives.cend(), [&d](const Drive &o) { return o.path == d.path; }))
            qInfo("[LocalFiles] drive in: %s", qPrintable(d.path));
    }
    m_drives = now;
    emit changed(gone);
}
