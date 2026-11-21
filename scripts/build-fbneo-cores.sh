#!/usr/bin/env bash
# Both of OSD/OS's FinalBurn Neo cores, built from the commit
# scripts/engines.env names, as every package's build makes them
# (.github/actions/fbneo-cores, and scripts/bsd-ci.sh for FreeBSD's and
# OpenBSD's):
#
#   scripts/build-fbneo-cores.sh download
#   scripts/build-fbneo-cores.sh build <folder for the cores> \
#     <file for the list of games> <file for the licence>
#
# download fetches the commit's source (github.com/libretro/FBNeo's archive
# of it) into fbneo-src.tar.gz in the current folder. build unpacks it twice,
# into fbneo-src and fbneo-lan-src, since the LAN core's patch goes in a copy
# of its own, removes the archive, and builds the LAN core
# (scripts/build-fbneo-lan.sh), then the whole core with its list of games
# (scripts/build-fbneo.sh). The two are apart because a BSD's virtual machine
# builds the cores from the archive its host downloaded, and Windows builds
# them in MSYS2's shell.
set -euo pipefail

usage="usage: build-fbneo-cores.sh download | build <folder for the cores> <file for the list of games> <file for the licence>"
here=$(cd "$(dirname "$0")" && pwd)
archive=fbneo-src.tar.gz

# The archive unpacked into the folder $1 without its own top folder, and
# without the source's project files for IDEs, whose symbolic links Windows
# can't make. OpenBSD's tar has neither --strip-components nor --exclude,
# but rewrites each name (-s).
unpack() {
    mkdir -p "$1"
    if [ "$(uname -s)" = OpenBSD ]; then
        tar -xzf "$archive" -C "$1" -s '|^[^/]*/||'
    else
        tar -xzf "$archive" -C "$1" --strip-components 1 --exclude='*/projectfiles'
    fi
}

case "${1:-}" in
    download)
        # shellcheck source-path=SCRIPTDIR source=engines.env
        . "$here/engines.env"
        curl -fsSL --retry 3 -o "$archive" "https://github.com/libretro/FBNeo/archive/${FBNEO_COMMIT}.tar.gz" ;;
    build)
        out=${2:?$usage}
        dat=${3:?$usage}
        license=${4:?$usage}
        unpack fbneo-src
        unpack fbneo-lan-src
        rm "$archive"
        bash "$here/build-fbneo-lan.sh" fbneo-lan-src "$out" "$license"
        bash "$here/build-fbneo.sh" fbneo-src "$out" "$dat" "$license" ;;
    *)
        echo "$usage" >&2
        exit 2 ;;
esac
