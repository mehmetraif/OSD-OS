#!/bin/bash
# scripts/web-player.sh, which the Netflix, Prime Video and YouTube modules
# run, with stand-ins for uname (an Arm Linux or a PC) and Chromium, on a
# desktop (DISPLAY set: the browser started straight away): the user agent it
# gives the browser on Arm, with the browser's own version, which is asked of
# the browser once and kept in the data folder (web-player-browser) by the
# browser's file and time; asked again once the browser has changed, or when
# what is kept can't be taken; never on a PC, nor with a user agent given.
#
#   web_player_test.sh <path to web-player.sh>
set -euo pipefail

script=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

failures=0
check() {
    local name=$1 expected=$2 actual=$3
    if [ "$expected" == "$actual" ]; then
        echo "PASS: $name"
    else
        echo "FAIL: $name"
        diff <(printf '%s\n' "$expected") <(printf '%s\n' "$actual") || true
        failures=$((failures + 1))
    fi
}

# uname says what $FAKE_MACHINE is; Chromium notes each time it is asked its
# version, and what it was started with otherwise.
mkdir -p "$work/bin"
cat > "$work/bin/uname" << 'EOF'
#!/bin/sh
case "$1" in
    -m) echo "${FAKE_MACHINE:-aarch64}" ;;
    *) echo Linux ;;
esac
EOF
cat > "$work/bin/chromium" << EOF
#!/bin/sh
if [ "\$1" = --version ]; then
    echo asked >> "$work/asked"
    echo "Chromium \${FAKE_VERSION:-126.0.6478.164} built on Debian GNU/Linux 12 (bookworm)"
    exit 0
fi
printf '%s\n' "\$@" > "$work/started"
EOF
chmod +x "$work/bin/uname" "$work/bin/chromium"
export PATH="$work/bin:/usr/bin:/bin"
export DATA_ROOT="$work/data"
export DISPLAY=:99
unset WAYLAND_DISPLAY OSDOS_WEB_PLAYER_UA MP240_WEB_PLAYER_UA FAKE_MACHINE FAKE_VERSION 2>/dev/null || true

asked() { [ -f "$work/asked" ] && wc -l < "$work/asked" | tr -d ' ' || echo 0; }
agent() { grep '^--user-agent=' "$work/started" | sed 's/.*Chrome\/\([^ ]*\) .*/\1/' || true; }
play() { rm -f "$work/started"; sh "$script" netflix https://www.netflix.com/ letterbox; }
kept="$work/data/web-player-browser"

play
check "Arm: the browser asked its version once" 1 "$(asked)"
check "Arm: its version in the user agent" 126.0.6478.164 "$(agent)"
check "Arm: the version kept with the browser's file and time" \
    "$work/bin/chromium $(stat -c %Y "$work/bin/chromium")
126.0.6478.164" "$(cat "$kept")"

play
check "the next run: not asked" 1 "$(asked)"
check "the next run: the kept version in the user agent" 126.0.6478.164 "$(agent)"

# The browser updated: another time, another version.
touch -d '2001-02-03 04:05:06' "$work/bin/chromium"
export FAKE_VERSION=127.0.6533.88
play
check "the browser changed: asked again" 2 "$(asked)"
check "the browser changed: its new version" 127.0.6533.88 "$(agent)"
play
check "then kept again" 2 "$(asked)"

# What is kept and isn't a version: asked, and written over.
printf '%s\n%s\n' "$work/bin/chromium $(stat -c %Y "$work/bin/chromium")" '1.2 --no-sandbox' > "$kept"
play
check "a kept line that isn't a version: asked" 3 "$(asked)"
check "a kept line that isn't a version: never on the command line" 0 "$(grep -c -- '--no-sandbox' "$work/started" || true)"
check "a kept line that isn't a version: the browser's own" 127.0.6533.88 "$(agent)"
# Cut short: asked.
printf '%s' "$work/bin/chromium" > "$kept"
play
check "a kept file cut short: asked" 4 "$(asked)"

# A user agent given: nothing asked.
export OSDOS_WEB_PLAYER_UA="Test Agent"
play
unset OSDOS_WEB_PLAYER_UA
check "a user agent given: not asked" 4 "$(asked)"
check "a user agent given: it" "--user-agent=Test Agent" "$(grep '^--user-agent=' "$work/started")"

# A PC: no user agent, nothing asked.
export FAKE_MACHINE=x86_64
play
check "a PC: not asked" 4 "$(asked)"
check "a PC: no user agent" "" "$(grep '^--user-agent=' "$work/started" || true)"
check "a PC: the page opened" https://www.netflix.com/ "$(tail -n 1 "$work/started")"

if [ "$failures" -gt 0 ]; then
    echo "$failures failed"
    exit 1
fi
echo "all passed"
