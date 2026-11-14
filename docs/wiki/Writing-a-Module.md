# Writing a module

A module adds a source of videos to OSD/OS. This page builds one, with every file whole: **Hello Channel** lists the videos in a folder, plays them, has settings, then gains a C++ backend and tests.

The reference is [ARCHITECTURE.md](https://github.com/mehmetraif/OSD-OS/blob/main/ARCHITECTURE.md): [manifest.json](https://github.com/mehmetraif/OSD-OS/blob/main/ARCHITECTURE.md#manifestjson-reference), [AppCore](https://github.com/mehmetraif/OSD-OS/blob/main/ARCHITECTURE.md#appcore--the-app-shell), [QML View Patterns](https://github.com/mehmetraif/OSD-OS/blob/main/ARCHITECTURE.md#qml-view-patterns). The house rules are in [CONTRIBUTING.md](https://github.com/mehmetraif/OSD-OS/blob/main/CONTRIBUTING.md).

## What a module is

A module browses its own content and hands the video to mpv through `mpvController`. It has up to three parts:

| Part | Where | Needed |
|---|---|---|
| `manifest.json` | `modules/<name>/manifest.json` | Yes: the module's identity and its settings |
| QML views | `modules/<name>/views/`, the entry point `Root.qml` | Yes |
| A C++ backend | `src/modules/<name>/<Name>Backend.h` and `.cpp` | Only when QML can't do it alone: files, a network API, a process |

`AppCore` reads every `modules/*/manifest.json` at start, in the order of the folders' names. A module with a backend also adds one `registerModule(...)` call to `src/main.cpp`.

```mermaid
flowchart LR
    MF["modules/hello_channel/manifest.json"] -->|read at start| AC["AppCore"]
    AC -->|enabled: a row| ML["Main menu"]
    AC -. its settings .-> MS["Settings → Hello Channel"]
    ML -->|select| RT["views/Root.qml, the router"]
    RT --> CH["Channels.qml, a list"]
    CH -->|select on About| DT["Detail.qml, a leaf"]
    CH -->|select on a video| PL["Player.qml"]
    PL -->|loadAndPlay| MP["mpvController, then mpv"]
    MP -->|playbackEnded| PL
```

Five principles from CONTRIBUTING.md:

1. **A remote is enough.** Every screen works with the arrows, select and back. A gamepad sends the same keys: never add gamepad code to a view.
2. **Laid out for a CRT.** Size everything from `root.sw` and `root.sh`, never in pixels.
3. **Self-contained.** The module's files stay in `modules/<name>` and `src/modules/<name>`.
4. **No tracking.** A module talks only to the service it shows, and writes only to the data folder.
5. **Browse, then hand off.** Playing is handed to mpv, or to another program through `DisplayHandoff`.

## The manifest

`manifest.json` is the module's identity and its settings. Settings builds the module's page from it.

| Key | Needed | What it is |
|---|---|---|
| `id` | Yes | The module's id, `com.osdos.<name>` by convention. Settings are saved under `modules.<id>` in `config.json`, lists under it in `lists.json` |
| `name` | | Shown on the main menu, as the Settings row and in the title bar |
| `icon` | | Its logo, relative to the module's folder |
| `entry_point_qml` | Yes | Its router, relative to the module's folder: `views/Root.qml` |
| `settings` | | Its settings, in the order Settings shows them. A module with none isn't listed in Settings |
| `settings_group` | | Where Settings lists the page: `"tv"` under **Modules** on the **TV / Movie** page; `"arcade"` on the **Arcade** page, under the module's name as a heading; left out, under **Modules** on the **System** page |

A manifest that isn't JSON is skipped, with `[AppCore] Bad manifest.json in <folder>: <error>` in the log. One without `id` or `entry_point_qml` is skipped with `[AppCore] Skipping <folder>: manifest missing 'id' or 'entry_point_qml'`.

Each setting is an object:

| Key | For | What it does |
|---|---|---|
| `key` | Every type | Its key under `modules.<id>`. A dot reaches one level down (`libraries.abc` is `"libraries": { "abc": … }`) |
| `label` | Every type | The row's text, shown in capitals |
| `type` | Every type | One of the types below |
| `default` | `toggle`, `list_single`, `slider` | What the row shows while nothing is saved: `"ON"` or `"OFF"`, one of the options, or a slider's number. OSD/OS doesn't write it: your code must read an unset key as this default too |
| `description` | Every type | The help line while the row is selected. Lines split with `\n` are joined with `•`. By convention `[WORD]` explains one choice |
| `options` | `list_single` | The choices, saved as written |
| `options_source`, `options_slot` | `list_single`, `multiselect_submenu` | `"dynamic"`, and the backend slot that sends the choices ([part 3](#part-3-a-c-backend)) |
| `apply_slot` | `list_single` with dynamic choices | A backend slot called after each change |
| `action_slot` | `action` | The backend slot select calls |
| `params` | `module_view` | The `navParams` the module's `Root.qml` opens with |
| `settings` | `submenu` | Its own rows, in this same format |
| `step`, `start_text`, `end_text` | `slider` | How far ◄ ► move it (10 unless given), and the names under the bar's two ends |
| `requires_auth` | Every type | Usable only while the backend's `get_auth_state()` is neither `""` nor `"none"`. Dimmed until then (`Only once signed in`) |
| `requires_capability` | Every type | Usable only while the backend's choices for `_capabilities` name it. Dimmed otherwise (`Not offered by the server`) |
| `shown_with` | Every type | Usable only while another row has a value as the page shows it: `{ "key": "screen", "value": "Arcade" }`. A toggle's value is `"ON"` or `"OFF"`; an unset row counts as its `default`. A list of them holds while any holds. Dimmed otherwise; a dimmed row keeps what was saved |

The types, as Settings shows and saves them:

| `type` | In Settings | Saved as |
|---|---|---|
| `toggle` | `ON` or `OFF`; ◄ ► and select flip it | `true` or `false` |
| `list_single` with `options` | ◄ ► step through the choices, round the ends | The choice's text |
| `list_single` with `"options_source": "dynamic"` | The backend's labels. Unset, the row shows the first choice, so put the default first | The choice's `id` |
| `multiselect_submenu` | Select opens a page of the backend's choices, each switched on or off. A heading (`{ "label": "Genres", "heading": true }`) turns the choices under it on or off together | `{ "<id>": true, … }` under the key, saved whole at every change. A choice is on until switched off, unless sent with `"default": false` |
| `submenu` | Select opens its rows on a page of their own, titled `MODULE / LABEL` | Its rows' keys, flat under `modules.<id>` |
| `module_view` | Select opens the module itself, its `Root.qml` with `params` | Nothing |
| `directory_browser` | Select opens the folder picker. The row shows the path, or `Default` | The folder's path; `""` for **Default Folder**, which the module gives a meaning |
| `slider` | The deck's tape bar under its label; ◄ ► move it a `step`. Each step reaches `onSettingChanged()` and `get_setting()` at once; `config.json` has it once the steps rest | A number from 0 to 100 |
| `action` | Select calls `action_slot` | Nothing |

There is no free-text type. A view of your own can open the `OnScreenKeyboard`.

**`enabled`** is a setting with one more meaning: the module is on the main menu while `modules.<id>.enabled` is true or, unset, while its manifest's `enabled` default isn't `"OFF"`. A module without an `enabled` setting is always on.

## Part 1: a pure-QML module

Hello Channel's first version is QML only: four files and a logo, no build.

```text
modules/hello_channel/
├── manifest.json
├── assets/
│   └── images/
│       └── logo.svg
└── views/
    ├── Root.qml
    ├── Channels.qml
    ├── Detail.qml
    └── Player.qml        (part 2)
```

### manifest.json

An `enabled` toggle, a folder, a fixed list, another toggle, and the module's own Scaling:

```json
{
  "id": "com.osdos.hello_channel",
  "name": "Hello Channel",
  "icon": "assets/images/logo.svg",
  "entry_point_qml": "views/Root.qml",
  "settings": [
    {
      "key": "enabled",
      "label": "Enabled",
      "type": "toggle",
      "default": "ON"
    },
    {
      "key": "video_folder",
      "label": "Video Folder",
      "type": "directory_browser",
      "default": "",
      "description": "The folder whose videos the channel lists\n[DEFAULT] None: pick one here first"
    },
    {
      "key": "sort_order",
      "label": "Sort Order",
      "type": "list_single",
      "options": ["A to Z", "Z to A"],
      "default": "A to Z",
      "description": "The order the videos are listed in"
    },
    {
      "key": "loop_playback",
      "label": "Loop Playback",
      "type": "toggle",
      "default": "OFF",
      "description": "Play a video over and over until you stop it"
    },
    {
      "key": "video_scaling",
      "label": "Scaling",
      "type": "list_single",
      "options": ["Default", "Letterbox", "14:9", "Pan & Scan", "Anamorphic"],
      "default": "Default",
      "description": "How a 16:9 picture fills the 4:3 screen in this module\n[DEFAULT] As Settings' Scaling says"
    }
  ]
}
```

`video_scaling` needs no code: while a module's views are open, the player takes that module's `video_scaling` (unless `Default`) over Settings' Scaling.

### The logo

`assets/images/logo.svg`, a pixel-art TV on a 20 × 20 grid, in one colour:

```xml
<?xml version="1.0" encoding="utf-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20">
  <path fill-rule="evenodd" style="fill: rgb(255, 255, 255);"
        d="M5 0H7V2H5Z M13 0H15V2H13Z M7 2H9V4H7Z M11 2H13V4H11Z
           M0 4H20V18H0Z M2 6V16H15V6Z M17 7V9H19V7Z M17 11V13H19V11Z
           M2 18H5V20H2Z M15 18H18V20H15Z"/>
</svg>
```

The title bar draws the logo from its alpha, in the colour scheme's text colour: draw it white on clear. A skin can replace it by the folder's name, with `"icons": { "hello_channel": "tv.png" }` in its `skin.json` ([Skins](https://github.com/mehmetraif/OSD-OS/wiki/Skins)).

### Root.qml: the router

The shell loads `Root.qml` and listens for its `goBack()`, which leaves the module. The router keeps a stack of views in one `Loader`:

```qml
import QtQuick

// Hello Channel's router: the module's own stack of views. The shell loads
// this file (the manifest's entry_point_qml) and listens for goBack(), which
// leaves the module.
FocusScope {
    id: moduleRoot

    // Leaves the module, back to the main menu.
    signal goBack()

    property var navParams: ({})

    // The manifest's id: the one place it appears in this module's QML.
    // The views read it as moduleRoot.moduleId.
    property string moduleId: "com.osdos.hello_channel"
    property var _moduleInfo: appCore ? appCore.get_module_info(moduleId) : ({})
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

    // Shows a view in place of this one, without pushing it on the stack.
    function replaceWith(viewPath, params) {
        var resolved = Qt.resolvedUrl(viewPath)
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
        // The list state the view left with comes back in its navParams.
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
            function onReplaceWith(path, params) { moduleRoot.replaceWith(path, params) }
            function onGoBack() { moduleRoot.navigateBack() }
        }
    }

    // The first view. resumePlayer is set when the main menu's first row
    // takes a video playing behind the menus back (Transparent Background).
    Component.onCompleted: navigateTo("Channels.qml", { resumePlayer: navParams.resumePlayer })
}
```

- **`id: moduleRoot`, always.** Views reach the router by that id: `moduleRoot.moduleId`, `moduleRoot.moduleName`, `moduleRoot.moduleIcon`.
- **The id once.** `moduleId` is the only place the manifest's id is written in the module's QML.
- **`root.changeWindow(loader, source, properties)`**, never `loader.source` or `setSource()`: it plays the transition, and rests effects and menu music for a video's view.
- **Position comes back.** A view leaves with a list state (`{ currentIndex: … }`) and gets it back as `navParams.navListState`.
- **Starting params** are in the router's own `navParams`: `resumePlayer` (below), `fromAppStartup` when OSD/OS opened the module at start, and `startupPlay`, the favourite to play at once.

### Channels.qml: a list view

The channel's first page: the videos of the Video Folder, a heading, and **About This Channel**. A view is a `FocusScope` that declares `navParams` and talks to the router only through its signals:

```qml
import QtQuick
import Components

// The channel's first page: the videos in the Video Folder, then ABOUT THIS
// CHANNEL. Select plays a video, or opens About.
FocusScope {
    id: channelsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    readonly property var videoTypes: ["mp4", "mkv", "avi", "mov", "m4v", "webm"]
    property var rows: []

    // The folder the setting names: "" until one is picked.
    function folder() {
        return appCore.get_setting(moduleRoot.moduleId, "video_folder") || ""
    }

    // One row per video: { type, label, value, path }.
    function videoRows() {
        var dir = folder()
        if (dir === "")
            return [{ type: "empty", label: "No Folder Chosen", value: "" }]
        var out = []
        var entries = appCore.folderEntries(dir, videoTypes)
        for (var i = 0; i < entries.length; i++) {
            if (entries[i].isFolder)
                continue
            var name = entries[i].name
            out.push({ type: "video", label: name.replace(/\.[^.]+$/, ""),
                       value: name.replace(/^.*\./, "").toUpperCase(), path: entries[i].path })
        }
        if ((appCore.get_setting(moduleRoot.moduleId, "sort_order") || "A to Z") === "Z to A")
            out.reverse()
        return out.length > 0 ? out : [{ type: "empty", label: "No Videos", value: "" }]
    }

    function reload() {
        rows = videoRows().concat([{ type: "section", label: "Channel" },
                                   { type: "about", label: "About This Channel" }])
    }

    Component.onCompleted: {
        reload()
        // Back from a view: the cursor on the row it was on.
        if (navListState.currentIndex !== undefined)
            list.currentIndex = Math.min(navListState.currentIndex, rows.length - 1)
        // From the main menu's row for a video playing behind the menus: its
        // player, which takes it back. Deferred: the router listens to this
        // view only once it has been made.
        var behind = navParams.resumePlayer
        if (!navParams.navListState && behind && behind.filePath)
            Qt.callLater(function() {
                channelsRoot.navigateTo("Player.qml", behind, { currentIndex: list.currentIndex })
            })
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    MenuList {
        id: list
        model: channelsRoot.rows
        focus: true

        Keys.onReturnPressed: {
            var row = channelsRoot.rows[currentIndex]
            if (!row)
                return
            if (row.type === "video")
                channelsRoot.navigateTo("Player.qml", { filePath: row.path, title: row.label },
                                        { currentIndex: currentIndex })
            else if (row.type === "about")
                channelsRoot.navigateTo("Detail.qml", { videoCount: channelsRoot.rows.filter(
                                            function(r) { return r.type === "video" }).length },
                                        { currentIndex: currentIndex })
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                channelsRoot.goBack()
                event.accepted = true
            }
        }

        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            heading: modelData.type === "section"
            keepValue: modelData.type === "video"
            label: modelData.label || ""
            value: modelData.value || ""
            selected: list.currentIndex === index
        }
    }

    HelpLine {
        readonly property var row: channelsRoot.rows[list.currentIndex]
        text: !row ? ""
            : row.type === "video" ? row.path
            : row.type === "empty" ? "Pick a folder in Settings, Hello Channel, Video Folder"
            : "The channel's settings, and how many videos it has"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        readonly property var row: channelsRoot.rows[list.currentIndex]
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + (row && row.type === "video" ? root.hints.select + ":PLAY"
                 : row && row.type === "about" ? root.hints.select + ":OPEN" : "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
```

- **`navigateTo(path, params, listState)`** takes the view (a path in `views/`), its params, and the state this view wants back.
- **`Qt.callLater`** for a `navigateTo` from `Component.onCompleted`: the router connects to a view only once it is made.
- **`goBack()`** goes one view back; only the router's own `goBack()` leaves the module.
- **Settings.** `appCore.get_setting(moduleId, key)` returns `undefined` for an unset key: fall back to the manifest's `default` yourself.
- **Files.** `appCore.folderEntries(path, types)` returns `[{ name, path, isFolder }]`: folders, then files of those types, hidden ones left out.
- **`MenuList`**'s cursor skips rows of `type: "section"`.
- **`MenuRow`**: `heading` makes a group's heading, `keepValue` cuts the label short rather than the value, `opens` ends the line in a ►, `changes` puts the value between ◄ and ►.
- **`HintBar`**'s text is built from **`root.hints`**; never type `[ESC]`, and never bind to `inputManager.hints`.
- **Sizes** are fractions of `root.sw` and `root.sh`, with the 640 × 480 pixels in a comment.
- **Colours and font**: `root.primaryColor` on `root.surfaceColor`, in `root.globalFont`.

### Detail.qml: a leaf view

**About This Channel** is a leaf: it only goes back, so it declares `goBack()` and no `navigateTo`:

```qml
import QtQuick
import Components

// About This Channel: a page to read, not to choose from. A leaf view: it
// only goes back.
FocusScope {
    id: detailRoot

    property var navParams: ({})

    signal goBack()

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    // Each line as Settings would show it: a toggle unset is its default.
    readonly property var lines: [
        { label: "Folder", value: appCore.get_setting(moduleRoot.moduleId, "video_folder") || "None" },
        { label: "Videos", value: String(navParams.videoCount || 0) },
        { label: "Sort Order", value: appCore.get_setting(moduleRoot.moduleId, "sort_order") || "A to Z" },
        { label: "Loop Playback", value: appCore.get_setting(moduleRoot.moduleId, "loop_playback") ? "On" : "Off" },
        { label: "OSD/OS", value: root.appVersion }
    ]

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "About"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    Column {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492

        Repeater {
            model: detailRoot.lines
            delegate: MenuRow {
                width: parent.width
                height: root.sh * 0.0583333 //28
                label: modelData.label
                value: modelData.value
                // Read, not chosen: a long value scrolls all the time.
                alwaysScroll: true
            }
        }
    }

    HintBar {
        text: root.hints.back + ":BACK"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
```

A question or a notice that hides the whole view is a `PromptScreen` ([Components](#the-shared-components)).

### Trying it

Copy the folder into `modules/` and start OSD/OS (QML needs no build):

```sh
APP_ROOT=$(pwd) ./build/osdos
```

**HELLO CHANNEL** is on the main menu. Its list says **No Folder Chosen** until Settings → Hello Channel → Video Folder names one:

<table>
<tr><th width="50%">The modules in Settings</th><th width="50%">A module's settings</th></tr>
<tr><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/settings-modules.png" width="100%" alt="Settings → TV / Movie: each video module is turned on and set up from its own row, under the MODULES heading" /></td><td><img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/module-settings.png" width="100%" alt="Local Files' settings page, made from its manifest" /></td></tr>
<tr><td>The video modules' rows, on the TV / Movie page (<code>"settings_group": "tv"</code>). Hello Channel names no group, so its row is on the System page.</td><td>A settings page made from a manifest, Local Files' here.</td></tr>
</table>

Changing a row saves it at once (a slider's steps, half a second after the last, or as the page is left). With Sort Order on Z to A and Loop Playback on, `config.json` has:

```json
{
    "modules": {
        "com.osdos.hello_channel": {
            "loop_playback": true,
            "sort_order": "Z to A",
            "video_folder": "/home/pi/Videos"
        }
    }
}
```

## Part 2: playing a video

Select on a video opens `Player.qml`, which hands the file to mpv:

```qml
import QtQuick
import Components

// Plays one file through mpv the way Local Files' player does: a tape
// loading until the first position comes, the remote's keys handed to mpv's
// on-screen controls, and one playbackEnded handler that every end goes
// through. Where the video stopped goes on the module's "recent" list, so it
// starts there next time. Named …Player.qml, so the effects and the menu
// music rest while it is shown.
FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string filePath: navParams.filePath || ""
    property string itemTitle: navParams.title || ""

    // The first position has come: the loading screen goes.
    property bool playbackStarted: false
    property int startMs: 0
    property int lastKnownPositionMs: 0
    property int lastKnownDurationMs: 0

    focus: true

    // Where this file stopped last time: its entry on the module's own
    // "recent" list (AppCore keeps it in lists.json).
    function savedPosition() {
        var recent = appCore.get_list(moduleRoot.moduleId, "recent")
        for (var i = 0; i < recent.length; i++) {
            if (recent[i].path === filePath)
                return recent[i].pos || 0
        }
        return 0
    }

    function play(fromMs) {
        playbackStarted = false
        startMs = fromMs
        var loop = !!appCore.get_setting(moduleRoot.moduleId, "loop_playback")
        // url, start (s), audio track (0: mpv's choice), subtitles (-2: off),
        // subtitle files, subtitle languages, loop.
        mpvController.loadAndPlay(filePath, fromMs / 1000.0, 0, -2, [], [], loop)
        // With Transparent Background, how the main menu's first row takes
        // the video back once it plays behind the menus.
        mpvController.noteSession({ module: moduleRoot.moduleId, title: itemTitle,
                                    params: { filePath: filePath, title: itemTitle } })
    }

    Component.onCompleted: play(savedPosition())

    // mpv has the picture: the keys go to its on-screen controls.
    Keys.onPressed: function(event) {
        var key = ""
        switch (event.key) {
        case Qt.Key_Escape:
        case Qt.Key_Back:      key = "ESC"; break
        case Qt.Key_Backspace: key = "BS"; break
        case Qt.Key_Up:        key = "UP"; break
        case Qt.Key_Down:      key = "DOWN"; break
        case Qt.Key_Left:      key = "LEFT"; break
        case Qt.Key_Right:     key = "RIGHT"; break
        case Qt.Key_Space:     key = "SPACE"; break
        case Qt.Key_Return:
        case Qt.Key_Enter:     key = "ENTER"; break
        }
        if (key !== "") {
            mpvController.sendKey(key)
            event.accepted = true
        }
    }

    Connections {
        target: mpvController
        function onPositionChanged(ms) {
            if (ms > 0) {
                playerRoot.lastKnownPositionMs = ms
                playerRoot.playbackStarted = true
            }
        }
        function onDurationChanged(ms) {
            if (ms > 0)
                playerRoot.lastKnownDurationMs = ms
        }
        // Every end comes here once: "eof", "stopped" or "failed". By then
        // mpv has gone, so every branch goes back (or starts another video):
        // a player that stays shown over a dead mpv freezes the app.
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            var pos = playerRoot.lastKnownPositionMs || finalPositionMs
            var dur = playerRoot.lastKnownDurationMs || finalDurationMs
            var done = reason === "eof" || (dur > 0 && pos >= dur * 0.95)
            if (reason === "failed")
                console.warn("[HelloChannel] mpv could not play " + playerRoot.filePath)
            appCore.add_to_list(moduleRoot.moduleId, "recent",
                                { name: playerRoot.itemTitle, path: playerRoot.filePath,
                                  pos: done ? 0 : pos }, 30)
            playerRoot.goBack()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"

        // A tape loading, until the picture comes.
        LoadingScreen {
            anchors.fill: parent
            source: moduleRoot.moduleName
            startMs: playerRoot.startMs
            durationMs: playerRoot.lastKnownDurationMs
            visible: !playerRoot.playbackStarted
        }
    }
}
```

```mermaid
sequenceDiagram
    participant P as Player.qml
    participant C as mpvController
    participant M as mpv
    P->>C: loadAndPlay(file, start, 0, -2, [], [], loop)
    P->>C: noteSession({ module, title, params })
    C->>M: starts it (an mpv process, or libmpv in the window)
    M-->>C: time-pos, duration
    C-->>P: positionChanged, durationChanged
    Note over P: the loading screen goes at the first position
    M-->>C: ends: the file's end, back, an error
    C-->>P: playbackEnded(position, duration, reason)
    P->>P: keeps where it stopped, then goBack()
```

### loadAndPlay

`mpvController.loadAndPlay()` takes the video and how to play it. Everything after the first four arguments has a default:

| # | Argument | Hello Channel passes | What it is |
|---|---|---|---|
| 1 | `url` | the file's path | A file, a playlist (`.m3u`) or a stream's URL |
| 2 | `startSeconds` | the saved position | Where to start; under half a second, from the beginning |
| 3 | `audioTrack` | `0` | `0` for mpv's choice, else a track's number |
| 4 | `subTrack` | `-2` | `-2` subtitles off, `0` on, `-1` forced only (needs mpv 0.38 or newer), above 0 a track's number |
| 5, 6 | `subFiles`, `subLangs` | `[]`, `[]` | Subtitle files to load; preferred subtitle languages |
| 7 | `loop` | Loop Playback | Over and over (`--loop-playlist=inf`) |
| 8 | `playlistStart` | | Where in a playlist to start, `-1` for its start |
| 12 | `oscMode` | | `"ambient"` for Ambient:Mode's own on-screen controls |
| 13 | `shuffle` | | A playlist in random order |
| 15, 16 | `imageDurationSec`, `imageContent` | | For pictures: how long each shows |
| 17 | `extraArgs` | | mpv options of the module's own: YouTube passes `--ytdl=yes`, with its yt-dlp options |
| 19 | `extraUrls` | | More files after the first, one playlist |

The app adds the rest: the board's decoder and output, Scaling, Video Levels, Audio Output, the channel logo and the IPC socket ([Playback and mpv](https://github.com/mehmetraif/OSD-OS/wiki/Playback-and-mpv)).

### The rules a player keeps

- **One handler for every end.** `playbackEnded(finalPositionMs, finalDurationMs, reason)` comes once, after mpv has gone. Every branch must go back or start the next video.
- **The reasons**: `"eof"` (the end of the file), `"stopped"` (back, a stop, or a crash), `"failed"` (mpv's exit code 2), `"menu"` (only for a player whose note says `menu: true`).
- **A failure says why.** OSD/OS's own players show a `PlaybackNotice` at `failed` (`showMpv(canRetry)`), as CONTRIBUTING.md asks. Hello Channel only logs it, to stay short.
- **The keys go to mpv** through `mpvController.sendKey()`, by mpv's names (`ESC`, `ENTER`, `UP`, …). `ESC` ends the video, or first closes mpv's deck menu.
- **The name.** A view whose file name ends in `Player.qml`, `Play.qml`, `Launch.qml`, `Takeover.qml`, `SignIn.qml` or `Console.qml` is a video's view: no effect, transition or menu music plays while it shows. A layer elsewhere says so with `root.restFor(item, true)`.
- **Where it stopped.** `appCore.add_to_list(moduleId, name, entry, limit)` puts an entry first in `lists.json`, in place of one with its `path`, and keeps the newest `limit`. `get_list()`, `remove_from_list()` and `list_contains()` read and change the lists.

<img src="https://raw.githubusercontent.com/mehmetraif/OSD-OS/main/docs/screenshots/loading.png" width="100%" alt="The loading screen: TAPE A, TAPE B LOADING and the source, over a tape's noise" />

`LoadingScreen` as YouTube's player shows it: the module's name (`source`), TAPE A at the start (`startMs`), TAPE B with the length once known (`durationMs`).

### Transparent Background, and the main menu's first row

With Transparent Background on, back returns to the menus with the video playing behind them. The player still gets `playbackEnded(…, "stopped")` and goes back. Two things let the user take the video back:

1. **`noteSession({ module, title, params })`**, right after `loadAndPlay()`. While the video plays behind the menus, the main menu leads with a row for it, `► TEST CARD`.
2. **Routing `resumePlayer`.** Select on that row opens the module with `navParams.resumePlayer`, the `params` noted. `Root.qml` passes it to `Channels.qml`, which opens the player with it. `mpvController` then brings the playing video back to full screen.

A player with a menu of its own (`menu: true` in its note) gets the row without Transparent Background too. Its menu's Browse, with nothing playing (`mpvController.videoActive` false), calls `mpvController.leaveSession()` before going back. Opened with the noted `params`, the player finds its video in `root.takeBackNote`.

## Part 3: a C++ backend

A backend is a `QObject` the views call, for what QML alone can't do: a service's API (`QNetworkAccessManager`), a process, state in JSON files. Hello Channel's gives the module a folder of its own, lists it, and fills the **Video Type** setting's choices.

```text
src/modules/hello_channel/
├── HelloChannelBackend.h
└── HelloChannelBackend.cpp
```

### HelloChannelBackend.h

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>

// Hello Channel's backend: the folder its videos are in (the Video Folder
// setting, or <data folder>/hello_channel), what is in it, and the choices
// of the Video Type setting, which depend on what is in it.
class HelloChannelBackend : public QObject {
    Q_OBJECT
public:
    explicit HelloChannelBackend(const QString &dataRoot, QObject *parent = nullptr);

    // The folder in force: the setting's, else the module's own.
    Q_INVOKABLE QString videoFolder() const { return m_folder; }
    // Its videos of one type ("all", or an extension such as "mkv"), by
    // name, as [{ name, path, type }].
    Q_INVOKABLE QVariantList videos(const QString &type = QStringLiteral("all")) const;

    // manifest: video_type (list_single, "options_source": "dynamic")
    Q_INVOKABLE void getVideoTypes();
    // manifest: rescan (action)
    Q_INVOKABLE void rescan();

public slots:
    // Connected to AppCore::moduleSettingChanged by registerModule, because
    // it is declared with exactly this signature.
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

signals:
    // Re-emitted by AppCore as dynamicOptionsReady(moduleId, key, options).
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    // The folder, or what is in it, changed: the list reads it again.
    void videosChanged();

private:
    void setFolder(const QString &path);

    QString m_dataRoot;
    QString m_folder;
};
```

### HelloChannelBackend.cpp

```cpp
#include "HelloChannelBackend.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

static const char *kModuleId = "com.osdos.hello_channel";
static const QStringList kVideoTypes = { "mp4", "mkv", "avi", "mov", "m4v", "webm" };

HelloChannelBackend::HelloChannelBackend(const QString &dataRoot, QObject *parent)
    : QObject(parent), m_dataRoot(dataRoot)
{
    // The saved folder, read straight from config.json, as the Scripts and
    // Ambient Mode backends read theirs as they are made.
    QString configured;
    QFile f(m_dataRoot + QStringLiteral("/config.json"));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject cfg = QJsonDocument::fromJson(f.readAll()).object();
        configured = cfg.value("modules").toObject().value(QLatin1String(kModuleId)).toObject()
                         .value("video_folder").toString();
    }
    setFolder(configured);
}

void HelloChannelBackend::setFolder(const QString &path) {
    // "" (the picker's Default Folder) is the module's own folder.
    m_folder = path.isEmpty() ? m_dataRoot + QStringLiteral("/hello_channel") : path;
    QDir().mkpath(m_folder);
    qInfo("[HelloChannel] video folder: %s", qPrintable(m_folder));
}

QVariantList HelloChannelBackend::videos(const QString &type) const {
    QVariantList list;
    const QFileInfoList files = QDir(m_folder).entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &file : files) {
        const QString suffix = file.suffix().toLower();
        if (!kVideoTypes.contains(suffix) || (type != QLatin1String("all") && suffix != type))
            continue;
        list.append(QVariantMap{ { "name", file.completeBaseName() },
                                 { "path", file.absoluteFilePath() },
                                 { "type", suffix } });
    }
    return list;
}

void HelloChannelBackend::getVideoTypes() {
    // "All" first: Settings shows the first choice while none is saved.
    QVariantList options{ QVariantMap{ { "id", "all" }, { "label", "All" } } };
    const QVariantList found = videos();
    for (const QString &type : kVideoTypes) {
        for (const QVariant &video : found) {
            if (video.toMap().value("type").toString() == type) {
                options.append(QVariantMap{ { "id", type }, { "label", type.toUpper() } });
                break;
            }
        }
    }
    emit dynamicOptionsReady(QStringLiteral("video_type"), options);
}

void HelloChannelBackend::rescan() {
    // Settings' Video Type row takes the new choices at once.
    getVideoTypes();
    emit videosChanged();
}

void HelloChannelBackend::onSettingChanged(const QString &moduleId, const QString &key,
                                           const QVariant &value) {
    // Every module's changes come here: only this module's matter.
    if (moduleId != QLatin1String(kModuleId))
        return;
    if (key == QLatin1String("video_folder")) {
        setFolder(value.toString());
        emit videosChanged();
    }
}
```

- **The folder is resolved in the constructor**, from `config.json`. `main.cpp` never touches a module's paths.
- **`Q_INVOKABLE`** for what QML calls, **signals** for what it hears back.
- **Logging** with the module's prefix: `qInfo("[HelloChannel] …")` for a Release build's log, `qWarning` for what went wrong, `qDebug` for builds from source only ([Troubleshooting → Reading the log](https://github.com/mehmetraif/OSD-OS/wiki/Troubleshooting#reading-the-log)).
- **State on disk** goes through `writeFileAtomically()` (`src/util/AtomicFile.h`), owner-only for a token or a key, never a `QFile` opened for writing.

### Wiring it in

Three lines in `src/main.cpp`, beside the other backends:

```cpp
#include "modules/hello_channel/HelloChannelBackend.h"
```

```cpp
    AmbientModeBackend  ambientMode(dataRoot);
    HelloChannelBackend helloChannel(dataRoot);
```

```cpp
    appCore.registerModule("com.osdos.playlists",    "playlistsBackend",   &playlistsBackend, ctx);
    appCore.registerModule("com.osdos.hello_channel", "helloChannelBackend", &helloChannel, ctx);
```

and its source in `CMakeLists.txt`'s `add_executable(osdos …)`:

```cmake
    src/modules/playlists/ServerDownload.cpp
    src/modules/hello_channel/HelloChannelBackend.cpp
```

Then configure and build again (`cmake -B build && cmake --build build`). `registerModule(moduleId, contextProperty, backend, ctx)` makes the backend the QML context property `helloChannelBackend` and connects what it declares, looked up by name and signature:

| Declared on the backend | Becomes |
|---|---|
| signal `dynamicOptionsReady(QString, QVariant)` | `appCore.dynamicOptionsReady(moduleId, key, options)`, which fills a dynamic row |
| signal `authStateChanged()` | `appCore.moduleAuthStateChanged(moduleId)`: Settings checks the `requires_auth` rows again |
| slot `onSettingChanged(QString, QString, QVariant)` | Called by `appCore.moduleSettingChanged` for every module's setting saved, not for an app setting |
| property `QStringList statusIcons`, with a notify signal | Gathered into `appCore.statusIcons`: `PixelIcon` names shown at the right end of every title bar while the module is on |
| `Q_INVOKABLE QString get_auth_state()` | Asked by `appCore.get_module_auth_state()` for the `requires_auth` rows |
| `Q_INVOKABLE QVariantList get_menu_entries()` | Asked as the main menu is built: rows `{ name, params }` after the modules' rows |

Settings calls `options_slot`, `action_slot` and `apply_slot` through `appCore.invoke_module_action(moduleId, slot)`, queued and with no arguments. They must be `Q_INVOKABLE` (or slots) that take none. A missing slot is logged: `[AppCore] invoke_module_action: slot '<name>' not found on backend '<id>'`.

### The settings that need it

In `manifest.json`, the folder's default gets a meaning, and two rows join, a dynamic list and an action:

```json
    {
      "key": "video_folder",
      "label": "Video Folder",
      "type": "directory_browser",
      "default": "",
      "description": "The folder whose videos the channel lists\n[DEFAULT] hello_channel in the data folder"
    },
    {
      "key": "video_type",
      "label": "Video Type",
      "type": "list_single",
      "options_source": "dynamic",
      "options_slot": "getVideoTypes",
      "default": "all",
      "description": "Which of the folder's videos the channel lists\n[ALL] Every one"
    },
```

and, after `video_scaling`:

```json
    {
      "key": "rescan",
      "label": "Rescan Folder",
      "type": "action",
      "action_slot": "rescan",
      "description": "Read the folder again, for videos copied in since"
    }
```

As the settings page opens, Settings calls `getVideoTypes()`, which fills Video Type with **All** and one choice per type in the folder. ◄ ► save the chosen one's `id`: `"video_type": "mp4"`. Rescan Folder sends the choices again.

### The views, with the backend

`Channels.qml` asks the backend for the videos, and listens to it. Take out `videoTypes` and `folder()`, and put in this `videoRows()`:

```qml
    // One row per video: { type, label, value, path }, from the backend.
    function videoRows() {
        var type = appCore.get_setting(moduleRoot.moduleId, "video_type") || "all"
        var videos = helloChannelBackend.videos(type)
        var out = []
        for (var i = 0; i < videos.length; i++)
            out.push({ type: "video", label: videos[i].name, value: videos[i].type.toUpperCase(),
                       path: videos[i].path })
        if ((appCore.get_setting(moduleRoot.moduleId, "sort_order") || "A to Z") === "Z to A")
            out.reverse()
        return out.length > 0 ? out : [{ type: "empty", label: "No Videos", value: "" }]
    }
```

then, after `Component.onCompleted`, the backend's signal:

```qml
    // The backend says when the folder, or what is in it, changed.
    Connections {
        target: helloChannelBackend
        function onVideosChanged() { channelsRoot.reload() }
    }
```

and, in the `HelpLine`, the empty list's line:

```qml
            : row.type === "empty" ? "Copy videos into " + helloChannelBackend.videoFolder()
```

In `Detail.qml`, the folder line asks the backend too:

```qml
        { label: "Folder", value: helloChannelBackend.videoFolder() },
```

A view that names `helloChannelBackend` runs only in a build with the backend.

### More a backend can do

| To | Do as | See |
|---|---|---|
| Sign in to a service | `get_auth_state()`, `authStateChanged()`, `requires_auth` | `PlexBackend`, the most complete backend |
| Put rows on the main menu | `get_menu_entries()`; its `params` reach `Root.qml` as `navParams` | `ScriptsBackend` |
| Hand the whole screen to another program | `DisplayHandoff`, never your own VT or DRM calls | `ScriptLauncher`, and [Adding a different hand-off target](https://github.com/mehmetraif/OSD-OS/blob/main/ARCHITECTURE.md#adding-a-different-hand-off-target) |
| Play sound of its own in the menus | `MenuMusic::hold(who)` first, `release(who)` after | `WeatherBackend`, `AmbientModeBackend` |
| Play from an NFC card | A `CardPlay.qml`, a `cardRef` branch in `Root.qml` ahead of any sign-in screen, a row in the NFC Reader's `kHandoffModules` | [Card Hand-off](https://github.com/mehmetraif/OSD-OS/blob/main/ARCHITECTURE.md#card-hand-off-nfc--a-module), Plex's `CardPlay.qml` |
| Put its videos on playlists | A key prefix, an entry and a URL to play in `PlaylistsBackend`, or `MediaServer` for a media server | [Playlists](https://github.com/mehmetraif/OSD-OS/blob/main/ARCHITECTURE.md#playlists-videos-from-several-modules) |

## Part 4: testing

### Under Xvfb

Xvfb is an X server with a screen in memory. xdotool presses keys on it, and ImageMagick's `import` photographs it. On Debian or Ubuntu: `sudo apt install xvfb xdotool imagemagick ffmpeg`. Then:

```sh
#!/bin/sh
# Hello Channel on a screen of its own, driven like a remote. From the
# repository's folder, after cmake --build build.
set -e
D=/tmp/hello-data
rm -rf "$D" && mkdir -p "$D/hello_channel"

# A minute of test card, with a tone, as the channel's video.
ffmpeg -loglevel error -f lavfi -i testsrc=duration=60:size=640x480:rate=25 \
       -f lavfi -i sine=frequency=440:duration=60 -shortest \
       -pix_fmt yuv420p "$D/hello_channel/Test Card.mp4"

# The Video Folder, set before the app starts (the pure-QML version needs it).
printf '{ "modules": { "com.osdos.hello_channel": { "video_folder": "%s/hello_channel" } } }\n' "$D" \
    > "$D/config.json"

Xvfb :99 -screen 0 640x480x24 > /dev/null 2>&1 & XVFB=$!
sleep 1
export DISPLAY=:99
DATA_ROOT="$D" APP_ROOT="$(pwd)" ./build/osdos > /tmp/hello.log 2>&1 & APP=$!
sleep 4

W=$(xdotool search --name '^OSD/OS$' | head -1)
key() { xdotool windowfocus --sync "$W" key "$@"; sleep 1.5; }

key Return                              # HELLO CHANNEL, first on the main menu
import -window root /tmp/hello-1-list.png
key Return                              # TEST CARD: it plays
sleep 3
import -window root /tmp/hello-2-playing.png
key Escape                              # back: mpv stops, the list comes back
import -window root /tmp/hello-3-back.png

kill "$APP" "$XVFB"
cat "$D/lists.json"; echo               # the recent list, with where it stopped
```

- **640 × 480** is the size the views' fractions are written against. Try 1920 × 1080 too.
- **No sound card**: a video played to its end may end as `failed` rather than `eof`. Check a video's end on a machine with sound.
- **The log** is in `/tmp/hello.log`, mpv's own in `$XDG_RUNTIME_DIR/osdos-mpv.log` (`/tmp/runtime-<user>/` where that is not set, the temp folder on a Mac). A QML mistake names its file and line there.
- **The keys**: `Up`, `Down`, `Left`, `Right`, `Return` (select), `Escape` (back), `space` (play/pause).

### A test in tests/

The regression tests in [`tests/`](https://github.com/mehmetraif/OSD-OS/tree/main/tests) are Qt Test programs built from the sources they test. `tests/hello_channel_test.cpp` checks what the backend lists, and that `registerModule` connects it:

```cpp
#include "AppCore.h"
#include "modules/hello_channel/HelloChannelBackend.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// Hello Channel's backend, alone and wired into AppCore as main.cpp wires it.
class HelloChannelTest : public QObject {
    Q_OBJECT

    static bool touch(const QString &path) {
        QFile f(path);
        return f.open(QIODevice::WriteOnly);
    }

private slots:
    // With no folder set, its own in the data folder, made at once.
    void defaultFolder() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        HelloChannelBackend backend(data.path());
        QCOMPARE(backend.videoFolder(), data.path() + "/hello_channel");
        QVERIFY(QDir(backend.videoFolder()).exists());
    }

    // Only videos, by name; one type when asked.
    void listsVideos() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        HelloChannelBackend backend(data.path());
        const QString dir = backend.videoFolder();
        QVERIFY(touch(dir + "/b.mkv"));
        QVERIFY(touch(dir + "/A.mp4"));
        QVERIFY(touch(dir + "/notes.txt"));
        const QVariantList all = backend.videos();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[0].toMap().value("name").toString(), QString("A"));
        QCOMPARE(all[1].toMap().value("type").toString(), QString("mkv"));
        QCOMPARE(backend.videos("mkv").size(), 1);
    }

    // registerModule connects what the backend declares: its options come
    // back from AppCore with the module id, and its settings reach it.
    void wiredByRegisterModule() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        AppCore core(data.path(), data.path());
        HelloChannelBackend backend(data.path());
        core.registerModule("com.osdos.hello_channel", "helloChannelBackend", &backend, nullptr);
        QVERIFY(touch(backend.videoFolder() + "/clip.mp4"));

        QSignalSpy options(&core, &AppCore::dynamicOptionsReady);
        core.invoke_module_action("com.osdos.hello_channel", "getVideoTypes");   // queued
        QTRY_COMPARE(options.count(), 1);
        QCOMPARE(options.first().at(0).toString(), QString("com.osdos.hello_channel"));
        QCOMPARE(options.first().at(1).toString(), QString("video_type"));
        QCOMPARE(options.first().at(2).toList().size(), 2);   // All, MP4

        QSignalSpy changed(&backend, &HelloChannelBackend::videosChanged);
        const QString other = data.path() + "/other";
        core.save_setting("com.osdos.hello_channel", "video_folder", other);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(backend.videoFolder(), other);
    }
};

QTEST_GUILESS_MAIN(HelloChannelTest)
#include "hello_channel_test.moc"
```

Add it to `tests/CMakeLists.txt`, after the `looks` test, with `AppCore` and what it needs:

```cmake
# Hello Channel's backend, and registerModule's wiring of it (AppCore).
add_executable(hello_channel_test
    hello_channel_test.cpp
    ../src/AppCore.cpp
    ../src/util/SetupFile.cpp
    ../src/util/LegacyNames.cpp
    ../src/modules/hello_channel/HelloChannelBackend.cpp)
target_include_directories(hello_channel_test PRIVATE ../src)
target_link_libraries(hello_channel_test PRIVATE Qt6::Core Qt6::Network Qt6::Qml Qt6::Test)
add_test(NAME hello_channel COMMAND hello_channel_test)
```

and run it:

```sh
cmake -S tests -B build-tests
cmake --build build-tests --parallel
ctest --test-dir build-tests -R hello_channel --output-on-failure
```

- `registerModule()` takes a null context, so a test needs no QML engine.
- `invoke_module_action()` is queued, hence `QTRY_COMPARE`. `save_setting()` reaches `onSettingChanged()` at once.
- A test that needs mpv or another program puts a shell script of that name first on the `PATH`, as `playback_retire` and `menu_music` do.
- Your module's views are tested already: `app_module_<folder>` starts the app on the module and fails on any QML warning ([The regression tests](https://github.com/mehmetraif/OSD-OS/wiki/Building-from-Source#the-regression-tests)).

### On the device

CONTRIBUTING.md asks to check by hand:

- **With a remote or a keyboard only**: every screen can be reached, and left.
- **On a CRT**, if you have one: nothing runs off a tube's edges. On HDMI too, ideally.
- **Settings survive a restart**, and settings saved by an older version still load.
- **The video's end**: the module goes back to its list.

## The finished module

```text
modules/hello_channel/
├── manifest.json                  identity, and seven settings
├── assets/
│   └── images/
│       └── logo.svg               the title bar's logo
└── views/
    ├── Root.qml                   the router
    ├── Channels.qml               the list
    ├── Detail.qml                 About This Channel, a leaf
    └── Player.qml                 hands a video to mpv
src/modules/hello_channel/
├── HelloChannelBackend.h          the backend
└── HelloChannelBackend.cpp
tests/hello_channel_test.cpp       its regression test
```

Plus three lines in `src/main.cpp`, one in `CMakeLists.txt` and a block in `tests/CMakeLists.txt`. Without the backend, `modules/hello_channel/` alone is a module.

## The shared components

Every screen is drawn from the components in [`views/Components`](https://github.com/mehmetraif/OSD-OS/tree/main/views/Components) (`import Components`):

| Component | Use it for |
|---|---|
| `AppBar` | The title bar: `iconSource`, `title`, `subtitle` |
| `HintBar` | The key hints at the foot, from `root.hints` |
| `MenuRow`, `MenuList`, `ScrollMarks` | Menu lines, a menu of them, ▲ ▼ when lines are hidden |
| `HelpLine` | The line about the selected row, under the menu |
| `PromptScreen`, `ChoiceOverlay` | A full-screen question or notice, and a short list of answers |
| `OnScreenKeyboard` | Typing with a remote: `open(text)`, then `accepted(text)` or `canceled()` |
| `TreeBrowser`, `InfoPanel`, `EntryOptions`, `PlaylistAdder` | Browsing as Local Files does, a title's info screen, an entry's options, Add to Playlist |
| `LoadingScreen`, `PlayerMenu`, `PlaybackNotice` | The tape loading while a video starts, a video's own menu, the notice for a video that couldn't be played |
| `OsdGround`, `SelectionBox`, `SkinImage`, `Dither` | The ground under a full-screen layer, the selected line's box, a skin's picture, two-colour dimming |
| `PixelIcon`, `OsdTicks`, `OsdTapeBar`, `VhsCassette` | Pixel-drawn pieces: symbols, a segment bar, the tape position bar, the boot screen's cassette |
| `NfcCardWriter` | Writing an NFC card for what a detail view shows |
| `WebPlayerBrowse`, `WebPlayerLaunch` | A streaming service's catalogue and its web player |

Each component's properties are in [ARCHITECTURE.md → Components](https://github.com/mehmetraif/OSD-OS/blob/main/ARCHITECTURE.md#components-wip).

Every view also has `Main.qml`'s own properties, as `root.*`:

- `sw`, `sh`, `px` (one pixel of a 240-line picture) and `contentBox` (the area views lay out in);
- `primaryColor`, `surfaceColor`, `globalFont`, `hints`, `appVersion`;
- `formatTime(ms)`, `changeWindow()`, `restFor()`, `behindNote` and `videoBehind`.

## Before a pull request

CONTRIBUTING.md's checklist, for a module:

- [ ] It keeps the five principles [above](#what-a-module-is).
- [ ] It writes its state files whole (`writeFileAtomically()`).
- [ ] Its views change through `root.changeWindow()`, its hints come from `root.hints`, its screens are the shared components.
- [ ] A setting's key, once released, never changes.
- [ ] It is described for users in [`docs/wiki/Modules.md`](https://github.com/mehmetraif/OSD-OS/blob/main/docs/wiki/Modules.md), with a line in the README's list if it is a big one.
- [ ] The pull request says which platforms it was tried on, and how AI was used in it.

## See also

- [Modules](https://github.com/mehmetraif/OSD-OS/wiki/Modules): the modules OSD/OS comes with
- [How it works](https://github.com/mehmetraif/OSD-OS/wiki/How-It-Works): the shell, playback and the screen's hand-off
- [Building from source](https://github.com/mehmetraif/OSD-OS/wiki/Building-from-Source): building, running and the tests
- [Configuration files](https://github.com/mehmetraif/OSD-OS/wiki/Configuration-Files): `config.json` and `lists.json`
