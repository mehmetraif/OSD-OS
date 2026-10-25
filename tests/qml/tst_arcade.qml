import QtQuick
import QtTest
import "../../views/Components/Hover.js" as Hover

// The Arcade module's views, modules/arcade/views/Games.qml (the original
// screen), ArcadeScreen.qml and Play.qml, against a stand-in for
// ArcadeBackend: a game chosen in the tree, the startup favourite played at
// once, and a game's screen, from how to come back (player 1's own buttons)
// to MAME having the screen, every key kept from the menus meanwhile, and
// back to the list, or why it didn't play. The Arcade screen: its filters
// and pages, a game's details and high scores, COIN, select (let go or
// held) and START, the selected name and a long filter's name scrolling,
// the room of its hint bar and help line, and the credits going in with
// the game. The module's
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
        function playTime(seconds) {
            if (!seconds)
                return "Never"
            var minutes = Math.ceil(seconds / 60)
            return minutes >= 60 ? Math.floor(minutes / 60) + "H " + (minutes % 60) + "M" : minutes + "M"
        }
        function players(count) { return count > 0 ? count + (count === 1 ? " Player" : " Players") : "" }
        function startGame(set, title, online) {
            var lan = !!online && !!online.lan
            var started = online && online.room !== undefined
                          ? (lan ? arcadeBackend.joinLanRoom(online.room) : arcadeBackend.joinRoom(online.room))
                        : online && online.hosting ? (lan ? arcadeBackend.hostLanGame(set) : arcadeBackend.hostGame(set))
                        : arcadeBackend.play(set, credits)
            if (!started)
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
            // A filter's own rows (the lobby's rooms), else its sets', Search's
            // own row first on it.
            for (var i = 0; i < filterList.length; ++i) {
                if (filterList[i].id === id && filterList[i].rows)
                    return filterList[i].rows.slice(first, first + count)
            }
            return setsOf(id).slice(first, first + count).map(function(set) {
                if (id === "search" && set === "")
                    return { set: "", search: true, name: searchWords !== "" ? searchWords : "Search" }
                return { set: set, name: gameInfo[set] ? gameInfo[set].title : set,
                         favorite: favorites.indexOf(set) >= 0 }
            })
        }
        // Search: the words given, and, on the Search filter of filterList,
        // its own row and All Games' games whose title or set has them in it.
        property string searchWords: ""
        property var searches: []
        function search() { return searchWords }
        function setSearch(words) {
            searchWords = words
            searches = searches.concat([words])
            for (var i = 0; i < filterList.length; ++i) {
                if (filterList[i].id === "search") {
                    filterList[i].sets = [""].concat(setsOf("all").filter(function(set) {
                        var title = gameInfo[set] ? gameInfo[set].title : set
                        return (title + " " + set).toLowerCase().indexOf(words.toLowerCase()) >= 0
                    }))
                    return filterList[i].sets.length
                }
            }
            return 0
        }
        // On a filter of rooms, room's row when it is there.
        function filterIndex(id, set, room) {
            for (var i = 0; i < filterList.length; ++i) {
                var rows = filterList[i].id === id && room !== undefined && room >= 0 ? filterList[i].rows || [] : []
                for (var r = 0; r < rows.length; ++r) {
                    if (rows[r].room === room)
                        return r
                }
            }
            return setsOf(id).indexOf(set)
        }
        // The filters whose games are still to come.
        property var pendingFilters: []
        function filterPending(id) { return pendingFilters.indexOf(id) >= 0 }
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
        // RetroArch's own download, on Windows.
        property bool retroarchDownloading: false
        property int retroarchProgress: 0
        property string retroarchMessage: ""
        property int retroarchDownloads: 0
        // MAME's page, and its download on Windows: mame's own change
        // signal stands for the backend's mameChanged().
        property var mame: ({})
        property bool mameDownloading: false
        property int mameProgress: 0
        property string mameMessage: ""
        property int mameDownloads: 0
        function mameInfo() { return mame }
        function downloadMame() { mameDownloads++; mameDownloading = true; mameProgress = 20; mameChanged() }
        // fbneo's own change signal stands for the backend's fbneoChanged().
        property int downloads: 0
        function fbneoInfo() { return fbneo }
        // The Game Pictures page, and the look through every game for them
        // (picturesProgress, as the backend's property).
        property var picturesProgress: ({ active: false })
        // What picturesInfo() answers (not "pictures": picturesChanged is a
        // game's signal above).
        property var pictureInfo: ({ games: 0, withPictures: 0, achievements: true, snaps: true, key: "none", writable: true })
        property int pictureDownloads: 0
        function picturesInfo() { return pictureInfo }
        function downloadPictures() {
            pictureDownloads++
            picturesProgress = { active: true, source: "RetroAchievements", done: 0, total: 0, asked: 0, failed: 0,
                                 again: false, result: "" }
        }
        function fbneoLicenseText() { return "You may freely use, modify, and distribute FB Neo." }
        function downloadFbneo() { downloads++; fbneoDownloading = true; fbneoProgress = 40; fbneoChanged() }
        function downloadRetroArch() {
            retroarchDownloads++; retroarchDownloading = true; retroarchProgress = 35; fbneoChanged()
        }
        property string achievementsUser: ""
        property string achievementsState: "none"
        property string achievementsMessage: ""
        property bool achievementsKey: false
        property bool achievementsKeyRefused: false
        property bool achievementsKeyWorks: false
        property var signIns: []
        property string keyGiven: ""
        property int keyChecks: 0
        function checkAchievementsKey() { keyChecks++ }
        signal achievementsChanged()
        // Online Play: the sets that can be hosted, the rooms joined, the
        // games hosted and the lobby's reads.
        property var hostable: []
        property var joined: []
        property var hosted: []
        property int roomReads: 0
        signal roomsChanged()
        function canHost(set) { return hostable.indexOf(set) >= 0 }
        function refreshRooms() { roomReads++ }
        function joinRoom(id) { joined = joined.concat([id]); running = playResult; return playResult }
        function hostGame(set) { hosted = hosted.concat([set]); running = playResult; return playResult }
        // The whole lobby, as lobby() gives it, kept where a page sees it
        // change only on roomsChanged(), as with the backend's; and whether
        // Online Play is on.
        property var lobbyStore: ({ data: { state: "loading", problem: "", rooms: [] } })
        function lobby() { return lobbyStore.data }
        function setLobby(data) { lobbyStore.data = data }
        // LAN Play: the sets that can be hosted on the local network, the
        // rooms there joined, the games hosted there and the network's reads.
        property var lanHostable: []
        property var lanJoined: []
        property var lanHosted: []
        property int lanReads: 0
        function canHostLan(set) { return lanHostable.indexOf(set) >= 0 }
        function refreshLanRooms() { lanReads++ }
        // What LAN Games says while it has no game.
        property var lanState: ({})
        function lanStatus() { return lanState }
        function joinLanRoom(id) { lanJoined = lanJoined.concat([id]); running = playResult; return playResult }
        function hostLanGame(set) { lanHosted = lanHosted.concat([set]); running = playResult; return playResult }
        // Each game's RetroAchievements leaderboard, by set, as leaderboard()
        // gives it ("off" for any other); changed in place, so the screen
        // reads it again only on leaderboardReady().
        property var boards: ({})
        signal leaderboardReady(string set)
        function leaderboard(set) { return (engineOf(set) === "fbneo" && boards[set]) || { state: "off" } }
        function setAchievementsKey(key) {
            keyGiven = key
            achievementsKey = key !== ""
            achievementsKeyRefused = false
            achievementsKeyWorks = false
        }
        function signInAchievements(user, password) {
            signIns = signIns.concat([user + ":" + password])
            achievementsUser = user
            achievementsState = "pending"
        }
        function signOutAchievements() { achievementsUser = ""; achievementsState = "none" }
        // A game's pictures coming in, and its achievements as
        // gameAchievements() gives them (achievementsData, by set; a game
        // with none "none"); the badges downloaded (badgeFiles: a file URL
        // by name and earned) and those asked for. Changed in place, so the
        // views read them again only on gameAchievementsReady() and
        // badgesReady().
        signal picturesChanged(string set)
        property var achievementsData: ({})
        property var badgeFiles: ({})
        property var badgeAsks: []
        signal gameAchievementsReady(string set)
        signal badgesReady()
        function gameAchievements(set) { return achievementsData[set] || { state: "none" } }
        function achievementCount(set) {
            var found = gameAchievements(set)
            var list = found.achievements || []
            var earned = list.filter(function(a) { return a.earned !== "" }).length
            return { state: found.state, earned: earned, total: list.length, progress: !!found.progress }
        }
        // The sets whose pictures were wanted (wantPictures()).
        property var picturesWanted: []
        function wantPictures(set) { picturesWanted.push(set) }
        function achievementBadge(set, id, name, earned) {
            badgeAsks.push(name + (earned ? "" : "_lock"))
            return badgeFiles[name + (earned ? "" : "_lock")] || ""
        }
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

    // InputManager: player 1's pad, a JAMMA stick, its buttons' labels and
    // its Arcade Buttons' (Coin arcadeCoin's).
    QtObject {
        id: inputManager
        property var players: [{ player: 1, key: "", name: "OSD/OS JAMMA P1", playing: "pad-0600aaaa", connected: true }]
        property string arcadeCoin: "COIN"
        function buttonLabel(key, token) {
            if (key !== "pad-0600aaaa") return ""
            return token === "back" ? "COIN" : token === "start" ? "START" : ""
        }
        function arcadeLabel(key, input) {
            if (key !== "pad-0600aaaa") return ""
            return input === "coin" ? arcadeCoin : input === "start" ? "START" : ""
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
        // How to put a program on, for the system (util/InstallHint.h).
        function installHint(program) { return "On Test OS: install " + program }
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
            inputManager.arcadeCoin = "COIN"
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
                                    games: 0, license: "", canDownload: true, canDownloadRetroarch: false,
                                    retroarchDownloaded: false }
            arcadeBackend.fbneoDownloading = false
            arcadeBackend.fbneoProgress = 0
            arcadeBackend.fbneoMessage = ""
            arcadeBackend.downloads = 0
            arcadeBackend.retroarchDownloading = false
            arcadeBackend.retroarchProgress = 0
            arcadeBackend.retroarchMessage = ""
            arcadeBackend.retroarchDownloads = 0
            arcadeBackend.mame = { mame: "/usr/games/mame", downloaded: false, canDownload: false, plugins: true,
                                   pluginsFolder: "/usr/share/games/mame/plugins" }
            arcadeBackend.mameDownloading = false
            arcadeBackend.mameProgress = 0
            arcadeBackend.mameMessage = ""
            arcadeBackend.mameDownloads = 0
            arcadeBackend.achievementsUser = ""
            arcadeBackend.achievementsState = "none"
            arcadeBackend.achievementsMessage = ""
            arcadeBackend.achievementsKey = false
            arcadeBackend.achievementsKeyRefused = false
            arcadeBackend.achievementsKeyWorks = false
            arcadeBackend.signIns = []
            arcadeBackend.keyGiven = ""
            arcadeBackend.keyChecks = 0
            arcadeBackend.boards = {}
            arcadeBackend.achievementsData = {}
            arcadeBackend.badgeFiles = {}
            arcadeBackend.badgeAsks = []
            arcadeBackend.picturesWanted = []
            root.statusIcons = []
            arcadeBackend.hostable = []
            arcadeBackend.joined = []
            arcadeBackend.hosted = []
            arcadeBackend.roomReads = 0
            arcadeBackend.lobbyStore = { data: { state: "loading", problem: "", rooms: [] } }
            arcadeBackend.lanHostable = []
            arcadeBackend.lanJoined = []
            arcadeBackend.lanHosted = []
            arcadeBackend.lanReads = 0
            arcadeBackend.lanState = { name: "No LAN Games",
                                       detail: "No one on this network hosts a game you have: host one from a game's options" }
            arcadeBackend.pendingFilters = []
            arcadeBackend.searchWords = ""
            arcadeBackend.searches = []
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

        // What stands in the way comes first in the line under the tree,
        // with how to put MAME on for this system, on both screens.
        function test_problem() {
            arcadeBackend.problem = "MAME is not installed"
            var games = open("Games.qml")
            verify(findText(games, "MAME is not installed. On Test OS: install mame") !== null)
            loader.source = ""
            var screen = openScreen()
            verify(findText(screen, "MAME is not installed. On Test OS: install mame") !== null)
            loader.source = ""
            arcadeBackend.problem = "MAME could not list the games"
            games = open("Games.qml")
            verify(findText(games, "MAME could not list the games", true) !== null)
        }

        // An empty folder says where the games go; what stands in the way of
        // the list comes first (how to put MAME on under it), and a folder
        // that can't be read is all.
        function test_empty() {
            arcadeBackend.entries = { "top": [] }
            var games = open("Games.qml")
            tryVerify(function() { return findText(games, "No games found") !== null })
            verify(findText(games, "Put MAME's ROM sets") !== null)
            compare(findText(games, "On Test OS"), null)
            arcadeBackend.problem = "MAME is not installed"
            verify(findText(games, "MAME is not installed", true) !== null)
            verify(findText(games, "On Test OS: install mame", true) !== null)
            verify(findText(games, "Put MAME's ROM sets") !== null)
            // On a system that can download MAME (Windows): the screen says
            // select does, select starts it, and the line follows it.
            compare(findText(games, "DOWNLOAD MAME"), null)
            arcadeBackend.mame = { mame: "", downloaded: false, canDownload: true, plugins: false, pluginsFolder: "" }
            tryVerify(function() { return findText(games, "DOWNLOAD MAME") !== null }, 1000)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.mameDownloads, 1)
            tryVerify(function() { return findText(games, "Downloading MAME… 20%") !== null }, 1000)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.mameDownloads, 1)
            arcadeBackend.mameDownloading = false
            arcadeBackend.mameMessage = "github.com answered 404"
            tryVerify(function() { return findText(games, "The download failed: github.com answered 404") !== null }, 1000)
            arcadeBackend.mameMessage = ""
            arcadeBackend.problem = "The games folder can't be read: /media/usb/MAME"
            arcadeBackend.folderReadable = false
            verify(findText(games, "The games folder can't be read") !== null)
            compare(findText(games, "On Test OS"), null)
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
            inputManager.arcadeCoin = "6"
            var play = open("Play.qml", { game: "galaga", title: "Galaga (Namco rev. B)" })
            // Off line, its Arcade Buttons' Coin and Start.
            compare(play.exitKeys, "6 + START")
            verify(findText(play, "Press 6 + START together") !== null)
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

        // One that never started, and how to put MAME, or RetroArch, on for
        // this system; nothing under another reason.
        function test_error() {
            arcadeBackend.playResult = false
            arcadeBackend.error = "MAME is not installed"
            var play = open("Play.qml", { game: "galaga", title: "Galaga" })
            tryCompare(play, "phase", "error")
            verify(findText(play, "On Test OS: install mame", true) !== null)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
            loader.source = ""
            arcadeBackend.error = "RetroArch is not installed"
            play = open("Play.qml", { game: "sf2", title: "Street Fighter II" })
            tryCompare(play, "phase", "error")
            verify(findText(play, "On Test OS: install retroarch", true) !== null)
            loader.source = ""
            arcadeBackend.error = "A game is still running"
            play = open("Play.qml", { game: "sf2", title: "Street Fighter II" })
            tryCompare(play, "phase", "error")
            compare(findText(play, "On Test OS"), null)
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

        // The mouse over a row chooses its game, as ▲ ▼ do (Main.qml's
        // pointer, through Hover.js): on the arc, and on the wheel, where
        // the game comes to the row and the wheel stays; between the rows,
        // over the bar, under the options or while the screen is adjusted,
        // nothing changes.
        function test_screenHover() {
            var screen = openScreen()
            var rowOf = function(number) { return findText(screen, number).parent }
            var middle = function(item) { return item.mapToItem(root, item.width / 2, item.height / 2) }
            var hover = function(p) { Hover.follow(root, root.Window.activeFocusItem, p.x, p.y) }
            hover(middle(rowOf("003")))
            compare(screen.selected, 2)
            compare(screen.currentSet, "avsp")
            verify(screen.browsed)
            hover(middle(rowOf("003")))
            compare(screen.selected, 2)
            var two = rowOf("002"), three = rowOf("003")
            hover(two.mapToItem(root, two.width / 2, two.height + (three.y - two.y - two.height) / 2))
            compare(screen.selected, 2)
            hover(middle(findText(screen, "All Games")))
            compare(screen.selected, 2)
            // The options over the list: their answers follow the mouse, the
            // list doesn't.
            keyPress(Qt.Key_Return)
            tryVerify(function() { return findText(screen, "Game Details", true) !== null }, 1500)
            keyRelease(Qt.Key_Return)
            hover(middle(rowOf("001")))
            compare(screen.selected, 2)
            var details = findText(screen, "Game Details", true)
            hover(middle(details))
            compare(details.color, root.surfaceColor)
            keyClick(Qt.Key_Escape)
            // Adjusting the screen.
            loader.source = ""
            screen = openScreen({ adjust: true })
            hover(middle(rowOf("003")))
            compare(screen.selected, 0)
            // The wheel.
            loader.source = ""
            moduleRoot.listStyle = "3D Curved"
            screen = openScreen()
            compare(screen.selectedRow, 4)
            var top = screen.wheelTop
            hover(middle(rowOf("003")))
            compare(screen.currentSet, "avsp")
            compare(screen.selectedRow, 6)
            compare(screen.wheelTop, top)
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
            verify(findText(screen, "[ESC]:BACK [ENTER]:PLAY HOLD:OPTIONS", true) !== null)
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
        // date and its games, its licence left to About.
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
            compare(page.keys, ["retroarch", "core", "games", "download"])
            compare(findText(page, "freely use, modify"), null)

            // On Windows without RetroArch: Download RetroArch under
            // RetroArch's line, select starting it and the line saying how
            // far it is, or why it failed; once it is in, the line says
            // Downloaded and offers it again; one found elsewhere has no
            // line for it.
            var keysAre = function(keys) { return JSON.stringify(page.keys) === JSON.stringify(keys) }
            arcadeBackend.fbneo = { retroarch: "", core: "", coreDate: "", downloaded: false, games: 0, license: "",
                                    canDownload: true, canDownloadRetroarch: true, retroarchDownloaded: false }
            arcadeBackend.fbneoChanged()
            tryVerify(function() { return keysAre(["retroarch", "retroarch_download", "core", "games", "download"]) }, 1000)
            verify(findText(page, "Download RetroArch") !== null)
            // The cursor stayed on Download: up to the new line.
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.retroarchDownloads, 1)
            tryVerify(function() { return findText(page, "35%") !== null }, 1000)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.retroarchDownloads, 1)
            arcadeBackend.retroarchDownloading = false
            arcadeBackend.retroarchMessage = "127.0.0.1 answered 404"
            arcadeBackend.fbneoChanged()
            tryVerify(function() { return findText(page, "Failed") !== null }, 1000)
            verify(findText(page, "answered 404") !== null)
            arcadeBackend.retroarchMessage = ""
            arcadeBackend.fbneo = { retroarch: "C:/Users/me/AppData/Roaming/OSD-OS/arcade/retroarch/RetroArch-Win64/retroarch.exe",
                                    core: "", coreDate: "", downloaded: false, games: 0, license: "",
                                    canDownload: true, canDownloadRetroarch: true, retroarchDownloaded: true }
            arcadeBackend.fbneoChanged()
            tryVerify(function() { return findText(page, "Download RetroArch Again") !== null }, 1000)
            verify(findText(page, "Downloaded") !== null)
            arcadeBackend.fbneo = { retroarch: "C:/RetroArch-Win64/retroarch.exe", core: "", coreDate: "", downloaded: false,
                                    games: 0, license: "", canDownload: true, canDownloadRetroarch: true,
                                    retroarchDownloaded: false }
            arcadeBackend.fbneoChanged()
            tryVerify(function() { return keysAre(["retroarch", "core", "games", "download"]) }, 1000)
            verify(findText(page, "Installed") !== null)

            compare(root.backs, 0)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // The MAME page: what is installed and where, its hiscore plugin;
        // on a Windows without MAME, Download MAME under MAME's line, select
        // starting it and the line saying how far it is, or why it failed;
        // once it is in, Downloaded and Download MAME Again; one installed
        // elsewhere, no such line.
        function test_mamePage() {
            var page = open("Mame.qml")
            verify(findText(page, "Installed", true) !== null)
            verify(findText(page, "Hiscore Plugin") !== null)
            compare(page.keys, ["mame", "plugin"])
            verify(findText(page, "/usr/games/mame") !== null)
            arcadeBackend.mame = { mame: "", downloaded: false, canDownload: true, plugins: false, pluginsFolder: "" }
            tryVerify(function() { return JSON.stringify(page.keys) === JSON.stringify(["mame", "download"]) }, 1000)
            verify(findText(page, "Not Installed") !== null)
            verify(findText(page, "On Test OS: install mame") !== null)
            keyClick(Qt.Key_Down)
            verify(findText(page, "half a gigabyte unpacked") !== null)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.mameDownloads, 1)
            tryVerify(function() { return findText(page, "20%") !== null }, 1000)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.mameDownloads, 1)
            arcadeBackend.mameDownloading = false
            arcadeBackend.mameMessage = "github.com answered 404"
            arcadeBackend.mameChanged()
            tryVerify(function() { return findText(page, "Failed") !== null }, 1000)
            verify(findText(page, "answered 404") !== null)
            arcadeBackend.mameMessage = ""
            arcadeBackend.mame = { mame: "C:/Users/me/AppData/Roaming/OSD-OS/arcade/mame/mame.exe", downloaded: true,
                                   canDownload: true, plugins: true,
                                   pluginsFolder: "C:/Users/me/AppData/Roaming/OSD-OS/arcade/mame/plugins" }
            tryVerify(function() { return findText(page, "Download MAME Again") !== null }, 1000)
            verify(findText(page, "Downloaded", true) !== null)
            compare(page.keys, ["mame", "download", "plugin"])
            arcadeBackend.mame = { mame: "C:/MAME/mame.exe", downloaded: false, canDownload: true, plugins: false,
                                   pluginsFolder: "" }
            tryVerify(function() { return JSON.stringify(page.keys) === JSON.stringify(["mame", "plugin"]) }, 1000)
            verify(findText(page, "Not Found") !== null)
            compare(root.backs, 0)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // The Game Pictures page: the games with a picture of all, each
        // source as Pictures From and the key have it, and Download Pictures
        // starting the look, whose way the row shows as it goes and how it
        // ended; back closes the page.
        function test_picturesPage() {
            arcadeBackend.pictureInfo = { games: 120, withPictures: 45, achievements: true, snaps: true, key: "none",
                                          writable: true }
            var page = open("Pictures.qml")
            verify(findText(page, "45/120") !== null)
            verify(findText(page, "No Web API Key") !== null)
            compare(page.keys, ["games", "achievements", "snaps", "download"])
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.pictureDownloads, 1)
            tryVerify(function() { return findText(page, "Starting") !== null }, 1000)
            arcadeBackend.picturesProgress = { active: true, source: "progetto-SNAPS", done: 12, total: 40, asked: 12,
                                               failed: 0, again: false, result: "" }
            tryVerify(function() { return findText(page, "progetto-SNAPS 12/40") !== null }, 1000)
            // Select while it goes on starts nothing anew.
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.pictureDownloads, 1)
            arcadeBackend.picturesProgress = { active: true, again: true, failed: 3, result: "" }
            tryVerify(function() { return findText(page, "Trying Again Soon") !== null }, 1000)
            arcadeBackend.picturesProgress = { active: false, result: "3 Not Downloaded" }
            tryVerify(function() { return findText(page, "3 Not Downloaded") !== null }, 1000)
            arcadeBackend.pictureInfo = { games: 120, withPictures: 117, achievements: true, snaps: false, key: "refused",
                                          writable: true }
            arcadeBackend.gamesChanged()
            tryVerify(function() { return findText(page, "117/120") !== null }, 1000)
            verify(findText(page, "Key Refused") !== null)
            verify(findText(page, "Off") !== null)
            compare(root.backs, 0)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // The account's page: the user and password typed (the password a
        // star a letter, both cases kept), Sign In hands them over, and Sign
        // Out brings them back; the key asked about as the page opens.
        function test_accountPage() {
            var page = open("Account.qml")
            verify(findText(page, "None") !== null)
            compare(arcadeBackend.keyChecks, 1)
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
            // RetroAchievements took it.
            arcadeBackend.achievementsKeyWorks = true
            tryVerify(function() { return findText(page, "Working", true) !== null }, 1000)
            arcadeBackend.achievementsKeyWorks = false
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
            // Signed in again, RetroArch signing in at once: the page says
            // so; refused, it says why, the user kept and the cursor on the
            // password, to type it again.
            arcadeBackend.achievementsUser = "Player"
            arcadeBackend.achievementsState = "checking"
            tryVerify(function() { return findText(page, "Checking", true) !== null }, 1000)
            compare(page.keys, ["state", "status", "signOut", "key"])
            page.typedUser = ""
            arcadeBackend.achievementsMessage = "Invalid User/Password combination. Please try again"
            arcadeBackend.achievementsState = "failed"
            tryVerify(function() { return findText(page, "Not Signed In", true) !== null }, 1000)
            compare(page.currentKey, "password")
            compare(page.typedUser, "Player")
            verify(findText(page, "Invalid User/Password combination") !== null)
        }

        // Pictures of the repository's, as a game's: a screenshot (4:3) and
        // one wider than a screen.
        readonly property string screenshot: Qt.resolvedUrl("../../docs/screenshots/arcade-games.png")
        readonly property string widePicture: Qt.resolvedUrl("../../assets/images/cassette.png")

        // The picture on the screen: the one of the two shown.
        function shownPicture(screen) {
            var found = null
            var walk = function(item) {
                if (found)
                    return
                if (item.fillMode !== undefined && item.sourceSize !== undefined && item.visible
                        && String(item.source) !== "")
                    found = item
                for (var i = 0; i < item.children.length; i++)
                    walk(item.children[i])
            }
            walk(screen)
            return found
        }

        // A game's pictures go round, the first again as it is chosen anew:
        // a screenshot fills the screen, one of another shape keeps its own.
        // One more comes in (picturesChanged) and joins them; with none,
        // which file it would be.
        function test_screenPictures() {
            arcadeBackend.gameInfo.sf2.pictures = [screenshot, widePicture]
            var screen = openScreen()
            compare(screen.currentSet, "sf2")
            tryVerify(function() { var p = shownPicture(screen); return p !== null && String(p.source) === screenshot }, 2000)
            var picture = shownPicture(screen)
            verify(picture.parent.screenWidth > 0)
            compare(picture.width, picture.parent.screenWidth)
            compare(picture.height, picture.parent.screenHeight)
            screen.pictureIndex = 1
            tryVerify(function() { var p = shownPicture(screen); return p !== null && String(p.source) === widePicture }, 2000)
            picture = shownPicture(screen)
            compare(picture.width, picture.parent.screenWidth)
            verify(picture.height < picture.parent.screenHeight)
            fuzzyCompare(picture.width / picture.height, 612 / 284, 0.05)
            // Chosen anew: its first again.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Up)
            compare(screen.pictureIndex, 0)
            tryVerify(function() { var p = shownPicture(screen); return p !== null && String(p.source) === screenshot }, 2000)
            // One more comes in.
            arcadeBackend.gameInfo.sf2.pictures = [screenshot, widePicture, screenshot + "?3"]
            arcadeBackend.picturesChanged("sf2")
            compare(screen.pictures.length, 3)
            // 1942 has none: which file it would be, and why none was
            // downloaded: no Web API key, or one refused.
            keyClick(Qt.Key_Down)
            compare(screen.currentSet, "1942")
            verify(findText(screen, "No Picture", true) !== null)
            verify(findText(screen, "snap/1942.png", true) !== null)
            verify(findText(screen, "No Web API Key", true) !== null)
            compare(shownPicture(screen), null)
            arcadeBackend.achievementsKey = true
            tryVerify(function() { return findText(screen, "No Web API Key", true) === null }, 1000)
            arcadeBackend.achievementsKeyRefused = true
            tryVerify(function() { return findText(screen, "Web API Key Refused", true) !== null }, 1000)
        }

        // The status icons at the right of the help line, which makes room
        // for them; with no help line, at the hint bar's right end in its
        // letters' colour; with neither bar, and with none to show, nowhere.
        function test_screenStatusIcons() {
            var screen = openScreen()
            var help = findText(screen, "Street Fighter II: The World Warrior")
            verify(help !== null)
            var box = help
            while (box && box.border === undefined)
                box = box.parent
            var fullWidth = Math.round(screen.areaWidth * screen.u)
            compare(box.width, fullWidth)
            compare(screen.statusRoom, 0)
            root.statusIcons = ["hiscore", "globe", "signal3"]
            verify(screen.statusRoom > 0)
            var row = null
            var walk = function(item) {
                if (!row && item.spacing !== undefined && item.color !== undefined && item.children.length === 4)
                    row = item
                for (var i = 0; !row && i < item.children.length; i++)
                    walk(item.children[i])
            }
            walk(screen)
            verify(row !== null, "the status icons")
            verify(row.visible)
            compare(row.x + row.width, Math.round(screen.areaRight * screen.u))
            verify(Qt.colorEqual(row.color, root.primaryColor))
            // The help line made room for them.
            compare(box.width, fullWidth - screen.statusRoom)
            // No help line: on the hint bar, while the hint line still fits
            // beside them.
            root.helpLine = false
            verify(row.visible)
            verify(Qt.colorEqual(row.color, root.surfaceColor))
            compare(box.width, fullWidth)
            var many = []
            for (var i = 0; i < 40; ++i)
                many.push("trophy")
            // (The row is laid out a frame later.)
            root.statusIcons = many
            tryVerify(function() { return !row.visible }, 1000)
            compare(screen.statusRoom, 0)
            root.statusIcons = ["hiscore", "globe", "signal3"]
            tryVerify(function() { return row.visible }, 1000)
            root.hintBar = false
            verify(!row.visible)
            compare(screen.statusRoom, 0)
            root.hintBar = true
            root.helpLine = true
            root.statusIcons = []
            verify(!row.visible)
        }

        // A game's achievements counted in the box, first among its details:
        // those earned of all, all alone with no account to say, "…" while
        // they are read; none for a game RetroAchievements has none of. The
        // game's pictures are wanted as it is shown.
        function test_screenAchievements() {
            arcadeBackend.achievementsData = { sf2: { state: "loading" } }
            var screen = openScreen()
            compare(screen.currentSet, "sf2")
            verify(arcadeBackend.picturesWanted.indexOf("sf2") >= 0)
            compare(screen.detailList[0].label, "Achievements")
            compare(screen.detailList[0].value, "…")
            arcadeBackend.achievementsData.sf2 = { state: "ready", progress: true, achievements: [
                { earned: "2026-03-14" }, { earned: "" }, { earned: "2025-01-02" }] }
            arcadeBackend.gameAchievementsReady("1942")
            compare(screen.detailList[0].value, "…")
            arcadeBackend.gameAchievementsReady("sf2")
            compare(screen.detailList[0].value, "2/3")
            // In the box while it has a line for it, else on the help line.
            if (screen.detailRows > 0)
                verify(findText(screen, "2/3", true) !== null)
            else
                verify(screen.about.indexOf("2/3 Achievements") >= 0)
            arcadeBackend.achievementsData.sf2.progress = false
            arcadeBackend.gameAchievementsReady("sf2")
            compare(screen.detailList[0].value, "3")
            arcadeBackend.achievementsData.sf2 = { state: "none" }
            arcadeBackend.gameAchievementsReady("sf2")
            compare(screen.detailList[0].label, "Maker")
            keyClick(Qt.Key_Down)
            verify(arcadeBackend.picturesWanted.indexOf("1942") >= 0)
        }

        // A game's options offer its Game Details, which open on it; back
        // from them lands where it was, as from playing it. The tree's too.
        function test_gameDetailsOption() {
            var screen = openScreen()
            keyPress(Qt.Key_Return)
            wait(900)
            keyRelease(Qt.Key_Return)
            verify(findText(screen, "Game Details", true) !== null)
            keyClick(Qt.Key_Down)   // Game Details
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "Details.qml")
            compare(root.navigations[0].params.game, "sf2")
            compare(root.navigations[0].params.title, "Street Fighter II: The World Warrior (World 910522)")
            compare(root.navigations[0].listState.set, "sf2")
            compare(root.navigations[0].listState.filter, "all")
            loader.source = ""
            root.navigations = []
            var games = open("Games.qml")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)   // All Games
            keyClick(Qt.Key_Right)  // into it, on Galaga
            keyClick(Qt.Key_Right)  // its options
            verify(findText(games, "Game Details", true) !== null)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "Details.qml")
            compare(root.navigations[0].params.game, "galaga")
            compare(root.navigations[0].listState.trail[root.navigations[0].listState.trail.length - 1].path, "all")
        }

        // Game Details: what the game is; while its achievements are read,
        // that they are; then those earned and those not, under headings,
        // the cursor on the first, each with its badge (earned or locked)
        // once downloaded and its points; the help line says what it asks
        // for and when it was earned. Select plays the game, back goes back.
        function test_details() {
            arcadeBackend.achievementsData = { sf2: { state: "loading" } }
            var details = open("Details.qml", { game: "sf2", title: "Street Fighter II" })
            compare(arcadeBackend.picturesWanted, ["sf2"])
            verify(findText(details, "Capcom • 1991 • 2 Players • CPS-1 • Played 1H 30M", true) !== null)
            verify(findText(details, "Reading RetroAchievements…", true) !== null)
            arcadeBackend.achievementsData.sf2 = { state: "ready", progress: true, achievements: [
                { id: 1, title: "Hadouken", description: "Throw a fireball", points: 5, badge: "101", earned: "", hardcore: false },
                { id: 2, title: "World Warrior", description: "Beat the game", points: 25, badge: "102",
                  earned: "2026-03-14", hardcore: true },
                { id: 3, title: "Bonus Stage", description: "Wreck the car", points: 10, badge: "103",
                  earned: "2025-01-02", hardcore: false }] }
            arcadeBackend.gameAchievementsReady("1942")
            verify(findText(details, "Reading RetroAchievements…", true) !== null)
            arcadeBackend.gameAchievementsReady("sf2")
            tryVerify(function() { return findText(details, "2 of 3 Earned • 35 of 40 Points", true) !== null }, 1000)
            verify(findText(details, "Earned", true) !== null)
            verify(findText(details, "Not Earned", true) !== null)
            // Earned first, the cursor on the first of them.
            var top = function(text) { return findText(details, text, true).mapToItem(details, 0, 0).y }
            verify(top("World Warrior") < top("Bonus Stage"))
            verify(top("Bonus Stage") < top("Not Earned"))
            verify(top("Not Earned") < top("Hadouken"))
            tryVerify(function() { return findText(details, "Beat the game") !== null }, 1000)
            verify(findText(details, "Earned 2026-03-14 in Hardcore") !== null)
            // Their badges, earned or locked, asked for and shown once in.
            verify(arcadeBackend.badgeAsks.indexOf("102") >= 0)
            verify(arcadeBackend.badgeAsks.indexOf("101_lock") >= 0)
            compare(shownPicture(details), null)
            arcadeBackend.badgeFiles["102"] = screenshot
            compare(shownPicture(details), null)
            arcadeBackend.badgesReady()
            tryVerify(function() {
                var p = shownPicture(details)
                return p !== null && String(p.source) === screenshot
            }, 2000)
            // The selected one's badge large, at the right under the title
            // bar, beside the lines; the next one's, not downloaded yet, framed.
            var big = null
            var findBig = function(item) {
                if (!big && item.fillMode !== undefined && item.sourceSize !== undefined && item.width > root.sh * 0.08)
                    big = item
                for (var i = 0; !big && i < item.children.length; ++i)
                    findBig(item.children[i])
            }
            findBig(details)
            verify(big !== null, "the selected badge")
            compare(String(big.source), screenshot)
            verify(big.parent.visible)
            compare(big.parent.x + big.parent.width, root.sw * 0.875)
            var facts = findText(details, "Capcom • 1991 • 2 Players • CPS-1 • Played 1H 30M", true)
            verify(facts.mapToItem(details, 0, 0).x + facts.width <= big.parent.x)
            keyClick(Qt.Key_Down)
            compare(String(big.source), "")
            verify(big.parent.visible)
            // Down to the one not earned.
            keyClick(Qt.Key_Down)
            tryVerify(function() { return findText(details, "Not Earned Yet") !== null }, 1000)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "Play.qml")
            compare(root.navigations[0].params.game, "sf2")
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // Why there are none: no key, a key refused, RetroAchievements not
        // reached, a game it has none of; with no user to say which were
        // earned, all under one heading, each badge in colour.
        function test_detailsWithout() {
            var cases = [["nokey", "No Web API key: Settings, Arcade, RetroAchievements Account"],
                         ["refused", "RetroAchievements refused the Web API key"],
                         ["failed", "RetroAchievements could not be reached"],
                         ["none", "RetroAchievements has no achievements for this game"]]
            var details
            for (var i = 0; i < cases.length; ++i) {
                arcadeBackend.achievementsData = { sf2: { state: cases[i][0] } }
                details = open("Details.qml", { game: "sf2", title: "Street Fighter II" })
                verify(findText(details, cases[i][1], true) !== null, cases[i][0])
                loader.source = ""
            }
            arcadeBackend.achievementsData = { sf2: { state: "ready", progress: false, achievements: [
                { id: 1, title: "Hadouken", description: "", points: 5, badge: "101", earned: "", hardcore: false }] } }
            details = open("Details.qml", { game: "sf2", title: "Street Fighter II" })
            verify(findText(details, "1 Achievement • 5 Points", true) !== null)
            verify(findText(details, "Achievements", true) !== null)
            compare(findText(details, "Not Earned", true), null)
            compare(arcadeBackend.badgeAsks[arcadeBackend.badgeAsks.length - 1], "101")
        }

        // A game FinalBurn Neo plays with a leaderboard to show: its top
        // three under RA Hi-Score, the users' names wider than initials;
        // while it is read, and for a game RetroAchievements has no
        // leaderboard of or one that couldn't be read, this machine's own
        // table under Hi-Score, and without one the box says so; another
        // game's leaderboard read changes nothing; the High Scores module's
        // table for a game MAME plays.
        function test_screenLeaderboard() {
            arcadeBackend.fbneoSets = ["sf2"]
            arcadeBackend.boards = { sf2: { state: "loading" } }
            var screen = openScreen()
            compare(screen.currentSet, "sf2")
            // Read: the machine's own meanwhile.
            verify(findText(screen, "Hi-Score", true) !== null)
            compare(findText(screen, "RA Hi-Score", true), null)
            verify(findText(screen, "110,000", true) !== null)
            compare(findText(screen, "Reading…", true), null)
            arcadeBackend.boards.sf2 = { state: "ready", title: "High Score", entries: [
                { rank: "1ST", score: "1,234,560", name: "Mehmet" }, { rank: "2ND", score: "990,000", name: "Player2" },
                { rank: "3RD", score: "12,000", name: "abc" }] }
            arcadeBackend.leaderboardReady("1942")
            verify(findText(screen, "110,000", true) !== null)
            arcadeBackend.leaderboardReady("sf2")
            tryVerify(function() { return findText(screen, "1,234,560", true) !== null }, 1000)
            verify(findText(screen, "RA Hi-Score", true) !== null)
            compare(findText(screen, "110,000", true), null)
            var name = findText(screen, "Mehmet", true)
            verify(name !== null)
            compare(name.width, Math.round(100 * screen.u))
            verify(findText(screen, "3RD", true) !== null)
            // No leaderboard, or none to be read: the machine's own again.
            arcadeBackend.boards.sf2 = { state: "none" }
            arcadeBackend.leaderboardReady("sf2")
            tryVerify(function() { return findText(screen, "110,000", true) !== null }, 1000)
            compare(findText(screen, "No Leaderboard", true), null)
            compare(findText(screen, "RA Hi-Score", true), null)
            arcadeBackend.boards.sf2 = { state: "failed" }
            arcadeBackend.leaderboardReady("sf2")
            verify(findText(screen, "110,000", true) !== null)
            compare(findText(screen, "Not Reachable", true), null)
            // Without a table of its own, the box says what the leaderboard
            // is up to.
            var tables = highScoresBackend.tables
            highScoresBackend.tables = {}
            arcadeBackend.boards.sf2 = { state: "loading" }
            arcadeBackend.leaderboardReady("sf2")
            tryVerify(function() { return findText(screen, "Reading…", true) !== null }, 1000)
            arcadeBackend.boards.sf2 = { state: "failed" }
            arcadeBackend.leaderboardReady("sf2")
            tryVerify(function() { return findText(screen, "Not Reachable", true) !== null }, 1000)
            arcadeBackend.boards.sf2 = { state: "none" }
            arcadeBackend.leaderboardReady("sf2")
            tryVerify(function() { return findText(screen, "No Scores Yet", true) !== null }, 1000)
            highScoresBackend.tables = tables
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

        // Online Games in the tree: select joins a room, which has no
        // options, the line under the tree saying whose it is; a game's
        // options host it where it can be.
        function test_onlineTree() {
            arcadeBackend.entries = Object.assign({}, arcadeBackend.entries, {
                "top": [{ name: "Online Games", path: "online", isFolder: true }].concat(arcadeBackend.entries.top),
                "online": [{ name: "Street Fighter II (ann)", path: "room:11", isFolder: false, kind: "room",
                             game: "sf2", room: 11, host: "ann", detail: "Hosted by ann in ES • sf2.zip" }]
            })
            arcadeBackend.hostable = ["pacman"]
            var games = open("Games.qml")
            keyClick(Qt.Key_Right)
            tryVerify(function() { return findText(games, "Hosted by ann in ES") !== null }, 1000)
            verify(findText(games, ":JOIN") !== null)
            compare(findText(games, ":OPTIONS"), null)
            keyClick(Qt.Key_Right)
            compare(findText(games, "Add to Favorites"), null)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "Play.qml")
            compare(root.navigations[0].params.game, "sf2")
            compare(root.navigations[0].params.online.room, 11)
            compare(root.navigations[0].params.online.host, "ann")
            // Pac-Man, in Favorites: hosted from its options, the last of
            // them.
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Right)
            tryVerify(function() { return findText(games, "Host Online Game") !== null }, 1000)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 2)
            compare(root.navigations[1].params.game, "pacman")
            verify(root.navigations[1].params.online.hosting)
        }

        // The Arcade screen's Online Games: select joins the room, JOIN in
        // the hint bar, and held, nothing; a game's options host it where it
        // can be.
        function test_onlineScreen() {
            arcadeBackend.filterList = [{ id: "online", name: "Online Games", sets: ["sf2"],
                                          rows: [{ set: "sf2", name: "Street Fighter II (ann)", favorite: false,
                                                   room: 11, host: "ann" }] }].concat(arcadeBackend.filterList)
            arcadeBackend.hostable = ["sf2"]
            moduleRoot.startFilter = "online"
            var screen = openScreen()
            compare(screen.filterId, "online")
            verify(findText(screen, "Street Fighter II (ann)") !== null)
            verify(findText(screen, ":JOIN") !== null)
            keyPress(Qt.Key_Return)
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].params.game, "sf2")
            compare(root.navigations[0].params.online.room, 11)
            compare(root.navigations[0].params.online.host, "ann")
            keyPress(Qt.Key_Return)
            wait(1000)
            keyRelease(Qt.Key_Return)
            compare(findText(screen, "Add to Favorites"), null)
            compare(root.navigations.length, 1)
            // All Games, on the same game: hosted from its options.
            keyClick(Qt.Key_Right)
            compare(screen.filterId, "all")
            compare(screen.currentSet, "sf2")
            keyPress(Qt.Key_Return)
            tryVerify(function() { return findText(screen, "Host Online Game") !== null }, 1500)
            keyRelease(Qt.Key_Return)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 2)
            compare(root.navigations[1].params.game, "sf2")
            verify(root.navigations[1].params.online.hosting)
        }

        // Two rooms of one game: the cursor keeps to its room as the lobby is
        // read again, and a room joined comes back to it. A room takes no
        // credits bought on the screen: FinalBurn Neo plays it. It has no
        // picture: the box, in the frame's place from the column's top, says
        // what the lobby says of it, as RetroArch Lobby's does, with no high
        // scores, and the help line says the rest; its marks follow our
        // versions as they are read (roomsChanged()); select held opens
        // nothing.
        function test_onlineRooms() {
            var bob = lobbyRoom(14, "FinalBurn Neo", "Street Fighter II", "bob",
                                { rom: "sf2", set: "sf2", country: "us", coreVersion: "v1.0.0.03", joinable: true,
                                  coreMatch: "same", retroarchMatch: "same", relay: "us-east1", players: 1, minutes: 12 })
            var ann = lobbyRoom(11, "FinalBurn Neo", "Street Fighter II", "ann",
                                { rom: "sf2", set: "sf2", country: "es", joinable: true, retroarchMatch: "other" })
            arcadeBackend.setLobby({ state: "ready", problem: "", yourCore: "v1.0.0.03", yourCoreState: "read",
                                     yourRetroArch: "1.22.2", yourRetroArchState: "read", rooms: [ann, bob] })
            arcadeBackend.filterList = [{ id: "online", name: "Online Games", sets: ["sf2", "sf2"],
                                          rows: [onlineRow(bob), onlineRow(ann)] }].concat(arcadeBackend.filterList)
            moduleRoot.startFilter = "online"
            var screen = openScreen()
            compare(screen.current.room, 14)
            verify(screen.fbneo)
            verify(screen.onRoom)
            verify(!screen.gameBox)
            verify(!findChild(screen, "preview").visible)
            var box = findChild(screen, "infoBox")
            compare(box.y, Math.round(screen.areaTop * screen.v))
            compare(box.y + box.height, Math.round(screen.listBottom * screen.v))
            compare(findText(screen, "Hi-Score"), null)
            compare(screen.roomLines.map(function(line) { return line.label }),
                    ["Game", "ROM", "CRC", "Core", "Version", "RetroArch", "System", "Host", "Country", "Connection",
                     "Password", "Players", "Hosted", "Can Join"])
            compare(roomLineLabels(screen), screen.roomLines.map(function(line) { return line.label }))
            compare(screen.roomLines[4].icons, ["ok"])
            compare(screen.roomLines[5].icons, ["ok"])
            verify(findText(screen, "bob", true) !== null)
            verify(findText(screen, "Relay", true) !== null)
            verify(findText(screen, "12M Ago", true) !== null)
            verify(findText(screen, "Through the relay us-east1") !== null)
            verify(findText(screen, "Select to join it") !== null)
            verify(findText(screen, ":JOIN") !== null)
            compare(findText(screen, "HOLD:"), null)
            keyClick(Qt.Key_Down)
            compare(screen.current.room, 11)
            compare(screen.roomLines[4].label, "RetroArch")
            compare(screen.roomLines[4].icons, ["no"])
            verify(findText(screen, "Straight to the host") !== null)
            // Ours read since, as the lobby says it: the marks follow.
            var same = lobbyRoom(11, "FinalBurn Neo", "Street Fighter II", "ann",
                                 { rom: "sf2", set: "sf2", country: "es", joinable: true, retroarchMatch: "same" })
            arcadeBackend.filterList[0].rows = [onlineRow(bob), onlineRow(same)]
            arcadeBackend.roomsChanged()
            compare(screen.current.room, 11)
            compare(screen.roomLines[4].icons, ["ok"])
            arcadeBackend.gamesChanged()
            compare(screen.current.room, 11)
            // Select held: no options, and no page.
            keyPress(Qt.Key_Return)
            wait(1000)
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 0)
            compare(findText(screen, "Game Details"), null)
            keyPress(Qt.Key_Return)
            keyRelease(Qt.Key_Return)
            compare(root.navigations[0].params.online.room, 11)
            compare(root.navigations[0].listState.room, 11)
            loader.source = ""
            screen = openScreen({ navListState: root.navigations[0].listState })
            compare(screen.current.room, 11)
            // The room gone: the game's other one.
            arcadeBackend.filterList[0].rows = arcadeBackend.filterList[0].rows.slice(0, 1)
            arcadeBackend.filterList[0].sets = ["sf2"]
            arcadeBackend.gamesChanged()
            compare(screen.current.room, 14)
        }

        // A room of the whole lobby, as lobby() gives it.
        function lobbyRoom(id, core, title, host, more) {
            return Object.assign({ id: id, core: core, title: title, rom: title, set: "", crc: "", coreVersion: "",
                                   subsystem: "", retroarch: "1.22.2", frontend: "unix x64", host: host,
                                   country: "", relay: "", players: -1, spectators: -1, minutes: -1,
                                   password: false, spectatePassword: false, connectable: true,
                                   joinable: false, why: "", coreMatch: core === "FinalBurn Neo" ? "unknown" : "",
                                   retroarchMatch: "unknown" }, more || {})
        }
        // A room of Online Games', as filterGames() gives it: its game and
        // who hosts it, with the room as lobby() has it (info).
        function onlineRow(r) {
            return { set: r.set, name: r.title + " (" + r.host + ")", favorite: false, room: r.id, host: r.host, info: r }
        }
        // A room of the local network's, as lanRooms() has it (a LAN Games
        // row's info): what its host answered with, matched against ours, and
        // where it is joined, its address on this network and the game's
        // file.
        function lanRoom(id, title, host, more) {
            return lobbyRoom(id, "FinalBurn Neo", title, host,
                             Object.assign({ rom: "sf2", set: "sf2", joinable: true, address: "192.168.1.20",
                                             file: "sf2.zip", frontend: "unix ARMv8" }, more || {}))
        }

        // RetroArch's whole lobby: read as the page opens, its rooms under
        // their cores' headings, FinalBurn Neo's first, ▶ on the one to join
        // and a lock on one with a password, the help line saying what each
        // is; select opens a room, a line each of what the lobby says of it,
        // Join first for one that can be joined, which joins it; back to the
        // rooms on the same room, then out. Coming back from a game joined
        // lands on its room; a lobby not reached says so.
        function test_lobby() {
            var page = open("Lobby.qml")
            compare(arcadeBackend.roomReads, 1)
            verify(findText(page, "Reading RetroArch's lobby") !== null)
            arcadeBackend.setLobby({ state: "ready", problem: "", yourCore: "v1.0.0.03", yourCoreState: "read",
                                     yourRetroArch: "1.19.1", yourRetroArchState: "read", rooms: [
                lobbyRoom(11, "FinalBurn Neo", "Street Fighter II", "ann",
                          { rom: "sf2", set: "sf2", country: "es", coreVersion: "v1.0.0.03", crc: "B62D0BC7",
                            joinable: true, coreMatch: "same", retroarchMatch: "other" }),
                lobbyRoom(14, "FinalBurn Neo", "Metal Slug", "bob",
                          { rom: "mslug", set: "mslug", password: true, why: "It asks for a password" }),
                lobbyRoom(16, "Snes9x", "Super Mario Kart PBD Edit", "dee",
                          { crc: "82993FA8", coreVersion: "1.63", country: "gb", relay: "us-east1", players: 2,
                            spectators: 0, minutes: 75, retroarchMatch: "other",
                            why: "OSD/OS joins FinalBurn Neo's games alone: this one is Snes9x's" })] })
            // Seen once the backend says the lobby was read.
            compare(findText(page, "3 Rooms"), null)
            arcadeBackend.roomsChanged()
            tryVerify(function() { return findText(page, "3 Rooms • 2 FinalBurn Neo • 1 To Join") !== null }, 1000)
            verify(findText(page, "FinalBurn Neo", true) !== null)
            verify(findText(page, "Snes9x", true) !== null)
            // On the first room, which can be joined.
            tryVerify(function() { return findText(page, "Select to join it") !== null }, 1000)
            verify(findText(page, "ROM sf2, CRC B62D0BC7") !== null)
            // Its core's version yours, its RetroArch not.
            verify(findText(page, "FinalBurn Neo v1.0.0.03 (yours too)") !== null)
            verify(findText(page, "RetroArch 1.22.2 (yours 1.19.1)") !== null)
            verify(findText(page, ":OPEN") !== null)
            // Down over the heading, to the Snes9x room.
            keyClick(Qt.Key_Down)
            verify(findText(page, "It asks for a password") !== null)
            keyClick(Qt.Key_Down)
            verify(findText(page, "ROM Super Mario Kart PBD Edit, CRC 82993FA8") !== null)
            verify(findText(page, "Through the relay us-east1") !== null)
            verify(findText(page, "this one is Snes9x's") !== null)
            // Its lines: what the lobby says of it, nothing to join.
            keyClick(Qt.Key_Return)
            verify(findText(page, "Snes9x • Hosted by dee") !== null)
            verify(findText(page, "Core Version", true) !== null)
            verify(findText(page, "1.63", true) !== null)
            verify(findText(page, "82993FA8", true) !== null)
            compare(findText(page, "Join", true), null)
            compare(findText(page, ":JOIN"), null)
            // Its core not yours, nothing to match its version against, its
            // RetroArch another than yours.
            compare(page.lines[2].icons, ["no"])
            compare(page.lines[3].icons, [])
            compare(page.lines[4].icons, ["no"])
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            verify(findText(page, "OSD/OS plays FinalBurn Neo's games alone") !== null)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            verify(findText(page, "Yours is 1.19.1: another version") !== null)
            // Down to how it is reached, then to whether it can be joined.
            for (var i = 0; i < 4; ++i)
                keyClick(Qt.Key_Down)
            verify(findText(page, "Through the relay us-east1, for") !== null)
            verify(findText(page, "Relay", true) !== null)
            verify(findText(page, "GB", true) !== null)
            for (i = 0; i < 5; ++i)
                keyClick(Qt.Key_Down)
            verify(findText(page, "1H 15M Ago", true) !== null)
            verify(findText(page, "this one is Snes9x's") !== null)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 0)
            // Back on the rooms, on the same one; up to the first, opened and
            // joined.
            keyClick(Qt.Key_Escape)
            compare(root.backs, 0)
            verify(findText(page, "3 Rooms") !== null)
            verify(findText(page, "this one is Snes9x's") !== null)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            verify(findText(page, "Join", true) !== null)
            verify(findText(page, ":JOIN") !== null)
            // Its core and its version yours, its RetroArch not.
            compare(page.lines[4].icons, ["ok"])
            compare(page.lines[5].icons, ["ok"])
            compare(page.lines[6].icons, ["no"])
            for (i = 0; i < 5; ++i)
                keyClick(Qt.Key_Down)
            verify(findText(page, "The same as yours: v1.0.0.03") !== null)
            for (i = 0; i < 5; ++i)
                keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "Play.qml")
            compare(root.navigations[0].params.game, "sf2")
            compare(root.navigations[0].params.online.room, 11)
            compare(root.navigations[0].params.online.host, "ann")
            compare(root.navigations[0].listState.room, 11)
            keyClick(Qt.Key_Escape)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
            // Back from a game: on its room.
            loader.source = ""
            page = open("Lobby.qml", { navListState: { room: 16 } })
            tryVerify(function() { return findText(page, "this one is Snes9x's") !== null }, 1000)
            // Not reached: why.
            arcadeBackend.setLobby({ state: "failed", problem: "RetroArch's lobby can't be reached: Connection refused",
                                     rooms: [] })
            arcadeBackend.roomsChanged()
            tryVerify(function() { return findText(page, "Lobby Not Reachable") !== null }, 1000)
            verify(findText(page, "Connection refused") !== null)
        }

        // A room's core, its version and its RetroArch while yours aren't
        // known: still being read, they couldn't be, or not installed here.
        function test_lobbyYours() {
            var page = open("Lobby.qml")
            var yours = function(core, retroarch, theirs) {
                arcadeBackend.setLobby({ state: "ready", problem: "", yourCore: "", yourCoreState: core,
                                         yourRetroArch: retroarch === "read" ? "1.19.1" : "", yourRetroArchState: retroarch,
                                         rooms: [
                    lobbyRoom(11, "FinalBurn Neo", "Street Fighter II", "ann",
                              { rom: "sf2", set: "sf2", coreVersion: "v1.0.0.03",
                                retroarch: theirs !== undefined ? theirs : "1.22.2" })] })
                arcadeBackend.roomsChanged()
            }
            var line = function(at) { return page.lines[at].description }
            yours("reading", "reading")
            tryVerify(function() { return findText(page, "1 Room • 1 FinalBurn Neo • 0 To Join") !== null }, 1000)
            // On the room, which opens.
            tryVerify(function() { return findText(page, "ROM sf2") !== null }, 1000)
            keyClick(Qt.Key_Return)
            // Game, ROM, CRC, Core, Core Version, RetroArch.
            compare(page.lines[3].label, "Core")
            compare(page.lines[3].icons, ["ok"])
            compare(page.lines[3].description, "The emulator it is played in: yours too")
            compare(page.lines[4].icons, [])
            compare(page.lines[4].description, "Its host's version: yours is still being read")
            compare(page.lines[5].icons, [])
            compare(page.lines[5].description, "The RetroArch its host plays it in: yours is still being read")
            yours("failed", "failed")
            tryVerify(function() { return line(4) === "Its host's version: yours couldn't be read" }, 1000)
            compare(page.lines[3].icons, ["ok"])
            compare(line(5), "The RetroArch its host plays it in: yours couldn't be read")
            yours("missing", "missing")
            tryVerify(function() { return page.lines[3].icons[0] === "no" }, 1000)
            compare(line(3), "The emulator it is played in: FinalBurn Neo, not installed here")
            compare(line(4), "Its host's version: FinalBurn Neo is not installed here")
            compare(line(5), "The RetroArch its host plays it in: RetroArch is not installed here")
            // Yours read, its host's RetroArch not said: nothing to say of it;
            // and the core's version of a room matched before yours was read
            // (one kept as it left the lobby): just its version.
            yours("read", "read", "")
            tryVerify(function() { return line(5) === "The RetroArch its host plays it in" }, 1000)
            compare(page.lines[5].value, "?")
            compare(line(4), "FinalBurn Neo v1.0.0.03")
        }

        // The lobby read again with a room open: the cursor stays on its line,
        // Join or not; a room gone, or the lobby not reached, has nothing to
        // join and says why, and once it is back can be joined again; back on
        // the rooms, the cursor is on it, though the list lost it a while.
        function test_lobbyReadAgain() {
            var sf2 = function(minutes) {
                return lobbyRoom(11, "FinalBurn Neo", "Street Fighter II", "ann",
                                 { rom: "sf2", set: "sf2", players: 1, spectators: 0, minutes: minutes, joinable: true })
            }
            var mslug = lobbyRoom(14, "FinalBurn Neo", "Metal Slug", "bob",
                                  { rom: "mslug", set: "mslug", password: true, why: "It asks for a password" })
            var kart = lobbyRoom(16, "Snes9x", "Super Mario Kart", "dee",
                                 { why: "OSD/OS joins FinalBurn Neo's games alone: this one is Snes9x's" })
            var page = open("Lobby.qml")
            var read = function(state, rooms, problem) {
                arcadeBackend.setLobby({ state: state, problem: problem || "", rooms: rooms })
                arcadeBackend.roomsChanged()
            }
            read("ready", [mslug, sf2(5), kart])
            tryVerify(function() { return findText(page, "3 Rooms") !== null }, 1000)
            // The second room opened, and down to how long ago it was hosted:
            // Join, Game, ROM, CRC, Core, RetroArch, System, Host, Country,
            // Connection, Password, Players, Spectators, Hosted.
            tryVerify(function() { return findText(page, "It asks for a password") !== null }, 1000)
            keyClick(Qt.Key_Down)
            verify(findText(page, "Select to join it") !== null)
            keyClick(Qt.Key_Return)
            for (var i = 0; i < 13; ++i)
                keyClick(Qt.Key_Down)
            verify(findText(page, "How long ago it was hosted") !== null)
            verify(findText(page, "5M Ago", true) !== null)
            // Read again a minute on: still there, and select joins nothing.
            read("ready", [mslug, sf2(6), kart])
            tryVerify(function() { return findText(page, "6M Ago", true) !== null }, 1000)
            wait(50)
            verify(findText(page, "How long ago it was hosted") !== null)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 0)
            // Gone from the lobby, the rooms' cursor falling back to another:
            // no Join, the cursor on its line still, and why on the last line.
            read("ready", [mslug, kart])
            tryVerify(function() { return page.lines[0].type !== "join" }, 1000)
            wait(50)
            verify(findText(page, "How long ago it was hosted") !== null)
            keyClick(Qt.Key_Down)
            verify(findText(page, "It is no longer in RetroArch's lobby") !== null)
            // The lobby not reached: why.
            read("failed", [], "RetroArch's lobby can't be reached: Connection refused")
            tryVerify(function() { return findText(page, "Connection refused") !== null }, 1000)
            // Back, and to be joined again.
            read("ready", [mslug, sf2(7), kart])
            tryVerify(function() { return findText(page, "From here, in FinalBurn Neo") !== null }, 1000)
            compare(page.lines[0].type, "join")
            // Back on the rooms: on it, not on the room the list fell back to.
            keyClick(Qt.Key_Escape)
            compare(root.backs, 0)
            verify(findText(page, "Select to join it") !== null)
        }

        // The lobby from the tree's Online Games; on the Arcade screen a filter
        // of its own (test_screenLobby()), not a game's option.
        function test_lobbyOpened() {
            arcadeBackend.entries = Object.assign({}, arcadeBackend.entries, {
                "top": [{ name: "Online Games", path: "online", isFolder: true }].concat(arcadeBackend.entries.top),
                "online": [{ name: "RetroArch Lobby", path: "lobby", isFolder: false, kind: "lobby",
                             detail: "Every game hosted in RetroArch's lobby" }]
            })
            var games = open("Games.qml")
            keyClick(Qt.Key_Right)
            tryVerify(function() { return findText(games, "Every game hosted in RetroArch's lobby") !== null }, 1000)
            verify(findText(games, ":OPEN") !== null)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "Lobby.qml")
            loader.source = ""
            // The Arcade screen: not among a game's options.
            var screen = openScreen()
            keyPress(Qt.Key_Return)
            tryVerify(function() { return findText(screen, "Game Details") !== null }, 1500)
            keyRelease(Qt.Key_Return)
            compare(findText(screen, "RetroArch Lobby"), null)
        }

        // A room joined from the game's screen says whose game it is and
        // joins it; a game hosted says so and hosts it; and with Start Info
        // Screen off, the router joins straight from the list.
        function test_playOnline() {
            // Online, FinalBurn Neo plays it, whatever its engine, and the
            // way back is the pad's own Back and Start: no player has their
            // Arcade Buttons there.
            arcadeBackend.engines = { sf2: "mame" }
            inputManager.arcadeCoin = "6"
            var play = open("Play.qml", { game: "sf2", title: "Street Fighter II (ann)", online: { room: 11, host: "ann" } })
            verify(play.fbneo)
            compare(play.exitKeys, "COIN + START")
            verify(findText(play, "Joining ann's game online") !== null)
            tryCompare(play, "phase", "running", 3000)
            compare(arcadeBackend.joined, [11])
            compare(arcadeBackend.started, [])
            arcadeBackend.running = false
            arcadeBackend.finished(0, "ok", "")
            play = open("Play.qml", { game: "sf2", title: "Street Fighter II", online: { hosting: true } })
            verify(findText(play, "Hosting online: others join from RetroArch's lobby") !== null)
            tryCompare(play, "phase", "running", 3000)
            compare(arcadeBackend.hosted, ["sf2"])
            arcadeBackend.running = false
            arcadeBackend.finished(0, "ok", "")
            loader.source = ""
            appCore.added = []
            var router = openRoot({ start_info: false })
            router.playGame({ game: "sf2", title: "Street Fighter II (ann)", online: { room: 11, host: "ann" } }, {})
            tryVerify(function() { return arcadeBackend.joined.length === 2 })
            compare(arcadeBackend.started, [])
            compare(appCore.added, [["com.osdos.arcade", "recent", "game:sf2"]])
        }

        // LAN Games in the tree: select joins a room at its host on the
        // local network, the line under the tree saying who hosts it and
        // their versions against ours; a game's options host it on the
        // network alone.
        function test_lanTree() {
            arcadeBackend.entries = Object.assign({}, arcadeBackend.entries, {
                "top": [{ name: "LAN Games", path: "lan", isFolder: true }].concat(arcadeBackend.entries.top),
                "lan": [{ name: "Street Fighter II (ann)", path: "lan:1", isFolder: false, kind: "room", game: "sf2",
                          room: 1, host: "ann", lan: true,
                          detail: "Hosted by ann at 192.168.1.20 on this network • sf2.zip • Its RetroArch is 1.21.0, yours 1.22.2" }]
            })
            arcadeBackend.lanHostable = ["pacman"]
            var games = open("Games.qml")
            // The network asked as LAN Games opens, and again while it is
            // open; not while it is shut.
            var lanRefresh = findChild(games, "lanRefresh")
            verify(!lanRefresh.running)
            compare(arcadeBackend.lanReads, 0)
            keyClick(Qt.Key_Right)
            tryVerify(function() { return lanRefresh.running }, 1000)
            tryCompare(arcadeBackend, "lanReads", 1, 1000)
            tryVerify(function() { return findText(games, "Its RetroArch is 1.21.0, yours 1.22.2") !== null }, 1000)
            verify(findText(games, ":JOIN") !== null)
            keyClick(Qt.Key_Right)
            compare(findText(games, "Add to Favorites"), null)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "Play.qml")
            compare(root.navigations[0].params.game, "sf2")
            compare(root.navigations[0].params.online.room, 1)
            compare(root.navigations[0].params.online.host, "ann")
            verify(root.navigations[0].params.online.lan)
            // Pac-Man, in Favorites: hosted on the network from its options,
            // the last of them; not online, with Online Play off.
            keyClick(Qt.Key_Left)
            tryVerify(function() { return !lanRefresh.running }, 1000)
            compare(arcadeBackend.lanReads, 1)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Right)
            tryVerify(function() { return findText(games, "Host LAN Game") !== null }, 1000)
            compare(findText(games, "Host Online Game"), null)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 2)
            compare(root.navigations[1].params.game, "pacman")
            verify(root.navigations[1].params.online.hosting)
            verify(root.navigations[1].params.online.lan)
        }

        // The Arcade screen's LAN Games: no picture, the box in the frame's
        // place saying what the host answered with, its core and RetroArch
        // against ours, the help line saying who hosts the room first, select
        // joining it at its host; a game's options host it on the network
        // alone.
        function test_lanScreen() {
            arcadeBackend.filterList = [{ id: "lan", name: "LAN Games", sets: ["sf2"],
                                          rows: [{ set: "sf2", name: "Street Fighter II (ann)", favorite: false, room: 1,
                                                   host: "ann", lan: true,
                                                   detail: "Hosted by ann at 192.168.1.20 on this network • sf2.zip",
                                                   info: lanRoom(1, "Street Fighter II", "ann",
                                                                 { coreVersion: "v1.0.0.03", coreMatch: "same",
                                                                   retroarch: "1.21.0", retroarchMatch: "other" }) }] }]
                                        .concat(arcadeBackend.filterList)
            arcadeBackend.lanHostable = ["sf2"]
            moduleRoot.startFilter = "lan"
            var screen = openScreen()
            compare(screen.filterId, "lan")
            // The network asked as the screen comes up, and again while it is
            // up; not while a game is played, and at once after.
            var lanRefresh = findChild(screen, "lanRefresh")
            verify(lanRefresh.running)
            tryCompare(arcadeBackend, "lanReads", 1, 1000)
            arcadeBackend.running = true
            verify(!lanRefresh.running)
            arcadeBackend.running = false
            verify(lanRefresh.running)
            tryCompare(arcadeBackend, "lanReads", 2, 1000)
            verify(findText(screen, "Street Fighter II (ann)") !== null)
            verify(findText(screen, ":JOIN") !== null)
            // No picture: the box, in the frame's place from the column's
            // top, says what the host answered with, its core and RetroArch
            // against ours; the help line says who hosts it first, then the
            // game's full name and what it is.
            verify(screen.onRoom)
            verify(!screen.gameBox)
            verify(!findChild(screen, "preview").visible)
            var box = findChild(screen, "infoBox")
            compare(box.y, Math.round(screen.areaTop * screen.v))
            compare(findText(screen, "Hi-Score"), null)
            compare(screen.roomLines.map(function(line) { return line.label }),
                    ["Game", "File", "Host", "Address", "Core", "Version", "RetroArch", "System", "Password", "Connection"])
            compare(roomLineLabels(screen), screen.roomLines.map(function(line) { return line.label }))
            compare(screen.roomLines[4].icons, ["ok"])
            compare(screen.roomLines[5].icons, ["ok"])
            compare(screen.roomLines[6].icons, ["no"])
            compare(screen.roomLines[9].value, "LAN")
            verify(findText(screen, "192.168.1.20", true) !== null)
            verify(findText(screen, "sf2.zip", true) !== null)
            verify(findText(screen, "1.21.0", true) !== null)
            verify(findText(screen, "unix ARMv8", true) !== null)
            compare(screen.about.split("\n"),
                    ["Hosted by ann at 192.168.1.20 on this network • sf2.zip",
                     "Street Fighter II: The World Warrior (World 910522)", "Capcom", "1991", "2 Players", "CPS-1"])
            verify(findText(screen, "Hosted by ann at 192.168.1.20 on this network") !== null)
            keyPress(Qt.Key_Return)
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].params.game, "sf2")
            compare(root.navigations[0].params.online.room, 1)
            compare(root.navigations[0].params.online.host, "ann")
            verify(root.navigations[0].params.online.lan)
            // All Games, on the same game: the line its own, the picture's
            // frame back with the box under it and the game's scores in it,
            // and hosted on the network from its options.
            keyClick(Qt.Key_Right)
            compare(screen.filterId, "all")
            compare(screen.currentSet, "sf2")
            compare(findText(screen, "Hosted by ann at 192.168.1.20 on this network"), null)
            verify(!screen.onRoom)
            verify(findChild(screen, "preview").visible)
            compare(box.y, Math.round(screen.infoTop * screen.v))
            verify(findText(screen, "Hi-Score") !== null)
            keyPress(Qt.Key_Return)
            tryVerify(function() { return findText(screen, "Host LAN Game") !== null }, 1500)
            keyRelease(Qt.Key_Return)
            compare(findText(screen, "Host Online Game"), null)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 2)
            compare(root.navigations[1].params.game, "sf2")
            verify(root.navigations[1].params.online.hosting)
            verify(root.navigations[1].params.online.lan)
        }

        // LAN Games with no game, there while LAN Play is on: why in the
        // list's place and on the help line, as the backend says it, and
        // nothing to select.
        function test_lanScreenEmpty() {
            arcadeBackend.filterList = [{ id: "lan", name: "LAN Games", sets: [], rows: [] }]
                                        .concat(arcadeBackend.filterList)
            moduleRoot.startFilter = "lan"
            var screen = openScreen()
            compare(screen.filterId, "lan")
            compare(screen.count, 0)
            verify(findText(screen, "No LAN Games") !== null)
            verify(findText(screen, "No one on this network hosts a game you have") !== null)
            // As the backend's word changes and the filters are read again.
            arcadeBackend.lanState = { name: "FinalBurn Neo Not Installed",
                                       detail: "LAN games play in FinalBurn Neo: Settings → Arcade → FinalBurn Neo" }
            arcadeBackend.gamesChanged()
            verify(findText(screen, "FinalBurn Neo Not Installed") !== null)
            verify(findText(screen, "LAN games play in FinalBurn Neo") !== null)
            compare(findText(screen, "No LAN Games"), null)
            keyPress(Qt.Key_Return)
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 0)
            // On to All Games: none of it there.
            keyClick(Qt.Key_Right)
            compare(screen.filterId, "all")
            compare(findText(screen, "FinalBurn Neo Not Installed"), null)
        }

        // A room on the local network joined from the game's screen says
        // whose game it is and joins it at its host; a game hosted there
        // says so and hosts it; and with Start Info Screen off, the router
        // joins and hosts straight from the list.
        function test_playLan() {
            var play = open("Play.qml", { game: "sf2", title: "Street Fighter II (ann)",
                                          online: { room: 1, host: "ann", lan: true } })
            verify(play.fbneo)
            verify(findText(play, "Joining ann's game on this network") !== null)
            tryCompare(play, "phase", "running", 3000)
            compare(arcadeBackend.lanJoined, [1])
            compare(arcadeBackend.joined, [])
            arcadeBackend.running = false
            arcadeBackend.finished(0, "ok", "")
            play = open("Play.qml", { game: "sf2", title: "Street Fighter II", online: { hosting: true, lan: true } })
            verify(findText(play, "Hosting on this network: others join from LAN Games") !== null)
            tryCompare(play, "phase", "running", 3000)
            compare(arcadeBackend.lanHosted, ["sf2"])
            compare(arcadeBackend.hosted, [])
            arcadeBackend.running = false
            arcadeBackend.finished(0, "ok", "")
            loader.source = ""
            appCore.added = []
            var router = openRoot({ start_info: false })
            router.playGame({ game: "sf2", title: "Street Fighter II (ann)", online: { room: 1, host: "ann", lan: true } }, {})
            tryVerify(function() { return arcadeBackend.lanJoined.length === 2 })
            compare(arcadeBackend.started, [])
            compare(arcadeBackend.joined, [])
            compare(appCore.added, [["com.osdos.arcade", "recent", "game:sf2"]])
            arcadeBackend.running = false
            arcadeBackend.finished(0, "ok", "")
            compare(router.playing, null)
            router.playGame({ game: "sf2", title: "Street Fighter II", online: { hosting: true, lan: true } }, {})
            tryVerify(function() { return arcadeBackend.lanHosted.length === 2 })
            compare(arcadeBackend.lanHosted, ["sf2", "sf2"])
            compare(arcadeBackend.hosted, [])
            compare(arcadeBackend.started, [])
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

        // A filter's name too long for the bar scrolls through it, the
        // page's number keeping its place; the next filter's name starts
        // from its beginning, and one that fits stands still.
        function test_screenBarScroll() {
            arcadeBackend.filterList = arcadeBackend.filterList.concat([
                { id: "genres/Shooter / Flying Vertical", name: "Shooter / Flying Vertical", sets: ["1942", "zerowing"] },
                { id: "publishers/Technos Japan Corporation", name: "Technos Japan Corporation", sets: ["tmnt"] }])
            var screen = openScreen()
            // The bar's own text, in the bar, which has the name as its text.
            var bar = findText(screen, "All Games", true)
            var label = null
            for (var i = 0; i < bar.children.length && !label; ++i)
                label = findText(bar.children[i], "All Games", true)
            verify(label !== null)
            var page = findText(screen, "1/3", true)
            verify(page !== null)
            var pageX = page.mapToItem(screen, 0, 0).x
            wait(2000)
            compare(label.x, 0)

            keyClick(Qt.Key_Left)
            compare(label.text, "Technos Japan Corporation")
            compare(page.text, "1/1")
            verify(label.parent.clip)
            verify(label.parent.width < label.implicitWidth)
            verify(label.parent.mapToItem(screen, label.parent.width, 0).x < pageX)
            compare(page.mapToItem(screen, 0, 0).x, pageX)
            // Cut between two letters, as AppBar's title is.
            var cell = Qt.createQmlObject('import QtQuick; TextMetrics { text: "M" }', screen)
            cell.font = label.font
            var letters = label.parent.width / cell.advanceWidth
            fuzzyCompare(letters, Math.round(letters), 0.001)
            cell.destroy()
            // At its far end, the next name, as long, starts over.
            var far = label.parent.width - label.implicitWidth
            tryCompare(label, "x", far, 6000)
            keyClick(Qt.Key_Left)
            compare(label.text, "Shooter / Flying Vertical")
            tryCompare(label, "x", 0, 200)
            wait(1000)
            compare(label.x, 0)
            tryVerify(function() { return label.x < 0 }, 4000)

            // Back to one that fits: still.
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Right)
            compare(label.text, "All Games")
            tryCompare(label, "x", 0, 200)
            verify(!label.parent.clip)
            wait(2000)
            compare(label.x, 0)
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

        // 3D Curved: a wheel opening with the game chosen in its middle row,
        // round from the list's end to its start; ▲ ▼ move it up and down
        // the rows, the wheel turning a row once it would leave them; the
        // rows going back from it, wherever it is, smaller and fainter, as
        // far back as they go wheelReach rows from it (five of nine), and
        // nearer each other as they are smaller, from the list's top to its
        // foot: nine rows where Curved has seven. The bar has the game of so
        // many. Fewer games than rows lie from the top row down, the others
        // empty; without the hint bar, ten rows.
        function test_screenWheel() {
            moduleRoot.listStyle = "3D Curved"
            var screen = openScreen()
            var row = function(number) { var t = findText(screen, number); return t ? t.parent : null }
            var top = function(r) { return Math.round(screen.wheelTops[r] * screen.v) }
            var gap = function(a, b) { return b.y - (a.y + a.height) }
            verify(screen.wheel)
            compare(screen.rowsFit, 7)
            compare(screen.perPage, 9)
            compare(screen.middleRow, 4)
            compare(screen.currentSet, "sf2")
            compare(screen.selectedRow, 4)
            verify(findText(screen, "1/15") !== null)
            var rows = ["012", "013", "014", "015", "001", "002", "003", "004", "005"].map(row)
            compare(rows[4].y, top(4))
            compare(row("006"), null)
            compare(rows[4].opacity, 1)
            // From the list's top to its foot.
            compare(rows[0].y, Math.round(screen.listTop * screen.v))
            fuzzyCompare(rows[8].y + rows[8].height, Math.round(screen.listBottom * screen.v), 1)
            for (var i = 0; i < 4; ++i) {
                verify(rows[i].y < rows[i + 1].y)
                verify(rows[i].opacity < rows[i + 1].opacity)
                verify(rows[i].height < rows[i + 1].height)
                verify(rows[i].x + rows[i].width < rows[i + 1].x + rows[i + 1].width)
                verify(rows[8 - i].opacity < rows[7 - i].opacity)
                verify(rows[8 - i].x + rows[8 - i].width < rows[7 - i].x + rows[7 - i].width)
                // Apart, and nearer the next as they go back.
                verify(gap(rows[i], rows[i + 1]) >= 1)
                verify(gap(rows[7 - i], rows[8 - i]) >= 1)
                verify(gap(rows[i], rows[i + 1]) <= gap(rows[3], rows[4]))
                // Their left ends on Curved's arc, the ends furthest left.
                compare(rows[i].x, Math.round((screen.areaLeft + 12
                                               + Math.round(24 * Math.sin(Math.PI * (i + 0.5) / 9))) * screen.u))
                compare(rows[8 - i].x, rows[i].x)
            }
            verify(rows[0].x < rows[1].x && rows[1].x < rows[2].x)
            // No two further apart than Curved's rows are.
            var curvedPitch = Math.floor((screen.listRoom - screen.rowHeight) / (screen.rowsFit - 1))
            for (i = 0; i < 8; ++i)
                verify(gap(rows[i], rows[i + 1]) <= Math.ceil((curvedPitch - screen.rowHeight) * screen.v))
            // The game chosen stands out of the arc to the left.
            compare(rows[4].x, Math.round((screen.areaLeft + 12 + 24 - 16) * screen.u))

            // Down moves it a row; the wheel stays, and the rows go back from
            // the game chosen now.
            keyClick(Qt.Key_Down)
            compare(screen.currentSet, "1942")
            verify(findText(screen, "2/15") !== null)
            compare(screen.selectedRow, 5)
            compare(row("002").y, top(5))
            compare(row("002").opacity, 1)
            verify(row("001").opacity < 1)
            verify(row("012").opacity < row("013").opacity)
            compare(row("006"), null)
            compare(row("012").y, Math.round(screen.listTop * screen.v))
            // To the bottom row, and past it: the wheel turns a row.
            for (var d = 0; d < 3; ++d)
                keyClick(Qt.Key_Down)
            compare(screen.selectedRow, 8)
            verify(row("012") !== null)
            keyClick(Qt.Key_Down)
            compare(screen.selectedRow, 8)
            verify(findText(screen, "6/15") !== null)
            compare(row("012"), null)
            compare(row("006").y, top(8))
            fuzzyCompare(row("006").y + row("006").height, Math.round(screen.listBottom * screen.v), 1)
            // Up to the top row, the rows under it going back the further
            // they are, those five rows from it and more as far back as they
            // go; and past it, round to the list's end.
            for (var k = 0; k < 8; ++k)
                keyClick(Qt.Key_Up)
            compare(screen.selectedRow, 0)
            compare(row("013").y, top(0))
            var below = ["013", "014", "015", "001", "002", "003", "004", "005", "006"].map(row)
            for (var j = 0; j < 5; ++j)
                verify(below[j + 1].opacity < below[j].opacity)
            for (j = 6; j < 9; ++j)
                compare(below[j].opacity, below[5].opacity)
            fuzzyCompare(below[5].opacity, 0.4, 0.001)
            keyClick(Qt.Key_Up)
            compare(screen.selectedRow, 0)
            verify(findText(screen, "12/15") !== null)
            compare(row("012").y, top(0))
            compare(row("006"), null)

            loader.source = ""
            moduleRoot.startFilter = "genres/Fighting"
            screen = openScreen()
            compare(screen.currentSet, "kinst")
            compare(screen.rows.filter(function(r) { return r !== null }).length, 2)
            compare(screen.rows[0].game.set, "kinst")
            compare(screen.rows[1].game.set, "sf2")
            compare(screen.selectedRow, 0)
            keyClick(Qt.Key_Down)
            compare(screen.currentSet, "sf2")
            compare(screen.selectedRow, 1)
            loader.source = ""
            root.hintBar = false
            screen = openScreen()
            compare(screen.rowsFit, 8)
            compare(screen.perPage, 10)
        }

        // The wheel with as many games as rows turns at its ends as it does
        // with more. The game kept as the list is read again keeps its row,
        // but for one found in another filter, which comes to the middle
        // row; a game played comes back to the row it was played from; and
        // fewer rows (Screen Adjustment) keep it in the last of them.
        function test_screenWheelRows() {
            moduleRoot.listStyle = "3D Curved"
            arcadeBackend.filterList[0].sets = arcadeBackend.filterList[0].sets.slice(0, 9)
            var screen = openScreen()
            compare(screen.count, 9)
            compare(screen.perPage, 9)
            compare(screen.selectedRow, 4)
            compare(screen.rows[0].place, 5)
            for (var i = 0; i < 5; ++i)
                keyClick(Qt.Key_Down)
            compare(screen.currentSet, "ffight")
            compare(screen.selectedRow, 8)
            compare(screen.rows[0].place, 6)
            for (i = 0; i < 8; ++i)
                keyClick(Qt.Key_Up)
            compare(screen.currentSet, "galaga")
            compare(screen.selectedRow, 0)
            keyClick(Qt.Key_Up)
            compare(screen.currentSet, "ffight")
            compare(screen.selectedRow, 0)
            compare(screen.rows[0].place, 5)

            // Read again, a game more ahead of it.
            arcadeBackend.filterList[0].sets = ["kinst"].concat(arcadeBackend.filterList[0].sets)
            arcadeBackend.gamesChanged()
            compare(screen.count, 10)
            compare(screen.currentSet, "ffight")
            compare(screen.selectedRow, 0)

            // Its filter gone, its last favourite with it.
            loader.source = ""
            moduleRoot.startFilter = "favorites"
            screen = openScreen()
            compare(screen.filterId, "favorites")
            keyClick(Qt.Key_Down)
            compare(screen.currentSet, "sf2")
            compare(screen.selectedRow, 1)
            arcadeBackend.filterList.splice(1, 1)
            arcadeBackend.gamesChanged()
            compare(screen.filterId, "all")
            compare(screen.currentSet, "sf2")
            compare(screen.selectedRow, 4)

            // Played from the seventh row, and back.
            loader.source = ""
            moduleRoot.startFilter = "all"
            screen = openScreen()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(screen.currentSet, "1942")
            compare(screen.selectedRow, 6)
            keyClick(Qt.Key_Space)
            var played = root.navigations[root.navigations.length - 1]
            compare(played.path, "Play.qml")
            compare(played.listState.row, 6)
            loader.source = ""
            screen = openScreen({ navListState: played.listState })
            compare(screen.currentSet, "1942")
            compare(screen.selectedRow, 6)
            moduleRoot.screenAdjustment = { top: -30, bottom: -105, left: 0, right: 0 }
            compare(screen.perPage, 6)
            compare(screen.currentSet, "1942")
            compare(screen.selectedRow, 5)
        }

        // Border OFF: no frame round the picture or the box under it, the
        // picture filling its place.
        function test_screenBorder() {
            var frames = function(screen) {
                var found = []
                for (var i = 0; i < screen.children.length; ++i) {
                    var item = screen.children[i]
                    if (item.border !== undefined && item.visible && item.x === Math.round(screen.columnX * screen.u))
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
            // MAME missing on a system that can download it (Windows): the
            // screen says select does, and select starts it once.
            arcadeBackend.problem = "MAME is not installed"
            compare(findText(screen, "DOWNLOAD MAME"), null)
            arcadeBackend.mame = { mame: "", downloaded: false, canDownload: true, plugins: false, pluginsFolder: "" }
            tryVerify(function() { return findText(screen, "DOWNLOAD MAME") !== null }, 1000)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.mameDownloads, 1)
            tryVerify(function() { return findText(screen, "Downloading MAME… 20%") !== null }, 1000)
            keyClick(Qt.Key_Return)
            compare(arcadeBackend.mameDownloads, 1)
            arcadeBackend.mameDownloading = false
            arcadeBackend.problem = ""
            arcadeBackend.mame = { mame: "/usr/games/mame", downloaded: false, canDownload: false, plugins: true,
                                   pluginsFolder: "/usr/share/games/mame/plugins" }
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

        // One whose games are still to come (RetroAchievements' list being
        // read) is waited for once MAME has answered too, and opens on its
        // first game as they come; come without it, it gives way.
        function test_screenFilterPending() {
            var all = arcadeBackend.filterList
            moduleRoot.startFilter = "achievements"
            arcadeBackend.pendingFilters = ["achievements"]
            var screen = openScreen()
            compare(screen.filterId, "all")
            compare(screen.wanted, "achievements")
            arcadeBackend.pendingFilters = []
            arcadeBackend.filterList = all.concat([{ id: "achievements", name: "RetroAchievements",
                                                     sets: ["mslug", "sf2"] }])
            arcadeBackend.gamesChanged()
            compare(screen.filterId, "achievements")
            compare(screen.currentSet, "mslug")
            loader.source = ""
            arcadeBackend.filterList = all
            arcadeBackend.pendingFilters = ["achievements"]
            screen = openScreen()
            compare(screen.filterId, "all")
            arcadeBackend.pendingFilters = []
            arcadeBackend.gamesChanged()
            compare(screen.filterId, "all")
            compare(screen.wanted, "all")
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
            verify(findText(play, "On Test OS: install mame", true) !== null)
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
        // The file of the view it shows.
        function shownFile(router) {
            for (var i = 0; i < router.children.length; ++i) {
                if (router.children[i].source !== undefined)
                    return String(router.children[i].source).replace(/.*\//, "")
            }
            return ""
        }

        // Screen unset, the module opens on the Arcade screen; set to
        // Original, on the tree.
        function test_rootScreen() {
            var router = openRoot({})
            verify(router.arcadeScreen)
            compare(shownFile(router), "ArcadeScreen.qml")
            router = openRoot({ screen: "Original" })
            verify(!router.arcadeScreen)
            compare(shownFile(router), "Games.qml")
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
            router = openRoot({ screen: "Original", start_info: false })
            compare(shownFile(router), "Games.qml")
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
            verify(findText(play, "On Test OS: install mame", true) !== null)
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

        // The visible marks at the rows' right ends (PixelIcon's mark): a
        // favourite's heart, Search's glass, a room's ▶ or lock.
        function marks(item) {
            var found = []
            if (item.mark !== undefined && item.visible && item.mark !== "")
                found.push(item.mark)
            for (var i = 0; i < item.children.length; ++i)
                found = found.concat(marks(item.children[i]))
            return found
        }
        // The labels of the lines the box shows of a room, from its top.
        function roomLineLabels(item) {
            var found = []
            if (item.objectName === "roomLine" && item.visible)
                found.push(item.label)
            for (var i = 0; i < item.children.length; ++i)
                found = found.concat(roomLineLabels(item.children[i]))
            return found
        }

        // The whole lobby as a filter of the Arcade screen's, as filters()
        // and filterGames() give it.
        function lobbyFilter(rooms) {
            return { id: "lobby", name: "RetroArch Lobby", sets: rooms.map(function(r) { return r.set }),
                     rows: rooms.map(function(r) {
                         return { set: r.set, name: r.title + " (" + r.host + ")", favorite: false, room: r.id,
                                  host: r.host, lobby: r }
                     }) }
        }

        // RetroArch Lobby on the Arcade screen, with Online Play on: every
        // room, ▶ on the one to join and a lock on one with a password; no
        // room has a picture, the box in the frame's place, from the
        // column's top, saying what the lobby says of it, the room page's
        // lines, the help line the rest; select joins one, or with nothing
        // to join opens its page, as select held does for any; no credits
        // bought for a room; back from a room's page lands on it, one of a
        // game not in the folder too, which the cursor keeps to as the
        // lobby is read again.
        function test_screenLobby() {
            var rooms = [
                lobbyRoom(11, "FinalBurn Neo", "Street Fighter II", "ann",
                          { rom: "sf2", set: "sf2", country: "es", coreVersion: "v1.0.0.03", joinable: true,
                            coreMatch: "same", retroarchMatch: "other", players: 1, minutes: 5 }),
                lobbyRoom(14, "FinalBurn Neo", "Metal Slug", "bob",
                          { rom: "mslug", set: "mslug", password: true, why: "It asks for a password" }),
                lobbyRoom(16, "Snes9x", "Super Mario Kart", "dee",
                          { why: "OSD/OS joins FinalBurn Neo's games alone: this one is Snes9x's" })]
            arcadeBackend.setLobby({ state: "ready", problem: "", yourCore: "v1.0.0.03", yourCoreState: "read",
                                     yourRetroArch: "1.19.1", yourRetroArchState: "read", rooms: rooms })
            arcadeBackend.filterList = arcadeBackend.filterList.concat([lobbyFilter(rooms)])
            moduleRoot.startFilter = "lobby"
            var screen = openScreen()
            compare(screen.filterId, "lobby")
            verify(findText(screen, "RetroArch Lobby", true) !== null)
            verify(findText(screen, "Street Fighter II (ann)") !== null)
            compare(marks(screen), ["play", "lock"])
            // The room to join: its game's, with no picture; the box, in the
            // frame's place from the column's top, has the room page's lines,
            // every one, the rest is on the help line, and no high scores.
            compare(screen.lobbyRoom.id, 11)
            verify(screen.isGame)
            verify(screen.onRoom)
            verify(!screen.gameBox)
            verify(!findChild(screen, "preview").visible)
            var box = findChild(screen, "infoBox")
            compare(box.y, Math.round(screen.areaTop * screen.v))
            compare(box.y + box.height, Math.round(screen.listBottom * screen.v))
            compare(findText(screen, "Hi-Score"), null)
            compare(screen.roomLines.map(function(line) { return line.label }),
                    ["Game", "ROM", "CRC", "Core", "Version", "RetroArch", "System", "Host", "Country", "Connection",
                     "Password", "Players", "Hosted", "Can Join"])
            compare(screen.roomRows, 15)
            compare(roomLineLabels(screen), screen.roomLines.map(function(line) { return line.label }))
            compare(screen.roomLines[3].icons, ["ok"])
            compare(screen.roomLines[4].icons, ["ok"])
            compare(screen.roomLines[5].icons, ["no"])
            verify(findText(screen, "Street Fighter II", true) !== null)
            verify(findText(screen, "1.22.2", true) !== null)
            verify(findText(screen, "ES", true) !== null)
            verify(findText(screen, "Direct", true) !== null)
            verify(findText(screen, "5M Ago", true) !== null)
            verify(findText(screen, "Select to join it") !== null)
            verify(findText(screen, "RetroArch 1.22.2 (yours 1.19.1)") !== null)
            verify(findText(screen, ":JOIN HOLD:DETAILS") !== null)
            // COIN buys nothing for a room.
            keyClick(Qt.Key_5)
            compare(moduleRoot.credits, 0)
            keyPress(Qt.Key_Return)
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "Play.qml")
            compare(root.navigations[0].params.game, "sf2")
            compare(root.navigations[0].params.online.room, 11)
            compare(root.navigations[0].params.online.host, "ann")
            compare(root.navigations[0].listState.filter, "lobby")
            compare(root.navigations[0].listState.room, 11)
            // One with a password: its page, nothing to join.
            keyClick(Qt.Key_Down)
            compare(screen.lobbyRoom.id, 14)
            verify(findText(screen, "It asks for a password") !== null)
            verify(findText(screen, ":DETAILS") !== null)
            compare(findText(screen, ":JOIN"), null)
            keyPress(Qt.Key_Return)
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 2)
            compare(root.navigations[1].path, "Lobby.qml")
            compare(root.navigations[1].params.room, 14)
            compare(root.navigations[1].listState.room, 14)
            // Another core's, of no game of the folder: no picture either,
            // the box saying its game as its host has it, its core, marked as
            // not yours, and that it can't be joined.
            keyClick(Qt.Key_Down)
            compare(screen.lobbyRoom.id, 16)
            verify(!screen.isGame)
            verify(screen.onRoom)
            verify(!findChild(screen, "preview").visible)
            compare(findText(screen, "No Picture"), null)
            compare(findText(screen, "Not Recognized"), null)
            compare(screen.roomLines[0].value, "Super Mario Kart")
            compare(screen.roomLines[2].label, "Core")
            compare(screen.roomLines[2].value, "Snes9x")
            compare(screen.roomLines[2].icons, ["no"])
            compare(screen.roomLines[screen.roomLines.length - 1].label, "Can Join")
            compare(screen.roomLines[screen.roomLines.length - 1].value, "No")
            verify(findText(screen, "this one is Snes9x's") !== null)
            // Select held, on any: its page.
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            keyPress(Qt.Key_Return)
            tryVerify(function() { return root.navigations.length === 3 }, 1500)
            keyRelease(Qt.Key_Return)
            compare(root.navigations[2].path, "Lobby.qml")
            compare(root.navigations[2].params.room, 11)
            compare(root.navigations.length, 3)
            // Back from a room's page: on it.
            loader.source = ""
            screen = openScreen({ navListState: root.navigations[1].listState })
            compare(screen.filterId, "lobby")
            compare(screen.lobbyRoom.id, 14)
            // The cursor kept on one of a game not in the folder as the lobby
            // is read again; back from its page, on it, and on it still as
            // the lobby read again puts a room before it.
            keyClick(Qt.Key_Down)
            arcadeBackend.gamesChanged()
            compare(screen.lobbyRoom.id, 16)
            keyPress(Qt.Key_Return)
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 4)
            compare(root.navigations[3].params.room, 16)
            loader.source = ""
            screen = openScreen({ navListState: root.navigations[3].listState })
            compare(screen.lobbyRoom.id, 16)
            // The rooms changed under the screen, as the backend's do, until
            // it says so: roomsChanged(), then gamesChanged().
            var lobby = arcadeBackend.filterList[arcadeBackend.filterList.length - 1]
            var more = lobbyFilter([lobbyRoom(17, "Genesis Plus GX", "Sonic the Hedgehog", "eve")].concat(rooms))
            lobby.sets = more.sets
            lobby.rows = more.rows
            arcadeBackend.roomsChanged()
            compare(screen.lobbyRoom.id, 16)
            arcadeBackend.gamesChanged()
            compare(screen.selected, 3)
            compare(screen.lobbyRoom.id, 16)
        }

        // RetroArch Lobby with no room: being read, not reached and why, or no
        // one hosting; nothing to join.
        function test_screenLobbyEmpty() {
            arcadeBackend.filterList = arcadeBackend.filterList.concat([lobbyFilter([])])
            moduleRoot.startFilter = "lobby"
            var screen = openScreen()
            compare(screen.filterId, "lobby")
            verify(findText(screen, "Reading RetroArch's lobby…") !== null)
            arcadeBackend.setLobby({ state: "failed", problem: "RetroArch's lobby can't be reached: Connection refused",
                                     rooms: [] })
            arcadeBackend.roomsChanged()
            tryVerify(function() { return findText(screen, "Lobby Not Reachable") !== null }, 1000)
            verify(findText(screen, "Connection refused") !== null)
            arcadeBackend.setLobby({ state: "ready", problem: "", rooms: [] })
            arcadeBackend.roomsChanged()
            tryVerify(function() { return findText(screen, "No one is hosting a game") !== null }, 1000)
            verify(findText(screen, "[ESC]:BACK", true) !== null)
            keyPress(Qt.Key_Return)
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 0)
        }

        // A room's lines as the box has room for them, from its top: fewer
        // in a shorter area, more with the hint bar and the help line off,
        // every one once they all fit.
        function test_screenRoomRows() {
            var rooms = [lobbyRoom(11, "FinalBurn Neo", "Street Fighter II", "ann",
                                   { rom: "sf2", set: "sf2", country: "es", coreVersion: "v1.0.0.03", joinable: true,
                                     coreMatch: "same", retroarchMatch: "other", crc: "1A2B3C4D", subsystem: "neogeo",
                                     spectatePassword: true, players: 1, spectators: 0, minutes: 5 })]
            arcadeBackend.setLobby({ state: "ready", problem: "", yourCore: "v1.0.0.03", yourCoreState: "read",
                                     yourRetroArch: "1.19.1", yourRetroArchState: "read", rooms: rooms })
            arcadeBackend.filterList = arcadeBackend.filterList.concat([lobbyFilter(rooms)])
            moduleRoot.startFilter = "lobby"
            moduleRoot.screenAdjustment = { top: -135, bottom: 0, left: 0, right: 0 }
            var screen = openScreen()
            compare(screen.lobbyRoom.id, 11)
            compare(screen.roomLines.length, 17)
            compare(screen.roomRows, 10)
            compare(roomLineLabels(screen),
                    ["Game", "ROM", "CRC", "Core", "Version", "Subsystem", "RetroArch", "System", "Host", "Country"])
            root.hintBar = false
            root.helpLine = false
            compare(screen.roomRows, 14)
            compare(roomLineLabels(screen).length, 14)
            moduleRoot.screenAdjustment = { top: 0, bottom: 0, left: 0, right: 0 }
            compare(screen.roomRows, 18)
            compare(roomLineLabels(screen),
                    ["Game", "ROM", "CRC", "Core", "Version", "Subsystem", "RetroArch", "System", "Host", "Country",
                     "Connection", "Password", "Watch Password", "Players", "Spectators", "Hosted", "Can Join"])
        }

        // The lobby's page opened on a room (the Arcade screen's): that room's
        // lines at once, Join joining it, back leaving the page.
        function test_lobbyRoomPage() {
            arcadeBackend.setLobby({ state: "ready", problem: "", yourCore: "", yourCoreState: "read",
                                     yourRetroArch: "1.22.2", yourRetroArchState: "read", rooms: [
                lobbyRoom(11, "FinalBurn Neo", "Street Fighter II", "ann", { rom: "sf2", set: "sf2", joinable: true }),
                lobbyRoom(16, "Snes9x", "Super Mario Kart", "dee",
                          { why: "OSD/OS joins FinalBurn Neo's games alone: this one is Snes9x's" })] })
            var page = open("Lobby.qml", { room: 16 })
            compare(page.room.id, 16)
            verify(findText(page, "Snes9x • Hosted by dee") !== null)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
            loader.source = ""
            page = open("Lobby.qml", { room: 11 })
            compare(page.lines[0].type, "join")
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].params.online.room, 11)
        }

        // Search, the last filter: its own row, then the games its words
        // find, its keys in the picture's frame, with no box on them while
        // the list has the keys and no credits over them, nor bought there.
        // ► goes onto them at their left end, as select on its own row does;
        // the list follows each letter typed, how many it finds in the bar,
        // none found said so; OK goes down to the first game found, back
        // returns to the list with the words kept; typing on a keyboard goes
        // onto the keys with the letter typed. ◄ ► pressed at the keys' ends
        // go round the filters, ► from Search on to the first, on the game
        // ◄ ► came to Search with. Coming back from a game found keeps the
        // words, and its own row keeps the cursor as the games are read
        // again; the screen opened afresh has none. On the wheel, Search's
        // row at its top, the first game found under it after OK.
        function test_screenSearch() {
            var all = arcadeBackend.filterList[0]
            arcadeBackend.filterList = arcadeBackend.filterList.concat([
                { id: "years/1990s", name: "1990s", sets: ["sf2", "ffight", "xmen6p", "dino"] },
                { id: "search", name: "Search", sets: [""].concat(all.sets) }])
            var screen = openScreen()
            compare(arcadeBackend.searches, [""])
            var keys = findChild(screen, "searchKeys")
            verify(!keys.visible)
            // ◄ from All Games round to it, the last.
            keyClick(Qt.Key_Left)
            compare(screen.filterId, "search")
            verify(screen.onSearch)
            verify(keys.visible)
            verify(!screen.typing)
            verify(!keys.live)
            compare(marks(screen).indexOf("search"), 0)
            verify(findText(screen, "15 Found", true) !== null)
            verify(findText(screen, ":SEARCH") !== null)
            verify(findText(screen, "► or select to type what to look for") !== null)
            compare(findText(screen, "Credit "), null)
            // COIN buys nothing where no credit is shown.
            keyClick(Qt.Key_5)
            compare(moduleRoot.credits, 0)
            // ► onto the keys, at the left end of their row.
            keyClick(Qt.Key_Right)
            compare(screen.filterId, "search")
            verify(screen.typing)
            verify(keys.live)
            compare(keys.col, 0)
            verify(!screen.takesCoin)
            verify(findText(screen, ":DONE") !== null)
            // A letter at a time.
            keyClick(Qt.Key_S)
            compare(arcadeBackend.searchWords, "S")
            verify(findText(screen, "4 Found", true) !== null)
            keyClick(Qt.Key_T)
            verify(findText(screen, "2 Found", true) !== null)
            keyClick(Qt.Key_R)
            verify(findText(screen, "1 Found", true) !== null)
            verify(findText(screen, "STR", true) !== null)
            verify(findText(screen, "Street Fighter II", true) !== null)
            keyClick(Qt.Key_X)
            verify(findText(screen, "0 Found", true) !== null)
            verify(findText(screen, "No Game Found") !== null)
            keyClick(Qt.Key_Backspace)
            verify(findText(screen, "1 Found", true) !== null)
            compare(findText(screen, "No Game Found"), null)
            // OK: down to the game found, the keys still there, the box on the
            // game, no credits over the keys; it plays.
            keyClick(Qt.Key_Return)
            verify(!screen.typing)
            verify(keys.visible)
            compare(screen.currentSet, "sf2")
            verify(screen.gameBox)
            verify(findText(screen, "[ESC]:BACK [ENTER]:PLAY HOLD:OPTIONS", true) !== null)
            compare(findText(screen, "Credit "), null)
            keyPress(Qt.Key_Return)
            keyRelease(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].params.game, "sf2")
            compare(root.navigations[0].listState.filter, "search")
            // Back from it: the words kept, on the game.
            loader.source = ""
            screen = openScreen({ navListState: root.navigations[0].listState })
            keys = findChild(screen, "searchKeys")
            compare(screen.filterId, "search")
            compare(screen.currentSet, "sf2")
            compare(arcadeBackend.searches[arcadeBackend.searches.length - 1], "STR")
            compare(keys.text, "STR")
            verify(findText(screen, "STR", true) !== null)
            // Select on its own row: onto the keys; back returns to the list,
            // the words as typed. The row kept as the games are read again.
            keyClick(Qt.Key_Up)
            verify(screen.onSearch)
            arcadeBackend.gamesChanged()
            verify(screen.onSearch)
            keyPress(Qt.Key_Return)
            keyRelease(Qt.Key_Return)
            verify(screen.typing)
            keyClick(Qt.Key_G)
            compare(arcadeBackend.searchWords, "STRG")
            keyClick(Qt.Key_Escape)
            verify(!screen.typing)
            compare(arcadeBackend.searchWords, "STRG")
            compare(root.backs, 0)
            // Typed on a keyboard: onto the keys with it.
            keyClick(Qt.Key_A)
            verify(screen.typing)
            compare(arcadeBackend.searchWords, "STRGA")
            // ► at the keys' right end: the first filter, the list with the
            // keys.
            keys.moveTo(0, 0)
            for (var i = 0; i < 9; ++i)
                keyClick(Qt.Key_Right)
            compare(keys.col, 9)
            compare(screen.filterId, "search")
            keyClick(Qt.Key_Right)
            compare(screen.filterId, "all")
            verify(!screen.typing)
            verify(!keys.visible)
            keyClick(Qt.Key_Down)
            compare(screen.selected, 1)
            // ◄ at the keys' left end: the filter before Search, past
            // Search's own row on the game ◄ ► came to it with (X-Men, the
            // 1990s' third).
            for (i = 0; i < 4; ++i)
                keyClick(Qt.Key_Up)
            compare(screen.currentSet, "xmen6p")
            keyClick(Qt.Key_Left)
            compare(screen.filterId, "search")
            keyClick(Qt.Key_Right)
            verify(screen.typing)
            keyClick(Qt.Key_Left)
            compare(screen.filterId, "years/1990s")
            verify(!screen.typing)
            compare(screen.selected, 2)
            compare(screen.currentSet, "xmen6p")
            keyClick(Qt.Key_Right)
            verify(screen.onSearch)
            keyClick(Qt.Key_Right)
            keys.moveTo(0, 0)
            for (i = 0; i < 10; ++i)
                keyClick(Qt.Key_Right)
            compare(screen.filterId, "all")
            compare(screen.currentSet, "xmen6p")
            // Afresh: no words.
            loader.source = ""
            screen = openScreen()
            compare(arcadeBackend.searchWords, "")
            compare(findChild(screen, "searchKeys").text, "")
            // On 3D Curved's wheel: its row at the top, the first game found
            // under it after OK, nothing typed.
            loader.source = ""
            moduleRoot.listStyle = "3D Curved"
            screen = openScreen()
            keys = findChild(screen, "searchKeys")
            keyClick(Qt.Key_Left)
            verify(screen.onSearch)
            compare(screen.selectedRow, 0)
            verify(screen.count > screen.perPage)
            keyClick(Qt.Key_Right)
            keys.moveTo(keys.rows.length - 1, keys.okCol)
            keyClick(Qt.Key_Return)
            verify(!screen.typing)
            compare(screen.selected, 1)
            compare(screen.selectedRow, 1)
        }
    }
}
