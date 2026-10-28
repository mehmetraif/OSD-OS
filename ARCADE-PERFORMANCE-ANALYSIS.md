# Arcade performance analysis

**Branch:** `cursor/arcade-performance-analysis-1aa3`  
**Against:** `main` @ `1b8c80d`  
**Scope:** where Arcade can be faster or lighter on Pi 4 / Pi 5 (JAMMA, CRT, USB ROM drive, LAN) — browse UI and around play  
**Not in scope:** product code on this branch; hand-off/input fixes live elsewhere  

Companion to [ARCADE-PLAY-ANALYSIS.md](ARCADE-PLAY-ANALYSIS.md) (play/netplay design). This note is about CPU, I/O, GPU, and RAM cost.

---

## Performance map

| Phase | What runs | Dominant cost on Pi |
|-------|-----------|---------------------|
| **Cold open** | `startRead` → `read()` (folder + `games.json` + catver + FBNeo DAT + full `snap/` / `titles/` walk) then optional `-listxml` batches (`kBatch=300`) + `saveCache()` | SD/USB I/O; huge JSON; MAME subprocess; single-thread `m_pictureSaves` shared with picture writes |
| **Browse** | Windowed `filterGames(first,n)`; `filters()`; details + `wantPictures` on focus; theme shaders / `VhsNoise` / menu music while `!effectsRest` | GPU effects + music; `filters()` rebuild on `gamesChanged`; LAN UDP every 5s while Arcade screen is visible |
| **Filter / scroll** | `reload` → `filterGames` for ~one page; search → `searchSets` over every game | Search O(games×words) per keystroke; favorites star via linear `QStringList::contains` |
| **Details** | Images + RetroAchievements badges (debounced ~400 ms) | Network + decode |
| **Launch** | Play delay (150 ms / 3 s); `pauseScan`; write cfg / remap / BIOS links; `DisplayHandoff::acquire`; `MenuMusic::hold`; pad release; spawn | Sync main-thread I/O before fork; VT/DRM |
| **In-game** | Child owns DRM; UI rests; `watchNetplay` may poll `last.log` every 500 ms | Core/netplay RAM (CV1000); log I/O; claims UDP on LAN host |
| **Return** | Child exit → `releaseDeferred` (~200 ms) → reclaim pads → resume scan/sweep → list reload | Restore blank; scan resume CPU |
| **LAN / lobby** | Discover 5 s / lobby 20 s; room list rebuild; join claims | UDP + `gamesChanged` → full `reload()` when rooms change |

The browse path is already **windowed** (`filterGames(first, count)`). That is the right shape. The Pi tax is full-catalog invalidation, filter/reload storms, search, picture walks, and serialized cache/picture I/O — then a sync write + DRM stack at launch.

---

## Findings

### High

#### H1 — `index()` full rebuild, invalidated aggressively

`index()` walks every set, classifies, sorts (~tens of thousands for a full MAME set). `invalidate()` clears it (and `m_madeSets`). It is called from `takeMachines` as machines arrive, and from folder / genre / FBNeo / settings changes.

**Why it hurts:** Mid-scan or settings churn rebuilds the whole catalog whenever anything asks `filters` / `filterSets` / `madeSets`.

**Idea:** Dirty bit; invalidate once per batch / once per `takeMachines` call; incremental place updates for single inserts.

#### H2 — Cold `read()` rewalks all picture directories

`read()` `QDirIterator`s `snap/`, `titles/`, and shot dirs. Comments already note exFAT has no useful directory index. Triggered by `refresh` / `readAgain` after play and settings.

**Why it hurts:** USB ROM drives with thousands of PNGs → multi-second stalls before the list settles.

**Idea:** Persist snap/title maps with mtime/size stamps; only rewalk changed directories.

#### H3 — `games.json` save is heavy and shares the picture I/O thread

`saveCache()` sorts all machine keys, builds compact JSON, writes on `m_pictureSaves` (`setMaxThreadCount(1)`). During scan, about every `kCacheSaveMs` (30 s). Comments admit seconds on a Pi and a past O(n²) JSON bug.

**Why it hurts:** Contends with picture downloads; spikes latency; SD wear.

**Idea:** Separate writer pool; save on `finishScan` + dirty debounce; smaller/binary machine cache long-term.

#### H4 — Search rescans the library on every keypress

`searchKeys.onTextChanged` → `setSearch` → `searchSets`: for every game, `contains` for every word. No debounce.

**Why it hurts:** O(G×W) on Pi CPU while QML also rebuilds rows and details.

**Idea:** Debounce 150–250 ms; prefix / inverted index; filter from the previous query when the user only appends.

#### H5 — LAN refresh every 5 s while the Arcade screen is visible

`ArcadeScreen.qml` `lanRefresh`: `running: arcadeScreen.visible && !running` — not limited to the LAN filter. `Games.qml` already gates better. Room changes can `emit gamesChanged()` → full `reload()` → `filters()` + `filterIndex`.

**Why it hurts:** Two-Pi cabinets always pay UDP + list rebuild while browsing All Games; focus/list jitter.

**Idea:** Run the timer only when `filterId === "lan"` (or poll without `gamesChanged` unless the joinable set changes).

#### H6 — Launch: sync config/BIOS I/O then DRM hand-off

`playFbneo` / `play`: mkdir, BIOS links, remap/info/options/`retroarch.cfg`, `pauseScan`, then `acquire` (VT → drop master → save CRTC) + music hold + pad release. Play may wait 3 s (Start Info) or 150 ms.

**Why it hurts:** Felt start latency; main thread blocked before fork; VT/DRM is necessary but stacks with file I/O.

**Idea:** Prefetch cfg templates; incremental BIOS links by stamp; overlap music hold with writes; keep the long delay only when Start Info is on.

#### H7 — Netplay RAM / CV1000

Host keeps a large RetroArch state ring; stock FBNeo CV1000 states are ~145 MiB each — far beyond Pi. The lan core (~17 MiB states) and `netplayTooBig` already defend this. `madeSets("netplay")` still scans games with `netplayTooBig`.

**Why it hurts:** Wrong core or ungated host → OOM/thrash; repeated size checks on large libraries.

**Idea:** Cache a “hostable” bit per set; never offer CV1000 online without the lan core; keep LAN UI on the lan core.

### Medium

| Id | Topic | Idea |
|----|--------|------|
| **M1** | `filters()` rebuilds rooms, favorites, places, search counts on every `reload()` | Cache the filter strip until a structural change |
| **M2** | Favorites star: `QStringList::contains` per row | `QSet` membership |
| **M3** | Picture sweep 1 Hz (`kSweepStepMs`) shares `m_pictureSaves` with `games.json` | Pause while user is scrolling Arcade; separate I/O pool |
| **M4** | `wantPictures` on every details change while scrolling | Cancel in-flight when the set changes; stop carousel off-screen |
| **M5** | `watchNetplay` opens/seeks `last.log` every 500 ms (join: whole session) | inotify / slower poll after “netplay ready” |
| **M6** | Menu shaders + `VhsNoise` (~20 fps CPU) while browsing Arcade | Rest heavy effects while Arcade is focused or the wheel is moving |
| **M7** | Lobby HTTP up to 4 MiB, full sort | Filter to joinable FBNeo earlier; avoid QML reload when only ages change |

### Low

- `m_asked.contains` per machine in a 300-set batch  
- `picturesInfo()` counting by scanning all games when Pictures opens  
- Idle activity timer every 10 s while a game runs (trivial)  
- Claim/layout UDP waits (small vs spawn)

---

## Quick wins vs structural work

### Quick wins (localized, high ROI)

1. Gate ArcadeScreen LAN timer to the LAN filter (or suppress `gamesChanged` unless joinable rooms change).  
2. Debounce Search.  
3. `QSet` for favorites in `filterGames`.  
4. Coalesce `invalidate()` during `-listxml` batches (once per batch, not once per machine if anything already asks the index).  
5. Pause picture sweep while the user is actively navigating Arcade.  
6. Prefetch RetroArch cfg skeleton; skip full BIOS re-link when the stamp is unchanged.  
7. Optional: rest screen shaders / `VhsNoise` while Arcade is focused.

### Structural

1. Stamped picture + index caches (no full snap walk / full `index()` on every refresh).  
2. Split `games.json` writer from the picture download pool; smaller machine cache format.  
3. Search index (prefix / inverted).  
4. Stable filter model in QML (don’t rebuild every genre strip on room age churn).  
5. Netplay “can host/join” bitset updated only when memory / core / DAT changes.  
6. Move BIOS link + cfg write off the GUI thread with a readiness barrier before `acquire()`.

---

## Suggested measurement on a Pi 5 cabinet

Instrument with `QElapsedTimer` + tagged `qInfo` (cause: lan / lobby / scan / settings).

| Probe | What to log |
|-------|-------------|
| `read()` | Wall ms; set count; snap file count; `games.json` bytes; media on USB vs SD |
| `index()` | Rebuild count and ms; size of `games` |
| `saveCache()` | Queue wait on `m_pictureSaves` + write ms |
| Scan | Batch wall time; `invalidate` count per batch |
| `filters` / `reload` | Call rate; ms; cause tag |
| `searchSets` | ms vs library size and keystroke interval |
| LAN | UDP RTT; whether `gamesChanged` fired |
| Launch | Split: cfg / BIOS / `acquire` / process start / first log line / first frame |
| Return | Exit → restore fire → first QML frame; pad reclaim ms |
| Netplay | RSS host/join normal vs CV1000 lan-core; `watchNetplay` read bytes/s |
| Contention | CPU% of `osdos` vs child with effects on/off and sweep on/off |

**Scenarios:** (1) cold Arcade on a full-set USB drive, (2) type five search letters, (3) spin the wheel 30 s with pictures on, (4) local host+join, (5) CV1000 LAN with lan core, (6) exit to list while scan unfinished.

---

## Bottom line

Windowed list browsing is already sound. The biggest Pi gains are:

1. **Stop rebuilding the world** — coalesce `invalidate` / `filters` / `reload`, especially from LAN polling.  
2. **Stop touching the whole disk** on every refresh — stamp picture and machine caches.  
3. **Debounce and index search.**  
4. **Separate and lighten `games.json` I/O** from picture downloads.  
5. **Shorten the launch stack** by moving file work earlier or off the GUI thread before DRM hand-off.  
6. **Keep CV1000 on the lan core** and cache hostability so netplay filters stay cheap.

---

*Analysis only. No code changes on this branch.*
