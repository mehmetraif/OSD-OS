#!/bin/bash -e

# The film partition: on the first boot the root partition grows to
# OSDOS_ROOT_SIZE GiB instead of to the end of the card, and the rest of the
# card becomes a partition of its own in exFAT, labelled OSD-OS (see
# files/resize_early). Windows and macOS open it too, so films copied into its
# Movies folder from a computer show up in Local Files, which opens that folder
# by default. os/build.sh skips this sub-stage with OSDOS_ROOT_SIZE=0.

ROOT_MIB=$(( ${OSDOS_ROOT_SIZE:-8} * 1024 ))
ETC="${ROOTFS_DIR}/etc"

# In place of raspberrypi-sys-mods' own resize_early: a script of the same name
# in /etc/initramfs-tools/scripts overrides the one in /usr/share. The
# initramfs is built at the end, by pi-gen's export-image stage.
install -d "${ETC}/initramfs-tools/scripts/local-premount" "${ETC}/initramfs-tools/hooks"
sed "s/@ROOT_MIB@/${ROOT_MIB}/" files/resize_early \
	> "${ETC}/initramfs-tools/scripts/local-premount/resize_early"
chmod 755 "${ETC}/initramfs-tools/scripts/local-premount/resize_early"
install -m 755 files/osdos-media "${ETC}/initramfs-tools/hooks/osdos-media"

install -d "${ROOTFS_DIR}/media/OSD-OS"

install -d "${ETC}/systemd/system/osdos.service.d"
cat > "${ETC}/systemd/system/osdos.service.d/osdos-media.conf" << 'CONF'
# OSD/OS image: Local Files opens Movies on the film partition by default.
[Service]
Environment=OSDOS_MEDIA_DIR=/media/OSD-OS
CONF

# Movies made on the partition each time it is mounted, wanted by its mount
# unit, which systemd-fstab-generator makes from the fstab line.
UNITS="${ETC}/systemd/system"
install -m 644 files/osdos-media-folders.service "${UNITS}/"
sed -i "s/@FIRST_USER_NAME@/${FIRST_USER_NAME}/g" "${UNITS}/osdos-media-folders.service"
install -d "${UNITS}/media-OSD\\x2dOS.mount.wants"
ln -sf /etc/systemd/system/osdos-media-folders.service "${UNITS}/media-OSD\\x2dOS.mount.wants/"
