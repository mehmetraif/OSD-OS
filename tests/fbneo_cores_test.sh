#!/bin/bash
# scripts/build-fbneo-cores.sh with stand-ins for curl and for the two
# scripts that build the cores: the download's address, the archive unpacked
# twice without its top folder or its project files and then removed, the
# LAN core built before the whole core, and the usage without arguments.
# Then each system's settings both scripts build with (scripts/fbneo-make.sh),
# with stand-ins for uname and make: what make is given, and what it finds in
# its environment, -ffp-contract=off on every system among it.
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

# fbneo_make on each system it serves: a stand-in make that says what it
# was given, with FBNEO_DEFINES and LDFLAGS from its environment, and a
# stand-in uname naming the system.
mkdir -p "$work/make-bin"
cat > "$work/make-bin/make" << 'EOF'
#!/bin/sh
echo "args: $*"
echo "FBNEO_DEFINES=${FBNEO_DEFINES-unset} LDFLAGS=${LDFLAGS-unset}"
EOF
cp "$work/make-bin/make" "$work/make-bin/gmake"
chmod +x "$work/make-bin/make" "$work/make-bin/gmake"
made() {  # system
    printf '#!/bin/sh\necho %s\n' "$1" > "$work/make-bin/uname"
    chmod +x "$work/make-bin/uname"
    env -u FBNEO_DEFINES -u LDFLAGS PATH="$work/make-bin:$PATH" bash -c \
        '. "$1"; fbneo_make src GIT_VERSION=abc1234 GIT_DATE=2026-01-01 > "$2"; sed "s/-j[0-9]*/-jN/" "$2"; echo "ext=$ext"' \
        made "$source_dir/fbneo-make.sh" "$work/made"
}
check "Linux: the makefile's own build, without fused multiply-adds" \
    "args: -C src/src/burner/libretro -jN GIT_VERSION=abc1234 GIT_DATE=2026-01-01
FBNEO_DEFINES=-ffp-contract=off LDFLAGS=unset
ext=so" "$(made Linux)"
check "macOS: the same" \
    "args: -C src/src/burner/libretro -jN GIT_VERSION=abc1234 GIT_DATE=2026-01-01
FBNEO_DEFINES=-ffp-contract=off LDFLAGS=unset
ext=dylib" "$(made Darwin)"
check "Windows: MinGW's compilers, linked statically, the same flag" \
    "args: -C src/src/burner/libretro -jN GIT_VERSION=abc1234 GIT_DATE=2026-01-01 CC=gcc CXX=g++
FBNEO_DEFINES=-ffp-contract=off LDFLAGS=-static
ext=dll" "$(made MINGW64_NT-10.0-26100)"
for bsd in FreeBSD OpenBSD; do
    # shellcheck disable=SC2016  # the makefile's own $(VERSION_SCRIPT)
    check "$bsd: the system's compilers, the flag and the GBA define" \
        'args: -C src/src/burner/libretro -jN GIT_VERSION=abc1234 GIT_DATE=2026-01-01 CC=cc CXX=c++ SHARED=-shared -Wl,--version-script=$(VERSION_SCRIPT)
FBNEO_DEFINES=-ffp-contract=off -DGBAPPU_WORKER_DISABLE LDFLAGS=unset
ext=so' "$(made "$bsd")"
done
# What the caller already put in FBNEO_DEFINES is kept, the flag after it.
check "a caller's FBNEO_DEFINES kept" "FBNEO_DEFINES=-DEXTRA -ffp-contract=off LDFLAGS=unset" \
    "$(printf '#!/bin/sh\necho Linux\n' > "$work/make-bin/uname"
       env -u LDFLAGS FBNEO_DEFINES=-DEXTRA PATH="$work/make-bin:$PATH" bash -c \
           '. "$1"; fbneo_make src | sed -n 2p' made "$source_dir/fbneo-make.sh")"
# No CPU flags: every machine of a release plays the same cores.
check "no CPU flags" "" "$(grep -nE -- '-m(cpu|arch|tune)=' "$source_dir/fbneo-make.sh")"

if [ "$failures" -gt 0 ]; then
    echo "$failures failed"
    exit 1
fi
echo "all passed"
