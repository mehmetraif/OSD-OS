#include "TermSignals.h"

#include <QObject>
#include <csignal>

#ifdef Q_OS_WIN
#include <QTimer>
#else
#include <QSocketNotifier>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace termsignals {

#ifdef Q_OS_WIN

static volatile std::sig_atomic_t g_signal = 0;
extern "C" void osdosRecordTerm(int sig) { g_signal = sig; }

void install(QObject *context, std::function<void(int)> onSignal) {
    std::signal(SIGTERM, osdosRecordTerm);
    std::signal(SIGINT,  osdosRecordTerm);
    auto *poll = new QTimer(context);
    poll->setInterval(100);
    QObject::connect(poll, &QTimer::timeout, context, [onSignal]() {
        if (const int sig = g_signal) {
            g_signal = 0;
            onSignal(sig);
        }
    });
    poll->start();
}

#else

// The pipe's ends: the handler writes the signal's number to the one, the
// event loop reads it from the other.
static int g_pipe[2] = {-1, -1};

// Async-signal-safe: write() alone, errno left as it was.
extern "C" void osdosRecordTerm(int sig) {
    const int saved = errno;
    const unsigned char number = static_cast<unsigned char>(sig);
    if (g_pipe[1] >= 0)
        (void)::write(g_pipe[1], &number, 1);
    errno = saved;
}

static bool openPipe() {
    if (::pipe(g_pipe) != 0)
        return false;
    for (int fd : g_pipe) {
        // Not handed to the programs the app starts, and never waited on.
        ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    }
    return true;
}

void install(QObject *context, std::function<void(int)> onSignal) {
    if (g_pipe[0] < 0 && !openPipe()) {
        qWarning("[main] No pipe for the termination signals: they end the app where it stands");
        return;
    }
    auto *notifier = new QSocketNotifier(g_pipe[0], QSocketNotifier::Read, context);
    QObject::connect(notifier, &QSocketNotifier::activated, context, [onSignal]() {
        unsigned char number = 0;
        while (::read(g_pipe[0], &number, 1) == 1)
            onSignal(number);
    });
    struct sigaction action = {};
    action.sa_handler = osdosRecordTerm;
    sigemptyset(&action.sa_mask);
    // A system call the signal breaks into goes on.
    action.sa_flags = SA_RESTART;
    for (int sig : {SIGTERM, SIGINT, SIGHUP})
        sigaction(sig, &action, nullptr);
}

#endif

} // namespace termsignals
