#!/usr/bin/env bash
# The image's stop helper, osdos-stop, taken from scripts/install.sh as
# os/build.sh takes it, run on a stand-in boot partition with the image's
# presets: what each exit code writes, At Next Start's requests through the
# socket (osdos-stop next-start), good and bad, the Wi-Fi country's
# (osdos-stop wifi-country), checked against a stand-in for the time zone
# database's list of countries, and the device name's (osdos-stop
# device-name), on a stand-in /etc/hosts. systemctl, raspi-config and
# hostnamectl are stand-ins that only note what they were asked.
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
                      -e "s#/run/osdos-display-boot.txt#$T/run/osdos-display-boot.txt#g" \
                      -e "s#/usr/share/zoneinfo/iso3166.tab#$T/iso3166.tab#g" \
                      -e "s#/etc/hosts#$T/etc/hosts#g" -e "s#/etc/hostname#$T/etc/hostname#g" > "$T/osdos-stop"
grep -q "$T/run/osdos-display-boot.txt" "$T/osdos-stop" || { echo "FAIL: osdos-stop not found in $INSTALL_SH"; exit 1; }
grep -q "$T/iso3166.tab" "$T/osdos-stop" || { echo "FAIL: osdos-stop's list of countries not found in $INSTALL_SH"; exit 1; }
grep -q "$T/etc/hosts" "$T/osdos-stop" || { echo "FAIL: osdos-stop's /etc/hosts not found in $INSTALL_SH"; exit 1; }
printf '#!/bin/sh\necho "$*" >> "%s/systemctl"\n' "$T" > "$T/bin/systemctl"
# raspi-config says something of its own, which must stay out of the answer,
# and fails while raspi-config-fails is there.
printf '#!/bin/sh\necho "$*" >> "%s/raspi-config"\necho "raspi-config on its own"\n[ ! -e "%s/raspi-config-fails" ]\n' \
    "$T" "$T" > "$T/bin/raspi-config"
# hostnamectl writes /etc/hostname, as hostnamed does, and fails while
# hostnamectl-fails is there.
printf '#!/bin/sh\necho "$*" >> "%s/hostnamectl"\n[ ! -e "%s/hostnamectl-fails" ] || exit 1\necho "$2" > "%s/etc/hostname"\n' \
    "$T" "$T" "$T" > "$T/bin/hostnamectl"
chmod +x "$T/osdos-stop" "$T/bin/systemctl" "$T/bin/raspi-config" "$T/bin/hostnamectl"
cp "$PRESETS"/osdos-display-*.txt "$T/boot/"
# The time zone database's list of countries, as tzdata lays it out, in short.
printf '%s\n' '# ISO 3166 alpha-2 country codes' '#' '#country-' \
    $'#code\tname of country, territory, area, or subdivision' \
    $'DE\tGermany' $'GB\tBritain (UK)' $'TR\tTurkey' $'US\tUnited States' > "$T/iso3166.tab"

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
# A request through the Wi-Fi country's socket: standard input sent, what goes
# to the journal kept in $T/stderr, what raspi-config is asked noted afresh.
wifi_request() { : > "$T/raspi-config"; PATH="$T/bin:$PATH" "$T/osdos-stop" wifi-country 2> "$T/stderr"; }
wifi_country() { printf '%s\n' "$1" | wifi_request; }
raspi_asked()  { cat "$T/raspi-config"; }
# A request through the device name's socket, as the Wi-Fi country's: what
# hostnamectl and systemctl are asked noted afresh.
name_request() { : > "$T/hostnamectl"; : > "$T/systemctl"; PATH="$T/bin:$PATH" "$T/osdos-stop" device-name 2> "$T/stderr"; }
device_name()  { printf '%s\n' "$1" | name_request; }
# The same request under the locale named, from those built in $T/locales.
name_request_in() { : > "$T/hostnamectl"; : > "$T/systemctl"; PATH="$T/bin:$PATH" LOCPATH="$T/locales" LC_ALL="$1" "$T/osdos-stop" device-name 2> "$T/stderr"; }
hostnamectl_asked() { cat "$T/hostnamectl"; }
# /etc/hosts and /etc/hostname as pi-gen writes them, for a card named osdos.
fresh_hosts() {
    printf '%s\n' $'127.0.0.1\tlocalhost' $'::1\t\tlocalhost ip6-localhost ip6-loopback' \
        $'ff02::1\t\tip6-allnodes' $'127.0.1.1\tosdos' > "$T/etc/hosts"
    echo osdos > "$T/etc/hostname"
}
this_host() { grep '^127\.0\.1\.1' "$T/etc/hosts"; }
# What the helper writes aside before /etc/hosts, left behind.
left_aside() { [ -e "$T/etc/hosts.osdos-new" ] && echo "hosts.osdos-new"; }

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

# A preset that can't be written: the output as it was and why in the
# journal, and the reboot or the power-off all the same, since only that
# brings the app back.
mv "$T/boot/osdos-display-crt-pal.txt" "$T/crt-pal.txt"
on_exit 22 2> "$T/stderr"
check "exit 22 without its preset writes nothing" jamma-31k "$(in_force)"
check "...reboots all the same" reboot "$(asked)"
check "...and says why in the journal" 1 "$(grep -c "the crt-pal display preset couldn't be written" "$T/stderr")"
new_boot
on_exit 42 2> "$T/stderr"
check "exit 42 without its preset powers off all the same" poweroff "$(asked)"
check "...and says why in the journal" 1 "$(grep -c "the crt-pal display preset couldn't be written" "$T/stderr")"
mv "$T/crt-pal.txt" "$T/boot/osdos-display-crt-pal.txt"
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

# The Wi-Fi country, while the app runs: a code the list has goes to
# raspi-config as it came, and raspi-config's own words go to the journal,
# not into the answer. No reboot, no power-off.
: > "$T/systemctl"
check "wifi country: TR answers ok" ok "$(wifi_country TR)"
check "wifi country: raspi-config sets TR" "nonint do_wifi_country TR" "$(raspi_asked)"
check "wifi country: raspi-config's words in the journal" "raspi-config on its own" "$(cat "$T/stderr")"
check "wifi country: no reboot" "" "$(asked)"
check "wifi country: US answers ok" ok "$(wifi_country US)"
check "wifi country: raspi-config sets US" "nonint do_wifi_country US" "$(raspi_asked)"

# What isn't a code in the list is refused, isn't repeated back, and never
# reaches raspi-config.
check "a lower-case code isn't one" "no such country" "$(wifi_country tr)"
check "a lower-case code: raspi-config not asked" "" "$(raspi_asked)"
check "a code not in the list isn't one" "no such country" "$(wifi_country XX)"
check "a code not in the list: raspi-config not asked" "" "$(raspi_asked)"
check "three letters aren't a code" "no such country" "$(wifi_country TUR)"
check "three letters: raspi-config not asked" "" "$(raspi_asked)"
check "an empty line isn't a code" "no such country" "$(wifi_country '')"
check "an empty line: raspi-config not asked" "" "$(raspi_asked)"
check "two codes aren't one" "no such country" "$(wifi_country 'TR US')"
check "two codes: raspi-config not asked" "" "$(raspi_asked)"
check "a pattern that the list matches isn't a code" "no such country" "$(wifi_country 'T.')"
check "a pattern: raspi-config not asked" "" "$(raspi_asked)"
check "a long line isn't a code" "no such country" "$(wifi_country "TR$(printf 'x%.0s' $(seq 1 200))")"
check "a long line: raspi-config not asked" "" "$(raspi_asked)"
check "no code sent" "no country named" "$(wifi_request < /dev/null)"
check "no code sent: raspi-config not asked" "" "$(raspi_asked)"
touch "$T/raspi-config-fails"
check "raspi-config failing is a refusal" "raspi-config couldn't set it" "$(wifi_country TR)"
check "raspi-config failing: it was asked for TR" "nonint do_wifi_country TR" "$(raspi_asked)"
rm "$T/raspi-config-fails"
printf 'XX\n' | wifi_request > /dev/null
check "a Wi-Fi refusal answered is no failure" 0 "$?"
check "a Wi-Fi refusal goes to the journal" "osdos-stop wifi-country: no such country" "$(cat "$T/stderr")"
check "after them all, no reboot" "" "$(asked)"

# The device's name, while the app runs: one the network takes goes to
# hostnamectl as it came, /etc/hosts names this machine by it, its other lines
# kept, and avahi starts again under it. No reboot.
fresh_hosts
check "device name: osdos-salon answers ok" ok "$(device_name osdos-salon)"
check "device name: hostnamectl sets it" "set-hostname osdos-salon" "$(hostnamectl_asked)"
check "device name: /etc/hosts names it" $'127.0.1.1\tosdos-salon' "$(this_host)"
check "device name: localhost kept" $'127.0.0.1\tlocalhost' "$(head -n1 "$T/etc/hosts")"
check "device name: four lines still" 4 "$(wc -l < "$T/etc/hosts")"
check "device name: avahi starts again" "try-restart avahi-daemon.service" "$(asked)"
check "device name: nothing left aside" "" "$(left_aside)"
check "device name: capitals and digits, 63 letters" ok "$(device_name "Kabin2-$(printf 'x%.0s' $(seq 1 56))")"
check "device name: one letter" ok "$(device_name k)"
# Without the line pi-gen writes: one added.
printf '%s\n' $'127.0.0.1\tlocalhost' > "$T/etc/hosts"
check "device name: answers ok without the line" ok "$(device_name salon)"
check "device name: the line added" $'127.0.1.1\tsalon' "$(this_host)"
check "device name: localhost still first" $'127.0.0.1\tlocalhost' "$(head -n1 "$T/etc/hosts")"
# The line's other names and its comment stay, the old name goes, however
# it is written; the file's other lines stay too.
printf '%s\n' $'127.0.0.1\tlocalhost' $'127.0.1.1\tSalon kabin  # the cabinet' $'192.168.1.9\tnas' > "$T/etc/hosts"
check "device name: answers ok with other names on the line" ok "$(device_name kabin-2)"
check "device name: the line's other names and comment kept" $'127.0.1.1\tkabin-2 kabin # the cabinet' "$(this_host)"
check "device name: the lines after it kept" $'192.168.1.9\tnas' "$(tail -n1 "$T/etc/hosts")"
# A last line without its newline: the line added stays a line of its own.
printf '%s' $'127.0.0.1\tlocalhost' > "$T/etc/hosts"
check "device name: answers ok without a last newline" ok "$(device_name salon)"
check "device name: the last line as it was" $'127.0.0.1\tlocalhost' "$(head -n1 "$T/etc/hosts")"
check "device name: the line added after it" $'127.0.1.1\tsalon' "$(this_host)"
check "device name: two lines" 2 "$(wc -l < "$T/etc/hosts")"
# Names made of the old one follow it, in their places, as Debian writes the
# line: 127.0.1.1 <name>.<domain> <name>.
printf '%s\n' $'127.0.0.1\tlocalhost' $'127.0.1.1\tosdos.lan osdos' > "$T/etc/hosts"
echo osdos > "$T/etc/hostname"
check "device name: answers ok with a domain on the line" ok "$(device_name salon)"
check "device name: the name with its domain follows it" $'127.0.1.1\tsalon.lan salon' "$(this_host)"
# As cloud-init writes the line for a name without a domain: the name once.
printf '%s\n' $'127.0.1.1\tsalon salon' > "$T/etc/hosts"
check "device name: answers ok with the name twice" ok "$(device_name osdos)"
check "device name: named once" $'127.0.1.1\tosdos' "$(this_host)"
# A line without the old name: the new one goes first, the others after it.
printf '%s\n' $'127.0.1.1\tkabin' > "$T/etc/hosts"
check "device name: answers ok on a line without the old name" ok "$(device_name osdos-2)"
check "device name: first on that line" $'127.0.1.1\tosdos-2 kabin' "$(this_host)"
# /etc/hostname's comments aren't the name.
printf '%s\n' '# named by hand' '' 'osdos' > "$T/etc/hostname"
printf '%s\n' $'127.0.1.1\tosdos' > "$T/etc/hosts"
check "device name: answers ok after a comment in /etc/hostname" ok "$(device_name salon)"
check "device name: the old name read past the comment" $'127.0.1.1\tsalon' "$(this_host)"
# No /etc/hosts at all: one made with the line.
rm "$T/etc/hosts"
check "device name: answers ok without /etc/hosts" ok "$(device_name salon)"
check "device name: /etc/hosts made with the line" $'127.0.1.1\tsalon' "$(cat "$T/etc/hosts")"

# What the network doesn't take is refused, isn't repeated back, and never
# reaches hostnamectl or /etc/hosts.
fresh_hosts
for bad in 'salon kabini' '-salon' 'salon-' 'salon.local' 'salon;reboot' '$(reboot)' 'kabin_2' \
           "$(printf 'x%.0s' $(seq 1 64))" 'çay'; do
    check "device name: '$bad' refused" "not a name the network takes" "$(device_name "$bad")"
    check "device name: '$bad': hostnamectl not asked" "" "$(hostnamectl_asked)"
done
check "device name: an empty line isn't a name" "not a name the network takes" "$(device_name '')"
check "device name: nothing sent" "no name given" "$(name_request < /dev/null)"
check "device name: refused, /etc/hosts as it was" $'127.0.1.1\tosdos' "$(this_host)"
# Whatever the system's language: under en_GB.UTF-8, Raspberry Pi OS's own,
# bash's A-Z takes ç, é and İ too. The locale is built here from its source;
# where it can't be, or keeps its ranges to ASCII, there is nothing to see.
mkdir -p "$T/locales"
localedef -i en_GB -f UTF-8 "$T/locales/en_GB.UTF-8" > /dev/null 2>&1
if LOCPATH="$T/locales" LC_ALL=en_GB.UTF-8 bash -c '[[ é =~ ^[a-z]$ ]]' 2> /dev/null; then
    for bad in 'çay' 'kabiné' 'İzmir'; do
        check "device name: '$bad' refused under en_GB.UTF-8" "not a name the network takes" \
            "$(printf '%s\n' "$bad" | name_request_in en_GB.UTF-8)"
        check "device name: '$bad' under en_GB.UTF-8: hostnamectl not asked" "" "$(hostnamectl_asked)"
    done
else
    echo "SKIP: device name under en_GB.UTF-8: no such locale here, or its ranges are ASCII's"
fi
# hostnamectl failing: /etc/hosts as it was, and the old name asked back,
# should part of the new one have been set.
touch "$T/hostnamectl-fails"
check "device name: hostnamectl failing is a refusal" "hostnamectl couldn't set it" "$(device_name salon)"
check "device name: hostnamectl asked, then asked the old name back" $'set-hostname salon\nset-hostname osdos' "$(hostnamectl_asked)"
check "device name: failed, /etc/hosts as it was" $'127.0.1.1\tosdos' "$(this_host)"
check "device name: failed, nothing left aside" "" "$(left_aside)"
check "device name: failed, avahi left alone" "" "$(asked)"
rm "$T/hostnamectl-fails"
# /etc/hosts that can't be written: refused before hostnamectl is asked (the
# answer names the stand-in's path).
mkdir "$T/etc/hosts.osdos-new"
check "device name: /etc/hosts not written is a refusal" "$T/etc/hosts couldn't be written" "$(device_name salon)"
check "device name: not written, hostnamectl not asked" "" "$(hostnamectl_asked)"
check "device name: not written, /etc/hosts as it was" $'127.0.1.1\tosdos' "$(this_host)"
rmdir "$T/etc/hosts.osdos-new"
printf '%s\n' '-x' | name_request > /dev/null
check "a name refused answered is no failure" 0 "$?"
check "a name refused goes to the journal" "osdos-stop device-name: not a name the network takes" "$(cat "$T/stderr")"

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
