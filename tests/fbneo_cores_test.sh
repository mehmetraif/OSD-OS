#!/bin/bash
# scripts/build-fbneo-cores.sh with stand-ins for curl and for the two
# scripts that build the cores: the download's address, the archive unpacked
# twice without its top folder or its project files and then removed, the
# LAN core built before the whole core, and the usage without arguments.
#
#   fbneo_cores_test.sh <path to build-fbneo-cores.sh>
set -euo pipefail

source_dir=$(cd "$(dirname "$1")" && pwd)
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

# The script beside its engines.env, with build scripts that say how they
# were called.
mkdir -p "$work/scripts" "$work/bin" "$work/run"
cp "$source_dir/build-fbneo-cores.sh" "$source_dir/engines.env" "$work/scripts/"
for name in build-fbneo build-fbneo-lan; do
    printf 'echo "%s $*" >> calls\n' "$name" > "$work/scripts/$name.sh"
done
printf '#!/bin/sh\necho "curl $*"\n' > "$work/bin/curl"
chmod +x "$work/bin/curl"
script="$work/scripts/build-fbneo-cores.sh"
. "$work/scripts/engines.env"
cd "$work/run"

check "download" \
    "curl -fsSL --retry 3 -o fbneo-src.tar.gz https://github.com/libretro/FBNeo/archive/${FBNEO_COMMIT}.tar.gz" \
    "$(PATH="$work/bin:$PATH" bash "$script" download)"

# An archive as GitHub makes one: everything under one top folder.
mkdir -p "$work/archive/FBNeo-$FBNEO_COMMIT/src/burner" "$work/archive/FBNeo-$FBNEO_COMMIT/projectfiles/xcode"
echo source > "$work/archive/FBNeo-$FBNEO_COMMIT/src/burner/burn.cpp"
echo project > "$work/archive/FBNeo-$FBNEO_COMMIT/projectfiles/xcode/project"
tar -czf fbneo-src.tar.gz -C "$work/archive" "FBNeo-$FBNEO_COMMIT"

bash "$script" build cores cores/fbneo-arcade.dat cores/license.txt
check "both cores built, the LAN core first" \
    "build-fbneo-lan fbneo-lan-src cores cores/license.txt
build-fbneo fbneo-src cores cores/fbneo-arcade.dat cores/license.txt" \
    "$(cat calls)"
check "each copy unpacked without its top folder or project files" \
    "fbneo-lan-src/src/burner/burn.cpp
fbneo-src/src/burner/burn.cpp" \
    "$(find fbneo-src fbneo-lan-src -type f | sort)"
check "the archive removed" "no" "$([ -e fbneo-src.tar.gz ] && echo yes || echo no)"

status=0
bash "$script" > /dev/null 2> usage || status=$?
check "usage without arguments" "2 1" "$status $(grep -c '^usage: build-fbneo-cores.sh' usage)"

if [ "$failures" -gt 0 ]; then
    echo "$failures failed"
    exit 1
fi
echo "all passed"
