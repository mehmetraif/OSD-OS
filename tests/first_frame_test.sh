#!/usr/bin/env bash
# The app's first frame is drawn in the look's own colours: with a theme
# chosen, on the theme's ground, never on the default scheme's blue for a
# frame first. The settings are read before the window shows (Main.qml's
# showWindow()). Run on Qt's "minimal" platform, which draws a window the
# moment it is shown, as the Pi's EGLFS does: there a window shown before
# the settings were read drew a frame without them. The app is started on a
# copy of its root whose Main.qml says what ground each of its first frames
# is drawn on (a line added here, after the window's title). Every proxy is
# a dead port.
#   first_frame_test.sh <osdos> <repository>
# Skipped (77) without the app, which is built apart from the tests (build/),
# and where Qt has no "minimal" platform.
set -u

APP="$1"
[ -x "$APP" ] || { echo "SKIP: no app at $APP (build it first, in build/)"; exit 77; }
APP="$(cd "$(dirname "$APP")" && pwd)/$(basename "$APP")"
ROOT="$(cd "$2" && pwd)"

T="$(mktemp -d)"
pid=""
trap '[ -n "$pid" ] && kill -KILL "$pid" 2> /dev/null; rm -rf "$T"' EXIT
mkdir -p "$T/root" "$T/data" "$T/home" "$T/media"
mkdir -m 700 "$T/run"
for entry in "$ROOT"/*; do
    [ "$(basename "$entry")" = Main.qml ] || ln -s "$entry" "$T/root/"
done
awk '{ print }
     /^    title: "OSD\/OS"$/ {
         print "    property int framesSaid: 0"
         print "    onFrameSwapped: if (framesSaid++ < 1) console.log(\"FIRST-FRAME\", String(root.color).toUpperCase())"
         probed = 1
     }
     END { if (!probed) exit 3 }' "$ROOT/Main.qml" > "$T/root/Main.qml" \
    || { echo "FAIL: Main.qml has no title line to put the probe after"; exit 1; }
# Matrix: green on black.
printf '%s\n' '{"app": {"osd_theme": "matrix", "menu_music": "Off"}, "modules": {}}' > "$T/data/config.json"
DEAD=http://127.0.0.1:9
( cd "$T" && exec env -i PATH="$PATH" HOME="$T/home" LANG="${LANG:-C.UTF-8}" \
      APP_ROOT="$T/root" DATA_ROOT="$T/data" OSDOS_MEDIA_DIR="$T/media" XDG_RUNTIME_DIR="$T/run" \
      OSDOS_WATCHDOG=0 QT_QPA_PLATFORM=minimal QT_QUICK_BACKEND=software \
      http_proxy=$DEAD https_proxy=$DEAD all_proxy=$DEAD HTTP_PROXY=$DEAD HTTPS_PROXY=$DEAD ALL_PROXY=$DEAD \
      no_proxy=127.0.0.1,localhost NO_PROXY=127.0.0.1,localhost "$APP" ) > "$T/log" 2>&1 &
pid=$!

drawn() { grep -q 'FIRST-FRAME' "$T/log"; }
running() { kill -0 "$pid" 2> /dev/null; }
tries=0
while ! drawn && running && [ "$tries" -lt 300 ]; do
    sleep 0.1; tries=$((tries + 1))
done
if ! drawn; then
    if grep -q 'Could not find the Qt platform plugin "minimal"' "$T/log"; then
        echo 'SKIP: this Qt has no "minimal" platform'; exit 77
    fi
    echo "FAIL: the app drew no first frame within 30 seconds"; tail -n 30 "$T/log"; exit 1
fi
kill -TERM "$pid"; wait "$pid" 2> /dev/null; pid=""

ground=$(sed -n 's/^.*FIRST-FRAME \(#[0-9A-F]*\).*$/\1/p' "$T/log" | head -n 1)
if [ "$ground" != "#000000" ]; then
    echo "FAIL: the first frame was drawn on $ground, not on the theme's ground (#000000)"; exit 1
fi
echo "PASS: the first frame is drawn on the theme's ground"
