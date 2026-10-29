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
# The release as GitHub's API has it, the latest or the tag named: its tag, and
# the SHA-256 GitHub keeps for the tarball ("sha256:<hex>"), which the download
# is checked against, as the in-app updater checks its own.
VERSION="${1:-latest}"
if [ "$VERSION" = "latest" ]; then
    echo "Fetching the latest release..."
    RELEASE_URL="https://api.github.com/repos/${REPO}/releases/latest"
else
    RELEASE_URL="https://api.github.com/repos/${REPO}/releases/tags/${VERSION}"
fi
RELEASE_JSON=$(curl -fsSL "${RELEASE_URL}") \
    || { echo "Error: couldn't read the OSD/OS release ${VERSION} from GitHub." >&2; exit 1; }
VERSION=$(printf '%s' "${RELEASE_JSON}" | python3 -c "import sys, json; print(json.load(sys.stdin)['tag_name'])")
echo "Installing OSD/OS ${VERSION}"

TARBALL="OSD-OS-${VERSION}-linux-arm64.tar.gz"
DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${VERSION}/${TARBALL}"
DIGEST=$(printf '%s' "${RELEASE_JSON}" | python3 -c "
import sys, json
for asset in json.load(sys.stdin).get('assets', []):
    if asset.get('name') == sys.argv[1]:
        print(asset.get('digest') or '')" "${TARBALL}")

# ── Verify architecture ────────────────────────────────────────────────────────
ARCH=$(uname -m)
if [ "$ARCH" != "aarch64" ]; then
    echo "Error: this installer is for arm64 (aarch64). Detected: $ARCH"
    exit 1
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
    libsdl2-2.0-0 \
    libpcsclite1 \
    libmpv2 \
    mpv \
    fluidsynth \
    timgm6mb-soundfont \
    openmpt123 \
    polkitd
# Qt Quick 3D, which draws the Arcade screen's Orb look (the Orb theme's), where
# the system has it: without it the Arcade screen keeps its own look.
sudo apt-get install -y qml6-module-qtquick3d qml6-module-qtquick3d-helpers \
    || echo "Qt Quick 3D isn't available: the Orb theme's Arcade screen keeps its own look"

# ── udev rule: allow tty group to open /dev/tty0 for VT switching ─────────────
echo 'KERNEL=="tty0", GROUP="tty", MODE="0620"' \
    | sudo tee /etc/udev/rules.d/99-osdos-tty.rules > /dev/null
sudo udevadm control --reload-rules
sudo udevadm trigger /dev/tty0

# ── Choose install owner ──────────────────────────────────────────────────────
# Asked up front so the extract step can hand /opt/osdos to the user the app
# will run as — the launcher applies in-app staged updates there without root.
# A service already installed runs the app as the user it names: that one is
# offered, and kept when the service is. The OSD/OS image's service is its own
# (it holds the boot screen's units back until the app is up): never rewritten,
# though the helpers around it are brought up to date.
EXISTING_USER=""
if [ -f "${SYSTEMD_SERVICE}" ]; then
    EXISTING_USER=$(sed -n '/^User=/{s///p;q}' "${SYSTEMD_SERVICE}")
fi
IMAGE_SERVICE=""
SERVICE_USER=""
if [ -f /etc/osdos/boot-units ] && [ -n "${EXISTING_USER}" ]; then
    IMAGE_SERVICE=1
    AUTOSTART_REPLY=y
    SERVICE_USER="${EXISTING_USER}"
    echo "The OSD/OS image's own service is kept; it runs the app as ${SERVICE_USER}."
else
    echo ""
    read -r -p "Install systemd autostart service? [y/N] " AUTOSTART_REPLY
    if [[ "${AUTOSTART_REPLY}" =~ ^[Yy]$ ]]; then
        read -r -p "Run service as user [default: ${EXISTING_USER:-pi}]: " SERVICE_USER
        SERVICE_USER="${SERVICE_USER:-${EXISTING_USER:-pi}}"
    elif [ -n "${EXISTING_USER}" ]; then
        SERVICE_USER="${EXISTING_USER}"
        echo "The autostart service already installed is kept; it runs the app as ${SERVICE_USER}."
    fi
fi
# Without a service, the user installing: run as "sudo bash install.sh", the
# one who ran sudo, not root.
OWNER_USER="${SERVICE_USER:-${SUDO_USER:-${USER:-$(id -un)}}}"
if ! id -u "${OWNER_USER}" > /dev/null 2>&1; then
    echo "Error: there is no user named ${OWNER_USER} on this system." >&2
    exit 1
fi

# Settings → Bluetooth talks to BlueZ as the user the app runs as, which
# BlueZ's D-Bus policy lets in through the bluetooth group.
if getent group bluetooth > /dev/null; then
    sudo usermod -aG bluetooth "${OWNER_USER}"
fi

# Settings → Wi-Fi scans, joins and forgets networks and turns Wi-Fi on and
# off through NetworkManager, as the user the service runs the app as. The
# service is outside any login session, where polkit would ask for a password
# nobody can type: this rule lets that user use the five NetworkManager actions
# those take, which reach every connection NetworkManager keeps, Ethernet's
# too, and nothing else.
# Written for a service kept as it is too, which the launcher offers Wi-Fi to.
if [ -n "${SERVICE_USER}" ]; then
    sudo mkdir -p /etc/polkit-1/rules.d
    sudo tee /etc/polkit-1/rules.d/50-osdos-networkmanager.rules > /dev/null << 'POLKIT_RULE'
// OSD/OS's Wi-Fi setting scans, joins and forgets networks and turns Wi-Fi
// on and off through NetworkManager, as the user the app's service runs as,
// outside any login session, where polkit would otherwise ask for a password
// nobody can type.
polkit.addRule(function(action, subject) {
    if (subject.user == "@OSDOS_USER@" &&
        (action.id == "org.freedesktop.NetworkManager.wifi.scan" ||
         action.id == "org.freedesktop.NetworkManager.network-control" ||
         action.id == "org.freedesktop.NetworkManager.settings.modify.system" ||
         action.id == "org.freedesktop.NetworkManager.settings.modify.own" ||
         action.id == "org.freedesktop.NetworkManager.enable-disable-wifi")) {
        return polkit.Result.YES;
    }
});
POLKIT_RULE
    sudo sed -i "s/@OSDOS_USER@/${SERVICE_USER}/" /etc/polkit-1/rules.d/50-osdos-networkmanager.rules
fi

# ── Download tarball ───────────────────────────────────────────────────────────
# The app, stopped below for the release's swap, starts again as the script
# ends, however it ends: on the new release, or on the one before.
echo "Downloading ${TARBALL}..."
TMP_DIR=$(mktemp -d)
APP_WAS_RUNNING=""
trap 'rm -rf "$TMP_DIR"; if [ -n "$APP_WAS_RUNNING" ]; then sudo systemctl start osdos.service; fi' EXIT

curl -fsSL -o "${TMP_DIR}/${TARBALL}" "${DOWNLOAD_URL}"
# Checked against the SHA-256 GitHub keeps for it before anything changes; a
# release with none is taken as it came.
case "${DIGEST}" in
    sha256:*)
        if ! echo "${DIGEST#sha256:}  ${TMP_DIR}/${TARBALL}" | sha256sum -c --status -; then
            echo "Error: ${TARBALL} doesn't match the SHA-256 GitHub keeps for it; nothing was installed." >&2
            exit 1
        fi
        ;;
esac

# ── Put the release in place ───────────────────────────────────────────────────
# The tarball holds ./usr/local/bin/osdos and ./usr/local/share/osdos/...; with
# the usr/local prefix stripped, it goes straight into $INSTALL_DIR the way the
# launcher applies an in-app update: extracted whole into .new and written to
# the card, then each top-level folder (bin, lib, share) swapped in for the one
# there, which waits in .old until the swap is done. A Pi switched off
# meanwhile starts on the release before, which the launcher puts back whole
# from .old, never on half of each; and a file a release dropped (a module's
# folder) doesn't live on under the new one. The app, if it runs, stops for the
# swap rather than run on as half of each.
echo "Extracting to ${INSTALL_DIR}..."
if [ -f "${SYSTEMD_SERVICE}" ]; then
    if systemctl is-active --quiet osdos.service; then
        APP_WAS_RUNNING=1
    fi
    sudo systemctl stop osdos.service
fi
sudo mkdir -p "${INSTALL_DIR}"
# A swap cut short before and not put right since: the release before back
# whole from .old first, as the launcher does.
if [ -d "${INSTALL_DIR}/.old" ]; then
    for d in "${INSTALL_DIR}"/.old/*; do
        [ -e "$d" ] || continue
        name=$(basename "$d")
        sudo rm -rf "${INSTALL_DIR:?}/${name}"
        sudo mv "$d" "${INSTALL_DIR}/${name}"
    done
fi
sudo rm -rf "${INSTALL_DIR}/.new" "${INSTALL_DIR}/.old" "${INSTALL_DIR}/.discard"
sudo mkdir "${INSTALL_DIR}/.new" "${INSTALL_DIR}/.old"
sudo tar -xzf "${TMP_DIR}/${TARBALL}" --strip-components=3 --no-same-owner -C "${INSTALL_DIR}/.new"
if [ ! -x "${INSTALL_DIR}/.new/bin/osdos" ]; then
    sudo rm -rf "${INSTALL_DIR}/.new" "${INSTALL_DIR}/.old"
    echo "Error: ${TARBALL} has no bin/osdos; nothing was installed." >&2
    exit 1
fi
# All of it the user's the app runs as, the release there and the swap's
# folders too: the launcher swaps in staged in-app updates without root, and
# puts the release before back should the swap below be cut short.
sudo chown -R "${OWNER_USER}:" "${INSTALL_DIR}"
sync -f "${INSTALL_DIR}" 2> /dev/null || sync
for d in "${INSTALL_DIR}"/.new/*; do
    name=$(basename "$d")
    if [ -e "${INSTALL_DIR}/${name}" ]; then
        sudo mv "${INSTALL_DIR}/${name}" "${INSTALL_DIR}/.old/${name}"
    fi
    sudo mv "$d" "${INSTALL_DIR}/${name}"
done
sudo mv "${INSTALL_DIR}/.old" "${INSTALL_DIR}/.discard"
sync -f "${INSTALL_DIR}" 2> /dev/null || sync
sudo rm -rf "${INSTALL_DIR}/.new" "${INSTALL_DIR}/.discard"
if ! sudo -u "${OWNER_USER}" test -w "${INSTALL_DIR}"; then
    echo "Warning: ${OWNER_USER} can't write ${INSTALL_DIR}, so in-app updates won't apply." >&2
fi

# ── An install from 240-MP's days ─────────────────────────────────────────────
# OSD/OS was 240-MP. An install of that (240-MP's own installer) ran
# /opt/240mp through 240mp.service: its service and what it put around the
# system go, so the two never fight over the screen; only now, with OSD/OS in
# place, so that a download that fails leaves it as it was. The app moves its
# data folder (~/.local/share/240-MP) over to its own on its first start.
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
# 6: osdos-stop also writes the JAMMA board's other modes on exit 31-38 (and
#    powers off after them on 51-58), so Display Output offers them.
# 7: NetworkManager lets the app's user scan, join and forget networks and
#    turn Wi-Fi on and off (50-osdos-networkmanager.rules), and osdos-stop
#    sets the Wi-Fi country through osdos-wifi.socket, so Settings offers
#    Wi-Fi.
# 8: osdos-stop also names the device through osdos-name.socket, so Settings
#    → About → System Information can rename it.
export OSDOS_LAUNCHER_API=8

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
        # Extract fully, then swap top-level dirs (bin, lib, share) via rename —
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
            # Bad extract: keep the old tree, and nothing staged. A tarball
            # that doesn't extract is of no use again, so it goes with its
            # staged.json: with that left, the app would offer an Install
            # that applies nothing, until the next boot discarded the stage
            # (above). The app offers the update again, downloaded afresh;
            # .failed says what became of this one.
            rm -rf "$INSTALL_DIR/.new" "$INSTALL_DIR/.old"
            mv -f "$UPDATES_DIR/staged.sha256" "$UPDATES_DIR/staged.sha256.failed" 2>/dev/null
            rm -f "$UPDATES_DIR/$STAGED_TARBALL" "$UPDATES_DIR/staged.json"
        fi
    else
        # Corrupt/partial stage — discard; the app re-offers the update.
        rm -f "$UPDATES_DIR/staged.sha256" "$UPDATES_DIR/staged.json"
    fi
fi

# ── Display ───────────────────────────────────────────────────────────────────
# From DISPLAY_BEGIN to DISPLAY_END is what scripts/rpi-run-local.sh takes, as
# it stands, to run a build of your own the way this launcher runs the app.
# DISPLAY_BEGIN
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
# DISPLAY_END

exec "${INSTALL_DIR}/bin/osdos" "$@"
LAUNCHER_SCRIPT

sudo chmod +x "${LAUNCHER}"

# ── Optional: systemd autostart ───────────────────────────────────────────────
# The service is written when it is asked for, never over the OSD/OS image's
# own. The helpers around it (osdos-stop, the sockets the app asks root
# through, the exit-to-terminal unit) are brought up to date for whichever
# service there is, one kept from an install before too: the launcher above
# offers the app everything they do (OSDOS_LAUNCHER_API).
if [[ "${AUTOSTART_REPLY}" =~ ^[Yy]$ ]] && [ -z "${IMAGE_SERVICE}" ]; then
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
SuccessExitStatus=10 12 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 129 130 143
ExecStopPost=+/usr/local/bin/osdos-stop
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
UNIT
fi

if [ -n "${SERVICE_USER}" ]; then
    # ExecStopPost helper: the user's Quit → Power Off (exit 0) powers the Pi off;
    # exit 10 means the user chose "Exit to Terminal", so instead spawn a login
    # shell on tty1 (see views/Settings.qml). Exit 12 is the quit menu's
    # "Restart": reboot. Exit 20-38 is Settings → Display Output on the OSD/OS
    # image: a display preset written, then a reboot; 40-58 the same, then off
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
# next-start, by osdos-display.socket instead: Display Output's At Next Start;
# with wifi-country, by osdos-wifi.socket: Settings → Wi-Fi's country; with
# device-name, by osdos-name.socket: Settings → About → System Information's
# Device Name.

# What comes in is read as the C locale reads it, whatever the system's
# language: under en_GB.UTF-8, Raspberry Pi OS's own, a range such as A-Z
# takes ç, é and İ too.
export LC_ALL=C

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
                 jamma-240p jamma-240p-native jamma-240p-super jamma-288p jamma-288p-super
                 jamma-480i jamma-576i jamma-25k jamma-31k)

# A switch the app has ended for: the preset written, or why not in the
# journal. The Pi restarts or goes off either way, since only that brings the
# app back: on the output it had, should the preset not be written, where the
# app says it is still on it.
switch_output() {
    display_output "$1" \
        || echo "osdos-stop: the $1 display preset couldn't be written; the output stays as it was" >&2
}

# At Next Start: a preset's name on a line from the app, through the socket,
# and "ok" back, or why not. Written now, for the firmware to read the next
# time the Pi starts; the output in force is kept in AT_BOOT first, as the one
# to go back to. Only a name from the list, never echoed. A refusal goes to
# the journal too, under the mode's name; answered, the request hasn't
# failed, whatever the answer.
refuse() {
    echo "$1"
    echo "osdos-stop $MODE: $1" >&2
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

# Settings → Wi-Fi's country: a two-letter code on a line from the app,
# through the socket, and "ok" back, or why not. Only two capital letters,
# and of those only a code the time zone database lists as a country (the
# check raspi-config makes itself); never echoed. raspi-config writes the
# code into the kernel's command line on the boot partition, sets it now (iw
# reg set) and turns Wi-Fi on; what it says goes to the journal, not into the
# answer.
ISO3166=/usr/share/zoneinfo/iso3166.tab
wifi_country() {
    local code
    IFS= read -r -t 5 -n 64 code || [ -n "$code" ] || { refuse "no country named"; return; }
    [[ "$code" =~ ^[A-Z]{2}$ ]] || { refuse "no such country"; return; }
    grep -q "^${code}[[:space:]]" "$ISO3166" || { refuse "no such country"; return; }
    raspi-config nonint do_wifi_country "$code" < /dev/null >&2 \
        || { refuse "raspi-config couldn't set it"; return; }
    # On the card before the answer, as a display preset is: the boot
    # partition only.
    sync -f /boot/firmware
    echo ok
}

# Settings → About → System Information's Device Name: a name on a line from
# the app, through the socket, and "ok" back, or why not. Only a name the
# network takes (RFC 1123, as the app has it too: letters, digits and
# hyphens, 63 at most, neither end a hyphen), never echoed. hostnamectl sets
# it now and for every start after; the line of /etc/hosts that names this
# machine (127.0.1.1, as pi-gen and raspi-config write it) names it so too,
# in place of the old name, its other names and the file's other lines kept
# (the line added if there is none); and avahi, which answers for
# <name>.local, starts again under it. What they say goes to the journal,
# not into the answer.
device_name() {
    local name old hosts=/etc/hosts
    IFS= read -r -t 5 -n 64 name || [ -n "$name" ] || { refuse "no name given"; return; }
    [[ "$name" =~ ^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$ ]] || { refuse "not a name the network takes"; return; }
    # The name it has now: /etc/hostname's first line that isn't a comment.
    old="$(awk '!/^[[:space:]]*(#|$)/ { print $1; exit }' /etc/hostname 2> /dev/null)"
    [ -e "$hosts" ] || hosts=/dev/null
    # The new /etc/hosts is written aside first, and put in place once
    # hostnamectl has the name: should either fail, the old name stays in
    # both, never one name in one and another in the other. On the line, the
    # old name and those made of it (<old>.lan) become the new one's in their
    # places; the new name goes first on a line that had neither.
    awk -v new="$name" -v old="$old" '
        $1 == "127.0.1.1" && !named {
            names = ""
            comment = ""
            for (i = 2; i <= NF; i++) {
                if ($i ~ /^#/) {
                    for (; i <= NF; i++)
                        comment = comment " " $i
                    break
                }
                n = $i
                if (old != "" && tolower(n) == tolower(old))
                    n = new
                else if (old != "" && index(tolower(n), tolower(old) ".") == 1)
                    n = new substr(n, length(old) + 1)
                if (tolower(n) in seen)
                    continue
                seen[tolower(n)] = 1
                names = names (names == "" ? "" : " ") n
            }
            if (!(tolower(new) in seen))
                names = new (names == "" ? "" : " " names)
            print "127.0.1.1\t" names comment
            named = 1
            next
        }
        { print }
        END { if (!named) print "127.0.1.1\t" new }
    ' "$hosts" > /etc/hosts.osdos-new || { rm -f /etc/hosts.osdos-new; refuse "/etc/hosts couldn't be written"; return; }
    if ! hostnamectl set-hostname "$name" < /dev/null >&2; then
        rm -f /etc/hosts.osdos-new
        # Some of it may have been set (the running name, not the file's).
        [ -z "$old" ] || hostnamectl set-hostname "$old" < /dev/null >&2
        refuse "hostnamectl couldn't set it"
        return
    fi
    if ! mv -f /etc/hosts.osdos-new /etc/hosts; then
        rm -f /etc/hosts.osdos-new
        [ -z "$old" ] || hostnamectl set-hostname "$old" < /dev/null >&2
        refuse "/etc/hosts couldn't be written"
        return
    fi
    systemctl try-restart avahi-daemon.service < /dev/null >&2
    # On the card before the answer: the root file system only.
    sync -f /etc/hostname /etc/hosts
    echo ok
}

MODE="${1:-}"
case "$MODE" in
    next-start)   next_start;   exit 0 ;;
    wifi-country) wifi_country; exit 0 ;;
    device-name)  device_name;  exit 0 ;;
esac

case "${EXIT_STATUS:-}" in
    0)  systemctl poweroff ;;  # Quit → Power Off, or Ctrl+Q
    10) systemctl start osdos-terminal.service ;;
    11) : ;;  # in-app update restart — Restart=on-failure brings the app back up
    12) systemctl reboot ;;  # the quit menu's Restart
    2[0-9]|3[0-8]) switch_output "${DISPLAY_PRESETS[EXIT_STATUS - 20]}"; systemctl reboot ;;
    # The same, then off: on the new output the next time the Pi is switched on.
    4[0-9]|5[0-8]) switch_output "${DISPLAY_PRESETS[EXIT_STATUS - 40]}"; systemctl poweroff ;;
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

    # Settings → Wi-Fi's country, while the app runs: it writes a two-letter
    # code to this socket, which only its user may, and osdos-stop
    # wifi-country, as root, has raspi-config set it and answers.
    sudo tee /etc/systemd/system/osdos-wifi.socket > /dev/null << 'WIFI_SOCKET'
[Unit]
Description=OSD/OS Wi-Fi country (Settings → Wi-Fi)

[Socket]
ListenStream=/run/osdos-wifi.sock
SocketUser=root
SocketGroup=@OSDOS_GROUP@
SocketMode=0660
Accept=yes
MaxConnections=1

[Install]
WantedBy=sockets.target
WIFI_SOCKET
    sudo sed -i "s/@OSDOS_GROUP@/$(id -gn "${SERVICE_USER}")/" /etc/systemd/system/osdos-wifi.socket
    sudo tee /etc/systemd/system/osdos-wifi@.service > /dev/null << 'WIFI_SERVICE'
[Unit]
Description=OSD/OS Wi-Fi country, one request
# One unit a request: gone once it has run, however it ended.
CollectMode=inactive-or-failed

[Service]
ExecStart=/usr/local/bin/osdos-stop wifi-country
StandardInput=socket
StandardOutput=socket
StandardError=journal
NoNewPrivileges=yes
# No PrivateNetwork=, unlike the display output's: raspi-config sets the Wi-Fi's
# regulatory domain over the kernel's nl80211 (iw reg set), and the Wi-Fi
# device is in the host's network namespace, not one of the unit's own.
PrivateTmp=yes
ProtectHome=yes
WIFI_SERVICE

    # Settings → About → System Information's Device Name, while the app
    # runs: it writes a name to this socket, which only its user may, and
    # osdos-stop device-name, as root, sets it and answers.
    sudo tee /etc/systemd/system/osdos-name.socket > /dev/null << 'NAME_SOCKET'
[Unit]
Description=OSD/OS device name (Settings → About → System Information)

[Socket]
ListenStream=/run/osdos-name.sock
SocketUser=root
SocketGroup=@OSDOS_GROUP@
SocketMode=0660
Accept=yes
MaxConnections=1

[Install]
WantedBy=sockets.target
NAME_SOCKET
    sudo sed -i "s/@OSDOS_GROUP@/$(id -gn "${SERVICE_USER}")/" /etc/systemd/system/osdos-name.socket
    sudo tee /etc/systemd/system/osdos-name@.service > /dev/null << 'NAME_SERVICE'
[Unit]
Description=OSD/OS device name, one request
# One unit a request: gone once it has run, however it ended.
CollectMode=inactive-or-failed

[Service]
ExecStart=/usr/local/bin/osdos-stop device-name
StandardInput=socket
StandardOutput=socket
StandardError=journal
NoNewPrivileges=yes
# hostnamectl and systemctl talk to their daemons over D-Bus, whose socket is
# a file's: the unit itself has no network of its own.
PrivateNetwork=yes
PrivateTmp=yes
ProtectHome=yes
NAME_SERVICE

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

    sudo systemctl daemon-reload
    sudo systemctl enable --now osdos-display.socket osdos-wifi.socket osdos-name.socket
    # A service kept as it was stays enabled or not, as it was left, and the
    # login on tty1 with it.
    if [[ "${AUTOSTART_REPLY}" =~ ^[Yy]$ ]]; then
        sudo systemctl mask getty@tty1.service autovt@.service
        sudo systemctl enable osdos.service
        echo "Service installed and enabled."
    else
        echo "The autostart service's helpers are brought up to date."
    fi
    if [ -z "${APP_WAS_RUNNING}" ]; then
        echo "Start now with: sudo systemctl start osdos"
    fi
fi

echo ""
echo "OSD/OS ${VERSION} installed successfully."
if [ -n "${APP_WAS_RUNNING}" ]; then
    echo "OSD/OS starts again on it."
fi
echo "Run: osdos"
