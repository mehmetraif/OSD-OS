# OSD/OS wiki

OSD/OS makes a TV, preferably a CRT, a smart TV with the look of a VCR. On a Raspberry Pi it is the whole system: flash it, plug the Pi into the TV, and it starts straight into its menus. It has two sides, TV / Movie and Arcade. These pages cover all of it: getting it running, using it, every setting and module, playing arcade games alone and together, making it look the way you want, and how it works inside.

<p align="center"><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/images/default-look.gif" width="640" alt="OSD/OS as it comes: the main menu, Local Files' tree, the Arcade screen's games on a 3D wheel, its filters and the games others host online, and Settings" /></p>

## Getting started

| Page | What is in it |
|---|---|
| [Installation](https://github.com/mehmetraif/OSD-OS/wiki/Installation) | The OSD/OS image for a Raspberry Pi, or OSD/OS as an app on Raspberry Pi OS, macOS, SteamOS, Windows, FreeBSD and OpenBSD; updating; supported hardware |
| [Using OSD/OS](https://github.com/mehmetraif/OSD-OS/wiki/Using-OSD-OS) | Every screen: the main menu, the tree, search, info screens, options, playing a video, the menus over a video, quitting |
| [Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls) | Keyboards, remotes, gamepads and the mouse; remapping buttons; pairing Bluetooth devices |
| [Features](https://github.com/mehmetraif/OSD-OS/wiki/Features) | Everything it does, at a glance |

## The system

| Page | What is in it |
|---|---|
| [The OSD/OS image](https://github.com/mehmetraif/OSD-OS/wiki/The-OSD-OS-Image) | What the image changes in Raspberry Pi OS: the boot, the services, the films partition, USB drives, SSH, logs, building it yourself |
| [Display Output](https://github.com/mehmetraif/OSD-OS/wiki/Display-Output) | HDMI, composite and SCART RGB on a Pi 4 and a Pi 5, the cables, and what each choice writes |
| [Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output) | The AV jack, HDMI or a USB sound card |

## TV / Movie

Your films and videos, each source a module, like the inputs on a deck.

| Page | What is in it |
|---|---|
| [Local Files](https://github.com/mehmetraif/OSD-OS/wiki/Local-Files) | Films, videos and photos in a folder: the card's Movies folder, a USB drive, a network share |
| [Plex](https://github.com/mehmetraif/OSD-OS/wiki/Plex) · [Jellyfin](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin) · [Emby](https://github.com/mehmetraif/OSD-OS/wiki/Emby) | A media server's libraries, signed in from the TV |
| [YouTube](https://github.com/mehmetraif/OSD-OS/wiki/YouTube) | Subscriptions, channels, playlists and search, with no account needed |
| [Netflix and Prime Video](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video) | The service's catalogue in OSD/OS's tree, played in the service's own player |
| [Playlists](https://github.com/mehmetraif/OSD-OS/wiki/Playlists) | Lists of videos across modules, streamed or downloaded |
| [NFC Reader](https://github.com/mehmetraif/OSD-OS/wiki/NFC-Reader) · [Weather](https://github.com/mehmetraif/OSD-OS/wiki/Weather) · [Ambient:Mode](https://github.com/mehmetraif/OSD-OS/wiki/Ambient-Mode) | A card that plays a film, a 90s weather channel, a fireplace on a loop |

## Arcade

Arcade games, on a cabinet's selection screen, for up to six players, and with others on every system OSD/OS runs on.

| Page | What is in it |
|---|---|
| [Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade) | MAME and FinalBurn Neo from a folder of ROM sets: the two screens, filters, pictures, credits, Check ROMs, RetroAchievements, every setting |
| [Online and LAN Play](https://github.com/mehmetraif/OSD-OS/wiki/Online-and-LAN-Play) | Playing together: online through RetroArch's lobby, or on the local network with no internet, up to six players across Raspberry Pis, PCs and Macs, Cave's CV1000 games on a Pi on OSD/OS's own core |
| [High Scores](https://github.com/mehmetraif/OSD-OS/wiki/High-Scores) | Each game's high score table, as MAME kept it |
| [JAMMA cabinets](https://github.com/mehmetraif/OSD-OS/wiki/Display-Output#jamma-a-raspberry-pi-to-jamma-board) | A cabinet's own monitor and controls, through a Raspberry Pi to JAMMA board |

## Modules

| Page | What is in it |
|---|---|
| [Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules) | All of them at a glance, and turning them on |
| [Scripts](https://github.com/mehmetraif/OSD-OS/wiki/Scripts) | Your own shell scripts: anything else on the machine |

## Making it yours

| Page | What is in it |
|---|---|
| [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings) | Every row of Settings: what it does, its choices and where it is saved |
| [Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes) | The whole look at once: the eight that come with it, and making your own |
| [Skins](https://github.com/mehmetraif/OSD-OS/wiki/Skins) | The window's shapes and icons, and drawing your own |
| [Effects](https://github.com/mehmetraif/OSD-OS/wiki/Effects) | Text, background, selector and screen effects, and the transitions between windows |
| [Shaders](https://github.com/mehmetraif/OSD-OS/wiki/Shaders) | Writing a background or screen effect of your own for the GPU |
| [Menu music](https://github.com/mehmetraif/OSD-OS/wiki/Menu-Music) | The tune under the menus: recordings, tracker modules and MIDI files |
| [Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files) | The data folder, config.json and the other files OSD/OS keeps |
| [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv) | How videos play, mpv's options, scaling, subtitles and resume |

## Help

| Page | What is in it |
|---|---|
| [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting) | Reading the log, and problems by symptom |
| [FAQ](https://github.com/mehmetraif/OSD-OS/wiki/FAQ) | Kodi, other Raspberry Pis, 240p, RGB, HDMI, Bluetooth and the name |
| [Credits and license](https://github.com/mehmetraif/OSD-OS/wiki/Credits) | Who made it, what it stands on, and its license |

## Development

For those who build OSD/OS themselves or add to it. Nothing here is needed to use it.

| Page | What is in it |
|---|---|
| [How it works](https://github.com/mehmetraif/OSD-OS/wiki/How-It-Works) | The shell and its modules, the display path, playback, the effects, settings and logs |
| [Writing a module](https://github.com/mehmetraif/OSD-OS/wiki/Writing-a-Module) | A module of your own, step by step, with working code |
| [Building from source](https://github.com/mehmetraif/OSD-OS/wiki/Building-from-Source) | Building the app, the tests and the image; CI and releases |

Every screen is also in the [screen tour](https://github.com/mehmetraif/OSD-OS/blob/main/docs/TOUR.md).

These pages are kept in the repository, in [docs/wiki](https://github.com/mehmetraif/OSD-OS/tree/main/docs/wiki), and published here whenever they change there. To change one, open a pull request: an edit made here is replaced at the next publish.
