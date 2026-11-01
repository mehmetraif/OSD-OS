import QtQuick
import Components

FocusScope {
    id: settingsRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})
    // The page: the first screen (""), a heading of it's ("tv", "look",
    // "screen", "sound", "network", "startup", "system"), or Look's effects
    // ("effects"). The first screen has the headings alone; each heading's
    // page has its rows, at most a screenful.
    readonly property string group: navParams.group || ""
    readonly property var groupTitles: ({ tv: "TV / Movie", look: "Look", effects: "Look / Effects",
                                          screen: "Screen", sound: "Sound", network: "Network",
                                          startup: "Startup", system: "System" })

    // Settings → Look → Menu Layout (Main.qml's menuLayout). Camcorder lays
    // the first screen out as a camcorder's menu: its rows (camPages) as
    // icons in a column at the left, the rows of the page the cursor is on
    // beside them, ► or select going into them and back out to the icons
    // again (camIcons). A page with no rows of its own (Arcade's, Controls,
    // Quit) has its first-screen row there, which ► or select opens. A page
    // opened from the rows (Look's Effects) has the column too, its
    // heading's icon framed. Studio Monitor does the same in a monitor's
    // window (Components/MonitorChrome), a STATUS page first, what is set,
    // read alone (statusRows()). Broadcast and Flat Panel are lists, as
    // Standard is, in a monitor's look: a value changed in two steps
    // (editIndex), and Cabinet Lock at the first screen's foot as Key
    // Protect; Flat Panel a page of rows at a time.
    readonly property string layout: root.menuLayout
    readonly property bool camcorder: layout === "Camcorder"
    readonly property bool studio: layout === "Studio Monitor"
    readonly property bool broadcast: layout === "Broadcast"
    readonly property bool flat: layout === "Flat Panel"
    // Drawn by MonitorChrome and MonitorRow, in the layout's own colours.
    readonly property bool monitor: studio || broadcast || flat
    // The pages as icons beside the rows: Camcorder's column, Studio
    // Monitor's strip.
    readonly property bool iconMenu: camcorder || studio
    // A value changed in two steps: select, ▲ ▼, then select again (back
    // leaves it as it was).
    readonly property bool twoStep: broadcast || flat
    property var camPages: []
    property int camIndex: 0
    property bool camIcons: true
    // Each first-screen row's icon (PixelIcon's names), by its key or type.
    readonly property var pageIcons: ({ group_tv: "tv", group_arcade: "joystick", group_look: "sparkle",
                                        group_screen: "monitor", group_sound: "speaker", remap_controls: "gamepad",
                                        group_network: "antenna", group_startup: "power", group_system: "gear",
                                        quit: "eject", group_status: "status" })
    // Studio Monitor's first page, before the first screen's.
    readonly property var statusPage: ({ type: "submenu", key: "group_status", label: "Status",
                                         description: "What is set, at a glance: the outputs, the network, the theme and what OSD/OS starts on",
                                         moduleId: "" })
    // The page a first-screen row opens in this view, "" for one that is a
    // view of its own.
    function pageGroup(row) {
        return row && row.key && row.key.indexOf("group_") === 0 && row.key !== "group_arcade" ? row.key.substring(6) : ""
    }
    // A first-screen row's key, which every layout's first screen keeps.
    function pageKey(row) {
        return row ? (row.key || row.type || "") : ""
    }
    // The keys in the icons.
    function inIcons() {
        return iconMenu && group === "" && camIcons
    }
    // Where the list is, for the view this page opens to bring it back: the
    // row; on the icons' first screen the page (as the first screen's row,
    // so every layout finds its own), its row and whether the keys were in
    // the rows.
    function listState() {
        if (!(iconMenu && group === ""))
            return { currentIndex: settingsList.currentIndex }
        var page = camPages[camIndex]
        return { currentIndex: Math.max(0, camIndex - (studio ? 1 : 0)), page: pageKey(page && page.row),
                 rowIndex: settingsList.currentIndex, camRows: !camIcons }
    }
    // The icons: the page above (-1) or below (1), round, its rows beside
    // them at once.
    function moveIcon(direction) {
        var n = camPages.length
        if (n === 0)
            return
        camIndex = (camIndex + direction + n) % n
        buildModel(0)
    }
    // ► or select on an icon: into the page's rows, or a view of its own
    // opened. STATUS is read from the icons.
    function enterPage() {
        var page = camPages[camIndex]
        if (!page)
            return
        if (pageGroup(page.row) === "") {
            settingsList.openRow(page.row)
            return
        }
        if (settingsList.currentIndex >= 0 && selectable(settingsList.currentIndex))
            camIcons = false
    }
    // Menu Layout changed while the page is open: laid out again where the
    // cursor is (deferred, as the change comes from the row's own save), once
    // the rows are laid out for the other layout (builtLayout: not as the
    // page is made, when the binding first takes its value). On the first
    // screen the page under the cursor stays, an icon or a row.
    property string builtLayout: ""
    property bool builtIcons: false
    onLayoutChanged: Qt.callLater(relayout)
    function relayout() {
        if (builtLayout === "" || builtLayout === layout)
            return
        editIndex = -1
        if (group !== "") {
            buildModel(settingsList.currentIndex)
            return
        }
        var key = builtIcons ? pageKey((camPages[camIndex] || {}).row) : pageKey(settingsItems[settingsList.currentIndex])
        if (iconMenu) {
            camIcons = true
            camIndex = 0
            buildModel(0)
            camIndex = Math.max(0, camPages.findIndex(function(page) { return pageKey(page.row) === key }))
            buildModel(0)
        } else {
            buildModel(0)
            buildModel(Math.max(0, settingsItems.findIndex(function(row) { return pageKey(row) === key })))
        }
    }

    // Broadcast's and Flat Panel's second step: the row whose value is being
    // changed (-1 for none) and the value it would take, a choice's label or
    // a slider's number (-1 for a slider's Off). Nothing is saved until
    // select; back leaves the row as it was.
    property int editIndex: -1
    property var editValue
    function startEdit() {
        var row = settingsItems[settingsList.currentIndex]
        editIndex = settingsList.currentIndex
        editValue = row.type === "slider" ? (row.on === false ? -1 : row.value) : row.value
    }
    // ▲ (1) or ▼ (-1): the next choice round, or a step up or down the
    // slider, below its first step to Off where it can be.
    function stepEdit(direction) {
        var row = settingsItems[editIndex]
        if (row.type === "slider") {
            var v = editValue < 0 ? (direction > 0 ? 0 : -1) : editValue + direction * row.step
            editValue = Math.min(100, v < 0 ? (row.on !== undefined ? -1 : 0) : v)
        } else {
            var opts = row.options
            editValue = opts[(opts.indexOf(editValue) + direction + opts.length) % opts.length]
        }
    }
    function confirmEdit() {
        var row = settingsItems[editIndex]
        var value = editValue
        editIndex = -1
        if (row.type === "slider")
            settingsList.setSlider(value)
        else
            settingsList.setChoice(row.options.indexOf(value))
    }
    // What a row shows as its value in a monitor layout: a slider's number
    // (or Off), the value being changed while it is.
    function shownValue(row, index) {
        var v = index === editIndex ? editValue : row.type === "slider" ? (row.on === false ? -1 : row.value) : row.value
        if (row.type === "slider")
            return v < 0 ? "Off" : v + "%"
        return v === undefined || v === null ? "" : String(v)
    }
    // A monitor layout's title: Studio Monitor's tab names the page shown;
    // Broadcast's MENU or the page's name; Flat Panel's the path to it.
    readonly property string monitorTitle: {
        var page = group !== "" ? (groupTitles[group] || "") : ""
        if (studio)
            return page !== "" ? page : ((camPages[camIndex] || {}).row || {}).label || "Settings"
        if (flat)
            return page !== "" ? ">" + page.split(" / ").join(" >") : "Menu"
        return page !== "" ? page : "Menu"
    }
    // Its key guide, as a monitor's: Broadcast's three lines, a value's
    // second step its own.
    readonly property var monitorGuide: {
        var h = root.hints
        if (studio)
            return [h.navigate + ":MENU/ITEM", (inIcons() ? h.browse + ":ENTER" : h.change + ":CHANGE"), h.back + ":BACK"]
        if (broadcast)
            return editIndex >= 0 ? ["TO CHANGE  :" + h.navigate, "TO CONFIRM :" + h.select, "TO CANCEL  :" + h.back]
                                  : ["TO SELECT  :" + h.navigate, "TO ENTER   :" + h.select, "TO CANCEL  :" + h.back]
        return editIndex >= 0 ? [h.navigate + " Change", h.select + " Confirm", h.back + " Cancel"]
                              : [h.navigate + " Select", h.browse + "/" + h.select + " Open", h.left + "/" + h.back + " Back"]
    }

    // Back: a value being changed left as it was; from the rows to the
    // icons; else out of the page.
    function back() {
        if (editIndex >= 0)
            editIndex = -1
        else if (iconMenu && group === "" && !camIcons)
            camIcons = true
        else
            goBack()
    }
    // Where the cursor's row shows: in Flat Panel its page, else scrolled
    // into view a whole row at a time.
    function showCurrent() {
        if (flat) {
            chrome.pageTo(settingsList)
        } else {
            settingsList.positionViewAtIndex(settingsList.currentIndex, ListView.Contain)
            settingsList.showWholeRows()
        }
    }

    property var appSettings: ({})
    // Where the menus can be a window on a desktop (Main.qml's canWindow):
    // Windows, and a Linux or BSD desktop. On the Pi's console and a Mac the
    // Window Mode row is dimmed.
    readonly property bool desktopWindow: Qt.platform.os === "windows" || Qt.platform.pluginName === "xcb"
                                          || Qt.platform.pluginName === "wayland"
    // Where there is a clipboard to paste OSD/OS Setup's settings from: a
    // computer's, not the Pi's own console, where its row is dimmed.
    readonly property bool canPaste: Qt.platform.pluginName !== "eglfs" && Qt.platform.pluginName !== "linuxfb"
    property var installedModules: []

    // Flat model: mix of section headers and rows
    property var settingsItems: []
    // Counted up as a row changes in place, so the rows read theirs again:
    // the model stays as it is, and the list where it is.
    property int revision: 0


    // Quit overlay choices. Under the autostart service (headless RPi) the quit menu has an
    // "Exit to Terminal" option that drops to a tty1 login without powering off, and a
    // "Restart" that reboots (where the stop helper knows how, see canRestartSystem());
    // they are not needed on macOS/Desktop or when run by hand, so it's yes/no for that case.
    property bool autostartSession: false
    property bool canRestart: false
    // Quit's Save Logs: on the OSD/OS image, the logs copied to the card's
    // OSD-OS partition, which a computer shows as a drive (AppCore::saveLogs).
    property bool canSaveLogs: false

    // Display Output, read from displayOutput in buildModel(): the outputs
    // this board has, the one in force, the one written for the next start,
    // the ways to switch the launcher knows, and the one chosen to switch to.
    property string boardModel: ""
    property var outputOptions: []
    property string outputCurrent: ""
    property string outputCurrentLabel: ""
    property string outputNext: ""
    property string outputNextLabel: ""
    property bool outputAtNextStart: false
    property bool outputPowerOff: false
    property var outputChoice: ({})
    property string outputFailure: ""
    readonly property var saveLogsOption: settingsRoot.canSaveLogs ? [{ label: "Save Logs", action: "logs" }] : []
    property var quitOptions: settingsRoot.autostartSession
        ? [{ label: "Power Off",        action: "quit"     }]
          .concat(settingsRoot.canRestart ? [{ label: "Restart", action: "restart" }] : [])
          .concat([{ label: "Exit to Terminal", action: "terminal" }])
          .concat(saveLogsOption)
          .concat([{ label: "Cancel",           action: "cancel"   }])
        : [{ label: "Yes", action: "quit" }]
          .concat(saveLogsOption)
          .concat([{ label: "No",  action: "cancel" }])

    // `at`: the row the cursor stays on, else the one it was on last.
    function buildModel(at) {
        var cfg = appCore.get_settings()
        appSettings = cfg.app || {}
        installedModules = appCore.get_installed_modules()
        autostartSession = appCore.isAutostartSession()
        canRestart = appCore.canRestartSystem()
        canSaveLogs = appCore.canSaveLogs()

        // The first screen's rows, and with icons its pages (Studio
        // Monitor's STATUS first): the page shown is the icon's, a page
        // opened from one has its heading's.
        var first = []
        firstRows(first)
        builtLayout = layout
        builtIcons = iconMenu
        editIndex = -1
        var pages = iconMenu ? (studio ? [statusPage] : []).concat(first) : []
        camPages = pages.map(function(row) { return { row: row, icon: pageIcons[pageKey(row)] || "" } })
        if (iconMenu && group !== "") {
            var heading = "group_" + (group === "effects" ? "look" : group)
            camIndex = Math.max(0, pages.findIndex(function(row) { return row.key === heading }))
        }
        camIndex = Math.max(0, Math.min(camIndex, pages.length - 1))
        var shown = iconMenu && group === "" ? pageGroup(pages[camIndex]) : group

        var items = []
        if (shown === "tv")
            tvRows(items)
        else if (shown === "look")
            lookRows(items)
        else if (shown === "effects")
            effectRows(items)
        else if (shown === "screen")
            screenRows(items)
        else if (shown === "sound")
            soundRows(items)
        else if (shown === "network")
            networkRows(items)
        else if (shown === "startup")
            startupRows(items)
        else if (shown === "system")
            systemRows(items)
        else if (shown === "status")
            statusRows(items)
        else if (iconMenu && group === "")
            items = [pages[camIndex]]
        else
            items = first

        settingsItems = items

        // Restore saved position, or default to first selectable row
        var start = at !== undefined ? Math.min(at, items.length - 1)
                  : navListState.currentIndex !== undefined ? Math.min(navListState.currentIndex, items.length - 1) : 0
        settingsList.currentIndex = Math.max(0, Math.min(start, items.length - 1))
        for (var k = 0; k < items.length; k++) {
            if (selectable((start + k) % items.length)) {
                settingsList.currentIndex = (start + k) % items.length
                break
            }
        }
        showCurrent()
    }

    // The first screen: a heading for each page, every row under one of them,
    // then Quit. TV / Movie's page is the videos' (this view again, group
    // "tv"), with the video modules'; Arcade's the arcade modules' own
    // settings on one page, the first module's with the others' under their
    // names (ModuleSettings, alsoModules). A module's group is its
    // manifest's "settings_group"; the modules of no group are on System's.
    function firstRows(items) {
        items.push(groupRow("tv", "The videos' settings: how a picture fills the screen, Transparent Background, the loading screen, the channel logo, the info screen, and each video module's own"))
        var arcade = groupModules("arcade")
        if (arcade.length > 0) {
            items.push({
                type: "submenu",
                key: "group_arcade",
                label: "Arcade",
                description: "The games' settings on one page: " + arcade.map(function(m) { return m.name }).join(", "),
                moduleId: arcade[0].id,
                alsoModules: arcade.slice(1).map(function(m) { return m.id })
            })
        }
        items.push(groupRow("look", "The whole look: the theme, the color scheme, the skin, the effects, what the menus are drawn on and the bars around them"))
        items.push(groupRow("screen", "Where the picture goes and setting the screen up: Display Output, Test Patterns, the screen saver, the mouse pointer and Window Mode"))
        items.push(groupRow("sound", "Which sound card plays, and the music under the menus"))
        items.push({
            type: "submenu",
            key: "remap_controls",
            label: "Controls",
            description: "Every controller connected, each with its own buttons (and a pad's button labels), and extra keys for every keyboard and remote",
            moduleId: ""
        })
        items.push(groupRow("network", "Wi-Fi's networks, and pairing Bluetooth keyboards, gamepads and remotes"))
        items.push(groupRow("startup", "What OSD/OS opens on: Start on Module, Play at Startup and Cabinet Lock"))
        items.push(groupRow("system", "OSD/OS Setup's settings pasted, Update, About, and the settings of the modules of no group"))
        items.push({ type: "quit", label: "Quit",
                     description: settingsRoot.autostartSession ? "Power off or restart the Pi, or leave OSD/OS for a terminal"
                                                                : "Leave OSD/OS" })
        // Broadcast's and Flat Panel's: Cabinet Lock as a monitor's Key
        // Protect, at the foot after a gap (also on Startup's page).
        if (twoStep) {
            items.push({ type: "section", label: "" })
            items.push(Object.assign(cabinetLockRow(), { label: "Key Protect" }))
        }
    }

    // STATUS (Studio Monitor): what is set, a line each, read alone; each
    // value as its own row has it.
    function statusRows(items) {
        function valueOf(build, key) {
            var rows = []
            build(rows)
            var row = rows.filter(function(r) { return r.key === key || r.type === key })[0]
            return !row || row.unavailable ? "None" : (row.value || "None")
        }
        var wifi = "None"
        if (wifiManager && wifiManager.supported) {
            wifi = wifiManager.state === "connected" ? ((wifiManager.current || {}).ssid || "Joined")
                 : wifiManager.state === "off" || wifiManager.state === "blocked" || !wifiManager.enabled ? "Off"
                 : "Not Joined"
        }
        var bluetooth = "None"
        if (bluetoothManager && bluetoothManager.supported) {
            var linked = (bluetoothManager.devices || []).filter(function(d) { return !!d.connected }).length
            bluetooth = !bluetoothManager.powered ? "Off" : linked > 0 ? linked + " Connected" : "On"
        }
        var lines = [["Output", valueOf(screenRows, "display_output")],
                     ["Audio", valueOf(soundRows, "audio_output")],
                     ["Wi-Fi", wifi],
                     ["Bluetooth", bluetooth],
                     ["Theme", valueOf(lookRows, "osd_theme")],
                     ["Start On", valueOf(startupRows, "startup_module")],
                     ["Cabinet Lock", appSettings["cabinet_lock"] === "On" ? "On" : "Off"],
                     ["Version", root.devBuild ? "Dev" : root.appVersion],
                     ["IP Address", ipAddress || "None"]]
        for (var i = 0; i < lines.length; i++)
            items.push({ type: "info", label: lines[i][0], value: lines[i][1] })
    }

    // A heading's row: its page, this view again; its label the page's
    // title unless given.
    function groupRow(group, description, label) {
        return { type: "submenu", key: "group_" + group, label: label || groupTitles[group],
                 description: description, moduleId: "" }
    }

    // LOOK: the theme and its parts, the effects on a page of their own, and
    // what the menus are drawn on.
    function lookRows(items) {
        // Theme — the whole look in one (AppCore::themes(), the app's own and
        // the data folder's themes folder; read in Main.qml as root.theme):
        // the color scheme, the skin, the effects and the menu music. Each of
        // those has its row below, THEME (as the theme has it) unless set
        // otherwise; choosing a theme sets them all back to THEME
        // (themeChosen()). Without a theme they have no THEME: "" is what
        // each is without one (Video 1, OSD/OS's own window, off).
        items.push(lookRow("osd_theme", "Theme", "None", appCore.themes(),
            "The whole look in one: the color scheme, the skin, the effects and the menu music\n[NONE] OSD/OS's own  Choosing one sets each row below back to the theme's  A new one is a folder in the data folder's themes, see the wiki"))

        // Orb Fog — the fog behind the panels of the Arcade screen's Orb look
        // (OrbLook.qml; Main.qml's orbFogLive), for a theme with that look:
        // the rows are made again as one is chosen (themeChosen()).
        items.push({
            type: "list_single",
            key: "orb_fog",
            label: "Orb Fog",
            options: ["Picture", "Live"],
            value: appSettings["orb_fog"] === "Live" ? "Live" : "Picture",
            description: "The fog drifting behind the Arcade screen's panels in the Orb look\n[PICTURE] A picture of fog, light on the GPU  [LIVE] Worked out as it drifts, as in the look's first design, more work for a Raspberry Pi's GPU",
            unavailable: root.theme && root.theme.arcade === "orb" ? "" : "Only with a theme in the Orb look",
            moduleId: ""
        })

        // Main.qml's schemes, the custom ones among them (root.allSchemes).
        items.push(partRow("color_scheme", "Color Scheme", Object.keys(root.allSchemes), false,
            "The two colors everything is drawn in\n[THEME] The theme's, else Video 1  Please see the wiki for details on adding a custom one"))

        // Skin — how the window is dressed, apart from the colours: the
        // shapes of its frame, bars and selected line, and its icons, in the
        // scheme's colours (AppCore::skins(); read in Main.qml as root.skin).
        // Saved by folder name, shown by the skin's name.
        // A skin chosen before skins had their name was saved as app.theme.
        var skinRow = lookRow("skin", "Skin", "Theme", appCore.skins(),
            "How the window is dressed: its frame, the title and hint bars, the selected line and the icons, in the color scheme's colors\n[THEME] The theme's  [NONE] OSD/OS's own  A new one is a folder in the data folder's skins, see the wiki",
            appSettings["skin"] !== undefined ? appSettings["skin"] : (appSettings["theme"] || ""))
        skinRow.options.splice(1, 0, "None")
        skinRow.values.splice(1, 0, "Off")
        if (appSettings["skin"] === "Off" || (!appSettings["osd_theme"] && skinRow.value === "Theme"))
            skinRow.value = "None"
        if (!appSettings["osd_theme"]) {
            skinRow.options.shift()
            skinRow.values.shift()
        }
        items.push(skinRow)

        items.push(groupRow("effects", "What the text, the background, the selected line and the screen do, and how one window gives way to the next", "Effects"))

        // MENU LAYOUT — how Settings and the main menu are laid out (Main.qml's
        // menuLayout): a list a page at a time, or a camcorder's menu.
        items.push({
            type: "list_single",
            key: "menu_layout",
            label: "Menu Layout",
            options: ["Standard", "Camcorder", "Studio Monitor", "Broadcast", "Flat Panel"],
            value: root.menuLayoutOf(appSettings["menu_layout"]),
            description: "How Settings and the main menu are laid out\n[STANDARD] A list, a page at a time  [CAMCORDER] As a camcorder's menu: the pages, or the main menu's groups, as icons in a column, the rows of the one chosen beside it  [STUDIO MONITOR] As a studio monitor's window over the picture, the pages as icons in a strip, a Status page first  [BROADCAST] As a broadcast monitor's menu, plain blue, a value changed in two steps: select, then ▲ ▼, then select  [FLAT PANEL] As a flat-panel monitor's menu, black, a page of rows at a time, a value changed in two steps",
            moduleId: ""
        })

        // OSD Background — what the menus are drawn on (Components/OsdGround,
        // read in Main.qml): the colour scheme's background over the whole
        // screen, none (black), or a framed window of it behind the menus.
        items.push({
            type: "list_single",
            key: "osd_background",
            label: "OSD Background",
            options: ["Full", "Off", "Window"],
            value: root.osdBackgroundOf(appSettings["osd_background"]),
            description: "What the menus are drawn on\n[FULL] The color scheme's background, all over  [OFF] None, the menus on black like a deck's on-screen display  [WINDOW] A framed window of it behind the menus, black around it",
            moduleId: ""
        })

        // Window Frame — the frame of OSD Background's window, for Window
        // alone (shownWith). Read in Main.qml, drawn by OsdGround.
        items.push({
            type: "list_single",
            key: "osd_frame",
            label: "Window Frame",
            options: ["On", "Off", "Shadow"],
            value: root.osdFrameOf(appSettings["osd_frame"]),
            description: "The frame of the OSD Background's window\n[ON] A line in the color scheme's color  [OFF] None  [SHADOW] The line and a shadow below and to the right, like a DOS window's",
            shownWith: { key: "osd_background", value: "Window" },
            moduleId: ""
        })

        // BOOT SCREEN COLORS — the boot screen in the colours in force, as
        // the loading screen's are (TV / Movie). Read in Main.qml
        // (root.bootInk).
        items.push({
            type: "list_single",
            key: "boot_colors",
            label: "Boot Screen Colors",
            options: ["Theme", "Default"],
            value: appSettings["boot_colors"] === "Default" ? "Default" : "Theme",
            description: "The colours of the boot screen, with its cassette, on the OSD/OS image\n[THEME] The theme's or color scheme's  [DEFAULT] White on blue",
            moduleId: ""
        })

        // HINT BAR — the key hints on the bar at the foot of every screen
        // (Components/HintBar). Read in Main.qml.
        items.push({
            type: "list_single",
            key: "hint_bar",
            label: "Hint Bar",
            options: ["On", "Off"],
            value: appSettings["hint_bar"] === "Off" ? "Off" : "On",
            description: "The key hints on the bar at the foot of every screen\n[OFF] No bar; the keys work as they do",
            moduleId: ""
        })

        // HELP LINE — the line about the selected row in the box under a
        // menu, this one (Components/HelpLine). Read in Main.qml.
        items.push({
            type: "list_single",
            key: "help_line",
            label: "Help Line",
            options: ["On", "Off"],
            value: appSettings["help_line"] === "Off" ? "Off" : "On",
            description: "The box under a menu with a line about the selected row, this one\n[OFF] No box",
            moduleId: ""
        })

        // STATUS ICONS — what is on, at the title bar's top right and the
        // Arcade screen's bottom right (Components/StatusIcons). Read in
        // Main.qml.
        items.push({
            type: "list_single",
            key: "status_icons",
            label: "Status Icons",
            options: ["On", "Off"],
            value: appSettings["status_icons"] === "Off" ? "Off" : "On",
            description: "What is on, at the top right of the title bar and the bottom right of the Arcade screen: Wi-Fi's signal, Bluetooth, and the Arcade's HI (Save High Scores), trophy (RetroAchievements), globe (Online Play) and network (LAN Play)\n[OFF] None",
            moduleId: ""
        })
    }

    // LOOK / EFFECTS: the theme's effects (Main.qml; their names
    // root.*Presets), never on a video, nor while one loads or has its menu
    // open. Those the GPU draws need one that can.
    function effectRows(items) {
        var noGpu = root.effectsUsable ? "" : "Only where the GPU draws the menus' effects"
        var text = partRow("text_effect", "Text Effect", Object.keys(root.textPresets), true,
            "What the text, the lines and the bars do, on the GPU\n[THEME] The theme's  [RAINBOW] Run through the colors  [SHIMMER] A glint sweeping across now and then  [GLOW] A halo round them  [FLICKER] Like a failing neon sign")
        text.unavailable = noGpu
        items.push(text)
        var background = partRow("background_effect", "Background Effect", Object.keys(root.backgroundPresets), true,
            "What goes on behind the menus, in the window, on the GPU\n[THEME] The theme's  [MATRIX] A rain of glyphs  [FIRE] Pixel flames burning up from under the window  [STARS] A starfield drifting by  [SNOW] Snow falling  [CHECKERED FLAG] A racing flag waving in the window  [PLASMA] An old demo's plasma drifting, dithered in pixels")
        background.unavailable = noGpu
        items.push(background)
        items.push(partRow("selector_effect", "Selector Effect", root.selectorPresets, true,
            "What goes on round the selected line\n[THEME] The theme's  [SPARKLES] Sparks flying off its corners  [WELDING] A welder's sparks bursting from its corners  [LIGHTNING] Bolts crackling out of it  [RAINBOW] A pixel rainbow running down from under it"))
        var screenFx = partRow("screen_effect", "Screen Effect", Object.keys(root.screenPresets), true,
            "A picture tube's or a tape's look over the menus, on the GPU\n[THEME] The theme's  [SCANLINES] Dark lines between the picture's  [CRT] A tube's curve, scanlines and glow  [VHS] A tape's color bleed and noise")
        screenFx.unavailable = noGpu
        items.push(screenFx)
        items.push(partRow("transition", "Transition", root.transitionPresets.filter(root.transitionUsable), true,
            "How one window gives way to the next\n[THEME] The theme's  [FADE] The old fades away  [CUBE] The window turns over like a cube, the next on a face at random  [RIPPLE] The old ripples out as the new ripples in  [WAVE] A wave runs out from a corner and dies away, the new one behind it  [DROP] A drop falls in a corner, the new one inside its spreading ring"))
    }

    // SCREEN: where the picture goes, setting the screen up, and what the
    // menus do while no one is at them.
    function screenRows(items) {
        // DISPLAY OUTPUT — where the picture goes, among the outputs this
        // board has (displayOutput, by its model): select opens them. The Pi
        // reads it at power-on, so a change restarts it, powers it off, or
        // waits for its next start, and the new output stays only when kept
        // on it (views/DisplayKeep.qml). Elsewhere there is nothing to
        // switch, and the row says so.
        if (displayOutput && displayOutput.available) {
            boardModel = displayOutput.boardModel
            outputOptions = displayOutput.options
            outputCurrent = displayOutput.current
            outputCurrentLabel = displayOutput.currentLabel
            outputNext = displayOutput.next
            outputNextLabel = displayOutput.nextLabel
            outputAtNextStart = displayOutput.nextStartAvailable
            outputPowerOff = displayOutput.powerOffAvailable
            var later = outputAtNextStart || outputPowerOff
            items.push({
                type: "display_output",
                label: "Display Output",
                value: outputNext !== "" ? "Next: " + outputNextLabel : outputCurrentLabel,
                description: (outputNext !== "" ? "Now " + outputCurrentLabel + ", " + outputNextLabel + " from the next start • " : "")
                             + "Where the picture goes, among the outputs this Pi has: "
                             + (later ? "now, with a restart, or from the next start. " : "a change restarts OSD/OS. ")
                             + "The new output stays only when you keep it on its screen, else the old one comes back in 30 seconds\n[COMPOSITE] The AV jack (Pi 4) or TV pads (Pi 5)  [GPIO COMPOSITE] Pi 5's GPIO pins  [SCART RGB] A cable on the GPIO pins  [JAMMA] A Raspberry Pi to JAMMA board, in the modes of a 15 kHz monitor (240p at 720, 320 or 1920 across, 320 the games' own pixels and 1920 a super resolution; 288p at 720 or 1920; 480i and 576i on a Pi 4), or a 25 kHz or 31 kHz one's, each named with its rate and resolution",
                moduleId: ""
            })
        } else {
            items.push({
                type: "display_output",
                label: "Display Output",
                value: "",
                description: "Where the picture goes, among the outputs a Raspberry Pi has: HDMI, composite, SCART RGB or a JAMMA board",
                unavailable: "Only on a Raspberry Pi with the OSD/OS image",
                moduleId: ""
            })
        }
        // TEST PATTERNS — a picture to set a TV or monitor up by, its own
        // knobs or menu: geometry, size, convergence, brightness, colour
        // (views/TestPatterns.qml).
        items.push({
            type: "submenu",
            key: "test_patterns",
            label: "Test Patterns",
            description: "Pictures to set up the screen by, with its own knobs or menu: size and position, geometry, convergence, focus, brightness, contrast and colour",
            moduleId: ""
        })

        // SCREEN SAVER — single control: OFF disables, a number sets the
        // timeout for both menu idle and playback pause (handled inside mpv).
        items.push({
            type: "list_single",
            key: "screensaver_timeout",
            label: "Screen Saver",
            options: ["OFF", "30", "60", "120"],
            value: appSettings["screensaver_timeout"] || "OFF",
            description: "Prevent CRT burn-in after seconds of inactivity or pause",
            moduleId: ""
        })

        // MOUSE POINTER — drawn by Main.qml: it shows as the mouse moves and
        // goes again after this many seconds without moving.
        var pointerVals = ["off", "2", "5", "10", "30", "always"]
        var pointerOpts = ["Off", "2 sec", "5 sec", "10 sec", "30 sec", "Always"]
        var pointerIdx = pointerVals.indexOf(appSettings["mouse_pointer"] || "5")
        items.push({
            type: "list_single",
            key: "mouse_pointer",
            label: "Mouse Pointer",
            options: pointerOpts,
            values: pointerVals,
            value: pointerOpts[pointerIdx < 0 ? pointerVals.indexOf("5") : pointerIdx],
            description: "A mouse's pointer shows as it moves, and goes again after these seconds without moving\n[OFF] Never shown  [ALWAYS] Stays on screen",
            moduleId: ""
        })

        // WINDOW MODE — Main.qml: the menus in a window on the desktop in
        // place of the whole screen, where there is a desktop (desktopWindow).
        items.push({
            type: "list_single",
            key: "window_mode",
            label: "Window Mode",
            options: ["Off", "On"],
            value: appSettings["window_mode"] === "On" ? "On" : "Off",
            description: "The menus in a window on the desktop instead of the whole screen: 960×720 to start with, moved and sized as any window. F11 or Alt+Enter switches at any time",
            unavailable: settingsRoot.desktopWindow ? "" : "Only on a desktop: Windows, or Linux or BSD with one",
            moduleId: ""
        })
    }

    // SOUND: the sound card the players play through, and the menu music.
    function soundRows(items) {
        // AUDIO OUTPUT — the sound card the players play through, among those
        // ALSA has now (audioOutput): ◄ ► change it at once, for a video
        // playing behind the menus too. One chosen but unplugged stays
        // chosen, passed over until it is back.
        if (audioOutput && audioOutput.available) {
            // Without a USB sound card to choose: what the system sees of one
            // (note).
            var audio = audioOutput.settingRow()
            var usbNote = audio.note || ""
            items.push({
                type: "list_single",
                key: "audio_output",
                label: "Audio Output",
                options: audio.options,
                values: audio.values,
                value: audio.value,
                description: "Which sound card plays: the AV jack, HDMI or a USB sound card, from the moment it is chosen"
                             + (usbNote !== "" ? ". " + usbNote : "")
                             + "\n[AUTO] The Pi's own default, or the first card that can play while that one can't (a Pi 5's HDMI without the picture)  [UNPLUGGED] Chosen but not plugged in: sound goes as on Auto until it is back",
                moduleId: ""
            })
        } else {
            items.push({
                type: "list_single",
                key: "audio_output",
                label: "Audio Output",
                options: [],
                value: "",
                description: "Which sound card plays: the AV jack, HDMI or a USB sound card",
                unavailable: "Only where OSD/OS chooses among ALSA's sound cards, as on Linux",
                moduleId: ""
            })
        }

        // Menu Music — a tune under the menus (MenuMusic), never while a video
        // plays: the theme's, or a file of the user's own, picked on the file
        // picker; and how loud.
        items.push(partRow("menu_music", "Menu Music", ["File"], true,
            "A tune under the menus, never while a video plays, loads or has its menu open\n[THEME] The theme's, if it has one  [FILE] A file of your own, below"))
        var musicFile = appSettings["menu_music_file"] || ""
        items.push({
            type: "file",
            key: "menu_music_file",
            label: "Music File",
            value: musicFile !== "" ? musicFile.substring(musicFile.lastIndexOf("/") + 1) : "None",
            path: musicFile,
            picker: { mode: "file", types: ["ogg", "opus", "mp3", "flac", "wav", "m4a", "aac",
                                            "mid", "midi", "xm", "mod", "s3m", "it"],
                      defaultLabel: "No File" },
            description: "The menu music's file, played over and over\n[ENTER] Pick it on the file browser: an MP3, WAV, OGG, FLAC, Opus or M4A, a MIDI file (played with a SoundFont), or a tracker's XM, MOD, S3M or IT",
            shownWith: { key: "menu_music", value: "File" },
            moduleId: ""
        })
        var volume = parseInt(appSettings["menu_music_volume"])
        items.push({
            type: "slider",
            key: "menu_music_volume",
            label: "Music Volume",
            value: isNaN(volume) ? 60 : Math.max(0, Math.min(100, volume)),
            step: 10,
            startText: "QUIET",
            endText: "LOUD",
            description: "How loud the menu music is",
            shownWith: { key: "menu_music", not: "Off" },
            moduleId: ""
        })
    }

    // NETWORK: joining a Wi-Fi network (NetworkManager), pairing Bluetooth
    // (BlueZ); both Linux's.
    function networkRows(items) {
        var wifi = !!(wifiManager && wifiManager.supported)
        items.push({
            type: "submenu",
            key: "wifi",
            label: "Wi-Fi",
            description: "The networks within reach: join one with its password, or one that doesn't say its name, and the country Wi-Fi keeps to the rules of",
            unavailable: wifi ? "" : "Only where NetworkManager runs, as on the OSD/OS image",
            moduleId: ""
        })
        var bluetooth = !!(bluetoothManager && bluetoothManager.supported)
        items.push({
            type: "submenu",
            key: "bluetooth",
            label: "Bluetooth",
            description: "Pairing a keyboard, gamepad or remote, and the devices paired",
            unavailable: bluetooth ? "" : "Only where BlueZ runs, as on the OSD/OS image",
            moduleId: ""
        })
    }

    // STARTUP: what OSD/OS opens on, and keeping it there.
    function startupRows(items) {
        // Start on Module — pick a module to auto-launch into on startup.
        // Only present enabled modules as options so disabled ones won't display.
        // The setting is keyed by module id and the picker shows the display name
        // If the stored id isn't an enabled module, the display falls back to None.
        var moduleOpts = ["None"] // display labels
        var moduleVals = ["None"] // stored values (module ids)
        var startupId = appSettings["startup_module"] || "None"
        var startupValue = "None"
        for (var mi = 0; mi < installedModules.length; mi++) {
            if (!installedModules[mi].enabled) continue
            moduleOpts.push(installedModules[mi].name)
            moduleVals.push(installedModules[mi].id)
            if (installedModules[mi].id === startupId) startupValue = installedModules[mi].name
        }
        items.push({
            type: "list_single",
            key: "startup_module",
            label: "Start on Module",
            options: moduleOpts,
            values: moduleVals,
            value: startupValue,
            description: "Directly launch into a specific module on startup",
            moduleId: ""
        })

        // Play at Startup — a favourite played straight after the boot screen,
        // ahead of Start on Module. It is chosen in its module (right on it,
        // OPTIONS, PLAY AT STARTUP); here it can only be turned off.
        var startupFav = appSettings["startup_favorite"]
        var favName = startupFav && startupFav.path ? (startupFav.name || "Favorite") : ""
        items.push({
            type: "list_single",
            key: "startup_favorite",
            label: "Play at Startup",
            options: favName !== "" ? ["None", favName] : ["None"],
            values: favName !== "" ? ["", startupFav] : [""],
            value: favName !== "" ? favName : "None",
            description: "A favourite played straight after the boot screen, instead of opening a module\nChoose one with right on it in its module: OPTIONS, PLAY AT STARTUP",
            moduleId: ""
        })

        // Startup From — where that favourite begins. Either way it begins
        // without asking: a resume prompt is no question to put to a player
        // switched on to play. For a favourite set above (shownWith).
        items.push({
            type: "list_single",
            key: "startup_from",
            label: "Startup From",
            options: ["Resume", "Beginning"],
            value: appSettings["startup_from"] === "Beginning" ? "Beginning" : "Resume",
            description: "Where the favourite played at startup begins, without asking\n[RESUME] Where it was stopped  [BEGINNING] From the start",
            shownWith: { key: "startup_favorite", not: "None" },
            moduleId: ""
        })

        items.push(cabinetLockRow())
    }

    // CABINET LOCK — OSD/OS kept on Start on Module, the main menu and
    // Settings opened with the unlock buttons alone (views/CabinetLock.qml).
    function cabinetLockRow() {
        return {
            type: "submenu",
            key: "cabinet_lock",
            label: "Cabinet Lock",
            value: appSettings["cabinet_lock"] === "On" ? "On" : "Off",
            description: "Keeps OSD/OS on its Start on Module, a cabinet's Arcade screen: the main menu, Settings and Quit open only with the unlock buttons held together for three seconds",
            moduleId: ""
        }
    }

    // SYSTEM: OSD/OS Setup's settings, the update, About, and the modules
    // of no group, each a page.
    function systemRows(items) {
        // PASTE OSD/OS SETUP — the settings copied on OSD/OS Setup's page,
        // set at once (pasteSetup(), AppCore::applySetupText()), where there
        // is a clipboard to paste them from: a computer's.
        items.push({
            type: "paste_setup",
            label: "Paste OSD/OS Setup",
            description: "The settings copied on OSD/OS Setup's page (osdos.tv/setup, Copy for a computer), set here at once:"
                         + " OSD/OS's own and the RetroAchievements account. The device's name, Wi-Fi and the"
                         + " rest are set on an OSD/OS card alone",
            unavailable: settingsRoot.canPaste ? "" : "Only on a computer, from its clipboard; an OSD/OS card takes OSD/OS Setup's file",
            moduleId: ""
        })
        items.push({
            type: "submenu",
            key: "software_update",
            label: "Update",
            description: "A newer OSD/OS, downloaded and installed",
            moduleId: ""
        })
        // ABOUT — this device's System Information, then what OSD/OS is, who
        // makes it, what it is made of and under which licence
        // (views/About.qml).
        items.push({
            type: "submenu",
            key: "about",
            label: "About",
            description: "This device's System Information, its name on the network first; what OSD/OS is, who makes it,"
                         + " what it is made of and under which license",
            moduleId: ""
        })
        // The modules of no group with settings, a page each.
        moduleRows(items, groupModules(""))
    }

    // TV / Movie's page: the rows that are the videos' alone, then the video
    // modules' (manifest "settings_group": "tv").
    function tvRows(items) {
        // Scaling — how a picture of another shape fills the screen: a 16:9 film
        // on a 4:3 tube above all. Every video module's settings can override
        // it for that module. It replaces Auto Crop, whose ON reads as Pan &
        // Scan until a Scaling is chosen. The OSC's CROP still toggles live.
        var scalingOpts = ["Letterbox", "14:9", "Pan & Scan", "Anamorphic"]
        var scaling = appSettings["video_scaling"]
                      || (appSettings["auto_crop"] === "On" ? "Pan & Scan" : "Letterbox")
        items.push({
            type: "list_single",
            key: "video_scaling",
            label: "Scaling",
            options: scalingOpts,
            value: scalingOpts.indexOf(scaling) >= 0 ? scaling : "Letterbox",
            description: "How a 16:9 picture fills the 4:3 screen, in every module that doesn't set its own\n[LETTERBOX] All of it, bars above and below  [14:9] A little of the sides cut, thinner bars  [PAN & SCAN] Fills it, the sides cut  [ANAMORPHIC] Fills it squeezed, for a TV set to 16:9",
            moduleId: ""
        })

        // Transparent Background — video played inside the app's own window,
        // so back from it returns to the menus with the picture going on
        // behind them (MpvController). A slider, like the deck's tape bar,
        // from TRANSPARENT to SOLID: how solid the menus' ground is over the
        // picture, in tenths, and seen at once over a video behind them.
        // SOLID hides the picture, which plays on, sound and all; select turns
        // the setting off and on, as it does a module's toggles. It needs
        // libmpv: without it the row says so.
        var embedded = mpvController.embeddedAvailable()
        var backgroundRaw = appSettings["transparent_background"]
        var backgroundOn = embedded && root.backgroundOn(backgroundRaw)
        items.push({
            type: "slider",
            key: "transparent_background",
            label: "Transparent Background",
            on: backgroundOn,
            offValue: "Off",
            // Off, the bar waits where turning it on puts it.
            value: backgroundOn ? root.solidityOf(backgroundRaw) : 40,
            step: 10,
            startText: "TRANSPARENT",
            endText: "SOLID",
            description: "How much of a video shows through the menus when back returns to them and leaves it playing behind, until you play something else or stop it on the main menu, whose first row takes it back to full screen\n[SOLID] None of it, but it plays on, sound and all  [ENTER] On or off: off, back stops the video, as it always has",
            unavailable: embedded ? "" : "Needs libmpv, which isn't installed",
            moduleId: ""
        })

        // Video Output Range, the RGB range mpv converts YUV into. mpv's default
        // is full range; a display expecting studio (limited) levels renders that
        // as crushed blacks and blown whites, and the reverse reads as washed-out
        // gray. Takes effect on the next video.
        // The key keeps mpv's own name for the option; the label uses the wording
        // TVs and GPU drivers use for the same control (HDMI RGB range, DRM
        // "Broadcast RGB"), since that's what the setting is matching.
        items.push({
            type: "list_single",
            key: "video_output_levels",
            label: "Video Levels",
            options: ["Auto", "Limited", "Full"],
            value: appSettings["video_output_levels"] || "Auto",
            description: "Match the color range your display expects\n[LIMITED] If blacks crush  [FULL] If blacks look gray",
            moduleId: ""
        })

        // 1080p Playback — for a device whose smooth decode path can't
        // crop/zoom (the Pi 3 overlay path). Default ON; turning it off
        // restores the crop-capable video output. Takes effect on the next
        // video. Elsewhere there is nothing to trade: the row says so.
        items.push({
            type: "list_single",
            key: "smooth_playback",
            label: "1080p Playback",
            options: ["On", "Off"],
            value: appSettings["smooth_playback"] || "On",
            description: "[ON] Enable 1080p content playback, crop, 14:9 and Pan & Scan will not function\n[OFF] Enable crop, 1080p content playback will stutter",
            unavailable: mpvController.hasSmoothPlaybackTradeoff() ? "" : "Only where smooth 1080p playback can't crop, as on a Raspberry Pi 3",
            moduleId: ""
        })

        // Loading Effect — the screen a video loads behind is a tape loading
        // (Components/LoadingScreen): its noise and tracking bands, or, off,
        // the deck's display alone on the plain background. Read in Main.qml.
        items.push({
            type: "list_single",
            key: "loading_effect",
            label: "Loading Effect",
            options: ["On", "Off"],
            value: appSettings["loading_effect"] === "Off" ? "Off" : "On",
            description: "While a video loads, a tape's noise and tracking bands roll over its counters\n[OFF] The counters alone, on the plain background",
            moduleId: ""
        })

        // LOADING SCREEN COLORS — the screen a video loads behind in the
        // colours in force, a theme's or a scheme's (Theme, the default), or
        // in OSD/OS's own whatever the look: Video 1's white on a VCR's blue.
        // Read in Main.qml (root.loadingInk).
        items.push({
            type: "list_single",
            key: "loading_colors",
            label: "Loading Screen Colors",
            options: ["Theme", "Default"],
            value: appSettings["loading_colors"] === "Default" ? "Default" : "Theme",
            description: "The colours of the screen a video loads behind\n[THEME] The theme's or color scheme's  [DEFAULT] White on blue",
            moduleId: ""
        })
        // CHANNEL LOGO — OSD/OS's logo in a corner of the picture while a
        // video plays, drawn by mpv (scripts/mpv-logo.lua, loaded by
        // MpvController at each launch, so it applies from the next video).
        var logoVals = ["off", "tl", "tr", "bl", "br", "all"]
        var logoOpts = ["Off", "Top Left", "Top Right", "Bottom Left", "Bottom Right", "All Corners"]
        var logoIdx = logoVals.indexOf(appSettings["video_logo"] || "tr")
        items.push({
            type: "list_single",
            key: "video_logo",
            label: "Channel Logo",
            options: logoOpts,
            values: logoVals,
            value: logoOpts[logoIdx < 0 ? logoVals.indexOf("tr") : logoIdx],
            description: "A logo in a corner of the picture while a video plays, like a channel's: OSD/OS's, or your own (Logo Image), from the next video on\n[ALL CORNERS] In all four  [OFF] None",
            moduleId: ""
        })

        // LOGO IMAGE — the channel logo's picture: OSD/OS's, or one of the
        // user's own, picked on the file picker (views/FilePicker.qml) with
        // select. Offered while Channel Logo is on; MpvController reads it.
        var logoImage = appSettings["video_logo_image"] || ""
        items.push({
            type: "file",
            key: "video_logo_image",
            label: "Logo Image",
            value: logoImage !== "" ? logoImage.substring(logoImage.lastIndexOf("/") + 1) : "OSD/OS",
            path: logoImage,
            picker: { mode: "file", types: ["png", "jpg", "jpeg", "svg", "gif", "bmp", "webp"],
                      defaultLabel: "OSD/OS Logo" },
            description: "The channel logo's picture, from the next video on: OSD/OS's, or one of your own, as tall as OSD/OS's\n[ENTER] Pick a picture on the file browser, a PNG with a clear background best",
            shownWith: { key: "video_logo", not: "Off" },
            moduleId: ""
        })

        // INFO SCREEN — a film's details in the trees (Netflix, Prime Video,
        // YouTube): with right on it always, and with a number also on its own
        // once the cursor has rested on it that many seconds. One control, like
        // the screen saver's.
        var infoVals = ["off", "key", "1", "2", "3", "5"]
        var infoOpts = ["Off", "Key", "1 sec", "2 sec", "3 sec", "5 sec"]
        var infoIdx = infoVals.indexOf(appSettings["info_screen"] || "3")
        items.push({
            type: "list_single",
            key: "info_screen",
            label: "Info Screen",
            options: infoOpts,
            values: infoVals,
            value: infoOpts[infoIdx < 0 ? infoVals.indexOf("3") : infoIdx],
            description: "A film's details in Netflix, Prime Video and YouTube\n[KEY] With right on a film  [SEC] Also on its own once the cursor rests on it",
            moduleId: ""
        })

        moduleRows(items, groupModules("tv"))
    }

    // The installed modules with settings of a group ("" for none, as a
    // group with no page here counts), and their rows under the MODULES
    // heading, a page each.
    function groupModules(group) {
        return installedModules.filter(function(m) {
            var own = m.settings_group === "tv" || m.settings_group === "arcade" ? m.settings_group : ""
            return m.has_settings && own === group
        })
    }
    function moduleRows(items, modules) {
        if (modules.length === 0)
            return
        items.push({ type: "section", label: "Modules" })
        for (var i = 0; i < modules.length; i++)
            items.push({ type: "submenu", label: modules[i].name, moduleId: modules[i].id })
    }

    // Why a row can't be used now, or "" when it can. No row is ever hidden:
    // one this system hasn't (its unavailable, such as Wi-Fi without
    // NetworkManager) or one that goes with another row's value (its
    // shownWith, { key, value }, or any but one, { key, not }: Window Frame
    // with OSD Background on Window, Logo Image while Channel Logo isn't Off,
    // Music File with Menu Music on File) is shown dimmed, the cursor
    // stopping on it and the help line saying why.
    function offReason(idx) {
        var row = settingsItems[idx]
        if (!row)
            return ""
        if (row.unavailable)
            return row.unavailable
        if (!row.shownWith)
            return ""
        for (var i = 0; i < settingsItems.length; i++) {
            var other = settingsItems[i]
            if (other.key !== row.shownWith.key)
                continue
            if (row.shownWith.not !== undefined)
                return other.value !== row.shownWith.not ? "" : "Off while " + other.label + " is " + row.shownWith.not
            if (other.value === row.shownWith.value)
                return ""
            var lower = String(row.shownWith.value).toLowerCase()
            return "Only with " + other.label + (lower === "on" || lower === "off" ? " " + lower : " on " + row.shownWith.value)
        }
        return ""
    }

    // A row choosing one of the themes or the skins there are (AppCore's
    // themes(), skins(): [{ id, name }]), or none (`none`, saved as ""):
    // saved by folder name, shown by name, two of one name apart by their
    // folders.
    // `saved` is the setting as saved, appSettings[key] unless given.
    function lookRow(key, label, none, looks, description, saved) {
        if (saved === undefined)
            saved = appSettings[key]
        var row = { type: "list_single", key: key, label: label, options: [none], values: [""],
                    value: none, description: description, moduleId: "" }
        for (var i = 0; i < looks.length; i++) {
            var name = looks[i].name
            if (row.options.indexOf(name) >= 0)
                name += " (" + looks[i].id + ")"
            row.options.push(name)
            row.values.push(looks[i].id)
            if (looks[i].id === saved)
                row.value = name
        }
        return row
    }

    // A row for one of the theme's parts: THEME (saved as "", as the theme
    // has it) while there is a theme, OFF (with `off`, saved as "Off") and
    // the choices. Without a theme, "" shows as the first of those, what it
    // is then.
    function partRow(key, label, choices, off, description) {
        var themed = !!appSettings["osd_theme"]
        var options = (themed ? ["Theme"] : []).concat(off ? ["Off"] : [], choices)
        var values = (themed ? [""] : []).concat(off ? ["Off"] : [], choices)
        var at = values.indexOf(appSettings[key] === undefined ? "" : String(appSettings[key]))
        return { type: "list_single", key: key, label: label, options: options, values: values,
                 value: options[Math.max(0, at)], description: description, moduleId: "" }
    }

    // The theme's parts, each set back to THEME as a theme is chosen (to what
    // it is without one, for None), hidden rows too; the rows made again.
    readonly property var themeParts: ["color_scheme", "skin", "text_effect", "background_effect",
                                       "selector_effect", "screen_effect", "transition", "menu_music"]
    function themeChosen() {
        for (var i = 0; i < themeParts.length; i++)
            appCore.save_setting("", themeParts[i], "")
        buildModel(settingsList.currentIndex)
    }

    // The cursor stops on rows, not on headings or STATUS's lines, read
    // alone.
    function selectable(idx) {
        var type = settingsItems[idx].type
        return type !== "section" && type !== "info"
    }

    // Paste OSD/OS Setup: the clipboard's text, through a box no one sees
    // (QML reads the clipboard only so), set at once; then what became of
    // it, over the rows as they are now.
    property var pasted: ({})
    function pasteSetup() {
        clipboardBox.text = ""
        clipboardBox.paste()
        pasted = appCore.applySetupText(clipboardBox.text)
        // Its passwords not kept about.
        clipboardBox.text = ""
        buildModel(settingsList.currentIndex)
        pastedNotice.open()
    }

    TextEdit {
        id: clipboardBox
        visible: false
        textFormat: TextEdit.PlainText
    }

    // The outputs, the cursor on the one in force.
    function openOutputs() {
        outputPrompt.open()
        for (var i = 0; i < outputOptions.length; i++) {
            if (outputOptions[i].id === outputCurrent)
                outputPrompt.choiceIndex = i
        }
    }

    // The next row the cursor stops on, up (-1) or down (1), round from one
    // end of the list to the other.
    function nextSelectable(idx, direction) {
        var n = settingsItems.length
        for (var step = 1; step < n; step++) {
            var i = ((idx + direction * step) % n + n) % n
            if (selectable(i)) return i
        }
        return idx
    }

    // Back on Camcorder's first screen: the page, its row and where the keys
    // were, as listState() left them.
    Component.onCompleted: {
        if (iconMenu && group === "") {
            // The first screen's row, after Studio Monitor's STATUS.
            camIndex = navListState.currentIndex === undefined || navListState.page === "group_status" ? 0
                     : navListState.currentIndex + (studio ? 1 : 0)
            camIcons = !navListState.camRows
            buildModel(navListState.rowIndex !== undefined ? navListState.rowIndex : 0)
        } else {
            buildModel()
        }
    }

    // Header: a release's version beside the title; none for a build made
    // from source ("dev"), which About names. A group's page: its name. The
    // first page's: the IP address at the bar's right end.
    AppBar {
        visible: !settingsRoot.monitor
        iconSource: "../../assets/images/settings.svg"
        title: "Settings"
        subtitle: settingsRoot.group !== "" ? (settingsRoot.groupTitles[settingsRoot.group] || "")
                  : settingsRoot.camcorder ? (settingsRoot.camPages[settingsRoot.camIndex] ? settingsRoot.camPages[settingsRoot.camIndex].row.label : "")
                  : root.devBuild ? "" : root.appVersion
        note: settingsRoot.group === "" ? settingsRoot.ipAddress : ""
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // IP address, on the first page's bar alone.
    property string ipAddress: ""
    // STATUS shows it too.
    onIpAddressChanged: if (studio && group === "" && pageGroup((camPages[camIndex] || {}).row) === "status") buildModel(0)
    Timer {
        interval: 5000
        running: settingsRoot.group === ""
        repeat: true
        triggeredOnStart: true
        onTriggered: settingsRoot.ipAddress = appCore.localIpAddress()
    }

    // A monitor layout's window, title, icons, help line and key guide,
    // under the rows (Settings → Look → Menu Layout).
    MonitorChrome {
        id: chrome
        anchors.fill: parent
        visible: settingsRoot.monitor
        title: settingsRoot.monitorTitle
        icons: settingsRoot.studio ? settingsRoot.camPages.map(function(page) { return page.icon }) : []
        iconIndex: settingsRoot.camIndex
        iconsActive: settingsRoot.inIcons()
        helpText: settingsRoot.monitor && root.helpLine ? settingsRoot.helpText : ""
        guide: settingsRoot.monitorGuide
    }

    ListView {
        id: settingsList
        model: settingsItems
        anchors.top: parent.top
        anchors.left: parent.left
        // Where a monitor layout has its rows (MonitorChrome), beside
        // Camcorder's icons, or where every view has its list.
        anchors.topMargin: settingsRoot.monitor ? chrome.listY : root.sh * 0.25 //120
        anchors.leftMargin: settingsRoot.monitor ? chrome.listX
                          : settingsRoot.camcorder ? root.sw * 0.209375 : root.sw * 0.115625 //134 : 74
        width: settingsRoot.monitor ? chrome.listWidth : settingsRoot.camcorder ? root.sw * 0.675 : root.sw * 0.76875 //432 : 492
        // One row short of the space, so the ▼ fits above the help line; a
        // row more for each bar that is off (Main.qml's helpRoom, hintRoom).
        height: settingsRoot.monitor ? chrome.listHeight : root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224
        // Flat Panel's page, again as its rows take more room or less.
        onHeightChanged: if (settingsRoot.flat) Qt.callLater(settingsRoot.showCurrent)
        // Flat Panel's last page filled out to a whole page, and the view
        // left where its page puts it (not moved to follow the cursor).
        footer: Item { width: 1; height: chrome.pageFill(settingsList.count) }
        highlightFollowsCurrentItem: !settingsRoot.flat
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (settingsRoot.inIcons()) {
                settingsRoot.moveIcon(-1)
                return
            }
            if (settingsRoot.editIndex >= 0) {
                settingsRoot.stepEdit(1)
                return
            }
            currentIndex = settingsRoot.nextSelectable(currentIndex, -1)
            settingsRoot.showCurrent()
        }
        Keys.onDownPressed: {
            if (settingsRoot.inIcons()) {
                settingsRoot.moveIcon(1)
                return
            }
            if (settingsRoot.editIndex >= 0) {
                settingsRoot.stepEdit(-1)
                return
            }
            currentIndex = settingsRoot.nextSelectable(currentIndex, 1)
            settingsRoot.showCurrent()
        }

        // Rows are whole lines, a slider's several, so a scroll can stop with
        // a slider cut by an edge: carry on past it, keeping the current row.
        function showWholeRows() {
            var top = itemAt(0, contentY + 1)
            if (top && top !== currentItem && top.y < contentY - 1)
                contentY = Math.min(top.y + top.height, originY + contentHeight - height)
            var bottom = itemAt(0, contentY + height - 1)
            if (bottom && bottom !== currentItem && bottom.y + bottom.height > contentY + height + 1)
                contentY = Math.max(bottom.y - height, originY)
        }

        // The current row, changed, in place of the old one. The model is
        // not replaced: a new one starts the list over, and it is not put
        // back where it was for sure once the list has been scrolled about
        // (a row's height is not known until it is drawn).
        function replaceCurrentRow(row) {
            settingsItems[currentIndex] = row
            settingsRoot.revision++
        }

        // A slider's step toward one end (-1 left, 1 right), saved and kept.
        // One that is off turns on instead, where its bar waits. A row whose
        // value couldn't be saved (a full card) stays on what is saved, here
        // and in the list ones below.
        function moveSlider(direction) {
            var row = settingsItems[currentIndex]
            if (row.on === false) {
                toggleSlider()
                return
            }
            var v = Math.max(0, Math.min(100, row.value + direction * row.step))
            if (v === row.value || !appCore.save_setting(row.moduleId, row.key, v))
                return
            replaceCurrentRow(Object.assign({}, row, { value: v }))
        }

        // A slider that can be off (one with `on`), turned off, saved as its
        // offValue, or back on at its value.
        function toggleSlider() {
            var row = settingsItems[currentIndex]
            var on = !row.on
            if (appCore.save_setting(row.moduleId, row.key, on ? row.value : row.offValue))
                replaceCurrentRow(Object.assign({}, row, { on: on }))
        }

        // A list row's choice, one step back (-1) or on (1), saved and shown;
        // the label shown, the parallel value saved when there is one.
        function cycleList(direction) {
            var opts = settingsItems[currentIndex].options
            setChoice((opts.indexOf(settingsItems[currentIndex].value) + direction + opts.length) % opts.length)
        }
        function setChoice(newIdx) {
            var row = settingsItems[currentIndex]
            var opts = row.options
            if (newIdx < 0 || opts[newIdx] === row.value)
                return
            if (!appCore.save_setting(row.moduleId, row.key, row.values ? row.values[newIdx] : opts[newIdx]))
                return
            replaceCurrentRow(Object.assign({}, row, { value: opts[newIdx] }))
            if (row.key === "osd_theme")
                settingsRoot.themeChosen()
        }
        // A slider set at once (a monitor layout's second step): a value,
        // or -1 for Off where it can be.
        function setSlider(v) {
            var row = settingsItems[currentIndex]
            if (v < 0) {
                if (row.on !== false && appCore.save_setting(row.moduleId, row.key, row.offValue))
                    replaceCurrentRow(Object.assign({}, row, { on: false }))
                return
            }
            if ((v === row.value && row.on !== false) || !appCore.save_setting(row.moduleId, row.key, v))
                return
            replaceCurrentRow(Object.assign({}, row, row.on !== undefined ? { value: v, on: true } : { value: v }))
        }

        // A line select leads on from, to another screen: a page's, Display
        // Output's list, a file's picker, Quit's question. ► opens it too,
        // and its line ends in a ►.
        function opens(row) {
            return !!row && (row.type === "submenu" || row.type === "display_output"
                             || row.type === "file" || row.type === "quit" || row.type === "paste_setup")
        }

        function openRow(row) {
            if (row.type === "submenu") {
                if (row.key === "software_update")
                    settingsRoot.navigateTo("views/Update.qml", {}, settingsRoot.listState())
                else if (row.key === "remap_controls")
                    settingsRoot.navigateTo("views/Controllers.qml", {}, settingsRoot.listState())
                else if (row.key === "wifi")
                    settingsRoot.navigateTo("views/Wifi.qml", {}, settingsRoot.listState())
                else if (row.key === "bluetooth")
                    settingsRoot.navigateTo("views/Bluetooth.qml", {}, settingsRoot.listState())
                else if (row.key === "about")
                    settingsRoot.navigateTo("views/About.qml", {}, settingsRoot.listState())
                else if (row.key === "test_patterns")
                    settingsRoot.navigateTo("views/TestPatterns.qml", {}, settingsRoot.listState())
                else if (row.key === "cabinet_lock")
                    settingsRoot.navigateTo("views/CabinetLock.qml", {}, settingsRoot.listState())
                else if (row.key && row.key.indexOf("group_") === 0 && row.key !== "group_arcade")
                    settingsRoot.navigateTo("views/Settings.qml", { group: row.key.substring(6) },
                                            settingsRoot.listState())
                else
                    settingsRoot.navigateTo("views/ModuleSettings.qml", { moduleId: row.moduleId, alsoModules: row.alsoModules || [] },
                                            settingsRoot.listState())
            } else if (row.type === "display_output") {
                settingsRoot.openOutputs()
            } else if (row.type === "file") {
                // Picked on the file picker, which saves it and comes back.
                settingsRoot.navigateTo("views/FilePicker.qml",
                                        Object.assign({ moduleId: row.moduleId, settingKey: row.key,
                                                        currentPath: row.path, label: row.label }, row.picker),
                                        settingsRoot.listState())
            } else if (row.type === "quit") {
                quitPrompt.open()
            } else if (row.type === "paste_setup") {
                settingsRoot.pasteSetup()
            }
        }

        // A dimmed row (offReason) takes no keys but the cursor's and back.
        function usable() {
            return settingsRoot.offReason(currentIndex) === ""
        }

        Keys.onLeftPressed: {
            var row = settingsItems[currentIndex]
            // Flat Panel: ◄ is back (and leaves a value being changed).
            if (settingsRoot.flat) {
                settingsRoot.back()
                return
            }
            // Broadcast: ▲ ▼ change a value, in its second step.
            if (settingsRoot.inIcons() || !usable() || settingsRoot.twoStep)
                return
            if (row && row.type === "slider")
                moveSlider(-1)
            else if (row && row.type === "list_single")
                cycleList(-1)
        }

        Keys.onRightPressed: {
            if (settingsRoot.inIcons()) {
                settingsRoot.enterPage()
                return
            }
            var row = settingsItems[currentIndex]
            if (!usable() || settingsRoot.editIndex >= 0)
                return
            if (settingsRoot.twoStep) {
                if (opens(row))
                    openRow(row)
                return
            }
            if (row && row.type === "slider")
                moveSlider(1)
            else if (row && row.type === "list_single")
                cycleList(1)
            else if (opens(row))
                openRow(row)
        }

        Keys.onReturnPressed: {
            if (settingsRoot.inIcons()) {
                settingsRoot.enterPage()
                return
            }
            if (settingsRoot.editIndex >= 0) {
                settingsRoot.confirmEdit()
                return
            }
            var row = settingsItems[currentIndex]
            if (!usable())
                return
            // Broadcast and Flat Panel: select starts a value's change.
            if (settingsRoot.twoStep && row && (row.type === "list_single" || row.type === "slider")) {
                settingsRoot.startEdit()
                return
            }
            if (opens(row))
                openRow(row)
            else if (row && row.type === "slider" && row.on !== undefined)
                toggleSlider()
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                settingsRoot.back()
                event.accepted = true
            }
        }

        // The mouse over a row (Hover.js): on Camcorder's first screen, the
        // keys go into the rows; over the row of a page with none of its own,
        // they stay in the icons.
        function hoverRow(index, list) {
            if (index !== currentIndex)
                settingsRoot.editIndex = -1
            if (settingsRoot.inIcons()) {
                if (settingsRoot.pageGroup(settingsRoot.camPages[settingsRoot.camIndex].row) === "")
                    return
                settingsRoot.camIcons = false
            }
            currentIndex = index
        }

        delegate: Item {
            id: rowItem
            // The row as it is now, read again as one changes.
            readonly property var row: (settingsRoot.revision, settingsRoot.settingsItems[index])
            // Why it can't be used now, "" when it can: dimmed, with no
            // ◄ ► or ► of its own (offReason()).
            readonly property string off: (settingsRoot.revision, settingsRoot.offReason(index))
            readonly property bool slider: row.type === "slider"
            readonly property real lineHeight: root.sh * 0.0583333 //28
            // A slider's bar takes whole lines under its own, so every line
            // keeps to the list's rule as it scrolls.
            // In a monitor layout a slider is its line alone, its value a
            // number (shownValue()).
            readonly property int barLines: slider && !settingsRoot.monitor ? Math.ceil((tape.height + 2 * root.px) / lineHeight) : 0
            width: settingsList.width
            height: settingsRoot.monitor ? chrome.rowHeight : lineHeight * (1 + barLines)

            // A line laid out like a camcorder's menu, "DISPLAY······ON", or a
            // section's heading, as large as the lines under it: "MODULES ─────".
            // A line that leads to a page ends in a ►; a list's choice, which
            // ◄ ► change, stands between a ◄ and a ►.
            MenuRow {
                visible: !settingsRoot.monitor
                width: parent.width
                height: rowItem.lineHeight
                heading: rowItem.row.type === "section"
                label: rowItem.row.label || ""
                value: rowItem.row.type === "list_single" || rowItem.row.type === "display_output"
                       || rowItem.row.type === "file" || rowItem.row.type === "submenu"
                       ? (rowItem.row.value || "")
                     : rowItem.slider && rowItem.row.on !== undefined ? (rowItem.row.on ? "On" : "Off") : ""
                opens: rowItem.off === "" && settingsList.opens(rowItem.row)
                changes: rowItem.off === "" && rowItem.row.type === "list_single"
                dimmed: rowItem.off !== ""
                // In Camcorder's icons the box is on the icon.
                selected: settingsList.currentIndex === index && !settingsRoot.inIcons()
            }

            // A slider's setting, as the deck's tape bar shows how far the
            // tape is: ◄ ► move the ▼ between the two ends, and stand beyond
            // them. Off, or dimmed, its lines stay, empty.
            // The line in a monitor layout.
            Loader {
                active: settingsRoot.monitor
                width: parent.width
                height: rowItem.height
                sourceComponent: MonitorRow {
                    label: rowItem.row.label || ""
                    heading: rowItem.row.type === "section"
                    value: (settingsRoot.editIndex, settingsRoot.editValue,
                            settingsRoot.shownValue(rowItem.row, index))
                    opens: rowItem.off === "" && settingsList.opens(rowItem.row)
                    dimmed: rowItem.off !== ""
                    selected: settingsList.currentIndex === index && !settingsRoot.inIcons()
                    editing: settingsRoot.editIndex === index
                }
            }

            Loader {
                id: tape
                active: rowItem.slider && !settingsRoot.monitor
                visible: rowItem.row.on !== false && rowItem.off === ""
                x: root.sw * 0.009375 //6
                y: rowItem.lineHeight + Math.round((rowItem.barLines * rowItem.lineHeight - height) / 2)
                width: parent.width - 2 * x
                sourceComponent: OsdTapeBar {
                    changes: true
                    fontSize: root.sh * 0.0333333 //16
                    value: rowItem.row.value / 100
                    startText: rowItem.row.startText
                    endText: rowItem.row.endText
                }
            }
        }
    }

    // ▲ / ▼ while lines are hidden above or below; Flat Panel's page says
    // so instead.
    ScrollMarks {
        visible: !settingsRoot.flat
        anchors.fill: settingsList
        list: settingsList
        color: root.menuPalette ? root.menuPalette.ink : root.primaryColor
    }

    // Camcorder's icons, the first screen's pages, at the left of the rows.
    IconColumn {
        visible: settingsRoot.camcorder
        model: settingsRoot.camPages
        currentIndex: settingsRoot.camIndex
        active: settingsRoot.inIcons()
        x: root.sw * 0.115625 //74
        y: settingsList.y
        width: root.sw * 0.084375 //54
        height: settingsList.height
    }

    // --- HELP TEXT --- (shown when a focused row has a description), on one
    // line that scrolls when it is too long for the box.
    // A dimmed row's says first why it can't be used now.
    // In Camcorder's icons, the page's.
    readonly property var helpRow: inIcons() ? (camPages[camIndex] || {}).row : settingsItems[settingsList.currentIndex]
    readonly property string helpOff: inIcons() ? "" : (revision, offReason(settingsList.currentIndex))
    readonly property string helpText: (helpOff !== "" ? helpOff + "\n" : "") + ((helpRow && helpRow.description) || "")
    HelpLine {
        objectName: "helpLine"
        visible: !settingsRoot.monitor && text !== ""
        text: settingsRoot.helpText
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // --- FOOTER ---
    HintBar {
        id: footer
        visible: !settingsRoot.monitor
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + (settingsRoot.inIcons() ? root.hints.browse + ":OPEN " : root.hints.change + ":CHANGE ")
              + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // --- DISPLAY OUTPUT: the outputs this board has, then how to switch ---
    ChoiceOverlay {
        id: outputPrompt
        anchors.fill: parent
        promptText: "Display Output"
        subtitleText: settingsRoot.boardModel + "\nNow: " + settingsRoot.outputCurrentLabel
                      + (settingsRoot.outputNext !== "" ? "\nNext start: " + settingsRoot.outputNextLabel : "")
        choices: settingsRoot.outputOptions.map(function(o) { return { label: o.label, action: o.id } })
        onActivated: function(id) {
            for (var i = 0; i < settingsRoot.outputOptions.length; i++) {
                if (settingsRoot.outputOptions[i].id === id)
                    settingsRoot.outputChoice = settingsRoot.outputOptions[i]
            }
            // The one in force: nothing to do, or what waits for the next
            // start taken back.
            if (id === settingsRoot.outputCurrent) {
                if (settingsRoot.outputNext !== "")
                    outputStay.open()
                return
            }
            outputConfirm.open()
        }
        onClosed: settingsList.forceActiveFocus()
    }

    // Now, with a restart; written now for the next start, the Pi left on;
    // or written, then off: on the new output the next time it is switched
    // on, as when it goes into a cabinet whose screen this one isn't.
    ChoiceOverlay {
        id: outputConfirm
        anchors.fill: parent
        promptText: "Switch to " + (settingsRoot.outputChoice.label || "") + "?"
        // A scan rate only some monitors take says so first.
        subtitleText: (settingsRoot.outputChoice.warning ? settingsRoot.outputChoice.warning + "\n" : "")
                      + (settingsRoot.outputAtNextStart || settingsRoot.outputPowerOff
                         ? "Now, or from the next time the Pi starts. " : "OSD/OS restarts on it. ")
                      + "Keep it there, or in 30 seconds it comes back to " + settingsRoot.outputCurrentLabel
        choices: [{ label: "Switch and Restart", action: "switch" }]
                 .concat(settingsRoot.outputAtNextStart && settingsRoot.outputChoice.id !== settingsRoot.outputNext
                         ? [{ label: "At Next Start", action: "next" }] : [])
                 .concat(settingsRoot.outputPowerOff ? [{ label: "Switch and Power Off", action: "poweroff" }] : [])
                 .concat([{ label: "Cancel", action: "cancel" }])
        onActivated: function(act) {
            if (act === "switch")
                displayOutput.apply(settingsRoot.outputChoice.id)
            else if (act === "next")
                displayOutput.applyAtNextStart(settingsRoot.outputChoice.id)
            else if (act === "poweroff")
                displayOutput.applyAndPowerOff(settingsRoot.outputChoice.id)
        }
        onClosed: settingsList.forceActiveFocus()
    }

    ChoiceOverlay {
        id: outputStay
        anchors.fill: parent
        promptText: "Stay on " + settingsRoot.outputCurrentLabel + "?"
        subtitleText: settingsRoot.outputNextLabel + " was to come at the next start"
        // The title names the output: a long one wouldn't fit an answer.
        choices: [{ label: "Stay", action: "stay" },
                  { label: "Cancel", action: "cancel" }]
        onActivated: function(act) {
            if (act === "stay")
                displayOutput.applyAtNextStart(settingsRoot.outputCurrent)
        }
        onClosed: settingsList.forceActiveFocus()
    }

    // Window Mode switched from the keyboard (F11) while the page is open:
    // its row made again, where the page didn't make the change itself.
    Connections {
        target: appCore
        ignoreUnknownSignals: true
        function onAppSettingChanged(key, value) {
            if (key !== "window_mode" || !settingsRoot.desktopWindow)
                return
            var shown = settingsRoot.settingsItems.filter(function(r) { return r.key === "window_mode" })[0]
            if (shown && shown.value !== (value === "On" ? "On" : "Off"))
                settingsRoot.buildModel(settingsList.currentIndex)
        }
    }

    // At Next Start's answer: the row as it is now, or why it didn't happen.
    Connections {
        target: displayOutput
        function onChanged() {
            if (displayOutput.available)
                settingsRoot.buildModel(settingsList.currentIndex)
        }
        function onNextStartFinished(ok, reason) {
            if (ok)
                return
            settingsRoot.outputFailure = reason
            outputFailed.open()
        }
    }

    ChoiceOverlay {
        id: pastedNotice
        anchors.fill: parent
        promptKind: "notice"
        promptText: settingsRoot.pasted.title || ""
        subtitleText: settingsRoot.pasted.detail || ""
        choices: []
        onClosed: settingsList.forceActiveFocus()
    }

    ChoiceOverlay {
        id: outputFailed
        anchors.fill: parent
        promptKind: "notice"
        promptText: "Not written for the next start"
        subtitleText: (settingsRoot.outputChoice.label || "") + " couldn't be written: " + settingsRoot.outputFailure
        choices: []
        onClosed: settingsList.forceActiveFocus()
    }

    // --- QUIT CONFIRMATION OVERLAY ---
    ChoiceOverlay {
        id: quitPrompt
        anchors.fill: parent
        promptText: "Really quit?"
        choices: quitOptions
        onActivated: function(act) {
            if (act === "quit")          Qt.quit()
            else if (act === "restart")  Qt.exit(12)   // osdos-stop reboots on it
            else if (act === "terminal") Qt.exit(10)   // matches EXIT_STATUS check in osdos-stop
            else if (act === "logs")     settingsRoot.saveLogs()
        }
        onClosed: settingsList.forceActiveFocus()
    }

    // --- SAVE LOGS ---
    // Saving, then where they went or why not. Back closes it meanwhile; the
    // copy goes on and is told in the log.
    function saveLogs() {
        logsNotice.saving = true
        logsNotice.saved = false
        logsNotice.detail = ""
        logsNotice.open()
        if (!appCore.saveLogs()) {
            logsNotice.saving = false
            logsNotice.detail = "A copy is already being made, or this system has no card to copy them to"
        }
    }
    Connections {
        target: appCore
        ignoreUnknownSignals: true
        function onLogsSaved(ok, detail) {
            logsNotice.saving = false
            logsNotice.saved = ok
            logsNotice.detail = detail
        }
    }
    ChoiceOverlay {
        id: logsNotice
        property bool saving: false
        property bool saved: false
        property string detail: ""
        anchors.fill: parent
        promptKind: "notice"
        promptText: saving ? "Saving logs…" : saved ? "Logs saved" : "Logs not saved"
        subtitleText: saving ? "To the card's OSD-OS drive"
                    : saved ? "On the card's OSD-OS drive, in " + detail.replace(/\//g, "\\")
                    : detail
        choices: []
        onClosed: settingsList.forceActiveFocus()
    }
}
