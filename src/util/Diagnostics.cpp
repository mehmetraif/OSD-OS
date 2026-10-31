#include "Diagnostics.h"

#include <QCoreApplication>
#include <QFile>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <csignal>
#include <ctime>
#include <dirent.h>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace diagnostics {

#ifdef Q_OS_LINUX
namespace {

// What a signal handler needs is ready before it runs: the paths and the
// version as fixed buffers, nothing it would have to allocate.
char g_crashPath[1024];
char g_stallPath[1024];
char g_version[64] = "OSD/OS";

void copyInto(char *buffer, size_t size, const QString &text, bool fileName) {
    const QByteArray bytes = fileName ? QFile::encodeName(text) : text.toUtf8();
    std::strncpy(buffer, bytes.constData(), size - 1);
    buffer[size - 1] = '\0';
}

void noteVersion() {
    const QString version = QCoreApplication::applicationVersion();
    copyInto(g_version, sizeof g_version,
             version.isEmpty() ? QStringLiteral("OSD/OS") : QStringLiteral("OSD/OS ") + version, false);
}

// write(2) and nothing else, as a signal handler may.
void writeAll(int fd, const char *text, size_t size) {
    while (size > 0) {
        const ssize_t n = ::write(fd, text, size);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return;
        text += n;
        size -= size_t(n);
    }
}

void writeText(int fd, const char *text) {
    writeAll(fd, text, std::strlen(text));
}

void writeNumber(int fd, long long value) {
    char digits[24];
    int at = int(sizeof digits);
    unsigned long long left = value < 0 ? 0ULL - (unsigned long long)value : (unsigned long long)value;
    do {
        digits[--at] = char('0' + left % 10);
        left /= 10;
    } while (left && at > 1);
    if (value < 0)
        digits[--at] = '-';
    writeAll(fd, digits + at, sizeof digits - size_t(at));
}

const char *signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS:  return "SIGBUS";
    case SIGILL:  return "SIGILL";
    case SIGFPE:  return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    default:      return "a signal";
    }
}

// The figures behind memoryLine(), read with open(2) and read(2) into the
// stack: the watchdog reads them while the main thread may be stuck holding
// the allocator's lock.
struct Memory {
    long rssKb = -1;
    long totalKb = -1;
    long availableKb = -1;
    long swapTotalKb = -1;
    long swapFreeKb = -1;
    // Files the app has open, and how many it may (its soft limit): a leak
    // of them ends in every new one refused, a GPU's buffers among them.
    long files = -1;
    long filesMax = -1;
};

// The entries of /proc/self/fd, counted without allocating (the watchdog's
// thread reads it while the main thread may hold the allocator's lock).
long openFiles() {
    const int dir = ::open("/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0)
        return -1;
    long count = 0;
    alignas(8) char buffer[4096];
    for (;;) {
        const long n = ::syscall(SYS_getdents64, dir, buffer, sizeof buffer);
        if (n <= 0)
            break;
        for (long at = 0; at < n;) {
            // struct linux_dirent64: d_ino, d_off, d_reclen, d_type, d_name.
            unsigned short length;
            std::memcpy(&length, buffer + at + 16, sizeof length);
            const char *name = buffer + at + 19;
            if (name[0] != '.')
                ++count;
            at += length;
        }
    }
    ::close(dir);
    // The directory's own, open while it was read.
    return count - 1;
}

bool readFile(const char *path, char *buffer, size_t size) {
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    size_t used = 0;
    while (used < size - 1) {
        const ssize_t n = ::read(fd, buffer + used, size - 1 - used);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        used += size_t(n);
    }
    ::close(fd);
    buffer[used] = '\0';
    return used > 0;
}

long fieldKb(const char *text, const char *name) {
    const char *at = std::strstr(text, name);
    if (!at)
        return -1;
    return std::strtol(at + std::strlen(name), nullptr, 10);
}

Memory readMemory() {
    Memory m;
    char text[4096];
    if (readFile("/proc/self/status", text, sizeof text))
        m.rssKb = fieldKb(text, "VmRSS:");
    if (readFile("/proc/meminfo", text, sizeof text)) {
        m.totalKb = fieldKb(text, "MemTotal:");
        m.availableKb = fieldKb(text, "MemAvailable:");
        m.swapTotalKb = fieldKb(text, "SwapTotal:");
        m.swapFreeKb = fieldKb(text, "SwapFree:");
    }
    m.files = openFiles();
    rlimit limit {};
    if (::getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY)
        m.filesMax = long(limit.rlim_cur);
    return m;
}

void formatMemory(const Memory &m, char *out, size_t size) {
    std::snprintf(out, size, "app %ld MB, available %ld of %ld MB, swap %ld of %ld MB in use, %ld of %ld files open",
                  m.rssKb / 1024, m.availableKb / 1024, m.totalKb / 1024,
                  (m.swapTotalKb - m.swapFreeKb) / 1024, m.swapTotalKb / 1024, m.files, m.filesMax);
}

// --- A crash ---

// A stack of its own for the handler, so that a stack overflow on the main
// thread is reported too.
alignas(16) char g_altStack[64 * 1024];

extern "C" void onCrash(int sig) {
    void *frames[64];
    const int count = backtrace(frames, 64);
    const int report = ::open(g_crashPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    for (int fd : { int(STDERR_FILENO), report }) {
        if (fd < 0)
            continue;
        writeText(fd, "[crash] ");
        writeText(fd, g_version);
        writeText(fd, " ended by ");
        writeText(fd, signalName(sig));
        writeText(fd, " at ");
        writeNumber(fd, (long long)::time(nullptr));
        writeText(fd, " (seconds since 1970). Where it was:\n");
        backtrace_symbols_fd(frames, count, fd);
    }
    if (report >= 0)
        ::close(report);
    // The handler reset itself on entry (SA_RESETHAND): the signal now ends
    // the process as it would have.
    ::raise(sig);
}

// --- A main thread that doesn't answer ---

// The main thread's latest beat, and the longest time between two since the
// last freeze reported over.
std::atomic<long long> g_beat { 0 };
std::atomic<long long> g_longestGap { 0 };
// How often it beats; how long it may be away between two beats before the
// journal is told, and how long between two such lines at least.
constexpr int kBeatMs = 250;
constexpr long long kAwayMs = 400;
constexpr long long kAwaySayMs = 10000;
long long g_lastAwaySaid = 0;
std::atomic<bool> g_watching { false };
std::thread *g_watcher = nullptr;
QTimer *g_beatTimer = nullptr;
long g_mainTid = 0;

// The thread asked where it is, the file its answer goes to, whether the
// journal gets it too, and the last thread that answered.
std::atomic<long> g_probeTid { 0 };
std::atomic<int> g_probeFd { -1 };
std::atomic<bool> g_probeToStderr { false };
std::atomic<long> g_answered { 0 };

int probeSignal() {
    return SIGRTMIN + 3;
}

long currentTid() {
    return long(::syscall(SYS_gettid));
}

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// On the thread asked, interrupted wherever it is: that place, from its own
// stack. A thread that answers too late writes nothing over the next one's.
extern "C" void onProbe(int) {
    const long tid = currentTid();
    if (tid != g_probeTid.load())
        return;
    const int savedErrno = errno;
    void *frames[64];
    const int count = backtrace(frames, 64);
    const int report = g_probeFd.load();
    if (report >= 0)
        backtrace_symbols_fd(frames, count, report);
    if (g_probeToStderr.load())
        backtrace_symbols_fd(frames, count, STDERR_FILENO);
    g_answered = tid;
    errno = savedErrno;
}

// Asks one thread where it is, waiting up to waitMs for its answer.
enum class Probe { Answered, NoAnswer, Gone };
Probe probe(long tid, int report, bool toStderr, int waitMs) {
    g_answered = 0;
    g_probeFd = report;
    g_probeToStderr = toStderr;
    g_probeTid = tid;
    Probe result = Probe::Gone;
    if (::syscall(SYS_tgkill, long(::getpid()), tid, long(probeSignal())) == 0) {
        for (int waited = 0; waited < waitMs && g_answered.load() != tid; waited += 10)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        result = g_answered.load() == tid ? Probe::Answered : Probe::NoAnswer;
    }
    g_probeTid = 0;
    g_probeFd = -1;
    return result;
}

// Every thread of the process, read from /proc without allocating.
template <typename Visit>
void forEachThread(Visit visit) {
    const int dir = ::open("/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0)
        return;
    alignas(8) char buffer[4096];
    for (;;) {
        const long n = ::syscall(SYS_getdents64, dir, buffer, sizeof buffer);
        if (n <= 0)
            break;
        for (long at = 0; at < n;) {
            const auto *entry = reinterpret_cast<const struct dirent64 *>(buffer + at);
            at += entry->d_reclen;
            const long tid = std::strtol(entry->d_name, nullptr, 10);
            if (tid > 0)
                visit(tid);
        }
    }
    ::close(dir);
}

// Whether a thread blocks the signal that would ask it, as Mesa's threads
// block every signal: it would never answer.
bool blocksProbe(long tid) {
    char path[64];
    char text[4096];
    std::snprintf(path, sizeof path, "/proc/self/task/%ld/status", tid);
    if (!readFile(path, text, sizeof text))
        return false;
    const char *at = std::strstr(text, "SigBlk:");
    if (!at)
        return false;
    const unsigned long long mask = std::strtoull(at + 7, nullptr, 16);
    return (mask >> (probeSignal() - 1)) & 1ULL;
}

void writeBoth(int report, const char *text) {
    writeText(STDERR_FILENO, text);
    if (report >= 0)
        writeText(report, text);
}

// Where the main thread is, to the journal and the report; then, to the
// report only, every other thread but the watcher: what the main thread
// waits for (the scene graph's render thread, a libmpv core's) shows there.
// False if a thread didn't answer, and may still write to the report.
bool reportThreads(int report) {
    writeBoth(report, "[watchdog] The main thread is here:\n");
    bool answered = probe(g_mainTid, report, true, 2000) == Probe::Answered;
    if (!answered)
        writeBoth(report, "[watchdog] The main thread didn't answer.\n");
    if (report < 0)
        return answered;
    const long self = currentTid();
    const long long until = nowMs() + 5000;
    forEachThread([&](long tid) {
        if (tid == g_mainTid || tid == self || nowMs() > until)
            return;
        char path[64];
        char name[32];
        std::snprintf(path, sizeof path, "/proc/self/task/%ld/comm", tid);
        if (readFile(path, name, sizeof name))
            name[std::strcspn(name, "\n")] = '\0';
        else
            std::strcpy(name, "?");
        char header[96];
        std::snprintf(header, sizeof header, "[watchdog] Thread %ld, %s:\n", tid, name);
        writeText(report, header);
        if (blocksProbe(tid)) {
            writeText(report, "  (blocks signals: not asked)\n");
            return;
        }
        switch (probe(tid, report, false, 250)) {
        case Probe::Answered:
            break;
        case Probe::NoAnswer:
            writeText(report, "  (no answer)\n");
            answered = false;
            break;
        case Probe::Gone:
            writeText(report, "  (gone)\n");
            break;
        }
    });
    return answered;
}

void appendToStallReport(const char *line) {
    const int fd = ::open(g_stallPath, O_WRONLY | O_APPEND | O_CLOEXEC);
    if (fd < 0)
        return;
    writeText(fd, line);
    ::close(fd);
}

void watch(int reportMs, int abortMs) {
    bool reported = false;
    char line[512];
    while (g_watching.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (!g_watching.load())
            break;
        const long long stalled = nowMs() - g_beat.load();
        if (stalled < reportMs) {
            if (reported) {
                std::snprintf(line, sizeof line,
                              "[watchdog] The main thread answered again, %.1f s after it stopped.\n",
                              double(g_longestGap.exchange(0)) / 1000.0);
                writeText(STDERR_FILENO, line);
                appendToStallReport(line);
                reported = false;
            }
            continue;
        }
        if (!reported) {
            reported = true;
            char memory[160];
            formatMemory(readMemory(), memory, sizeof memory);
            std::snprintf(line, sizeof line,
                          "[watchdog] %s: the main thread hasn't answered for %.1f s (%s), at %lld"
                          " (seconds since 1970).\n",
                          g_version, double(stalled) / 1000.0, memory, (long long)::time(nullptr));
            // Appending, as a thread that answers late may write to it still.
            const int report = ::open(g_stallPath, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
            writeBoth(report, line);
            // Left open while a thread that didn't answer may still write to it.
            if (reportThreads(report) && report >= 0)
                ::close(report);
        }
        if (abortMs > 0 && stalled >= abortMs) {
            std::snprintf(line, sizeof line,
                          "[watchdog] The main thread hasn't answered for %.1f s: the app starts again.\n",
                          double(stalled) / 1000.0);
            writeText(STDERR_FILENO, line);
            appendToStallReport(line);
            ::_exit(kStallExitCode);
        }
    }
}

} // namespace

void installCrashHandlers(const QString &dataRoot) {
    noteVersion();
    copyInto(g_crashPath, sizeof g_crashPath, dataRoot + QStringLiteral("/crash-report.txt"), true);
    // backtrace() loads libgcc the first time: here, not in a handler.
    void *frames[4];
    backtrace(frames, 4);
    stack_t stack {};
    stack.ss_sp = g_altStack;
    stack.ss_size = sizeof g_altStack;
    sigaltstack(&stack, nullptr);
    struct sigaction action {};
    action.sa_handler = onCrash;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND | SA_ONSTACK;
    for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT })
        sigaction(sig, &action, nullptr);
}

void startStallWatchdog(const QString &dataRoot, int reportMs, int abortMs) {
    if (g_watching.exchange(true))
        return;
    noteVersion();
    copyInto(g_stallPath, sizeof g_stallPath, dataRoot + QStringLiteral("/stall-report.txt"), true);
    void *frames[4];
    backtrace(frames, 4);
    g_mainTid = currentTid();
    struct sigaction action {};
    action.sa_handler = onProbe;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(probeSignal(), &action, nullptr);
    g_beat = nowMs();
    g_beatTimer = new QTimer(QCoreApplication::instance());
    g_beatTimer->setInterval(kBeatMs);
    QObject::connect(g_beatTimer, &QTimer::timeout, [] {
        const long long now = nowMs();
        const long long gap = now - g_beat.exchange(now);
        if (gap > g_longestGap.load())
            g_longestGap = gap;
        // Away for a while, though not stuck: the menus answered late. Said
        // in the journal with the memory, once in a while, so that a Pi
        // whose menus lag shows when and for how long.
        if (gap - kBeatMs >= kAwayMs && now - g_lastAwaySaid >= kAwaySayMs) {
            g_lastAwaySaid = now;
            char memory[160];
            formatMemory(readMemory(), memory, sizeof memory);
            qWarning("[watchdog] The main thread was away for %lld ms (%s)", gap - kBeatMs, memory);
        }
    });
    g_beatTimer->start();
    g_watcher = new std::thread(watch, reportMs, abortMs);
}

void stopStallWatchdog() {
    if (!g_watching.exchange(false))
        return;
    if (g_watcher) {
        g_watcher->join();
        delete g_watcher;
        g_watcher = nullptr;
    }
    delete g_beatTimer;
    g_beatTimer = nullptr;
}

QString memoryLine() {
    const Memory m = readMemory();
    if (m.rssKb < 0)
        return QString();
    char text[160];
    formatMemory(m, text, sizeof text);
    return QString::fromLatin1(text);
}

void startAgain(const char *reason) {
    ::setenv("OSDOS_STARTED_AGAIN", reason, 1);
    // Every file closed by the exec but the journal's (0-2).
    if (DIR *dir = ::opendir("/proc/self/fd")) {
        const int own = ::dirfd(dir);
        while (dirent *entry = ::readdir(dir)) {
            const int fd = std::atoi(entry->d_name);
            if (fd <= 2 || fd == own)
                continue;
            const int flags = ::fcntl(fd, F_GETFD);
            if (flags >= 0)
                ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }
        ::closedir(dir);
    }
    // The arguments as the kernel kept them, NUL after each.
    QByteArray cmdline;
    {
        QFile file(QStringLiteral("/proc/self/cmdline"));
        if (file.open(QIODevice::ReadOnly))
            cmdline = file.readAll();
    }
    std::vector<char *> argv;
    for (char *arg = cmdline.data(), *end = arg + cmdline.size(); arg < end; arg += std::strlen(arg) + 1)
        argv.push_back(arg);
    argv.push_back(nullptr);
    if (argv.size() > 1)
        ::execv("/proc/self/exe", argv.data());
    char line[256];
    std::snprintf(line, sizeof line,
                  "[main] OSD/OS could not start again in place (%s): the service starts it again.\n",
                  std::strerror(errno));
    writeText(STDERR_FILENO, line);
    ::_exit(kStallExitCode);
}

QString startedAgainReason() {
    static const QString reason = [] {
        const QString why = qEnvironmentVariable("OSDOS_STARTED_AGAIN");
        qunsetenv("OSDOS_STARTED_AGAIN");
        return why;
    }();
    return reason;
}

#else // !Q_OS_LINUX

void installCrashHandlers(const QString &) {}
void startStallWatchdog(const QString &, int, int) {}
void stopStallWatchdog() {}
QString memoryLine() { return QString(); }
void startAgain(const char *) { std::_Exit(kStallExitCode); }
QString startedAgainReason() { return QString(); }

#endif

void limitMallocArenas() {
#if defined(__GLIBC__)
    if (qEnvironmentVariableIsEmpty("MALLOC_ARENA_MAX"))
        mallopt(M_ARENA_MAX, 2);
#endif
}

void releaseFreedMemory() {
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
}

void raiseFileLimit() {
#ifdef Q_OS_LINUX
    rlimit limit {};
    if (::getrlimit(RLIMIT_NOFILE, &limit) != 0)
        return;
    const rlim_t wanted = limit.rlim_max == RLIM_INFINITY ? rlim_t(65536) : std::min<rlim_t>(limit.rlim_max, 65536);
    if (limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur < wanted) {
        limit.rlim_cur = wanted;
        ::setrlimit(RLIMIT_NOFILE, &limit);
    }
#endif
}

} // namespace diagnostics
