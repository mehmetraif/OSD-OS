import QtQuick
import QtTest
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
Rectangle {
    id: root
    color: surfaceColor
    width: 640
    height: 480
    // Main.qml's, as the views and their components read them.
    property real sw: width
    // The layout's frame and its pane (Main.qml): the column's icons as a
    // view hands them over, and where a view's rows go.
    property var frameIcons: []
    property int frameIndex: 0
    property bool frameActive: false
    property bool framed: false
    readonly property real paneX: framed ? sw * 0.209375 : sw * 0.115625
    readonly property real paneWidth: framed ? sw * 0.675 : sw * 0.76875
    function setFrame(icons, index, active, tall) { frameIcons = icons; frameIndex = index; frameActive = active }
    property real sh: height
    property real px: 2
    property string globalFont: "monospace"
    property color primaryColor: "white"
    property color surfaceColor: "black"
    property var skin: ({})
    property bool hintBar: true
    property bool helpLine: true
    property var statusIcons: []
    readonly property real hintRoom: 0
    readonly property real helpRoom: 0
    readonly property real helpLineMargin: sh * 0.1583333
    property rect contentBox: Qt.rect(sw * 0.115625, sh * 0.11875, sw * 0.76875, sh * 0.7770833)
    property rect osdWindow: contentBox
    property string osdBackground: "Full"
    property string osdFrame: "On"
    property var backgroundEffect: ({})
    property string backgroundShader: ""
    property bool effectsRest: true
    property real fxTime: 0
    property var selector: null
    property var hints: ({ back: "[ESC]", navigate: "[▲▼]", arrows: "[▲▼◄►]", change: "[◄►]",
                           browse: "[►]", left: "[◄]", select: "[ENTER]", play_pause: "[SPACE]" })
    // Settings → Look → Menu Layout, as Main.qml reads it.
    property string menuLayout: "Standard"
    function menuLayoutOf(raw) {
        return ["Camcorder", "Studio Monitor", "Broadcast", "Flat Panel"].indexOf(raw) >= 0 ? raw : "Standard"
    }
    readonly property var menuPalette: ({
        "Studio Monitor": { ground: "#050608", ink: "#FFFFFF", dim: "#6E7891", accent: "#2350C8",
                            soft: "#CFDDFF", mark: "#F4D21C", bar: "#050608" },
        "Broadcast":      { ground: "#3A55B4", ink: "#FFFFFF", dim: "#94A8EC", accent: "#FFFFFF",
                            soft: "#FFFFFF", mark: "#FFFFFF", bar: "#3A55B4" },
        "Flat Panel":     { ground: "#000000", ink: "#EDEDED", dim: "#7B7B7B", accent: "#7B7B7B",
                            soft: "#9A9A9A", mark: "#F6CF00", bar: "#4C4C4C" }
    })[menuLayout] || null
    property bool videoBehind: false
    property int backdropSolidity: 100
    function selectorShown(item, shown) {}
    function coverShown(item, shown) {}
    function skinIcon(url) { return url }
    // What Settings reads of Main.qml's own.
    property bool devBuild: true
    property string appVersion: "1.0.0"
    property bool effectsUsable: true
    property var allSchemes: ({ "Video 1": {}, "Late Night": {} })
    property var textPresets: ({ "Rainbow": {} })
    property var backgroundPresets: ({ "Matrix": {} })
    property var selectorPresets: ["Sparkles"]
    property var screenPresets: ({ "Scanlines": {} })
    property var transitionPresets: ["Fade"]
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
        function get_settings() { return { app: values, modules: {} } }
        function get_installed_modules() {
            return moreModules.concat(
                   [{ id: "com.osdos.arcade", name: "Arcade", has_settings: true, settings_group: "arcade", enabled: true },
                    { id: "com.osdos.highscores", name: "High Scores", has_settings: true, settings_group: "arcade", enabled: false },
                    { id: "com.osdos.local_files", name: "Local Files", has_settings: true, settings_group: "tv", enabled: true },
                    { id: "com.osdos.plex", name: "Plex", has_settings: true, settings_group: "tv", enabled: false },
                    { id: "com.osdos.scripts", name: "Scripts", has_settings: true, settings_group: "", enabled: false },
                    { id: "com.osdos.radio", name: "Radio", has_settings: true, settings_group: "radio", enabled: false },
                    { id: "com.osdos.bare", name: "Bare", has_settings: false, settings_group: "tv", enabled: true }])
        }
        function isAutostartSession() { return false }
        function canRestartSystem() { return false }
        // Quit's Save Logs: offered while canLogs, saveLogs() counted, the
        // answer sent with logsSaved().
        property bool canLogs: false
        property int logSaves: 0
        signal logsSaved(bool ok, string detail)
        function canSaveLogs() { return canLogs }
        function saveLogs() { logSaves++; return true }
        function themes() { return [] }
        function skins() { return [] }
        function getCustomColorSchemes() { return ({}) }
        function getCustomColorScheme() { return ({}) }
        function localIpAddress() { return "" }
        function save_setting(module, key, value) {
            var v = Object.assign({}, values)
            v[key] = value
            values = v
            return true
        }
        // Several at once, and how many times that was asked.
        property int batches: 0
        function save_settings(module, some) {
            batches++
            values = Object.assign({}, values, some)
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
    QtObject {
        id: displayOutput
        property bool available: false
        signal changed()
        signal nextStartFinished(bool ok, string reason)
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
            appCore.pasted = ""
            appCore.canLogs = false
            appCore.logSaves = 0
            root.navigations = []
            root.backs = 0
            root.menuLayout = "Standard"
            root.setFrame([], 0, false)
            wifiManager.supported = false
            wifiManager.launcherTooOld = false
            bootProgress.available = true
            root.transitionPresets = ["Fade"]
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
        // the one in it (a slider's line has its bar beside it).
        function menuRow(item, index) {
            if (item.itemAtIndex !== undefined) {
                var d = item.itemAtIndex(index)
                if (!d)
                    return null
                if (d.opens !== undefined)
                    return d
                for (var j = 0; j < d.children.length; j++) {
                    if (d.children[j].opens !== undefined)
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
                { tag: "look", lines: ["Theme", "Orb Fog", "Color Scheme", "Skin", "Effects", "Menu Layout",
                                       "OSD Background", "Window Frame", "Boot Screen Colors", "Hint Bar", "Help Line",
                                       "Status Icons"] },
                { tag: "effects", lines: ["Text Effect", "Background Effect", "Selector Effect", "Screen Effect",
                                          "Transition"] },
                { tag: "screen", lines: ["Display Output", "Monitor", "Test Patterns", "Screen Saver", "Mouse Pointer",
                                         "Window Mode"] },
                { tag: "sound", lines: ["Audio Output", "Menu Music", "Music File", "Music Volume"] },
                { tag: "network", lines: ["Wi-Fi", "Bluetooth"] },
                { tag: "startup", lines: ["Start on Module", "Play at Startup", "Startup From", "Cabinet Lock"] },
                { tag: "system", lines: ["Paste OSD/OS Setup", "Export to OSD/OS Setup", "Update", "About", "# Modules", "Scripts", "Radio"] }
            ]
        }
        function test_pages(data) {
            var page = open({ group: data.tag })
            compare(lines(page), data.lines)
            verify(page.settingsItems.length <= 12, "a screenful")
        }

        // A row that goes with another's value is dimmed rather than hidden:
        // the cursor stops on it, ◄ ► and select leave it as it is, and the
        // help line says why. Its row set so, it is a row like the others.
        function test_dimmedRow() {
            var page = open({ group: "look" })
            moveTo(page, "Window Frame")
            var at = lines(page).indexOf("Window Frame")
            compare(page.offReason(at), "Only with OSD Background on Window")
            var frame = menuRow(page, at)
            verify(frame.dimmed)
            verify(!frame.changes)
            var help = helpOf(page)
            verify(help.visible)
            verify(help.text.indexOf("Only with OSD Background on Window") === 0, help.text)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Return)
            compare(appCore.values.osd_frame, undefined)
            compare(row(page, "Window Frame").value, "On")
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

        // Sound: Menu Music's rows dimmed while it is Off, each saying for
        // what; Audio Output dimmed where OSD/OS can't choose a sound card.
        function test_soundPage() {
            var page = open({ group: "sound" })
            var all = lines(page)
            verify(page.offReason(all.indexOf("Audio Output")).indexOf("Only where") === 0)
            compare(page.offReason(all.indexOf("Menu Music")), "")
            compare(page.offReason(all.indexOf("Music File")), "Only with Menu Music on File")
            compare(page.offReason(all.indexOf("Music Volume")), "Off while Menu Music is Off")
            moveTo(page, "Menu Music")
            keyClick(Qt.Key_Right)
            compare(appCore.values.menu_music, "File")
            compare(page.offReason(all.indexOf("Music File")), "")
            compare(page.offReason(all.indexOf("Music Volume")), "")
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
        }

        // Paste OSD/OS Setup, on System's page before Export to OSD/OS Setup
        // and Update, used where
        // there is a clipboard to paste from (not the Pi's own console):
        // select hands the clipboard's text to AppCore, the notice it answers
        // with shows over the rows, made again as they are now, and back
        // closes it.
        function test_pasteSetup() {
            var page = open({ group: "system" })
            var all = lines(page)
            compare(page.canPaste, Qt.platform.pluginName !== "eglfs" && Qt.platform.pluginName !== "linuxfb")
            compare(all.indexOf("Paste OSD/OS Setup") + 1, all.indexOf("Export to OSD/OS Setup"))
            compare(all.indexOf("Export to OSD/OS Setup") + 1, all.indexOf("Update"))
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
        }

        // TV / Movie's page: the videos' rows, then the video modules with
        // settings; nothing of the look or the application. 1080p Playback
        // is there, dimmed, off the Pi 3's decode path.
        function test_tvPage() {
            var page = open({ group: "tv" })
            var all = lines(page)
            compare(all.slice(0, 4), ["Scaling", "Transparent Background", "Video Levels", "1080p Playback"])
            verify(page.offReason(all.indexOf("1080p Playback")) !== "")
            compare(page.offReason(all.indexOf("Transparent Background")), "")
            verify(all.indexOf("Loading Effect") > 0)
            verify(all.indexOf("Loading Screen Colors") > 0)
            verify(all.indexOf("Channel Logo") > 0)
            verify(all.indexOf("Info Screen") > 0)
            compare(all.slice(-3), ["# Modules", "Local Files", "Plex"])
            verify(all.indexOf("Theme") < 0)
            verify(all.indexOf("Boot Screen Colors") < 0)
            verify(all.indexOf("Quit") < 0)
            verify(all.indexOf("Bare") < 0)
            // Its rows change as on any page.
            compare(cursor(page), "Scaling")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(appCore.values.transparent_background, 40)
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
            keyClick(Qt.Key_Down)
            compare(cursor(page), "Color Scheme")
            verify(menuRow(page, 2).changes)
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
            hover(middle(list.itemAtIndex(3)))
            compare(cursor(page), "About")
            hover(middle(list.itemAtIndex(4)))
            compare(cursor(page), "About")
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
            compare(page.quitOptions.map(function(o) { return o.label }), ["Yes", "Save Logs", "No"])
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
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(appCore.logSaves, 2)
            appCore.logsSaved(false, "The card's OSD-OS partition isn't there or can't be written")
            compare(notice.promptText, "Logs not saved")
            compare(notice.subtitleText, "The card's OSD-OS partition isn't there or can't be written")
            keyClick(Qt.Key_Return)
            verify(!notice.visible, "select closes it too")
        }

        // Nothing to save logs to (not the image): Quit is Yes or No.
        function test_quitWithoutLogs() {
            var page = open()
            compare(page.quitOptions.map(function(o) { return o.label }), ["Yes", "No"])
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
                    ["tv", "joystick", "sparkle", "monitor", "speaker", "gamepad", "antenna", "power", "gear", "eject"])
            compare(page.camIndex, 0)
            verify(page.inIcons())
            // Handed to the shell, which draws the column and keeps it
            // beside the pages opened from here.
            verify(page.inFrame && page.ownsFrame)
            compare(frameIcons(), ["tv", "joystick", "sparkle", "monitor", "speaker", "gamepad", "antenna", "power", "gear", "eject"])
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

        // The monitor layouts' frame: the one drawn, its title and guide.
        function chromeOf(page) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i].guide !== undefined && page.children[i].pageTo !== undefined)
                    return page.children[i]
            }
            return null
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
                    ["status", "tv", "joystick", "sparkle", "monitor", "speaker", "gamepad", "antenna", "power", "gear", "eject"])
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
            moveTo(page, "Boot Screen Colors")
            var before = row(page, "Boot Screen Colors").value
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
            compare(row(page, "Boot Screen Colors").value, before)
            compare(root.backs, 0)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(page.editIndex, -1)
            verify(appCore.values.boot_colors !== undefined)
            verify(row(page, "Boot Screen Colors").value !== before)
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
            compare(chrome.title, ">Look")
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
            compare(chromeOf(page).title, ">Look >Effects")
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
        }

        // Startup From, on Startup's page, is for a favourite that can be
        // resumed, Local Files' or YouTube's: with none it waits for Play at
        // Startup; with an Arcade game's, or a web player's title's, it is
        // dimmed, saying whose it is for, and ◄ ► leave it as it is.
        function test_startupFromRow() {
            var page = open({ group: "startup" })
            var at = lines(page).indexOf("Startup From")
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
                compare(page.offReason(at), favorites[i].usable ? "" : "Only with a Local Files or YouTube favourite")
                moveTo(page, "Startup From")
                compare(menuRow(page, at).dimmed, !favorites[i].usable)
                keyClick(Qt.Key_Right)
                compare(appCore.values.startup_from, favorites[i].usable ? "Beginning" : undefined)
            }
            verify(helpOf(page).text.indexOf("Only with a Local Files or YouTube favourite") === 0, helpOf(page).text)
        }

        // Boot Screen Colors, on Look's page: a row like the others on the
        // OSD/OS image, which has a boot screen; anywhere else dimmed, the
        // help line saying where it is for, ◄ ► leaving it as it is.
        function test_bootScreenColorsRow() {
            var page = open({ group: "look" })
            var at = lines(page).indexOf("Boot Screen Colors")
            compare(page.offReason(at), "")
            moveTo(page, "Boot Screen Colors")
            keyClick(Qt.Key_Right)
            compare(appCore.values.boot_colors, "Default")
            loader.source = ""
            appCore.values = {}
            bootProgress.available = false
            page = open({ group: "look" })
            compare(page.offReason(at), "Only on the OSD/OS image, which has a boot screen")
            moveTo(page, "Boot Screen Colors")
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
            appCore.values = { cabinet_lock: "On", startup_module: "com.osdos.arcade" }
            var page = open({ group: "startup" })
            compare(row(page, "Cabinet Lock").value, "On")
            compare(cursor(page), "Start on Module")
            keyClick(Qt.Key_Left)
            compare(appCore.values.startup_module, "None")
            compare(row(page, "Cabinet Lock").value, "On (No Module)")
            compare(cursor(page), "Start on Module")
            moveTo(page, "Cabinet Lock")
            verify(helpOf(page).text.indexOf("Locks nothing now") === 0, helpOf(page).text)
            compare(page.offReason(lines(page).indexOf("Cabinet Lock")), "")
            keyClick(Qt.Key_Return)
            compare(root.navigations[root.navigations.length - 1].path, "views/CabinetLock.qml")
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(cursor(page), "Start on Module")
            keyClick(Qt.Key_Right)
            compare(appCore.values.startup_module, "com.osdos.arcade")
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
            compare(title.parent.width, title.implicitWidth)
            verify(!title.parent.clip)
            verify(findText(bar, "Now").visible)
            wait(2000)
            compare(title.x, 0)
        }

        // One too long for the bar shows its start, then scrolls to its
        // end and back, with no room for a subtitle; hidden, it stops.
        function test_titleTooLong() {
            var text = "Switch to JAMMA 240p 15 kHz (1920x240)?"
            var bar = createTemporaryObject(appBar, root, { title: text, subtitle: "Now" })
            var title = findText(bar, text)
            verify(title !== null)
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
}
