# Arcade play analysis — MAME, FinalBurn Neo, netplay, LAN, local

**Branch:** `cursor/arcade-play-analysis-1aa3`  
**Against:** `main` @ `1b8c80d` (after the gamepad hand-off / USB-audio HID merges)  
**Scope:** design and reliability thoughts only — no product code on this branch  
**Lens:** Pi 4 (TV), Pi 5 (JAMMA cabinet), LAN between the two  

This note walks the local and networked arcade paths as they are wired today, then lists risks and improvement ideas. It is not a changelog and not a substitute for [ARCHITECTURE.md](ARCHITECTURE.md).

---

## How the pieces fit

| Mode | Engine | Screen | Pads | Network |
|------|--------|--------|------|---------|
| Local MAME | Debian/Homebrew/`bin` MAME | `DisplayHandoff` owner `arcade` | SDL released for child; `ctrlr/osdos.cfg` | — |
| Local FBNeo | RetroArch + FinalBurn Neo | same | SDL released; `sdl2` joypad + reserved device | — |
| Online host/join | Always FBNeo | same | same | Lobby + MITM relay |
| LAN host/join | Always FBNeo (CV1000 → lan core) | same | same + PortClaims | UDP discovery + claims |

QML entry is always Arcade’s `Play.qml` / `Root.qml` → `ArcadeBackend::play` or the netplay/LAN helpers that call `playFbneo` directly.

---

## Happy-path call chains

### Local MAME

`play(set, credits)` → `engineOf` ≠ FBNeo → write `ctrlr/osdos.cfg` (`mame::controllerConfig`) → optional `coins.lua` → `ScriptEntry` takeover → `ScriptLauncher::start` → `DisplayHandoff::acquire("arcade")` → `InputManager::releaseJoysticksForHandoff` → spawn MAME with `padEnvironment` + `ALSA_CARD` (or `-sound none`) → exit via Esc / Coin+Start (`UI_CANCEL`) → process-group drain → deferred DRM restore → reclaim pads → `finished`.

### Local FinalBurn Neo

Same QML path → `playFbneo(..., Netplay::None)` → BIOS links, `libretro::configText` / remap / hardcore options → `retroarch -L …` takeover → quit combo `input_quit_gamepad_combo=4` (Coin+Start) or Esc → `readLog` / `takeAchievements` (config removed).

### LAN host

`hostLanGame` → `playFbneo(..., LanHost)` → optional `fbneo_cv1k_libretro` for Cave CV1000 → `netplayTooBig` gate → `startClaims` on UDP 55436 → RetroArch `--host` (no lobby, no MITM) → joiners get pace / layout / ports over claims.

### LAN join

`joinLanRoom` → `askForPorts` (up to three tries) → `playFbneo(..., LanJoin, …)` → `releaseClaim` on exit.

### Online host / join

`hostGame` / `joinRoom` → `playFbneo` with MITM + lobby announce (host) or `--connect` (join). Joiner has **no** PortClaims (RetroArch’s first free port). `watchNetplay` polices alone-play after failed setup or host leave.

---

## Local play — MAME

### Takeover and pad release are load-bearing

`ScriptLauncher` acquires the screen before spawn. `main.cpp` releases SDL game controllers only while `DisplayHandoff` is held by `ArcadeBackend::kHandoffOwner`. `padEnvironment()` freezes `SDL_GAMECONTROLLERCONFIG` / mappings so MAME’s `JOYCODE_*` lines match the menus.

If release and spawn are reordered, or reclaim races a second acquire, the cabinet sticks fight two SDL stacks again. Treat acquire → release pads → spawn → drain → restore → reclaim as one state machine in tests.

### Exit is Coin+Start or Esc; SIGTERM does nothing useful

`mame::controllerConfig` maps `UI_CANCEL` to Esc or each player’s Coin+Start. MAME’s SDL OSD does not quit on SIGTERM. A stuck game leaves DRM handed off until the process group is killed (NVRAM at risk). The Play screen should keep teaching the exit combo; any “force stop” path must warn.

### Credits script is best-effort

`coins.lua` pulses `COIN1` after five seconds of game time. A failed write only logs a warning; the game still starts with zero credits. Prefer failing the launch or showing a clear miss when the script cannot be written.

### Silent MAME when ALSA has no playback card (Pi 5 JAMMA)

`!AudioOutput::hasPlaybackCard()` adds `-sound none` so MAME does not fall through to PortAudio and crash. On a Pi 5 with picture on JAMMA/composite and no USB sound card (or HDMI audio disabled), games launch mute with no UI error. That is the right crash avoidance, but the wrong UX: refuse start or demand Settings → Audio Output first.

---

## Local play — FinalBurn Neo / RetroArch

### Console driver bundle

With neither `DISPLAY` nor `WAYLAND_DISPLAY`, `configText` forces `gl`+`kms`, `udev` input, `alsa` audio. That matches the OSD/OS image. Under a compositor the desktop path is used instead. Log the chosen `ALSA_CARD` and DRM index at launch; they are easy to get wrong on a Pi 5.

### Device reservation (Linux type 2)

On Linux, `kPadsByVendor` writes `input_playerN_device_reservation_type = 2` and `vvvv:pppp Name`. Exclusive reservation pins the port; if RetroArch’s SDL names the pad differently from OSD/OS, a lone pad can land on port 2 and player 1 looks dead — while MAME (GUID `mapdevice`) still works. Prefer type 1 (preferred) or match on VID:PID alone when names disagree. (Open follow-up work outside this branch may already move this.)

### Remap ownership

If `FinalBurn Neo.rmp` does not start with OSD/OS’s marker, it is never rewritten. A remap saved from RetroArch’s menu shadows Controls forever. Offer an explicit “reset remap” in Arcade settings.

### Achievements and secrets

Hardcore is on when an account is present; patched ROM sets are disabled for hardcore. The password sits in `retroarch.cfg` only while RetroArch swaps it for a token; `takeAchievements` deletes the config. A kill mid-swap can leave a password on disk until the next cleanup — overwrite before remove if that matters.

### Exit parity with MAME

Coin+Start (`quit_gamepad_combo` 4) and Esc match the cabinet story. Keep that index stable across RetroArch upgrades.

---

## Online netplay

### Host always uses a MITM relay

`netplay_public_announce` + `netplay_use_mitm_server` + the configured relay (default `nyc`). Correct for home NAT; a dead relay looks like “host failed” after spawn. Probe or offer fallback relays before launch.

### `watchNetplay` is asymmetric

- **Host:** watch stops after 60 seconds (`kNetplayWatchFor`). A late “Failed to initialize netplay” can leave the host playing alone forever.
- **Joiner:** watch runs for the whole session; host leave or setup failure calls `requestStop()`.

Alone-play after a failed netplay init is RetroArch’s behaviour; OSD/OS must keep policing it. Extend the host watch for the whole session, or detect “netplay inactive” continuously.

### Lobby join is one port

Online join does not use PortClaims; RetroArch assigns the first free port. A JAMMA cabinet joining the lobby only drives one player. Document that; multi-port lobby would need protocol support.

### Lobby freshness

Password rooms are excluded; stale lobby entries yield dead connect addresses. Refresh the lobby immediately before join and show room age.

---

## LAN play

### Discovery

One UDP broadcast to port 55435, one second of listen, one-miss grace before a room disappears. Fine on wired cabinet LAN; flaky on Wi-Fi. Retry queries while the LAN filter is focused.

### PortClaims (UDP 55436)

Host binds the claims socket; failure forces `ports = 1` and keeps going. Joiners retry a few times, then guess. That is the weakest multi-stick LAN link: a blocked claims port silently ruins P2. Prefer failing the host start if claims cannot bind. `releaseClaim` is fire-and-forget — a crashed joiner leaks ports until the host game ends.

### CV1000 lan core

Cave CV1000 on stock FinalBurn Neo needs enormous RetroArch state rings (on the order of tens of GiB to host). OSD/OS’s `fbneo_cv1k_libretro` strips video memory from netplay states. Missing that core is a hard error; `netplayTooBig` gates the rest. Package and surface the core clearly on the image and in Host LAN UI. Pi 4 2 GB still cannot host some titles even with the lan core.

### Game-rate pacing (LAN only)

LAN hosts (and joiners whose host sent `claimPace`) set `audio_max_timing_skew` to 0 and disable audio rate control so every machine plays at the game’s rate, not a 60.05 Hz CRT’s. Older peers without pace stay on screen rate and skew returns. Version-gate or warn.

### Memory table coverage

`netplayStateSize` is board-driven from measured DAT machines; unknown boards need “0” and are allowed. Prefer a conservative default for unknown `sourceFile`.

---

## Engine selection

### Auto flips to FinalBurn Neo when networked features are on

`fbneoWanted() = achievements || online_play || lan_play`. Auto then prefers FBNeo whenever the set is usable. Turning on LAN or RetroAchievements silently changes which emulator runs — timing, audio path, and exit feel all change on the cabinet. Show a banner when Auto’s destination changes; consider per-game Auto vs a global “prefer FBNeo when networked”.

### Online / LAN always FinalBurn Neo

`hostGame` / `join*` call `playFbneo` even if the game’s engine is MAME. Intended (MAME has no netplay). Say so on the Engine page.

### High scores split

Local MAME hiscore plugin vs RetroAchievements on FBNeo for the same title depending on Auto. Details should name which backend applies.

---

## Cross-cutting risks

### Handoff and pad reclaim

DRM restore is deferred ~200 ms. Pads reclaim when `held` goes false. A downgraded takeover (CRTC save failed → console mode) can reclaim pads while the child still runs — two SDL consumers again. Keep pads released for any busy arcade launcher, including downgraded console runs. After Coin+Start, buttons are still held; reclaiming immediately synthesizes Back into the menus (UI “freeze”). Defer reclaim and seed held buttons without presses.

### Audio on Pi 5

Same `hasPlaybackCard` gate for MAME silence and RetroArch `launch.sound`. JAMMA video + USB audio must be selected; Auto often stays on HDMI. Pre-flight or refuse launch when the chosen card cannot play.

### Busy launcher / scan pause

Rapid replay during drain or the 200 ms restore hits “still running”. Expose draining to QML and disable Play until clear.

### Playing-state cleanup

FBNeo clears config and claims carefully. Ensure every `m_playing*` flag resets atomically on MAME and FBNeo finishes (including failed start).

### Panel vs Classic layout

Hosts with a JAMMA stick or Arcade Buttons use the Panel layout (6-Button Panel remap, max three ports per machine — RetroArch 1.20 netplay packet size). Games of five or six players force Classic so buttons map by FIRE number, not panel rows. Online lobby join never gets Panel. Cap the UI at three local players on Panel; explain Classic for large games.

### Alone-play is RetroArch’s footgun

Failed netplay or host hangup can leave a machine playing locally (sometimes unthrottled). `watchNetplay` exists because of that. Keep the adversarial log watch; do not shorten it on the host.

---

## Priority roll-up

| Severity | Themes |
|----------|--------|
| **High** | Pi 5 silent launch; host `watchNetplay` 60 s window; PortClaims bind failure; CV1000 RAM / lan core; handoff/pad reclaim races; Panel port cap; alone-play |
| **Medium** | Stuck MAME exit; reservation name mismatch; user remap shadow; lobby single-port; LAN discovery retries; Auto engine flip; busy-launcher UX |
| **Low / note** | Credits script write; relay probe; lobby age; scoring split; token cleanup; game-rate peer skew |

---

## Symbol index

| Concern | Primary symbols |
|---------|-----------------|
| MAME launch | `ArcadeBackend::play`, `mame::controllerConfig`, `mame::coinScript` |
| FBNeo launch | `ArcadeBackend::playFbneo`, `libretro::configText`, `arguments`, `reservedDevice` |
| Handoff | `DisplayHandoff::acquire` / `releaseDeferred`, `ScriptLauncher::start` / `finish` |
| Pads | `InputManager::releaseJoysticksForHandoff`, `padEnvironment` |
| Online | `hostGame`, `joinRoom`, `watchNetplay`, `libretro::NetplayLog` |
| LAN | `refreshLanRooms`, `PortClaims`, `claimPace` / `claimLayout` / `claimAnswer`, `playsOnLanCore` |
| Engine | `engineOf` / `engineFor`, `fbneoWanted`, `fbneoOnly` |
| Memory | `netplayStateSize`, `netplayMemory`, `netplayTooBig` |

---

## Suggested next probes on hardware

1. Pi 5 JAMMA, no USB sound: confirm MAME/FBNeo refuse or warn instead of silent `-sound none`.
2. Retrobit (or any non-Xbox pad) on FBNeo local: confirm port 1 binds with current reservation type.
3. Two-stick LAN join with claims port blocked: confirm host fails closed rather than `ports = 1`.
4. Online host: leave netplay failed after 60 s — does alone-play continue?
5. Coin+Start exit → menu: any Back storm or freeze on reclaim?

---

*Analysis only. No code changes on this branch.*
