#!/bin/sh
# OSD/OS's logs, copied where a computer reading the card finds them.
#
#   save-logs.sh <data folder> <card folder>
#
# The app runs this as it quits on the OSD/OS image (Quit → Power Off, Restart,
# an update's restart), with its data folder and OSDOS_MEDIA_DIR, the card's
# OSD-OS partition, which Windows and macOS show as a drive of its own. The
# journal lives in memory there and is gone at the next start, so what a bug
# report wants is copied to that partition first, into Logs/<date_time>/:
#
#   osdos.log            the app's journal since the Pi started
#   system.log           the whole journal since then (the kernel's and the
#                        other services' lines), its last 50000 lines
#   stall-report.txt     a freeze's report, and crash-report.txt a crash's,
#                        where there is one
#   retroarch-last.log   RetroArch's log of the last FinalBurn Neo game
#
# Not mpv's own log (/tmp/osdos-mpv.log): it holds each video's full address,
# a media server's token with it, and the partition is anyone's to read.
#
# The newest ten folders are kept. Nothing is done when the folder isn't
# there or can't be written (the partition not mounted, or read-only). Prints
# the folder it wrote. OSDOS_SAVE_LOGS=0 keeps the app from running it.
set -u

data=${1:-}
card=${2:-}
if [ -z "$data" ] || [ -z "$card" ]; then
    echo "usage: save-logs.sh <data folder> <card folder>" >&2
    exit 2
fi
if [ ! -d "$card" ] || [ ! -w "$card" ]; then
    exit 0
fi

logs="$card/Logs"
# Without colons: exFAT and Windows take none in a name.
dir="$logs/$(date +%Y-%m-%d_%H-%M-%S)"
mkdir -p "$dir" || exit 1

# A token in an address blanked on the way, as the app's own log does: a
# media server's (t=, token=, X-Plex-Token=, api_key=), whatever wrote it.
hide() {
    sed -E 's/([?&;]([Tt]|[Tt]oken|X-Plex-Token|api_?key|[Aa]pi[Kk]ey)=)[^&;[:space:]"]*/\1HIDDEN/g'
}

if command -v journalctl > /dev/null 2>&1; then
    journalctl -b --no-pager -o short-precise -u osdos.service 2>&1 | hide > "$dir/osdos.log"
    journalctl -b --no-pager -o short-precise -n 50000 2>&1 | hide > "$dir/system.log"
fi
for report in stall-report.txt crash-report.txt; do
    [ -f "$data/$report" ] && hide < "$data/$report" > "$dir/$report"
done
[ -f "$data/arcade/retroarch/last.log" ] && hide < "$data/arcade/retroarch/last.log" > "$dir/retroarch-last.log"

# The oldest gone, past the newest ten: the shell lists them by name, which
# is by their time.
set -- "$logs"/????-??-??_??-??-??
if [ -d "$1" ]; then
    while [ "$#" -gt 10 ]; do
        rm -rf "$1"
        shift
    done
fi

# On the card before the Pi powers off.
sync
echo "$dir"
