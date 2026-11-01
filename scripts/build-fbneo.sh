#!/usr/bin/env bash
# FinalBurn Neo's libretro core, built from libretro's own source at the
# commit scripts/engines.env names, for every package of OSD/OS: the same
# core on every machine, so that machines play together over the network
# (RetroArch warns of a peer with another version, and their games drift
# apart), and so that an update of OSD/OS brings the core along.
#
#   scripts/build-fbneo.sh <FBNeo source folder> <folder for the core> \
#     <file for the list of games> <file for the licence>
#
# The source is the commit named, unpacked (github.com/libretro/FBNeo's
# archive of it). The core reports the version a build from git reports
# (v1.0.0.03 <date> GIT<commit>), which the commit and its date make, so that
# it matches libretro's own build of the same commit. Nothing of FinalBurn
# Neo's code is changed. The core is fbneo_libretro.so (.dylib on a Mac, for
# macOS 11 and later; .dll on Windows, built in MSYS2's UCRT64 environment,
# MinGW-w64 on Windows' Universal C Runtime, which every Windows 10 and later
# has, and linked statically, so that it needs no DLL of MinGW's beside it).
# The list of games is FinalBurn Neo's arcade DAT of the same commit, which
# the app lists the games from. The licence goes with the core, as the
# licence asks of every copy.
#
# On FreeBSD and OpenBSD: the makefile's Unix build with the system's
# compiler (cc, c++), without -no-undefined, which OpenBSD can't link a
# library with (its libraries leave libc's symbols to the program that loads
# them), and with GBAPPU_WORKER_DISABLE, a switch of FinalBurn Neo's own
# (src/burn/drv/gba/gbappu_worker.h) that draws the Game Boy Advance's
# picture on the emulator's thread instead of a thread of its own: that
# thread is named with macOS's pthread_setname_np(name) on every BSD, which
# FreeBSD's takes the thread as well and OpenBSD hasn't, so it doesn't compile
# on either. The arcade games, all OSD/OS plays, don't use it.
set -euo pipefail

usage="usage: build-fbneo.sh <FBNeo source folder> <folder for the core> <file for the list of games> <file for the licence>"
src=${1:?$usage}
out=${2:?$usage}
dat=${3:?$usage}
license=${4:?$usage}
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source-path=SCRIPTDIR source=engines.env
. "$here/engines.env"
commit=$FBNEO_COMMIT
date=$FBNEO_DATE

make=$(command -v gmake || command -v make)
jobs=$(getconf NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

args=(GIT_VERSION="${commit:0:7}" GIT_DATE="$date")
strip=(strip)
case "$(uname -s)" in
    Darwin)
        ext=dylib
        # The oldest macOS the app runs on, not the one it is built on.
        export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-11.0}
        # A library's own symbols are what RetroArch loads it by.
        strip=(strip -x) ;;
    MINGW* | MSYS*)
        ext=dll
        # MinGW's compilers by name: make's own default for C is cc.
        args+=(CC=gcc CXX=g++)
        # From the environment, which the makefile's LDFLAGS += adds its own
        # to: winpthreads, which MinGW's libstdc++ is built on, in the DLL too.
        export LDFLAGS=-static ;;
    *BSD)
        ext=so
        # $(VERSION_SCRIPT) is the makefile's own, for make to expand. The
        # define goes in from the environment, which the makefile's
        # FBNEO_DEFINES += adds its own to (one on make's command line would
        # replace them).
        # shellcheck disable=SC2016
        args+=(CC=cc CXX=c++ SHARED='-shared -Wl,--version-script=$(VERSION_SCRIPT)')
        export FBNEO_DEFINES=-DGBAPPU_WORKER_DISABLE ;;
    *)
        ext=so ;;
esac
"$make" -C "$src/src/burner/libretro" -j"$jobs" "${args[@]}"

core=fbneo_libretro.$ext
list="$src/dats/FinalBurn Neo (ClrMame Pro XML, Arcade only).dat"
# The list is the one the app reads its games from: a short one is a broken
# source, not a list.
games=$(grep -c '<game name=' "$list")
[ "$games" -gt 1000 ] || { echo "only $games games in $list" >&2; exit 1; }

mkdir -p "$out" "$(dirname "$dat")" "$(dirname "$license")"
cp "$src/src/burner/libretro/$core" "$out/$core"
"${strip[@]}" "$out/$core"
cp "$list" "$dat"
cp "$src/src/license.txt" "$license"
# The file's own size: du counts the disk blocks written so far, which ZFS
# (FreeBSD's) reports late for a file just made.
echo "FinalBurn Neo ${commit:0:7} ($date): $games games, $(( $(wc -c < "$out/$core") / 1024 )) KB"
