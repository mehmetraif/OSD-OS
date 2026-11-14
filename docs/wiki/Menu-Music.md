# Menu music

OSD/OS can play a tune under the menus, over and over: a [theme](https://github.com/mehmetraif/OSD-OS/wiki/Themes)'s, or a file of your own. It stops the moment anything else is about to make a sound, and starts again from the beginning back in the menus.

## Settings

Settings → Sound:

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Menu Music | THEME (with a theme), Off, File | THEME; without a theme, Off | **THEME**: the theme's tune, if it has one. **Off**: none. **File**: the Music File below | `app.menu_music`: `""`, `"Off"` or `"File"` |
| Music File | A file picked on the file browser, or No File | None | The tune of your own: MP3, WAV, OGG, Opus, FLAC, M4A, AAC, MID, MIDI, XM, MOD, S3M or IT. Dimmed unless Menu Music is File | `app.menu_music_file`: the file's full path, `""` for none |
| Music Volume | A slider from QUIET to LOUD, 0 to 100 in steps of 10 | 60 | How loud the tune is. It changes while the music plays. Dimmed while Menu Music is Off | `app.menu_music_volume`: a number |

- **Choosing a theme** sets Menu Music back to THEME. Music File and Music Volume keep their values.
- **The file browser** reaches Home, Media (`/media`), Drives (`/run/media/<user>`), Volumes (a Mac's `/Volumes`) and Root. The file must be on this machine: a stream's address doesn't play.
- **Over a video** there is never music ([below](#when-it-plays-and-when-it-rests)).

In `config.json` ([Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files)):

```json
{
    "app": {
        "menu_music": "File",
        "menu_music_file": "/media/OSD-OS/Music/menu-loop.ogg",
        "menu_music_volume": 40
    }
}
```

On the OSD/OS image, copy the tune onto the card's OSD-OS partition from a computer. Then choose Settings → Sound → Menu Music: File, and Music File → Media → OSD-OS → the file.

## The themes' tunes

Seven of the nine themes have a tune, each a short loop made by [make-menu-music.py](https://github.com/mehmetraif/OSD-OS/blob/main/scripts/make-menu-music.py) ([below](#making-tunes-with-make-menu-musicpy)):

| Theme | File | What it is | Length |
|---|---|---|---|
| Trinitron | `trinitron.ogg` | Calm, 92 beats a minute | 21 s |
| Late Show | `late-show.ogg` | Slow and swung, 72 beats a minute | 27 s |
| Matrix | `matrix.ogg` | Dark and driving, 112 beats a minute | 17 s |
| Inferno | `inferno.ogg` | Fast, 140 beats a minute | 14 s |
| Arcade | `arcade.ogg` | A bouncy chiptune, 128 beats a minute | 15 s |
| Winter | `winter.ogg` | A music box, 84 beats a minute | 23 s |
| Demoscene | `demoscene.xm` | A four-channel FastTracker 2 module | 31 s |
| Green Screen, Orb | — | No music | — |

- The six OGGs are Opus, mono, 90 to 170 KB each, and loop without a seam. Demoscene's module is 7 KB.
- The [theme template](https://github.com/mehmetraif/OSD-OS/tree/main/docs/theme-template)'s tune is a MIDI file, `tune.mid`, 20 seconds long.

## What plays it

An **mpv** process of its own loops the file (`--loop-file=inf`), on the card Settings → Sound → Audio Output chooses ([Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output)).

- mpv's own `mpv.conf` isn't read for the menu music (`--no-config`).
- It starts 700 ms after it is wanted, from the beginning of the tune, every time.
- When Audio Output changes, the music starts again on the new card. An unplugged card is passed over for the system's default until it is back.

## Formats

| Kind | Extensions | How it plays | Needs |
|---|---|---|---|
| A recording | `.ogg`, `.opus`, `.mp3`, `.flac`, `.wav`, `.m4a`, `.aac` | mpv plays it as it is | mpv |
| A tracker's module | `.xm`, `.mod`, `.s3m`, `.it` | mpv plays it through ffmpeg's libopenmpt. Where mpv can't, openmpt123 makes it into a WAV, which mpv plays | mpv; openmpt123 where mpv's ffmpeg has no libopenmpt |
| A MIDI file | `.mid`, `.midi` | FluidSynth plays it into a WAV with a SoundFont, which mpv plays | FluidSynth and a SoundFont |

The extension says which, in capitals or not. A theme's `music` must be one of these, in the theme's folder.

### A tracker's module

mpv plays a module where its ffmpeg was built with libopenmpt; Homebrew's may not be. Where mpv ends within 3 seconds of starting, **openmpt123** renders the module once into a WAV, which mpv loops.

### A MIDI file

A MIDI file is notes only: the sound comes from a **SoundFont**. **FluidSynth** plays the file into a WAV with the first SoundFont of:

1. **one beside the MIDI file, of its name**: `tune.sf2` beside `tune.mid` (the extension in lower case);
2. **the first in the data folder's `soundfonts`**, by name (`.sf2` or `.SF2`);
3. **the system's General MIDI SoundFont**, the first of these there is:

| Path | From |
|---|---|
| `/usr/share/sounds/sf2/default-GM.sf2` | Debian's and Raspberry Pi OS's default |
| `/usr/share/sounds/sf2/FluidR3_GM.sf2` | `fluid-soundfont-gm` |
| `/usr/share/sounds/sf2/TimGM6mb.sf2` | `timgm6mb-soundfont` |
| `/usr/share/soundfonts/default.sf2` | other Linux systems |
| `/usr/share/soundfonts/FluidR3_GM.sf2` | other Linux systems |
| `/opt/homebrew/share/soundfonts/default.sf2` | Homebrew's folder on Apple silicon, if a SoundFont is put there |
| `/usr/local/share/soundfonts/default.sf2` | Homebrew's folder on Intel, the same way |

With FluidSynth 2.3 the WAV ends in about two seconds of silence, so a MIDI tune pauses before it starts again.

### The cache

The WAVs FluidSynth and openmpt123 make are kept, so a file is made into a WAV once:

| System | Cache folder |
|---|---|
| Linux, the OSD/OS image | `~/.cache/OSD-OS/menu-music/` (or `$XDG_CACHE_HOME/OSD-OS/menu-music/`) |
| macOS | `~/Library/Caches/OSD-OS/menu-music/` |

- A changed file, or another SoundFont, makes a new WAV. Only the newest is kept.
- A SoundFont replaced under the same path is not noticed: delete the folder. Deleting it is always safe.

## When it plays and when it rests

The music plays while it is **wanted** and **nothing holds it**.

**Wanted**: there is a tune (THEME with a theme that has one, or File with a Music File), and none of these:

- the effects are resting, as while a video plays, loads or has its menu open ([Effects](https://github.com/mehmetraif/OSD-OS/wiki/Effects#never-over-a-video));
- the boot screen is up;
- the screen saver is showing;
- the window asking to keep a new display output is up ([Display Output](https://github.com/mehmetraif/OSD-OS/wiki/Display-Output)).

**Held**: anything about to play sound of its own holds the music off, from its start to its end:

- a video, from any module;
- Ambient:Mode's or Weather's music;
- a script, from the Scripts module;
- Netflix's or Prime Video's web player;
- an Arcade game in MAME.

A hold stops the music at once. Once nothing holds it and it is wanted, it starts 700 ms later, from the beginning.

A file that wasn't there, or that mpv couldn't play, isn't tried again until the tune changes: choose another theme or file and back, or restart OSD/OS. Choosing a sound card in Settings → Sound → Audio Output tries it once more too.

With no sound card to play through (a USB sound card unplugged, or not up yet as the Pi starts), the music waits: it starts by itself once a card is there.

## Installing what it needs

| System | What to do |
|---|---|
| The OSD/OS image | Nothing: it has FluidSynth, a General MIDI SoundFont (TimGM6mb, about 6 MB) and openmpt123 |
| Raspberry Pi OS, installed with `install.sh` | Nothing: the installer installs `fluidsynth`, `timgm6mb-soundfont` and `openmpt123` |
| Raspberry Pi OS or Debian, by hand | `sudo apt install fluidsynth timgm6mb-soundfont openmpt123` (or `fluid-soundfont-gm` for a larger SoundFont) |
| macOS | `brew install fluid-synth libopenmpt` (libopenmpt brings openmpt123), and a General MIDI `.sf2` in the data folder's `soundfonts`: Homebrew's FluidSynth comes without one |
| Other systems | FluidSynth, a General MIDI SoundFont and openmpt123 from the system's packages |

## Adding a SoundFont

- **For every MIDI tune**: put it in the data folder's `soundfonts`, which comes before the system's. With more than one, the first by name is used.

  ```sh
  mkdir -p ~/.local/share/OSD-OS/soundfonts
  cp ~/Downloads/GeneralUser.sf2 ~/.local/share/OSD-OS/soundfonts/
  ```

  On a Mac, `~/Library/Application Support/OSD-OS/soundfonts/`.

- **For one theme's tune**: put it beside the MIDI file, under the same name: `tune.sf2` for `tune.mid` ([example](#examples)).

- **For a fuller sound on Debian or Raspberry Pi OS**: `sudo apt install fluid-soundfont-gm` brings FluidR3 (about 140 MB). Copy `/usr/share/sounds/sf2/FluidR3_GM.sf2` into `soundfonts` to have it used before the system's default.

If you replace a SoundFont under the same name, delete the [cache folder](#the-cache).

## Making tunes with make-menu-music.py

[scripts/make-menu-music.py](https://github.com/mehmetraif/OSD-OS/blob/main/scripts/make-menu-music.py) makes the themes' tunes from notes written in it, with simple instruments: pulse and triangle waves, noise drums, a bell, an electric piano.

### What it makes

| Name | It writes | What |
|---|---|---|
| `trinitron`, `late-show`, `matrix`, `inferno`, `arcade`, `winter` | `assets/themes/<name>/<name>.ogg` | Each theme's tune: eight bars that loop, encoded as Opus |
| `demoscene` | `assets/themes/demoscene/demoscene.xm` | A FastTracker 2 module |
| `template` | `docs/theme-template/tune.mid` | The theme template's MIDI file |

### Running it

It needs Python 3 and, for the OGGs, ffmpeg with libopus. It writes into the repository it is in, so run it in a copy:

```sh
curl -L https://github.com/mehmetraif/OSD-OS/archive/refs/heads/main.tar.gz | tar -xz -C ~
cd ~/OSD-OS-main
python3 scripts/make-menu-music.py                  # all eight
python3 scripts/make-menu-music.py matrix winter    # only these
```

It prints each file and its length.

### How a tune is written

Each tune is a function that builds a `Tune` and returns it with how to finish it:

| Piece | What it does |
|---|---|
| `Tune(bpm, bars, swing=0.0, seed=1)` | An empty loop of bars of four beats. `swing` delays every second eighth, `seed` sets the drums' noise |
| `note(start, length, note, voice, volume, attack, decay, sustain, release, vibrato)` | One note: from `start` for `length` beats, a name (`"A4"`, `"F#3"`, `"Bb2"`) on an instrument |
| `melody(start, voice, volume, line)` | A tune: `line` is `[(note, eighths)]`, `"R"` a rest |
| `chords(tune, start, progression, beats, voice, volume)` | Each chord of a progression held for `beats` |
| `arpeggio(tune, start, progression, pattern, step, voice, volume)` | Each chord's notes in `pattern`'s order, a note every `step` beats, a bar per chord |
| `kick`, `snare`, `hat`, `brush` | Drums, at a beat |
| `pulse(duty)`, `triangle`, `sine`, `saw`, `epiano`, `bell` | The instruments |
| `finish(path, echo, echo_beats, lowpass, gain)` | Mixes it and writes the OGG through ffmpeg |

A note that runs past the loop's end is folded back onto its start.

### A tune of your own

Save this as `scripts/my-tune.py` beside `make-menu-music.py` in your copy of the repository:

```python
#!/usr/bin/env python3
"""A menu tune of my own, made with make-menu-music.py's instruments: eight
bars that loop seamlessly, written as an OGG wherever it is told.

    python3 scripts/my-tune.py ~/.local/share/OSD-OS/themes/amber-terminal/amber.ogg
"""
import importlib.util
import os
import sys

# make-menu-music.py, beside this file, loaded as a module (its name has
# hyphens, so it can't simply be imported).
HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("menu_music", os.path.join(HERE, "make-menu-music.py"))
mm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mm)


def amber():
    # Slow and patient, in D minor: a terminal humming to itself.
    t = mm.Tune(80, 8, seed=21)
    prog = [["D3", "F3", "A3"], ["Bb2", "D3", "F3"], ["C3", "E3", "G3"], ["A2", "C#3", "E3"]] * 2
    mm.chords(t, 0, prog, 4, mm.sine, 0.05, attack=0.5, decay=0.4, sustain=0.8, release=0.6)
    mm.arpeggio(t, 0, prog, [0, 1, 2, 1], 0.5, mm.pulse(0.125), 0.045, decay=0.05, sustain=0.3)
    for bar, root in enumerate(["D2", "Bb1", "C2", "A1"] * 2):
        t.note(bar * 4, 3.6, root, mm.triangle, 0.3, decay=0.3, sustain=0.6)
    t.melody(0, mm.bell, 0.16, [
        ("A4", 4), ("F4", 2), ("D4", 2),
        ("F4", 4), ("D4", 4),
        ("E4", 2), ("G4", 2), ("C5", 4),
        ("A4", 6), ("R", 2),
        ("D5", 4), ("C5", 2), ("A4", 2),
        ("Bb4", 4), ("F4", 4),
        ("G4", 2), ("E4", 2), ("C4", 4),
        ("D4", 6), ("R", 2)], decay=1.0, sustain=0.0, release=0.4)
    for beat in range(32):
        t.hat(beat + 0.5, 0.03)
        if beat % 4 == 0:
            t.kick(beat, 0.3)
    return t, dict(echo=0.3, echo_beats=0.75, lowpass=4500, gain=0.45)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: my-tune.py OUTPUT.ogg")
    tune, finish = amber()
    tune.finish(os.path.expanduser(sys.argv[1]), **finish)
```

```sh
python3 scripts/my-tune.py ~/.local/share/OSD-OS/themes/amber-terminal/amber.ogg
```

Then name it in the theme's `theme.json`, `"music": "amber.ogg"`, and choose the theme again. Keep every line's lengths adding up to 8 eighths per bar, or the tune drifts against its chords.

### A MIDI tune of your own

`write_midi()`, from the same script, writes notes as a MIDI file. In a script that loads `make-menu-music.py` as `mm`, as `my-tune.py` does, and is run with the file to write:

```python
# [(start beat, beats, note, velocity, channel)]; channel 9 is the drums.
notes = []
for bar, chord in enumerate([["C4", "E4", "G4"], ["A3", "C4", "E4"], ["F3", "A3", "C4"], ["G3", "B3", "D4"]]):
    for beat in range(4):
        for note in chord:
            notes.append((bar * 4 + beat, 0.9, note, 60, 0))      # the chords, on every beat
for beat in range(16):
    notes.append((beat, 0.25, 36 if beat % 2 == 0 else 38, 80, 9))  # kick and snare

# 120 beats a minute; Bright Acoustic Piano on channel 0.
mm.write_midi(os.path.expanduser(sys.argv[1]), 120, {0: 1}, notes)
```

- The third argument is `{channel: instrument}`, a General MIDI program number counted from 0: 1 is Bright Acoustic Piano, 32 Acoustic Bass, 73 Flute.
- Channel 9 is the drum kit: 36 a kick, 38 a snare, 42 a closed hi-hat.

## Examples

A theme whose MIDI tune always plays with its own SoundFont, with `"music": "waltz.mid"` in its `theme.json`:

```text
themes/music-box/
├── theme.json
├── waltz.mid
└── waltz.sf2
```

## Troubleshooting

The log says what the music did, in `[MenuMusic]` lines ([where to read it](https://github.com/mehmetraif/OSD-OS/wiki/Themes#reading-the-log)):

```sh
journalctl -u osdos -b | grep MenuMusic
```

| Line | What happened | What to do |
|---|---|---|
| `[MenuMusic] playing /home/pi/.local/share/OSD-OS/themes/x/menu.ogg` | It plays, the file or the WAV made of it | — |
| `[MenuMusic] making /…/tune.mid into a WAV with fluidsynth` | A MIDI file (or, `with openmpt123`, a module) being made into a WAV, the first time | Wait a moment |
| `[MenuMusic] /…/tune.mid: no such file` | The file isn't there any more | Choose it again, or another |
| `[MenuMusic] /…/tune.mid: a MIDI file needs FluidSynth and a SoundFont: fluidsynth is not installed` | No FluidSynth | [Install it](#installing-what-it-needs) |
| `[MenuMusic] /…/tune.mid: a MIDI file needs FluidSynth and a SoundFont: no SoundFont found` | No SoundFont beside it, in `soundfonts`, or on the system | [Add one](#adding-a-soundfont) |
| `[MenuMusic] /…/tune.mid couldn't be made into a WAV (exit 1): …` | FluidSynth or openmpt123 failed; its own words follow | A damaged file, or SoundFont |
| `[MenuMusic] mpv couldn't play /…/menu.mp3 (exit 2)` | mpv ended within 3 seconds: a file it can't play. For a module, mpv's ffmpeg has no libopenmpt and openmpt123 isn't installed | Try the file in mpv. For a module, install openmpt123 |
| `[MenuMusic] no sound card to play /…/menu.ogg through: waiting for one` | No sound card can play now: a USB sound card unplugged, or not up yet | Plug it in: the music starts by itself. [Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output) |
| `[MenuMusic] mpv not found: no menu music` | No mpv | Install mpv ([Installation](https://github.com/mehmetraif/OSD-OS/wiki/Installation)) |
| `[AppCore] theme x: its music, "menu.wma", is not a ogg/opus/mp3/flac/wav/m4a/aac/mid/midi/xm/mod/s3m/it file in its folder` | A theme's `music` isn't one of the types, isn't there, or is outside its folder | Fix `music` |

| What you hear | Why | What to do |
|---|---|---|
| Nothing, and no `[MenuMusic]` line | Menu Music is Off. Or THEME with a theme that has none. Or File without a Music File. Or something is resting it | Check Settings → Sound → Menu Music and Music File |
| Nothing, though the log says `playing` | Music Volume at QUIET, or the sound going to another card | Music Volume; [Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output) |
| A MIDI tune sounds the same after you changed its SoundFont | Same path: the WAV in the cache is used | Delete the [cache folder](#the-cache) |
| A pause before the tune starts over | A MIDI tune's WAV ends in silence; a song may fade out at its end | Use a recording made to loop |
| It stops in Weather, in Ambient:Mode or during a video | By design | [Above](#when-it-plays-and-when-it-rests) |

## See also

- [Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes): a theme's `music`, and making a theme
- [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings#sound): Menu Music, Music File and Music Volume
- [Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files): the data folder, the cache and the logs
- [Ambient:Mode](https://github.com/mehmetraif/OSD-OS/wiki/Ambient-Mode) and [Weather](https://github.com/mehmetraif/OSD-OS/wiki/Weather): modules with music of their own
