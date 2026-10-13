#!/bin/bash -e

# Boot partition: a quiet boot straight to the app, with the display preset in
# a file of its own next to config.txt, a copy of one of the presets beside it:
# Settings → Display Output switches between them (as root, through
# osdos-stop), and from any computer it is a file copy.

BOOT="${ROOTFS_DIR}/boot/firmware"
DISPLAY_PRESET="${OSDOS_DISPLAY:-hdmi}"
if [ ! -f "files/osdos-display-${DISPLAY_PRESET}.txt" ]; then
	echo "OSDOS_DISPLAY must name a preset in os/stage-osdos/03-boot/files (got ${DISPLAY_PRESET})" >&2
	exit 1
fi

install -m 644 files/config.txt "${BOOT}/config.txt"
install -m 644 files/osdos-display-*.txt "${BOOT}/"
install -m 644 "files/osdos-display-${DISPLAY_PRESET}.txt" "${BOOT}/osdos-display.txt"
# A Pi 5's composite sync on GPIO 1 for SCART RGB (its presets name the DPI
# output), as osdos-stop writes it on a change: drm-rp1-dpi reads it as it loads.
# Not for a JAMMA board, which makes its own and has its I2C there.
if grep -q '^# osdos-output: DPI' "${BOOT}/osdos-display.txt" \
		&& ! grep -q '^# osdos-sync: separate' "${BOOT}/osdos-display.txt"; then
	install -d "${ROOTFS_DIR}/etc/modprobe.d"
	echo "options drm_rp1_dpi force_csync=1" > "${ROOTFS_DIR}/etc/modprobe.d/osdos-display.conf"
fi

# The JAMMA preset's overlay for a Pi 5 (osdos-jamma-dpi.dts: its DPI without
# GPIO 0 and 1), compiled in the image with the device tree compiler
# (00-packages-nr), as the firmware loads overlays only compiled.
install -m 644 files/osdos-jamma-dpi.dts "${ROOTFS_DIR}/tmp/osdos-jamma-dpi.dts"
on_chroot << EOF
dtc -@ -q -I dts -O dtb -o /boot/firmware/overlays/osdos-jamma-dpi.dtbo /tmp/osdos-jamma-dpi.dts
rm /tmp/osdos-jamma-dpi.dts
EOF

# Kernel messages go to tty3 instead of tty1, with no logo and no cursor, so the
# screen stays black until the app draws its boot screen.
if ! grep -q 'vt.global_cursor_default=0' "${BOOT}/cmdline.txt"; then
	sed -i -e 's/console=tty1/console=tty3/' \
		-e 's/$/ quiet loglevel=3 logo.nologo vt.global_cursor_default=0 consoleblank=0/' \
		"${BOOT}/cmdline.txt"
fi
