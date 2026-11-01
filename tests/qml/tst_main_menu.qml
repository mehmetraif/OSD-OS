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
    property string menuLayout: "Standard"
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
            verify(hint(menu).indexOf("[◄]:GROUPS") >= 0, hint(menu))
            keyClick(Qt.Key_Left)
            verify(menu.groupIcons)
            verify(hint(menu).indexOf("[►]:OPEN") >= 0, hint(menu))
            keyClick(Qt.Key_Down)
            compare(menu.groupIndex, 1)
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

        // Menu Layout changed while the menu is open: the cursor stays on its
        // row, in its group in Camcorder.
        function test_layoutChanged() {
            var menu = open()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(cursor(menu), "Plex")
            root.menuLayout = "Camcorder"
            compare(names(menu), ["Local Files", "Plex"])
            compare(cursor(menu), "Plex")
            root.menuLayout = "Standard"
            compare(names(menu).length, 5)
            compare(cursor(menu), "Plex")
        }
    }
}
