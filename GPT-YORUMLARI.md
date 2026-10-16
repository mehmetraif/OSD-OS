# OSD/OS performance and logic review

Review date: October 16, 2026<br>
Reviewed version: `b9c8eff`<br>
Target hardware: Raspberry Pi 4 and Raspberry Pi 5 (8 GB), CRT television, and arcade cabinet

## Executive summary

The project's core architecture is well suited to the target hardware. Separating the UI from video and emulation workloads, delegating video to `mpv` and arcade games to MAME, and using separate display profiles for the Pi 4 and Pi 5 are sound decisions. DRM/VT handoff, atomic settings writes, gamepad/JAMMA support, and suspending effects during playback are particularly strong.

The most significant performance risk is not RAM, but large catalog operations on the main thread, slow SD/USB storage, and the cost of full-screen shaders. Although 8 GB of RAM is more than sufficient for both boards, additional RAM will not resolve single-core JSON parsing/sorting or exFAT directory-scanning latency. The Pi 5 handles these workloads more comfortably than the Pi 4, but the same architectural limits still apply to large Plex/Jellyfin/Emby libraries and complete MAME collections.

A second, more aggressive review of logic and error paths found significant correctness issues in addition to the performance concerns. The most serious is that Plex/Jellyfin/Emby playback signals do not carry request identifiers: a late response for canceled video A can be accepted by screen B, starting the wrong content and the wrong viewing session. Updates during power loss, VT restoration, and Pi 5 CRT output selection also need to be made more robust.

Top priorities:

1. Add request/item identifiers to all media backend responses and cancel stale responses.
2. Make updates and DRM/VT restoration safe under interruption.
3. Load large media libraries with pagination or a lazy model.
4. Move arcade directory scanning, `games.json` processing, and catalog indexing off the GUI thread.
5. Eliminate unbounded parallel requests when expanding Plex seasons.

> This report is a static code review. FPS, temperature, power consumption, and per-game MAME performance were not measured on a Pi 4 or Pi 5. Unmeasured areas are identified below as “measurement/test gaps.”

## Priority findings

### 1. Large media libraries are loaded into memory and the QML model all at once

**Severity:** High<br>
**Type:** Confirmed performance/architectural risk

- Jellyfin requests all results without a limit, parses the JSON on the main thread, and converts the entire result into a `QVariantList`: `src/modules/jellyfin/JellyfinBackend.cpp:668-705`.
- Emby uses the same approach: `src/modules/emby/EmbyBackend.cpp:845-862`.
- Plex processes the complete result from `/library/sections/{id}/all`: `src/modules/plex/PlexBackend.cpp:1536-1553`.
- The result is then assigned wholesale on the QML side and an alphabetical index is built: `modules/jellyfin/views/Items.qml:79-84`, `modules/plex/views/Items.qml:172-179`.

In a library containing thousands of movies or shows, JSON parsing, object copying, sorting, and QML model refreshes may all occur at once after the network response arrives. This can make the menu appear frozen for several seconds, particularly on the Pi 4. The Pi 5 reduces the delay but does not eliminate main-thread blocking.

**Recommendation:** Use server-side pagination or alphabetical segments; in the longer term, implement a `QAbstractListModel` that supplies rows on demand. If alphabetical jumping must be retained, the first screen can be displayed immediately while the remaining segments load in the background.

### 2. Significant arcade collection work runs on the GUI thread

**Severity:** High (for a complete MAME set and slow USB/SD storage)<br>
**Type:** Confirmed performance risk

- The ROM directory is traversed synchronously, with additional asset existence checks for some files: `src/modules/arcade/ArcadeBackend.cpp:317-371`.
- The entire `games.json` file is read and parsed, then rebuilt in full when saved: `src/modules/arcade/ArcadeBackend.cpp:462-496`.
- All game names are sorted, and genre, player-count, year, and manufacturer indexes are built in one pass: `src/modules/arcade/ArcadeBackend.cpp:632-665`.
- These operations run on the GUI thread through `refresh()`/`prepare()` and the first use of filters: `src/modules/arcade/ArcadeBackend.cpp:389-421`.

A complete MAME set can contain tens of thousands of entries. Directory operations and “does this file exist?” queries are also expensive on exFAT USB disks. Input latency may occur when the Arcade screen is first opened or during a rescan.

**Recommendation:** Perform directory scanning and index generation on a worker, then transfer the completed result to the main thread in one operation. A lightweight game list can be shown first, with incorrect-name checks and detailed indexes completed incrementally. Saving `games.json` should also be handled as a single debounced operation rather than between scan batches.

### 3. Plex seasons can generate unbounded parallel HTTP requests

**Severity:** High<br>
**Type:** Confirmed logic/performance risk

`flattenSeasons()` immediately starts a separate `/children` request for every season in the list: `src/modules/plex/PlexBackend.cpp:819-859`. This function is called both for hub listings and while loading the entire library: `src/modules/plex/PlexBackend.cpp:1517-1553`.

A TV library with many seasons can generate a large number of concurrent requests, multiple JSON parsing operations, and result copies. The user does not see the complete result until the final request finishes. Wi-Fi, the Plex server, and the Pi are all placed under unnecessary load at the same time.

**Recommendation:** Do not expand seasons into episode lists during library scans; expand them only when the user selects a season or “Play All.” If expansion is required, limit concurrency to 4–8 requests and present results incrementally.

### 4. Arcade screen adjustment is not protected against corrupt or manually modified values

**Severity:** Medium<br>
**Type:** Confirmed robustness defect

The normal settings screen preserves a minimum area: `modules/arcade/views/ArcadeScreen.qml:146-169`. However, saved `screen_adjustment` values are loaded at startup without validation: `modules/arcade/views/Root.qml:205-210`. An extreme value can shrink the list area; when `perPage` is zero or one, the `(perPage - 1)` term in the `pitch` calculation can produce an invalid result: `modules/arcade/views/ArcadeScreen.qml:70-85`.

This is not easy to trigger through normal key input, but it is a genuine robustness gap for an incomplete legacy setting, a manually edited `config.json`, or future settings-format changes.

**Recommendation:** Clamp all four edges when loading the saved value so that a minimum 600×420 area is preserved, and use a safe `pitch` value when `perPage <= 1`.

### 5. Selection changes perform GUI-thread disk access while locating arcade artwork

**Severity:** Medium<br>
**Type:** Confirmed performance risk

Every new game selection calls `gameDetails()`: `modules/arcade/views/ArcadeScreen.qml:130-133`. On the first visit, `pictureOf()` performs PNG/JPG asset existence checks and lists the screenshot directory: `src/modules/arcade/ArcadeBackend.cpp:776-831`. Results, including missing artwork, are cached for the session, so a second visit is inexpensive.

When quickly browsing for the first time on slow exFAT/USB storage, each title may cause a brief stall.

**Recommendation:** Index the `snap` directory once, or prefetch artwork for the visible page on a worker. Preserve the existing negative-result cache.

### 6. Asynchronous list responses do not carry request identifiers

**Severity:** Medium<br>
**Type:** Logic and unnecessary-work risk

Jellyfin and Emby accept `itemsLoaded` responses based only on the screen mode; they do not verify which `parentId` request produced the response: `modules/jellyfin/views/Items.qml:79-84`, `modules/emby/views/Items.qml:79-84`. Plex likewise has no correlation by section/hub identifier: `modules/plex/views/Items.qml:172-179`.

If the user navigates quickly, an old, large response can rebuild the model after a new screen has opened or briefly display the wrong list.

**Recommendation:** Add an increasing request identifier to every load, return it with the signal, and ignore stale responses. When a new request starts in the same category, cancel the previous `QNetworkReply` where possible.

### 7. YouTube subscription refresh requests all channels simultaneously

**Severity:** Medium<br>
**Type:** Scale-dependent performance risk

When the 15-minute cache expires, `refreshChannel()` starts all channels at once: `src/modules/youtube/YouTubeBackend.cpp:160-190`. Each completion parses RSS XML on the main thread: `src/modules/youtube/YouTubeBackend.cpp:264-293`.

No issue is expected with a small number of subscriptions. With hundreds of channels, there will be a short burst of Wi-Fi and CPU load on the Pi 4.

**Recommendation:** Use a queue with 3–5 concurrent requests, emit `listingReady` incrementally, and prioritize the visible channel.

### 8. The settings file is read repeatedly at playback startup

**Severity:** Medium-low<br>
**Type:** Confirmed I/O inefficiency

`AppCore::get_setting()` reads and parses `config.json` on every call: `src/AppCore.cpp:58-71`, `src/AppCore.cpp:164-181`. `MpvController` calls it multiple times for a session's scaling, crop, subtitle, background, and video-profile settings; examples include `src/player/MpvController.cpp:1018-1028` and `src/player/MpvController.cpp:1100-1138`.

The effect is limited while the file is small, but repeated SD access at the start of every video is still unnecessary.

**Recommendation:** Keep a configuration copy in memory, update it after `save_setting()`, and invalidate it using mtime/`QFileSystemWatcher` if manual edits are supported. Preserve immediate application of `mpv_video_args` on the next playback.

### 9. The Local Files cache is cleared every time the screen opens

**Severity:** Medium-low<br>
**Type:** Confirmed I/O inefficiency

The `TreeBrowser.rootPath` binding calls `clearDirectoryCache()` whenever the screen is created: `modules/local_files/views/Items.qml:102-109`. Returning to the same USB/NAS directories therefore causes `AsyncDirectoryCache` to access the disk or network again.

**Recommendation:** Clear the cache only when the media root changes, a drive is connected or disconnected, or the user explicitly refreshes.

### 10. The TreeBrowser session cache can grow without bound

**Severity:** Medium-low<br>
**Type:** Long-session memory risk

Every visited path is stored in the `listings` JavaScript object: `views/Components/TreeBrowser.qml:172-180`, `views/Components/TreeBrowser.qml:222-247`. With deep local directories and paginated online catalogs, it can grow until the module closes.

No practical issue is expected in short sessions given 8 GB of RAM, but an unbounded structure is undesirable in an arcade cabinet that remains running for days.

**Recommendation:** Apply an LRU limit based on path count or estimated row count; retain the active navigation trail and nearby branches while evicting older paths.

### 11. The Arcade theme is an expensive default combination for the Pi 4

**Severity:** Medium (at high output resolutions)<br>
**Type:** Performance risk requiring measurement

The Arcade theme enables `Stars`, `Rainbow` text, the `Rainbow` selector, and the `Cube` transition simultaneously: `assets/themes/arcade/theme.json:1-11`. Rainbow text places the screen in a full-resolution shader layer: `Main.qml:936-947`. Animated effect time is updated at approximately 30 Hz: `Main.qml:239-250`.

On a physical 480i CRT, the cost may be reasonable because of the low pixel count. When the Pi 4 is connected through 1080p HDMI to a converter or LCD arcade display, the full-screen shader may impose a noticeable load. Suspending effects during games and video substantially reduces this risk.

**Recommendation:** Offer an “Arcade Lite” option for the Pi 4 (static text + Fade), or recommend the lightweight theme during initial setup based on output resolution. Measure frame time on real hardware before changing the default.

### 12. Pi 5 composite raster/aspect behavior is left solely to a comment and user configuration

**Severity:** Low-medium<br>
**Type:** Hardware validation gap

The code explicitly notes that Pi 5 composite output sometimes reports 704×432 instead of 720×480i, and leaves the solution to `monitorpixelaspect`/system configuration: `src/player/MpvController.cpp:1055-1063`.

**Recommendation:** Document validated PAL/NTSC sample settings for Pi 5 composite as a separate procedure. Add automatic correction only after measuring across different TVs and converters.

## Second pass: concrete logic and reliability defects

### 13. A late playback response can start the wrong video

**Severity:** Critical<br>
**Type:** Confirmed race/logic defect

Plex's `streamUrlReady(url, token)` signal does not identify its request or video. If an Extra is started from `Extras.qml` and the user presses Back before the response arrives, the request remains active: `modules/plex/views/Extras.qml:121-143`. The late response is accepted unconditionally by the now-visible main `Item.qml`: `modules/plex/views/Item.qml:152-187`. The Extra's URL is combined with the main item's title, rating key, resume position, and session information.

Jellyfin/Emby/Plex list, detail, and playback responses share the same general design flaw: backend signals carry no request identifier, so new screens cannot distinguish stale responses. Example backend paths: `src/modules/jellyfin/JellyfinBackend.cpp:1128-1246`, `src/modules/emby/EmbyBackend.cpp:1321-1508`, `src/modules/plex/PlexBackend.cpp:2553-2584`.

**Recommendation:** Add a generation/request ID and media ID to every playback request and return them with the signal; QML should accept only the identifiers it expects. Back/logout/new requests should cancel the previous `QNetworkReply`. A playback session should become active only after the correct screen accepts the response.

### 14. An autoplay or fallback failure can leave the user on a black, unresponsive Player screen

**Severity:** High<br>
**Type:** Confirmed error-path gap

If the next-episode or transcode-fallback request fails after an episode ends, the Player view remains open; Plex only logs the generic backend error: `modules/plex/views/Player.qml:232-253`. Jellyfin/Emby likewise handle only some retry cases: `modules/jellyfin/views/Player.qml:445-493`, `modules/jellyfin/views/Player.qml:637-647`, `modules/emby/views/Player.qml:416-465`, `modules/emby/views/Player.qml:554-564`.

Because mpv may already have exited, the Back command may be sent to a non-running mpv process. In a cabinet without a remote or keyboard, the user can become trapped on the dead screen.

**Recommendation:** Maintain a separate error state for each pending operation; on error, clear the flag, close the server session, and call `goBack()` safely. Errors must also carry request IDs.

### 15. A partial VT switch during DRM handoff can leave the display on the wrong terminal

**Severity:** High<br>
**Type:** Confirmed system-call error path

If `VT_ACTIVATE` succeeds but `VT_WAITACTIVE` is interrupted or fails, `switchToVt()` treats the entire operation as failed: `src/util/DisplayHandoff.cpp:261-279`. Consequently, `m_switchedVt` remains false and the restore code does not return to the previous VT: `src/util/DisplayHandoff.cpp:176-183`.

In addition, if the active VT cannot be read from `/sys/class/tty/tty0/active`, a switch to another VT may still be attempted while `m_previousVt=-1`: `src/util/DisplayHandoff.cpp:90-105`, `src/util/DisplayHandoff.cpp:190-228`. Restoration is skipped because the terminal to return to is unknown.

**Recommendation:** Track activation and wait results separately, retry `VT_WAITACTIVE` on `EINTR`, and always attempt restoration if activation succeeded. If the active VT is unknown, do not begin handoff, or verify it with `VT_GETSTATE`.

### 16. Power loss can leave a staged update in a mixed installation state

**Severity:** Critical<br>
**Type:** Confirmed power-loss resilience defect

The launcher moves the `bin` and `share` directories separately: `scripts/install.sh:150-177`. Recovery runs only if `bin/osdos` is missing. If power is lost after the new `bin` is installed but before the new `share`, recovery is not triggered because the executable exists; the new binary starts with old or incomplete QML and assets.

Manual reinstallation also extracts the tarball directly over the live `/opt/osdos`: `scripts/install.sh:100-116`. A full disk or interruption does not preserve the previous working version and does not remove obsolete files.

**Recommendation:** Extract and validate versions in separate directories, maintain a small fsynced transaction journal, and atomically switch the active version with a single `current` symlink/rename. The presence of `.new` or `.old` should trigger recovery at every startup.

### 17. An early-boot race can select HDMI for Pi 5 CRT/JAMMA output

**Severity:** High<br>
**Type:** Confirmed hardware-dependent race

`wait-for-display` exits when it sees any DRM connector rather than the connector requested by the preset: `os/stage-osdos/02-system/files/wait-for-display:7-14`. The launcher then searches only once for the Composite/DPI connector in the Pi 5 preset; if it has not yet registered, it falls back to the first connected connector, usually HDMI: `scripts/install.sh:190-247`.

**Recommendation:** The wait script should read the preset first and wait until the requested connector and mode appear. On timeout, use an explicit error/log and controlled fallback rather than silently falling back to HDMI.

### 18. Remapping an NFC card loses the old mapping if the new write fails

**Severity:** High<br>
**Type:** Confirmed data loss

When a card is mapped to another title, the old file is deleted before the new file is written atomically: `src/modules/nfc_reader/NfcReaderBackend.cpp:499-516`. If the disk is full or the directory is read-only, the new write fails and the previous valid card mapping cannot be restored.

**Recommendation:** Commit the new file successfully before removing the old one. Briefly seeing the same UID in two files is safer than permanently losing the old mapping; rescan when the operation completes.

### 19. The system signals a settings change even when saving fails

**Severity:** High<br>
**Type:** Confirmed runtime/disk inconsistency

When `saveConfig()` fails, a warning is logged, but `appSettingChanged` or `moduleSettingChanged` is still emitted immediately afterward: `src/AppCore.cpp:220-231`. With a full or read-only disk, audio, input, and module backends apply the new value for that session, while `get_setting()` and the next restart use the old value.

**Recommendation:** Emit the change signal only after a successful commit and return a bool/error to QML. On save failure, restore the old value in the UI as well.

### 20. Success is reported even when the playlist file cannot be written

**Severity:** High<br>
**Type:** Confirmed error-checking gap

`PlaylistsBackend::prepare()` does not require directory creation, `QSaveFile::open`, `write`, and `commit` to succeed; it deletes old M3Us first and always returns the new path/item count: `src/modules/playlists/PlaylistsBackend.cpp:506-530`.

With a full or read-only disk, QML passes an invalid or incomplete M3U path to mpv.

**Recommendation:** Preserve the old file until the new file is committed; do not update `m_prepared` until the full write and commit are verified, and return an explicit error.

### 21. Exiting MAME depends solely on the Player 1 joystick

**Severity:** High (in a keyboardless cabinet)<br>
**Type:** Confirmed control lockout

Controller mapping preserves player order, but `UI_CANCEL` accepts only Esc or `JOYCODE_1_SELECT + JOYCODE_1_START`: `src/modules/arcade/Mame.cpp:473-499`. If the controller/encoder assigned to Player 1 is disconnected while Player 2 is connected, there is no normal way to exit a game even though the menus may work.

**Recommendation:** Use the first connected joystick for exiting, or add the Coin/Select + Start combination for every connected assigned player to the `UI_CANCEL` sequence.

### 22. A canceled game or one that fails to start is added to “Recently Played”

**Severity:** Medium<br>
**Type:** Confirmed logic defect

The Arcade root adds a game to the recent list before the information-screen delay and before receiving the result of `arcadeBackend.play()`: `modules/arcade/views/Root.qml:64-73`. If the user presses Back on the information screen or MAME preflight fails, a game that was never played appears in the Recent and High Scores flows.

**Recommendation:** Record the game as recent only after `play()` successfully starts the process.

### 23. A missing external ROM drive can be recreated as an empty directory

**Severity:** Medium-high<br>
**Type:** Confirmed storage-safety risk

Arcade `refresh()` calls `mkpath()` unconditionally on the configured games directory: `src/modules/arcade/ArcadeBackend.cpp:389-394`. If the external drive is not mounted, the same path can be created as a normal directory on the system disk; the UI shows an empty library, and any ROMs copied there are written to the SD card.

**Recommendation:** Verify that a user-selected external path is under the expected mount; do not create a directory for a missing volume, and display a “drive not connected” error.

### 24. A successful Jellyfin authentication check unexpectedly resets navigation

**Severity:** Medium<br>
**Type:** Confirmed navigation race

With a valid token, the module opens Libraries immediately and then runs asynchronous `check_auth()`: `modules/jellyfin/views/Root.qml:112-120`. A successful check also emits `authStateChanged`, causing the root to clear the current stack and return to Libraries: `modules/jellyfin/views/Root.qml:90-108`.

If the server is slow, the user may open a library/item in the meantime and be abruptly returned to the start when the response arrives.

**Recommendation:** Emit the signal only when authentication state actually changes, or do not treat successful validation as a navigation reset when already authenticated and not on the authentication screen.

### 25. Plex logout adds a protected screen back to the cleared stack

**Severity:** Medium<br>
**Type:** Confirmed navigation defect

The logout handler first sets `navStack=[]`, then calls `navigateTo("PinAuth.qml")`: `modules/plex/views/Root.qml:76-87`. Because `navigateTo` pushes the current protected view back onto the stack, pressing Back on PinAuth can return to the previous authenticated screen.

**Recommendation:** Transition to the authentication screen with `replaceWith`, which does not push onto the stack, and keep the stack empty after the transition.

### 26. Weather can disrupt screen-saver ownership held by another playback

**Severity:** Medium<br>
**Type:** Confirmed shared-state ownership defect

Weather sets shared `idleTracker.mpvActive=true` when opened and unconditionally sets it to false when closed: `modules/weather/views/Weather.qml:41-46`, `modules/weather/views/Weather.qml:83-87`. If embedded video is still playing in the background, leaving Weather erases the actual mpv activity state and allows the screen saver to appear over the video.

**Recommendation:** Use a separate suppression/ref-count owner for Weather, or restore the value to the actual `mpvController.videoActive` state when closing.

### 27. A script startup timeout can release a process that starts late

**Severity:** Medium-high<br>
**Type:** Confirmed rare process-lifecycle defect

If `QProcess` remains in `Starting` for more than five seconds, the watchdog calls `finish()`; however, the process is not killed and its signals are not disconnected. A later `started` signal can record the PGID and run the script after the display and menu music have already been restored: `src/modules/scripts/ScriptLauncher.cpp:330-357`, `src/modules/scripts/ScriptLauncher.cpp:495-569`.

**Recommendation:** The startup timeout must perform real cancellation: terminate/kill until the process reaches a terminal state, then release display/music ownership and report one result.

### 28. Stopping Ambient Mode audio can block the GUI and leave audio running

**Severity:** Medium<br>
**Type:** Confirmed process error path

`stopAudio()` calls `waitForFinished()` for one second on the GUI thread; it does not check the timeout result, calls `deleteLater()` without sending kill, and releases sound-card ownership: `src/modules/ambient_mode/AmbientModeBackend.cpp:119-130`.

**Recommendation:** Use asynchronous terminate → timed kill → ownership release after `finished`. This prevents a one-second menu freeze and ensures the old mpv process does not block new audio.

### 29. The update check accepts an older release as an “update”

**Severity:** Medium<br>
**Type:** Confirmed version-comparison defect

UpdateManager reports “up to date” only when the tag exactly matches the current version: `src/update/UpdateManager.cpp:232-284`. If the feed/GitHub mistakenly returns an older release, a downgrade is offered as long as a valid asset exists.

**Recommendation:** Use normalized semantic-version comparison and update only when `latest > current`. If reverting is required, provide a separate, explicit rollback path.

### 30. An interrupted first-boot media partition operation does not self-repair

**Severity:** Medium<br>
**Type:** Confirmed power-loss error path

`resize_early` skips the entire operation if a third partition exists: `os/stage-osdos/05-media/files/resize_early:87-105`. If power is lost after `mkpart` but before `mkfs.exfat` completes, the next boot assumes the partition is ready and does not format it.

**Recommendation:** Check the filesystem with `blkid TYPE`; if the partition exists but is unformatted, safely complete formatting, and if an unexpected filesystem exists, leave it untouched and report an explicit error.

### 31. Boot progress can delay the first frame by up to 2.5 seconds

**Severity:** Medium-low<br>
**Type:** Confirmed startup blocking

Before QML loads, the GUI/startup thread waits two seconds for `systemctl is-system-running`, then another 500 ms after kill: `src/boot/BootProgress.cpp:40-51`. Because the stall watchdog is installed afterward, it cannot report this delay.

**Recommendation:** Start the probe asynchronously and dismiss the boot overlay on response or after a short timeout.

### 32. The installation owner can diverge from the existing systemd user

**Severity:** Medium-high<br>
**Type:** Confirmed installation/operations defect

If an existing autostart installation runs under another user and the installer is rerun with “N” selected, the service is not removed, but `/opt/osdos` is recursively assigned to the invoking user: `scripts/install.sh:83-116`. The service may read the files but be unable to write to the installation tree for in-app updates.

**Recommendation:** Detect the existing unit's `User=` value and preserve it as the default owner; ask separately whether to disable/remove autostart, and verify at the end that the service user can write.

### 33. A release tag can be published without running regression tests

**Severity:** Medium<br>
**Type:** CI assurance gap

The regression workflow runs for PRs and `main` pushes, but not for tag pushes: `.github/workflows/regression-tests.yml:6-30`. The release workflow does not make publishing depend on a test job: `.github/workflows/release.yml:436-439`.

**Recommendation:** Run a reusable regression job within the release workflow and make all artifact/publish steps depend on it.

## Pi 4 and Pi 5 assessment

### Raspberry Pi 4 (8 GB)

- Video uses `--vo=drm --hwdec=drm-copy,v4l2m2m-copy`: `src/player/MpvController.cpp:1031-1043`. According to the code comment, this is a deliberate tradeoff targeting smooth cadence and crop at a CPU cost of approximately 50–70% across four cores.
- A 1080p source, Transparent Background, and heavy theme shaders running simultaneously may approach the Pi 4's limits. Suspending effects during normal full-screen playback is the correct safeguard.
- With large MAME/media catalogs, single-core performance, JSON copies, and storage latency will become noticeable before 8 GB of RAM is exhausted.
- For CRT/arcade use, 480-line output provides a performance advantage. Applying a CRT shader at 1080p and then converting to analog may be unnecessary GPU work.

### Raspberry Pi 5 (8 GB)

- Full KMS uses `--vo=drm --hwdec=auto-safe` and passes the selected DRM device/connector/mode: `src/player/MpvController.cpp:1055-1075`.
- The Pi 5 handles menu shaders and catalog operations more comfortably, but unbounded HTTP fan-out and GUI-thread work remain logically unchanged.
- Because there is no 3.5 mm audio output, the HDMI or USB sound-card path is important. Passing `-sound none` when no usable ALSA card exists at MAME startup is safe behavior: `src/modules/arcade/ArcadeBackend.cpp:1180-1183`.
- Composite raster/aspect behavior must be validated separately for PAL and NTSC on a real Pi 5 and real CRT.

## Strong design decisions

1. **Appropriate division of responsibilities:** The UI handles browsing; video runs through `mpv`, and arcade games through MAME.
2. **Pi-specific playback profiles:** The different KMS/decode behavior of the Pi 4 and Pi 5 is not forced into one generic setting: `src/player/MpvController.cpp:1003-1098`.
3. **DRM/VT handoff:** `DisplayHandoff` cleanly transfers Qt rendering and DRM master state while an external process takes over the display.
4. **Suspending effects during playback:** Shaders/transitions and menu music stop for video, MAME, or takeover views: `Main.qml:225-250`.
5. **Arcade pagination:** Only the current page is sent to QML rather than the complete game list: `src/modules/arcade/ArcadeBackend.cpp:749-769`.
6. **Pausing MAME scans during games:** The next `-listxml` batch continues after the MAME game exits: `src/modules/arcade/ArcadeBackend.cpp:580-588`.
7. **Bounded image decoding:** Arcade screenshots load asynchronously and decode to their on-screen size: `modules/arcade/views/ArcadeScreen.qml:533-536`.
8. **Atomic file writes:** Settings and state files are written without leaving partial JSON.
9. **Bounded workloads:** Local search is limited to 200 results, the script console has line/byte limits, and asynchronous directory reading is limited to two concurrent operations.
10. **Input architecture:** Centralized SDL/gamepad/JAMMA input management keeps QML screens independent of hardware type.
11. **CRT-focused layout:** `root.sh`/`root.sw`, overscan adjustment, and 960×720 design units are used.
12. **Existing test coverage:** There are substantial regression tests for difficult logic such as arcade scan batches, controller mapping, display output, and rapid video switching.

## Test and measurement gaps

Automated tests provide strong business-logic coverage, but do not emulate real GPU/DRM behavior:

- There is no QML/backend test for delayed or reordered Plex/Jellyfin/Emby list/detail/stream responses.
- There is no test for safely leaving Player when an autoplay/fallback request fails.
- There is no power-loss/recovery test at every rename point during a staged update.
- There is no launcher test emulating HDMI and Composite/DPI connectors appearing in different orders during Pi 5 boot.
- There are no rollback tests for config signals, NFC remapping, and playlist preparation with a full or read-only disk.
- There is no unit test validating Pi 4/Pi 5 `mpv` arguments by board model.
- Real VT/DRM master transitions in `DisplayHandoff` can be tested only on a physical Pi.
- Because QML tests use the software backend, they do not measure CRT/Rainbow/Stars shader frame-time cost.
- The Pi 5 composite 704×432/720×480i difference is not validated automatically.
- There are no performance thresholds for complete-MAME-set startup, snap-directory latency, or catalog-indexing time.

`tests/README.md:122-139` already contains a good physical-Pi checklist. The following measurements could be added:

| Scenario | Measurement | Recommended acceptance criterion |
|---|---|---|
| First Arcade launch, complete ROM set | Time from keypress to first renderable list | Show progress on Pi 4; menu input must not be lost |
| 100 rapid game selections | Worst frame time and disk wait | No visible selection skips |
| Arcade and lightweight themes, 480i/1080p | Average and 99th-percentile frame time | Must not regularly miss the output refresh interval |
| Pi 4, 1080p H.264/HEVC | CPU, dropped frames, temperature | No sustained dropped frames after 30 minutes |
| Pi 5 composite PAL/NTSC | Active raster and geometry | Circles remain circular; menu edges remain in the safe area |
| Plex/Jellyfin with 5,000 items | Post-response UI blocking time | Navigation input must not be lost; after pagination, the first screen should appear quickly |
| 200 YouTube subscriptions | Concurrent requests, time to first content | Fixed concurrency limit and incremental results |
| 24 hours of menu navigation | RSS/memory curve | No continuously rising trend |

## Recommended implementation order

### Phase 1 — Wrong content, black screens, and unrecoverable installations

1. Add request/media IDs to Plex/Jellyfin/Emby list, detail, stream, and error signals; cancel stale replies.
2. Exit Player safely in autoplay and transcode-fallback error paths.
3. Move staged updates to a versioned-directory + atomic-pointer and transaction-recovery model.
4. Fix partial VT switching and unknown active VT handling in `DisplayHandoff`.
5. Make the Pi 5 launcher actually wait for the preset connector.
6. Fix failure/rollback handling for NFC remapping, playlist preparation, and config saving.
7. Make MAME exit available on every connected controller.

### Phase 2 — Everyday-use logic and error paths

1. Fix Jellyfin authentication and Plex logout navigation races.
2. Resolve Weather/screen-saver ownership with a reference count or separate flag.
3. Move Script and Ambient processes to fully asynchronous terminate/kill flows.
4. Record Arcade recents only after successful launch and distinguish a missing ROM mount.
5. Clamp Arcade screen adjustments and guard `perPage`.
6. Ensure installer owner/service-user consistency and first-boot partition recovery.
7. Make releases depend on the regression job.

### Phase 3 — Large-collection scale and performance

1. Move Arcade directory scanning and indexing to a worker.
2. Convert media libraries to pagination/a lazy `QAbstractListModel`.
3. Add a concurrency limit or lazy expansion to Plex season requests.
4. Index the snap directory once.
5. Add a concurrency limit to the YouTube RSS request queue.
6. Remove unnecessary Local Files cache clearing and add an LRU limit to `TreeBrowser`.

### Phase 4 — Hardware profiles and measurement

1. Pi 4/Pi 5 `mpv` argument tests.
2. Heavy/light theme frame-time comparison on the Pi 4 at 480i and 1080p.
3. Pi 5 PAL/NTSC composite geometry and connector-registration-order tests.
4. A repeatable startup/navigation benchmark for a complete MAME set.

## Final assessment

The codebase is more mature than a simple “port a desktop application to the Pi” approach; Pi, CRT, and cabinet requirements have genuinely informed the architecture. Nevertheless, the second pass showed that although the happy path is well tested, significant gaps remain in error paths involving cancellation, late responses, full disks, power loss, and partial system calls. Request identifiers, the update transaction, and VT/connector restoration should be made safe first; large-collection workloads should then be moved off the main thread, and real-device measurements made reproducible.
