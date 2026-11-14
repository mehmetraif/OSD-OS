#pragma once

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <memory>
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
#include <sys/stat.h>
#include <unistd.h>
#endif

// Where the files go that OSD/OS makes for a run and hands to mpv: its key
// bindings, control socket and log, the deck menu's subtitle names, a logo's
// overlay, and the fontconfig file that shows it the VCR font. They used to
// lie in the temp folder under fixed names, and on Linux and the BSDs that
// folder (/tmp) is every account's to write in: another account could put a
// file of its own there first, key bindings that run a command among them,
// or read a media server's token out of mpv's log.
//
// On Linux and the BSDs the folder is the user's run-time folder, which only
// they can open (QStandardPaths::RuntimeLocation): $XDG_RUNTIME_DIR, which a
// login session sets (/run/user/<uid>); without one, as under the autostart
// service, /tmp/runtime-<user>, which Qt makes for its owner alone and names
// in the log. Where neither can be had (the name taken by someone else), a
// folder made for this run alone, gone when the app ends. On a Mac and on
// Windows the temp folder is the user's own already ($TMPDIR, %TEMP%), and
// the files stay there.
//
// Asked for each time a file is about to be made, not once: a cleaner may
// have taken a folder under /tmp away since (systemd-tmpfiles, after ten idle
// days), and it is made again then.
namespace runtimefiles {

#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
// A folder of the user's own that nobody else can open, and no link to one.
inline bool ownAlone(const QString &path) {
    struct stat info;
    return ::lstat(QFile::encodeName(path).constData(), &info) == 0 && S_ISDIR(info.st_mode)
           && info.st_uid == ::geteuid() && (info.st_mode & 0777) == 0700;
}
#endif

// The folder; "" where none could be had.
inline QString dir() {
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
    static QMutex mutex;
    static QString folder;
    static QByteArray askedWith;
    static std::unique_ptr<QTemporaryDir> thisRun;
    const QMutexLocker locker(&mutex);
    // Kept while it is still there and still the user's alone, so that Qt
    // names its stand-in for $XDG_RUNTIME_DIR in the log once, not at every
    // video.
    const QByteArray session = qgetenv("XDG_RUNTIME_DIR");
    if (!folder.isEmpty() && session == askedWith && ownAlone(folder))
        return folder;
    askedWith = session;
    folder = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!folder.isEmpty() && ownAlone(folder))
        return folder;
    thisRun.reset(new QTemporaryDir(QDir::tempPath() + QStringLiteral("/osdos-XXXXXX")));
    folder = thisRun->isValid() ? thisRun->path() : QString();
    if (folder.isEmpty())
        qWarning("[RuntimeFiles] No folder of the user's own for this run's files: %s",
                 qPrintable(thisRun->errorString()));
    else
        qWarning("[RuntimeFiles] No run-time folder of the user's own: this run's files are in %s",
                 qPrintable(folder));
    return folder;
#else
    return QDir::tempPath();
#endif
}

// `name` in that folder; "" where there is none, which a caller takes for a
// file that couldn't be written.
inline QString path(const QString &name) {
    const QString folder = dir();
    return folder.isEmpty() ? QString() : folder + QLatin1Char('/') + name;
}

} // namespace runtimefiles
