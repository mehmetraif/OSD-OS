# Modules

Everything OSD/OS plays comes in through a module, like the inputs on a VHS deck: each is a source of video, browsed in OSD/OS's own menus and handed to mpv, or, for Netflix and Prime Video, to the service's own player. This page lists them all and shows how to turn them on. Each has a page of its own, with its setup, its settings and examples.

<table>
<tr><th width="50%">The main menu</th><th width="50%">Settings → Modules</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/main-menu.png" width="100%" alt="The main menu" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/settings-modules.png" width="100%" alt="Settings → Modules" /></td></tr>
<tr><td>The modules turned on, like the inputs on a deck. Select opens one; back opens Settings.</td><td>Every module has a row under Modules in Settings, which opens its own settings.</td></tr>
</table>

## All of them

| Module | What it plays | On at first | What it needs |
|---|---|---|---|
| [Local Files](https://github.com/mehmetraif/OSD-OS/wiki/Local-Files) | Films, videos and photos in a folder: the SD card's films partition on the OSD/OS image, a USB drive, a network share | Yes | Nothing more |
| [Playlists](https://github.com/mehmetraif/OSD-OS/wiki/Playlists) | Lists of videos from Local Files, YouTube, Jellyfin and Emby, streamed or downloaded to play offline | Yes | yt-dlp and ffmpeg for offline YouTube videos |
| [Plex](https://github.com/mehmetraif/OSD-OS/wiki/Plex) | A Plex Media Server's films, series and other videos | No | A Plex account and a server |
| [Jellyfin](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin) | A Jellyfin server's libraries | No | A Jellyfin server with Quick Connect |
| [Emby](https://github.com/mehmetraif/OSD-OS/wiki/Emby) | An Emby server's libraries | No | An Emby server, or Emby Connect |
| [YouTube](https://github.com/mehmetraif/OSD-OS/wiki/YouTube) | Subscriptions, channels, playlists and search, with no account needed | No | yt-dlp and Deno (the OSD/OS image has both) |
| [Netflix and Prime Video](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video) | The service's catalogue in your country, from TMDB; a title plays in the service's own player | No | A TMDB API key; Chromium with Widevine, cage and wtype (the OSD/OS image has them) |
| [NFC Reader](https://github.com/mehmetraif/OSD-OS/wiki/NFC-Reader) | The video an NFC card stands for, the moment it touches the reader | No | A PN532 USB reader, or a PC/SC reader with pcscd. On Linux, `setup-nfc-reader.sh` gives OSD/OS access to it; the image doesn't run it for you |
| [Weather](https://github.com/mehmetraif/OSD-OS/wiki/Weather) | The weather, the way a 90s cable channel showed it | No | The network (Open-Meteo, and NWS in the US) |
| [Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade) | Arcade games in MAME, from a folder of ROM sets, listed by players, decade and maker, or on a cabinet's selection screen with pictures, high scores and genres | No | MAME (the OSD/OS image has it) and MAME's ROM sets; for genres and pictures, catver.ini and a snap folder |
| [High Scores](https://github.com/mehmetraif/OSD-OS/wiki/High-Scores) | The high score tables of the games played in Arcade, as MAME kept them | No | Games played in Arcade |
| [Scripts](https://github.com/mehmetraif/OSD-OS/wiki/Scripts) | Your own shell scripts: anything else on the machine | No | Nothing more |
| [Ambient:Mode](https://github.com/mehmetraif/OSD-OS/wiki/Ambient-Mode) | A video on a loop, a fireplace or an aquarium, with music of your choice | No | Nothing more |

## Turning a module on or off

1. On the main menu, press back: Settings opens.
2. Go down to the **Modules** section and select the module.
3. **Enabled** is its first row: left, right or select turns it on or off. Back on the main menu, which looks for the modules again each time it opens, a module turned on is there.

The rest of that screen is the module's own settings: each module's page lists them. Every setting is saved in `config.json` in the data folder, under the module's id, as in `modules.com.osdos.youtube.enabled`:

```json
{
    "modules": {
        "com.osdos.local_files": { "enabled": true },
        "com.osdos.youtube": { "enabled": true },
        "com.osdos.plex": { "enabled": false }
    }
}
```

| Module | Its id, in `config.json` | Its folder |
|---|---|---|
| Local Files | `com.osdos.local_files` | [modules/local_files](https://github.com/mehmetraif/OSD-OS/tree/main/modules/local_files) |
| Playlists | `com.osdos.playlists` | [modules/playlists](https://github.com/mehmetraif/OSD-OS/tree/main/modules/playlists) |
| Plex | `com.osdos.plex` | [modules/plex](https://github.com/mehmetraif/OSD-OS/tree/main/modules/plex) |
| Jellyfin | `com.osdos.jellyfin` | [modules/jellyfin](https://github.com/mehmetraif/OSD-OS/tree/main/modules/jellyfin) |
| Emby | `com.osdos.emby` | [modules/emby](https://github.com/mehmetraif/OSD-OS/tree/main/modules/emby) |
| YouTube | `com.osdos.youtube` | [modules/youtube](https://github.com/mehmetraif/OSD-OS/tree/main/modules/youtube) |
| Netflix | `com.osdos.netflix` | [modules/netflix](https://github.com/mehmetraif/OSD-OS/tree/main/modules/netflix) |
| Prime Video | `com.osdos.prime_video` | [modules/prime_video](https://github.com/mehmetraif/OSD-OS/tree/main/modules/prime_video) |
| NFC Reader | `com.osdos.nfc_reader` | [modules/nfc_reader](https://github.com/mehmetraif/OSD-OS/tree/main/modules/nfc_reader) |
| Weather | `com.osdos.weather` | [modules/weather](https://github.com/mehmetraif/OSD-OS/tree/main/modules/weather) |
| Arcade | `com.osdos.arcade` | [modules/arcade](https://github.com/mehmetraif/OSD-OS/tree/main/modules/arcade) |
| High Scores | `com.osdos.highscores` | [modules/highscores](https://github.com/mehmetraif/OSD-OS/tree/main/modules/highscores) |
| Scripts | `com.osdos.scripts` | [modules/scripts](https://github.com/mehmetraif/OSD-OS/tree/main/modules/scripts) |
| Ambient:Mode | `com.osdos.ambient_mode` | [modules/ambient_mode](https://github.com/mehmetraif/OSD-OS/tree/main/modules/ambient_mode) |

## What the modules share

- **One way to browse.** Local Files, YouTube, Netflix, Prime Video and Arcade open as the same horizontal tree, starting with Recently Watched (Recently Played in Arcade), Favorites and Search. [Using OSD/OS](https://github.com/mehmetraif/OSD-OS/wiki/Using-OSD-OS) shows it.
- **Options with ►** on an entry: Add to Favorites, Play at Startup, Add to Playlist, where the module has them.
- **One player.** Every video but Netflix's and Prime Video's plays through mpv, with the tape loading screen while it starts; a Local Files, YouTube or Playlists video has a menu of its own on back. [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv) covers it.
- **Scaling** for 16:9 pictures on a 4:3 screen, set for every module in Settings, or for one in its own settings.

## A module of your own

A module is a folder in `modules/` with a `manifest.json` and its QML views, and a C++ backend if it needs one. OSD/OS finds it at startup: [Writing a module](https://github.com/mehmetraif/OSD-OS/wiki/Writing-a-Module) builds one step by step.

## See also

- [Using OSD/OS](https://github.com/mehmetraif/OSD-OS/wiki/Using-OSD-OS)
- [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings)
- [Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files)
- [Writing a module](https://github.com/mehmetraif/OSD-OS/wiki/Writing-a-Module)
