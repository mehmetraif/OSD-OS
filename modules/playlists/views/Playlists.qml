import QtQuick
import Components

// The module's first page: NEW ONLINE PLAYLIST and NEW OFFLINE PLAYLIST, then
// every playlist with its kind and how many videos it has (an offline one,
// how many of them are on the device). Select opens one, or names a new one
// on the on-screen keyboard and opens it.
FocusScope {
    id: listRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var rows: []
    // The kind of playlist being named on the keyboard.
    property string newKind: ""

    function rowsFrom(playlists) {
        var out = [{ type: "new", kind: "online", label: "New Online Playlist" },
                   { type: "new", kind: "offline", label: "New Offline Playlist" }]
        if (playlists.length > 0)
            out.push({ type: "section", label: "Playlists" })
        for (var i = 0; i < playlists.length; i++) {
            var p = playlists[i]
            out.push({ type: "playlist", id: p.id, label: p.name, kind: p.kind,
                       value: p.kind === "offline" ? p.ready + "/" + p.count + " Offline"
                                                   : p.count + " Online" })
        }
        return out
    }

    function reload() {
        var current = list.currentIndex
        rows = rowsFrom(playlistsBackend ? playlistsBackend.playlists() : [])
        list.currentIndex = Math.max(0, Math.min(current, rows.length - 1))
    }

    function open(id) {
        listRoot.navigateTo("Playlist.qml", { playlistId: id }, { currentIndex: list.currentIndex })
    }

    Component.onCompleted: {
        reload()
        if (navListState.currentIndex !== undefined)
            list.currentIndex = Math.min(navListState.currentIndex, rows.length - 1)
        // The list left playing behind the menus (the main menu's row for
        // it): its page, which goes on to its player. Only as the view first
        // opens: coming back to it brings navListState instead.
        var behind = navParams.resumePlayer
        if (!navParams.navListState && behind && behind.playlistId)
            Qt.callLater(function() {
                listRoot.navigateTo("Playlist.qml", { playlistId: behind.playlistId, resumePlayer: behind },
                                    { currentIndex: list.currentIndex })
            })
    }

    Connections {
        target: playlistsBackend
        function onPlaylistsChanged() { listRoot.reload() }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    MenuList {
        id: list
        model: listRoot.rows
        focus: !osk.visible

        Keys.onReturnPressed: {
            var row = listRoot.rows[currentIndex]
            if (!row)
                return
            if (row.type === "new") {
                listRoot.newKind = row.kind
                osk.title = row.kind === "offline" ? "Offline Playlist Name" : "Online Playlist Name"
                osk.open("")
            } else if (row.type === "playlist") {
                listRoot.open(row.id)
            }
        }
        // ► opens a playlist's page as select does.
        Keys.onRightPressed: {
            var row = listRoot.rows[currentIndex]
            if (row && row.type === "playlist")
                listRoot.open(row.id)
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                listRoot.goBack()
                event.accepted = true
            }
        }

        // A playlist's line leads to its page: it ends in a ►.
        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            heading: modelData.type === "section"
            keepValue: modelData.type === "playlist"
            label: modelData.label || ""
            value: modelData.value || ""
            opens: modelData.type === "playlist"
            selected: list.currentIndex === index
        }
    }

    HelpLine {
        visible: !osk.visible
        readonly property var row: listRoot.rows[list.currentIndex]
        text: !row ? ""
            : row.type === "new" && row.kind === "online"
              ? "Plays each video from where it lives, Local Files, YouTube, Jellyfin or Emby; YouTube and the servers need the network"
            : row.type === "new"
              ? "Downloads each video to " + (playlistsBackend ? playlistsBackend.downloadFolder() : "the device")
                + " once, then plays without the network"
            : row.kind === "offline" ? "Offline: plays the videos downloaded to the device"
            : "Online: plays each video from where it lives"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        visible: !osk.visible
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select
              + (listRoot.rows[list.currentIndex] && listRoot.rows[list.currentIndex].type === "new" ? ":NEW" : ":OPEN")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    OnScreenKeyboard {
        id: osk
        anchors.fill: parent
        onAccepted: function(text) {
            var id = playlistsBackend.createPlaylist(text, listRoot.newKind)
            list.forceActiveFocus()
            listRoot.reload()
            listRoot.open(id)
        }
        onCanceled: list.forceActiveFocus()
    }
}
