#!/usr/bin/env bash
# The image's stop helper, osdos-stop, taken from scripts/install.sh as
# os/build.sh takes it, run on a stand-in boot partition with the image's
# presets: what each exit code writes, At Next Start's requests through the
# socket (osdos-stop next-start), good and bad, the Wi-Fi country's
# (osdos-stop wifi-country), checked against a stand-in for the time zone
# database's list of countries, and the device name's (osdos-stop
# device-name), on a stand-in /etc/hosts, and the time zone's and the
# keyboard's (osdos-stop system-setting), against stand-ins for the time zone
# database and XKB's layouts, and SSH's (osdos-stop system-setting ssh), on a
# stand-in /etc/shadow. systemctl, raspi-config, hostnamectl and timedatectl
# are stand-ins that only note what they were asked.
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
                      -e "s#^ZONES=/usr/share/zoneinfo\$#ZONES=$T/zoneinfo#" \
                      -e "s#^LAYOUTS=/usr/share/X11/xkb/symbols\$#LAYOUTS=$T/xkb#" \
                      -e "s#^SHADOW=/etc/shadow\$#SHADOW=$T/etc/shadow#" \
                      -e "s#/etc/hosts#$T/etc/hosts#g" -e "s#/etc/hostname#$T/etc/hostname#g" \
                      -e "s#^GAME_RUN=/run/osdos-game\$#GAME_RUN=$T/run/osdos-game#" \
                      -e "s#^GAME_FLAG=/run/osdos-game/playing\$#GAME_FLAG=$T/run/osdos-game/playing#" \
                      -e "s#^CPUFREQ=/sys/devices/system/cpu/cpufreq\$#CPUFREQ=$T/cpufreq#" \
                      -e "s#^THERMAL=/sys/class/thermal\$#THERMAL=$T/thermal#" \
                      -e "s#^FIRMWARE=/sys/devices/platform\$#FIRMWARE=$T/platform#" \
                      -e "s#^NET=/sys/class/net\$#NET=$T/net#" \
                      -e "s#^MODULES=/sys/module\$#MODULES=$T/module#" > "$T/osdos-stop"
grep -q "$T/run/osdos-display-boot.txt" "$T/osdos-stop" || { echo "FAIL: osdos-stop not found in $INSTALL_SH"; exit 1; }
grep -q "$T/iso3166.tab" "$T/osdos-stop" || { echo "FAIL: osdos-stop's list of countries not found in $INSTALL_SH"; exit 1; }
grep -q "$T/etc/hosts" "$T/osdos-stop" || { echo "FAIL: osdos-stop's /etc/hosts not found in $INSTALL_SH"; exit 1; }
grep -q "^ZONES=$T/zoneinfo\$" "$T/osdos-stop" || { echo "FAIL: osdos-stop's time zones not found in $INSTALL_SH"; exit 1; }
grep -q "^LAYOUTS=$T/xkb\$" "$T/osdos-stop" || { echo "FAIL: osdos-stop's keyboard layouts not found in $INSTALL_SH"; exit 1; }
grep -q "^SHADOW=$T/etc/shadow\$" "$T/osdos-stop" || { echo "FAIL: osdos-stop's /etc/shadow not found in $INSTALL_SH"; exit 1; }
# systemctl names the app's service's user as app-user has it, and fails a
# command while <command>-fails is there (enable-fails, start-fails, ...).
for v in GAME_RUN GAME_FLAG CPUFREQ THERMAL FIRMWARE NET MODULES; do
    grep -q "^$v=$T/" "$T/osdos-stop" || { echo "FAIL: osdos-stop's $v not found in $INSTALL_SH"; exit 1; }
done
printf '#!/bin/sh\necho "$*" >> "%s/systemctl"\n[ "$1" != show ] || cat "%s/app-user" 2> /dev/null\n[ ! -e "%s/$1-fails" ]\n' \
    "$T" "$T" "$T" > "$T/bin/systemctl"
# raspi-config says something of its own, which must stay out of the answer,
# and fails while raspi-config-fails is there.
printf '#!/bin/sh\necho "$*" >> "%s/raspi-config"\necho "raspi-config on its own"\n[ ! -e "%s/raspi-config-fails" ]\n' \
    "$T" "$T" > "$T/bin/raspi-config"
# hostnamectl writes /etc/hostname, as hostnamed does, and fails while
# hostnamectl-fails is there.
printf '#!/bin/sh\necho "$*" >> "%s/hostnamectl"\n[ ! -e "%s/hostnamectl-fails" ] || exit 1\necho "$2" > "%s/etc/hostname"\n' \
    "$T" "$T" "$T" > "$T/bin/hostnamectl"
# timedatectl fails while timedatectl-fails is there.
printf '#!/bin/sh\necho "$*" >> "%s/timedatectl"\necho "timedatectl on its own"\n[ ! -e "%s/timedatectl-fails" ]\n' \
    "$T" "$T" > "$T/bin/timedatectl"
chmod +x "$T/osdos-stop" "$T/bin/systemctl" "$T/bin/raspi-config" "$T/bin/hostnamectl" "$T/bin/timedatectl"
# The time zone database's zones and XKB's layouts, in short.
mkdir -p "$T/zoneinfo/Europe" "$T/zoneinfo/America/Argentina" "$T/xkb"
: > "$T/zoneinfo/Europe/Istanbul"; : > "$T/zoneinfo/America/Argentina/Buenos_Aires"; : > "$T/zoneinfo/UTC"
: > "$T/xkb/tr"; : > "$T/xkb/us"; : > "$T/xkb/latam"
echo secret > "$T/shadow"
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
# A request through the time zone's and the keyboard's socket: what
# timedatectl and raspi-config are asked noted afresh.
system_request() { : > "$T/timedatectl"; : > "$T/raspi-config"; PATH="$T/bin:$PATH" "$T/osdos-stop" system-setting 2> "$T/stderr"; }
system_setting() { printf '%s\n' "$1" | system_request; }
timedatectl_asked() { cat "$T/timedatectl"; }
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
check "exit 26 writes SCART RGB 576i" scart-rgb-pal "$(in_force)"
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
# The JAMMA board's other modes, 31-39, none with a composite sync.
on_exit 31
check "exit 31 writes JAMMA 320x240" jamma-240p-native "$(in_force)"
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
check "exit 39 writes JAMMA 480i at 640 across" jamma-480i-640 "$(in_force)"
check "exit 39 writes no composite sync" no "$(csync)"
check "exit 39 reboots" reboot "$(asked)"
new_boot
on_exit 60
check "exit 60 is no preset's: nothing written" jamma-480i-640 "$(in_force)"
check "exit 60 is no preset's: nothing asked" "" "$(asked)"
new_boot
on_exit 29
check "exit 29 goes back to JAMMA 31 kHz" jamma-31k "$(in_force)"
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
check "exit 59 writes JAMMA 480i at 640 across" jamma-480i-640 "$(in_force)"
check "exit 59 powers off" poweroff "$(asked)"
new_boot
on_exit 58
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

# The time zone and the keyboard, while the app runs: a zone the database
# has goes to timedatectl, a layout XKB has to raspi-config, as they came.
check "time zone: Europe/Istanbul answers ok" ok "$(system_setting 'time-zone Europe/Istanbul')"
check "time zone: timedatectl sets it" "set-timezone Europe/Istanbul" "$(timedatectl_asked)"
check "time zone: raspi-config not asked" "" "$(raspi_asked)"
check "time zone: three parts" ok "$(system_setting 'time-zone America/Argentina/Buenos_Aires')"
check "time zone: one part" ok "$(system_setting 'time-zone UTC')"
check "keyboard: tr answers ok" ok "$(system_setting 'keyboard tr')"
check "keyboard: raspi-config sets it" "nonint do_configure_keyboard tr" "$(raspi_asked)"
check "keyboard: timedatectl not asked" "" "$(timedatectl_asked)"
check "keyboard: five letters" ok "$(system_setting 'keyboard latam')"
# What isn't a zone or a layout the system has is refused, isn't repeated
# back, and reaches neither.
for bad in 'Europe/Ankara' 'Europe' '../shadow' 'Europe/../../shadow' '/etc/shadow' 'Europe/Istanbul; reboot' \
           'Europe/Istanbul UTC' '$(reboot)' 'a/b/c/d' ''; do
    check "time zone: '$bad' isn't one" "no such time zone" "$(system_setting "time-zone $bad")"
    check "time zone: '$bad': timedatectl not asked" "" "$(timedatectl_asked)"
done
for bad in 'xx' 'TR' 't' 'tr us' 'tr;reboot' '../shadow' 'turkish' ''; do
    check "keyboard: '$bad' isn't one" "no such keyboard" "$(system_setting "keyboard $bad")"
    check "keyboard: '$bad': raspi-config not asked" "" "$(raspi_asked)"
done
check "a setting it doesn't set" "nothing this sets" "$(system_setting 'hostname salon')"
check "a word alone names nothing it sets" "no such time zone" "$(system_setting 'time-zone')"
check "an empty line names nothing" "nothing this sets" "$(system_setting '')"
check "nothing sent" "nothing named" "$(system_request < /dev/null)"
check "after the refusals: timedatectl not asked" "" "$(timedatectl_asked)"
check "after the refusals: raspi-config not asked" "" "$(raspi_asked)"
touch "$T/timedatectl-fails"
check "timedatectl failing is a refusal" "timedatectl couldn't set it" "$(system_setting 'time-zone UTC')"
rm "$T/timedatectl-fails"
touch "$T/raspi-config-fails"
check "raspi-config failing is a refusal of the keyboard" "raspi-config couldn't set it" "$(system_setting 'keyboard us')"
rm "$T/raspi-config-fails"
printf 'keyboard xx\n' | system_request > /dev/null
check "a setting refused answered is no failure" 0 "$?"
check "a setting refused goes to the journal" "osdos-stop system-setting: no such keyboard" "$(cat "$T/stderr")"
check "after them all, still no reboot" "" "$(asked)"

# SSH, while the app runs: on, enabled and started; off, disabled and
# stopped; nothing else, and nothing of sshd's own settings. Turned on, the
# answer says when the app's user (osdos.service's, pi here) has no password
# to log in with, never anything of the password itself.
ssh_setting() { : > "$T/systemctl"; system_setting "$1"; }
changed()     { grep -v '^show ' "$T/systemctl"; }
echo pi > "$T/app-user"
printf 'root:*:19000:0:99999:7:::\npi:%s:19000:0:99999:7:::\n' '$6$invented$cAfEcAfEcAfE' > "$T/etc/shadow"
check "ssh on: answers ok" ok "$(ssh_setting 'ssh on')"
check "ssh on: enabled, then started" $'enable ssh.service\nstart ssh.service' "$(changed)"
check "ssh on: asks whose the app is" "show --property=User --value osdos.service" "$(grep '^show ' "$T/systemctl")"
check "ssh off: answers ok" ok "$(ssh_setting 'ssh off')"
check "ssh off: disabled, then stopped" $'disable ssh.service\nstop ssh.service' "$(changed)"
check "ssh off: whose the app is not asked" "" "$(grep '^show ' "$T/systemctl")"
for locked in '!' '!$6$invented$cAfEcAfEcAfE' '*' ''; do
    printf 'pi:%s:19000:0:99999:7:::\n' "$locked" > "$T/etc/shadow"
    check "ssh on, pi's password '$locked': no password to log in with" "ok no-password" "$(ssh_setting 'ssh on')"
    check "ssh on, pi's password '$locked': on all the same" $'enable ssh.service\nstart ssh.service' "$(changed)"
done
check "ssh on: nothing of the password in the journal" "" "$(grep -e invented -e cAfE "$T/stderr")"
check "ssh off, no password: only ok" ok "$(ssh_setting 'ssh off')"
# Where whose the app is, or its password, can't be told: nothing said.
: > "$T/app-user"
check "ssh on, the app's user unknown: ok" ok "$(ssh_setting 'ssh on')"
echo 'pi;reboot' > "$T/app-user"
check "ssh on, a user that isn't a name: ok" ok "$(ssh_setting 'ssh on')"
echo pi > "$T/app-user"
rm "$T/etc/shadow"
check "ssh on, no /etc/shadow: ok" ok "$(ssh_setting 'ssh on')"
printf 'pi:$6$invented$cAfEcAfEcAfE:19000:0:99999:7:::\n' > "$T/etc/shadow"
# Only on or off: nothing else reaches systemctl.
for bad in '' 'yes' 'ON' 'on off' 'on;reboot' '$(reboot)' 'restart' 'on ' 'mask'; do
    check "ssh: '$bad' isn't on or off" "SSH is only on or off" "$(ssh_setting "ssh $bad")"
    check "ssh: '$bad': systemctl not asked" "" "$(cat "$T/systemctl")"
done
check "ssh alone is neither" "SSH is only on or off" "$(ssh_setting 'ssh')"
check "ssh refused goes to the journal" "osdos-stop system-setting: SSH is only on or off" "$(cat "$T/stderr")"
# systemctl failing: said, and SSH not left enabled for the next start when
# it couldn't start now.
touch "$T/enable-fails"
check "enable failing is a refusal" "systemctl couldn't enable it" "$(ssh_setting 'ssh on')"
check "enable failing: not started" "enable ssh.service" "$(changed)"
rm "$T/enable-fails"
touch "$T/start-fails"
check "start failing is a refusal" "it didn't start" "$(ssh_setting 'ssh on')"
check "start failing: disabled again" $'enable ssh.service\nstart ssh.service\ndisable ssh.service' "$(changed)"
rm "$T/start-fails"
touch "$T/disable-fails"
check "disable failing is a refusal" "systemctl couldn't disable it" "$(ssh_setting 'ssh off')"
check "disable failing: not stopped" "disable ssh.service" "$(changed)"
rm "$T/disable-fails"
touch "$T/stop-fails"
check "stop failing is a refusal" "it didn't stop" "$(ssh_setting 'ssh off')"
rm "$T/stop-fails"
check "after SSH, still no reboot" "" "$(grep -e reboot -e poweroff "$T/systemctl")"

# An arcade game's CPU governor (osdos-stop game, through osdos-game.socket),
# on a stand-in for the kernel's files: one CPU frequency policy, the CPU's
# thermal zone and the firmware's throttling bits. The helper runs as the
# socket runs it, its standard input the app's connection, held open for
# the game (descriptor 7 here).
mkdir -p "$T/cpufreq/policy0" "$T/thermal/thermal_zone0" "$T/platform/soc/soc:firmware"
echo cpu-thermal > "$T/thermal/thermal_zone0/type"
governor()  { cat "$T/cpufreq/policy0/scaling_governor"; }
playing()   { [ -e "$T/run/osdos-game/playing" ] && echo up || echo down; }
pi_is() {  # governor, temperature (millidegrees), get_throttled
    echo "$1" > "$T/cpufreq/policy0/scaling_governor"
    echo "$2" > "$T/thermal/thermal_zone0/temp"
    echo "$3" > "$T/platform/soc/soc:firmware/get_throttled"
}
# The game started with the line given: the helper left running, its
# answer (the first line it writes) printed once it has one.
game_start() {
    rm -f "$T/game-in" "$T/game-out"
    mkfifo "$T/game-in"
    : > "$T/game-out"
    PATH="$T/bin:$PATH" "$T/osdos-stop" game < "$T/game-in" > "$T/game-out" 2> "$T/stderr" &
    GAME_PID=$!
    exec 7> "$T/game-in"
    printf '%s\n' "$1" >&7
    local i
    for i in $(seq 100); do
        grep -q . "$T/game-out" && break
        sleep 0.05
    done
    head -n1 "$T/game-out"
}
# Its end: "end" sent, or the connection closed (the app gone), and the
# helper waited for.
game_end()   { printf 'end\n' >&7; exec 7>&-; wait "$GAME_PID"; }
game_close() { exec 7>&-; wait "$GAME_PID"; }

pi_is ondemand 52000 0
game_start 'start full-speed' > "$T/answer"
check "game: full speed asked, performance" "ok performance" "$(cat "$T/answer")"
check "game: the governor at performance during it" performance "$(governor)"
check "game: the flag up during it" up "$(playing)"
check "game: the governor before it kept" "policy0 ondemand" "$(cat "$T/run/osdos-game/governors")"
game_end
check "game: its end answered is no failure" 0 "$?"
check "game: the governor put back at its end" ondemand "$(governor)"
check "game: the flag down at its end" down "$(playing)"

game_start 'start full-speed network' > "$T/answer"
check "game: a network game, performance" "ok performance" "$(cat "$T/answer")"
game_close
check "game: the app's connection closed, the governor put back" ondemand "$(governor)"
check "game: ...and the flag down" down "$(playing)"

game_start 'start as-is' > "$T/answer"
check "game: full speed turned off in Settings" "ok kept ondemand, as Settings asks" "$(cat "$T/answer")"
check "game: ...the governor as it was" ondemand "$(governor)"
check "game: ...the flag up all the same" up "$(playing)"
game_end
check "game: ...and down after" down "$(playing)"

pi_is schedutil 78000 0
game_start 'start full-speed' > "$T/answer"
check "game: a Pi already hot keeps its governor" "ok kept schedutil, as the Pi was at 78.0 °C at the start" \
      "$(cat "$T/answer")"
check "game: ...which stays as it was" schedutil "$(governor)"
game_end
check "game: ...and is said in the journal" \
      "osdos-stop game: the governor kept at schedutil, as the Pi was at 78.0 °C at the start" "$(head -n1 "$T/stderr")"
pi_is ondemand 50000 50005
game_start 'start full-speed' > "$T/answer"
check "game: a Pi throttled now keeps its governor" "ok kept ondemand, as the Pi was throttled at the start (0x50005)" \
      "$(cat "$T/answer")"
check "game: ...which stays as it was" ondemand "$(governor)"
game_end
pi_is ondemand 50000 50000
game_start 'start full-speed' > "$T/answer"
check "game: throttled earlier only, performance" "ok performance" "$(cat "$T/answer")"
game_end
check "game: ...put back" ondemand "$(governor)"

# A helper stopped mid-game (the system going down) puts it back too.
pi_is ondemand 50000 0
game_start 'start full-speed' > /dev/null
kill -TERM "$GAME_PID"
wait "$GAME_PID"
exec 7>&-
check "game: its helper stopped, the governor put back" ondemand "$(governor)"
check "game: ...and the flag down" down "$(playing)"
# One killed outright leaves it to the app's service: osdos.service's
# ExecStopPost (osdos-stop with the app's exit) puts it back.
game_start 'start full-speed' > /dev/null
kill -KILL "$GAME_PID"
wait "$GAME_PID" 2> /dev/null
exec 7>&-
check "game: its helper killed, performance stays meanwhile" performance "$(governor)"
on_exit 143
check "game: the app's service stopped, the governor put back" ondemand "$(governor)"
check "game: ...and the flag down" down "$(playing)"
check "game: ...and the Pi left on" "" "$(asked)"
# And what such a game left is put back before the next is kept.
mkdir -p "$T/run/osdos-game"
echo "policy0 schedutil" > "$T/run/osdos-game/governors"
pi_is performance 50000 0
game_start 'start full-speed' > "$T/answer"
check "game: one left from before put back first" "ok performance" "$(cat "$T/answer")"
check "game: ...the governor from before it kept" "policy0 schedutil" "$(cat "$T/run/osdos-game/governors")"
game_end
check "game: ...and put back" schedutil "$(governor)"

# Only its own words: anything else changes nothing.
pi_is ondemand 50000 0
for bad in 'start' 'start fast' 'start full-speed wifi' 'start full-speed network;reboot' 'stop' \
           'start ../governor' 'end' ' '; do
    game_start "$bad" > "$T/answer"
    check "game: '$bad' is refused" "nothing this does" "$(cat "$T/answer")"
    game_close
    check "game: '$bad': the governor as it was" ondemand "$(governor)"
    check "game: '$bad': no flag" down "$(playing)"
done
check "game: nothing sent" "nothing asked" "$(PATH="$T/bin:$PATH" "$T/osdos-stop" game < /dev/null 2> /dev/null)"

# What the Pi did during a game, said at its end: the governor, the clock,
# the throttling bits the game saw or set, the highest temperature, and in a
# network game the route's interface, and on Wi-Fi its band, its signal at
# the start and the end and the radio's scans and roams (stand-ins for ip
# and iw: the route, the link and the radio's events as they print them).
told()   { sed -n 2p "$T/game-out"; }
journal() { tail -n1 "$T/stderr"; }
echo 1800000 > "$T/cpufreq/policy0/scaling_cur_freq"
mkdir -p "$T/net/eth0" "$T/net/wlan0/wireless" "$T/module/brcmfmac/parameters"
echo 1 > "$T/module/brcmfmac/parameters/roamoff"
printf '#!/bin/sh\ncat "%s/route"\n' "$T" > "$T/bin/ip"
# iw dev wlan0 link: the link as iw prints it; iw event -t: the radio's
# events, then on until it is stopped.
cat > "$T/bin/iw" << EOF
#!/bin/sh
case "\$1" in
    dev) cat "$T/iw-link" ;;
    event) cat "$T/iw-events"; exec sleep 60 ;;
esac
EOF
chmod +x "$T/bin/ip" "$T/bin/iw"
link() {  # frequency, signal
    printf 'Connected to 02:00:00:00:00:01 (on wlan0)\n\tSSID: home\n\tfreq: %s\n\tRX: 1 bytes (1 packets)\n\tsignal: %s dBm\n' \
        "$1" "$2" > "$T/iw-link"
}

pi_is ondemand 52000 0
game_start 'start full-speed' > /dev/null
echo 63400 > "$T/thermal/thermal_zone0/temp"
sleep 2.5
echo 1500000 > "$T/cpufreq/policy0/scaling_cur_freq"
echo 58000 > "$T/thermal/thermal_zone0/temp"
game_end
check "game: its end says what the Pi did" \
      "notice=none governor performance, clock 1500-1800 MHz, throttling none, highest 63.4 °C" "$(told)"
check "game: ...and so does the journal" \
      "osdos-stop game: the game ended, the system as it was: governor performance, clock 1500-1800 MHz, throttling none, highest 63.4 °C" \
      "$(journal)"
echo 1800000 > "$T/cpufreq/policy0/scaling_cur_freq"

# Throttled or short of power since the game began: by the firmware's bits
# since boot that the game set.
pi_is ondemand 50000 0
game_start 'start full-speed' > /dev/null
echo 50000 > "$T/platform/soc/soc:firmware/get_throttled"
game_end
check "game: short of power and throttled during it" \
      "notice=power,throttled governor performance, clock 1800 MHz, throttling 0x5 (under-voltage, throttled), highest 50.0 °C" \
      "$(told)"
# Set before the game: not the game's.
game_start 'start full-speed' > /dev/null
game_end
check "game: what was there before it isn't the game's" "notice=none" "$(told | cut -d' ' -f1)"
pi_is ondemand 50000 0
game_start 'start full-speed' > /dev/null
echo 20002 > "$T/platform/soc/soc:firmware/get_throttled"
game_end
check "game: the clock capped is throttling" "notice=throttled" "$(told | cut -d' ' -f1)"
pi_is ondemand 50000 0
game_start 'start as-is' > /dev/null
echo 10001 > "$T/platform/soc/soc:firmware/get_throttled"
game_end
check "game: short of power only" \
      "notice=power governor ondemand, clock 1800 MHz, throttling 0x1 (under-voltage), highest 50.0 °C" "$(told)"

# A network game over Wi-Fi.
pi_is ondemand 50000 0
echo "default via 192.168.1.1 dev wlan0 proto dhcp src 192.168.1.20 metric 600" > "$T/route"
link 5180.0 -58
printf '%s\n' "1700000000.100000: wlan0 (phy #0): scan started" \
    "1700000000.200000: wlan0 (phy #0): scan finished: 5180 5200, \"\"" \
    "1700000030.100000: wlan0 (phy #0): scan started" \
    "1700000031.100000: wlan0 (phy #0): roamed to 02:00:00:00:00:02" \
    "1700000040.100000: wlan1 (phy #1): scan started" > "$T/iw-events"
game_start 'start full-speed network' > /dev/null
sleep 0.3
link 5180.0 -61
game_end
check "game: a network game over Wi-Fi: its band, signal, scans and roams" \
      "notice=none governor performance, clock 1800 MHz, throttling none, highest 50.0 °C, Wi-Fi wlan0 5 GHz (5180 MHz), signal -58 dBm at the start, -61 dBm at the end, 2 scans, 1 roams, roamoff 1" \
      "$(told)"
link 2437 -70
: > "$T/iw-events"
game_start 'start full-speed network' > /dev/null
: > "$T/iw-link"
game_end
check "game: on 2.4 GHz, and gone at the end" \
      "Wi-Fi wlan0 2.4 GHz (2437 MHz), signal -70 dBm at the start, not connected at the end, 0 scans, 0 roams, roamoff 1" \
      "$(told | sed 's/.*°C, //')"
# Over a cable, or without a route, there is no Wi-Fi to count.
echo "default via 192.168.1.1 dev eth0 proto dhcp src 192.168.1.21 metric 100" > "$T/route"
game_start 'start full-speed network' > /dev/null
game_end
check "game: a network game over a cable" "network over eth0" "$(told | sed 's/.*°C, //')"
: > "$T/route"
game_start 'start full-speed network' > /dev/null
game_end
check "game: a network game without a route" "network: no default route" "$(told | sed 's/.*°C, //')"
# A game alone says nothing of the network.
game_start 'start full-speed' > /dev/null
game_end
check "game: a game alone says nothing of the network" "highest 50.0 °C" "$(told | sed 's/.*, //')"
# The connection closed (the app gone): the journal has it, nobody else.
game_start 'start full-speed' > /dev/null
game_close
check "game: closed, nothing more on the connection" "" "$(told)"
check "game: ...the journal has it" \
      "osdos-stop game: the game ended, the system as it was: governor performance, clock 1800 MHz, throttling none, highest 50.0 °C" \
      "$(journal)"

# The yt-dlp update waits on the flag the helper keeps, by its path.
flag=$(sed -n 's/^GAME_FLAG=//p' "$INSTALL_SH")
check "game: the yt-dlp update skips its run while the flag is up" \
      "ExecCondition=/usr/bin/test ! -e $flag" \
      "$(grep '^ExecCondition=/usr/bin/test' "$PRESETS/../../06-youtube/files/osdos-yt-dlp-update.service")"

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
