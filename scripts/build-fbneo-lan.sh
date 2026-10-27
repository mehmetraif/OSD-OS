#!/usr/bin/env bash
# OSD/OS's FinalBurn Neo core for games of Cave's CV1000 board (Akai Katana,
# Mushihime-sama, DoDonPachi DaiFukkatsu...) on the local network: FinalBurn
# Neo's libretro core of that board's driver alone (its makefile's subset
# build, SUBSET=cv1k), with scripts/fbneo-lan.patch, which leaves the board's
# 128 MiB of video memory out of the states netplay keeps a ring of, and has
# the game wait from power on until player 2 presses something, so that it
# starts on every machine at the same frame (ARCHITECTURE.md, the Arcade
# module's LAN Play). It ships with the app, which plays a CV1000 game hosted
# or joined on the local network with it (src/modules/arcade/Libretro.cpp,
# lanCorePath()); online play keeps libretro's own core, as everyone's.
#
#   scripts/build-fbneo-lan.sh <FBNeo source folder> <folder for the core> \
#     <file for FinalBurn Neo's licence>
#
# The source is the commit scripts/fbneo-lan.env names, unpacked
# (github.com/libretro/FBNeo's archive of it), which the patch is then
# applied to. The core is fbneo_cv1k_libretro.so (.dylib on a Mac, for macOS
# 11 and later; .dll on Windows, built with MSYS2's MinGW-w64 and linked
# statically, so it needs no DLL of MinGW's beside it). FinalBurn Neo's
# licence goes with it, as the licence asks of every copy. The core names
# itself FinalBurn Neo (cv1k subset), which RetroArch won't let a machine
# with another core join, and its version is a build from git's (v1.0.0.03
# <date> GIT<commit>) with -lan after it. Only the subset's driver list is
# generated (gamelist.pl, as the makefile's REGEN_HEADERS does): the
# source's is the whole core's. On FreeBSD and OpenBSD, the makefile's Unix
# build with the system's compiler and without -no-undefined, as
# scripts/build-fbneo.sh has it.
set -euo pipefail

usage="usage: build-fbneo-lan.sh <FBNeo source folder> <folder for the core> <file for FinalBurn Neo's licence>"
src=${1:?$usage}
out=${2:?$usage}
license=${3:?$usage}
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source-path=SCRIPTDIR source=fbneo-lan.env
. "$here/fbneo-lan.env"
commit=$FBNEO_LAN_COMMIT
date=$FBNEO_LAN_DATE

make=$(command -v gmake || command -v make)
jobs=$(getconf NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

patch -d "$src" -p1 -i "$here/fbneo-lan.patch"
perl "$src/src/dep/scripts/gamelist.pl" -o "$src/src/dep/generated/driverlist.h" \
    -l "$src/gamelist.txt" "$src/src/burn/drv/cave/d_cv1k.cpp"

args=(SUBSET=cv1k GIT_VERSION="${commit:0:7}-lan" GIT_DATE="$date")
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
        # shellcheck disable=SC2016
        args+=(CC=cc CXX=c++ SHARED='-shared -Wl,--version-script=$(VERSION_SCRIPT)') ;;
    *)
        ext=so ;;
esac
"$make" -C "$src/src/burner/libretro" -j"$jobs" "${args[@]}"

core=fbneo_cv1k_libretro.$ext
mkdir -p "$out" "$(dirname "$license")"
cp "$src/src/burner/libretro/$core" "$out/$core"
"${strip[@]}" "$out/$core"
cp "$src/src/license.txt" "$license"
echo "FinalBurn Neo ${commit:0:7} CV1000 LAN core: $(( $(wc -c < "$out/$core") / 1024 )) KB"
