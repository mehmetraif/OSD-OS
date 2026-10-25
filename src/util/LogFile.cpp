#include "LogFile.h"

#include <QFile>
#include <QMutex>
#include <QStringList>
#include <QTime>
#include <cstdio>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace logfile {
namespace {

constexpr qint64 kMostBytes = 16 * 1024 * 1024;
constexpr int kMostEarlyLines = 1000;

QMutex g_mutex;
QtMessageHandler g_previous = nullptr;
bool g_begun = false;
QFile *g_file = nullptr; // open once the data folder is known
QStringList g_early;     // what was logged before it was
qint64 g_written = 0;

// Under g_mutex. Nothing in it logs, so no line comes back to the handler.
void writeLine(const QString &line) {
    if (!g_file) {
        if (g_early.size() < kMostEarlyLines)
            g_early.append(line);
        return;
    }
    if (g_written >= kMostBytes)
        return;
    const qint64 written = g_file->write(line.toUtf8());
    if (written > 0)
        g_written += written;
    if (g_written >= kMostBytes)
        g_file->write("[log] 16 MB written: the rest of this run is not kept here\n");
    // Each line on disk as it comes, so that a crash leaves the lines before it.
    g_file->flush();
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    const QString line = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")) + QLatin1Char(' ')
                         + qFormatLogMessage(type, context, message) + QLatin1Char('\n');
    {
        QMutexLocker lock(&g_mutex);
        writeLine(line);
    }
    if (g_previous)
        g_previous(type, context, message);
}

#ifdef Q_OS_WIN
bool sentElsewhere(DWORD which) {
    const HANDLE handle = GetStdHandle(which);
    return handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

// A windowed program has no console of its own. Started from one, it writes
// there all the same, where its output isn't already sent to a file or a
// pipe (`> osdos.txt`, or QProcess's when the app asks a copy of itself for
// a core's version, --core-info).
void attachParentConsole() {
    const bool out = sentElsewhere(STD_OUTPUT_HANDLE);
    const bool err = sentElsewhere(STD_ERROR_HANDLE);
    if ((out && err) || !AttachConsole(ATTACH_PARENT_PROCESS))
        return;
    FILE *stream = nullptr;
    if (!out)
        freopen_s(&stream, "CONOUT$", "w", stdout);
    if (!err)
        freopen_s(&stream, "CONOUT$", "w", stderr);
}
#endif

} // namespace

void begin() {
    QMutexLocker lock(&g_mutex);
    if (g_begun)
        return;
    g_begun = true;
#ifdef Q_OS_WIN
    attachParentConsole();
#endif
    g_previous = qInstallMessageHandler(handler);
}

void open(const QString &dataRoot) {
    const QString path = dataRoot + QStringLiteral("/osdos.log");
    const QString previous = dataRoot + QStringLiteral("/osdos-previous.log");
    QFile::remove(previous);
    QFile::rename(path, previous);
    auto *file = new QFile(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        delete file;
        file = nullptr;
    }
    QMutexLocker lock(&g_mutex);
    delete g_file;
    g_file = file;
    g_written = 0;
    if (!g_file)
        return;
    const QStringList early = std::exchange(g_early, QStringList());
    for (const QString &line : early)
        writeLine(line);
}

void end() {
    QMutexLocker lock(&g_mutex);
    if (!g_begun)
        return;
    qInstallMessageHandler(g_previous);
    g_previous = nullptr;
    g_begun = false;
    delete g_file;
    g_file = nullptr;
    g_early.clear();
    g_written = 0;
}

} // namespace logfile
