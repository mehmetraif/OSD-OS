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
# licence asks of every copy. How it is made on each system, the BSDs'
# settings among them, is scripts/fbneo-make.sh's.
set -euo pipefail

usage="usage: build-fbneo.sh <FBNeo source folder> <folder for the core> <file for the list of games> <file for the licence>"
src=${1:?$usage}
out=${2:?$usage}
dat=${3:?$usage}
license=${4:?$usage}
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source-path=SCRIPTDIR source=engines.env
. "$here/engines.env"
# shellcheck source=fbneo-make.sh
. "$here/fbneo-make.sh"
commit=$FBNEO_COMMIT
date=$FBNEO_DATE

fbneo_make "$src" GIT_VERSION="${commit:0:7}" GIT_DATE="$date"

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
