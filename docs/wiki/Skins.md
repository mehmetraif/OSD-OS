# Skins

A skin gives the shapes of the window's frame, the title bar, the hint bar and the selected line, and can replace OSD/OS's icons. It gives only shapes: they are drawn in the color scheme's two colours, so every skin goes with every scheme.

## What a skin dresses

<table>
<tr><th width="50%">Skin: DOS, Window Frame: Shadow</th><th width="50%">Skin: Rounded</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/skin-dos.png" width="100%" alt="Skin: DOS, Window Frame: Shadow" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/skin-rounded.png" width="100%" alt="Skin: Rounded" /></td></tr>
</table>

<table>
<tr><th width="100%">The skin template, in Video 1</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/skin-template.png" width="100%" alt="The skin template: clipped corners with brackets, a striped title bar end, a dotted hint bar, a selected line with a dark foot, and a television for the logo" /></td></tr>
</table>

| Part | Key | Where it shows | Without the skin's picture |
|---|---|---|---|
| The window's frame | `window` | With Settings → Look → OSD Background: Window, and Window Frame On or Shadow. Drawn over the whole window | A line an art pixel wide round the window |
| The title bar | `titleBar` | At the top of every screen, behind the title | A solid bar |
| The hint bar | `hintBar` | At the foot of every screen and dialog, unless Settings → Look → Hint Bar is Off | A solid bar |
| The selected line | `selection` | Every selected line: the menus, the tree's cursor, a question's answers | A solid bar |
| Icons | `icons` | The logo at the left end of the title bar | OSD/OS's own icons |

The on-screen keyboard's keys stay plain bars. The selector effect plays round the selected line whatever its shape ([Effects](https://github.com/mehmetraif/OSD-OS/wiki/Effects#selector-effects)).

## Choosing a skin

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Skin | THEME (with a theme), None, DOS, Rounded, and the skins in the data folder's `skins` | THEME; without a theme, None | The shapes of the window's parts and its icons. None is OSD/OS's own drawing | `app.skin` |

- The row is on Settings → Look. It saves the skin's folder name, `"Off"` for None, `""` for THEME.
- Choosing a theme sets it back to THEME ([Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes#how-a-theme-is-applied)).
- `app.theme`, an earlier key for the skin, is read while `app.skin` is unset.

## The two skins that come with OSD/OS

They are in [assets/skins](https://github.com/mehmetraif/OSD-OS/tree/main/assets/skins). Their pictures are drawn here as text: `#` light (the scheme's colour), `o` dark (its background), `.` clear.

### DOS

A double line round the window. The bars and the selected line stay OSD/OS's own.

```json
{
    "name": "DOS",
    "window": { "image": "window.png", "border": 4 }
}
```

`window.png`, 9 × 9:

```text
ooooooooo
o#######o
o#ooooo#o
o#o###o#o
o#o#o#o#o
o#o###o#o
o#ooooo#o
o#######o
ooooooooo
```

With `"border": 4`, each corner is its 4 × 4 square. The middle row and column stretch along the window's sides, and the dark middle pixel fills the window.

### Rounded

Round corners on the window, and round ends on the title bar, the hint bar and the selected line, which share one picture.

```json
{
    "name": "Rounded",
    "window": { "image": "window.png", "border": 4 },
    "titleBar": { "image": "bar.png", "border": 3 },
    "hintBar": { "image": "bar.png", "border": 3 },
    "selection": { "image": "bar.png", "border": 3 }
}
```

`window.png`, 9 × 9, and `bar.png`, 7 × 7:

```text
..#####..        ..###..
.#ooooo#.        .#####.
#ooooooo#        #######
#ooooooo#        #######
#ooooooo#        #######
#ooooooo#        .#####.
#ooooooo#        ..###..
.#ooooo#.
..#####..
```

## skin.json reference

The [skin template](https://github.com/mehmetraif/OSD-OS/tree/main/docs/skin-template)'s file uses every option:

```json
{
    "name": "Template",
    "window":    { "image": "window.png", "border": 4 },
    "titleBar":  { "image": "titlebar.png", "border": [2, 2, 5, 2] },
    "hintBar":   { "image": "hintbar.png", "border": 3, "tile": "repeat" },
    "selection": { "image": "selection.png", "border": 3 },
    "icons":     { "logo": "logo.png" }
}
```

| Key | What it is | Left out |
|---|---|---|
| `name` | What Settings → Look → Skin shows, 28 characters at most (runs of spaces count as one) | The folder's name |
| `window`, `titleBar`, `hintBar`, `selection` | A part's picture ([above](#what-a-skin-dresses)) | The part as OSD/OS draws it |
| `icons` | Icons of its own, by name ([below](#icons)) | OSD/OS's icons |

Every key is optional. Unknown keys are ignored.

### A part

A part is a picture's file name, stretched whole over the part:

```json
{ "hintBar": "hintbar.png" }
```

or an object:

| Key | Takes | Left out |
|---|---|---|
| `image` | The picture: a PNG, GIF or BMP in the skin's folder, or in a folder inside it | The part as OSD/OS draws it |
| `border` | How many of the picture's pixels at each edge are its frame: one whole number for all four sides, or four, `[left, top, right, bottom]`, from 0 to 512 | 0: the picture stretched whole |
| `tile` | How the edges and the middle fill the part: `"stretch"`, `"repeat"` (at their own size) or `"round"` (scaled so that a whole number fit) | `"stretch"` |

- `border` counts whole pixels: `2.5` counts as 0, with no line in the log. Numbers over 512 count as 512.
- A `border` that is neither a number nor a list of four numbers (`"wide"`, `[3, 3]`) is logged, and the picture is stretched whole.
- A `tile` other than the three is taken as `"stretch"`, with no line in the log.
- A picture outside the skin's folder (a path or a link out of it) is refused and logged.

### The icons key

An object of names and files: `{ "logo": "tv.png", "youtube": "yt.svg" }`.

- Each file is a PNG, SVG, GIF, BMP or JPEG in the skin's folder. The names are listed [below](#icons).
- A name is letters, digits, `-` and `_`, up to 40 of them, read in lower case. One with anything else (`"My Logo"`) is logged and left out.

## How a picture is drawn

### Two colours

Every pixel of a picture is one of three kinds:

| A pixel that is | Is drawn | Draw it in |
|---|---|---|
| less than half opaque | not at all: what is behind shows through | transparent |
| at least half opaque, and at least half bright | in the scheme's colour (light) | white |
| at least half opaque, and darker | in the scheme's background (dark) | black |

- Brightness is grey = (11 × red + 16 × green + 5 × blue) / 32, light from 128 up. Pure red (87) and pure green (127) count as dark, yellow (215) as light.
- There are no half tones: a soft edge becomes hard.
- The picture is drawn again when the scheme changes, and with Settings → Look → OSD Background: Off, where the menus are in the scheme's lighter colour on black.

### Art pixels

Each pixel of a picture is an **art pixel**, a pixel of a 240-line picture. It is drawn without smoothing as a square of screen pixels: the screen's height ÷ 240, rounded down.

| Screen lines | Screen pixels per art pixel |
|---|---|
| 480, 576 | 2 × 2 |
| 720 | 3 × 3 |
| 1080 | 4 × 4 |

At 480 lines the title bar is 14 art pixels tall, a selected line 12 (the tree) to 14 (Settings), the hint bar 9 to 11. A `border` of 2 to 4 at the top and the bottom leaves room for the text.

### Nine slices

`border` cuts a picture into nine. The four corners are drawn as they are, the four edges stretch or repeat between them, and the middle fills the rest. The template's 9 × 9 window with `"border": 4`, cut apart:

```text
..##  #  ##..     the top corners, and between them the top edge,
.#oo  o  oo#.     one column wide, stretched along the window
#o##  o  ##o#
#o#o  o  o#o#

#ooo  o  ooo#     the left and right edges, one row tall, and the middle

#o#o  o  o#o#
#o##  o  ##o#     the bottom corners and edge
.#oo  o  oo#.
..##  #  ##..
```

- **`"stretch"`** stretches an edge or a middle as a whole, a pattern in it too. **`"repeat"`** keeps the pattern at its size and cuts the last one short. **`"round"`** scales it a little so that whole ones fit.
- **A `border` of 0 on two sides**, as `[3, 0, 3, 0]`, makes the left and right parts edges from top to bottom: they keep their width and stretch with the part's height.

### What goes where

- **The window's frame** is drawn over the whole window. Draw its middle dark for the scheme's background. A clear middle shows what is behind the window: black, or a video with Transparent Background.
- **The background effect** starts the skin's `border` in from the window's edge ([Effects](https://github.com/mehmetraif/OSD-OS/wiki/Effects#background-effects)).
- **The bars and the selected line** have their text written over them in the dark colour. Keep their middles light.

## Icons

A skin's icon replaces OSD/OS's in the title bar, by name:

| Name | Where it shows |
|---|---|
| `logo` | The main menu, About, System Information |
| `settings` | Settings, a module's settings, the file browser that picks a folder or a file |
| `keyboard` | Controls |
| `wifi` | Wi-Fi |
| `bluetooth` | Bluetooth |
| `update` | Update |
| `lock` | Cabinet Lock |
| `question` | A question: resume, quit, install an update |
| `notice` | A notice: an error, a code to type |
| `ambient_mode`, `arcade`, `emby`, `highscores`, `jellyfin`, `local_files`, `netflix`, `nfc_reader`, `playlists`, `plex`, `prime_video`, `scripts`, `weather`, `youtube` | Each module's screens, by its folder's name in [modules](https://github.com/mehmetraif/OSD-OS/tree/main/modules) |

A module's icon goes by its folder's name, any other by its file's name without the extension (`assets/images/settings.svg` is `settings`).

- **Only its shape counts.** The icon is drawn in the title bar's colour. Half-transparent pixels stay half transparent.
- **It is trimmed** of its clear margin and scaled to the logo's height: 17 art pixels (34 screen pixels) at 480 lines. A wide icon takes room from the title bar.
- **A picture of pixels** is scaled without smoothing: one 17 pixels tall is exactly doubled at 480 lines and tripled at 720. At 576 and 1080 lines the factor isn't whole.
- **Only the title bar's logo** is a skin's to replace: About's lettering, the boot screen's cassette and the screen saver's logo stay OSD/OS's.

## Drawing the pictures

### In a pixel editor

Draw at one pixel per art pixel, in white, black and transparent, and save as PNG, GIF or BMP. Keep pictures small: a window of 9 × 9 to 13 × 13, bars 7 pixels tall.

### From text drawings: make-pictures.py

The template's [make-pictures.py](https://github.com/mehmetraif/OSD-OS/blob/main/docs/skin-template/make-pictures.py) writes PNGs from text drawings in it. It needs only Python 3. Every character is one art pixel:

| Character | Pixel |
|---|---|
| `#` | light: the scheme's colour |
| `o` | dark: its background |
| `.` | clear |

The drawings are a dictionary, `PICTURES`, of file names and rows of equal length ([example](#example-cassette)).

Run it in the skin's folder, or name the folder to write in:

```sh
python3 make-pictures.py
python3 make-pictures.py ~/.local/share/OSD-OS/skins/my-skin
```

It prints each picture and its size (`hintbar.png: 8x7`). It stops at a drawing with another character (`window.png: x is not one of # o .`) or rows of different lengths (`window.png: its rows are not all as long`).

## Make a skin from the template

1. **Get the template** from the repository (it isn't installed with OSD/OS):

   ```sh
   curl -L https://github.com/mehmetraif/OSD-OS/archive/refs/heads/main.tar.gz | tar -xz -C /tmp
   ```

2. **Copy it into the data folder's `skins`**, under a folder name of your own, which is what Settings saves. A folder named `dos` or `rounded` replaces OSD/OS's of that name.

   ```sh
   mkdir -p ~/.local/share/OSD-OS/skins
   cp -r /tmp/OSD-OS-main/docs/skin-template ~/.local/share/OSD-OS/skins/my-skin
   ```

   On a Mac the folder is `~/Library/Application\ Support/OSD-OS/skins`. On the OSD/OS image, do it as `pi`, over SSH or from Exit to Terminal ([The OSD/OS image](https://github.com/mehmetraif/OSD-OS/wiki/The-OSD-OS-Image)).

3. **Name it**: `"name": "My Skin"` in `skin.json`, 28 characters at most.

4. **Draw it**: change the drawings in `make-pictures.py` and run it, or edit the PNGs in a pixel editor.

5. **Choose it**: open Settings again and set **Skin** to My Skin. For the frame, set **OSD Background** to Window and **Window Frame** to On or Shadow.

6. **See a change**: choosing the skin again reads `skin.json` again. A picture changed under the same file name shows after a restart (Settings → Quit), or under a new file name.

7. **Check the log** for what couldn't be used ([below](#what-the-log-says)).

## Example: Cassette

Rounded corners with a screw in each, bars with clipped corners, a selected line pointed at both ends, and a cassette for the logo.

```json
{
    "name": "Cassette",
    "window":    { "image": "window.png", "border": 5 },
    "titleBar":  { "image": "bar.png", "border": 3 },
    "hintBar":   { "image": "bar.png", "border": 3 },
    "selection": { "image": "selection.png", "border": [3, 0, 3, 0] },
    "icons":     { "logo": "cassette.png" }
}
```

Its drawings, in place of `PICTURES` in a copy of the template's `make-pictures.py`. `bar.png` is a 7 × 7 light bar with clipped corners, and `cassette.png` a drawing 17 pixels tall, the logo's height at 480 lines.

```python
PICTURES = {
    # The window's frame, "border": 5: rounded corners, each with a screw, as
    # on a cassette's shell; one light line round a dark ground.
    "window.png": [
        "..#######..",
        ".#ooooooo#.",
        "#ooooooooo#",
        "#oo#ooo#oo#",
        "#ooooooooo#",
        "#ooooooooo#",
        "#ooooooooo#",
        "#oo#ooo#oo#",
        "#ooooooooo#",
        ".#ooooooo#.",
        "..#######..",
    ],
    # The selected line, "border": [3, 0, 3, 0]: pointed at both ends, like a
    # tag. Only its middle columns stretch along the line; its ends stretch
    # up and down with it.
    "selection.png": [
        "...###...",
        "..#####..",
        ".#######.",
        "#########",
        ".#######.",
        "..#####..",
        "...###...",
    ],
}
```

- **The window**: each screw is inside its 5 × 5 corner, so there is one in each corner of the window at any size.
- **The selected line**: the text starts three art pixels in, past the point.

## A skin inside a theme

A theme names a skin (`"skin": "cassette"`), or brings one of its own: a `skin` object with the keys of a `skin.json` except `name`, its pictures in the theme's folder. It is used while Settings → Look → Skin is on THEME ([Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes#skin)).

## Early skins: theme.json in themes

A `theme.json` in the data folder's `themes` is read as a skin, and listed under Skin, when it holds window pictures only. That is: at least one of `window`, `titleBar`, `hintBar` and `selection`, and none of `colors`, `skin`, `effects` and `music`.

- A skin of the same folder name in `skins` comes first.
- To make it a skin like any other, move the folder to `skins` and rename `theme.json` to `skin.json`.

## What the log says

For a skin in the folder `my-skin` (in a theme's own skin, the lines begin `theme <folder>:` instead):

| Line | What happened | What is drawn |
|---|---|---|
| `[AppCore] skin my-skin: /home/pi/.local/share/OSD-OS/skins/my-skin` | The skin was read, from there. This line comes last | The skin |
| `[AppCore] skin my-skin: not found, none used` | The saved skin, or the one a theme names, has no folder with a `skin.json` | OSD/OS's own drawing |
| `[AppCore] /home/pi/.local/share/OSD-OS/skins/my-skin/skin.json: object is missing after a comma` | The file isn't JSON. The reason follows the colon | Not listed in Settings |
| `[AppCore] skin my-skin: its window, "window.jpg", is not a png/gif/bmp file in its folder` | A part's picture is missing, of another type, or out of the folder | That part as OSD/OS draws it |
| `[AppCore] skin my-skin: its titleBar's border is not a number or four: drawn whole, stretched` | `border` is a string, or a list not of four numbers | The picture stretched whole |
| `[AppCore] skin my-skin: "My Logo" is not an icon's name` | An icon's name has a space or another character | That icon left out |
| `[AppCore] skin my-skin: its icon settings, "notes.txt", is not a png/svg/gif/bmp/jpg/jpeg file in its folder` | An icon's file is missing, of another type, or out of the folder | OSD/OS's icon |

Where to read the log: [Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes#reading-the-log).

## Troubleshooting

| What you see | Why | What to do |
|---|---|---|
| The skin isn't in Settings → Look → Skin | Settings was open when the folder was made. Or `skin.json` isn't JSON (logged), is misnamed, or isn't in the data folder's `skins` | Open Settings again. Fix the JSON |
| No frame round the window | OSD Background isn't Window, or Window Frame is Off. Or the skin has no `window`, or its picture couldn't be used (logged) | Settings → Look → OSD Background: Window, Window Frame: On or Shadow |
| A pattern along an edge is uneven | `"stretch"` stretches an edge as a whole | `"tile": "repeat"` or `"round"` |
| The whole picture is stretched, corners and all | `border` is missing, not a whole number, or not one number or four | Give a whole number, or four |
| An icon doesn't change | Its name isn't the one OSD/OS looks for (a module's is its folder's name: `local_files`). Or the file couldn't be used (logged) | Check the [names](#icons) |

## See also

- [Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes) and [Effects](https://github.com/mehmetraif/OSD-OS/wiki/Effects): the rest of the look
- [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings#look): Skin, OSD Background and Window Frame
- [Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files): the data folder and its `skins`
