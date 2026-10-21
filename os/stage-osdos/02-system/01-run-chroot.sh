#!/bin/bash -e

# Runs inside the image. Which services run at all: the app, and nothing an
# appliance plugged into a TV doesn't need.

systemctl enable osdos.service osdos-cloud-init-once.service osdos-jamma.service osdos-display.socket osdos-wifi.socket osdos-name.socket \
	osdos-setup-app.service osdos-setup.service

# Built without a password (os/build.sh): the app still runs as this user, but
# nobody logs in as it. Raspberry Pi Imager's OS customisation creates the
# user people log in with.
if [ "${OSDOS_LOCK_FIRST_USER:-0}" = "1" ]; then
	passwd -l "${FIRST_USER_NAME}"
fi

# tty1 belongs to the app; "Exit to Terminal" starts osdos-terminal.service.
systemctl mask getty@tty1.service autovt@.service

# Settings → Bluetooth talks to BlueZ as this user, which BlueZ's D-Bus policy
# lets in through the bluetooth group.
if getent group bluetooth > /dev/null; then
	usermod -aG bluetooth "${FIRST_USER_NAME}"
fi

# Raspberry Pi Connect's remote-access agent: not wanted on an appliance.
if dpkg -s rpi-connect-lite > /dev/null 2>&1; then
	apt-get purge -y rpi-connect-lite
fi

# Periodic jobs that wake the CPU and the SD card at random times, mid-movie
# included. Updating the system is the image's business, not a timer's.
for unit in apt-daily.timer apt-daily-upgrade.timer man-db.timer \
            e2scrub_all.timer dpkg-db-backup.timer cron.service; do
	# is-enabled works offline in the chroot; it fails for a unit that isn't installed.
	if systemctl is-enabled "${unit}" > /dev/null 2>&1; then
		systemctl disable "${unit}"
	fi
done
