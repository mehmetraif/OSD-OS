import QtQuick
import Components
import "Rooms.js" as Rooms

// RetroArch's whole netplay lobby (lobby.libretro.com), with Online Play on:
// every room hosted, of every core, from ArcadeBackend::lobby(), under a
// heading of its core, FinalBurn Neo's first, read again every 20 s while
// the page is open. A room's line is its game and who hosts it, with ▶ at
// its end for one that can be joined and a lock for one with a password;
// the help line says what the lobby says of it. Select opens the room: each
// of what the lobby says of it on a line of its own (its game, its ROM and
// the ROM's CRC, the core and its version, the host's RetroArch and system,
// who hosts it and where, how it is reached, its passwords, players and
// spectators, how long ago it was hosted, and whether it can be joined, why
// not), the core, its version and RetroArch's marked ok where they are
// yours and no where not, the help line saying yours or why it isn't known;
// with Join first for a room of a game in the folder that FinalBurn Neo
// plays, which joins it as Online Games does. Back goes back to the rooms,
// then to where the page was opened from. Opened on a room
// (navParams.room: the Arcade screen's RetroArch Lobby), the page is that
// room's, and back from it goes back there.
FocusScope {
    id: lobbyRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    // Read again as the lobby comes. Context properties read null while the
    // module's Loader tears this view down, hence the guards.
    property int revision: 0
    readonly property var lobby: (revision, arcadeBackend ? arcadeBackend.lobby() : ({ state: "loading", rooms: [] }))
    readonly property var rooms: lobby.rooms || []

    // The rooms' lines: a heading over each core's rooms, which come
    // FinalBurn Neo's first, then by core and game.
    readonly property var rows: {
        var made = []
        var core = null
        for (var i = 0; i < rooms.length; ++i) {
            if (rooms[i].core !== core) {
                core = rooms[i].core
                made.push({ type: "section", label: core })
            }
            made.push(rooms[i])
        }
        return made
    }

    // The room open, whose lines are shown; null for the rooms.
    property var room: null
    readonly property var lines: room ? Rooms.linesOf(room, lobby) : []
    // The room the cursor is on, kept there as the lobby is read again, and
    // as coming back from a game it joined; or the one room of the page.
    readonly property bool roomPage: navParams.room !== undefined
    property int roomId: roomPage ? navParams.room : navListState.room !== undefined ? navListState.room : -1

    // How many rooms, FinalBurn Neo's and those to join; or why there are
    // none.
    readonly property string summary: {
        if (lobby.state !== "ready" || rooms.length === 0)
            return Rooms.emptyLine(lobby)
        var fbneo = 0
        var join = 0
        for (var i = 0; i < rooms.length; ++i) {
            if (rooms[i].core === "FinalBurn Neo")
                ++fbneo
            if (rooms[i].joinable)
                ++join
        }
        return rooms.length + (rooms.length === 1 ? " Room • " : " Rooms • ") + fbneo + " FinalBurn Neo • "
               + join + " To Join"
    }

    Component.onCompleted: {
        if (arcadeBackend)
            arcadeBackend.refreshRooms()
        placeCursor()
        if (roomPage)
            open(rows.find(function(row) { return row.type !== "section" && row.id === roomId }))
    }
    onRowsChanged: {
        // The room open as the lobby has it now, or, gone from it or the
        // lobby not read, kept as it was with nothing to join; the cursor on
        // the line it was on, which the list's new lines took back to the
        // first.
        if (room) {
            var now = null
            for (var i = 0; i < rooms.length; ++i) {
                if (rooms[i].id === room.id)
                    now = rooms[i]
            }
            var on = roomLines.currentIndex
            var label = (lines[on] || {}).label
            room = now || Object.assign({}, room, {
                joinable: false,
                why: lobby.state === "failed" ? lobby.problem || "RetroArch's lobby can't be reached"
                                              : "It is no longer in RetroArch's lobby"
            })
            Qt.callLater(keepLine, label, on)
        }
        Qt.callLater(placeCursor)
    }

    // The cursor on the room it was on, else the first room.
    function placeCursor() {
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].type !== "section" && rows[i].id === roomId) {
                roomList.currentIndex = i
                roomList.show(i)
                return
            }
        }
        var at = Math.min(roomList.currentIndex, rows.length - 1)
        while (at >= 0 && at < rows.length && rows[at].type === "section")
            ++at
        roomList.currentIndex = at >= 0 && at < rows.length ? at : (rows.length > 1 ? 1 : 0)
    }

    // The cursor on the room's line called label, else on the line at, or
    // the last.
    function keepLine(label, at) {
        if (!room)
            return
        var i = 0
        while (i < lines.length && lines[i].label !== label)
            ++i
        roomLines.currentIndex = i < lines.length ? i : Math.max(0, Math.min(at, lines.length - 1))
        roomLines.show(roomLines.currentIndex)
    }

    function open(row) {
        if (!row || row.type === "section")
            return
        roomId = row.id
        room = row
        roomLines.currentIndex = 0
        roomLines.forceActiveFocus()
    }
    function close() {
        if (roomPage) {
            lobbyRoot.goBack()
            return
        }
        room = null
        roomList.forceActiveFocus()
        placeCursor()
    }
    function join() {
        if (!room || !room.joinable)
            return
        lobbyRoot.navigateTo("Play.qml", { game: room.set, title: room.title, online: { room: room.id, host: room.host } },
                             { room: room.id })
    }

    // What the help line says of a line of the rooms: a room's (Rooms.js), a
    // heading's core, or why the lobby couldn't be read.
    function about(row) {
        if (!row)
            return lobby.state === "failed" ? lobby.problem || "" : ""
        if (row.type === "section")
            return row.label
        return Rooms.about(row, lobby)
    }

    // The lobby read again while the page is open: rooms last a minute
    // there, and hosts say they are still there every 20 s.
    Timer {
        interval: 20000
        repeat: true
        running: !!arcadeBackend && !arcadeBackend.running
        onTriggered: arcadeBackend.refreshRooms()
    }
    Connections {
        target: arcadeBackend
        ignoreUnknownSignals: true
        function onRoomsChanged() { lobbyRoot.revision++ }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: lobbyRoot.room ? lobbyRoot.room.title : "RetroArch Lobby"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    Text {
        x: root.sw * 0.125 //80
        y: root.sh * 0.2041667 //98
        width: root.sw * 0.75 //480
        text: lobbyRoot.room ? lobbyRoot.room.core + " • Hosted by " + lobbyRoot.room.host : lobbyRoot.summary
        textFormat: Text.PlainText
        elide: Text.ElideRight
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0375 //18
    }

    // The rooms, under their cores' headings.
    MenuList {
        id: roomList
        visible: !lobbyRoot.room
        focus: true
        model: lobbyRoot.rows
        // Under the line above, down to where every view's list ends over a
        // help line, which this page always has.
        anchors.topMargin: root.sh * 0.2833333 //136
        height: root.sh * 0.4333333 + root.hintRoom //208
        // The room the cursor is on; not while a room is open, whose room the
        // list may have lost for a read.
        onCurrentIndexChanged: {
            var row = lobbyRoot.rows[currentIndex]
            if (!lobbyRoot.room && row && row.type !== "section")
                lobbyRoot.roomId = row.id
        }

        delegate: MenuRow {
            required property var modelData
            required property int index
            width: roomList.width
            height: root.sh * 0.0583333 //28
            readonly property string mark: heading ? "" : Rooms.mark(modelData)
            heading: modelData.type === "section"
            label: heading ? modelData.label : modelData.title
            value: heading ? "" : modelData.host
            icons: mark !== "" ? [mark] : []
            keepValue: true
            selected: roomList.currentIndex === index && !heading
        }

        Keys.onReturnPressed: function(event) {
            if (!event.isAutoRepeat)
                lobbyRoot.open(lobbyRoot.rows[currentIndex])
        }
        Keys.onEnterPressed: function(event) {
            if (!event.isAutoRepeat)
                lobbyRoot.open(lobbyRoot.rows[currentIndex])
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                if (!event.isAutoRepeat)
                    lobbyRoot.goBack()
                event.accepted = true
            }
        }
    }

    // The room open: what the lobby says of it, a line each.
    MenuList {
        id: roomLines
        visible: !!lobbyRoot.room
        model: lobbyRoot.lines
        anchors.topMargin: root.sh * 0.2833333 //136
        height: root.sh * 0.4333333 + root.hintRoom //208

        delegate: MenuRow {
            required property var modelData
            required property int index
            width: roomLines.width
            height: root.sh * 0.0583333 //28
            label: modelData.label
            value: modelData.value || ""
            icons: modelData.type === "join" ? ["play"] : modelData.icons || []
            keepValue: true
            selected: roomLines.currentIndex === index
        }

        Keys.onReturnPressed: function(event) {
            if (!event.isAutoRepeat && (lobbyRoot.lines[currentIndex] || {}).type === "join")
                lobbyRoot.join()
        }
        Keys.onEnterPressed: function(event) {
            if (!event.isAutoRepeat && (lobbyRoot.lines[currentIndex] || {}).type === "join")
                lobbyRoot.join()
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                if (!event.isAutoRepeat)
                    lobbyRoot.close()
                event.accepted = true
            }
        }
    }

    // What the lobby says of a room, and why it can't be joined, are on it
    // alone: there with Help Line off too, as on Settings' other pages.
    HelpLine {
        always: true
        text: lobbyRoot.room ? ((lobbyRoot.lines[roomLines.currentIndex] || {}).description || "")
                             : lobbyRoot.about(lobbyRoot.rows[roomList.currentIndex])
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        readonly property bool onRoom: !lobbyRoot.room && !!lobbyRoot.rows[roomList.currentIndex]
                                       && lobbyRoot.rows[roomList.currentIndex].type !== "section"
        readonly property bool onJoin: !!lobbyRoot.room && (lobbyRoot.lines[roomLines.currentIndex] || {}).type === "join"
        text: root.hints.back + ":BACK"
              + ((lobbyRoot.room ? lobbyRoot.lines.length : lobbyRoot.rooms.length) > 1 ? " " + root.hints.navigate + ":NAVIGATE" : "")
              + (onRoom ? " " + root.hints.select + ":OPEN" : onJoin ? " " + root.hints.select + ":JOIN" : "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
