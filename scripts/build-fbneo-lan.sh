#!/usr/bin/env bash
# OSD/OS's FinalBurn Neo core for the local network: FinalBurn Neo's whole
# libretro core with scripts/fbneo-lan.patch (ARCHITECTURE.md, the Arcade
# module's LAN Play). It ships with the app, which plays every game hosted or
# joined on the local network with it (src/modules/arcade/Libretro.cpp,
# lanCorePath()); online play keeps libretro's own core, as everyone's.
#
# The patch:
# - seats the game's players in the order they first press COIN or START:
#   netplay's port 0, the host's player 1, is player 1, and any other port,
#   on whichever machine, becomes the first free player as it comes in, so
#   that a controller no one plays holds no player. The seats are in the
#   core's states, so every machine seats alike.
# - sets a game's DIP switch for how many players its cabinet has
#   ("Cabinet: 2 Players / 4 Players", "Number of Players", "Max Players")
#   to the most it has inputs for, each with a coin slot of its own where
#   the switch offers one, by default, which is what every machine on the
#   network plays with: a game left on two players reads no third or fourth
#   player's coin, start or controls. A setting for two boards linked, a
#   cocktail table or free play is never taken.
# - for Cave's CV1000 board (Akai Katana, Mushihime-sama, DoDonPachi
#   DaiFukkatsu...), leaves the board's 128 MiB of video memory out of the
#   states netplay keeps a ring of, and has the game wait from power on
#   until player 2 presses something, so that it starts on every machine at
#   the same frame.
#
#   scripts/build-fbneo-lan.sh <FBNeo source folder> <folder for the core> \
#     <file for FinalBurn Neo's licence>
#
# The source is the commit scripts/engines.env names, unpacked
# (github.com/libretro/FBNeo's archive of it), in a folder of its own, which
# the patch is applied to. The core is osdos_lan_libretro.so (.dylib on a
# Mac, for macOS 11 and later; .dll on Windows, built in MSYS2's UCRT64
# environment, MinGW-w64 on Windows' Universal C Runtime, which every
# Windows 10 and later has, and linked statically, so it needs no DLL of
# MinGW's beside it),
# built as scripts/build-fbneo.sh builds the whole core. FinalBurn Neo's
# licence goes with it, as the licence asks of every copy. The core names
# itself OSD-OS LAN Core, which RetroArch won't let a machine with another
# core join (no slash in it: RetroArch makes folders of a core's name for its
# options and remap files), and its version is a build from git's (v1.0.0.03
# <date> GIT<commit>) with -lan after it.
set -euo pipefail

usage="usage: build-fbneo-lan.sh <FBNeo source folder> <folder for the core> <file for FinalBurn Neo's licence>"
src=${1:?$usage}
out=${2:?$usage}
license=${3:?$usage}
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source-path=SCRIPTDIR source=engines.env
. "$here/engines.env"
commit=$FBNEO_COMMIT
date=$FBNEO_DATE

make=$(command -v gmake || command -v make)
jobs=$(getconf NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

patch -d "$src" -p1 -i "$here/fbneo-lan.patch"

args=(GIT_VERSION="${commit:0:7}-lan" GIT_DATE="$date")
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
        # As scripts/build-fbneo.sh builds the whole core on the BSDs.
        # shellcheck disable=SC2016
        args+=(CC=cc CXX=c++ SHARED='-shared -Wl,--version-script=$(VERSION_SCRIPT)')
        export FBNEO_DEFINES=-DGBAPPU_WORKER_DISABLE ;;
    *)
        ext=so ;;
esac
"$make" -C "$src/src/burner/libretro" -j"$jobs" "${args[@]}"

core=osdos_lan_libretro.$ext
mkdir -p "$out" "$(dirname "$license")"
cp "$src/src/burner/libretro/fbneo_libretro.$ext" "$out/$core"
"${strip[@]}" "$out/$core"
cp "$src/src/license.txt" "$license"
echo "FinalBurn Neo ${commit:0:7} LAN core: $(( $(wc -c < "$out/$core") / 1024 )) KB"
