#pragma once

// The codes the app exits with for what comes after it on the OSD/OS image.
// The autostart service runs the stop helper, osdos-stop (scripts/install.sh,
// STOP_HELPER), as the app ends, with the code in $EXIT_STATUS, and it does
// what the code says; osdos.service's SuccessExitStatus= counts each of these
// as no failure, so the app isn't started again over what comes next, and
// RestartForceExitStatus= starts it again on kUpdateRestart. Keep the three
// in step. A code is used only from the launcher API whose osdos-stop knows
// it (OSDOS_LAUNCHER_API, the launcher's notes in scripts/install.sh).
namespace exitcode {

// Quit → Power Off (Qt.quit(), or Ctrl+Q) exits 0, as a Qt app does: osdos-stop
// powers the Pi off.
//
// Quit → Exit to Terminal: osdos-stop starts osdos-terminal.service, a
// login prompt on tty1, in the app's place.
constexpr int kTerminal = 10;
// The updater's Apply & Restart (UpdateManager): osdos-stop does nothing,
// and the service starts the app again through the launcher, which puts the
// staged update in place first.
constexpr int kUpdateRestart = 11;
// Quit → Restart: osdos-stop reboots (launcher API 2).
constexpr int kReboot = 12;

// Settings → Screen → Display Output (DisplayOutput, whose presets have their
// codes): osdos-stop writes the preset DISPLAY_PRESETS has at the code less
// kDisplayPresetFirst, then reboots (launcher API 3). kDisplayRevert is the
// one before, KEEP's SWITCH BACK.
constexpr int kDisplayPresetFirst = 20;
constexpr int kDisplayPresetLast = 39;
constexpr int kDisplayRevert = 29;
// Added to a preset's code (40-59): written, then powered off (launcher API 5).
constexpr int kDisplayThenPowerOff = 20;

// The main thread frozen (util/Diagnostics' watchdog), or a fresh start that
// couldn't be made in place: an error, after which osdos-stop leaves the Pi
// on and the service starts the app again (Restart=on-failure).
constexpr int kStall = 70;

// Ended by a signal (systemctl stop or restart, a shutdown): 128 and the
// signal's number, the shell's way, so that osdos-stop tells it from Quit
// (0) and leaves the Pi on, and the service counts it as no failure (129,
// 130, 143: SIGHUP, SIGINT, SIGTERM).
constexpr int bySignal(int signal) {
    return 128 + signal;
}

} // namespace exitcode
