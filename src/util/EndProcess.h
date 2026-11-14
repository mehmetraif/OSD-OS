#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>
#include <utility>

// Ends a helper process (an audio-only mpv) without the app's thread waiting
// for it: asked to end now, killed if it is still there after `graceMs`, and
// deleted once it has gone, when `gone` runs (the sound card is free then).
// The menus come back at once instead of up to a second later. For a process
// that is running, whose own handlers the caller has already disconnected.
inline void endProcessLater(QProcess *process, QObject *context, std::function<void()> gone, int graceMs = 1000) {
    QObject::connect(process, &QProcess::finished, context, [process, gone = std::move(gone)]() {
        process->deleteLater();
        if (gone)
            gone();
    });
    QTimer::singleShot(graceMs, process, [process]() {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
    process->terminate();
}
