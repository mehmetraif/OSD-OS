import QtQuick
import QtTest
import "../../views/Components" as Components
import "../../views/Components/Hover.js" as Hover

// Settings, views/Settings.qml, against stand-ins for AppCore and the rest it
// reads: the first screen opens on TV / Movie's and Arcade's pages, then the
// general settings under their heading, the modules of no group and the
// application's rows; TV / Movie's page holds the videos' rows and the video
// modules'; select opens TV / Movie's page, and Arcade's as the arcade
// modules' settings on one page (ModuleSettings, alsoModules). And the title
// bar its questions share with every view (AppBar): a title too long for it,
// as a long display output's name makes Switch to …?, scrolls through it.
Rectangle {
    id: root
    color: surfaceColor
    width: 640
    height: 480
    // Main.qml's, as the views and their components read them.
    property real sw: width
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
                           browse: "[►]", select: "[ENTER]", play_pause: "[SPACE]" })
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
    function transitionUsable(kind) { return true }
    function osdBackgroundOf(raw) { return raw === "Window" || raw === "Off" ? raw : "Full" }
    function osdFrameOf(raw) { return raw === "Off" || raw === "Shadow" ? raw : "On" }
    function backgroundOn(raw) { return false }
    function solidityOf(raw) { return 100 }

    // AppCore as Settings uses it: the modules, a group's each, and the
    // app's settings.
    QtObject {
        id: appCore
        property var values: ({})
        function get_settings() { return { app: values, modules: {} } }
        function get_installed_modules() {
            return [{ id: "com.osdos.arcade", name: "Arcade", has_settings: true, settings_group: "arcade", enabled: true },
                    { id: "com.osdos.highscores", name: "High Scores", has_settings: true, settings_group: "arcade", enabled: false },
                    { id: "com.osdos.local_files", name: "Local Files", has_settings: true, settings_group: "tv", enabled: true },
                    { id: "com.osdos.plex", name: "Plex", has_settings: true, settings_group: "tv", enabled: false },
                    { id: "com.osdos.scripts", name: "Scripts", has_settings: true, settings_group: "", enabled: false },
                    { id: "com.osdos.radio", name: "Radio", has_settings: true, settings_group: "radio", enabled: false },
                    { id: "com.osdos.bare", name: "Bare", has_settings: false, settings_group: "tv", enabled: true }]
        }
        function isAutostartSession() { return false }
        function canRestartSystem() { return false }
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
    }
    QtObject {
        id: bluetoothManager
        property bool supported: false
    }
    QtObject {
        id: wifiManager
        property bool supported: false
    }

    // Where the page asked to go.
    property var navigations: []

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }
    Connections {
        target: loader.item
        ignoreUnknownSignals: true
        function onNavigateTo(path, params, listState) {
            root.navigations = root.navigations.concat([{ path: path, params: params }])
        }
    }

    TestCase {
        name: "Settings"
        when: windowShown

        function init() {
            appCore.values = {}
            root.navigations = []
            wifiManager.supported = false
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

        // The first screen: TV / Movie and Arcade at the top, the general
        // settings under their heading, the modules of no group (with
        // settings; a group with no page counts as none), then the
        // application's; no video or arcade rows.
        function test_firstScreen() {
            var page = open()
            var all = lines(page)
            compare(all.slice(0, 4), ["TV / Movie", "Arcade", "# General Settings", "Theme"])
            verify(all.indexOf("Mouse Pointer") > all.indexOf("# General Settings"))
            verify(all.indexOf("Boot Screen Colors") > 0)
            var elsewhere = ["Scaling", "Transparent Background", "Video Levels", "Loading Effect",
                             "Loading Screen Colors", "Channel Logo", "Info Screen", "Local Files", "Plex",
                             "High Scores", "System Information"]
            for (var i = 0; i < elsewhere.length; i++)
                verify(all.indexOf(elsewhere[i]) < 0, elsewhere[i] + " isn't on the first screen")
            var modules = all.indexOf("# Modules")
            compare(all.slice(modules + 1, modules + 4), ["Scripts", "Radio", "# Application"])
            compare(all.slice(-2), ["About", "Quit"])
            compare(row(page, "Arcade").moduleId, "com.osdos.arcade")
            compare(row(page, "Arcade").alsoModules, ["com.osdos.highscores"])
            compare(cursor(page), "TV / Movie")
        }

        // Window Mode is offered where the menus can be a window, on a
        // desktop, and nowhere else: not on the offscreen platform the tests
        // run on, as not on the Pi's console.
        function test_windowModeRow() {
            var page = open()
            var all = lines(page)
            compare(all.indexOf("Window Mode") >= 0, page.desktopWindow)
            compare(page.desktopWindow, Qt.platform.os === "windows" || Qt.platform.pluginName === "xcb"
                                        || Qt.platform.pluginName === "wayland")
            if (page.desktopWindow) {
                compare(row(page, "Window Mode").options, ["Off", "On"])
                compare(row(page, "Window Mode").value, "Off")
            }
        }

        // TV / Movie's page: the videos' rows, then the video modules with
        // settings; nothing of the look or the application.
        function test_tvPage() {
            var page = open({ group: "tv" })
            var all = lines(page)
            compare(all.slice(0, 2), ["Scaling", "Transparent Background"])
            verify(all.indexOf("Video Levels") > 0)
            verify(all.indexOf("Loading Effect") > 0)
            verify(all.indexOf("Loading Screen Colors") > 0)
            verify(all.indexOf("Channel Logo") > 0)
            verify(all.indexOf("Info Screen") > 0)
            compare(all.slice(-3), ["# Modules", "Local Files", "Plex"])
            verify(all.indexOf("Theme") < 0)
            verify(all.indexOf("Boot Screen Colors") < 0)
            verify(all.indexOf("Quit") < 0)
            verify(all.indexOf("Bare") < 0)
            // Its rows change as on the first screen.
            compare(cursor(page), "Scaling")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(appCore.values.transparent_background, 40)
        }

        // Select on TV / Movie opens its page; on Arcade, the arcade modules'
        // settings, the first module's page with the others' rows on it.
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
            // Down from Arcade steps over the heading, onto Theme.
            keyClick(Qt.Key_Down)
            compare(cursor(loader.item), "Theme")
        }

        // A line that leads on (TV / Movie, Arcade, a module's, Quit) ends in
        // a ►, and ► opens it as select does; a list's choice stands between
        // the ◄ and ► that change it, and ► changes it, opening nothing; a
        // heading has neither.
        function test_marks() {
            var page = open()
            var tv = menuRow(page, 0)
            verify(tv !== null)
            verify(tv.opens)
            verify(!tv.changes)
            var heading = menuRow(page, 2)
            verify(!heading.opens)
            verify(!heading.changes)
            var theme = menuRow(page, 3)
            verify(theme.changes)
            verify(!theme.opens)
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "views/Settings.qml")
            compare(root.navigations[0].params.group, "tv")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 2)
            compare(root.navigations[1].path, "views/ModuleSettings.qml")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(cursor(page), "Color Scheme")
            verify(menuRow(page, 4).changes)
            var before = row(page, "Color Scheme").value
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 2)
            verify(row(page, "Color Scheme").value !== before)
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
            var list = null
            for (var i = 0; i < page.children.length && !list; i++) {
                if (page.children[i].currentIndex !== undefined && page.children[i].model !== undefined)
                    list = page.children[i]
            }
            verify(list !== null)
            // The lines are laid out again as the cursor moves (the help line
            // under them): a moment for that, as a mouse takes between rows.
            var hover = function(p) { Hover.follow(root, root.Window.activeFocusItem, p.x, p.y); waitForRendering(page) }
            var middle = function(item) { verify(item !== null); return item.mapToItem(root, item.width / 2, item.height / 2) }
            compare(cursor(page), "TV / Movie")
            hover(middle(list.itemAtIndex(3)))
            compare(cursor(page), "Theme")
            hover(middle(list.itemAtIndex(2)))
            compare(cursor(page), "Theme")
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
            hover(middle(list.itemAtIndex(list.count - 2)))   // About, under the question
            compare(cursor(page), "Quit")
            verify(yesNo.itemAtIndex(1).current)
            keyClick(Qt.Key_Escape)
            compare(root.navigations.length, 0)
        }

        // Wi-Fi among the general settings once NetworkManager can be asked
        // (wifiManager.supported), after Controls; select opens its page.
        function test_wifiRow() {
            var page = open()
            verify(lines(page).indexOf("Wi-Fi") < 0)
            loader.source = ""
            wifiManager.supported = true
            page = open()
            var all = lines(page)
            verify(all.indexOf("Wi-Fi") > all.indexOf("Controls"))
            for (var i = 0; i < all.length && cursor(page) !== "Wi-Fi"; i++)
                keyClick(Qt.Key_Down)
            compare(cursor(page), "Wi-Fi")
            keyClick(Qt.Key_Return)
            compare(root.navigations[root.navigations.length - 1].path, "views/Wifi.qml")
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
