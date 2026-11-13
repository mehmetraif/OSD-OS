import QtQuick
import QtTest

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
        signal modulesLoaded(var modules)
        function scan_for_modules() {
            modulesLoaded([{ name: "Arcade", entry_point: "modules/arcade/views/Root.qml", group: "arcade" },
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

        // Standard: every module's row in the order they come; select opens
        // one, back opens Settings.
        function test_standard() {
            var menu = open()
            compare(names(menu), ["Arcade", "High Scores", "Local Files", "Plex", "Scripts"])
            verify(!menu.camcorder)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(root.navigations[0].path, "modules/highscores/views/Root.qml")
            keyClick(Qt.Key_Escape)
            compare(root.navigations[1].path, "views/Settings.qml")
            verify(hint(menu).indexOf("GROUPS") < 0)
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

        // The monitor layouts' frame.
        function chromeOf(menu) {
            for (var i = 0; i < menu.children.length; i++) {
                if (menu.children[i].guide !== undefined && menu.children[i].pageTo !== undefined)
                    return menu.children[i]
            }
            return null
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
            tryVerify(function() { return names(menu).length === 5 })
            compare(cursor(menu), "Plex")
        }
    }
}
