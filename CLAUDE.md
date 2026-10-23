# OSD/OS Development Guidelines

OSD/OS (formerly 240-MP) is a retro VHS-style media app built with C++ Qt6 + QML, targeting Raspberry Pi 4 and 5 and macOS. Modules are self-contained media integrations (Plex, Local Files, Ambient Mode, etc.) that the app shell discovers and loads at startup.

**Playback engine**: OSD/OS launches **mpv** as a subprocess for video playback. mpv must be installed separately (`apt install mpv` on RPi/Debian, `brew install mpv` on macOS). The app handles all browsing, auth, and settings; when a video is selected it hands off to mpv fullscreen via `MpvController`, then resumes when mpv exits. The one exception is Settings → **Transparent Background**: mpv then plays inside the app's own window through libmpv (opened at run time, `EmbeddedMpv`), so back from a video returns to the menus with the picture still playing behind them.

---

## Build & Run (macOS ARM)

```bash
# First time / after CMakeLists.txt changes:
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/macos . && cmake --build build

# Incremental (code changes only):
cmake --build build

# Run:
APP_ROOT=$(pwd) ./build/osdos
```

For the full build/install story on both targets (macOS and Raspberry Pi OS), CI, and config paths, see **[BUILDING.md](BUILDING.md)** and **[INSTALL.md](INSTALL.md)**.

---

## Where things live

This file stays intentionally lean. The detailed documentation is single-sourced elsewhere — read the relevant doc before working in an area rather than relying on memory:

| If you need… | Read |
|---|---|
| Architecture, module anatomy, `manifest.json` setting types, `AppCore` / `registerModule`, C++ backend patterns, input/gamepad handling (`InputManager`), QML view/navigation patterns, Components, config shape | **[ARCHITECTURE.md](ARCHITECTURE.md)** |
| How to contribute, project principles, best-practices checklist, adding/changing a module, testing, coding style | **[CONTRIBUTING.md](CONTRIBUTING.md)** |
| Building & running on macOS / Raspberry Pi, CI/release workflow, per-OS config/data directory paths | **[BUILDING.md](BUILDING.md)** |
| End-user install (Raspberry Pi imaging, `config.txt`, macOS DMG) | **[INSTALL.md](INSTALL.md)** |
| The OSD/OS image (pi-gen stage, app-first boot order, boot screen) | **[os/README.md](os/README.md)** |

---

## Key facts to keep in mind

- **Test hardware**: the owner tries builds on a Raspberry Pi 4 and a Raspberry Pi 5 (8 GB): the Pi 4 as a TV, the Pi 5 in a JAMMA arcade cabinet, and LAN Play between the two. The Pi 5 differs where it matters here: it boots Full KMS with mpv flags of its own ([ARCHITECTURE.md → Per-device video decode profiles](ARCHITECTURE.md#per-device-video-decode-profiles)), and it has no AV jack (composite from its TV pads, sound over HDMI or a USB card), so Settings → Display Output and Audio Output offer it other outputs.
- **Modules are discovered from `modules/*/manifest.json`** at startup — a pure-QML module needs no C++ changes. A module with a backend adds **one** `registerModule(...)` call in `src/main.cpp`; that call is the single place the module ID is stated. (Details: [ARCHITECTURE.md → AppCore](ARCHITECTURE.md#appcore--the-app-shell).)
- **`registerModule` wires optional backend signals/slots by introspection** (`dynamicOptionsReady`, `authStateChanged`, `onSettingChanged`) — declare them with the exact signatures and no `main.cpp` changes are needed.
- **Every module's QML entry point is `Root.qml`** (the router). Views are `FocusScope`s that pass state via `navParams` and communicate via `navigateTo` / `goBack` signals. Size everything with `root.sh` / `root.sw`, never hardcoded pixels.
- **`PlexBackend` is the reference implementation** for backends.
- **Config** is `config.json` in the data dir; module settings live under `modules.<id>`. Use `save_setting` / `get_setting` (dot-notation supported), not direct file writes.
- **An NFC card can hand off to another module** (a Plex guid on a card plays through the Plex module). The NFC backend routes by URI scheme; the receiving module carries a `CardPlay.qml` that resolves and then `replaceWith`s its Player. A module reached this way must bypass its own auth/user gate — a card plays as whoever is already active and must never prompt a profile switch. (Details: [ARCHITECTURE.md → Card Hand-off](ARCHITECTURE.md#card-hand-off-nfc--a-module).)
- **Gamepad input is centralized in `src/input/InputManager`** (SDL2) and arrives in QML as ordinary synthesized key events — never add gamepad-specific handling to views; if a view handles the right keys it handles gamepads. Footer hint labels bind to `root.hints.*` (adapts keyboard↔gamepad), never hardcoded `[ESC]`/`[ENTER]` strings and never `inputManager.hints.*` directly — context-property bindings throw TypeErrors when the view Loader tears down; id-resolved `root.*` is teardown-safe. (Details: [ARCHITECTURE.md → Input](ARCHITECTURE.md#input-inputmanager).)

---

## Commits, pull requests and releases

These rules apply to everything written for the repository: commit messages, pull request titles and descriptions, comments and release notes.

- **No session links or signatures.** Never include a link to a Claude Code session (`claude.ai/code/…`), and never add a "Generated with Claude Code" line. The only attribution in a commit is its `Co-Authored-By` trailer.
- **No "AI involvement" section.** Pull request descriptions follow `.github/pull_request_template.md` with its AI involvement section left out.
- **Professional language.** Write clear, complete sentences that state what a change does and why. Describe the change itself; never attribute it to a request ("the owner asked for…", "as requested").
- **Release notes** open with what the release brings since the previous tag, as short descriptions under **What's New**, **Changelog** and **Bug Fixes**. The release workflow writes these sections from the commits' subject lines (`scripts/release-notes.sh`): `feat:` commits under What's New, `fix:` commits under Bug Fixes and every other commit under Changelog. Give every commit a conventional-commit type and a subject that reads well as a release-note line. (Details: [BUILDING.md → GitHub Actions](BUILDING.md#github-actions).)
