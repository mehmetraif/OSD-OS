#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# Run a build of your own the way the installed launcher (/usr/local/bin/osdos,
# scripts/install.sh's LAUNCHER_SCRIPT) runs the installed app: on the display
# server there is, else on EGLFS pointed at the DRM card with a display (on a
# Pi 5 the GPU's render-only node comes first and makes Qt fail), on the output
# and in the mode the OSD/OS image's display preset names. The launcher's
# display part is taken from install.sh as it stands (DISPLAY_BEGIN to
# DISPLAY_END), so the two can't drift apart: the system's Qt QML modules
# (QML2_IMPORT_PATH) come with it, over any of your own. Its staged in-app
# update is the installed app's and is left out.
#
# Usage (from anywhere):
#   scripts/rpi-run-local.sh                       # runs ./build/osdos from the repo
#   OSDOS_BIN=/path/to/osdos scripts/rpi-run-local.sh
#
# Headless (RPi Lite / EGLFS): run from the Pi's console VT, not over SSH, and
# stop the autostart service first so it isn't holding the display:
#   sudo systemctl stop osdos
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

# Repo root = parent of this script's dir, so APP_ROOT resolves assets/modules.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN="${OSDOS_BIN:-${MP240_BIN:-${REPO_ROOT}/build/osdos}}"

if [ ! -x "$BIN" ]; then
    echo "Error: binary not found or not executable at: $BIN" >&2
    echo "Build it first (cmake --build build) or set OSDOS_BIN." >&2
    exit 1
fi

export APP_ROOT="${APP_ROOT:-${REPO_ROOT}}"

# The launcher's display part, then the binary in the launcher's place. The
# script deletes itself as the last thing before the app takes its place.
RUN_SCRIPT="$(mktemp "${TMPDIR:-/tmp}/osdos-run.XXXXXX")"
{
    echo '#!/usr/bin/env bash'
    awk -v marker=LAUNCHER_SCRIPT '
        $0 ~ "<< .?" marker ".?$" { on = 1; next }
        on && $0 == marker        { exit }
        on                        { print }
    ' "${SCRIPT_DIR}/install.sh" | sed -n '/^# DISPLAY_BEGIN$/,/^# DISPLAY_END$/p'
    printf 'rm -f %q\nexec %q "$@"\n' "$RUN_SCRIPT" "$BIN"
} > "$RUN_SCRIPT"
if ! grep -q '^# DISPLAY_END$' "$RUN_SCRIPT"; then
    rm -f "$RUN_SCRIPT"
    echo "Error: the launcher's display part (DISPLAY_BEGIN to DISPLAY_END) not found in ${SCRIPT_DIR}/install.sh" >&2
    exit 1
fi
exec bash "$RUN_SCRIPT" "$@"
