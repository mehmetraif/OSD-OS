#!/usr/bin/env bash
# RetroArch for OSD/OS's Linux arm64 package (the OSD/OS image, and a
# Raspberry Pi OS it is installed on), built from libretro's own source at
# the commit scripts/engines.env names: the tag of the version every machine
# plays with. Debian's package of that version is built without TLS, so that
# RetroAchievements' password and token cross the network in the clear
# (http://retroachievements.org). This one is built with it, and plays over
# https.
#
#   scripts/build-retroarch.sh <RetroArch source folder> <folder for the program> \
#     <file for the licences>
#
# The source is the commit named, unpacked (github.com/libretro/RetroArch's
# archive of it). Nothing of RetroArch's code is changed, and it says the
# version Debian's says, so machines play together as before.
#
# The TLS is BearSSL's, which comes in RetroArch's source: it checks the
# server's certificate against the system's (/etc/ssl/certs/ca-certificates.crt),
# with its name and its dates, and hangs up on one that fails. RetroArch's
# other TLS, mbedTLS, which libretro's own builds have, asks for the
# certificate and takes whichever comes (tests/retroarch_tls_test.sh tries
# both ways).
#
# The rest is what Debian's package is built with (its debian/rules), but:
# - without FFmpeg (recording and streaming), the Qt desktop menu, JACK,
#   PipeWire, Video4Linux, libusb, Discord and xdelta patches, which OSD/OS
#   asks nothing of;
# - FLAC, glslang and SPIRV-Cross from RetroArch's own source, built in,
#   rather than the system's libraries.
# The program then needs only libraries whose names are the same on the
# system it is built on and on the one it runs on, all of which Debian's
# retroarch package brings: the list below, which the build is held to.
#
# The licences file is RetroArch's (GPL-3.0) and those of the libraries built
# into it from its source, each under its own.
set -euo pipefail

usage="usage: build-retroarch.sh <RetroArch source folder> <folder for the program> <file for the licences>"
src=${1:?$usage}
out=${2:?$usage}
licenses=${3:?$usage}
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source-path=SCRIPTDIR source=engines.env
. "$here/engines.env"
version=$RETROARCH_VERSION
commit=$RETROARCH_COMMIT

make=$(command -v gmake || command -v make)
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

# Debian's own, by processor.
case "$(uname -m)" in
    x86_64 | i?86) processor=(--enable-sse) ;;
    aarch64 | arm64) processor=(--disable-cg --enable-opengles) ;;
    arm*) processor=(--disable-cg --enable-opengles --enable-neon --enable-floathard) ;;
    *) processor=() ;;
esac

mkdir -p "$out" "$(dirname "$licenses")"
out=$(cd "$out" && pwd)
licenses=$(cd "$(dirname "$licenses")" && pwd)/$(basename "$licenses")

cd "$src"
# What must be there is asked for by name: configure then stops at a missing
# library, where left to itself it would build without the feature.
LDFLAGS="${LDFLAGS:-} -Wl,--as-needed" ./configure --prefix=/usr \
    --enable-ssl --enable-builtinbearssl --disable-builtinmbedtls --disable-systemmbedtls \
    --enable-builtinflac --disable-builtinzlib --enable-zlib \
    --disable-ffmpeg --disable-ssa --disable-qt --disable-jack --disable-pipewire \
    --disable-v4l2 --disable-libusb --disable-discord --disable-xdelta \
    --disable-sixel --disable-caca --disable-libdecor --disable-mpv --disable-al \
    --disable-update_assets --disable-oss --disable-vg \
    --enable-dbus --enable-vulkan --enable-kms --enable-egl --enable-x11 --enable-wayland \
    --enable-alsa --enable-pulse --enable-udev --enable-sdl2 --enable-freetype \
    --enable-threads --enable-networking \
    "${processor[@]}"
"$make" -j"$jobs" GIT_VERSION="${commit:0:7}"

cp retroarch "$out/retroarch"
strip "$out/retroarch"

# It is the version named, and BearSSL's TLS is in it: the file of the
# system's certificates it reads is named nowhere else in RetroArch. That it
# asks RetroAchievements over https, and whom it trusts, is for
# tests/retroarch_tls_test.sh.
said=$("$out/retroarch" --version 2>&1 || true)
case "$said" in
    *"$version"*) ;;
    *) echo "the program built does not say it is RetroArch $version: $said" >&2; exit 1 ;;
esac
if ! grep -a -q -F "/etc/ssl/certs/ca-certificates.crt" "$out/retroarch"; then
    echo "the program built has no BearSSL: no TLS, or one that checks no certificate" >&2
    exit 1
fi

# The libraries it may need: the C and C++ libraries, and those of Debian's
# retroarch package whose names don't change from one Debian or Ubuntu to
# the next.
allowed=" ld-linux-aarch64.so.1 ld-linux-x86-64.so.2 ld-linux-armhf.so.3 libc.so.6 libm.so.6 libmvec.so.1
 libdl.so.2 libpthread.so.0 librt.so.1 libgcc_s.so.1 libstdc++.so.6 libz.so.1
 libEGL.so.1 libGL.so.1 libGLESv2.so.2 libdrm.so.2 libgbm.so.1
 libX11.so.6 libX11-xcb.so.1 libxcb.so.1 libXext.so.6 libXxf86vm.so.1 libXinerama.so.1 libXrandr.so.2 libXss.so.1
 libxkbcommon.so.0 libwayland-client.so.0 libwayland-cursor.so.0 libwayland-egl.so.1
 libasound.so.2 libpulse.so.0 libSDL2-2.0.so.0 libudev.so.1 libdbus-1.so.3
 libfreetype.so.6 libfontconfig.so.1 "
other=""
for library in $(readelf -d "$out/retroarch" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
    case "$allowed" in
        *" $library "* | *" $library"$'\n'*) ;;
        *) other="$other $library" ;;
    esac
done
if [ -n "$other" ]; then
    echo "the program built needs libraries the package can't count on:$other" >&2
    exit 1
fi

# RetroArch's licence, then those of the libraries built in from its source
# (the folders make left objects in), each as its own folder has it.
{
    echo "RetroArch $version, built by OSD/OS (scripts/build-retroarch.sh) from"
    echo "https://github.com/libretro/RetroArch at commit $commit, unchanged."
    echo
    echo "RetroArch is under the GNU General Public License, version 3, which follows."
    echo "The libraries built into it from its source come after it, each under its"
    echo "own licence."
    echo
    echo "================================================================================"
    echo "RetroArch"
    echo "================================================================================"
    echo
    cat COPYING
    unnamed=""
    for built in obj-unix/release/deps/*/; do
        [ -d "$built" ] || continue
        name=$(basename "$built")
        found=""
        while IFS= read -r file; do
            found=1
            echo
            echo "================================================================================"
            echo "$name ($file)"
            echo "================================================================================"
            echo
            cat "$file"
        done < <(find "deps/$name" -maxdepth 3 -type f \( -iname 'LICEN[CS]E*' -o -iname 'COPYING*' \) | LC_ALL=C sort)
        [ -n "$found" ] || unnamed="$unnamed $name"
    done
    if [ -n "$unnamed" ]; then
        echo
        echo "================================================================================"
        echo "Built in too, with their notices in their own source files (deps/ in"
        echo "RetroArch's source):$unnamed"
        echo "================================================================================"
    fi
} > "$licenses"

echo "RetroArch $version (${commit:0:7}): TLS with BearSSL, $(( $(wc -c < "$out/retroarch") / 1024 )) KB"
