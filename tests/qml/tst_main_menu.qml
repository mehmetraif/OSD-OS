import QtQuick
import QtTest
import OSDOS.Testing

// The main menu, views/ModuleList.qml, against a stand-in for AppCore: the
// modules turned on, a row each; select opens one, back opens Settings. With
// Settings → Look → Menu Layout on Camcorder, the rows are in groups by
// their module's settings group (TV / Movie, Arcade, More, then Settings),
// each an icon in a column at the left: ◄ goes to the icons, ▲ ▼ there from
// group to group, ► or select into the group's rows, Settings' opening it.
StandInRoot {
    id: root
    // White on black in every layout, and the frame's colours in force
    // whenever it is up.
    primaryColor: schemeInk
    surfaceColor: schemePaper
    cursorColor: primaryColor
    cursorInk: surfaceColor
    monitorInk: monitorFramed
    // The modules' rows, as the main menu hands them on.
    property var menuModules: []

    // AppCore as the main menu uses it: the modules turned on, as
    // scan_for_modules() sends them, each with its settings group.
    QtObject {
        id: appCore
        // The first row's name, which a test makes too long for the row.
        property string firstName: "Arcade"
        // The modules turned off, by their names: they have no row.
        property var turnedOff: []
        signal modulesLoaded(var modules)
        function scan_for_modules() {
            modulesLoaded([{ name: firstName, entry_point: "modules/arcade/views/Root.qml", group: "arcade" },
                           { name: "High Scores", entry_point: "modules/highscores/views/Root.qml", group: "arcade" },
                           { name: "Local Files", entry_point: "modules/local_files/views/Root.qml", group: "tv" },
                           { name: "Plex", entry_point: "modules/plex/views/Root.qml", group: "tv" },
                           { name: "Scripts", entry_point: "modules/scripts/views/Root.qml", group: "" }]
                          .filter(function(row) { return turnedOff.indexOf(row.name) < 0 }))
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
            appCore.turnedOff = []
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

        // The cursor never rests on the gap before Settings' row, where
        // select does nothing: back from Settings with the module it was on
        // turned off there (its place is now the gap's), and with no module
        // on at all, it is on Settings' row, which select opens. Standard
        // and Broadcast have the gap; Camcorder and Flat Panel have none,
        // and the cursor is on a row there too.
        function test_neverOnTheGap_data() {
            return [{ tag: "Standard", layout: "Standard", after: "Settings", none: "Settings" },
                    { tag: "Broadcast", layout: "Broadcast", after: "Settings", none: "Settings" },
                    { tag: "Camcorder", layout: "Camcorder", after: "Local Files", none: "Settings" },
                    { tag: "Flat Panel", layout: "Flat Panel", after: "Plex", none: "" }]
        }
        function test_neverOnTheGap(data) {
            root.menuLayout = data.layout
            var gapped = data.layout === "Standard" || data.layout === "Broadcast"
            var menu = open()
            if (data.layout === "Camcorder") {
                // To the last group but Settings', More: Scripts' own.
                keyClick(Qt.Key_Left)
                keyClick(Qt.Key_Down)
                keyClick(Qt.Key_Down)
                keyClick(Qt.Key_Right)
            } else {
                for (var i = 0; i < 4; i++)
                    keyClick(Qt.Key_Down)
            }
            compare(cursor(menu), "Scripts")
            keyClick(Qt.Key_Escape)
            compare(root.navigations[0].path, "views/Settings.qml")
            var left = root.navigations[0].listState

            // Scripts turned off in Settings, and back.
            loader.source = ""
            appCore.turnedOff = ["Scripts"]
            menu = open({ navListState: left })
            if (gapped)
                compare(names(menu), ["Arcade", "High Scores", "Local Files", "Plex", "", "Settings"])
            compare(cursor(menu), data.after)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 2)
            compare(root.navigations[1].path, gapped ? "views/Settings.qml"
                                                     : data.layout === "Camcorder" ? "modules/local_files/views/Root.qml"
                                                                                   : "modules/plex/views/Root.qml")

            // No module on.
            loader.source = ""
            appCore.turnedOff = ["Arcade", "High Scores", "Local Files", "Plex", "Scripts"]
            menu = open()
            if (data.none === "") {
                compare(names(menu), [])
                return
            }
            compare(names(menu), gapped ? ["", "Settings"] : ["Settings"])
            compare(cursor(menu), data.none)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 3)
            compare(root.navigations[2].path, "views/Settings.qml")
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
            // Its line, in the room it scrolls through (a ScrolledText's).
            label = label.textItem
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
