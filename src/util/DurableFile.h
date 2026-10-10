#pragma once
#include <QFile>
#include <QString>
#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

// Empty on success; propagate delayed write-back errors to the download UI.
inline QString syncPath(const QString &path, bool directory = false) {
#ifdef Q_OS_UNIX
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_RDONLY | (directory ? O_DIRECTORY : 0));
    if (fd < 0) return QString::fromLocal8Bit(std::strerror(errno));
    int result;
    do { result = ::fsync(fd); } while (result < 0 && errno == EINTR);
    const int error = result < 0 ? errno : 0;
    const int closed = ::close(fd);
    if (error) return QString::fromLocal8Bit(std::strerror(error));
    if (closed < 0) return QString::fromLocal8Bit(std::strerror(errno));
#else
    Q_UNUSED(path)
    Q_UNUSED(directory)
#endif
    return {};
}
