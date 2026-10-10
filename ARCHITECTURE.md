# OSD/OS Architecture

OSD/OS is a retro VCR-style media app built with **C++ Qt6 + QML**, targeting **Raspberry Pi 4** and **macOS**. and this is the reference for working on OSD/OS's code (whether you're adding a new module or changing an existing one). 

If you just want to install or build the app, see [INSTALL.md](INSTALL.md) and [BUILDING.md](BUILDING.md). 

If you want to contribute, please start with [CONTRIBUTING.md](CONTRIBUTING.md).

## Philosophy

Think of OSD/OS as a **browsing shell** that hands off to **purpose-built tools**.

- The app shell handles browsing, auth, and settings
- **Modules** are self-contained media integrations (Local Files, Plex, Ambient Mode, etc...) that the shell discovers and loads at startup.
- When a user picks something to play, the shell hands off to a dedicated fullscreen tool and resumes when that tool exits. For video, that tool is **mpv**, launched as a subprocess by `MpvController`. mpv is installed separately (`apt install mpv` / `brew install mpv`).  OSD/OS does not link against libmpv. The one exception is the **Transparent Background** setting, which plays video inside the app's own window through libmpv, opened at run time (see [Transparent Background](#transparent-background-video-inside-the-app)).

The guiding idea: **browse structured content, then hand off to the right tool for the job** rather than bundling everything into one binary.

## Project Structure

```
osd-os/
  src/                              # C++ source
    main.cpp                        # app entry point — engine setup, context properties, registerModule calls
    AppCore.h / AppCore.cpp         # app shell: module registry, config r/w, settings routing
    modules/                        # per-module C++ backends
      local_files/
        LocalFilesBackend.h/.cpp
        RemovableDrives.h/.cpp      # the USB drives plugged in, from the mount table
      plex/
        PlexBackend.h/.cpp          # good reference backend implementation
      playlists/
        PlaylistsBackend.h/.cpp     # lists across modules, their m3u, offline downloads (see Playlists)
        MediaServer.h               # what Playlists asks of a media server; Jellyfin's and Emby's backends implement it
        ServerDownload.h/.cpp       # one download from a media server, on a thread of its own
      ...
    util/
      AtomicFile.h                  # writeFileAtomically(): a state file written whole, never cut short
      FileNames.h                   # safeFileName(): a name every filesystem takes (exFAT's rules)
      SslErrors.h                   # expectedLanSslErrors(): a LAN server's own certificate
      EmbyApi.h                     # the Emby API's URLs that Jellyfin shares (browse, download, stream)
      OsdSkinProvider.h/.cpp        # a skin's pictures in the colour scheme's two colours (image://osdskin)
      OsdIconProvider.h/.cpp        # an icon (a module's logo, a skin's) in one colour (image://osdicon)
      ...
    player/
      MpvController.h/.cpp          # mpv subprocess controller: QProcess launch + IPC socket
      EmbeddedMpv.h/.cpp            # mpv inside the app's window (Transparent Background), libmpv opened at run time
      VideoSurface.h/.cpp           # the QML item that shows EmbeddedMpv's picture
      VhsNoise.h/.cpp               # a tape's noise, drawn afresh each frame, for LoadingScreen
    boot/
      BootProgress.h/.cpp           # boot screen state on the OSD/OS image (inert elsewhere)
    bluetooth/
      BluetoothManager.h/.cpp       # Settings → Bluetooth: BlueZ over D-Bus (Linux)
      BluetoothAgent.h/.cpp         # the pairing agent BlueZ asks (org.bluez.Agent1)
    display/
      DisplayOutput.h/.cpp          # Settings → Display Output on the image: the Pi's outputs, switched through a restart
    audio/
      AudioOutput.h/.cpp            # Settings → Audio Output: the sound card each player is told (ALSA)
      MenuMusic.h/.cpp              # Settings → Menu Music: a tune under the menus, held off by anything else that plays sound
    fx/
      SelectorFx.h/.cpp             # Settings → Selector Effect: sparks, lightning, a rainbow or snow round the selected line
  modules/                          # QML + assets per module (discovered at startup)
    plex/
      manifest.json                 # module identity and settings shape
      assets/images/logo.svg
      views/
        Root.qml                    # module router (required)
        ...
    local_files/
    ...
  views/                            # app-level QML
    ModuleList.qml
    Settings.qml
    About.qml                       # Settings → About: the credits, and the licence's text
    FilePicker.qml                  # the folder or file a setting names, picked on a TreeBrowser
    ...
    Components/                     # shared QML components (AppBar, HintBar, MenuRow, MenuList, HelpLine, ScrollMarks, TreeBrowser, InfoPanel, EntryOptions, PlaylistAdder, PlayerMenu, LoadingScreen, PromptScreen, OsdGround, SkinImage, SelectionBox, BackgroundFx, MousePointer, OnScreenKeyboard, WebPlayerBrowse, WebPlayerLaunch, the Osd* elements, ChoiceOverlay, qmldir)
    BootScreen.qml                  # boot screen of the OSD/OS image (see os/README.md)
  assets/
    themes/                         # Settings → Theme's own themes (Trinitron … Demoscene): theme.json and its music
    skins/                          # Settings → Skin's own skins (DOS, Rounded): skin.json and two-colour pictures
  shaders/                          # compiled into the app (Qt Shader Tools)
    effects.frag                    # the screen and text effects, one pass over the menus
    bg-matrix.frag, bg-fire.frag, … # the background effects
    ripple.frag, wave.frag, drop.frag # the transitions drawn by a shader
  Main.qml                          # app root
  CMakeLists.txt
  tests/                            # regression tests, built apart (see tests/README.md)
  os/                               # OSD/OS image: a pi-gen stage on Raspberry Pi OS Lite
  docs/                             # the wiki's pages (docs/wiki/, published to the GitHub wiki), the screen tour (TOUR.md) and its screenshots (docs/screenshots/, 640×480), diagrams and animations (docs/images/), and the theme and skin templates (docs/theme-template/, docs/skin-template/)
```

There are twelve modules today, each with a backend: `local_files`, `playlists`, `plex`, `jellyfin`, `emby`, `youtube`, `netflix`, `prime_video`, `nfc_reader`, `weather`, `scripts` and `ambient_mode`. `plex` is a helpful reference when building something new as it covers a more complex use case (connecting to a 3rd party API with auth)

## Anatomy of a Module

A module has up to three parts:

| Part | Location | Required? |
|---|---|---|
| `manifest.json` | `modules/<name>/manifest.json` | **Yes** — read by `AppCore` at startup |
| QML views | `modules/<name>/views/` (entry point `Root.qml`) | **Yes** |
| C++ backend | `src/modules/<name>/<Name>Backend.h/.cpp` | Optional |

`AppCore` scans `modules/*/manifest.json` at startup. A module that needs **no backend** (pure QML) requires **no C++ changes at all** — drop in the folder and it's discovered. A module that needs a backend adds one `registerModule(...)` call in `main.cpp` (see [AppCore](#appcore--the-app-shell)).

```
modules/<name>/
  manifest.json             # identity + settings
  assets/images/logo.svg    # logo for the module / single color `#ffffff` to enable color schemes to re-color
  views/
    Root.qml                # module router (entry point)
    Items.qml               # list view
    Detail.qml              # detail/leaf view
```

## manifest.json Reference

Loaded at startup by `AppCore` — the single source of truth for a module's identity and settings. No C++ changes are needed to add or modify settings.

```json
{
  "id": "com.osdos.<name>",
  "name": "<DISPLAY NAME>",
  "icon": "assets/images/logo.svg",
  "entry_point_qml": "views/Root.qml",
  "settings": [ ... ]
}
```

### Setting types

| `type` | Description | Extra fields |
|---|---|---|
| `toggle` | ON/OFF toggle | `default: "ON"` or `"OFF"` |
| `list_single` | Single-select list | `options` (its choices) and `default`, or `options_source: "dynamic"` with `options_slot` (and `apply_slot`) |
| `multiselect_submenu` | Multi-select list via submenu | `options_source`, `options_slot` |
| `submenu` | A page of its own for a group of settings, like YouTube's ADVANCED | `settings`: its rows, in this same format (a submenu may hold another) |
| `module_view` | Opens the module itself on one of its own views, like Netflix's SIGN IN | `params`: the `navParams` its `Root.qml` routes on |
| `directory_browser` | A folder, picked on the file picker's tree ([FilePicker](#filepicker-viewsfilepickerqml)); `""` is the module's own | `default` (path string, may be empty) |
| `action` | Button that calls a backend slot | `action_slot` |

Additional fields any setting may carry:

- `key` — the config key written under `modules.<id>.<key>` in `config.json`. Supports dot-notation.
- `label` — display text in Settings.
- `description` — the help line under the menu while the row is selected.
- `requires_auth` — if `true`, the setting is only shown when the module reports an authenticated state via `get_module_auth_state(moduleId)`. Used by Plex to hide server/user/library settings until sign-in.
- `requires_capability` — the setting is only shown while the backend has reported that capability, as `dynamicOptionsReady("_capabilities", [names])`. Jellyfin's intro and credit skipping rows wait for `mediasegments` this way.

A `submenu` row opens `ModuleSettings.qml` again on just its rows (`navParams.submenu` names it, and the title bar reads `MODULE / LABEL`); back returns to the row. Its rows' keys stay flat under `modules.<id>`, so moving a setting into or out of a submenu keeps what was saved.

A `module_view` row loads the module's `Root.qml` (`AppCore::module_entry_point`, so a disabled module's too) on the app's navigation stack, with the row's `params` as its `navParams`; the router opens the view they ask for, and back from that view returns to the row. Netflix, Prime Video and YouTube open their sign-in page this way (`{ "signIn": true }`).

### Dynamic options and apply slots

- For `list_single` / `multiselect_submenu` with `"options_source": "dynamic"`, the backend slot named by `options_slot` must emit `dynamicOptionsReady(key, [{id, label}])`. `AppCore` re-emits it to QML with the module ID prepended.
- For a dynamic `list_single` with `apply_slot`, that slot is called automatically (routed through `invoke_module_action`) when the user changes the value. A list with fixed `options` doesn't call it.

A real example (Plex) — note `requires_auth`, dynamic options, and apply slots:

```json
{
  "key": "server_machine_id",
  "label": "Server",
  "type": "list_single",
  "options_source": "dynamic",
  "options_slot": "getServers",
  "apply_slot": "applyCurrentServerSetting",
  "requires_auth": true
}
```

## AppCore — the App Shell

`AppCore` (`src/AppCore.h/.cpp`) is the shell. It's exposed to all QML as the context property **`appCore`**.

**Global context properties** (available in all QML): `appCore`, `mpvController`, plus one per module backend (`localFilesBackend`, `plexBackend`, `ambientModeBackend`, …). Backend names are assigned by the `registerModule` call in `main.cpp`.

### Q_INVOKABLE slots used by QML

| Slot | Purpose |
|---|---|
| `scan_for_modules()` | Emits `modulesLoaded` with enabled modules |
| `get_settings()` | Returns entire `config.json` as a map |
| `get_setting(moduleId, key)` | Returns a single setting value |
| `save_setting(moduleId, key, value)` | Writes to `config.json`; supports dot-notation keys |
| `get_list(moduleId, name)` | One of a module's lists of entries (`recent`, `favorites`), newest first, from `lists.json` |
| `add_to_list(moduleId, name, entry, limit)` | Puts an entry first, in place of one with its `path`, and keeps the newest `limit` (50 unless given) |
| `remove_from_list(moduleId, name, path)` / `list_contains(moduleId, name, path)` | Takes an entry off a list / says whether one is on it |
| `get_module_info(moduleId)` | Returns `{name, icon}` for a module |
| `moduleEntryPoint(moduleId)` | An enabled module's QML entry point (`startupModuleEntryPoint()` is the startup module's) |
| `get_module_settings_schema(moduleId)` | Returns the module's settings array |
| `invoke_module_action(moduleId, slotName)` | Routes to the registered backend via `QMetaObject::invokeMethod` |
| `get_module_auth_state(moduleId)` | Returns the module's auth state (for `requires_auth` settings) |
| `getCustomColorScheme()` | Returns the user's custom color scheme |
| `themes()` / `theme(id)` | Settings → Theme: the themes there are (`[{ id, name }]`: the app's own in `assets/themes` and the data folder's `themes`, one there in place of the app's of the same folder name), and one read for QML (`root.theme`): its colours, skin, effects and music (see [Themes](#themes-settings--theme)) |
| `skins()` / `skin(id)` | Settings → Skin: the skins there are (`[{ id, name }]`, found as themes are, in `assets/skins` and the data folder's `skins`), and one read for QML (`root.skin`): a picture of each of the window's parts it dresses, and its icons (see [Skins](#skins-settings--skin)) |
| `effectShader(name)` | A shader built into the app as a URL (`"effects"`: `qrc:/shaders/effects.frag.qsb`), `""` in a build without it |
| `filePlaces()` / `folderEntries(path, types)` | The file picker's places (home, `/media`, `/run/media/<user>`, `/Volumes`, the root, those there are) and a folder's entries: its folders, then its files of those types, hidden ones left out |
| `licenseText()` | The licence's text (`LICENSE` next to the app), its paragraphs each on one line, for Settings → About |

### Signals

`modulesLoaded`, `appSettingChanged`, `moduleSettingChanged(moduleId, key, value)`, `dynamicOptionsReady(moduleId, key, options)`, `moduleAuthStateChanged(moduleId)`.

### registerModule — wiring a backend in

Backends are wired in from `main.cpp` with a single call:

```cpp
YourBackend yourBackend(appRoot, dataRoot);   // construct with whatever args the ctor needs

appCore.registerModule("com.osdos.<name>", "yourBackend", &yourBackend, ctx);
```

`registerModule(moduleId, contextProperty, backend, ctx)` does everything: it stores the backend for `invoke_module_action` routing, exposes it to QML under `contextProperty`, and connects the backend's optional signals/slots **by introspection** — each is wired only if the backend actually declares it, so there are no per-capability lambdas:

| Backend member (if declared) | Auto-connected to |
|---|---|
| signal `dynamicOptionsReady(QString, QVariant)` | re-emitted as `appCore.dynamicOptionsReady(moduleId, key, options)` |
| signal `authStateChanged()` | re-emitted as `appCore.moduleAuthStateChanged(moduleId)` |
| slot `onSettingChanged(QString, QString, QVariant)` | `appCore.moduleSettingChanged(moduleId, key, value)` |

The module ID lives in exactly one place per module — this call. Declare these members with the exact signatures above and `registerModule` wires them with no other changes to `main.cpp`.

#### Probed, not connected

Two further capabilities are **probed on demand** rather than connected at registration — `AppCore` checks `metaObject()->indexOfMethod(...)` and calls the method with `QMetaObject::invokeMethod` only if the backend declares it. Same idea, but they return a value, so there's nothing to connect:

| Backend member (if declared) | Used by |
|---|---|
| `Q_INVOKABLE QString get_auth_state()` | `appCore.get_module_auth_state(moduleId)` — drives the `requires_auth` setting gate |
| `Q_INVOKABLE QVariantList get_menu_entries()` | `scan_for_modules()` — lets a backend add its own rows to the **main menu** |

`get_menu_entries()` returns a list of `{name, params}`. `AppCore` fills in `entry_point` from the module's manifest and appends the rows to the `modulesLoaded` payload, so `views/ModuleList.qml` renders them like any other row and forwards `params` as `navParams` then the module's `Root.qml` router interprets them.

Rows are appended **after** all module rows on purpose: module row indices then stay stable, so a saved menu position still restores onto the same row when a contributed row appears or disappears. The scripts module uses this to list `favorite = yes` scripts after native OSD/OS modules.

## Playback Hand-off (MpvController)

The current MPV implementation is a good reference implementation of the "browse & hand-off" philosophy. When a module decides to play a video, it hands off to **mpv** rather than rendering video itself. All of that lives in `MpvController` (`src/player/MpvController.h/.cpp`), exposed to QML as the context property **`mpvController`**.

### How the hand-off works

1. **Launch** — `loadAndPlay(url, startSeconds, audioTrack, subTrack, ...)` starts mpv as a `QProcess`. Playback parameters are passed as mpv command-line flags: `--start=<sec>` (resume offset), `--playlist-start=<n>`, `--loop-playlist=inf`, and so on; what plays comes last, behind a `--`, so a name beginning with `-` is never taken for an option. The mpv it starts is the one next to the app's own binary if there is one (a bundle's), else the first on `PATH` (`src/util/MpvLocator`); the app never links libmpv (Transparent Background opens it at run time, see [below](#transparent-background-video-inside-the-app)). Whatever played until then is told to quit (SIGTERM, and a kill a second on if it is still there) without the app waiting for it: the new session starts once that process has gone and the screen is free (`screenBusy()`), and no sooner than a tick (50 ms) later, so that the player's loading screen is drawn first: starting mpv runs synchronously and, on the Pi, switches the VT at once, suspending Qt's render thread before the frame can paint. A `stop()` before then cancels the start and ends it as stopped where it was to begin, once the old process has gone; another `loadAndPlay()` supersedes it, the stop then not reported. A retired process that outlives even its kill (stuck in a driver, or on a network mount gone away) is given up on after 5 s, so that the menus come back. A player calls `loadAndPlay()` and is done; it never defers the call itself.
2. **Control channel** — mpv is started with `--input-ipc-server=<socket>` (a Unix domain socket at `/tmp/osdos-mpv.sock`). `MpvController` connects to it with a `QLocalSocket` and sends JSON commands via `sendCommand(QJsonArray)`. `seekTo()` and `sendKey()` (which sends mpv a `keypress` command) go over this channel — that's how the USB remote / keyboard drives mpv's OSC while it's fullscreen.
3. **State back to QML** — `MpvController` issues `observe_property` for `time-pos`, `duration`, and `playlist-pos`, and re-publishes them as `Q_PROPERTY`s + the `positionChanged` / `durationChanged` / `playlistPosChanged` signals. A watchdog timer, every 10 s, logs a warning once no `time-pos` event has arrived for 30 s while mpv is connected and not paused (freeze detection).
4. **Exit** — when mpv quits, `MpvController` emits a single signal, **`playbackEnded(finalPos, finalDur, reason)`**, where `reason` is one of:
    - `"eof"` — the file played to its natural end. What happens next is the module's call: most just return to the menu.  For example: Plex may autoplay the next episode (based on the user's autoplay setting, and fall back to a normal return when there is no next episode, e.g. a movie or the last episode of a season).
    - `"stopped"` — the user quit/stopped before the end (also the safe default for a crash/kill with no end-file event). Record the resume position and return.
    - `"failed"` — mpv exited with code 2 (file couldn't be played). A module may attempt recovery first.  For example: Plex retries with transcoding — otherwise it just returns.
    - `"menu"` — the process ended for its player's own menu: back, for a player whose session note says `menu: true` (see [Its player's menu](#transparent-background-video-inside-the-app)). The player saves where it got to as for `"stopped"`, opens its menu instead of going back, and starts the video again where it was as the menu closes.

    On headless Linux it comes once the screen is the app's again (`handBackScreen()`: DisplayHandoff's release, 200 ms after mpv exits, for the vc4 driver's last commit), and a session asked for meanwhile waits for that release rather than have it take the screen from it. An end that comes after another session was asked for isn't reported: it would read as the new one's.

    **The baseline for every module to keep in mind:** by the time `playbackEnded` fires, mpv has already exited, so a handler that returns without either calling `goBack()` or starting fresh playback (`loadAndPlay`, e.g. in an autoplay/retry scenario) will leave the now-defunct Player view focused over a dead subprocess which will cause the app to freeze. So please handle the one signal, then branch on `reason` only where you have special behavior, and make sure no branch falls through.

    With Transparent Background on, back from a video fires `playbackEnded(…, "stopped")` while the picture goes on behind the menus (see below): handled as above, it returns to the menus over it. A player with a menu of its own gets `playerMenuRequested()` instead, and `playbackEnded` only once it leaves the video. Without it, back still ends the mpv process, and such a player gets `playbackEnded(…, "menu")`, with nothing playing under its menu (see [Its player's menu](#transparent-background-video-inside-the-app)).

### Per-device video decode profiles

The `--vo`/`--hwdec` flags mpv launches with are auto-selected per device to try to target hardware-decodes efficiently per device without the need for user setup. `MpvController::detectVideoProfile()` takes the board's family from `src/util/Board` (`/proc/device-tree/model`, read once; `OSDOS_BOARD_MODEL` stands in for it in tests); `appendVideoArgs()` then picks the flag set. The Pi and headless rows apply when the app runs without a display server (on the console, as the OSD/OS image runs it); there, when the launcher found the display preset's output (`OSDOS_DRM_DEVICE`, `OSDOS_DRM_CONNECTOR`, `OSDOS_DRM_MODE`), mpv is also given `--drm-device`, `--drm-connector` and `--drm-mode` for it. The embedded player (Transparent Background) has decoder lists of its own (`appendEmbeddedVideoArgs()`).

| Target | Boot driver | Video flags |
|---|---|---|
| Pi 4B | Fake KMS (`vc4-fkms-v3d`) | `--vo=drm --hwdec=drm-copy,v4l2m2m-copy` |
| Pi 3B / 3B+ | Fake KMS (`vc4-fkms-v3d`) | `--vo=gpu --gpu-context=drm --hwdec=v4l2m2m`; with Settings → **1080p Playback** → Off, `--vo=drm --hwdec=v4l2m2m-copy` |
| Pi 5 | Full KMS (`vc4-kms-v3d`) | `--vo=drm --hwdec=auto-safe` |
| Unknown headless Linux | — | `--vo=drm --hwdec=auto-safe` (a safe fallback for now - will research this more later) |
| Linux under a desktop (X11, Wayland) | — | `--hwdec=vaapi,nvdec,vaapi-copy,nvdec-copy,no` |
| macOS (Apple Silicon) | — | `--hwdec=videotoolbox` |

The key levers are which decoder and which DRM plane the frames land on:

- **Pi 4** 
    - H264 - in my testing I found that the Pi4 has the CPU headroom to implement `-copy` + software-downscale cost (~50–70% across four cores) in exchange for the primary-plane path with working crop (`--panscan`), so it uses native `--vo=drm` + hardware decode.
    - HEVC — `v4l2m2m-copy` can't reach the Pi 4's HEVC decoder (rpivid is a stateless V4L2-request device, not the stateful `hevc_v4l2m2m` wrapper that `v4l2m2m` drives), so `drm-copy` comes first in the list: it decodes HEVC on rpivid and, being a `-copy` decoder, still puts the frames on the primary plane. H.264 falls through to `v4l2m2m-copy`.
    - I tried a bunch of other paths just to be safe... `auto`/`auto-copy` excludes `v4l2m2m` entirely (so they'd drop H.264 to software too), the Pi5's Vulkan path is unavailable here (the Pi4's V3D 4.2 GPU looks ot lack `VK_KHR_video_decode_queue`), and `--hwdec=drm` without `-copy` hands its frames to the overlay plane, which judders. So the list is `drm-copy` for HEVC, then `v4l2m2m-copy` for H.264.
- **Pi 3**
    - H264 - the copy path I am using on the pi4 sadly pegs all four cores and goes choppy on the pi3. So I chose to take lowest-CPU path with zero-copy (e.g. `v4l2m2m` straight to the overlay plane).  
    - That gives around ~15% CPU, smooth playback, with the single trade-off that crop (`--panscan`) is unavailable with this set up.  I figured that was an acceptable tradeoff for supporting 1080p video but Settings → **1080p Playback** → Off (`smooth_playback`, offered on a Pi 3 only) switches to `--vo=drm --hwdec=v4l2m2m-copy`, which crops but stutters on 1080p, so it suits 720p content. (The `mpv_video_args` override changes the flags, but not whether the app offers crop: that follows the setting.)
- **Pi 5** 
    - boots Full KMS, so plain `--vo=drm` direct-renders. when testing `auto-safe` I found no working VA-API/V4L2 path (the V3D VA-API driver fails to open) and it selects FFmpeg's Vulkan video decoder (`vulkan-copy`) on the V3D GPU for both H264 and HEVC. HEVC reaches the Pi5's hardware HEVC block this way — ~15% for 1080p, ~45% for 4K; H.264 goes through the same Vulkan path and stays light (~20–27% for 1080p). Because it's a `-copy` decoder the frames land on the primary draw plane, so I found this path is smooth and supports crop.

Advanced users can override the auto-detected flags with the app-level `mpv_video_args` setting in `config.json` (a space-separated flag string under `"app"`); it is read at each launch, so changes apply on the next playback without a rebuild — useful for on-hardware tuning.

### How mpv flags are layered (the precedence cascade)

Every flag mpv receives belongs to one of a few layers, and the model that keeps them straight is a single precedence cascade where each layer can only override what the layers above it didn't nail down:

I think of it like this:
```
app constants → 
  app per-playback → 
    app presentation (user-set in Settings) → 
      device decode (user-overridable in config) → 
        ~/.config/mpv/mpv.conf
```

| Layer | Examples | Owner | Where |
|---|---|---|---|
| **App constants** | `--input-ipc-server`, `--input-conf`, `--osc`, `--script`, `--log-file`, `--no-input-terminal` | App only | command-line |
| **App per-playback** | `--start`, `--aid`, `--sub-file`, `--http-header-fields` (stream URL, tokens) | App only | command-line |
| **App presentation** | `--panscan` / `--keepaspect=no` (Scaling), `--video-output-levels` (Video Levels), `--audio-device` (Audio Output) | User, via a Settings row; Scaling also per module | command-line |
| **Device decode** | `--vo` / `--gpu-context` / `--hwdec` | App auto-detects; user may override via `mpv_video_args` | command-line |
| **User prefs** | `deinterlace`, `cache`, `sub-scale`, `audio-device`, profiles | User | `mpv.conf` |

- The first four layers are app-owned and the first two are load-bearing because they wire the IPC control channel, the input/OSC bridge, and (headless) the DRM/VT hand-off. Changing them would break functionality in the app, not just playback, so they are never user-overridable. The last two app layers are the ones the user can steer: *app presentation* through a Settings row (Scaling, Video Levels, Audio Output) for the knobs worth reaching without a keyboard, and *device decode* through the `mpv_video_args` override if per device tweaks are needed.
- A presentation setting left at its default emits **no flag at all** (Video Levels on `Auto`, Scaling on `Letterbox`, Audio Output on `Auto`), so a `video-output-levels=` or `audio-device=` line in someone's `mpv.conf` still applies; picking Limited/Full, or a sound card, puts it on the command line, where it wins.
- **Scaling** is how a picture of another shape fills the screen, a 16:9 film on a 4:3 tube above all: `Letterbox` (all of it, bars above and below), `14:9` (`--panscan=0.43`: a little of the sides cut, thinner bars), `Pan & Scan` (`--panscan=1`: fills the screen, the sides cut) and `Anamorphic` (`--keepaspect=no`: fills it squeezed, for a TV switched to 16:9). It is the app's `video_scaling`, unless the playing module's own `video_scaling` (every video module's manifest has one, `Default` first) says otherwise: `MpvController::videoScaling()`, with the module from `setActiveModule()`, which Main.qml calls as its module loader changes (`AppCore::moduleIdForSource`). The older `auto_crop` `On` reads as Pan & Scan until a Scaling is chosen. The cropping modes need panscan, so the Pi 3 overlay path keeps the whole picture. During playback the deck's menu (`scripts/mpv-osc.lua`, and Ambient Mode's) steps its CROP button through the same four, live, from the one mpv reads as in force (`panscan`, `keepaspect`), and shows it as `CROP: 14:9` under the track lines. The web players take the same setting to `web-player.sh`, which applies it with a small Chromium extension: a stylesheet transforming the player's `<video>` (8/7 larger, 4/3 larger, or 4/3 taller).
- And all app layers are command-line, so they all win over `mpv.conf`. I do pass no `--no-config`, so mpv will look to read `~/.config/mpv/mpv.conf` on launch, which means users can add anything the app doesn't set explicitly direclty in their MPV config.

### Custom OSC (Lua)

The on-screen controls mpv shows during playback are custom Lua scripts in `scripts/` (`mpv-osc.lua` for normal playback, `mpv-osc-ambient.lua` for Ambient Mode), loaded via mpv's `--script=` flag. Options are passed in with `--script-opts=` (e.g. `transcode-offset=<sec>`). The remote's key events reach these scripts through the `keypress` IPC bridge described above.

### The channel logo (`scripts/mpv-logo.lua`)

Settings → **Channel Logo** (`app.video_logo`: `"tl"`, `"tr"` (the default, when unset), `"bl"`, `"br"`, `"all"` for one in each corner, or `"off"`) puts OSD/OS's logo in a corner of the picture while a video plays, the way a channel's sits in a broadcast. mpv draws it itself: `sessionArgs()` loads `scripts/mpv-logo.lua` with `logo-corner=<corner>` among the script options, so it is there in both modes (the embedded session takes the same `--script`), and it is in the picture, under whatever the app draws over it (the menus over a video behind them, with Transparent Background). The script redraws `assets/images/logo-bug.svg` as ASS vector shapes (`mp.assdraw`, one event per colour) on `mp.set_osd_ass` at the output's size: 7% of the output's height tall, 10% of the picture's width and height in from the picture's corner (`osd-dimensions`, its margins taken off, so a letterboxed picture's bars stay clear of it), and again whenever the output's size changes. Like every setting mpv is launched with, it applies from the next video.

Settings → **Logo Image** (`app.video_logo_image`, a picture's path; OSD/OS's logo when unset), offered while Channel Logo isn't Off, puts a picture of the user's own there instead, picked on the [FilePicker](#filepicker-viewsfilepickerqml) (PNG, JPEG, SVG, GIF, BMP or WebP). `sessionArgs()` reads it at the logo's height (7% of the screen's) into raw premultiplied BGRA (`osdos-logo.bgra` in the temp folder), mpv's overlay format, and hands the script `logo-image`, `logo-width` and `logo-height`; the script lays it in each corner with `overlay-add` (ids 0 to 3), placed as the drawn logo is. It is made at the size it is shown because mpv before 0.38 can't scale an overlay. A picture that can't be read leaves OSD/OS's logo, with a line in the log.

### Transparent Background: video inside the app

With Settings → **Transparent Background** (`app.transparent_background`: how solid the menus' ground is over the picture, `0` to `100`, or `Off`, the default), `loadAndPlay()` plays the video inside the app's own window instead of starting an mpv process over it, so the menus can be drawn over the picture. Back from a video then returns to the menus and leaves it playing behind them, the way a deck's menu lies over the tape.

- **`EmbeddedMpv`** (`src/player/EmbeddedMpv.h/.cpp`) is mpv as a library. libmpv is opened at run time with `QLibrary` (`libmpv.so.2`, or `libmpv.2.dylib` in Homebrew's prefixes), never linked, so the app runs where it is missing; `mpvController.embeddedAvailable()` says whether it loaded, and Settings offers the row only then. Its headers are optional at build time (`pkg-config mpv`, so `libmpv-dev` / Homebrew's mpv); without them `OSDOS_EMBEDDED_MPV` is left undefined and it is never available.
- **The same session.** `sessionArgs()` builds the command line the subprocess gets, and `EmbeddedMpv::start()` turns it into options and a playlist: `--x=y` is option `x`, a repeated list option (`--script`, `--sub-file`, `--http-header-fields`) is gathered and set whole as a node array, so an item keeps any character, and an option this libmpv doesn't know is skipped with a warning where the mpv command line would refuse to start. On top: `vo=libmpv`, `idle=once` (it quits when its playlist has played out, as the process exits), `input-default-bindings=yes` (libmpv leaves them off). Decoding follows where the picture is drawn (below): on the GPU each decoder hands its frames over as they are, ahead of its copy-back mode (Pi 4 `drm,v4l2m2m,drm-copy,v4l2m2m-copy`, Pi 3 `v4l2m2m,v4l2m2m-copy`, desktop Linux `nvdec,vaapi-copy,nvdec-copy,no`, VA-API only as a copy since its frames would need the window system's display; a Pi 5 keeps `auto-copy-safe`, as FFmpeg's Vulkan decoder that it picks there hands mpv's OpenGL renderer only copies), for the software renderer the copy-back modes alone (Pi 4 `drm-copy,v4l2m2m-copy`, Pi 3 `v4l2m2m-copy`, Pi 5 `auto-copy-safe`, desktop Linux `vaapi-copy,nvdec-copy,no`, macOS `videotoolbox-copy`). Should the GPU not take a decoder's frames (`Mapping hardware decoded surface failed`: mpv then draws nothing, and doesn't fall back by itself), `EmbeddedMpv` sets the session's `hwdec` to the copy-back modes in its list, and the sessions after it start with them. The `mpv_video_args` override is not used, and libmpv reads no `mpv.conf`. The IPC socket, the OSC scripts and every signal work as for the process; there is no `DisplayHandoff`, since the app keeps the screen.
- **The picture** is drawn at the pixel size of the **`VideoSurface`** item (`src/player/VideoSurface.h/.cpp`, `import OSDOS.Video`) that shows it, on the GPU wherever Qt Quick draws with OpenGL (eglfs on a Pi, desktop Linux). `main.cpp` sets `Qt::AA_ShareOpenGLContexts`, so a context `EmbeddedMpv` makes shares its objects with the scene graph's; it is current on a thread of its own (`mpv-gpu`), where libmpv's OpenGL renderer (`MPV_RENDER_API_TYPE_OPENGL`) draws each picture into one of four textures: never one the scene graph may still be showing (the newest and the two it took last), and finished (`glFinish`) before it is handed over. `VideoSurface` shows that texture as it is (`QNativeInterface::QSGOpenGLTexture::fromNative`), so no picture passes through the CPU: the GPU converts and scales it, and a hardware decoder's frames reach it without a copy (`drm`, `v4l2m2m`, NVDEC). mpv's passes between render into 8-bit textures (`--fbo-format=rgba8`, half the memory traffic of its half floats). On a Pi it enlarges with mpv's `fast` profile (bilinear), but shrinks with hermite widened to the scale (`--dscale=hermite --correct-downscaling=yes`, mpv's own default), so a 1080p or 4K picture brought down to a CRT's lines doesn't shimmer, and dithers (`--dither=fruit`) as the software renderer does. The textures stay from one session to the next.
- **On the CPU** otherwise: libmpv's software renderer (`MPV_RENDER_API_TYPE_SW`), on a thread of its own (`mpv-render`), the same colour conversion and scaling `--vo=drm` does on a Pi 4, into memory that `VideoSurface` uploads each picture from. It works on every scene graph backend: Metal on macOS, `QT_QUICK_BACKEND=software`, and OpenGL drawn on the CPU (Mesa's llvmpipe, in a VM say), where the GPU path gains nothing and mpv draws some of its passes wrong. On a Pi 3 the CPU is short for it. `EmbeddedMpv::gpuAvailable()` decides once, before the first session, so that its command line is made for where it is drawn; should the GPU then fail to take a session, that one and the rest are drawn on the CPU. The log says which (`[EmbeddedMpv] pictures drawn on the GPU: <renderer>`). `OSDOS_EMBEDDED_RENDER=sw` keeps to the CPU, and `=gpu` takes OpenGL drawn on the CPU too, for tests.
- **Back** is mapped by the session's input.conf to `script-message osdos-menu` (the OSC's own menu, while open, still takes it first). `detachToMenus()` then emits `playbackEnded(pos, dur, "stopped")`, unless its player has a menu of its own (below): the module saves where it got to, reports it stopped, and goes back, while the session goes on (`mpvController.background`). Its position is followed but no longer reported. `Main.qml` puts the `VideoSurface` over everything while a session plays full screen and under the views while `background` (`root.videoBehind`); the views draw no background of their own, so the picture is theirs, and their ground (`OsdGround`, in OSD BACKGROUND's shape: all over, or only its window, the picture showing whole around it) lies between them at the setting's solidity (`root.backdropSolidity`). Full-screen dialogs keep their own ground, and the title bar's logo gets a box of it.
- **The setting** is a slider in Settings: its line, `ON` or `OFF`, with the deck's tape bar (`OsdTapeBar`) under it from TRANSPARENT to SOLID, which ◄ ► move by 10 and save at once, so the menus over a video show each step. At SOLID none of the picture shows, but the video plays on, sound and all. Select turns the setting off (`Off`, the bar's lines left empty) and back on at the bar's value, as it does a module's toggle; ◄ ► turn it on too. The bar takes whole lines under its own, so the list scrolls by lines as before. Its first values, `On` and `Dim`, read as `0` and `60`. `100` was off until SOLID kept the video playing, and now reads as SOLID. `Main.qml`'s `backgroundOn()` and `MpvController::transparentBackground()` read the value the same way.
- **Chosen again**, the session is not reloaded: `takeBack(startSeconds)` brings it back full screen (`false` when there is none, and the player starts its video as it would any), as does a `loadAndPlay()` with the same command line apart from `--start`. A module resuming at the point it saved when back left it carries on where the picture is now; any other start (from the beginning, say) is sought.
- **Back to it from the main menu.** Right after `loadAndPlay()` a player notes the session: `noteSession({ module, title, params })`, `params` being its own `navParams`. Every `loadAndPlay()` clears the note, so a player that notes nothing leaves none. While the session plays behind the menus, `backgroundNote` (`root.behindNote`) holds it, and the main menu (`views/ModuleList.qml`) leads with a row for it, `► <title>`, the cursor on it; it does for a session left with its menu's Browse without Transparent Background too (`leftNote`, below), `root.takeBackNote` being whichever of the two there is. Select opens the module with `{ resumePlayer: params }`, and its tree opens the player with them as if chosen from RECENTLY WATCHED. A player that finds its own video in `root.takeBackNote` skips its resume prompt and plays from the point back saved, so the same video chosen in the tree comes back at once too. YouTube (by `videoId`) and Local Files (by path, with the `plPos` and `shuffle` it was started with, so the command line matches) note their sessions; Playlists notes what it was started with and takes the session back outright (`takeBack`). The other players don't yet, so their videos come back only by choosing them again.
- **Its player's menu.** A player whose note says `menu: true` keeps the session when back is pressed: `detachToMenus()` emits `playerMenuRequested()` instead of `playbackEnded`, and the player opens its menu over the picture (`PlayerMenu`, see Components), its position still reported. Back from the menu calls `closePlayerMenu()`, and the picture is full screen again. A setting changed there is saved at once; the player puts what it can on the session as it plays with `setVideoProperty(name, value)` (`speed`, `loop-playlist`, a Scaling's `keepaspect` and `panscan`), and reloads it where it is for the rest as the menu closes. **Browse** calls `leavePlayerMenu()`: `playbackEnded(…, "stopped")` follows, as from back without a menu, and the player returns to its module's menus over the picture. **Close Video** is `stop()`, and the player, at its `playbackEnded`, leaves the module for the main menu (`moduleRoot.goBack()`). If something else takes the screen while the menu is open, the player gets `playbackEnded(…, "stopped")` too. YouTube, Local Files and Playlists have one.
- **Its player's menu without Transparent Background.** An mpv process has the screen while it plays, so nothing of the app's can lie over the picture. The process's input.conf maps back to `script-message osdos-menu` too, and for a session whose note says `menu: true`, `backFromProcess()` ends the process. Once the screen is the app's again (after `DisplayHandoff`'s release), `playbackEnded(…, "menu")` comes, and `videoActive` is false: the player saves where it got to, opens its menu on the app's own background, and as it closes starts the video again from there, with the settings as they are now, so every change in the menu applies then. Nothing plays under the menu, so **Browse** and **Close Video** are the player's own: it goes back to its module's menus, or leaves the module, itself (it calls `leavePlayerMenu()` and `stop()` only while `videoActive`). Browse first calls `leaveSession()`, which keeps the session's note as `leftNote` (`root.leftNote`) until the next `loadAndPlay()`: the main menu leads with its row as for a video behind the menus (`root.takeBackNote` is the one or the other), and its player, opened with its `params`, starts it where it was saved, without asking: a playlist at the video it was on, a shuffled one, whose places went with its order, shuffled afresh. Any other player's back quits the process, as it always has. A video that ends by itself while back is on its way ends as usual.
- **It ends** when its playlist plays out, with play/pause on the main menu (`stopBackground()`, `[SPACE]:STOP` in its footer), with any other playback, when something else takes the screen (`DisplayHandoff::handingOff`, emitted at every `acquire()`: a takeover script, a web player) and when the setting is turned off. Behind the menus no `playbackEnded` follows: its module took it as stopped already (under its player's menu, it does). A server told the stream stopped (a Plex, Jellyfin or Emby transcode) may end it sooner.

### Raspberry Pi headless hand-off (EGLFS)

On RPi Lite there is no display server; Qt draws via EGLFS straight to the KMS/DRM framebuffer, so the app and a fullscreen child can't both own the screen at once. **`DisplayHandoff`** (`src/util/DisplayHandoff.h/.cpp`) owns this hand-off for the whole app. `MpvController` delegates to it and does not implement any of the ioctls itself.

The order is load-bearing and was established against real Pi hardware — read the header comment before touching it:

- **`acquire(owner)`**: VT switch → `drmDropMaster` → save CRTC state. The VT switch goes *first* because it suspends Qt's render thread via the kernel's VT-switch signal before master is dropped; on kernels 5.8+ `drmSetMaster()` returns `EACCES` for non-root while another process holds master, and Qt EGLFS runs `VT_AUTO` and never drops master itself.
- **`releaseDeferred(owner, cb)`**: after 200 ms (>3 VSync at 60 Hz, so the child's last pending KMS commit can clear), `drmSetMaster` → restore CRTC → switch back, then run `cb`. The restore uses **legacy** `drmModeSetCrtc`, not an atomic commit: the child's atomic cleanup leaves `CRTC_ACTIVE=0` and EGLFS would get `EINVAL` on its first page flip.
- **`releaseNow(owner)`**: synchronous, for shutdown; `MpvController`'s destructor calls it so quitting mid-playback no longer leaves the Pi on a blank VT.

The `owner` token means two subsystems can never both believe they hold the screen — `acquire()` refuses if someone else holds it, and `isHeldBy()` is the re-entrancy guard for relaunching a child without releasing first. Its `held` property (`heldChanged`) is true from a successful `acquire()` until the restore; it is the context property **`displayHandoff`**, read in views as `root.screenHandedOff` (Main.qml), so that an animation can rest while nothing it draws reaches the screen (LoadingScreen does). All of it is Linux-only in effect (`isHeadless()` is false on macOS and whenever a compositor is present), where the hand-off is just a fullscreen window swap.

Two consequences that are easy to get wrong, both of them Pi-only and both invisible on any other target:

- **Whatever Qt last put on the glass stays there for the whole hand-off.** The VT switch suspends Qt's renderer, but nothing clears the framebuffer, and we no longer hold DRM master so we cannot. A child that draws immediately (mpv) hides this completely; a child that draws late or never leaves the previous frame frozen on screen, which reads as a hang rather than a hand-off. So **paint the screen you want frozen, wait for it to be presented, and only then call `acquire()`**. As an example `modules/scripts/views/Takeover.qml` does this with a short timer, deliberately not `Qt.callLater`, because what matters is a frame actually presented instead of the scene graph being updated.
- **The VT we switch to must not be the one we are on.** `findFreeVt()` starts from `VT_OPENQRY`, which reports the lowest VT the kernel considers unused. Activating the VT we are already on would be a silent no-op so Qt never suspends, and master is then dropped out from under a still-drawing Qt, with nothing logged because the ioctl succeeds. `findFreeVt()` takes the active VT and steps past it so that cannot happen.

  In practice `VT_OPENQRY` does not return Qt's own VT, because Qt EGLFS opens `/dev/tty0` for its `KD_GRAPHICS`/`VT_AUTO` handling and `/dev/tty0` *is* the foreground console, which pins that VT's tty count. Measured on an installed card: idle `tty1`, during playback `tty2`, back to `tty1` on exit.

  Note that `VT_OPENQRY`'s notion of "in use" is the *virtual console's* tty count, not "some process has `/dev/ttyN` open". `fuser -v /dev/tty1` comes back empty under the service and yet VT 1 is in use, because the process pinning it opened `/dev/tty0`. `fuser` on the numbered node is the wrong instrument here; read `/sys/class/tty/tty0/active` instead.

On a dev box neither of these bites the same way, because `autovt@` is unmasked there: a getty spawns on the VT we switch to, repaints the console for us, and holds that VT open so `VT_OPENQRY` keeps moving up. The login prompt you see mid-hand-off on a dev Pi is that getty, not anything OSD/OS drew.

### Adding a different hand-off target

The longer-term vision is to hand off to *other* purpose-built tools (e.g. RetroArch), not just mpv. `MpvController` is the template: launch the external tool as a `QProcess`, drive it over whatever control channel it offers, and surface progress/exit back to QML via signals. **Use `DisplayHandoff` for the screen — do not re-implement the VT/DRM ioctls in a new caller.**

The **scripts module** (`modules/scripts/`, `src/modules/scripts/`) is the second worked example, and generalises the idea to arbitrary user programs. Its `ScriptLauncher` has things that `MpvController` doesn't need:

- **Two run modes per target.** `console` keeps OSD/OS on screen and streams the child's merged output into a QML view; `takeover` gives the child the display. The split is per-target. On macOS / desktop Linux / SteamOS a takeover needs nothing at all (the child's window covers ours), and only headless Linux needs the `DisplayHandoff` bracket.
- **Nothing is handed over before a spawn that might still be refused.** All validation happens first, `QProcess::errorOccurred(FailedToStart)` is handled explicitly (`finished` is *never* emitted in that case), and a started-watchdog covers "started but silent". 
- **`setsid()` in a child-process modifier**, so the child leads its own process group: `killpg` reaches everything it spawned, and an empty group is how you know the screen is free again. A launcher script that backgrounds its real work and exits immediately would otherwise have the display taken back out from under its children.
- **Report only after the display is restored.** The caller pops its view on the "finished" signal; doing that while the framebuffer still belongs to the child draws into memory you don't own.
- **No stop key during a takeover.** A takeover child should own input for it's whole run, and on EGLFS every keystroke is double-delivered (Qt's libinput and the child both read the same evdev devices) so any tap-to-stop key would also fire inside inside a launched takeover application (For example ESC/Back is used by RetroArch's to navigate its menus just like its used inside OSD/OS so pressing that key while RA is open would SIGTERM the session mid-run). With this in mind, the runner view is set up to swallow Back events while a takeover is busy and offers no direct stop key. What covers failures instead: the started-watchdog and `FailedToStart` handling, the downgrade-to-console refusal when display state can't be saved, and `~ScriptLauncher`'s SIGTERM → SIGKILL + `releaseNow()` at app quit. Console mode and downgraded runs (where OSD/OS kept the screen) still have the Back-to-stop key with `requestStop()`'s SIGTERM → SIGKILL escalation.

The **web player modules**, Netflix and Prime Video (`modules/netflix/`, `modules/prime_video/`, `src/modules/web_player/`), reuse `ScriptLauncher` rather than growing a third launcher. `WebPlayerBackend` runs the bundled `scripts/web-player.sh <service> <url>` as a takeover through its own instance, named after the service for `DisplayHandoff` with `setHandoffOwner()`; `main.cpp` makes one per service. The script opens the service's web player in Chromium (`--kiosk`, a profile per service), under the `cage` Wayland kiosk compositor when there is no desktop; cage opens the display and input devices itself (libseat's `noop` backend), since the app holds no login seat to share. Each module has two views, both shared components: `WebPlayerBrowse` lists the service's catalogue in a `TreeBrowser`, and `WebPlayerLaunch` opens what was chosen straight away and waits for the browser to close. Only a service's first launch in a run waits 1.2 s first, to show how to come back, since a headless Pi's screen is dark while the browser starts. Unlike a script takeover it does have a way out: holding Back for two seconds closes the browser. A browser has no use for a held Back, so the double-delivered key can't misfire the way a tap would in RetroArch. Only Escape and Qt's Back key count (a gamepad's Back arrives as Escape), not Backspace, which the browser's text fields need, and the view leaves once Back is let go, so its auto-repeat can't carry on back through the menus. While the browser is open the view focuses a hidden `TextInput`, so `InputManager` treats the keyboard as typing: Right Shift stays Shift instead of standing in for Back (held for an "@", it closed the browser), and remote remaps don't fire. An open browser also counts as activity, so no screen saver comes up behind it.

The sign-in lives in the browser's profile for the service (`<data>/<service>/browser`), as in any browser. **SIGN IN** in a module's settings (a `module_view` row) opens `WebPlayerLaunch` with `navParams.signIn`: it says how signing in goes and, on Select, opens the service's `signInUrl` (Netflix's login page, Prime Video's `auth-redirect`, which goes on to Amazon's sign-in for the region). **Sign out** (`signOut()`) deletes the profile. Closing has to keep a sign-in: Chromium writes new cookies to disk only every 30 s, and a SIGTERM loses what it hasn't written, so `close()` (the held Back) first runs `web-player.sh --close`, which types Ctrl+W into cage through its virtual keyboard (`wtype`; cage 0.1.5 or later) and Chromium shuts down as it does when the user closes it. Where that can't be done (no `wtype`, an older cage, a desktop session, where the browser has the keyboard anyway), or the browser is still open 5 s later, the run is stopped (`ScriptLauncher::requestStop`).

YouTube's sign-in is a `WebPlayerBackend` too (`YouTubeBackend::browser`), with no catalogue, Google's sign-in as its `signInUrl` and `needsChromium`, which on a Mac wants Google Chrome rather than falling back to Safari, whose sign-in yt-dlp couldn't read. Once its profile has a cookie store, every yt-dlp the backend runs gets `--cookies-from-browser chromium+basictext:<profile>` (`chrome:<profile>` on a Mac; `basictext` because `web-player.sh` runs Chromium with `--password-store=basic`), and so does mpv's ytdl hook, in `playbackArgs()`' one `--ytdl-raw-options` list with the path in mpv's `%bytes%` quoting. yt-dlp reads the profile afresh each run, so no exported copy can go stale when YouTube rotates the cookies. Using an account through yt-dlp can get it blocked, which the row's help line and the sign-in screen say.

The catalogue is `TmdbCatalog`, one per backend (its `catalog` property). Neither service has an API a front end could browse with, so the titles come from TMDB's v3 API: `/discover/{movie,tv}` filtered to the service (`with_watch_providers`, its TMDB provider id, looked up by name per region) and to the subscription (`with_watch_monetization_types=flatrate`), by popularity and by genre, a page at a time; search is `/search/multi`, kept to the matches whose `/{type}/{id}/watch/providers` lists the service in the region. A title opens at the service's own page for it when Wikidata holds the service's id (`P1874` for Netflix, `P14440` for Prime Video) next to its TMDB id (`P4947`/`P4983`) or its IMDb id (`P345`, from TMDB's `external_ids`), in one SPARQL query. Otherwise it opens at the service's search for its name. The key is read from `tmdb_api_key.txt` in the data folder (a v3 key, or a v4 token sent as a bearer); `problem` says what is in the way when there is no key or no network, for a `HelpLine`. `OSDOS_TMDB_URL` and `OSDOS_WIKIDATA_URL` point it at a local stand-in for tests.

## Card Hand-off (NFC → a module)

An NFC card's tag file can point at content another module owns, rather than at a file this module can play itself. The NFC module resolves *which* module, and that module resolves *what to play* — auth, lookup and playback stay where they already live.

**The tag file.** Line 1 is the card UID, line 2 the ref, and an optional line 3 a bare mode token (`shuffle`). Line 3 is deliberately generic rather than Plex-specific so `.m3u` and YouTube-playlist cards can use the same slot later. `parseTagFile` stopped at two lines before, so a third line is backwards-compatible meaning existing cards are unaffected.

**Routing.** `handoffModuleForRef()` in `NfcReaderBackend.cpp` maps a ref's URI scheme to a module id via `kHandoffModules`. `http`/`https` are deliberately absent as those are stream URLs this module hands straight to mpv. A recognised scheme emits `cardHandoffRequested(moduleId, ref, mode)` instead of `playbackRequested(videoPath)`; the file/stream path is untouched. If the target module is disabled the card is refused (`AppCore::is_module_enabled`).

**Navigation.** `Items.qml` resolves the target's entry point with `AppCore::module_entry_point(moduleId)` and emits the **shell-level** `navigateTo` (not the router's internal one). That is why `nfc_reader/views/Root.qml` declares `signal navigateTo(...)` and calls its own router function `navigateToView()`: `Main.qml` only listens for a signal named exactly `navigateTo` on the loaded module, so the name has to be free.

**The receiving module carries a `CardPlay.qml`.** `modules/plex/views/CardPlay.qml` is the reference. It is a thin resolver, not a view the user navigates to:

- `Root.qml` routes to it on `navParams.cardRef`, **ahead of and exclusive of the auth/user gate**. For Plex, falling through that would land a card tap on `UserSelect.qml` whenever `auto_sign_in` is off, and switching profiles from a card would sidestep the profile PIN. A missing sign-in or a pending PIN is surfaced as an error, never a prompt.
- So it resolves the ref, builds the stream, then **`replaceWith("Player.qml", …)`** — `replaceWith` doesn't push to the nav stack, so the stack stays `[NFC Root] → [Player]` and backing out of playback returns straight to the NFC tap screen instead of stranding the user inside a module so they can tap another card easily after playback stops.
- It doesn't write player state back to the service (Plex's `set_audio_stream` / `set_subtitle_stream`) — a card tap must not mutate stored per-item preferences. Whatever the server already prefers is what plays.
- Errors render in the NFC module's visual language, so a card tap looks the same whichever module ends up serving it.

**A card can also name a *set*:** Plex's `CardPlay.qml` recognises `plex://collection/…` and `plex://playlist/…` and hands those off to `QueuePlay.qml` (with `replaceWith`) rather than resolving a stream itself. Note: line 3's `shuffle` means *shuffle this queue*. The set's contents are resolved at tap time, so a card follows the collection or playlist as it changes rather than freezing whatever it held when it was originally written.

**Adding another module** (e.g. Jellyfin, Emby, …) means: a row in `kHandoffModules`, a `CardPlay.qml`, and a `cardRef` branch in that module's `Root.qml`. Nothing in the NFC module is service-specific.

### Plex specifics

- Cards store a Plex **guid** (`plex://movie/…`), never a ratingKey because guids survive library re-scans and moves between servers, which a physical card on a shelf will benefit from. Resolution is `/library/all?guid=` unscoped: it searches every section, so the card says *what* to play and the app decides *where* it lives. That query omits `Media`/`Part` unless given a `type=` filter so the chosen item is always re-fetched by ratingKey. External ids (`imdb://`, `tmdb://`) are **not** resolvable through this filter.
- Libraries on a legacy metadata agent report `com.plexapp.agents.*://…` guids. They resolve fine and are routed to Plex by prefix, but they're agent-scoped: re-agenting such a library breaks cards written against it.
- **Cards never switch server or user.** `select_server` persists config (see the settings-write rule), and auto-switching a Plex Home profile would be a PIN bypass in physical form. Wrong server / no permission / signed out are all errors.
- A **shuffle** card sets `trackProgress: false` on the Player, suppressing both `update_timeline` calls. Progress reporting is entirely client-side, so that is sufficient to leave watched state, Continue Watching and on-deck untouched.
- **Collections and playlists are the exception to the guid rule.** They are server-local, user-created objects with no metadata-agent guid to be portable with, so their cards carry the ratingKey (`plex://collection/<ratingKey>`) and `resolve_card_queue` fetches `/library/collections/<key>/items` or `/playlists/<key>/items` in one request. Such a card breaks only if the set is deleted and recreated. The rows go through `formatItem` + `flattenSeasons` exactly as the in-app loaders do, so `expand_queue` fans shows out identically either way.
- Shuffle keeps rolling via a **shuffle bag** in `PlexBackend` (`m_shuffleBag`): a shuffled permutation played to exhaustion then reshuffled, rather than independent random draws, which clump badly over the hours a jukebox card runs. `resolve_card` reports the show/season as `cardScope`; the Player's EOF branch calls `load_random_episode(scope)` instead of `load_next_episode(ratingKey)`. Both emit `nextEpisodeReady`, so the advance itself is shared. Continuation respects the module's `autoplay_next_episode` setting.

## Playlists (videos from several modules)

The Playlists module (`modules/playlists`, `PlaylistsBackend`) keeps lists of videos that other modules own, and plays a list as one. It is the one module that reaches into others: `main.cpp` hands its backend Local Files', YouTube's, and the media servers' as a map of module id to **`MediaServer`** (`src/modules/playlists/MediaServer.h`: `signedIn()`, `downloadRequest(itemId)`, `streamUrl(itemId)`, `browse(parentId, context, done)`), which `JellyfinBackend` and `EmbyBackend` implement with their own requests, item format and TLS allowances (`src/util/EmbyApi.h` builds the URLs both share); the module never minds which server it has.

**Two kinds.** An **online** playlist plays each video from where it lives: a Local Files path, a YouTube watch URL that mpv's ytdl hook opens with the YouTube module's ways (`YouTubeBackend::playbackArgs`, its ADVANCED settings, yt-dlp's path), a Jellyfin or Emby stream (`/Videos/{id}/stream?static=true`, the token in the query, so in a list mixing sources it goes to that server and nowhere else). An **offline** playlist plays only what is on the device: Local Files' files as they are, and for everything else a copy, downloaded once.

**Storage.** `<data>/playlists.json`: the playlists (`id`, `name`, `kind`, `order`, `items`, and `resume`, where it stopped) and `downloads`, by key. An item's **key** names the video whatever list it is on: `local:<path>`, `youtube:<videoId>`, `jellyfin:<itemId>`, `emby:<itemId>`. `addEntry(playlistId, moduleId, entry)` turns an entry as its module has it (a tree's entry, a server's item) into an item (`itemFor`), and refuses a second of the same key on a list.

**Downloads.** Every offline list's videos share one copy per key, in the download folder (the module's `download_folder` setting, else a `Playlists` folder in Local Files' media folder: on the OSD/OS image, the card's OSD-OS partition, mounted writable for it), under `YouTube/`, `Jellyfin/`, `Emby/`. One runs at a time: queued as a video goes on an offline list, and 15 s after start for any still missing. `enqueue()` never queues a key already downloaded, so a video on several lists is fetched once; `dropUnreferenced()` deletes a copy as soon as no offline list has it (an item removed, a list deleted). YouTube's run yt-dlp (`YouTubeBackend::downloadArgs(canMerge)`: the module's format and the account's cookies; `--merge-output-format mp4` with ffmpeg, the best progressive file without; `--windows-filenames` for exFAT; `--print after_move:filepath` names the file), in a process group of its own, so a cancelled download's ffmpeg ends with it (an interrupt, then a kill two seconds later; the app never waits on it). Jellyfin's and Emby's fetch the server's `downloadRequest()` (`/Items/{id}/Download`, the original file, which a server allows a user or not: 401/403 is `not allowed`, kept until **Retry Downloads**) through a **`ServerDownload`** (`src/modules/playlists/ServerDownload.h/.cpp`): the reply read and the file written on a thread of its own, so a server faster than the card never holds the app's thread, and a transfer that stops moving for half a minute fails rather than holding the slot. A finished file is `fsync`ed, with its folder, off the app's thread (the item still downloading, at 100%, until it is) before it counts as done; a copy deleted has its folder flushed the same way. A download that fails says why (the page's help line); a YouTube one keeps what it got, for yt-dlp to carry on from on a retry, while a server's starts again from nothing (its `.part` file is deleted); an unwritable folder fails it at once, naming the folder (a card from before the partition was writable). File names come from `safeFileName()` (`src/util/FileNames.h`, exFAT's rules, which the NFC module's tag files follow too).

**Playing.** `prepare(playlistId, fromItemId)` writes what plays into an m3u in `<data>/playlists/`, a new file each time (owner-only: it may hold a token) and the last one only, in the order it plays: a shuffled list is shuffled there rather than by mpv, so a place in the m3u always names a video (`savePosition` keeps the item, not the place), and **Play from Here** puts that video first. What can't play (a file gone, a server signed out of, a download not done) is left out. The player hands the m3u to `loadAndPlay` with its own SUBTITLES and LOOP PLAYBACK; an in-order list asks to resume where it stopped, and forgets that once it has played out (`eof`). It notes its session (`noteSession`, `menu: true`) with what `prepare()` wrote, so chosen again from the main menu while it plays behind the menus it takes the session back (`takeBack`); a list played afresh has a new m3u, so MpvController never takes it for the one behind.

**Adding videos.** From inside the module, ADD VIDEOS is a `TreeBrowser` over the sources, each browsed as its own module browses it: Local Files' and YouTube's trees come from their backends' `entries(path)` (the one place each tree's folders are built: RECENTLY WATCHED, FAVORITES and SEARCH ahead of the media folder, YouTube's home with its lists and without Shorts when DISPLAY SHORTS is off), which their own `Items.qml` use too; Jellyfin's and Emby's from `serverListing(moduleId, parentId)`, the server's `browse()` through `MediaServer`, so it never shares their views' signals. `addEntry` says whether the video is now to be downloaded (`downloading`), for the wording. From the modules, a `PlaylistAdder`: EntryOptions' ADD TO PLAYLIST, and Right on PLAY on Jellyfin's and Emby's item pages, which hand it the item as the backend formats it.

**Another module** joins with a key prefix (`keyPrefix`), its entry's id and title (`itemFor`), what plays (`playableUrl`) and, to go on offline lists, a download (`startNext`); another media server joins by implementing `MediaServer` and being handed to the backend in `main.cpp`. Netflix and Prime Video can't: they play in the service's own player.

## Input (InputManager)

All input arrives in QML as **ordinary key events** — views bind `Keys.onPressed` / `Keys.onUpPressed` / etc. and never know which physical device produced the event. Keyboards and keyboard-emulating USB remotes deliver real key events natively; **USB game controllers** are translated by `InputManager` (`src/input/InputManager.h/.cpp`, exposed to QML as the context property **`inputManager`**).

**Please don't add gamepad-specific handling to a view** — if a view handles the right keyboard keys then with this setup it will also handle gamepads.

### How it works

1. **SDL2 GameController** — `SDL_Init(SDL_INIT_GAMECONTROLLER)` only (no video subsystem, so it works headless under EGLFS). A 16 ms `QTimer` on the main thread polls SDL events: hotplug (`CONTROLLERDEVICEADDED/REMOVED`), buttons, and axes. SDL's built-in controller database normalizes most pads to a standard layout, so defaults "should" work out of the box. The `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` hint keeps controller input flowing while mpv's window holds OS focus during playback.
2. **Buttons → actions → key events** — each SDL input maps to one of seven named actions below, and each action synthesizes one Qt key. Button identities are **positional** (using an Xbox reference layout — `SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS` is forced off so Nintendo-type pads behave the same): `a` is always the south face button and input.cfg accepts `south`/`east`/`west`/`north` aliases to try to make it easier to wrap my head around =)

   | Action | Qt key | Default binding |
   |---|---|---|
   | `up` / `down` / `left` / `right` | arrows | D-pad, left stick, LB/RB (left/right) |
   | `select` | Return | A |
   | `back` | Escape (the views take Backspace from a keyboard too) | B, Select |
   | `play_pause` | Space | Start |

3. **Delivery** — while the Qt window is **active**, synthesized `QKeyEvent`s are posted to the root QQuickWindow and reach the QML `activeFocusItem` like real key presses; on RPi/EGLFS the window is always active, so during playback they flow through the Player views' existing key forwarding (`mpvController.sendKey(...)`). When the window is **inactive** (like on MacOS where fullscreen mpv holds OS focus) and QQuickWindow has no `activeFocusItem`; `InputManager` instead emits `mpvKeyRequested(key)`, which `main.cpp` connects to `MpvController::sendKey`.  That will drive mpv directly over IPC with the same key names. The net result is that gamepads drive mpv identically to the keyboard on both platforms. Held directions auto-repeat (400 ms delay, 100 ms interval) so lists and ff/rw feel like keyboard repeat.
4. **User overrides** — `$DATA_ROOT/input.cfg` (`<input> <action>` per line, `#` comments, merged over defaults, live-reloaded via `QFileSystemWatcher`). An optional `$DATA_ROOT/gamecontrollerdb.txt` can add SDL mappings for exotic pads. Check out grammar and examples in [BUILDING.md → Gamepad input](BUILDING.md#gamepad-input-inputcfg).
5. **Adaptive footers** — `inputManager` exposes `lastInputDevice` (`"keyboard"` | `"gamepad"`, tracked via an app-wide event filter that ignores the synthesized events by their magic `nativeScanCode`) and a `hints` map (`back`, `select`, `navigate`, `arrows` for the trees, where all four arrows move, `change`, `browse`, `play_pause`). Main.qml mirrors it as **`root.hints`**, and footer hint labels bind to that — e.g. `root.hints.back + ":BACK"` renders `[ESC]:BACK` while the keyboard is active and `[B]:BACK` after a controller press, reflecting the live mapping. Views should bind to `root.hints.*` (similar to how we handle `root.sh`), **not** `inputManager.hints.*` because id-resolved `root.*` will stay valid when swappig views.  If you don't when the module Loader swaps views, the dying view's context properties will resolve to null and bindings on them will throw TypeErrors during teardown. Face-button labels are translated to what's printed on the **last-touched** controller via `SDL_GameControllerGetType` (Nintendo swaps A/B & X/Y; PlayStation shows X/O/SQ/TR), and `label <button> <text>` lines in input.cfg override them for pads that misreport their type. New views with footers should now use `root.hints.*`, and not hardcoded `[ESC]`/`[ENTER]` strings like I had in my previous implementation.

### Input survives a display hand-off

On RPi/EGLFS, input keeps flowing while Qt is VT-switched away: Qt's libinput/evdev handlers and SDL both read `/dev/input/event*` directly, with no VT gating. **Only rendering is suspended.** That's why a Player view can forward keys to fullscreen mpv over IPC on the Pi.

### The mouse pointer

Qt's own pointer stays hidden (`main.cpp`): on EGLFS it is a hardware cursor, and `Qt::BlankCursor` didn't always keep it hidden after a display hand-off. The app draws its own instead, in the OSD's pixels (`Components/MousePointer.qml`). `Main.qml` lays a hover-only `MouseArea` over everything (`acceptedButtons: Qt.NoButton`, so clicks and the wheel still reach what is under it). As the mouse moves, the pointer follows it and shows, and it hides again after Settings → **Mouse Pointer** seconds without moving: `app.mouse_pointer`, which is `"off"`, `"always"` or a number of seconds (`5` when unset). Moving the mouse also resets the idle tracker and dismisses the screen saver, unless Mouse Pointer is Off. The menus still go by keys; the pointer is for seeing where a mouse is.

## Bluetooth (BluetoothManager)

Settings → **Bluetooth** (`views/Bluetooth.qml`) pairs a Bluetooth keyboard, gamepad or remote. It is backed by `BluetoothManager` (`src/bluetooth/`, the context property **`bluetoothManager`**), which talks to BlueZ, the Linux Bluetooth daemon, over D-Bus (`org.bluez` on the system bus).

- **Optional, Linux only.** It is built when CMake finds Qt D-Bus (`qt6-base-dev` has it), with `OSDOS_BLUETOOTH` defined. On macOS, or without Qt D-Bus, `supported` is false and Settings leaves the row out.
- **BlueZ is only called once it is on the bus.** A `QDBusServiceWatcher` follows `org.bluez` coming and going, and `GetManagedObjects` loads the adapter (the first one, `hci0` on a Pi) and its devices. `InterfacesAdded`/`InterfacesRemoved`/`PropertiesChanged` keep them up to date. On OSD/OS image `bluetooth.service` starts after the app is on screen (see [os/README.md](os/README.md)), and a call to `org.bluez` before then would start it early through D-Bus activation. A change to a property the list doesn't show (`RSSI`, with every answer while searching) doesn't touch it.
- **Search** is `StartDiscovery` for a minute (`startSearch()`/`stopSearch()`, `searching`). `devices` lists the paired devices by name, then the devices found in the order they were found. A device that hasn't said its name is left out until it is paired. `kind` comes from the Class of Device (a keyboard with a touchpad is a "combo", which BlueZ has no icon for), else from BlueZ's `Icon`.
- **Pairing** (`pair(path)`) stops the search, calls `Device1.Pair` (two minutes, enough to type a code), then sets `Trusted` so the device reconnects by itself after a restart, and calls `Connect`. `connectDevice`, `disconnectDevice` and `forget` (`Adapter1.RemoveDevice`) work on a paired device. `busy` marks the row while one of these runs, and `message` says how it went.
- **The agent.** `BluetoothAgent` (`org.bluez.Agent1`) is exported at `/com/osdos/BluetoothAgent`, then registered as BlueZ's default agent with the `DisplayYesNo` capability. That covers a keyboard (BlueZ shows a passkey to type on it: `DisplayPasskey`, called again with the digits typed so far; or an older keyboard's PIN, `DisplayPinCode`/`RequestPinCode`), a phone (the same code on both: `RequestConfirmation`, answered with `answerPrompt`), and a pad or mouse with nothing to show (no question). What is to be shown is in `prompt` (`{ kind, name, code, entered }`), drawn by `views/BluetoothPrompt.qml` over the page; back cancels the pairing (`cancelPairing()`). A pairing nobody started here (`RequestAuthorization`) is refused. `AuthorizeService` lets a paired device in.
- **Leaving the page** stops a search and cancels a pairing, since nothing would show the code any more.
- **Turning it on** (`setPowered`, and SEARCH while off) is tried once more 2 s later when BlueZ refuses, as bluetoothd may still be setting the adapter up. A second refusal sets `powerFailed`, and the page then offers DETAILS. An adapter that rfkill blocks (its switch in `/sys/class/rfkill`, named after it) isn't tried again: BlueZ only answers "Failed" for it, so `message` says rfkill blocks it. `collectDetails()` puts into `details` the adapter's state as BlueZ has it, rfkill's switches and the system log's last Bluetooth lines (`journalctl`, which the app's user reads as a member of `adm`), each line once with how many times it came, so a photo of the screen shows what went wrong without a terminal. On OSD/OS image the image unblocks Bluetooth as bluetoothd starts, which the Pi's own adapter needs to turn on at all (see [os/README.md](os/README.md)).
- **Permissions.** The app talks to BlueZ as the user it runs as. `install.sh` and the OS image add that user to the `bluetooth` group, which BlueZ's D-Bus policy lets in.

## Display Output (Settings, OS image)

Settings → **Display Output** switches the OSD/OS image between HDMI, composite and SCART RGB, among the outputs the board has. `DisplayOutput` (`src/display/`, the context property **`displayOutput`**) offers them; the Pi's firmware reads its display settings only at power-on, so a switch goes through a reboot, and the new output stays only when it is kept on its own screen.

- **What the board has.** `src/util/Board` reads the model (`/proc/device-tree/model`; `OSDOS_BOARD_MODEL` in tests) and its family (Pi 3, 4, 5; the Pi 400 is a Pi 4 and the Pi 500 a Pi 5, both without the AV jack's or the TV pads' composite; the Pi 500 still has composite from GPIO). `boardHas()` in `DisplayOutput.cpp` says which presets fit: composite on a Pi 3, 4 or 5, composite from GPIO on a Pi 5, SCART RGB on a Pi 4 or 5, its 240p/288p only on a Pi 4.
- **The presets** are `os/stage-osdos/03-boot/files/osdos-display-<id>.txt`, installed beside `config.txt`, which includes `osdos-display.txt`, a copy of one of them. `current` is the preset whose bytes it has ("" and "Custom" when none). A preset can name, for a Pi 5, the output and its mode (`# osdos-output: Composite`, `# osdos-mode: 720x576`): the launcher reads them. `options` lists the presets that are there and fit, as `{ id, label }`; `available` needs them, the app run by the image's service (`OSDOS_AUTOSTART`) and launcher API 3.
- **The switch.** `apply(id)` writes `display-output.json` in the data folder (`{ from, to }`) and exits with the preset's code, 20–28; `revert()` exits with 29. `osdos-stop` (scripts/install.sh, `ExecStopPost=+`, so as root) copies the preset over `osdos-display.txt`, keeping the one before as `osdos-display-previous.txt` (29 copies that back), writes `/etc/modprobe.d/osdos-display.conf` (`force_csync` for a Pi 5's SCART RGB, whose presets name the DPI output) or removes it, and reboots. The codes are in `kPresets` and in `osdos-stop`'s `DISPLAY_PRESETS`, in the same order. `RestartPreventExitStatus` lists them, so systemd doesn't start the app again on the way down.
- **Keep or go back.** At start, a `display-output.json` whose `to` is the preset in force sets `confirmPending`: Main.qml loads `views/DisplayKeep.qml` above the boot screen, and `openStartupModule()` waits for it (`root.displayHolding`), a favourite played at startup with it. Select on KEEP is `keep()`; 15 seconds without it, or SWITCH BACK, is `revert()`. Back does nothing there: a key pressed at random on a screen that shows nothing must not keep it. A switch that didn't happen (`to` not in force) or a revert leaves `notice` and `noticeDetail`, shown once in the same window.
- **The output on a Pi 5**, which may keep HDMI on beside a CRT: the launcher, on a Pi 5, takes the connector of the type the preset names (`/sys/class/drm/card*-Composite-*`, `-DPI-*`), points Qt's EGLFS at its card with that output primary and in the preset's mode where the connector lists it (`QT_QPA_EGLFS_KMS_CONFIG`), and exports `OSDOS_DRM_DEVICE`, `OSDOS_DRM_CONNECTOR` and `OSDOS_DRM_MODE`, which `MpvController` passes to mpv as `--drm-device`, `--drm-connector` and `--drm-mode`. The Pi 5's composite (`drm-rp1-vec`) takes PAL or NTSC from the mode's lines, so the mode is the standard. Without such a connector it falls back to the card it always picked. A Pi 4 has one output on at a time and is left as it was.

## Audio Output (Settings)

Settings → **Audio Output** picks the sound card the players play through: the Pi's AV jack, its HDMI, a USB sound card (a Pi 5 has no jack). `AudioOutput` (`src/audio/`, the context property **`audioOutput`**) lists ALSA's cards and tells each player the one chosen; ALSA's own default (`/etc/asound.conf`) is left as it is.

- **The cards** are read from `/proc/asound/cards` (`OSDOS_ASOUND_DIR` in tests), those with a playback device (`card<N>/pcm*p`), so a webcam's microphone is not one. The Pi's analog card (`Headphones`, `bcm2835 Headphones`) is **AV Jack**, its HDMI cards (`vc4hdmi0` and `vc4hdmi1` under KMS, `bcm2835 HDMI 1` and `2` under the Pi 4's fkms) **HDMI**, numbered by the board's port when there are two, and any other by its short name (a USB card's product name), its id after it when two share one.
- **The setting** is `app.audio_output`: `""` for Auto, or `{ card, name }`, the card's ALSA id and its name when chosen. The row is an ordinary `list_single` from `settingRow()`: Auto, then the cards there are now, and the chosen one marked `(Unplugged)` while it is not there, so it stays chosen. ◄ ► save it as any row; `AudioOutput` hears it through `AppCore::appSettingChanged`.
- **The players are told; the app's environment is not touched.** `card()` is the chosen card's id while `/proc/asound/cards` lists it, else "". mpv gets `--audio-device=alsa/default:CARD=<id>` (`mpvArgs()`): a video's session (`sessionArgs()`), Ambient Mode's and Weather's music. ALSA's `default` device on a card is the card's own where ALSA has one (dmix on a USB card, IEC958 framing for the Pi's KMS HDMI, which takes nothing else), else `plughw`. Any other program `ScriptLauncher` starts (a script, a web player's browser) gets `ALSA_CARD=<id>` in its environment (`applyTo()`), which ALSA's `default` device reads. The app's own environment stays as it is: libmpv reads it from threads of its own.
- **Live.** `cardChanged` calls `MpvController::followAudioOutput()`, which sets `audio-device` (`auto` for Auto) on a session playing inside the app, behind the menus, and mpv reopens its output on the new card. A video chosen again is matched against that session without its `--audio-device`, as without its `--start`. An mpv process can't be playing while Settings is up; the next one starts with the card.
- **Where.** Linux, where `/proc/asound/cards` is there and no sound server picks the card for what plays through it (PipeWire's or PulseAudio's socket in `$XDG_RUNTIME_DIR`, or `PULSE_SERVER`): the OS image, Raspberry Pi OS Lite. On Auto nothing goes on the command line, so `audio-device` in `mpv.conf` and `/etc/asound.conf` still choose (the cascade above).

## About (views/About.qml)

Settings → **About** opens on the wordmark, as the logo has it: `assets/images/logo-wordmark.svg` drawn through `OsdIconProvider` in the scheme's colour, with `logo-slash.svg` (the slash alone, in the same frame) over it in its own three colours, and SMART TV FOR CRT under a rule in the deck's own letters, so a CRT reads it. Under it, what OSD/OS is made of, who makes it and under which licence, as menu lines (`MenuList`) whose `HelpLine` carries each one's detail: the build (`appCore.appBuild`, CMake's `APP_BUILD`: the commit the tree was configured from and the day; the version is in the title bar), the developer, 240-MP that it is a modified version of, the licence, the source, what its code was written with (Claude Code) and its artwork made with (ChatGPT), the fonts, the libraries with their licences, the system the image is built on (Raspberry Pi OS Lite on Debian, with the trademark notices) and the data the modules draw on (TMDB with the notice its terms ask for, Open-Meteo with the credit its licence asks for, Wikidata). Its help line is set `always`, so it stays when Settings' Help Line is off: these lines are the page.

Behind the LICENSE line is the licence itself, below the title bar in place of the lines: the notice the GNU GPL asks an interactive program to show (whose copyright it is, that it comes with no warranty, that it may be passed on under the licence, and where the licence is), then the licence's text, a page at a time with ▲ ▼. The text is `LICENSE` next to the app, which CMake installs with `Main.qml` into every build (the GPL asks that every copy carry it), read through `appCore.licenseText()`, which puts each paragraph on one line for the view to wrap. The developer's name and the year are properties at the top of the view.

## C++ Backend Patterns

Backends are `QObject` subclasses registered via `registerModule(...)` before the engine loads.
Please review `PlexBackend` as a reference implementation.

- All HTTP via `QNetworkAccessManager` — async, on the main thread, no worker threads needed.
- Results returned to QML via signals.
- Auth/state persisted to JSON files in the data dir, each written whole with `writeFileAtomically()` (`src/util/AtomicFile.h`; owner-only for a token or a key), never through a `QFile` opened for writing: a crash or a power cut mid-write would leave it cut short.
- `Q_INVOKABLE` for slots called from QML; `signals:` for callbacks to QML.
- For dynamic settings dropdowns, emit `dynamicOptionsReady(key, [{id, label}])` — auto-connected; `AppCore` re-emits with the module ID prepended.
- For auth-gated modules, emit `authStateChanged()` on sign-in/out — auto-connected and re-emitted as `moduleAuthStateChanged(moduleId)`.
- To react to your own settings changing, add a slot `onSettingChanged(moduleId, key, value)` — auto-connected to `moduleSettingChanged`.
- A backend resolves its own configured paths in its constructor — e.g. `LocalFilesBackend` / `AmbientModeBackend` read `media_directory` from `config.json` (defaulting to `dataRoot/media` / `dataRoot/ambient`; Local Files takes `OSDOS_MEDIA_DIR` first when the environment sets it, as OSD/OS image does for the card's film partition). `main.cpp` does not touch module paths.

## QML View Patterns

### Root.qml — module router

Every module requires `Root.qml` as its entry point. It owns the internal nav stack and handles exiting back to the module list.

```qml
import QtQuick

FocusScope {
    id: moduleRoot

    signal goBack()

    property var navParams: ({})

    // must match your manifest id
    property var _moduleInfo: appCore ? appCore.get_module_info("com.osdos.<name>") : ({})
    property string moduleName: _moduleInfo.name || ""
    property string moduleIcon: _moduleInfo.icon || ""

    property var navStack: []
    property var currentParams: ({})

    function navigateTo(viewPath, params, fromState) {
        var resolved = Qt.resolvedUrl(viewPath)
        navStack.push({ source: internalLoader.source, params: currentParams, listState: fromState || {} })
        currentParams = params || {}
        root.changeWindow(internalLoader, resolved, { "navParams": params || {} })
    }

    function navigateBack() {
        if (navStack.length === 0) {
            moduleRoot.goBack()
            return
        }
        var prev = navStack.pop()
        if (!prev.source || prev.source.toString() === "") {
            moduleRoot.goBack()
            return
        }
        var restored = Object.assign({}, prev.params)
        restored.navListState = prev.listState || {}
        currentParams = restored
        root.changeWindow(internalLoader, prev.source, { "navParams": restored })
    }

    Loader {
        id: internalLoader
        anchors.fill: parent
        focus: true
        onLoaded: { if (item) item.forceActiveFocus() }

        Connections {
            target: internalLoader.item
            ignoreUnknownSignals: true
            function onNavigateTo(path, params, listState) { moduleRoot.navigateTo(path, params, listState) }
            function onGoBack() { moduleRoot.navigateBack() }
        }
    }

    Component.onCompleted: navigateTo("Items.qml", {})
}
```

**Rules:**
- `id` is always `moduleRoot`.
- `moduleName` / `moduleIcon` always come from `appCore.get_module_info(...)` — never hardcoded.
- `goBack()` is the only signal that leaves the module — child views never emit it directly.
- `navigateBack` merges `navListState` back into params on pop so list views can restore position.
- For auth flows that need `replaceWith` (navigate without pushing to the stack), please see the Plex module as a reference.

### Items.qml — list view

```qml
import QtQuick
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            goBack()
            event.accepted = true
        }
    }

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
    }

    ListView {
        id: itemList
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625

        // restore list position on back-navigate
        Component.onCompleted: {
            var restore = navListState.currentIndex !== undefined ? navListState.currentIndex : 0
            currentIndex = Math.min(restore, Math.max(0, count - 1))
            positionViewAtIndex(currentIndex, ListView.Contain)
        }

        // Up/Down with wraparound. The positionViewAtIndex call is required:
        // changing currentIndex alone does not scroll a clipped ListView, so
        // without it a wrap moves the selection off-screen.
        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
            itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
        }

        Keys.onReturnPressed: {
            navigateTo("Detail.qml", { item: model[currentIndex] }, { currentIndex: currentIndex })
        }
    }
}
```

### Detail.qml — leaf view

```qml
import QtQuick
import Components

FocusScope {
    id: detailRoot

    property var navParams: ({})

    signal goBack()

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            goBack()
            event.accepted = true
        }
    }

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: navParams.item || ""
    }
}
```

**View rules:**
- Always declare `property var navParams: ({})` — the router passes params through `root.changeWindow()`, which sets the loader's source with them.
- List views also declare `property var navListState: navParams.navListState || ({})` and restore position in `Component.onCompleted`.
- `navigateTo` always takes 3 args: `(path, params, listState)` — pass `{ currentIndex: listView.currentIndex }` as listState when pushing to a detail view. Detail views with multiple focus rows (play button / list) also pass `focusRow` in listState and restore it in their data-loaded handler, so backing in lands on the row the user left.
- Up/Down navigation wraps: past the last item returns to the first and vice versa, always followed by `positionViewAtIndex(..., ListView.Contain)` (see the handlers in Items.qml above). Views with an A–Z letter panel additionally keep `letterList.currentIndex` in sync on every move and wrap the panel itself — `modules/jellyfin/views/Items.qml` is the reference.
- Leaf views only need `signal goBack()` — no `navigateTo`.
- Use `root.sh` / `root.sw` for all margins and sizes — never hardcoded pixels. This keeps layouts responsive across CRT (240p/480i, watch overscan) and HDMI/LCD.
- Access shared state via `moduleRoot.moduleName`, `moduleRoot.moduleIcon`.
- Navigate via signals — never call router functions directly.
- `navParams.fromAppStartup` is `true` only when the app booted straight into this module because it's the configured **Start On Module**, or the module of the favourite set to **Play at Startup** — never when the user navigated in from the main menu. `Main.qml` sets it as it opens the startup module (`openStartupModule()`); a module's `Root.qml` that needs it forwards it by passing `navParams` into its first `navigateTo` (Local Files' passes on only `startupPlay`). Use it to gate boot-only behaviour such as Ambient Mode's Auto-Launch Playback, so the module's normal screens stay reachable from the menu.
- A view that emits `navigateTo` from its own `Component.onCompleted` must defer it with `Qt.callLater` — the router's `Connections { target: internalLoader.item }` only rebinds once `root.changeWindow()` has set the loader's source, so a synchronous emit goes out before anything is listening.

## Components (WIP)

Shared QML components live in `views/Components/` (registered via `qmldir`, imported as `import Components`).

### AppBar (`views/Components/AppBar.qml`)

| Property | Type | Description |
|---|---|---|
| `iconSource` | `url` | Module icon — use `moduleRoot.moduleIcon` |
| `title` | `string` | Module name — use `moduleRoot.moduleName` |
| `subtitle` | `string` | Optional context label (hidden when empty) |

The module's logo stands at its left end, in the colour scheme's text colour, a fifth taller than the bar so it stands out of it above and below, an art pixel clear of it on each side. Then comes a solid bar in the same colour with the title and subtitle in the background colour, the way a deck's on-screen menu starts; the skin's `titleBar` picture takes the bar's place (Settings → Skin, or the theme's own skin). The logo is drawn by `OsdIconProvider` (`src/util/`, `image://osdicon/<rrggbb>/<url>`): trimmed to its shape and drawn from the original at the bar's size (a vector is rendered at that height, not scaled from a bitmap), in one colour with its own smooth edges. A width asked for as well as a height (`sourceSize` both) stretches the drawing to it, for a picture that must keep its shape on a screen whose pixels are not square (the boot screen's cassette at 720×480 on a 4:3 tube). It does not use a shader effect, which the software scene graph draws as nothing.

### PromptScreen (`views/Components/PromptScreen.qml`)

A question or a notice, full screen, in the window every view has: the question in the title bar (an `AppBar`) behind a **?**, or a notice's (an error, a code to type, a button to press) behind a **!**; the hint line in its fixed place at the foot; and between them, centred both ways in the space the bars leave, what it is about and the answers, as the main menu's rows. However few lines it has, the bar and the hint line stay where every view has them.

| Property | Type | Description |
|---|---|---|
| `title` | `string` | The question (`"Resume playback?"`), or what happened (`"Playback failed"`) |
| `kind` | `string` | `"question"` (the default: `assets/images/question.svg`) or `"notice"` (`notice.svg`) |
| `message` | `string` | Under the bar: what it is about (the video, the device), or a notice's details. Wraps |
| `choices` | `var` | The answers: labels, or maps with a `label` (`{ label, action }`) |
| `currentIndex` | `int` | The answer under the cursor |
| `maxChoices` | `int` | More answers than this (5, or 6 with the hint bar off) show a window of them that follows the cursor (a `ListView` with `ScrollMarks`), ▲ / ▼ while some are hidden above or below it (`PlaylistAdder`'s playlists) |
| `hint` | `string` | The hint line; unless set, back, with navigate and select while there are answers |

It only draws: the host keeps its keys, its cursor and its visibility, so a dialog becomes one by swapping its drawing for it. Items declared inside it go between the message and the answers, centring themselves across its width (the pairing code in `BluetoothPrompt`). Every question and notice in the app is one: the players' resume prompts and error screens, Settings' quit, the update's install, the script's run, a new button for Controls, Bluetooth pairing, and `ChoiceOverlay` (so `EntryOptions` and the Plex PLAY chooser) too.

### OsdGround (`views/Components/OsdGround.qml`)

What the OSD is drawn on, as Settings → **OSD Background** (`app.osd_background`, `root.osdBackground`) has it: **Full** (the default), the colour scheme's background all over; **Window**, a window of it behind what a view shows, black around it, framed as Settings → **Window Frame** (`app.osd_frame`, `root.osdFrame`) has it: **On** (the default), a line in the scheme's colour, or the skin's `window` picture; **Off**, none; **Shadow**, the frame and a DOS window's shadow, its two arms down the window's right side and along its foot, six art pixels deep and dithered (`Dither`, whose `phase` keeps one checkerboard across both arms): a half tone of the window's colour on the black around it, every other art pixel black over a video; **Off**, none, black (with the colours above). The window (`root.osdWindow`) is the area the views lay their content out in (`root.contentBox`: the title bar's logo to the hint bar, 74 to 566 across and 57 to 430 down at 640×480), with a margin (`root.osdMargin`, 12) on every side, on art pixels.

`Main.qml` lays one under every view (`z: -1`, over the window's own colour, black but for Full). Over a video behind the menus it is as solid as Transparent Background says, and in Window only the window is drawn (`surround: false`), the picture showing whole around it. A layer that hides the whole view under it lays its own (`PromptScreen`, the `OnScreenKeyboard` and `InfoPanel` below the title bar, `NfcCardWriter`, Bluetooth's DETAILS), never a `Rectangle` of `root.surfaceColor`, so the window goes on under it just as it was: it works in screen coordinates, the window placed where it lies on the screen whatever part of it the ground covers. `LoadingScreen` and the boot screen keep their own full-screen ground: the tape's picture, not the OSD's.

Settings offers Window Frame only while OSD Background is Window: the row's `shownWith: { key, value }` (or `{ key, not }`, Logo Image's while Channel Logo isn't Off) keeps it in the model, with no line and passed over by the cursor, while the row with that key has another value, so the list stays where it is as the row comes and goes.

### The foot's room (Hint Bar and Help Line off)

Settings → **Hint Bar** and **Help Line** off hide the bars, and the window stays as it is (`root.contentBox`, `root.osdWindow`): what is above them takes their room. `Main.qml` gives it:

| Property | Description |
|---|---|
| `root.menuRowHeight` | A menu's row (28 at 480 high) |
| `root.hintRoom` | A row while the hint bar is off, else 0: every list, page and box above the hint bar grows by it |
| `root.helpRoom` | A row while the help line is off, else 0: a list over a help line grows by it too |
| `root.helpLineMargin` | A help line's `bottomMargin`: over the hint bar (76), or in its place while that is off (50) |

- A menu with a help line (Settings, a module's settings, Controls, Bluetooth, the player's menu, `MenuList`) is 8 rows with both bars, 9 with either off and 10 with both; one without (the main menu, the modules' lists) is 9, or 10 with the hint bar off. Ten rows end at 400 (at 480 high), their ▼ inside the content box.
- A detail page's body and its episode or season list, the release notes, a script's output, a launch's failure box and Bluetooth's details grow by `hintRoom`; a list of taller rows (Plex's extras) by a row of its own.
- A help line moves down into the hint bar's place, and so does an error line over the hint bar (Plex's user switch, Emby Connect's servers).
- `TreeBrowser` reaches `areaBottom`, the content box's foot with the hint bar off: more rows under the spine, which stays put. Its host's `reservedBottom` counts a help line only while it is drawn (`shown`).
- `PromptScreen` centres its lines down to where the hint bar ends while it is off, and shows six answers in place of five.
- About's help line stays (`always`), so its list gets only the hint bar's row; its licence page reaches the foot.

### Themes (Settings → Theme)

Settings → **Theme** (`app.osd_theme`, a theme's folder name; none when unset; `root.theme`) is the whole look at once: the colour scheme, the skin, the effects and the menu music. A theme is a folder with a `theme.json`, the app's own in `assets/themes` (Trinitron, Late Show, Green Screen, Matrix, Inferno, Arcade, Winter, Demoscene) or the data folder's `themes`, one there in place of the app's of the same folder name. The format, for a theme's author, is in the README's [Themes](https://github.com/mehmetraif/OSD-OS/wiki/Themes), and a theme to start from, with shaders and music of its own, in [docs/theme-template](docs/theme-template/).

- `AppCore::themes()` lists them for the Settings row, by name (the value saved is the folder's name), and `theme(id)` reads one: `{ id, name, colors, skin, effects: { text, background, selector, screen, transition }, music }`, each part only if the theme has it. `colors` is a scheme's name as it is (the schemes are `Main.qml`'s), or `{ primary, surface }`, two `#rrggbb`; `skin` a skin's name, read with `skin()`, or parts of its own (as in a `skin.json`, its pictures in the theme's folder); an effect a preset's name as it is, or `{ knobs…, animate, shader, area }` (the text effect's knobs `rainbow`, `shimmer`, `flicker`, `glow`; the screen's `scanlines`, `curvature`, `glow`, `bleed`, `noise`, `vignette`; held to 0..1; `shader` a `.qsb` in its folder; `area` a background's, `"foot"` or the window); `music` a file of a type `MenuMusic` plays. Every file must be in the theme's own folder (a path or a link out of it is refused). A part that can't be used is there, empty, and is drawn as none: Video 1's colours, OSD/OS's own window, no effect. The log names what was left out, and a `theme.json` that isn't JSON leaves the theme out of the list (in a folder named like one of the app's own, that one is listed and used instead).
- **Its parts, in Settings.** Each part has a row and a setting of its own: Color Scheme (`app.color_scheme`), Skin (`app.skin`), Text Effect, Background Effect, Selector Effect, Screen Effect (`app.text_effect` …), Transition (`app.transition`) and Menu Music (`app.menu_music`). Each is `""`, the theme's (THEME), `"Off"`, or a choice of OSD/OS's (a preset's name, a scheme's, a skin's folder). Picking a theme sets them all back to `""` (`Settings.themeChosen()`); THEME is offered only while there is a theme, and without one `""` shows as what it then is (Video 1, None, Off).
- **In force.** `Main.qml` resolves each part: Settings' value unless it is `""`, else the theme's (`effectOf()` reads a preset's name from `root.screenPresets`, `textPresets`, `backgroundPresets`; `nameOf()` checks a selector effect's or a transition's name against `root.selectorPresets`, `transitionPresets`). `root.scheme`, `root.skin`, `root.screenEffect`, `root.textEffect`, `root.backgroundEffect`, `root.selectorEffect`, `root.transition` and `root.music` are what the views and the shell read; views read `root.primaryColor`, `root.surfaceColor` and `root.skin` as they always have, so a theme reaches them without a change.
- A skin chosen before skins had their name was saved as `app.theme`: the Skin row and `root.skin` read it while `app.skin` is unset.

### Skins (Settings → Skin)

Settings → **Skin** (`app.skin`, a skin's folder name, `"Off"` for none, `""` the theme's; `root.skin`) dresses the window, apart from the colour scheme: a skin gives the shapes of the window's parts, and the scheme still gives every colour, so any skin goes with any scheme. A skin is a folder with a `skin.json` (its `name`, a picture for any of `window`, `titleBar`, `hintBar` and `selection`: `{ "image", "border", "tile" }`, or just the file's name; and `icons`, `{ name: file }`), the app's own in `assets/skins` (DOS, Rounded) or the data folder's `skins`, one there in place of the app's of the same folder name. A `theme.json` of those parts only, in the data folder's `themes`, is a skin made before skins had their name (`isEarlySkin()`), read as a skin and not listed as a theme. The format, for a skin's author, is in the README's [Skins](https://github.com/mehmetraif/OSD-OS/wiki/Skins), and a skin to start from, with a script that draws its pictures from text drawings, in [docs/skin-template](docs/skin-template/).

- `AppCore::skins()` lists them for the Settings row, and `skin(id)` reads one for `Main.qml`'s `root.skin`: the parts it has that can be used, their files as URLs, and `icons`, `{ name: URL }`. A picture must be a PNG, GIF or BMP in the skin's own folder (a path out of it, or a link out of it, is refused) and a `border` one number or four; an icon a PNG, SVG, GIF, BMP or JPEG there, its name `[a-z0-9_-]`, in lower case.
- `SkinImage` draws a part where the window's own drawing goes, which stays as the fallback: `OsdGround`'s window frame (`window`, with Window Frame On or Shadow; under it the window has no fill of its own, so the picture draws the whole window and what it leaves clear shows what is around it), the `AppBar`'s bar (`titleBar`), the `HintBar` (`hintBar`), and the selected line (`selection`, in `SelectionBox`).
- `OsdSkinProvider` (`src/util/`, `image://osdskin/<primary>/<surface>/<file URL>`) maps the picture to the two colours a pixel at a time: clear (alpha under half) stays clear, light (grey from half up) takes the scheme's colour, dark its background. It reads only local files. A change of scheme, or OSD Background's Off, asks for the picture again in the new colours.
- **Icons.** `root.skinIcon(url)` is the skin's icon of the name `url` has, else `url`: a module's by its folder's name (`.../modules/youtube/...` is `youtube`), any other by its file's name (`logo`, `settings`). The `AppBar` draws its logo through it, by `OsdIconProvider` (`image://osdicon/<rrggbb>/<URL>`): the drawing trimmed of its clear margin, at the logo's height, in the bar's colour from its alpha; a vector drawn at that size, a picture of pixels scaled with its pixels kept square as it grows.

### Effects

Each part of a theme's look the GPU or the CPU draws, in the menus only. `Main.qml` holds them; the views know nothing of them but `SelectionBox` and `root.changeWindow()`.

**Never over a video.** `root.effectsRest` is true while a video plays in the window or behind the menus (`root.videoActive`), another process has the screen (`root.screenHandedOff`, an mpv process on the Pi's console), a layer of a video's is up (`root.restFor(item, true)`: `LoadingScreen`, `PlayerMenu`) or a video's view is shown (`root.playbackLoaders`: `changeWindow()` notes each loader that shows a view `root.playbackView` matches, `…Player.qml`, `…Play.qml`, `Launch.qml`, `Takeover.qml`, `SignIn.qml`, `Console.qml`, and a new view in `moduleLoader` clears them). While it is, every effect stops (the screen layer off, the background and selector effects hidden, the time still), the menu music stops, and a window changes without its transition. `VideoSurface` lies outside `screen`, the layer the screen and text effects draw, so not even a frame of a video passes through a shader.

**The screen and the text: one pass.** `Main.qml` draws the menus inside one item, `screen`: the window's colour, the views' `face`, the boot screen, the window asking to keep a display output, the screen saver and the mouse pointer. With a screen or text effect in force (`screen.shaded`: a GPU, `GraphicsInfo.api` not `Software`; a shader; a knob above 0; not resting) `screen` is a layer: Qt Quick draws it into a texture, and its `layer.effect`, a `ShaderEffect`, draws that through `shaders/effects.frag` (or a theme's own screen shader). With none the layer is off, and nothing of it runs.

- `qt_add_shaders` (CMake, with Qt Shader Tools, optional) compiles the shaders at build time for every graphics API Qt Quick draws with (GLSL ES 100 and GLSL 120/150 for OpenGL and the Pi's OpenGL ES, SPIR-V, HLSL, MSL) into the binary, at `:/shaders/<name>.frag.qsb`; without Qt Shader Tools `effectShader()` is `""` and Settings leaves out what needs one (`root.effectsUsable`, `root.transitionUsable()`).
- The knobs, 0 (none) to 1 each, are uniforms the `ShaderEffect` sets from its properties of the same names. The screen's: `scanlines` (on the screen's own lines: curved lines a pixel or two apart would beat against them into bands), `curvature`, `glow`, `bleed`, `noise`, `vignette`. The text's work on what is drawn in the two colours only, a colour on the line from `paper` to `ink` (a letter's soft edge too), so a picture, a flame or a spark is left as it is: `rainbow` (the ink through the hues, slanting and moving), `shimmer` (a glint sweeping across every three and a half seconds), `flicker` (dips like a failing neon sign, and a buzz), `inkGlow` (a halo of the ink). A knob at 0 skips its part.
- **A shader that can't be used** (`ShaderEffect.status` `Error`: a theme's `.qsb` that isn't one) is logged (`[Effect]`) and replaced: a theme's own by the built-in, with its knobs; the built-in by none. The flag that does it is set later (`Qt.callLater`), as the shader is still being set then. A `.qsb` that has nothing for the API this Qt draws with isn't caught: it loads, Qt logs that it found no shader code, and the effect draws nothing (a screen shader, nothing of the menus either).

**The background: `BackgroundFx`.** Each `OsdGround` places one in its window (inset by the skin's window border), or, for an effect whose `area` is `"foot"` (a theme's own; none of OSD/OS's), from the window's top to the screen's foot, rising into it. It draws its shader (`shaders/bg-*.frag`, or a theme's) at art-pixel size and scales it up without smoothing, on the screen's grid: the shader gets `size`, its area in art pixels, and `origin`, where that is on the screen, so a dialog's ground draws the same picture as the window's under it. Made only while shown (a `Loader`): a shader effect without its shader draws the default one, which wants a `source` this has none of.

**The selector: `SelectorFx`.** A C++ item (`src/fx/`, `OSDOS.Video`) drawing sparks, bolts, a rainbow or snow into a picture of art pixels about thirty times a second, uploaded as a texture and drawn without smoothing. Its `target` is `root.selector`: every `SelectionBox` (the selected line of `MenuRow`, the main menu, the `TreeBrowser`'s cursor, a `PromptScreen`'s answers) tells `Main.qml` it is shown (`selectorShown()`), each `OsdGround` that covers the screen that it covers (`coverShown()`), and the selector is the newest box shown in the newest cover's layer. Two lie in `face`: one over the views (`z` 1) and one under them for Rainbow, which runs under the line below when the box isn't in a dialog's layer. It skips a frame with nothing to draw, and stops once its last spark is out with no box to play round. Its particles live in its own coordinates, not the box's, so they stay where they are as the box moves: Snow's flakes, falling from the box's top edge, are left above it as it moves down and below it as it moves up. `ink` and `paper` are the scheme's two colours (`root.primaryColor`, `root.surfaceColor`); a flake over the box takes `paper`, as the box's label does.

**Time.** `root.fxTime` counts seconds every 33 ms while something moves (`root.fxMoving`: a background effect, noise, a text effect that moves, a screen shader with `animate`) and nothing rests. Otherwise the screen is drawn again only when something on it changes.

**Transitions: `root.changeWindow(loader, source, properties)`.** Every view a loader shows comes through it: `moduleLoader`'s (the main menu, Settings, a module's `Root.qml`) and each module router's own. It sets the new view at once (no transition, resting, the boot screen up, the first view, or into or out of a video's view), or catches the old one first: `lastFace`, a `ShaderEffectSource` of `face` with `live: false`, `scheduleUpdate()`, and the change made at `scheduledUpdateCompleted` (or after 250 ms, should nothing draw). Then `transitionAnimation` takes `windowChange.progress` from 0 to 1:

- **Fade** draws `lastFace` over the new window, fading.
- **Cube** turns `face` and `lastFace` by a `Matrix4x4` each (`windowChange.cubeMatrix()`): a cube's faces, its edge the screen's width (height, turning up or down), seen from two and a half edges in front and flattened onto the screen, as Qt Quick draws in two dimensions. A face turned away from the eye is hidden (`facing()`), seen from inside the cube where the other hides it. The way is `windowChange.direction`, at random each time; Cube Left, Right, Up and Down, which once fixed it, are read as Cube (`cubeOf()`), and a setting saved as one of them is saved again as Cube at start.
- **Ripple**, **Wave** and **Drop** are shaders (`ripple.frag`, `wave.frag`, `drop.frag`) over both windows as pictures, `from` (`lastFace`) and `to` (`nextFace`, which hides `face` meanwhile), on art pixels; Wave and Drop start from a corner at random. Without the shader or a GPU they fade instead.

### Menu music (Settings → Menu Music)

Settings → **Menu Music** (`app.menu_music`: `""` the theme's, `"Off"`, `"File"` with `app.menu_music_file`; `app.menu_music_volume`, 0 to 100, 60 when unset) plays a tune under the menus. `MenuMusic` (`src/audio/`, the context property **`menuMusic`**) plays it in an mpv process (`--no-video --loop-file=inf`, on Settings → Audio Output's card, its volume changed over its IPC socket as the slider moves). `Main.qml` binds its `source`, `volume` and `wanted`: in the menus only, not while the effects rest, nor under the boot screen, the screen saver or the window asking to keep a display output.

- **Anything else that plays sound holds it off** first, `MenuMusic::hold(who)` and `release(who)`: `MpvController` as a video starts (released at `playbackEnded`), Weather's and Ambient Mode's music, a script (`ScriptLauncher`). `hold()` kills its process and waits for it, so the sound card is free before the other opens it. It starts 700 ms after nothing holds it, so a hold and a release in quick turns don't start it between them, from the beginning. mpv ending within 3 s of starting couldn't play the file: that source isn't tried again until it changes.
- **What it plays.** A recording (OGG, Opus, MP3, FLAC, WAV, M4A, AAC) and a tracker's module (XM, MOD, S3M, IT, through ffmpeg's libopenmpt) as they are. A MIDI file is made into a WAV first by FluidSynth (`fluidsynth -ni -F`), with the first SoundFont of: one beside it of its name, the data folder's `soundfonts` (by name), the system's General MIDI ones (`/usr/share/sounds/sf2/default-GM.sf2` …). A module mpv couldn't play is made into one by openmpt123, where it is installed. What they make is kept in the cache folder (`menu-music/<hash>.wav`, the hash of the file's path, size, time and SoundFont; the newest only), so a file is made once; a hold kills the maker, and what it had made goes.
- The built-in themes' music is made by `scripts/make-menu-music.py`: the OGG loops from notes and simple instruments, Demoscene's XM and the theme template's MIDI file.

### VCR OSD elements

The UI keeps to two colours, like a deck's on-screen display: the colour scheme's `primary` on its `surface`. `Main.qml` maps `secondaryColor`, `tertiaryColor` and `accentColor` to `primaryColor`, so existing views follow without change. Settings → **OSD Background** Off makes the two the scheme's lighter colour and black (`surfaceColor` black, `primaryColor` whichever of `primary` and `surface` is lighter, so a scheme with dark text, T-120, doesn't vanish), and every view follows that too. Within that:

- a selection is a solid box with its text in `surfaceColor`;
- anything dimmed is dithered with `Dither` instead of given a lower opacity;
- a box that used to be tinted is outlined (`border.width: root.px`).

Pixel-drawn pieces of a deck's on-screen menu, built on `root.px` (one pixel of a 240-line picture, `sh / 240`) so their edges stay crisp at any screen size:

| Component | What it draws |
|---|---|
| `HintBar` | The footer hint line on a solid bar (or the skin's `hintBar` picture). It is a `Text`, so a view sets `text` and anchors exactly as on one. It owns its font size, steps it down only as far as a long hint needs to fit the safe width. Every view's footer and every dialog's hint line uses it, always in the same place: anchored to the bottom, `bottomMargin: root.sh * 0.1041667`, `leftMargin: root.sw * 0.125`, never under a dialog's last line (a `PromptScreen` does this for a dialog). Settings → **Hint Bar** (`app.hint_bar`, `"On"` when unset, or `"Off"`; `root.hintBar`) hides every one at once, by the bar's `opacity`, so a view's own `visible` binding holds and the window keeps its shape; what is above it takes its room (see [The foot's room](#the-foots-room-hint-bar-and-help-line-off)). |
| `MenuRow` | A settings line the way a camcorder's menu lays one out, `DISPLAY······ON`: `label`, a dot per character cell, then `value` against the line's right end (none for a submenu), with `selected` as a solid bar (a `SelectionBox`). With `heading` it heads a group instead: the label and a rule to the line's end (`MODULES ─────`). A value too long for the line is cut short, and scrolls through while the line is selected (with `alwaysScroll`, all the time: About's lines, which are read rather than chosen); with `keepValue` the label is cut instead, two dots before a value that always shows whole (a playlist's videos, `TEEN TITANS GO!… ··READY`). Settings, every module's settings, Controls and the Playlists module use it. |
| `ScrollMarks` | The ▲ above a list while lines are hidden above it and the ▼ below while lines are hidden below. Laid over a list (`anchors.fill` and `list`); the main menu and the settings menus use it. |
| `MenuList` | A view's menu of `MenuRow`s in the place every view's list has (under the title bar, one row short of the help line, a row more for each of the hint bar and the help line that is off), with `ScrollMarks` and a cursor (`step(delta)`, Up and Down) that steps over section headings (rows whose `type` is `"section"`), round the ends. The host gives it `model` (a list) and `delegate`, and keys it; `currentIndex`, `count` and `contentY` are the list's. The Playlists module's pages use it. |
| `HelpLine` | The help line under a settings menu: the focused line's description in an outlined box, on one line. A description too long for the box scrolls through it like a ticker; one written as several lines reads as one, joined with `•`. Settings → **Help Line** (`app.help_line`, `"On"` when unset, or `"Off"`; `root.helpLine`) hides every one at once, by its `opacity`, so a host's own `visible` binding holds; the menu above it takes its row. One whose lines are the page itself sets `always` (About's). A host places it with `anchors.bottomMargin: root.helpLineMargin`, which puts it in the hint bar's place while that is off; `shown` says whether it is drawn. |
| `Dither` | A checkerboard of background-colour art pixels laid over an area: the two-colour way to dim it. `phase` 1 starts it a pixel along, so two pieces of one pattern meet (the arms of the window's shadow). |
| `SkinImage` | One of the skin's pictures over an area (`part`: `root.skin.window`, `.titleBar`, `.hintBar` or `.selection`): nine slices as its `border` has them, drawn on art pixels and scaled up by `root.px` without smoothing, in the colour scheme's two colours (`OsdSkinProvider`). `shown` is false while there is none, and the caller draws its own then (see [Skins](#skins-settings--skin)). |
| `SelectionBox` | The selected line's box: the skin's `selection` picture, or a solid bar. It tells `Main.qml` where it is, for the selector effect (`SelectorFx`); `skinned: false` keeps it a plain bar (the on-screen keyboard's keys). Every selection uses it. |
| `BackgroundFx` | The background effect in an area (`area`, in screen coordinates), drawn on art pixels on the screen's grid; `OsdGround` places it (see [Effects](#effects)). |
| `PixelIcon` | A symbol from a small bitmap: `play`, `left`, `up`, `down`, `ff`, `rew`, `pause`, `stop`, `rec`, `eject`, plus the `ok` key and `tape` badges. |
| `OsdTicks` | The segment bar, `||||----`: a tick per filled step and a dash per empty one. The boot screen's progress bar. |
| `OsdTapeBar` | The tape position bar: a ▼ over the position, a ruled bar filled up to it, and the names of its ends under them: BEGIN and END, or a setting's own (`startText`, `endText`), as Settings' TRANSPARENT … SOLID slider. |

### TreeBrowser (`views/Components/TreeBrowser.qml`)

Anything shaped like folders, browsed as a horizontal tree, the way Local Files, the Netflix and Prime Video catalogues and YouTube are. The open folders run left to right along a line through the middle of the screen (the spine), each folder's entries stacked above and below the one that leads on. Every folder in the current one branches off to the right on a dotted line to a few of its entries (all of them, with `expandedFolderPreviews`), and the folder under the cursor branches once more. Only the branch off the folder under the cursor is drawn in full: every other block and line is faint (`faint`, at `faintOpacity`, 45%, the lines with half their dots), so folders side by side don't run together. Not dithered as other dimmed things are: over a video behind the menus a dither's dots would lie on the picture. An empty folder off the spine has no branch. Up/Down move, Right or Select opens a folder, Left or Back closes one. Named columns stay inside the frame (left of it, a CRT's bezel starts), and only whole rows are drawn.

| Property / signal | Description |
|---|---|
| `rootPath` | The folder the tree starts at |
| `fetch(path, preview)` | The host's lister: `[{ name, path, isFolder, … }]`, or `null` while the entries are on their way (shown as `loading…` until the host calls `refresh(path)`). `preview` is true when only a branch wants them, so a slow source can return `null` then without fetching. Entries may carry anything else the host needs back |
| `labelOf(item)` | What a row says; `item.name` by default (Local Files drops extensions here) |
| `savedTrail` | A `trailState()` to reopen on creation: pass it through `navigateTo`'s list state so coming back lands in the same folders |
| `reservedBottom` | Room the host keeps under the tree for a line of its own (a `HelpLine` while it shows: `helpLine.shown ? areaBottom - helpLine.y : 0`); the spine stays put |
| `areaBottom` | Where the tree's area ends: over the hint bar (`treeBottom`), or with Settings' Hint Bar off at the content box's foot, more rows showing under the spine, which stays where it was |
| `expandedFolderPreviews` | A folder's branch holds all its entries, not a few, and the folder under the cursor branches two levels when it has folders in it; rows off the screen are laid out but not drawn, and only folders near the screen are read for the second level. Local Files and the `FilePicker` set it |
| `activated(item)` | Select on an entry that isn't a folder |
| `preview`, `previewDelay` | Whether an entry that isn't a folder has an info screen, and how long (ms) the cursor rests on one before asking for it on its own; `0` asks only on Right or the INFO key (Space, which is the play/pause button) |
| `previewRequested(item)` | Right on an entry that isn't a folder, with `preview` on, or the cursor resting on one: show its info (the tree's last layer, an `InfoPanel`) |
| `optionsRequested(item)` | Right on an entry that isn't a folder, with `preview` off: offer its options (an `EntryOptions`) |
| `leaveRequested()` | Back with no folder left to close |

`openItem({ name, path })` opens a folder that isn't an entry of the current one, like a search's results; `refresh(path, keepEntry)` fetches a folder again (its entries changed, or came in after `fetch` returned `null`), the cursor keeping its row, or with `keepEntry` the entry it was on (not for a list that grows under the cursor, like YouTube's MORE); `leave(prefix)` closes the open folders at `prefix` and in it, a drive pulled out; `folderName` is the open folder's name, for the `AppBar` subtitle, and `currentEntry` the entry under the cursor, for a footer that says what select will do (`[ENTER]:OPEN` on a folder, `:PLAY` on a film). A key already held as the tree appears (Back held to close a player) does not repeat into it.

### FilePicker (`views/FilePicker.qml`)

The picker every setting that names a folder or a file opens: the same horizontal tree Local Files is browsed with, from its places (`AppCore::filePlaces()`: home, where drives and partitions are mounted, the root) down, each folder's entries from `folderEntries()`. A module's `directory_browser` setting opens it (`ModuleSettings`), and so does Settings → Logo Image (a row of `type: "file"`, its `picker` the navParams below). It opens down to the folder or file chosen now, the cursor on it; the choice is saved with `save_setting` and back comes back, and back at the top leaves the setting as it was.

| navParams | Description |
|---|---|
| `moduleId`, `settingKey` | The setting it saves to (`moduleId` `""` for an app one) |
| `currentPath` | The folder or file chosen now; `""` for the default |
| `mode` | `"folder"` (when unset): each folder's first entry is **USE THIS FOLDER**, which picks it; `"file"`: the folders, then the files of `types`, select on one picking it |
| `types` | With `"file"`: the file types offered, lower case (`["png", "jpg", …]`) |
| `defaultLabel` | An entry before the places that saves `""`: the module's own folder (**Default Folder**), OSD/OS's logo |
| `label` | What is picked, for the title bar |

USE THIS FOLDER carries `branchHidden`, which the `TreeBrowser` leaves out of a branch's glance at a folder, where it would be every folder's first line.

### InfoPanel (`views/Components/InfoPanel.qml`)

A film's info, the way a deck's INFO key puts up what is on the tape, laid out like Plex's detail page: the PLAY box, the name, a line of facts (`1997 - 2HR:29MIN`), the story (scrolling through when long), then its details as `MenuRow` lines (`GENRE······DRAMA`). It is a tree's last layer: the Netflix, Prime Video and YouTube views open it on `previewRequested` with `show(item)`, then set `details` (`{ title, facts, summary, rows: [{ label, value }] }`) from their backend: `TmdbCatalog.loadDetails` (TMDB details and credits, the story in English when TMDB has none in the chosen language), `YouTubeBackend.loadDetails` (what the list knows at once, then yt-dlp's length, views and description). Select plays (`playRequested`), up/down close it and move on through the list (`moveRequested`), left or back close it, and right asks for its options (`optionsRequested`: an `EntryOptions`), `optionsHint` being the footer's hint for that (`""` for a host with none).

When it comes up is the app's **INFO SCREEN** setting (`app.info_screen`): `off`, `key` (Right on a film, or INFO), or `1`, `2`, `3` (the default) or `5` seconds the cursor rests on a film before it comes up on its own.

### Recently Watched and Favorites

The tree modules (Local Files, Netflix, Prime Video, YouTube) begin with **RECENTLY WATCHED** and **FAVORITES**, then **SEARCH** and their own folders. Both are the module's lists in AppCore (`get_list(moduleId, "recent" | "favorites")`, kept in `lists.json` in the data folder), holding entries as the tree had them, so one plays from there as it would from anywhere else: a view puts an entry on `recent` as it plays it (the newest 30), and on `favorites` from its options (`EntryOptions`). YouTube's RECENTLY WATCHED is its own watch history (`history`), which keeps the resume positions too. Local Files leaves out the entries whose file has gone (`existing()`, a drive taken out, say), and its SEARCH walks the whole media folder, then the USB drives plugged in, on a worker thread (`QtConcurrent`, given only copies of what it needs), so a big library never holds the screen still, keeping no more than the first 200 names by name as it goes: `search(path, words)` gives what the last search for that folder found, or starts it and `searchReady(path)` follows; a search for another folder, or a new media folder, cancels one under way.

One favourite can **PLAY AT STARTUP**: chosen in its options (`EntryOptions`, which puts it on FAVORITES too), it is the app setting `startup_favorite` (`{ module, path, name }`). After the boot screen `Main.qml`'s `openStartupModule()` opens its module, ahead of Start on Module, with `navParams.startupPlay` set to the entry, as long as it is still one of that module's favourites; the module's router passes it on to its tree view, which plays it as if chosen in FAVORITES (a trail into that folder, so back from it lands there). Only as the view first opens: a view coming back gets `navListState` instead. Settings → Play at Startup can only turn it off. `AppCore::save_setting` takes a JS object for this (QML hands it over as a `QJSValue`). The tree tells the player it plays at startup (`navParams.startup`), and the player then never asks where to start: it resumes where the video was stopped, or starts it from the beginning, as the app setting `startup_from` (Settings → Startup From, `Resume` or `Beginning`, offered while there is a startup favourite) says. A resume prompt is no question to put to a player switched on to play.

### USB drives in Local Files

Local Files lists the USB drives plugged in (sticks, disks, a card in a reader) at the top of its tree, after RECENTLY WATCHED, FAVORITES and SEARCH, as folders named `USB: <label>`, and they come and go as they are plugged in and pulled out.

- **`RemovableDrives`** (`src/modules/local_files/RemovableDrives.h/.cpp`) finds them in the mount table: on Linux, `/proc/self/mountinfo` (`OSDOS_MOUNTINFO` names another file, for tests), read again whenever the kernel flags it (`POLLPRI`, at every mount and unmount, through a `QSocketNotifier`); on macOS, the mounted volumes, read again as `/Volumes` changes. A drive is a block device (`/dev/…`) mounted under `/media/` or `/run/media/` (the OSD/OS image's `/media/usb/<label>`, udisks' `/media/<user>/<label>`), or under `/Volumes/`, named after its mount point's folder, which is its label. Not one that `/etc/fstab` mounts (the image's own OSD-OS partition), nor one holding the media folder or held in it, which the tree shows already.
- **The backend** puts them into `entries()` for the media folder, ahead of its own folders (the three lists come whenever there is anything at all, a drive alone included), lets `getItems()` into them as roots of their own beside the media folder, walks them in a search after the media folder, and emits `drivesChanged(gone)`, `gone` being the mount points of those pulled out.
- **The tree** (`Items.qml`, and Playlists' ADD VIDEOS) closes the folders open on a drive pulled out (`TreeBrowser.leave(prefix)`) and refreshes the top of the tree and the two lists with `keepEntry`, so the cursor stays on the entry it was on as rows come and go above it. A file of a drive pulled out drops out of RECENTLY WATCHED and FAVORITES (`existing()`) until it is plugged in again.
- **The file systems' own folders** (`lost+found`, Windows' `System Volume Information` and `$RECYCLE.BIN`) are never listed, on a drive or in the media folder.
- **On the OSD/OS image** a drive is mounted read-only as it comes (`os/stage-osdos/07-usb`, see [os/README.md](os/README.md#usb-drives)), so it can be pulled out at any moment; a player still reading one gets mpv's read errors and ends as a video that fails does. An offline playlist plays a drive's video as it is, as it does the media folder's, so only while the drive is in.

### OnScreenKeyboard (`views/Components/OnScreenKeyboard.qml`)

Typing with a remote: a grid of letters and digits under the line being typed, the way a deck's menu spells a title. The arrows move the box, Select types what is in it (or `SPACE`, `DEL`, `OK` on the last row), Back cancels; a real keyboard types straight in. It covers its parent below the title bar: `open(initial)`, then `accepted(text)` or `canceled()`.

### ChoiceOverlay (`views/Components/ChoiceOverlay.qml`)

Full-screen keyboard-driven chooser: a prompt, the thing being acted on, and a short list of options, drawn as a `PromptScreen` (the prompt in the title bar, the thing under it). Use it whenever a single button has to ask "which way?" — the Plex show/season PLAY button asks next-episode vs shuffle through it.

| Property | Type | Description |
|---|---|---|
| `promptText` | `string` | The question, e.g. `"What would you like to play?"` |
| `subtitleText` | `string` | What is being acted on — the show or season name (hidden when empty) |
| `choices` | `var` | List of `{ label, action }` maps |
| `promptKind` | `string` | The `PromptScreen`'s kind: `"question"` (the default) or `"notice"` |
| `hintText` | `string` | The hint line; the `PromptScreen`'s own unless set |
| `closeOnSelect` | `bool` | Whether select closes it before acting (the default), or leaves it to the host to close (`close()`) once it is done, as one that shows what became of the choice does |

Call `open()` to show it. It emits `activated(action)` when the user picks one and `closed()` once it hides (bind `onClosed: <host>.forceActiveFocus()`); Up/Down wrap, Esc/Back cancels, and select with nothing to choose (a notice) closes it too. Its keys stop at it (Ctrl chords aside), so a host's Left and Right never reach the view under it. A host that acts on a choice before anything closes overrides `choose(action)` (`EntryOptions`, `PlaylistAdder`). As with NfcCardWriter, **behaviour keys off `action`, never the label text** — labels are free to change with state (`"Resume Next Episode"` vs `"Play Next Episode"`) without touching the handler. Settings' quit, the update's install and the Playlists module's delete ask through one.

### EntryOptions (`views/Components/EntryOptions.qml`)

An entry's options, as a `ChoiceOverlay`: **Add to Favorites** or **Remove from Favorites** (the module's `favorites` list, `moduleId`), **Play at Startup** or **Don't Play at Startup** (see above), **Add to Playlist** where the Playlists module takes the module's videos (a `PlaylistAdder` of its own, over the options, which stay under it and close with it: one `closed()` to the host, as it closes), then whatever the host adds in `moreChoices` and acts on in its own `onActivated` (YouTube's **Save to Watch Later**). A tree view opens it with `offer(entry)` on Right on an entry (`optionsRequested`, from the tree or its `InfoPanel`), and refreshes its FAVORITES folder on `favoritesEdited()`.

### PlaylistAdder (`views/Components/PlaylistAdder.qml`)

Putting a video on one of the app's playlists, as a `ChoiceOverlay` that stays open for the outcome (`closeOnSelect: false`): **Add to playlist?**, the video under it, then every playlist (an offline one marked so), **New Online Playlist** and **New Offline Playlist** (named on an `OnScreenKeyboard` loaded only while the window is up). What became of the video (added, and whether it is now to be downloaded; already on it; can't go on one) shows in the same window as a notice, until Back or Select closes it (`closed()`). `available(moduleId)` says whether to offer it at all (the Playlists module on, and taking the module's videos); `offer(moduleId, entry)` opens it, the entry being the video as its module has it (see [Playlists](#playlists-videos-from-several-modules)). `EntryOptions` carries one; Jellyfin's and Emby's item pages open theirs with Right on PLAY.

### PlayerMenu (`views/Components/PlayerMenu.qml`)

A video's own menu, over the picture while it plays on (Transparent Background, see [Its player's menu](#transparent-background-video-inside-the-app)), or without it in the picture's place, the video starting again where it was as it closes. Its player opens it at `mpvController`'s `playerMenuRequested()`, or at `playbackEnded(…, "menu")`.

| Property | Type | Description |
|---|---|---|
| `moduleId`, `iconSource`, `moduleName` | `string` | Whose settings, and its title bar |
| `keys` | `var` | The module's settings it offers, by manifest key, in this order. They are found wherever they sit in the manifest (YouTube keeps its own under ADVANCED); a `list_single` (dynamic options too) or a `toggle` |
| `actions` | `var` | The host's own lines after them: `{ label, action }` |

◄ ► change a setting and save it at once, as the module's settings do, then emit `settingChanged(key, value)` for the host to put on the video. Select on a line of `actions`, or on **Close Video** (always last, `action: "close"`), emits `activated(action)`. Back emits `closed()`. `open(title)` shows it with the cursor on the first line; `refresh()` rebuilds the lines where they are, for labels that change with state (`Add to Favorites` / `Remove from Favorites`). `applyScaling(value)` puts a Scaling on the video as it plays, `Default` being Settings'. A key it has no use for goes on to its player, so play/pause still pauses the video under it. As with ChoiceOverlay, behaviour keys off `action`, never the label.

### LoadingScreen (`views/Components/LoadingScreen.qml`)

What a player shows while its video starts, in place of a black screen: a VCR's screen as a tape loads, after a dubbing deck's on-screen display. The colour scheme's background, in a tape's noise. Its corners:

- top left: TAPE A, PLAY, and where the video is (while it loads, the point it starts from);
- top middle: TV, the deck's output;
- top right: TAPE B, LOADING blinking, and how long the video is once that is known, the seconds it has been up until then;
- bottom left: SLP ▶ and the source;
- bottom right: SLP ◀ and DEST.

The tracking band jitters across the top and, every few seconds, rolls down the picture, breaking up the letters it passes. The display jumps sideways now and then, and its letters bleed a little to the right. Settings → **Loading Effect** (`app.loading_effect`, `"On"` when unset, or `"Off"`; Main.qml's `root.loadingEffect`) turns all of that off: the display then stands alone on the plain background, its counters and blinking LOADING as before. Settings → **Loading Screen Colors** (`app.loading_colors`) draws it in the colours in force (`"Theme"`, when unset) or in Video 1's white on blue (`"Default"`), whatever the theme: Main.qml's `root.loadingInk` and `root.loadingPaper`. The boot screen has the same choice, Settings → **Boot Screen Colors** (`app.boot_colors`, `root.bootInk`, `root.bootPaper`).

| Property | Type | Description |
|---|---|---|
| `source` | `string` | Under SLP ▶: what plays (the players give their module's name) |
| `startMs` | `int` | TAPE A's counter: where the video is; while it loads, where it starts from |
| `durationMs` | `int` | TAPE B's counter once known: how long the video is (0, the default, while it isn't) |
| `title` | `string` | Optional, across the middle: what loads, when the player knows before it plays (a card's title) |
| `effect` | `bool` | The noise, bands, jumps and bleed. Defaults to `root.loadingEffect` |

Players give `durationMs` what they know: the server's length for Plex, Jellyfin and Emby (from the detail screen, `navParams.duration`), else mpv's, which comes as it opens the file (`lastKnownDurationMs`). The detail screens' launch overlays show the item's own position and length.

The noise is **`VhsNoise`** (`src/player/VhsNoise.h/.cpp`, `import OSDOS.Video`), a C++ item that draws a new frame about twenty times a second: short horizontal streaks of its `color` at random strengths, at the art pixel (`pixel`, `root.px`), scaled up without smoothing. `streaks` sets how many cover the picture. `bands` adds the tracking band and the head-switching strip along the bottom, and with a `shade` (the background colour), dropouts in them cut into whatever lies under the noise. LoadingScreen lays one under its text and one with only the bands over it. Both, and the screen's timers, run only while it is visible and the screen is the app's: while another process has it (`root.screenHandedOff`, see [the hand-off](#raspberry-pi-headless-hand-off-eglfs)), nothing drawn would reach it, so they rest (`running: false` on VhsNoise keeps its last frame) and leave the CPU to mpv.

Every video player shows one until the first position arrives, and the launch overlays of Plex, Jellyfin and Emby's detail screens (while the stream is prepared) are one too. A still image never moves mpv's position, so Local Files ends it as image content is launched. On the Pi, with Transparent Background off, mpv takes the screen as soon as it starts, and what stays on it until the picture comes is the frame drawn last: `MpvController` starts mpv a tick (50 ms) late, so that it is the loading screen, still. mpv's own length comes only after that, so the still frame shows one only when the player knew it before (a server's, or the video's when it starts again after its menu). With Transparent Background on, it goes on moving until the picture comes, and mpv's length shows as soon as mpv has opened the file.

### NfcCardWriter (`views/Components/NfcCardWriter.qml`)

Full-screen takeover that writes an NFC card for the item a detail view is showing. Shared so every module reachable from a card writes them the same way; the host supplies only what goes on the card.

| Property | Type | Description |
|---|---|---|
| `cardRef` | `string` | Line 2 of the tag file — e.g. a Plex guid, or a set ref like `plex://collection/<ratingKey>` |
| `cardTitle` | `string` | Filename **and** display title |
| `offerShuffle` | `bool` | Show the shuffle option — only meaningful for a set (show, season, collection, playlist) |
| `orderedLabel` / `shuffleLabel` | `string` | What the two `offerShuffle` choices are called. Defaults (`"Sequential Episodes"` / `"Shuffle Episodes"`) suit a show or season; Plex's collection and playlist lists override them with `"In Order"` / `"Shuffled"`, since a set of movies has no episodes to sequence |
| `available` | `bool` | Read-only. True when the NFC module is enabled and a reader is connected — bind the host's entry-point row's `visible` to this |

Call `open()` to show it; it emits `closed()` when done. Two things worth preserving if you touch it:

- **Capture is armed only while it is open**, so a card resting near the reader while the user browses can never trigger a write. Arming is always a deliberate action, never a passive listen. The backend handles capture *ahead of* its module-active gate, because arming happens from another module's screen.
- **Choices carry a stable `action` field; behaviour never keys off the label text.** An earlier version matched `indexOf("shuffle")` on the label and silently broke the moment the wording changed.

Writing also offers an option to the user to replace any previous tag file for that UID, that way a card can be written easily from with the UI.

- **Two cards never share a filename.** `writeCardFile` suffixes the name (`Dune (2021) (2).txt`) when the target name already belongs to a different UID. Hosts should still qualify `cardTitle` so the suffix stays rare.  For example Plex names cards with the year of the item like: `Dune (2021)`, `Cowboy Bebop (1998) - S1`, `Cowboy Bebop (1998) - S1E5`.
- **The host's title is passed through verbatim.**  To keep the NFC writing generalized for other callers its built to just pass the name through cleanly and the NFC card writing portion doesn't reason about it. I had a use case in my Plex library where an item already carried the year in its title (e.g. Cowboy Bebop (2021)) so for that case the name written is `Cowboy Bebop (2021) (2021).txt`. This is deliberate because inferring if trailing `(NNNN)` is a year or something else would guess at a user's own metadata (think of the the use case for Cyberpunk 2077). The cost of guessing wrong I think outweighs a cosmetic repeat and users can always choose rename the tag file manually as well without any impact to the mapping.

## Config Storage

User configuration is stored in `config.json` in the app's data directory:

```json
{
  "app": { "color_scheme": "Video 1" },
  "modules": {
    "com.osdos.plex": { "enabled": true, "server_machine_id": "...", ... }
  }
}
```

Each module's settings live under `modules.<id>`. Use `save_setting` / `get_setting` (which support dot-notation keys) rather than writing the file directly. A module's lists (RECENTLY WATCHED, FAVORITES) are in `lists.json` beside it, through `get_list` / `add_to_list` / `remove_from_list`. Both are written whole (`writeFileAtomically()`): a crash, a power cut or a full disk mid-save leaves the old file, never a cut-short one, which `loadConfig()` would take for none, the next save then writing the defaults over every setting. The data directory is created on first run and is separate from the app itself, so rebuilding never wipes user settings. For the exact per-OS path (macOS vs Raspberry Pi OS), see [BUILDING.md](BUILDING.md#configuration).
