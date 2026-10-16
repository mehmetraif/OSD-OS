import QtQuick
import Components

// The Arcade module's games as a horizontal tree (see TreeBrowser): Recently
// Played, Favorites and Search, then All Games and the games by players,
// decade and maker, from what MAME says of each set in the games folder
// (ArcadeBackend), and the sets MAME has no game of under Not Recognized.
// Select on a game plays it (Root.qml's playGame), right on it offers its
// options (EntryOptions: its favourite, Play at Startup), and the line under
// the tree says what the game is. Coming back from a game restores the tree
// from the listState handed to navigateTo; a game played from the tree
// itself (Start Info Screen off) leaves it as it was. Opened with the
// startup favourite (navParams.startupPlay), it plays that at once.
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

    // Plays a game; trail is where coming back lands.
    function play(item, trail) {
        moduleRoot.playGame(item.game, item.name, { trail: trail })
    }

    // The startup favourite, played as if chosen in Favorites, so coming back
    // from it lands there. Only as the view first opens: coming back from the
    // game brings navListState instead.
    Component.onCompleted: {
        if (!navParams.navListState && navParams.startupPlay)
            Qt.callLater(playAtStartup, navParams.startupPlay)
    }
    function playAtStartup(entry) {
        var favorites = arcadeBackend.requestEntries("favorites")
        for (var i = 0; i < favorites.length; ++i) {
            if (favorites[i].path === entry.path) {
                play(favorites[i], [{ path: "top", sel: 1, name: "", pushed: false },
                                    { path: "favorites", sel: i, name: "Favorites", pushed: false }])
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
            else if (item.game)
                gamesRoot.play(item, tree.trailState())
        }
        onOptionsRequested: function(item) {
            if (item.game)
                options.offer({ name: item.name, path: item.path, isFolder: false })
        }
        onLeaveRequested: gamesRoot.goBack()
    }

    Connections {
        target: arcadeBackend
        function onEntriesReady(path) { tree.refresh(path, true) }
        // A game played from the tree, now on Recently Played.
        function onFinished() { tree.refresh("recent", true) }
    }

    // What the entry under the cursor is: a game's year, maker, players and
    // file, or why a set isn't one; where the games are, on anything else.
    // What stands in the way of the list comes first.
    HelpLine {
        id: infoLine
        visible: !tree.rootEmpty && !osk.visible && !options.visible
        readonly property var entry: tree.currentEntry
        text: gamesRoot.problem !== "" ? gamesRoot.problem
              : entry && (entry.game || entry.kind === "unknown") ? entry.detail
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
        readonly property bool onGame: !!entry && !!entry.game
        text: root.hints.back + ":BACK "
              + (onGame ? String(root.hints.navigate).replace("]", "◄]") + ":NAVIGATE "
                          + root.hints.browse + ":OPTIONS "
                        : root.hints.arrows + ":NAVIGATE ")
              + (entry && entry.kind === "unknown" ? ""
                 : root.hints.select + (onGame ? ":PLAY" : entry && entry.kind === "search" ? ":SEARCH" : ":OPEN"))
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

    EntryOptions {
        id: options
        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        onFavoritesEdited: tree.refresh("favorites")
        onClosed: tree.forceActiveFocus()
    }
}
