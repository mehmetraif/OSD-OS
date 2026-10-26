#!/bin/bash
# The Downloads table the release workflow writes at the end of a release's
# notes (.github/workflows/release.yml): a row per file the release
# publishes, in the order its page lists them under Assets. GitHub lists a
# release's files by name, case-blind, whatever order they were uploaded in,
# so the rows go in that order and the table reads like the Assets list.
#
#   release_downloads_test.sh <path to release.yml>
set -euo pipefail

workflow=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

failures=0
check() {
    local name=$1
    shift
    if "$@"; then
        echo "PASS: $name"
    else
        echo "FAIL: $name"
        failures=$((failures + 1))
    fi
}

# Each row's file, for a made-up tag: the File cell of every row the step
# echoes, which must name one file and nothing else.
row_files() {
    local cell
    # shellcheck disable=SC2016  # the workflow's own ${TAG}, as written
    grep -E '^ +echo "\| \*\*' "$1" | awk -F' [|] ' '{ print $2 }' |
        sed -e 's/\\`/`/g' -e 's/\${TAG}/v1.2.3/g' |
        while IFS= read -r cell; do
            [[ $cell =~ ^\`([^\`]+)\`$ ]] || { echo "not one file: $cell" >&2; return 1; }
            echo "${BASH_REMATCH[1]}"
        done
}

count() { printf '%s\n' "$1" | grep -c . || true; }

# The files in the order the release page lists them.
in_assets_order() { [ "$1" == "$(printf '%s\n' "$1" | LC_ALL=C sort -f)" ]; }
out_of_assets_order() { ! in_assets_order "$1"; }

files=$(row_files "$workflow") || files=
rows=$(count "$files")
check "a file per row" test "$rows" -gt 0
check "each file in one row" test "$(count "$(printf '%s\n' "$files" | sort -u)")" -eq "$rows"
check "the rows in the order of the release's Assets" in_assets_order "$files"

# A row for every file the Publish step uploads, and none for anything else:
# each entry under its files names exactly one of the rows' files.
published=$(awk '/^ +files: \|/ { on = 1; next } on && /^ +[a-z_]+:/ { on = 0 } on && NF { print $1 }' "$workflow")
matched=0
while IFS= read -r glob; do
    hits=0
    while IFS= read -r file; do
        # shellcheck disable=SC2053  # the entry's name is a glob
        [[ $file == ${glob##*/} ]] && hits=$((hits + 1))
    done <<< "$files"
    if [ "$hits" -eq 1 ]; then
        matched=$((matched + 1))
    else
        echo "$glob is in $hits rows" >&2
    fi
done <<< "$published"
check "a row for each file published" test "$matched" -eq "$(count "$published")"
check "no row for a file not published" test "$rows" -eq "$(count "$published")"

# The order check holds a table out of order to account: the first two rows
# swapped are caught.
awk '/^ +echo "\| \*\*/ && ++n <= 2 { held[n] = $0; if (n == 2) { print held[2]; print held[1] }; next } { print }' \
    "$workflow" > "$work/swapped.yml"
swapped=$(row_files "$work/swapped.yml") || swapped=
check "two rows swapped are out of order" out_of_assets_order "$swapped"

[ "$failures" -eq 0 ]
