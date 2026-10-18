# High Scores

The High Scores module shows the high score tables of the arcade games played in the [Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade) module, as MAME kept them: each game's table, a line for each entry, and what each entry holds. It reads MAME's own files on the device, through hi2txt's game definitions, with nothing online. This page covers setting it up, the list, where the tables come from and which games have them, the files the module keeps, and what to do when a game's table doesn't show.

The module is in [modules/highscores](https://github.com/mehmetraif/OSD-OS/tree/main/modules/highscores) (views, manifest, and hi2txt's definitions in `data/hi2txt.zip`) and [src/modules/highscores](https://github.com/mehmetraif/OSD-OS/tree/main/src/modules/highscores) (`HighScoresBackend`, and `ScoreTable`, a table laid out as lines). It reads the files with [openhi2txt](https://github.com/inigomonyota/openhi2txt), which OSD/OS builds with it ([thirdparty/openhi2txt](https://github.com/mehmetraif/OSD-OS/tree/main/thirdparty/openhi2txt)). It is off at first: turn it on in **Settings → Arcade → High Scores → Enabled**, and **High Scores** is on the main menu.

<table>
<tr><th width="50%">The games</th><th width="50%">All Games</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/high-scores.png" width="100%" alt="High Scores: Recently Played and All Games; Recently Played open on Final Fight, Out Run, Galaga, Ghosts'n Goblins and Bubble Bobble" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/high-scores-games.png" width="100%" alt="High Scores | All Games: Final Fight (World, set 1) selected, its table beside it, 1 116,370 ACD to 5 16,000 CAP; under them 1 116,370 ACD, ffight.hi" /></td></tr>
<tr><td>The games played last in Arcade, latest first.</td><td>A game's table beside it, and under the tree its best entry and its file.</td></tr>
<tr><th>A game's table</th><th>A game with several tables</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/high-scores-table.png" width="100%" alt="High Scores | Final Fight: entry 2 selected, 2 89,760 .Z; under them RANK 2, SCORE 89,760, NAME .Z" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/high-scores-tables.png" width="100%" alt="High Scores | Tetris the Absolute The Grand Master 2: Top Section Times, Grandmaster Top, Normal Top and Doubles Top" /></td></tr>
<tr><td>An entry's cells by column, under the tree.</td><td>A table for each mode of Tetris The Grand Master 2, a folder each.</td></tr>
</table>

## Setting it up

1. **Play in Arcade.** The tables are those MAME keeps of the games the Arcade module plays, with **Settings → Arcade → Save High Scores** on (the default; [Arcade → High scores](https://github.com/mehmetraif/OSD-OS/wiki/Arcade#high-scores)). Leave a game the proper way, with Coin and Start together, so MAME saves its table as it closes.
2. **Turn it on**: **Settings → Arcade → High Scores → Enabled**, under the High Scores heading of the Arcade page. **High Scores** is then on the main menu.

The releases and the OSD/OS image read the tables as they are. A build of your own needs minizip, which openhi2txt opens hi2txt's definitions with (`libminizip-dev` on Raspberry Pi OS and Debian, `brew install minizip` on a Mac; [Building from source](https://github.com/mehmetraif/OSD-OS/wiki/Building-from-Source)): without it the module is there, but shows no tables and says why.

## The list

**High Scores** opens on a tree, which works as Local Files' does ([The tree](https://github.com/mehmetraif/OSD-OS/wiki/Local-Files#the-tree)): up and down move, right or select opens a folder, left or back closes it, and back at the top leaves the module.

| Folder | What it holds |
|---|---|
| **Recently Played** | The games played last, latest first, as the Arcade module's own Recently Played has them: those of them with a table (up to 30) |
| **All Games** | Every game with a table, by name |
| **Not Readable** | The games whose files hi2txt can't read, and why ([below](#not-readable)) |

Each folder is there while it has a game in it.

A game opens on its table: a line for each entry, in the game's order, its columns lined up, numbers to the right and names to the left. A game with several tables, by difficulty, by mode or a table of times beside the scores, opens on a folder for each, named as hi2txt names it. A game that keeps a best score alone, with no table (Solar Fox), opens on it, a line for each of its fields. An entry is only read: there is nothing to open.

The line under the tree says what the entry under the cursor holds:

- on a game, its best entry and the file it was read from: `1 116,370 ACD • ffight.hi`;
- on an entry, each cell by its column: `RANK 2 • SCORE 89,760 • NAME .Z` (a column left blank isn't named);
- on a game that can't be read, why, and its file.

The names are MAME's, from the Arcade module's list (`arcade/games.json`); a game the Arcade module hasn't listed shows its set's name (`ffightu`). With no tables at all, the screen says so, with what keeps them from being read, if anything.

### Reading the tables

Each time it opens, the module looks at MAME's files and reads the games whose files are new, gone or changed since it last read them (MAME writes a game's files as the game ends): all of them the first time, then only the games played since. The first time, until every file is read, the folders show `loading…` and the line `Reading the high scores…`. What it read is kept in `highscores/scores.json` in the data folder, so after that the module opens at once on what it kept, and a game played since comes in once its files are read. When the definitions change (an update of OSD/OS that brings new ones), or MAME's `hiscore.dat` does, every game is read again, what was read shown meanwhile.

## Where the tables come from

MAME keeps a game's high scores in one of two places, both in `arcade` in the data folder, as the Arcade module runs it:

- **The hiscore plugin's files**, `arcade/hiscore/<game>.hi`, for the games that forget their table when they are switched off, as most early ones do. The plugin saves the part of the game's memory that holds the table as the game ends, and puts it back when it starts again. Its `hiscore.dat` (in MAME's plugins) says where that part is, for each game it knows.
- **The game's own NVRAM**, `arcade/nvram/<game>/`, for the games that keep their table themselves, as the real board does in its battery-backed memory or EEPROM: Williams', Atari's and Konami's, for many.

Either way the file is the game's memory, laid out as the game has it. hi2txt's game definitions describe that layout, game by game: 3,102 definitions, clones included, from GreatStone's [hi2txt-xml](https://github.com/GreatStoneEx/hi2txt-xml). openhi2txt reads a game's files with its definition and makes the tables of them. Some games' files are the same size from one version of the game to another, laid out differently; MAME's `hiscore.dat` tells them apart, so the module reads the files with the `hiscore.dat` of the MAME the Arcade module runs (the OSD/OS image's, or the one beside a MAME of your own).

The module shows what hi2txt shows by default: the main tables and fields. What a definition marks as extra (a day's scores beside the all-time table, a game's settings, checksums) is left out.

A game whose NVRAM holds its settings but no table hi2txt knows of (most games' NVRAM) isn't listed at all: its table, if it has one, is the hiscore plugin's.

### Not Readable

| The line says | What it means |
|---|---|
| `hi2txt has no description of this game's high scores yet` | hi2txt has no definition of the game. Its table can't be read until one is written ([hi2txt-xml](https://github.com/GreatStoneEx/hi2txt-xml) takes new ones) |
| `The file isn't laid out as hi2txt knows this game's: another version of it?` | There is a definition, but the file doesn't match it: a version of the game the definition doesn't cover, or a file saved with another `hiscore.dat` than the MAME's now (a file from another MAME, copied in) |
| `The file holds what hi2txt can't make out` | The file matched the definition, but what is in it couldn't be read: a damaged file |
| `hi2txt's description of this game can't be read` | The definition itself is broken |
| `hi2txt reads this game's table from a file MAME hasn't saved` | MAME kept a `.hi` file of the game, but its definition reads the table from another file, its NVRAM, which isn't there yet |
| `The file holds no high score table yet` | The file was read, but holds no table to show: a game whose `.hi` holds only what hi2txt marks as extra, its main table in its NVRAM, which MAME hasn't saved yet |
| `The high scores couldn't be read: …` | Anything else openhi2txt reports, in its own words after the colon: a file it isn't let read, a link that loops |

## Settings

Settings → Arcade → High Scores: the module shares the Arcade module's page, its row under a **High Scores** heading after the Arcade module's own.

| Setting | Values | Default | What it does | Config key |
|---|---|---|---|---|
| Enabled | ON, OFF | OFF | Shows High Scores on the main menu | `modules.com.osdos.highscores.enabled` |

## Files it keeps

In `highscores` in the data folder (`~/.local/share/OSD-OS/` on Linux and the OSD/OS image, `~/Library/Application Support/OSD-OS/` on a Mac):

| File | What it holds |
|---|---|
| `highscores/scores.json` | The tables read of each game, what its files were then (their names, sizes and times), why a file couldn't be read, and the games' names ([Reading the tables](#reading-the-tables)) |
| `highscores/mame/` | MAME's folders as openhi2txt reads them: links to `arcade/hiscore`, `arcade/nvram` and MAME's `hiscore.dat` |

MAME's own files are only read: the module writes nothing in `arcade`. Deleting `highscores` only makes the module read every game again, and a folder found where one of the links in `highscores/mame` should be (a backup restored through it) is made the link again.

## Attribution

- The tables are read with [openhi2txt](https://github.com/inigomonyota/openhi2txt), by inigomonyota (GPL-3.0), which parses XML with [RapidXML](https://rapidxml.sourceforge.net/), by Marcin Kalicinski (Boost Software License or MIT), and opens the definitions with zlib's minizip (zlib licence).
- They are read through [hi2txt](http://greatstone.free.fr/hi2txt/)'s game definitions (many of them from HiToText), by GreatStone and their contributors ([hi2txt-xml](https://github.com/GreatStoneEx/hi2txt-xml), GPL-2.0).
- MAME's hiscore plugin, which keeps most of those tables, was written by borgar, and its `hiscore.dat` is maintained by Leezer.

See [Credits](https://github.com/mehmetraif/OSD-OS/wiki/Credits).

## Troubleshooting

**`This build of OSD/OS reads no high score tables: it was built without minizip`.** The app was built without minizip, so without openhi2txt: build it again with minizip ([Setting it up](#setting-it-up)).

**`hi2txt's game definitions are missing: …`.** The app's `modules/highscores/data/hi2txt.zip` isn't there: install OSD/OS again.

**A game played isn't listed.**

- Is **Settings → Arcade → Save High Scores** on? Without it MAME runs without its hiscore plugin, and only the games that keep their own scores in NVRAM have a file.
- Did the game end the proper way, with Coin and Start? MAME saves the hiscore plugin's file as the game closes, and also a few seconds after a new score goes in.
- Does the plugin know the game? It keeps a table only for the games in its `hiscore.dat`: without an entry there, `arcade/hiscore` has no file for the game.
- A game whose NVRAM hi2txt has no table in isn't listed ([above](#where-the-tables-come-from)).

**A table looks wrong** (scores out of order, names garbled).

- No `hiscore.dat` was found beside the MAME the Arcade module runs: `highscores/mame/plugins/hiscore/hiscore.dat` isn't there. Some games' files can't be told apart without it, and are read with the first layout of their size.
- The game's file was saved by a MAME whose `hiscore.dat` gave it another layout than the one now: a `.hi` file from another MAME, for one. The plugin rewrites a file only when its table changes: delete the game's file in `arcade/hiscore`, play the game in this MAME, and its table is saved anew once a score goes in.

## See also

- [Arcade](https://github.com/mehmetraif/OSD-OS/wiki/Arcade): the games, and its Save High Scores setting
- [Troubleshooting → High Scores](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting#high-scores)
- [Settings](https://github.com/mehmetraif/OSD-OS/wiki/Settings) and [Configuration Files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files)
