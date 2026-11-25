#!/bin/bash -e

# Runs inside the image. Which services run at all: the app, and nothing an
# appliance plugged into a TV doesn't need.

# The sockets the app asks root through (00-run.sh): all of them.
sockets=(/etc/systemd/system/osdos-*.socket)
systemctl enable osdos.service osdos-cloud-init-once.service osdos-jamma.service "${sockets[@]##*/}" \
	osdos-setup-app.service osdos-setup.service osdos-netplay-priority.service osdos-firewall.service

# Built without a password (os/build.sh): the app still runs as this user, but
# nobody logs in as it. Raspberry Pi Imager's OS customisation creates the
# user people log in with.
if [ "${OSDOS_LOCK_FIRST_USER:-0}" = "1" ]; then
	passwd -l "${FIRST_USER_NAME}"
fi

# tty1 belongs to the app; "Exit to Terminal" starts osdos-terminal.service.
# The Pi 5's debug UART (ttyAMA10) offers no login either: nobody plugs a
# serial lead into an appliance.
systemctl mask getty@tty1.service autovt@.service serial-getty@ttyAMA10.service

# Raspberry Pi OS's switches on the boot partition, which anyone with the card
# can write: an empty file named ssh (sshswitch.service) turns SSH on with
# whatever password the card has, and userconf.txt (userconfig.service) sets
# a user and its password. OSD/OS Setup's file does both, with its own rules
# (os/stage-osdos/02-system/files/osdos-setup). Masked whether or not this
# release of Raspberry Pi OS has them: masking a unit that isn't there only
# leaves a link to /dev/null in its place.
systemctl mask sshswitch.service userconfig.service

# sudo asks for the password, always: Exit to Terminal logs pi in by itself
# on an image built without one, and that shell must not be root's. pi-gen
# has given the first user a passwordless rule (010_pi-nopasswd), and
# cloud-init writes one for the users it creates (90-cloud-init-users, which
# osdos-cloud-init-once.service takes the NOPASSWD out of after the first
# boot). The build stops if any rule without a password is left.
rm -f /etc/sudoers.d/010_pi-nopasswd /etc/sudoers.d/90-cloud-init-users
if grep -rIlE '^[^#]*NOPASSWD' /etc/sudoers /etc/sudoers.d 2> /dev/null; then
	echo "The files above still let someone use sudo without a password: the image would give root away." >&2
	exit 1
fi

# udisks2: nothing on the image asks it for anything. USB drives are mounted
# by OSD/OS's own rule (stage 07-usb), and there is no desktop. Masked, as
# D-Bus would otherwise start it the moment anything looked for it.
systemctl mask udisks2.service

# Settings → Network → Bluetooth talks to BlueZ as this user, which BlueZ's D-Bus policy
# lets in through the bluetooth group.
if getent group bluetooth > /dev/null; then
	usermod -aG bluetooth "${FIRST_USER_NAME}"
fi

# bluetoothd without the profiles for sound, networking and the rest nothing
# here uses: OSD/OS pairs keyboards, gamepads and remotes, which input and hog
# (HID) serve, with battery and deviceinfo, and plays no sound over Bluetooth.
# Fewer profiles, less of BlueZ that a device in range can reach. Written from
# the packaged unit's own ExecStart line, so the daemon's path is never
# guessed; a unit without exactly one, or a drop-in that sets its own, leaves
# bluetoothd as it is. --noplugin ignores names this BlueZ hasn't.
NOPLUGIN=a2dp,avrcp,network,sap,bap,bass,mcp,vcp,micp,ccp,csip,asha,midi
DROPIN=/etc/systemd/system/bluetooth.service.d/osdos-noplugin.conf
rm -f "${DROPIN}"
start=$(grep '^ExecStart=.' /usr/lib/systemd/system/bluetooth.service 2> /dev/null || true)
if [ "$(printf '%s' "${start}" | grep -c '^')" = 1 ] && [ "${start#*--noplugin}" = "${start}" ] \
		&& ! cat /usr/lib/systemd/system/bluetooth.service.d/*.conf \
			/etc/systemd/system/bluetooth.service.d/*.conf 2> /dev/null | grep -q '^ExecStart='; then
	install -d "${DROPIN%/*}"
	cat > "${DROPIN}" << EOF
# OSD/OS image: bluetoothd without the audio and networking profiles; HID, battery and device information stay.
[Service]
ExecStart=
${start} --noplugin=${NOPLUGIN}
EOF
else
	echo "bluetooth.service's ExecStart couldn't be read: bluetoothd keeps every plugin" >&2
fi

# Raspberry Pi Connect's remote-access agent: not wanted on an appliance.
if dpkg -s rpi-connect-lite > /dev/null 2>&1; then
	apt-get purge -y rpi-connect-lite
fi

# Periodic jobs that wake the CPU and the SD card at random times, mid-movie
# included. Updating the system is the image's business, not a timer's.
# fstrim.timer too: its weekly trim, caught up soon after a boot that missed
# it (Persistent=true), can hold the card's writes for seconds, as a game
# starts; `sudo fstrim -av` trims by hand.
for unit in apt-daily.timer apt-daily-upgrade.timer man-db.timer \
            e2scrub_all.timer dpkg-db-backup.timer fstrim.timer cron.service; do
	# is-enabled works offline in the chroot; it fails for a unit that isn't installed.
	if systemctl is-enabled "${unit}" > /dev/null 2>&1; then
		systemctl disable "${unit}"
	fi
done
