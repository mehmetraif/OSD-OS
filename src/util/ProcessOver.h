#pragma once

#include "EndProcess.h"

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <memory>
#include <utility>

// How a process came to be over (whenOver()).
struct ProcessEnd {
    enum How {
        Exited,      // it ended by itself: code is its exit code
        Crashed,     // it crashed, or was killed
        NotStarted,  // it couldn't be started at all (no such program)
        TimedOut,    // still running at the time limit
    };
    How how = Exited;
    // Its exit code when it exited, else -1.
    int code = -1;
};

// Calls `over` on context's thread each time the process is over: it exited,
// crashed, or couldn't be started at all, which QProcess says with
// errorOccurred(FailedToStart) alone, no finished() after it. Connected once,
// before the process is started; a process started again is told of again.
//
// With timeoutMs, for one start counted from now: a process still running
// then is over too, TimedOut, told at once, what it wrote by then still there
// to read. It is then ended without the app's thread waiting for it
// (util/EndProcess.h: asked to end, killed a moment later), and deleted once
// it has gone, which may take a while (one stuck on a card gone bad), with
// nothing more told: `over` must not delete a process that timed out.
template <typename Over>
void whenOver(QProcess *process, QObject *context, Over over, int timeoutMs = 0) {
    struct State {
        Over over;
        bool timedOut = false;
    };
    const auto state = std::make_shared<State>(State{ std::move(over) });
    QObject::connect(process, &QProcess::finished, context, [state](int code, QProcess::ExitStatus status) {
        if (!state->timedOut)
            state->over(status == QProcess::NormalExit ? ProcessEnd{ ProcessEnd::Exited, code }
                                                       : ProcessEnd{ ProcessEnd::Crashed, -1 });
    });
    QObject::connect(process, &QProcess::errorOccurred, context, [state](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !state->timedOut)
            state->over(ProcessEnd{ ProcessEnd::NotStarted, -1 });
    });
    if (timeoutMs <= 0)
        return;
    QTimer::singleShot(timeoutMs, process, [process, context = QPointer<QObject>(context), state]() {
        if (!context || process->state() == QProcess::NotRunning)
            return;
        state->timedOut = true;
        state->over(ProcessEnd{ ProcessEnd::TimedOut, -1 });
        endProcessLater(process, process, {});
    });
}
