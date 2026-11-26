#!/bin/sh
# Opens a streaming service's own web player full screen, for OSD/OS's Netflix
# and Prime Video modules, and Google's sign-in for its YouTube module
# (src/modules/web_player/WebPlayerBackend.h):
#
#   web-player.sh <service> <url> [letterbox|14:9|panscan|anamorphic]
#   web-player.sh --close <service>
#
# The module runs this as a takeover: on a headless Pi the app has handed the
# screen over before this starts, and takes it back once everything this
# starts has exited. Closing the browser (Ctrl+W or Alt+F4), or holding BACK in
# OSD/OS, ends the run.
#
# Holding BACK runs --close, which closes the browser the way Ctrl+W does, so
# that it saves what it holds first: Chromium writes new cookies, a sign-in
# among them, only every half minute, and stopping it outright loses them. It
# types Ctrl+W into cage (0.1.5 or later, for its virtual keyboard) with wtype,
# and exits non-zero where it can't, for the app to stop the run instead.
#
# Needs a Chromium-based browser with Widevine, which these players require.
# On Raspberry Pi OS: `sudo apt install chromium libwidevinecdm0`, and `cage`
# to give the browser a screen when there is no desktop, with `wtype` for
# --close.
#
# The third argument is the module's Scaling setting: how a 16:9 picture fills
# a 4:3 screen (letterbox by default). Chromium only; Safari and Google Chrome
# on a Mac keep the player's own letterbox.
#
# Environment (optional):
#   OSDOS_WEB_PLAYER_UA  the browser's user agent (default: see below; its
#                        240-MP name, MP240_WEB_PLAYER_UA, still works)
set -u

DATA=${DATA_ROOT:-${XDG_DATA_HOME:-$HOME/.local/share}/OSD-OS}

if [ "${1:-}" = "--close" ] && [ $# -eq 2 ]; then
    # Where the run's cage is (see the end), and wtype to type into it. A
    # moment first, for the browser to take the new keyboard's keymap: keys
    # typed straight away are sometimes lost.
    WAYLAND_FILE=$DATA/$2/wayland
    { [ -r "$WAYLAND_FILE" ] && command -v wtype >/dev/null 2>&1; } || exit 1
    { read -r RUNTIME_DIR && read -r SOCKET; } < "$WAYLAND_FILE" || exit 1
    XDG_RUNTIME_DIR=$RUNTIME_DIR WAYLAND_DISPLAY=$SOCKET exec wtype -s 300 -M ctrl -k w -m ctrl
fi

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    echo "usage: $0 <service> <url> [letterbox|14:9|panscan|anamorphic]"
    echo "       $0 --close <service>"
    exit 2
fi
SERVICE=$1
URL=$2
SCALING=${3:-letterbox}
# The browser's own profile for this service, so its sign-in survives between
# runs. WebPlayerBackend::signOut() deletes it.
PROFILE=$DATA/$SERVICE/browser
mkdir -p "$PROFILE"

if [ "$(uname -s)" = Darwin ]; then
    # A new instance on its own profile, so -W waits for exactly this one.
    if [ -d "/Applications/Google Chrome.app" ]; then
        exec open -W -n -a "Google Chrome" --args --kiosk --no-first-run \
            --user-data-dir="$PROFILE" "$URL"
    fi
    exec open -W -n -a Safari "$URL"
fi

# The same browsers, in the same order, as WebPlayerBackend checks for.
BROWSER=
for candidate in chromium chromium-browser google-chrome-stable google-chrome; do
    if command -v "$candidate" >/dev/null 2>&1; then
        BROWSER=$candidate
        break
    fi
done
if [ -z "$BROWSER" ]; then
    echo "No Chromium found. On Raspberry Pi OS: sudo apt install chromium libwidevinecdm0 cage wtype"
    exit 127
fi

set -- \
    --kiosk \
    --user-data-dir="$PROFILE" \
    --no-first-run \
    --noerrdialogs \
    --disable-infobars \
    --hide-crash-restore-bubble \
    --password-store=basic \
    --autoplay-policy=no-user-gesture-required \
    --ozone-platform-hint=auto \
    --enable-spatial-navigation

# Scaling: a stylesheet on the player's picture, through a small extension made
# for this run. Transforms rather than object-fit, so it works whether a player
# sizes its video element to the window or to the picture: for a 16:9 picture,
# 14:9 is 8/7 larger (thinner bars, a little of the sides cut), pan & scan 4/3
# (the sides cut), and anamorphic 4/3 taller only (squeezed to fill, for a TV
# set to 16:9).
CSS=
case "$SCALING" in
    14:9)       CSS='video { transform: scale(1.142857) !important; }' ;;
    panscan)    CSS='video { transform: scale(1.333333) !important; }' ;;
    anamorphic) CSS='video { transform: scaleY(1.333333) !important; }' ;;
esac
SCALER=$DATA/$SERVICE/scaling
rm -rf "$SCALER"
if [ -n "$CSS" ]; then
    mkdir -p "$SCALER"
    cat > "$SCALER/manifest.json" <<'MANIFEST'
{
  "manifest_version": 3,
  "name": "OSD/OS scaling",
  "version": "1",
  "content_scripts": [
    { "matches": ["<all_urls>"], "css": ["scaling.css"], "all_frames": true }
  ]
}
MANIFEST
    printf '%s\n' "$CSS" > "$SCALER/scaling.css"
    set -- "$@" --load-extension="$SCALER"
fi

# These players play on an Arm Linux browser only when it says it is ChromeOS,
# as Raspberry Pi OS's Chromium already does; say so here too, with the
# browser's real version, so other builds work the same. Asking it starts the
# browser once just for that, a second or more on a Pi: the answer is kept in
# the data folder's web-player-browser (the browser's file, resolved, and its
# time, then its version), and asked again only once the browser has changed.
UA=${OSDOS_WEB_PLAYER_UA:-${MP240_WEB_PLAYER_UA:-}}
case "$(uname -m)" in
    aarch64|arm64|armv7l|armv6l)
        if [ -z "$UA" ]; then
            VERSION=
            KEPT=$DATA/web-player-browser
            BROWSER_FILE=$(command -v "$BROWSER")
            BROWSER_FILE=$(readlink -f "$BROWSER_FILE" 2>/dev/null || printf '%s' "$BROWSER_FILE")
            BROWSER_TIME=$(stat -c %Y "$BROWSER_FILE" 2>/dev/null || true)
            STAMP=
            [ -n "$BROWSER_TIME" ] && STAMP="$BROWSER_FILE $BROWSER_TIME"
            if [ -n "$STAMP" ] && [ -r "$KEPT" ] \
                && { read -r KEPT_STAMP && read -r KEPT_VERSION; } < "$KEPT" && [ "$KEPT_STAMP" = "$STAMP" ]; then
                # Only a version's digits and dots: nothing else reaches the
                # command line from the file.
                case "$KEPT_VERSION" in
                    *[!0-9.]*|.*|'') ;;
                    *) VERSION=$KEPT_VERSION ;;
                esac
            fi
            if [ -z "$VERSION" ]; then
                VERSION=$("$BROWSER" --version 2>/dev/null | grep -oE '[0-9]+(\.[0-9]+)+' | head -n 1)
                # Whole or not at all: written beside it, then moved over it.
                if [ -n "$VERSION" ] && [ -n "$STAMP" ] \
                    && printf '%s\n%s\n' "$STAMP" "$VERSION" > "$KEPT.$$" 2>/dev/null; then
                    mv -f "$KEPT.$$" "$KEPT" 2>/dev/null || rm -f "$KEPT.$$"
                fi
            fi
            if [ -n "$VERSION" ]; then
                UA="Mozilla/5.0 (X11; CrOS aarch64 15917.71.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/$VERSION Safari/537.36"
            fi
        fi
        ;;
esac
if [ -n "$UA" ]; then
    set -- "$@" --user-agent="$UA"
fi

# With a desktop, the browser's window simply covers OSD/OS's.
if [ -n "${WAYLAND_DISPLAY:-}" ] || [ -n "${DISPLAY:-}" ]; then
    exec "$BROWSER" "$@" "$URL"
fi

# Without one, cage gives the browser the screen OSD/OS just handed over. The
# app holds no login seat for cage to share, so cage opens the display and the
# input devices itself, which the app's video and input groups allow.
if ! command -v cage >/dev/null 2>&1; then
    echo "cage is not installed: sudo apt install cage"
    exit 127
fi
export LIBSEAT_BACKEND="${LIBSEAT_BACKEND:-noop}"
# cage puts its socket in XDG_RUNTIME_DIR, which a service has no login
# session to provide: then a private one for this run, removed afterwards,
# even when OSD/OS ends the run (it signals the whole process group).
RUNTIME=
if [ -z "${XDG_RUNTIME_DIR:-}" ] || [ ! -w "${XDG_RUNTIME_DIR:-/nonexistent}" ]; then
    RUNTIME=$(mktemp -d "${TMPDIR:-/tmp}/osdos-$SERVICE.XXXXXX") || exit 1
    export XDG_RUNTIME_DIR="$RUNTIME"
fi
# Where cage's socket is, for --close: written from inside cage, which names
# it only to the browser.
WAYLAND_FILE=$DATA/$SERVICE/wayland
trap '[ -n "$RUNTIME" ] && rm -rf "$RUNTIME"; rm -f "$WAYLAND_FILE"' EXIT
trap 'exit 143' TERM INT HUP
cage -- sh -c 'printf "%s\n%s\n" "$XDG_RUNTIME_DIR" "$WAYLAND_DISPLAY" > "$1"; shift; exec "$@"' \
    web-player "$WAYLAND_FILE" "$BROWSER" "$@" --ozone-platform=wayland "$URL"
