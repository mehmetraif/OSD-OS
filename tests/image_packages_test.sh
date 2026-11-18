#!/usr/bin/env bash
# The OSD/OS image installs what scripts/install.sh installs on Raspberry Pi
# OS Lite (os/stage-osdos/00-packages/00-packages-nr), so a card made from the
# image and an installed system run with the same packages: every package the
# installer's apt-get installs is in the image's list, less qt6-wayland
# (desktop sessions only), and the image adds alsa-utils alone. A package
# added to one list and not the other fails here.
#   image_packages_test.sh <scripts/install.sh> <os/stage-osdos/00-packages/00-packages-nr>
set -u

INSTALL_SH="$1"
IMAGE_LIST="$2"

# The packages of every `sudo apt-get install -y` in the installer, its lines
# joined where they end in a backslash, up to a `||` or the command's end.
installer_packages() {
    awk '
        { line = line $0 }
        /\\$/ { sub(/\\$/, " ", line); next }
        { if (line ~ /^[[:space:]]*sudo apt-get install /) print line; line = "" }
    ' "$INSTALL_SH" | while read -r -a words; do
        for word in "${words[@]:3}"; do
            case $word in
                '||') break ;;
                -*) ;;
                *) echo "$word" ;;
            esac
        done
    done | sort -u
}

image_packages() {
    sed 's/#.*//' "$IMAGE_LIST" | tr -s ' \t' '\n\n' | sed '/^$/d' | sort -u
}

installer=$(installer_packages)
image=$(image_packages)

failures=0
check() {  # what, expected, actual
    if [ "$2" != "$3" ]; then
        echo "FAIL: $1: expected '$2', got '$3'"
        failures=$((failures + 1))
    else
        echo "PASS: $1"
    fi
}

check "the installer's packages were found" "yes" "$(echo "$installer" | grep -qx mpv && echo yes || echo no)"
check "the image's packages were found" "yes" "$(echo "$image" | grep -qx mpv && echo yes || echo no)"
check "the installer's packages missing from the image" "" \
      "$(comm -23 <(echo "$installer" | grep -vx qt6-wayland) <(echo "$image") | tr '\n' ' ' | sed 's/ $//')"
check "the image's packages the installer doesn't install" "alsa-utils" \
      "$(comm -13 <(echo "$installer") <(echo "$image") | tr '\n' ' ' | sed 's/ $//')"

[ "$failures" -eq 0 ] || { echo "$failures check(s) failed"; exit 1; }
echo "All checks passed"
