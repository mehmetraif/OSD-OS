# Regression tests

Nineteen test suites, built apart from the app. CI runs them on Linux x64 and arm64 for every pull request that touches `src/`, `views/`, `modules/`, `tests/`, `thirdparty/` or the build ([regression-tests.yml](../.github/workflows/regression-tests.yml)): on Ubuntu, with the oldest Qt the releases are built with (6.4), and on Raspberry Pi OS (Debian 13 "trixie" with Raspberry Pi's archive, arm64), the system the OS image is made of, with the Pi's own Qt (6.8) and mpv (0.40). They need Lua 5.4, CMake, a C++17 compiler, Qt 6's development packages (Core, Concurrent, Gui, Network, Qml, Quick, QuickTest and Test) and SDL2's, libdrm's on Linux, and zlib's and minizip's for **highscores** (`libminizip-dev`; without them that suite is left out, with a warning); the QML tests also need Qt Quick's QML modules at run time (on Debian and Raspberry Pi OS, `qml6-module-qttest qml6-module-qtquick qml6-module-qtqml-workerscript qml6-module-qtquick-window`). None needs mpv, FluidSynth, MAME or a display.

```sh
cmake -S tests -B build-tests
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

- **tree_browser** (`qml/tst_tree_browser.qml`, run by `tree_browser_test.cpp` on Qt Quick Test, offscreen, with the software renderer): the `TreeBrowser` on its own, over a made-up tree.
  - With `expandedFolderPreviews`, a folder's branch holds all its entries, and the folders in the one under the cursor branch once more; an empty folder off the spine has no branch.
  - Only the branch off the folder under the cursor, and its line, are drawn in full; every other is faint, expanded or compact.
  - No two blocks in a column overlap; of a thousand entries only the rows on the screen are drawn.
  - A folder opened again comes back with its cursor where it was; the compact branches keep to a few rows.
  - With Settings' Hint Bar off, the area reaches the content box's foot and shows more rows, the spine where it was.
  - A picture of it is saved as `tree-browser-preview.png`, which CI keeps.
- **controls_views** (`qml/tst_controllers.qml`, run by the same runner): Settings → Controls' pages, `views/Controllers.qml`, `views/ControllerSettings.qml` and `views/RemapControls.qml`, against a stand-in for `InputManager`.
  - The list: All Keyboards & Remotes, the players, a line for each player there is a pad for (two at least), the controllers connected by kind (P1, P2… on the pads that play), the one used last marked ◄, those away under Not Connected, nothing connected; the cursor skips the headings, keeps to its controller as another goes, and comes back to it from its page.
  - The players: Select lists Auto and the pads connected (no keyboard, remote or pad away), the player's own chosen; ◄ ► go round them; Back leaves the chooser unchanged; a pad chosen and away, and its line kept beyond the pads there.
  - A pad's page: its labels go round both ways; a press on it is waited for and set, another controller's input ignored; another controller's Back cancels, and so does the wait running out; its only Select / OK, and a button that does the action already, say so; Clear a Button and Reset to Defaults; leaving the page while it waits lets the pad act again.
  - A keyboard's page: what does Back on it cancels, Select / OK's key stays and says so, another key is set as an extra one; Remove a Key takes one of its own, says so of a key that isn't; its wait runs out too, as its hint says.
  - All Keyboards & Remotes: a key that keeps what it does says so and saves nothing, Back cancels, another key is saved for the action.
  - A controller away: its buttons wait for it, its labels don't, and Reset to Defaults leaves its page.
- **controllers** (`controllers_test.cpp`): each controller apart (`ControllerProfile`, and `InputManager` with pads SDL makes up, its virtual joysticks, its settings in `config.json` through `AppCore`).
  - What each label style calls a pad's buttons, sticks and d-pad; a profile as config.json keeps it, unknown actions dropped; which `/dev/input` nodes are keyboards and remotes; whose a key is (with and without X11's 8, the latest press, too old, a device gone, a queue that stays short).
  - A pad's own map over the shared one: buttons and a stick's direction set and cleared, its page's rows, its labels in the hints, a press reported instead of acting while its page waits.
  - Kept for the next start, listed while away, forgotten by Reset to Defaults; `input.cfg` underneath, with its labels, and the pages told when it is saved anew.
  - A button or a stick let go ends what its press began, though it was bound anew, or its page began to wait, while it was held; a pad that goes lets go of what it held, and not of another pad's.
  - No press takes the menus out of reach: a pad keeps one Select / OK, Back, Up and Down; on a keyboard, whatever does Back cancels, the keys for Select / OK, Up and Down and the menus' keys stay, the shared remap's too, any other key is set or moved, and Remove a Key takes only its own. A key held through a wait does nothing until let go.
  - A pad Steam's virtual gamepad comes to stand for lets go of what it held.
  - The same model twice is one line and one set of settings, only its presses taken while its page waits; the JAMMA board's two sticks are an arcade's, numbered, each by its own name, and one away without settings of its own keeps its name, kind and labels.
  - The players: Auto takes the JAMMA board's sticks by their number whatever came first, and Auto's player keeps its own stick beside a chosen one; one pad, one player, unless two of its model are there; six players, the seventh pad none, three of a model as three players; a pad chosen and away leaves its player none; SDL's player index; the players in a program's environment, nothing inherited; kept in `config.json`.
  - A pad's Back button is MAME's coin key, 5, where the item with the focus takes a coin (`takesCoin`, the nearest item round it that says), and Back anywhere else, its release the key's too; its B is Back either way; the coin hint names the pad's Back button (`[COIN]` on the JAMMA board's sticks).
- **storage_search** (`storage_search_test.cpp`):
  - Local Files' search keeps the first 200 matches by name.
  - A search replaced by the next is never reported.
  - The backend can go away while a search runs.
  - Settings and resume points survive a restart.
  - A save that can't be made (the data folder read-only) leaves the old file as it was. Run as root, that test skips itself: root writes to a read-only folder anyway.
  - State files keep their permissions, and a token is owner-only from its first write.
- **looks** (`looks_test.cpp`): themes and skins (Settings → Theme and Skin), read from the app's folder and the data folder's.
  - Skins and themes are listed by name, each once, the data folder's in place of the app's; one that isn't JSON is left out, for the app's of its name.
  - A skin's pictures and icons are read from its folder; one out of it, by a path or a link, is refused, and so is an icon whose name isn't one or isn't a picture. A skin made before skins had their name, a `theme.json` in the data folder's `themes`, is read as a skin, not a theme.
  - A theme's names (a scheme's, a skin's, a preset's) are passed on, a skin's read; its own colours, skin, effects (each kind's knobs, held between 0 and 1, a background's area, a shader in its folder) and music (of every kind the menu music plays) are read from its folder.
  - What can't be used is there but empty (Video 1's colours, OSD/OS's own window, no effect), and null is as left out.
  - An id is a folder's name, never a path.
- **playback_retire** (`playback_retire_test.cpp`, Linux only). A video is asked for while another plays in an mpv process, against a stand-in for mpv: a shell script first on `PATH` that writes down when it starts, is told to quit and exits.
  - The app goes on while the old player quits, and the new one starts only once it has gone.
  - Of several videos asked for in a row, only the last plays.
  - Stopped while it waits, nothing starts and its player is told once.
  - Stopped and followed at once by another, that one plays and the stop isn't reported.
  - A player that ignores being told to quit is killed a second on.
  - Menu music playing as a video is asked for is gone before the video's player starts, and stays off while it plays.
  - A video its menu's Browse left is offered back (`leftNote`) until another video is asked for; one its player noted nothing of leaves none.
- **menu_music** (`menu_music_test.cpp`, Linux only). The menu music against stand-ins for mpv, FluidSynth and openmpt123: shell scripts first on `PATH` that write down what they were asked.
  - Wanted, with a file, it plays a moment later; unwanted, it stops.
  - A hold stops it before `hold()` returns, so the sound card is free; it plays again once nothing holds it, and two holds keep it off until both let go.
  - A file mpv can't play, or that isn't there, isn't tried again.
  - A MIDI file is made into a WAV by FluidSynth with the SoundFont beside it, else the data folder's first, and played; the next time, the WAV made then. A failed one isn't tried again, and one held while it is being made is stopped, nothing half made kept, and made again.
  - A tracker's module plays as it is; one mpv can't play, openmpt123 makes into a WAV for it.
- **diagnostics** (`diagnostics_test.cpp`, Linux only). The crash and freeze reports (`util/Diagnostics`), each from a child process, the test itself run again, that crashes or freezes on purpose.
  - A crash is written to `crash-report.txt` with where it was, and still ends the process as a crash.
  - A main thread stuck for a while is reported in `stall-report.txt` with where it is and where the other threads are, and again once it answers; the app goes on.
  - Stuck for good, the process ends with the code (70) that has the service start it again.
  - The memory line has the app's memory and the system's.
- **display_output** (`display_output_test.cpp`, Linux only). Settings → Display Output on a Pi 4, and as **display_output_pi5** on a Pi 5, against the image's own presets copied into a stand-in boot partition.
  - JAMMA 240p is offered only by a launcher whose stop helper knows its code (`OSDOS_LAUNCHER_API` 4), last in the menu, beside the board's own outputs: a Pi 5's GPIO composite, a Pi 4's 240p SCART.
  - In force, it is known for what it is.
  - Chosen, it leaves its way back (`display-output.json`); with an older launcher it can't be chosen.
  - At Next Start, through a stand-in for its socket: written while the app goes on, still waiting when the app starts again in the same boot, taken back, and asked about once the Pi has started on it. A refusal or a socket gone says why and notes nothing.
  - Switch and Power Off needs launcher API 5.
- **osdos_stop** (`osdos_stop_test.sh`, Linux only). The stop helper, taken from `scripts/install.sh` as the image build takes it, on a stand-in boot partition with the image's presets.
  - Each display exit code writes its preset and reboots, or powers off (40–50); a Pi 5's SCART sync is written, JAMMA's isn't.
  - At Next Start writes without a reboot, keeps the output in force to go back to, and keeps it through a second request and a switch made after.
  - A name that isn't a preset (`previous`, a path, two names, a long line, nothing), or a preset not on the boot partition, changes nothing.
- **release_notes** (`release_notes_test.sh`, Linux only). A release's notes (`scripts/release-notes.sh`) on a made-up git history with tags.
  - The commits since the previous tag, by subject: `feat:` and "Add…" under What's New, `fix:` and "Fix…" under Bug Fixes, the rest under Changelog; a merge commit left out, a subject listed once, its `<` and `>` escaped.
  - The previous tag found by itself or given; the first release lists the whole history; a section with nothing in it is left out; no tag is an error.
- **jamma** (`jamma_test.cpp`, Linux only). A Raspberry Pi to JAMMA board's pins as gamepad events (`src/jamma`, `osdos-jamma`).
  - The built-in mapping is that of RGB-Pi's driver (JammaPi's `joypi`), on both expanders.
  - A mapping file moves pins and takes them out; its bad lines are named and change nothing.
  - Buttons go out as their codes, a button on two pins held while either is; the joystick as a digital stick on two axes (up and down together: neither).
  - A switch's bounce within 5 ms is let go by, without holding back another pin; an expander lost lets go of everything at once.
- **arcade** (`arcade_test.cpp`, Linux only). The Arcade module (`ArcadeBackend`, `Mame`) against a stand-in for MAME: a shell script in the data folder's `bin`, where the module looks first, that answers `-listxml` and writes down each game's arguments, folder, display card and controller file.
  - A set's short name, a maker before its licence or partner (none for one MAME calls unknown), a year's decade (`1982?` too); MAME's last error line, and of the files it says are missing only those it can't go without, with the sets it looked in.
  - The controller file: each player's pad as MAME's joystick of the same number, a pad of a model a player before has left to MAME's order, and Esc or player 1's Coin and Start together as the way out, with players or none.
  - `-listxml` read as it comes, a byte at a time as well as at once: a game's name, year, maker, players, rotation and status; a BIOS and a device aren't games; a broken answer fails.
  - The tree: Recently Played, Favorites, Search, All Games, Players, Years, Makers and Not Recognized, each game with its line under the tree and each set that isn't one with why; MAME asked once, about every file or folder whose name can be a set's, not the `bios` and `samples` folders, a zip ahead of a folder of its name; while MAME answers the top holds Recently Played and Favorites, and the folders asked for come once it has.
  - What MAME said is kept in `arcade/games.json`: it is asked again only about a set new in the folder, and about every one once MAME itself has changed. Each test runs on its own too.
  - A game started in `arcade` with every folder MAME writes in there, the samples folder, the hiscore plugin and the controller file, and on a Pi 5 the output's DRM card for SDL; Video and High Scores change its arguments; a MAME with its own folders beside it runs in its folder.
  - A game that didn't play says why: the files missing and where MAME looked (a folder of files as a folder, a set not there as a zip), or MAME's error with another Video suggested; a game gone from the folder, a name that isn't a set's and a second game while one runs are refused.
  - A set not in lower case on a filesystem that tells cases apart is under Not Recognized, with what to rename it to, and isn't played, but a name that can't be a set's isn't told to be renamed; `BIOS` and `Samples` folders go to MAME as they are named; a games folder that can't be made says so.
  - A game started while MAME is asked about the sets holds the next batch until it has ended.
  - Recently Played and Favorites show the games still in the folder, by MAME's names; without MAME the sets are listed by their names, and the module says MAME is not installed.
  - What the Arcade screen makes of a game: its title from MAME's description, its system from its driver's source file (a board by its name, any other driver by its own, with or without a maker's folder), the genres arcade players know games by, from catver.ini's categories (Fighting, Beat 'em Up, Shmup, Run and Gun, Light Gun, Racing, the others' main category; the `[Category]` section alone, without `* Mature *`), and the credits' script (player 1's coin, as many times as bought, once whatever resets the game).
  - The filters, in order: All Games, the lists, the genres (a clone's its parent's), players, decades and Not Recognized; while MAME is asked, only the lists. Menu Filters: its page's choices under their headings, a publisher's off; the filters and Start Filter's choices as filters are turned on or off, one or a heading's all at once, a publisher's among them; a publisher with dots in its name saved and read back as it is; a Start Filter saved under catver.ini's own genre read as the one it became. A page of one, where a game is in it, a game's details (system, maker, year, players, play time, vertical), its picture from the games folder's `snap` (its own, then its parent's) or MAME's own snapshots, looked for once till the list opens again, a `snap` folder that isn't a set, a set not in the folder, and Start Filter's and Play at Startup's choices.
  - Credits: MAME started with the script, nine credits at most, and none without credits.
  - Play time: a game's time added as it ends and kept for the next start; none for a game MAME refused.
  - Play at Startup follows the app's startup favourite whichever way it is set, None clears it only while it is the module's, and the choices are None and the favorites.
  - A `games.json` from before the system was kept is read as none, and every set asked about again.
- **arcade_views** (`qml/tst_arcade.qml`, run by tree_browser's runner): the Arcade module's views, `modules/arcade/views/Games.qml`, `ArcadeScreen.qml` and `Play.qml`, and its router, `Root.qml`, against a stand-in for `ArcadeBackend`.
  - A game chosen in the tree plays, and back from it lands where it was chosen; what stands in the way of the list shows under the tree, and on the empty screen with where the games go (not for a folder that can't be read); a set that isn't a game says why; the startup favourite plays at once, from Favorites.
  - A game's screen says how to come back in player 1's own buttons (`COIN + START`), or with Esc when there is no controller; the first game of a run waits for that to be read, and back meanwhile starts nothing.
  - While MAME has the screen every key is kept from the menus and the screen saver held off, until the game ends and the list comes back.
  - A game MAME ended with an error says why, with MAME's lines; one that never started says how to install MAME.
  - The Arcade screen: a filter's page on the arc, seven games with both bars, the filter and the page in the bar, the game's high scores (the High Scores module's tables read first) and play time, its full name and what it is in the help line, letters no smaller than the hint bar's; ▲ ▼ from game to game and page to page, round, and ◄ ► round the filters, keeping the game; COIN (5, which a pad's Back button is there, but not in the options) a credit, nine at most, START the game, back leaving; select the game as it is let go, and held its options, Play first, playing nothing as it is let go, a release without its press or a hold another key came in doing nothing; the selected name scrolling through when it doesn't fit, the others cut short; three high scores; with the hint bar and the help line off, a row more each, the high scores still three; nothing to list, or the games being read; opening on Start Filter's filter, on the filter and game a game was played from, on All Games for a filter that isn't there, which a favourite added later doesn't change, though one waited for while MAME is asked is shown once it has games; the startup favourite played at once, the screen on Favorites as it plays; the column's ground, the scheme's background (a colour's name, as Main.qml has it) as solid as Info Background says; laid out in the area Screen Adjustment saved, a taller one holding more games; adjusted, a frame whose sides ▲ ▼ ◄ ► move out, or in after select, a step at a time, as far as the screen's edge and no smaller than the list and the column need, the rows and the column following, held select back as the design has it, saved a moment after the last move and as back leaves.
  - A game from the Arcade screen: the credits going in, COIN for another and START to play at once, MAME given them and they spent; it waits for how to come back to be read every time; opened for why a game played from the list didn't, it says so at once and starts nothing.
  - The router: with Start Info Screen off, a game played straight from either screen's list, on Recently Played, with the credits bought, every key kept from the list while it runs and the screen saver held off, then the list with the keys again on the same game; a game MAME ended with an error, or one that never started, on `Play.qml` with why, back landing on it in the list; with Start Info Screen on, how to come back first. Screen Adjustment's row opening the Arcade screen to adjust whichever screen lists the games, in the area saved.
- **module_settings_view** (`qml/tst_module_settings.qml`, run by tree_browser's runner): a module's settings page, `views/ModuleSettings.qml`, against a stand-in for `AppCore`: rows shown only with another row's value (`shown_with`), unset as its default, coming and going as it changes, the cursor kept on it, their values kept while they are hidden, and a dynamic row's choices asked for as it comes; a toggle's rows following it as ON or OFF, and a row whose own row is hidden hidden too; a slider's tape bar, its value unset as its default, ◄ ► a step toward either end and no further, saved, and left out at the list's foot as the page opens, or at its top at the list's end, until a scroll brings it in whole. A multi-select page (`views/MultiSelectSettings.qml`): its choices asked for as it opens, each on unless saved off or its default off, ◄ ► or select flipping one, and a heading turning its group all on, or all off once they all are, the row saved whole each time; the help line on a heading only; a name too long for its line cut short, its ON or OFF whole.
- **highscores** (`highscores_test.cpp`): the High Scores module (`HighScoresBackend`, `ScoreTable`) reading, through openhi2txt and hi2txt's definitions, MAME's files of four games from hi2txt's own tests (`data/highscores`, see its README): Final Fight's and Ghosts'n Goblins' from the hiscore plugin, Tetris The Grand Master 2's EEPROM and Solar Fox's NVRAM, with a stand-in MAME's `hiscore.dat` beside it. Each test catches up with the files first, so it runs on its own too.
  - A table as lines: numbers to the right, names to the left with their leading space, a column blank in every row left out; an entry's cells by column; a game kept and read back.
  - The first read: nothing for a folder until it is done, then `entriesReady`; the links in `highscores/mame` to the Arcade module's folders and MAME's `hiscore.dat`.
  - The tree: Recently Played in the Arcade module's order, those of its games with a table (none without it), All Games by MAME's names, a game's table, a game of four tables as four folders, a game with a best score alone as its fields; Ghosts'n Goblins in the order its `hiscore.dat` block gives.
  - Not Readable: a game hi2txt has no definition of, and Final Fight's file as another version would leave it, each with why; NVRAM with settings in it isn't listed, even where its `hiscore.dat` block matches none of the game's layouts.
  - A game's best for the Arcade screen: the first table's first rows as rank, score and name, a best score kept on its own as the first, nothing for a game without a table.
  - A file is read again once its time changes, not before, and a game once a file of it is new or gone, whatever the times; a refresh asked for during a read follows it; another `hiscore.dat` (none) reads every game again, the old tables shown meanwhile; the names follow `games.json`; a game whose file is gone goes; what openhi2txt throws (a link that loops) is why a game can't be read; a folder where a link should be is made the link again; without the definitions, nothing is read and the module says why.
- **highscores_views** (`qml/tst_highscores.qml`, run by tree_browser's runner): the High Scores module's view, `modules/highscores/views/Scores.qml`, against a stand-in for `HighScoresBackend`.
  - A game's table from All Games, the line under the tree with its best entry and file, then an entry's cells; a name such as `<B>` shown as it is, not as markup; select offers to open a folder, not an entry; back at the top leaves the module.
  - A file that can't be read says why; while the files are first read the line says so, and the tree comes once they are; with no tables, how they come or what keeps them from being read.

## On a Raspberry Pi

What the tests can't reach, above all the screen changing hands (DRM and the VT), wants checking on the device:

- Search a large USB or NAS library while moving about the menus. The menus keep up, and no more than 200 results are kept.
- Start A, then B, then C quickly (NFC cards, say). Only C plays, once A has gone, and the menus never freeze meanwhile.
- Press back while the next video waits for the old one. Nothing starts, and the menus come back about 200 ms after the old player has gone. Repeat with Transparent Background on.
- Tap a card just as a video ends. The new one plays, and the menus don't take the screen from it.
- Cut the power while settings are being changed and while a video's resume point is saved. Every file comes back as the old JSON or the new, never cut short.
- Fill the data folder, or make it read-only. A save then logs why and leaves the old file.
- Choose an output with At Next Start, then switch the Pi off at the wall: it starts on the new output and asks to keep it. Choose one with Switch and Power Off: it powers off, and starts on it.
- With a Raspberry Pi to JAMMA board and JAMMA 240p, on a Pi 4 and on a Pi 5 (its sound through a USB sound card): the picture holds on the cabinet's monitor in the right colours, the sound comes from its speaker, `osdos-jamma --test` names each control as it is pressed, and both players' joysticks and buttons move the menus and reach a game.
- Plug in two keyboards (or a keyboard and a remote's dongle) and pair a Bluetooth one: each is listed in Settings → Controls under its own name as it comes, ◄ follows the one typed on, a key set on one's page works on it and not on the other, and a remote's Home or Menu button can be set on its page.
- With several pads, the JAMMA board's two sticks among them: each pad's page shows its buttons by its labels (Auto), a button set on one pad does that action on it alone, and the hints follow the pad used last.
- Pick each theme with music, on the sound card in use: the music starts a moment after, stops the moment a video starts (and while it loads, and under its menu), and starts again back in the menus. A MIDI theme's first start waits for FluidSynth a few seconds.
- Move about the menus with each theme: the effects keep up at the CRT's 480 lines, and none of them, nor the music, shows or plays while a video does.
- With Transparent Background, play ten films in a row, each from the last one's Browse. The log's `[MpvController] memory as a video starts` lines stay about level, and no `[watchdog]` line comes.
- Hold Esc on the main menu for a second. Settings opens once and stays open.

- **osc_controls** runs the actual menu script at five output sizes with and without subtitles; verifies menu and seek bindings, including Lua 5.4 fractional-coordinate formatting.
- **reliability** exercises stale geocoding callbacks, failed update marker writes, fsync errors, asynchronous directory cache invalidation, atomic download replacement under a file-size limit, and download-owner destruction.
- **playback_retire** also replaces three films without stopping, verifies IPC keys/seeks for each, and disconnects/reconnects every control socket. Hardware decoding, DRM handoff and the user's actual media file still require a physical Pi.
