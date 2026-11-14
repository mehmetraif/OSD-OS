# Configuration files

OSD/OS keeps everything in one folder, the **data folder**: settings, lists, sign-ins, your themes and skins, and the files some modules read.

## The data folder

| System | Data folder |
|---|---|
| The OSD/OS image | `/home/pi/.local/share/OSD-OS/` (the image's first user, `pi` by default) |
| Raspberry Pi OS, other Linux, SteamOS (the AppImage) | `~/.local/share/OSD-OS/`, or `$XDG_DATA_HOME/OSD-OS/` when `XDG_DATA_HOME` is set |
| macOS | `~/Library/Application Support/OSD-OS/` |
| Windows | `%APPDATA%\OSD-OS\`, that is `C:\Users\<you>\AppData\Roaming\OSD-OS\` |
| FreeBSD, OpenBSD | `~/.local/share/OSD-OS/` |

- OSD/OS creates it at its first start. Updating, reinstalling or removing the app leaves it as it is.
- It belongs to the user OSD/OS runs as: under the autostart service, the service's user, not root.
- `DATA_ROOT` names another folder in its place (below).

## Environment variables

### APP_ROOT and DATA_ROOT

| Variable | What it names | Without it |
|---|---|---|
| `APP_ROOT` | OSD/OS's own files: `Main.qml`, `views/`, `modules/`, `assets/`, `scripts/` and `LICENSE`. Handed on to mpv and to scripts | The first that fits: a Mac app's `Contents/Resources`; the binary's folder when `Main.qml` is in it (Windows); `share/osdos` beside the binary's folder (`/opt/osdos/share/osdos`, `usr/share/osdos` in the AppImage); the folder above the binary's |
| `DATA_ROOT` | The data folder | The system's folder, in the table above |

Both must name a folder that exists; OSD/OS creates none for `DATA_ROOT`:

```sh
mkdir -p ~/osdos-test-data
DATA_ROOT=~/osdos-test-data APP_ROOT=$(pwd) ./build/osdos
```

Under `DATA_ROOT` the 240-MP data folder is not moved over ([Coming from 240-MP](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#coming-from-240-mp)).

The launcher (`/usr/local/bin/osdos`) and `scripts/web-player.sh` read `DATA_ROOT` too. Under the autostart service, set it in the unit:

```sh
sudo systemctl edit osdos
```

```ini
[Service]
Environment=DATA_ROOT=/home/pi/osdos-data
```

### The OSDOS_ variables

Each is also read under its 240-MP name, `MP240_<NAME>`, when the `OSDOS_` one isn't set.

| Variable | Set by | What it does |
|---|---|---|
| `OSDOS_AUTOSTART` | The autostart service (`Environment=OSDOS_AUTOSTART=1`) | Set to anything: OSD/OS runs as the system's app. Quit offers Power Off, Restart and Exit to Terminal (and Save Logs with `OSDOS_MEDIA_DIR`), and Display Output can be offered |
| `OSDOS_LAUNCHER_API` | The launcher, `/usr/local/bin/osdos` (`9`) | What the launcher can do, each level adding to those below. Set at all, with the app in `/opt/osdos`: in-app updates. `2`: Restart in Quit. `3`: Display Output. `4`: JAMMA 240p 15 kHz (720x240). `5`: Switch and Power Off, At Next Start. `6`: the JAMMA board's other modes. `7`: Wi-Fi. `8`: renaming the device. `9`: JAMMA 480i 15 kHz (640x480) |
| `OSDOS_DRM_DEVICE`, `OSDOS_DRM_CONNECTOR`, `OSDOS_DRM_MODE` | The launcher, on a Pi 5, from the display preset's `# osdos-output:` and `# osdos-mode:` lines | The output OSD/OS draws on and mpv plays on: `--drm-device`, `--drm-connector` and, when set, `--drm-mode` ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv#per-device-decode-profiles)) |
| `OSDOS_MEDIA_DIR` | The OSD/OS image: `/media/OSD-OS`, in `/etc/systemd/system/osdos.service.d/osdos-media.conf` | The folder the media go in; else `media` in the data folder. Default folders there: Local Files' `Movies`, Playlists' `Movies/Playlists`, the Arcade's `Games/Arcade` |
| `OSDOS_BOOT_UNITS_FILE` | The OSD/OS image: `/etc/osdos/boot-units` | The services the boot screen follows, as `unit\|LABEL` lines. Unset, there is no boot screen |
| `OSDOS_READY_FILE` | The OSD/OS image: `/run/osdos/ready` | Written, with OSD/OS's process id, once its first frame is on screen |
| `OSDOS_BOOT_DIR` | Nothing; `/boot/firmware` by default | Where Display Output finds `osdos-display.txt` and its presets, and Wi-Fi the country in `cmdline.txt` |
| `OSDOS_RUN_DIR` | Nothing; `/run` by default | Where the sockets are: `osdos-display.sock`, `osdos-wifi.sock`, `osdos-name.sock`, and the boot's preset, `osdos-display-boot.txt` |
| `OSDOS_ZONEINFO_DIR` | Nothing; `/usr/share/zoneinfo` by default | Where Wi-Fi finds the list of countries (`iso3166.tab`) |
| `OSDOS_WATCHDOG` | Nothing | `0`: no freeze reports, and no restart after 20 seconds frozen. For a debugger ([Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting#when-it-crashes-or-freezes-the-reports)) |
| `OSDOS_EMBEDDED_RENDER` | Nothing | With Transparent Background: `sw` draws the video on the CPU; `gpu` with OpenGL even when OpenGL runs on the CPU |
| `OSDOS_UPDATE_FEED_URL` | Nothing | The release feed Update asks, in place of `https://api.github.com/repos/mehmetraif/OSD-OS/releases/latest` |
| `OSDOS_NFC_DEBUG` | Nothing | Set to anything: the NFC Reader logs how it looks for readers |
| `OSDOS_NFC_SERIAL_DEVICE` | Nothing | A serial device to use as the PN532 reader (`/dev/ttyUSB0`), instead of looking for one |
| `OSDOS_WEB_PLAYER_UA` | Nothing | Read by `scripts/web-player.sh`: the user agent the Netflix and Prime Video browser sends |

Stand-ins, for tests:

| Variable | Stands in for |
|---|---|
| `OSDOS_BOARD_MODEL` | The board's model (`/proc/device-tree/model`): the decode profile and Display Output's outputs |
| `OSDOS_ASOUND_DIR` | `/proc/asound` (Audio Output) |
| `OSDOS_ALSA_CONFIG` | ALSA's configuration files (`/etc/asound.conf`, `~/.asoundrc`) |
| `OSDOS_AMIXER`, `OSDOS_DRM_DIR`, `OSDOS_HWMON_DIR`, `OSDOS_PLATFORM_DIR`, `OSDOS_USB_DEVICES_DIR` | `amixer`, `/sys/class/drm`, `/sys/class/hwmon`, `/sys/devices/platform`, `/sys/bus/usb/devices` (Audio Output) |
| `OSDOS_DEVICE_NAME` | The device's hostname (System Information, LAN Play's default name) |
| `OSDOS_SYSTEM_ROOT` | `/`, where System Information reads `/proc` and `/sys` |
| `OSDOS_MOUNTINFO` | `/proc/self/mountinfo` (Local Files' USB drives) |
| `OSDOS_TMDB_URL`, `OSDOS_WIKIDATA_URL` | TMDB's API (`https://api.themoviedb.org/3`) and Wikidata's query service |
| `OSDOS_ARCADEDB_URL`, `OSDOS_GENRES_URL`, `OSDOS_LOBBY_URL` | The Arcade module's services: ArcadeDB, the genres' list, RetroArch's lobby |

Other variables:

- `QT_QPA_EGLFS_HIDECURSOR=1`: set by OSD/OS when unset.
- `QT_QPA_PLATFORM`: set by the launcher when unset, to `wayland` with `WAYLAND_DISPLAY`, `xcb` with `DISPLAY`, else `eglfs`.
- For `eglfs` the launcher sets `QT_QPA_EGLFS_ALWAYS_SET_MODE=1`, `QT_QPA_EGLFS_KMS_ATOMIC=1` and `QT_QPA_EGLFS_KMS_CONFIG` (`$XDG_RUNTIME_DIR/osdos-kms.json`, or `/tmp/osdos-kms.json`).
- `MALLOC_ARENA_MAX`: unset on Linux, OSD/OS keeps glibc's allocator to two arenas.
- `os/build.sh`'s variables: [The OSD/OS image](https://github.com/mehmetraif/OSD-OS/wiki/The-OSD-OS-Image).

### What OSD/OS hands on

| To | Variables |
|---|---|
| An mpv process | `APP_ROOT`. On Linux `FONTCONFIG_FILE=/tmp/osdos-fonts.conf` (the VCR font). `ALSA_CARD`, as for a script. On a Linux desktop with `DISPLAY` set, `WAYLAND_DISPLAY` is removed |
| A script (the Scripts module) and a takeover program (`web-player.sh`, MAME) | `APP_ROOT`, `DATA_ROOT`. `OSDOS_MODE` (`console` or `takeover`) and `OSDOS_VT`, also as `MP240_MODE` and `MP240_VT`. `ALSA_CARD`, when Audio Output names a card. On a Pi 5 `SDL_KMSDRM_DEVICE_INDEX`. The players' pads: `OSDOS_PLAYER1_NAME`, `_GUID`, `_DEVICE`, and players 2 to 6's |

## What is in it

```text
~/.local/share/OSD-OS/
├── config.json                 every setting: the app's and each module's
├── lists.json                  each module's Recently Watched and Favorites
├── local_files_history.json    Local Files' resume points
├── nfc_reader_history.json     the NFC Reader's resume points
├── youtube_history.json        YouTube's Recently Watched, with resume points
├── youtube_watch_later.json    YouTube's Watch Later
├── youtube_subscriptions.txt   you write it: YouTube channel ids, one a line
├── youtube_playlists.txt       you write it: YouTube playlists, one a line
├── playlists.json              the Playlists module's lists, where each stopped, its downloads
├── playlists/                  the m3u a list was last played from
├── plex_auth.json              the Plex sign-in            (owner-only)
├── plex_key.pem                Plex's key for this device  (owner-only)
├── jellyfin_auth.json          the Jellyfin sign-in        (owner-only)
├── emby_auth.json              the Emby sign-in            (owner-only)
├── tmdb_api_key.txt            you write it: the TMDB key for Netflix and Prime Video
├── weather_location.txt        you write it: Weather's places
├── weather_music.txt           you write it, if you like: Weather's music
├── custom_color_schemes.json   you write it, if you like: color schemes of your own
├── custom_color_scheme.json    you write it, if you like: the scheme called Custom
├── input.cfg                   you write it, if you like: gamepad buttons
├── gamecontrollerdb.txt        you add it, if you like: SDL mappings for unusual pads
├── display-output.json         a Display Output switch under way (the image)
├── osdos-setup.json            you put it, if you like: OSD/OS Setup's settings, set at the next start
├── crash-report.txt            where the latest crash happened (Linux)
├── stall-report.txt            where the latest freeze happened (Linux)
├── themes/                     your themes, a folder each
├── skins/                      your skins, a folder each
├── soundfonts/                 SoundFonts (.sf2) for MIDI menu music
├── bin/yt-dlp                  the yt-dlp OSD/OS and mpv use first
├── bin/mame                    you add it, if you like: a MAME of your own, used first
├── media/                      Movies/ and Games/Arcade/: Local Files' and the Arcade's folders when nothing else is named (not on the image)
├── ambient/                    Ambient:Mode's folder when nothing else is named
├── nfc_tags/                   the NFC Reader's card files
├── user_scripts/               the Scripts module's folder when nothing else is named
├── arcade/                     the Arcade module's MAME files: games.json, playtime.json, cfg/, nvram/, hiscore/, ctrlr/, sta/, snap/…
├── highscores/                 the High Scores module's: scores.json, and mame/, links to MAME's files
├── netflix/browser/            Chromium's profile for Netflix: the sign-in
├── prime_video/browser/        Chromium's profile for Prime Video
├── youtube/browser/            Chromium's profile for the YouTube sign-in
└── updates/                    downloads of Settings → System → Update
```

A file or folder appears only once something needs it. A module's own files are described on its page.

| File or folder | What it holds |
|---|---|
| `lists.json` | `{ "<module id>": { "recent": [...], "favorites": [...] } }`, newest first. Recently Watched keeps 30, Favorites 100 |
| `local_files_history.json`, `nfc_reader_history.json` | `{ "<path>": { "pos": <ms>, "plPos": <video in a playlist, or -1> } }` |
| `youtube_history.json` | By video id: `{ "pos", "title", "channelName", "lastPlayed" }` |
| `playlists/` | Each m3u is owner-only, as it may hold a server's token |
| `plex_auth.json`, `plex_key.pem`, `jellyfin_auth.json`, `emby_auth.json` | Sign-ins and tokens, owner-only. Sign out deletes a module's own |
| `tmdb_api_key.txt` | Its first line that isn't empty or a `#` comment: a TMDB v3 API key, or a v4 read access token |
| `input.cfg`, `gamecontrollerdb.txt` | Gamepad remapping, read again when it changes; extra SDL mappings, read at start ([Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls)) |
| `display-output.json` | `{ "from", "to" }` while a new output waits to be kept, `{ "reverted" }` after one wasn't. Deleted afterwards |
| `osdos-setup.json` | Settings from [OSD/OS Setup](https://osdos.tv/setup/). At the next start OSD/OS merges its `config` into `config.json` and deletes it. One beside the app is taken the same way |
| `crash-report.txt`, `stall-report.txt` | The latest crash, or freeze of 5 seconds, only ([Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting#when-it-crashes-or-freezes-the-reports)) |
| `themes/`, `skins/` | One with the folder name of one of OSD/OS's replaces it |
| `soundfonts/` | For a MIDI file with no SoundFont of its name beside it: the first `.sf2` here by name ([Menu music](https://github.com/mehmetraif/OSD-OS/wiki/Menu-Music)) |
| `bin/yt-dlp` | Found first, before one beside the `osdos` binary and one on `PATH`. On the image a timer updates it after each boot and once a day |
| `bin/mame` | Found first, before `mame` on `PATH` and `/usr/games/mame` ([Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#setting-it-up)) |
| `arcade/`, `highscores/` | Listed in [Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#files-it-keeps) and [High Scores](https://github.com/mehmetraif/OSD-OS/wiki/High-Scores#files-it-keeps) |
| `<service>/browser/` | A Chromium profile per service, with its sign-in. Sign out deletes it |
| `updates/` | The downloaded release with `staged.json` and `staged.sha256` (written at Apply), unfinished `*.part` downloads, and on an AppImage or a Mac the helper script and its `apply.log` |

## config.json

`config.json` is one JSON object: `app` holds OSD/OS's own settings, `modules` each module's under its id.

```json
{
    "app": {
        "<key>": "<value>"
    },
    "modules": {
        "<module id>": {
            "<key>": "<value>"
        }
    }
}
```

- A key is written only once its setting has been changed; a missing key has its default.
- The first change on a new install also writes `"color_scheme": "Video 1"`.
- The file is indented by four spaces, with the keys in alphabetical order.

### A complete example

The `config.json` of an OSD/OS image on a Pi 4, with Local Files and Plex set up. The Plex ids are examples of their shape.

```json
{
    "app": {
        "audio_output": {
            "card": "Headphones",
            "name": "AV Jack"
        },
        "background_effect": "",
        "boot_colors": "Theme",
        "color_scheme": "",
        "help_line": "On",
        "hint_bar": "On",
        "info_screen": "3",
        "loading_colors": "Theme",
        "loading_effect": "On",
        "menu_music": "File",
        "menu_music_file": "/media/OSD-OS/Music/menu.mid",
        "menu_music_volume": 40,
        "mouse_pointer": "5",
        "osd_background": "Window",
        "osd_frame": "Shadow",
        "osd_theme": "trinitron",
        "remote_keymap": {
            "back": 33554604,
            "down": 0,
            "left": 0,
            "right": 0,
            "select": 0,
            "up": 0
        },
        "screen_effect": "",
        "screensaver_timeout": "120",
        "selector_effect": "Off",
        "skin": "",
        "startup_favorite": {
            "module": "com.osdos.local_files",
            "name": "Rambo Marathon.m3u",
            "path": "/media/OSD-OS/Movies/Rambo Marathon.m3u"
        },
        "startup_from": "Resume",
        "startup_module": "com.osdos.local_files",
        "text_effect": "",
        "transition": "",
        "transparent_background": 40,
        "video_logo": "br",
        "video_logo_image": "",
        "video_output_levels": "Auto",
        "video_scaling": "14:9"
    },
    "modules": {
        "com.osdos.local_files": {
            "auto_subtitles": "forced",
            "enabled": true,
            "hide_extensions": true,
            "image_duration": "10",
            "loop_playback": false,
            "media_directory": "",
            "resume_playback": "yes",
            "shuffle_playback": "ask",
            "sub_lang": "en",
            "video_scaling": "Default"
        },
        "com.osdos.plex": {
            "auto_sign_in": true,
            "autoplay_next_episode": true,
            "current_user_id": "28451337",
            "enabled": true,
            "libraries": {
                "9f1c2e7a4b6d8f0a3c5e7b9d1f2a4c6e8b0d2f4a_1": true,
                "9f1c2e7a4b6d8f0a3c5e7b9d1f2a4c6e8b0d2f4a_2": false
            },
            "resume_playback": "yes",
            "server_machine_id": "9f1c2e7a4b6d8f0a3c5e7b9d1f2a4c6e8b0d2f4a",
            "video_quality": "auto",
            "video_scaling": "Default"
        }
    }
}
```

### The app's keys

Every key Settings writes, with its row. The values and defaults are on [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings).

| Key | Settings row | Type |
|---|---|---|
| `osd_theme` | Theme | string: a theme's folder name, `""` for None |
| `orb_fog` | Orb Fog | string: `"Picture"` (when unset) or `"Live"` |
| `color_scheme`, `skin`, `text_effect`, `background_effect`, `selector_effect`, `screen_effect`, `transition`, `menu_music` | The theme's parts | string: `""` for THEME, `"Off"`, or a name (`skin` takes a folder name) |
| `menu_music_file` | Music File | string: a full path, `""` for none |
| `menu_music_volume` | Music Volume | number, 0 to 100 |
| `osd_background`, `osd_frame` | OSD Background, Window Frame | string |
| `menu_layout` | Menu Layout | string: `"Standard"`, `"Camcorder"`, `"Studio Monitor"`, `"Broadcast"`, `"Flat Panel"` |
| `startup_module` | Start on Module | string: a module id, or `"None"` |
| `startup_favorite` | Play at Startup | object `{ "module", "path", "name" }`, or `""` |
| `startup_from` | Startup From | string: `"Resume"` or `"Beginning"` |
| `cabinet_lock` | Cabinet Lock | string: `"On"` or `"Off"` |
| `unlock_buttons` | Cabinet Lock → Unlock Buttons | array of one to four `{ "input", "name" }`. `input`: a pad's button by its player (`"p1/start"`, `"p2/dpup"`), or a key by its Qt key code (`"key/49"` for 1). `name`: what the page shows (`"P1 START"`) |
| `monitor` | Monitor | string: `"Horizontal"`, `"Turned Right"`, `"Turned Left"` |
| `smooth_playback` | 1080p Playback (Pi 3) | string: `"On"` or `"Off"` |
| `video_scaling` | Scaling | string: `"Letterbox"`, `"14:9"`, `"Pan & Scan"`, `"Anamorphic"` |
| `transparent_background` | Transparent Background | number, 0 to 100, or the string `"Off"`. `"On"` and `"Dim"` read as 0 and 60 |
| `video_output_levels` | Video Levels | string: `"Auto"`, `"Limited"`, `"Full"` |
| `loading_effect`, `hint_bar`, `help_line`, `status_icons` | Loading Effect, Hint Bar, Help Line, Status Icons | string: `"On"` or `"Off"` |
| `loading_colors`, `boot_colors` | Loading Screen Colors, Boot Screen Colors | string: `"Theme"` (when unset) or `"Default"` |
| `video_logo` | Channel Logo | string: `"off"`, `"tl"`, `"tr"`, `"bl"`, `"br"`, `"all"` |
| `video_logo_image` | Logo Image | string: a full path, `""` for OSD/OS's logo |
| `screensaver_timeout` | Screen Saver | string: `"OFF"`, `"30"`, `"60"`, `"120"` |
| `mouse_pointer` | Mouse Pointer | string: `"off"`, `"2"`, `"5"`, `"10"`, `"30"`, `"always"` |
| `window_mode` | Window Mode | string: `"Off"` or `"On"`. Windows and Linux or BSD desktops only |
| `info_screen` | Info Screen | string: `"off"`, `"key"`, `"1"`, `"2"`, `"3"`, `"5"` |
| `audio_output` | Audio Output | object `{ "card", "name" }` (the card's ALSA id and its name), or `""` for Auto |
| `remote_keymap` | Controls → All Keyboards & Remotes | object: `up`, `down`, `left`, `right`, `select`, `back`, each a key's number, `0` for none |
| `players` | Controls → Players | object: `"1"` to `"6"`, each `{ "key", "name" }`, a pad's key (`""` for Auto) and its name |
| `controllers` | Controls → a controller's page | object by controller key (`pad-<SDL GUID>`, `kbd-<bus>-<vendor>-<product>`), each with `name`, `kind`, `labels` and `bindings` ([Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls#how-a-controllers-settings-are-saved)) |

Three keys have no row and are set by hand:

```json
{
    "app": {
        "display_index": 1,
        "mpv_video_args": "--vo=drm --hwdec=v4l2m2m-copy",
        "auto_crop": "On"
    }
}
```

- `display_index`: the display the menus open on and mpv plays on, on a Mac or a Linux desktop. Read at start; an index out of range falls back to 0. The log lists the displays: `[main] display index 1: "<name>" 1280x720 at (3840,0)`.
- `mpv_video_args`: mpv flags, separated by spaces, that replace the video output and decoder flags OSD/OS picks. Read at every video ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv#mpv_video_args)).
- `auto_crop`: `"On"` reads as Pan & Scan while `video_scaling` isn't set.

### A module's keys

A module's settings are under `modules.<module id>`, with the keys its `manifest.json` names.

| Kind of row | Saved as | Example |
|---|---|---|
| Toggle | `true` or `false` | `"loop_playback": false` |
| A fixed list | the choice as shown | `"video_scaling": "Pan & Scan"` |
| A list the module fills in | the choice's id, which the module chooses | Local Files' `"resume_playback": "yes"` for Always |
| A multi-select page | an object of ids, each `true` or `false`; one missing is on | `"libraries": { "<id>": false }` |
| A folder | its path, `""` for the module's own | `"media_directory": "/media/usb/FILMS"` |

- Every module has `enabled` (`true` or `false`). Unset, the manifest's default applies: Local Files and Playlists on, the others off.
- A module's id is `com.osdos.` and its folder name: `ambient_mode`, `arcade`, `emby`, `highscores`, `jellyfin`, `local_files`, `netflix`, `nfc_reader`, `playlists`, `plex`, `prime_video`, `scripts`, `weather`, `youtube`.

### How OSD/OS reads and writes it

- **Read from the disk.** `appCore.get_setting(moduleId, key)` returns what `config.json` holds now, `get_settings()` the whole file. A key with one dot reads one level down: `get_setting("", "remote_keymap.back")` is `app.remote_keymap.back`.
- **Written whole.** `appCore.save_setting(moduleId, key, value)` changes the one key and writes the file again, atomically (`writeFileAtomically()`), as every state file is. A crash or a full disk leaves the old file.
- **Then announced** with `appSettingChanged` or `moduleSettingChanged`. A change that couldn't be written is not announced, and `save_setting` returns `false`.
- **A missing file** reads as `{ "app": { "color_scheme": "Video 1" }, "modules": {} }`, and the first change writes a new file.
- **An invalid file is left alone.** A `config.json` that isn't valid JSON reads as those defaults but is never written over: every `save_setting` returns `false` until it is fixed. `lists.json` is kept the same way.

When each part is read:

| What | When it is read |
|---|---|
| The look, Hint Bar, Help Line, Mouse Pointer, Window Mode, Screen Saver, Audio Output, Controls, Cabinet Lock, the modules' folders | At start, then at each change made in Settings |
| `display_index`, Start on Module, Play at Startup, Startup From | At start |
| Scaling, Video Levels, Channel Logo, Logo Image, Screen Saver's script, 1080p Playback, `mpv_video_args`, Transparent Background on or off | As each video starts |
| A module's other settings | When the module needs them: most as a video starts or a list opens |

A change made by hand while OSD/OS runs reaches the per-video settings at the next video, and the rest at the next start.

## Editing by hand

1. **Stop OSD/OS.**
   - Under the autostart service, over SSH: `sudo systemctl stop osdos`. This leaves the Pi on; Settings → Quit would power it off.
   - Run by hand, or on a Mac: Settings → Quit, or Ctrl+Q.
2. **Edit**, for example `nano ~/.local/share/OSD-OS/config.json`. One missing comma makes the file unreadable.
3. **Check it** before starting. An error names the line and the column:

   ```sh
   python3 -m json.tool ~/.local/share/OSD-OS/config.json > /dev/null && echo "config.json is valid"
   ```

4. **Start OSD/OS**: `sudo systemctl start osdos`, or `osdos` by hand.

Without stopping OSD/OS:

- Settings from [OSD/OS Setup](https://osdos.tv/setup/): Settings → System → Paste OSD/OS Setup ([Installation](https://github.com/mehmetraif/OSD-OS/wiki/Installation#osdos-setup-on-a-computer)).
- A module's own files (`tmdb_api_key.txt`, `youtube_subscriptions.txt`, `weather_location.txt`): open the module again to see the change.
- `input.cfg`: it takes effect as soon as it is saved.

### Custom color schemes

Settings → Look → Color Scheme offers schemes of your own from two files in the data folder, read at start.

`custom_color_schemes.json` adds any number, each under its name (3 to 28 characters: letters, digits, spaces and ASCII punctuation but `"` and `` ` ``):

```json
{
    "Phosphor Blue": {
        "primary": "#9FD8FF",
        "secondary": "#6FA8CF",
        "tertiary": "#3F789F",
        "surface": "#001428",
        "accent": "#FFFFFF"
    }
}
```

`custom_color_scheme.json` adds one, which Settings calls **Custom**:

```json
{
    "primary": "#FFE0A0",
    "secondary": "#C0A070",
    "tertiary": "#806040",
    "surface": "#201000",
    "accent": "#FFE0A0"
}
```

- Each scheme needs all five keys, each a `#rrggbb` colour, or it is left out with a line in the log.
- OSD/OS draws with two of them: `primary` (text, lines, the selection) and `surface` (the background).
- A scheme named like one of OSD/OS's (`Video 1`) changes that one's colours.
- Restart OSD/OS after changing either file.
- A chosen scheme that is gone: **Custom** goes back to THEME at the next start; a named one draws as Video 1.
- A theme can carry its own two colours instead ([Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes)).

## Backing up

With OSD/OS stopped, a copy of the data folder is a full backup. The sign-in files hold tokens: keep the copy private.

```sh
sudo systemctl stop osdos
tar -czf ~/osdos-backup-$(date +%F).tar.gz -C ~/.local/share OSD-OS
sudo systemctl start osdos
```

- Add `--exclude=OSD-OS/updates` before `-C` to leave out the downloaded updates.
- Keep `bin/`: on the image nothing puts yt-dlp back once it is gone.
- On a Mac, quit OSD/OS and use `-C ~/"Library/Application Support" OSD-OS`.

To restore, stop OSD/OS, move the folder aside, and unpack the copy in its place:

```sh
sudo systemctl stop osdos
mv ~/.local/share/OSD-OS ~/.local/share/OSD-OS.old
tar -xzf ~/osdos-backup-2026-10-10.tar.gz -C ~/.local/share
sudo systemctl start osdos
```

- Delete `OSD-OS.old` once all is well.
- Restore onto the same user's home: the files belong to the user who made them.
- On the image, the films and games on the card's OSD-OS partition (`/media/OSD-OS/Movies`, `/media/OSD-OS/Games`) are not in the data folder.

## Coming from 240-MP

OSD/OS carries over what a 240-MP install left:

- **The data folder.** At its first start, when `OSD-OS` doesn't exist or is empty, OSD/OS moves `~/.local/share/240-MP` to `~/.local/share/OSD-OS` (on a Mac, in `~/Library/Application Support`). The log says `[legacy] moved the 240-MP data folder … to …`. Should the move fail, it uses the 240-MP folder where it is.
- **Module ids.** At every start, keys and values beginning `com.240mp.` in the data folder's `.json` files, `nfc_tags` included, are renamed `com.osdos.`.
- **Environment variables.** Every `OSDOS_<NAME>` is read as `MP240_<NAME>` when unset. `web-player.sh` reads `MP240_WEB_PLAYER_UA`, and `scripts/rpi-run-local.sh` `MP240_BIN`.
- **Old settings.** `app.theme` is read while `app.skin` is unset, and `app.auto_crop` while `app.video_scaling` is. The `true`/`false` values of Local Files' Shuffle Playback and Auto Show Subtitles still read.
- **The install.** `install.sh` removes a 240-MP install (`240mp.service`, `/opt/240mp`, `/usr/local/bin/240mp`) first. An app still running from `/opt/240mp` can't update itself: run the installer once ([Installation](https://github.com/mehmetraif/OSD-OS/wiki/Installation)).

## The cache folder

OSD/OS caches one kind of file: menu music made into a WAV from a MIDI file (FluidSynth) or from a tracker's module mpv can't play (openmpt123).

| System | Cache folder |
|---|---|
| Linux, the OSD/OS image | `~/.cache/OSD-OS/menu-music/`, or `$XDG_CACHE_HOME/OSD-OS/menu-music/` |
| macOS | `~/Library/Caches/OSD-OS/menu-music/` |

Only the newest WAV is kept, and a changed file is made again. Deleting the folder is always safe.

## Logs and temporary files

| What | Where |
|---|---|
| OSD/OS's own log | Its standard output and error. Under the autostart service: `journalctl -u osdos -b` (this boot), `journalctl -u osdos -f` (as it happens). A Mac app opened from the Finder: Console.app. On Windows: `osdos.log` in the data folder |
| mpv's messages | In OSD/OS's log as `[mpv] …` lines, with every token and an address's sign-in (`user:password@`) blanked out |
| mpv's full log | `/tmp/osdos-mpv.log`, owner-only: the latest video's, verbose, with its command line (and tokens) |
| The boot screen's timings (the image) | `journalctl -b -u osdos \| grep '\[boot\]'` |
| USB drives (the image) | `journalctl -u 'osdos-usb-mount@*'` |
| yt-dlp's updates (the image) | `journalctl -u osdos-yt-dlp-update` |
| An AppImage's or a Mac's update | `updates/apply.log` in the data folder |

A Release build leaves out OSD/OS's debug lines, such as `[AppCore] Setting saved: app.video_scaling = 14:9` and `[MpvController] launch: mpv …` ([Building from source](https://github.com/mehmetraif/OSD-OS/wiki/Building-from-Source)).

OSD/OS's temporary files go in the system's temporary folder (`/tmp`, or `$TMPDIR`):

| File | What it is |
|---|---|
| `osdos-mpv.sock` | The IPC socket of the video playing |
| `osdos-input.conf` | The key bindings each video's mpv gets ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv#back-and-the-end-of-a-video)) |
| `osdos-mpv.log` | mpv's log, above |
| `osdos-mpv-subinfo.json` | Subtitle tracks' names for the deck menu, while a server's sidecar subtitles play |
| `osdos-logo.bgra` | Logo Image's picture, in mpv's overlay format |
| `osdos-fonts.conf` | The fontconfig file that shows mpv the VCR font |
| `osdos-menu-music-<pid>.sock`, `osdos-weather-music.sock` | The menu music's and Weather's music's mpv sockets |

## See also

- [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings): every row, with its key
- [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv): `mpv_video_args`, `mpv.conf` and mpv's log
- [Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes) and [Skins](https://github.com/mehmetraif/OSD-OS/wiki/Skins): what goes in `themes/` and `skins/`
- [Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls): `input.cfg` and `gamecontrollerdb.txt`
- [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting)
