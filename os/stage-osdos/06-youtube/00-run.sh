#!/bin/bash -e

# YouTube: yt-dlp, which the module searches YouTube with and mpv finds a
# video's streams with, and Deno, the JavaScript runtime yt-dlp needs to answer
# YouTube's challenges (https://github.com/yt-dlp/yt-dlp/wiki/EJS); without one
# a video plays in few formats or none. os/build.sh skips this sub-stage with
# OSDOS_YOUTUBE=0.

# yt-dlp from its nightly builds, the channel its README recommends for regular
# use: the monthly stable release is often already broken by a change on
# YouTube's side. Not pinned, since a pinned one would soon stop working; it
# updates itself (osdos-yt-dlp-update.timer), so it goes where the app looks
# first (src/util/YtDlpLocator.h), the data directory of the user the app runs
# as, where that user can replace it.
YTDLP_URL=https://github.com/yt-dlp/yt-dlp-nightly-builds/releases/latest/download
# Deno, pinned like pi-gen: bump deliberately, with files/deno-LICENSE.md from
# the same tag.
DENO_VERSION=v2.9.7
DENO_SHA256=c832298b1ad4422481334855f6003e0f54145762c5a134f20a489511d2f65bbf

DOWNLOADS=$(mktemp -d)
trap 'rm -rf "${DOWNLOADS}"' EXIT

# A download tried again on any failure, a connection reset among them (curl's
# --retry alone tries again after a timeout or a 5xx), and named when it fails
# for good.
fetch() {
	curl -fsSL --retry 5 --retry-all-errors --retry-delay 3 -o "$1" "$2" \
		|| { echo "Could not download $2" >&2; exit 1; }
}

fetch "${DOWNLOADS}/yt-dlp" "${YTDLP_URL}/yt-dlp"
fetch "${DOWNLOADS}/SHA2-256SUMS" "${YTDLP_URL}/SHA2-256SUMS"
( cd "${DOWNLOADS}" && grep ' yt-dlp$' SHA2-256SUMS | sha256sum -c - )

fetch "${DOWNLOADS}/deno.zip" \
	"https://github.com/denoland/deno/releases/download/${DENO_VERSION}/deno-aarch64-unknown-linux-gnu.zip"
echo "${DENO_SHA256}  ${DOWNLOADS}/deno.zip" | sha256sum -c -
bsdtar -xf "${DOWNLOADS}/deno.zip" -C "${DOWNLOADS}" deno

DATA_BIN="/home/${FIRST_USER_NAME}/.local/share/OSD-OS/bin"
install -d "${ROOTFS_DIR}${DATA_BIN}"
install -m 755 "${DOWNLOADS}/yt-dlp" "${ROOTFS_DIR}${DATA_BIN}/yt-dlp"
# On the PATH of the app's service, where yt-dlp looks for it, with its
# licence (MIT, which asks for it to go with every copy): Deno's LICENSE.md at
# the pinned version, kept in files/.
install -m 755 "${DOWNLOADS}/deno" "${ROOTFS_DIR}/usr/local/bin/deno"
install -D -m 644 files/deno-LICENSE.md "${ROOTFS_DIR}/usr/local/share/doc/deno/LICENSE.md"

UNITS="${ROOTFS_DIR}/etc/systemd/system"
install -m 644 files/osdos-yt-dlp-update.service files/osdos-yt-dlp-update.timer "${UNITS}/"
sed -i "s/@FIRST_USER_NAME@/${FIRST_USER_NAME}/g" "${UNITS}/osdos-yt-dlp-update.service"

on_chroot << CHROOT
chown -R "${FIRST_USER_NAME}:" "/home/${FIRST_USER_NAME}/.local"
systemctl enable osdos-yt-dlp-update.timer
CHROOT
