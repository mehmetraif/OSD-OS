import QtQuick
import QtTest
import OSDOS.Testing
import "../../views/Components" as Components
import "../../views/Components/Hover.js" as Hover

// Settings, views/Settings.qml, against stand-ins for AppCore and the rest it
// reads: the first screen is a heading for each page (TV / Movie, Arcade,
// Look, Screen, Sound, Controls, Network, Startup, System) and Quit; each
// page has its rows, every one on every system, a row this system or another
// row's value keeps back dimmed, the help line saying why; select opens a
// heading's page, and Arcade's as the arcade modules' settings on one page
// (ModuleSettings, alsoModules). And the title bar its questions share with
// every view (AppBar): a title too long for it, as a long display output's
// name makes Switch to …?, scrolls through it.
StandInRoot {
    id: root
    // White on black in every layout, and the frame's colours in force
    // whenever it is up.
    primaryColor: schemeInk
    surfaceColor: schemePaper
    cursorColor: primaryColor
    cursorInk: surfaceColor
    monitorInk: monitorFramed
    // Settings → Look → Menu Layout, as Main.qml reads it.
    function menuLayoutOf(raw) {
        return ["Camcorder", "Studio Monitor", "Broadcast", "Flat Panel"].indexOf(raw) >= 0 ? raw : "Standard"
    }
    // What Settings reads of Main.qml's own.
    property bool devBuild: true
    property bool effectsUsable: true
    property var allSchemes: ({ "Video 1": {}, "Late Night": {} })
    property var textPresets: ({ "Rainbow": {} })
    property var backgroundPresets: ({ "Matrix": {} })
    property var selectorPresets: ["Sparkles"]
    property var screenPresets: ({ "Scanlines": {} })
    property var transitionPresets: ["Fade"]
    // The theme in force, as Main.qml reads it (AppCore::theme()).
    property var theme: ({})
    // The transitions this system can't play (Main.qml: those that are
    // shaders, without a GPU).
    property var unusableTransitions: []
    function transitionUsable(kind) { return unusableTransitions.indexOf(kind) < 0 }
    function osdBackgroundOf(raw) { return raw === "Window" || raw === "Off" ? raw : "Full" }
    function osdFrameOf(raw) { return raw === "Off" || raw === "Shadow" ? raw : "On" }
    function backgroundOn(raw) { return false }
    function solidityOf(raw) { return 100 }

    // AppCore as Settings uses it: the modules, a group's each, and the
    // app's settings.
    QtObject {
        id: appCore
        property var values: ({})
        // Modules installed besides the ones below, for a test.
        property var moreModules: []
        // How often the page asked for the settings, the modules, the themes
        // and the skins (counted in place: no binding follows them), and a
        // setting changed by another hand than the page's, as AppCore tells
        // it.
        readonly property var calls: ({ settings: 0, modules: 0, themes: 0, skins: 0 })
        signal appSettingChanged(string key, string value)
        function get_settings() {
            calls.settings++
            return { app: values, modules: {} }
        }
        // A module's own settings as saved (a module row's on or off), by
        // module: its enabled the one get_installed_modules() gives.
        property var moduleValues: ({})
        function get_installed_modules() {
            calls.modules++
            var all = moreModules.concat(
                   // In AppCore's order: by group, then each manifest's menu_order.
                   [{ id: "com.osdos.local_files", name: "Local Files", has_settings: true, settings_group: "tv", enabled: true },
                    { id: "com.osdos.plex", name: "Plex", has_settings: true, settings_group: "tv", enabled: false },
                    { id: "com.osdos.bare", name: "Bare", has_settings: false, settings_group: "tv", enabled: true },
                    { id: "com.osdos.arcade", name: "Arcade", has_settings: true, settings_group: "arcade", enabled: true },
                    { id: "com.osdos.highscores", name: "High Scores", has_settings: true, settings_group: "arcade", enabled: false },
                    { id: "com.osdos.scripts", name: "Scripts", has_settings: true, settings_group: "", enabled: false },
                    { id: "com.osdos.radio", name: "Radio", has_settings: true, settings_group: "radio", enabled: false }])
            return all.map(function(m) {
                var own = moduleValues[m.id] || {}
                return own.enabled === undefined ? m : Object.assign({}, m, { enabled: own.enabled })
            })
        }
        property bool autostart: false
        function isAutostartSession() { return autostart }
        function canRestartSystem() { return false }
        function restartSystem() {}
        function exitToTerminal() {}
        // Quit's Save Logs: offered while canLogs, saveLogs() counted, the
        // answer sent with logsSaved().
        property bool canLogs: false
        property int logSaves: 0
        signal logsSaved(bool ok, string detail)
        function canSaveLogs() { return canLogs }
        function saveLogs() { logSaves++; return true }
        function themes() { calls.themes++; return [] }
        function skins() { calls.skins++; return [] }
        function getCustomColorSchemes() { return ({}) }
        function getCustomColorScheme() { return ({}) }
        function localIpAddress() { return "" }
        // How many times config.json was written, and the settings held
        // back from it: a slider's steps (save_setting_soon()), which are
        // its value at once and written with flush_settings() or the next
        // save. settingsNotSaved() is what AppCore says when that write
        // fails.
        property int writes: 0
        property var held: []
        signal settingsNotSaved()
        function save_setting(module, key, value) {
            if (module) {
                var m = Object.assign({}, moduleValues)
                m[module] = Object.assign({}, m[module] || {})
                m[module][key] = value
                moduleValues = m
                writes++
                return true
            }
            var v = Object.assign({}, values)
            v[key] = value
            values = v
            writes++
            held = []
            return true
        }
        function save_setting_soon(module, key, value) {
            var v = Object.assign({}, values)
            v[key] = value
            values = v
            if (held.indexOf(key) < 0)
                held = held.concat([key])
            return true
        }
        function flush_settings() {
            if (held.length > 0) {
                writes++
                held = []
            }
            return true
        }
        // Several at once, and how many times that was asked.
        property int batches: 0
        function save_settings(module, some) {
            batches++
            values = Object.assign({}, values, some)
            writes++
            held = []
            return true
        }
        // Paste OSD/OS Setup: what was pasted, and a setting it sets.
        property string pasted: ""
        function applySetupText(text) {
            pasted = text
            save_setting("", "screensaver_timeout", "OFF")
            return { ok: true, title: "OSD/OS Setup pasted", detail: "OSD/OS settings: 1 set" }
        }
    }
    // Where the test copies what is pasted from.
    TextEdit {
        id: clipboardSource
        visible: false
    }
    QtObject {
        id: mpvController
        function hasSmoothPlaybackTradeoff() { return false }
        function embeddedAvailable() { return true }
    }
    // DisplayOutput as the page uses it: a Pi 4's outputs while available,
    // the one in force and the one written for the next start, and what
    // was asked of it.
    QtObject {
        id: displayOutput
        property bool available: false
        property string boardModel: "Raspberry Pi 4 Model B"
        property var options: [{ id: "hdmi", label: "HDMI", help: "A TV on HDMI" },
                               { id: "composite", label: "Composite", help: "A TV on the AV jack" },
                               { id: "jamma-240p", label: "JAMMA 240p", help: "An arcade monitor", warning: "A 15 kHz monitor only" }]
        property string current: "hdmi"
        property string currentLabel: "HDMI"
        property string next: ""
        property string nextLabel: ""
        property bool nextStartAvailable: true
        property bool powerOffAvailable: false
        property var asked: []
        signal changed()
        signal nextStartFinished(bool ok, string reason)
        function apply(id) { asked = asked.concat(["now " + id]); return true }
        function applyAtNextStart(id) { asked = asked.concat(["next " + id]); return true }
        function applyAndPowerOff(id) { asked = asked.concat(["off " + id]); return true }
    }
    QtObject {
        id: audioOutput
        property bool available: false
        // Its row as AudioOutput::settingRow() gives it, from the cards
        // there are as it is asked.
        property var row: ({ options: ["Auto"], values: [""], value: "Auto", note: "" })
        function settingRow() { return row }
    }
    QtObject {
        id: bluetoothManager
        property bool supported: false
        // As BluetoothManager has them: devicesChanged is the property's own.
        property bool powered: false
        property var devices: []
        signal adapterChanged()
    }
    // The boot screen there is on the OSD/OS image (BootProgress).
    QtObject {
        id: bootProgress
        property bool available: true
    }
    // The screen saver's tracker (IdleTracker), as the pages that hold the
    // saver off use it.
    QtObject {
        id: idleTracker
        property bool viewActive: false
        property int resets: 0
        function resetActivity() { resets++ }
    }
    // Export to OSD/OS Setup's codes (SetupExporter): one, made at once.
    QtObject {
        id: setupExport
        property var links: ["https://osdos.tv/setup/#1"]
        property bool busy: false
        property var missing: []
        property var carried: []
        function make(version) {}
        function clear() {}
    }
    // SystemSettings as the page uses it: what the system has, the lists to
    // pick from, and what was asked of it.
    QtObject {
        id: systemSettings
        property bool available: false
        property string timeZone: "Europe/Istanbul"
        property string keyboard: "tr"
        property string keyboardName: "Turkish (Q)"
        readonly property var timeZones: [{ name: "America/New_York", region: "America" },
                                          { name: "Asia/Tokyo", region: "Asia" },
                                          { name: "Europe/Berlin", region: "Europe" },
                                          { name: "Europe/Istanbul", region: "Europe" },
                                          { name: "UTC", region: "UTC" }]
        readonly property var keyboards: [{ name: "English (US)", code: "us" }, { name: "German", code: "de" },
                                          { name: "Turkish (Q)", code: "tr" }]
        property bool busy: false
        property string message: ""
        property var asked: []
        signal changed()
        function setTimeZone(zone) { asked = asked.concat(["time-zone " + zone]); return true }
        function setKeyboard(code) { asked = asked.concat(["keyboard " + code]); return true }
        function clearMessage() { message = "" }
    }
    // SystemInfo as System's row reads it: the device's name.
    QtObject {
        id: systemInfo
        property string deviceName: "den-tv"
        property int refreshes: 0
        signal changed()
        function refresh() { refreshes++ }
    }
    QtObject {
        id: wifiManager
        property bool supported: false
        // Not supported for the launcher alone (WifiManager::launcherTooOld).
        property bool launcherTooOld: false
        // As WifiManager has them: stateChanged is the property's own.
        property bool enabled: true
        property string state: ""
        property var current: ({})
    }

    // Where the page asked to go, and how often back out of it.
    property var navigations: []
    property int backs: 0

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }
    Connections {
        target: loader.item
        ignoreUnknownSignals: true
        function onNavigateTo(path, params, listState) {
            root.navigations = root.navigations.concat([{ path: path, params: params, listState: listState }])
        }
        function onGoBack() {
            root.backs++
        }
    }

    TestCase {
        name: "Settings"
        when: windowShown

        function init() {
            appCore.values = {}
            appCore.moreModules = []
            appCore.moduleValues = {}
            appCore.pasted = ""
            appCore.canLogs = false
            appCore.autostart = false
            appCore.logSaves = 0
            appCore.writes = 0
            appCore.held = []
            appCore.calls.settings = appCore.calls.modules = appCore.calls.themes = appCore.calls.skins = 0
            root.navigations = []
            root.backs = 0
            root.menuLayout = "Standard"
            root.setFrame([], 0, false)
            wifiManager.supported = false
            wifiManager.launcherTooOld = false
            systemSettings.available = false
            systemSettings.timeZone = "Europe/Istanbul"
            systemSettings.keyboard = "tr"
            systemSettings.keyboardName = "Turkish (Q)"
            systemSettings.busy = false
            systemSettings.message = ""
            systemSettings.asked = []
            bootProgress.available = true
            displayOutput.available = false
            displayOutput.next = ""
            displayOutput.nextLabel = ""
            displayOutput.powerOffAvailable = false
            displayOutput.asked = []
            root.transitionPresets = ["Fade"]
            root.theme = {}
            root.unusableTransitions = []
            audioOutput.available = false
            audioOutput.row = { options: ["Auto"], values: [""], value: "Auto", note: "" }
            wifiManager.state = ""
            wifiManager.current = {}
            bluetoothManager.supported = false
            bluetoothManager.powered = false
            bluetoothManager.devices = []
        }
        function cleanup() {
            loader.source = ""
        }

        function open(params) {
            loader.setSource(Qt.resolvedUrl("../../views/Settings.qml"), { navParams: params || {} })
            verify(loader.item !== null, "loaded Settings.qml")
            loader.item.forceActiveFocus()
            return loader.item
        }
        // The page's lines as their labels, a heading's marked "# ".
        function lines(page) {
            return page.settingsItems.map(function(row) {
                return (row.type === "section" ? "# " : "") + (row.label || "")
            })
        }
        function row(page, label) {
            return page.settingsItems.filter(function(r) { return r.label === label })[0]
        }
        // The label of the row the cursor is on.
        function cursor(page) {
            for (var i = 0; i < page.children.length; i++) {
                var list = page.children[i]
                if (list.currentIndex !== undefined && list.model !== undefined)
                    return page.settingsItems[list.currentIndex].label
            }
            return ""
        }
        // The MenuRow drawn for the line at index: the list's delegate, or
        // the one in it (a SettingRow's, which has a slider's bar under it).
        function menuRow(item, index) {
            if (item.itemAtIndex !== undefined) {
                var d = item.itemAtIndex(index)
                if (!d)
                    return null
                if (d.keepValue !== undefined)
                    return d
                for (var j = 0; j < d.children.length; j++) {
                    if (d.children[j].keepValue !== undefined)
                        return d.children[j]
                }
                return null
            }
            for (var i = 0; i < item.children.length; i++) {
                var found = menuRow(item.children[i], index)
                if (found)
                    return found
            }
            return null
        }

        // The list in the page, and the help line under it.
        function listOf(page) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i].currentIndex !== undefined && page.children[i].model !== undefined)
                    return page.children[i]
            }
            return null
        }
        function helpOf(page) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i].objectName === "helpLine")
                    return page.children[i]
            }
            return null
        }
        // Down until the cursor is on the row of this label.
        function moveTo(page, label) {
            for (var i = 0; i < page.settingsItems.length && cursor(page) !== label; i++)
                keyClick(Qt.Key_Down)
            compare(cursor(page), label)
        }

        // The first screen: a heading for each page, every row under one of
        // them, then Quit. Arcade's is the arcade modules' page; the modules
        // of no group (with settings; a group with no page counts as none)
        // are on System's.
        function test_firstScreen() {
            var page = open()
            compare(lines(page), ["TV / Movie", "Arcade", "Look", "Screen", "Sound", "Controls", "Network",
                                  "Startup", "System", "Quit"])
            compare(row(page, "Arcade").moduleId, "com.osdos.arcade")
            compare(row(page, "Arcade").alsoModules, ["com.osdos.highscores"])
            compare(cursor(page), "TV / Movie")
        }

        // Each heading's page: its rows, at most a screenful, every one of
        // them there on every system, those it can't use dimmed.
        function test_pages_data() {
            return [
                { tag: "look", lines: ["Theme", "Color Scheme", "Skin", "Effects", "Menu Layout",
                                       "OSD Background", "OSD Frame", "Loading Effect", "Loading Colors", "Boot Colors",
                                       "Hint Bar", "Help Line",
                                       "Status Icons"] },
                { tag: "effects", lines: ["Text Effect", "Background Effect", "Selector Effect", "Screen Effect",
                                          "Transition", "Orb Fog"] },
                { tag: "screen", lines: ["Display Output", "Use Display", "Rotation", "Test Patterns", "Screen Saver",
                                         "Mouse Pointer", "Window Mode"] },
                { tag: "sound", lines: ["Audio Output", "Menu Music", "Music File", "Music Volume"] },
                { tag: "network", lines: ["Wi-Fi", "Bluetooth"] },
                { tag: "startup", lines: ["Start On", "Play at Startup", "Play From", "Cabinet Lock"] },
                { tag: "system", lines: ["Update", "System Information", "Time Zone", "Keyboard Layout", "Export to OSD/OS Setup",
                                         "Paste OSD/OS Setup", "About", "# Modules", "Scripts", "Radio"] }
            ]
        }
        function test_pages(data) {
            var page = open({ group: data.tag })
            compare(lines(page), data.lines)
            // A screenful, but Look's, whose loading rows are together.
            verify(page.settingsItems.length <= (data.tag === "look" ? 14 : 12), "a screenful")
        }

        // A row that goes with another's value is dimmed rather than hidden:
        // the cursor stops on it, ◄ ► and select leave it as it is, and the
        // help line says why. Its row set so, it is a row like the others.
        function test_dimmedRow() {
            var page = open({ group: "look" })
            moveTo(page, "OSD Frame")
            var at = lines(page).indexOf("OSD Frame")
            compare(page.offReason(at), "Only with OSD Background on Box")
            var frame = menuRow(page, at)
            verify(frame.dimmed)
            verify(!frame.changes)
            var help = helpOf(page)
            verify(help.visible)
            verify(help.text.indexOf("Only with OSD Background on Box") === 0, help.text)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Return)
            compare(appCore.values.osd_frame, undefined)
            compare(row(page, "OSD Frame").value, "On")
            // OSD Background on Window: Window Frame's own.
            keyClick(Qt.Key_Up)
            compare(cursor(page), "OSD Background")
            keyClick(Qt.Key_Left)
            compare(appCore.values.osd_background, "Window")
            compare(page.offReason(at), "")
            verify(!menuRow(page, at).dimmed)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(appCore.values.osd_frame, "Off")
        }

        // The help line: the row's first part, then only the [ ] of the value
        // on the row, following ◄ ► (Menu Layout's five took over a minute to
        // scroll past); a key's bracket ([ENTER]) stays; a dimmed row's reason
        // first; a row with no value, or no [ ], whole.
        function test_helpLineFollowsTheValue() {
            var page = open({ group: "look" })
            moveTo(page, "Menu Layout")
            var help = helpOf(page)
            compare(help.value, "Standard")
            compare(help.shownText, "How Settings and the main menu are laid out\n[STANDARD] A list, a page at a time")
            keyClick(Qt.Key_Right)
            compare(help.value, "Camcorder")
            verify(help.shownText.indexOf("[CAMCORDER] As a camcorder's menu") > 0, help.shownText)
            verify(help.shownText.indexOf("[STANDARD]") < 0, help.shownText)
            verify(help.shownText.indexOf("[FLAT PANEL]") < 0, help.shownText)
            // A value with none of its own: the first part alone.
            moveTo(page, "Theme")
            compare(help.value, "None")
            verify(help.shownText.indexOf("[NONE] OSD/OS's own") > 0, help.shownText)
            // A dimmed row: why, first.
            moveTo(page, "OSD Frame")
            compare(help.shownText, "Only with OSD Background on Box\nThe frame of the OSD Background's box\n[ON] A line in the color scheme's color")
            loader.source = ""
            // A key's bracket stays with the value's.
            page = open({ group: "tv" })
            moveTo(page, "Transparent Background")
            help = helpOf(page)
            compare(help.value, "Off")
            verify(help.shownText.indexOf("[ENTER] On or off") > 0, help.shownText)
            verify(help.shownText.indexOf("[SOLID]") < 0, help.shownText)
            // A value with a note: its name before the note.
            compare(help.cut("Head\n[THEME] The theme's  [OFF] None", "Theme (Glow)"), "Head\n[THEME] The theme's")
            compare(help.cut("Head\n[AUTO] Any  [UNPLUGGED] Away", "USB Card (Unplugged)"), "Head\n[UNPLUGGED] Away")
            // No value, or no [ ]: whole.
            compare(help.cut("Head\n[A] One  [B] Two", ""), "Head\n[A] One  [B] Two")
            compare(help.cut("Head, no brackets", "On"), "Head, no brackets")
            compare(help.cut("Head\n[A] One  [B] Two", "C"), "Head")
            loader.source = ""
            // In a value's second step, the value being changed.
            root.menuLayout = "Broadcast"
            appCore.values = { menu_layout: "Broadcast" }
            page = open({ group: "look" })
            moveTo(page, "Menu Layout")
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Up)
            compare(page.helpValue, page.editValue)
            verify(page.editValue !== "Broadcast")
            verify(helpOf(page).shownText.indexOf("[" + String(page.editValue).toUpperCase() + "]") > 0)
            verify(root.chrome.helpText.indexOf("[BROADCAST]") < 0, root.chrome.helpText)
            keyClick(Qt.Key_Escape)
            compare(page.helpValue, "Broadcast")
        }

        // The hint bar says what the keys do on the row under the cursor:
        // OPEN on a row that opens, CHANGE on a list, ON/OFF for a slider
        // that can be off, a module's ON/OFF and SETTINGS, PASTE, and on a
        // dimmed row back alone, never [◄►]:CHANGE [ENTER]:SELECT on every
        // row.
        function hintOf(page) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i].frameRole === "hints")
                    return page.children[i].text
            }
            return ""
        }
        function test_hintsByRow() {
            var page = open()
            compare(hintOf(page), "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:OPEN")
            loader.source = ""
            page = open({ group: "tv" })
            compare(cursor(page), "Scaling")
            compare(hintOf(page), "[ESC]:BACK [▲▼]:NAVIGATE [◄►]:CHANGE")
            moveTo(page, "Transparent Background")
            compare(hintOf(page), "[ESC]:BACK [▲▼]:NAVIGATE [◄►]:CHANGE [ENTER]:ON/OFF")
            moveTo(page, "Info Screen")
            compare(hintOf(page), "[ESC]:BACK [▲▼]:NAVIGATE")
            moveTo(page, "Plex")
            compare(hintOf(page), "[ESC]:BACK [▲▼]:NAVIGATE [◄►]:ON/OFF [ENTER]:SETTINGS")
            moveTo(page, "Video Arguments")
            compare(hintOf(page), "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:OPEN")
            loader.source = ""
            page = open({ group: "system" })
            moveTo(page, "Paste OSD/OS Setup")
            compare(hintOf(page), page.canPaste ? "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:PASTE" : "[ESC]:BACK [▲▼]:NAVIGATE")
            loader.source = ""
            // Studio Monitor's guide the same way.
            root.menuLayout = "Studio Monitor"
            page = open({ group: "tv" })
            moveTo(page, "Info Screen")
            compare(page.monitorGuide, ["[▲▼]:MENU/ITEM", "[ESC]:BACK"])
            moveTo(page, "Plex")
            compare(page.monitorGuide, ["[▲▼]:MENU/ITEM", "[◄►]:ON/OFF", "[ENTER]:SETTINGS", "[ESC]:BACK"])
        }

        // A page opens on its first row that can be used, not a dimmed one
        // (Screen's Display Output off a Pi); back to it, on the row it was
        // left on. The first screen's Network row is dimmed, with why, where
        // neither Wi-Fi nor Bluetooth can be used, and usable with either.
        function test_startsOnAUsableRow() {
            var page = open({ group: "screen" })
            verify(page.offReason(0) !== "")
            compare(cursor(page), "Rotation")
            loader.source = ""
            page = open({ group: "screen", navListState: { currentIndex: 0 } })
            compare(cursor(page), "Rotation")
            loader.source = ""
            page = open({ group: "screen", navListState: { currentIndex: 3 } })
            compare(cursor(page), lines(page)[3])
            loader.source = ""
            displayOutput.available = true
            page = open({ group: "screen" })
            compare(cursor(page), "Display Output")
            loader.source = ""
            page = open()
            var at = lines(page).indexOf("Network")
            compare(page.offReason(at), "Only where NetworkManager or BlueZ runs, as on the OSD/OS image")
            verify(menuRow(page, at).dimmed)
            wifiManager.launcherTooOld = true
            loader.source = ""
            page = open()
            verify(page.offReason(at).indexOf("Needs a newer launcher") === 0)
            loader.source = ""
            bluetoothManager.supported = true
            page = open()
            compare(page.offReason(at), "")
            moveTo(page, "Network")
            keyClick(Qt.Key_Return)
            compare(root.navigations[root.navigations.length - 1].params.group, "network")
        }

        // Sound: Menu Music's rows dimmed while it is Off, each saying for
        // what; Audio Output dimmed where OSD/OS can't choose a sound card.
        function test_soundPage() {
            var page = open({ group: "sound" })
            var all = lines(page)
            compare(page.offReason(all.indexOf("Audio Output")),
                    "Only on a Raspberry Pi or a Linux without a sound server: here the system's sound settings choose")
            // Opened on the first row that can be used.
            compare(cursor(page), "Menu Music")
            compare(page.offReason(all.indexOf("Menu Music")), "")
            compare(page.offReason(all.indexOf("Music File")), "Only with Menu Music on File")
            compare(page.offReason(all.indexOf("Music Volume")), "Off while Menu Music is Off")
            moveTo(page, "Menu Music")
            keyClick(Qt.Key_Right)
            compare(appCore.values.menu_music, "File")
            compare(page.offReason(all.indexOf("Music File")), "")
            compare(page.offReason(all.indexOf("Music Volume")), "")
        }

        // A slider's steps (Music Volume) are its value at once, on the row
        // and in the settings, but config.json is not written at each: the
        // steps are held (save_setting_soon()) and written once, as the page
        // is left. Another row saved meanwhile is written at once, as ever.
        function test_sliderWrittenOnce() {
            appCore.values = { menu_music: "File", menu_music_volume: 60 }
            var page = open({ group: "sound" })
            moveTo(page, "Music Volume")
            for (var volume = 50; volume >= 20; volume -= 10) {
                keyClick(Qt.Key_Left)
                compare(appCore.values.menu_music_volume, volume)
                compare(row(page, "Music Volume").value, volume)
            }
            keyClick(Qt.Key_Right)
            compare(appCore.values.menu_music_volume, 30)
            compare(appCore.writes, 0)
            compare(appCore.held, ["menu_music_volume"])
            // Leaving the page (back, or a page opened from it) writes it.
            loader.source = ""
            compare(appCore.writes, 1)
            compare(appCore.held, [])
            compare(appCore.values.menu_music_volume, 30)

            // A page left with nothing held writes nothing.
            page = open({ group: "sound" })
            moveTo(page, "Menu Music")
            keyClick(Qt.Key_Left)
            compare(appCore.writes, 2)
            loader.source = ""
            compare(appCore.writes, 2)
        }

        // Steps the card couldn't take (AppCore's settingsNotSaved, once the
        // write has failed): the row is back on what is saved, the cursor
        // still on it.
        function test_sliderNotSaved() {
            appCore.values = { menu_music: "File", menu_music_volume: 60 }
            var page = open({ group: "sound" })
            moveTo(page, "Music Volume")
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            compare(row(page, "Music Volume").value, 40)
            // As AppCore does: the settings as the file has them, then told.
            appCore.values = { menu_music: "File", menu_music_volume: 60 }
            appCore.held = []
            appCore.settingsNotSaved()
            compare(row(page, "Music Volume").value, 60)
            compare(cursor(page), "Music Volume")
            keyClick(Qt.Key_Right)
            compare(row(page, "Music Volume").value, 70)
        }

        // Window Mode, on Screen's page, can be used where the menus can be
        // a window, on a desktop, and is dimmed anywhere else: on the
        // offscreen platform the tests run on, as on the Pi's console.
        function test_windowModeRow() {
            var page = open({ group: "screen" })
            var at = lines(page).indexOf("Window Mode")
            compare(page.desktopWindow, Qt.platform.os === "windows" || Qt.platform.pluginName === "xcb"
                                        || Qt.platform.pluginName === "wayland")
            compare(page.offReason(at) === "", page.desktopWindow)
            compare(row(page, "Window Mode").options, ["Off", "On"])
            compare(row(page, "Window Mode").value, "Off")
            // Display Output: nothing to switch off a Pi.
            verify(page.offReason(lines(page).indexOf("Display Output")) !== "")
            // Where Main.qml says the menus can be a window, it can be used.
            var was = root.canWindow
            loader.source = ""
            root.canWindow = true
            page = open({ group: "screen" })
            compare(page.offReason(at), "")
            root.canWindow = was
        }

        // Paste OSD/OS Setup, on System's page before Export to OSD/OS Setup
        // and Update, used where
        // there is a clipboard to paste from (not the Pi's own console):
        // select hands the clipboard's text to AppCore, the notice it answers
        // with shows over the rows, made again as they are now, and back
        // closes it.
        // A child of the page by its objectName.
        function named(page, name) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i].objectName === name)
                    return page.children[i]
            }
            return null
        }

        // Time Zone and Keyboard, first on System's page: the system's own,
        // dimmed with why where OSD/OS can't set them; on the image, select
        // opens the list on the one in force, ◄ ► step by region, select
        // asks the system for it, the row says Setting meanwhile and follows
        // the system as it changes, and a refusal is said.
        function test_systemRows() {
            var page = open({ group: "system" })
            var tz = lines(page).indexOf("Time Zone")
            compare(lines(page).slice(tz, tz + 2), ["Time Zone", "Keyboard Layout"])
            compare(row(page, "Time Zone").value, "Europe/Istanbul")
            compare(row(page, "Keyboard Layout").value, "Turkish (Q)")
            // Off the image: dimmed, saying so and no more.
            compare(page.offReason(tz), "Only on the OSD/OS image")
            compare(page.offReason(tz + 1), "Only on the OSD/OS image")
            // Opened on its first row, Update; the cursor still stops on a
            // dimmed row, and select opens nothing.
            compare(cursor(page), "Update")
            moveTo(page, "Time Zone")
            keyClick(Qt.Key_Return)
            verify(!named(page, "systemPicker").visible)
            loader.source = ""

            systemSettings.available = true
            page = open({ group: "system" })
            compare(page.offReason(tz), "")
            moveTo(page, "Time Zone")
            var picker = named(page, "systemPicker")
            verify(picker !== null)
            verify(!picker.visible)
            keyClick(Qt.Key_Return)
            verify(picker.visible)
            compare(picker.title, "Time Zone")
            compare(picker.entries.length, 5)
            compare(picker.entries[0].label, "America/New York")
            compare(picker.entries[picker.index].value, "Europe/Istanbul")
            // ◄ ► from region to region, round.
            keyClick(Qt.Key_Right)
            compare(picker.entries[picker.index].value, "UTC")
            keyClick(Qt.Key_Right)
            compare(picker.entries[picker.index].value, "America/New_York")
            keyClick(Qt.Key_Left)
            compare(picker.entries[picker.index].value, "UTC")
            keyClick(Qt.Key_Left)
            compare(picker.entries[picker.index].value, "Europe/Berlin")
            keyClick(Qt.Key_Up)
            compare(picker.entries[picker.index].value, "Asia/Tokyo")
            // Back leaves it as it was.
            keyClick(Qt.Key_Escape)
            verify(!picker.visible)
            compare(systemSettings.asked, [])
            compare(cursor(page), "Time Zone")
            // Select asks for the one under the cursor.
            keyClick(Qt.Key_Right)
            verify(picker.visible)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            verify(!picker.visible)
            compare(systemSettings.asked, ["time-zone Europe/Berlin"])
            // On its way, then as the system has it.
            systemSettings.busy = true
            compare(row(page, "Time Zone").value, "Setting")
            keyClick(Qt.Key_Return)
            verify(!picker.visible)
            systemSettings.timeZone = "Europe/Berlin"
            systemSettings.busy = false
            systemSettings.changed()
            compare(row(page, "Time Zone").value, "Europe/Berlin")
            compare(cursor(page), "Time Zone")

            // The keyboard's layouts, by letter.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(picker.visible)
            compare(picker.title, "Keyboard Layout")
            compare(picker.entries[picker.index].value, "tr")
            keyClick(Qt.Key_Right)
            compare(picker.entries[picker.index].value, "us")
            keyClick(Qt.Key_Return)
            compare(systemSettings.asked, ["time-zone Europe/Berlin", "keyboard us"])
            // Refused: said, and gone with its message once read.
            var notice = named(page, "systemNotice")
            verify(!notice.visible)
            systemSettings.message = "Keyboard set"
            verify(!notice.visible)
            systemSettings.message = "Keyboard not set: raspi-config couldn't set it"
            verify(notice.visible)
            compare(notice.promptText, "Keyboard not set")
            compare(notice.subtitleText, "raspi-config couldn't set it")
            keyClick(Qt.Key_Return)
            verify(!notice.visible)
            compare(systemSettings.message, "")
        }

        // Video Arguments, on TV / Movie's page: Default until something is
        // typed on the on-screen keyboard, kept without the spaces at its
        // ends, and Default again once nothing is.
        function test_videoArguments() {
            var page = open({ group: "tv" })
            compare(lines(page).indexOf("Video Arguments"), lines(page).length - 1)
            compare(row(page, "Video Arguments").value, "Default")
            moveTo(page, "Video Arguments")
            var keyboard = named(page, "videoArgsKeyboard")
            verify(keyboard !== null)
            keyClick(Qt.Key_Return)
            verify(keyboard.visible)
            compare(keyboard.text, "")
            keyboard.accepted("  --vo=gpu --hwdec=no ")
            verify(!keyboard.visible)
            compare(appCore.values.mpv_video_args, "--vo=gpu --hwdec=no")
            compare(row(page, "Video Arguments").value, "--vo=gpu --hwdec=no")
            compare(cursor(page), "Video Arguments")
            // Opened again on what is saved; back keeps it.
            keyClick(Qt.Key_Right)
            verify(keyboard.visible)
            compare(keyboard.text, "--vo=gpu --hwdec=no")
            keyboard.canceled()
            compare(appCore.values.mpv_video_args, "--vo=gpu --hwdec=no")
            keyClick(Qt.Key_Return)
            keyboard.accepted("")
            compare(appCore.values.mpv_video_args, "")
            compare(row(page, "Video Arguments").value, "Default")
        }

        // Display, on Screen's page: the displays there are, by number, name
        // and size, and nothing to choose with one.
        function test_displayRow() {
            var page = open({ group: "screen" })
            var display = row(page, "Use Display")
            compare(display.options.length, Qt.application.screens.length)
            verify(display.value.indexOf("1: ") === 0, display.value)
            compare(page.offReason(lines(page).indexOf("Use Display")) === "", Qt.application.screens.length > 1)
        }

        function test_pasteSetup() {
            var page = open({ group: "system" })
            var all = lines(page)
            compare(page.canPaste, Qt.platform.pluginName !== "eglfs" && Qt.platform.pluginName !== "linuxfb")
            compare(all.indexOf("Export to OSD/OS Setup") + 1, all.indexOf("Paste OSD/OS Setup"))
            compare(all.indexOf("Paste OSD/OS Setup") + 1, all.indexOf("About"))
            compare(page.offReason(all.indexOf("Paste OSD/OS Setup")) === "", page.canPaste)
            if (!page.canPaste)
                return
            var text = '{"osdos_setup": 1, "config": {"app": {"screensaver_timeout": "OFF"}}}'
            clipboardSource.text = text
            clipboardSource.selectAll()
            clipboardSource.copy()
            var list = listOf(page)
            list.currentIndex = all.indexOf("Paste OSD/OS Setup")
            verify(list.opens(row(page, "Paste OSD/OS Setup")), "its line ends in a ►")
            // Asked first, Cancel under the cursor: nothing set.
            keyClick(Qt.Key_Return)
            var confirm = named(page, "pasteConfirm")
            verify(confirm.visible)
            compare(confirm.promptText, "Paste these settings?")
            compare(labelsOf(confirm), ["Cancel", "Paste"])
            compare(confirm.choiceIndex, 0)
            keyClick(Qt.Key_Return)
            verify(!confirm.visible)
            compare(appCore.pasted, "")
            compare(cursor(page), "Paste OSD/OS Setup")
            // Back keeps it as it was too.
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Escape)
            verify(!confirm.visible)
            compare(appCore.pasted, "")
            // PASTE sets them.
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(appCore.pasted, text)
            compare(page.pasted.title, "OSD/OS Setup pasted")
            compare(page.appSettings.screensaver_timeout, "OFF")
            var notice = null
            for (var j = 0; j < page.children.length; j++) {
                if (page.children[j].promptText === "OSD/OS Setup pasted")
                    notice = page.children[j]
            }
            verify(notice !== null && notice.visible, "the notice is shown")
            compare(notice.subtitleText, "OSD/OS settings: 1 set")
            keyClick(Qt.Key_Escape)
            verify(!notice.visible, "back closes it")
            compare(cursor(page), "Paste OSD/OS Setup")
            // Nothing that can be pasted: said, and nothing asked.
            var others = ["", "just some text", '{"config": {}}']
            for (var k = 0; k < others.length; k++) {
                appCore.pasted = "none"
                clipboardSource.text = others[k] === "" ? " " : others[k]
                clipboardSource.selectAll()
                clipboardSource.copy()
                keyClick(Qt.Key_Return)
                verify(!confirm.visible, others[k])
                compare(page.pasted.title, "Nothing to paste")
                verify(notice.visible)
                compare(notice.promptText, "Nothing to paste")
                compare(appCore.pasted, "none")
                keyClick(Qt.Key_Escape)
                compare(cursor(page), "Paste OSD/OS Setup")
            }
        }

        // TV / Movie's page: the videos' rows, then the video modules with
        // settings; nothing of the look or the application. 1080p Playback
        // is there, dimmed, off the Pi 3's decode path.
        function test_tvPage() {
            var page = open({ group: "tv" })
            var all = lines(page)
            compare(all, ["Scaling", "Transparent Background", "Info Screen", "# Modules", "Local Files", "Plex",
                          "# Video", "Video Levels", "1080p Playback", "Channel Logo", "Logo Image", "Video Arguments"])
            verify(page.offReason(all.indexOf("1080p Playback")) !== "")
            compare(page.offReason(all.indexOf("Transparent Background")), "")
            verify(all.indexOf("Theme") < 0)
            verify(all.indexOf("Boot Colors") < 0)
            verify(all.indexOf("Quit") < 0)
            verify(all.indexOf("Bare") < 0)
            // Its rows change as on any page.
            compare(cursor(page), "Scaling")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(appCore.values.transparent_background, 40)
        }

        // A module's row (TV / Movie's, System's) says whether the module is
        // on, in AppCore's order; ◄ ► turn it on and off, saved as the
        // module's enabled, and the rows that follow the modules follow it
        // (Info Screen, Start on Module); select and ► open its settings.
        // Broadcast and Flat Panel change a value in two steps: there ►
        // opens it as select does.
        function test_moduleRows() {
            var page = open({ group: "tv" })
            var plex = row(page, "Plex")
            compare([plex.value, row(page, "Local Files").value], ["Off", "On"])
            var at = lines(page).indexOf("Plex")
            var line = menuRow(page, at)
            compare(line.value, "Off")
            verify(line.opens)
            verify(!line.changes)
            moveTo(page, "Plex")
            var help = helpOf(page)
            compare(help.shownText, "Shows Plex on the main menu\n[ENTER] Opens its settings")
            keyClick(Qt.Key_Right)
            compare(appCore.moduleValues["com.osdos.plex"].enabled, true)
            compare(appCore.values.enabled, undefined)
            compare(page.appSettings.enabled, undefined)
            compare(row(page, "Plex").value, "On")
            compare(menuRow(page, at).value, "On")
            compare(cursor(page), "Plex")
            compare(root.navigations.length, 0)
            keyClick(Qt.Key_Left)
            compare(appCore.moduleValues["com.osdos.plex"].enabled, false)
            compare(row(page, "Plex").value, "Off")
            // Info Screen follows the modules turned on.
            appCore.moreModules = [{ id: "com.osdos.youtube", name: "YouTube", has_settings: true, settings_group: "tv", enabled: false }]
            page.buildModel(page.settingsItems.indexOf(row(page, "Plex")))
            verify(page.offReason(lines(page).indexOf("Info Screen")) !== "")
            moveTo(page, "YouTube")
            keyClick(Qt.Key_Right)
            compare(page.offReason(lines(page).indexOf("Info Screen")), "")
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "views/ModuleSettings.qml")
            compare(root.navigations[0].params.moduleId, "com.osdos.youtube")
            loader.source = ""
            // System's too; the order is AppCore's.
            appCore.moreModules = [{ id: "com.osdos.zed", name: "Zed", has_settings: true, settings_group: "", enabled: true }]
            page = open({ group: "system" })
            var all = lines(page)
            compare(all.slice(all.indexOf("# Modules") + 1), ["Zed", "Scripts", "Radio"])
            compare(row(page, "Zed").value, "On")
            loader.source = ""
            // Two steps: ► opens it, ◄ changes nothing.
            root.menuLayout = "Broadcast"
            page = open({ group: "tv" })
            moveTo(page, "Plex")
            keyClick(Qt.Key_Left)
            compare(appCore.moduleValues["com.osdos.plex"].enabled, false)
            keyClick(Qt.Key_Right)
            compare(root.navigations[root.navigations.length - 1].params.moduleId, "com.osdos.plex")
        }

        // Select on a heading opens its page; on Arcade, the arcade modules'
        // settings, the first module's page with the others' rows on it;
        // Look's Effects, the effects' page.
        function test_groups() {
            open()
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "views/Settings.qml")
            compare(root.navigations[0].params.group, "tv")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 2)
            compare(root.navigations[1].path, "views/ModuleSettings.qml")
            compare(root.navigations[1].params.moduleId, "com.osdos.arcade")
            compare(root.navigations[1].params.alsoModules, ["com.osdos.highscores"])
            var pages = ["look", "screen", "sound"]
            for (var i = 0; i < pages.length; i++) {
                keyClick(Qt.Key_Down)
                keyClick(Qt.Key_Return)
                compare(root.navigations[root.navigations.length - 1].params.group, pages[i])
            }
            keyClick(Qt.Key_Down)
            compare(cursor(loader.item), "Controls")
            keyClick(Qt.Key_Return)
            compare(root.navigations[root.navigations.length - 1].path, "views/Controllers.qml")
            loader.source = ""
            var look = open({ group: "look" })
            moveTo(look, "Effects")
            keyClick(Qt.Key_Return)
            compare(root.navigations[root.navigations.length - 1].params.group, "effects")
        }

        // A line that leads on (a heading's, a module's, Quit) ends in a ►,
        // and ► opens it as select does; a list's choice stands between the
        // ◄ and ► that change it, and ► changes it, opening nothing; a
        // heading has neither.
        function test_marks() {
            var page = open()
            var tv = menuRow(page, 0)
            verify(tv !== null)
            verify(tv.opens)
            verify(!tv.changes)
            verify(menuRow(page, 2).opens)          // Look
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "views/Settings.qml")
            compare(root.navigations[0].params.group, "tv")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 2)
            compare(root.navigations[1].path, "views/ModuleSettings.qml")
            loader.source = ""
            page = open({ group: "look" })
            var theme = menuRow(page, 0)
            verify(theme.changes)
            verify(!theme.opens)
            keyClick(Qt.Key_Down)
            compare(cursor(page), "Color Scheme")
            verify(menuRow(page, 1).changes)
            var before = row(page, "Color Scheme").value
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 2)
            verify(row(page, "Color Scheme").value !== before)
            // System's Modules heading has neither.
            loader.source = ""
            page = open({ group: "system" })
            var heading = menuRow(page, lines(page).indexOf("# Modules"))
            verify(!heading.opens)
            verify(!heading.changes)
            // TV / Movie's page: a slider's line has no ◄ ► of its own (its
            // bar has them), a list's does.
            loader.source = ""
            page = open({ group: "tv" })
            verify(menuRow(page, 0).changes)        // Scaling
            verify(!menuRow(page, 1).changes)       // Transparent Background
            verify(!menuRow(page, 1).opens)
        }

        // The list of answers the question shown has (PromptScreen's), or null.
        function answers(item) {
            if (!item.visible)
                return null
            if (typeof item.keep === "function" && typeof item.indexAt === "function")
                return item
            for (var i = 0; i < item.children.length; i++) {
                var found = answers(item.children[i])
                if (found)
                    return found
            }
            return null
        }

        // The mouse over a line puts the cursor on it, as Main.qml's pointer
        // asks through Hover.js; a heading is passed over, the title bar
        // moves nothing. A question's answers follow it too, the list under
        // the question left as it is.
        function test_hover() {
            var page = open()
            var list = listOf(page)
            verify(list !== null)
            // The lines are laid out again as the cursor moves (the help line
            // under them): a moment for that, as a mouse takes between rows.
            var hover = function(p) { Hover.follow(root, root.Window.activeFocusItem, p.x, p.y); waitForRendering(page) }
            var middle = function(item) { verify(item !== null); return item.mapToItem(root, item.width / 2, item.height / 2) }
            compare(cursor(page), "TV / Movie")
            hover(middle(list.itemAtIndex(3)))
            compare(cursor(page), "Screen")
            hover(middle(list.itemAtIndex(1)))
            compare(cursor(page), "Arcade")
            hover(Qt.point(root.width / 2, 10))
            compare(cursor(page), "Arcade")
            // Really quit?
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(cursor(page), "Quit")
            keyClick(Qt.Key_Return)
            var yesNo = answers(page)
            verify(yesNo !== null)
            compare(yesNo.count, 2)
            verify(yesNo.itemAtIndex(0).current)
            hover(middle(yesNo.itemAtIndex(1)))
            verify(yesNo.itemAtIndex(1).current)
            verify(!yesNo.itemAtIndex(0).current)
            hover(middle(list.itemAtIndex(list.count - 2)))   // System, under the question
            compare(cursor(page), "Quit")
            verify(yesNo.itemAtIndex(1).current)
            keyClick(Qt.Key_Escape)
            compare(root.navigations.length, 0)
            // A heading (System's Modules) is passed over.
            loader.source = ""
            page = open({ group: "system" })
            list = listOf(page)
            hover(middle(list.itemAtIndex(6)))
            compare(cursor(page), "About")
            hover(middle(list.itemAtIndex(7)))
            compare(cursor(page), "About")
            // Studio Monitor's STATUS, read alone: a line under the mouse
            // takes neither the cursor nor the keys out of the strip, which
            // ▼ still moves along; a row of the next page takes both.
            loader.source = ""
            root.menuLayout = "Studio Monitor"
            page = open()
            list = listOf(page)
            verify(page.inIcons())
            compare(chromeOf(page).title, "Status")
            var before = list.currentIndex
            hover(middle(list.itemAtIndex(2)))
            verify(page.inIcons(), "the keys stay in the strip")
            verify(chromeOf(page).iconsActive)
            compare(list.currentIndex, before)
            keyClick(Qt.Key_Down)
            compare(chromeOf(page).title, "TV / Movie")
            hover(middle(list.itemAtIndex(1)))
            verify(!page.inIcons())
            compare(cursor(page), "Transparent Background")
        }

        // On the OSD/OS image Quit offers Save Logs before No: the logs go
        // to the card's OSD-OS drive, a notice saying so while they are
        // copied, then where they are (Windows' way: Logs\<time>) or why not.
        function test_quitSavesLogs() {
            appCore.canLogs = true
            var page = open()
            var all = lines(page)
            var list = listOf(page)
            list.currentIndex = all.indexOf("Quit")
            keyClick(Qt.Key_Return)
            compare(page.quitOptions.map(function(o) { return o.label }), ["No", "Yes", "Save Logs"])
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(appCore.logSaves, 1)
            var notice = null
            for (var j = 0; j < page.children.length; j++) {
                if (page.children[j].promptText === "Saving logs…")
                    notice = page.children[j]
            }
            verify(notice !== null && notice.visible, "the notice says it is saving")
            compare(notice.subtitleText, "To the card's OSD-OS drive")
            appCore.logsSaved(true, "Logs/2026-10-31_16-05-12")
            compare(notice.promptText, "Logs saved")
            compare(notice.subtitleText, "On the card's OSD-OS drive, in Logs\\2026-10-31_16-05-12")
            keyClick(Qt.Key_Escape)
            verify(!notice.visible, "back closes it")
            compare(cursor(page), "Quit")
            // A copy that failed says why.
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(appCore.logSaves, 2)
            appCore.logsSaved(false, "The card's OSD-OS partition isn't there or can't be written")
            compare(notice.promptText, "Logs not saved")
            compare(notice.subtitleText, "The card's OSD-OS partition isn't there or can't be written")
            keyClick(Qt.Key_Return)
            verify(!notice.visible, "select closes it too")
        }

        // Nothing to save logs to (not the image): Quit is No or Yes, the
        // cursor opening on No, so select twice keeps the session.
        function test_quitWithoutLogs() {
            var page = open()
            compare(page.quitOptions.map(function(o) { return o.label }), ["No", "Yes"])
            listOf(page).currentIndex = lines(page).indexOf("Quit")
            keyClick(Qt.Key_Return)
            var prompt = promptShown(page)
            compare(prompt.promptText, "Really quit?")
            compare(prompt.choiceIndex, 0)
            keyClick(Qt.Key_Return)
            compare(promptShown(page), null)
            compare(cursor(page), "Quit")
        }

        // Under autostart (the OSD/OS image) Quit is a power menu: the row
        // Power with the power symbol in Camcorder's column (Startup has the
        // play symbol), the question "Power off?", Cancel under the cursor.
        function test_powerMenu() {
            appCore.autostart = true
            appCore.canLogs = true
            var page = open()
            compare(lines(page).slice(-1), ["Power"])
            compare(page.quitOptions.map(function(o) { return o.label }),
                    ["Cancel", "Power Off", "Exit to Terminal", "Save Logs"])
            moveTo(page, "Power")
            keyClick(Qt.Key_Return)
            var prompt = promptShown(page)
            compare(prompt.promptText, "Power off?")
            compare(prompt.choiceIndex, 0)
            keyClick(Qt.Key_Return)
            compare(promptShown(page), null)
            compare(cursor(page), "Power")
            loader.source = ""
            root.menuLayout = "Camcorder"
            page = open()
            var icons = page.camPages.map(function(p) { return p.row.label + ":" + p.icon })
            verify(icons.indexOf("Power:power") >= 0, icons)
            verify(icons.indexOf("Startup:play") >= 0, icons)
            loader.source = ""
            appCore.autostart = false
            page = open()
            icons = page.camPages.map(function(p) { return p.row.label + ":" + p.icon })
            verify(icons.indexOf("Quit:eject") >= 0, icons)
        }

        // The question shown over the page (a ChoiceOverlay), or null.
        function promptShown(item) {
            if (item.promptText !== undefined && typeof item.open === "function")
                return item.visible ? item : null
            for (var i = 0; i < item.children.length; i++) {
                var found = promptShown(item.children[i])
                if (found)
                    return found
            }
            return null
        }
        function labelsOf(prompt) {
            return prompt.choices.map(function(c) { return c.label })
        }

        // Display Output on a Pi: the row says the output in force, or the
        // one written for the next start; select opens the outputs, the
        // cursor on the one in force. Another asks how to switch to it (At
        // Next Start where the launcher can, and not for the one written
        // already; Switch and Power Off where it can), a scan rate only some
        // monitors take saying so first; the one in force, with another
        // written for the next start, asks whether to stay on it; nothing
        // else. At Next Start that didn't happen says why.
        function test_displayOutputPrompts() {
            displayOutput.available = true
            var page = open({ group: "screen" })
            compare(row(page, "Display Output").value, "HDMI")
            moveTo(page, "Display Output")
            keyClick(Qt.Key_Return)
            var prompt = promptShown(page)
            verify(prompt !== null)
            compare(prompt.promptText, "Display Output")
            compare(prompt.subtitleText, "Raspberry Pi 4 Model B\nNow: HDMI")
            compare(labelsOf(prompt), ["HDMI", "Composite", "JAMMA 240p"])
            compare(prompt.choiceIndex, 0)
            // The line for the output under the cursor, in the chooser's
            // window; the row's, the one in force's.
            var outputHelp = findChild(prompt, "outputHelp") || findChild(page, "outputHelp")
            verify(outputHelp !== null)
            verify(outputHelp.visible)
            compare(outputHelp.text, "A TV on HDMI")
            keyClick(Qt.Key_Down)
            compare(outputHelp.text, "A TV on the AV jack")
            keyClick(Qt.Key_Up)
            verify(row(page, "Display Output").description.indexOf("\n[HDMI] A TV on HDMI") > 0)
            // The one in force, nothing written for the next start: nothing.
            keyClick(Qt.Key_Return)
            compare(promptShown(page), null)
            compare(cursor(page), "Display Output")
            // JAMMA: its warning, then how.
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            prompt = promptShown(page)
            compare(prompt.promptText, "Switch to JAMMA 240p?")
            compare(prompt.subtitleText, "A 15 kHz monitor only\nNow, or from the next time the Pi starts. "
                                         + "Keep it there, or in 30 seconds it comes back to HDMI")
            compare(labelsOf(prompt), ["Switch and Restart", "At Next Start", "Cancel"])
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(displayOutput.asked, ["next jamma-240p"])
            compare(promptShown(page), null)
            // Written for the next start: the row says so, and so do the
            // outputs; JAMMA again has no At Next Start, Power Off has its.
            displayOutput.next = "jamma-240p"
            displayOutput.nextLabel = "JAMMA 240p"
            displayOutput.powerOffAvailable = true
            displayOutput.changed()
            compare(row(page, "Display Output").value, "Next: JAMMA 240p")
            compare(cursor(page), "Display Output")
            keyClick(Qt.Key_Return)
            prompt = promptShown(page)
            compare(prompt.subtitleText, "Raspberry Pi 4 Model B\nNow: HDMI\nNext start: JAMMA 240p")
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(labelsOf(promptShown(page)), ["Switch and Restart", "Switch and Power Off", "Cancel"])
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(displayOutput.asked, ["next jamma-240p", "off jamma-240p"])
            // The one in force: stay on it.
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            prompt = promptShown(page)
            compare(prompt.promptText, "Stay on HDMI?")
            compare(prompt.subtitleText, "JAMMA 240p was to come at the next start")
            keyClick(Qt.Key_Return)
            compare(displayOutput.asked, ["next jamma-240p", "off jamma-240p", "next hdmi"])
            compare(promptShown(page), null)
            // Not written: why, and back to the row.
            displayOutput.nextStartFinished(false, "the card can't be written")
            prompt = promptShown(page)
            compare(prompt.promptText, "Not written for the next start")
            compare(prompt.subtitleText, "HDMI couldn't be written: the card can't be written")
            keyClick(Qt.Key_Escape)
            compare(promptShown(page), null)
            compare(cursor(page), "Display Output")
            displayOutput.nextStartFinished(true, "")
            compare(promptShown(page), null)
        }

        // A notice that opens by itself over a question still open (At Next
        // Start's answer over the outputs; a time zone refused over Paste
        // OSD/OS Setup's notice) is drawn over it, and closed, hands the
        // keys back to that question, not to the rows under both.
        function test_noticeOverAQuestion() {
            displayOutput.available = true
            var page = open({ group: "screen" })
            moveTo(page, "Display Output")
            keyClick(Qt.Key_Return)
            var outputs = promptShown(page)
            compare(outputs.promptText, "Display Output")
            displayOutput.nextStartFinished(false, "the card can't be written")
            var failed = null
            for (var i = 0; i < outputs.parent.children.length; i++) {
                if (outputs.parent.children[i].promptText === "Not written for the next start")
                    failed = outputs.parent.children[i]
            }
            verify(failed.visible && failed.activeFocus)
            keyClick(Qt.Key_Escape)
            verify(!failed.visible)
            verify(outputs.visible)
            verify(outputs.activeFocus, "the outputs have the keys again")
            keyClick(Qt.Key_Down)
            compare(outputs.choiceIndex, 1)
            keyClick(Qt.Key_Escape)
            verify(!outputs.visible)
            compare(cursor(page), "Display Output")
            loader.source = ""

            systemSettings.available = true
            page = open({ group: "system" })
            page.applyPaste()
            var pasted = promptShown(page)
            compare(pasted.promptText, "OSD/OS Setup pasted")
            var notice = named(page, "systemNotice")
            systemSettings.message = "Time zone not set: timedatectl couldn't set it"
            verify(notice.visible && notice.activeFocus)
            verify(notice.z > pasted.z, "drawn over the notice it opened on")
            keyClick(Qt.Key_Return)
            verify(!notice.visible)
            verify(pasted.visible && pasted.activeFocus, "the pasted notice has the keys again")
            keyClick(Qt.Key_Escape)
            verify(!pasted.visible)
            verify(listOf(page).activeFocus)
        }

        // Menu Layout, on Look's page: one of the five layouts, saved as it
        // changes; Standard while unset.
        function test_menuLayoutRow() {
            var page = open({ group: "look" })
            compare(row(page, "Menu Layout").options, ["Standard", "Camcorder", "Studio Monitor", "Broadcast", "Flat Panel"])
            compare(row(page, "Menu Layout").value, "Standard")
            moveTo(page, "Menu Layout")
            keyClick(Qt.Key_Right)
            compare(appCore.values.menu_layout, "Camcorder")
            compare(row(page, "Menu Layout").value, "Camcorder")
        }

        // Camcorder's column is the shell's (Main.qml's frame): the icons the
        // page handed it, by their names.
        function frameIcons() {
            return root.frameIcons.map(function(icon) { return icon.icon })
        }

        // Camcorder: the first screen's pages as icons, the keys on them,
        // the rows of the one the cursor is on beside them, none boxed; ▲ ▼
        // from page to page (round), ► or select into the rows, back out to
        // the icons, and back again to the main menu.
        function test_camcorder() {
            root.menuLayout = "Camcorder"
            var page = open()
            verify(page.camcorder)
            compare(page.camPages.map(function(p) { return p.icon }),
                    ["tv", "joystick", "sparkle", "monitor", "speaker", "gamepad", "antenna", "play", "gear", "eject"])
            compare(page.camIndex, 0)
            verify(page.inIcons())
            // Handed to the shell, which draws the column and keeps it
            // beside the pages opened from here.
            verify(page.inFrame && page.ownsFrame)
            compare(frameIcons(), ["tv", "joystick", "sparkle", "monitor", "speaker", "gamepad", "antenna", "play", "gear", "eject"])
            compare(root.frameIndex, 0)
            verify(root.frameActive)
            compare(lines(page).slice(0, 2), ["Scaling", "Transparent Background"])
            verify(!menuRow(page, 0).selected, "the box is on the icon")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(page.camIndex, 2)
            compare(root.frameIndex, 2)
            compare(lines(page)[0], "Theme")
            verify(helpOf(page).text.indexOf("The whole look") === 0, helpOf(page).text)
            keyClick(Qt.Key_Right)
            verify(!page.inIcons())
            verify(!root.frameActive)
            compare(cursor(page), "Theme")
            verify(menuRow(page, 0).selected)
            // The rows as on Look's page: Menu Layout changes, dimmed rows
            // stay as they are.
            moveTo(page, "Color Scheme")
            keyClick(Qt.Key_Right)
            verify(appCore.values.color_scheme !== undefined)
            keyClick(Qt.Key_Escape)
            verify(page.inIcons())
            compare(page.camIndex, 2)
            compare(root.backs, 0)
            // Round from the first page to Quit, a page of one row, which
            // select opens.
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(page.camIndex, 9)
            compare(lines(page), ["Quit"])
            keyClick(Qt.Key_Return)
            var yesNo = answers(page)
            verify(yesNo !== null && yesNo.count === 2)
            keyClick(Qt.Key_Escape)
            verify(page.inIcons())
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // Camcorder and Studio Monitor: a move along the icons makes the rows
        // of the page moved to, once, and nothing else. The settings, the
        // modules, the first screen's rows and the icons are as they were
        // read when the page was made: not read again for each icon passed,
        // nor the icons handed to the frame anew. The themes and skins are
        // listed once as well, as a page first needs them (Look's, STATUS's
        // Theme).
        function test_iconMovesMakeOnlyThePagesRows_data() {
            return [{ tag: "Camcorder", layout: "Camcorder", first: 0 },
                    { tag: "Studio Monitor", layout: "Studio Monitor", first: 1 }]
        }
        function test_iconMovesMakeOnlyThePagesRows(data) {
            root.menuLayout = data.layout
            var page = open()
            verify(page.inIcons())
            compare(appCore.calls.settings, 1)
            compare(appCore.calls.modules, 1)
            // Studio Monitor opens on Status, which shows the theme's name.
            compare(appCore.calls.themes, data.first)
            compare(appCore.calls.skins, 0)
            var pages = page.camPages
            var icons = root.frameIcons
            var shown = []
            // Down every page, round to the first, and back up over two.
            for (var i = 0; i < pages.length + 2; i++) {
                keyClick(i < pages.length ? Qt.Key_Down : Qt.Key_Up)
                shown.push(lines(page)[0])
            }
            compare(page.camIndex, pages.length - 2)
            compare(appCore.calls.settings, 1)
            compare(appCore.calls.modules, 1)
            verify(page.camPages === pages, "the pages as they were made")
            if (data.layout === "Camcorder")
                verify(root.frameIcons === icons, "the icons as they were handed")
            // Every page's rows as its own page has them: each page's first.
            var firstRows = ["Scaling", "Arcade", "Theme", "Display Output", "Audio Output", "Controls", "Wi-Fi",
                             "Start On", "Update", "Quit"]
            if (data.first === 1)
                firstRows.unshift("Output")
            var expected = []
            for (i = 0; i < pages.length; i++)
                expected.push(firstRows[(i + 1) % pages.length])
            expected.push(firstRows[pages.length - 1], firstRows[pages.length - 2])
            compare(shown, expected)
            // Look was passed on the way, and Status again: the themes and
            // the skins listed once, for the first of them.
            compare(appCore.calls.themes, 1)
            compare(appCore.calls.skins, 1)
        }

        // What a page's row was set to is what it shows when the icons come
        // back to the page, a slider's step too, without the settings read
        // again; a setting another hand changed meanwhile is read before the
        // next rows are made.
        function test_iconMovesKeepWhatWasSet() {
            root.menuLayout = "Camcorder"
            appCore.values = { menu_music: "File", menu_music_volume: 60 }
            var page = open()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(lines(page)[0], "Theme")
            keyClick(Qt.Key_Right)
            moveTo(page, "Hint Bar")
            compare(row(page, "Hint Bar").value, "On")
            keyClick(Qt.Key_Right)
            compare(appCore.values.hint_bar, "Off")
            keyClick(Qt.Key_Escape)
            verify(page.inIcons())
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(lines(page)[0], "Audio Output")
            keyClick(Qt.Key_Right)
            moveTo(page, "Music Volume")
            keyClick(Qt.Key_Left)
            compare(appCore.values.menu_music_volume, 50)
            keyClick(Qt.Key_Escape)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(row(page, "Hint Bar").value, "Off")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(row(page, "Music Volume").value, 50)
            compare(appCore.calls.settings, 1)

            // Another hand's: Window Mode's key, a setup file taken, a module.
            appCore.values = Object.assign({}, appCore.values, { screensaver_timeout: "60" })
            appCore.appSettingChanged("screensaver_timeout", "60")
            compare(appCore.calls.settings, 1)
            keyClick(Qt.Key_Up)
            compare(lines(page)[0], "Display Output")
            compare(row(page, "Screen Saver").value, "1 min")
            compare(appCore.calls.settings, 2)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Down)
            compare(appCore.calls.settings, 2)
        }

        // Camcorder: a page that is a view of its own (Arcade's) shows its
        // first-screen row, and ► opens it, keeping where the cursor was;
        // a page opened from the rows (Look's Effects) has the column too,
        // its heading's icon framed, the keys in its rows.
        function test_camcorderPages() {
            root.menuLayout = "Camcorder"
            var page = open()
            keyClick(Qt.Key_Down)
            compare(lines(page), ["Arcade"])
            keyClick(Qt.Key_Right)
            var went = root.navigations[root.navigations.length - 1]
            compare(went.path, "views/ModuleSettings.qml")
            compare(went.params.moduleId, "com.osdos.arcade")
            compare(went.listState.currentIndex, 1)
            compare(went.listState.camRows, false)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            moveTo(page, "Effects")
            keyClick(Qt.Key_Return)
            went = root.navigations[root.navigations.length - 1]
            compare(went.params.group, "effects")
            compare(went.listState.currentIndex, 2)
            compare(went.listState.camRows, true)
            compare(went.listState.rowIndex, lines(page).indexOf("Effects"))
            // Back there: Look's rows, the cursor on Effects.
            loader.source = ""
            page = open({ navListState: went.listState })
            compare(page.camIndex, 2)
            verify(!page.inIcons())
            compare(cursor(page), "Effects")
            // The effects' page.
            loader.source = ""
            page = open({ group: "effects" })
            compare(page.camIndex, 2)
            verify(!page.inIcons())
            compare(frameIcons().length, 10)
            compare(root.frameIndex, 2)
            verify(!root.frameActive)
            compare(cursor(page), "Text Effect")
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // Menu Layout changed while the first screen is open: laid out again
        // with the cursor where it was, on the page in Camcorder's icons, on
        // its row in Standard's list.
        function test_layoutChanged() {
            root.menuLayout = "Camcorder"
            var page = open()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            root.menuLayout = "Standard"
            tryVerify(function() { return page.settingsItems.length === 10 })
            compare(cursor(page), "Look")
            keyClick(Qt.Key_Down)
            root.menuLayout = "Camcorder"
            tryVerify(function() { return page.camIndex === 3 && page.inIcons() })
            compare(lines(page)[0], "Display Output")
        }

        // The monitor layouts' frame: the shell's, which the page's title
        // bar, help line and hint bar speak to; its title and guide.
        function chromeOf(page) {
            return root.chrome
        }

        // Studio Monitor: the pages as icons in the window's strip, STATUS
        // first (what is set, read alone: the keys stay in the strip), then
        // the first screen's pages as Camcorder has them, ◄ ► changing a
        // row's value at once.
        function test_studioMonitor() {
            root.menuLayout = "Studio Monitor"
            var page = open()
            var chrome = chromeOf(page)
            verify(chrome !== null && chrome.visible)
            compare(page.camPages.map(function(p) { return p.icon }),
                    ["status", "tv", "joystick", "sparkle", "monitor", "speaker", "gamepad", "antenna", "play", "gear", "eject"])
            compare(chrome.icons.length, 11)
            compare(chrome.title, "Status")
            compare(lines(page), ["Output", "Audio", "Wi-Fi", "Bluetooth", "Theme", "Start On", "Cabinet Lock",
                                  "Version", "IP Address"])
            compare(row(page, "Version").value, "Dev")
            compare(row(page, "Cabinet Lock").value, "Off")
            verify(page.inIcons())
            keyClick(Qt.Key_Right)
            verify(page.inIcons(), "STATUS is read from the strip")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(chrome.title, "Look")
            compare(lines(page)[0], "Theme")
            keyClick(Qt.Key_Right)
            verify(!page.inIcons())
            verify(!chrome.iconsActive)
            moveTo(page, "Color Scheme")
            keyClick(Qt.Key_Right)
            verify(appCore.values.color_scheme !== undefined)
            keyClick(Qt.Key_Escape)
            verify(page.inIcons())
            compare(root.backs, 0)
            // Arcade's page, a view of its own, opens from its icon; the way
            // back finds it after STATUS.
            keyClick(Qt.Key_Up)
            compare(lines(page), ["Arcade"])
            keyClick(Qt.Key_Return)
            var went = root.navigations[root.navigations.length - 1]
            compare(went.path, "views/ModuleSettings.qml")
            compare(went.listState.currentIndex, 1)
            loader.source = ""
            page = open({ navListState: went.listState })
            compare(page.camIndex, 2)
            compare(lines(page), ["Arcade"])
        }

        // Studio Monitor's Status follows what it shows while it is open:
        // Wi-Fi's line as a network is joined, Bluetooth's as a device
        // connects or it is turned off, the keys still in the strip. An
        // output OSD/OS doesn't offer here reads System, the system's own,
        // not None. On another page nothing is made again for them.
        function test_statusFollows() {
            root.menuLayout = "Studio Monitor"
            wifiManager.supported = true
            wifiManager.state = "disconnected"
            bluetoothManager.supported = true
            bluetoothManager.powered = true
            var page = open()
            compare(row(page, "Output").value, "System")
            compare(row(page, "Audio").value, "System")
            compare(row(page, "Wi-Fi").value, "Not Joined")
            compare(row(page, "Bluetooth").value, "On")
            wifiManager.current = { ssid: "Home" }
            wifiManager.state = "connected"
            compare(row(page, "Wi-Fi").value, "Home")
            bluetoothManager.devices = [{ connected: true }, { connected: false }]
            compare(row(page, "Bluetooth").value, "1 Connected")
            bluetoothManager.powered = false
            bluetoothManager.adapterChanged()
            compare(row(page, "Bluetooth").value, "Off")
            verify(page.inIcons())
            compare(chromeOf(page).title, "Status")
            // A sound card OSD/OS chooses reads as its row does.
            audioOutput.available = true
            page.audioFollowInterval = 50
            wifiManager.state = "disconnected"
            compare(row(page, "Wi-Fi").value, "Not Joined")
            compare(row(page, "Audio").value, "Auto")
            audioOutput.row = { options: ["Auto (USB Audio)", "USB Audio"], values: ["", { card: "Device", name: "USB Audio" }],
                                value: "Auto (USB Audio)", note: "" }
            tryVerify(function() { return row(page, "Audio").value === "Auto (USB Audio)" }, 3000)
            // TV / Movie's page: left as it is.
            keyClick(Qt.Key_Down)
            compare(lines(page)[0], "Scaling")
            var built = page.settingsItems
            wifiManager.state = "connected"
            bluetoothManager.adapterChanged()
            verify(page.settingsItems === built)
        }

        // Status's values as their own rows have them: the output in force,
        // or the one written for the next start; the module started on,
        // None for one turned off; the theme, None without one.
        function test_statusValuesAsTheirRows() {
            root.menuLayout = "Studio Monitor"
            displayOutput.available = true
            appCore.values = { startup_module: "com.osdos.local_files" }
            var page = open()
            compare(row(page, "Output").value, "HDMI")
            compare(row(page, "Start On").value, "Local Files")
            compare(row(page, "Theme").value, "None")
            loader.source = ""
            displayOutput.next = "composite"
            displayOutput.nextLabel = "Composite"
            appCore.values = { startup_module: "com.osdos.plex" }
            page = open()
            compare(row(page, "Output").value, "Next: Composite")
            compare(row(page, "Start On").value, "None")
            compare(row(page, "Cabinet Lock").value, "Off")
        }

        // Broadcast: the first screen as a list, Key Protect (Cabinet Lock)
        // at its foot; a value changed in two steps: select, ▲ ▼ through
        // the choices with nothing saved, select saves, back leaves it.
        // ◄ ► change nothing.
        function test_broadcast() {
            root.menuLayout = "Broadcast"
            var page = open()
            compare(chromeOf(page).title, "Menu")
            var all = lines(page)
            compare(all.slice(-3), ["Quit", "# ", "Key Protect"])
            compare(row(page, "Key Protect").key, "cabinet_lock")
            moveTo(page, "Key Protect")
            keyClick(Qt.Key_Return)
            compare(root.navigations[root.navigations.length - 1].path, "views/CabinetLock.qml")
            loader.source = ""
            page = open({ group: "look" })
            compare(chromeOf(page).title, "Look")
            moveTo(page, "Boot Colors")
            var before = row(page, "Boot Colors").value
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Left)
            compare(appCore.values.boot_colors, undefined)
            keyClick(Qt.Key_Return)
            verify(page.editIndex >= 0)
            compare(chromeOf(page).guide[1].indexOf("TO CONFIRM"), 0)
            keyClick(Qt.Key_Down)
            verify(page.editValue !== before)
            compare(appCore.values.boot_colors, undefined)
            keyClick(Qt.Key_Escape)
            compare(page.editIndex, -1)
            compare(row(page, "Boot Colors").value, before)
            compare(root.backs, 0)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(page.editIndex, -1)
            verify(appCore.values.boot_colors !== undefined)
            verify(row(page, "Boot Colors").value !== before)
            // A slider, Off below its first step: Transparent Background.
            loader.source = ""
            page = open({ group: "tv" })
            moveTo(page, "Transparent Background")
            compare(page.shownValue(row(page, "Transparent Background"), page.settingsItems.indexOf(row(page, "Transparent Background"))), "Off")
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(page.editValue, 10)
            keyClick(Qt.Key_Return)
            compare(appCore.values.transparent_background, 10)
            verify(row(page, "Transparent Background").on)
        }

        // Flat Panel: a page of rows at a time, "1/2" at the top right; ◄ is
        // back; a value changed in two steps as Broadcast's.
        function test_flatPanel() {
            root.menuLayout = "Flat Panel"
            var page = open({ group: "look" })
            var chrome = chromeOf(page)
            compare(chrome.title, ">Settings >Look")
            var list = listOf(page)
            var per = Math.floor(list.height / chrome.rowHeight + 0.001)
            verify(page.settingsItems.length > per, "Look's rows take two pages")
            compare(chrome.pages, Math.ceil(page.settingsItems.length / per))
            compare(chrome.page, 0)
            compare(list.contentY, list.originY)
            for (var i = 0; i < per; i++)
                keyClick(Qt.Key_Down)
            compare(chrome.page, 1)
            compare(list.contentY, list.originY + per * chrome.rowHeight)
            // The last page filled out to a whole page: its top within the
            // list's bounds, where a ListView keeps it.
            verify(list.contentY <= list.originY + list.contentHeight - list.height + 0.5,
                   list.contentY + " past " + (list.contentHeight - list.height))
            keyClick(Qt.Key_Up)
            compare(chrome.page, 0)
            moveTo(page, "Status Icons")
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(appCore.values.status_icons, "Off")
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Left)
            compare(page.editIndex, -1)
            compare(root.backs, 0)
            keyClick(Qt.Key_Left)
            compare(root.backs, 1)
            loader.source = ""
            page = open({ group: "effects" })
            compare(chromeOf(page).title, ">Settings >Look >Effects")
        }

        // Menu Layout changed to and from a monitor layout: Studio Monitor's
        // strip on the page the cursor was on (after STATUS), Broadcast's
        // list on its row.
        function test_layoutChangedMonitor() {
            var page = open()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(cursor(page), "Look")
            root.menuLayout = "Studio Monitor"
            tryVerify(function() { return page.camIndex === 3 && page.inIcons() })
            compare(lines(page)[0], "Theme")
            keyClick(Qt.Key_Down)
            root.menuLayout = "Broadcast"
            tryVerify(function() { return cursor(page) === "Screen" })
            verify(lines(page).indexOf("Key Protect") > 0)
            root.menuLayout = "Camcorder"
            tryVerify(function() { return page.camIndex === 3 && page.inIcons() })
        }

        // Audio Output's cards are read again while Sound's page stays open:
        // a card plugged in is offered and one taken out goes, or reads
        // (Unplugged) while it is the one chosen, the cursor kept where it
        // is; while nothing changes the page is left as it is, and so it is
        // under a value being changed in two steps.
        function test_audioOutputFollowsTheCards() {
            var hdmi = { card: "vc4hdmi0", name: "HDMI" }
            var usb = { card: "Device", name: "USB Audio" }
            audioOutput.available = true
            audioOutput.row = { options: ["Auto", "HDMI"], values: ["", hdmi], value: "Auto", note: "No USB sound card" }
            var page = open({ group: "sound" })
            page.audioFollowInterval = 50
            compare(row(page, "Audio Output").options, ["Auto", "HDMI"])
            verify(row(page, "Audio Output").description.indexOf("No USB sound card") > 0)
            moveTo(page, "Menu Music")
            var built = page.settingsItems
            wait(300)
            verify(page.settingsItems === built, "left as it is while the cards stay")
            audioOutput.row = { options: ["Auto", "HDMI", "USB Audio"], values: ["", hdmi, usb], value: "Auto", note: "" }
            tryVerify(function() { return row(page, "Audio Output").options.length === 3 }, 3000)
            compare(row(page, "Audio Output").options, ["Auto", "HDMI", "USB Audio"])
            compare(cursor(page), "Menu Music")
            // Chosen, then taken out.
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Left)
            compare(appCore.values.audio_output, usb)
            audioOutput.row = { options: ["Auto", "HDMI", "USB Audio (Unplugged)"], values: ["", hdmi, usb],
                                value: "USB Audio (Unplugged)", note: "No USB sound card" }
            tryVerify(function() { return row(page, "Audio Output").value === "USB Audio (Unplugged)" }, 3000)
            compare(cursor(page), "Audio Output")
            // Broadcast: not under a value being changed.
            loader.source = ""
            root.menuLayout = "Broadcast"
            page = open({ group: "sound" })
            page.audioFollowInterval = 50
            keyClick(Qt.Key_Return)
            verify(page.editIndex >= 0)
            audioOutput.row = { options: ["Auto", "HDMI"], values: ["", hdmi], value: "Auto", note: "" }
            wait(300)
            verify(page.editIndex >= 0)
            compare(row(page, "Audio Output").options.length, 3)
            keyClick(Qt.Key_Escape)
            tryVerify(function() { return row(page, "Audio Output").options.length === 2 }, 3000)
        }

        // A part's row (the eight a theme sets) whose saved value is not
        // among its choices shows what is drawn in its place, not its first
        // choice: a color scheme removed since Video 1, a skin gone from its
        // folder None, an effect's unknown name Off, a transition this
        // system can't play Fade and an unknown one Off; Menu Music's is the
        // theme's. ◄ ► go on from what is shown. A value among the choices
        // shows as itself.
        // With a theme, a row on THEME says what the theme has for it: the
        // help line opens with "Theme's: …" (its scheme's, skin's,
        // effect's name, its music's file, "its own", or none), one rule
        // for the eight; on another value it doesn't.
        function test_themeValues() {
            root.theme = { id: "glowy", name: "Glowy", colors: "Late Night", skin: { name: "Rounded" },
                           effects: { text: "Glow", screen: { scanlines: 0.5 } }, music: "/themes/glowy/tune.ogg" }
            appCore.values = { osd_theme: "glowy" }
            var page = open({ group: "look" })
            var help = helpOf(page)
            moveTo(page, "Color Scheme")
            verify(help.shownText.indexOf("Theme's: Late Night\n") === 0, help.shownText)
            keyClick(Qt.Key_Right)
            verify(help.shownText.indexOf("Theme's") < 0, help.shownText)
            moveTo(page, "Skin")
            verify(help.shownText.indexOf("Theme's: Rounded\n") === 0, help.shownText)
            loader.source = ""
            page = open({ group: "effects" })
            help = helpOf(page)
            var expected = [["Text Effect", "Glow"], ["Background Effect", "none"], ["Selector Effect", "none"],
                            ["Screen Effect", "its own"], ["Transition", "none"]]
            for (var i = 0; i < expected.length; i++) {
                moveTo(page, expected[i][0])
                verify(help.shownText.indexOf("Theme's: " + expected[i][1] + "\n") === 0, help.shownText)
            }
            loader.source = ""
            page = open({ group: "sound" })
            moveTo(page, "Menu Music")
            verify(helpOf(page).shownText.indexOf("Theme's: tune.ogg\n") === 0, helpOf(page).shownText)
            loader.source = ""
            // Without a theme there is no THEME to say.
            root.theme = {}
            appCore.values = {}
            page = open({ group: "look" })
            moveTo(page, "Color Scheme")
            verify(helpOf(page).shownText.indexOf("Theme's") < 0)
        }

        // Every row's label with its longest value fits Camcorder's line:
        // Loading Colors, Boot Colors, Start On; a scheme's and an effect's
        // long names shown shorter (SMPTE ECR, Flag), saved as they were.
        function test_labelsFitCamcorder() {
            var page = open({ group: "startup" })
            compare(lines(page)[0], "Start On")
            loader.source = ""
            var schemes = root.allSchemes
            var backgrounds = root.backgroundPresets
            root.allSchemes = { "Video 1": {}, "SMPTE ECR 1-1978": {} }
            root.backgroundPresets = { "Matrix": {}, "Checkered Flag": {} }
            appCore.values = { color_scheme: "SMPTE ECR 1-1978", background_effect: "Checkered Flag" }
            page = open({ group: "look" })
            compare(row(page, "Color Scheme").value, "SMPTE ECR")
            compare(row(page, "Color Scheme").values.indexOf("SMPTE ECR 1-1978"), row(page, "Color Scheme").options.indexOf("SMPTE ECR"))
            loader.source = ""
            page = open({ group: "effects" })
            compare(row(page, "Background Effect").value, "Flag")
            moveTo(page, "Background Effect")
            verify(helpOf(page).shownText.indexOf("[FLAG] A racing flag") > 0, helpOf(page).shownText)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Right)
            compare(appCore.values.background_effect, "Checkered Flag")
            root.allSchemes = schemes
            root.backgroundPresets = backgrounds
        }

        // Screen's Use Display and Rotation: Rotation None | Turned Right |
        // Turned Left, None shown over the saved Horizontal, saved as before.
        function test_rotationRow() {
            var page = open({ group: "screen" })
            compare(row(page, "Rotation").options, ["None", "Turned Right", "Turned Left"])
            compare(row(page, "Rotation").value, "None")
            moveTo(page, "Rotation")
            keyClick(Qt.Key_Right)
            compare(appCore.values.monitor, "Turned Right")
            keyClick(Qt.Key_Left)
            compare(appCore.values.monitor, "Horizontal")
            compare(row(page, "Rotation").value, "None")
            loader.source = ""
            appCore.values = { monitor: "Turned Left" }
            page = open({ group: "screen" })
            compare(row(page, "Rotation").value, "Turned Left")
            verify(row(page, "Use Display") !== undefined)
        }

        // System Information, System's second row: the device's name its
        // value, read again as the page opens and followed as it changes;
        // select opens its page.
        function test_systemInformationRow() {
            systemInfo.deviceName = "den-tv"
            systemInfo.refreshes = 0
            var page = open({ group: "system" })
            compare(systemInfo.refreshes, 1)
            compare(row(page, "System Information").value, "den-tv")
            moveTo(page, "System Information")
            systemInfo.deviceName = "attic-tv"
            systemInfo.changed()
            compare(row(page, "System Information").value, "attic-tv")
            compare(cursor(page), "System Information")
            keyClick(Qt.Key_Return)
            compare(root.navigations[root.navigations.length - 1].path, "views/SystemInfo.qml")
            systemInfo.deviceName = "den-tv"
        }

        // Screen Saver: Off, 1, 2, 5 or 10 min, saved in seconds; 5 min where
        // it was never saved; 30, 60 or 120 saved before read as they are,
        // shown as the nearest, and not written until another is chosen.
        function test_screenSaverRow() {
            var page = open({ group: "screen" })
            var saver = row(page, "Screen Saver")
            compare(saver.options, ["Off", "1 min", "2 min", "5 min", "10 min"])
            compare(saver.value, "5 min")
            moveTo(page, "Screen Saver")
            keyClick(Qt.Key_Right)
            compare(appCore.values.screensaver_timeout, "600")
            keyClick(Qt.Key_Right)
            compare(appCore.values.screensaver_timeout, "OFF")
            compare(row(page, "Screen Saver").value, "Off")
            loader.source = ""
            var old = [["30", "1 min"], ["60", "1 min"], ["120", "2 min"], ["OFF", "Off"]]
            for (var i = 0; i < old.length; i++) {
                appCore.values = { screensaver_timeout: old[i][0] }
                appCore.writes = 0
                page = open({ group: "screen" })
                compare(row(page, "Screen Saver").value, old[i][1], old[i][0])
                loader.source = ""
                compare(appCore.values.screensaver_timeout, old[i][0])
                compare(appCore.writes, 0)
            }
            appCore.values = { screensaver_timeout: "30" }
            page = open({ group: "screen" })
            moveTo(page, "Screen Saver")
            keyClick(Qt.Key_Right)
            compare(appCore.values.screensaver_timeout, "120")
        }

        // One spelling, the labels' (color, favorite, license, center,
        // gray), in every text Settings shows: each page's rows' help lines
        // and reasons.
        function test_spelling() {
            appCore.values = { osd_theme: "x" }
            var groups = ["", "tv", "look", "effects", "screen", "sound", "network", "startup", "system", "status"]
            var british = /colour|favourite|licence|centre|grey/i
            for (var g = 0; g < groups.length; g++) {
                var page = open(groups[g] === "" ? {} : { group: groups[g] })
                for (var i = 0; i < page.settingsItems.length; i++) {
                    var r = page.settingsItems[i]
                    var text = [r.label, r.description, page.offReason(i)].concat(r.options || []).join(" ")
                    verify(!british.test(text), groups[g] + ": " + r.label)
                }
                loader.source = ""
            }
        }

        function test_partRowsInForce() {
            root.transitionPresets = ["Fade", "Cube", "Ripple"]
            root.unusableTransitions = ["Ripple"]
            var saved = { osd_theme: "vhs", color_scheme: "My Scheme", skin: "gone", text_effect: "Sparkle",
                          background_effect: "Gone", selector_effect: "Gone", screen_effect: "Gone",
                          transition: "Ripple", menu_music: "Gone" }
            appCore.values = saved
            var page = open({ group: "look" })
            compare(row(page, "Color Scheme").options, ["Theme", "Video 1", "Late Night"])
            compare(row(page, "Color Scheme").value, "Video 1")
            compare(row(page, "Skin").options, ["Theme", "None"])
            compare(row(page, "Skin").value, "None")
            moveTo(page, "Color Scheme")
            keyClick(Qt.Key_Right)
            compare(appCore.values.color_scheme, "Late Night")
            loader.source = ""
            appCore.values = saved
            page = open({ group: "effects" })
            compare(row(page, "Text Effect").value, "Off")
            compare(row(page, "Background Effect").value, "Off")
            compare(row(page, "Selector Effect").value, "Off")
            compare(row(page, "Screen Effect").value, "Off")
            compare(row(page, "Transition").options, ["Theme", "Off", "Fade", "Cube"])
            compare(row(page, "Transition").value, "Fade")
            loader.source = ""
            appCore.values = saved
            page = open({ group: "sound" })
            compare(row(page, "Menu Music").value, "Theme")
            // A transition of no name there is: none plays.
            loader.source = ""
            appCore.values = { osd_theme: "vhs", transition: "Gone", color_scheme: "Late Night", text_effect: "" }
            page = open({ group: "effects" })
            compare(row(page, "Transition").value, "Off")
            compare(row(page, "Text Effect").value, "Theme")
            loader.source = ""
            page = open({ group: "look" })
            compare(row(page, "Color Scheme").value, "Late Night")
            // Without a theme: the same, where there is no THEME to show.
            loader.source = ""
            appCore.values = { color_scheme: "My Scheme", transition: "Ripple", selector_effect: "Gone", skin: "gone" }
            page = open({ group: "look" })
            compare(row(page, "Color Scheme").value, "Video 1")
            compare(row(page, "Skin").value, "None")
            loader.source = ""
            page = open({ group: "effects" })
            compare(row(page, "Transition").value, "Fade")
            compare(row(page, "Selector Effect").value, "Off")
        }

        // Info Screen, on TV / Movie's page, is Netflix's, Prime Video's and
        // YouTube's: with none of them turned on it is dimmed, saying whose
        // it is, and ◄ ► leave it as it is; with one on (not merely
        // installed), a row like the others.
        function test_infoScreenRow() {
            appCore.moreModules = [{ id: "com.osdos.youtube", name: "YouTube", has_settings: false,
                                     settings_group: "tv", enabled: false }]
            var page = open({ group: "tv" })
            var at = lines(page).indexOf("Info Screen")
            compare(page.offReason(at), "Only with Netflix, Prime Video or YouTube enabled")
            listOf(page).currentIndex = at
            verify(helpOf(page).text.indexOf("Only with Netflix, Prime Video or YouTube enabled") === 0, helpOf(page).text)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Left)
            compare(appCore.values.info_screen, undefined)
            var on = ["com.osdos.youtube", "com.osdos.netflix", "com.osdos.prime_video"]
            for (var i = 0; i < on.length; i++) {
                loader.source = ""
                appCore.values = {}
                appCore.moreModules = [{ id: on[i], name: "Films", has_settings: false, settings_group: "tv", enabled: true }]
                page = open({ group: "tv" })
                compare(page.offReason(at), "")
                listOf(page).currentIndex = at
                keyClick(Qt.Key_Right)
                compare(appCore.values.info_screen, "5")
            }
            // "key" shown With ►, its help line's bracket the same; ► as
            // the controller in use names it.
            loader.source = ""
            appCore.values = { info_screen: "key" }
            page = open({ group: "tv" })
            compare(row(page, "Info Screen").options[1], "With ►")
            compare(row(page, "Info Screen").value, "With ►")
            listOf(page).currentIndex = at
            verify(helpOf(page).shownText.indexOf("[With ►] Only with right on a film") > 0, helpOf(page).shownText)
            keyClick(Qt.Key_Right)
            compare(appCore.values.info_screen, "1")
            keyClick(Qt.Key_Left)
            compare(appCore.values.info_screen, "key")
        }

        // OSD Background's Window shown as Box (saved as it was), OSD Frame
        // going with it; Play From, Keyboard Layout by their new names.
        function test_renamedRows() {
            appCore.values = { osd_background: "Window" }
            var page = open({ group: "look" })
            compare(row(page, "OSD Background").options, ["Full", "Off", "Box"])
            compare(row(page, "OSD Background").value, "Box")
            compare(page.offReason(lines(page).indexOf("OSD Frame")), "")
            moveTo(page, "OSD Background")
            keyClick(Qt.Key_Left)
            compare(appCore.values.osd_background, "Off")
            keyClick(Qt.Key_Right)
            compare(appCore.values.osd_background, "Window")
            loader.source = ""
            page = open({ group: "startup" })
            verify(row(page, "Play From") !== undefined)
            loader.source = ""
            page = open({ group: "system" })
            verify(row(page, "Keyboard Layout") !== undefined)
        }

        // Startup From, on Startup's page, is for a favourite that can be
        // resumed, Local Files' or YouTube's: with none it waits for Play at
        // Startup; with an Arcade game's, or a web player's title's, it is
        // dimmed, saying whose it is for, and ◄ ► leave it as it is.
        function test_startupFromRow() {
            var page = open({ group: "startup" })
            var at = lines(page).indexOf("Play From")
            compare(page.offReason(at), "Off while Play at Startup is None")
            var favorites = [{ module: "com.osdos.local_files", usable: true },
                             { module: "com.osdos.youtube", usable: true },
                             { module: "com.osdos.arcade", usable: false },
                             { module: "com.osdos.netflix", usable: false }]
            for (var i = 0; i < favorites.length; i++) {
                loader.source = ""
                appCore.values = { startup_favorite: { module: favorites[i].module, path: "p", name: "Favourite" } }
                page = open({ group: "startup" })
                compare(row(page, "Play at Startup").value, "Favourite")
                compare(page.offReason(at), favorites[i].usable ? "" : "Only with a Local Files or YouTube favorite")
                moveTo(page, "Play From")
                compare(menuRow(page, at).dimmed, !favorites[i].usable)
                keyClick(Qt.Key_Right)
                compare(appCore.values.startup_from, favorites[i].usable ? "Beginning" : undefined)
            }
            verify(helpOf(page).text.indexOf("Only with a Local Files or YouTube favorite") === 0, helpOf(page).text)
        }

        // Boot Screen Colors, on Look's page: a row like the others on the
        // OSD/OS image, which has a boot screen; anywhere else dimmed, the
        // help line saying where it is for, ◄ ► leaving it as it is.
        function test_bootScreenColorsRow() {
            var page = open({ group: "look" })
            var at = lines(page).indexOf("Boot Colors")
            compare(page.offReason(at), "")
            compare(row(page, "Boot Colors").options, ["Menu", "Blue"])
            compare(row(page, "Boot Colors").value, "Menu")
            compare(row(page, "Loading Colors").value, "Menu")
            moveTo(page, "Boot Colors")
            keyClick(Qt.Key_Right)
            compare(appCore.values.boot_colors, "Default")
            compare(row(page, "Boot Colors").value, "Blue")
            verify(helpOf(page).shownText.indexOf("[BLUE] White on blue") > 0, helpOf(page).shownText)
            moveTo(page, "Loading Colors")
            keyClick(Qt.Key_Left)
            compare(appCore.values.loading_colors, "Default")
            keyClick(Qt.Key_Left)
            compare(appCore.values.loading_colors, "Theme")
            loader.source = ""
            // Saved as before: Default read as Blue.
            appCore.values = { loading_colors: "Default", boot_colors: "Theme" }
            page = open({ group: "look" })
            compare([row(page, "Loading Colors").value, row(page, "Boot Colors").value], ["Blue", "Menu"])
            loader.source = ""
            appCore.values = {}
            bootProgress.available = false
            page = open({ group: "look" })
            compare(page.offReason(at), "Only on the OSD/OS image, which has a boot screen")
            moveTo(page, "Boot Colors")
            verify(menuRow(page, at).dimmed)
            verify(helpOf(page).text.indexOf("Only on the OSD/OS image") === 0, helpOf(page).text)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Left)
            compare(appCore.values.boot_colors, undefined)
        }

        // Cabinet Lock's row says what the lock does now: On while there is a
        // Start on Module to stay on; with none (set to None since, or its
        // module turned off) On (No Module), the help line saying it locks
        // nothing; it follows Start on Module as that changes on the page,
        // and still opens its page. Studio Monitor's Status says the same.
        function test_cabinetLockRow() {
            appCore.values = { cabinet_lock: "On", startup_module: "com.osdos.local_files" }
            var page = open({ group: "startup" })
            compare(row(page, "Cabinet Lock").value, "On")
            compare(cursor(page), "Start On")
            keyClick(Qt.Key_Left)
            compare(appCore.values.startup_module, "None")
            compare(row(page, "Cabinet Lock").value, "On (No Module)")
            compare(cursor(page), "Start On")
            moveTo(page, "Cabinet Lock")
            verify(helpOf(page).text.indexOf("Locks nothing now") === 0, helpOf(page).text)
            compare(page.offReason(lines(page).indexOf("Cabinet Lock")), "")
            keyClick(Qt.Key_Return)
            compare(root.navigations[root.navigations.length - 1].path, "views/CabinetLock.qml")
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(cursor(page), "Start On")
            keyClick(Qt.Key_Right)
            compare(appCore.values.startup_module, "com.osdos.local_files")
            compare(row(page, "Cabinet Lock").value, "On")
            // A module turned off since is no module to stay on; off is Off.
            loader.source = ""
            appCore.values = { cabinet_lock: "On", startup_module: "com.osdos.plex" }
            page = open({ group: "startup" })
            compare(row(page, "Cabinet Lock").value, "On (No Module)")
            loader.source = ""
            appCore.values = { cabinet_lock: "Off" }
            page = open({ group: "startup" })
            compare(row(page, "Cabinet Lock").value, "Off")
            loader.source = ""
            root.menuLayout = "Studio Monitor"
            appCore.values = { cabinet_lock: "On" }
            page = open()
            compare(row(page, "Cabinet Lock").value, "On (No Module)")
        }

        // Test Patterns, on Screen's page on every machine, opens its page.
        function test_testPatternsRow() {
            var page = open({ group: "screen" })
            moveTo(page, "Test Patterns")
            keyClick(Qt.Key_Return)
            compare(root.navigations[root.navigations.length - 1].path, "views/TestPatterns.qml")
        }

        // Wi-Fi, on Network's page, opens its page once NetworkManager can be
        // asked (wifiManager.supported); without it, dimmed, it opens nothing.
        function test_wifiRow() {
            var page = open({ group: "network" })
            compare(cursor(page), "Wi-Fi")
            verify(page.offReason(0).indexOf("NetworkManager") > 0)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 0)
            // Kept off by an old launcher alone, on a machine NetworkManager
            // runs on: dimmed still, the reason what it needs, not where it
            // would work.
            loader.source = ""
            wifiManager.launcherTooOld = true
            page = open({ group: "network" })
            verify(page.offReason(0).indexOf("Needs a newer launcher") === 0, page.offReason(0))
            verify(page.offReason(0).indexOf("NetworkManager") < 0)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 0)
            wifiManager.launcherTooOld = false
            loader.source = ""
            wifiManager.supported = true
            page = open({ group: "network" })
            compare(page.offReason(0), "")
            keyClick(Qt.Key_Return)
            compare(root.navigations[root.navigations.length - 1].path, "views/Wifi.qml")
        }
    }

    // Settings' pages that are read with no key pressed hold the screen saver
    // off while they are (IdleTracker's viewActive), and let it come again
    // as they go.
    TestCase {
        name: "SaverHeldOff"
        when: windowShown

        function init() {
            idleTracker.viewActive = false
            idleTracker.resets = 0
            root.backs = 0
        }
        function cleanup() {
            loader.source = ""
        }

        // Export to OSD/OS Setup (views/SetupExport.qml): its question is a
        // menu like any other; once the codes are up, which a phone reads,
        // the saver waits, until the page goes. Left at the question, it
        // never held it.
        function test_exportCodes() {
            loader.setSource(Qt.resolvedUrl("../../views/SetupExport.qml"), { navParams: {} })
            var page = loader.item
            verify(page !== null, "loaded SetupExport.qml")
            page.forceActiveFocus()
            verify(page.asking)
            verify(!idleTracker.viewActive)
            keyClick(Qt.Key_Return)
            verify(!page.asking)
            verify(idleTracker.viewActive)
            compare(idleTracker.resets, 0)
            loader.source = ""
            tryVerify(function() { return !idleTracker.viewActive })
            compare(idleTracker.resets, 1)
            idleTracker.resets = 0
            loader.setSource(Qt.resolvedUrl("../../views/SetupExport.qml"), { navParams: {} })
            loader.item.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
            loader.source = ""
            wait(50)
            verify(!idleTracker.viewActive)
            compare(idleTracker.resets, 0)
        }

        // A Bluetooth pairing's code (views/BluetoothPrompt.qml), typed on
        // the device or compared with its own: the saver waits while it is
        // up, and may come again once the pairing is done, or the page goes
        // with it up.
        function test_pairingCode() {
            loader.setSource(Qt.resolvedUrl("../../views/BluetoothPrompt.qml"), { prompt: {} })
            var prompt = loader.item
            verify(prompt !== null, "loaded BluetoothPrompt.qml")
            verify(!prompt.visible)
            verify(!idleTracker.viewActive)
            prompt.prompt = { kind: "passkey", code: "123456", entered: 0, name: "Keyboard" }
            verify(prompt.visible)
            verify(idleTracker.viewActive)
            prompt.prompt = {}
            verify(!prompt.visible)
            verify(!idleTracker.viewActive)
            compare(idleTracker.resets, 1)
            prompt.prompt = { kind: "confirm", code: "123456", name: "Pad" }
            verify(idleTracker.viewActive)
            loader.source = ""
            tryVerify(function() { return !idleTracker.viewActive })
            compare(idleTracker.resets, 2)
        }
    }

    // Settings' pages that show words from outside the app, each with a
    // stand-in of its own for what it reads: Update's (a release's tag, why
    // the check failed) and Bluetooth's details (what the system says, its
    // devices' names among it).
    Component {
        id: updatePage
        Item {
            property alias page: updateLoader.item
            anchors.fill: parent
            QtObject {
                id: updateManager
                property string state: "error"
                property string latestVersion: "<b>v9.9.9</b>"
                property string releaseNotes: ""
                property string errorMessage: "<b>The server</b> could not be reached"
                property string applyHint: ""
                property bool canApply: false
                property real downloadProgress: 0
                function checkForUpdates() {}
            }
            Loader {
                id: updateLoader
                anchors.fill: parent
                source: Qt.resolvedUrl("../../views/Update.qml")
            }
        }
    }
    Component {
        id: bluetoothPage
        Item {
            property alias page: bluetoothLoader.item
            anchors.fill: parent
            QtObject {
                id: bluetoothManager
                property bool supported: true
                property bool available: true
                property bool powered: true
                property bool searching: false
                property var devices: []
                property var prompt: ({})
                property string message: ""
                property bool powerFailed: false
                property string adapterName: "den-tv"
                property string details: "Device 00:11:22:33:44:55 <b>Pad</b>\n\tPaired: yes"
                function stopSearch() {}
                function cancelPairing() {}
                function collectDetails() {}
                function clearMessage() {}
            }
            Loader {
                id: bluetoothLoader
                anchors.fill: parent
                source: Qt.resolvedUrl("../../views/Bluetooth.qml")
            }
        }
    }

    // What a release, a network or a device says is shown as it is written
    // on Settings' pages: a name that looks like markup is its letters, never
    // read as markup (a picture's tag in one would fetch its address).
    TestCase {
        name: "WordsAsWritten"
        when: windowShown

        function cleanup() {
            loader.source = ""
            setupExport.links = ["https://osdos.tv/setup/#1"]
            setupExport.missing = []
        }

        // Every Text an item holds whose words have "<b>" in them, all the
        // way down.
        function marked(item) {
            var found = []
            if (item.text !== undefined && item.textFormat !== undefined && String(item.text).indexOf("<b>") >= 0)
                found.push(item)
            for (var i = 0; i < item.children.length; i++)
                found = found.concat(marked(item.children[i]))
            return found
        }
        // At least so many of them, each as it is written.
        function asWritten(item, atLeast) {
            var texts = marked(item)
            verify(texts.length >= atLeast, texts.length + " of " + atLeast + " lines found")
            for (var i = 0; i < texts.length; i++)
                compare(texts[i].textFormat, Text.PlainText, String(texts[i].text))
        }

        // The latest release's tag, and why the check failed.
        function test_update() {
            var held = createTemporaryObject(updatePage, root)
            verify(held !== null && held.page !== null, "loaded Update.qml")
            compare(held.page.statusText, "<b>The server</b> could not be reached")
            asWritten(held.page, 2)
        }

        // Bluetooth's details.
        function test_bluetoothDetails() {
            var held = createTemporaryObject(bluetoothPage, root)
            verify(held !== null && held.page !== null, "loaded Bluetooth.qml")
            asWritten(held.page, 1)
        }

        // Why Export to OSD/OS Setup has no code, a Wi-Fi network's name in
        // it: in the question and in the code's place.
        function test_exportWithoutACode() {
            setupExport.links = []
            setupExport.missing = ["Wi-Fi: <b>Attic</b>'s password couldn't be read"]
            loader.setSource(Qt.resolvedUrl("../../views/SetupExport.qml"), { navParams: {} })
            var page = loader.item
            verify(page !== null, "loaded SetupExport.qml")
            asWritten(page, 2)
        }
    }

    Component {
        id: appBar
        Components.AppBar {}
    }

    TestCase {
        name: "AppBar"
        when: windowShown

        function findText(item, text) {
            if (item.text === text)
                return item
            for (var i = 0; i < item.children.length; i++) {
                var found = findText(item.children[i], text)
                if (found)
                    return found
            }
            return null
        }

        // A title that fits stands still, its subtitle beside it.
        function test_titleThatFits() {
            var bar = createTemporaryObject(appBar, root, { title: "Display Output", subtitle: "Now" })
            var title = findText(bar, "Display Output")
            verify(title !== null)
            // Its line, in the room it scrolls through (a ScrolledText's).
            title = title.textItem
            compare(title.parent.width, title.implicitWidth)
            verify(!title.parent.clip)
            verify(findText(bar, "Now").visible)
            wait(2000)
            compare(title.x, 0)
        }

        // One too long for the bar shows its start, then scrolls to its
        // end and back, with no room for a subtitle; hidden, it stops.
        function test_titleTooLong() {
            var text = "Switch to a display output of a long name (1920x240)?"
            var bar = createTemporaryObject(appBar, root, { title: text, subtitle: "Now" })
            var title = findText(bar, text)
            verify(title !== null)
            // Its line, in the room it scrolls through (a ScrolledText's).
            title = title.textItem
            var clip = title.parent
            verify(title.implicitWidth > clip.width)
            verify(clip.clip)
            verify(clip.mapToItem(bar, clip.width, 0).x <= bar.width)
            verify(!findText(bar, "Now").visible)
            compare(title.x, 0)
            // A pause, then 20 ms a pixel.
            var scrollBy = clip.width - title.implicitWidth
            tryVerify(function() { return Math.abs(title.x - scrollBy) < 1 }, 1500 - scrollBy * 20 + 3000,
                      "scrolled to the title's end")
            tryCompare(title, "x", 0, 4000)
            bar.visible = false
            wait(2500)
            compare(title.x, 0)
        }
    }

    // Every symbol PixelIcon has, side by side on a ground of a colour no
    // symbol is drawn in, as the component draws them: pictures from the
    // app's image provider.
    Component {
        id: pictureSheet
        Rectangle {
            id: sheet
            property color ink: "white"
            property int cell: 2
            property var names: []
            width: 620
            height: flow.height
            color: "#3a55b4"
            Flow {
                id: flow
                width: parent.width
                spacing: 3
                Repeater {
                    model: sheet.names
                    Components.PixelIcon {
                        required property string modelData
                        name: modelData
                        color: sheet.ink
                        pixel: sheet.cell
                    }
                }
            }
        }
    }
    // The same, each painted on a Canvas of its own from its cells, a run of
    // set cells a rectangle: how PixelIcon drew a symbol before it was a
    // picture, and what the pictures are held to.
    Component {
        id: canvasSheet
        Rectangle {
            id: sheet
            property color ink: "white"
            property int cell: 2
            property var names: []
            property var shapes: ({})
            property int painted: 0
            width: 620
            height: flow.height
            color: "#3a55b4"
            Flow {
                id: flow
                width: parent.width
                spacing: 3
                Repeater {
                    model: sheet.names
                    Canvas {
                        required property string modelData
                        readonly property var rows: sheet.shapes[modelData] || []
                        width: (rows.length ? rows[0].length : 0) * sheet.cell
                        height: rows.length * sheet.cell
                        antialiasing: false
                        smooth: false
                        onPainted: sheet.painted++
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            ctx.fillStyle = sheet.ink
                            for (var y = 0; y < rows.length; ++y) {
                                var row = rows[y]
                                var x = 0
                                while (x < row.length) {
                                    if (row[x] !== "#") {
                                        ++x
                                        continue
                                    }
                                    var start = x
                                    while (x < row.length && row[x] === "#")
                                        ++x
                                    ctx.fillRect(start * sheet.cell, y * sheet.cell, (x - start) * sheet.cell, sheet.cell)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Component {
        id: pixelIcon
        Components.PixelIcon {}
    }

    TestCase {
        name: "PixelIcon"
        when: windowShown

        // A symbol is a picture of its cells in its colour, the size of its
        // cells times its pixel, and two that show the same share it; another
        // colour, size or symbol is another picture.
        function test_picture() {
            var play = createTemporaryObject(pixelIcon, root, { name: "play", color: "#ff8000", pixel: 3 })
            compare(play.width, 4 * 3)
            compare(play.height, 7 * 3)
            compare(play.status, Image.Ready)
            compare(play.sourceSize, Qt.size(12, 21))
            verify(String(play.source).indexOf("image://osdpixel/ff8000/1000,1100,1110,1111,") === 0, play.source)
            var twin = createTemporaryObject(pixelIcon, root, { name: "play", color: "#ff8000", pixel: 3 })
            compare(twin.source, play.source)
            twin.color = "#ffffff"
            verify(twin.source !== play.source)
            compare(twin.status, Image.Ready)
            twin.color = "#ff8000"
            twin.pixel = 4
            compare(twin.source, play.source)
            compare(twin.sourceSize, Qt.size(16, 28))
            compare(twin.status, Image.Ready)
            // A name it hasn't: nothing drawn, no room taken, no picture asked for.
            var none = createTemporaryObject(pixelIcon, root, { name: "no such symbol" })
            compare(none.width, 0)
            compare(none.height, 0)
            compare(String(none.source), "")
            compare(none.status, Image.Null)
        }

        // Every symbol, at each size a screen gives a pixel and in a scheme's
        // colours, a dark one on a light ground too: pixel for pixel what a
        // Canvas painted of it.
        function test_sameAsPainted_data() {
            var rows = []
            var inks = ["#ffffff", "#000000", "#ffb000", "#1e90ff", "#102030"]
            var cells = [1, 2, 3, 4, 6]
            for (var c = 0; c < cells.length; c++) {
                for (var i = 0; i < inks.length; i++)
                    rows.push({ tag: cells[c] + " px " + inks[i], cell: cells[c], ink: inks[i] })
            }
            return rows
        }
        function test_sameAsPainted(data) {
            var probe = createTemporaryObject(pixelIcon, root, {})
            var shapes = probe.shapes
            var names = Object.keys(shapes)
            verify(names.length >= 38, "every symbol: " + names.length)

            var painted = createTemporaryObject(canvasSheet, root, { ink: data.ink, cell: data.cell,
                                                                    names: names, shapes: shapes })
            var pictures = createTemporaryObject(pictureSheet, root, { ink: data.ink, cell: data.cell, names: names })
            tryVerify(function() { return painted.painted >= names.length }, 5000, "every Canvas painted")
            compare(pictures.width, painted.width)
            compare(pictures.height, painted.height)
            verify(pictures.height >= 7 * data.cell)

            var was = grabImage(painted)
            var now = grabImage(pictures)
            compare(now.width, was.width)
            compare(now.height, was.height)
            // The two sheets the same, pixel for pixel; where they aren't,
            // the first pixel that differs is said.
            if (!now.equals(was)) {
                for (var y = 0; y < was.height; y++) {
                    for (var x = 0; x < was.width; x++) {
                        if (!Qt.colorEqual(was.pixel(x, y), now.pixel(x, y)))
                            fail("at " + x + "," + y + ": painted " + was.pixel(x, y) + ", the picture " + now.pixel(x, y))
                    }
                }
                fail("the sheets differ, though no pixel does")
            }
            // And not two sheets of nothing: the first symbol, play, is its
            // sixteen cells in the colour and twelve clear.
            compare(names[0], "play")
            var inked = 0, clear = 0
            for (y = 0; y < 7 * data.cell; y++) {
                for (x = 0; x < 4 * data.cell; x++) {
                    if (Qt.colorEqual(now.pixel(x, y), data.ink))
                        inked++
                    else if (Qt.colorEqual(now.pixel(x, y), pictures.color))
                        clear++
                }
            }
            compare(inked, 16 * data.cell * data.cell)
            compare(clear, 12 * data.cell * data.cell)
            // Its tip a cell wide, its fourth row all four.
            verify(Qt.colorEqual(now.pixel(0, 0), data.ink))
            verify(Qt.colorEqual(now.pixel(data.cell, 0), pictures.color))
            verify(Qt.colorEqual(now.pixel(4 * data.cell - 1, 3 * data.cell), data.ink))
        }
    }
}
