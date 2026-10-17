#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# OSD/OS installer for Raspberry Pi OS Trixie (arm64)
#
# Usage:
#   bash install.sh             # install latest release
#   bash install.sh v1.2.0      # install a specific release tag
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

REPO="mehmetraif/OSD-OS"
INSTALL_DIR="/opt/osdos"
LAUNCHER="/usr/local/bin/osdos"
SYSTEMD_SERVICE="/etc/systemd/system/osdos.service"

# ── Resolve version ────────────────────────────────────────────────────────────
VERSION="${1:-latest}"
if [ "$VERSION" = "latest" ]; then
    echo "Fetching latest release tag..."
    VERSION=$(curl -fsSL \
        "https://api.github.com/repos/${REPO}/releases/latest" \
        | python3 -c "import sys, json; print(json.load(sys.stdin)['tag_name'])")
fi
echo "Installing OSD/OS ${VERSION}"

TARBALL="OSD-OS-${VERSION}-linux-arm64.tar.gz"
DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${VERSION}/${TARBALL}"

# ── Verify architecture ────────────────────────────────────────────────────────
ARCH=$(uname -m)
if [ "$ARCH" != "aarch64" ]; then
    echo "Error: this installer is for arm64 (aarch64). Detected: $ARCH"
    exit 1
fi

# ── An install from 240-MP's days ─────────────────────────────────────────────
# OSD/OS was 240-MP. An install of that (240-MP's own installer) ran
# /opt/240mp through 240mp.service: its service and what it put around the
# system go, so the two never fight over the screen. The app moves its data
# folder (~/.local/share/240-MP) over to its own on its first start.
if [ -e /etc/systemd/system/240mp.service ] || [ -d /opt/240mp ] || [ -e /usr/local/bin/240mp ]; then
    echo "Removing the 240-MP install this replaces..."
    if [ -e /etc/systemd/system/240mp.service ]; then
        sudo systemctl disable --now 240mp.service 2> /dev/null || true
        sudo rm -f /etc/systemd/system/240mp.service /etc/systemd/system/240mp-terminal.service
        sudo systemctl daemon-reload
    fi
    sudo rm -f /usr/local/bin/240mp /usr/local/bin/240mp-stop /etc/udev/rules.d/99-240mp-tty.rules
    sudo rm -rf /opt/240mp
fi

# ── Install runtime dependencies ──────────────────────────────────────────────
echo "Installing runtime dependencies..."
sudo apt-get update -qq
sudo apt-get install -y \
    libqt6quick6 \
    libqt6qml6 \
    libqt6opengl6 \
    libqt6network6 \
    libqt6svg6 \
    qt6-svg-plugins \
    qt6-wayland \
    qml6-module-qtquick \
    qml6-module-qtquick-controls \
    qml6-module-qtquick-window \
    qml6-module-qtquick-effects \
    libsdl2-2.0-0 \
    libpcsclite1 \
    libmpv2 \
    mpv \
    fluidsynth \
    timgm6mb-soundfont \
    openmpt123

# ── udev rule: allow tty group to open /dev/tty0 for VT switching ─────────────
echo 'KERNEL=="tty0", GROUP="tty", MODE="0620"' \
    | sudo tee /etc/udev/rules.d/99-osdos-tty.rules > /dev/null
sudo udevadm control --reload-rules
sudo udevadm trigger /dev/tty0

# ── Choose install owner ──────────────────────────────────────────────────────
# Asked up front so the extract step can hand /opt/osdos to the user the app
# will run as — the launcher applies in-app staged updates there without root.
echo ""
read -r -p "Install systemd autostart service? [y/N] " AUTOSTART_REPLY
SERVICE_USER=""
if [[ "${AUTOSTART_REPLY}" =~ ^[Yy]$ ]]; then
    read -r -p "Run service as user [default: pi]: " SERVICE_USER
    SERVICE_USER="${SERVICE_USER:-pi}"
fi
OWNER_USER="${SERVICE_USER:-$USER}"

# Settings → Bluetooth talks to BlueZ as the user the app runs as, which
# BlueZ's D-Bus policy lets in through the bluetooth group.
if getent group bluetooth > /dev/null; then
    sudo usermod -aG bluetooth "${OWNER_USER}"
fi

# ── Download tarball ───────────────────────────────────────────────────────────
echo "Downloading ${TARBALL}..."
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

curl -fsSL -o "${TMP_DIR}/${TARBALL}" "${DOWNLOAD_URL}"

# ── Extract to install directory ───────────────────────────────────────────────
# Tarball structure: usr/local/bin/osdos + usr/local/share/osdos/...
# We strip the usr/local prefix and place files directly in $INSTALL_DIR.
echo "Extracting to ${INSTALL_DIR}..."
sudo mkdir -p "${INSTALL_DIR}"
sudo tar -xzf "${TMP_DIR}/${TARBALL}" \
    --strip-components=3 \
    -C "${INSTALL_DIR}"

# Owned by the running user so the launcher can swap in staged in-app updates.
sudo chown -R "${OWNER_USER}:" "${INSTALL_DIR}"

# ── Create launcher ────────────────────────────────────────────────────────────
echo "Creating launcher at ${LAUNCHER}..."
sudo tee "${LAUNCHER}" > /dev/null << 'LAUNCHER_SCRIPT'
#!/usr/bin/env bash
# OSD/OS launcher — applies staged in-app updates, then auto-detects the
# display platform and execs the binary.
INSTALL_DIR="/opt/osdos"

# Tells the app this launcher knows how to apply staged updates; the in-app
# updater refuses to stage anything without it (older installs must re-run
# this installer once to pick up the launcher/osdos-stop contract).
# 2: osdos-stop also reboots on exit 12, so the quit menu offers Restart.
# 3: osdos-stop also writes a display preset on exit 20-29 and reboots, so
#    Settings offers Display Output (OSD/OS image), and the launcher takes the
#    output that preset names.
# 4: osdos-stop also writes the JAMMA preset on exit 30, so Display Output
#    offers it (and the OSD/OS image reads a JAMMA board's controls).
# 5: osdos-stop also writes a preset and powers off on exit 40-50, and writes
#    one for the next start while the app runs (osdos-display.socket), so
#    Display Output offers Switch and Power Off and At Next Start.
export OSDOS_LAUNCHER_API=5

# ── Apply a staged in-app update ───────────────────────────────────────────────
# The app downloads the release tarball to DATA_ROOT/updates and writes
# staged.sha256 ("<sha256>  <tarball>", coreutils format). Applied here, before
# exec, so it works identically for manual runs, desktop sessions, and the
# autostart service (which relaunches through this script on exit code 11 —
# see osdos-stop). Mirror any DATA_ROOT override into the app's environment
# (e.g. the systemd unit) or this block won't find the staging directory.
UPDATES_DIR="${DATA_ROOT:-${XDG_DATA_HOME:-$HOME/.local/share}/OSD-OS}/updates"

# Crash recovery: a previous apply died between the child renames below. What
# it had swapped in goes back out for what .old kept of the release before, so
# that one runs whole, and the stage, still there until a swap is on the card,
# is applied again below. A .discard is the release before a finished apply,
# left half deleted.
if [ -d "$INSTALL_DIR/.old" ] && [ -w "$INSTALL_DIR" ]; then
    for d in "$INSTALL_DIR"/.old/*; do
        [ -e "$d" ] || continue
        name=$(basename "$d")
        rm -rf "${INSTALL_DIR:?}/$name"
        mv "$d" "$INSTALL_DIR/$name"
    done
fi
if [ -w "$INSTALL_DIR" ]; then
    rm -rf "$INSTALL_DIR/.new" "$INSTALL_DIR/.old" "$INSTALL_DIR/.discard"
fi

if [ -f "$UPDATES_DIR/staged.sha256" ] && [ -w "$INSTALL_DIR" ]; then
    if ( cd "$UPDATES_DIR" && sha256sum -c --status staged.sha256 ); then
        STAGED_TARBALL=$(awk '{print $2}' "$UPDATES_DIR/staged.sha256")
        rm -rf "$INSTALL_DIR/.new" "$INSTALL_DIR/.old"
        mkdir -p "$INSTALL_DIR/.new" "$INSTALL_DIR/.old"
        # Extract fully, then swap top-level dirs (bin, share) via rename —
        # shrinks the power-loss window from the whole extraction to two mv's,
        # and drops files that no longer exist in the new release. The new
        # files are on the card before any rename names them, and the renames
        # before the stage that would apply them again goes: a Pi switched off
        # at the wall meanwhile starts on the release before, or applies it
        # again, never on a file half written.
        if tar -xzf "$UPDATES_DIR/$STAGED_TARBALL" --strip-components=3 -C "$INSTALL_DIR/.new" \
           && [ -x "$INSTALL_DIR/.new/bin/osdos" ]; then
            sync -f "$INSTALL_DIR" 2> /dev/null || sync
            for d in "$INSTALL_DIR"/.new/*; do
                name=$(basename "$d")
                [ -e "$INSTALL_DIR/$name" ] && mv "$INSTALL_DIR/$name" "$INSTALL_DIR/.old/$name"
                mv "$d" "$INSTALL_DIR/$name"
            done
            mv "$INSTALL_DIR/.old" "$INSTALL_DIR/.discard"
            sync -f "$INSTALL_DIR" 2> /dev/null || sync
            rm -rf "$INSTALL_DIR/.new" "$INSTALL_DIR/.discard"
            rm -f "$UPDATES_DIR/$STAGED_TARBALL" "$UPDATES_DIR/staged.sha256" "$UPDATES_DIR/staged.json"
        else
            # Bad extract: keep the old tree; .failed stops a retry every boot.
            rm -rf "$INSTALL_DIR/.new" "$INSTALL_DIR/.old"
            mv -f "$UPDATES_DIR/staged.sha256" "$UPDATES_DIR/staged.sha256.failed" 2>/dev/null
        fi
    else
        # Corrupt/partial stage — discard; the app re-offers the update.
        rm -f "$UPDATES_DIR/staged.sha256" "$UPDATES_DIR/staged.json"
    fi
fi

if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"
elif [ -n "${DISPLAY:-}" ]; then
    QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
else
    # No display server — use EGLFS for headless/kiosk mode (RPi Lite)
    QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-eglfs}"
    export QT_QPA_EGLFS_ALWAYS_SET_MODE=1
    export QT_QPA_EGLFS_KMS_ATOMIC=1

    # On a Pi 5, the output the OSD/OS image's display preset names
    # (/boot/firmware/osdos-display.txt: "# osdos-output: <type>"), in its mode
    # ("# osdos-mode: WxH") if the connector has it: a Pi 5 may keep HDMI on
    # beside a CRT, and there a composite mode's lines make PAL or NTSC. mpv
    # plays on it too (OSDOS_DRM_*). A Pi 4 has one output on at a time.
    KMS_CARD=""; KMS_OUTPUT=""; KMS_MODE=""
    PRESET=/boot/firmware/osdos-display.txt
    if [ -r "$PRESET" ] && tr -d '\0' 2>/dev/null < /proc/device-tree/model | grep -q '^Raspberry Pi 5'; then
        want=$(sed -n 's/^# osdos-output: *\([A-Za-z-]*\).*/\1/p' "$PRESET" | head -n1)
        mode=$(sed -n 's/^# osdos-mode: *\([0-9]*x[0-9]*\).*/\1/p' "$PRESET" | head -n1)
        # Composite and DPI come from the RP1's drivers, which may register
        # their connector after vc4's HDMI has: waited for, up to ten seconds,
        # rather than taking HDMI for the boot and leaving the CRT dark.
        tries=0
        while [ -n "$want" ] && [ "$tries" -lt 100 ] \
                && ! compgen -G "/sys/class/drm/card*-${want}-*" > /dev/null; do
            sleep 0.1
            tries=$((tries + 1))
        done
        if [ -n "$want" ] && ! compgen -G "/sys/class/drm/card*-${want}-*" > /dev/null; then
            echo "osdos: the display preset's $want output never appeared; taking the first display connected" >&2
        fi
        for d in /sys/class/drm/card*-"${want:-none}"-*; do
            [ -e "$d" ] || continue
            n=$(basename "$d"); KMS_CARD="${n%%-*}"; KMS_OUTPUT="${n#*-}"
            if [ -n "$mode" ] && grep -qE "^${mode}i?\$" "$d/modes" 2>/dev/null; then
                KMS_MODE="$mode"
            fi
            break
        done
    fi

    # Point Qt EGLFS at the DRM card that has a real display pipeline. Render-
    # only nodes (v3d) have no connector dirs under /sys/class/drm and make Qt
    # fail with "drmModeGetResources failed (Operation not supported)". On
    # Pi3B+/Pi4 the display card happens to be card0 (auto-pick works), but on
    # Pi5 the v3d render node often enumerates first, so we must select the
    # right card explicitly. Prefer a connected connector; fall back to the
    # first card that has any connector at all.
    if [ -z "$KMS_CARD" ]; then
        for s in /sys/class/drm/card*-*/status; do
            [ -e "$s" ] || continue
            if [ "$(cat "$s")" = "connected" ]; then
                n=$(basename "$(dirname "$s")"); KMS_CARD="${n%%-*}"; break
            fi
        done
    fi
    if [ -z "$KMS_CARD" ]; then
        for d in /sys/class/drm/card*-*; do
            [ -e "$d" ] || continue
            n=$(basename "$d"); KMS_CARD="${n%%-*}"; break
        done
    fi
    if [ -n "$KMS_CARD" ] && [ -e "/dev/dri/$KMS_CARD" ]; then
        KMS_CONF="${XDG_RUNTIME_DIR:-/tmp}/osdos-kms.json"
        if [ -n "$KMS_OUTPUT" ]; then
            # Qt names an output by its type and number: HDMI-A-1 is HDMI1.
            qt_name="${KMS_OUTPUT%-*}"
            qt_name="${qt_name%-[AB]}${KMS_OUTPUT##*-}"
            printf '{ "device": "/dev/dri/%s", "outputs": [ { "name": "%s", "primary": true%s } ] }\n' \
                "$KMS_CARD" "$qt_name" "${KMS_MODE:+, \"mode\": \"$KMS_MODE\"}" > "$KMS_CONF"
            export OSDOS_DRM_DEVICE="/dev/dri/$KMS_CARD" OSDOS_DRM_CONNECTOR="$KMS_OUTPUT" \
                OSDOS_DRM_MODE="$KMS_MODE"
        else
            printf '{ "device": "/dev/dri/%s" }\n' "$KMS_CARD" > "$KMS_CONF"
        fi
        export QT_QPA_EGLFS_KMS_CONFIG="$KMS_CONF"
    fi
fi

export QT_QPA_PLATFORM
export QML2_IMPORT_PATH="/usr/lib/aarch64-linux-gnu/qt6/qml"

exec "${INSTALL_DIR}/bin/osdos" "$@"
LAUNCHER_SCRIPT

sudo chmod +x "${LAUNCHER}"

# ── Optional: systemd autostart ───────────────────────────────────────────────
if [[ "${AUTOSTART_REPLY}" =~ ^[Yy]$ ]]; then
    sudo tee "${SYSTEMD_SERVICE}" > /dev/null << UNIT
[Unit]
Description=OSD/OS Media Player
After=multi-user.target sound.target
StartLimitIntervalSec=120
StartLimitBurst=3
OnFailure=osdos-terminal.service

[Service]
Type=simple
User=${SERVICE_USER}
SupplementaryGroups=tty video input
AmbientCapabilities=CAP_SYS_TTY_CONFIG
Environment=QT_QPA_PLATFORM=eglfs
Environment=QT_QPA_EGLFS_ALWAYS_SET_MODE=1
Environment=QT_QPA_EGLFS_KMS_ATOMIC=1
Environment=QML2_IMPORT_PATH=/usr/lib/aarch64-linux-gnu/qt6/qml
Environment=OSDOS_AUTOSTART=1
ExecStartPre=+-/usr/bin/systemctl stop osdos-terminal.service
ExecStart=${LAUNCHER}
Restart=on-failure
RestartSec=5s
RestartMode=direct
SuccessExitStatus=10 12 20 21 22 23 24 25 26 27 28 29 30 40 41 42 43 44 45 46 47 48 49 50 129 130 143
ExecStopPost=+/usr/local/bin/osdos-stop
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
UNIT

    # ExecStopPost helper: the user's Quit → Power Off (exit 0) powers the Pi off;
    # exit 10 means the user chose "Exit to Terminal", so instead spawn a login
    # shell on tty1 (see views/Settings.qml). Exit 12 is the quit menu's
    # "Restart": reboot. Exit 20-30 is Settings → Display Output on the OSD/OS
    # image: a display preset written, then a reboot; 40-50 the same, then off
    # (Switch and Power Off). SuccessExitStatus= counts
    # those as no failure, so Restart=on-failure doesn't start the app again
    # over the shell or on the way down, and OnFailure= stays out of it.
    # Exit 11 is "Apply & Restart" from the in-app updater (views/Update.qml):
    # do nothing here — it's a failure status, so Restart=on-failure relaunches
    # through the launcher, which applies the staged update before exec.
    # A stop or restart from outside (systemctl stop/restart, a shutdown) ends the
    # app by signal — it exits 128+signal, or dies of it — and leaves the Pi on.
    # So does a crash or an error exit: Restart=on-failure starts the app again,
    # and when it has failed three times in two minutes (StartLimitBurst=),
    # OnFailure= puts the login prompt on the screen in its place. Without
    # RestartMode=direct (systemd 254 or later), every failure on the way
    # would bring that prompt up for the five seconds before the restart.
    sudo tee /usr/local/bin/osdos-stop > /dev/null << 'STOP_HELPER'
#!/usr/bin/env bash
# Called by osdos.service ExecStopPost. systemd sets $EXIT_STATUS to the app's
# exit code, or to the signal name if a signal killed it. Called with
# next-start, by osdos-display.socket instead: Display Output's At Next Start.

# Settings → Display Output on the OSD/OS image: the chosen preset (or the one
# before it, to go back) over osdos-display.txt, which the firmware reads only
# at power-on. A missing preset changes nothing; the app says so after the
# reboot. The one to go back to is the output this boot started on, which
# AT_BOOT keeps once a preset has been written for the next start. A Pi 5's
# SCART RGB needs its composite sync on GPIO 1; a JAMMA board makes its own,
# and has its I2C on GPIO 1 ("# osdos-sync: separate").
AT_BOOT=/run/osdos-display-boot.txt
display_output() {
    local boot=/boot/firmware src now
    if [ "$1" = previous ]; then
        src="$boot/osdos-display-previous.txt"
    else
        src="$boot/osdos-display-$1.txt"
    fi
    [ -f "$src" ] || return 1
    now="$boot/osdos-display.txt"
    [ -f "$AT_BOOT" ] && now="$AT_BOOT"
    [ "$1" = previous ] || cp -f "$now" "$boot/osdos-display-previous.txt"
    cp -f "$src" "$boot/osdos-display.txt.new" && mv -f "$boot/osdos-display.txt.new" "$boot/osdos-display.txt" || return 1
    if grep -q '^# osdos-output: DPI' "$boot/osdos-display.txt" \
            && ! grep -q '^# osdos-sync: separate' "$boot/osdos-display.txt"; then
        echo "options drm_rp1_dpi force_csync=1" > /etc/modprobe.d/osdos-display.conf
    else
        rm -f /etc/modprobe.d/osdos-display.conf
    fi
    # Those two file systems only: a download on the film partition mustn't
    # hold the answer back.
    sync -f "$boot/osdos-display.txt" /etc/modprobe.d
}
# The app's codes for them (src/display/DisplayOutput.cpp): keep the two in step.
DISPLAY_PRESETS=(hdmi crt-ntsc crt-pal crt-gpio-ntsc crt-gpio-pal
                 scart-rgb-ntsc scart-rgb-pal scart-rgb-240p scart-rgb-288p previous
                 jamma-240p)

# At Next Start: a preset's name on a line from the app, through the socket,
# and "ok" back, or why not. Written now, for the firmware to read the next
# time the Pi starts; the output in force is kept in AT_BOOT first, as the one
# to go back to. Only a name from the list, never echoed. A refusal goes to
# the journal too; answered, the request hasn't failed, whatever the answer.
refuse() {
    echo "$1"
    echo "osdos-stop next-start: $1" >&2
}
next_start() {
    local id p known=""
    IFS= read -r -t 5 -n 64 id || [ -n "$id" ] || { refuse "no preset named"; return; }
    for p in "${DISPLAY_PRESETS[@]}"; do
        [ "$p" != previous ] && [ "$p" = "$id" ] && known=1
    done
    [ -n "$known" ] || { refuse "no such preset"; return; }
    [ -f "/boot/firmware/osdos-display-$id.txt" ] || { refuse "the preset isn't on the boot partition"; return; }
    [ -f "$AT_BOOT" ] || cp -f /boot/firmware/osdos-display.txt "$AT_BOOT" \
        || { refuse "the output in force couldn't be kept"; return; }
    display_output "$id" || { refuse "the preset couldn't be written"; return; }
    echo ok
}
if [ "${1:-}" = next-start ]; then
    next_start
    exit 0
fi

case "${EXIT_STATUS:-}" in
    0)  systemctl poweroff ;;  # Quit → Power Off, or Ctrl+Q
    10) systemctl start osdos-terminal.service ;;
    11) : ;;  # in-app update restart — Restart=on-failure brings the app back up
    12) systemctl reboot ;;  # the quit menu's Restart
    2[0-9]|30) display_output "${DISPLAY_PRESETS[EXIT_STATUS - 20]}"; systemctl reboot ;;
    # The same, then off: on the new output the next time the Pi is switched on.
    4[0-9]|50) display_output "${DISPLAY_PRESETS[EXIT_STATUS - 40]}"; systemctl poweroff ;;
    # Anything else leaves the Pi on: a stop from outside (129, 130, 143, or the
    # signal itself), and a crash or an error exit, after which systemd starts
    # the app again (Restart=on-failure), or brings up a login prompt.
    *)  : ;;
esac
STOP_HELPER
    sudo chmod +x /usr/local/bin/osdos-stop

    # Display Output's At Next Start, while the app runs: it writes a preset's
    # name to this socket, which only its user may, and osdos-stop next-start,
    # as root, writes that preset for the next start and answers.
    sudo tee /etc/systemd/system/osdos-display.socket > /dev/null << 'DISPLAY_SOCKET'
[Unit]
Description=OSD/OS display output for the next start (Settings → Display Output)

[Socket]
ListenStream=/run/osdos-display.sock
SocketUser=root
SocketGroup=@OSDOS_GROUP@
SocketMode=0660
Accept=yes
MaxConnections=1

[Install]
WantedBy=sockets.target
DISPLAY_SOCKET
    sudo sed -i "s/@OSDOS_GROUP@/$(id -gn "${SERVICE_USER}")/" /etc/systemd/system/osdos-display.socket
    sudo tee /etc/systemd/system/osdos-display@.service > /dev/null << 'DISPLAY_SERVICE'
[Unit]
Description=OSD/OS display output for the next start, one request
# One unit a request: gone once it has run, however it ended.
CollectMode=inactive-or-failed

[Service]
ExecStart=/usr/local/bin/osdos-stop next-start
StandardInput=socket
StandardOutput=socket
StandardError=journal
NoNewPrivileges=yes
PrivateNetwork=yes
PrivateTmp=yes
ProtectHome=yes
DISPLAY_SERVICE

    # On-demand login shell for "Exit to Terminal". Not enabled (no boot race with
    # osdos.service); getty@tty1 stays masked. Started only by osdos-stop, or by
    # osdos.service's OnFailure= once the app has failed three times running, and
    # stopped again by osdos.service's ExecStartPre when the app comes back.
    sudo tee /etc/systemd/system/osdos-terminal.service > /dev/null << 'TERMINAL_UNIT'
[Unit]
Description=OSD/OS exit-to-terminal login shell

[Service]
Type=idle
ExecStart=-/sbin/agetty --noclear tty1 linux
StandardInput=tty
StandardOutput=tty
TTYPath=/dev/tty1
TTYReset=yes
TTYVHangup=yes
KillMode=process
Restart=no
TERMINAL_UNIT

    sudo systemctl mask getty@tty1.service autovt@.service
    sudo systemctl daemon-reload
    sudo systemctl enable osdos.service
    sudo systemctl enable --now osdos-display.socket
    echo "Service installed and enabled."
    echo "Start now with: sudo systemctl start osdos"
fi

echo ""
echo "OSD/OS ${VERSION} installed successfully."
echo "Run: osdos"
