#!/bin/bash -e

# FinalBurn Neo for RetroArch, the Arcade module's second engine: the core from
# libretro's build server, and its list of arcade games and its licence from
# FinalBurn Neo's sources, where the module looks for them
# (src/modules/arcade/Libretro.cpp). libretro builds the core every night and
# keeps no dated builds, so the image takes the latest and notes which (its
# date and CRC, from the build server's index) beside the licence.
#
# FinalBurn Neo's licence lets it be shared with its text, which goes with it,
# but never sold, nor used by a project that asks for donations; OSD/OS does
# neither, and no ROMs come with it.
BUILDBOT=https://buildbot.libretro.com/nightly/linux/aarch64/latest
FBNEO=https://raw.githubusercontent.com/libretro/FBNeo/master

DOWNLOADS=$(mktemp -d)
trap 'rm -rf "${DOWNLOADS}"' EXIT

curl -fsSL --retry 3 -o "${DOWNLOADS}/fbneo.zip" "${BUILDBOT}/fbneo_libretro.so.zip"
curl -fsSL --retry 3 -o "${DOWNLOADS}/index" "${BUILDBOT}/.index-extended"
curl -fsSL --retry 3 -o "${DOWNLOADS}/fbneo-arcade.dat" \
	"${FBNEO}/dats/FinalBurn%20Neo%20(ClrMame%20Pro%20XML%2C%20Arcade%20only).dat"
curl -fsSL --retry 3 -o "${DOWNLOADS}/license.txt" "${FBNEO}/src/license.txt"
# bsdtar checks each file's CRC as it unpacks it.
bsdtar -xf "${DOWNLOADS}/fbneo.zip" -C "${DOWNLOADS}" fbneo_libretro.so
# The list with its games in it, not a page of an error.
test "$(grep -c '<game name=' "${DOWNLOADS}/fbneo-arcade.dat")" -gt 1000

install -D -m 644 "${DOWNLOADS}/fbneo_libretro.so" "${ROOTFS_DIR}/usr/local/lib/libretro/fbneo_libretro.so"
install -D -m 644 "${DOWNLOADS}/fbneo-arcade.dat" "${ROOTFS_DIR}/usr/local/share/libretro/fbneo/fbneo-arcade.dat"
install -D -m 644 "${DOWNLOADS}/license.txt" "${ROOTFS_DIR}/usr/local/share/doc/fbneo/license.txt"
grep ' fbneo_libretro\.so\.zip$' "${DOWNLOADS}/index" > "${ROOTFS_DIR}/usr/local/share/doc/fbneo/BUILD"
