# NFC Reader

Tap a card to play. Each NFC card stands for something to play: a file, a stream, a YouTube video or playlist, or a Plex film, show, collection or playlist. Open the NFC Reader module and touch a card to the reader.

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/nfc-reader.png" width="100%" alt="NFC Reader: the cassette, saying Reader not connected and Connect a PN532 USB or PC/SC reader" />

The screen is a cassette whose label shows the state: **Reader not connected**, **Tap a card to play**, **Playing ►** or **Card not matched**. Back leaves it.

## Readers

OSD/OS reads only a card's UID, its fixed serial number. Nothing is written to the card: what it plays is kept in a text file on the player. Any cheap NFC card, sticker or key fob works.

| Reader | Needs |
|---|---|
| **PN532 USB** (recommended) | No driver and no daemon: only permission to open the serial device. Works on SteamOS too. |
| **ACS ACR122U** and other PC/SC contactless readers | `pcscd` running on Linux, and a build of OSD/OS with PC/SC support (the release builds have it). Not on immutable systems like SteamOS. |

A PN532 module is recognised by its USB-serial bridge chip:

| Chip | USB id (vendor:product) |
|---|---|
| QinHeng CH340 (the common PN532 USB module) | `1a86:7523` |
| QinHeng CH341 | `1a86:5523` |
| QinHeng CH9102 | `1a86:55d4` |
| Silicon Labs CP210x | `10c4:ea60` |
| FTDI FT232 | `0403:6001` |
| FTDI FT231X | `0403:6015` |
| Prolific PL2303 | `067b:2303` |

- Linux: the `ttyUSB*` and `ttyACM*` devices with one of these ids are tried.
- macOS: `/dev/cu.usbserial*`, `/dev/cu.wchusbserial*`, `/dev/cu.SLAB_USBtoUART*` and, last, `/dev/cu.usbmodem*`.
- Detection is automatic: there is no reader setting.
- Other systems say **Not supported on this platform**.

## Setting up the reader

### Linux

Run the setup script once, as the user OSD/OS runs as (it uses `sudo` itself). From a checkout:

```sh
bash scripts/setup-nfc-reader.sh
```

or without one:

```sh
bash <(curl -fsSL https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/scripts/setup-nfc-reader.sh)
```

[The script](https://github.com/mehmetraif/OSD-OS/blob/main/scripts/setup-nfc-reader.sh) does two things:

- **PN532 USB:** writes `/etc/udev/rules.d/99-osdos-nfc.rules` and adds the user to the serial group (`dialout` on Debian and Raspberry Pi OS, `uucp` on Arch and Fedora). No packages.
- **PC/SC** (skipped on immutable systems): installs `pcscd`, `pcsc-tools` and `libpcsclite1`, blacklists the kernel's `pn533` and `pn533_usb` modules, and enables `pcscd`.

If it added you to a group, log out and back in (or reboot). Options:

```sh
bash scripts/setup-nfc-reader.sh pi       # authorise another user than the one running it
SKIP_PCSC=1 bash scripts/setup-nfc-reader.sh   # PN532 only
SKIP_PN532=1 bash scripts/setup-nfc-reader.sh  # PC/SC only
```

### The OSD/OS image

The image doesn't run the script and has no `pcscd`. A PN532 appears as `/dev/ttyUSB0` (or `/dev/ttyACM0`), which the `dialout` group may open.

Run the script over SSH for a PC/SC reader, or if a PN532 still reads **Reader not connected**. Logged in as another user, name `pi`: `bash <(curl -fsSL https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/scripts/setup-nfc-reader.sh) pi`.

### macOS

Nothing to set up.

### Turning the module on

Settings → TV / Movie → **NFC Reader** → **Enabled**: On. NFC Reader then appears on the main menu.

## How cards map to videos

Each card has a text file in the tags folder: `nfc_tags` in the data folder (`~/.local/share/OSD-OS/nfc_tags/` on Linux and the image, `~/Library/Application Support/OSD-OS/nfc_tags/` on a Mac), or the folder **Tags Directory** names.

| Part of the file | What it is |
|---|---|
| Name, without `.txt` | The title the screen shows (**Playing ►** `Terminator 2 (1991)`) |
| First non-empty line | The card's **UID**, in any form: `04:A2:3B:1C:5D:80:01`, `04a23b1c5d8001` and `04 A2 3B 1C 5D 80 01` are the same card |
| Second line | **What the card plays** (below) |
| Third line (optional) | A **mode**: `shuffle`, for Plex show, season, collection and playlist cards |

- Only files ending in `.txt` count. Blank lines are ignored.
- If two files hold the same UID, the one that sorts first alphabetically wins.
- A file you add or fix works at the next tap.

### What a card can play

| Second line | Plays |
|---|---|
| An absolute path: `/media/OSD-OS/Movies/Sci-Fi/Terminator 2 (1991).mp4` | That file. On the image, `/media/OSD-OS` is the film partition and USB drives are under `/media/usb/<label>`. |
| A relative path: `films/Fireplace.mp4` | That file in OSD/OS's own folder, otherwise in the data folder. |
| A playlist file: `/media/OSD-OS/Movies/Rambo Marathon.m3u` | The list. Resume remembers which video it was on. |
| A stream URL: `http://…` or `https://…` | That stream. |
| A YouTube URL: `https://www.youtube.com/watch?v=Eg8tK1LpLS8` | The video, through yt-dlp, at YouTube Video Resolution. A playlist URL (`https://www.youtube.com/playlist?list=…`) plays the playlist. |
| A Plex guid: `plex://movie/…`, `plex://show/…`, `plex://season/…`, `plex://episode/…` | Handed to the Plex module. See [Card hand-off](https://github.com/mehmetraif/OSD-OS/wiki/NFC-Reader#card-hand-off-to-another-module). |
| A Plex set: `plex://collection/<ratingKey>`, `plex://playlist/<ratingKey>` | Handed to Plex, which plays the collection or playlist as a queue. |

YouTube URLs are played by the NFC module itself, without the YouTube module's Advanced settings or sign-in.

### Examples

A film on the film partition, `Terminator 2 (1991).txt`:

```text
04:A2:3B:1C:5D:80:01
/media/OSD-OS/Movies/Sci-Fi/Terminator 2 (1991).mp4
```

A Plex show played as a shuffle of its episodes, `Cowboy Bebop (1998).txt`:

```text
04:D3:5A:72:19:4B:80
plex://show/<the show's guid>
shuffle
```

The UIDs here are made up; the screen shows your card's when you tap it. Plex cards are best written by the [card writer](https://github.com/mehmetraif/OSD-OS/wiki/NFC-Reader#writing-a-card-from-plex). By hand, the `guid` and `ratingKey` are in Plex Web's **Get Info → View XML**.

## Unknown cards

Tap a card OSD/OS has no file for, and it writes a stub: the UID with `:` made `-` as the name (`04-A2-3B-1C-5D-80-01.txt`), holding just the UID. The screen says **Card not matched** and shows the UID.

Rename the file to the title you want, add the second line, and tap the card again. An existing file is never written over.

## Playing a card

1. Open **NFC Reader** from the main menu. It says **Tap a card to play** and the reader's name.
2. Touch a card to the reader. The label says **Playing ►** and the card's title, then the video starts.
3. When it ends, or you stop it, you are back at **Tap a card to play**.

- Cards play only while the NFC Reader screen is open.
- A card already resting on the reader does nothing until it is lifted and touched again.
- While a card's video plays, other cards are ignored.

### Resume

- A video stopped more than 5 seconds in is resumed. One watched to 95% starts over.
- A playlist (an `.m3u`/`.m3u8`, or a YouTube URL with `list=`) also remembers which video it was on: **Resume video 3 at 12:34**.
- **Resume Playback**: **Ask** (the default) offers to resume or start from the beginning, **Always** resumes, **Never** starts from the beginning.
- Positions are kept per mapped path in `nfc_reader_history.json`.

### Subtitles and YouTube quality

[Settings](https://github.com/mehmetraif/OSD-OS/wiki/NFC-Reader#settings) lists the subtitle, resolution and Scaling settings. A Plex card plays with the Plex module's own settings.

A YouTube card prefers H.264 at the chosen resolution. For 480p, yt-dlp gets:

```text
bestvideo[height<=?480][vcodec^=avc1]+bestaudio/bestvideo[height<=?480]+bestaudio/best[height<=?480]/best
```

## Writing a card from Plex

With the NFC Reader module enabled and a reader connected, Plex offers **WRITE NFC CARD** on a film's, episode's, show's and season's page. A collection's or playlist's list has **@ WRITE NFC CARD** at its top.

1. Select it. The screen says **TAP A CARD FOR…** and the title.
2. Touch a card to the reader.
3. Choose how it plays: **Write card**, or for a show, season, collection or playlist, in order or shuffled ([Plex](https://github.com/mehmetraif/OSD-OS/wiki/Plex#writing-an-nfc-card) lists the choices).
4. If the card already plays something: **THIS CARD PLAYS "…". CHANGE TO…** → **Change this card** or **Cancel**.
5. **CARD WRITTEN FOR…**: select to finish.

It writes an ordinary tag file in the tags folder, which you can rename afterwards:

- Named after the title: `Dune (2021)`, `Cowboy Bebop (1998) - S1`, `Cowboy Bebop (1998) - S1E5`, `80s Action (Collection)`, `Road Trip (Playlist)`. A name already taken gets ` (2)`, ` (3)` and so on.
- Holding the item's Plex guid (`plex://movie/…`), which survives library rescans and moves between servers.
- A collection or playlist is held by its ratingKey (`plex://collection/<ratingKey>`), which exists on one server only.
- With `shuffle` on the third line for the shuffled choices.
- Replacing the file that held this card before. If the new file can't be written, the screen says **COULD NOT WRITE CARD** and the card keeps what it played.

## Card hand-off to another module

The second line's scheme decides which module plays the card:

| Scheme | Goes to |
|---|---|
| `plex://…`, and legacy agent guids `com.plexapp.agents.…://…` | Plex (`com.osdos.plex`) |
| `http://`, `https://`, a path | Played by the NFC module itself |

- A card plays as whoever is already signed in. It never signs in, switches server, switches profile or asks for a PIN.
- A shuffle card reports no progress: watched state and Continue Watching stay as they were.
- Plex's errors show on the NFC Reader's screen; [Plex](https://github.com/mehmetraif/OSD-OS/wiki/Plex#writing-an-nfc-card) lists them.
- If the receiving module is turned off, the card reads **Card not matched**.

To take cards in a module of your own, see [Writing a module](https://github.com/mehmetraif/OSD-OS/wiki/Writing-a-Module) and [ARCHITECTURE.md](https://github.com/mehmetraif/OSD-OS/blob/main/ARCHITECTURE.md#card-hand-off-nfc--a-module).

## Polling

There is no polling setting. The reader is polled whenever the module is **Enabled**, so that Plex's card writer works. Taps play only while the NFC Reader screen is open.

## Settings

Settings → TV / Movie → **NFC Reader**. The keys sit under `modules.com.osdos.nfc_reader`.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Enabled | On, Off | Off | Shows NFC Reader on the main menu and polls the reader. | `enabled` |
| Tags Directory | A folder, or Default Folder | Default | Where the tag files are. Default is `nfc_tags` in the data folder. | `tags_directory` (`""` for the default) |
| Resume Playback | Ask, Always, Never | Ask | See [Resume](https://github.com/mehmetraif/OSD-OS/wiki/NFC-Reader#resume). | `resume_playback` (`ask`, `yes`, `no`) |
| YouTube Video Resolution | 480p, 720p, 1080p | 480p | The most a YouTube card plays at. | `playback_resolution` |
| Auto Show Subtitles | Forced Only, On, Off | Forced Only | **Forced Only**: subtitles only for foreign dialogue. **On**: always. **Off**: never. | `auto_subtitles` (`forced`, `on`, `off`) |
| Subtitle Language | Any, or a language | Any | The subtitle language to prefer. | `sub_lang` (`-` for Any, else an ISO 639-1 code: `en`, `tr` …) |
| Scaling | Default, Letterbox, 14:9, Pan & Scan, Anamorphic | Default | How a 16:9 picture fills a 4:3 screen in this module. Default follows Settings → TV / Movie → Scaling. | `video_scaling` |

## Files and config keys

| File | What it is |
|---|---|
| `nfc_tags/*.txt` (or the Tags Directory) | One tag file per card |
| `nfc_reader_history.json` | Resume points: `{ "<mapped path>": { "pos": <ms>, "plPos": <playlist index or -1> } }` |

```json
{
    "modules": {
        "com.osdos.nfc_reader": {
            "enabled": true,
            "tags_directory": "",
            "resume_playback": "ask",
            "playback_resolution": "480p",
            "auto_subtitles": "forced",
            "sub_lang": "en",
            "video_scaling": "Default"
        }
    }
}
```

Two environment variables help when a reader isn't found:

| Variable | What it does |
|---|---|
| `OSDOS_NFC_DEBUG=1` | Logs every step of reader detection. |
| `OSDOS_NFC_SERIAL_DEVICE=/dev/ttyUSB0` | Tries only this serial device as a PN532: for a bridge chip outside the list above, or a machine with several. |

On the image (or an install with the autostart service), set them with `sudo systemctl edit osdos`, then `sudo systemctl restart osdos`:

```ini
[Service]
Environment=OSDOS_NFC_DEBUG=1
```

Read the log with `sudo journalctl -u osdos -f`.

## Troubleshooting

| Problem | What to do |
|---|---|
| **Reader not connected**, with a PN532 plugged in | Run the commands below. The device should belong to `dialout` (or `uucp`) with `rw` for the group, and `id` should list that group. If not, run the [setup script](https://github.com/mehmetraif/OSD-OS/wiki/NFC-Reader#linux) and log in again. Otherwise set `OSDOS_NFC_DEBUG=1`: an id not on the list needs `OSDOS_NFC_SERIAL_DEVICE`. |
| **Reader not connected**, with a PC/SC reader plugged in | `systemctl status pcscd` should say it is running and `pcsc_scan` should list the reader. If not, run the setup script, then unplug and replug the reader. |
| **Connect a PN532 USB reader** (without "or PC/SC") | This build has no PC/SC support: use a PN532. |
| **Card not matched** | No file holds this UID, or the file has no second line. Compare the UID on screen with the files (`grep -ril "04:A2:3B" ~/.local/share/OSD-OS/nfc_tags`). Check Tags Directory. For a Plex card, check the Plex module is on. |
| **Playback failed: Check the mapped path or URL (YouTube links require yt-dlp)** | The file isn't there (paths are case-sensitive on Linux), or a YouTube URL failed: see [YouTube](https://github.com/mehmetraif/OSD-OS/wiki/YouTube#troubleshooting). Select retries. With mpv missing it says **mpv is not installed**. |
| A Plex card shows an error | See [Plex](https://github.com/mehmetraif/OSD-OS/wiki/Plex#writing-an-nfc-card). |

```sh
ls -l /dev/ttyUSB* /dev/ttyACM*
id
```

More in [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting).

## See also

[Plex](https://github.com/mehmetraif/OSD-OS/wiki/Plex), [YouTube](https://github.com/mehmetraif/OSD-OS/wiki/YouTube), [Local Files](https://github.com/mehmetraif/OSD-OS/wiki/Local-Files), [Writing a module](https://github.com/mehmetraif/OSD-OS/wiki/Writing-a-Module), [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv), [Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files)
