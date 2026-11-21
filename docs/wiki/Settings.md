# Settings

Settings is OSD/OS's own menu. This page lists every page and row in screen order: values, default, what it does and its key in `config.json`.

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/settings.png" width="100%" alt="Settings' first screen: TV / Movie, Arcade, Look, Screen, Sound, Controls, Network and Startup, each ending in a ►, a ▼ for System and Quit below, the help line under them" />

## Getting around

Back on the main menu opens Settings (`[ESC]:SETTINGS` in the hint bar), and so does the **Settings** row at the foot of the Standard and Broadcast menus. Its first screen lists the pages: **TV / Movie**, **Arcade**, **Look**, **Screen**, **Sound**, **Controls**, **Network**, **Startup**, **System**, then **Quit**.

| Action | Keyboard | Gamepad | What it does in Settings |
|---|---|---|---|
| ▲ ▼ | Up, Down | D-pad, left stick | Moves a row, round at the ends |
| ◄ ► | Left, Right | D-pad, left stick, LB, RB | Changes the value. A slider moves by 10. ► opens a row ending in ► |
| Select | Enter | A | Opens a row ending in ►. Turns Transparent Background on or off |
| Back | Esc, Backspace | B, or the pad's Back/Select button | Back to the row that opened the page, or to the main menu |

- A change is saved at once, under `app.<key>` in `config.json` ([Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#configjson)).
- Most rows apply at once. Scaling, Video Levels, Channel Logo, Logo Image, 1080p Playback and Screen Saver during a video apply from the next video; Scaling and Video Levels also at once on a video playing behind the menus (Transparent Background).
- A value that can't be saved (a full or read-only disk) isn't applied. The log says `[AppCore] Setting not saved: …`.
- The first screen's title bar shows a release's version and the device's IP address. A page shows its name: `SETTINGS | LOOK`.
- The help line under a page says what the selected row is, then what the value on it does, and follows ◄ ► as the value changes.
- The hint bar names only the keys the selected row takes: **OPEN** on a row ending in ►, **CHANGE** on a list, **ON/OFF** where select turns a slider on or off, **ON/OFF** and **SETTINGS** on a module's row, and only back on a dimmed row.
- No row is hidden. One that can't be used is dimmed, and its help line starts with why; a page opens on its first row that can be used. The first screen's Network row is dimmed where neither Wi-Fi nor Bluetooth can be used (a Mac, Windows) ([Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting#rows-dimmed-in-settings)).

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/settings-dimmed.png" width="100%" alt="Settings → Look with OSD Frame selected and dimmed rows for Orb Fog and OSD Frame, the help line starting: Only with OSD Background on Box" />

## TV / Movie

`SETTINGS | TV / MOVIE`: Scaling, Transparent Background and Info Screen, then each video module under **MODULES**, then the rest of the videos' rows under **VIDEO**. What the rows do to mpv is in [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv).

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Scaling | Letterbox, 14:9, Pan & Scan, Anamorphic | Letterbox | How a 16:9 picture fills a 4:3 screen, in every module whose own Scaling is Default. **Letterbox**: bars above and below. **14:9**: thinner bars, the sides cut a little. **Pan & Scan**: no bars, the sides cut. **Anamorphic**: squeezed, for a TV set to 16:9. CROP in the deck menu changes it during a video | `app.video_scaling` |
| Transparent Background | Off, or a slider from TRANSPARENT to SOLID, 0 to 100 in steps of 10 | Off | Plays the video inside OSD/OS's own window: back returns to the menus with the picture behind them, and the main menu's first row returns to the video. The slider sets how solid the menus are; at SOLID the picture is hidden but plays on. Select turns it on (at 40) or off. Needs libmpv | `app.transparent_background`: a number from 0 to 100, or `"Off"` |
| Info Screen | Off, With ►, 1 sec, 2 sec, 3 sec, 5 sec | 3 sec | A film's details in Netflix, Prime Video and YouTube. **With ►** (saved as `key`): with ► on a film. **A number**: also by itself once the cursor has rested that long. **Off**: ► offers the film's options instead. Dimmed while none of the three is turned on | `app.info_screen`: `off`, `key`, `1`, `2`, `3` or `5` |
| Video Levels | Auto, Limited, Full | Auto | The colour range mpv sends: **Limited** if blacks look crushed, **Full** if they look grey | `app.video_output_levels` |
| 1080p Playback | On, Off | On | Raspberry Pi 3 only. **On**: smooth 1080p; 14:9 and Pan & Scan show the whole picture, and the deck menu has no CROP. **Off**: cropping works, 1080p stutters | `app.smooth_playback` |
| Channel Logo | Off, Top Left, Top Right, Bottom Left, Bottom Right, All Corners | Off | A logo in a corner of the picture while a video plays | `app.video_logo`: `off`, `tl`, `tr`, `bl`, `br` or `all` |
| Logo Image | OSD/OS Logo, or a PNG, JPG, JPEG, SVG, GIF, BMP or WebP picked on the file browser | OSD/OS | Your own picture as the channel logo, as tall as OSD/OS's. A PNG with a clear background works best. Only while Channel Logo isn't Off | `app.video_logo_image`: the file's full path, `""` for OSD/OS's |
| Video Arguments | Default, or a line typed on the on-screen keyboard | Default | For experts: mpv's own options for the video output and decoder, in place of those OSD/OS picks for the device ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv#mpv_video_args)). From the next video | `app.mpv_video_args` |

<table>
<tr><th width="50%">Channel Logo</th><th width="50%">Under the menus</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/channel-logo.png" width="100%" alt="Channel Logo" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/channel-logo-menu.png" width="100%" alt="The logo under the menus" /></td></tr>
<tr><td>The logo in a corner of the picture.</td><td>With Transparent Background the menus lie over it.</td></tr>
</table>

<table>
<tr><th width="50%">Logo Image</th><th width="50%">Picking the picture</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/channel-logo-custom.png" width="100%" alt="Logo Image" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/logo-picker.png" width="100%" alt="Picking the picture" /></td></tr>
<tr><td>Your own picture in its place.</td><td>Select on a picture, or <b>OSD/OS Logo</b> to go back to it.</td></tr>
</table>

**The file browser** that Music File and Logo Image open is Local Files' tree. At its top is the default (**No File**, **OSD/OS Logo**), then **Home**, **Media** (`/media`), **Drives** (`/run/media/<user>`), **Volumes** (`/Volumes`, on a Mac) and **Root** (`/`); on Windows, in Root's place, each drive by its letter (**C:**, **D:**).

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/settings-modules.png" width="100%" alt="TV / Movie's page further down: 1080p Playback dimmed, Loading Effect, Loading Colors, Channel Logo, Logo Image, Info Screen, then the MODULES heading and Ambient:Mode" />

### The video modules

Under **MODULES** is a row for each module of the TV / Movie group (`"settings_group": "tv"` in its manifest). Select opens the module's page ([Modules](#modules)).

## Arcade

`SETTINGS | ARCADE` is one page for the Arcade group (`"settings_group": "arcade"`): the [Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#settings) module's rows, Enabled first and the rest under the headings **GAMES**, **ARCADE SCREEN**, **PLAYING**, **SCORES AND PICTURES**, **ONLINE AND LAN** and **EMULATORS**, which the cursor steps over; then the [High Scores](https://github.com/mehmetraif/OSD-OS/wiki/High-Scores#settings) module's Enabled, a row named **High Scores**, under a **HIGH SCORES** heading. Each row is saved in its own module, under `modules.com.osdos.arcade` or `modules.com.osdos.highscores` ([Modules](#modules)).

## Look

`SETTINGS | LOOK`. A theme sets the part rows: Color Scheme, Skin, the effects and Menu Music.

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/settings-look.png" width="100%" alt="Settings → Look: THEME NONE, ORB FOG dimmed, COLOR SCHEME VIDEO 1, SKIN NONE, EFFECTS, OSD BACKGROUND FULL, OSD FRAME dimmed, BOOT COLORS THEME" />

- **THEME** in a part row (saved as `""`) means the theme's. **Off** (saved as `"Off"`) means none. A choice of its own (saved by its name) replaces the theme's.
- Choosing a theme sets every part row back to THEME.
- On THEME, the row's help line opens with what the theme has for it: **Theme's: Glow**, **Theme's: none**, or **its own** for a part the theme draws itself.
- Without a theme the rows have no THEME, and `""` shows as Video 1, None or Off.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Theme | None, Arcade, Demoscene, Green Screen, Inferno, Late Show, Matrix, Orb, Trinitron, Winter, and those in the data folder's `themes` | None | The colour scheme, skin, effects and menu music in one ([Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes)) | `app.osd_theme`: the theme's folder name (`trinitron`, `late-show`), `""` for None |
| Color Scheme | THEME (with a theme), Video 1, Late Night, Synthwave, Terminal, T-120, Amber, Kinescope, SMPTE ECR (saved as SMPTE ECR 1-1978), and your own | THEME; without a theme, Video 1 | The two colours everything is drawn in ([below](#color-schemes)). It has no Off | `app.color_scheme`: the scheme's name, `""` for THEME |
| Skin | THEME (with a theme), None, DOS, Rounded, and those in the data folder's `skins` | THEME; without a theme, None | The shapes of the window's frame, bars and selected line, and its icons ([Skins](https://github.com/mehmetraif/OSD-OS/wiki/Skins)) | `app.skin`: the skin's folder name (`dos`, `rounded`), `"Off"` for None, `""` for THEME |
| Effects | A page | | [Below](#effects) | |
| Menu Layout | Standard, Camcorder, Studio Monitor, Broadcast, Flat Panel | Standard | How the menus are laid out: Settings, the main menu and the pages and modules opened from them ([below](#menu-layout)) | `app.menu_layout` |
| OSD Background | Full, Off, Box | Full | What the menus are drawn on. **Full**: the scheme's background over the whole screen. **Off**: black, the menus in the scheme's lighter colour. **Box** (saved as `Window`): a framed window of the scheme's background, black or a playing video around it | `app.osd_background` |
| OSD Frame | On, Off, Shadow | On | The window's frame: a line (or the skin's frame), none, or the line with a shadow at the right and the foot. Only with OSD Background on Box | `app.osd_frame` |
| Loading Effect | On, Off | On | A tape's noise and tracking bands on the loading screen | `app.loading_effect` |
| Loading Colors | Menu, Blue | Menu | The loading screen in the menus' colors, a theme's or the color scheme's, or white on blue | `app.loading_colors`: `Theme` (Menu) or `Default` (Blue) |
| Boot Colors | Menu, Blue | Menu | The OSD/OS image's boot screen in the menus' colors, a theme's or the color scheme's, or white on blue. Dimmed elsewhere: only the image has a boot screen | `app.boot_colors`: `Theme` (Menu) or `Default` (Blue) |
| Hint Bar | On, Off | On | The key hints at the foot of every screen. Off gives every menu a row more; the keys work as before | `app.hint_bar` |
| Help Line | On, Off | On | The line about the selected row under a menu. Off hides it everywhere but About | `app.help_line` |
| Status Icons | On, Off | On | What is on, at the right end of every title bar ([below](#status-icons)) | `app.status_icons` |

### Effects

`SETTINGS | LOOK / EFFECTS`. Each row but the last, Orb Fog, is a part of the theme. Its default is THEME; without a theme, Off.

| Setting | Values | What it does | Config key |
|---|---|---|---|
| Text Effect | THEME, Off, Rainbow, Shimmer, Glow, Flicker | What the text, the lines and the bars do. Needs a GPU | `app.text_effect` |
| Background Effect | THEME, Off, Matrix, Fire, Stars, Snow, Flag (saved as Checkered Flag), Plasma | What goes on behind the menus, in the window. Needs a GPU | `app.background_effect` |
| Selector Effect | THEME, Off, Sparkles, Welding, Lightning, Rainbow, Snow | What goes on round the selected line. Works everywhere | `app.selector_effect` |
| Screen Effect | THEME, Off, Scanlines, CRT, VHS | A picture tube's or a tape's look over the whole screen. Needs a GPU | `app.screen_effect` |
| Transition | THEME, Off, Fade, Cube, Ripple, Wave, Drop | How one window gives way to the next. Ripple, Wave and Drop need a GPU; without one a theme's falls back to Fade, and one saved before plays, and reads, as Fade | `app.transition` |
| Orb Fog | Picture, Live | The fog behind the Arcade screen's panels in the [Orb look](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#the-orb-look). **Live** is thicker and quicker, and more work for a Raspberry Pi's GPU. Only with a theme in the Orb look (Picture when unset) | `app.orb_fog` |

The effects and the menu music rest while a video plays, loads or has its menu open, and while a player has the screen ([Effects](https://github.com/mehmetraif/OSD-OS/wiki/Effects)).

### Menu Layout

<table>
<tr><th width="50%">Settings in Camcorder</th><th width="50%">The main menu in Camcorder</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/settings-camcorder.png" width="100%" alt="Settings in Camcorder: SETTINGS | LOOK in the title bar, a column of icons at the left with Look's boxed, Look's rows beside it, Menu Layout's row on Camcorder selected and the help line about it" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/main-menu-camcorder.png" width="100%" alt="The main menu in Camcorder: OSD/OS | TV / MOVIE in the title bar, the TV, Arcade, More and Settings icons at the left, the video modules beside them" /></td></tr>
<tr><td>The pages as icons, the selected one's rows beside them.</td><td>The modules in groups: TV / Movie, Arcade, More, and Settings.</td></tr>
</table>

Every layout has the same pages and rows, and changes at once. A layout is kept on what is opened from the menus too: the pages opened from Settings (Wi-Fi, Bluetooth, Controls, About, Update, Cabinet Lock, a module's settings), the questions they ask, and the modules' lists and trees (Local Files, Playlists, YouTube, the streaming services, High Scores, Scripts, and Plex's, Jellyfin's and Emby's libraries), with Plex's sign-in, NFC Reader's page and Weather's setup page.

| Layout | How it looks | Keys |
|---|---|---|
| Standard | A list, a page at a time | As [above](#getting-around) |
| Camcorder | The pages as icons in a column, the selected page's rows beside it. The column stays on the pages opened from there (Wi-Fi, Bluetooth, Controls, About, a module's settings) | ▲ ▼ go from page to page, ► or select into the rows, back out to the icons |
| Studio Monitor | A blue window, black inside, all of a piece: the page's name on its tab, the pages' icons down its left side, the keys on its foot. Its first page, **Status**, shows what is set (**System** where the system picks the output), and follows it while it is open. The window stays on every page opened from there, the page drawn inside it and named on its tab | As Camcorder |
| Broadcast | Plain blue, the way to the page centred at the top (`NETWORK / WI-FI`), a three-line key guide | A list. A value changes in two steps |
| Flat Panel | Black, mixed case, the path at the top (`>Settings >Network >Wi-Fi`), a page of rows at a time (`1/2`) | As Broadcast. ◄ goes back |

<table>
<tr><th width="50%">Settings</th><th width="50%">The main menu</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/settings-studio-monitor.png" width="100%" alt="Studio Monitor: a blue window, black inside with round corners, over the blue ground, STATUS on the tab that rises from its top left corner, the pages' icons down its left side with STATUS's on yellow, the keys on its foot, and the lines Output, Audio, Wi-Fi, Bluetooth, Theme, Start On, Cabinet Lock, Version and IP Address with their values" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/main-menu-studio-monitor.png" width="100%" alt="The main menu in Studio Monitor: TV / MOVIE on the tab, the groups' icons down the window's left side, the video modules in the window, a yellow square before Local Files" /></td></tr>
<tr><td colspan="2"><b>Studio Monitor.</b></td></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/settings-broadcast.png" width="100%" alt="Broadcast: plain blue, LOOK centred at the top, the Look page's rows, Menu Layout's value STUDIO MONITOR boxed in white while it is being changed, and the guide TO CHANGE, TO CONFIRM, TO CANCEL" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/main-menu-broadcast.png" width="100%" alt="The main menu in Broadcast: OSD/OS centred, every module with three dots after it, a filled square before Local Files, and the guide TO SELECT, TO ENTER, TO SETTINGS" /></td></tr>
<tr><td colspan="2"><b>Broadcast.</b></td></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/settings-flat-panel.png" width="100%" alt="Flat Panel: black, >Look at the top left and 1/2 at the top right, the rows in mixed case as Label: Value, Menu Layout's value Broadcast in yellow while it is being changed on a grey bar" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/main-menu-flat-panel.png" width="100%" alt="The main menu in Flat Panel: black, OSD/OS at the top left, the modules in mixed case each with a ▶, Local Files on a grey bar" /></td></tr>
<tr><td colspan="2"><b>Flat Panel.</b> The title is the path to the page (<code>&gt;Settings &gt;Look &gt;Effects</code>).</td></tr>
</table>

- **Two steps** (Broadcast, Flat Panel): select starts the change, ▲ ▼ go through the choices, select saves, back cancels. ◄ ► change nothing. A module's settings change the same way there.
- **Inside a module**, Studio Monitor's strip shows the modules of its group by their logos, as Camcorder's column does, and the three monitor layouts draw the module's lists in their own colours.
- **Outside every layout**, as they take the whole screen: a video and its loading screen, a film's or a show's page with its poster, the Arcade screen and the Arcade module's own pages, Test Patterns, Export to OSD/OS Setup's codes, the on-screen keyboard, Jellyfin's and Emby's Connect to Server pages, Ambient Mode's page, and a pad's Game Buttons page.
- **Key Protect**, the last row of the first screen in Broadcast and Flat Panel, is Cabinet Lock.
- **The main menu** in Camcorder and Studio Monitor groups the modules: **TV / Movie**, **Arcade**, **More** (Scripts), then **Settings**. Broadcast's ends with a row for Settings.
- Studio Monitor, Broadcast and Flat Panel use colours of their own, whatever the theme. A row that can't be used is paler, not checkered.
- A module's page is laid out as in Standard in those three. The Arcade screen keeps its own layout.

### Color schemes

The eight colour schemes, by their two colours (`primary` on `surface`):

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

Your own come from two files in the data folder: `custom_color_schemes.json` adds any number by name, and `custom_color_scheme.json` adds one called **Custom** ([Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#custom-color-schemes)).

### OSD Background and OSD Frame

<table>
<tr><th width="33%">OSD Background: Box</th><th width="33%">OSD Background: Off</th><th width="33%">OSD Frame: Shadow</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/osd-window.png" width="100%" alt="OSD Background: Box" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/osd-off.png" width="100%" alt="OSD Background: Off" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/skin-dos.png" width="100%" alt="Skin: DOS, OSD Frame: Shadow" /></td></tr>
<tr><td>A framed window of the scheme's background, black around it.</td><td>The menus on black, in the scheme's lighter colour.</td><td>Skin DOS gives the frame a double line; Shadow adds a shadow.</td></tr>
</table>

### Status Icons

With **Status Icons** on, the right end of every title bar shows what is on:

| Icon | Shown while |
|---|---|
| **HI** | Arcade's **Save High Scores** is on ([Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#high-scores)) |
| A trophy | Arcade's **Online High Scores** is RetroAchievements and its account is signed in, or signs in at the next game |
| A globe | Arcade's **Online Play** is on |
| A network, two machines joined to a third | Arcade's **LAN Play** is on |
| Bluetooth's rune | Bluetooth is on; with a dot either side while a device is connected |
| Wi-Fi's bars | Wi-Fi is on: the signal of the network joined, or the bars' feet alone while none is |

The Arcade screen shows them at its bottom right. A module turned off takes its icons with it.

## Screen

`SETTINGS | SCREEN`.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Display Output | Those this Pi has of: HDMI, Composite NTSC, Composite PAL, GPIO NTSC, GPIO PAL, SCART RGB 480i, SCART RGB 576i, SCART RGB 240p, SCART RGB 288p, JAMMA 720x240, JAMMA 320x240, JAMMA 1920x240, JAMMA 720x288, JAMMA 1920x288, JAMMA 720x480i, JAMMA 640x480i, JAMMA 720x576i, JAMMA 25 kHz, JAMMA 31 kHz | The image's build setting, HDMI unless built otherwise | Where the picture goes ([below](#display-output)). On the OSD/OS image only | None: the preset is copied over `/boot/firmware/osdos-display.txt` |
| Use Display | Each display there is, by number, name and size | The first | Which display OSD/OS opens on, and mpv plays on, from its next start. Dimmed with one display, as on a Pi | `app.display_index`: `0` for the first |
| Rotation | None, Turned Right, Turned Left | None | For a monitor turned on its side (TATE): menus, games and videos stand upright on it. **Turned Right**: its top is on the right as you face it. **Turned Left**: on the left ([A monitor on its side](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#a-monitor-on-its-side-tate)). On a desktop, turn the screen in the system's settings instead. For a monitor turned only for a vertical game, use Arcade → Emulator Video → Vertical Games | `app.monitor` |
| Test Patterns | A page | | Pictures to set the screen up by ([below](#test-patterns)) | None |
| Screen Saver | Off, 1 min, 2 min, 5 min, 10 min | 5 min | How long without a key or a mouse move before a black screen with the OSD/OS logo bouncing across it, in the menus (with a video playing behind them too) and over a paused video. It waits while a Test Pattern, Export to OSD/OS Setup's codes or a Bluetooth pairing's code is up. The first key only wakes it | `app.screensaver_timeout`: `"OFF"` or the seconds |
| Mouse Pointer | Off, 2 sec, 5 sec, 10 sec, 30 sec, Always | 5 sec | How long the pointer stays after the mouse stops. **Off**: never shown, and the selection doesn't follow the mouse. Clicks and the wheel work whatever this is set to ([Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls#the-mouse)) | `app.mouse_pointer`: `off`, `2`, `5`, `10`, `30` or `always` |
| Window Mode | Off, On | Off | The menus in a desktop window, 960×720 to start with. F11 or Alt+Enter switches at any time. On Windows and on a Linux or BSD desktop only | `app.window_mode` |

### Display Output

Select opens the list of this Pi's outputs, with the one in use (`Now: HDMI`). Choose another, then:

| Choice | What happens |
|---|---|
| **Switch and Restart** | The Pi restarts on it |
| **At Next Start** | The Pi starts on it the next time. Until then the row reads **Next: <output>**; choosing the output in force again takes it back (**Stay on <output>?**, **Stay**) |
| **Switch and Power Off** | The Pi powers off, and starts on it when switched on: for a screen that isn't connected yet |

- On the new output OSD/OS asks **Keep this display output?**. **Keep** keeps it. **Switch Back**, back, or 30 seconds without an answer returns to the output before.
- JAMMA 25 kHz and 31 kHz warn first: too high a scan rate can damage a 15 kHz monitor.

The outputs, the cables, the launcher each choice needs and putting a card right from a computer are in [Display Output](https://github.com/mehmetraif/OSD-OS/wiki/Display-Output).

### Test Patterns

Full-screen pictures to set up a TV or monitor by, from Artemio Urbina's [240p Test Suite](https://github.com/ArtemioUrbina/240pTestSuite) (GPL v2 or later). **◄ ►** go through the patterns and **▲ ▼** through a pattern's kinds. **Select** shows or hides the pattern's name and how to set the screen with it.

<table>
<tr><th width="50%">Grid</th><th width="50%">Monoscope</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/test-patterns.png" width="100%" alt="Settings → Screen → Test Patterns: the Grid, white squares with a dot in each and red ones round the edge, and in a box at the foot its name, what to set with it and the keys" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/test-patterns-monoscope.png" width="100%" alt="The Monoscope: the centre cross, the arrows at the sides, the corners' patterns and the red square, with its name and what to set with it in the box" /></td></tr>
<tr><td>Size, position and geometry.</td><td>A tube's convergence, purity and geometry.</td></tr>
</table>

| Pattern | Kinds | What to set with it |
|---|---|---|
| Grid | | Size, position and geometry: the red squares just show at every edge |
| Monoscope | | A tube's convergence, purity and geometry |
| Convergence | Lines, Crosses, Dots, Colors, Colors on Black | Red, green and blue landing on one another, at a lowered brightness |
| PLUGE | | Black level, with Brightness |
| Color Bars | | White level with Contrast, then each colour's |
| SMPTE Color Bars | | Colour and tint, with a blue filter or the set's blue-only mode |
| Gray Ramp | | Colour balance: every bar grey |
| Sharpness | | Sharpness (aperture on a monitor) |
| Solid Color | White, Black, Red, Green, Blue | Purity and white balance |

While a pattern is up the screen effects rest and the screen saver waits.

## Sound

`SETTINGS | SOUND`: the sound card that plays, and the music under the menus.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Audio Output | Auto, then each sound card that can play: AV Jack, HDMI (HDMI 0 and HDMI 1 when there are two), a USB card by its name | Auto | Which sound card plays, from the moment you choose it ([below](#audio-output)). On Linux with plain ALSA only | `app.audio_output`: `""` for Auto, or `{ "card", "name" }` |
| Menu Music | THEME, Off, File | THEME; without a theme, Off | A tune under the menus: the theme's, if it has one, or the file below ([Menu music](https://github.com/mehmetraif/OSD-OS/wiki/Menu-Music)) | `app.menu_music`: `""`, `"Off"` or `"File"` |
| Music File | No File, or an MP3, WAV, OGG, Opus, FLAC, M4A, AAC, MID, MIDI, XM, MOD, S3M or IT picked on the file browser | None | The menu music's own file, played over and over. Only with Menu Music on File | `app.menu_music_file`: the file's full path |
| Music Volume | A slider from QUIET to LOUD, 0 to 100 in steps of 10 | 60 | How loud the menu music is. Dimmed while Menu Music is Off | `app.menu_music_volume`: a number |

### Audio Output

- **Auto** is the Pi's own default; `audio-device` in `mpv.conf` and `/etc/asound.conf` still choose. While that card can't play, the first card that can plays, and the row says so: **Auto (USB Audio Device)**.
- A card chosen and then unplugged shows as `NAME (Unplugged)`. Sound goes as on Auto until it is back.
- Without a USB sound card to choose, the help line says what the system sees of one.

More in [Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output).

## Controls

Controls is a page of its own, explained in [Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls#settings--controls).

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/controls.png" width="100%" alt="Controls: 8BITDO PRO 2 PLAYER 3, marked as the one used last and selected, OSD/OS JAMMA P1 PLAYER 1, OSD/OS JAMMA P2 PLAYER 2, KEYBOARDS & REMOTES, NOT CONNECTED 1" />

| Line | What it does |
|---|---|
| A gamepad | Each one connected, the one used last on top with a ●, and the player it is. Select opens its page: **Test Buttons**, **Menu Buttons**, **Game Buttons**, **Player**, **Button Names** and **Reset**, which asks first. Saved under `app.controllers`, by its key; its player under `app.players` |
| **All Players Automatic** | Only while a pad's player was chosen by hand: every player back to Auto |
| **Keyboards & Remotes** | **All Keyboards & Remotes** and, on Linux, each keyboard and remote: extra keys for **Up**, **Down**, **Left**, **Right**, **Select / OK**, **Back** and **Play / Pause**. The action's own key keeps working. Every keyboard's are saved as `app.remote_keymap.up`, `.down`, `.left`, `.right`, `.select`, `.back` and `.play_pause`: a number for one key, a list for several, `0` for none |
| **Not Connected** | Only while there are any: controllers that are away and have settings of their own |

Every screen that waits for a press takes a tap for the answer; holding any button for a second and a half stops it with nothing changed.

Every gamepad's starting map can also be set in the data folder's `input.cfg`.

## Network

`SETTINGS | NETWORK`: **Wi-Fi** and **Bluetooth**, a page each. Neither is saved in `config.json`: NetworkManager keeps the networks, BlueZ the pairings.

### Wi-Fi

<table>
<tr><th width="50%">Wi-Fi</th><th width="50%">The country</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/wifi.png" width="100%" alt="Wi-Fi: COUNTRY, SEARCH and NETWORK NOT LISTED, then the saved networks, HOME connected, and the others, each with its lock and its signal in bars" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/wifi-country.png" width="100%" alt="The Wi-Fi country's chooser, the countries by name, Turkey chosen" /></td></tr>
<tr><td>Each network with its lock and its signal in bars.</td><td>▲ ▼ move a country at a time, ◄ ► from letter to letter.</td></tr>
</table>

| Row | What it does |
|---|---|
| **Wi-Fi** | Turns it on or off. **Blocked** means a switch keeps the radio off |
| **Country** | The country the Pi is used in. Wi-Fi stays off until it is set |
| **Search** | Looks for networks for a minute; select stops it early |
| **Network Not Listed** | Joins a network that doesn't say its name: type its name, then choose open or type its password |
| **Saved** | The networks NetworkManager keeps, those out of reach as **Not Here**. Select offers **Connect**, **New Password** and **Forget**; on the one joined, **Disconnect** and **Forget**. Forget asks first (**Forget …?**, the cursor on **Cancel**): its password has to be typed again to join it |
| **Networks** | The others, the strongest first. Select one and type its password; an open one is joined at once |

- A network joined is saved and joined by itself from then on.
- A network's help line opens with its band where NetworkManager gives it (**On 5 GHz**), and the title bar shows the band of the one joined beside its address. A game over the network plays better on 5 GHz than on 2.4 GHz, most of all with Bluetooth pads, which share the 2.4 GHz radio.
- The hint bar says what select does on the line under the cursor (**TURN ON**, **CHOOSE**, **SEARCH**, **JOIN**, **OPTIONS**), and only back on **No Wi-Fi**.
- A network that wants a user name too (WPA Enterprise) can't be joined here.
- A character the on-screen keyboard hasn't (`/`, `:`, a bracket) can be typed on a real keyboard.
- Back from a network not listed's password, or from the question before it, keeps the name you typed: **Network Not Listed** opens on it again.
- The help line says how it went: **Connected to …**, or why not (**wrong password?**, **it gave no address (DHCP)**, **it is out of reach**).
- Without NetworkManager or Wi-Fi hardware the page says **No Wi-Fi**. On a Mac, join networks in the Mac's own settings.
- The Wi-Fi row is dimmed where OSD/OS can't use NetworkManager. With a launcher older than level 7 its help line says **Needs a newer launcher**: run `install.sh` again, or put a newer image on the card.

Problems are in [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting#wi-fi-on-the-image).

### Bluetooth

<table>
<tr><th width="50%">Bluetooth</th><th width="50%">Pairing a keyboard</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/bluetooth.png" width="100%" alt="Bluetooth" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/bluetooth-pairing.png" width="100%" alt="Pairing a keyboard" /></td></tr>
<tr><td>Select pairs a device found and connects it.</td><td>Type the code the keyboard asks for, then its Enter.</td></tr>
</table>

| Row | What it does |
|---|---|
| **Bluetooth** | Turns the adapter on or off. If it won't turn on, a **Details** line shows what the system says ([Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting#bluetooth-wont-turn-on)) |
| **Search** | Looks for devices for a minute; select stops it early. Put the device in pairing mode first |
| **Found** | What the search found. Select pairs a device, trusts it so it reconnects after a restart, and connects it |
| **Paired** | The paired devices. Select offers **Connect** or **Disconnect**, and **Forget**, which asks first (**Forget …?**, the cursor on **Cancel**) |

- A keyboard shows a code to type on it. A phone shows the same code on both, to confirm: **Cancel**, under the cursor, or **Pair**. A gamepad or mouse asks nothing.
- Back cancels a pairing. Leaving the page stops a search.
- The hint bar says what select does on the line under the cursor (**TURN ON**, **SEARCH**, **PAIR**, **OPTIONS**), and only back on **No Bluetooth**.
- Without BlueZ or an adapter the page says **No Bluetooth**. On a Mac, pair devices in the Mac's own Bluetooth settings.

## Startup

`SETTINGS | STARTUP`: what OSD/OS opens on, and keeping it there.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Start On | None, then each module that is turned on, by name | None | Opens that module straight after start. A module turned off later is never opened | `app.startup_module`: the module's id, such as `com.osdos.local_files`; `"None"` |
| Play at Startup | None, or the name of the favourite chosen | None | A favourite played straight after start, ahead of Start On. Choose it in its module (► on the entry, then **Play at Startup**); here you can only turn it off | `app.startup_favorite`: `{ "module", "path", "name" }`, or `""` for None |
| Play From | Resume, Beginning | Resume | Where that favourite begins, without asking. Only with a Play at Startup favourite of Local Files' or YouTube's, whose players resume | `app.startup_from` |
| Cabinet Lock | A page: On, Off | Off | Keeps OSD/OS on Start On's module ([below](#cabinet-lock)) | `app.cabinet_lock`: `"On"` or `"Off"`; the buttons `app.unlock_buttons` |

The Scripts module's **Auto-Run On Startup** needs Start On set to Scripts ([Scripts](https://github.com/mehmetraif/OSD-OS/wiki/Scripts)).

### Cabinet Lock

Cabinet Lock keeps OSD/OS on Start On's module, such as a cabinet's Arcade screen. The main menu, Settings and Quit open only with the unlock buttons.

| Row | What it does |
|---|---|
| Cabinet Lock | ◄ ► or select turn it on or off. On asks first, **Lock the cabinet?**. It needs a Start On and unlock buttons. On with no module to stay on any more (Start On set to None since, or its module turned off) it locks nothing, and the row reads **On (No Module)** |
| Unlock Buttons | Select, then hold one to four buttons together for two seconds, of any pads and keyboards. The window names them as they are held (`P1 START + P2 START`). Letting go of everything cancels: after ten seconds with nothing held the old ones are kept, and the window says so and counts the seconds (`STOPS BY ITSELF IN 9`) |

<table>
<tr><th width="33%">Cabinet Lock</th><th width="33%">Unlock Buttons</th><th width="33%">Lock the cabinet?</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/cabinet-lock.png" width="100%" alt="Cabinet Lock: Cabinet Lock On and Unlock Buttons 1 + 2" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/cabinet-lock-buttons.png" width="100%" alt="Unlock Buttons: hold the buttons that unlock the cabinet together, 3 + 4 held" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/cabinet-lock-confirm.png" width="100%" alt="Lock the cabinet? OSD/OS stays on Arcade. Hold 3 + 4 together for three seconds to open the menus. Lock, Cancel" /></td></tr>
<tr><td>The lock and the buttons that open it.</td><td>The buttons held, kept after two seconds.</td><td>Asked before it locks.</td></tr>
</table>

While it is locked:

- **Back** on the start module goes nowhere, and **Ctrl+Q** doesn't quit.
- **The unlock buttons**, held together for three seconds, open the main menu. Held again, or after a restart, OSD/OS is locked again.
- An unlock button pressed alone acts 0.3 seconds late; a direction acts at once.
- With only one unlock button, that button acts as it is let go (a tap), since holding it is the unlock; a direction still acts at once.

To turn the lock off from outside (forgotten buttons, a pad that is gone), either:

- put an `osdos-setup.json` holding `{"osdos_setup": 1, "config": {"app": {"cabinet_lock": "Off"}}}` on the card's boot partition (**bootfs**) or in the data folder and start OSD/OS ([Installation](https://github.com/mehmetraif/OSD-OS/wiki/Installation#osdos-setup-settings-on-the-card-before-the-first-boot)); or
- set `app.cabinet_lock` to `"Off"` in `config.json` over SSH with OSD/OS stopped ([Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#editing-by-hand)).

## System

`SETTINGS | SYSTEM`: Update, System Information, the time zone, the keyboard layout and SSH, OSD/OS Setup and About, then under **MODULES** the modules of no group ([Modules](#modules)). The page opens on Update; off the OSD/OS image the image's rows are dimmed, saying **Only on the OSD/OS image**.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Update | A page | | Checks for a newer release, downloads it and installs it | None |
| System Information | A page; its value the device's name | | The device's name on the network, which it renames, then its model, OS, memory, storage, temperature and network addresses | None: the device's name is the system's hostname |
| Time Zone | A list of the system's time zones; ◄ ► step by region | The system's | The time zone the clock goes by. On the OSD/OS image (from a launcher at level 10); elsewhere it shows the system's, dimmed | None: `timedatectl` keeps it |
| Keyboard Layout | A list of layouts; ◄ ► step by letter | The system's | The keyboard's layout at Exit to Terminal, where a name and a password are typed. As Time Zone | None: `/etc/default/keyboard` |
| SSH | On, Off | The image's | Lets a computer on your network sign in to the device over SSH; its password and keys are set with [OSD/OS Setup](https://osdos.tv/setup/). While on, the help line says why no one could sign in yet, if so. On the OSD/OS image; elsewhere dimmed | None: the system's SSH service |
| Export to OSD/OS Setup | A page | | This device's settings as QR codes for a phone ([below](#export-to-osdos-setup)) | None |
| Paste OSD/OS Setup | An action | | Sets the settings copied on [OSD/OS Setup](https://osdos.tv/setup/) ([below](#paste-osdos-setup)). Only where there is a clipboard, so not on the image | Those it sets |
| About | A page | | What OSD/OS is made of and its licenses | None |

### SSH

Turns `ssh.service` on and starts it, or turns it off and stops it, through the image's stop helper ([The OSD/OS image](https://github.com/mehmetraif/OSD-OS/wiki/The-OSD-OS-Image#the-time-zones-the-keyboards-and-sshs-socket)).

- **Only on or off.** `pi`'s password, the keys that log in and whether a password logs in at all stay as [OSD/OS Setup](https://osdos.tv/setup/) set them, and a later card's SSH setting wins over this row, as it did before. Nothing here sets or shows a password or a key.
- **Set a password or a key with OSD/OS Setup first** shows under the row once it has turned SSH on, when nobody could sign in yet: `pi` has no password (an image built without one) and no key in `~/.ssh/authorized_keys`.
- Turning SSH off leaves a session that is already open running; new ones are refused.

### Paste OSD/OS Setup

1. On [OSD/OS Setup](https://osdos.tv/setup/), choose the settings, then **Copy for a computer**.
2. Select on this row. OSD/OS asks **Paste these settings?**, the cursor on **Cancel**; **Paste** sets OSD/OS's own settings and the RetroAchievements account at once. A clipboard that holds no OSD/OS Setup settings is said, and nothing is asked.

- A window says what was set. What only an OSD/OS card sets stays as it is: the device's name, the display output, Wi-Fi, the password, SSH, the time zone, the keyboard (the time zone, the keyboard and whether SSH is on have rows of their own above).
- The page's file does the same at the next start, put in the data folder or beside the app ([Installation](https://github.com/mehmetraif/OSD-OS/wiki/Installation#osdos-setup-on-a-computer)).

### Export to OSD/OS Setup

This device's settings as QR codes. A phone's camera opens [OSD/OS Setup](https://osdos.tv/setup/) from them with its form filled in, to change and make a card's file of.

- **What goes**: every setting on the form that isn't at its default; the Arcade's favorites; the RetroAchievements user name, Web API key and sign-in token. On an OSD/OS card also the device's name, the display output, Wi-Fi with its password, the time zone, the keyboard, and SSH with its keys: on or off as it is now, the SSH row's say included.
- **What can't go**: pi's password, of which only the hash is kept.
- **Show the codes?** is asked first: anyone who sees the screen can read them.
- **Several codes** (**1/3**, **2/3**…) come where one can't hold the settings; ◄ ► go through them. The page takes them in any order.
- The settings go after the `#` of the page's address, which a browser never sends to a server.

### Update

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/update.png" width="100%" alt="Update: INSTALLED: DEV, PRESS [ENTER] TO CHECK FOR LATEST" />

Update compares the installed version with the latest release of [mehmetraif/OSD-OS](https://github.com/mehmetraif/OSD-OS/releases/latest) and shows its notes. Select does the step the hint bar names:

1. **CHECK** offers a release only when it is newer; otherwise **You're up to date**.
2. **DOWNLOAD** fetches this platform's file (`-linux-arm64.tar.gz` on a Pi, `-linux-x86_64.AppImage`, `-macOS-arm64.dmg`) into the data folder's `updates` and checks its SHA-256 digest. Back cancels it.
3. **INSTALL** asks **Install update?**:

| Answer | Offered | What it does |
|---|---|---|
| **Apply & Restart** | A Pi under the autostart service | OSD/OS exits, and the launcher swaps the new release into `/opt/osdos` before it starts again |
| **Quit & Apply on Next Launch** | A Pi run by hand; an AppImage | A Pi's launcher applies it at the next start. An AppImage swaps the new file over itself, keeping a `.bak`, and starts again; in Steam's Gaming Mode it quits for Steam to start it |
| **Apply & Relaunch** | A Mac, the app in `/Applications` | Installs it and starts again |
| **Quit & Open Disk Image** | A Mac, the app elsewhere | Opens the disk image, to install by hand |
| **Discard Update** | Always | Deletes the download |

- A build from source (`DEV`), and an OSD/OS image built from a branch, can check but not download.
- An install that can't apply updates says why under the status line: run the installer once more, or, outside `/opt/osdos`, update by hand.
- On Windows, FreeBSD and OpenBSD, CHECK names the new release's file, to install by hand.

More in [Installation](https://github.com/mehmetraif/OSD-OS/wiki/Installation).

### About

<table>
<tr><th width="50%">About</th><th width="50%">The license</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/about.png" width="100%" alt="About" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/license.png" width="100%" alt="The license" /></td></tr>
<tr><td>System Information, then a line for each part of OSD/OS.</td><td>The notice, then the GNU GPL's text, a page at a time.</td></tr>
</table>

- **System Information** is the first line: this device's name. Select opens its page ([below](#system-information)).
- **Build** has the commit and the day of the build. The version is in the title bar.
- The other lines say who makes OSD/OS and what it is made of; under **ARCADE**, the arcade's software and sites ([Credits](https://github.com/mehmetraif/OSD-OS/wiki/Credits)).
- Each line's detail is in the help line, shown even with Help Line off.
- Select on **License**, **Test Patterns**, **QR Code generator** or an Arcade software line shows its license, a page at a time with ▲ ▼.

#### System Information

<table>
<tr><th width="50%">System Information</th><th width="50%">Renaming the device</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/system-info.png" width="100%" alt="System Information: DEVICE NAME, MODEL, OS, PROCESSOR, MEMORY, STORAGE, MEDIA STORAGE, TEMPERATURE and UP FOR, then under NETWORK each connection's IP and MAC addresses" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/system-info-rename.png" width="100%" alt="The device's new name typed on the on-screen keyboard" /></td></tr>
<tr><td>The device's name, what it is, and each connection's addresses.</td><td>On the image, select on the name types a new one.</td></tr>
</table>

| Line | What it shows |
|---|---|
| **Device Name** | The system's hostname: what a router lists the device under, and what others reach it at, `<name>.local`. LAN Play hosts under it unless Arcade → Online Name says another ([LAN play](https://github.com/mehmetraif/OSD-OS/wiki/Online-and-LAN-Play#lan-play)) |
| **Model**, **OS**, **Processor**, **Memory** | The board or computer, the operating system, the processor and the memory. The help line adds a Pi's revision and serial number, the versions and the free memory |
| **Storage**, **Media Storage** | What is free on the data folder's drive, and on the films' own drive where it is another |
| **Temperature**, **Up For** | The processor's temperature, and the time since the system started |
| **Network** | Each connection, Ethernet first, then Wi-Fi: its **IP** address (every one in the help line) or **Not Connected**, and its **MAC** address |

Every OSD/OS image starts as `osdos`, so give each device a name of its own:

- **On the OSD/OS image**, select on Device Name types a new one: letters, digits and hyphens, 63 at most, neither end a hyphen. The help line says **Now called …**, or why not ([Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting#two-osdos-on-one-network)).
- **Elsewhere**, rename the computer in its own settings: `sudo hostnamectl set-hostname <name>` on Raspberry Pi OS.

## Modules

All fourteen modules have settings, defined in their `manifest.json` ([Writing a module](https://github.com/mehmetraif/OSD-OS/wiki/Writing-a-Module)). Where a module's row is depends on `settings_group` in the manifest:

| Group | Where the module's page is | Modules, in the order of their folders |
|---|---|---|
| None | Under **MODULES** on System's page ([above](#system)) | Scripts, and a module of your own that names no group |
| `"tv"` | Under **MODULES** on TV / Movie's page ([above](#the-video-modules)) | Ambient:Mode, Emby, Jellyfin, Local Files, Netflix, NFC Reader, Playlists, Plex, Prime Video, Weather, YouTube |
| `"arcade"` | The Arcade page, which the group shares ([above](#arcade)) | Arcade, High Scores |

<table>
<tr><th width="50%">A module's settings</th><th width="50%">Picking a folder</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/module-settings.png" width="100%" alt="A module's settings: Local Files" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/folder-picker.png" width="100%" alt="Picking a folder" /></td></tr>
<tr><td>Local Files' page.</td><td><b>Use This Folder</b> picks the folder open, <b>Default Folder</b> the module's own.</td></tr>
</table>

### Turning a module on

Every module's first row is **Enabled**, saved as `modules.<module id>.enabled`, `true` or `false`. A module's row under **MODULES** on TV / Movie's and System's pages shows it too (`PLEX ····· OFF ►`): ◄ ► turn the module on and off there, and select opens its settings. In Broadcast and Flat Panel, which change a value in two steps, ► and select open its settings, where Enabled is.

- A module turned off is left off the main menu, can't be chosen in Start On, and an NFC card can't hand off to it.
- By default only **Local Files** and **Playlists** are on.
- The main menu picks up a change the next time you go back to it.

Marks in the table: \* only with Screen on Arcade; † only once signed in.

| Module | Module id | Its rows, in order |
|---|---|---|
| [Ambient:Mode](https://github.com/mehmetraif/OSD-OS/wiki/Ambient-Mode) | `com.osdos.ambient_mode` | Enabled, Media Folder, Auto-Launch Playback, Scaling |
| [Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade) | `com.osdos.arcade` | Enabled; GAMES: Games Folder, Hide Bootlegs, Hide Incomplete Games, Regions, Read the Games Again, Check ROMs, Favorites; ARCADE SCREEN: Screen, Start Filter\*, Menu Filters\*, List Style\*, Border\*, Info Background\*, Screen Adjustment\*, Hint Bar\*, Help Line\*, Attract Mode\*, Attract After\* (with Attract Mode on); PLAYING: Start Info Screen, Play Mode, Play at Startup; SCORES AND PICTURES: Save High Scores, Online High Scores, RetroAchievements Account, Game Pictures (Pictures From on its page); ONLINE AND LAN: Online Play, Relay Server (with Online Play on), LAN Play, LAN Sync (Smooth or Responsive, default Smooth; with LAN Play on), Online Name (with Online Play or LAN Play on); EMULATORS: MAME, FinalBurn Neo, Emulator Video (Game Picture, Screen Shape, Integer Scale, Vertical Games, MAME Video) |
| [Emby](https://github.com/mehmetraif/OSD-OS/wiki/Emby) | `com.osdos.emby` | Enabled, Sign In, Server Address, Libraries†, Video Quality†, Resume Playback†, Autoplay Next Episode†, Intro Skip†, Credit Skip†, Scaling, Sign Out† |
| [High Scores](https://github.com/mehmetraif/OSD-OS/wiki/High-Scores) | `com.osdos.highscores` | Enabled, the row named **High Scores** on the Arcade page under the **HIGH SCORES** heading |
| [Jellyfin](https://github.com/mehmetraif/OSD-OS/wiki/Jellyfin) | `com.osdos.jellyfin` | Enabled, Sign In, Server Address, Libraries†, Video Quality†, Resume Playback†, Autoplay Next Episode†, Intro Skip† and Credit Skip† (where the server has the Intro Skipper plugin), Scaling, Sign Out† |
| [Local Files](https://github.com/mehmetraif/OSD-OS/wiki/Local-Files) | `com.osdos.local_files` | Enabled, Media Folder, Loop Playback, Shuffle Playback, Resume Playback, Subtitles, Subtitles In, Image Duration, Hide File Extensions, Scaling |
| [Netflix](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video) | `com.osdos.netflix` | Enabled, Sign In, TMDB Key, Catalog Region, Catalog Language, Scaling, Sign Out† |
| [NFC Reader](https://github.com/mehmetraif/OSD-OS/wiki/NFC-Reader) | `com.osdos.nfc_reader` | Enabled, Cards Folder, Resume Playback, YouTube Resolution, Subtitles, Subtitles In, Scaling |
| [Playlists](https://github.com/mehmetraif/OSD-OS/wiki/Playlists) | `com.osdos.playlists` | Enabled, Download Folder, Subtitles, Loop Playback, Scaling |
| [Plex](https://github.com/mehmetraif/OSD-OS/wiki/Plex) | `com.osdos.plex` | Enabled, Sign In, Current User†, Auto Sign In†, Server†, Libraries†, Video Quality†, Resume Playback†, Autoplay Next Episode†, Scaling, Sign Out† |
| [Prime Video](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video) | `com.osdos.prime_video` | Enabled, Sign In, TMDB Key, Catalog Region, Catalog Language, Scaling, Sign Out† |
| [Scripts](https://github.com/mehmetraif/OSD-OS/wiki/Scripts) | `com.osdos.scripts` | Enabled, Scripts Folder, Auto-Run On Startup, Rescan Scripts |
| [Weather](https://github.com/mehmetraif/OSD-OS/wiki/Weather) | `com.osdos.weather` | Enabled, Location, Other Locations, Screens, Music, Music List, Units, Time per Screen, Hours Format |
| [YouTube](https://github.com/mehmetraif/OSD-OS/wiki/YouTube) | `com.osdos.youtube` | Enabled, Sign In, Subscriptions, Playlists, Subtitles, Subtitles In, Audio Language, Resume Playback, Scaling, Show Shorts, Advanced (Max Resolution, Video Codec, Max Frame Rate, Playback Speed), Delete Watch Later, Delete Recently Watched, Sign Out† |

Each module's wiki page explains its rows. A module's own **Scaling** is **Default** (follow Settings → TV / Movie → Scaling) or one of the four, for that module only.

### The kinds of row a module can have

| Kind (`type` in the manifest) | Keys | Saved in `modules.<id>` as |
|---|---|---|
| `toggle` | ◄ ► or select flips it | `true` or `false` |
| `list_single` | ◄ ► step through the choices | The choice's text, or its id for a list the module supplies |
| `multiselect_submenu` | Shows how many are on (ALL, NONE, 3 OF 4); select opens a page of on/off lines, where ◄ ► or select flips one | `{ "<id>": true, "<id>": false }` |
| `submenu` | Select opens a page of its own rows | Its rows' keys, flat under the module |
| `directory_browser` | Select opens the folder picker: **Default Folder**, the places, and **Use This Folder** in every folder | The folder's path; `""` for the module's own |
| `slider` | ◄ ► move it a step | A number from 0 to 100 |
| `action` | Select runs it; one that deletes or signs out asks first, Cancel under the cursor, then its verb (Sign Out, Delete) | Nothing |
| `module_view` | Select opens one of the module's own screens (Sign in) | Nothing |

The manifest's fields are in [Writing a module](https://github.com/mehmetraif/OSD-OS/wiki/Writing-a-Module#the-manifest). A row is dimmed, its help line saying why, until:

| In the manifest | Dimmed until | The help line starts |
|---|---|---|
| `requires_auth` | The module is signed in, with its SIGN IN row | `Sign in first (SIGN IN above)` |
| `requires_capability` | The server offers it (Jellyfin's Intro Skip and Credit Skip) | `Not offered by the server` |
| `shown_with` | Another row has a given value (Arcade's Relay Server) | `Only with Online Play on` |
| `shown_with` on an app setting | An app setting has a given value (Ambient:Mode's Auto-Launch Playback, Scripts' Auto-Run On Startup: Settings → Startup → Start On naming the module) | Its own reason: `Only with Start On on Ambient:Mode (Settings > Startup)` |

## Quit

The first screen's last row.

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/quit.png" width="100%" alt="Quit" />

Select asks **Really quit?**, the cursor on the answer that stays, so select twice never ends the session. Run by hand, or on a Mac, the answers are **No** and **Yes**. Started by the autostart service (`OSDOS_AUTOSTART` is set, as on the OSD/OS image) the row is a power menu: it is called **Power**, has the power symbol in Camcorder's column (Startup has the play symbol), and asks **Power off?**:

| Answer | What it does |
|---|---|
| Cancel | Nothing |
| Power Off | Powers the Pi off |
| Restart | Reboots. Offered with `OSDOS_LAUNCHER_API` 2 or later |
| Exit to Terminal | Starts a login shell on `tty1`; `sudo systemctl start osdos` brings OSD/OS back |

- On the OSD/OS image both questions also offer **Save Logs**, which copies the logs to the card's OSD-OS partition ([Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting#where-the-log-is)).
- Ctrl+Q quits too; under the service that is Power Off.
- `sudo systemctl stop osdos` over SSH stops the app and leaves the Pi on.

## Settings without a row

Two older keys have no row, and are read only while their successors are unset. Set them in `config.json` by hand, with OSD/OS stopped ([Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files#editing-by-hand)):

| Key | Values | What it does |
|---|---|---|
| `app.auto_crop` | `On`, `Off` | Read only while `app.video_scaling` is unset: `On` means Pan & Scan |
| `app.theme` | A skin's folder name | Read as the Skin only while `app.skin` is unset |

## See also

- [Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files): `config.json` and the data folder
- [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv)
- [Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes), [Skins](https://github.com/mehmetraif/OSD-OS/wiki/Skins), [Effects](https://github.com/mehmetraif/OSD-OS/wiki/Effects) and [Menu music](https://github.com/mehmetraif/OSD-OS/wiki/Menu-Music)
- [Display Output](https://github.com/mehmetraif/OSD-OS/wiki/Display-Output) and [Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output)
- [Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls): keyboards, remotes, gamepads and `input.cfg`
- [Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules): what each module does
- [Using OSD/OS](https://github.com/mehmetraif/OSD-OS/wiki/Using-OSD-OS)
