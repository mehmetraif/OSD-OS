# OSD/OS follow-up review — improvements since October 2026

Review date: October 22, 2026<br>
Reviewed revision: `0b5629a` (`origin/main`)<br>
Baseline: prior static review of `b9c8eff` (33 findings)<br>
Target hardware: Raspberry Pi 4 and Pi 5 (8 GB), CRT / JAMMA arcade cabinet<br>
Scope: read-only evaluation — no code changes in this report

## Executive summary

The codebase has improved substantially since the October 16 review. Of the 33 earlier findings, **17 are fixed**, **5 are mitigated**, and **11 remain open**. The most serious prior defects — wrong-video races from unkeyed stream replies, autoplay dead Player screens, NFC remap data loss, failed settings still applying, MAME Player-1-only exit, release without regression tests — are addressed with real code and, in several cases, tests.

Arcade has also grown into a much larger subsystem (FinalBurn Neo, RetroArch Lobby, LAN Play, RetroAchievements, 3D Curved list, picture sweeps). That work is mostly carefully bounded, but it introduces **new GUI-thread and double-reload risks** that matter on a Pi 4 CRT cabinet. Performance for huge Plex/Jellyfin libraries and unbounded YouTube channel refresh remain open.

Bottom line: reliability and async correctness are in much better shape; scale and lobby/online menu hitching are the next frontier.

## Scorecard vs prior 33 findings

| Status | Count | Items |
|--------|------:|-------|
| Fixed | 17 | 3, 5, 6, 13, 14, 15, 18, 19, 20, 21, 22, 25, 26, 27, 29, 30, 32, 33 |
| Mitigated | 5 | 2, 4, 16, 17 |
| Still open | 11 | 1, 7, 8, 9, 10, 11, 12, 23, 24, 28, 31 |

### Fixed (high-value)

1. **Plex season fan-out** — requests now queue with concurrency `kAtOnce = 4` (`src/modules/plex/PlexBackend.cpp:871-890`).
2. **Arcade snap lookup on selection** — pictures come from in-memory indexes built during background `read()` (`ArcadeBackend.cpp:875-915`, `1610-1636`).
3. **Unkeyed list loads** — Jellyfin/Emby/Plex list requests carry generation IDs (`m_listRequest` / equivalent).
4. **Wrong video from late `streamUrlReady`** — stream request generations plus QML `isLaunching` / `launchingExtra` guards.
5. **Autoplay / fallback dead Player** — `leaveWaiting()` and error handlers return to the previous screen.
6. **DisplayHandoff VT restore** — unknown active VT refused; `VT_GETSTATE` fallback; activate success tracked separately from wait; `EINTR` retry (`DisplayHandoff.cpp`).
7. **NFC remap** — write new mapping first; remove old only after success; rollback on failed remove.
8. **Settings save signals** — `appSettingChanged` / `moduleSettingChanged` emit only after successful `saveConfig` (`AppCore.cpp:225-241`).
9. **Playlist prepare** — atomic write failure returns an error map instead of a fake success path.
10. **MAME exit** — `UI_CANCEL` includes Select+Start for every mapped player (`Mame.cpp`).
11. **Arcade Recently Played** — recorded only after a successful `play` / `join` / `host`.
12. **Plex logout** — uses `replaceWith("PinAuth.qml")` with a cleared stack.
13. **Weather vs screen saver** — no longer clobbers `idleTracker.mpvActive`; uses a view-owned flag.
14. **Script startup watchdog** — kills the process on start timeout before finishing.
15. **Update downgrades** — `isNewer()` gates offers and staging.
16. **First-boot blank media partition** — `format_blank_media` formats an existing unformatted p3.
17. **Installer service owner** — answering “N” preserves the existing unit user instead of chowning to the SSH caller.
18. **Release CI** — `release` job `needs` the reusable `regression-tests` workflow.

### Mitigated (better, not complete)

| # | Topic | What improved | What remains |
|---|--------|---------------|--------------|
| 2 | Arcade folder/cache on GUI | `QtConcurrent::run(read)` for folder + cache | `index()` still builds on the GUI/caller thread |
| 4 | Screen adjustment / pitch | `pitch` uses `Math.max(1, perPage - 1)`; display clamp helpers | Saved `screen_adjustment` still loaded as raw ints |
| 16 | Staged update power loss | Extract to `.new`, rename swap, `.old` recovery, tests | Manual `tar` install still overlays the live tree |
| 17 | Pi 5 CRT connector race | Launcher waits up to ~10s for preset output, logs fallback | `wait-for-display` still exits on *any* DRM connector |

### Still open (unchanged intent)

1. **Unbounded media libraries** — Jellyfin/Emby/Plex still load full libraries into `QVariantList`.
2. **YouTube channel refresh** — all stale channels start at once (playlists are queued; channels are not).
3. **`get_setting` re-reads `config.json`** every call.
4. **Local Files clears directory cache** on many Items opens.
5. **TreeBrowser `listings` growth** — no global LRU cap.
6. **Arcade theme default** — Stars + Rainbow + Cube still expensive on Pi 4 @ high res.
7. **Pi 5 composite 704×432** — still comment/docs only.
8. **Arcade `mkpath` on missing external mount** — `refresh`/`read` can recreate the games path on the SD card.
9. **Jellyfin successful `check_auth`** still clears nav and replaces with Libraries.
10. **Ambient Mode `stopAudio`** — terminate + 1s wait, no kill on timeout.
11. **BootProgress** — still blocks the startup thread on `systemctl`; since Setup work the worst-case stall is longer (~5s with a second probe).

## What improved in product capability (not just bug fixes)

Since the baseline, the project leaned harder into the arcade/CRT mission:

- **FinalBurn Neo + RetroArch** as a second engine path, with DisplayHandoff reuse and Coin/Start quit combo.
- **RetroArch Lobby / online play** and **LAN Play** with room/byte caps and miss-tolerant LAN discovery.
- **RetroAchievements** sign-in, filters, badges, leaderboards, and picture downloads (single-flight media queue).
- **JAMMA mode catalogue**, scan-rate naming, Wi-Fi from Settings, System Information, OSD/OS Setup web page.
- **Arcade UX**: 3D Curved wheel, publisher filters, status icons, scrolling long titles/filter names.
- **Tests**: launcher staged-update and Pi 5 connector wait coverage; more Arcade/display-preset assertions; release gated on regression.

Architecture strengths called out earlier (mpv handoff, Pi decode profiles, atomic writes, effect rest during playback) remain intact and are being extended rather than abandoned.

## New risks introduced by recent Arcade/online work

These were not in the October 16 list. They are the main new cabinet concerns.

### N1. Lobby refresh can double-reload the Arcade screen — High

`refreshRooms()` emits both `roomsChanged` and `gamesChanged` (`ArcadeBackend.cpp:2793-2796`). On the lobby filter, QML `reload()`s for both (`ArcadeScreen.qml`). Each reload rebuilds filters and can rebuild up to ~1000 lobby room maps, on a 20s timer. Pi 4 CRT browsing can hitch every poll.

**Recommendation:** One consumer-facing signal; cache `lobby()` until rooms/versions change; do not clear `m_madeSets` on lobby-only updates.

### N2. Lobby / catver / RA list parse-write on the GUI thread — High

Lobby JSON (up to ~4 MiB), catver (up to ~16 MiB), and RA hash-list parse + atomic write run in network finished slots on the GUI thread. Large responses can freeze menus and coin handling for seconds on Pi 4.

**Recommendation:** Parse and write on a worker; post compact results to the GUI.

### N3. FBNeo launch does BIOS symlink/config I/O before handoff — High

`playFbneo()` walks BIOS links and writes remap/config on the GUI thread before `DisplayHandoff::acquire`. Large `bios/` trees delay the Select → blank VT → KMS sequence.

**Recommendation:** Prepare off-thread or cache until bios folder mtime changes; acquire only when argv is ready.

### N4. Core version probe loads the `.so` while lobby/LAN is open — High

`readOwnVersions()` can mmap/load FBNeo for up to 10s and then emit `roomsChanged`, triggering another full reload. First lobby open or core update is the painful case.

**Recommendation:** Defer until Lobby is shown; cache; emit only when version text actually changes.

### N5. RetroAchievements media queue unbounded — Medium

Downloads are correctly single-flight, but the queue has no max depth; sweep + fast browsing can enqueue many images, and each save may rewrite JSON on the GUI thread.

**Recommendation:** Cap/drop stale jobs; debounce picture record saves; pause sweep while the user is scrubbing.

### N6. Online Play off leaves lobby reply live — Medium

Disabling online play emits `gamesChanged` but does not abort `m_lobbyReply` (LAN off clears more carefully). A late reply can still parse and reload.

**Recommendation:** Abort and clear lobby state when Online Play turns off.

### N7. 3D Curved wheel binding cost — Medium (Pi 4)

Nested layout recalculation and dual `filterGames` fills on wrap; no shader, but CPU scene updates matter at 240p JAMMA on Pi 4.

**Recommendation:** Cache wheel geometry until list room changes; optional Straight default on tube profiles.

### N8. Manual installer still live-overlays `/opt/osdos` — Medium-High

Staged in-app updates are much safer; a re-run of `install.sh` extract still tars onto the live tree without an atomic swap or stopping a running service (`scripts/install.sh:157-161`).

### N9. BootProgress worse before first frame — Medium-Low

Second synchronous `systemctl` wait added with Setup work; worst-case GUI stall before QML ≈ ~5s if systemd is unresponsive.

## Pi 4 / Pi 5 reading (current)

### Raspberry Pi 4 (8 GB)

Still the tighter cabinet target. Fixed races and background Arcade folder reads help a lot. Remaining pain is: lobby JSON on GUI, double reload, FBNeo prep I/O, full media-library loads, and heavy theme + 3D Curved at higher pixel counts. Prefer 480i/JAMMA modes, Straight/Curved over 3D Curved when hitching, and keep Transparent Background off for 1080p watch sessions.

### Raspberry Pi 5 (8 GB)

Comfortably ahead on decode and shader headroom. Launcher now waits for the preset CRT/JAMMA connector, which closes the earlier HDMI-first race for installed images. Residual: `wait-for-display` prematurity, composite aspect still manual, and the same lobby GUI-thread work (less severe but not free). USB audio / `-sound none` path remains important.

## Recommended priority (no code in this PR)

### Phase A — Keep cabinets trustworthy

1. Lobby single-reload + cache; abort lobby when Online Play turns off.
2. Move lobby / RA-list / catver parse-write off the GUI thread.
3. Defer/cache FBNeo core version probe; prepare BIOS/config off-thread.
4. Bound RA media queue; stop clearing `madeSets` on lobby-only ticks.
5. Atomic/manual install path for live `tar` reinstalls; make `wait-for-display` honor the preset connector.

### Phase B — Finish prior leftovers

1. Paginate or lazy-model Jellyfin/Emby/Plex libraries.
2. Cap YouTube channel refresh concurrency.
3. In-memory `config.json` cache for `get_setting`.
4. Stop Local Files cache clear on every open; LRU for TreeBrowser.
5. Clamp saved Arcade screen adjustment; avoid `mkpath` for missing external volumes.
6. Jellyfin auth check must not reset navigation when already authed.
7. Ambient audio async kill; BootProgress fully async.

### Phase C — Measure on hardware

1. Lobby poll frame-time on Pi 4 @ JAMMA 240p with a large ROM set.
2. FBNeo launch latency from Select to first frame.
3. “Read the Games Again” + RA picture sweep disk/Wi-Fi load.
4. Pi 5 composite PAL/NTSC geometry recipe validation.
5. Heavy vs light theme + 3D Curved vs Straight at 480i and 1080p.

## Final assessment

Between `b9c8eff` and `0b5629a`, OSD/OS did the hard reliability work the earlier review asked for: request identity, safer persistence, VT restore, update/CI gates, and Arcade launch bookkeeping. The product also grew into a credible online/LAN arcade shell without throwing away DisplayHandoff or Pi decode profiles.

What remains is mostly **scale and menu hitching under the new online features**, plus a short list of earlier performance leftovers. That is a healthier place than mid-October: the catastrophic wrong-content / black-screen / data-loss class of bugs is largely closed; the next wins are smoother Pi 4 lobby browsing and large-library behavior.
