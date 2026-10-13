#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# Builds the OSD/OS image: Raspberry Pi OS Lite (arm64, Trixie) from pi-gen,
# plus the stage in os/stage-osdos. See os/README.md.
#
# Usage:
#   [FIRST_USER_PASS=...] os/build.sh path/to/OSD-OS-<version>-linux-arm64.tar.gz
#
# The tarball is what the release workflow (or a native arm64 build plus
# `cmake --install` into a usr/local tree) produces. The image lands in
# os/work/pi-gen/deploy/.
#
# Settings (environment):
#   FIRST_USER_PASS   password of the first user; without one the account is
#                     locked and people log in as the user Imager creates
#   FIRST_USER_NAME   default "pi"; the user the app runs as
#   OSDOS_DISPLAY     hdmi (default), or another preset in stage-osdos/03-boot/files
#   ENABLE_SSH        0 (default) or 1
#   OSDOS_STREAMING   1 (default) or 0: the browser the Netflix and Prime
#                     Video modules open (Chromium with Widevine, and cage),
#                     about 400 MB
#   OSDOS_YOUTUBE     1 (default) or 0: the YouTube module's yt-dlp (its
#                     latest nightly build, which then updates itself) and
#                     Deno, the JavaScript runtime it needs, about 90 MB
#   OSDOS_ARCADE      1 (default) or 0: MAME, which the Arcade module plays
#                     arcade games in, about 450 MB
#   OSDOS_ROOT_SIZE   GiB the system keeps of the card, 8 by default; the rest
#                     becomes the exFAT film partition on the first boot. 0: no
#                     film partition, the system takes the whole card
#   TARGET_HOSTNAME   default "osdos"
#   IMG_NAME          default "osdos"
#   WPA_COUNTRY, LOCALE_DEFAULT, KEYBOARD_KEYMAP, KEYBOARD_LAYOUT,
#   TIMEZONE_DEFAULT, PUBKEY_SSH_FIRST_USER, PUBKEY_ONLY_SSH,
#   DEPLOY_COMPRESSION      passed to pi-gen; xz when unset
#   OSDOS_NATIVE=1    run pi-gen's build.sh directly (a Debian host, as root)
#                     instead of build-docker.sh
#   OSDOS_PREPARE_ONLY=1  set up the pi-gen tree and config, then stop
#   PI_GEN_REF        pi-gen commit to build from (pinned below)
#   WORK              scratch directory (default os/work)
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${HERE}/.." && pwd)

TARBALL=${1:-${OSDOS_TARBALL:-}}
if [ -z "${TARBALL}" ] || [ ! -f "${TARBALL}" ]; then
    echo "usage: [FIRST_USER_PASS=...] $0 <OSD-OS-<version>-linux-arm64.tar.gz>" >&2
    exit 2
fi
TARBALL=$(realpath "${TARBALL}")

# pi-gen insists on a password once the first-boot rename wizard is off. With
# none given, hand it a random one and lock the account: the app runs as that
# user all the same, and people log in as the user Raspberry Pi Imager creates.
OSDOS_LOCK_FIRST_USER=0
if [ -z "${FIRST_USER_PASS:-}" ]; then
    FIRST_USER_PASS=$(head -c 32 /dev/urandom | base64 | tr -d '\n')
    OSDOS_LOCK_FIRST_USER=1
    echo "note: no FIRST_USER_PASS; the first user's password will be locked." >&2
fi

# The arm64 branch of pi-gen builds the 64-bit images; pinned so a build is
# reproducible. Bump deliberately.
PI_GEN_REPO=${PI_GEN_REPO:-https://github.com/RPi-Distro/pi-gen}
PI_GEN_REF=${PI_GEN_REF:-4d8ee447dd3d37e8b0ef8752e460d9082d9d435d}
WORK=${WORK:-${HERE}/work}
PIGEN="${WORK}/pi-gen"
STAGE="${PIGEN}/stage-osdos"

# ── pi-gen at the pinned commit ───────────────────────────────────────────────
if [ ! -d "${PIGEN}/.git" ]; then
    mkdir -p "${PIGEN}"
    git -C "${PIGEN}" init -q
    git -C "${PIGEN}" remote add origin "${PI_GEN_REPO}"
fi
git -C "${PIGEN}" fetch -q --depth 1 origin "${PI_GEN_REF}"
git -C "${PIGEN}" checkout -q --force FETCH_HEAD
git -C "${PIGEN}" clean -qfdx -e work -e deploy

# Lite (stage2) is the base; only our stage exports an image.
touch "${PIGEN}/stage2/SKIP_IMAGES"

# ── Our stage, with the pieces generated from this repo ───────────────────────
# Docker builds copy the pi-gen directory into the container, so the stage has
# to live inside it.
cp -a "${HERE}/stage-osdos" "${STAGE}"
mkdir -p "${STAGE}/01-app/files"
# The streaming modules' browser is the bulk of the image; OSDOS_STREAMING=0
# leaves it out (the modules then say what to install).
if [ "${OSDOS_STREAMING:-1}" = "0" ]; then
    touch "${STAGE}/04-streaming/SKIP"
fi
# YouTube's yt-dlp and Deno; with OSDOS_YOUTUBE=0 the module says yt-dlp is
# missing.
if [ "${OSDOS_YOUTUBE:-1}" = "0" ]; then
    touch "${STAGE}/06-youtube/SKIP"
fi
# MAME for the Arcade module; with OSDOS_ARCADE=0 the module says it is
# missing.
if [ "${OSDOS_ARCADE:-1}" = "0" ]; then
    touch "${STAGE}/08-arcade/SKIP"
fi
# The film partition, unless OSDOS_ROOT_SIZE=0 leaves the card to the system.
case "${OSDOS_ROOT_SIZE:-8}" in
    ''|*[!0-9]*) echo "error: OSDOS_ROOT_SIZE must be a whole number of GiB" >&2; exit 1 ;;
esac
if [ "${OSDOS_ROOT_SIZE:-8}" -eq 0 ]; then
    touch "${STAGE}/05-media/SKIP"
fi
# pi-gen silently skips a prerun.sh or NN-run.sh that isn't executable, which
# would leave the stage without its root filesystem or the app. Don't rely on
# the checkout having kept the bits (a ZIP download, a Windows clone).
chmod +x "${STAGE}/prerun.sh" "${STAGE}"/*/[0-9][0-9]-run.sh

# The launcher, the stop helper, the exit-to-terminal unit and Display Output's
# next-start socket are taken verbatim from scripts/install.sh, so the image
# and a manual install never drift apart.
extract() {
    awk -v marker="$1" '
        $0 ~ "<< .?" marker ".?$" { on = 1; next }
        on && $0 == marker        { exit }
        on                        { print }
    ' "${REPO_ROOT}/scripts/install.sh"
}
extract LAUNCHER_SCRIPT > "${STAGE}/01-app/files/osdos-launcher"
extract STOP_HELPER     > "${STAGE}/02-system/files/osdos-stop"
extract TERMINAL_UNIT   > "${STAGE}/02-system/files/osdos-terminal.service"
extract DISPLAY_SOCKET  > "${STAGE}/02-system/files/osdos-display.socket"
extract DISPLAY_SERVICE > "${STAGE}/02-system/files/osdos-display@.service"
for f in 01-app/files/osdos-launcher 02-system/files/osdos-stop; do
    head -n1 "${STAGE}/${f}" | grep -q '^#!' \
        || { echo "error: could not extract ${f} from scripts/install.sh" >&2; exit 1; }
done
for f in osdos-terminal.service osdos-display.socket osdos-display@.service; do
    head -n1 "${STAGE}/02-system/files/${f}" | grep -q '^\[Unit\]' \
        || { echo "error: could not extract ${f} from scripts/install.sh" >&2; exit 1; }
done

cp "${TARBALL}" "${STAGE}/01-app/files/osdos.tar.gz"
# What the image is made of and under which licences, os/NOTICE.
cp "${HERE}/NOTICE" "${STAGE}/01-app/files/NOTICE"

# ── pi-gen config ─────────────────────────────────────────────────────────────
# %q-quoted: pi-gen sources this file, and a password may hold any character.
CONFIG="${PIGEN}/config"
{
    setting() { printf '%s=%q\n' "$1" "$2"; }
    setting IMG_NAME                       "${IMG_NAME:-osdos}"
    setting PI_GEN_RELEASE                 "OSD/OS"
    setting TARGET_HOSTNAME                "${TARGET_HOSTNAME:-osdos}"
    setting FIRST_USER_NAME                "${FIRST_USER_NAME:-pi}"
    setting FIRST_USER_PASS                "${FIRST_USER_PASS}"
    # The app's service runs as FIRST_USER_NAME, so it must keep its name: no
    # first-boot rename wizard (which would also fight the app for tty1).
    setting DISABLE_FIRST_BOOT_USER_RENAME 1
    setting ENABLE_SSH                     "${ENABLE_SSH:-0}"
    setting DEPLOY_COMPRESSION             "${DEPLOY_COMPRESSION:-xz}"
    setting STAGE_LIST                     "stage0 stage1 stage2 stage-osdos"
    for var in WPA_COUNTRY LOCALE_DEFAULT KEYBOARD_KEYMAP KEYBOARD_LAYOUT \
               TIMEZONE_DEFAULT PUBKEY_SSH_FIRST_USER PUBKEY_ONLY_SSH; do
        if [ -n "${!var:-}" ]; then
            setting "${var}" "${!var}"
        fi
    done
    # Read by the stage's own scripts, so they have to reach their environment.
    printf 'export OSDOS_DISPLAY=%q\n' "${OSDOS_DISPLAY:-hdmi}"
    printf 'export OSDOS_LOCK_FIRST_USER=%q\n' "${OSDOS_LOCK_FIRST_USER}"
    printf 'export OSDOS_ROOT_SIZE=%q\n' "${OSDOS_ROOT_SIZE:-8}"
} > "${CONFIG}"

# ── Build ─────────────────────────────────────────────────────────────────────
if [ "${OSDOS_PREPARE_ONLY:-0}" = "1" ]; then
    echo "pi-gen tree ready in ${PIGEN} (config: ${CONFIG}); not building."
    exit 0
fi
cd "${PIGEN}"
if [ "${OSDOS_NATIVE:-0}" = "1" ]; then
    ./build.sh -c "${CONFIG}"
else
    ./build-docker.sh -c "${CONFIG}"
fi
shopt -s nullglob
images=("${PIGEN}"/deploy/*osdos*)
echo "Image: ${images[*]:-(none found in ${PIGEN}/deploy)}"
