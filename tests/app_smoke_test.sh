#!/usr/bin/env bash
# The app itself, started offscreen on a data folder of its own: the main
# menu, with every module on it, or one module as the startup module, opens
# without a QML error or warning. Qt's import trace names each QML file as it
# loads, so the test knows the screen it is about is up; it waits a moment
# more for what that screen's first view prints, then stops the app as
# systemd does (SIGTERM). What the app prints once it says it is shutting
# down is its teardown's and doesn't count. Every proxy is a dead port, so
# nothing leaves the machine.
#   app_smoke_test.sh <osdos> <repository> <Qt's QML folder> [module folder]
# Skipped (77) without the app, which is built apart from the tests (build/),
# and for a module that imports a QML module of Qt's this Qt doesn't have.
set -u

APP="$1" QML="$3" FOLDER="${4:-}"
[ -x "$APP" ] || { echo "SKIP: no app at $APP (build it first, in build/)"; exit 77; }
# Both absolute: the app runs in a folder of the test's.
APP="$(cd "$(dirname "$APP")" && pwd)/$(basename "$APP")"
ROOT="$(cd "$2" && pwd)"

id() {  # a module's id, from its manifest
    sed -n 's/^ *"id": *"\([^"]*\)".*/\1/p' "$1" | head -n 1
}
if [ -n "$FOLDER" ]; then
    MANIFEST="$ROOT/modules/$FOLDER/manifest.json"
    ID=$(id "$MANIFEST")
    ENTRY=$(sed -n 's/^ *"entry_point_qml": *"\([^"]*\)".*/\1/p' "$MANIFEST" | head -n 1)
    [ -n "$ID" ] && [ -n "$ENTRY" ] || { echo "FAIL: no id or entry point in $MANIFEST"; exit 1; }
    WHAT="the $FOLDER module"
    VIEW="modules/$FOLDER/$ENTRY"
    SOURCES=("$ROOT/modules/$FOLDER")
    CONFIG="{\"app\": {\"startup_module\": \"$ID\"}, \"modules\": {\"$ID\": {\"enabled\": true}}}"
else
    WHAT="the main menu"
    VIEW="views/ModuleList.qml"
    SOURCES=("$ROOT/Main.qml" "$ROOT/views")
    CONFIG="{\"modules\": {"
    for manifest in "$ROOT"/modules/*/manifest.json; do
        CONFIG="$CONFIG\"$(id "$manifest")\": {\"enabled\": true}, "
    done
    CONFIG="${CONFIG%, }}}"
fi

# Qt's own QML modules the screen imports, each looked for in Qt's QML folder.
if [ -f "$QML/QtQuick/qmldir" ]; then
    for module in $(grep -rhoE '^import Qt[A-Za-z0-9.]*' --include='*.qml' "${SOURCES[@]}" | cut -d' ' -f2 | sort -u); do
        [ -f "$QML/${module//.//}/qmldir" ] \
            || { echo "SKIP: $WHAT needs $module, which this Qt doesn't have ($QML)"; exit 77; }
    done
fi

T="$(mktemp -d)"
pid=""
trap '[ -n "$pid" ] && kill -KILL "$pid" 2> /dev/null; rm -rf "$T"' EXIT
mkdir -p "$T/data" "$T/home" "$T/media"
mkdir -m 700 "$T/run"
printf '%s\n' "$CONFIG" > "$T/data/config.json"
DEAD=http://127.0.0.1:9
( cd "$T" && exec env -i PATH="$PATH" HOME="$T/home" LANG="${LANG:-C.UTF-8}" \
      APP_ROOT="$ROOT" DATA_ROOT="$T/data" OSDOS_MEDIA_DIR="$T/media" XDG_RUNTIME_DIR="$T/run" \
      OSDOS_WATCHDOG=0 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_IMPORT_TRACE=1 \
      http_proxy=$DEAD https_proxy=$DEAD all_proxy=$DEAD HTTP_PROXY=$DEAD HTTPS_PROXY=$DEAD ALL_PROXY=$DEAD \
      no_proxy=127.0.0.1,localhost NO_PROXY=127.0.0.1,localhost "$APP" ) > "$T/log" 2>&1 &
pid=$!

# The app's root is its canonical path: only the part after it is known here.
shown() { grep -qF "/$VIEW" "$T/log"; }
running() { kill -0 "$pid" 2> /dev/null; }
report() {  # the log without the import trace
    grep -vE '^(qt\.qml\.import:|QQmlImport)' "$T/log" | tail -n 40
}
tries=0
while ! shown && running && [ "$tries" -lt 300 ]; do
    sleep 0.1; tries=$((tries + 1))
done
if ! shown; then
    running && echo "FAIL: $WHAT wasn't shown within 30 seconds" || echo "FAIL: the app ended before $WHAT was shown"
    report; exit 1
fi
sleep 2
if ! running; then
    echo "FAIL: the app ended while $WHAT was shown"; report; exit 1
fi
kill -TERM "$pid"
tries=0
while running && [ "$tries" -lt 150 ]; do
    sleep 0.1; tries=$((tries + 1))
done
if running; then
    echo "FAIL: the app didn't stop within 15 seconds of SIGTERM"; report; exit 1
fi
wait "$pid"; status=$?
pid=""

# A QML warning starts with the file it is about (and the line in it); the
# rest are the engine's own. Up to the shutdown only.
warnings=$(sed '/^\[main\] Termination signal received/q' "$T/log" \
    | grep -vE '^(qt\.qml\.import:|QQmlImport)' \
    | grep -E '\.(qml|js):|<Unknown File>|QQmlComponent|QQmlApplicationEngine failed|QML engine failed|Binding loop')
failed=0
if [ -n "$warnings" ]; then
    echo "FAIL: QML warnings with $WHAT:"; printf '%s\n' "$warnings"; failed=1
fi
# 128 + the signal, as the service's stop helper (osdos-stop) expects it.
if [ "$status" -ne 143 ]; then
    echo "FAIL: the app exited with $status on SIGTERM, not 143"; report; failed=1
fi
[ "$failed" -eq 0 ] && echo "PASS: $WHAT opened without a QML warning, and the app stopped cleanly"
exit "$failed"
