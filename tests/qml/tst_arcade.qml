import QtQuick
import QtTest

// The Arcade module's views, modules/arcade/views/Games.qml and Play.qml,
// against a stand-in for ArcadeBackend: a game chosen in the tree, the
// startup favourite played at once, and a game's screen, from how to come
// back (player 1's own buttons) to MAME having the screen, every key kept
// from the menus meanwhile, and back to the list, or why it didn't play.
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
    property color secondaryColor: "white"
    property color tertiaryColor: "white"
    property color accentColor: "white"
    property color surfaceColor: "black"
    property var skin: ({})
    property bool hintBar: true
    property bool helpLine: true
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

    // The module's router, as the views read it.
    QtObject {
        id: moduleRoot
        property string moduleId: "com.osdos.arcade"
        property string moduleName: "Arcade"
        property string moduleIcon: ""
    }

    function game(set, name, detail) {
        return { name: name, path: "game:" + set, isFolder: false, game: set, detail: detail }
    }

    // ArcadeBackend as the views use it, and a note of every game started.
    QtObject {
        id: arcadeBackend
        property bool running: false
        property bool played: true
        property bool scanning: false
        property string problem: ""
        property bool folderReadable: true
        property bool playResult: true
        property string error: ""
        property var started: []
        property var entries: ({})
        signal finished(int exitCode, string reason, string notice)
        signal entriesReady(string path)
        function gamesFolder() { return "/media/OSD-OS/Games/Arcade" }
        function refresh() {}
        function requestEntries(path) { return entries[path] || [] }
        function play(name) {
            started = started.concat([name])
            running = playResult
            return playResult
        }
        function lastError() { return error }
        function output() { return "sp-s2.sp1 NOT FOUND (tried in mslug neogeo)" }
    }

    // InputManager: player 1's pad, a JAMMA stick, and its buttons' labels.
    QtObject {
        id: inputManager
        property var players: [{ player: 1, key: "", name: "OSD/OS JAMMA P1", playing: "pad-0600aaaa", connected: true }]
        function buttonLabel(key, token) {
            if (key !== "pad-0600aaaa") return ""
            return token === "back" ? "COIN" : token === "start" ? "START" : ""
        }
    }

    QtObject {
        id: idleTracker
        property int resets: 0
        function resetActivity() { resets++ }
    }

    // AppCore's lists and settings, for Recently Played and the options.
    QtObject {
        id: appCore
        property var added: []
        function add_to_list(module, list, entry, limit) { added = added.concat([[module, list, entry.path]]) }
        function list_contains(module, list, path) { return false }
        function get_setting(module, key) { return undefined }
        function save_setting(module, key, value) {}
        function is_module_enabled(id) { return false }
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }
    property var navigations: []
    property int backs: 0
    Connections {
        target: loader.item
        ignoreUnknownSignals: true
        function onNavigateTo(path, params, listState) {
            root.navigations = root.navigations.concat([{ path: path, params: params, listState: listState }])
        }
        function onGoBack() { root.backs++ }
    }

    TestCase {
        name: "ArcadeViews"
        when: windowShown

        function init() {
            arcadeBackend.running = false
            arcadeBackend.played = true
            arcadeBackend.scanning = false
            arcadeBackend.problem = ""
            arcadeBackend.folderReadable = true
            arcadeBackend.playResult = true
            arcadeBackend.error = ""
            arcadeBackend.started = []
            arcadeBackend.entries = {
                "top": [{ name: "Recently Played", path: "recent", isFolder: true },
                        { name: "Favorites", path: "favorites", isFolder: true },
                        { name: "All Games", path: "all", isFolder: true }],
                "recent": [],
                "favorites": [root.game("pacman", "Pac-Man (Midway)", "1980\nNamco\n2 players\npacman.zip")],
                "all": [root.game("galaga", "Galaga (Namco rev. B)", "1981\nNamco\n2 players\ngalaga.zip"),
                        root.game("pacman", "Pac-Man (Midway)", "1980\nNamco\n2 players\npacman.zip")]
            }
            inputManager.players = [{ player: 1, key: "", name: "OSD/OS JAMMA P1", playing: "pad-0600aaaa", connected: true }]
            idleTracker.resets = 0
            appCore.added = []
            root.navigations = []
            root.backs = 0
        }

        function cleanup() {
            loader.source = ""
        }

        function open(view, params) {
            loader.setSource(Qt.resolvedUrl("../../modules/arcade/views/" + view), { navParams: params || {} })
            verify(loader.item !== null, "loaded " + view)
            loader.item.forceActiveFocus()
            return loader.item
        }

        function findText(item, text) {
            if (item.text !== undefined && String(item.text).indexOf(text) >= 0 && item.visible)
                return item
            for (var i = 0; i < item.children.length; i++) {
                var found = findText(item.children[i], text)
                if (found)
                    return found
            }
            return null
        }

        // A game chosen in the tree plays, and goes on Recently Played.
        function test_choose() {
            var games = open("Games.qml")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)   // All Games
            keyClick(Qt.Key_Right)  // into it, on Galaga
            // The line under the tree says what the game is.
            verify(findText(games, "1981") !== null)
            keyClick(Qt.Key_Down)   // Pac-Man
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            var nav = root.navigations[0]
            compare(nav.path, "Play.qml")
            compare(nav.params.game, "pacman")
            compare(nav.params.title, "Pac-Man (Midway)")
            // Back from the game lands where it was chosen.
            compare(nav.listState.trail[nav.listState.trail.length - 1].path, "all")
            compare(appCore.added, [["com.osdos.arcade", "recent", "game:pacman"]])
        }

        // What stands in the way comes first in the line under the tree.
        function test_problem() {
            arcadeBackend.problem = "MAME is not installed"
            var games = open("Games.qml")
            verify(findText(games, "MAME is not installed") !== null)
        }

        // An empty folder says where the games go; what stands in the way of
        // the list comes first, and a folder that can't be read is all.
        function test_empty() {
            arcadeBackend.entries = { "top": [] }
            var games = open("Games.qml")
            tryVerify(function() { return findText(games, "No games found") !== null })
            verify(findText(games, "Put MAME's ROM sets") !== null)
            arcadeBackend.problem = "MAME is not installed"
            verify(findText(games, "MAME is not installed") !== null)
            verify(findText(games, "Put MAME's ROM sets") !== null)
            arcadeBackend.problem = "The games folder can't be read: /media/usb/MAME"
            arcadeBackend.folderReadable = false
            verify(findText(games, "The games folder can't be read") !== null)
            compare(findText(games, "Put MAME's ROM sets"), null)
        }

        // A set that isn't a game says why.
        function test_unknown() {
            arcadeBackend.entries = {
                "top": [{ name: "Not Recognized", path: "unknown", isFolder: true }],
                "unknown": [{ name: "DKONG.ZIP", path: "unknown:dkong", isFolder: false, kind: "unknown",
                              detail: "MAME looks for dkong.zip, in lower case\nRename the file to dkong.zip" }]
            }
            var games = open("Games.qml")
            keyClick(Qt.Key_Right)
            tryVerify(function() { return findText(games, "MAME looks for dkong.zip") !== null })
        }

        // The startup favourite plays at once, from Favorites.
        function test_startup() {
            open("Games.qml", { startupPlay: { name: "Pac-Man (Midway)", path: "game:pacman" } })
            tryVerify(function() { return root.navigations.length === 1 })
            var nav = root.navigations[0]
            compare(nav.params.game, "pacman")
            compare(nav.listState.trail[1].path, "favorites")
        }

        // How to come back, then MAME has the screen and every key is kept
        // from the menus until the game ends, then back to the list.
        function test_play() {
            arcadeBackend.played = false
            var play = open("Play.qml", { game: "galaga", title: "Galaga (Namco rev. B)" })
            compare(play.exitKeys, "COIN + START")
            verify(findText(play, "Press COIN + START together") !== null)
            compare(play.phase, "opening")
            // The first game this run waits for that to be read.
            wait(500)
            compare(arcadeBackend.started, [])
            tryCompare(play, "phase", "running", 3000)
            compare(arcadeBackend.started, ["galaga"])
            tryVerify(function() { return idleTracker.resets > 0 })
            keyClick(Qt.Key_Escape)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(root.backs, 0)
            compare(play.phase, "running")
            arcadeBackend.running = false
            arcadeBackend.finished(0, "ok", "")
            compare(root.backs, 1)
        }

        // Without a controller, Esc.
        function test_keyboardOnly() {
            inputManager.players = []
            var play = open("Play.qml", { game: "galaga", title: "Galaga" })
            compare(play.exitKeys, "")
            verify(findText(play, "Press Esc on a keyboard") !== null)
        }

        // A game MAME ended with an error says why, and what MAME printed.
        function test_failed() {
            var play = open("Play.qml", { game: "mslug", title: "Metal Slug" })
            tryCompare(play, "phase", "running")
            arcadeBackend.running = false
            arcadeBackend.finished(2, "failed", "Missing sp-s2.sp1, looked for in mslug.zip, neogeo.zip")
            compare(play.phase, "failed")
            compare(root.backs, 0)
            verify(findText(play, "Missing sp-s2.sp1") !== null)
            verify(findText(play, "NOT FOUND (tried in mslug neogeo)") !== null)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // One that never started, and how to put MAME on.
        function test_error() {
            arcadeBackend.playResult = false
            arcadeBackend.error = "MAME is not installed"
            var play = open("Play.qml", { game: "galaga", title: "Galaga" })
            tryCompare(play, "phase", "error")
            verify(findText(play, "sudo apt install mame") !== null)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // Back before MAME starts starts nothing.
        function test_cancel() {
            arcadeBackend.played = false
            var play = open("Play.qml", { game: "galaga", title: "Galaga" })
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
            wait(1800)
            compare(arcadeBackend.started, [])
        }
    }
}
