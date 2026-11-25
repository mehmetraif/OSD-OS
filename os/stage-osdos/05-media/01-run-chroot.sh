#!/bin/bash -e

# Runs inside the image. The film partition is mounted for the user the app runs
# as, since exFAT keeps no owners of its own; a card too small for one leaves
# the folder on the root filesystem, where it belongs to that user too. It is
# writable: films are copied onto it from a computer, and the Playlists module
# downloads its offline playlists' videos into it (flushing each one to the
# card as it completes). A Pi switched off at the wall mid-download can still
# leave it untidy, so it is checked before it is mounted when it is
# (osdos-media-check.service, see 00-run.sh, which reads the flag exFAT keeps
# for it): no pass here, which would run a full fsck.exfat at every boot.
# nofail and the short device timeout keep a boot without it from waiting. It
# only ever holds media, and anyone can write it from a computer, so nothing
# on it can run (noexec, nosuid, nodev).

USER_ID=$(id -u "${FIRST_USER_NAME}")
GROUP_ID=$(id -g "${FIRST_USER_NAME}")
chown "${USER_ID}:${GROUP_ID}" /media/OSD-OS

if ! grep -q '/media/OSD-OS' /etc/fstab; then
	echo "LABEL=OSD-OS  /media/OSD-OS  exfat  rw,noexec,nosuid,nodev,nofail,noatime,uid=${USER_ID},gid=${GROUP_ID},umask=0022,x-systemd.device-timeout=3s  0  0" >> /etc/fstab
fi
