#!/usr/bin/env bash
# FinalBurn Neo's libretro core built from libretro's own source, for the
# systems libretro's build server has no build for (FreeBSD, OpenBSD): the
# core, the list of arcade games it reads and its licence, laid out under a
# prefix where the app looks for them in its package (src/modules/arcade/
# Libretro.cpp, packagedCore()):
#
#   <prefix>/lib/osdos/fbneo_libretro.so
#   <prefix>/share/osdos/fbneo/fbneo-arcade.dat, license.txt and BUILD
#
#   FBNEO_COMMIT=<40-hex commit> FBNEO_DATE=<yymmdd> \
#     scripts/build-fbneo.sh <FBNeo source folder> <prefix>
#
# The source is the commit named, unpacked (github.com/libretro/FBNeo's
# archive of it). The core reports the version a build from git reports
# (v1.0.0.03 <date> GIT<commit>), which the commit and its date make, so that
# it matches libretro's build of the same commit, which online play compares.
# Nothing of FinalBurn Neo's code is changed: the makefile's own Unix build,
# with the system's compiler (cc, c++), and without -no-undefined, which
# OpenBSD can't link a library with (its libraries leave libc's symbols to
# the program that loads them).
set -euo pipefail

src=${1:?usage: build-fbneo.sh <FBNeo source folder> <prefix>}
prefix=${2:?usage: build-fbneo.sh <FBNeo source folder> <prefix>}
commit=${FBNEO_COMMIT:?FBNEO_COMMIT: the commit the source is}
date=${FBNEO_DATE:?FBNEO_DATE: the commit date, yymmdd}

make=$(command -v gmake || command -v make)
jobs=$(getconf NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

# $(VERSION_SCRIPT) is the makefile's own, for make to expand.
# shellcheck disable=SC2016
"$make" -C "$src/src/burner/libretro" -j"$jobs" CC=cc CXX=c++ \
    SHARED='-shared -Wl,--version-script=$(VERSION_SCRIPT)' \
    GIT_VERSION="${commit:0:7}" GIT_DATE="$date"

core=$src/src/burner/libretro/fbneo_libretro.so
dat="$src/dats/FinalBurn Neo (ClrMame Pro XML, Arcade only).dat"
# The list is the one the app reads its games from: a short one is a broken
# download, not a list.
games=$(grep -c '<game name=' "$dat")
[ "$games" -gt 1000 ] || { echo "only $games games in $dat" >&2; exit 1; }

install -d "$prefix/lib/osdos" "$prefix/share/osdos/fbneo"
install -m 644 "$core" "$prefix/lib/osdos/fbneo_libretro.so"
strip "$prefix/lib/osdos/fbneo_libretro.so"
install -m 644 "$dat" "$prefix/share/osdos/fbneo/fbneo-arcade.dat"
install -m 644 "$src/src/license.txt" "$prefix/share/osdos/fbneo/license.txt"
printf 'FinalBurn Neo %s (%s), built from https://github.com/libretro/FBNeo/tree/%s\n' \
    "${commit:0:7}" "$date" "$commit" > "$prefix/share/osdos/fbneo/BUILD"
echo "FinalBurn Neo ${commit:0:7}: $games games, $(du -k "$prefix/lib/osdos/fbneo_libretro.so" | cut -f1) KB"
