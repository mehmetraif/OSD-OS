# Configuration files

Everything OSD/OS keeps lives in one folder of its own, the **data folder**: the settings, the lists, the sign-ins, your themes and skins, and the files some modules read. This page says where that folder is, what is in it, the shape of `config.json`, how OSD/OS reads and writes it, how to edit and back it up safely, how a 240-MP install is carried over, and where the cache and the logs go.

## The data folder

| System | Data folder |
|---|---|
| The OSD/OS image | `/home/pi/.local/share/OSD-OS/` (the image's first user, `pi` unless it was built with another) |
| Raspberry Pi OS, other Linux, SteamOS (the AppImage) | `~/.local/share/OSD-OS/`, or `$XDG_DATA_HOME/OSD-OS/` when `XDG_DATA_HOME` is set |
| macOS | `~/Library/Application Support/OSD-OS/` |

- OSD/OS creates it at its first start. Updating, reinstalling or removing the app leaves what is in it as it is: an update keeps every setting, and a new install finds the old ones.
- It belongs to the user OSD/OS runs as. Under the autostart service (`install.sh`'s and the image's) that is the service's user, so the folder is in that user's home, not root's.
- The environment variable `DATA_ROOT` names another folder in its place (below).

## Environment variables

### APP_ROOT and DATA_ROOT

| Variable | What it names | Without it |
|---|---|---|
| `APP_ROOT` | OSD/OS's own files: `Main.qml`, `views/`, `modules/`, `assets/`, `scripts/` (mpv's Lua scripts) and `LICENSE`. OSD/OS hands it on to mpv and to scripts | Inside a Mac app, its `Contents/Resources`; else `share/osdos` beside the binary's folder when there is one (`/opt/osdos/share/osdos` for `/opt/osdos/bin/osdos`, `usr/share/osdos` in the AppImage); else the folder above the binary's |
| `DATA_ROOT` | The data folder | The folder for the system, in the table above |

Both must name a folder that exists. OSD/OS creates the default data folder, but not one named by `DATA_ROOT`: create it first.

```sh
mkdir -p ~/osdos-test-data
DATA_ROOT=~/osdos-test-data APP_ROOT=$(pwd) ./build/osdos
```

A data folder of its own is a quick way to try a build, or a change to `config.json`, without touching your real settings. Under `DATA_ROOT` the 240-MP data folder is not moved over (see [Coming from 240-MP](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#coming-from-240-mp)).

`DATA_ROOT` is read outside the app too: the launcher (`/usr/local/bin/osdos`) looks for staged updates in `$DATA_ROOT/updates`, and `scripts/web-player.sh` keeps the browser profiles under it. Under the autostart service, set it in the unit as well as anywhere else, or the launcher won't find the updates:

```sh
sudo systemctl edit osdos
```

```ini
[Service]
Environment=DATA_ROOT=/home/pi/osdos-data
```

### The OSDOS_ variables

OSD/OS reads these. Each is also read under its 240-MP name, `MP240_<NAME>`, when the `OSDOS_` one isn't set, so a launcher, a service or a script from before the name changed goes on working.

| Variable | Set by | What it does |
|---|---|---|
| `OSDOS_AUTOSTART` | The autostart service (`Environment=OSDOS_AUTOSTART=1`) | Set to anything: OSD/OS runs as the system's app. Quit offers Power Off, Restart and Exit to Terminal; Update's Apply & Restart exits for the service to start it again; Display Output can be offered |
| `OSDOS_LAUNCHER_API` | The launcher, `/usr/local/bin/osdos` (`5`) | What the launcher and its stop helper can do. Set at all, with the app in `/opt/osdos`: in-app updates can be applied. `2` or more: Quit offers Restart. `3` or more: Settings offers Display Output. `4` or more: JAMMA 240p among its outputs. `5` or more: Switch and Power Off and At Next Start |
| `OSDOS_DRM_DEVICE`, `OSDOS_DRM_CONNECTOR`, `OSDOS_DRM_MODE` | The launcher, on a Pi 5, from the display preset's `# osdos-output:` and `# osdos-mode:` lines | The output OSD/OS draws on, which mpv plays on too: `--drm-device`, `--drm-connector` and, when set, `--drm-mode` ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv#per-device-decode-profiles)) |
| `OSDOS_MEDIA_DIR` | The OSD/OS image: `/media/OSD-OS`, in `/etc/systemd/system/osdos.service.d/osdos-media.conf` | Local Files' folder while its Media Directory is Default; else `media` in the data folder. The Playlists module downloads into a `Playlists` folder there unless its Download Folder names another, and the Arcade module looks for its games in `Games/Arcade` there unless its Games Folder names another |
| `OSDOS_BOOT_UNITS_FILE` | The OSD/OS image: `/etc/osdos/boot-units` | The services the boot screen follows, as `unit\|LABEL` lines. Unset, there is no boot screen |
| `OSDOS_READY_FILE` | The OSD/OS image: `/run/osdos/ready` | Written, with OSD/OS's process id, once its first frame is on screen: the image's held-back services wait for it |
| `OSDOS_BOOT_DIR` | Nothing; `/boot/firmware` by default | Where Display Output finds `osdos-display.txt` and its presets |
| `OSDOS_RUN_DIR` | Nothing; `/run` by default | Where Display Output finds At Next Start's socket (`osdos-display.sock`) and the preset this boot started on (`osdos-display-boot.txt`), for tests |
| `OSDOS_WATCHDOG` | Nothing | `0`: no freeze reports, and no restart after 20 seconds frozen under the service. For a debugger, which stops the app at a breakpoint ([Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting#when-it-crashes-or-freezes-the-reports)) |
| `OSDOS_EMBEDDED_RENDER` | Nothing | With Transparent Background: `sw` draws the video on the CPU always; `gpu` draws it with OpenGL even when OpenGL itself runs on the CPU (Mesa's llvmpipe), for tests |
| `OSDOS_UPDATE_FEED_URL` | Nothing | The release feed Update asks, in place of `https://api.github.com/repos/mehmetraif/OSD-OS/releases/latest` |
| `OSDOS_NFC_DEBUG` | Nothing | Set to anything: the NFC Reader logs how it looks for readers |
| `OSDOS_NFC_SERIAL_DEVICE` | Nothing | A serial device to use as the PN532 reader (`/dev/ttyUSB0`), instead of looking for one |
| `OSDOS_BOARD_MODEL` | Nothing | Stands in for the board's model (`/proc/device-tree/model`), for tests: it picks the decode profile and the outputs Display Output offers |
| `OSDOS_ASOUND_DIR` | Nothing | Stands in for `/proc/asound` (Audio Output), for tests |
| `OSDOS_MOUNTINFO` | Nothing | Stands in for `/proc/self/mountinfo` (Local Files' USB drives), for tests |
| `OSDOS_TMDB_URL`, `OSDOS_WIKIDATA_URL` | Nothing | Stand in for TMDB's API (`https://api.themoviedb.org/3`) and Wikidata's query service, for tests |
| `OSDOS_WEB_PLAYER_UA` | Nothing | Read by `scripts/web-player.sh`: the user agent the Netflix and Prime Video browser sends |

Two Qt variables matter too. OSD/OS sets `QT_QPA_EGLFS_HIDECURSOR=1` for itself when it is unset, so Qt's own cursor never shows. The launcher picks the platform, `QT_QPA_PLATFORM` (`wayland` with `WAYLAND_DISPLAY`, `xcb` with `DISPLAY`, else `eglfs`), and for `eglfs` sets `QT_QPA_EGLFS_ALWAYS_SET_MODE=1`, `QT_QPA_EGLFS_KMS_ATOMIC=1` and `QT_QPA_EGLFS_KMS_CONFIG`, a file naming the display card (`$XDG_RUNTIME_DIR/osdos-kms.json`, or `/tmp/osdos-kms.json`). A `QT_QPA_PLATFORM` already set is left as it is. On Linux, glibc's `MALLOC_ARENA_MAX` matters as well: unset, OSD/OS keeps its allocator to two arenas, so that a video's threads don't each leave memory behind; set, it decides. The variables `os/build.sh` takes to build the image are in [The OSD/OS image](https://github.com/mehmetraif/OSD-OS/wiki/The-OSD-OS-Image).

### What OSD/OS hands on

| To | Variables |
|---|---|
| An mpv process | `APP_ROOT`; on Linux `FONTCONFIG_FILE=/tmp/osdos-fonts.conf`, so mpv's on-screen menu finds the VCR font; on a Linux desktop with `DISPLAY` set, `WAYLAND_DISPLAY` removed, so mpv runs on X11 or Xwayland |
| A script (the Scripts module), and what runs as a takeover script: `web-player.sh` (Netflix, Prime Video, YouTube's sign-in) and MAME (the Arcade module) | `APP_ROOT`, `DATA_ROOT`, `OSDOS_MODE` (`console` or `takeover`), `OSDOS_VT` for a takeover on its own VT, the same two as `MP240_MODE` and `MP240_VT`, `ALSA_CARD` when Settings → Audio Output names a card, on a Pi 5 `SDL_KMSDRM_DEVICE_INDEX` (the number of the DRM card of the output the display preset names, for SDL programs), and the players' pads (`OSDOS_PLAYER1_NAME`, `_GUID`, `_DEVICE`, and players 2 to 6's) |

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
├── crash-report.txt            where the latest crash happened (Linux)
├── stall-report.txt            where the latest freeze happened (Linux)
├── themes/                     your themes, a folder each
├── skins/                      your skins, a folder each
├── soundfonts/                 SoundFonts (.sf2) for MIDI menu music
├── bin/yt-dlp                  the yt-dlp OSD/OS and mpv use first
├── bin/mame                    you add it, if you like: a MAME of your own, used first
├── media/                      Local Files' folder when nothing else is named (not on the image)
├── ambient/                    Ambient:Mode's folder when nothing else is named
├── nfc_tags/                   the NFC Reader's card files
├── user_scripts/               the Scripts module's folder when nothing else is named
├── arcade/                     the Arcade module's MAME files: games.json, playtime.json, cfg/, nvram/, hiscore/, ctrlr/, sta/, snap/…
├── highscores/                 the High Scores module's: scores.json, and mame/, links to MAME's files
├── netflix/browser/            Chromium's profile for Netflix: the sign-in
├── prime_video/browser/        Chromium's profile for Prime Video
├── youtube/browser/            Chromium's profile for the YouTube sign-in
└── updates/                    downloads of Settings → Update
```

A file or folder appears only once something needs it. The rest of this section says what each holds; the formats of a module's own files are on its page.

| File or folder | Written by | What it holds |
|---|---|---|
| `config.json` | OSD/OS, at every change in Settings or a module's settings; Plex and Jellyfin for the server and user they are on | The settings ([below](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#configjson)) |
| `lists.json` | OSD/OS | `{ "<module id>": { "recent": [...], "favorites": [...] } }`, each entry as the module's tree has it (`name`, `path`, …), newest first: Recently Watched keeps 30, Favorites 100. Local Files, Netflix, Prime Video, YouTube and Arcade use it |
| `local_files_history.json`, `nfc_reader_history.json` | Local Files, the NFC Reader | Where each file stopped, by its path: `{ "<path>": { "pos": <ms>, "plPos": <video in a playlist, or -1> } }` |
| `youtube_history.json` | YouTube | By video id: `{ "pos", "title", "channelName", "lastPlayed" }` |
| `youtube_watch_later.json` | YouTube | The Watch Later list |
| `youtube_subscriptions.txt`, `youtube_playlists.txt` | You | The channels and playlists YouTube lists ([YouTube](https://github.com/mehmetraif/OSD-OS/wiki/YouTube)) |
| `playlists.json`, `playlists/` | Playlists | The lists and their downloads; the m3u each list plays from, made afresh each time and readable by its owner only, since it may hold a server's token ([Playlists](https://github.com/mehmetraif/OSD-OS/wiki/Playlists)) |
| `plex_auth.json`, `plex_key.pem`, `jellyfin_auth.json`, `emby_auth.json` | Plex, Jellyfin, Emby | Sign-ins and tokens, readable by their owner only. Sign out in a module's settings deletes its own |
| `tmdb_api_key.txt` | You | The first line that isn't empty or a `#` comment: a TMDB v3 API key, or a v4 read access token ([Netflix and Prime Video](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video)) |
| `weather_location.txt`, `weather_music.txt` | You | Weather's places (a name, or `lat, lon`) and its optional music ([Weather](https://github.com/mehmetraif/OSD-OS/wiki/Weather)) |
| `custom_color_schemes.json`, `custom_color_scheme.json` | You | Color schemes of your own ([below](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#custom-color-schemes)) |
| `input.cfg`, `gamecontrollerdb.txt` | You | Gamepad remapping, read again as soon as it changes, and extra SDL controller mappings, read at start ([Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls)) |
| `display-output.json` | Settings → Display Output | `{ "from", "to" }` while a new output waits to be kept, `{ "reverted" }` after one wasn't. Deleted once the output is kept, or once OSD/OS has said what became of it |
| `crash-report.txt`, `stall-report.txt` | OSD/OS on Linux: at a crash, and when its screens stop answering for 5 seconds | The latest crash or freeze only: when, the memory, and where the app was ([Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting#when-it-crashes-or-freezes-the-reports)) |
| `themes/`, `skins/` | You | A theme or skin of the same folder name as one of OSD/OS's replaces it ([Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes), [Skins](https://github.com/mehmetraif/OSD-OS/wiki/Skins)). A `theme.json` of window pictures only in `themes/` is read as a skin |
| `soundfonts/` | You | MIDI menu music plays with a SoundFont beside the file of its name, else the first `.sf2` here by name, else the system's ([Menu music](https://github.com/mehmetraif/OSD-OS/wiki/Menu-Music)) |
| `bin/yt-dlp` | You; on the OSD/OS image a timer, after each boot and once a day | The yt-dlp found first: then one beside the `osdos` binary, then one on `PATH`. mpv's ytdl hook is pointed at the same one |
| `bin/mame` | You | The MAME the Arcade module finds first: then `mame` on `PATH`, then Debian's `/usr/games/mame` ([Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#setting-it-up)) |
| `media/`, `ambient/`, `user_scripts/`, `nfc_tags/` | You, and the modules | The modules' default folders, used while their folder setting is Default. Tapping an unknown card makes a stub file in `nfc_tags/`; a new script gets its `.txt` in `user_scripts/` |
| `arcade/` | The Arcade module, and MAME | `games.json`, what MAME said of each set and which MAME said it; `playtime.json`, how long each game has been played; `coins.lua`, the credits bought on the Arcade screen, for the game being started; `catver.ini`, the games' genres, when it isn't in the games folder; MAME's settings for each game (`cfg/`), the games' NVRAM (`nvram/`), the hiscore plugin's tables (`hiscore/`), save states, snapshots, input recordings and hard disk diffs (`sta/`, `snap/`, `inp/`, `diff/`), and the controller file written before each game (`ctrlr/osdos.cfg`) ([Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#files-it-keeps)) |
| `highscores/` | The High Scores module | `scores.json`, the tables read of each game's files in `arcade/`, why a file couldn't be read, and the games' names, so a file is read again only once it has changed; `mame/`, links to `arcade/hiscore`, `arcade/nvram` and MAME's `hiscore.dat`, as openhi2txt reads them ([High Scores](https://github.com/mehmetraif/OSD-OS/wiki/High-Scores#files-it-keeps)) |
| `<service>/browser/` | Chromium, through `web-player.sh` | A browser profile per service, which keeps its sign-in. Sign out deletes it. A run with a Scaling other than Letterbox also leaves `<service>/scaling/` (the small extension that applies it), and a run under `cage` keeps `<service>/wayland` while it lasts |
| `updates/` | Settings → Update | The downloaded release with `staged.json` (what it is), `staged.sha256` (written at Apply: it arms the release for the launcher), unfinished `*.part` downloads (deleted at the next start), and on an AppImage or a Mac the helper script and its `apply.log` |

## config.json

`config.json` is one JSON object with two parts: `app`, OSD/OS's own settings, and `modules`, an object holding each module's settings under its id.

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

A key is written only once its setting has been changed: one that isn't there has its default. (The very first change on a new install also writes `"color_scheme": "Video 1"`, the value OSD/OS starts from.) OSD/OS writes the file indented by four spaces, with the keys in alphabetical order, as below.

### A complete example

The `config.json` of an OSD/OS image on a Pi 4 that has been used for a while: a theme with its selector effect turned off, its own MIDI file as menu music, a window with a shadow, a favourite played at startup, Transparent Background at 40, the logo bottom right, the AV jack chosen for sound, and an air mouse's clicks added as Select and Back in Controls. Local Files, YouTube and Plex are set up. The Plex ids are examples of their shape.

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
            "back": 50331650,
            "down": 0,
            "left": 0,
            "right": 0,
            "select": 50331649,
            "up": 0
        },
        "screen_effect": "",
        "screensaver_timeout": "120",
        "selector_effect": "Off",
        "skin": "",
        "startup_favorite": {
            "module": "com.osdos.local_files",
            "name": "Rambo Marathon.m3u",
            "path": "/media/OSD-OS/Rambo Marathon.m3u"
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
        },
        "com.osdos.youtube": {
            "audio_language": "original",
            "display_shorts": false,
            "enabled": true,
            "max_frame_rate": "30",
            "playback_resolution": "480p",
            "playback_speed": "1x",
            "resume_playback": "Ask",
            "subtitle_language": "en",
            "subtitles": "Off",
            "video_codec": "H.264",
            "video_scaling": "Pan & Scan"
        }
    }
}
```

### The app's keys

Every key Settings writes, with the row it belongs to. The values and defaults are on [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings).

| Key | Settings row | Type |
|---|---|---|
| `osd_theme` | Theme | string: a theme's folder name, `""` for None |
| `color_scheme`, `skin`, `text_effect`, `background_effect`, `selector_effect`, `screen_effect`, `transition`, `menu_music` | The theme's parts | string: `""` for THEME, `"Off"`, or a name (`skin` takes a folder name) |
| `menu_music_file` | Music File | string: a full path, `""` for none |
| `menu_music_volume` | Music Volume | number, 0 to 100 |
| `osd_background`, `osd_frame` | OSD Background, Window Frame | string |
| `startup_module` | Start on Module | string: a module id, or `"None"` |
| `startup_favorite` | Play at Startup | object `{ "module", "path", "name" }`, or `""` |
| `startup_from` | Startup From | string: `"Resume"` or `"Beginning"` |
| `smooth_playback` | 1080p Playback (Pi 3) | string: `"On"` or `"Off"` |
| `video_scaling` | Scaling | string: `"Letterbox"`, `"14:9"`, `"Pan & Scan"`, `"Anamorphic"` |
| `transparent_background` | Transparent Background | number, 0 to 100, or the string `"Off"`. Its first values, `"On"` and `"Dim"`, still read as 0 and 60 |
| `video_output_levels` | Video Levels | string: `"Auto"`, `"Limited"`, `"Full"` |
| `loading_effect`, `hint_bar`, `help_line` | Loading Effect, Hint Bar, Help Line | string: `"On"` or `"Off"` |
| `loading_colors`, `boot_colors` | Loading Screen Colors, Boot Screen Colors | string: `"Theme"` (when unset) or `"Default"` |
| `video_logo` | Channel Logo | string: `"off"`, `"tl"`, `"tr"`, `"bl"`, `"br"`, `"all"` |
| `video_logo_image` | Logo Image | string: a full path, `""` for OSD/OS's logo |
| `screensaver_timeout` | Screen Saver | string: `"OFF"`, `"30"`, `"60"`, `"120"` |
| `mouse_pointer` | Mouse Pointer | string: `"off"`, `"2"`, `"5"`, `"10"`, `"30"`, `"always"` |
| `info_screen` | Info Screen | string: `"off"`, `"key"`, `"1"`, `"2"`, `"3"`, `"5"` |
| `audio_output` | Audio Output | object `{ "card", "name" }` (the card's ALSA id and its name), or `""` for Auto |
| `remote_keymap` | Controls → All Keyboards & Remotes | object: `up`, `down`, `left`, `right`, `select`, `back`, each a key's number, `0` for none |
| `players` | Controls → Players | object: `"1"` to `"6"`, each `{ "key", "name" }`, a pad's key (`""` for Auto) and its name |
| `controllers` | Controls → a controller's page | object: by each controller's key (`pad-<SDL GUID>`, `kbd-<bus>-<vendor>-<product>`), an object of `name`, `kind`, `labels` and `bindings` ([Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls#how-a-controllers-settings-are-saved)) |

And three that have no row, set by hand:

```json
{
    "app": {
        "display_index": 1,
        "mpv_video_args": "--vo=drm --hwdec=v4l2m2m-copy",
        "auto_crop": "On"
    }
}
```

- `display_index`: the display the menus open on and mpv plays on, where a Mac or a Linux desktop has several. OSD/OS lists them in its log at start, `[main] display index 1: "<name>" 1280x720 at (3840,0)`; an index out of range falls back to 0. Read at start.
- `mpv_video_args`: mpv flags, separated by spaces, that replace the video output and decoder flags OSD/OS picks for the device. Read at every video ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv#mpv_video_args)).
- `auto_crop`: from before Scaling; `"On"` reads as Pan & Scan while `video_scaling` isn't set.

### A module's keys

A module's settings are under `modules.<module id>`, the keys its `manifest.json` names, so each module's page lists them. How each kind of row is saved:

| Kind of row | Saved as | Example |
|---|---|---|
| Toggle | `true` or `false` | `"loop_playback": false` |
| A fixed list | the choice as shown | `"video_scaling": "Pan & Scan"` |
| A list the module fills in | the choice's id, which the module chooses | Local Files' `"resume_playback": "yes"` for Always (Jellyfin's is `"always"`), `"sub_lang": "-"` for Any |
| A multi-select page | an object of ids, each `true` or `false`; one missing is on | `"libraries": { "<id>": false }` |
| A folder | its path, `""` for the module's own | `"media_directory": "/media/usb/FILMS"` |

Every module has `enabled` (`true` or `false`); while it is missing, the manifest's default applies: Local Files and Playlists on, the others off. The module ids are `com.osdos.` followed by the module's folder name: `com.osdos.ambient_mode`, `com.osdos.arcade`, `com.osdos.emby`, `com.osdos.highscores`, `com.osdos.jellyfin`, `com.osdos.local_files`, `com.osdos.netflix`, `com.osdos.nfc_reader`, `com.osdos.playlists`, `com.osdos.plex`, `com.osdos.prime_video`, `com.osdos.scripts`, `com.osdos.weather`, `com.osdos.youtube`.

### How OSD/OS reads and writes it

- **Read afresh every time.** `appCore.get_setting(moduleId, key)` reads `config.json` from the disk at each call, and `get_settings()` the whole file. A key with one dot reads one level down: `get_setting("", "remote_keymap.back")` is `app.remote_keymap.back`.
- **Written whole at every change.** `appCore.save_setting(moduleId, key, value)` reads the file, changes the one key (a dotted key one level down, as above) and writes the whole file again. It writes it into a new file beside it, which replaces the old one only once it is complete and on the disk (`writeFileAtomically()`, Qt's `QSaveFile`). A crash, a power cut or a full disk on the way leaves the file as it was, never cut short. `lists.json`, the history files, the sign-ins and `playlists.json` are written the same way.
- **Then told.** The change is announced (`appSettingChanged` for an app key, `moduleSettingChanged` for a module's), and what follows that setting changes at once: the look, the menus, Audio Output, Controls, a module's folder. A change that couldn't be written (a full or read-only disk) is not announced: `save_setting` returns `false`, nothing changes, and Settings keeps the row on the value saved.
- **A file that can't be read counts as none.** A `config.json` that is missing, or isn't valid JSON, reads as `{ "app": { "color_scheme": "Video 1" }, "modules": {} }`, and the next change is written over it on that basis: every other setting is lost. That is why the next section matters.

When each part is read:

| What | When it is read |
|---|---|
| The look (Theme and its parts, menu music), OSD Background, Window Frame, Transparent Background's solidity, Loading Effect, Loading Screen Colors, Boot Screen Colors, Hint Bar, Help Line, Mouse Pointer, Screen Saver | At start, then at each change made in Settings |
| `display_index` | At start |
| Audio Output, Controls' buttons and button labels | At start, then at each change made in Settings |
| The modules' folders (Media Directory, Tags Directory, Scripts Directory, Ambient:Mode's, Arcade's Games Folder) | At start, then at each change made in their settings |
| Scaling (the app's and the module's), Video Levels, Channel Logo, Logo Image, Screen Saver's script, 1080p Playback, `mpv_video_args`, Transparent Background on or off | As each video starts |
| Start on Module, Play at Startup, Startup From | As OSD/OS starts |
| A module's other settings | When the module needs them: most as a video starts or a list opens |
| Settings' rows | Each time Settings is opened |

So a change made by hand while OSD/OS runs reaches what is read as a video starts at the next video, and the rest at the next start.

## Editing by hand

Stop OSD/OS first, edit, check the JSON, then start it again.

1. **Stop OSD/OS.**
   - Under the autostart service, over SSH: `sudo systemctl stop osdos`. This leaves the Pi on. (Settings → Quit would power it off; Quit → Exit to Terminal also stops it, leaving a login shell on the TV.)
   - Run by hand: Settings → Quit, or Ctrl+Q.
   - On a Mac: Settings → Quit.
2. **Edit**, for example `nano ~/.local/share/OSD-OS/config.json`. Keep the quotes, colons, commas and braces: one missing comma makes the file unreadable.
3. **Check it** before starting:

   ```sh
   python3 -m json.tool ~/.local/share/OSD-OS/config.json > /dev/null && echo "config.json is valid"
   ```

   An error names the line and the column. `python3` is on Raspberry Pi OS and the OSD/OS image.
4. **Start OSD/OS**: `sudo systemctl start osdos`, or `osdos` by hand.

The files you write for a module (`tmdb_api_key.txt`, `youtube_subscriptions.txt`, `weather_location.txt`) are read when the module needs them, not only at start, so they can be written while OSD/OS runs; open the module again to see the change. `input.cfg` takes effect as soon as it is saved.

### Custom color schemes

Settings → Color Scheme offers schemes of your own from two files in the data folder, read at start.

`custom_color_schemes.json` adds any number, each under its name (3 to 28 characters: letters, digits, spaces and ASCII punctuation but `"` and `` ` ``):

```json
{
    "Phosphor Blue": {
        "primary": "#9FD8FF",
        "secondary": "#6FA8CF",
        "tertiary": "#3F789F",
        "surface": "#001428",
        "accent": "#FFFFFF"
    },
    "Sepia": {
        "primary": "#F2E3C6",
        "secondary": "#C8B48F",
        "tertiary": "#8C7A5B",
        "surface": "#3B2A1A",
        "accent": "#F2E3C6"
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

- Each scheme needs all five keys, each a `#rrggbb` colour, or it is left out with a line in the log. OSD/OS draws with two of them only: `primary` (the text, the lines and the selection) and `surface` (the background); the other three must be there but aren't used.
- A scheme named like one of OSD/OS's (`Video 1`) changes that one's colours, and isn't listed twice.
- Restart OSD/OS after changing either file. With **Custom** chosen and no scheme of that name any more, the setting goes back to THEME (Video 1 without a theme) at the next start. A named scheme taken out of `custom_color_schemes.json` while chosen draws as Video 1 until you choose another.
- A theme can carry its own two colours instead ([Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes)).

## Backing up

With OSD/OS stopped, a copy of the data folder is a full backup: settings, lists, resume points, sign-ins, themes and NFC cards. The sign-in files hold tokens: keep the copy private.

```sh
sudo systemctl stop osdos
tar -czf ~/osdos-backup-$(date +%F).tar.gz -C ~/.local/share OSD-OS
sudo systemctl start osdos
```

To leave out the downloaded updates, which Settings → Update fetches again, add `--exclude=OSD-OS/updates` before `-C`. Keep `bin/`: on the image its timer only updates the yt-dlp there (`yt-dlp --update`), and nothing puts it back once it is gone. On a Mac, quit OSD/OS and use `-C ~/"Library/Application Support" OSD-OS`.

To restore, stop OSD/OS, move the folder there aside, and unpack the copy in its place:

```sh
sudo systemctl stop osdos
mv ~/.local/share/OSD-OS ~/.local/share/OSD-OS.old
tar -xzf ~/osdos-backup-2026-10-10.tar.gz -C ~/.local/share
sudo systemctl start osdos
```

Once all is well, delete `OSD-OS.old`.

Restore onto the same user's home: the files belong to the user who made them. On the OSD/OS image, the films on the card's OSD-OS partition (`/media/OSD-OS`) are not in the data folder; copy them from a computer as you put them there.

## Coming from 240-MP

OSD/OS was 240-MP, and it carries over what a 240-MP install left:

- **The data folder.** At its first start, when `OSD-OS` beside it doesn't exist yet or is empty, OSD/OS moves the 240-MP data folder over: `~/.local/share/240-MP` becomes `~/.local/share/OSD-OS` (on a Mac, in `~/Library/Application Support`). The log says `[legacy] moved the 240-MP data folder … to …`. Should the move fail, it uses the 240-MP folder where it is. Under `DATA_ROOT` nothing is moved.
- **Module ids.** In every `.json` file of the data folder and of its `nfc_tags`, keys and values beginning `com.240mp.` are renamed `com.osdos.`, so `config.json`'s and `lists.json`'s settings and lists stay with their modules. This runs at every start and only touches a file that still has an old id.
- **Environment variables.** Every `OSDOS_<NAME>` above is read as `MP240_<NAME>` when unset, and scripts get `MP240_MODE` and `MP240_VT` beside the new names. `web-player.sh` reads `MP240_WEB_PLAYER_UA`, and `scripts/rpi-run-local.sh` `MP240_BIN`.
- **Old settings.** `app.theme` (a skin chosen before skins had their name) is read while `app.skin` is unset, `app.auto_crop` while `app.video_scaling` is, and the old `true`/`false` values of Local Files' Shuffle Playback and Auto Show Subtitles still read as the choices they meant.
- **The install.** `install.sh` removes a 240-MP install it finds (its service `240mp.service`, `/opt/240mp` and `/usr/local/bin/240mp`) before installing OSD/OS. An app still running from `/opt/240mp` can't update itself: run the installer once ([Installation](https://github.com/mehmetraif/OSD-OS/wiki/Installation)).

## The cache folder

OSD/OS caches one kind of file: menu music made from a MIDI file (played through FluidSynth) or from a tracker's module that mpv can't play (through openmpt123), as a WAV.

| System | Cache folder |
|---|---|
| Linux, the OSD/OS image | `~/.cache/OSD-OS/menu-music/`, or `$XDG_CACHE_HOME/OSD-OS/menu-music/` |
| macOS | `~/Library/Caches/OSD-OS/menu-music/` |

Each WAV is named after a hash of the file's path, size and time (and, for MIDI, the SoundFont), so a changed file is made again. Only the newest is kept. Deleting the folder is always safe: the music is made again the next time it plays.

## Logs and temporary files

| What | Where |
|---|---|
| OSD/OS's own log | Its standard output and error. Under the autostart service, the journal: `journalctl -u osdos -b` (this boot), `journalctl -u osdos -f` (as it happens). Run by hand in a terminal, the terminal. A Mac app opened from the Finder: macOS's log, which Console.app shows |
| mpv's messages | In OSD/OS's log as `[mpv] …` lines, with every token blanked out |
| mpv's full log | `/tmp/osdos-mpv.log`, readable by its owner only: mpv's own log of the latest video, verbose, its command line included (with tokens) |
| The boot screen's timings (the image) | `journalctl -b -u osdos \| grep '\[boot\]'` |
| USB drives (the image) | `journalctl -u 'osdos-usb-mount@*'` |
| yt-dlp's updates (the image) | `journalctl -u osdos-yt-dlp-update` |
| An AppImage's or a Mac's update | `updates/apply.log` in the data folder |

A Release build (the releases and the image) leaves out OSD/OS's debug lines. A build made from source without `-DCMAKE_BUILD_TYPE=Release` keeps them, among them `[main] dataRoot = …` at start, `[AppCore] Setting saved: app.video_scaling = 14:9` at each change, and `[MpvController] launch: mpv …`, each video's command line with the tokens blanked out ([Building from source](https://github.com/mehmetraif/OSD-OS/wiki/Building-from-Source)).

OSD/OS's temporary files go in the system's temporary folder (`/tmp`, or `$TMPDIR`):

| File | What it is |
|---|---|
| `osdos-mpv.sock` | The IPC socket of the video playing |
| `osdos-input.conf`, `osdos-input-embedded.conf` | The key bindings each video's mpv gets ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv#back-and-the-end-of-a-video)) |
| `osdos-mpv.log` | mpv's log, above |
| `osdos-mpv-subinfo.json` | Subtitle tracks' names for the deck menu, while a server's sidecar subtitles play |
| `osdos-logo.bgra` | Settings → Logo Image's picture, made into mpv's overlay format |
| `osdos-fonts.conf` | The fontconfig file that shows mpv the VCR font |
| `osdos-menu-music-<pid>.sock`, `osdos-weather-music.sock` | The menu music's and Weather's music's mpv sockets |

## See also

- [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings): every row, with its key
- [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv): `mpv_video_args`, `mpv.conf` and mpv's log
- [Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes) and [Skins](https://github.com/mehmetraif/OSD-OS/wiki/Skins): what goes in `themes/` and `skins/`
- [Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls): `input.cfg` and `gamecontrollerdb.txt`
- [Installation](https://github.com/mehmetraif/OSD-OS/wiki/Installation) and [The OSD/OS image](https://github.com/mehmetraif/OSD-OS/wiki/The-OSD-OS-Image)
- [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting)
- [Writing a module](https://github.com/mehmetraif/OSD-OS/wiki/Writing-a-Module): `save_setting`, `get_setting` and a module's lists from the code's side
