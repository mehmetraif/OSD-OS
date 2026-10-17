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
    // Colour names, as Main.qml has them.
    property string primaryColor: "white"
    property string secondaryColor: "white"
    property string tertiaryColor: "white"
    property string accentColor: "white"
    property string surfaceColor: "#000080"
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
    // Main.qml's, for Root.qml's views: the new one, without a transition;
    // and whether the effects rest for the module.
    function changeWindow(loader, source, properties) { loader.setSource(source, properties) }
    property bool effectsResting: false
    function restFor(layer, resting) { effectsResting = resting }

    // The module's router, as the views read it.
    QtObject {
        id: moduleRoot
        property string moduleId: "com.osdos.arcade"
        property string moduleName: "Arcade"
        property string moduleIcon: ""
        property string screen: "Original"
        readonly property bool arcadeScreen: screen === "Arcade"
        property string startFilter: "all"
        readonly property int handOverDelay: 150
        property int credits: 0
        readonly property int maxCredits: 9
        property int infoBackground: 100
        property string listStyle: "Curved"
        property bool border: true
        property var screenAdjustment: ({ top: 0, bottom: 0, left: 0, right: 0 })
        function addCredit() { credits = Math.min(maxCredits, credits + 1) }
        function startGame(set, title) {
            if (!arcadeBackend.play(set, credits))
                return false
            credits = 0
            appCore.add_to_list(moduleId, "recent", { name: title || set, path: "game:" + set, isFolder: false }, 30)
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
        property bool reading: false
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
        function gameDetails(set) {
            var details = gameInfo[set]
            return details ? Object.assign({}, details, { engine: engineOf(set) }) : ({})
        }
        function play(name, credits) {
            started = started.concat([name])
            credited = credited.concat([credits || 0])
            running = playResult
            return playResult
        }
        function lastError() { return error }
        function output() { return "sp-s2.sp1 NOT FOUND (tried in mslug neogeo)" }
        // The sets FinalBurn Neo has (fbneoSets), the engine each plays in
        // (engines: "fbneo" or "mame", Auto taking FinalBurn Neo's), and the
        // choices made.
        property var fbneoSets: []
        property var engines: ({})
        property var engineChoicesMade: []
        // FinalBurn Neo's page, and the RetroAchievements account's.
        property var fbneo: ({})
        property bool fbneoDownloading: false
        property int fbneoProgress: 0
        property string fbneoMessage: ""
        // fbneo's own change signal stands for the backend's fbneoChanged().
        property int downloads: 0
        function fbneoInfo() { return fbneo }
        function fbneoLicenseText() { return "You may freely use, modify, and distribute FB Neo." }
        function downloadFbneo() { downloads++; fbneoDownloading = true; fbneoProgress = 40; fbneoChanged() }
        property string achievementsUser: ""
        property string achievementsState: "none"
        property string achievementsMessage: ""
        property bool achievementsKey: false
        property bool achievementsKeyRefused: false
        property var signIns: []
        property string keyGiven: ""
        signal achievementsChanged()
        // Each game's RetroAchievements leaderboard, by set, as leaderboard()
        // gives it ("off" for any other); changed in place, so the screen
        // reads it again only on leaderboardReady().
        property var boards: ({})
        signal leaderboardReady(string set)
        function leaderboard(set) { return (engineOf(set) === "fbneo" && boards[set]) || { state: "off" } }
        function setAchievementsKey(key) { keyGiven = key; achievementsKey = key !== ""; achievementsKeyRefused = false }
        function signInAchievements(user, password) {
            signIns = signIns.concat([user + ":" + password])
            achievementsUser = user
            achievementsState = "pending"
        }
        function signOutAchievements() { achievementsUser = ""; achievementsState = "none" }
        function engineOf(set) {
            return engines[set] || (fbneoSets.indexOf(set) >= 0 ? "fbneo" : "mame")
        }
        function engineChoices(set) {
            if (fbneoSets.indexOf(set) < 0)
                return [{ id: "auto", label: "Auto (MAME)", current: true }, { id: "mame", label: "MAME", current: false }]
            return [{ id: "auto", label: "Auto (FinalBurn Neo)", current: !engines[set] },
                    { id: "mame", label: "MAME", current: engines[set] === "mame" },
                    { id: "fbneo", label: "FinalBurn Neo", current: engines[set] === "fbneo" }]
        }
        function setEngine(set, engine) {
            engineChoicesMade = engineChoicesMade.concat([set + "=" + engine])
            var map = Object.assign({}, engines)
            if (engine === "auto")
                delete map[set]
            else
                map[set] = engine
            engines = map
        }
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
        property var lastAdded: null
        property var settings: ({})
        property var saved: ({})
        function get_settings() { return { modules: { "com.osdos.arcade": settings } } }
        function get_module_info(id) { return { name: "Arcade", icon: "" } }
        function add_to_list(module, list, entry, limit) {
            added = added.concat([[module, list, entry.path]])
            lastAdded = entry
        }
        function list_contains(module, list, path) { return false }
        function get_setting(module, key) { return undefined }
        function save_setting(module, key, value) {
            var all = Object.assign({}, saved)
            all[key] = value
            saved = all
            return true
        }
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
            arcadeBackend.reading = false
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
            moduleRoot.infoBackground = 100
            moduleRoot.listStyle = "Curved"
            moduleRoot.border = true
            moduleRoot.screenAdjustment = { top: 0, bottom: 0, left: 0, right: 0 }
            moduleRoot.credits = 0
            arcadeBackend.credited = []
            arcadeBackend.fbneoSets = []
            arcadeBackend.engines = {}
            arcadeBackend.engineChoicesMade = []
            arcadeBackend.fbneo = { retroarch: "/usr/bin/retroarch", core: "", coreDate: "", downloaded: false,
                                    games: 0, license: "", canDownload: true }
            arcadeBackend.fbneoDownloading = false
            arcadeBackend.fbneoProgress = 0
            arcadeBackend.fbneoMessage = ""
            arcadeBackend.downloads = 0
            arcadeBackend.achievementsUser = ""
            arcadeBackend.achievementsState = "none"
            arcadeBackend.achievementsMessage = ""
            arcadeBackend.achievementsKey = false
            arcadeBackend.achievementsKeyRefused = false
            arcadeBackend.signIns = []
            arcadeBackend.keyGiven = ""
            arcadeBackend.boards = {}
            arcadeBackend.favorites = ["galaga", "sf2"]
            arcadeBackend.filterList = [
                { id: "all", name: "All Games",
                  sets: ["sf2", "1942", "avsp", "bublbobl", "dkong", "ffight", "galaga", "kinst", "mslug",
                         "pacman", "rtype", "tmnt", "xmen6p", "zerowing", "dino"] },
                { id: "favorites", name: "Favorites", sets: ["galaga", "sf2"] },
                { id: "genres/Fighting", name: "Fighting", sets: ["kinst", "sf2"] },
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
            appCore.lastAdded = null
            appCore.settings = {}
            appCore.saved = {}
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
            var games = open("Games.qml", { startupPlay: { name: "Pac-Man (Midway)", path: "game:pacman" } })
            tryVerify(function() { return root.navigations.length === 1 })
            var nav = root.navigations[0]
            compare(nav.params.game, "pacman")
            compare(nav.listState.trail[1].path, "favorites")
            // The tree there as it plays, its line on the game.
            verify(findText(games, "1980") !== null)
        }

        // While the folder is read, the startup favourite waits for it, on
        // both screens; then it plays.
        function test_startupWaitsForFolder() {
            arcadeBackend.reading = true
            open("Games.qml", { startupPlay: { name: "Pac-Man (Midway)", path: "game:pacman" } })
            wait(100)
            compare(root.navigations, [])
            arcadeBackend.reading = false
            tryVerify(function() { return root.navigations.length === 1 })
            compare(root.navigations[0].params.game, "pacman")
            loader.source = ""
            root.navigations = []
            arcadeBackend.reading = true
            openScreen({ startupPlay: { name: "Street Fighter II", path: "game:sf2" } })
            wait(100)
            compare(root.navigations, [])
            arcadeBackend.reading = false
            tryVerify(function() { return root.navigations.length === 1 })
            compare(root.navigations[0].params.game, "sf2")
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
        // and the page in the bar, the game's high scores, maker and play time
        // in the box under its picture, its full name and the rest of what it
        // is under the list, and the screen's own hints; no letters smaller
        // than the hint bar's.
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
            // Three high scores, the High Scores module's tables read first.
            compare(screen.scores.length, 3)
            verify(findText(screen, "110,000") !== null)
            verify(findText(screen, "70,000") !== null)
            compare(findText(screen, "50,000"), null)
            compare(screen.detailRows, 1)
            verify(findText(screen, "Maker") !== null)
            verify(findText(screen, "Capcom", true) !== null)
            verify(findText(screen, "Street Fighter II: The World Warrior (World 910522)  •  1991  •  2 Players  •  CPS-1") !== null)
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
            // In Fighting too: kept.
            keyClick(Qt.Key_Right)
            compare(screen.filterId, "genres/Fighting")
            compare(screen.currentSet, "sf2")
            verify(findText(screen, "Fighting") !== null)
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

        // A game FinalBurn Neo plays takes no credits bought (COIN), and
        // shows none; its options offer its engine, Auto naming FinalBurn
        // Neo, and MAME chosen there is saved for it, which takes credits.
        function test_screenFbneoGame() {
            arcadeBackend.fbneoSets = ["sf2"]
            var screen = openScreen()
            compare(screen.currentSet, "sf2")
            verify(screen.fbneo)
            keyClick(Qt.Key_5)
            compare(moduleRoot.credits, 0)
            compare(findText(screen, "Credit 0"), null)
            keyPress(Qt.Key_Return)
            tryVerify(function() { return findText(screen, "Engine: Auto (FinalBurn Neo)") !== null }, 1500)
            keyRelease(Qt.Key_Return)
            // The last of them, round from Play.
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            // The chooser's answers as its list lays them out.
            tryVerify(function() { return findText(screen, "FinalBurn Neo", true) !== null }, 1000)
            verify(findText(screen, "Auto (FinalBurn Neo)", true) !== null)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.engineChoicesMade, ["sf2=mame"])
            verify(!screen.fbneo)
            keyClick(Qt.Key_5)
            compare(moduleRoot.credits, 1)
            verify(findText(screen, "Credit 1") !== null)
            // A game FinalBurn Neo hasn't: no engine to choose.
            keyClick(Qt.Key_Down)
            compare(screen.currentSet, "1942")
            keyPress(Qt.Key_Return)
            tryVerify(function() { return findText(screen, "Add to Favorites") !== null }, 1500)
            keyRelease(Qt.Key_Return)
            compare(findText(screen, "Engine:"), null)
        }

        // FinalBurn Neo's page: RetroArch there, the core not; Download
        // starts it and shows how far it has got; once it is in, the core's
        // date, its games and its licence, a page of its own.
        function test_fbneoPage() {
            var page = open("Fbneo.qml")
            verify(findText(page, "Not Installed") !== null)
            verify(findText(page, "Download", true) !== null)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.downloads, 1)
            tryVerify(function() { return findText(page, "40%") !== null }, 1000)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.downloads, 1)
            arcadeBackend.fbneoDownloading = false
            arcadeBackend.fbneo = { retroarch: "/usr/bin/retroarch", core: "/data/arcade/retroarch/cores/fbneo_libretro.so",
                                    coreDate: "2026-10-17", downloaded: true, games: 9123,
                                    license: "/data/arcade/retroarch/fbneo-license.txt", canDownload: true }
            arcadeBackend.fbneoChanged()
            tryVerify(function() { return findText(page, "Downloaded 2026-10-17") !== null }, 1000)
            verify(findText(page, "9123") !== null)
            verify(findText(page, "Download Again") !== null)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryVerify(function() { return findText(page, "freely use, modify") !== null }, 1000)
            keyClick(Qt.Key_Escape)
            compare(findText(page, "freely use, modify"), null)
            compare(root.backs, 0)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // The account's page: the user and password typed (the password a
        // star a letter, both cases kept), Sign In hands them over for the
        // next game, and Sign Out brings them back.
        function test_accountPage() {
            var page = open("Account.qml")
            verify(findText(page, "None") !== null)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)   // User
            keyClick(Qt.Key_Return)
            tryVerify(function() { return findText(page, "RetroAchievements User") !== null }, 1000)
            for (var i = 0; i < "Player".length; ++i)
                keyClick("Player"[i])
            keyClick(Qt.Key_Return)
            tryVerify(function() { return findText(page, "Player", true) !== null }, 1000)
            keyClick(Qt.Key_Down)   // Password
            keyClick(Qt.Key_Return)
            for (var j = 0; j < "Se".length; ++j)
                keyClick("Se"[j])
            verify(findText(page, "**", true) !== null)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)   // Sign In
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.signIns, ["Player:Se"])
            tryVerify(function() { return findText(page, "At the Next Game") !== null }, 1000)
            verify(findText(page, "Sign Out", true) !== null)
            // Sign In gone, the cursor on the account's first line; the
            // password gone from the page.
            compare(page.keys, ["state", "status", "signOut", "key"])
            compare(page.typedPassword, "")
            // The key, kept by the backend and never shown; refused, the
            // page says so; forgotten, its line goes.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryVerify(function() { return findText(page, "Web API Key") !== null }, 1000)
            keyClick("A")
            keyClick("b")
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.keyGiven, "Ab")
            tryVerify(function() { return findText(page, "Kept", true) !== null }, 1000)
            compare(findText(page, "Ab", true), null)
            compare(page.keys, ["state", "status", "signOut", "key", "forgetKey"])
            arcadeBackend.achievementsKeyRefused = true
            tryVerify(function() { return findText(page, "Refused", true) !== null }, 1000)
            verify(findText(page, "refused the key") !== null)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.keyGiven, "")
            tryVerify(function() { return page.keys.length === 4 }, 1000)
            compare(page.keys[3], "key")
            // Its line gone, the cursor on the first; signed out, the lines
            // to sign in again.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryVerify(function() { return page.keys.length === 6 }, 1000)
            compare(page.keys, ["state", "status", "user", "password", "signIn", "key"])
        }

        // A game FinalBurn Neo plays with a leaderboard to show: its top
        // three under RA Hi-Score, the users' names wider than initials;
        // while it is read and for a game RetroAchievements hasn't, the box
        // says so; another game's leaderboard read changes nothing; the High
        // Scores module's table for a game MAME plays.
        function test_screenLeaderboard() {
            arcadeBackend.fbneoSets = ["sf2"]
            arcadeBackend.boards = { sf2: { state: "loading" } }
            var screen = openScreen()
            compare(screen.currentSet, "sf2")
            verify(findText(screen, "RA Hi-Score", true) !== null)
            verify(findText(screen, "Reading…", true) !== null)
            compare(findText(screen, "110,000"), null)
            arcadeBackend.boards.sf2 = { state: "ready", title: "High Score", entries: [
                { rank: "1ST", score: "1,234,560", name: "Mehmet" }, { rank: "2ND", score: "990,000", name: "Player2" },
                { rank: "3RD", score: "12,000", name: "abc" }] }
            arcadeBackend.leaderboardReady("1942")
            verify(findText(screen, "Reading…", true) !== null)
            arcadeBackend.leaderboardReady("sf2")
            tryVerify(function() { return findText(screen, "1,234,560", true) !== null }, 1000)
            compare(findText(screen, "Reading…", true), null)
            var name = findText(screen, "Mehmet", true)
            verify(name !== null)
            compare(name.width, Math.round(100 * screen.u))
            verify(findText(screen, "3RD", true) !== null)
            arcadeBackend.boards.sf2 = { state: "none" }
            arcadeBackend.leaderboardReady("sf2")
            tryVerify(function() { return findText(screen, "No Leaderboard", true) !== null }, 1000)
            arcadeBackend.boards.sf2 = { state: "failed" }
            arcadeBackend.leaderboardReady("sf2")
            tryVerify(function() { return findText(screen, "Not Reachable", true) !== null }, 1000)
            // A game MAME plays: this machine's own table.
            arcadeBackend.boards = { sf2: { state: "ready", title: "High Score", entries: [] } }
            arcadeBackend.engines = { sf2: "mame" }
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Up)
            compare(screen.currentSet, "sf2")
            tryVerify(function() { return findText(screen, "110,000", true) !== null }, 1000)
            verify(findText(screen, "Hi-Score", true) !== null)
            compare(findText(screen, "RA Hi-Score", true), null)
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

        // The column's ground, the scheme's background as solid as Info
        // Background says, under the picture's frame when there is none, and
        // in the box of the high scores.
        function test_screenGround() {
            var screen = openScreen()
            compare(screen.ground.a, 1)
            fuzzyCompare(screen.ground.b, 128 / 255, 0.01)
            compare(screen.ground.r, 0)
            loader.source = ""
            moduleRoot.infoBackground = 40
            screen = openScreen()
            fuzzyCompare(screen.ground.a, 0.4, 0.01)
            fuzzyCompare(screen.ground.b, 128 / 255, 0.01)
            verify(findText(screen, "No Picture") !== null)
        }

        // The hint bar's and the help line's room, when they are off, goes to
        // the list, a row more each, and to the box under the picture, a line
        // of the game's details more for the hint bar's and two for the help
        // line's; the high scores stay three.
        function test_screenRoom() {
            root.hintBar = false
            var screen = openScreen()
            compare(screen.perPage, 8)
            compare(screen.scoreRows, 3)
            compare(screen.detailRows, 2)
            loader.source = ""
            root.hintBar = true
            root.helpLine = false
            screen = openScreen()
            compare(screen.perPage, 8)
            compare(screen.scoreRows, 3)
            compare(screen.detailRows, 3)
            loader.source = ""
            root.hintBar = false
            screen = openScreen()
            compare(screen.perPage, 9)
            compare(screen.pages, 2)
            compare(screen.scores.length, 3)
            compare(findText(screen, "50,000"), null)
            compare(screen.detailRows, 4)
            verify(findText(screen, "System") !== null)
            verify(findText(screen, "CPS-1", true) !== null)
        }

        // The box under the picture: the high scores first, then the game's
        // maker, year, players and system over the play time, as many lines as
        // its height has room for, the help line saying the rest. An area too
        // short for a line under a picture as wide as the column's share
        // narrows the column, the box keeping the play time alone.
        function test_screenDetails() {
            moduleRoot.screenAdjustment = { top: 48, bottom: 60, left: 0, right: 0 }
            var screen = openScreen()
            compare(screen.scoreRows, 3)
            compare(screen.detailRows, 4)
            var topOf = function(text) { return findText(screen, text, true).mapToItem(screen, 0, 0).y }
            var lines = ["70,000", "Maker", "Year", "Players", "System", "Play Time"]
            for (var i = 1; i < lines.length; ++i)
                verify(topOf(lines[i - 1]) < topOf(lines[i]), lines[i - 1] + " over " + lines[i])
            verify(findText(screen, "Street Fighter II: The World Warrior (World 910522)", true) !== null)
            loader.source = ""
            moduleRoot.screenAdjustment = { top: -30, bottom: -105, left: 120, right: 120 }
            screen = openScreen()
            compare(screen.areaWidth, 960)
            compare(screen.columnWidth, 374)
            verify(screen.infoTop + 2 * screen.infoPadY + screen.textLines <= screen.listBottom)
            compare(screen.infoLines, 0)
            compare(findText(screen, "Hi-Score"), null)
            verify(findText(screen, "Play Time", true) !== null)
            verify(findText(screen, "Street Fighter II: The World Warrior (World 910522)  •  Capcom  •  1991  •  2 Players  •  CPS-1") !== null)
        }

        // A saved adjustment Screen Adjustment couldn't have made (config.json
        // edited, an older form): no side past the screen's edge, and a pair
        // of sides leaving less than the least room as the design has them,
        // the list keeping its rows.
        function test_screenAdjustmentLimits() {
            moduleRoot.screenAdjustment = { top: 500, bottom: 300, left: 400, right: 200 }
            var screen = openScreen()
            compare([screen.areaLeft, screen.areaTop, screen.areaRight, screen.areaBottom], [0, 0, 960, 720])
            loader.source = ""
            moduleRoot.screenAdjustment = { top: -100, bottom: -100, left: -200, right: 0 }
            screen = openScreen({ adjust: true })
            compare([screen.areaLeft, screen.areaTop, screen.areaRight, screen.areaBottom], [120, 90, 840, 645])
            compare(screen.perPage, 7)
            verify(screen.pitch >= screen.minPitch)
            // A move starts from the area shown, so what it saves is within
            // the limits.
            keyClick(Qt.Key_Down)
            compare(moduleRoot.screenAdjustment, { top: 0, bottom: 6, left: 0, right: 0 })
            loader.source = ""
            // The least room itself is kept.
            moduleRoot.screenAdjustment = { top: -60, bottom: -75, left: -60, right: -60 }
            screen = openScreen()
            compare([screen.areaWidth, screen.areaBottom - screen.areaTop], [600, 420])
        }

        // A detail too long for its line in the box scrolls through it, so it
        // shows whole.
        function test_screenLongDetail() {
            var info = Object.assign({}, arcadeBackend.gameInfo)
            info.sf2 = Object.assign({}, info.sf2, { maker: "Data East Corporation" })
            arcadeBackend.gameInfo = info
            var screen = openScreen()
            var value = findText(screen, "Data East Corporation", true)
            verify(value !== null)
            compare(value.width, value.implicitWidth)
            verify(value.width > value.parent.width)
            tryVerify(function() { return value.x < 0 }, 4000)
        }

        // List Style Straight: the rows one under another as wide as the bar,
        // the selected one out to its left end; Curved, on an arc.
        function test_screenStraight() {
            moduleRoot.listStyle = "Straight"
            var screen = openScreen()
            var row = function(number) { return findText(screen, number).parent }
            compare(row("001").x, Math.round(screen.areaLeft * screen.u))
            compare(row("002").x, Math.round((screen.areaLeft + 16) * screen.u))
            compare(row("004").x, row("002").x)
            compare(row("007").x, row("002").x)
            fuzzyCompare(row("002").x + row("002").width, Math.round((screen.areaLeft + screen.listWidth) * screen.u), 1)
            fuzzyCompare(row("001").x + row("001").width, row("002").x + row("002").width, 1)
            loader.source = ""
            moduleRoot.listStyle = "Curved"
            screen = openScreen()
            verify(row("004").x > row("002").x)
        }

        // 3D Curved: a wheel turning with the game chosen, in its middle row
        // furthest out, round from the list's end to its start; the rows going
        // back from it smaller and fainter. The bar has the game of so many.
        // Fewer games than rows lie round the middle row, the others empty;
        // the room of an even number of rows holds one fewer, for a middle.
        function test_screenWheel() {
            moduleRoot.listStyle = "3D Curved"
            var screen = openScreen()
            verify(screen.wheel)
            compare(screen.perPage, 7)
            compare(screen.middleRow, 3)
            compare(screen.currentSet, "sf2")
            verify(findText(screen, "1/15") !== null)
            var rows = ["013", "014", "015", "001", "002", "003", "004"].map(function(number) {
                return findText(screen, number).parent
            })
            var middle = Math.round((screen.listTop + 3 * screen.pitch) * screen.v)
            verify(rows[3].y <= middle)
            compare(findText(screen, "005"), null)
            compare(rows[3].opacity, 1)
            for (var i = 0; i < 3; ++i) {
                verify(rows[i].y < rows[i + 1].y)
                verify(rows[i].opacity < rows[i + 1].opacity)
                verify(rows[i].height < rows[i + 1].height)
                verify(rows[i].x + rows[i].width < rows[i + 1].x + rows[i + 1].width)
                verify(rows[6 - i].opacity < rows[5 - i].opacity)
                verify(rows[6 - i].x + rows[6 - i].width < rows[5 - i].x + rows[5 - i].width)
                // Their left ends on Curved's arc, the ends furthest left.
                compare(rows[i].x, Math.round((screen.areaLeft + 12
                                               + Math.round(24 * Math.sin(Math.PI * (i + 0.5) / 7))) * screen.u))
                compare(rows[6 - i].x, rows[i].x)
            }
            verify(rows[0].x < rows[1].x && rows[1].x < rows[2].x)
            // The game chosen stands out of the arc to the left.
            compare(rows[3].x, Math.round((screen.areaLeft + 12 + 24 - 16) * screen.u))
            keyClick(Qt.Key_Down)
            compare(screen.currentSet, "1942")
            verify(findText(screen, "2/15") !== null)
            compare(findText(screen, "002").parent.y, rows[3].y)
            compare(findText(screen, "013"), null)
            verify(findText(screen, "005") !== null)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(screen.currentSet, "dino")
            compare(findText(screen, "015").parent.y, rows[3].y)
            verify(findText(screen, "003") !== null)
            loader.source = ""
            moduleRoot.startFilter = "genres/Fighting"
            screen = openScreen()
            compare(screen.currentSet, "kinst")
            compare(screen.rows.filter(function(r) { return r !== null }).length, 2)
            compare(screen.rows[3].game.set, "kinst")
            compare(screen.rows[4].game.set, "sf2")
            loader.source = ""
            root.hintBar = false
            screen = openScreen()
            compare(screen.rowsFit, 8)
            compare(screen.perPage, 7)
        }

        // Border OFF: no frame round the picture or the box under it, the
        // picture filling its place.
        function test_screenBorder() {
            var frames = function(screen) {
                var found = []
                for (var i = 0; i < screen.children.length; ++i) {
                    var item = screen.children[i]
                    if (item.border !== undefined && item.x === Math.round(screen.columnX * screen.u))
                        found.push(item.border.width)
                }
                return found
            }
            var screen = openScreen()
            compare(frames(screen), [root.px, root.px])
            loader.source = ""
            moduleRoot.border = false
            screen = openScreen()
            compare(screen.frame, 0)
            compare(frames(screen), [0, 0])
        }

        // Screen Adjustment: a frame round the area, whose sides ▲ ▼ ◄ ► move
        // out, or in once select has switched, a step at a time, as far as the
        // screen's edge and no smaller than the list and the column need; the
        // list holding as many games as fit, the column its share of the
        // width; held select back as the design has it; saved a moment after
        // the last move, and as back leaves.
        function test_screenAdjust() {
            var screen = openScreen({ adjust: true })
            verify(screen.adjusting)
            verify(!screen.takesCoin)
            verify(findText(screen, "Screen Adjustment") !== null)
            verify(findText(screen, "[ESC]:DONE [▲▼◄►]:EXPAND [ENTER]:SHRINK HOLD:RESET") !== null)
            compare([screen.areaLeft, screen.areaTop, screen.areaRight, screen.areaBottom], [120, 90, 840, 645])
            compare(screen.perPage, 7)
            for (var i = 0; i < 9; ++i)
                keyClick(Qt.Key_Down)
            compare(moduleRoot.screenAdjustment.bottom, 54)
            compare(screen.perPage, 8)
            for (i = 0; i < 9; ++i)
                keyClick(Qt.Key_Down)
            compare(screen.areaBottom, 720)
            keyClick(Qt.Key_Right)
            compare(screen.areaRight, 846)
            compare(screen.columnWidth, 348)
            compare(screen.listWidth, 726 - 348 - 20)
            // In, from here on.
            keyClick(Qt.Key_Return)
            verify(screen.shrinking)
            verify(findText(screen, "[▲▼◄►]:SHRINK [ENTER]:EXPAND") !== null)
            keyClick(Qt.Key_Up)
            compare(screen.areaTop, 96)
            for (i = 0; i < 30; ++i)
                keyClick(Qt.Key_Left)
            compare(screen.areaWidth, 600)
            tryVerify(function() { return appCore.saved.screen_adjustment !== undefined })
            compare(appCore.saved.screen_adjustment, moduleRoot.screenAdjustment)
            // Held: back as the design has it.
            keyPress(Qt.Key_Return)
            wait(900)
            keyRelease(Qt.Key_Return)
            compare(moduleRoot.screenAdjustment, { top: 0, bottom: 0, left: 0, right: 0 })
            verify(screen.shrinking)
            keyClick(Qt.Key_Left)
            compare(moduleRoot.screenAdjustment.left, -6)
            keyClick(Qt.Key_Escape)
            compare(appCore.saved.screen_adjustment, { top: 0, bottom: 0, left: -6, right: 0 })
            compare(root.backs, 1)
            compare(root.navigations, [])
        }

        // Laid out in the adjusted area as it opens: a taller one holds more
        // games, the bar, the column and the bars where its sides are.
        function test_screenAdjusted() {
            moduleRoot.screenAdjustment = { top: 30, bottom: 54, left: 60, right: 0 }
            var screen = openScreen()
            verify(!screen.adjusting)
            compare(screen.perPage, 9)
            compare(screen.areaWidth, 780)
            verify(findText(screen, "[ESC]:BACK [5]:COIN [ENTER]:PLAY HOLD:OPTIONS") !== null)
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
            moduleRoot.startFilter = "genres/Fighting"
            var screen = openScreen()
            compare(screen.filterId, "genres/Fighting")
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
            moduleRoot.startFilter = "genres/Fighting"
            arcadeBackend.scanning = true
            arcadeBackend.filterList = all.filter(function(f) { return f.id === "favorites" })
            screen = openScreen()
            compare(screen.filterId, "favorites")
            arcadeBackend.scanning = false
            arcadeBackend.filterList = all
            arcadeBackend.gamesChanged()
            compare(screen.filterId, "genres/Fighting")
        }

        // Opened while the games are read, the lists stand in for Start
        // Filter's, which then opens on its first game, not on the favourite
        // the screen stood on meanwhile; a game moved to meanwhile is kept.
        function test_screenWaitedFilterOpensAtItsStart() {
            var all = arcadeBackend.filterList
            arcadeBackend.scanning = true
            arcadeBackend.filterList = all.filter(function(f) { return f.id === "favorites" })
            var screen = openScreen()
            compare(screen.filterId, "favorites")
            compare(screen.currentSet, "galaga")
            arcadeBackend.scanning = false
            arcadeBackend.filterList = all
            arcadeBackend.gamesChanged()
            compare(screen.filterId, "all")
            compare(screen.currentSet, "sf2")
            loader.source = ""
            arcadeBackend.scanning = true
            arcadeBackend.filterList = all.filter(function(f) { return f.id === "favorites" })
            screen = openScreen()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Up)
            compare(screen.currentSet, "galaga")
            arcadeBackend.scanning = false
            arcadeBackend.filterList = all
            arcadeBackend.gamesChanged()
            compare(screen.filterId, "all")
            compare(screen.currentSet, "galaga")
        }

        // The startup favourite plays at once, as if chosen in Favorites.
        function test_screenStartup() {
            var screen = openScreen({ startupPlay: { name: "Street Fighter II", path: "game:sf2" } })
            tryVerify(function() { return root.navigations.length === 1 })
            var nav = root.navigations[0]
            compare(nav.params.game, "sf2")
            compare(nav.listState.filter, "favorites")
            // The screen there as it plays.
            compare(screen.filterId, "favorites")
            compare(screen.currentSet, "sf2")
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
            // The list's own frame first.
            compare(arcadeBackend.started, [])
            tryVerify(function() { return arcadeBackend.started.length === 1 })
            compare(arcadeBackend.started, ["sf2"])
            compare(arcadeBackend.credited, [1])
            compare(router.credits, 0)
            compare(appCore.added, [["com.osdos.arcade", "recent", "game:sf2"]])
            compare(shown(router), screen)
            compare(router.playing.game, "sf2")
            verify(root.effectsResting)
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
            verify(!root.effectsResting)
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
            tryVerify(function() { return arcadeBackend.started.length === 1 })
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
            tryVerify(function() { return arcadeBackend.started.length === 1 })
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
            tryVerify(function() { return shown(router).phase === "error" })
            play = shown(router)
            compare(router.playing, null)
            verify(findText(play, "sudo apt install mame") !== null)
            // On Recently Played: the game MAME started, not the one it
            // couldn't.
            compare(appCore.added, [["com.osdos.arcade", "recent", "game:1942"]])
        }

        // Start Info Screen on: how to come back first. The game goes on
        // Recently Played as it starts, not before: canceled on this screen,
        // it isn't.
        function test_rootStartInfoOn() {
            var router = openRoot({ screen: "Arcade" })
            keyClick(Qt.Key_Return)
            var play = shown(router)
            compare(play.game, "sf2")
            compare(play.phase, "opening")
            compare(arcadeBackend.started, [])
            compare(appCore.added, [])
            keyClick(Qt.Key_Escape)
            compare(shown(router).currentSet, "sf2")
            compare(appCore.added, [])
            keyClick(Qt.Key_Return)
            play = shown(router)
            compare(play.phase, "opening")
            keyClick(Qt.Key_Space)
            compare(play.phase, "running")
            compare(arcadeBackend.started, ["sf2"])
            compare(appCore.added, [["com.osdos.arcade", "recent", "game:sf2"]])
            compare(appCore.lastAdded.name, "Street Fighter II: The World Warrior (World 910522)")
        }

        // Settings → Arcade → Screen Adjustment opens the Arcade screen to
        // adjust, whichever screen lists the games, in the area saved (a side
        // saved as no number taken as 0); back from it leaves the module.
        function test_rootAdjust() {
            appCore.settings = { screen: "Original", screen_adjustment: { top: 6, bottom: -12, left: "x", right: 3 } }
            var router = open("Root.qml", { adjustScreen: true })
            var screen = shown(router)
            verify(screen.adjusting)
            compare(router.screenAdjustment, { top: 6, bottom: -12, left: 0, right: 3 })
            compare([screen.areaLeft, screen.areaTop, screen.areaRight, screen.areaBottom], [120, 84, 843, 633])
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }
    }
}
