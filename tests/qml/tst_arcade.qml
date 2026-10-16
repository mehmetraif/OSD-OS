import QtQuick
import QtTest

// The Arcade module's views, modules/arcade/views/Games.qml (the original
// screen), ArcadeScreen.qml and Play.qml, against a stand-in for
// ArcadeBackend: a game chosen in the tree, the startup favourite played at
// once, and a game's screen, from how to come back (player 1's own buttons)
// to MAME having the screen, every key kept from the menus meanwhile, and
// back to the list, or why it didn't play. The Arcade screen: its filters
// and pages, a game's details and high scores, COIN, select (let go or
// held) and START, the selected name scrolling, the room of its hint bar
// and help line, and the credits going in with the game. The module's
// router, Root.qml: a game played straight from the list with Start Info
// Screen off, every key kept from it meanwhile, back on it after, or why it
// didn't play.
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
                           browse: "[►]", select: "[ENTER]", play_pause: "[SPACE]", coin: "[5]" })
    function selectorShown(item, shown) {}
    function coverShown(item, shown) {}
    function skinIcon(url) { return url }
    // Main.qml's, for Root.qml's views: the new one, without a transition.
    function changeWindow(loader, source, properties) { loader.setSource(source, properties) }

    // The module's router, as the views read it.
    QtObject {
        id: moduleRoot
        property string moduleId: "com.osdos.arcade"
        property string moduleName: "Arcade"
        property string moduleIcon: ""
        property string screen: "Original"
        readonly property bool arcadeScreen: screen === "Arcade"
        property string startFilter: "all"
        property int credits: 0
        readonly property int maxCredits: 9
        function addCredit() { credits = Math.min(maxCredits, credits + 1) }
        function startGame(set) {
            if (!arcadeBackend.play(set, credits))
                return false
            credits = 0
            return true
        }
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
        property var credited: []
        property var entries: ({})
        // The Arcade screen's: the filters in order, each { id, name, sets },
        // what each game is, and the favourites.
        property var filterList: []
        property var gameInfo: ({})
        property var favorites: []
        signal finished(int exitCode, string reason, string notice)
        signal entriesReady(string path)
        signal gamesChanged()
        function gamesFolder() { return "/media/OSD-OS/Games/Arcade" }
        function refresh() {}
        function requestEntries(path) { return entries[path] || [] }
        function setsOf(id) {
            for (var i = 0; i < filterList.length; ++i) {
                if (filterList[i].id === id)
                    return filterList[i].sets
            }
            return []
        }
        function filters() {
            return filterList.map(function(f) { return { id: f.id, name: f.name, count: f.sets.length } })
        }
        function filterGames(id, first, count) {
            return setsOf(id).slice(first, first + count).map(function(set) {
                return { set: set, name: gameInfo[set] ? gameInfo[set].title : set,
                         favorite: favorites.indexOf(set) >= 0 }
            })
        }
        function filterIndex(id, set) { return setsOf(id).indexOf(set) }
        function gameDetails(set) { return gameInfo[set] || ({}) }
        function play(name, credits) {
            started = started.concat([name])
            credited = credited.concat([credits || 0])
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

    // The High Scores module's backend, as the Arcade screen asks it: its
    // tables once it has read what it keeps.
    QtObject {
        id: highScoresBackend
        property bool reading: false
        property bool loaded: false
        property int refreshes: 0
        property var tables: ({})
        function refresh() { refreshes++; loaded = true }
        function topEntries(set, count) { return loaded ? (tables[set] || []).slice(0, count) : [] }
    }

    QtObject {
        id: idleTracker
        property int resets: 0
        function resetActivity() { resets++ }
    }

    // AppCore's lists and settings, for Recently Played and the options, and
    // the module's settings as Root.qml reads them.
    QtObject {
        id: appCore
        property var added: []
        property var settings: ({})
        function get_settings() { return { modules: { "com.osdos.arcade": settings } } }
        function get_module_info(id) { return { name: "Arcade", icon: "" } }
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
            moduleRoot.screen = "Original"
            root.hintBar = true
            root.helpLine = true
            moduleRoot.startFilter = "all"
            moduleRoot.credits = 0
            arcadeBackend.credited = []
            arcadeBackend.favorites = ["galaga", "sf2"]
            arcadeBackend.filterList = [
                { id: "all", name: "All Games",
                  sets: ["sf2", "1942", "avsp", "bublbobl", "dkong", "ffight", "galaga", "kinst", "mslug",
                         "pacman", "rtype", "tmnt", "xmen6p", "zerowing", "dino"] },
                { id: "favorites", name: "Favorites", sets: ["galaga", "sf2"] },
                { id: "genres/Fighter", name: "Fighter", sets: ["kinst", "sf2"] },
                { id: "players/6", name: "6 Players", sets: ["xmen6p"] }
            ]
            arcadeBackend.gameInfo = {
                "sf2": { set: "sf2", title: "Street Fighter II", name: "Street Fighter II: The World Warrior (World 910522)",
                         system: "CPS-1", maker: "Capcom", year: "1991", players: 2, playTime: 5400, picture: "",
                         vertical: false },
                "1942": { set: "1942", title: "1942", name: "1942 (Revision B)", system: "1942", maker: "Capcom",
                          year: "1984", players: 2, playTime: 0, picture: "", vertical: true },
                "galaga": { set: "galaga", title: "Galaga", name: "Galaga (Namco rev. B)", system: "GALAGA",
                            maker: "Namco", year: "1981", players: 2, playTime: 59, picture: "", vertical: true }
            }
            highScoresBackend.refreshes = 0
            highScoresBackend.loaded = false
            highScoresBackend.tables = {
                "sf2": [{ rank: "1ST", score: "110,000", name: "RYU" }, { rank: "2ND", score: "90,000", name: "KEN" },
                        { rank: "3RD", score: "70,000", name: "CHN" }, { rank: "4TH", score: "50,000", name: "GUY" },
                        { rank: "5TH", score: "30,000", name: "DAN" }]
            }
            idleTracker.resets = 0
            appCore.added = []
            appCore.settings = {}
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

        // The text item showing this, or with exact, showing it alone.
        function findText(item, text, exact) {
            if (item.text !== undefined && item.visible
                    && (exact ? String(item.text) === text : String(item.text).indexOf(text) >= 0))
                return item
            for (var i = 0; i < item.children.length; i++) {
                var found = findText(item.children[i], text, exact)
                if (found)
                    return found
            }
            return null
        }

        // A game chosen in the tree plays.
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

        // ── The Arcade screen ────────────────────────────────────────────

        function openScreen(params) {
            moduleRoot.screen = "Arcade"
            return open("ArcadeScreen.qml", params)
        }

        // A filter's page on the arc, seven games with both bars, the filter
        // and the page in the bar, the game's high scores and play time in
        // the column, its full name and what it is under the list, and the
        // screen's own hints; no letters smaller than the hint bar's.
        function test_screen() {
            var screen = openScreen()
            compare(screen.filterId, "all")
            compare(screen.perPage, 7)
            compare(screen.pages, 3)
            compare(screen.currentSet, "sf2")
            verify(findText(screen, "All Games") !== null)
            verify(findText(screen, "1/3") !== null)
            verify(findText(screen, "007") !== null)
            compare(findText(screen, "008"), null)
            verify(findText(screen, "1H 30M") !== null)
            compare(screen.textSize, 18)
            compare(screen.nameSize, 20)
            // Four high scores, with the hint bar and the help line there,
            // the High Scores module's tables read first.
            compare(screen.scores.length, 4)
            verify(findText(screen, "110,000") !== null)
            verify(findText(screen, "50,000") !== null)
            compare(findText(screen, "30,000"), null)
            verify(findText(screen, "Street Fighter II: The World Warrior (World 910522)  •  CPS-1  •  Capcom  •  1991  •  2 Players") !== null)
            verify(findText(screen, "[ESC]:BACK [5]:COIN [ENTER]:PLAY HOLD:OPTIONS") !== null)
            verify(highScoresBackend.refreshes > 0)
            // A game not played, with no table.
            keyClick(Qt.Key_Down)
            compare(screen.currentSet, "1942")
            verify(findText(screen, "No Scores Yet") !== null)
            verify(findText(screen, "Never") !== null)
            // Under a minute played is a minute.
            keyClick(Qt.Key_Right)
            compare(screen.currentSet, "galaga")
            verify(findText(screen, "1M") !== null)
        }

        // ▲ ▼ from game to game and page to page, round; ◄ ► round the
        // filters, the game kept where the next has it.
        function test_screenMove() {
            var screen = openScreen()
            keyClick(Qt.Key_Up)
            compare(screen.selected, 14)
            compare(screen.page, 2)
            verify(findText(screen, "3/3") !== null)
            verify(findText(screen, "015") !== null)
            keyClick(Qt.Key_Down)
            compare(screen.selected, 0)
            for (var i = 0; i < 10; ++i)
                keyClick(Qt.Key_Down)
            compare(screen.page, 1)
            compare(screen.currentSet, "rtype")
            // Not in Favorites: its first.
            keyClick(Qt.Key_Right)
            compare(screen.filterId, "favorites")
            compare(screen.currentSet, "galaga")
            keyClick(Qt.Key_Down)
            compare(screen.currentSet, "sf2")
            // In Fighter too: kept.
            keyClick(Qt.Key_Right)
            compare(screen.filterId, "genres/Fighter")
            compare(screen.currentSet, "sf2")
            verify(findText(screen, "Fighter") !== null)
            // Not in 6 Players: its first, kept round in All Games.
            keyClick(Qt.Key_Right)
            compare(screen.filterId, "players/6")
            compare(screen.currentSet, "xmen6p")
            keyClick(Qt.Key_Right)
            compare(screen.filterId, "all")
            compare(screen.currentSet, "xmen6p")
            keyClick(Qt.Key_Left)
            compare(screen.filterId, "players/6")
        }

        // COIN (MAME's 5, which a pad's Back button is here: takesCoin) buys
        // a credit, nine at most, and doesn't leave; START plays the game,
        // from where the list is; back leaves.
        function test_screenCoinStart() {
            var screen = openScreen()
            verify(screen.takesCoin)
            keyClick(Qt.Key_5)
            compare(moduleRoot.credits, 1)
            verify(findText(screen, "Credit 1") !== null)
            keyClick(Qt.Key_5)
            compare(moduleRoot.credits, 2)
            compare(root.backs, 0)
            for (var i = 0; i < 10; ++i)
                keyClick(Qt.Key_5)
            compare(moduleRoot.credits, 9)
            keyClick(Qt.Key_Space)
            compare(root.navigations.length, 1)
            var nav = root.navigations[0]
            compare(nav.path, "Play.qml")
            compare(nav.params.game, "sf2")
            compare(nav.params.title, "Street Fighter II: The World Warrior (World 910522)")
            compare(nav.listState.filter, "all")
            compare(nav.listState.set, "sf2")
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // Select plays the game as it is let go; held, it opens the game's
        // options, Play first, and plays nothing as it is let go. Only a press
        // the screen saw counts, and a held select is select alone.
        function test_screenSelect() {
            var screen = openScreen()
            keyPress(Qt.Key_Return)
            compare(root.navigations.length, 0)
            wait(300)
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].params.game, "sf2")
            compare(findText(screen, "Add to Favorites"), null)
            // A release without its press.
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 1)
            // Another key while it is held.
            keyPress(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            wait(1000)
            compare(findText(screen, "Add to Favorites"), null)
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(screen.currentSet, "1942")
            // Held.
            keyClick(Qt.Key_Up)
            keyPress(Qt.Key_Return)
            tryVerify(function() { return findText(screen, "Add to Favorites") !== null }, 1500)
            verify(findText(screen, "Play at Startup") !== null)
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 1)
            // A pad's Back button is Back in them, and keys they leave don't
            // buy credits behind them.
            verify(!screen.takesCoin)
            keyClick(Qt.Key_5)
            compare(moduleRoot.credits, 0)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 2)
            compare(root.navigations[1].params.game, "sf2")
        }

        // The selected game's name, too long for its row, scrolls through;
        // the others' are cut short.
        function test_screenScroll() {
            var screen = openScreen()
            var name = findText(screen, "Street Fighter II", true)
            verify(name !== null)
            compare(name.x, 0)
            tryVerify(function() { return name.x < 0 }, 4000)
            keyClick(Qt.Key_Down)
            tryCompare(name, "x", 0)
            verify(name.width < name.implicitWidth)
            wait(2000)
            compare(name.x, 0)
        }

        // The hint bar's and the help line's room, when they are off, goes to
        // the list, a row more each, and to the high scores.
        function test_screenRoom() {
            root.hintBar = false
            var screen = openScreen()
            compare(screen.perPage, 8)
            compare(screen.scoreRows, 5)
            loader.source = ""
            root.hintBar = true
            root.helpLine = false
            screen = openScreen()
            compare(screen.perPage, 8)
            compare(screen.scoreRows, 5)
            loader.source = ""
            root.hintBar = false
            screen = openScreen()
            compare(screen.perPage, 9)
            compare(screen.pages, 2)
            compare(screen.scores.length, 5)
            verify(findText(screen, "30,000") !== null)
        }

        // Nothing to list: what stands in the way, or that the games are
        // read; the list as soon as they have been.
        function test_screenEmpty() {
            arcadeBackend.filterList = []
            var screen = openScreen()
            verify(findText(screen, "No games found") !== null)
            verify(findText(screen, "Put MAME's ROM sets") !== null)
            arcadeBackend.scanning = true
            verify(findText(screen, "Reading the games") !== null)
            compare(findText(screen, "Put MAME's ROM sets"), null)
            arcadeBackend.scanning = false
            arcadeBackend.filterList = [{ id: "all", name: "All Games", sets: ["sf2", "galaga"] }]
            arcadeBackend.gamesChanged()
            compare(screen.filterId, "all")
            compare(screen.currentSet, "sf2")
            compare(findText(screen, "No games found"), null)
        }

        // Start Filter's, or back from a game, the filter and the game it was
        // on; a filter that isn't there, All Games.
        function test_screenOpensOn() {
            moduleRoot.startFilter = "genres/Fighter"
            var screen = openScreen()
            compare(screen.filterId, "genres/Fighter")
            loader.source = ""
            moduleRoot.startFilter = "genres/Nothing"
            screen = openScreen()
            compare(screen.filterId, "all")
            loader.source = ""
            screen = openScreen({ navListState: { filter: "favorites", set: "sf2" } })
            compare(screen.filterId, "favorites")
            compare(screen.currentSet, "sf2")
        }

        // A Start Filter with no games once MAME has answered gives way to
        // the one shown for good: a favourite added later doesn't take the
        // list from it. While MAME is asked, it is waited for.
        function test_screenFilterGone() {
            var all = arcadeBackend.filterList
            moduleRoot.startFilter = "favorites"
            arcadeBackend.filterList = all.filter(function(f) { return f.id !== "favorites" })
            var screen = openScreen()
            compare(screen.filterId, "all")
            keyClick(Qt.Key_Down)
            compare(screen.currentSet, "1942")
            arcadeBackend.filterList = all
            arcadeBackend.gamesChanged()
            compare(screen.filterId, "all")
            compare(screen.currentSet, "1942")
            loader.source = ""
            moduleRoot.startFilter = "genres/Fighter"
            arcadeBackend.scanning = true
            arcadeBackend.filterList = all.filter(function(f) { return f.id === "favorites" })
            screen = openScreen()
            compare(screen.filterId, "favorites")
            arcadeBackend.scanning = false
            arcadeBackend.filterList = all
            arcadeBackend.gamesChanged()
            compare(screen.filterId, "genres/Fighter")
        }

        // The startup favourite plays at once, as if chosen in Favorites.
        function test_screenStartup() {
            openScreen({ startupPlay: { name: "Street Fighter II", path: "game:sf2" } })
            tryVerify(function() { return root.navigations.length === 1 })
            var nav = root.navigations[0]
            compare(nav.params.game, "sf2")
            compare(nav.listState.filter, "favorites")
        }

        // From the Arcade screen: the credits going in, COIN for another and
        // START to play at once; MAME given them, and they are spent.
        function test_playArcade() {
            moduleRoot.screen = "Arcade"
            moduleRoot.credits = 2
            var play = open("Play.qml", { game: "sf2", title: "Street Fighter II" })
            verify(findText(play, "Press COIN + START together") !== null)
            verify(findText(play, "2 credits go in as the game starts") !== null)
            verify(play.takesCoin)
            keyClick(Qt.Key_5)
            verify(findText(play, "3 credits go in as the game starts") !== null)
            keyClick(Qt.Key_5)
            compare(moduleRoot.credits, 4)
            compare(play.phase, "opening")
            compare(root.backs, 0)
            keyClick(Qt.Key_Space)
            compare(play.phase, "running")
            verify(!play.takesCoin)
            compare(arcadeBackend.started, ["sf2"])
            compare(arcadeBackend.credited, [4])
            compare(moduleRoot.credits, 0)
        }

        // Opened for why a game played from the list didn't (Start Info
        // Screen off): that, at once, and nothing started.
        function test_playWhyNot() {
            moduleRoot.screen = "Arcade"
            var play = open("Play.qml", { game: "galaga", title: "Galaga", error: "MAME is not installed" })
            compare(play.phase, "error")
            verify(findText(play, "Could not play Galaga") !== null)
            verify(findText(play, "sudo apt install mame") !== null)
            compare(findText(play, "Press COIN + START together"), null)
            loader.source = ""
            play = open("Play.qml", { game: "mslug", title: "Metal Slug", failed: true,
                                      message: "Missing sp-s2.sp1", output: "sp-s2.sp1 NOT FOUND" })
            compare(play.phase, "failed")
            verify(findText(play, "Missing sp-s2.sp1") !== null)
            verify(findText(play, "sp-s2.sp1 NOT FOUND") !== null)
            wait(3500)
            compare(arcadeBackend.started, [])
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // On, from the Arcade screen, every game waits for it to be read.
        function test_playArcadeWaits() {
            moduleRoot.screen = "Arcade"
            var play = open("Play.qml", { game: "sf2", title: "Street Fighter II" })
            wait(1500)
            compare(play.phase, "opening")
            tryCompare(play, "phase", "running", 3000)
        }

        // ── The module's router, Root.qml ────────────────────────────────

        // Root.qml as the module opens, with these settings of its own; and
        // the view it shows.
        function openRoot(settings) {
            appCore.settings = settings
            return open("Root.qml")
        }
        function shown(router) {
            for (var i = 0; i < router.children.length; ++i) {
                if (router.children[i].source !== undefined)
                    return router.children[i].item
            }
            return null
        }

        // Start Info Screen off: select plays the game straight from the
        // list, on Recently Played, with the credits bought. While it runs,
        // every key is kept from the list and the screen saver held off; as
        // it ends the list has the keys again, the game still selected.
        function test_rootStartInfoOff() {
            var router = openRoot({ screen: "Arcade", start_info: false })
            var screen = shown(router)
            compare(screen.currentSet, "sf2")
            keyClick(Qt.Key_5)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.started, ["sf2"])
            compare(arcadeBackend.credited, [1])
            compare(router.credits, 0)
            compare(appCore.added, [["com.osdos.arcade", "recent", "game:sf2"]])
            compare(shown(router), screen)
            compare(router.playing.game, "sf2")
            tryVerify(function() { return idleTracker.resets > 0 })
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Escape)
            keyClick(Qt.Key_Space)
            compare(screen.selected, 0)
            compare(root.backs, 0)
            compare(arcadeBackend.started, ["sf2"])
            arcadeBackend.running = false
            arcadeBackend.finished(0, "ok", "")
            compare(router.playing, null)
            compare(shown(router), screen)
            compare(screen.currentSet, "sf2")
            keyClick(Qt.Key_Down)
            compare(screen.currentSet, "1942")
            // The original screen's tree too.
            loader.source = ""
            arcadeBackend.started = []
            router = openRoot({ start_info: false })
            var games = shown(router)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.started, ["galaga"])
            compare(shown(router), games)
            arcadeBackend.finished(0, "ok", "")
            compare(router.playing, null)
        }

        // A game MAME ended with an error, or one that never started, says
        // why on Play.qml; back from it lands on the game in the list.
        function test_rootWhyNot() {
            var router = openRoot({ screen: "Arcade", start_info: false })
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.started, ["1942"])
            arcadeBackend.running = false
            arcadeBackend.finished(2, "failed", "Missing 1942.zip")
            var play = shown(router)
            compare(play.phase, "failed")
            verify(findText(play, "Missing 1942.zip") !== null)
            verify(findText(play, "NOT FOUND (tried in mslug neogeo)") !== null)
            keyClick(Qt.Key_Escape)
            var screen = shown(router)
            compare(screen.currentSet, "1942")
            compare(root.backs, 0)
            arcadeBackend.playResult = false
            arcadeBackend.error = "MAME is not installed"
            keyClick(Qt.Key_Return)
            play = shown(router)
            compare(play.phase, "error")
            compare(router.playing, null)
            verify(findText(play, "sudo apt install mame") !== null)
        }

        // Start Info Screen on: how to come back first.
        function test_rootStartInfoOn() {
            var router = openRoot({ screen: "Arcade" })
            keyClick(Qt.Key_Return)
            var play = shown(router)
            compare(play.game, "sf2")
            compare(play.phase, "opening")
            compare(arcadeBackend.started, [])
            compare(appCore.added, [["com.osdos.arcade", "recent", "game:sf2"]])
        }
    }
}
