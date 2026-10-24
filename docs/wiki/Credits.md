# Credits and license

Settings → About lists these credits on the device.

- OSD/OS is developed by mehmet raif tasdemir (darkBLACK), [github.com/mehmetraif](https://github.com/mehmetraif).
- It is a modified version of [240-MP](https://github.com/anthonycaccese/240-MP) by Anthony Caccese and its contributors: the app it all started from.
- OSD/OS's changes to 240-MP were written with [Claude Code](https://www.anthropic.com/claude-code), Anthropic's coding agent.
- The OSD/OS logo, the channel logo over the picture and the boot screen's cassette were made with [ChatGPT](https://chatgpt.com), OpenAI's assistant.
- 240-MP was made the same way. In Anthony's words, from its README: "Because this is a hobby project (and a fairly niche use case), I am using [Claude Code](https://www.anthropic.com/product/claude-code) to build a large part of the backend C++ code and structure the modules. If you have concerns with that, I am glad to talk through it. Also, please feel free to fork this repo, update any aspects and tailor things to your own use case; that's why the source is fully open and available."
- The `VCR OSD Mono` font was created by Riciery Santos Leal (a.k.a. mrmanet) https://www.dafont.com/vcr-osd-mono.font
- The `Unifont` font (used as a fallback for characters that VCR OSD Mono does not cover) is GNU Unifont by Roman Czyborra, Paul Hardy, et al., licensed under the SIL Open Font License v1.1. https://unifoundry.com/unifont/ — license text: [assets/fonts/LICENSE-unifont.txt](https://github.com/mehmetraif/OSD-OS/blob/main/assets/fonts/LICENSE-unifont.txt)
- Thank you to Plex, Jellyfin, Emby and Open-Meteo for providing open and free apis to enable building modules for each, and to [TMDB](https://www.themoviedb.org), [JustWatch](https://www.justwatch.com) and [Wikidata](https://www.wikidata.org) for the catalogues behind Netflix and Prime Video. This product uses the TMDB API but is not endorsed or certified by TMDB. Weather data by [Open-Meteo.com](https://open-meteo.com), under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- Thank you to [the MPV team](https://mpv.io/) for a simple, extensible and cross platform media player, and to [Qt](https://www.qt.io), [SDL](https://www.libsdl.org), [FFmpeg](https://ffmpeg.org), [yt-dlp](https://github.com/yt-dlp/yt-dlp), [Deno](https://deno.com), [Chromium](https://www.chromium.org) and [MAME](https://www.mamedev.org), which OSD/OS stands on.
- The High Scores module reads MAME's high score files with [openhi2txt](https://github.com/inigomonyota/openhi2txt), by inigomonyota (GPL-3.0), which parses XML with [RapidXML](https://rapidxml.sourceforge.net/), by Marcin Kalicinski (Boost Software License or MIT), and through [hi2txt](http://greatstone.free.fr/hi2txt/)'s game definitions (many of them from HiToText), by GreatStone and their contributors ([hi2txt-xml](https://github.com/GreatStoneEx/hi2txt-xml), GPL-2.0). MAME's hiscore plugin, which keeps most of those tables, was written by borgar, and its `hiscore.dat` is maintained by Leezer.
- The Arcade module's pictures and data come from [RetroAchievements](https://retroachievements.org) (achievements, leaderboards and hardcore play, and the games' pictures and the achievements' badges, read through its Web API with your own account; OSD/OS is not affiliated with RetroAchievements), from [progetto-SNAPS](https://www.progettosnaps.net/) by AntoPISA (the *Snapshots* and *Titles* packs and `catver.ini`; its screenshots are free to use under fair use, their copyright held by the companies that developed and published the games), scraping using [Arcade Database](https://adb.arcadeitalia.net/) by motoschifo (whose service gives progetto-SNAPS' pictures of a set on request; names and images under fair use, all copyrights and trademarks their holders'), and from [libretro's lobby](https://lobby.libretro.com) and relay servers for online play. Thank you to all of them.
- Thank you to [Raspberry Pi](https://www.raspberrypi.com) for the boards, and for Raspberry Pi OS and [pi-gen](https://github.com/RPi-Distro/pi-gen), which the OSD/OS image is built with, and to [Debian](https://www.debian.org), which Raspberry Pi OS is based on. What the image carries, under which licences, is in [os/NOTICE](https://github.com/mehmetraif/OSD-OS/blob/main/os/NOTICE).
- And from 240-MP's README, Anthony's thanks to the [Raspberry Pi Foundation](https://www.raspberrypi.org/) "for helping me fill a drawer with SBCs to tinker with and inspire fun ideas like this project ❤️"

## License

OSD/OS is free software under the GNU General Public License v3.0. See [LICENSE](https://github.com/mehmetraif/OSD-OS/blob/main/LICENSE) for the full text; every build carries it next to the app, and Settings → About → License shows it on the device, with the notice. Under About's ARCADE heading are the arcade's free software, each line with its license (the GNU GPL v2, RapidXML's and zlib's are in [assets/licenses](https://github.com/mehmetraif/OSD-OS/tree/main/assets/licenses), FinalBurn Neo's comes with the core), and the sites it reads from.

OSD/OS is a modified version of [240-MP](https://github.com/anthonycaccese/240-MP), Copyright (C) 2026 Anthony Caccese and the 240-MP contributors. The modifications, from 2026 on, are Copyright (C) 2026 mehmet raif tasdemir (darkBLACK). The whole stays under GPL-3.0.

You are free to use, study, and modify this code. If you distribute a modified version, you must also distribute it under GPL-3.0 and make the source available.

The OSD/OS image carries Raspberry Pi OS Lite and the software OSD/OS needs alongside it, each under its own licence: [os/NOTICE](https://github.com/mehmetraif/OSD-OS/blob/main/os/NOTICE) lists them, and the image has it as `/usr/share/doc/osdos/NOTICE`.

Raspberry Pi is a trademark of Raspberry Pi Ltd, and Debian a registered trademark of Software in the Public Interest, Inc. Netflix, Prime Video, YouTube, Plex, Jellyfin and Emby are their owners' trademarks, named for the services the modules reach. OSD/OS is not affiliated with or endorsed by any of them.

## 240-MP

OSD/OS grew out of [240-MP](https://github.com/anthonycaccese/240-MP), Anthony Caccese's VCR-style frontend for CRTs. Its own pictures show where it started, before OSD/OS's menus:

- Photos of 240-MP on a CRT: [the picture that headed its README](https://github.com/user-attachments/assets/73c3e46f-a74a-4d96-9c4f-ae30f28378be), [module selection](https://github.com/user-attachments/assets/9472d55a-4617-4a7f-80c4-32aa28494048), [an item's page](https://github.com/user-attachments/assets/4f7d8230-860a-4ace-9370-9f59f43289c0), [the resume option](https://github.com/user-attachments/assets/490e9ebd-fab2-4fd1-9959-35ebb619eff0), [playback](https://github.com/user-attachments/assets/a3c768c7-6ede-4cdf-9d03-90aee7b8cdfb) and [settings](https://github.com/user-attachments/assets/0fd48977-8776-4334-b34e-d12256f23b97).
- Its video overview, on YouTube: https://youtu.be/r-gylGDoELY
