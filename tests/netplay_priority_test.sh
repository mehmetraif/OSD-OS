#!/usr/bin/env bash
# The image's marking of a network game's packets (osdos-netplay-priority:
# the rules nft reads and the unit that has it read them): the port marked
# is the one the Arcade module hosts LAN games on, only the private
# networks' addresses are marked, the unit reads the file where the stage
# puts it and the stage enables the unit; each interface answering only for
# its own address (osdos-arp.conf); and, where nft is here and may ask the
# kernel (root), the rules themselves are ones it takes.
#   netplay_priority_test.sh <os/stage-osdos/02-system> <src/modules/arcade/Libretro.h>
set -u

STAGE="$1"
LIBRETRO_H="$2"
RULES="$STAGE/files/osdos-netplay-priority.nft"
UNIT="$STAGE/files/osdos-netplay-priority.service"
fails=0
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }

port=$(sed -n 's/^constexpr quint16 kLanPort = \([0-9]*\);.*/\1/p' "$LIBRETRO_H")
[ -n "$port" ] || fail "kLanPort not found in $LIBRETRO_H"

# Both ways of the game's port, and no other port.
marked=$(grep -v '^[[:space:]]*#' "$RULES" | grep -c "ip dscp set cs4")
[ "$marked" = 2 ] || fail "two rules mark packets, not $marked"
grep -v '^[[:space:]]*#' "$RULES" | grep -q "tcp dport $port ip dscp set cs4" || fail "packets to port $port aren't marked"
grep -v '^[[:space:]]*#' "$RULES" | grep -q "tcp sport $port ip dscp set cs4" || fail "packets from port $port aren't marked"
# Only on the private networks: no rule without their addresses.
unbound=$(grep -v '^[[:space:]]*#' "$RULES" | grep "ip dscp set" \
    | grep -vc 'ip daddr { 10\.0\.0\.0/8, 172\.16\.0\.0/12, 192\.168\.0\.0/16 }')
[ "$unbound" = 0 ] || fail "a rule marks packets to addresses beyond the private networks"
# A table of its own, made anew: read twice, it is the same.
grep -q '^delete table inet osdos_netplay$' "$RULES" || fail "the table isn't made anew as the rules are read"

# The unit reads the file where the stage installs it, and the stage enables it.
path=$(sed -n 's/^ExecStart=\/usr\/sbin\/nft -f \(.*\)$/\1/p' "$UNIT")
[ "$path" = /usr/lib/osdos/netplay-priority.nft ] || fail "the unit reads '$path'"
# shellcheck disable=SC2016  # the stage's own ${LIB} and ${ETC}, as written
grep -q 'install -m 644 files/osdos-netplay-priority.nft "${LIB}/netplay-priority.nft"' "$STAGE/00-run.sh" \
    || fail "00-run.sh doesn't install the rules where the unit reads them"
# shellcheck disable=SC2016
grep -q 'install -m 644 files/osdos-netplay-priority.service "${ETC}/systemd/system/"' "$STAGE/00-run.sh" \
    || fail "00-run.sh doesn't install the unit"
grep -q 'osdos-netplay-priority.service' "$STAGE/01-run-chroot.sh" || fail "01-run-chroot.sh doesn't enable the unit"
grep -qx 'nftables' "$STAGE/00-packages-nr" || fail "the stage doesn't name nftables"

# With a cable and Wi-Fi both up, each interface answers only for its own
# address: the sysctl file, and the stage installing it where systemd-sysctl
# reads it.
ARP="$STAGE/files/osdos-arp.conf"
grep -qx 'net.ipv4.conf.all.arp_ignore = 1' "$ARP" || fail "arp_ignore isn't 1"
grep -qx 'net.ipv4.conf.all.arp_announce = 2' "$ARP" || fail "arp_announce isn't 2"
[ "$(grep -vc '^\(#.*\)\?$' "$ARP")" = 2 ] || fail "osdos-arp.conf sets more than the two"
# shellcheck disable=SC2016
grep -q 'install -D -m 644 files/osdos-arp.conf "${ETC}/sysctl.d/90-osdos-arp.conf"' "$STAGE/00-run.sh" \
    || fail "00-run.sh doesn't install osdos-arp.conf into sysctl.d"

# The rules themselves, where nft can be asked: a check that changes nothing.
if command -v nft > /dev/null 2>&1 && [ "$(id -u)" = 0 ] && nft list tables > /dev/null 2>&1; then
    out=$(nft -c -f "$RULES" 2>&1) || fail "nft doesn't take the rules: $out"
else
    echo "note: nft not here, or not root: the rules weren't given to nft"
fi

if [ "$fails" = 0 ]; then
    echo "PASS"
    exit 0
fi
exit 1
