#!/bin/bash
# scripts/save-logs.sh, which the app runs as it quits on the OSD/OS image, on
# a stand-in card partition and data folder, with a stand-in journalctl: the
# logs copied into Logs/<date_time>/ with their tokens blanked, the newest ten
# folders kept, and nothing done for a partition that isn't there or can't be
# written.
#
#   save_logs_test.sh <path to save-logs.sh>
set -euo pipefail

script=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
work=$(mktemp -d)
trap 'chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT

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

# journalctl says what it was asked.
mkdir -p "$work/bin"
cat > "$work/bin/journalctl" << 'EOF'
#!/bin/sh
echo "journalctl $*"
EOF
chmod +x "$work/bin/journalctl"
export PATH="$work/bin:$PATH"
export TMPDIR="$work/tmp"
mkdir -p "$TMPDIR"

data="$work/data"
card="$work/card"
mkdir -p "$data/arcade/retroarch" "$card"
echo "stall" > "$data/stall-report.txt"
printf '%s\n' "this run" \
    "20:32:55.335 Failed to commit atomic request (code=-22)" \
    "20:32:55.501 Failed to commit atomic request (code=-22)" \
    "20:32:55.502 [input] joysticks reclaimed" \
    "20:32:55.668 Failed to commit atomic request (code=-22)" \
    "20:32:55.834 Failed to commit atomic request (code=-22)" \
    "20:33:03.104 Failed to commit atomic request (code=-13)" \
    "20:33:03.159 [input] joysticks released" > "$data/osdos.log"
printf '%s\n' "the run before" "GET http://jellyfin.local/Videos/1/stream?api_key=SECRET3" > "$data/osdos-previous.log"
printf '%s\n' "retroarch" "GET https://ra.example/dorequest.php?r=login2&u=me&t=SECRET1 done" \
    "http://plex.local/library?X-Plex-Token=SECRET2" > "$data/arcade/retroarch/last.log"
# mpv's own log, which holds a media server's token, stays where it is.
echo "mpv" > "$TMPDIR/osdos-mpv.log"

# Ten folders of earlier quits, the oldest first by their names.
for n in 01 02 03 04 05 06 07 08 09 10; do
    mkdir -p "$card/Logs/2020-01-$n"_00-00-00
done

folder=$(sh "$script" "$data" "$card")
check "prints the folder it wrote" "$card/Logs" "$(dirname "$folder")"
check "the folder's name has no colons" "" "$(basename "$folder" | tr -d '0-9_-')"
check "the files copied, not mpv's log" "app-previous-run.log
app-this-run.log
osdos.log
retroarch-last.log
stall-report.txt
system.log" "$(ls "$folder")"
check "the app's journal" "journalctl -b --no-pager -o short-precise -u osdos.service" "$(cat "$folder/osdos.log")"
check "the whole journal" "journalctl -b --no-pager -o short-precise -n 50000" "$(cat "$folder/system.log")"
check "a report as it was" "stall" "$(cat "$folder/stall-report.txt")"
check "the app's own log of this run, a flood folded" "this run
20:32:55.335 Failed to commit atomic request (code=-22)
  ... 1 more of the line(s) above, the last: 20:32:55.501 Failed to commit atomic request (code=-22)
20:32:55.502 [input] joysticks reclaimed
  ... 2 more of the line(s) above, the last: 20:32:55.834 Failed to commit atomic request (code=-22)
20:33:03.104 Failed to commit atomic request (code=-13)
20:33:03.159 [input] joysticks released" "$(cat "$folder/app-this-run.log")"
check "the run before's, its tokens blanked" "the run before
GET http://jellyfin.local/Videos/1/stream?api_key=HIDDEN" "$(cat "$folder/app-previous-run.log")"
check "RetroArch's log, its tokens blanked" "retroarch
GET https://ra.example/dorequest.php?r=login2&u=me&t=HIDDEN done
http://plex.local/library?X-Plex-Token=HIDDEN" "$(cat "$folder/retroarch-last.log")"
check "the newest ten kept, the oldest gone" "10 2020-01-02_00-00-00" \
    "$(ls "$card/Logs" | wc -l | tr -d ' ') $(ls "$card/Logs" | head -n 1)"

# A partition not there, or read-only: nothing done, no error.
status=0
out=$(sh "$script" "$data" "$work/missing") || status=$?
check "no partition: nothing done" "0 " "$status $out"
check "no partition: nothing made" "no" "$([ -e "$work/missing" ] && echo yes || echo no)"
if [ "$(id -u)" != 0 ]; then
    mkdir -p "$work/readonly"
    chmod a-w "$work/readonly"
    status=0
    out=$(sh "$script" "$data" "$work/readonly") || status=$?
    check "read-only partition: nothing done" "0 " "$status $out"
fi

# Without its arguments it says how it is used.
status=0
sh "$script" > /dev/null 2>&1 || status=$?
check "no arguments: usage" "2" "$status"

exit $((failures > 0))
