#!/usr/bin/env bash
# The image's inbound filter (osdos-firewall: the rules nft reads and the
# unit that has it read them): an input chain that drops by default and
# takes only answers, the loopback, ICMP, DHCP's answers and the home
# network's addresses; no forward chain; a table of its own made anew as it
# is read; the unit loading it before the network, reading it where the
# stage puts it, and the stage enabling it; avahi made to answer by IPv4
# alone; LAN Play's sockets on IPv4, which the home addresses cover; and,
# where nft is here and may ask the kernel (root), the rules themselves are
# ones it takes, and loading them twice in a network namespace of the
# test's own leaves one table, which the unit's stop takes away.
#   firewall_test.sh <os/stage-osdos/02-system> <src/modules/arcade/LanRooms.cpp>
set -u

STAGE="$1"
LANROOMS="$2"
RULES="$STAGE/files/osdos-firewall.nft"
UNIT="$STAGE/files/osdos-firewall.service"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
fails=0
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }
# The rules without their comments and blank lines, each line trimmed.
rules() { sed -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' "$RULES" | sed '/^$/d'; }
has() { rules | grep -qxF -- "$1" || fail "no line '$1'"; }

# A table of its own, made anew in the same read.
[ "$(rules | head -n 2)" = $'table inet osdos_firewall\ndelete table inet osdos_firewall' ] \
    || fail "the table isn't made anew as the rules are read"
[ "$(rules | grep -c '^table ')" = 2 ] || fail "the rules make another table"
# One chain, the input's, dropping what nothing takes; nothing forwarded.
has "type filter hook input priority filter; policy drop;"
[ "$(rules | grep -c '^chain ')" = 1 ] || fail "a chain other than the input's"
rules | grep -q 'hook forward\|hook output\|hook prerouting' && fail "a hook other than input"
# What it takes, and nothing more.
has "ct state established,related accept"
has 'iif "lo" accept'
has "meta l4proto { icmp, ipv6-icmp } accept"
has "meta nfproto ipv4 udp sport 67 udp dport 68 accept"
has "meta nfproto ipv6 udp sport 547 udp dport 546 accept"
has "ip saddr @home4 accept"
has "ip6 saddr @home6 accept"
[ "$(rules | grep -c 'accept$')" = 7 ] || fail "$(rules | grep -c 'accept$') rules accept, not 7"
# The home network's kinds of address, those alone: no carrier-grade NAT's
# (100.64/10), no global ones.
has "elements = { 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 169.254.0.0/16 }"
has "elements = { fe80::/10, fc00::/7 }"
[ "$(rules | grep -c '^elements = ')" = 2 ] || fail "a set other than the two"
# The only interface it names is the loopback, which is there before any
# other: the rules load before the network's interfaces exist.
[ "$(rules | grep -c 'iif\|oif')" = 1 ] || fail "the rules name an interface other than the loopback"

# The unit: before the network, the file where the stage puts it, its own
# table taken away as it stops, the state the app reads, and nothing held
# up by it but the network.
grep -qx 'ExecStart=/usr/sbin/nft -f /usr/lib/osdos/firewall.nft' "$UNIT" || fail "the unit doesn't read /usr/lib/osdos/firewall.nft"
grep -qx 'ExecReload=/usr/sbin/nft -f /usr/lib/osdos/firewall.nft' "$UNIT" || fail "the unit isn't read again in place"
grep -qx 'ExecStop=-/usr/sbin/nft delete table inet osdos_firewall' "$UNIT" || fail "the unit's stop doesn't take its table away"
grep -qx 'Before=network-pre.target' "$UNIT" || fail "the unit isn't loaded before the network"
grep -qx 'Wants=network-pre.target' "$UNIT" || fail "the unit doesn't pull network-pre.target in"
grep -qx 'DefaultDependencies=no' "$UNIT" || fail "the unit waits for basic.target"
grep -qx 'ConditionPathExists=/usr/sbin/nft' "$UNIT" || fail "the unit runs without nft"
grep -qx 'RuntimeDirectory=osdos-firewall' "$UNIT" || fail "the unit leaves no /run/osdos-firewall while loaded"
grep -qx 'WantedBy=multi-user.target' "$UNIT" || fail "the unit isn't wanted by multi-user.target"
grep -q '^TimeoutStartSec=' "$UNIT" || fail "the unit may wait forever"
# OSD/OS never waits for it, nor it for OSD/OS.
grep -q 'wait-for-app\|osdos.service\|RequiredBy\|Requires=' "$UNIT" && fail "the unit is tied to the app"
# shellcheck disable=SC2016  # the stage's own ${LIB} and ${ETC}, as written
grep -q 'install -m 644 files/osdos-firewall.nft "${LIB}/firewall.nft"' "$STAGE/00-run.sh" \
    || fail "00-run.sh doesn't install the rules where the unit reads them"
# shellcheck disable=SC2016
grep -q 'install -m 644 files/osdos-firewall.service "${ETC}/systemd/system/"' "$STAGE/00-run.sh" \
    || fail "00-run.sh doesn't install the unit"
grep -q 'osdos-firewall.service' "$STAGE/01-run-chroot.sh" || fail "01-run-chroot.sh doesn't enable the unit"

# avahi by IPv4 alone: the stage's own lines run on Raspberry Pi OS's file,
# as Debian ships it (in short), change those two lines and no others.
mkdir -p "$T/etc/avahi"
printf '%s\n' '[server]' '#host-name=foo' 'use-ipv4=yes' 'use-ipv6=yes' 'ratelimit-burst=1000' '' \
    '[publish]' 'publish-hinfo=no' '#publish-aaaa-on-ipv4=yes' '#publish-a-on-ipv6=no' > "$T/etc/avahi/avahi-daemon.conf"
cp "$T/etc/avahi/avahi-daemon.conf" "$T/before.conf"
awk '/^AVAHI=/ { on = 1 } on { print } on && /^fi$/ { exit }' "$STAGE/00-run.sh" > "$T/avahi.sh"
grep -q 'use-ipv6=no' "$T/avahi.sh" || fail "00-run.sh's avahi lines not found"
ETC="$T/etc" bash -e "$T/avahi.sh" || fail "00-run.sh's avahi lines failed on Raspberry Pi OS's file"
[ "$(diff "$T/before.conf" "$T/etc/avahi/avahi-daemon.conf" | grep '^>')" = $'> use-ipv6=no\n> publish-aaaa-on-ipv4=no' ] \
    || fail "avahi's file isn't changed as it should be: $(diff "$T/before.conf" "$T/etc/avahi/avahi-daemon.conf")"
# A file with neither line stops the build rather than leave IPv6 on.
printf '[server]\nuse-ipv4=yes\n' > "$T/etc/avahi/avahi-daemon.conf"
ETC="$T/etc" bash -e "$T/avahi.sh" 2> /dev/null && fail "a file without the lines doesn't stop the build"
grep -qx 'avahi-daemon' "$STAGE/00-packages-nr" || fail "the stage doesn't name avahi-daemon"

# LAN Play's sockets are IPv4's (the room query and its answers, the seats'
# claims): the home network's IPv4 addresses cover every packet of it. One
# on IPv6 would need the filter looked at again.
grep -q 'QHostAddress::AnyIPv4' "$LANROOMS" || fail "LanRooms.cpp binds no IPv4 socket"
grep -q 'QHostAddress::AnyIPv6\|QHostAddress::Any)' "$LANROOMS" \
    && fail "LanRooms.cpp binds a socket on IPv6: see what osdos-firewall.nft lets in"

# The rules themselves, where nft can be asked: a check that changes nothing,
# then, in a network namespace of the test's own, read twice and taken away.
if command -v nft > /dev/null 2>&1 && [ "$(id -u)" = 0 ] && nft list tables > /dev/null 2>&1; then
    out=$(nft -c -f "$RULES" 2>&1) || fail "nft doesn't take the rules: $out"
    if command -v unshare > /dev/null 2>&1 && unshare -n true 2> /dev/null; then
        out=$(unshare -n sh -c 'nft -f "$1" && nft -f "$1" && nft list tables && nft delete table inet osdos_firewall && echo gone && nft list tables' \
              sh "$RULES" 2>&1)
        [ "$out" = $'table inet osdos_firewall\ngone' ] || fail "read twice and taken away: $out"
    else
        echo "note: no network namespace of the test's own: the rules weren't loaded"
    fi
else
    echo "note: nft not here, or not root: the rules weren't given to nft"
fi

if [ "$fails" = 0 ]; then
    echo "PASS"
    exit 0
fi
exit 1
