#!/bin/bash -e

# OSD/OS itself, laid out the way scripts/install.sh lays it out: the release
# tarball unpacked under /opt/osdos and owned by the user the app runs as (so
# the launcher can swap in-app updates in), the launcher at /usr/local/bin.
# os/build.sh drops the tarball, the launcher (taken from install.sh) and
# os/NOTICE in files/.

install -d "${ROOTFS_DIR}/opt/osdos"
# The tarball holds ./usr/local/{bin,share}; strip that prefix like install.sh.
tar -xzf files/osdos.tar.gz --strip-components=3 -C "${ROOTFS_DIR}/opt/osdos"
if [ ! -x "${ROOTFS_DIR}/opt/osdos/bin/osdos" ]; then
	echo "files/osdos.tar.gz has no usr/local/bin/osdos" >&2
	exit 1
fi

install -m 755 files/osdos-launcher "${ROOTFS_DIR}/usr/local/bin/osdos"
# The image's notices: what it is made of, under which licences (os/NOTICE).
install -D -m 644 files/NOTICE "${ROOTFS_DIR}/usr/share/doc/osdos/NOTICE"

on_chroot << EOF
chown -R "${FIRST_USER_NAME}:" /opt/osdos
EOF

# The app's QML compiled into the QML cache of the user it runs as
# (osdos --compile-qml), so that the image's first start doesn't compile it
# before the menus can be drawn: the cache is kept by each file's path and
# time, both as they will be on the card. Off the screen, which the build
# has none of. The build goes on without it: the first start compiles it.
on_chroot << EOF || echo "OSD/OS's QML wasn't compiled ahead: its first start compiles it"
timeout 600 runuser -u "${FIRST_USER_NAME}" -- env HOME="/home/${FIRST_USER_NAME}" \
	QML2_IMPORT_PATH=/usr/lib/aarch64-linux-gnu/qt6/qml /opt/osdos/bin/osdos --compile-qml
EOF
