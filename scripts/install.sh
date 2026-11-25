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

# Settings → Network → Bluetooth talks to BlueZ as the user the app runs as, which
# BlueZ's D-Bus policy lets in through the bluetooth group.
if getent group bluetooth > /dev/null; then
    sudo usermod -aG bluetooth "${OWNER_USER}"
fi

# Settings → Network → Wi-Fi scans, joins and forgets networks and turns Wi-Fi on and
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
# The release's folders unpacked for the swap go too, should the script end
# before the swap begins: the launcher, which runs as the app's user, finds
# none left in its way.
TMP_DIR=$(mktemp -d)
APP_WAS_RUNNING=""
UNPACKED=""
trap 'rm -rf "$TMP_DIR"
      if [ -n "$UNPACKED" ]; then sudo rm -rf "${INSTALL_DIR}/.new" "${INSTALL_DIR}/.old" || true; fi
      if [ -n "$APP_WAS_RUNNING" ]; then sudo systemctl start osdos.service; fi' EXIT

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
# It is a copy of the launcher's, apart from it where root is needed: the
# leftovers of a swap cut short are cleared whoever owns them (the launcher
# can only say it can't); the folders are made, and the whole of
# $INSTALL_DIR given, to the app's user, who may not own it yet; a tarball
# without bin/osdos ends the script rather than being noted in
# staged.sha256.failed; the tarball is the download checked against GitHub's
# digest, not a stage; and the service is stopped and started around it.
# The launcher is written after the release is in place, so it isn't there
# to be called for it.
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
# The swap's folders the user's the app runs as from the start, and the
# release unpacked into them as that user: an install cut short as it
# unpacks (a power cut) leaves nothing the launcher, which applies in-app
# updates as that user, can't clear. The tarball is read by this script, in
# a folder of its own.
UNPACKED=1
sudo install -d -o "${OWNER_USER}" -g "$(id -gn "${OWNER_USER}")" "${INSTALL_DIR}/.new" "${INSTALL_DIR}/.old"
sudo -u "${OWNER_USER}" tar -xzf - --strip-components=3 --no-same-owner -C "${INSTALL_DIR}/.new" \
    < "${TMP_DIR}/${TARBALL}"
if [ ! -x "${INSTALL_DIR}/.new/bin/osdos" ]; then
    echo "Error: ${TARBALL} has no bin/osdos; nothing was installed." >&2
    exit 1
fi
# All of it the user's the app runs as, the release there too: the launcher
# swaps in staged in-app updates without root, and puts the release before
# back should the swap below be cut short.
sudo chown -R "${OWNER_USER}:" "${INSTALL_DIR}"
sync -f "${INSTALL_DIR}" 2> /dev/null || sync
# From here .old holds the release before, which the launcher puts back
# should the swap be cut short: kept.
UNPACKED=""
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
# An in-app update the app had staged and armed goes, with what the launcher
# said of one before: the launcher would otherwise put it in place over this
# release at the next start. The app sweeps its download.
OWNER_UPDATES="$(getent passwd "${OWNER_USER}" | cut -d: -f6)/.local/share/OSD-OS/updates"
sudo -u "${OWNER_USER}" rm -f "${OWNER_UPDATES}/staged.sha256" "${OWNER_UPDATES}/staged.json" \
    "${OWNER_UPDATES}/staged.sha256.failed"
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
#    → System Information can rename it.
# 9: osdos-stop also writes JAMMA 480i at 640 across on exit 39 (and powers
#    off after it on 59), so Display Output offers it.
# 10: osdos-stop also sets the time zone and the keyboard through
#    osdos-system.socket, so Settings → System offers Time Zone and Keyboard.
# 11: osdos-stop also sets the CPU's governor for an arcade game and puts it
#    back after, through osdos-game.socket, so the Arcade module asks for it.
# 12: osdos-stop also turns the firewall (osdos-firewall.service) on and off
#    through osdos-system.socket, so Settings → System offers Firewall.
export OSDOS_LAUNCHER_API=12

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
# One this user can't clear (left by an installer of old, cut short as it
# unpacked as root): said, and no stage tried over it, which would fail
# each time and be offered again for ever. The installer, run again,
# clears it.
STUCK=""
for d in "$INSTALL_DIR/.new" "$INSTALL_DIR/.old" "$INSTALL_DIR/.discard"; do
    if [ -e "$d" ] && [ -w "$INSTALL_DIR" ]; then
        STUCK="$d"
        echo "osdos: $d, left by an install cut short, can't be cleared; run the installer again" >&2
    fi
done

if [ -f "$UPDATES_DIR/staged.sha256" ] && [ -n "$STUCK" ]; then
    echo "$STUCK, left by an install cut short, can't be cleared; run the installer again" \
        > "$UPDATES_DIR/staged.sha256.failed"
    rm -f "$UPDATES_DIR/staged.sha256" "$UPDATES_DIR/staged.json"
elif [ -f "$UPDATES_DIR/staged.sha256" ] && [ -w "$INSTALL_DIR" ]; then
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
            # .failed says why this one wasn't put in place, which the app
            # tells on its Software Update page.
            rm -rf "$INSTALL_DIR/.new" "$INSTALL_DIR/.old"
            echo "its release didn't unpack in full" > "$UPDATES_DIR/staged.sha256.failed"
            rm -f "$UPDATES_DIR/staged.sha256" "$UPDATES_DIR/$STAGED_TARBALL" "$UPDATES_DIR/staged.json"
        fi
    else
        # Corrupt/partial stage — discard, saying why; the app re-offers the
        # update.
        echo "the download didn't match its checksum" > "$UPDATES_DIR/staged.sha256.failed"
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
    # A preset with a mode and no output (HDMI's, "# osdos-mode: 1920x1080@60")
    # asks it of the display connected, if that is HDMI and has the mode's
    # size, progressive: otherwise the display's own choice, as before.
    KMS_CARD=""; KMS_OUTPUT=""; KMS_MODE=""; want=""; mode=""
    PRESET=/boot/firmware/osdos-display.txt
    if [ -r "$PRESET" ] && tr -d '\0' 2>/dev/null < /proc/device-tree/model | grep -q '^Raspberry Pi 5'; then
        want=$(sed -n 's/^# osdos-output: *\([A-Za-z-]*\).*/\1/p' "$PRESET" | head -n1)
        mode=$(sed -n 's/^# osdos-mode: *\([0-9]*x[0-9]*\(@[0-9]*\)\{0,1\}\).*/\1/p' "$PRESET" | head -n1)
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
            if [ -n "$mode" ] && grep -qE "^${mode%@*}i?\$" "$d/modes" 2>/dev/null; then
                KMS_MODE="$mode"
            fi
            break
        done
        # HDMI's preset: the display connected, in the mode asked if it has
        # the size (the modes file lists sizes alone, 1920x1080i apart).
        if [ -z "$want" ] && [ -n "$mode" ]; then
            for s in /sys/class/drm/card*-HDMI-A-*/status; do
                [ -e "$s" ] && [ "$(cat "$s")" = connected ] || continue
                d=$(dirname "$s")
                if grep -qx "${mode%@*}" "$d/modes" 2>/dev/null; then
                    n=$(basename "$d"); KMS_CARD="${n%%-*}"; KMS_OUTPUT="${n#*-}"; KMS_MODE="$mode"
                fi
                break
            done
        fi
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
        if [ -n "$KMS_OUTPUT" ]; then
            # Qt names an output by its type and number: HDMI-A-1 is HDMI1.
            qt_name="${KMS_OUTPUT%-*}"
            qt_name="${qt_name%-[AB]}${KMS_OUTPUT##*-}"
            kms_json=$(printf '{ "device": "/dev/dri/%s", "outputs": [ { "name": "%s", "primary": true%s } ] }' \
                "$KMS_CARD" "$qt_name" "${KMS_MODE:+, \"mode\": \"$KMS_MODE\"}")
            # mpv's DRM output gets the size alone: it would refuse to play
            # on a display without the refresh asked, which the launcher can't
            # read from sysfs; of the modes of that size it takes the first,
            # the highest refresh. Qt takes the refresh, or the display's own
            # choice without it.
            export OSDOS_DRM_DEVICE="/dev/dri/$KMS_CARD" OSDOS_DRM_CONNECTOR="$KMS_OUTPUT" \
                OSDOS_DRM_MODE="${KMS_MODE%@*}"
        else
            kms_json=$(printf '{ "device": "/dev/dri/%s" }' "$KMS_CARD")
        fi
        # Qt's KMS configuration in a folder of this user's alone, the
        # service's run-time folder (RuntimeDirectory=) or the session's,
        # else in a file of its own made in the temp folder, never a name
        # there anyone could have made first; and given to Qt only once it is
        # written.
        kms_dir="${RUNTIME_DIRECTORY:-${XDG_RUNTIME_DIR:-}}"
        if [ -n "$kms_dir" ] && [ -d "$kms_dir" ] && [ -w "$kms_dir" ] && [ -O "$kms_dir" ]; then
            KMS_CONF="$kms_dir/osdos-kms.json"
        else
            KMS_CONF=$(mktemp "${TMPDIR:-/tmp}/osdos-kms.XXXXXX" 2> /dev/null) || KMS_CONF=""
        fi
        if [ -n "$KMS_CONF" ] && printf '%s\n' "$kms_json" > "$KMS_CONF"; then
            export QT_QPA_EGLFS_KMS_CONFIG="$KMS_CONF"
        else
            echo "osdos: Qt's KMS configuration couldn't be written; Qt picks the display itself" >&2
        fi
    fi
fi

export QT_QPA_PLATFORM
export QML2_IMPORT_PATH="/usr/lib/aarch64-linux-gnu/qt6/qml"
# DISPLAY_END

exec "${INSTALL_DIR}/bin/osdos" "$@"
LAUNCHER_SCRIPT

sudo chmod +x "${LAUNCHER}"

# ── The image's network settings for games and streams ────────────────────────
# Wi-Fi without power saving (NetworkManager's osdos-wifi-powersave.conf), and
# a network game's packets marked CS4 for Wi-Fi's video queue
# (osdos-netplay-priority.service, with the rules nft reads), as the OSD/OS
# image has them, and the image's inbound filter, installed but off (below):
# the image's own files, os/stage-osdos/02-system/files at
# this release's tag, never a copy of their text here. On a desktop Pi too,
# which then keeps its Wi-Fi awake as well. A release from before them, or no
# answer from GitHub, leaves the system as it is, and says so.
# NETWORK_FILES_BEGIN
IMAGE_FILES="https://raw.githubusercontent.com/${REPO}/${VERSION}/os/stage-osdos/02-system/files"
NETWORK_DIR="${TMP_DIR}/image-files"
mkdir -p "${NETWORK_DIR}"
network_file() {
    curl -fsSL --retry 3 -o "${NETWORK_DIR}/$1" "${IMAGE_FILES}/$1" && [ -s "${NETWORK_DIR}/$1" ]
}
if network_file osdos-wifi-powersave.conf; then
    # From the next connection, or the next start.
    sudo install -D -m 644 "${NETWORK_DIR}/osdos-wifi-powersave.conf" \
        /etc/NetworkManager/conf.d/osdos-wifi-powersave.conf
    echo "Wi-Fi's power saving is off from the next connection."
else
    echo "Warning: osdos-wifi-powersave.conf couldn't be read from ${VERSION}: Wi-Fi's power saving is left as it is." >&2
fi
if network_file osdos-netplay-priority.nft && network_file osdos-netplay-priority.service; then
    sudo apt-get install -y nftables || echo "Warning: nftables couldn't be installed: network games' packets go unmarked." >&2
    sudo install -D -m 644 "${NETWORK_DIR}/osdos-netplay-priority.nft" /usr/lib/osdos/netplay-priority.nft
    sudo install -m 644 "${NETWORK_DIR}/osdos-netplay-priority.service" /etc/systemd/system/osdos-netplay-priority.service
    sudo systemctl daemon-reload
    sudo systemctl enable osdos-netplay-priority.service
    sudo systemctl restart osdos-netplay-priority.service \
        || echo "Warning: osdos-netplay-priority.service didn't start: network games' packets go unmarked (journalctl -u osdos-netplay-priority)." >&2
else
    echo "Warning: the network game's marking couldn't be read from ${VERSION}: its packets go unmarked." >&2
fi
# The image's inbound filter (osdos-firewall.service: only the home network's
# addresses may connect), installed but left off: this system is its owner's,
# who may run services for other networks on it. Settings → System → Firewall
# turns it on. One turned on before is kept on, its rules read again in place.
if network_file osdos-firewall.nft && network_file osdos-firewall.service; then
    sudo install -D -m 644 "${NETWORK_DIR}/osdos-firewall.nft" /usr/lib/osdos/firewall.nft
    sudo install -m 644 "${NETWORK_DIR}/osdos-firewall.service" /etc/systemd/system/osdos-firewall.service
    sudo systemctl daemon-reload
    sudo systemctl try-reload-or-restart osdos-firewall.service \
        || echo "Warning: osdos-firewall.service's rules couldn't be read again (journalctl -u osdos-firewall)." >&2
    if sudo systemctl is-enabled --quiet osdos-firewall.service; then
        echo "The firewall stays on: only the home network can connect."
    else
        echo "The firewall is installed and off: Settings → System → Firewall turns it on."
    fi
else
    echo "Warning: the firewall couldn't be read from ${VERSION}: Settings → System → Firewall can't turn it on." >&2
fi
# NETWORK_FILES_END

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
# The app's user's own folder for what the launcher writes for a run (Qt's KMS
# configuration): /run/osdos.
RuntimeDirectory=osdos
# An arcade game's RetroArch or MAME runs at nice -5, ahead of the app and
# the system's services: the lowest nice the app may give. No real-time
# priority, which could starve the rest.
LimitNICE=-5
ExecStartPre=+-/usr/bin/systemctl stop osdos-terminal.service
ExecStart=${LAUNCHER}
Restart=on-failure
RestartForceExitStatus=11
RestartSec=5s
RestartMode=direct
SuccessExitStatus=10 11 12 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 129 130 143
ExecStopPost=+/usr/local/bin/osdos-stop
StandardOutput=journal
StandardError=journal
# A driver that says the same thing at every picture fills the journal,
# which is on the card: Mesa wrote 150 lines a second for six minutes once
# the app had run out of files. No more than 1000 lines in 30 seconds are
# kept, and the journal says how many it left out.
LogRateLimitIntervalSec=30s
LogRateLimitBurst=1000

[Install]
WantedBy=multi-user.target
UNIT
fi

if [ -n "${SERVICE_USER}" ]; then
    # ExecStopPost helper: the user's Quit → Power Off (exit 0) powers the Pi off;
    # exit 10 means the user chose "Exit to Terminal", so instead spawn a login
    # shell on tty1 (see views/Settings.qml). Exit 12 is the quit menu's
    # "Restart": reboot. Exit 20-39 is Settings → Screen → Display Output on the OSD/OS
    # image: a display preset written, then a reboot; 40-59 the same, then off
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
# with wifi-country, by osdos-wifi.socket: Settings → Network → Wi-Fi's country; with
# device-name, by osdos-name.socket: Settings → System → System Information's
# Device Name; with system-setting, by osdos-system.socket: Settings → System's
# Time Zone, Keyboard, SSH and Firewall; with game, by osdos-game.socket: an arcade
# game's CPU governor, for the length of the game.

# What comes in is read as the C locale reads it, whatever the system's
# language: under en_GB.UTF-8, Raspberry Pi OS's own, a range such as A-Z
# takes ç, é and İ too.
export LC_ALL=C

# Settings → Screen → Display Output on the OSD/OS image: the chosen preset (or the one
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
# The app's codes for them (os/stage-osdos/03-boot/files/display-presets.json): keep the two in step.
DISPLAY_PRESETS=(hdmi crt-ntsc crt-pal crt-gpio-ntsc crt-gpio-pal
                 scart-rgb-ntsc scart-rgb-pal scart-rgb-240p scart-rgb-288p previous
                 jamma-240p jamma-240p-native jamma-240p-super jamma-288p jamma-288p-super
                 jamma-480i jamma-576i jamma-25k jamma-31k jamma-480i-640)

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

# Settings → Network → Wi-Fi's country: a two-letter code on a line from the app,
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

# Settings → System → System Information's Device Name: a name on a line from
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

# Settings → System's Time Zone, Keyboard, SSH and Firewall: a line from the app, through the
# socket, "time-zone Europe/Istanbul" or "keyboard tr", and "ok" back, or why
# not. A time zone only as the time zone database names one (letters, digits,
# _ + and -, two slashes at most) and has one, a keyboard only as XKB names a
# layout and has it; neither echoed. timedatectl sets the zone, now and for
# every start after, and raspi-config the keyboard, the console's, where a
# name and a password are typed at Exit to Terminal: as OSD/OS Setup's file
# has them set. What they say goes to the journal, not into the answer.
# And SSH, "ssh on" or "ssh off" and nothing else: the service enabled and
# started, or disabled and stopped, now and for every start after. Only
# whether it runs: the password, the keys and whether a password logs in
# (/etc/ssh/sshd_config.d/00-osdos-setup.conf) stay as OSD/OS Setup's file
# left them. Turned on, the answer is "ok no-password" where the app's user
# (osdos.service's) has no password to log in with, for the app to say that
# nobody can yet without a key; never the password, nor anything of it.
# And the firewall, "firewall on" or "firewall off" and nothing else:
# osdos-firewall.service (only the home network's addresses may connect)
# enabled and started, or disabled and stopped, now and for every start
# after; refused where its unit or nft isn't there. Only through systemctl:
# this unit has no network of its own (PrivateNetwork=), and the rules are
# the service's to load. The journal says what the system now lets in.
ZONES=/usr/share/zoneinfo
LAYOUTS=/usr/share/X11/xkb/symbols
SHADOW=/etc/shadow
FIREWALL_UNIT=/etc/systemd/system/osdos-firewall.service
NFT=/usr/sbin/nft
# Whether the app's user has a password: its field of /etc/shadow neither
# empty nor locked (! or *). Taken as yes where the user or the file can't
# be told, so that nothing is said for want of knowing.
app_user_has_password() {
    local user
    user="$(systemctl show --property=User --value osdos.service < /dev/null 2> /dev/null)"
    [[ "$user" =~ ^[a-z_][a-z0-9_-]{0,31}$ ]] && [ -r "$SHADOW" ] || return 0
    awk -F: -v user="$user" '$1 == user { found = 1; usable = $2 != "" && $2 !~ /^[!*]/ }
                             END { exit !(usable || !found) }' "$SHADOW"
}
system_setting() {
    local line what value
    IFS= read -r -t 5 -n 96 line || [ -n "$line" ] || { refuse "nothing named"; return; }
    what="${line%% *}"
    value="${line#* }"
    [ "$what" != "$line" ] || value=""
    case "$what" in
        time-zone)
            [[ "$value" =~ ^[A-Za-z0-9_+-]+(/[A-Za-z0-9_+-]+){0,2}$ ]] || { refuse "no such time zone"; return; }
            [ -f "$ZONES/$value" ] || { refuse "no such time zone"; return; }
            timedatectl set-timezone "$value" < /dev/null >&2 \
                || { refuse "timedatectl couldn't set it"; return; }
            ;;
        keyboard)
            [[ "$value" =~ ^[a-z]{2,5}$ ]] || { refuse "no such keyboard"; return; }
            [ -f "$LAYOUTS/$value" ] || { refuse "no such keyboard"; return; }
            raspi-config nonint do_configure_keyboard "$value" < /dev/null >&2 \
                || { refuse "raspi-config couldn't set it"; return; }
            ;;
        ssh)
            case "$value" in
                on)
                    systemctl enable ssh.service < /dev/null >&2 || { refuse "systemctl couldn't enable it"; return; }
                    # Not left enabled for the next start when it can't run now.
                    if ! systemctl start ssh.service < /dev/null >&2; then
                        systemctl disable ssh.service < /dev/null >&2
                        refuse "it didn't start"
                        return
                    fi
                    ;;
                off)
                    systemctl disable ssh.service < /dev/null >&2 || { refuse "systemctl couldn't disable it"; return; }
                    systemctl stop ssh.service < /dev/null >&2 || { refuse "it didn't stop"; return; }
                    ;;
                *)
                    refuse "SSH is only on or off"
                    return
                    ;;
            esac
            ;;
        firewall)
            case "$value" in
                on|off) ;;
                *) refuse "the firewall is only on or off"; return ;;
            esac
            [ -f "$FIREWALL_UNIT" ] || { refuse "the firewall isn't installed"; return; }
            if [ "$value" = on ]; then
                [ -x "$NFT" ] || { refuse "nftables isn't installed"; return; }
                systemctl enable osdos-firewall.service < /dev/null >&2 || { refuse "systemctl couldn't enable it"; return; }
                # Not left enabled for the next start when its rules can't be loaded now.
                if ! systemctl start osdos-firewall.service < /dev/null >&2; then
                    systemctl disable osdos-firewall.service < /dev/null >&2
                    refuse "its rules couldn't be loaded"
                    return
                fi
                echo "osdos-stop $MODE: firewall on: only the home network's addresses can connect" >&2
            else
                systemctl disable osdos-firewall.service < /dev/null >&2 || { refuse "systemctl couldn't disable it"; return; }
                systemctl stop osdos-firewall.service < /dev/null >&2 || { refuse "it didn't stop"; return; }
                echo "osdos-stop $MODE: firewall off: anything that reaches this device can connect" >&2
            fi
            ;;
        *)
            refuse "nothing this sets"
            return
            ;;
    esac
    # On the card before the answer: the root file system only.
    sync -f /etc
    if [ "$what" = ssh ] && [ "$value" = on ] && ! app_user_has_password; then
        echo "ok no-password"
        return
    fi
    echo ok
}

# A game in the Arcade module, through osdos-game.socket: the app connects
# as RetroArch or MAME starts and holds the connection for the whole game.
# Its first line says what it wants, from fixed words only, never echoed:
# "start full-speed" (the CPU's governor at performance) or "start as-is"
# (Settings turned that off), "network" after either for a network game.
# Answered "ok" and the governor the game plays under, the flag the system's
# timers look for (GAME_FLAG) is up, and the old governors are kept in
# GAME_RUN. Meanwhile, every two seconds, the Pi's temperature, clock and
# throttling are read, and in a network game over Wi-Fi the radio's scans
# and roams counted (iw event). "end" from the app is answered with one line
# of what the Pi did during the game ("notice=" what is worth saying on
# screen, none, power, throttled or power,throttled, then the rest), which
# the journal has too. "end", the connection closing (the app gone), this
# unit stopped, or the app's service stopped (osdos-stop's ExecStopPost
# below) puts everything back and takes the flag down.
GAME_RUN=/run/osdos-game
GAME_FLAG=/run/osdos-game/playing
CPUFREQ=/sys/devices/system/cpu/cpufreq
THERMAL=/sys/class/thermal
FIRMWARE=/sys/devices/platform
NET=/sys/class/net
MODULES=/sys/module
# Hotter than this at the start (millidegrees Celsius), a Pi keeps its
# governor: performance would only bring the firmware's throttling at 80 °C
# (85 °C on a Pi 5) sooner.
GAME_HOT=75000

# The governors kept put back, and the flag taken down. Run more than once,
# by the game's own request and by ExecStopPost, it does no more.
game_undo() {
    # Its own IFS: run by the trap below, it may run in the middle of a read
    # with IFS= set for it.
    local policy governor IFS=$' \t\n'
    if [ -f "$GAME_RUN/governors" ]; then
        while read -r policy governor; do
            [[ "$policy" =~ ^policy[0-9]+$ && "$governor" =~ ^[a-z0-9_]+$ ]] || continue
            echo "$governor" > "$CPUFREQ/$policy/scaling_governor" 2> /dev/null \
                || echo "osdos-stop game: $policy's governor couldn't be put back to $governor" >&2
        done < "$GAME_RUN/governors"
    fi
    rm -rf "${GAME_RUN:?}"
}

# Where the Pi's numbers are, found once: GAME_ZONE, the CPU's thermal zone,
# and GAME_THROTTLE_FILE, the firmware's get_throttled (gone from kernel
# 6.12 on, where vcgencmd asks the firmware instead).
game_find() {
    local zone file
    GAME_ZONE=""
    for zone in "$THERMAL"/thermal_zone*; do
        [ "$(cat "$zone/type" 2> /dev/null)" = cpu-thermal ] && { GAME_ZONE=$zone; break; }
    done
    GAME_THROTTLE_FILE=""
    for file in "$FIRMWARE"/soc*/*firmware/get_throttled; do
        [ -r "$file" ] && { GAME_THROTTLE_FILE=$file; break; }
    done
    GAME_VCGENCMD=""
    command -v vcgencmd > /dev/null && GAME_VCGENCMD=1
}
# What the Pi is at: GAME_TEMP (millidegrees, "" without a CPU thermal
# zone), GAME_CLOCK (kHz, "" without CPU frequency scaling) and, with
# "throttling", GAME_THROTTLED, the firmware's get_throttled bits (0 where
# it can't be asked). Files are read by the shell itself: no program is
# started for them, every two seconds through the game.
game_look() {
    local value
    GAME_TEMP=""
    [ -n "$GAME_ZONE" ] && read -r GAME_TEMP 2> /dev/null < "$GAME_ZONE/temp"
    [[ "$GAME_TEMP" =~ ^[0-9]+$ ]] || GAME_TEMP=""
    GAME_CLOCK=""
    read -r GAME_CLOCK 2> /dev/null < "$CPUFREQ/policy0/scaling_cur_freq"
    [[ "$GAME_CLOCK" =~ ^[0-9]+$ ]] || GAME_CLOCK=""
    [ "${1:-}" = throttling ] || return 0
    GAME_THROTTLED=0
    if [ -n "$GAME_THROTTLE_FILE" ]; then
        read -r value 2> /dev/null < "$GAME_THROTTLE_FILE"
    elif [ -n "$GAME_VCGENCMD" ]; then
        value=$(vcgencmd get_throttled 2> /dev/null)
        value=${value#throttled=0x}
    fi
    [[ "${value:-}" =~ ^[0-9a-fA-F]+$ ]] && GAME_THROTTLED=$((16#$value))
    return 0
}
# The highest temperature, the clock's lowest and highest, and the
# throttling bits seen now (0 to 3) during the game, from one look.
game_note() {
    [ -n "$GAME_TEMP" ] && { [ -z "$TEMP_MAX" ] || [ "$GAME_TEMP" -gt "$TEMP_MAX" ]; } && TEMP_MAX=$GAME_TEMP
    if [ -n "$GAME_CLOCK" ]; then
        { [ -z "$CLOCK_MIN" ] || [ "$GAME_CLOCK" -lt "$CLOCK_MIN" ]; } && CLOCK_MIN=$GAME_CLOCK
        { [ -z "$CLOCK_MAX" ] || [ "$GAME_CLOCK" -gt "$CLOCK_MAX" ]; } && CLOCK_MAX=$GAME_CLOCK
    fi
    [ "${1:-}" = throttling ] && SEEN=$((SEEN | (GAME_THROTTLED & 0xf)))
    return 0
}
game_degrees() {
    printf '%d.%d °C' $(($1 / 1000)) $((($1 % 1000) / 100))
}
# get_throttled's bits by name: under-voltage, the clock capped, throttled,
# the soft temperature limit.
game_bits() {
    local names=""
    (($1 & 1)) && names+=", under-voltage"
    (($1 & 2)) && names+=", clock capped"
    (($1 & 4)) && names+=", throttled"
    (($1 & 8)) && names+=", soft temperature limit"
    printf '0x%x (%s)' "$1" "${names#, }"
}
# The Wi-Fi link: "<frequency in MHz> <signal in dBm>" as iw has it, or "".
game_link() {
    iw dev "$1" link 2> /dev/null | awk '
        $1 == "freq:"   { f = int($2) }
        $1 == "signal:" { s = $2 }
        END { if (f != "") print f, s }'
}
game_band() {
    if [ "$1" -lt 3000 ]; then echo 2.4 GHz
    elif [ "$1" -lt 5925 ]; then echo 5 GHz
    else echo 6 GHz; fi
}

game() {
    local line want words word network="" policy now kept="" governor
    local dev="" link_start="" link_end="" iw_pid="" scans roams roamoff wifi="" during
    local TEMP_MAX="" CLOCK_MIN="" CLOCK_MAX="" SEEN=0 start_bits end_bits samples=0 flags summary
    IFS= read -r -t 5 -n 64 line || [ -n "$line" ] || { refuse "nothing asked"; return; }
    read -r -a words <<< "$line"
    [ "${words[0]:-}" = start ] || { refuse "nothing this does"; return; }
    want="${words[1]:-}"
    [ "$want" = full-speed ] || [ "$want" = as-is ] || { refuse "nothing this does"; return; }
    for word in "${words[@]:2}"; do
        [ "$word" = network ] || { refuse "nothing this does"; return; }
        network=1
    done
    # A game before this one that was never put back (its helper killed):
    # put back first, so that what is kept is the system's own.
    [ -d "$GAME_RUN" ] && game_undo
    mkdir -p "$GAME_RUN" && : > "$GAME_FLAG" || { refuse "$GAME_RUN couldn't be written"; return; }
    trap 'game_undo; exit 0' TERM INT HUP
    game_find
    game_look throttling
    start_bits=$GAME_THROTTLED
    now=$(cat "$CPUFREQ/policy0/scaling_governor" 2> /dev/null)
    if [ -z "$now" ]; then
        kept="no CPU frequency scaling"
    elif [ "$want" = as-is ]; then
        kept="$now, as Settings asks"
    elif [ -n "$GAME_TEMP" ] && [ "$GAME_TEMP" -ge "$GAME_HOT" ]; then
        kept="$now, as the Pi was at $(game_degrees "$GAME_TEMP") at the start"
    elif [ $((GAME_THROTTLED & 0xe)) -ne 0 ]; then
        kept="$now, as the Pi was throttled at the start ($(printf '0x%x' "$GAME_THROTTLED"))"
    else
        for policy in "$CPUFREQ"/policy*; do
            [ -w "$policy/scaling_governor" ] || continue
            echo "${policy##*/} $(cat "$policy/scaling_governor")" >> "$GAME_RUN/governors"
            echo performance > "$policy/scaling_governor" \
                || echo "osdos-stop game: ${policy##*/} couldn't be set to performance" >&2
        done
    fi
    governor=$(cat "$CPUFREQ/policy0/scaling_governor" 2> /dev/null)
    if [ -n "$kept" ]; then
        echo "osdos-stop game: the governor kept at $kept" >&2
        echo "ok kept $kept"
    else
        echo "ok ${governor:-performance}"
    fi
    # A network game: the interface of the default route, and on Wi-Fi its
    # link and the radio's events, to count the scans and roams it makes
    # mid-game (those its firmware makes by itself, without telling the
    # kernel, can't be seen this way).
    if [ -n "$network" ]; then
        dev=$(ip -o -4 route show default 2> /dev/null | awk '{ for (i = 1; i < NF; i++) if ($i == "dev") { print $(i + 1); exit } }')
        if [ -z "$dev" ]; then
            wifi="network: no default route"
        elif [ ! -d "$NET/$dev/wireless" ]; then
            wifi="network over $dev"
        elif ! command -v iw > /dev/null; then
            wifi="Wi-Fi $dev not measured (no iw)"
        else
            link_start=$(game_link "$dev")
            iw event -t < /dev/null > "$GAME_RUN/wifi-events" 2> /dev/null &
            iw_pid=$!
        fi
    fi
    game_look
    game_note
    # Then the game, until "end" or the connection's end (read fails), the
    # Pi looked at every two seconds; the throttling every ten where only
    # vcgencmd can say it.
    while :; do
        if IFS= read -r -t 2 -n 64 line; then
            [ "$line" = end ] && break
        elif [ $? -le 128 ]; then
            line=""
            break
        else
            samples=$((samples + 1))
            if [ -n "$GAME_THROTTLE_FILE" ] || [ $((samples % 5)) -eq 0 ]; then
                game_look throttling
                game_note throttling
            else
                game_look
                game_note
            fi
        fi
    done
    game_look throttling
    game_note throttling
    end_bits=$GAME_THROTTLED
    # What happened during the game: the bits seen now, and the "since
    # boot" bits (16 to 19) the game set.
    during=$((SEEN | (((end_bits & ~start_bits) >> 16) & 0xf)))
    if [ -n "$iw_pid" ]; then
        kill "$iw_pid" 2> /dev/null
        wait "$iw_pid" 2> /dev/null
        link_end=$(game_link "$dev")
        scans=$(grep -c "^[0-9.]*: $dev (.*): scan started" "$GAME_RUN/wifi-events")
        roams=$(grep -ci "^[0-9.]*: $dev (.*): .*roam" "$GAME_RUN/wifi-events")
        roamoff=$(cat "$MODULES/brcmfmac/parameters/roamoff" 2> /dev/null)
        if [ -n "$link_start" ]; then
            wifi="Wi-Fi $dev $(game_band "${link_start%% *}") (${link_start%% *} MHz), signal ${link_start#* } dBm at the start"
            if [ -n "$link_end" ]; then
                wifi+=", ${link_end#* } dBm at the end"
            else
                wifi+=", not connected at the end"
            fi
        else
            wifi="Wi-Fi $dev not connected at the start"
        fi
        wifi+=", $scans scans, $roams roams${roamoff:+, roamoff $roamoff}"
    fi
    game_undo
    trap - TERM INT HUP
    flags=none
    (( during & 1 )) && flags=power
    (( during & 0xe )) && { [ "$flags" = none ] && flags=throttled || flags+=,throttled; }
    summary="governor ${governor:-none}"
    if [ -n "$CLOCK_MIN" ]; then
        if [ "$CLOCK_MIN" = "$CLOCK_MAX" ]; then
            summary+=", clock $((CLOCK_MAX / 1000)) MHz"
        else
            summary+=", clock $((CLOCK_MIN / 1000))-$((CLOCK_MAX / 1000)) MHz"
        fi
    fi
    if [ "$during" -eq 0 ]; then
        summary+=", throttling none"
    else
        summary+=", throttling $(game_bits "$during")"
    fi
    [ -n "$TEMP_MAX" ] && summary+=", highest $(game_degrees "$TEMP_MAX")"
    [ -n "$wifi" ] && summary+=", $wifi"
    echo "osdos-stop game: the game ended, the system as it was: $summary" >&2
    # Answered only to "end": a connection that closed has no one to read it.
    [ "$line" = end ] && echo "notice=$flags $summary"
}

MODE="${1:-}"
case "$MODE" in
    next-start)     next_start;     exit 0 ;;
    wifi-country)   wifi_country;   exit 0 ;;
    device-name)    device_name;    exit 0 ;;
    system-setting) system_setting; exit 0 ;;
    game)           game;           exit 0 ;;
esac

# The app's service stopped, or the app gone, mid-game: what the game's
# request changed put back, should its own helper not have done it.
[ -d "$GAME_RUN" ] && game_undo

case "${EXIT_STATUS:-}" in
    0)  systemctl poweroff ;;  # Quit → Power Off, or Ctrl+Q
    10) systemctl start osdos-terminal.service ;;
    11) : ;;  # in-app update restart — RestartForceExitStatus=11 brings the app back up
    12) systemctl reboot ;;  # the quit menu's Restart
    2[0-9]|3[0-9]) switch_output "${DISPLAY_PRESETS[EXIT_STATUS - 20]}"; systemctl reboot ;;
    # The same, then off: on the new output the next time the Pi is switched on.
    4[0-9]|5[0-9]) switch_output "${DISPLAY_PRESETS[EXIT_STATUS - 40]}"; systemctl poweroff ;;
    # Anything else leaves the Pi on: a stop from outside (129, 130, 143, or the
    # signal itself), and a crash or an error exit, after which systemd starts
    # the app again (Restart=on-failure), or brings up a login prompt.
    *)  : ;;
esac
STOP_HELPER
    sudo chmod +x /usr/local/bin/osdos-stop

    # The app's requests of root while it runs, each through a socket only its
    # user may write to, behind which osdos-stop, as root, does it and answers:
    # Display Output's At Next Start (next-start: a preset's name, written for
    # the next start), Settings → Network → Wi-Fi's country (wifi-country: a
    # two-letter code, which raspi-config sets), Settings → System → About →
    # System Information's Device Name (device-name), Settings → System's
    # Time Zone and Keyboard (system-setting: which, and its value) and an
    # arcade game's CPU governor (game: held for the length of the game,
    # everything put back as it ends). os/build.sh writes the image's units
    # from these lines too.
    # ROOT_REQUESTS_BEGIN
    # name|osdos-stop's mode|what it sets|where in Settings
    ROOT_REQUESTS=(
        "display|next-start|display output for the next start|Settings → Screen → Display Output"
        "wifi|wifi-country|Wi-Fi country|Settings → Network → Wi-Fi"
        "name|device-name|device name|Settings → System → System Information"
        "system|system-setting|time zone, keyboard, SSH and firewall|Settings → System"
        "game|game|CPU governor for a game|Arcade"
    )
    # A request's socket, osdos-<name>.socket, from its line: @OSDOS_GROUP@ is
    # the app's user's group.
    root_request_socket() {
        local name mode what where
        IFS='|' read -r name mode what where <<< "$1"
        cat << SOCKET
[Unit]
Description=OSD/OS ${what} (${where})

[Socket]
ListenStream=/run/osdos-${name}.sock
SocketUser=root
SocketGroup=@OSDOS_GROUP@
SocketMode=0660
Accept=yes
MaxConnections=1

[Install]
WantedBy=sockets.target
SOCKET
    }
    # The unit a request behind it runs, osdos-<name>@.service.
    root_request_service() {
        local name mode what where
        IFS='|' read -r name mode what where <<< "$1"
        cat << SERVICE
[Unit]
Description=OSD/OS ${what}, one request
# One unit a request: gone once it has run, however it ended.
CollectMode=inactive-or-failed

[Service]
ExecStart=/usr/local/bin/osdos-stop ${mode}
StandardInput=socket
StandardOutput=socket
StandardError=journal
NoNewPrivileges=yes
SERVICE
        case "${name}" in
            wifi) cat << 'NETWORK' ;;
# No PrivateNetwork=, unlike the display output's: raspi-config sets the Wi-Fi's
# regulatory domain over the kernel's nl80211 (iw reg set), and the Wi-Fi
# device is in the host's network namespace, not one of the unit's own.
NETWORK
            name) cat << 'NETWORK' ;;
# hostnamectl and systemctl talk to their daemons over D-Bus, whose socket is
# a file's: the unit itself has no network of its own.
PrivateNetwork=yes
NETWORK
            game) cat << 'NETWORK' ;;
# No PrivateNetwork=: in a network game it reads the route and the Wi-Fi
# link (ip, iw) of the host's network, not one of the unit's own.
NETWORK
            system) cat << 'NETWORK' ;;
# timedatectl talks to its daemon over D-Bus, whose socket is a file's: the
# unit itself has no network of its own.
PrivateNetwork=yes
NETWORK
            *) echo "PrivateNetwork=yes" ;;
        esac
        printf 'PrivateTmp=yes\nProtectHome=yes\n'
    }
    # ROOT_REQUESTS_END
    ROOT_SOCKETS=()
    for request in "${ROOT_REQUESTS[@]}"; do
        ROOT_SOCKETS+=("osdos-${request%%|*}.socket")
        root_request_socket "${request}" | sudo tee "/etc/systemd/system/osdos-${request%%|*}.socket" > /dev/null
        sudo sed -i "s/@OSDOS_GROUP@/$(id -gn "${SERVICE_USER}")/" "/etc/systemd/system/osdos-${request%%|*}.socket"
        root_request_service "${request}" \
            | sudo tee "/etc/systemd/system/osdos-${request%%|*}@.service" > /dev/null
    done

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
    # Restarted, not only started: one already listening keeps the group it
    # was made with until then, and the service's user may be another now.
    sudo systemctl enable "${ROOT_SOCKETS[@]}"
    sudo systemctl restart "${ROOT_SOCKETS[@]}"
    # A service kept as it was stays enabled or not, as it was left, and the
    # login on tty1 with it.
    if [[ "${AUTOSTART_REPLY}" =~ ^[Yy]$ ]]; then
        sudo systemctl mask getty@tty1.service autovt@.service
        # No consoles of logind's own: switched to for a game or a video, a
        # console would have it start the masked autovt@ there, and log the
        # failure at every hand-off. From the next start.
        sudo install -d /etc/systemd/logind.conf.d
        printf '[Login]\nNAutoVTs=0\nReserveVT=0\n' | sudo tee /etc/systemd/logind.conf.d/osdos.conf > /dev/null
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
