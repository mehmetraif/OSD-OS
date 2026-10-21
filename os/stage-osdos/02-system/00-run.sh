#!/bin/bash -e

# The app's service, its exit-to-terminal shell, its stop helper, Display
# Output's next-start socket, Settings → Wi-Fi's country socket, System
# Information's device name socket and the polkit rule that lets the app's
# user use NetworkManager (all but the service taken from scripts/install.sh
# by os/build.sh), and the boot order: the units below hold back until the
# app is on screen, then start one after another.

ETC="${ROOTFS_DIR}/etc"
LIB="${ROOTFS_DIR}/usr/lib/osdos"

install -m 644 files/osdos.service "${ETC}/systemd/system/osdos.service"
sed -i "s/@FIRST_USER_NAME@/${FIRST_USER_NAME}/" "${ETC}/systemd/system/osdos.service"
install -m 644 files/osdos-terminal.service "${ETC}/systemd/system/"
install -m 644 files/osdos-cloud-init-once.service "${ETC}/systemd/system/"
install -m 755 files/osdos-stop "${ROOTFS_DIR}/usr/local/bin/osdos-stop"
install -m 644 files/osdos-display.socket files/osdos-display@.service "${ETC}/systemd/system/"
sed -i "s/@OSDOS_GROUP@/${FIRST_USER_NAME}/" "${ETC}/systemd/system/osdos-display.socket"
install -m 644 files/osdos-wifi.socket files/osdos-wifi@.service "${ETC}/systemd/system/"
sed -i "s/@OSDOS_GROUP@/${FIRST_USER_NAME}/" "${ETC}/systemd/system/osdos-wifi.socket"
install -m 644 files/osdos-name.socket files/osdos-name@.service "${ETC}/systemd/system/"
sed -i "s/@OSDOS_GROUP@/${FIRST_USER_NAME}/" "${ETC}/systemd/system/osdos-name.socket"
install -D -m 644 files/50-osdos-networkmanager.rules "${ETC}/polkit-1/rules.d/50-osdos-networkmanager.rules"
sed -i "s/@OSDOS_USER@/${FIRST_USER_NAME}/" "${ETC}/polkit-1/rules.d/50-osdos-networkmanager.rules"
install -m 644 files/99-osdos-tty.rules "${ETC}/udev/rules.d/"
install -d "${LIB}"
install -m 755 files/wait-for-app files/wait-for-display "${LIB}/"

# A Raspberry Pi to JAMMA board's controls (osdos-jamma, in the app's folder):
# its service, which does nothing without the JAMMA display preset, the kernel
# modules it reads and plays through, and /dev/uinput for the input group, as
# it doesn't run as root.
install -m 644 files/osdos-jamma.service "${ETC}/systemd/system/"
install -D -m 644 files/osdos-jamma-modules.conf "${ETC}/modules-load.d/osdos-jamma.conf"
install -m 644 files/99-osdos-uinput.rules "${ETC}/udev/rules.d/"

# unit|LABEL, in start order. The boot screen lists them in this order too.
UNITS=(
	"NetworkManager.service|WI-FI"
	"bluetooth.service|BLUETOOTH"
	"avahi-daemon.service|LOCAL NETWORK"
)
if [ "${ENABLE_SSH}" = "1" ]; then
	UNITS+=("ssh.service|SSH")
fi

install -d "${ETC}/osdos"
{
	echo "# Services OSD/OS's boot screen follows, as unit|LABEL, in start order."
	echo "# All but the last hold back until the app is on screen (see their"
	echo "# osdos-defer.conf drop-ins); the last is the network-online check."
	printf '%s\n' "${UNITS[@]}"
	echo "NetworkManager-wait-online.service|ONLINE"
} > "${ETC}/osdos/boot-units"

PREVIOUS=""
for entry in "${UNITS[@]}"; do
	unit="${entry%%|*}"
	install -d "${ETC}/systemd/system/${unit}.d"
	{
		echo "# OSD/OS image: start once the app is on screen, after the service before it."
		if [ -n "${PREVIOUS}" ]; then
			echo "[Unit]"
			echo "After=${PREVIOUS}"
			echo
		fi
		echo "[Service]"
		echo "ExecStartPre=+-/usr/lib/osdos/wait-for-app"
	} > "${ETC}/systemd/system/${unit}.d/osdos-defer.conf"
	PREVIOUS="${unit}"
done

# The connection check gives up after 20 s instead of a minute: with no network
# configured it would otherwise hold the boot screen (and network-online.target)
# that long.
install -d "${ETC}/systemd/system/NetworkManager-wait-online.service.d"
cat > "${ETC}/systemd/system/NetworkManager-wait-online.service.d/osdos.conf" << 'EOF'
# OSD/OS image: don't hold the boot for a minute when there is no network.
[Service]
Environment=NM_ONLINE_TIMEOUT=20
EOF

# Bluetooth is switched on and off in Settings → Bluetooth, through BlueZ, and
# rfkill has no business keeping it off. Raspberry Pi OS starts every radio
# blocked (raspberrypi-sys-mods sets rfkill.default_state=0, so Wi-Fi stays off
# until its country is set), then unblocks Bluetooth only on the adapters
# pi-gen lists by device path (stage2/02-net-tweaks). The Pi 4 this was found
# on wasn't one of them: its adapter stayed blocked, and BlueZ couldn't turn
# it on ("Failed to set mode: Failed (0x03)"). Unblock it as bluetoothd
# starts; systemd-rfkill then keeps it so from one boot to the next.
install -d "${ETC}/systemd/system/bluetooth.service.d"
cat > "${ETC}/systemd/system/bluetooth.service.d/osdos-unblock.conf" << 'EOF'
# OSD/OS image: Bluetooth is switched in the app, through BlueZ; rfkill never blocks it.
[Service]
ExecStartPre=+-/usr/sbin/rfkill unblock bluetooth
EOF

# Built without a password (os/build.sh), the first user can't log in, and Exit
# to Terminal would end at a login nobody can pass: it logs that user in by
# itself instead. Whoever is at the keyboard could take the card out anyway.
if [ "${OSDOS_LOCK_FIRST_USER:-0}" = "1" ]; then
	install -d "${ETC}/systemd/system/osdos-terminal.service.d"
	cat > "${ETC}/systemd/system/osdos-terminal.service.d/osdos-autologin.conf" << EOF
# An OSD/OS image built without a password: the terminal logs ${FIRST_USER_NAME} in by itself.
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin ${FIRST_USER_NAME} --noclear tty1 linux
EOF
fi
