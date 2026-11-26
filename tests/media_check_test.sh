#!/usr/bin/env bash
# The film partition's check before it is mounted (os/stage-osdos/05-media:
# media-check and osdos-media-check.service), on an exFAT image made in a file
# (mkfs.exfat where exfatprogs is here, else a boot sector made by hand) and a
# stand-in fsck.exfat on the PATH that records its calls: a clean partition
# is not checked; one with VolumeDirty set is, with the repair the kernel's
# command line asks for; so is a boot sector zeroed or cut short; no device,
# and fsck.mode=skip, check nothing; fsck missing, failing or taking too long
# never fails it; one line says which each time. Then the real fsck.exfat on
# a dirty image, where it is here, the unit's place before the mount, and the
# fstab line without a pass.
#   media_check_test.sh <os/stage-osdos/05-media>
set -u

STAGE="$1"
CHECK="$STAGE/files/media-check"
UNIT="$STAGE/files/osdos-media-check.service"
fails=0
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# The stand-in fsck.exfat: records its arguments, says nothing, and exits as
# $FAKE_FSCK_EXIT says (after $FAKE_FSCK_SLEEP seconds).
mkdir "$WORK/fake"
cat > "$WORK/fake/fsck.exfat" << 'SH'
#!/bin/sh
echo "$*" >> "$FAKE_FSCK_LOG"
[ -n "${FAKE_FSCK_SLEEP:-}" ] && sleep "$FAKE_FSCK_SLEEP"
exit "${FAKE_FSCK_EXIT:-0}"
SH
chmod 755 "$WORK/fake/fsck.exfat"
export FAKE_FSCK_LOG="$WORK/calls"
# A PATH with what the check uses and no fsck.exfat at all.
mkdir "$WORK/nofsck"
for tool in bash cat dd od tr timeout printf; do
    p=$(command -v "$tool") && ln -s "$p" "$WORK/nofsck/$tool"
done
export OSDOS_MEDIA_CMDLINE="$WORK/cmdline"
echo "console=tty1 root=PARTUUID=4e639091-02 rootfstype=ext4 rootwait quiet" > "$OSDOS_MEDIA_CMDLINE"

# runs <device> [PATH]: the check's output in $out, its exit in $code, the
# stand-in's calls in $calls.
runs() {
    : > "$FAKE_FSCK_LOG"
    out=$(PATH="${2:-$WORK/fake:$PATH}" bash "$CHECK" "$1" 2>&1); code=$?
    calls=$(cat "$FAKE_FSCK_LOG")
}
lines() { printf '%s\n' "$1" | grep -c .; }
expect() {   # expect <what> <calls wanted> <words in the line>
    [ "$code" = 0 ] || fail "$1: exit $code"
    [ "$calls" = "$2" ] || fail "$1: fsck.exfat called '$calls', not '$2'"
    [ "$(lines "$out")" = 1 ] || fail "$1: not one line: $out"
    case "$out" in *"$3"*) ;; *) fail "$1: the line is '$out', without '$3'" ;; esac
}
set_flags() {   # set_flags <image> <byte 106> <byte 107>, in octal escapes
    # shellcheck disable=SC2059  # the bytes as octal escapes
    printf "\\$2\\$3" | dd of="$1" bs=1 seek=106 conv=notrunc 2> /dev/null
}

IMG="$WORK/img"
if command -v mkfs.exfat > /dev/null 2>&1; then
    truncate -s 32M "$IMG"
    mkfs.exfat -L OSD-OS "$IMG" > /dev/null || fail "mkfs.exfat failed"
    made=mkfs.exfat
else
    # The fields the check reads: FileSystemName at 3, VolumeFlags at 106,
    # the signature at 510.
    head -c 4096 /dev/zero > "$IMG"
    printf '\353\166\220EXFAT   ' | dd of="$IMG" conv=notrunc 2> /dev/null
    printf '\125\252' | dd of="$IMG" bs=1 seek=510 conv=notrunc 2> /dev/null
    made="a boot sector made by hand"
fi
echo "exFAT image: $made"
[ "$(od -An -tu1 -j106 -N2 "$IMG" | tr -s ' ')" = " 0 0" ] || fail "a new image's VolumeFlags aren't 0"

runs "$IMG"; expect "clean" "" "clean (VolumeFlags 0x0000); check skipped"
set_flags "$IMG" 001 000
runs "$IMG"; expect "ActiveFat alone" "" "clean (VolumeFlags 0x0001)"
set_flags "$IMG" 000 002
runs "$IMG"; expect "the high byte's bit 1 (not VolumeDirty)" "" "clean (VolumeFlags 0x0200)"
set_flags "$IMG" 002 000
runs "$IMG"; expect "dirty" "-p $IMG" "dirty (VolumeFlags 0x0002); checked (fsck.exfat -p): exit status 0"
set_flags "$IMG" 007 000
runs "$IMG"; expect "dirty among other flags" "-p $IMG" "dirty (VolumeFlags 0x0007)"
set_flags "$IMG" 002 000

# The repair the kernel's command line asks for, as systemd-fsck took it.
for pair in "fsck.repair=yes:-y" "fsck.repair=no:-n" "fsck.repair=preen:-p"; do
    echo "console=tty1 ${pair%%:*} rootwait" > "$OSDOS_MEDIA_CMDLINE"
    runs "$IMG"; expect "${pair%%:*}" "${pair#*:} $IMG" "checked (fsck.exfat ${pair#*:})"
done
echo "console=tty1 fsck.repair=yes fsck.mode=skip" > "$OSDOS_MEDIA_CMDLINE"
runs "$IMG"; expect "fsck.mode=skip, dirty" "" "fsck.mode=skip; nothing checked"
echo "console=tty1 fsck.repair=yes fsck.mode=force" > "$OSDOS_MEDIA_CMDLINE"
set_flags "$IMG" 000 000
runs "$IMG"; expect "fsck.mode=force, clean" "-y $IMG" "fsck.mode=force (VolumeFlags 0x0000); checked (fsck.exfat -y)"
echo "console=tty1 fsck.repair=yes rootwait" > "$OSDOS_MEDIA_CMDLINE"
set_flags "$IMG" 002 000

# fsck failing, missing, or taking too long: exit 0 all the same.
FAKE_FSCK_EXIT=1 runs "$IMG"; expect "errors corrected" "-y $IMG" "exit status 1, errors corrected"
FAKE_FSCK_EXIT=4 runs "$IMG"; expect "errors left" "-y $IMG" "exit status 4, mounted as it is"
FAKE_FSCK_EXIT=8 runs "$IMG"; expect "fsck's own failure" "-y $IMG" "exit status 8, mounted as it is"
runs "$IMG" "$WORK/nofsck"; expect "fsck.exfat missing" "" "dirty (VolumeFlags 0x0002); fsck.exfat is missing, mounted unchecked"
start=$(date +%s)
OSDOS_MEDIA_FSCK_TIMEOUT=1s FAKE_FSCK_SLEEP=30 runs "$IMG"
expect "fsck taking too long" "-y $IMG" "stopped after 1s (exit status 124)"
[ $(( $(date +%s) - start )) -lt 10 ] || fail "fsck taking too long: stopped after $(( $(date +%s) - start )) s"

# A boot sector that isn't exFAT's, or can't be read whole: checked.
cp "$IMG" "$WORK/zeroed"
dd if=/dev/zero of="$WORK/zeroed" bs=512 count=1 conv=notrunc 2> /dev/null
runs "$WORK/zeroed"; expect "a zeroed boot sector" "-y $WORK/zeroed" "unreadable (no exFAT boot sector); checked"
head -c 100 "$IMG" > "$WORK/short"
runs "$WORK/short"; expect "a boot sector cut short" "-y $WORK/short" "unreadable (the boot sector can't be read); checked"
: > "$WORK/empty"
runs "$WORK/empty"; expect "an empty device" "-y $WORK/empty" "unreadable (the boot sector can't be read)"
chmod 000 "$WORK/short"
if [ "$(id -u)" != 0 ]; then
    runs "$WORK/short"; expect "a device it can't read" "-y $WORK/short" "unreadable (the boot sector can't be read)"
fi
chmod 644 "$WORK/short"

# No device at all: nothing checked.
runs "$WORK/missing"; expect "no device" "" "no such device; nothing checked"
runs ""; expect "no device named" "" "no such device; nothing checked"

# The real fsck.exfat, where it is here: a dirty image checked with the
# image's repair (-y), the flag cleared by it, and the next boot's check
# skipped.
if command -v fsck.exfat > /dev/null 2>&1 && [ "$made" = mkfs.exfat ]; then
    set_flags "$IMG" 002 000
    out=$(bash "$CHECK" "$IMG" 2>&1); code=$?
    [ "$code" = 0 ] || fail "real fsck: exit $code"
    case "$out" in *"dirty (VolumeFlags 0x0002); checked (fsck.exfat -y): exit status 0, clean"*) ;;
        *) fail "real fsck: $out" ;; esac
    [ "$(od -An -tu1 -j106 -N2 "$IMG" | tr -s ' ')" = " 0 0" ] || fail "real fsck left VolumeDirty set"
    runs "$IMG"; expect "after the real fsck" "" "clean (VolumeFlags 0x0000); check skipped"
else
    echo "SKIP: the real fsck.exfat (exfatprogs isn't here)"
fi

# The unit: the script where the stage installs it, never failing; bound to
# the device /etc/fstab names, ordered before the partition's mount, which
# wants it; the fstab line without a pass.
fstab=$(sed -n 's/^\techo "\(LABEL=OSD-OS .*\)" >> \/etc\/fstab$/\1/p' "$STAGE/01-run-chroot.sh")
[ -n "$fstab" ] || fail "01-run-chroot.sh's fstab line not found"
read -r what where _type _opts _dump pass <<< "$fstab"
[ "$pass" = 0 ] || fail "the fstab line's pass is '$pass', not 0"
device="/dev/disk/by-label/${what#LABEL=}"
exec_start=$(sed -n 's/^ExecStart=//p' "$UNIT")
[ "$exec_start" = "-/usr/lib/osdos/media-check $device" ] || fail "the unit's ExecStart is '$exec_start'"
# shellcheck disable=SC2016  # the stage's own variables, as written
grep -q '^install -D -m 755 files/media-check "${ROOTFS_DIR}/usr/lib/osdos/media-check"$' "$STAGE/00-run.sh" \
    || fail "00-run.sh doesn't install media-check into /usr/lib/osdos"
# shellcheck disable=SC2016
grep -q '^ln -sf /etc/systemd/system/osdos-media-check.service "${UNITS}/media-OSD\\\\x2dOS.mount.wants/"$' "$STAGE/00-run.sh" \
    || fail "00-run.sh doesn't have the mount want the check"
if command -v systemd-escape > /dev/null 2>&1; then
    dev_unit=$(systemd-escape -p --suffix=device "$device")
    mount_unit=$(systemd-escape -p --suffix=mount "$where")
    grep -qxF "BindsTo=$dev_unit" "$UNIT" || fail "the unit isn't bound to $dev_unit"
    grep -qF "After=$dev_unit " "$UNIT" || fail "the unit isn't after $dev_unit"
    grep -qxF "Before=$mount_unit shutdown.target" "$UNIT" || fail "the unit isn't before $mount_unit"
    case "$(cat "$STAGE/00-run.sh")" in *"${mount_unit//\\/\\\\}.wants"*) ;; *) fail "00-run.sh's wants folder isn't $mount_unit's" ;; esac
fi
grep -q '^DefaultDependencies=no$' "$UNIT" || fail "the unit has the default dependencies (after basic.target: a cycle with the mount)"
grep -q '^Requires=' "$UNIT" && fail "the unit requires something"
if command -v systemd-analyze > /dev/null 2>&1; then
    mkdir "$WORK/units"
    sed "s|^ExecStart=-/usr/lib/osdos/media-check|ExecStart=-$(cd "$STAGE/files" && pwd)/media-check|" "$UNIT" > "$WORK/units/osdos-media-check.service"
    out=$(systemd-analyze verify "$WORK/units/osdos-media-check.service" 2>&1) || fail "systemd-analyze verify: $out"
    [ -z "$out" ] || echo "systemd-analyze verify: $out"
fi

if [ "$fails" -ne 0 ]; then
    echo "$fails failed"
    exit 1
fi
echo "all passed"
