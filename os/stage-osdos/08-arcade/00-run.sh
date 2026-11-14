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
# - The RetroArch the games play in comes with the app too, beside the
#   cores (/opt/osdos/lib/osdos/retroarch, scripts/build-retroarch.sh): the
#   same version, built with TLS, where Debian's has none and asks
#   RetroAchievements over plain HTTP. Debian's package stays for what it
#   brings with it: the assets RetroArch's menu reads and every library the
#   app's own RetroArch needs. It is started here, in the image, to see that
#   it runs on them and is the version pinned.
#
# FinalBurn Neo's licence lets it be shared with its text, which goes with it,
# but never sold, nor used by a project that asks for donations; OSD/OS does
# neither, and no ROMs come with it.

# shellcheck source=/dev/null
. files/engines.env

for file in lib/osdos/fbneo_libretro.so lib/osdos/osdos_lan_libretro.so \
	share/osdos/fbneo/fbneo-arcade.dat share/osdos/fbneo/license.txt \
	lib/osdos/retroarch share/osdos/retroarch/licenses.txt; do
	if [ ! -s "${ROOTFS_DIR}/opt/osdos/${file}" ]; then
		echo "The app's tarball has no ${file}: FinalBurn Neo's cores and RetroArch come with the app" >&2
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

# The app's own RetroArch, on the image's libraries: its version as it says
# it ("Version: 1.20.0 (Git ab3b175)").
own=$(on_chroot <<EOF | grep -a '^Version: ' | tail -n1
/opt/osdos/lib/osdos/retroarch --version 2>&1 || true
EOF
)
case "${own}" in
	"Version: ${RETROARCH_VERSION} (Git ${RETROARCH_COMMIT:0:7})"*) ;;
	*)
		echo "The app's own RetroArch does not start in the image as ${RETROARCH_VERSION}" \
			"(${RETROARCH_COMMIT:0:7}): '${own}'" >&2
		on_chroot <<EOF >&2 || true
/opt/osdos/lib/osdos/retroarch --version
EOF
		exit 1
		;;
esac
if [ ! -s "${ROOTFS_DIR}/etc/ssl/certs/ca-certificates.crt" ]; then
	echo "The image has no /etc/ssl/certs/ca-certificates.crt, which the app's own RetroArch checks" \
		"RetroAchievements' certificate against" >&2
	exit 1
fi
echo "RetroArch ${retroarch} (the app's own with TLS, ${own#Version: }), MAME ${mame} and" \
	"FinalBurn Neo ${FBNEO_COMMIT:0:7}, as engines.env pins them"
