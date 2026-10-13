# Arcade

The Arcade module plays arcade games in MAME, the arcade machine emulator, from a folder of MAME's ROM sets: a zip file for each game. It lists them in OSD/OS's own tree, by name, number of players, decade and maker, and starts MAME full screen for the game you choose. MAME takes its players from Settings → Controls, so the JAMMA board's two sticks and up to four more pads play as players 1 to 6, and you come back to OSD/OS with Coin and Start together. This page covers setting it up, the list, playing, the players and their buttons, high scores, every setting with its config key, the files the module keeps, and what to do when a game doesn't start.

The module is in [modules/arcade](https://github.com/mehmetraif/OSD-OS/tree/main/modules/arcade) (views and manifest) and [src/modules/arcade](https://github.com/mehmetraif/OSD-OS/tree/main/src/modules/arcade) (`ArcadeBackend`, and `Mame`, what it asks MAME and makes of its answers). It is off at first: turn it on in **Settings → Arcade → Enabled**, and **Arcade** is on the main menu.

<table>
<tr><th width="50%">The games</th><th width="50%">All Games</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/arcade.png" width="100%" alt="Arcade: Recently Played, Favorites, Search, All Games, Players, Years and Makers; Players open on 1 Player, 2 Players, 4 Players and 6 Players" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/arcade-games.png" width="100%" alt="Arcade | All Games: 1943, Defender, Dig Dug, Donkey Kong, Frogger, Galaga (Namco rev. B) selected, Joust, Metal Slug, Ms. Pac-Man, Out Run; under them 1981, Namco, 2 players, galaga.zip" /></td></tr>
<tr><td>The games by players, decade and maker, from what MAME says of each.</td><td>A game's year, maker, players and file under the tree.</td></tr>
<tr><th>Playing</th><th>A game that couldn't start</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/arcade-play.png" width="100%" alt="PLAY Galaga (Namco rev. B): press COIN + START together to come back to OSD/OS, or Esc on a keyboard" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/arcade-missing.png" width="100%" alt="Could not play Metal Slug: missing sp-s2.sp1, 000-lo.lo, sfix.sfix and 1 more, looked for in mslug.zip, neogeo.zip, and MAME's own lines" /></td></tr>
<tr><td>How to come back, in player 1's own buttons, then MAME has the screen.</td><td>What the set lacked and where MAME looked: here the Neo-Geo BIOS, neogeo.zip.</td></tr>
</table>

## Setting it up

1. **MAME.** The OSD/OS image has it: Debian's `mame` package, MAME 0.276 on Raspberry Pi OS (trixie), with its data and plugins (`mame-data`). Elsewhere, install it: `sudo apt install mame` on Raspberry Pi OS or Debian, `brew install mame` on a Mac. A MAME of your own (a newer one, or GroovyMAME for a CRT) goes in the data folder's `bin` as `bin/mame`, where the module looks first, with its own folders (`bgfx`, `plugins`, `hash`…) beside it, or as a link to the program where it is; then `mame` on the `PATH`, then Debian's `/usr/games/mame`.
2. **The games.** Put the ROM sets in the games folder, one per game, named the way MAME names the game, in lower case: `pacman.zip`, `galaga.zip`, `sf2.zip` (`PACMAN.ZIP` is a different name on a drive that tells upper and lower case apart, as Linux's own do; the card's OSD-OS partition doesn't). A set can be a zip, a 7z or a folder of the game's files. BIOS sets, such as `neogeo.zip` for Neo-Geo games, go beside them or in a `bios` folder there; they aren't listed as games. Samples, the recorded sounds some early games play (Donkey Kong's, Space Invaders'), go in a `samples` folder there, a zip for each game as MAME names it (`dkong.zip`). The folder is:
   - on the OSD/OS image, `Games/Arcade` on the card's OSD-OS partition (`/media/OSD-OS/Games/Arcade`), which a computer opens too: copy the games onto the card there;
   - elsewhere, `Games/Arcade` in Local Files' default folder (`media` in the data folder);
   - or the folder **Settings → Arcade → Games Folder** names.

   The module makes the folder the first time it opens, so it is there to copy into.
3. **Sets for this MAME.** A ROM set has to match the MAME that plays it: a set made for another version may lack files this one needs, or have a name it doesn't know. The image's MAME is 0.276.
4. **Turn it on**: **Settings → Arcade → Enabled**. **Arcade** is then on the main menu.

ROM sets are copyrighted, like the games on them: use the games you are entitled to. MAME itself is free software (GPL-2.0 or later).

## The list

**Arcade** opens on a tree, which works as Local Files' does ([The tree](https://github.com/mehmetraif/OSD-OS/wiki/Local-Files#the-tree)): up and down move, right or select opens a folder, left or back closes it, and back at the top leaves the module.

| Folder | What it holds |
|---|---|
| **Recently Played** | The games played last, latest first (up to 30) |
| **Favorites** | The games you added with right on a game ([below](#a-games-options)) |
| **Search** | Asks for words on the on-screen keyboard and lists the games whose name or set has them: `pac` finds Pac-Man and Ms. Pac-Man |
| **All Games** | Every game, by name |
| **Players** | **1 Player**, **2 Players**… up to the most a game takes: the games for that many players at once, as MAME counts them |
| **Years** | **1980s**, **1990s**…: the games of each decade, a year MAME isn't sure of (`198?`, `1982?`) in its decade (**Year Unknown** for those MAME has no year for) |
| **Makers** | The games by maker: the maker before any licence or partner, so `Namco (Midway license)` is under **Namco** and `Capcom / Mitchell` under **Capcom**. A game whose maker MAME doesn't know is in none |
| **Not Recognized** | Shown when there are any: the files MAME has no game of, those whose name can't be a set's (`Street Fighter.zip`), and on a drive that tells cases apart those not in lower case (`PACMAN.ZIP`). The line under the tree says which, and what to do |

The line under the tree says what the entry under the cursor is: a game's year, maker, players, how well MAME plays it when it isn't fully (`Imperfect in MAME`, `Not working in MAME`) and its file, as `1981 • Namco • 2 players • galaga.zip`; on a folder, where the games are. What stands in the way of the list comes first: `The games folder can't be read: …`, `MAME is not installed`, `MAME could not list the games`. With no games at all, the screen says so, with what stands in the way and the folder they go in.

### Reading the games

The names, years, makers and players come from MAME (`mame -listxml`). The module asks MAME about the sets it hasn't asked about yet each time it opens: all of them the first time, a few hundred at a time, then only the sets new in the folder. While it asks, the folders show `loading…` and the line `Reading the games…`; Recently Played and Favorites are there at once, and a game played from them meanwhile pauses the asking until it ends. What MAME said is kept in `arcade/games.json` in the data folder (read as the list first opens), until MAME itself changes (an update, or a MAME of your own): then it is asked about every set again. **Settings → Arcade → Read the Games Again** does the same by hand.

Without MAME the sets are listed by their names, and select says MAME is not installed.

### A game's options

Right on a game opens its options:

| Option | What it does |
|---|---|
| **Add to Favorites** / **Remove from Favorites** | Puts it on Favorites, or takes it off |
| **Play at Startup** / **Don't Play at Startup** | Starts it straight after the boot screen every time OSD/OS starts, ahead of Settings → Start on Module: a cabinet that boots into its game. It goes on Favorites too. There is one for the whole app, as in [Local Files](https://github.com/mehmetraif/OSD-OS/wiki/Local-Files): choosing another replaces it |

## Playing

Select on a game. The screen says **PLAY ▶** and the game's name, and how to come back; then MAME has the screen for as long as the game runs. On the OSD/OS image OSD/OS hands the screen to MAME as it does to a script's takeover ([Scripts](https://github.com/mehmetraif/OSD-OS/wiki/Scripts#takeover)), and what it showed stays there while MAME starts. The first game after OSD/OS starts waits a second and a half before MAME takes the screen, for how to come back to be read; back in that time cancels it.

**Coming back**: press **Coin and Start together** on player 1's controller, or **Esc** on a keyboard. The screen names them as player 1's controller does: `COIN + START` on the JAMMA board's sticks, `BACK + START` on an Xbox pad, `SELECT + START` on a PlayStation one, `- + +` on a Nintendo one. This is MAME's own way out (its UI Cancel input), so MAME closes the game properly and saves its high scores, its NVRAM and its settings on the way. Back in OSD/OS the list is where you left it, on the game.

While MAME runs:

- Every key and button reaches MAME and OSD/OS alike, as both read the controllers; OSD/OS keeps them to itself, so nothing happens in the menus behind the game.
- No screen saver comes up.
- The menu music is off.
- MAME's own menu is **Tab** on a keyboard, or the controller's guide button: **MODE** (the service button) on the JAMMA board.

A game that couldn't start, or that MAME ended with an error, stays on the screen with what went wrong and MAME's last lines; back returns to the list ([Troubleshooting](#troubleshooting)).

## Players and buttons

MAME's players are the ones **Settings → Controls → Players** sets ([Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls#players)): player 1's controller is MAME's joystick 1, player 2's joystick 2, up to player 6. Before each game the module writes them into a controller file for MAME (`arcade/ctrlr/osdos.cfg`, given to MAME as `-ctrlr osdos`), each controller named by its SDL GUID (MAME's `mapdevice`), so the numbers hold whatever order the controllers were plugged in.

With MAME's own defaults, each joystick works its player's controls:

| On the controller | In MAME, for that player |
|---|---|
| Joystick or D-pad | The joystick |
| Buttons 1 to 6 (A, B, X, Y, LB, RB on a pad) | Buttons 1 to 6 |
| **Coin** (Back, Select) | Coin |
| **Start** | Start |

On the JAMMA board that is the cabinet's own layout for both players, with the kick harness's buttons 4 to 6. Change a game's buttons in MAME's menu (**Tab → Input Settings**); MAME keeps them in `arcade/cfg`.

Two pads of the same model have the same GUID, so MAME can't tell them apart: the lower-numbered player of the two has its number, and the other takes the place MAME's own order gives it. The JAMMA board's two sticks are told apart. Keyboards play too, with MAME's keys (5 coin, 1 start, the arrows and Ctrl, Alt, Space for player 1).

Six-player games, such as X-Men (6 Players), take six controllers: the JAMMA board's two sticks and four pads over USB or Bluetooth (Settings → Controls lists a player for each pad connected).

## High scores

With **High Scores** on (the default), MAME runs with its hiscore plugin, which keeps the high score tables of the games that forget them when they are switched off, as most early ones do: each in `arcade/hiscore/<game>.hi` in the data folder, for the games its `hiscore.dat` knows (the one in MAME's plugins). Games that keep their own scores in NVRAM keep them in `arcade/nvram`. A table is saved when the game closes the proper way, with Coin and Start, and also a few seconds after a new score goes in.

## The picture and the sound

MAME draws the game in the display mode OSD/OS runs in (**Settings → Display Output**: on the JAMMA board, its 240p), scaled to it; a vertical game stands upright inside the horizontal screen. On a Raspberry Pi 5 it opens the output the display preset names, as mpv does, rather than the first with a screen plugged in. **Video** chooses how MAME draws: if a game shows no picture, or MAME stops with an error about OpenGL, try another. The sound goes to the card **Settings → Audio Output** chose ([Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output)). With no sound card at all, MAME plays the game without sound (`-sound none`), as its sound output would stop it otherwise.

## Settings

Settings → Arcade.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Enabled | ON, OFF | OFF | Shows Arcade on the main menu | `modules.com.osdos.arcade.enabled` |
| Games Folder | Default, or a folder | Default | The folder of ROM sets. Default: `Games/Arcade` in Local Files' default folder, on the OSD/OS image `/media/OSD-OS/Games/Arcade` | `modules.com.osdos.arcade.games_directory` |
| High Scores | ON, OFF | ON | Keeps the games' high score tables with MAME's hiscore plugin ([above](#high-scores)) | `modules.com.osdos.arcade.high_scores` |
| Video | Auto, OpenGL, BGFX, Accelerated, Software | Auto | How MAME draws the game: Auto leaves it to MAME's own settings (Debian's `mame.ini` says OpenGL); the others are MAME's `-video opengl`, `bgfx`, `accel` and `soft` | `modules.com.osdos.arcade.video` |
| Read the Games Again | (action) | | Asks MAME about every set in the folder again ([Reading the games](#reading-the-games)) | |

### How the values are saved

| Key | Saved as |
|---|---|
| `enabled`, `high_scores` | `true` or `false` |
| `games_directory` | `""` for the default, else the folder's full path |
| `video` | `"Auto"`, `"OpenGL"`, `"BGFX"`, `"Accelerated"` or `"Software"` |

The module's entry in `config.json` (in the data folder), with the games on a USB drive and MAME's software drawing:

```json
{
    "modules": {
        "com.osdos.arcade": {
            "enabled": true,
            "games_directory": "/media/usb/MAME",
            "high_scores": true,
            "video": "Software"
        }
    }
}
```

## Files it keeps

Everything MAME writes is in `arcade` in the data folder (`~/.local/share/OSD-OS/` on Linux and the OSD/OS image, `~/Library/Application Support/OSD-OS/` on a Mac), whatever MAME's own settings say:

| File | What it holds |
|---|---|
| `arcade/games.json` | What MAME said of each set ([Reading the games](#reading-the-games)), and which MAME said it |
| `arcade/cfg/` | MAME's settings for each game, and `default.cfg` for all of them: the buttons changed in its menu |
| `arcade/nvram/` | The games' NVRAM: their settings and, for many, their scores |
| `arcade/hiscore/` | The hiscore plugin's tables ([High scores](#high-scores)) |
| `arcade/sta/`, `arcade/snap/`, `arcade/inp/`, `arcade/diff/` | Save states, snapshots, input recordings, and what a game with a hard disk writes to it |
| `arcade/ctrlr/osdos.cfg` | The controller file, written afresh before each game ([Players and buttons](#players-and-buttons)) |

Recently Played and Favorites are in `lists.json` with the other modules' lists. MAME runs in `arcade`, with these arguments:

```sh
mame galaga \
    -rompath "/media/OSD-OS/Games/Arcade;/media/OSD-OS/Games/Arcade/bios" \
    -homepath ~/.local/share/OSD-OS/arcade \
    -cfg_directory ~/.local/share/OSD-OS/arcade/cfg \
    -nvram_directory ~/.local/share/OSD-OS/arcade/nvram \
    -input_directory ~/.local/share/OSD-OS/arcade/inp \
    -state_directory ~/.local/share/OSD-OS/arcade/sta \
    -snapshot_directory ~/.local/share/OSD-OS/arcade/snap \
    -diff_directory ~/.local/share/OSD-OS/arcade/diff \
    -ctrlrpath ~/.local/share/OSD-OS/arcade/ctrlr -ctrlr osdos \
    -skip_gameinfo \
    -samplepath /media/OSD-OS/Games/Arcade/samples \
    -pluginspath /usr/share/games/mame/plugins -plugin hiscore
```

(`;…/bios` while there is a `bios` folder; `-samplepath` while there is a `samples` folder, in place of MAME's own; the plugin's two while High Scores is on; `-video …` with any Video but Auto; `-sound none` without a sound card.) Everything else is MAME's own settings: `/etc/mame/mame.ini` on the image, or a `mame.ini` of your own in `~/.mame`. A MAME of your own with its folders beside it runs in its own folder instead, as it would by hand, to find them, and reads the `mame.ini` there.

## Troubleshooting

| Problem | What to do |
|---|---|
| `MAME is not installed` | Install it ([Setting it up](#setting-it-up)). The OSD/OS image has it, unless it was built with `OSDOS_ARCADE=0` ([The OSD/OS image](https://github.com/mehmetraif/OSD-OS/wiki/The-OSD-OS-Image)) |
| No games, `No games found` | The sets go straight in the games folder, not in folders of their own (but for a game's own folder of files), as zip or 7z files. The screen names the folder |
| `The games folder can't be read: …` | The folder isn't there and couldn't be made, or can't be read: a drive that isn't plugged in, or a Games Folder that is gone. Plug the drive in, or choose the folder again in **Settings → Arcade → Games Folder** |
| A game is under **Not Recognized** | Its file isn't named as MAME names the game (`pacman.zip`, not `Pac-Man.zip`, nor `PACMAN.ZIP` on a drive that tells cases apart), or this MAME has no such game: a set for another version of MAME. The line under the tree says which |
| `Missing …, looked for in …` | The set lacks files this MAME needs. The sets it names say where: a clone also needs its parent's set (`puckman.zip` for `pacman.zip`), unless the sets are non-merged; a Neo-Geo game needs `neogeo.zip`, a PGM game `pgm.zip`, and so on. A set made for another version of MAME often lacks a file or two |
| `MAME stopped: …` about OpenGL, video or a window | Try another **Video** in Settings → Arcade |
| `MAME closed unexpectedly` | MAME crashed. Try another **Video** in Settings → Arcade; if every Video does the same, run MAME by hand ([below](#troubleshooting)) to see its last lines |
| The game plays but there is no sound | Check **Settings → Audio Output**: on a Pi 5 with the JAMMA board, the USB sound card |
| A game plays without some of its sounds | It plays them from samples: put its sample set (`dkong.zip` for Donkey Kong) in a `samples` folder in the games folder |
| A player's controls are wrong | Check **Settings → Controls → Players**: who is player 1 is who MAME's joystick 1 is. Then MAME's own menu: **Tab → Input Settings** |
| Coin and Start don't bring you back | Press them together on player 1's controller (the one Settings → Controls → Players makes player 1), or Esc on a keyboard. A button changed in MAME's menu for UI Cancel replaces the way back |
| High scores aren't kept | **High Scores** must be on, the game must be in MAME's `hiscore.dat`, and the game must close with Coin and Start: a power cut loses what wasn't saved |

The app's log has the module's lines: `journalctl -u osdos -b | grep -E 'Arcade|Scripts'` with the service, such as `[Arcade] games folder: /media/OSD-OS/Games/Arcade`, `[Arcade] Asking MAME about 18 games`, `[Arcade] mslug: Missing sp-s2.sp1, …` and `[Scripts] 'mame' finished: failed (exit 2)`. To see all MAME says, run it by hand from a terminal with the [arguments above](#files-it-keeps). See [Troubleshooting](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting).

## See also

- [Controls](https://github.com/mehmetraif/OSD-OS/wiki/Controls): the players, and the JAMMA board's sticks
- [Display Output](https://github.com/mehmetraif/OSD-OS/wiki/Display-Output): the JAMMA board's 240p, SCART RGB and composite
- [Audio Output](https://github.com/mehmetraif/OSD-OS/wiki/Audio-Output): the sound card MAME plays through
- [Scripts](https://github.com/mehmetraif/OSD-OS/wiki/Scripts): other games and emulators, as takeover scripts
- [The OSD/OS Image](https://github.com/mehmetraif/OSD-OS/wiki/The-OSD-OS-Image): MAME on the image, and the card's OSD-OS partition
- [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings) and [Configuration Files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files)
