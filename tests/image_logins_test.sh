#!/usr/bin/env bash
# Who can log in to the OSD/OS image, and how (os/stage-osdos/02-system):
# sshd's own drop-in (files/osdos-sshd.conf, 90-osdos.conf) after OSD/OS
# Setup's (00-osdos-setup.conf) and cloud-init's (50-cloud-init.conf), with
# sshd's rule that the first value read of a setting is the one kept: no
# password logs in unless a card or Raspberry Pi Imager says so, or the image
# was built for it; and Raspberry Pi OS's boot-partition switches for SSH and
# a user masked. sshd
# itself isn't run: `sshd -T` on a Pi says what it takes.
#   image_logins_test.sh <os/stage-osdos/02-system>
set -u

STAGE="$1"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

failures=0
check() {  # what, expected, actual
    if [ "$2" != "$3" ]; then
        echo "FAIL: $1: expected '$2', got '$3'"
        failures=$((failures + 1))
    else
        echo "PASS: $1"
    fi
}

# --- sshd. The drop-ins' names, as the stage and the applier write them.
base_name=$(sed -n 's#^install -D -m 644 files/osdos-sshd.conf "${ETC}/ssh/sshd_config.d/\(.*\)"$#\1#p' "$STAGE/00-run.sh")
card_name=$(sed -n 's#^SSHD_CONF = "/etc/ssh/sshd_config.d/\(.*\)"$#\1#p' "$STAGE/files/osdos-setup")
check "the image's drop-in installed" "90-osdos.conf" "$base_name"
check "the card's drop-in" "00-osdos-setup.conf" "$card_name"
# sshd reads its Include's files in glob(3)'s order, by name.
check "read in this order: the card's, cloud-init's, the image's" \
      "$card_name 50-cloud-init.conf $base_name" \
      "$(printf '%s\n' "$base_name" 50-cloud-init.conf "$card_name" | LC_ALL=C sort | paste -sd' ')"

# The stage's choice of the image's PasswordAuthentication, run for each
# build: no, unless built with SSH on, a password and PUBKEY_ONLY_SSH not 1.
awk '/^password_authentication=no$/ { on = 1 } on { print } on && /^fi$/ { exit }' "$STAGE/00-run.sh" > "$T/choice.sh"
echo 'echo "$password_authentication"' >> "$T/choice.sh"
grep -q 'ENABLE_SSH' "$T/choice.sh" || { echo "FAIL: the choice of password logins not found in $STAGE/00-run.sh"; exit 1; }
built() { env -i ENABLE_SSH="$1" OSDOS_LOCK_FIRST_USER="$2" PUBKEY_ONLY_SSH="$3" bash "$T/choice.sh"; }
check "a release image (SSH off, a password or none): no password logs in" "no no" "$(built 0 0 0) $(built 0 1 0)"
check "built with SSH on and without a password: no" "no" "$(built 1 1 0)"
check "built with SSH on and PUBKEY_ONLY_SSH=1: no" "no" "$(built 1 0 1)"
check "built with SSH on and a password: yes, or it has no way in" "yes" "$(built 1 0 0)"

# sshd's settings from the drop-ins there, the first value of each kept, as
# sshd reads them (keywords in any case).
mkdir -p "$T/sshd_config.d"
sed 's/@PASSWORD_AUTHENTICATION@/no/' "$STAGE/files/osdos-sshd.conf" > "$T/sshd_config.d/$base_name"
setting() {  # keyword: its value as sshd takes it
    for f in $(cd "$T/sshd_config.d" && printf '%s\n' *.conf | LC_ALL=C sort); do
        cat "$T/sshd_config.d/$f"
    done | awk -v want="$(echo "$1" | tr 'A-Z' 'a-z')" '
        /^[[:space:]]*(#|$)/ { next }
        { key = tolower($1) }
        key == want { print $2; exit }'
}
check "no card: no password logs in" "no" "$(setting PasswordAuthentication)"
check "no keyboard-interactive" "no" "$(setting KbdInteractiveAuthentication)"
check "no root" "no" "$(setting PermitRootLogin)"
check "the sudo group's users alone (pi, and Imager's)" "sudo" "$(setting AllowGroups)"
check "three tries a connection" "3" "$(setting MaxAuthTries)"
check "20 seconds to sign in" "20" "$(setting LoginGraceTime)"
check "unauthenticated connections limited" "3:50:10" "$(setting MaxStartups)"
check "nothing but the template's value left to fill" "" "$(grep -o '@[A-Z_]*@' "$T/sshd_config.d/$base_name" | grep -v PASSWORD_AUTHENTICATION)"
# A card's, as the applier writes them (its tests check it writes these).
printf 'PasswordAuthentication yes\nKbdInteractiveAuthentication no\n' > "$T/sshd_config.d/$card_name"
check "a card with password login: a password logs in" "yes" "$(setting PasswordAuthentication)"
check "...through PasswordAuthentication alone" "no" "$(setting KbdInteractiveAuthentication)"
check "...the rest the image's" "no sudo 3" "$(setting PermitRootLogin) $(setting AllowGroups) $(setting MaxAuthTries)"
printf 'PasswordAuthentication no\nKbdInteractiveAuthentication no\n' > "$T/sshd_config.d/$card_name"
check "a card with keys only: no password" "no" "$(setting PasswordAuthentication)"
# Raspberry Pi Imager's customisation, through cloud-init, with password
# login asked for: it wins over the image's.
rm -f "$T/sshd_config.d/$card_name"
printf 'PasswordAuthentication yes\n' > "$T/sshd_config.d/50-cloud-init.conf"
check "Imager's password login: kept" "yes" "$(setting PasswordAuthentication)"
check "the applier says the card's drop-in comes first" "1" \
      "$(grep -c "^SSHD_CONF = \"/etc/ssh/sshd_config.d/$card_name\"" "$STAGE/files/osdos-setup")"

# --- The boot partition's switches, masked with the gettys.
check "sshswitch and userconfig masked" "1" \
      "$(grep -c '^systemctl mask sshswitch.service userconfig.service$' "$STAGE/01-run-chroot.sh")"

if [ "$failures" -gt 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
echo "All checks passed"
