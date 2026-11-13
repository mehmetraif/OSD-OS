# Scripts

The Scripts module lists the `.sh` files in a folder and runs the one you choose from the remote: an update, RetroArch, a TV-channel simulator, a backup. A script runs in a console, with its output shown, or takes over the whole screen until it exits. A small `.txt` beside each script sets its name and how it runs.

<table>
<tr><th width="50%">Scripts</th><th width="50%">A console script</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/scripts.png" width="100%" alt="Scripts: FieldStation42, RetroArch and Update yt-dlp, each with its mode" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/scripts-console.png" width="100%" alt="A console script: Update yt-dlp, its output and EXIT 0" /></td></tr>
</table>

## Setting it up

1. Settings → System → **Scripts** → **ENABLED**: On. Scripts appears on the main menu.
2. Put your scripts in the scripts folder: `user_scripts` in the data folder, which OSD/OS creates.

   | System | Folder |
   |---|---|
   | OSD/OS image, Raspberry Pi OS, SteamOS | `~/.local/share/OSD-OS/user_scripts/` |
   | macOS | `~/Library/Application Support/OSD-OS/user_scripts/` |

   Or pick another with Settings → System → Scripts → **Scripts Directory**: **USE THIS FOLDER** picks the one open, **Default Folder** goes back to `user_scripts`.
3. Open **Scripts**. Every script gets its `.txt` file the first time it is listed; edit it to name the script and choose its mode.

- Scripts are the files ending in `.sh` (in any case) directly in the folder, sorted by name.
- The folder is read each time the list opens. **Rescan Scripts** in Settings reads it at once.
- An executable script runs directly, so its `#!` line is honoured. One that isn't runs with `/bin/sh`, which is not bash: `chmod +x` a bash script.

## The list

Each row shows the script's name and its mode (CONSOLE or TAKEOVER), with a `*` before the mode for a favourite.

| Action | Keyboard | Gamepad | What it does |
|---|---|---|---|
| Move | ▲ ▼ | D-pad, left stick | Up and down the list (it wraps) |
| Run | Enter | A | Runs the script (after a yes/no question, if its `.txt` asks for one) |
| Favourite | ► | D-pad right | Puts the script on the main menu, or takes it off |
| Back | Esc | B | Back to the main menu |

With no scripts in the folder, the screen says **No scripts found: Please add .sh files to the scripts directory**.

## The .txt beside each script

Each script's settings are in a text file of the same name with `.txt` in place of `.sh`: `retroarch.sh` → `retroarch.txt`. The script itself is never read or changed.

- One `key = value` per line, split at the **first** `=`. Keys are not case-sensitive.
- `#` starts a comment, at the start of a line only. Blank lines are ignored.
- An unknown key is ignored. A key left out keeps its default.

| Key | Values | Default | What it does |
|---|---|---|---|
| `name` | Any text | The file name without `.sh`, with `_` and `-` made spaces | The name the list and the main menu show, in capitals. |
| `mode` | `console`, `takeover` | `console` | **console**: OSD/OS keeps the screen and shows the output. **takeover**: the script gets the whole screen until it exits. Anything else is read as `console`. See [Console and takeover](https://github.com/mehmetraif/OSD-OS/wiki/Scripts#console-and-takeover). |
| `favorite` | `yes`, `no` | `no` | `yes` also puts the script on the main menu. ► in the list rewrites this line. |
| `confirm` | `yes`, `no` | `no` | `yes` asks **Run script?** first, with Cancel selected. Asked from the main menu too, but not when the script runs at start. |
| `args` | Arguments | Nothing | Added after the script's path. Split on spaces; double quotes keep spaces together (`"/media/OSD-OS/Channel 3"`). Not a shell: `$VARIABLES`, `~`, `*` and `\|` are passed as they are. |
| `wait` | `pgroup`, `child` | `pgroup` | Takeover only. **pgroup**: OSD/OS takes the screen back once everything the script started has exited. **child**: as soon as the script itself exits. Anything else is read as `pgroup`. |
| `tty` | `yes`, `no` | `no` | Takeover on a Linux system without a desktop only. `yes` gives the script a real terminal, for a program that reads typed keys. Needs a udev rule: see [tty = yes](https://github.com/mehmetraif/OSD-OS/wiki/Scripts#tty--yes-and-its-udev-rule). |

- For `favorite`, `confirm` and `tty`: `yes`, `on`, `true` and `1` mean yes; `no`, `off`, `false` and `0` mean no.
- A script without a `.txt` gets one the first time it is listed, with the defaults and a comment per key. Delete the `.txt` to have it written again.
- An existing `.txt` is never written over. In a folder OSD/OS can't write to, the scripts run on their defaults.

## Console and takeover

### Console

OSD/OS stays on the screen and shows the script's output and errors as they come.

- The status line says **RUNNING…**, then **EXIT 0** (or another exit code), **STOPPED** or **FAILED TO START**. A script OSD/OS refused to start says **COULD NOT START:** and why.
- The output keeps the last 500 lines (64 KB at most). ▲ ▼ scroll.
- **Back while it runs stops it** and everything it started. The view stays, so you can read the end. Back again leaves.

### Takeover

The script gets the whole screen until it exits. For a program with a picture of its own: RetroArch, a game, a TV-channel simulator, a browser.

- **On a Linux system without a desktop** (the OSD/OS image, Raspberry Pi OS Lite), OSD/OS hands the display over. The program must draw straight to it (KMS/DRM), as mpv with `--vo=drm` or RetroArch do.
- **On a desktop, macOS or SteamOS**, the program's window covers OSD/OS's.
- **There is no stop key.** Quit the program its own way, and OSD/OS comes back.
- **Exit code 0** returns to where you started it. Any other ending stays on the takeover screen, with the exit code and the script's output.
- `wait` in the `.txt` sets when OSD/OS takes the screen back.
- If the display can't be handed over, the script runs without the screen: **DISPLAY COULD NOT BE HANDED OVER — RAN WITHOUT IT**. Back then stops the script.
- A process that hasn't started within five seconds counts as failed to start.
- Starting a takeover ends a video left playing behind the menus (Transparent Background).

### tty = yes, and its udev rule

A takeover script normally runs without a terminal. A program that takes its keys from a terminal, mpv among them, needs `tty = yes`.

- It works only on a Linux system without a desktop. The script's output then goes to the console, so nothing is shown if it fails.
- It needs a udev rule that OSD/OS doesn't install. It lets every member of the `tty` group read the virtual consoles:

```sh
echo 'KERNEL=="tty[1-9]*", GROUP="tty", MODE="0660"' | sudo tee /etc/udev/rules.d/99-osdos-vt.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=tty
```

Without the rule, a `tty = yes` script still runs, without a terminal, and the log says `tty=yes requested but /dev/ttyN is not openable … running without a controlling terminal`.

## Favourites on the main menu

A script with `favorite = yes` (or marked with ► in the list) gets its own row on the main menu, after the modules.

- Choosing it runs the script straight away. A `confirm = yes` script still asks first.
- The rows show only while the Scripts module is enabled.

## Running a script at start

1. Settings → System → Scripts → **Auto-Run On Startup**: the script (**None** by default).
2. Settings → Startup → **Start on Module**: Scripts.

- At start, OSD/OS opens Scripts and runs the script without asking, even with `confirm = yes`.
- If the script no longer exists, the Scripts list opens instead.
- A favourite chosen with **Play at Startup** ([Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings)) comes before Start on Module.
- The setting stores the script's file name, not its path.

## What a script receives

| | |
|---|---|
| **Working folder** | The script's own folder. |
| **Arguments** | The `.txt`'s `args`. |
| **`APP_ROOT`** | OSD/OS's own folder. |
| **`DATA_ROOT`** | The data folder (`~/.local/share/OSD-OS` on Linux and the image). |
| **`OSDOS_MODE`** | `console` or `takeover`: how it is really running (`console` for a takeover that couldn't be handed the screen). |
| **`OSDOS_VT`** | Only when the screen was handed over: the number of the virtual console OSD/OS switched to. |
| **`ALSA_CARD`** | Only when Settings → Sound → Audio Output names a sound card (or Auto has to pick one): that card. |
| **`SDL_KMSDRM_DEVICE_INDEX`** | Only on a Pi 5: the number of the DRM card of the display output in use. One already in the environment is kept. |
| **`OSDOS_PLAYER1_NAME`, `OSDOS_PLAYER1_GUID`, `OSDOS_PLAYER1_DEVICE`** | Player 1's pad (Settings → Controls → [Players](https://github.com/mehmetraif/OSD-OS/wiki/Controls#players)): its name, SDL's GUID and its device node (`/dev/input/event7`). `OSDOS_PLAYER2_…` to `OSDOS_PLAYER6_…` the same. None for a player without a pad. |
| **`MP240_MODE`, `MP240_VT`** | The same as `OSDOS_MODE` and `OSDOS_VT`, for scripts written for 240-MP. |
| **Everything else** | The environment OSD/OS was started with. On the image that includes `QT_QPA_PLATFORM=eglfs`. |

## Examples

Three scripts, each with its `.txt`. Put both files in the scripts folder.

### 1. Update yt-dlp (console)

Updates the yt-dlp the [YouTube](https://github.com/mehmetraif/OSD-OS/wiki/YouTube) module uses. A copy installed by root in `/usr/local/bin` can't replace itself as the user OSD/OS runs as.

`update-yt-dlp.sh`:

```sh
#!/bin/sh
# Updates the yt-dlp OSD/OS's YouTube module uses: the data folder's copy
# if there is one (where OSD/OS looks first), else the one on the PATH.
if [ -x "$DATA_ROOT/bin/yt-dlp" ]; then
    YTDLP="$DATA_ROOT/bin/yt-dlp"
else
    YTDLP=$(command -v yt-dlp) || { echo "yt-dlp is not installed"; exit 1; }
fi
echo "Updating $YTDLP"
exec "$YTDLP" --update
```

`update-yt-dlp.txt`:

```ini
name = Update yt-dlp
mode = console
favorite = yes
confirm = no
args =
wait = pgroup
tty = no
```

### 2. RetroArch, or one game (takeover)

[RetroArch](https://www.retroarch.com) with the whole screen. On Raspberry Pi OS and the OSD/OS image, install it and a core:

```sh
sudo apt install retroarch libretro-nestopia
```

Quit it from its own menu (Main Menu → Quit RetroArch) to come back to OSD/OS.

`retroarch.sh`:

```sh
#!/bin/sh
# RetroArch with the whole screen, passing on any arguments from the .txt.
exec retroarch "$@"
```

`retroarch.txt`:

```ini
name = RetroArch
mode = takeover
favorite = yes
confirm = no
args =
wait = pgroup
tty = no
```

For one game, straight in, copy the script as `alter-ego.sh` and give the core (`-L`) and the game in `alter-ego.txt`. Debian's cores are in `/usr/lib/aarch64-linux-gnu/libretro/` on a 64-bit Pi.

```ini
name = Alter Ego
mode = takeover
favorite = yes
args = -L /usr/lib/aarch64-linux-gnu/libretro/nestopia_libretro.so "/media/OSD-OS/Games/NES/Alter Ego.nes"
```

### 3. A channel of your own (takeover, tty)

Every video in a folder, shuffled, round and round, played by the mpv OSD/OS already has. It needs `tty = yes` and [the udev rule](https://github.com/mehmetraif/OSD-OS/wiki/Scripts#tty--yes-and-its-udev-rule): `q` ends the channel, space pauses, ◄ ► seek.

`channel-3.sh`:

```sh
#!/bin/sh
# Channel 3: every video in a folder (the first argument), shuffled once,
# then round and round. q ends it (with tty = yes in channel-3.txt).
CHANNEL=${1:-/media/OSD-OS/Channel 3}
LIST=$(mktemp) || exit 1
trap 'rm -f "$LIST"' EXIT

find "$CHANNEL" -type f \( -iname '*.mp4' -o -iname '*.mkv' -o -iname '*.avi' \
    -o -iname '*.mov' -o -iname '*.m4v' -o -iname '*.mpg' -o -iname '*.mpeg' \) | shuf > "$LIST"
if [ ! -s "$LIST" ]; then
    echo "No videos in $CHANNEL"
    exit 1
fi

if [ -n "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ]; then
    # A desktop: a full-screen window over OSD/OS.
    mpv --fs --loop-playlist=inf --really-quiet --playlist="$LIST"
else
    # No desktop (the OSD/OS image): straight to the screen OSD/OS handed over.
    mpv --vo=drm --hwdec=auto-safe --loop-playlist=inf --really-quiet --playlist="$LIST"
fi
```

`channel-3.txt`:

```ini
name = Channel 3
mode = takeover
favorite = yes
confirm = no
args = "/media/OSD-OS/Channel 3"
wait = pgroup
tty = yes
```

- On a Pi 4, `--hwdec=v4l2m2m-copy` in place of `--hwdec=auto-safe` decodes H.264 in hardware.
- Without the udev rule mpv gets no keys. End it over SSH with `pkill -x mpv`.
- FieldStation42, or any program with a launcher of its own, is listed the same way: a short takeover script that changes to its folder and starts it.

## Settings

Settings → System → **Scripts**. The keys sit under `modules.com.osdos.scripts`.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| ENABLED | On, Off | Off | Shows Scripts, and the favourites' rows, on the main menu. | `enabled` |
| Scripts Directory | A folder, or Default Folder | Default (`user_scripts` in the data folder) | The folder holding your `.sh` files. | `scripts_directory` (`""` for the default) |
| Auto-Run On Startup | None, or one of your scripts | None | Runs that script when OSD/OS starts on this module. | `startup_script` (the script's file name, or `None`) |
| Rescan Scripts | Select | | Reads the scripts folder again and writes any missing `.txt` files. | |

## Files and config keys

| File | What it is |
|---|---|
| `user_scripts/*.sh` (or the Scripts Directory) | Your scripts |
| `user_scripts/*.txt` | Each script's settings |

```json
{
    "app": {
        "startup_module": "com.osdos.scripts"
    },
    "modules": {
        "com.osdos.scripts": {
            "enabled": true,
            "scripts_directory": "",
            "startup_script": "channel-3.sh"
        }
    }
}
```

## Troubleshooting

| Message or symptom | What to do |
|---|---|
| **No scripts found** | The files must end in `.sh` and sit directly in the scripts folder (check Scripts Directory), readable by the user OSD/OS runs as. |
| **COULD NOT START: …** | The reason follows: **File no longer exists**, **Not a regular file**, **File is not readable**, **Not executable and /bin/sh is missing**, **Another script is still running** (a takeover's processes are still exiting), or **The screen is in use**. |
| **FAILED TO START** | A `#!` line names a program that isn't installed, or the script has Windows line ends. `sed -i 's/\r$//' script.sh` removes them. |
| A bash script fails with `/bin/sh` errors (`[[: not found`, `Syntax error`) | It isn't executable. `chmod +x` it, with `#!/bin/bash` as its first line. |
| A takeover leaves a black screen, or OSD/OS doesn't come back | Something the script started is still running: quit it, or over SSH find it with `ps -ef` and end it. `wait = child` returns as soon as the script exits. |
| A takeover program shows nothing on a Pi without a desktop | It must draw to the display itself (KMS/DRM). A program that needs a desktop can run inside a kiosk compositor such as `cage`. |
| Typed keys don't reach a takeover program | Set `tty = yes` and add [the udev rule](https://github.com/mehmetraif/OSD-OS/wiki/Scripts#tty--yes-and-its-udev-rule). |
| **DISPLAY COULD NOT BE HANDED OVER — RAN WITHOUT IT** | Try again from the list. |

The log on the image: `sudo journalctl -u osdos -f`. More in [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting).

## See also

[YouTube](https://github.com/mehmetraif/OSD-OS/wiki/YouTube), [Netflix and Prime Video](https://github.com/mehmetraif/OSD-OS/wiki/Netflix-and-Prime-Video), [Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade), [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings), [How it works](https://github.com/mehmetraif/OSD-OS/wiki/How-It-Works), [The OSD/OS image](https://github.com/mehmetraif/OSD-OS/wiki/The-OSD-OS-Image)
