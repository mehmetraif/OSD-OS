#!/usr/bin/env bash
# What the FreeBSD and OpenBSD build workflows (.github/workflows/freebsd.yml,
# openbsd.yml) run in their virtual machine, once the system's packages are
# installed: the app built and installed into stage/, the regression tests
# built and run, FinalBurn Neo's two cores built into fbneo/ from
# fbneo-src.tar.gz (unless a run before kept them there), the system's
# package made (scripts/bsd-package.sh), then installed as a user installs
# it, the cores loaded and the app started from it, and removed again.
#
#   scripts/bsd-ci.sh <version>
#
# The version is the release's tag, or dev (a package of version 0.0.0). It
# runs in the repository's root, where the workflow downloads FinalBurn
# Neo's source and leaves the package.
set -euo pipefail

usage="usage: bsd-ci.sh <version>"
version=${1:?$usage}
cd "$(dirname "$0")/.."

system=$(uname -s)
case "$system" in
    FreeBSD | OpenBSD) ;;
    *) echo "bsd-ci.sh: for FreeBSD or OpenBSD, not $system" >&2; exit 1 ;;
esac

if [ "$system" = OpenBSD ]; then
    # FinalBurn Neo's larger files take more memory to compile than
    # a process may have by default.
    ulimit -d unlimited 2> /dev/null || true
fi
uname -a
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/local \
  -DCMAKE_INSTALL_PREFIX=/usr/local -DAPP_VERSION="$version"
cmake --build build --parallel
DESTDIR="$PWD/stage" cmake --install build
cmake -S tests -B build-tests -G Ninja -DCMAKE_PREFIX_PATH=/usr/local
cmake --build build-tests --parallel
status=0
QT_QPA_PLATFORM=offscreen ctest --test-dir build-tests --output-on-failure --timeout 300 || status=$?
# A handler or a deferred call that ran on a view its Loader had
# let go: a warning on Linux, a crash on OpenBSD, so any is listed.
grep -n "invalid context\|delayed function evaluation" build-tests/Testing/Temporary/LastTest.log || true
[ "$status" -eq 0 ]

# FinalBurn Neo's source unpacked into the folder $1 without the archive's
# own top folder: OpenBSD's tar has no --strip-components, but rewrites
# each name (-s).
unpack_fbneo() {
    mkdir -p "$1"
    if [ "$system" = OpenBSD ]; then
        tar -xzf fbneo-src.tar.gz -C "$1" -s '|^[^/]*/||'
    else
        tar -xzf fbneo-src.tar.gz -C "$1" --strip-components 1
    fi
}

# The list of games is the last thing the scripts write. Both
# cores from the commit scripts/engines.env names, each from a
# copy of its own: the LAN core's patch goes in its copy.
if [ ! -f fbneo/share/osdos/fbneo/fbneo-arcade.dat ]; then
    unpack_fbneo fbneo-src
    unpack_fbneo fbneo-lan-src
    bash scripts/build-fbneo-lan.sh fbneo-lan-src fbneo/lib/osdos fbneo/share/osdos/fbneo/license.txt
    bash scripts/build-fbneo.sh fbneo-src fbneo/lib/osdos fbneo/share/osdos/fbneo/fbneo-arcade.dat \
      fbneo/share/osdos/fbneo/license.txt
fi
tar -C fbneo -cf - . | tar -C stage/usr/local -xf -
bash scripts/bsd-package.sh stage "$version" .

case "$system" in
    FreeBSD)
        package=$(ls OSD-OS-*-freebsd-amd64.pkg)
        # Installed as a user installs it, with mpv, which none of the
        # program's libraries brings, taken away first.
        pkg delete -y mpv
        if pkg install -y "./$package"; then
            echo "pkg install brought the package and what it needs"
        else
            echo "pkg install can't take the file: its packages first, then pkg add"
            pkg install -y mpv
            pkg add "./$package"
        fi
        pkg info osdos ;;
    OpenBSD)
        package=$(ls OSD-OS-*-openbsd-amd64.tgz)
        # Installed as a user installs it, with mpv, which none of the
        # program's libraries brings, taken away first: pkg_add fetches
        # what the package needs from the mirror. The package isn't
        # signed with OpenBSD's key (-D unsigned). Its stem is OSD-OS.
        pkg_delete mpv
        pkg_add -I -D unsigned "./$package"
        pkg_info OSD-OS ;;
esac
/usr/local/bin/osdos --core-info /usr/local/lib/osdos/fbneo_libretro.so
lan=$(/usr/local/bin/osdos --core-info /usr/local/lib/osdos/osdos_lan_libretro.so)
echo "$lan"
grep -qx 'name=OSD-OS LAN Core' <<< "$lan"
bash tests/app_smoke_test.sh /usr/local/bin/osdos /usr/local/share/osdos /usr/local/lib/qt6/qml

case "$system" in
    FreeBSD) pkg delete -y osdos ;;
    OpenBSD) pkg_delete OSD-OS ;;
esac
# Removed whole.
for path in /usr/local/bin/osdos /usr/local/lib/osdos /usr/local/share/osdos; do
    if [ -e "$path" ]; then
        echo "$path is left after the package is removed" >&2
        exit 1
    fi
done
