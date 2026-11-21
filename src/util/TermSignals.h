#pragma once

#include <functional>

class QObject;

// The signals that end the app (SIGTERM from `systemctl stop` or a shutdown,
// SIGINT, SIGHUP) handled from the event loop, where destructors run properly,
// not where the process stands: without that, mpv or another process could
// be left running, and a headless Pi on a blank VT with DRM master dropped,
// ~DisplayHandoff never having put the display back.
//
// The handler itself only records the signal, as a signal handler may. On
// Linux and macOS it writes it to a pipe the event loop watches, so nothing
// wakes the app until a signal comes; on Windows, whose handlers run on a
// thread of their own, it is looked for every 100 ms.
namespace termsignals {

// onSignal(sig) is called on context's thread as each signal comes, for as
// long as context lives. Called once, from main().
void install(QObject *context, std::function<void(int)> onSignal);

} // namespace termsignals
