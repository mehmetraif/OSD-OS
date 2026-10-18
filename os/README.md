# The OSD/OS image

A Raspberry Pi OS Lite (64-bit, Trixie) image that boots straight into OSD/OS: flash it, plug the Pi into the TV, and the first thing on screen is the app. It is built with Raspberry Pi's own image builder, [pi-gen](https://github.com/RPi-Distro/pi-gen), so the kernel, firmware, Wi-Fi/Bluetooth drivers and the patched FFmpeg that mpv's Pi 4 HEVC decoding relies on are exactly Raspberry Pi OS's. This directory only adds one stage on top of Raspberry Pi OS Lite.

It is a folder of its own because the image is a build of its own: pi-gen's, not the app's CMake, run by its own workflow (`.github/workflows/os-image.yml`) from the app's release tarball, which it puts on the image as `scripts/install.sh` would.

## What it is built on

- **Raspberry Pi OS Lite (64-bit)**, the system without a desktop that Raspberry Pi makes for its boards. It is based on **Debian 13 "trixie"**: its packages are Debian's, with Raspberry Pi's own from Raspberry Pi's archive on top (the kernel, the firmware, a patched FFmpeg, Widevine and more).
- **pi-gen**, the tool Raspberry Pi builds Raspberry Pi OS with, at a pinned commit of its `arm64` branch (`PI_GEN_REF` in `build.sh`). Its stages 0 to 2 make Raspberry Pi OS Lite, and `stage-osdos` adds OSD/OS and everything below.
- The image's release name is **OSD/OS** (`PI_GEN_RELEASE`, written to `/boot/firmware/issue.txt`), not pi-gen's default, which pi-gen keeps for Raspberry Pi's own builds.
- What it is made of, and under which licences, is in [NOTICE](NOTICE); see [Licences](#licences).

<img src="boot-screen.gif" width="480" alt="The boot screen: the OSD/OS cassette, whose reels turn and whose tape winds from the left reel onto the right one as the progress bar fills, and the list of services coming up">

## What is different from a manual install

Compared with flashing Raspberry Pi OS Lite and running `scripts/install.sh` ([INSTALL.md](../INSTALL.md)):

- **The app comes first.** `osdos.service` starts as soon as the display driver is up (after `basic.target`), not after every other service (`multi-user.target`).
- **The rest waits for it.** Wi-Fi (NetworkManager), Bluetooth, mDNS (`avahi-daemon`) and, if enabled, SSH hold back until the app has drawn its first frame. Then they start one after another, in that order.
- **The boot screen.** While those services start, the app shows a pixel-art VHS cassette, the owner's drawing (made with ChatGPT) with OSD/OS on its label (`assets/images/cassette.png`, drawn by `views/Components/VhsCassette.qml`): its reels turn, and the tape winds off the left reel onto the right one as the progress bar fills, plus a line per service (`[ OK ] WI-FI`, …). It ends with a check that the network is actually online. The startup module opens once it is done, so modules that need the network find it ready. No key does anything while it is up but Ctrl+Q, which quits, and so powers the Pi off.
- **A quiet boot.** There is no rainbow splash, no one-second firmware delay (`boot_delay=0`) and no kernel text, logo or cursor on `tty1`.
- **Less running.** The image has:
  - no apt, man-db, e2scrub or dpkg-backup timers (they wake the SD card at random times, mid-movie included);
  - no cron;
  - no Raspberry Pi Connect agent.

  cloud-init applies Raspberry Pi Imager's settings on the first boot and is then switched off, because otherwise its stages delay every boot.
- **The streaming modules' browser is included.** Chromium with Widevine and the `cage` kiosk compositor, for the Netflix and Prime Video modules, which open each service's own player full screen, and for YouTube's sign-in; `wtype` closes the browser cleanly when BACK is held. They add about 400 MB; build with `OSDOS_STREAMING=0` to leave them out.
- **YouTube works out of the box.** The image has yt-dlp and Deno, the JavaScript runtime yt-dlp needs to play YouTube's videos ([its EJS notes](https://github.com/yt-dlp/yt-dlp/wiki/EJS)).
  - yt-dlp is its latest nightly build, the channel its own README recommends: YouTube changes often, and a yt-dlp a few weeks old soon stops finding videos.
  - It lives in the app's data directory (`~/.local/share/OSD-OS/bin/yt-dlp`), where the app looks first, and replaces itself with the newest build two minutes after each boot and once a day (`osdos-yt-dlp-update.timer`). A check is one small request to GitHub. Without a connection within five minutes, it waits for the next run.
  - ffmpeg comes with them, for the Playlists module's offline playlists: above 360p YouTube sends a video's picture and sound apart, and ffmpeg puts a download back together.
  - They add about 90 MB; build with `OSDOS_YOUTUBE=0` to leave them out.
- **MAME for arcade games.** Debian's MAME (0.276) and its data, among which the hiscore plugin that keeps the games' high scores, for the Arcade module, which plays the ROM sets copied into `Games/Arcade` on the card's OSD-OS partition ([Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade)). They add about 450 MB with what they depend on.
  - The module's second engine comes with them, for RetroAchievements' high scores and online play: Debian's RetroArch and FinalBurn Neo, the core it plays the games with, the latest of libretro's nightly builds when the image is built, with its list of arcade games and its licence (`/usr/local/share/doc/fbneo/license.txt`). They add about 230 MB more.
  - Build with `OSDOS_ARCADE=0` to leave them all out.
- **Menu music in any format.** FluidSynth with a small General MIDI SoundFont (TimGM6mb, about 6 MB) plays a theme's or your own MIDI file, and openmpt123 a tracker's module mpv can't play itself (XM, MOD, S3M, IT).
- **Films go on the card.** On the first boot the system keeps 8 GiB of the card, and the rest becomes a partition of its own in exFAT, labelled **OSD-OS**, which Windows and macOS open too. Local Files opens it, and offline playlists download into it. See [Films on the card](#films-on-the-card).
- **USB drives mount by themselves.** A USB stick or disk plugged in is mounted read-only and shows up in Local Files under its label. See [USB drives](#usb-drives).
- **Bluetooth switches on from the app.** Bluetooth is unblocked (`rfkill unblock bluetooth`) as bluetoothd starts, so Settings → Bluetooth's switch is the only one. (The user the app runs as is in the `bluetooth` group, as with `install.sh`.)
  - Raspberry Pi OS starts every radio blocked, so that Wi-Fi stays off until its country is set (`rfkill.default_state=0`), and then unblocks Bluetooth only on the adapters pi-gen knows by device path. The Pi 4 this was found on wasn't one of them: its adapter stayed blocked, and BlueZ couldn't turn it on.

Everything else (the launcher, in-app updates, Exit to Terminal, the data directory in `~/.local/share/OSD-OS`) is the same as a manual install. The launcher, stop helper and terminal unit are taken from `scripts/install.sh` at build time. Run on the image, `install.sh` leaves the image's own `osdos.service` as it is, with its user, and brings the app, the launcher and those helpers up to date.

## Flashing

1. Download the image from the [latest release](https://github.com/mehmetraif/OSD-OS/releases/latest), `OSD-OS-<version>-raspberry-pi.img.xz` (or take the `.img.xz` out of the zip an OS image workflow run's artifact comes in). In Raspberry Pi Imager, choose **Use custom** and pick it.
2. Raspberry Pi Imager 2 skips OS customisation for an image chosen with **Use custom**: it can't tell which kind the image takes. For Wi-Fi, a user, SSH, the locale and the keyboard, open the image through a local manifest instead, one that gives it `"init_format": "cloudinit-rpi"` ([Imager's notes on it](https://github.com/raspberrypi/rpi-imager/tree/main/doc/local_json)). Imager 1 seems to apply it, but on Trixie its settings never take effect. Without one, Wi-Fi can be set up by hand: before the first boot, add it to `network-config` on the boot partition, following the example in that file. With Ethernet there is nothing to do.
3. The image's own user is `pi` (or whatever `FIRST_USER_NAME` was at build time), and the app runs as that user. Logging in is only needed for Exit to Terminal or SSH:
   - If the image was built with a password (`FIRST_USER_PASS`), you can log in as `pi`. The OS image workflow takes it from the repository secret `OS_FIRST_USER_PASS`, if there is one.
   - If it was built without one, the `pi` account has no password to log in with. Exit to Terminal then logs `pi` in by itself: whoever is at the keyboard could take the card out anyway. That shell has no root, though: `sudo` asks for the password `pi` doesn't have. For `sudo`, build the image with a password. SSH needs an image built with `ENABLE_SSH=1` and a key (`PUBKEY_SSH_FIRST_USER`): a key alone doesn't turn it on. Or Imager's customisation, which can turn SSH on with the user it creates.

### Films on the card

The first boot splits the card: the system keeps 8 GiB (`OSDOS_ROOT_SIZE`), and the rest becomes a partition of its own in exFAT, labelled **OSD-OS**. Windows and macOS open exFAT, so:

1. Quit OSD/OS (it powers the Pi off) and take the card out.
2. In the computer's card reader the card shows up as two drives, **bootfs** and **OSD-OS**. Copy films onto **OSD-OS**, in folders if you like.
3. Windows also offers to format the system's partition, which it can't read. Always say no (**Cancel**): formatting it erases the system.
4. Put the card back in the Pi. Local Files opens **OSD-OS** (`/media/OSD-OS`) until its Media Directory setting names another folder.

Arcade games go there too: the Arcade module plays the MAME ROM sets in a **Games/Arcade** folder on it, which it makes the first time it opens, or which you can make on the computer ([Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade)).

The Pi writes to it too: the Playlists module downloads its offline playlists' videos into a **Playlists** folder there (its Download Folder setting can name another), each video once, flushed to the card as it completes. Switching the Pi off at the wall in the middle of a download can still leave the partition untidy, so it is checked (`fsck.exfat`) at every boot before it is mounted. Nothing on it can run (it is mounted `noexec`): it only ever holds media, and anyone with the card can write it. A card flashed with an earlier image mounts it read-only (`ro` in `/etc/fstab`), where offline playlists' downloads fail with the folder named as the reason: flash the new image, or change `ro` to `rw,noexec,nosuid,nodev` (and the last `0` to `2`) on the film partition's line (`/media/240-MP`, as those images named it).

### USB drives

A USB stick or disk plugged into the Pi is mounted by itself, read-only, under `/media/usb`, in a folder named after its label, and Local Files lists it at the top of its tree as `USB: <label>`. Pulled out, its folder goes, and so does its row.

- **Read-only**, so a drive can be pulled out at any moment, mid-film included, with nothing on it half written. A file system with a journal (ext4, XFS, btrfs) is mounted without replaying it, which would write to the drive.
- **File systems:** FAT32, exFAT, NTFS (the kernel's `ntfs3`, or `ntfs-3g` where that is missing), ext2/3/4, HFS+ (a Mac's), XFS, btrfs, F2FS, and ISO 9660 and UDF (a disc in a USB drive). Their files belong to the user the app runs as, as the film partition's do. Nothing on a drive can run (`noexec`).
- **Two drives with one label:** the second is named after its device too (`KINGSTON (sdb1)`).
- **Never the system's own:** a partition already mounted (what `/etc/fstab` mounts, the card's OSD-OS partition) or on the disk the system boots from (a USB disk it boots from) is left alone.
- **How:** a udev rule (`/etc/udev/rules.d/99-osdos-usb-mount.rules`) starts `osdos-usb-mount@<device>.service` as a drive's partition comes; it runs `/usr/lib/osdos/usb-mount`, and stops, unmounting the drive, as it goes. `journalctl -u 'osdos-usb-mount@*'` says what each drive was mounted as, or why it couldn't be.
- A manual install (`scripts/install.sh`) doesn't do this: Raspberry Pi OS Lite mounts nothing by itself. Local Files still lists a drive mounted under `/media` some other way.

A card with less than 2 GiB to spare past the system gets no film partition, and the system takes all of it, as Raspberry Pi OS does. The split replaces Raspberry Pi OS's first-boot resize (`raspberrypi-sys-mods`' `resize_early`, overridden in `/etc/initramfs-tools/scripts`), so it happens once, on a freshly flashed card. A first boot cut short between making the film partition and formatting it leaves the partition blank, and the next boot formats it, but only when `blkid` (which the initramfs carries for this) finds no file system on it at all: one with a file system of any kind, or one it can't read, is left as it is.

### Display output: HDMI, composite or SCART

**Settings → Display Output** switches where the picture goes, among the outputs the Pi has: OSD/OS reads the board's model and offers only those.

| Output | Preset (`osdos-display-….txt`) | Pi 3 | Pi 4 | Pi 5 |
|---|---|---|---|---|
| HDMI, resolution auto-detected (the default) | `hdmi` | ✓ | ✓ | ✓ |
| Composite NTSC / PAL, 4:3: the AV jack, or a Pi 5's TV pads | `crt-ntsc`, `crt-pal` | ✓ | ✓ | ✓ |
| Composite NTSC / PAL from a Pi 5's GPIO pins | `crt-gpio-ntsc`, `crt-gpio-pal` | | | ✓ |
| SCART RGB on the GPIO pins, NTSC timing (480i) / PAL timing (576i) | `scart-rgb-ntsc`, `scart-rgb-pal` | | ✓ | ✓ |
| SCART RGB on the GPIO pins, 240p / 288p | `scart-rgb-240p`, `scart-rgb-288p` | | ✓ | |
| A JAMMA arcade cabinet through a Raspberry Pi to JAMMA board in RGB-Pi's layout, 240p: its picture, sound and controls | `jamma-240p` | | ✓ | ✓ |
| The same board in a 15 kHz monitor's other modes: 240p 320 across (Native) or 1920 (Super), 288p 720 or 1920 across | `jamma-240p-native`, `jamma-240p-super`, `jamma-288p`, `jamma-288p-super` | | ✓ | ✓ |
| The same board, interlaced: 480i, 576i | `jamma-480i`, `jamma-576i` | | ✓ | |
| The same board for a medium resolution (25 kHz, 496×384) or VGA (31 kHz, 640×480) monitor, or a tri-sync one | `jamma-25k`, `jamma-31k` | | ✓ | ✓ |

The Pi reads its display settings only as it powers on, so the switch is a restart:

1. Choose an output, then **Switch and Restart**, **At Next Start** or **Switch and Power Off**. OSD/OS writes that preset over `osdos-display.txt` on the boot partition (as root, through its stop helper `osdos-stop`: no `sudo`), and reboots, or stays on as it is until the next start, or powers off. The last two are for a screen that isn't here yet, as a JAMMA cabinet's.
2. On the new output it asks **Keep this display output?** first, over the boot screen. **Keep** keeps it. Without an answer within 30 seconds (a screen that shows nothing, a TV that can't show that standard), it goes back to the output before, restarting again, and says so.

From any computer, copy a preset over `osdos-display.txt` on the boot partition (the FAT one, **bootfs**), the way to put a card right that shows nothing. On a Pi 5 a SCART RGB preset also needs its composite sync: the line `options drm_rp1_dpi force_csync=1` in `/etc/modprobe.d/osdos-display.conf` (the setting writes it).

Only the Pi 4's HDMI and composite have been tested. The other outputs are starting points, there for the keep-or-go-back to fall back on:

- **Pi 4**: one output is on at a time. Its composite turns HDMI off, as Raspberry Pi's firmware does with `enable_tvout=1`, and so does its RGB, whose timing is the firmware's (`dpi_timings`). Should its 480i or 576i not hold, 240p and 288p are progressive (the menus then have half the lines).
- **JAMMA (Pi 4 and Pi 5)**: a Raspberry Pi to JAMMA board in RGB-Pi's layout (RGB-Pi's JAMMA board, Crown Arcade's) takes the picture in DPI "mode 6", not the VGA666's, so it has a preset of its own. It turns on I2C on GPIO 0 and 1, where `osdos-jamma.service` reads the board's two MCP23017s and plays the cabinet's joysticks and buttons as two gamepads ([Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls#a-jamma-cabinet)). On a Pi 4 it moves the sound to GPIO 18 and 19, the board's amplifier. On a Pi 5 the DPI is the kernel's, set up as RGUI-Pi has it, on pins that leave GPIO 0 and 1 to I2C (`osdos-jamma-dpi`, an overlay the image compiles); a Pi 5's sound can't reach the amplifier, so it goes through a USB sound card (Settings → Audio Output). Its DPI interlaces only with GPIO 1, the board's I2C, so 480i and 576i are a Pi 4's alone. Every JAMMA preset reads the controls and leaves the sync to the board; their timings are Recalbox RGB JAMMA's and Switchres' for the same layout, VESA's for 31 kHz. The 25 and 31 kHz modes are for a monitor that takes them: too high a scan rate can damage a 15 kHz one, so OSD/OS says so before it switches, and the keep-or-go-back brings the old mode back in 30 seconds.
- **Pi 5**: it may keep HDMI on beside a CRT. The preset names the output OSD/OS and its videos use (`# osdos-output:` in it) and the mode (`# osdos-mode:`), whose lines make composite NTSC (480) or PAL (576): its composite chip takes the standard from them. At boot the launcher waits up to ten seconds for that composite or DPI output to register (it may come after HDMI's); should it never come, the launcher takes the first display connected instead, and logs why (`the display preset's … output never appeared`). Its RGB is the kernel's (`vc4-kms-dpi-generic`), 480i and 576i as Raspberry Pi added them in 2025, with the composite sync SCART needs made on GPIO 1. Netflix and Prime Video's browser picks a screen of its own.

#### The cables

GPIO pins carry 3.3 V and take nothing more: SCART's 5 V and 12 V must never reach them.

**Composite to SCART (or RCA).** On a Pi 4 (and 3), the AV jack carries the picture and the sound (a 4-pole plug, as camcorder cables have, wired in this order):

| Pi AV jack | SCART pin |
|---|---|
| Tip: left audio | 6 (audio in, left) |
| Ring 1: right audio | 2 (audio in, right) |
| Ring 2: ground | 4 (audio ground), 17 (video ground), 21 (shield) |
| Sleeve: composite video | 20 (video in) |

A Pi 5 has no AV jack: its composite comes from the two pads by HDMI 1 (J7, the picture and ground) and needs wires or a pin header soldered on, to SCART pin 20 and 17. It has no analog audio either: a USB sound card gives SCART pins 6 and 2 their sound, chosen in **Settings → Audio Output**. The **GPIO composite** of a Pi 5 is an 8-bit code on GPIO 4 (lowest bit) to 11, for a DAC to turn into the picture: the pads are the simpler way.

**SCART RGB on the GPIO pins** (Pi 4, Pi 5), in the layout of the VGA666 board, which the presets use:

| GPIO (header pin) | Signal | SCART pin |
|---|---|---|
| 16–21 (36, 11, 12, 35, 38, 40) | red, 6 bits, GPIO 21 the highest | 15 (red), ground 13 |
| 10–15 (19, 23, 32, 33, 8, 10) | green, 6 bits, GPIO 15 the highest | 11 (green), ground 9 |
| 4–9 (7, 29, 31, 26, 24, 21) | blue, 6 bits, GPIO 9 the highest | 7 (blue), ground 5 |
| 1 (28), Pi 5 | composite sync | 20 (through 680 Ω), ground 17 |
| 2 and 3 (3, 5), Pi 4 | vertical and horizontal sync, negative | joined into a composite sync, negative as SCART's: two diodes (cathodes to GPIO 2 and 3), their anodes pulled up to 3.3 V by 470 Ω, then 330 Ω to pin 20; or a 74HC86 powered from 3.3 V (at 5 V it can't read the GPIO's 3.3 V), as XNOR: GPIO 2 XOR GPIO 3, through a second gate with its other input at 3.3 V, then 680 Ω to pin 20 |
| 3.3 V (1) | RGB on | 16 (blanking) through 100 Ω (or 5 V, pin 2, through 180 Ω), ground 18 |
| GND (6, 9, …) | ground | 4, 5, 9, 13, 17, 18, 21 |

- Each colour's six pins meet at its SCART pin through resistors of 510 Ω (the highest bit), 1 kΩ, 2 kΩ, 3.9 kΩ, 8.2 kΩ and 16 kΩ (the lowest), as on the VGA666: about 0.7 V into the TV's 75 Ω. 549 Ω, 1.1 kΩ, 2.21 kΩ, 4.42 kΩ, 8.87 kΩ and 17.8 kΩ (E96) halve each step exactly; 1 % resistors are close enough for 6 bits.
- Sound: from the Pi 4's AV jack (tip, ring 1, ring 2 as above), whose pins are inside the Pi, clear of the GPIO pins, or a USB sound card (a Pi 5 has no jack), to pins 6, 2 and 4. **Settings → Audio Output** picks which plays.
- Pin 8 at 9.5–12 V (from a 12 V supply through 1 kΩ) switches most TVs to the SCART input, in 4:3. Without it, choose the input with the TV's remote.
- The picture rolls on a Pi 4: change the sync polarities in the preset's `dpi_timings` (its 2nd and 7th numbers) from 0 to 1.

The rest of `config.txt` matches the settings [INSTALL.md](../INSTALL.md) documents, including the per-model drivers and overclocking.

## Building

The image needs the app's arm64 tarball (`OSD-OS-<version>-linux-arm64.tar.gz`, as the release workflow builds it).

**On GitHub:**

1. Run the **OS image** workflow from the Actions tab (on a fork, enable Actions first). It builds the app from the chosen branch, then the image, on an arm64 runner. It also runs on pull requests that touch the image or the boot screen.
2. The image is the run's `osdos-image` artifact.

The run gives the `pi` user the repository secret `OS_FIRST_USER_PASS` as its password. Without that secret the account is locked.

The app in these images is built as `dev`, which turns its self-update off. That way a branch image can't swap itself for a release that doesn't have its changes.

**With a release:** pushing a version tag (see [BUILDING.md](../BUILDING.md#github-actions)) builds the image from that release's own tarball and attaches it to the release as `OSD-OS-<tag>-raspberry-pi.img.xz`, with its `.info` (the packages on it). The release is published only once the regression tests have passed on the tagged commit. Its app is the release's, and updates itself from later releases.

**Locally:** with Docker on an arm64 Linux host (an x86 host works too, through QEMU emulation, but takes hours), run:

```bash
FIRST_USER_PASS='…' os/build.sh path/to/OSD-OS-<version>-linux-arm64.tar.gz
```

The image lands in `os/work/pi-gen/deploy/`. `os/build.sh` fetches pi-gen at a pinned commit of its `arm64` branch and builds stages 0–2 (Raspberry Pi OS Lite), then `stage-osdos`. Settings, all through the environment:

| Variable | Default | |
|---|---|---|
| `FIRST_USER_PASS` | — | Password of the first user. Without one, the account is locked. |
| `FIRST_USER_NAME` | `pi` | The first user; the app runs as this user. |
| `OSDOS_DISPLAY` | `hdmi` | Initial display preset, by its name in the table [above](#display-output-hdmi-composite-or-scart) (`crt-pal` for `osdos-display-crt-pal.txt`). |
| `OSDOS_STREAMING` | `1` | `0` leaves out the Netflix and Prime Video modules' browser (Chromium, Widevine, cage, wtype), which YouTube's sign-in uses too. |
| `OSDOS_YOUTUBE` | `1` | `0` leaves out the YouTube module's yt-dlp, Deno and ffmpeg; the module then says yt-dlp is missing. |
| `OSDOS_ARCADE` | `1` | `0` leaves out MAME (`mame`, `mame-data`), RetroArch and FinalBurn Neo; the Arcade module then says MAME is not installed. |
| `OSDOS_ROOT_SIZE` | `8` | GiB the system keeps of the card; the rest becomes the film partition on the first boot. `0`: no film partition, the system takes the whole card. |
| `ENABLE_SSH` | `0` | `1` enables SSH (it then also waits for the app, after mDNS). |
| `TARGET_HOSTNAME` | `osdos` | |
| `IMG_NAME` | `osdos` | |
| `WPA_COUNTRY`, `LOCALE_DEFAULT`, `KEYBOARD_KEYMAP`, `KEYBOARD_LAYOUT`, `TIMEZONE_DEFAULT`, `PUBKEY_SSH_FIRST_USER`, `PUBKEY_ONLY_SSH` | pi-gen's | Passed through to pi-gen. |
| `DEPLOY_COMPRESSION` | `xz` | Passed through to pi-gen, which would otherwise zip the image. |
| `OSDOS_TARBALL` | — | The OSD/OS release tarball to build the image with; the first argument, when given, wins. |
| `OSDOS_NATIVE` | `0` | `1` runs pi-gen's `build.sh` directly (a Debian host, as root) instead of in Docker. |
| `OSDOS_PREPARE_ONLY` | `0` | `1` sets up the pi-gen tree and its config, then stops. |
| `PI_GEN_REF` | pinned | pi-gen commit to build from. |
| `PI_GEN_REPO` | `https://github.com/RPi-Distro/pi-gen` | The pi-gen repository to fetch it from. |
| `WORK` | `os/work` | Where pi-gen is fetched and the image is built. |

## How the boot works

1. **Firmware.** It loads the kernel with no splash and no delay.
2. **Kernel.** It boots quietly and logs to `tty3`, so `tty1` stays black.
3. **systemd reaches `basic.target`.** `osdos.service` starts: `wait-for-display` holds it (at most five seconds) until the KMS driver has a display connector, then the launcher runs as in a manual install.
4. **The first frame is on screen.** The app writes `/run/osdos/ready` (`OSDOS_READY_FILE`).
5. **The held-back units start.** These are the ones listed in `/etc/osdos/boot-units`. Each has a `osdos-defer.conf` drop-in that runs `/usr/lib/osdos/wait-for-app` before it starts and orders it after the unit before it.
   - `wait-for-app` returns as soon as the ready file exists, or after 20 seconds whatever happens.
   - It returns at once when the app isn't starting this boot at all.
   - It is a wait, not an `After=osdos.service` ordering, on purpose: the worst a wait can cost is its timeout, while an ordering cycle with an early-boot unit could stall the whole boot.
6. **The boot screen follows them.** `BootProgress` (`src/boot/`) polls `systemctl` for the units in `OSDOS_BOOT_UNITS_FILE`. It closes once every one has settled (started, failed, or skipped because its condition failed), or after a minute. Keys don't close it.

Outside this image neither environment variable is set, so `BootProgress` does nothing and a normal install is unaffected. It also stays out of the way when the app restarts after the boot has finished.

`/etc/osdos/boot-units` lists `unit|LABEL` pairs. You can change a label there. To hold back another service, give it the same drop-in and add it to the list.

### Measuring

The app logs when its first frame appeared and when each service settled, relative to the app's start:

```bash
journalctl -b -u osdos | grep '\[boot\]'
systemd-analyze critical-chain osdos.service
systemd-analyze blame
```

## Licences

[NOTICE](NOTICE), which the image carries as `/usr/share/doc/osdos/NOTICE`, lists what the image is made of and under which licences:

- OSD/OS itself, under GPL-3.0, with its licence at `/opt/osdos/share/osdos/LICENSE` (and in Settings → About), and what is built into it or ships with it for the High Scores module: openhi2txt (GPL-3.0) with RapidXML (Boost or MIT), and hi2txt's game definitions (GPL-2.0, their licence beside them in `/opt/osdos/share/osdos/modules/highscores/data/`).
- Raspberry Pi OS's and Debian's packages, each with its own licence in `/usr/share/doc/<package>/copyright`. The list of them, with their versions, is the image's `.info` file, which the workflow publishes with the image. Their source is in the Debian and Raspberry Pi archives, and OSD/OS offers the source of the GPL and LGPL software on an image for three years after it is published.
- What the stage adds: yt-dlp (public domain), Deno (MIT, its licence in `/usr/local/share/doc/deno/LICENSE.md`, from `stage-osdos/06-youtube/files/deno-LICENSE.md`, which is bumped with `DENO_VERSION`), MAME (GPL-2.0 or later, Debian's `mame` and `mame-data` packages, with their copyright files in `/usr/share/doc`), RetroArch (GPL-3.0, Debian's `retroarch` package), FinalBurn Neo (Team FB Neo's own licence, which lets it be shared with its text but never sold, nor used by a project that asks for donations; its full text in `/usr/local/share/doc/fbneo/license.txt`, with the build's date and CRC beside it in `BUILD`) and Widevine, Google's proprietary module, from Raspberry Pi's archive as Raspberry Pi OS installs it.
- The notices for the data the modules show (TMDB, Open-Meteo, Wikidata), pi-gen's licence, the Raspberry Pi firmware's licence, which asks to be reproduced with it, and the trademarks: Raspberry Pi is a trademark of Raspberry Pi Ltd, Debian a registered trademark of Software in the Public Interest, Inc., and OSD/OS is endorsed by neither.

## Notes

- An image built from a release tarball keeps in-app updates, which install releases from [mehmetraif/OSD-OS](https://github.com/mehmetraif/OSD-OS). A release without `BootProgress` still runs on this image. It just never writes the ready file, so the held-back services start after their 20-second timeout instead of right away.
