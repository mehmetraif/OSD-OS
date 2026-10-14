#!/bin/sh
# Builds modules/highscores/data/hi2txt.zip, the game definitions the High
# Scores module reads MAME's high score files with, from a checkout of
# hi2txt-xml (https://github.com/GreatStoneEx/hi2txt-xml): every
# src/main/db/*.xml, in name order and with fixed times, so that the same
# checkout always gives the same zip.
#
#   git clone --depth 1 https://github.com/GreatStoneEx/hi2txt-xml
#   scripts/update-hi2txt.sh hi2txt-xml
#
# Then note the checkout's commit in modules/highscores/data/README.md.
set -eu

src=${1:?usage: $0 <hi2txt-xml checkout>}
root=$(cd "$(dirname "$0")/.." && pwd)
out="$root/modules/highscores/data/hi2txt.zip"

if [ ! -d "$src/src/main/db" ]; then
    echo "$src has no src/main/db: not a hi2txt-xml checkout" >&2
    exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp "$src"/src/main/db/*.xml "$tmp"/
# Zip's earliest time, and no extra attributes (-X): nothing of when or by
# whom the zip was made goes into it.
touch -t 198001010000 "$tmp"/*.xml
rm -f "$out"
(cd "$tmp" && ls | LC_ALL=C sort | zip -q -X -9 "$out" -@)

count=$(ls "$tmp" | wc -l | tr -d ' ')
commit=$(git -C "$src" rev-parse --short HEAD 2>/dev/null || echo "unknown")
echo "$out: $count definitions from hi2txt-xml $commit"
