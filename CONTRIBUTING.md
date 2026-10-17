# Contributing to OSD/OS

Thank you for considering contributing to OSD/OS!  I originally built this as a personal project for watching video content on a CRT but I'm also stoked to see what ideas you might want to add.  So with that in mind I'll try to make contributing to this project as easy and transparent as possible.  If you have any questions on the below please create a post in [Discussions > Q&A](https://github.com/mehmetraif/OSD-OS/discussions/categories/q-a).

## Non-code contributions

The most useful community contributions are often not code, items like the following are super helpful...

- Documentation improvements where a step was unclear
- Hardware validation reports for different Pi models and CRT outputs
- Logs from failures along with step by step ways to replicate
- Photos or screenshots of working setups

## Getting started

1. **Set up an environment** — follow [BUILDING.md](BUILDING.md) to build and run on macOS (ARM) or Raspberry Pi OS.
2. **Understand the codebase** — read [ARCHITECTURE.md](ARCHITECTURE.md). It's the technical reference for how the shell, modules, and views fit together.
3. **Discuss first if it's big** — for anything beyond a small fix, a quick post in [Discussions > Q&A](https://github.com/mehmetraif/OSD-OS/discussions/categories/q-a) can help make sure it fits the project's direction before you invest time.
4. **Branch and open a PR** — fork, work on a branch, and open a pull request against `main` with a clear description (see the [AI use](#note-on-ai-use) note below for what to disclose).

## Submitting code

### Principles to keep in mind

1. **Baseline on remote control as an input device**: All experiences should be built so they can be interacted with via up/down/left/right enter and esc/backspace.  More complex inputs should be avoided so that users can navigate via a simple usb remote.
2. **Lay out screens for 240p/480i on a CRT**: Design layouts and size elements to display well on a CRT TV.  Consider overscan when placing elements on screen.  If you leverage the `root.sh` and `root.sw` properties for sizing you'll get responsive display for LCD tvs out of the box.
3. **Keep modules self contained**:  If your module just relies on QML then you can simply add your module in a `/modules/[module name]` directory with a `manifest.json` and OSD/OS will pick it up for display.  If your module requires a backend then you'll also need to register it in `/src/main.cpp`.  But other than that please keep all of your module source in a `/src/modules/[module name]` folder.  See [Anatomy of a Module](ARCHITECTURE.md#anatomy-of-a-module) for the full layout.
4. **Don't add tracking or analytics**: Do not include any mechanisms for tracking or reporting usage to an external source that you maintain.  A module should only ever write details to the app's local configuration directory.  If a module relies on connecting to a 3rd party API (example: the Plex module) then it should only communicate with that API directly.
5. **Browse & Hand-off**: Think of OSD/OS and its modules as a way to browse structured content (either on a filesystem or via an API response) and to hand-off to a purpose built tool for an action (like how it relies on MPV for video playback which is purpose built for that ask).  The approach is to leverage existing, purpose built applications that exist on a system and not bundle everything into OSD/OS.

### Understanding the codebase

OSD/OS is a **browsing shell** plus a set of **self-contained modules**. The shell (`AppCore`) discovers modules at startup, exposes settings, and routes actions; each module owns its own QML views and, optionally, a C++ backend. When the user plays something, the shell hands off to a purpose-built tool (mpv for video).

For the full reference — module anatomy, `manifest.json` setting types, `AppCore` / `registerModule`, backend patterns, and the QML view/navigation contract — see **[ARCHITECTURE.md](ARCHITECTURE.md)**.

### Adding a new module

A module is a folder under `modules/` with a `manifest.json` and QML views, plus an optional C++ backend. At a high level:

1. **Create `modules/[name]/`** with a `manifest.json` (identity + settings) and `assets/images/logo.svg`. See the [manifest.json reference](ARCHITECTURE.md#manifestjson-reference).
2. **Add QML views** under `modules/[name]/views/`, with `Root.qml` as the entry point (the module router) plus list/detail views. Follow the [QML view patterns](ARCHITECTURE.md#qml-view-patterns).
3. **(Optional) Add a C++ backend** under `src/modules/[name]/`, add the `.cpp` to `CMakeLists.txt`, and register it with one `registerModule(...)` call in `src/main.cpp`. See [AppCore → registerModule](ARCHITECTURE.md#registermodule--wiring-a-backend-in).

4. **(Optional) Contribute main-menu rows** by declaring `Q_INVOKABLE QVariantList get_menu_entries()` on the backend — `AppCore` probes for it and appends `{name, params}` rows to the main menu, with no changes to the app shell. See [Probed, not connected](ARCHITECTURE.md#probed-not-connected).

5. **Describe it for users** in [docs/wiki/Modules.md](docs/wiki/Modules.md), with a short line in the README's list if it is a big one. The wiki's pages live in `docs/wiki` and are published to the [GitHub wiki](https://github.com/mehmetraif/OSD-OS/wiki) when they change on `main`, so they are reviewed with the code.

A pure-QML module needs **no C++ changes** — the shell discovers it from its manifest. `PlexBackend` is the most complete backend and the best one to study.

**If your module hands off the whole screen** to an external program, use `DisplayHandoff` (`src/util/DisplayHandoff.h`) — never re-implement the VT/DRM ioctls. `MpvController` and the scripts module's `ScriptLauncher` are both worked examples; see [Adding a different hand-off target](ARCHITECTURE.md#adding-a-different-hand-off-target) for the traps, all of which end in a black screen with no way back if you get them wrong.

### Changing an existing module

- **Don't break saved settings.** Users' choices live in `config.json` keyed by setting `key`. Keep existing keys stable (or migrate carefully) so an update doesn't reset someone's configuration.
- **Preserve the manifest contract.** If you add a setting, follow the existing [setting types](ARCHITECTURE.md#setting-types); if it needs dynamic options, wire up the `dynamicOptionsReady` / `apply_slot` pattern rather than inventing a new mechanism.
- **Follow the existing view and navigation patterns** in that module — the `navigateTo` / `goBack` contract and `navListState` position restoration. Don't introduce a different nav style.
- **Reuse shared `Components`** (e.g. `AppBar`) instead of re-implementing them.
- **Keep the window standard.** The title bar at the top, the `HintBar` in its fixed place at the foot, whatever is between them. A question or a notice is a `PromptScreen`: the question in the title bar behind a ? (a notice's behind a !), never a small line in the middle with the hint line under it. A layer that hides the whole view draws its ground with `OsdGround`, never a `Rectangle` of `root.surfaceColor`, so Settings' OSD Background (its window, or black) holds under it too. The window's parts come from the shared components (`AppBar`, `HintBar`, `MenuRow`, `PromptScreen`), and a selected line drawn elsewhere is a `SelectionBox`, so the skin dresses it and the selector effect plays round it. Colours are `root.primaryColor` and `root.surfaceColor`, never a scheme's own, so a theme's reach them too.
- **Change views through `root.changeWindow()`.** A module's router sets its loader's view with `root.changeWindow(loader, source, { "navParams": … })`, never `loader.source` or `setSource()`, so the transition plays and a video's view (`…Player.qml`, `Launch.qml`, …) is known to rest the effects.
- **Nothing over a video.** No effect, no transition and no menu music while a video plays, loads or has its menu open: a layer of a video's tells `Main.qml` with `root.restFor(item, true)` (as `LoadingScreen` and `PlayerMenu` do), and a backend about to play sound of its own holds the menu music off first with `MenuMusic::hold(who)`, and releases it after (as Weather's and Ambient Mode's music do).
- **Keep it self-contained** — module source stays under `modules/[name]` and `src/modules/[name]`.

### Use a Consistent Coding Style

- Please follow the same style as the source you are editing.
- If you are contributing new code, keep the style consistent with other similar works.
- Parameterize as much as possible, try to avoid hard coded values whenever you can.
- **C++**: backends are `QObject` subclasses — use `Q_INVOKABLE` for slots QML calls and `signals:` for callbacks to QML, and persist state as JSON in the data directory, each file written whole with `writeFileAtomically()` (see [C++ Backend Patterns](ARCHITECTURE.md#c-backend-patterns)).
- **QML**: views are `FocusScope`s that declare `navParams` and communicate via the `navigateTo` / `goBack` signals — never call router functions directly (see [QML View Patterns](ARCHITECTURE.md#qml-view-patterns)).

### Testing your change

A few things that are easy to get wrong have regression tests in `tests/` (Local Files' search, settings and state files written whole, themes and skins read from their folders, one video handed over to the next, the menu music and what holds it off; see [tests/README.md](tests/README.md)), and the app itself is started on the main menu and on each module, which must open without a QML warning. CI runs them on every pull request that touches the app or its tests, and you can run them yourself in a minute. Everything else is tested by hand:

- **Build and run** on at least one target (macOS ARM or Raspberry Pi). See [BUILDING.md](BUILDING.md#run).
- **Navigate with a remote/keyboard only** and confirm every screen in your change is reachable and exitable.
- **Check the layout** reads correctly on a CRT (mind overscan) and, ideally, over HDMI/LCD too.
- **Confirm settings persist** across an app restart, and that existing settings still load.
- If you can only test on one platform, please indicate that in your PR.

### Note on AI Use

- I used (and will continue to use) AI tools when building OSD/OS so leveraging AI tools for development is very much allowed. With that in mind, contributors are expected to own and understand the code they submit and any communication in a PR (including code, code comments, and GitHub comments) must come from a human contributor, not an AI agent acting autonomously.
- Pull requests should include a detailed description that outlines the scope of AI involvement (e.g. which parts were AI-generated and what human testing or review was performed prior to submission). PRs that omit this disclosure may be closed without review.

### Best-practices checklist

Before opening a PR, please check your change against these:

- [ ] **Changes work with remote-only navigation** works end to end — up/down/left/right, enter, and esc/backspace. No mouse or complex input added.
- [ ] **Sized and positioned elements using the `root.sh` / `root.sw` properties**, did not hardcode pixel sizes, and kept CRT overscan in mind.
- [ ] **Avoided hardcoded values** where a parameter or existing variable would do the change parameterized as much as possible.
- [ ] **Didn't add tracking or analytics.** The only network calls (if needed) are direct to the third-party API the module integrates with.
- [ ] **Only writes to the local data directory** (`config.json` and module state files) — nothing outside it.
- [ ] **Browse & hand-off** — heavy lifting (like playback) is handed to a purpose-built tool, not bundled in.

## License

By contributing, you agree your contributions are licensed under GPL (please see [LICENSE](LICENSE)).
