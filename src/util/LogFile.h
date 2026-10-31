#pragma once

#include <QString>

// What the app logs, kept in a file in the data folder as well: osdos.log,
// with the run before's as osdos-previous.log. The Windows build is a
// windowed program, which no console opens with, so its log goes there. On
// Linux it goes to the journal or the terminal as before, and to the file
// too: the OSD/OS image's journal lives in memory, and the file is what is
// left of a run once the Pi has been switched off and on (a frozen one
// too), which Quit → Save Logs copies to the card. main() starts it on
// Windows and Linux.
namespace logfile {

// Before anything is logged: on Windows, the console the app was started
// from (cmd, PowerShell), if any, takes its output, unless that output is
// already sent to a file or a pipe; and what is logged is kept until open()
// names the file. Every line goes on to the handler that was there before.
void begin();

// The data folder is known: osdos.log opened afresh there, the run before's
// kept as osdos-previous.log, and what was logged since begin() written
// first. A run writes 16 MB at most.
void open(const QString &dataRoot);

// The handler from before begin() back, and the file closed (the tests).
void end();

} // namespace logfile
