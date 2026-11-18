import QtQuick
import QtTest
import "../../views/Components" as Components

// The main menu, views/ModuleList.qml, against a stand-in for AppCore: the
// modules turned on, a row each; select opens one, back opens Settings. With
// Settings → Look → Menu Layout on Camcorder, the rows are in groups by
// their module's settings group (TV / Movie, Arcade, More, then Settings),
// each an icon in a column at the left: ◄ goes to the icons, ▲ ▼ there from
// group to group, ► or select into the group's rows, Settings' opening it.
Rectangle {
    id: root
    color: surfaceColor
    width: 640
    height: 480
    // Main.qml's, as the view and its components read them.
    property real sw: width
    // The layout's frame and its pane (Main.qml): the column's icons as a
    // view hands them over, and where a view's rows go.
    property var frameIcons: []
    property var menuModules: []
    property int frameIndex: 0
    property bool frameActive: false
    property bool framed: false
    readonly property real paneX: monitorFramed ? frameChrome.listX : framed ? sw * 0.209375 : sw * 0.115625
    readonly property real paneWidth: monitorFramed ? frameChrome.listWidth : framed ? sw * 0.675 : sw * 0.76875
    // A monitor layout's frame, which the shell draws under the views
    // (Main.qml's frameChrome): up in a monitor layout, the views' bars
    // telling it what to write, its place for their rows the pane.
    readonly property bool monitorFramed: menuPalette !== null
    readonly property bool monitorInk: monitorFramed
    readonly property alias chrome: frameChrome
    readonly property real listY: monitorFramed ? frameChrome.listY : sh * 0.25
    readonly property real listHeight: monitorFramed ? frameChrome.listHeight : sh * 0.4666667 + helpRoom + hintRoom
    readonly property real tallListHeight: monitorFramed ? frameChrome.tallHeight : sh * 0.525 + hintRoom
    readonly property real helpedListHeight: monitorFramed ? frameChrome.helpedHeight : sh * 0.4666667 + hintRoom
    readonly property real paneTop: monitorFramed ? frameChrome.listY : sh * 0.2083333
    readonly property real paneBottom: monitorFramed ? frameChrome.tallBottom : sh * 0.8333333
    Components.MonitorChrome {
        id: frameChrome
        anchors.fill: parent
        visible: root.monitorFramed
        icons: root.menuLayout === "Studio Monitor" ? root.frameIcons.map(function(entry) { return entry.icon }) : []
        iconIndex: root.frameIndex
        iconsActive: root.frameActive
    }
    function setFrame(icons, index, active, tall) { frameIcons = icons; frameIndex = index; frameActive = active }
    property real sh: height
    property real px: 2
    property string globalFont: "monospace"
    property color primaryColor: "white"
    property color surfaceColor: "black"
    // A selected cell's box and what is written in it (Main.qml's).
    readonly property color cursorColor: primaryColor
    readonly property color cursorInk: surfaceColor
    property var skin: ({})
    property bool hintBar: true
    property bool helpLine: true
    property var statusIcons: []
    readonly property real hintRoom: 0
    readonly property real helpRoom: 0
    property var hints: ({ back: "[ESC]", navigate: "[▲▼]", change: "[◄►]", browse: "[►]", left: "[◄]",
                           select: "[ENTER]", play_pause: "[SPACE]" })
    property var takeBackNote: null
    property bool videoBehind: false
    property color secondaryColor: "white"
    property color tertiaryColor: "white"
    property string menuLayout: "Standard"
    readonly property var menuPalette: ({
        "Studio Monitor": { ground: "#050608", ink: "#FFFFFF", dim: "#6E7891", accent: "#2350C8",
                            soft: "#CFDDFF", mark: "#F4D21C", bar: "#050608" },
        "Broadcast":      { ground: "#3A55B4", ink: "#FFFFFF", dim: "#94A8EC", accent: "#FFFFFF",
                            soft: "#FFFFFF", mark: "#FFFFFF", bar: "#3A55B4" },
        "Flat Panel":     { ground: "#000000", ink: "#EDEDED", dim: "#7B7B7B", accent: "#7B7B7B",
                            soft: "#9A9A9A", mark: "#F6CF00", bar: "#4C4C4C" }
    })[menuLayout] || null
    property int backdropSolidity: 100
    function selectorShown(item, shown) {}
    function skinIcon(url) { return url }

    // AppCore as the main menu uses it: the modules turned on, as
    // scan_for_modules() sends them, each with its settings group.
    QtObject {
        id: appCore
        // The first row's name, which a test makes too long for the row.
        property string firstName: "Arcade"
        signal modulesLoaded(var modules)
        function scan_for_modules() {
            modulesLoaded([{ name: firstName, entry_point: "modules/arcade/views/Root.qml", group: "arcade" },
                           { name: "High Scores", entry_point: "modules/highscores/views/Root.qml", group: "arcade" },
                           { name: "Local Files", entry_point: "modules/local_files/views/Root.qml", group: "tv" },
                           { name: "Plex", entry_point: "modules/plex/views/Root.qml", group: "tv" },
                           { name: "Scripts", entry_point: "modules/scripts/views/Root.qml", group: "" }])
        }
        function moduleEntryPoint(id) { return "" }
    }
    QtObject {
        id: mpvController
        function stopBackground() {}
    }

    // Where the menu asked to go.
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
            root.navigations = root.navigations.concat([{ path: path, params: params, listState: listState }])
        }
    }

    TestCase {
        name: "MainMenu"
        when: windowShown

        function init() {
            root.navigations = []
            root.menuLayout = "Standard"
            root.setFrame([], 0, false)
            root.width = 640
            appCore.firstName = "Arcade"
        }
        function cleanup() {
            loader.source = ""
        }

        function open(params) {
            loader.setSource(Qt.resolvedUrl("../../views/ModuleList.qml"), { navParams: params || {} })
            verify(loader.item !== null, "loaded ModuleList.qml")
            loader.item.forceActiveFocus()
            return loader.item
        }
        // The rows' list, and the names of its rows.
        function listOf(menu) {
            for (var i = 0; i < menu.children.length; i++) {
                if (menu.children[i].currentIndex !== undefined && menu.children[i].model !== undefined)
                    return menu.children[i]
            }
            return null
        }
        function names(menu) {
            return listOf(menu).model.map(function(row) { return row.name })
        }
        function cursor(menu) {
            var list = listOf(menu)
            return list.model[list.currentIndex].name
        }
        function hint(menu) {
            for (var i = 0; i < menu.children.length; i++) {
                if (menu.children[i].text !== undefined && String(menu.children[i].text).indexOf(":SETTINGS") >= 0)
                    return menu.children[i].text
            }
            return ""
        }

        // Standard: every module's row in the order they come, then, after a
        // gap the cursor passes over, a row for Settings; select opens a row,
        // and back opens Settings too.
        function test_standard() {
            var menu = open()
            compare(names(menu), ["Arcade", "High Scores", "Local Files", "Plex", "Scripts", "", "Settings"])
            verify(!menu.camcorder)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(root.navigations[0].path, "modules/highscores/views/Root.qml")
            keyClick(Qt.Key_Escape)
            compare(root.navigations[1].path, "views/Settings.qml")
            verify(hint(menu).indexOf("GROUPS") < 0)
            // With no Back to press: up from the first row, round to
            // Settings' row, over the gap, and select.
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(cursor(menu), "Settings")
            keyClick(Qt.Key_Return)
            compare(root.navigations[2].path, "views/Settings.qml")
            keyClick(Qt.Key_Down)
            compare(cursor(menu), "Arcade")
        }

        // Camcorder: the groups' icons, the rows of the one the cursor is
        // on beside them, the keys in the rows; ◄ to the icons, ▲ ▼ from
        // group to group, ► into its rows, select opening a module and
        // keeping where it was.
        function test_camcorder() {
            root.menuLayout = "Camcorder"
            var menu = open()
            compare(menu.groups.map(function(g) { return g.key }), ["tv", "arcade", "", "settings"])
            compare(menu.groups.map(function(g) { return g.icon }), ["tv", "joystick", "apps", "gear"])
            compare(names(menu), ["Local Files", "Plex"])
            verify(!menu.groupIcons)
            // The column is the shell's (Main.qml's frame), handed the
            // groups' icons, the group and whether the keys are in it.
            verify(menu.inFrame && menu.ownsFrame)
            // Its rows too, for the column inside a module (the modules of
            // its group, by their logos).
            compare(root.menuModules.length, menu.modules.length)
            compare(root.frameIcons.map(function(icon) { return icon.icon }), ["tv", "joystick", "apps", "gear"])
            compare(root.frameIndex, 0)
            verify(!root.frameActive)
            verify(hint(menu).indexOf("[◄]:GROUPS") >= 0, hint(menu))
            keyClick(Qt.Key_Left)
            verify(menu.groupIcons)
            verify(root.frameActive)
            verify(hint(menu).indexOf("[►]:OPEN") >= 0, hint(menu))
            keyClick(Qt.Key_Down)
            compare(menu.groupIndex, 1)
            compare(root.frameIndex, 1)
            compare(names(menu), ["Arcade", "High Scores"])
            keyClick(Qt.Key_Right)
            verify(!menu.groupIcons)
            keyClick(Qt.Key_Down)
            compare(cursor(menu), "High Scores")
            keyClick(Qt.Key_Return)
            var went = root.navigations[0]
            compare(went.path, "modules/highscores/views/Root.qml")
            compare(went.listState.group, "arcade")
            // Back to it: the same row, in its group.
            loader.source = ""
            menu = open({ navListState: went.listState })
            compare(menu.groupIndex, 1)
            compare(cursor(menu), "High Scores")
            // The modules of no group under More; Settings' icon opens it,
            // the way back too.
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Down)
            compare(names(menu), ["Scripts"])
            keyClick(Qt.Key_Down)
            compare(menu.groups[menu.groupIndex].key, "settings")
            keyClick(Qt.Key_Return)
            compare(root.navigations[1].path, "views/Settings.qml")
            keyClick(Qt.Key_Escape)
            compare(root.navigations[2].path, "views/Settings.qml")
        }

        // The monitor layouts' frame: the shell's, which the menu's title
        // bar and hint bar speak to.
        function chromeOf(menu) {
            return root.chrome
        }

        // Studio Monitor: Camcorder's groups, their icons in the window's
        // strip, its tab naming the group.
        function test_studioMonitor() {
            root.menuLayout = "Studio Monitor"
            var menu = open()
            var chrome = chromeOf(menu)
            verify(chrome.visible)
            compare(chrome.icons, ["tv", "joystick", "apps", "gear"])
            compare(chrome.title, "TV / Movie")
            compare(names(menu), ["Local Files", "Plex"])
            keyClick(Qt.Key_Left)
            verify(chrome.iconsActive)
            keyClick(Qt.Key_Down)
            compare(chrome.title, "Arcade")
            compare(names(menu), ["Arcade", "High Scores"])
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            compare(root.navigations[0].path, "modules/arcade/views/Root.qml")
        }

        // Broadcast: every module's row, then a gap the cursor passes over
        // and a row for Settings.
        function test_broadcast() {
            root.menuLayout = "Broadcast"
            var menu = open()
            compare(chromeOf(menu).title, "OSD/OS")
            compare(names(menu), ["Arcade", "High Scores", "Local Files", "Plex", "Scripts", "", "Settings"])
            keyClick(Qt.Key_Up)
            compare(cursor(menu), "Settings")
            keyClick(Qt.Key_Up)
            compare(cursor(menu), "Scripts")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(root.navigations[0].path, "views/Settings.qml")
            keyClick(Qt.Key_Escape)
            compare(root.navigations[1].path, "views/Settings.qml")
        }

        // Flat Panel: the rows a page at a time.
        function test_flatPanel() {
            root.menuLayout = "Flat Panel"
            var menu = open()
            var chrome = chromeOf(menu)
            var list = listOf(menu)
            var per = Math.floor(list.height / chrome.rowHeight + 0.001)
            compare(chrome.pages, Math.ceil(5 / per))
            compare(chrome.page, 0)
            keyClick(Qt.Key_Up)
            compare(cursor(menu), "Scripts")
            compare(chrome.page, chrome.pages - 1)
            verify(list.contentY <= list.originY + list.contentHeight - list.height + 0.5)
            keyClick(Qt.Key_Return)
            compare(root.navigations[0].path, "modules/scripts/views/Root.qml")
        }

        // The first row's name, too long for the row, scrolls as far as it
        // overflows and rests there: as the menu is first laid out, and
        // again once the row is laid out another width.
        function textIn(item, text) {
            if (item.text === text)
                return item
            for (var i = 0; i < item.children.length; i++) {
                var found = textIn(item.children[i], text)
                if (found)
                    return found
            }
            return null
        }
        function scrollsAsFarAsItOverflows(label) {
            // How far it overflows the row as it is laid out by then.
            var distance = function() { return label.parent.width - label.implicitWidth }
            // From the start (started over a turn later), to the far end.
            tryVerify(function() { return label.x === 0 })
            tryVerify(function() { return label.x <= distance() + 0.5 }, 6000)
            wait(200)
            fuzzyCompare(label.x, distance(), 0.5)
            verify(distance() < -10, "the name is too long for its row: " + distance())
        }
        function test_longNameScrollsAsFarAsItOverflows() {
            appCore.firstName = "A module whose name runs past the row"
            root.width = 600
            var menu = open()
            compare(cursor(menu), appCore.firstName)
            var label = textIn(listOf(menu).itemAtIndex(0), appCore.firstName)
            verify(label !== null)
            scrollsAsFarAsItOverflows(label)
            root.width = 640
            scrollsAsFarAsItOverflows(label)
        }

        // Menu Layout changed while the menu is open: the cursor stays on its
        // row, in its group in Camcorder.
        function test_layoutChanged() {
            var menu = open()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(cursor(menu), "Plex")
            root.menuLayout = "Camcorder"
            tryVerify(function() { return names(menu).length === 2 })
            compare(names(menu), ["Local Files", "Plex"])
            compare(cursor(menu), "Plex")
            root.menuLayout = "Standard"
            tryVerify(function() { return names(menu).length === 7 })
            compare(cursor(menu), "Plex")
        }
    }
}
