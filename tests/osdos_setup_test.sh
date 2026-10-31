#!/usr/bin/env bash
# OSD/OS Setup's file applied as the card starts (osdos-setup), on a stand-in
# card: a boot partition with the image's presets, /etc, /var/lib and the
# app's user's home under a folder of its own (--root). The two stages run as
# the image runs them, the app's (osdos-setup-app.service) and then the
# system's (osdos-setup.service). The image's own osdos-stop, taken from
# scripts/install.sh as os/build.sh takes it, names the device, sets the
# Wi-Fi country and writes the display output, as the app has it do;
# hostnamectl, raspi-config, systemctl, chpasswd and timedatectl are
# stand-ins that only note what they were asked. A whole file: off the boot
# partition at the first stage, root's alone until the second has applied
# it, nothing of its passwords in the log; the same again, changing nothing
# more; files it takes only part of, or none of; and a start cut short.
# Every name and password is an invented one.
#   osdos_setup_test.sh <osdos-setup> <scripts/install.sh> <os/stage-osdos/03-boot/files>
set -u

SETUP="$1"
INSTALL_SH="$2"
PRESETS="$3"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
BOOT="$T/boot/firmware"
HOME_PI="$T/home/pi"
DATA="$HOME_PI/.local/share/OSD-OS"
KEEP="$T/var/lib/osdos-setup"
PENDING="$KEEP/pending.json"
SSHD_CONF="$T/etc/ssh/sshd_config.d/00-osdos-setup.conf"
NM_STATE="$T/var/lib/NetworkManager/NetworkManager.state"
mkdir -p "$BOOT" "$T/etc/modprobe.d" "$T/etc/NetworkManager/system-connections" "$T/etc/ssh/sshd_config.d" \
         "$T/run" "$T/var/lib/NetworkManager" "$T/bin" "$T/proc/device-tree" "$T/usr/share/zoneinfo/Europe" \
         "$T/usr/share/X11/xkb/symbols" "$DATA"
# Wi-Fi off, as pi-gen leaves it on an image built without a country.
printf '[main]\nNetworkingEnabled=true\nWirelessEnabled=false\n' > "$NM_STATE"

awk -v marker=STOP_HELPER '
    $0 ~ "<< .?" marker ".?$" { on = 1; next }
    on && $0 == marker        { exit }
    on                        { print }
' "$INSTALL_SH" | sed -e "s#/boot/firmware#$BOOT#g" \
                      -e "s#/etc/modprobe.d#$T/etc/modprobe.d#g" \
                      -e "s#/run/osdos-display-boot.txt#$T/run/osdos-display-boot.txt#g" \
                      -e "s#/usr/share/zoneinfo/iso3166.tab#$T/usr/share/zoneinfo/iso3166.tab#g" \
                      -e "s#/etc/hosts#$T/etc/hosts#g" -e "s#/etc/hostname#$T/etc/hostname#g" > "$T/bin/osdos-stop"
grep -q "$T/run/osdos-display-boot.txt" "$T/bin/osdos-stop" || { echo "FAIL: osdos-stop not found in $INSTALL_SH"; exit 1; }
for tool in systemctl raspi-config timedatectl; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/asked"\n' "$tool" "$T" > "$T/bin/$tool"
done
# chpasswd notes what it reads; hostnamectl writes /etc/hostname, as
# hostnamed does.
printf '#!/bin/sh\necho "chpasswd $* $(cat)" >> "%s/asked"\n' "$T" > "$T/bin/chpasswd"
printf '#!/bin/sh\necho "hostnamectl $*" >> "%s/asked"\necho "$2" > "%s/etc/hostname"\n' "$T" "$T" > "$T/bin/hostnamectl"
chmod +x "$T"/bin/*
cp "$PRESETS"/osdos-display-*.txt "$BOOT/"
cp "$BOOT/osdos-display-hdmi.txt" "$BOOT/osdos-display.txt"
printf '%s\n' '#code	name' $'DE\tGermany' $'TR\tTurkey' > "$T/usr/share/zoneinfo/iso3166.tab"
: > "$T/usr/share/zoneinfo/Europe/Istanbul"
: > "$T/usr/share/X11/xkb/symbols/tr"
printf 'osdos\n' > "$T/etc/hostname"
printf '127.0.0.1\tlocalhost\n127.0.1.1\tosdos\n' > "$T/etc/hosts"
printf 'root:x:0:0:root:/root:/bin/bash\npi:x:%s:%s:,,,:/home/pi:/bin/bash\n' "$(id -u)" "$(id -g)" > "$T/etc/passwd"
printf 'Raspberry Pi 5 Model B Rev 1.0\0' > "$T/proc/device-tree/model"
# What the app has kept so far: a setting the file doesn't name stays.
printf '{\n    "app": { "theme": "", "hint_bar": "On" },\n    "modules": { "com.osdos.plex": { "enabled": true } }\n}\n' \
    > "$DATA/config.json"

failures=0
check() {  # what, expected, actual
    if [ "$2" != "$3" ]; then
        echo "FAIL: $1: expected '$2', got '$3'"
        failures=$((failures + 1))
    else
        echo "PASS: $1"
    fi
}
# One stage, its exit status echoed; never longer than a minute, whatever it
# reads.
stage() {
    PATH="$T/bin:$PATH" timeout 60 python3 -I "$SETUP" --stage "$1" --user pi --root "$T" \
        < /dev/null >> "$T/out" 2>> "$T/err"
    echo $?
}
# As the card starts: the app's stage, then the system's, their exit
# statuses in $status.
apply() { : > "$T/asked"; : > "$T/out"; : > "$T/err"; status="$(stage app) $(stage system)"; }
asked() { grep -c -F -x -- "$1" "$T/asked"; }
logged() { grep -c -F -x -- "$1" "$BOOT/osdos-setup.log"; }
there() { [ -e "$1" ] && echo yes || echo no; }
mode() { stat -c %a "$1"; }
json() { python3 -I -c 'import json, sys; d = json.load(open(sys.argv[1]))
for k in sys.argv[2].split("/"): d = d[k]
print(json.dumps(d))' "$1" "$2"; }
keys() { python3 -I -c 'import json, sys; print(" ".join(sorted(json.load(open(sys.argv[1]))["setup"])))' "$PENDING"; }
# An invented key, whole: its blob an ed25519 key's, of 32 made-up bytes.
KEY="ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4f den@laptop"
HASH='$6$saltsaltsaltsalt$Hh4ICNsl2uVf0JMkXLtqU8IyVRbF2hqBu7MVgRQ6DbkDCWV2s4IT02qjv5o7Fj3oYKY7W92YLd99uJNEpXNKt0'

# Nothing on the card: nothing done, no log.
apply
check "no file: nothing asked" "" "$(cat "$T/asked")"
check "no file: no log" "no" "$(there "$BOOT/osdos-setup.log")"
check "no file: both stages content" "0 0" "$status"

# The whole of it, as the page makes it, for this Pi 5.
cat > "$BOOT/osdos-setup.json" << EOF
{
  "osdos_setup": 1,
  "device": { "name": "den-tv", "model": "pi5", "kind": "arcade" },
  "display": { "preset": "scart-rgb-pal" },
  "wifi": { "country": "TR", "ssid": "Home 5G", "password": "correct battery\\\\x", "hidden": true },
  "system": { "password_hash": "$HASH",
              "ssh": { "enabled": true, "password_login": false, "authorized_keys": ["$KEY"] },
              "timezone": "Europe/Istanbul", "keyboard": "tr" },
  "retroachievements": { "user": "denplayer", "password": "hunter22hunter", "key": "0123456789abcdefABCDEF0123456789" },
  "config": { "app": { "osd_theme": "trinitron", "hint_bar": "Off" },
              "modules": { "com.osdos.arcade": { "enabled": true, "online_play": "On" } },
              "window": { "x": 1 } }
}
EOF
: > "$T/asked"
check "app stage: content" "0" "$(stage app)"
# Before the app starts: the file off the boot partition, OSD/OS's own
# settings set, nothing else yet.
check "app stage: the file off the boot partition" "no" "$(there "$BOOT/osdos-setup.json")"
check "app stage: the rest root's alone" "700 600" "$(mode "$KEEP") $(mode "$PENDING")"
check "app stage: OSD/OS's own taken out of it" "device display osdos_setup system wifi" "$(keys)"
check "app stage: OSD/OS's settings merged" '{"theme": "", "hint_bar": "Off", "osd_theme": "trinitron"}' \
    "$(json "$DATA/config.json" app)"
check "app stage: nothing else asked" "" "$(cat "$T/asked")"
check "app stage: the device not named yet" "osdos" "$(cat "$T/etc/hostname")"
check "app stage: no log yet" "no" "$(there "$BOOT/osdos-setup.log")"
check "system stage: content" "0" "$(stage system)"
check "the rest gone with its passwords" "no" "$(there "$PENDING")"
check "the device named" "den-tv" "$(cat "$T/etc/hostname")"
check "and /etc/hosts with it" "127.0.1.1	den-tv" "$(sed -n 2p "$T/etc/hosts")"
check "the Wi-Fi country through raspi-config" "1" "$(asked "raspi-config nonint do_wifi_country TR")"
wifi="$(ls "$T/etc/NetworkManager/system-connections")"
check "one connection for the network" "1" "$(echo "$wifi" | grep -c '^osdos-setup-Home-5G-[0-9a-f]\{8\}\.nmconnection$')"
conn="$T/etc/NetworkManager/system-connections/$wifi"
check "the connection only root's" "600" "$(mode "$conn")"
check "its network's name" "ssid=Home\\s5G" "$(grep '^ssid=' "$conn")"
check "hidden" "hidden=true" "$(grep '^hidden=' "$conn")"
check "without power saving" "powersave=2" "$(grep '^powersave=' "$conn")"
check "its password, as GLib keeps a string" "psk=correct\\sbattery\\\\x" "$(grep '^psk=' "$conn")"
check "WPA" "key-mgmt=wpa-psk" "$(grep '^key-mgmt=' "$conn")"
check "Wi-Fi switched on for NetworkManager, with its country" "[main] NetworkingEnabled=true WirelessEnabled=true" \
    "$(tr '\n' ' ' < "$NM_STATE" | sed 's/ $//')"
check "pi's password through chpasswd" "1" "$(asked "chpasswd -e pi:$HASH")"
check "SSH turned on" "1" "$(asked "systemctl enable ssh.service")"
check "and started without waiting" "1" "$(asked "systemctl start --no-block ssh.service")"
check "keys only, in sshd's first drop-in" "PasswordAuthentication no" "$(grep '^PasswordAuthentication' "$SSHD_CONF")"
check "the key for pi" "$KEY" "$(cat "$HOME_PI/.ssh/authorized_keys")"
check "~/.ssh pi's alone" "700 600" "$(mode "$HOME_PI/.ssh") $(mode "$HOME_PI/.ssh/authorized_keys")"
check "the time zone" "1" "$(asked "timedatectl set-timezone Europe/Istanbul")"
check "the keyboard" "1" "$(asked "raspi-config nonint do_configure_keyboard tr")"
check "a module's beside the others" '{"enabled": true, "online_play": "On"}' "$(json "$DATA/config.json" modules/com.osdos.arcade)"
check "and those it names not touched" '{"enabled": true}' "$(json "$DATA/config.json" modules/com.osdos.plex)"
check "nothing but app and modules" "no" "$(grep -q window "$DATA/config.json" && echo yes || echo no)"
ra="$DATA/arcade/retroachievements-setup.json"
check "the RetroAchievements account for the app" '"denplayer" "hunter22hunter" "0123456789abcdefABCDEF0123456789"' \
    "$(json "$ra" user) $(json "$ra" password) $(json "$ra" key)"
check "the account pi's alone" "600" "$(mode "$ra")"
check "the display output written" "$(cat "$PRESETS/osdos-display-scart-rgb-pal.txt")" "$(cat "$BOOT/osdos-display.txt")"
check "the Pi 5's composite sync" "options drm_rp1_dpi force_csync=1" "$(cat "$T/etc/modprobe.d/osdos-display.conf")"
check "and a restart for it" "1" "$(asked "systemctl --no-block reboot")"
check "said in the log" "1" "$(logged "Display output: scart-rgb-pal, from the restart that follows")"
for line in "Device name: den-tv" "Wi-Fi country: TR" "Wi-Fi: Home 5G (hidden), joined as the network starts" \
            "pi's password: set" "SSH: on, keys only" "SSH keys: 1 added" "Time zone: Europe/Istanbul" "Keyboard: tr" \
            "OSD/OS settings: 4 set in config.json" \
            "RetroAchievements: denplayer, signed in as OSD/OS starts; Web API key kept"; do
    check "logged: $line" "1" "$(logged "$line")"
done
check "the app's stage first in the log" "OSD/OS settings: 4 set in config.json" "$(sed -n 2p "$BOOT/osdos-setup.log")"
check "no password in the log" "0" "$(grep -c -e 'correct battery' -e hunter22 -e saltsalt "$BOOT/osdos-setup.log")"
check "nor in the journal" "0" "$(cat "$T/out" "$T/err" | grep -c -e 'correct battery' -e hunter22 -e saltsalt)"
check "nor left anywhere of root's" "" "$(grep -r -l -e 'correct battery' -e hunter22 -e saltsalt "$T/var" "$BOOT")"

# The same again without a password: the key not twice, nothing restarted,
# and password login not on over a password the card had (one every card of
# an image built with a password shares).
cp "$BOOT/osdos-display-scart-rgb-pal.txt" "$T/scart"
cat > "$BOOT/osdos-setup.json" << EOF
{ "osdos_setup": 1, "device": { "model": "pi5" }, "display": { "preset": "scart-rgb-pal" },
  "system": { "ssh": { "enabled": true, "authorized_keys": ["$KEY"] } } }
EOF
apply
check "again: the key once" "1" "$(grep -c -F -x "$KEY" "$HOME_PI/.ssh/authorized_keys")"
check "again: said so" "1" "$(logged "SSH keys: 0 added, 1 there already")"
check "again: keys only, without a password set" "PasswordAuthentication no" "$(grep '^PasswordAuthentication' "$SSHD_CONF")"
check "again: said why" "1" "$(logged "SSH: on, keys only: password login needs pi's password, set in the same file")"
check "again: the output as it was, no restart" "0" "$(asked "systemctl --no-block reboot")"
check "again: said so" "1" "$(logged "Display output: scart-rgb-pal")"
check "again: what wasn't as asked fails the system stage" "0 1" "$status"

# Password login with the password set in the same file: on.
printf '{ "osdos_setup": 1, "system": { "password_hash": "%s", "ssh": { "enabled": true } } }\n' "$HASH" \
    > "$BOOT/osdos-setup.json"
apply
check "password login: on, ahead of any other drop-in" "PasswordAuthentication yes" \
    "$(grep '^PasswordAuthentication' "$SSHD_CONF")"
check "password login: said so" "1" "$(logged "SSH: on, password login on")"
check "password login: content" "0 0" "$status"

# Without a password or a key: SSH left as it was.
rm -rf "$HOME_PI/.ssh"
printf '{ "osdos_setup": 1, "system": { "ssh": { "enabled": true } } }\n' > "$BOOT/osdos-setup.json"
apply
check "no password, no key: SSH not turned on" "0" "$(asked "systemctl enable ssh.service")"
check "no password, no key: said why" "1" \
    "$(logged "SSH: not turned on: password login needs pi's password, set in the same file, or else a key")"

# The page's model left at another Pi's: what counts is the outputs this one
# has.
printf '{ "osdos_setup": 1, "device": { "model": "pi4" }, "display": { "preset": "crt-gpio-pal" } }\n' > "$BOOT/osdos-setup.json"
apply
check "the Pi's own outputs: written" "$(cat "$PRESETS/osdos-display-crt-gpio-pal.txt")" "$(cat "$BOOT/osdos-display.txt")"
check "the Pi's own outputs: content" "0 0" "$status"
cp "$BOOT/osdos-display.txt" "$T/gpio"

# An output a Pi 4 hasn't, on a Pi 4: left alone.
printf 'Raspberry Pi 4 Model B Rev 1.5\0' > "$T/proc/device-tree/model"
printf '{ "osdos_setup": 1, "device": { "model": "pi5" }, "display": { "preset": "crt-gpio-ntsc" } }\n' > "$BOOT/osdos-setup.json"
apply
check "another Pi's output not written" "$(cat "$T/gpio")" "$(cat "$BOOT/osdos-display.txt")"
check "and said why" "1" "$(logged "Display output: not changed: crt-gpio-ntsc isn't an output of this Raspberry Pi 4")"
check "another Pi's: the system stage fails" "0 1" "$status"

# What can't be taken is said, and the rest goes ahead.
printf 'not json' > "$DATA/config.json"
cat > "$BOOT/osdos-setup.json" << 'EOF'
{ "osdos_setup": 1,
  "device": { "name": "-den" },
  "display": { "preset": "../../etc/passwd" },
  "wifi": { "country": "ZZ", "ssid": "Café ;Net", "password": "short" },
  "system": { "password_hash": "plain", "ssh": { "enabled": true, "password_login": false, "authorized_keys": ["not a key"] },
              "timezone": "Europe/../../etc", "keyboard": "zz" },
  "retroachievements": { "user": "denplayer" },
  "config": { "app": { "hint_bar": "On" } } }
EOF
apply
for line in "Device name: not set: not a name the network takes" "Wi-Fi country: not set: no such country" \
            "Wi-Fi: not saved: a Wi-Fi password is 8 to 63 plain ASCII characters" \
            "pi's password: not set: not a password hash" "SSH keys: 1 not taken: not a public key" \
            "SSH: not turned on: no key to log in with" \
            "Time zone: not set: not a time zone" "Keyboard: not set: not a keyboard layout" \
            "OSD/OS settings: not set: config.json couldn't be read, and is left as it is" \
            "Display output: not changed: no such display output"; do
    check "logged: $line" "1" "$(logged "$line")"
done
check "config.json left as it was" "not json" "$(cat "$DATA/config.json")"
check "no account without its password" '"hunter22hunter"' "$(json "$ra" password)"
check "no command for what was refused" "" "$(cat "$T/asked")"
check "both stages fail" "1 1" "$status"
printf '{}\n' > "$DATA/config.json"

# A network's name of other letters, kept as its bytes; a semicolon escaped.
cat > "$BOOT/osdos-setup.json" << 'EOF'
{ "osdos_setup": 1, "wifi": { "ssid": "Çay Evi", "password": "" } }
EOF
apply
conn="$(grep -l '^id=Çay\\sEvi$' "$T"/etc/NetworkManager/system-connections/*)"
check "its bytes" "ssid=195;135;97;121;32;69;118;105;" "$(grep '^ssid=' "$conn")"
check "open: no security" "0" "$(grep -c 'wifi-security' "$conn")"
printf '{ "osdos_setup": 1, "wifi": { "ssid": "a;b", "password": "" } }\n' > "$BOOT/osdos-setup.json"
apply
check "a semicolon escaped" "1" "$(grep -c -F -x 'ssid=a\\;b' "$T"/etc/NetworkManager/system-connections/osdos-setup-a-b-*)"

# A network without a country, Wi-Fi off: saved, and said why it won't join.
printf '[main]\nWirelessEnabled=false\n' > "$NM_STATE"
printf '{ "osdos_setup": 1, "wifi": { "ssid": "Attic", "password": "" } }\n' > "$BOOT/osdos-setup.json"
apply
check "no country: said so" "1" "$(logged "Wi-Fi: Attic saved, but Wi-Fi stays off until its country is set")"
check "no country: the system stage fails" "0 1" "$status"
check "no country: Wi-Fi left off" "WirelessEnabled=false" "$(grep WirelessEnabled "$NM_STATE")"

# A key cut short (its blob not a key's whole): not taken.
printf '{ "osdos_setup": 1, "system": { "ssh": { "enabled": false, "authorized_keys": ["%s"] } } }\n' \
    "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAABAgMEBQYHCAkKCwwNDg8Q den@laptop" > "$BOOT/osdos-setup.json"
apply
check "a key cut short: not taken" "1" "$(logged "SSH keys: 1 not taken: not a public key")"

# A key whose comment hides a second line from sshd (a vertical tab, which
# Python's splitlines() would split on): not taken.
mkdir -p "$HOME_PI/.ssh"
printf '%s\n' "$KEY" > "$HOME_PI/.ssh/authorized_keys"
printf '{ "osdos_setup": 1, "system": { "ssh": { "enabled": false, "authorized_keys": ["%s"] } } }\n' \
    "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIHelperKey helper@x\\u000bcommand=\\\"/bin/sh\\\" ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAISmuggled" \
    > "$BOOT/osdos-setup.json"
apply
check "a key with a line in it: not taken" "1" "$(logged "SSH keys: 1 not taken: not a public key")"
check "authorized_keys as it was" "$KEY" "$(cat "$HOME_PI/.ssh/authorized_keys")"

# authorized_keys a pipe: read by pi's own process, which doesn't wait on it.
rm -f "$HOME_PI/.ssh/authorized_keys"
mkfifo "$HOME_PI/.ssh/authorized_keys"
printf '{ "osdos_setup": 1, "system": { "ssh": { "enabled": true, "password_login": false, "authorized_keys": ["%s"] } } }\n' \
    "$KEY" > "$BOOT/osdos-setup.json"
apply
check "a pipe: not waited on" "0 1" "$status"
check "a pipe: said so" "1" "$(logged "SSH keys: not added: not a file")"
check "a pipe: SSH not turned on" "1" "$(logged "SSH: not turned on: no key to log in with")"
rm -f "$HOME_PI/.ssh/authorized_keys"

# Files it can't read: deleted, passwords and all, and why said; the boot
# screen's SETTINGS then fails.
deep="$(python3 -I -c 'print("[" * 100000 + "]" * 100000)')"
while IFS='|' read -r bad why; do
    [ "$bad" = deep ] && bad="$deep"
    [ "$bad" = bom ] && bad=$'\xef\xbb\xbf[1]'
    printf '%s' "$bad" > "$BOOT/osdos-setup.json"
    apply
    what="${bad:0:24}"
    check "$what: deleted" "no" "$(there "$BOOT/osdos-setup.json")"
    check "$what: nothing done" "" "$(cat "$T/asked")"
    check "$what: both stages fail" "1 1" "$status"
    check "$what: nothing left of it" "no" "$(there "$PENDING")"
    check "$what: said why" "1" "$(logged "osdos-setup.json couldn't be read: $why: deleted")"
done << 'EOF'
not json|not a JSON file
[1, 2]|not an OSD/OS Setup file
{"osdos_setup": 2, "wifi": {"password": "hunter22hunter"}}|a file of version 2, which this OSD/OS doesn't know
{"osdos_setup": "1"}|not an OSD/OS Setup file
deep|not a JSON file
{"osdos_setup": 1, "config": {"app": {"hint_bar": NaN}}}|not a JSON file
{"osdos_setup": 1, "config": {"app": {"hint_bar": 1e400}}}|not a JSON file
bom|not an OSD/OS Setup file
EOF
check "no password of it kept" "" "$(grep -r -l hunter22 "$T/var" "$BOOT")"

# One a Windows editor saved, with a BOM: read all the same.
printf '\xef\xbb\xbf{ "osdos_setup": 1, "device": { "name": "bom-tv" } }\n' > "$BOOT/osdos-setup.json"
apply
check "a BOM: read" "bom-tv" "$(cat "$T/etc/hostname")"

# A start cut short after the app's stage: the next start's app stage sets
# nothing twice, and the system's says all of it once.
printf '{ "osdos_setup": 1, "device": { "name": "attic-tv" }, "config": { "app": { "hint_bar": "Off" } } }\n' \
    > "$BOOT/osdos-setup.json"
: > "$T/out"
check "cut short: app stage" "0" "$(stage app)"
printf '{"app": {"hint_bar": "On"}}\n' > "$DATA/config.json"
check "cut short: the next start's app stage" "0" "$(stage app)"
check "cut short: nothing set twice" '{"hint_bar": "On"}' "$(json "$DATA/config.json" app)"
check "cut short: the system stage" "0" "$(stage system)"
check "cut short: the device named" "attic-tv" "$(cat "$T/etc/hostname")"
check "cut short: said once" "1" "$(logged "OSD/OS settings: 1 set in config.json")"

# Cut short before the app's part was set: set at the next start, before the
# app.
mkdir -p "$KEEP"
printf '{"setup": {"config": {"app": {"hint_bar": "Off"}}}, "lines": [], "failed": false}\n' > "$PENDING"
apply
check "cut shorter: set at the next start" '{"hint_bar": "Off"}' "$(json "$DATA/config.json" app)"
check "cut shorter: content" "0 0" "$status"

# The system stage without the app's before it: OSD/OS's settings not set
# under the app, which would write its own over them.
printf '{"setup": {"config": {"app": {"hint_bar": "On"}}}, "lines": [], "failed": false}\n' > "$PENDING"
: > "$T/out"
check "no app stage: the system stage fails" "1" "$(stage system)"
check "no app stage: config.json not touched" '{"hint_bar": "Off"}' "$(json "$DATA/config.json" app)"
check "no app stage: said why" "1" "$(logged "OSD/OS: not set: osdos-setup-app.service didn't run before OSD/OS started")"

# config.json a link elsewhere: replaced, never written through.
printf '{"victim": true}\n' > "$T/victim.json"
ln -sf "$T/victim.json" "$DATA/config.json"
printf '{ "osdos_setup": 1, "config": { "app": { "hint_bar": "Off" } } }\n' > "$BOOT/osdos-setup.json"
apply
check "the link's file untouched" '{"victim": true}' "$(cat "$T/victim.json")"
check "config.json a file of its own" "no" "$([ -L "$DATA/config.json" ] && echo yes || echo no)"

# Both stages in one run, without --stage.
printf '{ "osdos_setup": 1, "device": { "name": "den-tv" } }\n' > "$BOOT/osdos-setup.json"
: > "$T/asked"
PATH="$T/bin:$PATH" python3 -I "$SETUP" --user pi --root "$T" > "$T/out" 2> "$T/err"
check "both at once: content" "0" "$?"
check "both at once: applied, nothing left" "den-tv no no" \
    "$(cat "$T/etc/hostname") $(there "$BOOT/osdos-setup.json") $(there "$PENDING")"

echo
if [ "$failures" -gt 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
echo "All checks passed"
