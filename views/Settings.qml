import QtQuick
import Components
import "SettingsPages.js" as Pages

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
    // window (the shell's frame, Components/MonitorChrome), a STATUS page
    // first, what is set, read alone (SettingsPages.js's statusRows()).
    // Broadcast and Flat Panel are lists, as
    // Standard is, in a monitor's look: a value changed in two steps
    // (editIndex), and Cabinet Lock at the first screen's foot as Key
    // Protect; Flat Panel a page of rows at a time.
    readonly property string layout: root.menuLayout
    readonly property bool camcorder: root.camcorderLayout
    readonly property bool studio: root.studioLayout
    readonly property bool broadcast: root.broadcastLayout
    readonly property bool flat: root.flatLayout
    // In the shell's frame (Main.qml's root.chrome, a MonitorChrome), its
    // rows drawn by MonitorRow, in the layout's own colours.
    readonly property bool monitor: studio || broadcast || flat
    // The pages as icons beside the rows: Camcorder's column, Studio
    // Monitor's strip. Broadcast and Flat Panel change a value in two
    // steps (Main.qml's twoStep).
    readonly property bool iconMenu: camcorder || studio
    property var camPages: []
    property int camIndex: 0
    property bool camIcons: true
    // Each first-screen row's icon (PixelIcon's names), by its key or type.
    readonly property var pageIcons: ({ group_tv: "tv", group_arcade: "joystick", group_look: "sparkle",
                                        group_screen: "monitor", group_sound: "speaker", remap_controls: "gamepad",
                                        group_network: "antenna", group_startup: "play", group_system: "gear",
                                        quit: "eject", group_status: "status" })
    // Quit's under autostart (the OSD/OS image), where it is a power menu:
    // the power symbol.
    function pageIcon(row) {
        var key = pageKey(row)
        return key === "quit" && autostartSession ? "power" : pageIcons[key] || ""
    }
    // Studio Monitor's first page, before the first screen's.
    readonly property var statusPage: ({ type: "submenu", key: "group_status", label: "Status",
                                         description: "What is set, at a glance: the outputs, the network, the theme and what OSD/OS starts on",
                                         moduleId: "", page: "views/Settings.qml", params: { group: "status" } })
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
    // them at once. Only that page's rows are made (showRows()): the
    // settings, the modules, the first screen's rows and the icons are as
    // they were read, and read again only if a setting changed under the
    // page.
    function moveIcon(direction) {
        var n = camPages.length
        if (n === 0)
            return
        camIndex = (camIndex + direction + n) % n
        showRows(0)
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
        if (settingsList.stopsAt(settingsList.currentIndex))
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
    // Where the page is, from Settings down, for a monitor layout's frame,
    // which writes it its own way (MonitorChrome's title): Studio Monitor's
    // tab the page shown, Broadcast the way under Settings, Flat Panel the
    // whole path. With the pages as icons, the first screen is on the page
    // under the cursor.
    readonly property var pagePath: {
        if (group === "effects")
            return ["Settings", "Look", "Effects"]
        if (group !== "")
            return ["Settings", groupTitles[group] || ""]
        var page = iconMenu ? ((camPages[camIndex] || {}).row || {}).label || "" : ""
        return page !== "" ? ["Settings", page] : ["Settings"]
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

    property var appSettings: ({})
    // Where the menus can be a window on a desktop (Main.qml's canWindow):
    // Windows, and a Linux or BSD desktop. On the Pi's console and a Mac the
    // Window Mode row is dimmed.
    readonly property bool desktopWindow: root.canWindow === true
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
    readonly property var saveLogsOption: settingsRoot.canSaveLogs ? [{ label: "Save Logs", action: "logs" }] : []
    // The answer that keeps the session first, under the cursor as the
    // question opens: select twice never ends it.
    property var quitOptions: settingsRoot.autostartSession
        ? [{ label: "Cancel",           action: "cancel"   },
           { label: "Power Off",        action: "quit"     }]
          .concat(settingsRoot.canRestart ? [{ label: "Restart", action: "restart" }] : [])
          .concat([{ label: "Exit to Terminal", action: "terminal" }])
          .concat(saveLogsOption)
        : [{ label: "No",  action: "cancel" },
           { label: "Yes", action: "quit" }]
          .concat(saveLogsOption)

    // The page made, or made again where what it is made from may have
    // changed (a theme chosen, settings pasted, the layout, a row another
    // row follows). `at`: the row the cursor stays on, else the one it was
    // on last.
    function buildModel(at) {
        readAll()
        showRows(at)
    }

    // What every page's rows are made from: the settings, the modules, the
    // first screen's rows and, with icons, its pages (Studio Monitor's
    // STATUS first) and their icons for the layout's frame. Read as the page
    // is made, not at each move along the icons: a read of config.json's
    // whole content, the modules and the first screen for every icon passed.
    property var firstScreen: []
    property var pageIconList: []
    function readAll() {
        var cfg = appCore.get_settings()
        appSettings = cfg.app || {}
        stale = false
        installedModules = appCore.get_installed_modules()
        autostartSession = appCore.isAutostartSession()
        canRestart = appCore.canRestartSystem()
        canSaveLogs = appCore.canSaveLogs()
        themeList = null
        skinList = null

        var first = []
        Pages.firstRows(first)
        firstScreen = first
        builtLayout = layout
        builtIcons = iconMenu
        var pages = iconMenu ? (studio ? [statusPage] : []).concat(first) : []
        var icons = pages.map(function(row) { return { icon: pageIcon(row) } })
        pageIconList = icons
        camPages = pages.map(function(row, i) { return { row: row, icon: icons[i].icon } })
    }
    // The themes and the skins there are (AppCore's themes(), skins(), which
    // look through their folders and read each one's file), listed once a
    // read, as a page first needs them (Look's, STATUS's Theme): not again
    // for each page made, nor each time STATUS is read again.
    property var themeList: null
    property var skinList: null
    function themes() {
        if (themeList === null)
            themeList = appCore.themes()
        return themeList
    }
    function skins() {
        if (skinList === null)
            skinList = appCore.skins()
        return skinList
    }
    // The settings as readAll() read them are no longer what is saved: one
    // was changed by another hand than this page's (onAppSettingChanged; its
    // own saves keep appSettings in step, save()). Read again before the
    // next rows are made.
    property bool stale: false
    property bool saving: false

    // A row's value saved, and kept in appSettings, which the rows are made
    // from: a page's rows made again as the icons come back to it show what
    // was just set. `soon`, a slider's step, written once the steps rest
    // (AppCore::save_setting_soon()). False when it couldn't be saved.
    function save(row, value, soon) {
        saving = true
        var saved = soon ? appCore.save_setting_soon(row.moduleId, row.key, value)
                         : appCore.save_setting(row.moduleId, row.key, value)
        saving = false
        // A module's setting (a module row's on or off) is not one of the
        // app's.
        if (saved && !row.moduleId)
            appSettings[row.key] = value
        return saved
    }

    // The rows of the page shown, made from what readAll() read: the first
    // screen's, a heading's page's, or with icons those of the page the
    // cursor's icon is (a page opened from one has its heading's icon).
    function showRows(at) {
        if (stale)
            readAll()
        editIndex = -1
        var pages = camPages.map(function(page) { return page.row })
        if (iconMenu && group !== "") {
            var heading = "group_" + (group === "effects" ? "look" : group)
            camIndex = Math.max(0, pages.findIndex(function(row) { return row.key === heading }))
        }
        camIndex = Math.max(0, Math.min(camIndex, pages.length - 1))
        var shown = iconMenu && group === "" ? pageGroup(pages[camIndex]) : group

        var items = []
        if (shown === "tv")
            Pages.tvRows(items)
        else if (shown === "look")
            Pages.lookRows(items)
        else if (shown === "effects")
            Pages.effectRows(items)
        else if (shown === "screen")
            Pages.screenRows(items)
        else if (shown === "sound")
            Pages.soundRows(items)
        else if (shown === "network")
            Pages.networkRows(items)
        else if (shown === "startup")
            Pages.startupRows(items)
        else if (shown === "system")
            Pages.systemRows(items)
        else if (shown === "status")
            Pages.statusRows(items)
        else if (iconMenu && group === "")
            items = [pages[camIndex]]
        else
            items = firstScreen.slice()

        settingsItems = items
        // Audio Output's cards are on this page: read again as it stays open.
        audioListed = (shown === "sound" || shown === "status") && !!audioOutput && audioOutput.available

        // Restore saved position, or default to first selectable row
        var start = at !== undefined ? Math.min(at, items.length - 1)
                  : navListState.currentIndex !== undefined ? Math.min(navListState.currentIndex, items.length - 1) : 0
        start = Math.max(0, start)
        var stop = settingsList.selectableFrom(start, 1)
        settingsList.currentIndex = settingsList.stopsAt(stop) ? stop : start
        settingsList.show(settingsList.currentIndex)
    }

    // Audio Output's cards are read as its row is made (settingRow()), and
    // again every couple of seconds while the row is on the page (Sound's,
    // or STATUS's Audio line): a USB sound card plugged in or taken out
    // shows there, and (Unplugged) comes and goes, without leaving the page.
    // The page is made again only where the answer changed (audioShown, what
    // the row was made from), the cursor kept, and never under a value being
    // changed in two steps.
    property bool audioListed: false
    property string audioShown: ""
    property int audioFollowInterval: 2000
    function followAudio() {
        if (editIndex >= 0 || !audioOutput || !audioOutput.available)
            return
        if (JSON.stringify(audioOutput.settingRow()) !== audioShown)
            buildModel(settingsList.currentIndex)
    }
    Timer {
        interval: settingsRoot.audioFollowInterval
        repeat: true
        running: settingsRoot.audioListed
        onTriggered: settingsRoot.followAudio()
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

    // The theme's parts, each set back to THEME as a theme is chosen (to what
    // it is without one, for None), hidden rows too; the rows made again.
    readonly property var themeParts: ["color_scheme", "skin", "text_effect", "background_effect",
                                       "selector_effect", "screen_effect", "transition", "menu_music"]
    function themeChosen() {
        // In one write of the settings' file, not one a part.
        var parts = {}
        for (var i = 0; i < themeParts.length; i++)
            parts[themeParts[i]] = ""
        appCore.save_settings("", parts)
        buildModel(settingsList.currentIndex)
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

    // A slider's steps wait to be written to config.json until they rest
    // (moveSlider()): leaving the page writes them at once. AppCore as the
    // page opened, for that: the context property may read null while the
    // view Loader tears down.
    property var core: null
    Component.onDestruction: if (core) core.flush_settings()

    // Back on Camcorder's first screen: the page, its row and where the keys
    // were, as listState() left them.
    Component.onCompleted: {
        core = appCore
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
        iconSource: "../../assets/images/settings.svg"
        title: "Settings"
        path: settingsRoot.pagePath
        // Broadcast's first screen, as a monitor's menu is headed.
        frameTitle: settingsRoot.broadcast && settingsRoot.group === "" ? "Menu" : ""
        subtitle: settingsRoot.group !== "" ? (settingsRoot.groupTitles[settingsRoot.group] || "")
                  : settingsRoot.camcorder ? (settingsRoot.camPages[settingsRoot.camIndex] ? settingsRoot.camPages[settingsRoot.camIndex].row.label : "")
                  : root.devBuild ? "" : root.appVersion
        note: settingsRoot.group === "" ? settingsRoot.ipAddress : ""
    }

    // IP address, on the first page's bar alone.
    property string ipAddress: ""
    // STATUS shows it too.
    onIpAddressChanged: if (statusShown()) buildModel(0)
    // STATUS on the page (Studio Monitor's first), and its lines read again
    // as what they show changes while it is: Wi-Fi's as a network is joined
    // or left, Bluetooth's as it is turned on or off or a device connects.
    // Made again only where a line reads otherwise.
    function statusShown() {
        return studio && group === "" && pageGroup((camPages[camIndex] || {}).row) === "status"
    }
    function followStatus() {
        if (!statusShown())
            return
        var lines = []
        Pages.statusRows(lines)
        if (JSON.stringify(lines) !== JSON.stringify(settingsItems))
            buildModel(0)
    }
    Connections {
        target: wifiManager
        ignoreUnknownSignals: true
        function onStateChanged() { settingsRoot.followStatus() }
    }
    Connections {
        target: bluetoothManager
        ignoreUnknownSignals: true
        function onAdapterChanged() { settingsRoot.followStatus() }
        function onDevicesChanged() { settingsRoot.followStatus() }
    }
    Timer {
        interval: 5000
        running: settingsRoot.group === "" && !root.screenSaverActive
        repeat: true
        triggeredOnStart: true
        onTriggered: settingsRoot.ipAddress = appCore.localIpAddress()
    }

    // The rows: where a monitor layout has them (MonitorChrome), beside
    // Camcorder's icons, or where every view has its list, a whole row at a
    // time (a slider's several lines); Flat Panel's a page at a time. A
    // monitor layout's window, title, icons, help line and key guide are
    // under them (Settings → Look → Menu Layout): the shell's frame
    // (Main.qml's root.chrome), which says where the rows go, and is told
    // the rest by the title bar, the help line and the hint bar below.
    MenuList {
        id: settingsList
        model: settingsItems
        focus: true
        // In the icons ▲ ▼ go from page to page; a value being changed in
        // two steps, ▲ a step on and ▼ one back.
        held: settingsRoot.inIcons() || settingsRoot.editIndex >= 0
        onHeldStep: function(delta) {
            if (settingsRoot.inIcons())
                settingsRoot.moveIcon(delta)
            else
                settingsRoot.stepEdit(-delta)
        }

        // The current row, changed, in place of the old one. The model is
        // not replaced: a new one starts the list over, and it is not put
        // back where it was for sure once the list has been scrolled about
        // (a row's height is not known until it is drawn).
        function replaceCurrentRow(row) {
            settingsItems[currentIndex] = row
            settingsRoot.revision++
        }

        // A slider's step toward one end (-1 left, 1 right), kept and in
        // force at once, and saved once the steps rest or the page is left
        // (AppCore::save_setting_soon()): saved at every step, the key held
        // down would write config.json to the card ten times across the bar.
        // One that is off turns on instead, where its bar waits. A row whose
        // value couldn't be saved (a full card) stays on what is saved, here
        // and in the list ones below; a slider's goes back to it once the
        // write has failed (onSettingsNotSaved).
        function moveSlider(direction) {
            var row = settingsItems[currentIndex]
            if (row.on === false) {
                toggleSlider()
                return
            }
            var v = Math.max(0, Math.min(100, row.value + direction * row.step))
            if (v === row.value || !settingsRoot.save(row, v, true))
                return
            replaceCurrentRow(Object.assign({}, row, { value: v }))
        }

        // A slider that can be off (one with `on`), turned off, saved as its
        // offValue, or back on at its value.
        function toggleSlider() {
            var row = settingsItems[currentIndex]
            var on = !row.on
            if (settingsRoot.save(row, on ? row.value : row.offValue))
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
            if (!settingsRoot.save(row, row.values ? row.values[newIdx] : opts[newIdx]))
                return
            replaceCurrentRow(Object.assign({}, row, { value: opts[newIdx] }))
            if (row.key === "osd_theme")
                settingsRoot.themeChosen()
            // Cabinet Lock's row says whether it has a module to stay on.
            else if (row.key === "startup_module")
                settingsRoot.buildModel(currentIndex)
        }
        // A module's row: the module turned on or off (its "enabled", as its
        // own page's ENABLED saves it), the page made again from what the
        // modules are now (Info Screen and Start on Module follow them). In
        // Broadcast and Flat Panel, which change a value in two steps,
        // ► and select open its page, where ENABLED is.
        function toggleModule() {
            var row = settingsItems[currentIndex]
            if (settingsRoot.save(row, !row.on))
                settingsRoot.buildModel(currentIndex)
        }
        // A slider set at once (a monitor layout's second step): a value,
        // or -1 for Off where it can be.
        function setSlider(v) {
            var row = settingsItems[currentIndex]
            if (v < 0) {
                if (row.on !== false && settingsRoot.save(row, row.offValue))
                    replaceCurrentRow(Object.assign({}, row, { on: false }))
                return
            }
            if ((v === row.value && row.on !== false) || !settingsRoot.save(row, v))
                return
            replaceCurrentRow(Object.assign({}, row, row.on !== undefined ? { value: v, on: true } : { value: v }))
        }

        // A line select leads on from, to another screen: a page's, Display
        // Output's list, a file's picker, Quit's question. ► opens it too,
        // and its line ends in a ►.
        function opens(row) {
            return !!row && (row.type === "submenu" || row.type === "module" || row.type === "display_output"
                             || row.type === "file" || row.type === "quit" || row.type === "paste_setup")
        }

        function openRow(row) {
            if (row.type === "submenu" || row.type === "module") {
                // A row that leads to a page names it (`page`, `params`:
                // SettingsPages.js); the rest open over this one.
                if (row.key === "time_zone" || row.key === "keyboard")
                    settingsRoot.openSystemPicker(row.key)
                else if (row.key === "mpv_video_args")
                    videoArgsKeyboard.open(String(appSettings["mpv_video_args"] || ""))
                else
                    settingsRoot.navigateTo(row.page, Object.assign({}, row.params), settingsRoot.listState())
            } else if (row.type === "display_output") {
                outputPrompts.open()
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
            if (settingsRoot.inIcons() || !usable() || root.twoStep)
                return
            if (row && row.type === "slider")
                moveSlider(-1)
            else if (row && row.type === "list_single")
                cycleList(-1)
            else if (row && row.type === "module")
                toggleModule()
        }

        Keys.onRightPressed: {
            if (settingsRoot.inIcons()) {
                settingsRoot.enterPage()
                return
            }
            var row = settingsItems[currentIndex]
            if (!usable() || settingsRoot.editIndex >= 0)
                return
            if (root.twoStep) {
                if (opens(row))
                    openRow(row)
                return
            }
            if (row && row.type === "slider")
                moveSlider(1)
            else if (row && row.type === "list_single")
                cycleList(1)
            else if (row && row.type === "module")
                toggleModule()
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
            if (root.twoStep && row && (row.type === "list_single" || row.type === "slider")) {
                settingsRoot.startEdit()
                return
            }
            if (opens(row))
                openRow(row)
            else if (row && row.type === "slider" && row.on !== undefined)
                toggleSlider()
        }

        Keys.onPressed: function(event) {
            if (root.isBack(event)) {
                settingsRoot.back()
                event.accepted = true
            }
        }

        // The mouse over a row (Hover.js): on Camcorder's first screen, the
        // keys go into the rows; over the row of a page with none of its own,
        // they stay in the icons. A line the cursor doesn't stop on (STATUS's,
        // read alone: MenuList's selectable()) takes neither the cursor nor
        // the keys: the mouse passes over it.
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

        // A line laid out like a camcorder's menu, "DISPLAY······ON", or a
        // section's heading, as large as the lines under it: "MODULES ─────".
        // A line that leads to a page ends in a ►; a list's choice, which
        // ◄ ► change, stands between a ◄ and a ►. A slider's setting is the
        // deck's tape bar under its line, ◄ ► moving the ▼ between the two
        // ends; off, or dimmed, its lines stay, empty. In a monitor layout
        // a slider is its line alone, its value a number (shownValue()).
        delegate: SettingRow {
            id: rowItem
            // The row as it is now, read again as one changes.
            readonly property var row: (settingsRoot.revision, settingsRoot.settingsItems[index])
            // Why it can't be used now, "" when it can: dimmed, with no
            // ◄ ► or ► of its own (offReason()).
            readonly property string off: (settingsRoot.revision, settingsRoot.offReason(index))
            width: settingsList.width
            heading: row.type === "section"
            label: row.label || ""
            value: monitor ? (settingsRoot.editIndex, settingsRoot.editValue, settingsRoot.shownValue(row, index))
                 : row.type === "list_single" || row.type === "display_output" || row.type === "file" || row.type === "submenu"
                   || row.type === "module"
                 ? (row.value || "")
                 : slider && row.on !== undefined ? (row.on ? "On" : "Off") : ""
            editing: settingsRoot.editIndex === index
            opens: off === "" && settingsList.opens(row)
            changes: off === "" && row.type === "list_single"
            dimmed: off !== ""
            // In Camcorder's icons the box is on the icon.
            selected: settingsList.currentIndex === index && !settingsRoot.inIcons()
            slider: row.type === "slider"
            barShown: row.on !== false && off === ""
            barValue: row.value / 100
            startText: row.startText || ""
            endText: row.endText || ""
        }
    }

    // Camcorder's icons, the first screen's pages, at the left of the rows,
    // and Studio Monitor's in its strip: the layout's frame, which the shell
    // draws (Main.qml's setFrame()) and keeps beside the pages opened from
    // here.
    readonly property bool inFrame: true
    readonly property bool ownsFrame: true
    readonly property bool frameKeys: inIcons()
    // The icons as readAll() listed them, the one list handed each time: a
    // move changes which is boxed, not the icons.
    function handFrame() {
        if (iconMenu && camPages.length > 0)
            root.setFrame(pageIconList, camIndex, frameKeys, false)
    }
    onCamPagesChanged: handFrame()
    onCamIndexChanged: handFrame()
    onFrameKeysChanged: handFrame()
    onIconMenuChanged: handFrame()

    // --- HELP TEXT --- (shown when a focused row has a description), on one
    // line that scrolls when it is too long for the box.
    // A dimmed row's says first why it can't be used now.
    // In Camcorder's icons, the page's.
    // Read again as a row changes in place (revision), its value with it.
    readonly property var helpRow: (revision, inIcons() ? (camPages[camIndex] || {}).row : settingsItems[settingsList.currentIndex])
    readonly property string helpOff: inIcons() ? "" : (revision, offReason(settingsList.currentIndex))
    // "[ENTER]" in a row's description is select's name on the controller in
    // use, as the hint bar has it.
    readonly property string helpText: (helpOff !== "" ? helpOff + "\n" : "")
                                       + ((helpRow && helpRow.description) || "").split("[ENTER]").join(root.hints.select)
    // The value on the row, as it shows it (the one being changed, in a
    // value's second step): the help line has the row's first part and that
    // value's [ ] alone (HelpLine's value). A page's row in the icons has
    // none: its line whole.
    function helpValueOf(row, index) {
        if (!row || inIcons())
            return ""
        if (row.type === "slider")
            return index === editIndex ? shownValue(row, index) : row.on === undefined ? "" : row.on ? "On" : "Off"
        var v = index === editIndex ? editValue : row.value
        return v === undefined || v === null ? "" : String(v)
    }
    readonly property string helpValue: (revision, editIndex, editValue,
                                         helpValueOf(helpRow, settingsList.currentIndex))
    HelpLine {
        objectName: "helpLine"
        visible: text !== ""
        text: settingsRoot.helpText
        value: settingsRoot.helpValue
    }

    HintBar {
        id: footer
        guide: settingsRoot.monitor ? settingsRoot.monitorGuide : []
        text: root.hintLine(["back:BACK", "navigate:NAVIGATE", settingsRoot.inIcons() ? "browse:OPEN" : "change:CHANGE",
                             "select:SELECT"])
    }

    // The keys back from a question or a notice that closed: to another
    // still open, the topmost first, else to the rows. A notice opens by
    // itself (At Next Start's answer, a time zone or keyboard refused), so
    // over a question that may still be open, and is drawn over it (z).
    function keysBack() {
        if (outputPrompts.takeKeys())
            return
        var open = [systemNotice, logsNotice, quitPrompt, pastedNotice, videoArgsKeyboard, systemPicker]
                   .filter(function(overlay) { return overlay.visible })
        var front = open.length > 0 ? open[0] : settingsList
        front.forceActiveFocus()
    }

    DisplayOutputPrompts {
        id: outputPrompts
        anchors.fill: parent
        z: 1
        onClosed: settingsRoot.keysBack()
    }

    // Window Mode switched from the keyboard (F11) while the page is open:
    // its row made again, where the page didn't make the change itself.
    Connections {
        target: appCore
        ignoreUnknownSignals: true
        function onAppSettingChanged(key, value) {
            // Not this page's own save: the settings it read are behind,
            // and are read again before its rows are next made (showRows()).
            if (!settingsRoot.saving)
                settingsRoot.stale = true
            if (key !== "window_mode" || !settingsRoot.desktopWindow)
                return
            var shown = settingsRoot.settingsItems.filter(function(r) { return r.key === "window_mode" })[0]
            if (shown && shown.value !== (value === "On" ? "On" : "Off"))
                settingsRoot.buildModel(settingsList.currentIndex)
        }
        // A slider's steps couldn't be written (a full card): its row made
        // again from what is saved, the cursor where it is.
        function onSettingsNotSaved() {
            settingsRoot.buildModel(settingsList.currentIndex)
        }
    }

    // At Next Start's answer: the row as it is now (why it didn't happen is
    // the prompts' to say).
    Connections {
        target: displayOutput
        function onChanged() {
            if (displayOutput.available)
                settingsRoot.buildModel(settingsList.currentIndex)
        }
    }

    // TIME ZONE and KEYBOARD: the list to pick from, the one in force under
    // the cursor; ◄ ► from region to region, or letter to letter.
    // The system's time zone and keyboard (SystemSettings), where the app
    // has it.
    readonly property var system: typeof systemSettings !== "undefined" && systemSettings ? systemSettings : null
    property string systemPicking: ""
    function openSystemPicker(key) {
        if (!system || !system.available || system.busy)
            return
        systemPicking = key
        if (key === "time_zone") {
            systemPicker.title = "Time Zone"
            systemPicker.message = "The time zone the clock goes by"
            systemPicker.groupName = "REGION"
            systemPicker.entries = system.timeZones.map(function(z) {
                return { label: z.name.replace(/_/g, " "), value: z.name, group: z.region }
            })
            // A card not yet given a zone is on Etc/UTC, the list's UTC.
            systemPicker.open(system.timeZone === "Etc/UTC" ? "UTC" : system.timeZone)
        } else {
            systemPicker.title = "Keyboard"
            systemPicker.message = "The keyboard's layout at Exit to Terminal"
            systemPicker.groupName = "LETTER"
            systemPicker.entries = system.keyboards.map(function(k) {
                return { label: k.name, value: k.code, group: k.name.charAt(0).toUpperCase() }
            })
            systemPicker.open(system.keyboard)
        }
    }
    ListPicker {
        id: systemPicker
        objectName: "systemPicker"
        onPicked: function(value) {
            if (!settingsRoot.system)
                return
            if (settingsRoot.systemPicking === "time_zone")
                settingsRoot.system.setTimeZone(value)
            else
                settingsRoot.system.setKeyboard(value)
        }
        onClosed: settingsRoot.keysBack()
    }
    // The rows follow the system as it is read again, and as one is on its
    // way (Setting); one that couldn't be set says why.
    Connections {
        target: settingsRoot.system
        ignoreUnknownSignals: true
        function onChanged() { settingsRoot.followSystem() }
        function onBusyChanged() { settingsRoot.followSystem() }
        function onMessageChanged() {
            if (settingsRoot.system.message.indexOf(" not set: ") > 0)
                systemNotice.open()
        }
    }
    // Made again only while the two rows are on the page, the cursor kept,
    // and never under a value being changed in two steps.
    function followSystem() {
        if (editIndex < 0 && settingsItems.some(function(row) { return row.key === "time_zone" }))
            buildModel(settingsList.currentIndex)
    }
    ChoiceOverlay {
        id: systemNotice
        objectName: "systemNotice"
        anchors.fill: parent
        z: 1
        promptKind: "notice"
        readonly property var parts: (settingsRoot.system ? settingsRoot.system.message : "").split(": ")
        promptText: parts[0] || ""
        subtitleText: parts.slice(1).join(": ")
        choices: []
        onClosed: {
            settingsRoot.keysBack()
            if (settingsRoot.system)
                settingsRoot.system.clearMessage()
        }
    }

    // VIDEO ARGUMENTS: typed on the on-screen keyboard, over the page; kept
    // on OK without the spaces at its ends, nothing typed being OSD/OS's own.
    OnScreenKeyboard {
        id: videoArgsKeyboard
        objectName: "videoArgsKeyboard"
        anchors.fill: parent
        visible: false
        title: "Video Arguments"
        caseSensitive: true
        allowEmpty: true
        maxLength: 120
        onAccepted: function(text) {
            videoArgsKeyboard.close()
            settingsRoot.keysBack()
            if (appCore.save_setting("", "mpv_video_args", text.trim())) {
                settingsRoot.appSettings = Object.assign({}, settingsRoot.appSettings, { mpv_video_args: text.trim() })
                settingsRoot.buildModel(settingsList.currentIndex)
            }
        }
        onCanceled: {
            videoArgsKeyboard.close()
            settingsRoot.keysBack()
        }
    }

    ChoiceOverlay {
        id: pastedNotice
        anchors.fill: parent
        promptKind: "notice"
        promptText: settingsRoot.pasted.title || ""
        subtitleText: settingsRoot.pasted.detail || ""
        choices: []
        onClosed: settingsRoot.keysBack()
    }

    // --- QUIT CONFIRMATION OVERLAY ---
    ChoiceOverlay {
        id: quitPrompt
        anchors.fill: parent
        promptText: settingsRoot.autostartSession ? "Power off?" : "Really quit?"
        choices: quitOptions
        onActivated: function(act) {
            if (act === "quit")          Qt.quit()
            else if (act === "restart")  appCore.restartSystem()
            else if (act === "terminal") appCore.exitToTerminal()
            else if (act === "logs")     settingsRoot.saveLogs()
        }
        onClosed: settingsRoot.keysBack()
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
        onClosed: settingsRoot.keysBack()
    }
}
