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
# the new app on the old share) and the output the launcher gave it.
release_tree() {  # dir, version
    mkdir -p "$1/bin" "$1/share/osdos"
    printf '#!/bin/sh\necho "%s/$(cat "%s/share/osdos/version") ${OSDOS_DRM_CONNECTOR:-}"\n' "$2" "$APP" > "$1/bin/osdos"
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

if [ "$failures" -gt 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
