# To do: display modes

Work planned for Settings → Screen → Display Output and the Arcade's video, kept here until it is done. Every resolution below comes from a standard or from MAME's own data, never from a guess; the sources are at the end.

## 1. Presets with square pixels on a 4:3 tube

A resolution is exactly 4:3 when its width is 4/3 of its height: its pixels are then square on a 4:3 screen. OSD/OS has three of them today: 320x240 (JAMMA 240p, 320 across), 640x480 interlaced (JAMMA 480i, 640 across) and 640x480 (JAMMA 31 kHz).

- [ ] **JAMMA 288p 15 kHz (384x288)**: PAL's square-pixel sampling, halved for progressive lines (half of 14.75 MHz, 472 pixels a line, 312 lines, 50 Hz). Pi 3, Pi 4 and Pi 5.
- [ ] **JAMMA 576i 15 kHz (768x576)**: PAL's square-pixel sampling (14.75 MHz, 944 pixels a line, 625 lines, 50 fields a second). Pi 3 and Pi 4: the Pi 5 can't interlace beside the board's I2C on GPIO 1.
- [ ] **JAMMA 25 kHz (512x384)**: the medium-resolution mode of Atari's Seattle and Vegas games, 23 working games in MAME 0.264. Take the timing from those drivers' own figures in `mame -listxml` (`pixclock`, `htotal`, `vtotal`). Pi 3, Pi 4 and Pi 5.
- [ ] **SCART RGB at the same sizes**: 320x240 and 384x288 (Pi 3 and Pi 4), and 640x480 and 768x576 interlaced (Pi 3, Pi 4 and Pi 5, whose SCART leaves GPIO 1 free). Today every SCART RGB preset is 720 across.

## 2. Other standard modes

- [ ] **31 kHz SD**: 720x480p 60 Hz (CEA-861 VIC 2, 27 MHz, 858x525) and 720x576p 50 Hz (VIC 17, 27 MHz, 864x625), for a 31 kHz or tri-sync monitor.
- [ ] **HDMI fixed 4:3 modes** (VESA DMT), for a 4:3 flat panel or a PC CRT through an HDMI to VGA adapter. HDMI otherwise takes whatever the screen reports. Each at 60 Hz unless noted; pixel clock, then line and frame totals:

  | Mode | Clock | Totals | Horizontal |
  |---|---|---|---|
  | 800x600 | 40.000 MHz | 1056x628 | 37.9 kHz |
  | 1024x768 | 65.000 MHz | 1344x806 | 48.4 kHz |
  | 1152x864 (75 Hz only) | 108.000 MHz | 1600x900 | 67.5 kHz |
  | 1280x960 | 108.000 MHz | 1800x1000 | 60.0 kHz |
  | 1400x1050 | 121.750 MHz | 1864x1089 | 65.3 kHz |
  | 1600x1200 | 162.000 MHz | 2160x1250 | 75.0 kHz |
  | 1792x1344 | 204.750 MHz | 2448x1394 | 83.6 kHz |
  | 1856x1392 | 218.250 MHz | 2528x1439 | 86.3 kHz |
  | 1920x1440 | 234.000 MHz | 2600x1500 | 90.0 kHz |

- [ ] **Exit codes for them.** osdos-stop's codes are full: 20-39 write a preset, 40-59 the same and power off. New presets need a new range (60-79, and 80-99 to power off), osdos-stop and `DisplayOutput.cpp` taught it, `SuccessExitStatus` in both units, and launcher API 10.

## 3. Each game in its own mode on a Pi 5

The goal: a game played on its own lines and refresh rate (Switchres, as GroovyMAME does), so that a 224-line game at 59.18 Hz no longer runs on a fixed 240-line 60 Hz picture, out of step or at the wrong speed.

What the Pi 5's DPI driver allows (`drivers/gpu/drm/rp1/rp1-dpi/` in Raspberry Pi's kernel, rpi-6.12.y):

- The mode comes from the CRTC state, not from the device tree, so a program can set any timing at run time through KMS (libdrm).
- Its only check is the pixel clock, 1 to 200 MHz; it sets the clock from the video PLL for each mode.
- Interlace needs GPIO 1, 2 and 3 given to the DPI, so not with the JAMMA board, whose I2C is on GPIO 1: progressive modes only. The driver calls its interlace experimental.
- The `vc4-kms-dpi-generic` panel lists one mode, but an atomic commit isn't limited to the connector's list. To confirm on the Pi.

A Pi 4 runs its DPI from the firmware (`dpi_timings`, fake KMS): there a mode changes only with a reboot.

- [ ] **A test tool first**, `osdos-modetest`: sets a few custom modes on the Pi 5's DPI through libdrm (for instance 1920x224 at 59.18 Hz, 1920x256 at 55.02 Hz, 1920x240 at 57.55 Hz), each for a few seconds with a test pattern, then puts the old mode back. Run on a Pi 5 with the JAMMA board: the picture holds, rolls, or doesn't sync.
- [ ] **The modes worked out per game**: super resolution 1920 across, the game's own lines and refresh (MAME's `display` in `-listxml`), the line rate kept inside the monitor's class (about 15.7 kHz on a 15 kHz preset). Either our own small calculation, or the Switchres library (GPL-2.0+, so fine beside OSD/OS's GPL-3.0; its KMS backend is marked work in progress).
- [ ] **In the Arcade's launch**: the mode set through libdrm before the screen is handed to MAME (DisplayHandoff), MAME started on the desktop mode, OSD/OS's own mode back once it ends.
- [ ] **Safety**: never a mode outside the scan rate of the preset in force. A 15 kHz preset gets 15 kHz modes only; a 25 or 31 kHz game stays on the preset's mode. Too high a rate can damage a 15 kHz monitor.
- [ ] **Open questions**: does MAME's SDL KMSDRM keep the mode it finds, or set one of the connector's own? Does the monitor lose sync for a moment at each switch?

## 4. What MAME's games ask for

MAME 0.264's `-listxml`: 14,819 coin-operated machines, 14,711 of them raster. Below, the 4,414 working parents (driver status good or imperfect, clones left out).

| Horizontal rate | Games | Share |
|---|---|---|
| 15 kHz | 2,180 | 49.4% |
| Unknown (no raw timing in MAME, mostly older 15 kHz games) | 1,937 | 43.9% |
| 31 kHz | 138 | 3.1% |
| 25 kHz | 85 | 1.9% |
| Other | 74 | 1.7% |

| Lines | Share |
|---|---|
| 224 or fewer | 50.1% |
| 225 to 240 | 29.3% |
| 241 to 256 | 7.5% |
| 257 to 288 | 1.5% |
| 289 to 384 | 3.4% |
| 385 to 480 | 7.4% |
| More | 0.8% |

| Refresh | Share |
|---|---|
| 59.7 to 60.5 Hz | 58.4% |
| 59.0 to 59.7 Hz | 15.2% |
| 55 to 57 Hz | 10.2% |
| 60.5 to 62 Hz | 7.4% |
| 57.9 to 59.0 Hz | 3.4% |
| 50 Hz | 3.2% |

- Commonest sizes: 320x224 (8.9%), 256x224 (7.2%), 320x240 (7.2%), 256x224 vertical (6.5%), 320x200 (5.5%), 256x240 (5.1%), 640x480 (4.8%), 384x224 (2.7%). 21.5% are vertical.
- Exactly 4:3: 712 games, 320x240 (389), 640x480 (226), 256x192 (38), 512x384 (27), 288x216 (17), 400x300 (11), 576x432 (4).
- 25 kHz: 512x384 (23), 640x480 (23), 496x384 (20), 512x400 (12). 31 kHz: 640x480 (38), 543x465 (27), 640x240 (26), 724x483 (17).

## Sources

- [Linux `drm_edid.c`: VESA DMT and CEA-861 mode tables](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/drm_edid.c)
- [SGI, Video Sampling: square-pixel and CCIR-601 rates](https://ftp.jurassic.nl/mirrors/ftp.sgi.com/sgi/video/rld/vidpage/sampling.html)
- [Raspberry Pi kernel, RP1 DPI driver (rpi-6.12.y)](https://github.com/raspberrypi/linux/tree/rpi-6.12.y/drivers/gpu/drm/rp1/rp1-dpi)
- [RetroArch #19255: CRT SwitchRes on a Pi 5](https://github.com/libretro/RetroArch/issues/19255)
- [Switchres](https://github.com/antonioginer/switchres)
- [Recalbox RGB JAMMA 2: per-game modes on a Pi 5](https://www.recalbox.com/recalbox-rgb-jamma)
- [Museum of the Game: arcade monitor rates](https://arcade-museum.com/monitor.html)
- MAME 0.264's `-listxml` (Ubuntu 24.04's `mame` package), counted by display, scan rate and refresh.
