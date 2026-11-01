#!/bin/bash -e

# The Arcade module's engines in the image, at the versions every OSD/OS
# machine plays with (scripts/engines.env, which os/build.sh puts in
# files/):
#
# - FinalBurn Neo comes with the app (01-app): both of OSD/OS's cores, built
#   from the commit engines.env names, with FinalBurn Neo's list of arcade
#   games and its licence, in /opt/osdos/lib/osdos and
#   /opt/osdos/share/osdos/fbneo. An update of the app brings them along.
# - MAME and RetroArch are Debian trixie's (00-packages-nr). Their versions
#   are the lowest any distribution of OSD/OS has, which engines.env pins
#   every machine to. A Debian whose versions have risen stops the build
#   here: engines.env rises with them (.github/workflows/engines.yml), and
#   the Windows downloads follow.
#
# FinalBurn Neo's licence lets it be shared with its text, which goes with it,
# but never sold, nor used by a project that asks for donations; OSD/OS does
# neither, and no ROMs come with it.

# shellcheck source=/dev/null
. files/engines.env

for file in lib/osdos/fbneo_libretro.so lib/osdos/fbneo_lan_libretro.so \
	share/osdos/fbneo/fbneo-arcade.dat share/osdos/fbneo/license.txt; do
	if [ ! -s "${ROOTFS_DIR}/opt/osdos/${file}" ]; then
		echo "The app's tarball has no ${file}: FinalBurn Neo's cores come with the app" >&2
		exit 1
	fi
done

# Debian's version of a package, without its epoch, Debian's own changes and
# repacking: 1.20.0 of 1.20.0+dfsg-2, 0.276 of 0.276+dfsg.1-1.
upstream() {
	on_chroot <<EOF | tail -n1 | sed -e 's/^[0-9]*://' -e 's/[+~-].*$//'
dpkg-query -W -f '\${Version}\n' "$1"
EOF
}
retroarch=$(upstream retroarch)
mame=$(upstream mame)
if [ "${retroarch}" != "${RETROARCH_VERSION}" ] || [ "${mame}" != "${MAME_VERSION}" ]; then
	echo "Debian has RetroArch ${retroarch} and MAME ${mame}; scripts/engines.env pins" \
		"RetroArch ${RETROARCH_VERSION} and MAME ${MAME_VERSION}. Raise engines.env to Debian's" \
		"versions so that every machine plays with them." >&2
	exit 1
fi
echo "RetroArch ${retroarch}, MAME ${mame} and FinalBurn Neo ${FBNEO_COMMIT:0:7}, as engines.env pins them"
