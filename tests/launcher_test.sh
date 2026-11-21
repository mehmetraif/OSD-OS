#!/usr/bin/env bash
# The launcher (/usr/local/bin/osdos), taken from scripts/install.sh as
# os/build.sh takes it, run on a stand-in install, data folder, /sys and boot
# partition, with a stand-in app that says which release it is and which
# output it was given. A staged update applied; one cut short at each point a
# power cut could leave, which starts the release before whole or applies the
# stage again, never a mix; and on a Pi 5, the display preset's output waited
# for as it registers late, or the first display taken when it never comes.
#   launcher_test.sh <scripts/install.sh>
set -u

INSTALL_SH="$1"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

awk -v marker=LAUNCHER_SCRIPT '
    $0 ~ "<< .?" marker ".?$" { on = 1; next }
    on && $0 == marker        { exit }
    on                        { print }
' "$INSTALL_SH" | sed -e "s#/opt/osdos#$T/opt/osdos#g" \
                      -e "s#/sys/class/drm#$T/sys/class/drm#g" \
                      -e "s#/boot/firmware#$T/boot#g" \
                      -e "s#/proc/device-tree/model#$T/model#g" \
                      -e "s#/dev/dri#$T/dev/dri#g" \
                      -e 's#"$tries" -lt 100#"$tries" -lt 15#' > "$T/osdos"
grep -q "$T/opt/osdos" "$T/osdos" && grep -q -- '-lt 15' "$T/osdos" \
    || { echo "FAIL: the launcher not found in $INSTALL_SH"; exit 1; }
chmod +x "$T/osdos"

failures=0
check() {  # what, expected, actual
    if [ "$2" != "$3" ]; then
        echo "FAIL: $1: expected '$2', got '$3'"
        failures=$((failures + 1))
    else
        echo "PASS: $1"
    fi
}

APP="$T/opt/osdos"
UPDATES="$T/data/updates"
# A release's tree, as its tarball unpacks (strip 3: ./usr/local/...): an app
# that says its release, the release of the share it finds ("2/2"; "2/1" is
# the new app on the old share), the output the launcher gave it and the KMS
# configuration it gave Qt.
release_tree() {  # dir, version
    mkdir -p "$1/bin" "$1/share/osdos"
    printf '#!/bin/sh\necho "%s/$(cat "%s/share/osdos/version") ${OSDOS_DRM_CONNECTOR:-} ${QT_QPA_EGLFS_KMS_CONFIG:-} mode=${OSDOS_DRM_MODE:-}"\n' "$2" "$APP" > "$1/bin/osdos"
    chmod +x "$1/bin/osdos"
    echo "$2" > "$1/share/osdos/version"
}
install_release() {  # version
    rm -rf "$APP"
    release_tree "$APP" "$1"
}
stage_release() {  # version
    local src="$T/src-$1"
    rm -rf "$src" "$UPDATES"
    release_tree "$src/usr/local" "$1"
    mkdir -p "$UPDATES"
    tar -czf "$UPDATES/OSD-OS-$1.tar.gz" -C "$src" .
    ( cd "$UPDATES" && sha256sum "OSD-OS-$1.tar.gz" > staged.sha256 )
}
run() { DATA_ROOT="$T/data" XDG_RUNTIME_DIR="$T" WAYLAND_DISPLAY= DISPLAY= "$T/osdos" 2> "$T/stderr"; }
leftovers() { ls -A "$APP" | grep -c '^\.' ; }

# A stage applied whole: the new release runs, the stage and the swap's
# folders gone.
install_release 1
stage_release 2
check "a stage applied runs the new release" "2/2" "$(run | cut -d' ' -f1)"
check "a stage applied leaves no stage" "no" "$([ -e "$UPDATES/staged.sha256" ] && echo yes || echo no)"
check "a stage applied leaves no swap folders" "0" "$(leftovers)"

# A stage that can't be put in place: the release there runs, the stage goes,
# and staged.sha256.failed says why, for the app to tell.
install_release 1
stage_release 2
echo damaged >> "$UPDATES/OSD-OS-2.tar.gz"
check "a stage that doesn't match its checksum isn't applied" "1/1" "$(run | cut -d' ' -f1)"
check "...and says why" "the download didn't match its checksum" "$(cat "$UPDATES/staged.sha256.failed" 2>&1)"
check "...leaving no stage" "no" "$([ -e "$UPDATES/staged.sha256" ] && echo yes || echo no)"
install_release 1
stage_release 2
echo "not a tarball" > "$UPDATES/OSD-OS-2.tar.gz"
( cd "$UPDATES" && sha256sum "OSD-OS-2.tar.gz" > staged.sha256 )
check "a stage that doesn't unpack isn't applied" "1/1" "$(run | cut -d' ' -f1)"
check "...and says why" "its release didn't unpack in full" "$(cat "$UPDATES/staged.sha256.failed" 2>&1)"
check "...leaving no stage or swap folders" "no 0" \
      "$([ -e "$UPDATES/staged.sha256" ] && echo yes || echo no) $(leftovers)"

# A swap folder this user can't clear (an installer of old cut short as it
# unpacked as root; here an rm that leaves it): said, and the stage not tried
# over it, which would fail each time and be offered again for ever.
install_release 1
stage_release 2
mkdir -p "$APP/.new/bin" "$T/stuck"
printf '#!/bin/sh\nfor a; do case "$a" in */.new) ;; *) %q -rf "$a" ;; esac; done\n' "$(command -v rm)" > "$T/stuck/rm"
chmod +x "$T/stuck/rm"
check "a swap folder that can't be cleared: the release there runs" "1/1" "$(PATH="$T/stuck:$PATH" run | cut -d' ' -f1)"
check "...the stage not tried, and why told for the app" \
      "$APP/.new, left by an install cut short, can't be cleared; run the installer again" \
      "$(cat "$UPDATES/staged.sha256.failed" 2>&1)"
check "...and in the journal" "1" "$(grep -c "can't be cleared; run the installer again" "$T/stderr")"
check "...leaving no stage" "no" "$([ -e "$UPDATES/staged.sha256" ] && echo yes || echo no)"
rm -rf "$APP/.new" "$T/stuck"

# A swap cut short with bin out of the way and the new one not yet in, or
# with bin swapped and share not: never the new app on the old share. The
# release before runs whole, or the stage, there until a swap is on the card,
# is applied again.
for cut in before after; do
    for stage in kept gone; do
        install_release 1
        stage_release 2
        [ "$stage" = gone ] && rm -rf "$UPDATES"
        mkdir -p "$APP/.new" "$APP/.old"
        release_tree "$APP/.new" 2
        mv "$APP/bin" "$APP/.old/bin"
        [ "$cut" = after ] && mv "$APP/.new/bin" "$APP/bin"
        want=2/2; [ "$stage" = gone ] && want=1/1
        check "a swap cut short $cut bin's, stage $stage, runs $want" "$want" "$(run | cut -d' ' -f1)"
        check "...leaving no swap folders" "0" "$(leftovers)"
    done
done

# Cut short as a finished swap's release before was being deleted: the new
# one runs, the remains go.
install_release 2
mkdir -p "$APP/.discard/share/osdos"
check "a swap finished but not cleared runs the new release" "2/2" "$(run | cut -d' ' -f1)"
check "...and clears it" "0" "$(leftovers)"

# The installer's own swap (its "Put the release in place" block), as it runs
# there, with stand-ins for sudo and systemctl and a service installed: the
# release put in place whole, the app stopped for it; cut short where a power
# cut could leave it, after which the launcher starts the release before
# whole; and over a swap cut short before, the new release all the same.
awk '/^# ── Put the release in place/ { on = 1; next }
     on && /^# ── /                   { exit }
     on                               { print }' "$INSTALL_SH" > "$T/put.sh"
grep -q -- '--strip-components=3' "$T/put.sh" \
    || { echo "FAIL: the installer's swap not found in $INSTALL_SH"; exit 1; }
# With the installer's own trap, which clears what it unpacked should it end
# before the swap.
awk "/^trap '/ { on = 1 } on { print } on && /' EXIT\$/ { exit }" "$INSTALL_SH" > "$T/trap.sh"
grep -q '^trap ' "$T/trap.sh" && grep -q 'EXIT$' "$T/trap.sh" \
    || { echo "FAIL: the installer's trap not found in $INSTALL_SH"; exit 1; }
touch "$T/osdos.service"
# The release put in place, the script ended (a power cut) as it would move
# the path given, or as tar unpacks with TAR_CUT set: how it exited. Every
# sudo it ran is in $T/sudo.
put_release() {  # version [path]
    local src="$T/src-$1"
    rm -rf "$src" "$T/download"
    release_tree "$src/usr/local" "$1"
    mkdir -p "$T/download"
    tar -czf "$T/download/OSD-OS-$1.tar.gz" -C "$src" .
    : > "$T/systemctl"
    : > "$T/sudo"
    {
        echo 'set -euo pipefail'
        printf 'INSTALL_DIR=%q TMP_DIR=%q TARBALL=%q SYSTEMD_SERVICE=%q OWNER_USER=%q CUT=%q TAR_CUT=%q APP_WAS_RUNNING= UNPACKED=\n' \
            "$APP" "$T/download" "OSD-OS-$1.tar.gz" "$T/osdos.service" "$(id -un)" "${2:-}" "${TAR_CUT:-}"
        printf 'systemctl() { echo "$*" >> %q; }\n' "$T/systemctl"
        printf 'getent() { echo "owner:x:1000:1000::%s:/bin/sh"; }\n' "$T/home"
        printf 'sudo() { echo "$*" >> %q; if [ "$1" = -u ]; then shift 2; fi; "$@"; }\n' "$T/sudo"
        echo 'mv() { if [ "$1" = "$CUT" ]; then exit 3; fi; command mv "$@"; }'
        echo 'tar() { command tar "$@"; if [ -n "$TAR_CUT" ]; then exit 3; fi; }'
        cat "$T/trap.sh"
        cat "$T/put.sh"
    } > "$T/put-run.sh"
    bash "$T/put-run.sh" > /dev/null 2>&1
    echo $?
}
rm -rf "$UPDATES"
install_release 1
check "the installer puts a release in place" "0" "$(put_release 2)"
check "...whole" "2/2" "$(run | cut -d' ' -f1)"
check "...leaving no swap folders" "0" "$(leftovers)"
check "...the app stopped for the swap" "1" "$(grep -c '^stop osdos.service$' "$T/systemctl")"
for cut in bin .new/bin share .new/share .old; do
    install_release 1
    put_release 2 "$APP/$cut" > /dev/null
    check "the installer's swap cut short as it moves $cut runs the release before whole" "1/1" "$(run | cut -d' ' -f1)"
    check "...leaving no swap folders" "0" "$(leftovers)"
done
# Cut short as it unpacks: what it unpacked goes, and the release before
# runs. The release is unpacked as the user the app runs as, into folders
# that are that user's from the start, so that even a power cut, which no
# trap outlives, leaves the launcher nothing it can't clear.
install_release 1
check "the installer cut short as it unpacks" "3" "$(TAR_CUT=1 put_release 2)"
check "...leaves no swap folders" "0" "$(leftovers)"
check "...and the release before runs" "1/1" "$(run | cut -d' ' -f1)"
check "the installer unpacks the release as the app's user" "1" \
      "$(grep -c "^-u $(id -un) tar -xzf - " "$T/sudo")"
check "...into folders that are that user's from the start" "1" \
      "$(grep -c "^install -d -o $(id -un) -g $(id -gn) .*/\.new .*/\.old$" "$T/sudo")"
# A stage the app had armed (the user's data folder is the launcher's here)
# goes as the installer puts a release in place: the launcher would otherwise
# put it in place over that one at the next start.
install_release 1
stage_release 1a
mkdir -p "$T/home/.local/share"
ln -sfn "$T/data" "$T/home/.local/share/OSD-OS"
check "the installer over an armed stage puts its release in place" "0" "$(put_release 2)"
check "...the stage gone" "no" "$([ -e "$UPDATES/staged.sha256" ] && echo yes || echo no)"
check "...and its release, not the stage's, runs" "2/2" "$(run | cut -d' ' -f1)"
rm -rf "$T/home" "$UPDATES"
install_release 1
put_release 2 "$APP/.new/share" > /dev/null
check "the installer over a swap cut short puts the new release in place" "0" "$(put_release 3)"
check "...whole" "3/3" "$(run | cut -d' ' -f1)"
install_release 1
put_release 2 "$APP/.new/share" > /dev/null
put_release 3 "$APP/.new/share" > /dev/null
check "cut short again over it, the release before both runs whole" "1/1" "$(run | cut -d' ' -f1)"
check "...leaving no swap folders" "0" "$(leftovers)"

# A Pi 5 on a preset naming the DPI output, HDMI connected too: the DPI
# output is waited for as it registers late, and taken.
install_release 1
rm -rf "$UPDATES"
echo "Raspberry Pi 5 Model B Rev 1.0" > "$T/model"
mkdir -p "$T/boot" "$T/dev/dri" "$T/sys/class/drm/card1-HDMI-A-1"
printf '# osdos-output: DPI\n# osdos-mode: 720x480\n' > "$T/boot/osdos-display.txt"
echo connected > "$T/sys/class/drm/card1-HDMI-A-1/status"
touch "$T/dev/dri/card1" "$T/dev/dri/card2"
( sleep 0.5; mkdir -p "$T/sys/class/drm/card2-DPI-1"; echo connected > "$T/sys/class/drm/card2-DPI-1/status"
  echo 720x480i > "$T/sys/class/drm/card2-DPI-1/modes" ) &
check "a Pi 5 waits for its preset's late output" "DPI-1" "$(run | cut -d' ' -f2)"
wait

# One that never comes: HDMI, and why, in the journal.
rm -rf "$T/sys/class/drm/card2-DPI-1"
check "a Pi 5 whose preset's output never comes takes the display connected" "" "$(run | cut -d' ' -f2)"
check "...and says why" "1" "$(grep -c "DPI output never appeared" "$T/stderr")"

# HDMI's preset on a Pi 5 (a mode, no output): the HDMI display connected,
# asked for 1920x1080 at 60 Hz when it has that size; Qt is given the
# refresh, mpv the size alone. Without it (a display of 1280x720 at most),
# the display's own choice, as before.
HDMI_PRESET="$(dirname "$INSTALL_SH")/../os/stage-osdos/03-boot/files/osdos-display-hdmi.txt"
cp "$HDMI_PRESET" "$T/boot/osdos-display.txt"
mkdir -p "$T/sys/class/drm/card1-HDMI-A-2" "$T/service-run"
echo disconnected > "$T/sys/class/drm/card1-HDMI-A-1/status"
echo connected > "$T/sys/class/drm/card1-HDMI-A-2/status"
printf '3840x2160\n2560x1440\n1920x1080\n1920x1080i\n1280x720\n' > "$T/sys/class/drm/card1-HDMI-A-2/modes"
out=$(env -u XDG_RUNTIME_DIR RUNTIME_DIRECTORY="$T/service-run" DATA_ROOT="$T/data" WAYLAND_DISPLAY= DISPLAY= "$T/osdos" 2> "$T/stderr")
check "a Pi 5 on HDMI's preset takes the HDMI display connected" "HDMI-A-2" "$(cut -d' ' -f2 <<< "$out")"
check "...mpv in 1920x1080" "mode=1920x1080" "$(cut -d' ' -f4 <<< "$out")"
check "...Qt at 60 Hz" '{ "device": "'"$T"'/dev/dri/card1", "outputs": [ { "name": "HDMI2", "primary": true, "mode": "1920x1080@60" } ] }' \
      "$(cat "$T/service-run/osdos-kms.json")"
printf '1280x720\n1920x1080i\n720x480\n' > "$T/sys/class/drm/card1-HDMI-A-2/modes"
out=$(env -u XDG_RUNTIME_DIR RUNTIME_DIRECTORY="$T/service-run" DATA_ROOT="$T/data" WAYLAND_DISPLAY= DISPLAY= "$T/osdos" 2> "$T/stderr")
check "...a display without 1080p left to its own choice" " mode=" "$(cut -d' ' -f2,4 <<< "$out")"
check "...Qt not given a mode" '{ "device": "'"$T"'/dev/dri/card1" }' "$(cat "$T/service-run/osdos-kms.json")"
check "the HDMI preset asks for 1080p60 of a Pi 5" "# osdos-mode: 1920x1080@60" "$(grep '^# osdos-mode:' "$HDMI_PRESET")"
check "...and limits a Pi 4's firmware to its pixel clock" "hdmi_pixel_freq_limit=148500000" \
      "$(sed -n '/^\[pi4\]/,/^\[/p' "$HDMI_PRESET" | grep '^hdmi_pixel_freq_limit=')"
check "...naming no output, which stays the display connected" "" "$(grep '^# osdos-output:' "$HDMI_PRESET")"
rm -rf "$T/sys/class/drm/card1-HDMI-A-2" "$T/service-run"
echo connected > "$T/sys/class/drm/card1-HDMI-A-1/status"

# Qt's KMS configuration (the display connected, card1): in the service's
# run-time folder (RuntimeDirectory=), or the session's; with neither, in a
# file of its own in the temp folder, never at a name there someone else could
# have made first; and not given to Qt when it can't be written.
rm -f "$T/boot/osdos-display.txt"
mkdir -p "$T/service-run" "$T/tmp"
kms() { env -u XDG_RUNTIME_DIR -u RUNTIME_DIRECTORY DATA_ROOT="$T/data" WAYLAND_DISPLAY= DISPLAY= "$@" "$T/osdos" 2> "$T/stderr" | cut -d' ' -f3; }
check "Qt's KMS configuration in the service's run-time folder" "$T/service-run/osdos-kms.json" \
      "$(kms RUNTIME_DIRECTORY="$T/service-run" XDG_RUNTIME_DIR="$T")"
check "...written" '{ "device": "'"$T"'/dev/dri/card1" }' "$(cat "$T/service-run/osdos-kms.json")"
check "...or in the session's" "$T/osdos-kms.json" "$(kms XDG_RUNTIME_DIR="$T")"
echo '{ "device": "/dev/dri/someone-elses" }' > "$T/tmp/osdos-kms.json"
conf=$(kms TMPDIR="$T/tmp")
check "...with neither, in a file of its own in the temp folder" "yes" \
      "$(case "$conf" in "$T"/tmp/osdos-kms.??????) echo yes ;; *) echo "no: $conf" ;; esac)"
check "...written" '{ "device": "'"$T"'/dev/dri/card1" }' "$(cat "$conf" 2>&1)"
check "...the name someone else made left alone" '{ "device": "/dev/dri/someone-elses" }' "$(cat "$T/tmp/osdos-kms.json")"
check "...and not given to Qt when it can't be written" "" "$(kms TMPDIR="$T/no-such-folder")"
check "...saying so" "1" "$(grep -c "KMS configuration couldn't be written" "$T/stderr")"
rm -rf "$T/service-run" "$T/tmp" "$T/osdos-kms.json"

# scripts/rpi-run-local.sh runs a build of your own with the launcher's
# display part, from the same install.sh: on a display server (DISPLAY set)
# the build gets xcb, the repository as its root, and its arguments.
RUN_LOCAL="$(dirname "$INSTALL_SH")/rpi-run-local.sh"
REPO="$(cd "$(dirname "$INSTALL_SH")/.." && pwd)"
printf '#!/bin/sh\necho "${QT_QPA_PLATFORM:-} ${APP_ROOT:-} $*"\n' > "$T/mybuild"
chmod +x "$T/mybuild"
check "rpi-run-local.sh runs the build as the launcher would" "xcb $REPO --fullscreen x" \
      "$(DISPLAY=:9 WAYLAND_DISPLAY= QT_QPA_PLATFORM= APP_ROOT= OSDOS_BIN="$T/mybuild" TMPDIR="$T" \
         bash "$RUN_LOCAL" --fullscreen x 2>&1)"
check "...and leaves no script behind" "0" "$(ls "$T" | grep -c '^osdos-run\.')"

if [ "$failures" -gt 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
