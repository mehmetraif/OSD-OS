# Ambient:Mode

A backdrop for the room: a video of your choice plays on a loop, with music of your own instead of its sound if you like.

<table>
<tr><th width="50%">Ambient:Mode</th><th width="50%">Picking the media folder</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/ambient-mode.png" width="100%" alt="Ambient:Mode: the Video and Audio rows and START" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/folder-picker.png" width="100%" alt="Picking a folder on the tree: USE THIS FOLDER" /></td></tr>
</table>

## Setting it up

1. Settings → TV / Movie → **Ambient:Mode** → **ENABLED**: On. Ambient:Mode appears on the main menu.
2. Put your clips, and any music, in its media folder: `ambient` in the data folder. Create it if it isn't there.

   | System | Folder |
   |---|---|
   | OSD/OS image, Raspberry Pi OS, SteamOS | `~/.local/share/OSD-OS/ambient/` |
   | macOS | `~/Library/Application Support/OSD-OS/ambient/` |

   Or pick another with Settings → TV / Movie → Ambient:Mode → **Media Directory**: USE THIS FOLDER picks the folder open, Default Folder the module's own. On the OSD/OS image, make an `Ambient` folder on the card's **OSD-OS** drive from a computer, then pick `/media/OSD-OS/Ambient`.
3. Open **Ambient:Mode** from the main menu.

Only files directly in the folder are listed, by name, and only these types (in any case):

| Row | File types |
|---|---|
| **Video** | `mp4`, `mkv`, `avi`, `mov`, `m4v`, `webm`, `wmv`, `flv`, `f4v`, `mpg`, `mpeg`, `vob` |
| **Audio** | `mp3`, `wav`, `flac`, `m4a`, `ogg`, `aac`, and the playlists `m3u` and `m3u8` |

Other audio mpv plays (Opus, for one) can go in an `m3u` ([below](https://github.com/mehmetraif/OSD-OS/wiki/Ambient-Mode#music-playlists)). The folder is read each time the module opens.

## Choosing what plays

| Row | Choices |
|---|---|
| **Video** | Each video in the folder, then **SHUFFLE** (when there is more than one): every video, shuffled |
| **Audio** | **VIDEO AUDIO** (the video's own sound), then each audio file and playlist, then **SHUFFLE** (when there is more than one) |

The rows wrap, so SHUFFLE is one ◄ from each row's first choice.

▲ ▼ move between the rows. ◄ ►, or a gamepad's shoulder buttons, change the video or the audio. Select on **START** plays, and back leaves.

With no video in the folder, the screen says **No items found: Please add items in the ambient:mode media directory**. Audio files alone don't count.

## How it plays

- **The video loops forever**, full screen. With SHUFFLE, all videos play in an order shuffled once, then repeated.
- **With music chosen**, the clip plays silent. The music plays from a second mpv, on Settings → Sound → Audio Output's card, and loops too.
- **With Audio on SHUFFLE**, a playlist counts as one entry: its tracks play in their own order when its turn comes.
- **The menu music** (Settings → Sound → Menu Music) is held off while it plays, and the screen saver stays away.
- **Scaling** is the module's own, or Settings → TV / Movie → Scaling while it says Default.

During playback:

| Action | Keyboard | Gamepad | What it does |
|---|---|---|---|
| Its menu | ▲ or ▼ | D-pad up, down | A menu at the foot: **CROP** steps through the four Scalings (not offered on a Pi 3 with 1080p Playback on), **STOP** ends playback. It hides after five seconds |
| Pause | Enter or Space | A or Start | Pauses the clip. The music goes on |
| Stop | Esc | B | Ends playback and the music, and returns to the Ambient:Mode screen |

With **Transparent Background** (Settings → TV / Movie), back returns to the Ambient:Mode screen with the clip and the music playing on behind the menus. START takes it back to full screen and starts the music afresh. Play/pause on the main menu stops the clip ([Using OSD/OS](https://github.com/mehmetraif/OSD-OS/wiki/Using-OSD-OS)).

## Music playlists

An `m3u` or `m3u8` file in the media folder appears on the Audio row under its own name. Its tracks play in order, then from the top again. mpv reads it, so:

- one track per line: a file path or a URL, in any format mpv plays;
- a relative path is relative to the playlist's own folder;
- lines starting with `#` are comments.

`Cozy Jazz.m3u`, in the media folder:

```text
#EXTM3U
#EXTINF:-1,Blue Hour
music/Blue Hour.mp3
#EXTINF:-1,Rain on the Window
music/Rain on the Window.opus
/media/OSD-OS/Music/Lounge/Midnight Lounge.flac
```

The `music` folder itself isn't listed, but the playlist reaches into it.

## Starting by itself

**Auto-Launch Playback** plays straight away when OSD/OS starts on this module:

1. Settings → TV / Movie → Ambient:Mode → **Auto-Launch Playback**: On.
2. Settings → Startup → **Start on Module**: Ambient:Mode.

- It always shuffles: the videos when there is more than one, and the music when there is more than one audio file or playlist. With one or none, the clip's own sound plays.
- It doesn't happen when the module is opened from the main menu. Back from playback lands on the Ambient:Mode screen.
- A favourite chosen with **Play at Startup** comes before Start on Module ([Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings)).

## An example

A media folder with `Aquarium.mkv`, `Fireplace.mp4`, `Cozy Jazz.m3u` and `Thunderstorm.flac`. With these settings OSD/OS starts on both clips shuffled, the playlist and the recording shuffled under them, filling the 4:3 screen:

```json
{
    "app": {
        "startup_module": "com.osdos.ambient_mode"
    },
    "modules": {
        "com.osdos.ambient_mode": {
            "enabled": true,
            "media_directory": "",
            "auto_launch": true,
            "video_scaling": "Pan & Scan"
        }
    }
}
```

## Settings

Settings → TV / Movie → **Ambient:Mode**:

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| ENABLED | On, Off | Off | Shows Ambient:Mode on the main menu | `modules.com.osdos.ambient_mode.enabled` |
| Media Directory | A folder, or Default Folder | Default (`ambient` in the data folder) | Where the clips and the music are | `modules.com.osdos.ambient_mode.media_directory` (`""` for the default) |
| Auto-Launch Playback | On, Off | Off | Plays, shuffled, as soon as OSD/OS starts on this module ([above](https://github.com/mehmetraif/OSD-OS/wiki/Ambient-Mode#starting-by-itself)) | `modules.com.osdos.ambient_mode.auto_launch` |
| Scaling | Default, Letterbox, 14:9, Pan & Scan, Anamorphic | Default | How a 16:9 clip fills a 4:3 screen in this module. Default follows Settings → TV / Movie → Scaling | `modules.com.osdos.ambient_mode.video_scaling` |

What you chose on the module's screen isn't remembered between visits.

## Troubleshooting

| What happens | What to do |
|---|---|
| **No items found** | No video of a listed type sits directly in the media folder. Check Media Directory and the files' extensions |
| No music | The music needs mpv on the `PATH` or beside the app: otherwise the log says `[AmbientMode] mpv not found in PATH — audio will not play`. In a playlist, check the paths: names are case-sensitive on Linux. Check Settings → Sound → Audio Output too |
| Black bars round the clip | Set Scaling (Pan & Scan fills a 4:3 screen with a 16:9 clip), or use CROP in the playback menu |
| It doesn't start by itself | Start on Module must be Ambient:Mode, Auto-Launch Playback On, and the folder must hold a video. A Play at Startup favourite comes first |
| The clip stutters on a Pi | A Pi decodes H.264 in hardware, and a Pi 4 or 5 HEVC too. VP9 and AV1 are decoded by the CPU ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv)). Use a 1080p or 720p H.264 copy |

## See also

- [Weather](https://github.com/mehmetraif/OSD-OS/wiki/Weather): another screen to leave running, with music
- [Local Files](https://github.com/mehmetraif/OSD-OS/wiki/Local-Files): the film partition on the image
- [Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv): Scaling, CROP and decoding on each Pi
- [Menu music](https://github.com/mehmetraif/OSD-OS/wiki/Menu-Music), [Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output) and [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting)
