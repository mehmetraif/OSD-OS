#!/usr/bin/env bash
# OSD/OS as a package of the BSD it is built on: a FreeBSD package (.pkg,
# pkg-create(8)) or an OpenBSD one (.tgz, pkg_create(1)), made from an
# install staged as it lies under /usr/local (DESTDIR=<root> cmake --install
# build, with FinalBurn Neo's core beside it, scripts/build-fbneo.sh).
#
#   scripts/bsd-package.sh <root> <version> <folder>
#
# <version> is the release's tag (v0.0.24, v0.0.24-rc1) or anything else for
# a build that isn't one (0.0.0). The package goes in <folder>, named as the
# release names it, and the last line says which: on FreeBSD
# OSD-OS-<tag>-freebsd-amd64.pkg, as the release's other files are named; on
# OpenBSD OSD-OS-<version>-openbsd-amd64.tgz (OSD-OS-0.0.24-openbsd-amd64.tgz),
# the package's own name, since pkg_add takes a package's name from its
# file's and refuses one whose name inside differs, and an OpenBSD package's
# version is the part of its name that starts with a digit. The packages it
# depends on are the ones the program's own libraries come from, at the
# versions the system has where it is built (Qt keeps a program built with
# one version working with the ones after), and two it uses without linking
# them: mpv, which plays every video and whose libmpv Transparent Background
# opens, and Qt's SVG module, whose plugin draws the icons. RetroArch and
# MAME stay optional, as on every system: the app says how to install them
# where they're needed.
set -euo pipefail

usage="usage: bsd-package.sh <root> <version> <folder>"
root=$(cd "${1:?$usage}" && pwd)
tag=${2:?$usage}
folder=$(cd "${3:?$usage}" && pwd)
prefix=/usr/local
program=$root$prefix/bin/osdos
[ -x "$program" ] || { echo "no program at $program" >&2; exit 1; }
machine=$(uname -m)

# 0.0.24 for v0.0.24, 0.0.24rc1 for v0.0.24-rc1 (both systems read the rc as
# coming before the release), 0.0.0 for a build that isn't a release.
case $tag in
    v[0-9]*) version=${tag#v}; version=${version//-/} ;;
    *) version=0.0.0 ;;
esac

# One line, 60 characters at most: pkg_create refuses a longer COMMENT.
comment="VCR-style front end for films, streams and arcade games"
desc="OSD/OS turns a computer into a VCR-style smart TV for CRT and flat
screens: films from local files and Plex, Jellyfin or Emby servers, YouTube,
and arcade games through MAME or FinalBurn Neo with high scores,
RetroAchievements and online play. This package brings FinalBurn Neo's
libretro core, built from its source; RetroArch and MAME are the system's
own packages.
https://osdos.tv"

# The libraries the program links, by their files (ldd's lines differ
# between the two systems: the path is the field that is one).
linked_libraries() {
    ldd "$program" | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^\/.*\.so/) print $i }' | sort -u
}

# A JSON string's contents: backslashes and quotes escaped, lines joined
# with \n.
json() {
    printf '%s' "$1" | awk 'BEGIN { ORS = "" } { gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); print (NR > 1 ? "\\n" : "") $0 }'
}

case $(uname -s) in
FreeBSD)
    # Each package as "name origin version", for the manifest.
    packages=$(
        for library in $(linked_libraries); do
            pkg which -q "$library" 2>/dev/null || true
        done | sort -u | while read -r name; do
            [ -n "$name" ] && pkg query '%n %o %v' "$name"
        done
        pkg query '%n %o %v' mpv qt6-svg
    )
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    {
        printf '{\n'
        printf '  "name": "osdos",\n  "version": "%s",\n  "origin": "multimedia/osdos",\n' "$version"
        printf '  "comment": "%s",\n' "$(json "$comment")"
        printf '  "maintainer": "https://github.com/mehmetraif/OSD-OS",\n  "www": "https://osdos.tv",\n'
        printf '  "prefix": "%s",\n  "categories": ["multimedia", "games"],\n' "$prefix"
        printf '  "licenselogic": "and",\n  "licenses": ["GPLv3", "FBNEO"],\n'
        printf '  "desc": "%s",\n' "$(json "$desc")"
        printf '  "deps": {\n'
        sort -u <<< "$packages" | awk 'NF == 3 { printf "%s    \"%s\": { \"origin\": \"%s\", \"version\": \"%s\" }", (n++ ? ",\n" : ""), $1, $2, $3 } END { printf "\n" }'
        printf '  }\n}\n'
    } > "$work/manifest"
    # Every file, and the folders that are the package's own (@dir), which
    # pkg delete removes with them.
    (cd "$root$prefix" && {
        find . \( -type f -o -type l \) | sed 's|^\./||'
        find share/osdos lib/osdos -type d | sed 's|^|@dir |'
    } | LC_ALL=C sort) > "$work/plist"
    pkg create -M "$work/manifest" -p "$work/plist" -r "$root" -o "$work"
    out=$folder/OSD-OS-$tag-freebsd-$machine.pkg
    mv "$work"/osdos-*.pkg "$out"
    ;;
OpenBSD)
    # Each package as -P pkgpath:stem-*:name, the stem any version of it
    # meets, the name the one fetched when it is missing.
    # pkg_info -E names the package a file is in ("file: package", or the
    # package alone); a file of the base system is in none.
    packages=$(
        for library in $(linked_libraries); do
            pkg_info -qE "$library" 2>/dev/null | head -n 1 | sed -e 's/^.*: //' -e 's/ .*//' || true
        done
        pkg_info -q | grep -E '^(mpv|qt6-qtsvg)-[0-9]'
    )
    depends=()
    while read -r name; do
        [ -n "$name" ] || continue
        depends+=(-P "$(pkg_info -qP "$name" | tail -n 1):${name%%-[0-9]*}-*:$name")
    done < <(sort -u <<< "$packages")
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    # The description in a file: pkg_create takes it from the argument
    # itself (-d -text) only when it is one line.
    printf '%s\n' "$desc" > "$work/desc"
    # The folders that are the package's own, then every file, each folder
    # before what is in it.
    (cd "$root$prefix" && {
        find share/osdos lib/osdos -type d | sed 's|$|/|'
        find . \( -type f -o -type l \) | sed 's|^\./||'
    } | LC_ALL=C sort) > "$work/plist"
    # The name: stem OSD-OS, the version, and the system and machine as its
    # flavours, which is the file's name too. pkg_create writes the file its
    # argument names, the package's name that without .tgz.
    name=OSD-OS-$version-openbsd-$machine
    (cd "$work" && pkg_create -A "$machine" -B "$root" -p "$prefix" -f "$work/plist" \
        -D COMMENT="$comment" -D FULLPKGPATH=multimedia/osdos -D HOMEPAGE=https://osdos.tv \
        -D MAINTAINER=https://github.com/mehmetraif/OSD-OS -d "$work/desc" "${depends[@]}" "$name.tgz")
    out=$folder/$name.tgz
    mv "$work/$name.tgz" "$out"
    ;;
*)
    echo "not a BSD: $(uname -s)" >&2
    exit 1
    ;;
esac
# The file's own size: du counts the disk blocks written so far, which ZFS
# (FreeBSD's) reports late for a file just made.
echo "$out: osdos $version, $(( $(wc -c < "$out") / 1024 )) KB"
