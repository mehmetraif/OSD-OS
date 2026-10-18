#!/usr/bin/env bash
# The image's stop helper, osdos-stop, taken from scripts/install.sh as
# os/build.sh takes it, run on a stand-in boot partition with the image's
# presets: what each exit code writes, and At Next Start's requests through
# the socket (osdos-stop next-start), good and bad. systemctl is a stand-in
# that only notes what it was asked.
#   osdos_stop_test.sh <scripts/install.sh> <os/stage-osdos/03-boot/files>
set -u

INSTALL_SH="$1"
PRESETS="$2"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/boot" "$T/etc/modprobe.d" "$T/run" "$T/bin"

awk -v marker=STOP_HELPER '
    $0 ~ "<< .?" marker ".?$" { on = 1; next }
    on && $0 == marker        { exit }
    on                        { print }
' "$INSTALL_SH" | sed -e "s#/boot/firmware#$T/boot#g" \
                      -e "s#/etc/modprobe.d#$T/etc/modprobe.d#g" \
                      -e "s#/run/osdos-display-boot.txt#$T/run/osdos-display-boot.txt#g" > "$T/osdos-stop"
grep -q "$T/run/osdos-display-boot.txt" "$T/osdos-stop" || { echo "FAIL: osdos-stop not found in $INSTALL_SH"; exit 1; }
printf '#!/bin/sh\necho "$*" >> "%s/systemctl"\n' "$T" > "$T/bin/systemctl"
chmod +x "$T/osdos-stop" "$T/bin/systemctl"
cp "$PRESETS"/osdos-display-*.txt "$T/boot/"

failures=0
check() {  # what, expected, actual
    if [ "$2" != "$3" ]; then
        echo "FAIL: $1: expected '$2', got '$3'"
        failures=$((failures + 1))
    else
        echo "PASS: $1"
    fi
}
# The preset a file is a copy of.
preset_of() {
    local f
    for f in "$T"/boot/osdos-display-*.txt; do
        case "$f" in *-previous.txt) continue ;; esac
        cmp -s "$f" "$1" && { basename "$f" .txt | sed 's/^osdos-display-//'; return; }
    done
    echo none
}
in_force()  { preset_of "$T/boot/osdos-display.txt"; }
previous()  { preset_of "$T/boot/osdos-display-previous.txt"; }
csync()     { [ -f "$T/etc/modprobe.d/osdos-display.conf" ] && echo yes || echo no; }
asked()     { tail -n1 "$T/systemctl" 2>/dev/null; }
on_exit()   { : > "$T/systemctl"; PATH="$T/bin:$PATH" EXIT_STATUS="$1" "$T/osdos-stop"; }
next_start() { printf '%s\n' "$1" | PATH="$T/bin:$PATH" "$T/osdos-stop" next-start; }
new_boot()  { rm -f "$T/run/osdos-display-boot.txt"; }

cp "$T/boot/osdos-display-hdmi.txt" "$T/boot/osdos-display.txt"

# A switch now: written, then a reboot; a Pi 5's SCART sync, not JAMMA's.
on_exit 26
check "exit 26 writes SCART RGB PAL" scart-rgb-pal "$(in_force)"
check "exit 26 keeps HDMI to go back to" hdmi "$(previous)"
check "exit 26 writes SCART's composite sync" yes "$(csync)"
check "exit 26 reboots" reboot "$(asked)"
new_boot
on_exit 30
check "exit 30 writes JAMMA" jamma-240p "$(in_force)"
check "exit 30 writes no composite sync" no "$(csync)"
new_boot
on_exit 29
check "exit 29 goes back" scart-rgb-pal "$(in_force)"
check "exit 29 brings SCART's sync back" yes "$(csync)"
new_boot
# The JAMMA board's other modes, 31-38, none with a composite sync.
on_exit 31
check "exit 31 writes JAMMA 240p 15 kHz (320x240)" jamma-240p-native "$(in_force)"
check "exit 31 writes no composite sync" no "$(csync)"
check "exit 31 reboots" reboot "$(asked)"
new_boot
on_exit 35
check "exit 35 writes JAMMA 480i" jamma-480i "$(in_force)"
new_boot
on_exit 38
check "exit 38 writes JAMMA 31 kHz" jamma-31k "$(in_force)"
new_boot
on_exit 39
check "exit 39 is no preset's: nothing written" jamma-31k "$(in_force)"
check "exit 39 is no preset's: nothing asked" "" "$(asked)"
new_boot
on_exit 29
check "exit 29 goes back to JAMMA 480i" jamma-480i "$(in_force)"
new_boot
on_exit 26
new_boot

# Then off: on it the next time the Pi is switched on.
on_exit 40
check "exit 40 writes HDMI" hdmi "$(in_force)"
check "exit 40 powers off" poweroff "$(asked)"
check "exit 40 takes SCART's sync away" no "$(csync)"
new_boot
on_exit 50
check "exit 50 writes JAMMA" jamma-240p "$(in_force)"
check "exit 50 powers off" poweroff "$(asked)"
new_boot
on_exit 58
check "exit 58 writes JAMMA 31 kHz" jamma-31k "$(in_force)"
check "exit 58 powers off" poweroff "$(asked)"
new_boot
on_exit 59
check "exit 59 is no preset's: nothing written" jamma-31k "$(in_force)"
check "exit 59 is no preset's: nothing asked" "" "$(asked)"
new_boot
on_exit 20

# At Next Start, while the app runs: no reboot, no power-off.
new_boot
: > "$T/systemctl"
check "next start: JAMMA 25 kHz answers ok" ok "$(next_start jamma-25k)"
check "next start: JAMMA 25 kHz written" jamma-25k "$(in_force)"
new_boot
on_exit 20
new_boot
: > "$T/systemctl"
check "next start: JAMMA answers ok" ok "$(next_start jamma-240p)"
check "next start: JAMMA written" jamma-240p "$(in_force)"
check "next start: the output in force kept in /run" hdmi "$(preset_of "$T/run/osdos-display-boot.txt")"
check "next start: HDMI to go back to" hdmi "$(previous)"
check "next start: no reboot" "" "$(asked)"
check "another for the next start answers ok" ok "$(next_start scart-rgb-ntsc)"
check "another for the next start: written" scart-rgb-ntsc "$(in_force)"
check "another for the next start: still HDMI to go back to" hdmi "$(previous)"
check "another for the next start: SCART's sync" yes "$(csync)"
# A switch now after it goes back to the output in force, not to the waiting one.
on_exit 30
check "a switch now over a waiting one: JAMMA" jamma-240p "$(in_force)"
check "a switch now over a waiting one: HDMI to go back to" hdmi "$(previous)"
on_exit 20
check "taken back: HDMI at the next start" hdmi "$(in_force)"
check "taken back: no composite sync" no "$(csync)"
check "taken back for the next start answers ok" ok "$(next_start hdmi)"

# What isn't a preset's name changes nothing, and isn't repeated back.
check "previous isn't a preset" "no such preset" "$(next_start previous)"
check "a path isn't a preset" "no such preset" "$(next_start ../../../etc/shadow)"
check "nothing isn't a preset" "no such preset" "$(next_start '')"
check "two names aren't one" "no such preset" "$(next_start 'hdmi jamma-240p')"
check "a long line isn't one" "no such preset" "$(next_start "$(printf 'x%.0s' $(seq 1 200))")"
check "nothing sent" "no preset named" "$(PATH="$T/bin:$PATH" "$T/osdos-stop" next-start < /dev/null)"
rm "$T/boot/osdos-display-crt-pal.txt"
check "a preset not on the boot partition" "the preset isn't on the boot partition" "$(next_start crt-pal)"
check "after them all, still HDMI" hdmi "$(in_force)"
printf 'previous\n' | PATH="$T/bin:$PATH" "$T/osdos-stop" next-start > /dev/null 2> "$T/stderr"
check "a refusal answered is no failure" 0 "$?"
check "a refusal goes to the journal" "osdos-stop next-start: no such preset" "$(cat "$T/stderr")"

# Neither a stop from outside nor a crash touches anything.
for code in 143 1 SEGV; do
    on_exit "$code"
    check "exit $code leaves the Pi on" "" "$(asked)"
done

if [ "$failures" -ne 0 ]; then
    echo "$failures failed"
    exit 1
fi
echo "all passed"
