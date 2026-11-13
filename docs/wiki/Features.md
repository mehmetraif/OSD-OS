# Features

What OSD/OS does, at a glance. Each line links to its details. Every screen is in the [screen tour](https://github.com/mehmetraif/OSD-OS/blob/main/docs/TOUR.md).

OSD/OS has two sides, TV / Movie and Arcade, with the same controls, look and system under both.

## TV / Movie

- **Modules**: Local Files, Plex, Jellyfin, Emby, YouTube, Netflix and Prime Video, Playlists, an NFC reader, a 90s weather channel, Ambient:Mode and your own shell scripts ([Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules)).
- **One way to browse**: a horizontal tree that starts with **Recently Watched**, **Favorites** and **Search** ([Using OSD/OS](https://github.com/mehmetraif/OSD-OS/wiki/Using-OSD-OS)).
- **Search with the remote**, on an on-screen keyboard.
- **Info screens** for films and videos, with ► or when the cursor rests on a title: story, genre, director, cast and rating.
- **Options** on an entry, with ►: **Favorites**, **Play at Startup** and **Add to Playlist**.
- **Playlists** across modules, in order or shuffled, **online** (streamed) or **offline** (downloaded) ([Playlists](https://github.com/mehmetraif/OSD-OS/wiki/Playlists)).
- **Transparent Background**: the menus over a video that plays on, from TRANSPARENT to SOLID. It needs libmpv ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv)).
- **A tape loading** while a video starts, with Loading Effect and Loading Screen Colors in Settings → TV / Movie ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv)).
- **The logo** in a corner of the picture: Channel Logo and Logo Image in Settings → TV / Movie ([Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings)).
- **Scaling** for 16:9 pictures on a 4:3 screen: Letterbox, 14:9, Pan & Scan or Anamorphic ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv)).
- **Netflix and Prime Video** catalogues from TMDB, played in the service's own player ([Netflix and Prime Video](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video)).

## Arcade

- **Arcade games** from a folder of ROM sets, for up to six players, in the tree or on the Arcade screen. The Arcade screen is a list, an arc or a 3D wheel, with filters, search, pictures, high scores and Attract Mode ([Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade)).
- **Two engines**: MAME, and FinalBurn Neo in RetroArch. A game plays in the one that has its files, or the one chosen in its options ([Arcade → FinalBurn Neo](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#finalburn-neo)).
- **Online and LAN Play**: online through RetroArch's lobby, or on the local network with no internet. Up to six players across Raspberry Pis, PCs, Macs and the BSDs. On the local network, Cave's CV1000 games play on a Pi of 4 GB ([Online and LAN Play](https://github.com/mehmetraif/OSD-OS/wiki/Online-and-LAN-Play)).
- **LAN Sync** in Settings → Arcade: Smooth or Responsive, how a LAN game keeps its machines together ([Online and LAN Play → Speed](https://github.com/mehmetraif/OSD-OS/wiki/Online-and-LAN-Play#speed)).
- **RetroAchievements**, signed in from Settings → Arcade: achievements, leaderboards and game pictures ([Arcade → FinalBurn Neo](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#finalburn-neo)).
- **Check ROMs** in Settings → Arcade: whether each game's files are whole for MAME and for FinalBurn Neo. Hide Incomplete Games leaves out those neither has ([Arcade → Check ROMs](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#check-roms)).
- **JAMMA cabinets**: a Raspberry Pi to JAMMA board drives the cabinet's monitor and controls at 15, 25 or 31 kHz. Cabinet Lock keeps it on the Arcade screen. On a monitor turned on its side, the menus and games stand upright ([Arcade → A monitor on its side](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#a-monitor-on-its-side-tate), [Display Output → JAMMA](https://github.com/mehmetraif/OSD-OS/wiki/Display-Output#jamma-a-raspberry-pi-to-jamma-board), [Settings → Cabinet Lock](https://github.com/mehmetraif/OSD-OS/wiki/Settings#cabinet-lock)).
- **High scores**: each game's table as MAME kept it, read on the device with nothing online ([High Scores](https://github.com/mehmetraif/OSD-OS/wiki/High-Scores)).

## Controls and the look

- **A remote, a keyboard or a gamepad**: the arrows, select and back on any of them. Settings → Controls sets each controller's buttons and labels and each player's pad, and learns unknown buttons ([Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls)).
- **A mouse pointer** that shows while the mouse moves and hides 5 seconds later (Settings → Screen → Mouse Pointer).
- **Status icons** for Wi-Fi, Bluetooth and the arcade's HI, trophy, globe and network, turned off in Settings → Look → Status Icons ([Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings#status-icons)).
- **OSD Background** in Settings: Full, Off or Window, with Window Frame for the window ([Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings)).
- **Themes** in Settings: the color scheme, the skin, the effects and the menu music at once. Nine come with it; a new one is a folder ([Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes)).
- **Effects**, never over a video: text, background, selector and screen effects, and transitions between windows ([Effects](https://github.com/mehmetraif/OSD-OS/wiki/Effects)).
- **Menu music** under the menus: the theme's, or your own recording, MIDI file or tracker module ([Menu music](https://github.com/mehmetraif/OSD-OS/wiki/Menu-Music)).
- **Skins** in Settings: the window's shapes and icons. DOS and Rounded come with it; a new one is a folder of pictures ([Skins](https://github.com/mehmetraif/OSD-OS/wiki/Skins)).
- **Menu Layout** in Settings → Look: Standard, Camcorder, Studio Monitor, Broadcast or Flat Panel ([Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings#menu-layout)).
- **Hint Bar** and **Help Line** in Settings: the key hints and the line about the selected row, each on or off ([Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings)).

## The system

- **The OSD/OS image**: Raspberry Pi OS Lite that boots straight into OSD/OS, with a VHS boot screen ([The OSD/OS image](https://github.com/mehmetraif/OSD-OS/wiki/The-OSD-OS-Image)).
- **Display Output** in Settings, on the OSD/OS image: HDMI, composite or SCART RGB. An output not kept within 30 seconds goes back ([Display Output](https://github.com/mehmetraif/OSD-OS/wiki/Display-Output)).
- **Made for CRTs**: shapes kept on a tube's non-square pixels ([Display Output](https://github.com/mehmetraif/OSD-OS/wiki/Display-Output#overscan-and-the-safe-area)).
- **Audio Output** in Settings: the Pi's AV jack, HDMI or a USB sound card ([Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output)).
- **Wi-Fi** in Settings: the networks in reach, joined with the on-screen keyboard ([Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings#wi-fi)).
- **Bluetooth** in Settings: search for a keyboard, gamepad or remote and pair it ([Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls)).
- **System Information** in Settings → System → About: the device's name, model, memory, storage, temperature and network addresses ([Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings#system-information)).
- **Update** in Settings: a new release downloaded from GitHub and installed ([Installation](https://github.com/mehmetraif/OSD-OS/wiki/Installation)).
- **About** in Settings: what OSD/OS is, who makes it, and its license.
