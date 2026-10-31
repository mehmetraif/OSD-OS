#pragma once

#include <QString>

// What the app leaves behind when it goes wrong on a device no debugger is
// attached to (the Pi under the television): where a crash happened, where a
// frozen main thread is stuck, and how much memory there was. Written to the
// journal (stderr) and to files in the data folder, which outlive a reboot
// where the Pi's journal doesn't.
namespace diagnostics {

// Linux: a crash (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT) writes its
// backtrace to the journal and to <dataRoot>/crash-report.txt, then ends the
// process as the signal would have, so the service starts the app again.
void installCrashHandlers(const QString &dataRoot);

// Linux: watches the main thread from a thread of its own. Not answering for
// reportMs, where it is goes to the journal and <dataRoot>/stall-report.txt,
// with the memory, and where every other thread is to the report; and again
// once it answers. Still not answering at abortMs
// (0: never), the process exits with kStallExitCode, and the service starts
// the app again: a frozen menu comes back by itself. Short of a freeze, a
// main thread away for 400 ms or more between two beats (the menus
// answering late) is noted in the journal with the memory, once in ten
// seconds at most. Started once the event loop runs; stopped before the
// app's own shutdown, which may take a while.
constexpr int kStallExitCode = 70;
void startStallWatchdog(const QString &dataRoot, int reportMs, int abortMs);
void stopStallWatchdog();

// "app 372 MB, available 512 of 1848 MB, swap 0 of 100 MB in use" (Linux;
// empty elsewhere), for the journal.
QString memoryLine();

// Starts the app again in place, for a state only a fresh start leaves (the
// screen lost to it after a hand-off, DisplayHandoff::screenLost): on Linux
// the same process exec'd afresh from /proc/self/exe with the arguments and
// environment it was started with, every file it had open but the journal's
// closed (its DRM device, whose master the new start takes, a sound card),
// so the service sees no exit and waits for nothing. The new start says why
// in the journal (startedAgainReason()). Where it can't, the process exits
// with kStallExitCode and the service starts it again.
[[noreturn]] void startAgain(const char *reason);
// Why this start is a fresh start of the run before (startAgain()), or empty;
// taken out of the environment, so that no child is started with it.
QString startedAgainReason();

// glibc's allocator gives each thread that allocates an arena of its own
// (up to eight per core) and keeps what is freed in them. Every film played
// inside the window starts a libmpv core with threads of its own, so the app
// grew by tens of MB a film. limitMallocArenas(), before any thread exists,
// keeps them to two (unless MALLOC_ARENA_MAX says otherwise);
// releaseFreedMemory() hands what a finished video freed back to the system.
void limitMallocArenas();
void releaseFreedMemory();

}
