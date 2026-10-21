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
// shown and again every 5 s while they are. Coming back from a game
// restores the tree from the listState handed to navigateTo; a game played
// from the tree itself (Start Info Screen off) leaves it as it was. Opened
// with the startup favourite (navParams.startupPlay), it plays that at once,
// the tree open on it in Favorites.
FocusScope {
    id: gamesRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})
    // Context properties read null while the module's Loader tears this view
    // down, hence the guards.
    readonly property string gamesFolder: arcadeBackend ? arcadeBackend.gamesFolder() : ""
    readonly property string problem: arcadeBackend ? arcadeBackend.problem : ""
    readonly property bool folderReadable: arcadeBackend ? arcadeBackend.folderReadable : true
    readonly property bool scanning: arcadeBackend ? arcadeBackend.scanning : false

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    // Plays a game, or joins a room of the lobby or the local network
    // (item.room, item.lan), or with online ({ hosting: true, lan }) hosts
    // it; trail is where coming back lands.
    function play(item, trail, online) {
        var params = { game: item.game, title: item.name }
        if (item.room !== undefined)
            params.online = { room: item.room, host: item.host, lan: !!item.lan }
        else if (online)
            params.online = online
        gamesRoot.navigateTo("Play.qml", params, { trail: trail })
    }

    // The startup favourite, played as if chosen in Favorites, so coming back
    // from it lands there. Only as the view first opens: coming back from the
    // game brings navListState instead. Once the folder has been read
    // (ArcadeBackend reads it as the module opens): till then it waits here.
    property var startupEntry: null
    Component.onCompleted: {
        if (!navParams.navListState && navParams.startupPlay)
            Qt.callLater(playAtStartup, navParams.startupPlay)
    }
    function playAtStartup(entry) {
        if (arcadeBackend.reading) {
            startupEntry = entry
            return
        }
        var favorites = arcadeBackend.requestEntries("favorites")
        for (var i = 0; i < favorites.length; ++i) {
            if (favorites[i].path === entry.path) {
                var trail = [{ path: "top", sel: 1, name: "", pushed: false },
                             { path: "favorites", sel: i, name: "Favorites", pushed: false }]
                // There as it plays, for a game played from the tree itself.
                tree.reopen(trail)
                play(favorites[i], trail)
                return
            }
        }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: tree.folderName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Nothing in the folder: what stands in the way, and where the games go.
    NoGames {
        visible: tree.rootEmpty
        problem: gamesRoot.problem
        folderReadable: gamesRoot.folderReadable
        gamesFolder: gamesRoot.gamesFolder
    }

    TreeBrowser {
        id: tree
        anchors.fill: parent
        focus: true
        visible: !rootEmpty
        rootPath: "top"
        // The info line's room, while it shows (Help Line on).
        reservedBottom: infoLine.visible && infoLine.shown ? areaBottom - infoLine.y : 0
        savedTrail: gamesRoot.navListState.trail || []
        fetch: function(path, preview) {
            if (!arcadeBackend)
                return []
            var entries = arcadeBackend.requestEntries(path)
            return entries === undefined ? null : entries
        }
        onActivated: function(item) {
            if (item.kind === "search")
                osk.open("")
            else if (item.kind === "lobby")
                gamesRoot.navigateTo("Lobby.qml", {}, { trail: tree.trailState() })
            else if (item.game)
                gamesRoot.play(item, tree.trailState())
        }
        onOptionsRequested: function(item) {
            if (item.game && item.room === undefined)
                options.offer({ name: item.name, path: item.path, isFolder: false })
        }
        onLeaveRequested: gamesRoot.goBack()
    }

    // The lobby read again while its rooms are shown: they last a minute
    // there, and hosts say they are still there every 20 s.
    Timer {
        interval: 20000
        repeat: true
        running: tree.folderName === "Online Games" && !!arcadeBackend && !arcadeBackend.running
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
        running: tree.folderName === "LAN Games" && !!arcadeBackend && !arcadeBackend.running
        onTriggered: arcadeBackend.refreshLanRooms()
    }

    Connections {
        target: arcadeBackend
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
    // What stands in the way of the list comes first.
    HelpLine {
        id: infoLine
        visible: !tree.rootEmpty && !osk.visible && !options.visible
        readonly property var entry: tree.currentEntry
        text: gamesRoot.problem !== "" ? gamesRoot.problem
              : entry && (entry.game || entry.kind === "unknown" || entry.kind === "message" || entry.kind === "lobby")
                ? entry.detail
              : entry && entry.kind === "search" ? "Find a game by its name"
              : gamesRoot.scanning ? "Reading the games…"
              : "Games folder: " + gamesRoot.gamesFolder
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Footer
    HintBar {
        visible: !osk.visible && !options.visible
        // Select opens a folder and plays a game, and right on a game offers
        // its options rather than moving.
        readonly property var entry: tree.currentEntry
        readonly property bool onRoom: !!entry && entry.room !== undefined
        readonly property bool onGame: !!entry && !!entry.game && !onRoom
        text: root.hints.back + ":BACK "
              + (onGame ? String(root.hints.navigate).replace("]", "◄]") + ":NAVIGATE "
                          + root.hints.browse + ":OPTIONS "
                        : root.hints.arrows + ":NAVIGATE ")
              + (entry && (entry.kind === "unknown" || entry.kind === "message") ? ""
                 : root.hints.select + (onRoom ? ":JOIN" : onGame ? ":PLAY"
                                        : entry && entry.kind === "search" ? ":SEARCH" : ":OPEN"))
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
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

    // A game's options: its Game Details (Details.qml) first, its engine
    // among them where MAME and FinalBurn Neo both have it, and with Online
    // Play or LAN Play on, hosting it.
    property int engineRevision: 0
    readonly property string optionsSet: options.entry && String(options.entry.path).indexOf("game:") === 0
                                         ? String(options.entry.path).substring(5) : ""
    EntryOptions {
        id: options
        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        firstChoices: [{ label: "Game Details", action: "details" }]
        moreChoices: (gamesRoot.engineRevision,
                      (engines.offered(gamesRoot.optionsSet)
                       ? [{ label: engines.lineFor(gamesRoot.optionsSet), action: "engine" }] : [])
                      .concat(arcadeBackend && arcadeBackend.canHost(gamesRoot.optionsSet)
                              ? [{ label: "Host Online Game", action: "host" }] : [])
                      .concat(arcadeBackend && arcadeBackend.canHostLan(gamesRoot.optionsSet)
                              ? [{ label: "Host LAN Game", action: "hostLan" }] : []))
        onActivated: function(action) {
            if (action === "details")
                gamesRoot.navigateTo("Details.qml", { game: gamesRoot.optionsSet, title: options.entry.name },
                                     { trail: tree.trailState() })
            else if (action === "engine")
                engines.offer(gamesRoot.optionsSet, options.entry.name)
            else if (action === "host" || action === "hostLan")
                gamesRoot.play({ game: gamesRoot.optionsSet, name: options.entry.name }, tree.trailState(),
                               { hosting: true, lan: action === "hostLan" })
        }
        onFavoritesEdited: tree.refresh("favorites")
        onClosed: if (!engines.visible) tree.forceActiveFocus()
    }
    EngineChooser {
        id: engines
        anchors.fill: parent
        onEngineChosen: gamesRoot.engineRevision++
        onClosed: tree.forceActiveFocus()
    }
}
