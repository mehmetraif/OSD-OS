#!/usr/bin/env bash
# The installer's choice of the user OSD/OS runs as (scripts/install.sh, its
# "Choose install owner" block), run with the script's own shell options
# against a stand-in service file and image marker, its questions answered on
# standard input: no service yet, one installed before (its user offered, and
# kept when it isn't installed again), and the OSD/OS image's own, which asks
# nothing.
#   installer_test.sh <scripts/install.sh>
set -u

INSTALL_SH="$1"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

{
    echo 'set -euo pipefail'
    echo "SYSTEMD_SERVICE=\"$T/osdos.service\""
    awk '/^# ── Choose install owner/ { on = 1 }
         on                          { print }
         on && /^OWNER_USER=/        { exit }' "$INSTALL_SH" | sed "s#/etc/osdos/boot-units#$T/boot-units#g"
    echo 'echo "${AUTOSTART_REPLY}|${SERVICE_USER}|${OWNER_USER}|${IMAGE_SERVICE}"'
} > "$T/owner.sh"
grep -q "$T/boot-units" "$T/owner.sh" && grep -q '^OWNER_USER=' "$T/owner.sh" \
    || { echo "FAIL: the owner's block not found in $INSTALL_SH"; exit 1; }

failures=0
check() {  # what, expected, actual
    if [ "$2" != "$3" ]; then
        echo "FAIL: $1: expected '$2', got '$3'"
        failures=$((failures + 1))
    else
        echo "PASS: $1"
    fi
}

# The block with these answers, run by the user "me": its last line
# (answer|service user|owner|image), or how it exited.
choose() {  # answers, as printf's %b takes them
    local out
    out=$(printf '%b' "$1" | USER=me bash "$T/owner.sh" 2>&1) || { echo "exited $?"; return; }
    echo "${out##*$'\n'}"
}

check "no service yet, autostart declined: the files go to the user installing" "n||me|" "$(choose 'n\n')"
check "no service yet, autostart chosen: pi offered" "y|pi|pi|" "$(choose 'y\n\n')"
check "no service yet, autostart chosen with a user" "y|kiosk|kiosk|" "$(choose 'y\nkiosk\n')"

printf '[Service]\nType=simple\nUser=arcade\n' > "$T/osdos.service"
check "a service installed before, chosen again: its user offered" "y|arcade|arcade|" "$(choose 'y\n\n')"
check "a service installed before, declined: its user kept" "n|arcade|arcade|" "$(choose 'n\n')"

touch "$T/boot-units"
check "the image's own service: nothing asked, its user kept" "y|arcade|arcade|1" "$(choose '')"
rm -f "$T/osdos.service"
check "the image's marker without its service: asked" "n||me|" "$(choose 'n\n')"

if [ "$failures" -gt 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
