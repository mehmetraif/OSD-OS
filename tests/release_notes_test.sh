#!/bin/bash
# scripts/release-notes.sh on a made-up history: the commits since the
# previous tag, under What's New, Changelog and Bug Fixes.
#
#   release_notes_test.sh <path to release-notes.sh>
set -euo pipefail

script=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"

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

# Each commit a second after the last, so that their order is known.
second=0
commit() {
    second=$((second + 1))
    local date
    date=$(printf '2026-01-01T00:00:%02d+00:00' "$second")
    GIT_AUTHOR_DATE=$date GIT_COMMITTER_DATE=$date \
        git -c user.name=Test -c user.email=test@example.invalid commit -q --allow-empty -m "$1"
}

git init -q -b main
commit "feat: before the last release"
git tag v1.0.0
commit "feat: a new screen"
commit "fix(os): the image boots on a Pi 5"
commit "docs: the wiki's new page"
commit "Fix a crash on start"
git checkout -q -b topic
commit "Add a setting"
git checkout -q main
commit "Keep the cursor on its line"
commit "refactor!: one map per pad"
second=$((second + 1))
date=$(printf '2026-01-01T00:00:%02d+00:00' "$second")
GIT_AUTHOR_DATE=$date GIT_COMMITTER_DATE=$date \
    git -c user.name=Test -c user.email=test@example.invalid merge -q --no-ff topic -m "Merge pull request #1 from someone/topic"
commit "fix: Switch to <output>? asks twice"
commit "feat: a new screen"
git tag v1.1.0

check "the commits since the previous tag, by kind" "## What's New

- A new screen
- Add a setting

## Changelog

- The wiki's new page
- Keep the cursor on its line
- One map per pad

## Bug Fixes

- The image boots on a Pi 5
- Fix a crash on start
- Switch to &lt;output&gt;? asks twice" "$(bash "$script" v1.1.0)"

check "the previous tag given" "$(bash "$script" v1.1.0)" "$(bash "$script" v1.1.0 v1.0.0)"

check "the first release: the whole history" "## What's New

- Before the last release" "$(bash "$script" v1.0.0)"

commit "docs: only docs"
git tag v1.1.1
check "only what a release has" "## Changelog

- Only docs" "$(bash "$script" v1.1.1)"

if bash "$script" 2>/dev/null; then
    echo "FAIL: no tag given"
    failures=$((failures + 1))
else
    echo "PASS: no tag given"
fi

[ "$failures" -eq 0 ]
