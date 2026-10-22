# Building OSD/OS

If you are interested in building your own version of OSD/OS and adding things to it then this page should hopefully cover what you would need to get an environment set up.  I've included details for macOS on ARM (where I primarily build) and Raspberry Pi OS.  And if you create a feature you would like to contribute back to this repo please open a PR, I'd be glad to talk through it.

## macOS (ARM)

### Prerequisites (one-time)

**Set up Build tools:**

```bash
brew install cmake
```

**Install Qt 6.*:**

- Download from [qt.io/download](https://qt.io/download) or `brew install qt@6`.
- Install to `~/Qt/`
- With the installer, add **Qt Shader Tools** (under Additional Libraries; Homebrew's Qt includes it): it compiles the effects' shaders into the app. Optional: without it Settings leaves out the text, background and screen effects and the Ripple, Wave and Drop transitions, and a theme's with them.

**Install mpv (required for playback):**

```bash
brew install mpv
```

Note: OSD/OS uses mpv as an external subprocess for video playback. It does not link against libmpv at build time, so mpv only needs to be on your `PATH` when running the app. The Transparent Background setting (video played inside the app's window) opens Homebrew's libmpv at run time; it is built in when `pkg-config` finds mpv's headers, which `brew install mpv pkgconf` provides.

**Install FluidSynth and libopenmpt (optional, for menu music in MIDI or a tracker's module):**

```bash
brew install fluid-synth libopenmpt
```

A MIDI file is played by FluidSynth with a SoundFont, which Homebrew's FluidSynth doesn't bring: put a General MIDI `.sf2` (FluidR3_GM, TimGM6mb, …) in the data directory's `soundfonts` folder. Homebrew's mpv may not play XM, MOD, S3M or IT itself; libopenmpt's `openmpt123` makes them into a WAV for it.

**Install yt-dlp and Deno (optional, required only for the YouTube module):**

```bash
brew install yt-dlp deno
```

mpv's ytdl hook uses `yt-dlp` to resolve YouTube URLs at playback time, and the YouTube module's Search runs it too. Two optional files in the data directory add to the module's tree (`#` comments allowed in both; each file only gates its own entries): `youtube_subscriptions.txt` (one channel ID per line — adds Subscriptions/Channels; see the wiki's [YouTube](https://github.com/mehmetraif/OSD-OS/wiki/YouTube#channels-and-subscriptions) page) and `youtube_playlists.txt` (one playlist URL or ID per line, optional `My Name | <url>` display-name prefix — adds Playlists; contents are fetched by running `yt-dlp` directly).

For full YouTube support, current yt-dlp versions also use an external JavaScript runtime. Deno is the recommended runtime. See yt-dlp's [EJS setup guide](https://github.com/yt-dlp/yt-dlp/wiki/EJS) for the currently supported runtimes and versions.

**Install MAME (optional, required only for the Arcade module):**

```bash
brew install mame
```

The Arcade module plays its games in MAME, a program of its own: it looks for `bin/mame` in the data directory first, then `mame` on the `PATH`. See the wiki's [Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade) page.

**Install RetroArch (optional, for the Arcade module's second engine, FinalBurn Neo):**

```bash
brew install --cask retroarch
```

The module finds `RetroArch.app` in `/Applications`; Settings → Arcade → FinalBurn Neo then downloads the FinalBurn Neo core, its list of games and its licence, unpacking the core with minizip (below).

**Install minizip (optional, for the High Scores module's tables and FinalBurn Neo's download):**

```bash
brew install minizip
```

The High Scores module reads MAME's high score files with openhi2txt (`thirdparty/openhi2txt`, built with the app), which opens hi2txt's game definitions (`modules/highscores/data/hi2txt.zip`) with minizip, linked into the app. Without it the module is there but shows no tables, and says why, and the Arcade module can't unpack the FinalBurn Neo core it downloads (one installed with RetroArch still plays). See the wiki's [High Scores](https://github.com/mehmetraif/OSD-OS/wiki/High-Scores) page.

**Install SDL2 (required, gamepad input):**

```bash
brew install sdl2
```

SDL2 is a build-time dependency — `InputManager` links against it for USB game controller support (see [Gamepad input](#gamepad-input-inputcfg)).

**Optional NFC Reader build support:**

- No extra package install is needed on macOS. The PN532 USB driver talks to the reader over plain termios and links nothing, and `PCSC.framework` is provided by the OS — so both reader drivers are always compiled in.

### Get the source

```bash
git clone https://github.com/mehmetraif/OSD-OS.git
cd OSD-OS
```

### Build

**First time, and after any CMakeLists.txt changes:**

```bash
cmake -B build -DCMAKE_PREFIX_PATH=~/Qt/6.11.0/macos . && cmake --build build
```

**For incremental builds:**

```bash
cmake --build build
```

### Run

You can either double-click `build/osdos.app` in Finder, or run from the terminal:

```bash
APP_ROOT=$(pwd) ./build/osdos.app/Contents/MacOS/osdos
```

### Configuration

On macOS all user configuration is stored at:

```
~/Library/Application Support/OSD-OS/
  config.json       ← app and module settings
  lists.json        ← the modules' favorites and recently watched (YouTube keeps its history in youtube_history.json)
  plex_auth.json    ← plex auth
  input.cfg         ← optional gamepad mapping overrides (see Gamepad input below)
```

Those are the main ones; the wiki's [Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files) page lists every file the app keeps there.

This directory is created automatically on first run. It is separate from the app itself, so deleting or rebuilding the app will not wipe your settings.

## Raspberry Pi OS (arm64)

### Prerequisites (one-time)

Run on the Pi with RPi OS Trixie (Debian 13):

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake pkgconf \
  qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-window \
  libqt6svg6 qt6-svg-dev qt6-svg-plugins qt6-wayland \
  qt6-shadertools-dev \
  libdrm-dev libxkbcommon-dev libssl-dev \
  libsdl2-dev libpcsclite-dev libmpv-dev libminizip-dev \
  mpv
```

`libmpv-dev` builds in Transparent Background, `libpcsclite-dev` the NFC Reader's PC/SC readers and `libminizip-dev` the High Scores module's tables (its static library is linked in, so nothing more is needed at run time); without one of them, the build goes on without its part.

`mpv` is the playback engine — OSD/OS launches it as a subprocess. No libmpv build dependency is required. `libmpv-dev` is optional: with its headers the Transparent Background setting is built in, which plays video inside the app's window through libmpv (`libmpv2`, opened at run time; `install.sh` and the OS image install it).

`qt6-shadertools-dev` is optional too: it compiles the effects' shaders (`shaders/`) into the app, for the text, background and screen effects and the Ripple, Wave and Drop transitions; without it they are left out. Nothing of it is needed at run time. To compile a theme's own shader, `qt6-shader-baker` brings `qsb` (`/usr/lib/qt6/bin/qsb`; see the [theme template](docs/theme-template/)).

The menu music plays MIDI files with FluidSynth and a SoundFont, and a tracker's module mpv can't play with openmpt123, at run time only: `sudo apt install fluidsynth timgm6mb-soundfont openmpt123` (`install.sh` and the OS image install them). Without them those files don't play; recordings (MP3, OGG, FLAC, WAV …) need only mpv.

For the NFC Reader module, `libpcsclite-dev` is optional and only needed for PC/SC readers such as the ACR122U — it is detected automatically at configure time. A PN532 USB reader needs no build dependency at all.

Settings → Bluetooth talks to BlueZ through Qt D-Bus, which `qt6-base-dev` includes; without it the row is left out. At run time it needs `bluez` (Raspberry Pi OS Lite has it), with the user the app runs as in the `bluetooth` group (`sudo usermod -aG bluetooth $USER`; `install.sh` and the OS image do this).

```bash
sudo apt-get install -y libpcsclite-dev
```

Either way, run the reader setup script once to grant device access (it installs a udev rule for PN532 readers, and `pcscd` plus a polkit rule when PC/SC is usable on the distro):

```bash
bash scripts/setup-nfc-reader.sh
```

For the YouTube module, additionally install `yt-dlp` — mpv's ytdl hook uses it to resolve YouTube URLs at playback time and it needs to be up to date.  The version bundled with mpv by default on the Raspberry Pi is out of date so you'll need to use wget to get the latest directly from yt-dlp's github.

```bash
sudo wget https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp -O /usr/local/bin/yt-dlp && sudo chmod a+rx /usr/local/bin/yt-dlp
```

For full YouTube support, yt-dlp also uses an external JavaScript runtime; install the recommended Deno runtime by following yt-dlp's [EJS setup guide](https://github.com/yt-dlp/yt-dlp/wiki/EJS), and make sure `deno` is on the `PATH` of the user or systemd service that runs OSD/OS.

The [OSD/OS](os/README.md) image comes with both: yt-dlp's nightly build in the data directory (`~/.local/share/OSD-OS/bin/yt-dlp`), which a timer updates after each boot and once a day, and Deno in `/usr/local/bin`.

If yt-dlp is current and Deno is detected but YouTube still returns `Sign in to confirm you're not a bot`, the response can be route-specific. On a system that already has working IPv6, compare:

```bash
yt-dlp --verbose --simulate --force-ipv4 \
  'https://www.youtube.com/watch?v=VIDEO_ID'
yt-dlp --verbose --simulate --force-ipv6 \
  'https://www.youtube.com/watch?v=VIDEO_ID'
```

If IPv4 returns the bot-check error while IPv6 succeeds, the failure is tied to the IPv4 route rather than the yt-dlp installation. A working IPv6 route may allow playback to proceed, but enabling it is a device and network configuration choice outside OSD/OS.

IPv6 is not an OSD/OS requirement, and OSD/OS should not enable or force it automatically; these commands are only a diagnostic.

For the Arcade module, install MAME, at run time only: `sudo apt install mame` (the OS image has it; `install.sh` doesn't install it). The module looks for `bin/mame` in the data directory first, then `mame` on the `PATH`, then `/usr/games/mame`. For its second engine, FinalBurn Neo, install RetroArch the same way (`sudo apt install retroarch`), with the user the app runs as in the `input` group, then download the core in Settings → Arcade → FinalBurn Neo (an app built with minizip unpacks it; the OS image has the core already). See the wiki's [Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade) page.

### Get the source

```bash
git clone https://github.com/mehmetraif/OSD-OS.git
cd OSD-OS
```

### Build

**First time, and after any CMakeLists.txt changes:**

```bash
cmake -B build
```

**For incremental builds:**

```bash
cmake --build build
```

No `CMAKE_PREFIX_PATH` needed — Qt 6 from apt is found automatically.

### Run

**With a desktop** (RPi OS Full with a display server):

```bash
APP_ROOT=$(pwd) ./build/osdos
```

**Without a Desktop** (RPi OS Lite with no display server):

```bash
APP_ROOT=$(pwd) QT_QPA_PLATFORM=eglfs ./build/osdos
```

`eglfs` uses the KMS/DRM framebuffer directly without X11 or Wayland.

### Configuration

On Raspberry Pi OS all user configuration is stored at:

```
~/.local/share/OSD-OS/
  config.json      ← app and module settings
  lists.json       ← the modules' favorites and recently watched (YouTube keeps its history in youtube_history.json)
  plex_auth.json   ← plex auth
  input.cfg        ← optional gamepad mapping overrides (see Gamepad input below)
```

Those are the main ones; the wiki's [Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files) page lists every file the app keeps there.

This directory is created automatically on first run. It is separate from the app itself, so deleting or rebuilding the app will not wipe your settings.

## Linux x86_64 (AppImage)

For Intel/AMD desktops and the **Steam Deck**, OSD/OS ships as a self-contained **AppImage** — a single executable that bundles Qt, SDL2 and mpv, so it runs on immutable distros like SteamOS with no package-install step. (This differs from the Raspberry Pi arm64 build, which is a `.tar.gz` that relies on `apt` via `install.sh`.)

The app itself is architecture-agnostic — the same C++/QML builds on x86_64 unchanged. On a desktop compositor (SteamOS gamescope / KDE, X11/Wayland) it passes `--hwdec=vaapi,nvdec,vaapi-copy,nvdec-copy,no`, letting mpv pick VA-API on Intel/AMD GPUs and NVDEC on NVIDIA, and degrading to software otherwise (overridable via `mpv_video_args`). This is an explicit list rather than `auto-safe` because `auto-safe` also considers Vulkan video decode: on a host where neither NVDEC nor VA-API initialises, mpv reaches it, shows one frame and then deadlocks. Vulkan *output* is unaffected and still used.

### Prerequisites (one-time)

On a Debian/Ubuntu x86_64 build host:

```bash
sudo apt-get install -y build-essential cmake pkgconf \
  libdrm-dev libssl-dev libsdl2-dev libpcsclite-dev \
  libgl1-mesa-dev libxkbcommon-dev mpv
```

`libpcsclite-dev` is optional and only adds PC/SC reader support to the NFC module. It is listed here because CI builds with it, so the released AppImage bundles `libpcsclite.so.1` and PC/SC works on any host running `pcscd`. Leaving it out still produces a working build — the PN532 USB driver links nothing.

Qt 6 can come from your distro (`qt6-base-dev qt6-declarative-dev qt6-svg-dev qt6-shadertools-dev qml6-module-qtquick*`) or from the [Qt online installer](https://www.qt.io/download-qt-installer) with Qt Shader Tools (set `CMAKE_PREFIX_PATH` to it, matching CI's Qt 6.7).

> **The bundled `mpv` must be modern (≥ 0.38)** — the app's "forced subtitles only" option (`--subs-with-matching-audio=forced`) was added in mpv 0.38, and distro packages are often older (Ubuntu 24.04 ships 0.37, 22.04 ships 0.34.1). If your distro's mpv is too old, build one first and point `MPV_BIN` at it:
>
> ```bash
> MPV_BIN=$(scripts/build-mpv.sh) scripts/build-appimage.sh --configure
> ```
>
> `scripts/build-mpv.sh` compiles mpv 0.40 against your system FFmpeg (needs the meson + FFmpeg/libass/libplacebo/lua/vaapi `-dev` packages — see the CI job). If your distro already ships mpv ≥ 0.38, you can skip it and `build-appimage.sh` uses the system mpv. Note the build host sets the AppImage's glibc floor.

### Build the AppImage

```bash
# Configure + build, then bundle into a portable AppImage in one step:
CMAKE_PREFIX_PATH=/path/to/Qt/6.7.x/gcc_64 scripts/build-appimage.sh --configure
```

This produces `OSD-OS-linux-x86_64.AppImage` in the repo root. On first run the script downloads `linuxdeploy`, `linuxdeploy-plugin-qt` and `appimagetool` into `.appimage-tools/` (cached). Drop `--configure` if you have already built into `build/` yourself.

The script installs into an `AppDir` using the FHS layout (`usr/bin/osdos`, `usr/share/osdos`), bundles a copy of `mpv`, deploys Qt, then prunes host-provided GPU/driver libraries (VA-API, GL, libdrm…) so the target's own drivers are used.

#### Wayland client libraries and X11-only targets

Debian/Ubuntu build SDL2 and mpv with the Wayland backend on, so both hard-link `libwayland-client`, `libwayland-cursor` and `libwayland-egl`, and mpv additionally hard-links `libva-wayland`. The dynamic loader therefore needs all of them **even on a machine that will only ever run X11** — and minimal X11-only images (Batocera, other buildroot handhelds, slim containers) ship none of them, so the app used to die before `main()` with `error while loading shared libraries: libwayland-egl.so.1`.

`libvulkan.so.1` is carried for a subtler reason: Batocera *has* one, but built without Wayland support, so mpv died on `undefined symbol: vkCreateWaylandSurfaceKHR` (via libplacebo) even though every library resolved. None of these are driver libraries — the `libwayland` ones are protocol shims, `libva-wayland` is 27 KB of `vaGetDisplayWl` glue delegating to `libva.so.2`, and `libvulkan.so.1` is the Khronos *loader*, which `dlopen`s the host's ICD from `vulkan/icd.d`. Hardware decode therefore still runs on the host's own driver in every case. So the build stages a copy in `usr/lib/fallback/` instead of pruning them, and `packaging/linux/AppRun` prepends that directory to `LD_LIBRARY_PATH` **only when the host cannot satisfy them itself**. A real Wayland host keeps using its own copies, matching whatever its Mesa EGL loads.

`AppRun` decides by running both bundled binaries (`osdos` and `mpv`) through `LD_TRACE_LOADED_OBJECTS=1` and looking for `=> not found`, rather than checking whether files of that name exist. That distinction is load-bearing: Batocera keeps *32-bit* Wayland libraries in `/lib32` and registers them in `ld.so.cache` while shipping no 64-bit copies, so a filename check reports "the host has it" for libraries the 64-bit loader will correctly refuse. mpv is traced too because it is a separate executable with dependencies the app never has — `libva-wayland` is mpv's alone, so tracing only the app would miss a host that can start the UI but not play. Only the libraries we carry spares of are considered — a host missing Mesa reports `libGL => not found`, and reacting to that would wrongly pull our copies ahead of a host's working ones. A trace resolves libraries but not the symbols inside them, so `AppRun` additionally runs `mpv --version`: that is what catches a library which exists but is missing an entry point, and it is not something the build-time audit can ever detect — only the target can. `AppRun` also pins `QT_QPA_PLATFORM=xcb` when `DISPLAY` is set — the bundle ships the xcb platform plugin only.

#### Host-library audit

After pruning, the script walks every ELF in the `AppDir`, collects the `DT_NEEDED` sonames, and fails the build if any of them is neither bundled nor allowed to come from the host. The allowed set is `ALLOWED_HOST_LIBS` (the upstream AppImage excludelist) plus whatever the pruning step just deleted, added automatically so the two lists can't drift apart. That set is the explicit promise about which machines can run the build — the Wayland breakage above was exactly this promise growing silently.

If the audit fails it prints each unsatisfied soname and the files needing it. Usually the fix is to bundle the library. Only add it to `ALLOWED_HOST_LIBS` if every supported target genuinely provides it; to unblock a release without editing the script, pass `ALLOW_HOST_LIBS_EXTRA="soname …"`.

### Run

```bash
chmod +x OSD-OS-linux-x86_64.AppImage
./OSD-OS-linux-x86_64.AppImage
```

Configuration lives at `~/.local/share/OSD-OS/` (same as the Pi). See [INSTALL.md](INSTALL.md) for the Steam Deck end-user flow (Desktop Mode + adding it to Steam for Gaming Mode).

`yt-dlp` is deliberately **not** bundled (it needs to be updatable independently of app releases). For the YouTube module on an immutable distro, drop a copy at `~/.local/share/OSD-OS/bin/yt-dlp` (`chmod +x`, update with `yt-dlp -U`); the app resolves it there first, then a `yt-dlp` sibling of the binary, then `PATH`, and hands the chosen path to mpv's ytdl hook via `--script-opts=ytdl_hook-ytdl_path=…` so both use the same copy. See the wiki's [YouTube](https://github.com/mehmetraif/OSD-OS/wiki/YouTube#how-osdos-finds-yt-dlp) page.

## Gamepad input (input.cfg)

USB game controllers should work out of the box as SDL's built-in controller database normalizes most pads (Xbox, PlayStation, 8BitDo, NES-style clones etc...) to a standard layout. OSD/OS maps that stanard layout to its navigation actions:

| Controller input | Action |
|---|---|
| D-pad / left stick | navigate (up / down / left / right) |
| A | select |
| B/Select | back |
| Start | play / pause |
| LB / RB shoulder buttons | left / right (seek during playback) |

Controllers can be hotplugged at any time and during playback the same buttons drive mpv (seek, pause, quit) exactly like their keyboard equivalents.

**Overriding the mapping**

- Create an `input.cfg` file in the configuration directory.
- Add one binding per line, `<input> <action>`;
- Use `#` to start a comment, data is case-insensitive and you only need to include the things you want to change (anything not defined will fall back to defaults)
- The file is also live-reloaded while the app runs, so you can tune bindings without restarting.

Inputs use SDL controller names — short (`a`, `b`, `x`, `y`, `back`, `start`, `leftshoulder`, `rightshoulder`, `dpup`, `dpdown`, `dpleft`, `dpright`, ...) or the long `SDL_CONTROLLER_BUTTON_*` forms. Analog axes take a `+`/`-` direction suffix (`lefty-`, `triggerright+`). Actions: `up`, `down`, `left`, `right`, `select`, `back`, `play_pause`, and `none` to unbind a default.

**Button names are positional**, following an Xbox reference layout: `a` means the *south* face button, `b` east, `x` west, `y` north, no matter what's "printed" on the buttons on your pad. Because of that you can also write the positions directly: `south`, `east`, `west`, `north`. So `south select` makes the bottom face button select on an Xbox pad, an 8BitDo, and a PlayStation pad alike.

**Footer labels** will attempt to adapt automatically and the on-screen hints show what's printed on the controller you touched last (Nintendo-type pads show B at south, PlayStation pads show X/O/SQ/TR). If your controller reports the wrong type (which is common for pads with Nintendo-style labels running in X-input mode) you can pick its Button Labels on its page in Settings → Controls, or define the label you see with a `label` line in the input.cfg, which wins on every pad.

`input.cfg` is the map every pad starts from. A pad's own page in Settings → Controls changes that one model of pad over it (saved in `config.json` under `app.controllers`), so most people never need the file.

```
# input.cfg — example overrides
south                    select       # positions: south/east/west/north
SDL_CONTROLLER_BUTTON_A  select       # long names work
b                        back         # so do SDL short names ("b" = east)
x                        play_pause
rightshoulder            none         # unbind a default
lefty-                   up           # axes take a +/- suffix
triggerright+            play_pause
label south B                         # force the footer label to display "B" for the south button
label east  A                         # force the footer label to display "A" for the east button
```

Any bad lines are skipped with a warning in the log (line number included)

**Exotic controllers** — if SDL doesn't recognize your pad at all, drop a community [gamecontrollerdb.txt](https://github.com/mdqinc/SDL_GameControllerDB) into the configuration directory; it will be loaded at startup before controllers are opened.

## Video decode tuning (mpv_video_args)

OSD/OS detects your device at startup and attempts to launch with the most efficient video-output and hardware-decode flags for it. Currently the Pi 3 uses a low-CPU overlay path, the Pi 4 a hardware-decode + copy path, the Pi 5 the V3D Vulkan path, and macOS VideoToolbox. The exact flags and the reasoning per board are in [ARCHITECTURE.md → Per-device video decode profiles](ARCHITECTURE.md#per-device-video-decode-profiles).

**Overriding the decode flags**

If you find the need to tune for your hardware, you can add an `mpv_video_args` string under `"app"` in `config.json`.  It accepts a a space-separated list of mpv flags to replace the auto-detected `--vo` / `--hwdec` params that OSD/OS sets (and, on a Pi 5, the `--drm-*` ones that point mpv at the display preset's output).

```json
{
  "app": {
    "mpv_video_args": "--vo=drm --hwdec=v4l2m2m-copy"
  }
}
```

This config is read at each playback event, so a change applies on the next playback (no rebuild or restart needed). Only set video-output/decode flags here though; the app owns the rest (the IPC control channel, OSC, input) and for other mpv preferences (things like deinterlace, cache, subtitle styling, audio output device...) please just create a standard `~/.config/mpv/mpv.conf`. MPV will read that automatically every launch. Please check out [ARCHITECTURE.md → How mpv flags are layered](ARCHITECTURE.md#how-mpv-flags-are-layered-the-precedence-cascade) if you are interested in the background on this approach.

**Enabling crop on a Pi 3** — the Pi 3 default uses a zero-copy overlay path for performance, and a hardware overlay plane can't zoom/crop (`--panscan` blanks the video there), so the app doesn't offer crop on it. To allow crop on the Pi 3, set Settings → **1080p Playback** to Off (`smooth_playback`): mpv then takes the copy path, `--vo=drm --hwdec=v4l2m2m-copy`, so frames go through the scaler, where crop works. Setting those flags through `mpv_video_args` alone isn't enough, because whether the app offers crop follows the setting (`MpvController::cropUnavailable()`).

The trade-off with this approach: the copy path didn't look like it could reliabilty play back 1080p on the Pi 3 in my testing. I found it can easily peg the CPU and cause stuttering. So enabling crop on a Pi 3 means keeping your source content to **720p and below**. Ultimately its your call: smooth 1080p without crop (keep the default), or enable crop with a 720p ceiling (1080p Playback Off).

## Choosing the display (display_index)

On a multi-monitor machine (macOS or desktop Linux), the UI launches fullscreen on the primary display by default. To launch it on a different display (a dedicated TV, projector, or CRT) without making that display the OS primary you can use the app-level `display_index` setting. Video playback will also follow automatically and mpv will open fullscreen on the same display. There are three steps:

**1. Find the index of the display you want.** At startup the app logs every attached display with its index, name, and resolution, e.g.:

```
[main] display index 0: "DELL U2723QE" 3840x2160 at (0,0)
[main] display index 1: "SwitchResX4 - MACROSILICON" 1280x720 at (3840,0)
```

To see this output, launch the app from a terminal, or read it from Console.app / the log file — see [Debugging & logs](#debugging--logs) just below for where the output goes depending on how you launched. Pick the index whose name/resolution matches your target display.

**2. Add the setting** as an integer under `"app"` in `config.json` (macOS: `~/Library/Application Support/OSD-OS/config.json`, Linux: `~/.local/share/OSD-OS/config.json`):

```json
{
  "app": {
    "display_index": 1
  }
}
```

**3. Relaunch.** The UI opens fullscreen on that display. Index `0` (the default, or an unset/out-of-range value) is the primary screen — the prior behaviour.

Notes:

- The setting does nothing where there is only one display to pick: headless Raspberry Pi (EGLFS drives one output) and Steam Deck **gaming mode** (gamescope exposes a single virtual screen) and an out-of-range index falls back to `0` with a warning in the log.
- Display indices can shift if you re-plug dislays so if the UI comes up on the wrong screen after a display change then simply re-check the startup log and update the value.
- One desktop-Linux corner case: when the app runs on native Wayland but an X11 `DISPLAY` is also available, mpv is intentionally run through Xwayland (see `MpvController`), where display *names* don't line up — playback placement then falls back to index order, which matches in practice but is best-effort. If mpv can't resolve the display it warns and uses the current screen.

## Debugging & logs

OSD/OS logs to **stdout/stderr** via Qt's `qDebug` / `qWarning` (used throughout `AppCore`, `MpvController`, and the module backends). A Release build (every release, the image, the AppImage) defines `QT_NO_DEBUG_OUTPUT`, so only `qWarning` and `qInfo` lines show there; build with `-DCMAKE_BUILD_TYPE=Debug` to see the `qDebug` ones. The trick is knowing where that output goes depending on how you launched the app.

### Option 1: Running from source

Just run the binary in a terminal and the logs will print right there:

```bash
# macOS
APP_ROOT=$(pwd) ./build/osdos.app/Contents/MacOS/osdos

# Raspberry Pi
APP_ROOT=$(pwd) ./build/osdos                         # with a desktop
APP_ROOT=$(pwd) QT_QPA_PLATFORM=eglfs ./build/osdos   # headless / Lite
```

### Option 2: Raspberry Pi installed via `install.sh`

How you read logs depends on whether you installed the autostart service:

- **Run it by hand** — type `osdos` over SSH and logs print to that terminal. Use this while debugging. (Note: the launcher does **not** power off on exit, unlike the service.)
- **Via the systemd service** — the service sends output to the journal, so:
    ```bash
    journalctl -u osdos -b        # logs from this boot
    journalctl -u osdos -f        # follow live
    ```
    Heads-up: the autostart service runs `ExecStopPost=osdos-stop`, which **powers the Pi off when you quit** (exit 0). A crash or an error exit leaves the Pi on: systemd starts the app again five seconds later, and when it has failed three times in two minutes, puts a login prompt on `tty1` in its place (`OnFailure=osdos-terminal.service`); `journalctl -u osdos -b` then shows why. Stopping or restarting the service with `systemctl` leaves the Pi on too (installs set up before either changed need the installer re-run once). To debug without powering off, either pick **Exit to Terminal** in the Quit dialog (drops to a login shell on `tty1` without removing the service — `sudo systemctl start osdos` or `sudo reboot` to return to the service), or stop the service and run the binary directly:
    ```bash
    sudo systemctl stop osdos
    osdos
    ```

### mpv playback logs

During playback the app hands off to mpv as a subprocess (see [ARCHITECTURE.md → Playback Hand-off](ARCHITECTURE.md#playback-hand-off-mpvcontroller)). mpv keeps its own log in the temp dir, `/tmp/osdos-mpv.log` (`--log-file`), next to its IPC socket (`/tmp/osdos-mpv.sock`), and what mpv prints goes into the app's own log as `[mpv]` lines — both useful when a video won't play or transcoding misbehaves.

### Crash and freeze reports

On Linux a crash writes its backtrace, and a main thread that hasn't answered for 5 s writes where it and every other thread are, to the log and to `crash-report.txt` or `stall-report.txt` in the data folder, which outlive a reboot where the Pi's journal doesn't ([ARCHITECTURE.md → Crash and stall reports](ARCHITECTURE.md#crash-and-stall-reports-utildiagnostics)). Under the autostart service, a main thread stuck for 20 s ends the app with exit code 70, and systemd starts it again. Under a debugger, which stops the app at a breakpoint, set `OSDOS_WATCHDOG=0`.

### Qt / QML debugging knobs

These environment variables help when the UI itself is misbehaving:

```bash
QT_LOGGING_RULES="qt.qml.*=true"   # verbose QML engine logging
QML_IMPORT_TRACE=1                 # trace QML import resolution (missing modules/components)
QT_QPA_EGLFS_DEBUG=1               # EGLFS/DRM detail on Raspberry Pi headless
```

Set them inline, e.g. `QML_IMPORT_TRACE=1 APP_ROOT=$(pwd) ./build/osdos`.

## GitHub Actions

Besides the release workflow below, four run on their own: **Setup page** ([pages.yml](.github/workflows/pages.yml)) builds [OSD/OS Setup](os/README.md#osdos-setup) (`scripts/build-site.py`), tests it (`node --test tests/site/*.test.mjs`) and publishes it on the repository's GitHub Pages from `main`, and only builds and tests it for a pull request or another branch (Pages is turned on once, in the repository's **Settings → Pages**, with **GitHub Actions** as its source); **Regression tests** ([regression-tests.yml](.github/workflows/regression-tests.yml)) builds the app and runs the tests in `tests/` on Linux x64 and arm64 for every pull request, and every push to `main`, that touches the app or its tests (its C++, QML, assets, shaders and scripts; see [tests/README.md](tests/README.md)), and once more on Raspberry Pi OS: a Debian 13 "trixie" container with Raspberry Pi's archive added as pi-gen adds it for the OS image, on arm64 (other workflows can call it too, `workflow_call`, and the release workflow does); **OS image** ([os-image.yml](.github/workflows/os-image.yml)) builds the OSD/OS image for a pull request that touches it; and **Wiki** ([wiki.yml](.github/workflows/wiki.yml)) publishes `docs/wiki` to the [GitHub wiki](https://github.com/mehmetraif/OSD-OS/wiki) whenever it changes on `main`, replacing what the wiki had (GitHub makes a wiki's repository only once its first page is saved in the web interface, so that is done once first).

### How to trigger a build

Releases are built automatically when you push a version tag:

```bash
git tag v2026.06.04
git push origin v2026.06.04
```

And you can use pre-release tags to test CI without making a public release:

```bash
git tag v1.0.0-rc1
git push origin v1.0.0-rc1
```

Tags containing `-rc`, `-beta`, or `-alpha` are published as GitHub pre-releases.

### What the workflow does

These build jobs run in parallel:

| Job | Runner | Output |
|---|---|---|
| `build-macos-arm64` | `macos-15` (Apple Silicon) | `OSD-OS-<tag>-macOS-arm64.dmg` |
| `build-linux-arm64` | `ubuntu-24.04-arm` (native arm64) | `OSD-OS-<tag>-linux-arm64.tar.gz` |
| `build-linux-x86_64` | `ubuntu-24.04` | `OSD-OS-linux-x86_64.AppImage` (version-less — self-updates in place) |
| `build-os-image` | `ubuntu-24.04-arm` (after `build-linux-arm64`) | `OSD-OS-<tag>-raspberry-pi.img.xz` and its `.info` |

Beside them, `regression-tests` runs the regression tests on the tagged commit, calling [regression-tests.yml](.github/workflows/regression-tests.yml): on Linux x64 and arm64, and on Raspberry Pi OS.

macOS job: installs Qt via the Qt CDN, builds, runs `macdeployqt` to embed Qt frameworks (including `libSDL2.dylib`; Homebrew's minizip is linked in), signs, packages as `.dmg`. With the repository secrets `APPLE_CERT_P12_BASE64`, `APPLE_CERT_PASSWORD`, `APPLE_ID`, `APPLE_APP_PASSWORD` and `APPLE_TEAM_ID` it signs with the Developer ID and notarizes the `.dmg`, the hardened runtime letting in libraries others sign (`packaging/macos/osdos.entitlements`: Homebrew's libmpv, which plays a video inside the window, and libretro's FinalBurn Neo core, whose version the Arcade module asks) and letting that libmpv's LuaJIT make the code it compiles of the app's Lua scripts executable; without them (a fork) it signs ad hoc, and the release notes say how to open it the first time. mpv is not bundled — users install it via `brew install mpv`.

Linux arm64 job: installs Qt from apt, builds, package as `.tar.gz`. mpv and SDL2 are not bundled (minizip is linked in) — end users install them via `apt install mpv libsdl2-2.0-0` or by running the `install.sh` that is bundled with each release where they are installed as part of the dependency list.

Linux x86_64 job: installs Qt via the Qt CDN, builds the app, builds **mpv 0.40 from source** (`scripts/build-mpv.sh`, against 24.04's stock FFmpeg 6.1 — apt's mpv 0.37 is one release too old for the app's forced-subtitle option, and the savoury1 PPA for a newer one is now gated), then runs `scripts/build-appimage.sh` to bundle Qt, SDL2 **and** that mpv into a self-contained `.AppImage`. Built on `ubuntu-24.04`, which sets a glibc 2.39 floor (a current Steam Deck and modern distros — not Ubuntu 22.04 / Debian 12 / older SteamOS). Nothing to install on the target — it runs on immutable distros like SteamOS.

OS image job: once the arm64 tarball is built, builds the OSD/OS image from it with pi-gen ([os/README.md](os/README.md#building)), as the OS image workflow does, the first user's password from the secret `OS_FIRST_USER_PASS` if there is one (else the account is locked).

A final `release` job waits for all four builds and the regression tests, then creates a GitHub Release, a draft until every artifact is attached (including `install.sh`) and public then, so nobody sees a release short of a file, with notes written for the tag, their links absolute. A tag whose regression tests fail is not published: its builds are still made, and stay in the run's artifacts. The notes open with what the release brings since the previous tag, written by `scripts/release-notes.sh` from the commits' subject lines (merges left out): `feat:` commits under **What's New**, `fix:` commits under **Bug Fixes**, and every other commit under **Changelog**. A commit without a conventional-commit type goes under Bug Fixes when its subject starts with "Fix", under What's New when it starts with "Add", and under Changelog otherwise. Run the script locally to preview a release's notes: `scripts/release-notes.sh v1.2.0` (or `scripts/release-notes.sh <tag> <previous tag>`).

### Output

**While the workflow is running:**

Go to **Actions** → select the workflow run → each build job has an **Artifacts** section at the bottom where you can download that job's output before the release is published.

**After the workflow completes:**

Go to the repository on GitHub → **Releases** → select the release for the tag you set. All build artifacts (the OSD/OS image and its `.info`, the `.dmg`, both Linux packages, `SHA256SUMS`, `install.sh` and `setup-nfc-reader.sh`) are listed under Assets.
