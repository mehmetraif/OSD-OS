import QtQuick
import Components

// The Arcade module's games as a horizontal tree (see TreeBrowser): Recently
// Played, Favorites and Search, then All Games and the games by players,
// decade and maker, from what MAME says of each set in the games folder, or
// FinalBurn Neo's list of one MAME doesn't know (ArcadeBackend), and the sets
// neither plays under Not Recognized.
// Select on a game plays it (navigateTo Play.qml, which Root.qml plays as
// Start Info Screen says), right on it offers its options (EntryOptions: Game
// Details, its favourite, Play at Startup, its engine, hosting it online),
// and the line under the tree says what the game is. With Online Play on,
// Online Games lists the rooms of RetroArch's lobby that host games of the
// folder (ArcadeBackend::rooms()), read again every 20 s while they are
// shown: select joins one. Its last line, RetroArch Lobby, opens the whole
// lobby (Lobby.qml). With LAN Play on, LAN Games lists those hosted on the
// local network (ArcadeBackend::lanRooms()), asked for as they come to be
// shown and again every 5 s while they are. A game's screen, Game Details
// and the lobby open over the tree (Root.qml's playLayer, pageLayer), which
// coming back finds as it was left. Opened with the startup favourite
// (navParams.startupPlay), it plays that at once, the tree open on it in
// Favorites.
FocusScope {
    id: gamesRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    // With nothing listed the tree is hidden, still with the keys: back it
    // hands on (leaveRequested), select it leaves to this scope, which
    // downloads MAME where that is what is missing and the system can.
    Keys.onPressed: function(event) {
        if (!tree.rootEmpty || (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter))
            return
        if (!event.isAutoRepeat && moduleRoot.canDownloadMame && !moduleRoot.downloadingMame)
            arcadeBackend.downloadMame()
        event.accepted = true
    }

    // Plays a game, or joins a room of the lobby or the local network
    // (item.room, item.lan), or with online ({ hosting: true, lan }) hosts
    // it.
    function play(item, online) {
        var params = { game: item.game, title: item.name }
        if (item.room !== undefined)
            params.online = { room: item.room, host: item.host, lan: !!item.lan }
        else if (online)
            params.online = online
        gamesRoot.navigateTo("Play.qml", params, {})
    }

    // The startup favourite, played as if chosen in Favorites, so coming back
    // from it lands there, as the view opens. Once the folder has been read
    // (ArcadeBackend reads it as the module opens): till then it waits here.
    property var startupEntry: null
    Component.onCompleted: {
        if (navParams.startupPlay)
            Qt.callLater(playAtStartup, navParams.startupPlay)
    }
    function playAtStartup(entry) {
        if (arcadeBackend.reading) {
            startupEntry = entry
            return
        }
        // Favorites' row in the tree's first folder: where the backend lists
        // it, after Online Games and LAN Games while those are on.
        var top = arcadeBackend.requestEntries("top") || []
        var row = 0
        while (row < top.length && top[row].path !== "favorites")
            ++row
        var favorites = arcadeBackend.requestEntries("favorites")
        for (var i = 0; i < favorites.length; ++i) {
            if (favorites[i].path === entry.path) {
                var trail = [{ path: "top", sel: row, name: "", pushed: false },
                             { path: "favorites", sel: i, name: "Favorites", pushed: false }]
                // There as it plays, for a game played from the tree itself.
                tree.reopen(trail)
                play(favorites[i])
                return
            }
        }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: tree.folderName
    }

    // Nothing in the folder: what stands in the way, and where the games go.
    NoGames {
        visible: tree.rootEmpty
    }

    TreeBrowser {
        id: tree
        anchors.fill: parent
        focus: true
        visible: !rootEmpty
        rootPath: "top"
        // The info line's room, while it shows (Help Line on).
        reservedBottom: infoLine.visible && infoLine.shown ? areaBottom - infoLine.y : 0
        fetch: function(path, preview) {
            if (!arcadeBackend)
                return []
            // A glance at All Games' branch shows two names and "…": not
            // worth every game's row made for it (thousands, on the GUI
            // thread). Opened, it is listed.
            if (preview && path === "all")
                return null
            var entries = arcadeBackend.requestEntries(path)
            return entries === undefined ? null : entries
        }
        onActivated: function(item) {
            if (item.kind === "search")
                osk.open("")
            else if (item.kind === "lobby")
                gamesRoot.navigateTo("Lobby.qml", {}, {})
            else if (item.game)
                gamesRoot.play(item)
        }
        onOptionsRequested: function(item) {
            if (item.game && item.room === undefined)
                options.offer({ name: item.name, path: item.path, isFolder: false })
        }
        onLeaveRequested: gamesRoot.goBack()
    }

    // Whether the rooms are worth asking for again: the list is on the
    // screen, no game is playing over it, and the screen saver isn't up. A
    // cabinet left on a list of rooms asked the network every few seconds
    // for as long as it stood there, and the lobby with it.
    readonly property bool roomsShown: gamesRoot.visible && !!arcadeBackend && !moduleRoot.away

    // The lobby read as its rooms come to be shown, and again while they
    // are: they last a minute there, and hosts say they are still there
    // every 20 s.
    Timer {
        objectName: "lobbyRefresh"
        interval: 20000
        repeat: true
        triggeredOnStart: true
        running: tree.folderName === "Online Games" && gamesRoot.roomsShown
        onTriggered: arcadeBackend.refreshRooms()
    }
    // The local network asked as its rooms come to be shown, and again while
    // they are: a host is there as soon as it hosts, and gone once it has
    // stopped.
    Timer {
        objectName: "lanRefresh"
        interval: 5000
        repeat: true
        triggeredOnStart: true
        running: tree.folderName === "LAN Games" && gamesRoot.roomsShown
        onTriggered: arcadeBackend.refreshLanRooms()
    }

    Connections {
        target: arcadeBackend
        ignoreUnknownSignals: true
        function onEntriesReady(path) { tree.refresh(path, true) }
        // A game played from the tree, now on Recently Played.
        function onFinished() { tree.refresh("recent", true) }
        function onReadingChanged() {
            var entry = gamesRoot.startupEntry
            if (entry && !arcadeBackend.reading) {
                gamesRoot.startupEntry = null
                gamesRoot.playAtStartup(entry)
            }
        }
    }

    // What the entry under the cursor is: a game's year, maker, players and
    // file, or why a set isn't one; where the games are, on anything else.
    // A room on the local network, the warning of this machine's link to
    // its host first, where there is one (2.4 GHz Wi-Fi with Bluetooth
    // pads, lanLinkWarning()). What stands in the way of the list comes
    // first.
    HelpLine {
        id: infoLine
        visible: !tree.rootEmpty && !osk.visible && !options.menuUp
        readonly property var entry: tree.currentEntry
        text: moduleRoot.problem !== "" ? moduleRoot.problemLine
              : entry && (entry.game || entry.kind === "unknown" || entry.kind === "message" || entry.kind === "lobby")
                ? (entry.lan && arcadeBackend ? [arcadeBackend.lanLinkWarning(entry.room), entry.detail || ""]
                                                .filter(Boolean).join("\n")
                                              : entry.detail)
              : entry && entry.kind === "search" ? "Find a game by its name"
              : moduleRoot.scanning ? "Reading the games…"
              : "Games folder: " + moduleRoot.gamesFolder
    }

    HintBar {
        visible: !osk.visible && !options.menuUp
        // Select opens a folder and plays a game, and right on a game offers
        // its options rather than moving.
        readonly property var entry: tree.currentEntry
        readonly property bool onRoom: !!entry && entry.room !== undefined
        readonly property bool onGame: !!entry && !!entry.game && !onRoom
        // BACK at the top goes nowhere under Cabinet Lock (Main.qml's
        // leavingBarred), and isn't offered there.
        text: (tree.active > 0 || !root.leavingBarred ? root.hints.back + ":BACK " : "")
              + (onGame ? String(root.hints.navigate).replace("]", "◄]") + ":NAVIGATE "
                          + root.hints.browse + ":OPTIONS "
                        : root.hints.arrows + ":NAVIGATE ")
              + (entry && (entry.kind === "unknown" || entry.kind === "message") ? ""
                 : root.hints.select + (onRoom ? ":JOIN" : onGame ? ":PLAY"
                                        : entry && entry.kind === "search" ? ":SEARCH" : ":OPEN"))
    }

    OnScreenKeyboard {
        id: osk
        anchors.fill: parent
        title: "Search " + moduleRoot.moduleName
        onAccepted: function(text) {
            tree.forceActiveFocus()
            var path = "search/" + text
            tree.refresh(path)
            tree.openItem({ name: "Search: " + text, path: path })
        }
        onCanceled: tree.forceActiveFocus()
    }

    // A game's options, Game Details first, for the game they were opened
    // for.
    GameOptions {
        id: options
        anchors.fill: parent
        firstChoices: [{ label: "Game Details", action: "details" }]
        onActivated: function(action) {
            if (action === "details")
                gamesRoot.navigateTo("Details.qml", { game: options.set, title: options.title }, {})
            else if (action === "host" || action === "hostLan")
                gamesRoot.play({ game: options.set, name: options.title }, { hosting: true, lan: action === "hostLan" })
        }
        onFavoritesEdited: tree.refresh("favorites")
        onClosed: tree.forceActiveFocus()
    }
}
