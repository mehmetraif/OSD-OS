# Regression tests

Twelve test suites, built apart from the app. CI runs them on Linux x64 and arm64 for every pull request that touches `src/`, `views/`, `modules/`, `tests/` or the build ([regression-tests.yml](../.github/workflows/regression-tests.yml)): on Ubuntu, with the oldest Qt the releases are built with (6.4), and on Raspberry Pi OS (Debian 13 "trixie" with Raspberry Pi's archive, arm64), the system the OS image is made of, with the Pi's own Qt (6.8) and mpv (0.40). They need Lua 5.4, CMake, a C++17 compiler and Qt 6's development packages (Core, Concurrent, Gui, Network, Qml, Quick, QuickTest and Test), and libdrm's on Linux; the QML test also needs Qt Quick's QML modules at run time (on Debian and Raspberry Pi OS, `qml6-module-qttest qml6-module-qtquick qml6-module-qtqml-workerscript qml6-module-qtquick-window`). None needs mpv, FluidSynth or a display.

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
- Pick each theme with music, on the sound card in use: the music starts a moment after, stops the moment a video starts (and while it loads, and under its menu), and starts again back in the menus. A MIDI theme's first start waits for FluidSynth a few seconds.
- Move about the menus with each theme: the effects keep up at the CRT's 480 lines, and none of them, nor the music, shows or plays while a video does.
- With Transparent Background, play ten films in a row, each from the last one's Browse. The log's `[MpvController] memory as a video starts` lines stay about level, and no `[watchdog]` line comes.
- Hold Esc on the main menu for a second. Settings opens once and stays open.

- **osc_controls** runs the actual menu script at five output sizes with and without subtitles; verifies menu and seek bindings, including Lua 5.4 fractional-coordinate formatting.
- **reliability** exercises stale geocoding callbacks, failed update marker writes, fsync errors, asynchronous directory cache invalidation, atomic download replacement under a file-size limit, and download-owner destruction.
- **playback_retire** also replaces three films without stopping, verifies IPC keys/seeks for each, and disconnects/reconnects every control socket. Hardware decoding, DRM handoff and the user's actual media file still require a physical Pi.
