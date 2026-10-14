# Settings

Settings is OSD/OS's own menu: the look, the menus, playback, startup, the outputs, and a page for every module. This page goes through every row in the order the screen lists them, with its values, its default, what it does, the key it is saved under in `config.json`, and when it is offered at all.

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/settings.png" width="100%" alt="Settings: Theme, Color Scheme, Skin, Text Effect, Background Effect, Selector Effect, Screen Effect and Transition, the help line under them" />

## Getting around

Back on the main menu opens Settings (the hint bar there says `[ESC]:SETTINGS`). Each row reads like a camcorder's menu, `LABEL······VALUE`. The first rows have no heading; further down come two headings drawn as a rule, **MODULES** and **APPLICATION**.

| Action | Keyboard | Gamepad | What it does in Settings |
|---|---|---|---|
| ▲ ▼ | Up, Down | D-pad, left stick | Moves to the row above or below. Past the last row the cursor goes round to the first |
| ◄ ► | Left, Right | D-pad, left stick, LB, RB | Changes the value, round from the last choice to the first. A slider moves by 10 |
| Select | Enter | A | Opens a page (a module's settings, Controls, Bluetooth, Update, About), the list of Display Output, the file browser of Music File and Logo Image, or the Quit question. On Transparent Background it turns the setting on or off |
| Back | Esc, Backspace | B, or the pad's Back/Select button | Back to the main menu |

- **A change is saved at once.** Settings calls `appCore.save_setting("", key, value)`, which writes the value into `config.json` under `app.<key>` ([Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#configjson)). Most rows take effect at once. The rows mpv is started with (Scaling, Video Levels, Channel Logo, Logo Image, Screen Saver during a video) apply from the next video.
- **The title bar** shows the version (`DEV` for a build made from source). The top right corner shows the device's IP address on the network, wired before Wi-Fi, read again every five seconds: handy for SSH.
- **The help line** under the list explains the selected row, and scrolls when the text is longer than the box. Settings → Help Line turns it off.
- **Rows that come and go.** Some rows are offered only sometimes. A row that depends on another row (Window Frame on OSD Background, Music File and Music Volume on Menu Music, Logo Image on Channel Logo) stays in the list without a line while it isn't offered: the list doesn't move as it comes and goes, and the cursor skips over it.

| Row | Offered when |
|---|---|
| Text Effect, Background Effect, Screen Effect, and the Ripple, Wave and Drop transitions | The build has Qt Shader Tools (the releases and the OSD/OS image do) and Qt draws with a GPU, not its software renderer |
| Music File | Menu Music is File |
| Music Volume | Menu Music isn't Off |
| Window Frame | OSD Background is Window |
| Startup From | A favourite is set to Play at Startup |
| 1080p Playback | On a Raspberry Pi 3 only |
| Transparent Background | libmpv could be opened (`libmpv2` on Raspberry Pi OS and the image, Homebrew's mpv on a Mac), in a build made with libmpv's headers |
| Logo Image | Channel Logo isn't Off |
| Display Output | On the OSD/OS image, started by its service, on a Pi with more than one output to offer |
| Audio Output | On Linux where plain ALSA plays, with no PipeWire or PulseAudio server: the image and Raspberry Pi OS Lite |
| Bluetooth | On Linux builds made with Qt D-Bus (not on a Mac) |

## The look

The first rows set how the menus look: **Theme**, then one row for each part of a theme.

- **Theme** names one of the themes, or None. Eight come with OSD/OS, and any folder with a `theme.json` in the data folder's `themes` is one too ([Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes)). Choosing one with ◄ ► sets every part row below back to THEME, including the rows that are hidden at the time.
- **THEME** in a part row (saved as `""`) means the part as the theme has it.
- **Off** (saved as `"Off"`) means none of that part, whatever the theme says.
- **A choice of its own** (saved by its name) replaces the theme's. It stays as you set it until you choose another theme.
- **Without a theme** the rows have no THEME choice, and `""` shows as what the part is without one: Video 1, None or Off.
- **Color Scheme has no Off**: everything is always drawn in two colours.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Theme | None, then the themes by name: Arcade, Demoscene, Green Screen, Inferno, Late Show, Matrix, Trinitron, Winter, and those in the data folder's `themes` | None | The whole look in one: the color scheme, the skin, the effects and the menu music. Two themes of one name are told apart by their folder, `Name (folder)` | `app.osd_theme`: the theme's folder name (`trinitron`, `late-show`), `""` for None |
| Color Scheme | THEME (with a theme), Video 1, Late Night, Synthwave, Terminal, T-120, Amber, Kinescope, SMPTE ECR 1-1978, and your own | THEME; without a theme, Video 1 | The two colours everything is drawn in: the text, lines and selection, and the background | `app.color_scheme`: the scheme's name, `""` for THEME |
| Skin | THEME (with a theme), None, DOS, Rounded, and those in the data folder's `skins` | THEME; without a theme, None | The shapes of the window's frame, the title and hint bars and the selected line, and icons of its own, drawn in the color scheme's colours ([Skins](https://github.com/mehmetraif/OSD-OS/wiki/Skins)) | `app.skin`: the skin's folder name (`dos`, `rounded`), `"Off"` for None, `""` for THEME |
| Text Effect | THEME, Off, Rainbow, Shimmer, Glow, Flicker | THEME; without a theme, Off | What the text, the lines and the bars do, drawn by the GPU | `app.text_effect` |
| Background Effect | THEME, Off, Matrix, Fire, Stars, Snow | THEME; without a theme, Off | What goes on behind the menus, in the window, drawn by the GPU | `app.background_effect` |
| Selector Effect | THEME, Off, Sparkles, Welding, Lightning, Rainbow, Snow | THEME; without a theme, Off | What goes on round the selected line. Drawn by the CPU, so offered everywhere | `app.selector_effect` |
| Screen Effect | THEME, Off, Scanlines, CRT, VHS | THEME; without a theme, Off | A picture tube's or a tape's look over the whole screen, drawn by the GPU | `app.screen_effect` |
| Transition | THEME, Off, Fade, Cube, Ripple, Wave, Drop | THEME; without a theme, Off | How one window gives way to the next. Ripple, Wave and Drop need a GPU; without one a theme's falls back to Fade | `app.transition` |
| Menu Music | THEME, Off, File | THEME; without a theme, Off | A tune under the menus: the theme's, if it has one (Green Screen has none), or the file below ([Menu music](https://github.com/mehmetraif/OSD-OS/wiki/Menu-Music)) | `app.menu_music`: `""`, `"Off"` or `"File"` |
| Music File | A file picked on the file browser: MP3, WAV, OGG, Opus, FLAC, M4A, AAC, MID, MIDI, XM, MOD, S3M or IT; or No File | None | The menu music's own file, played over and over. Select opens the file browser | `app.menu_music_file`: the file's full path |
| Music Volume | A slider from QUIET to LOUD, 0 to 100 in steps of 10 | 60 | How loud the menu music is. It changes while the music plays | `app.menu_music_volume`: a number |

The effects and the menu music rest while a video plays, loads or has its menu open, and while a player has the screen ([Effects](https://github.com/mehmetraif/OSD-OS/wiki/Effects)).

The eight color schemes, by the two colours they draw with (`primary` on `surface`):

| Scheme | Text, lines, selection | Background |
|---|---|---|
| Video 1 | `#FFFFFF` | `#0110C5`, the blue of a VCR's menu |
| Late Night | `#FFFFFF` | `#000000` |
| Synthwave | `#FFFFFF` | `#12012B` |
| Terminal | `#4AF626` | `#000000` |
| T-120 | `#000000` | `#FAF5E8` |
| Amber | `#FFB000` | `#000000` |
| Kinescope | `#FFFFFF` | `#121212` |
| SMPTE ECR 1-1978 | `#BFBFBF` | `#131313` |

Schemes of your own come from two files in the data folder: `custom_color_schemes.json`, which adds any number of them by name, and `custom_color_scheme.json`, which adds one called **Custom**. Both are described, with examples, in [Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#custom-color-schemes).

## OSD Background and Window Frame

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| OSD Background | Full, Off, Window | Full | What the menus are drawn on. **Full**: the color scheme's background over the whole screen. **Off**: none; the menus on black, in whichever of the scheme's two colours is the lighter, like a deck's on-screen display with nothing playing (so T-120's dark text doesn't vanish). **Window**: a framed window of the scheme's background behind the menus, black around it. Over a video playing behind the menus, the window lies over the picture, which shows whole around it | `app.osd_background` |
| Window Frame | On, Off, Shadow | On | Offered with Window only. **On**: a line round the window in the scheme's colour, or the skin's own frame. **Off**: no frame. **Shadow**: the line and a DOS window's shadow down the right side and along the foot, a half tone of the window's colour (over a video, it darkens the picture) | `app.osd_frame` |

<table>
<tr><th width="33%">OSD Background: Window</th><th width="33%">OSD Background: Off</th><th width="33%">Window Frame: Shadow</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/osd-window.png" width="100%" alt="OSD Background: Window" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/osd-off.png" width="100%" alt="OSD Background: Off" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/skin-dos.png" width="100%" alt="Skin: DOS, Window Frame: Shadow" /></td></tr>
<tr><td>The menus in a framed window of the color scheme's background, black around it.</td><td>No background: the menus on black, in the scheme's lighter colour.</td><td>Skin DOS gives the frame a double line; Window Frame Shadow adds a DOS window's shadow.</td></tr>
</table>

## Starting up

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Start on Module | None, then each module that is turned on, by name | None | Opens that module straight after start (on the OSD/OS image, once the boot screen has closed). A module turned off later is never opened, and the row shows None | `app.startup_module`: the module's id, such as `com.osdos.local_files`; `"None"` |
| Play at Startup | None, or the name of the favourite chosen | None | A favourite played straight after start, ahead of Start on Module. You choose it in its module: ► on the entry, then **Play at Startup** (it goes on Favorites too). Here you can only turn it off. It plays only while it is still one of that module's favourites | `app.startup_favorite`: `{ "module", "path", "name" }`, or `""` for None |
| Startup From | Resume, Beginning | Resume | Offered while there is a favourite to play at startup. Where it begins, without asking: where it was stopped, or from the start | `app.startup_from` |

The Scripts module's **Auto-Run On Startup** runs a script when OSD/OS starts on that module, so it needs Start on Module set to Scripts ([Scripts](https://github.com/mehmetraif/OSD-OS/wiki/Scripts)).

## Video

These rows change how mpv plays. Everything about mpv itself, the flags each row adds and how they layer, is in [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv).

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| 1080p Playback | On, Off | On | Raspberry Pi 3 only. **On**: the smooth overlay path that plays 1080p, where cropping can't work, so 14:9 and Pan & Scan show the whole picture and the deck menu has no CROP. **Off**: the scaler path, where cropping works but 1080p stutters. From the next video | `app.smooth_playback` |
| Scaling | Letterbox, 14:9, Pan & Scan, Anamorphic | Letterbox | How a 16:9 picture fills a 4:3 screen, in every module whose own Scaling is Default. **Letterbox**: all of it, bars above and below. **14:9**: a little of the sides cut, thinner bars. **Pan & Scan**: fills the screen, the sides cut. **Anamorphic**: fills it squeezed, for a TV set to 16:9. From the next video; the deck menu's CROP changes it during one | `app.video_scaling` |
| Transparent Background | Off, or a slider from TRANSPARENT to SOLID, 0 to 100 in steps of 10 | Off | Plays the video inside OSD/OS's own window (libmpv), so back from a video returns to the menus with the picture going on behind them; the main menu's first row takes it back to full screen. The slider says how solid the menus' ground is over the picture: at SOLID none of it shows, but it plays on, sound and all. Select turns it on or off. ◄ ► on it while it is off turn it on where its bar waits: at 40, or where it was if you turned it off on this visit to Settings. A change shows at once over a video playing behind the menus, and turning it off stops that video | `app.transparent_background`: a number from 0 to 100, or `"Off"` |
| Video Levels | Auto, Limited, Full | Auto | The colour range mpv sends: **Limited** if blacks look crushed, **Full** if blacks look grey. Auto adds nothing, leaving mpv's default and your `mpv.conf`. From the next video | `app.video_output_levels` |
| Loading Effect | On, Off | On | While a video loads, a tape's noise and tracking bands roll over the dubbing deck's counters. Off leaves the counters alone on the plain background | `app.loading_effect` |
| Loading Screen Colors | Theme, Default | Theme | The colours of the screen a video loads behind: the theme's or colour scheme's, or OSD/OS's own, white on a VCR's blue, whatever the look | `app.loading_colors`: `"Theme"` or `"Default"` |
| Boot Screen Colors | Theme, Default | Theme | The colours of the boot screen on the OSD/OS image, its cassette and the services coming up: the theme's or colour scheme's, or white on blue | `app.boot_colors`: `"Theme"` or `"Default"` |
| Channel Logo | Off, Top Left, Top Right, Bottom Left, Bottom Right, All Corners | Top Right | OSD/OS's logo in a corner of the picture while a video plays, the way a channel's sits in a broadcast. mpv draws it into the picture, so with Transparent Background the menus lie over it. From the next video | `app.video_logo`: `off`, `tl`, `tr`, `bl`, `br` or `all` |
| Logo Image | OSD/OS Logo, or a picture picked on the file browser: PNG, JPG, JPEG, SVG, GIF, BMP or WebP | OSD/OS | Offered while Channel Logo isn't Off. A picture of your own in the logo's place, made as tall as OSD/OS's logo (7% of the screen's height), its shape kept. A PNG with a clear background works best. A picture that can't be read leaves OSD/OS's logo. From the next video | `app.video_logo_image`: the file's full path, `""` for OSD/OS's |

The older setting `app.auto_crop` (`On` or `Off`), from before Scaling, still counts while Scaling has never been set: `On` reads as Pan & Scan.

<table>
<tr><th width="50%">Channel Logo</th><th width="50%">Under the menus</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/channel-logo.png" width="100%" alt="Channel Logo" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/channel-logo-menu.png" width="100%" alt="The logo under the menus" /></td></tr>
<tr><td>OSD/OS's logo in a corner of the picture while a video plays.</td><td>It is in the picture itself, so with Transparent Background the menus lie over it.</td></tr>
</table>

<table>
<tr><th width="50%">Logo Image</th><th width="50%">Picking the picture</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/channel-logo-custom.png" width="100%" alt="Logo Image" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/logo-picker.png" width="100%" alt="Picking the picture" /></td></tr>
<tr><td>A picture of your own in the logo's place, as tall as OSD/OS's logo.</td><td>Picked on the same file browser as everything else: select on a picture, or <b>OSD/OS Logo</b> at the top to go back to it.</td></tr>
</table>

**The file browser** that Music File and Logo Image open is the tree Local Files is browsed with. At its top is the default (**No File**, **OSD/OS Logo**), then the places that exist on this system: **Home**, **Media** (`/media`, where the image mounts its OSD-OS partition and USB drives), **Drives** (`/run/media/<user>`), **Volumes** (`/Volumes`, on a Mac) and **Root** (`/`). It shows folders and the files of the types the setting takes, hidden ones left out, and opens at the file chosen now. Select on a file picks it and goes back; back at the top leaves the setting as it was.

## The menus

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Hint Bar | On, Off | On | The key hints on the bar at the foot of every screen. Off hides every one, and what is above it takes its room: a row more in every menu, more of the file browser, a help line moved down into its place; the keys work as before, and the window stays as it is | `app.hint_bar` |
| Help Line | On, Off | On | The box under a menu with a line about the selected row. Off hides it everywhere but About, whose lines are the page, and the menu above it gets a row more | `app.help_line` |
| Screen Saver | OFF, 30, 60, 120 | OFF | Seconds before a black screen with the OSD/OS logo bouncing across it, against CRT burn-in: in the menus after that long without a key or a mouse move, and over a video paused that long (mpv draws it then). Never over a playing video, a running script or the boot screen. The first key only wakes it | `app.screensaver_timeout`: `"OFF"` or the seconds |
| Mouse Pointer | Off, 2 sec, 5 sec, 10 sec, 30 sec, Always | 5 sec | A mouse's pointer, or a keyboard touchpad's, drawn in the OSD's pixels: it shows as the mouse moves and goes again after this long without moving. **Off**: never shown. **Always**: stays. Moving the mouse also wakes the screen saver. The menus still go by keys | `app.mouse_pointer`: `off`, `2`, `5`, `10`, `30` or `always` |
| Info Screen | Off, Key, 1 sec, 2 sec, 3 sec, 5 sec | 3 sec | A film's details (story, genre, director, cast, rating) in the Netflix, Prime Video and YouTube trees. **Key**: only with ► on a film. **A number**: also on its own once the cursor has rested on a film that long. **Off**: no info screen; ► offers the film's options instead | `app.info_screen`: `off`, `key`, `1`, `2`, `3` or `5` |

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/settings-modules.png" width="100%" alt="Settings further down: Channel Logo, Hint Bar, Help Line, Screen Saver, Mouse Pointer, Info Screen, then the MODULES heading and Ambient:Mode" />

## Modules

Under the **MODULES** heading is one row for every module that has settings, which is all fourteen, in the order of their folders: Ambient:Mode, Arcade, Emby, High Scores, Jellyfin, Local Files, Netflix, NFC Reader, Playlists, Plex, Prime Video, Scripts, Weather, YouTube. Select opens the module's own page, `SETTINGS | <MODULE>`. Its rows are written in the module's `manifest.json`, so a module brings its own ([Writing a module](https://github.com/mehmetraif/OSD-OS/wiki/Writing-a-Module)).

<table>
<tr><th width="50%">A module's settings</th><th width="50%">Picking a folder</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/module-settings.png" width="100%" alt="A module's settings: Local Files" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/folder-picker.png" width="100%" alt="Picking a folder" /></td></tr>
<tr><td>Local Files: its folder, looping, shuffle, resume, subtitles, and its own Scaling.</td><td>A folder is picked on the same tree: <b>Use This Folder</b> picks the folder open, <b>Default Folder</b> the module's own.</td></tr>
</table>

### Turning a module on

Every module's first row is **Enabled**. A module that is turned off is left off the main menu, can't be chosen in Start on Module, and an NFC card can't hand off to it. Until you change the row, the manifest's default applies: **Local Files** and **Playlists** are on, the other twelve off. The main menu picks up a change the next time you go back to it. The setting is saved as `modules.<module id>.enabled`, `true` or `false`.

| Module | Module id | On by default | Its rows, in order |
|---|---|---|---|
| [Ambient:Mode](https://github.com/mehmetraif/OSD-OS/wiki/Ambient-Mode) | `com.osdos.ambient_mode` | No | Enabled, Media Directory, Auto-Launch Playback, Scaling |
| [Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade) | `com.osdos.arcade` | No | Enabled, Games Folder, High Scores, Video, Read the Games Again |
| [Emby](https://github.com/mehmetraif/OSD-OS/wiki/Emby) | `com.osdos.emby` | No | Enabled; once signed in Libraries, Video Quality, Resume Playback, Autoplay Next Episode, Intro Skip, Credit Skip; Scaling; Sign out once signed in |
| [High Scores](https://github.com/mehmetraif/OSD-OS/wiki/High-Scores) | `com.osdos.highscores` | No | Enabled |
| [Jellyfin](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin) | `com.osdos.jellyfin` | No | Enabled; once signed in Libraries, Video Quality, Resume Playback, Autoplay Next Episode, and Intro Skip and Credit Skip where the server has the Intro Skipper plugin; Scaling; Sign out once signed in |
| [Local Files](https://github.com/mehmetraif/OSD-OS/wiki/Local-Files) | `com.osdos.local_files` | Yes | Enabled, Media Directory, Loop Playback, Shuffle Playback, Resume Playback, Auto Show Subtitles, Subtitle Language, Image Duration, Hide File Extensions, Scaling |
| [Netflix](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video) | `com.osdos.netflix` | No | Enabled, Catalogue Region, Catalogue Language, Scaling, Sign in, Sign out |
| [NFC Reader](https://github.com/mehmetraif/OSD-OS/wiki/NFC-Reader) | `com.osdos.nfc_reader` | No | Enabled, Tags Directory, Resume Playback, YouTube Video Resolution, Auto Show Subtitles, Subtitle Language, Scaling |
| [Playlists](https://github.com/mehmetraif/OSD-OS/wiki/Playlists) | `com.osdos.playlists` | Yes | Enabled, Download Folder, Subtitles, Loop Playback, Scaling |
| [Plex](https://github.com/mehmetraif/OSD-OS/wiki/Plex) | `com.osdos.plex` | No | Enabled; once signed in Current User, Auto Sign In, Server, Libraries, Video Quality, Resume Playback, Autoplay Next Episode; Scaling; Sign out once signed in |
| [Prime Video](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video) | `com.osdos.prime_video` | No | Enabled, Catalogue Region, Catalogue Language, Scaling, Sign in, Sign out |
| [Scripts](https://github.com/mehmetraif/OSD-OS/wiki/Scripts) | `com.osdos.scripts` | No | Enabled, Scripts Directory, Auto-Run On Startup, Rescan Scripts |
| [Weather](https://github.com/mehmetraif/OSD-OS/wiki/Weather) | `com.osdos.weather` | No | Enabled, Displays, Music, Units, Screen Time, Hours Format |
| [YouTube](https://github.com/mehmetraif/OSD-OS/wiki/YouTube) | `com.osdos.youtube` | No | Enabled, Advanced (Playback Resolution, Video Codec, Max Frame Rate, Scaling, Audio Language, Subtitles, Subtitle Language, Playback Speed, Resume Playback, Display Shorts), Delete Watch Later, Delete Recently Watched, Sign in, Sign out |

Each module's page on this wiki explains its rows. Every module that plays video has its own **Scaling**: **Default** (follow Settings → Scaling) or one of the four, for that module only.

### The kinds of row a module can have

| Kind (`type` in the manifest) | How the row looks | ◄ ► | Select | Saved in `modules.<id>` as |
|---|---|---|---|---|
| `toggle` | `LABEL······ON` | Flips it | Flips it | `true` or `false`; unset, the manifest's `default` (`"ON"` or `"OFF"`) |
| `list_single` | `LABEL······VALUE` | Steps through the choices, round | Nothing | The choice's text for a fixed list (`"Pan & Scan"`); for a list the module supplies (`"options_source": "dynamic"`), the choice's id (`"ask"`, `"forced"`) |
| `multiselect_submenu` | `LABEL` | Nothing | Opens a page of `NAME ◄ ON ►` lines, one per choice; ◄, ► or select flips one | An object under the key, `{ "<id>": true, "<id>": false }`; a choice not in it is on (Plex's Libraries: every library shows until you turn it off) |
| `submenu` | `LABEL` | Nothing | Opens a page of its own rows, titled `MODULE / LABEL` (YouTube's Advanced) | Nothing itself; its rows' keys stay flat under the module |
| `directory_browser` | `LABEL······/PATH`, or `DEFAULT` | Nothing | Opens the folder picker: **Default Folder** first, then the places, and **Use This Folder** at the top of every folder | The folder's path; `""` for the module's own |
| `action` | `LABEL` | Nothing | Runs it at once: Sign out, Delete Watch Later, Rescan Scripts | Nothing |
| `module_view` | `LABEL` | Nothing | Opens the module on one of its own screens, enabled or not: Sign in for Netflix, Prime Video and YouTube | Nothing |

Some rows wait for something:

- **Once signed in** (`requires_auth`): Plex's, Jellyfin's and Emby's server rows appear only after you have signed in, from inside the module.
- **When the server can** (`requires_capability`): Jellyfin's Intro Skip and Credit Skip appear only when the server reports media segments, which its Intro Skipper plugin provides.
- **At once** (`apply_slot`): changing Plex's Current User or Server tells the module straight away, and it switches.

## Application

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Display Output | HDMI, Composite NTSC, Composite PAL, GPIO Composite NTSC, GPIO Composite PAL, SCART RGB NTSC, SCART RGB PAL, SCART RGB 240p, SCART RGB 288p, JAMMA 240p: those this Pi has | The image's build setting, HDMI unless built otherwise | On the OSD/OS image only. Where the picture goes. A change restarts the Pi on the new output, powers it off for it, or waits for its next start, and the new output stays only when you keep it. **Next: <output>** while one waits | None: the preset is copied over `/boot/firmware/osdos-display.txt` |
| Audio Output | Auto, then each sound card that can play: AV Jack, HDMI (HDMI 0 and HDMI 1 when there are two), a USB card by its name | Auto | Which sound card plays, from the moment you choose it, for a video playing behind the menus too. Auto is the Pi's own default | `app.audio_output`: `""` for Auto, or `{ "card", "name" }` |
| Controls | A page | | Each controller connected, with its own buttons and a pad's button labels; the pads of players 1 to 6; and one more key for each action on every keyboard and remote | `app.controllers.<key>`, `app.players`, `app.remote_keymap.<action>` |
| Bluetooth | A page | | Searching for, pairing and connecting keyboards, gamepads and remotes | None: BlueZ keeps the pairings |
| Update | A page | | Checks for a newer release, downloads it and installs it | None |
| About | A page | | What OSD/OS is, who makes it, what it is made of, and its license | None |
| Quit | A question | | Quits OSD/OS, or under the image's service powers off, restarts or drops to a terminal | None |

### Display Output

Select opens the list of the outputs this Pi has, with the board's model and the output in use (`Now: HDMI`). Choose another, then:

- **Switch and Restart**: OSD/OS restarts the Pi on it.
- **At Next Start**: it is written now, and the Pi stays on as it is. It starts on the new output the next time it starts. The row reads **Next: <output>** until then; choosing the output in force again takes it back (**Stay on <output>**).
- **Switch and Power Off**: it is written, and the Pi powers off, to start on it when switched on. With At Next Start, the way to set up a screen that isn't here, as a JAMMA cabinet's.

On the new output it first asks **Keep this display output?**. **Keep** keeps it. **Switch Back**, or 15 seconds without an answer (a TV that shows nothing), goes back to the output before, restarting again, and says so. Back does nothing there, so a key pressed at random on a blank screen can't keep it.

The row needs the OSD/OS image's presets beside `config.txt`, the app started by the image's service, and a launcher new enough to switch (`OSDOS_LAUNCHER_API` 3; 4 for JAMMA 240p; 5 for At Next Start and Switch and Power Off). The outputs, the cables and how to put a card right from a computer are in [Display Output](https://github.com/mehmetraif/OSD-OS/wiki/Display-Output).

### Audio Output

◄ ► step through **Auto** and the cards ALSA lists that can play (so a webcam's microphone isn't one). The Pi's analog card is **AV Jack**, its HDMI cards **HDMI**, and any other card shows by its own name, its id after it when two share one. mpv is told the card as `--audio-device=alsa/default:CARD=<id>`, and a script or a web player's browser gets `ALSA_CARD=<id>`. A card chosen and then unplugged stays chosen, shown as `NAME (Unplugged)`: sound goes as on Auto until it is back. On Auto nothing is added, so `audio-device` in `mpv.conf` and `/etc/asound.conf` still choose. More in [Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output).

### Controls

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/controls.png" width="100%" alt="Controls: ALL KEYBOARDS & REMOTES; PLAYERS: PLAYER 1 8BITDO PRO 2, PLAYER 2 AUTO (OSD/OS JAMMA P2); CONNECTED: 8BITDO PRO 2 P1 GAMEPAD ◄, DUALSENSE WIRELESS CONT… GAMEPAD, OSD/OS JAMMA P1 ARCADE" />

Controls lists every controller connected: each gamepad (the JAMMA board's two sticks among them, as `ARCADE`) and, on Linux, each keyboard and remote, with **◄** on the one used last. Controllers away with settings of their own follow under **Not Connected**. Above them, **Player 1** to **Player 6** choose the pad each player has in games, from the pads connected, or Auto (the JAMMA board's stick of that number, else the pads in the order they came): two lines, and one more for each pad connected beyond two. Select on a controller opens its page:

- **Button Labels** (a pad's): Auto, Xbox, PlayStation, Nintendo or Arcade, what its buttons are called on its page and in the hints.
- **Up**, **Down**, **Left**, **Right**, **Select / OK**, **Back**, **Play / Pause**: what does each on it. Select, then press a button on that controller: on a pad that button does the action from then on; on a keyboard or remote the key is one more for it, on that one only. Another controller's Back cancels, and so does whatever does Back on a keyboard or remote; the wait ends by itself after 10 seconds. A pad keeps one Select / OK, one Back, one Up and one Down, and a keyboard's or remote's keys for Select / OK, Up and Down and the keys the menus use keep what they do: the help line says why nothing changed.
- **Clear a Button** (a pad's) or **Remove a Key**, and **Reset to Defaults**.

The first line, **All Keyboards & Remotes**, adds one more button to each of six actions on all of them at once: **Up**, **Down**, **Left**, **Right**, **Select / OK** and **Back**. Select on a row asks **Press a new button for [ACTION]**. The next key pressed is saved: a keyboard's key, a remote's own button (a remote's separate Consumer Control buttons such as Home or Menu, read straight from the device on Linux), or a mouse button (an air mouse's OK is often a left click). Whatever does Back cancels instead of binding, and the buttons that do Select / OK, Up or Down and the keys the menus use (the arrows, Enter, Space) keep what they do. The row then reads `DEFAULT + KEY`.

- The default key always keeps working, so a bad choice can't lock you out.
- One button serves one action: binding a button to an action takes it off any other.
- **Reset to Defaults** clears all six.
- Saved as `app.remote_keymap.up`, `.down`, `.left`, `.right`, `.select` and `.back`, each a number: a Qt key code; a Consumer Control button as 33554432 (`0x02000000`) plus its Linux key code; a mouse button as 50331648 (`0x03000000`) plus Qt's button number (1 left, 2 right). `0` means none.

A controller's own settings are saved under `app.controllers`, by its key. Every gamepad's buttons, the map each pad starts from, can also be set in the data folder's `input.cfg`. All of it is in [Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls#settings--controls).

### Bluetooth

<table>
<tr><th width="50%">Bluetooth</th><th width="50%">Pairing a keyboard</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/bluetooth.png" width="100%" alt="Bluetooth" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/bluetooth-pairing.png" width="100%" alt="Pairing a keyboard" /></td></tr>
<tr><td>Search finds what is in pairing mode nearby for a minute. Select pairs one and connects it.</td><td>A keyboard is paired by typing the code it asks for, then its Enter. The digits light up as they are typed.</td></tr>
</table>

The page talks to BlueZ, Linux's Bluetooth service, as the user OSD/OS runs as (who must be in the `bluetooth` group; `install.sh` and the image add it).

- **Bluetooth** turns the adapter on or off. If it won't turn on, a **Details** line appears with what the system says about it (the adapter's state, rfkill's switches, the system log's last Bluetooth lines), so a photo of the screen shows what went wrong.
- **Search** looks for devices for a minute; select stops it early. Put the device in pairing mode first.
- **Found** lists what the search found. Select pairs a device, trusts it so it reconnects by itself after a restart, and connects it. A keyboard shows a code to type on it; a phone shows the same code on both, to confirm; a gamepad or mouse asks nothing. Back cancels a pairing.
- **Paired** lists the paired devices. Select offers **Connect** or **Disconnect**, and **Forget**.
- Leaving the page stops a search and cancels a pairing.

If BlueZ isn't running, or the computer has no adapter, the page says **No Bluetooth**. On a Mac, pair devices in the Mac's own Bluetooth settings.

### Update

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/update.png" width="100%" alt="Update: INSTALLED: DEV, PRESS [ENTER] TO CHECK FOR LATEST" />

Update compares the installed version with the latest release of [mehmetraif/OSD-OS](https://github.com/mehmetraif/OSD-OS/releases/latest) and shows its notes (▲ ▼ scroll them). Select does the next step, which the hint bar names:

1. **CHECK** asks GitHub for the latest release.
2. **DOWNLOAD** fetches this platform's file (`-linux-arm64.tar.gz` on a Pi, `-linux-x86_64.AppImage`, `-macOS-arm64.dmg`) into the data folder's `updates`, with a progress bar, and checks it against the release's `SHA256SUMS` when the release has one. Back during a download cancels it.
3. **INSTALL** asks **Install update?**:
   - **Apply & Restart** on a Pi under the autostart service: OSD/OS exits, and the launcher swaps the new release into `/opt/osdos` before it starts again.
   - **Quit & Apply on Next Launch** on a Pi run by hand, and on an AppImage: on a Pi the launcher applies it at the next start; an AppImage swaps the new file over itself at once (keeping a `.bak` until the new one starts), then starts it again in a desktop session, or just quits in Steam's Gaming Mode, for Steam to start it.
   - **Apply & Relaunch** on a Mac with the app in `/Applications`, or **Quit & Open Disk Image** when it is elsewhere.
   - **Discard Update** deletes the download.

A build made from source (`DEV`) can check but not download, and so can the OSD/OS image when built from a branch rather than a release. An install that can't apply updates says why under the status line: one made before in-app updates, or 240-MP's, needs the installer run once more; one outside `/opt/osdos` must be updated by hand. Everything about installing is in [Installation](https://github.com/mehmetraif/OSD-OS/wiki/Installation).

### About

<table>
<tr><th width="50%">About</th><th width="50%">The license</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/about.png" width="100%" alt="About" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/license.png" width="100%" alt="The license" /></td></tr>
<tr><td>The wordmark, then a line for each part of what OSD/OS is, its detail in the help line.</td><td>Behind the LICENSE line: the notice, then the GNU GPL's text, a page at a time.</td></tr>
</table>

About opens on the OSD/OS wordmark and SMART TV FOR CRT. Under it are lines whose help line carries the detail, shown even with Help Line off: **Build** (the commit the build was made from and the day; the version is in the title bar), **Developer**, **Based On** (240-MP), **License** (GNU GPL v3), **Source**, **Written With** (Claude Code), **Artwork** (ChatGPT), **Fonts** (VCR OSD Mono, Unifont), **Built With** (Qt, SDL2, mpv), **Image OS** (Raspberry Pi OS) and **Data** (TMDB, Open-Meteo). Select on License shows the notice the GPL asks for, then the license's text from the `LICENSE` file every build carries, a page at a time with ▲ ▼ ([Credits](https://github.com/mehmetraif/OSD-OS/wiki/Credits)).

### Quit

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/quit.png" width="100%" alt="Quit" />

Select asks **Really quit?**. Run by hand, or on a Mac, the answers are **Yes** and **No**. Started by the autostart service (`OSDOS_AUTOSTART` is set, as on the OSD/OS image), they are:

| Answer | Exit code | What the service's stop helper (`osdos-stop`) does |
|---|---|---|
| Power Off | 0 | Powers the Pi off |
| Restart | 12 | Reboots. Offered when the launcher is new enough (`OSDOS_LAUNCHER_API` 2 or later) |
| Exit to Terminal | 10 | Starts a login shell on `tty1`; `sudo systemctl start osdos` brings OSD/OS back |
| Cancel | | Nothing |

Ctrl+Q on a keyboard quits too, which under the service is the same as Power Off. `sudo systemctl stop osdos` over SSH stops the app and leaves the Pi on.

## Settings without a row

A few app settings have no row and are set in `config.json` by hand, with OSD/OS stopped ([Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#editing-by-hand)):

| Key | Values | What it does |
|---|---|---|
| `app.display_index` | A number, `0` by default | The display the menus open on, and mpv plays on, when a Mac or a Linux desktop has several. The log lists them at start: `[main] display index 1: "…" 1280x720 at (3840,0)`. Out of range falls back to 0 |
| `app.mpv_video_args` | A string of mpv flags, separated by spaces | Replaces the video output and decoder flags OSD/OS picks for the device ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv#mpv_video_args)) |
| `app.auto_crop` | `On`, `Off` | The setting before Scaling: `On` reads as Pan & Scan while `app.video_scaling` is unset |
| `app.theme` | A skin's folder name | The Skin setting before skins had their own name, read while `app.skin` is unset |

## See also

- [Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files): `config.json` and everything else in the data folder
- [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv): what Scaling, Transparent Background, Video Levels, Channel Logo and Audio Output do to mpv
- [Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes), [Skins](https://github.com/mehmetraif/OSD-OS/wiki/Skins), [Effects](https://github.com/mehmetraif/OSD-OS/wiki/Effects) and [Menu music](https://github.com/mehmetraif/OSD-OS/wiki/Menu-Music): the look in detail
- [Display Output](https://github.com/mehmetraif/OSD-OS/wiki/Display-Output) and [Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output)
- [Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls): keyboards, remotes, gamepads and `input.cfg`
- [Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules): what each module does
- [Using OSD/OS](https://github.com/mehmetraif/OSD-OS/wiki/Using-OSD-OS)
