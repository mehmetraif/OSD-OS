import QtQuick
import Components

// Settings → Controls: every controller there is, each with a page of its own
// (ControllerSettings.qml): a gamepad, the JAMMA board's sticks, and on Linux
// each keyboard and remote apart. First the extra keys every keyboard and
// remote shares (RemapControls.qml), then which pad each player has (PLAYERS,
// up to six, chosen from the pads connected, or Auto); last, under NOT
// CONNECTED, those away with settings of their own. The line of the
// controller used last is marked ◄, so a button pressed on one shows which
// line it is, and a pad that plays says so (P1, P2…).
FocusScope {
    id: controllersRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    // Null guards: context properties resolve to null while the view Loader
    // tears down; guarded bindings stay teardown-safe (see Bluetooth.qml).
    readonly property var controllers: inputManager ? inputManager.controllers : []
    readonly property string active: inputManager ? inputManager.activeController : ""
    readonly property var players: inputManager ? inputManager.players : []

    // The lines: { type: "shared" | "section" | "player" | "note" | "controller", … }.
    property var rows: []
    // The player whose pad is being chosen.
    property int choosingPlayer: 0

    onControllersChanged: buildRows()
    onActiveChanged: buildRows()
    onPlayersChanged: buildRows()
    Component.onCompleted: {
        buildRows()
        // Back from a controller's page: on its line.
        var index = -1
        for (var i = 0; i < rows.length && navListState.key; i++) {
            if (rows[i].key === navListState.key)
                index = i
        }
        if (index < 0 && navListState.currentIndex !== undefined)
            index = selectableFrom(Math.min(navListState.currentIndex, rows.length - 1), 1)
        if (index >= 0)
            list.currentIndex = index
        list.positionViewAtIndex(list.currentIndex, ListView.Contain)
    }

    function kindTitle(kind) {
        if (kind === "arcade")   return "Arcade"
        if (kind === "keyboard") return "Keyboard"
        if (kind === "remote")   return "Remote"
        return "Gamepad"
    }

    // What a player's line says: the pad chosen, or Auto and the pad it
    // took; a pad chosen and away, so.
    function playerValue(p) {
        if (p.key === "")
            return p.connected ? "Auto (" + p.name + ")" : "Auto"
        return p.connected ? p.name : (p.name || "A pad") + " (Away)"
    }

    // The pads a player can have, Auto first: { label, action }, the action
    // a pad's key ("auto" for Auto).
    function playerChoices() {
        var list = [{ label: "Auto", action: "auto" }]
        for (var i = 0; i < controllers.length; i++) {
            var c = controllers[i]
            if (c.present && (c.kind === "gamepad" || c.kind === "arcade"))
                list.push({ label: c.name + (c.count > 1 ? " x" + c.count : ""), action: c.key })
        }
        return list
    }

    function playerOf(number) {
        for (var i = 0; i < players.length; i++) {
            if (players[i].player === number)
                return players[i]
        }
        return null
    }

    function choosePlayer(row) {
        var p = playerOf(row.player)
        if (!p)
            return
        choosingPlayer = row.player
        chooser.choices = playerChoices()
        chooser.promptText = "Player " + row.player
        chooser.subtitleText = "Which pad is player " + row.player
        chooser.open()
        for (var i = 0; i < chooser.choices.length; i++) {
            if (chooser.choices[i].action === (p.key === "" ? "auto" : p.key))
                chooser.choiceIndex = i
        }
    }

    // ◄ ► on a player's line: the next pad, round, Auto among them.
    function cyclePlayer(row, step) {
        var p = playerOf(row.player)
        if (!p || !inputManager)
            return
        var choices = playerChoices()
        var at = 0
        for (var i = 0; i < choices.length; i++) {
            if (choices[i].action === (p.key === "" ? "auto" : p.key))
                at = i
        }
        var next = choices[(at + step + choices.length) % choices.length].action
        inputManager.setPlayer(row.player, next === "auto" ? "" : next)
    }

    function buildRows() {
        // The cursor stays on its line as controllers come and go: a
        // controller by its key, a player by its number, the others by type.
        var at = list.currentIndex
        var current = rows[at]
        var items = [{ type: "shared", label: "All Keyboards & Remotes",
                       description: "An extra key for each action, on every keyboard and remote at once" }]
        var present = controllers.filter(function(c) { return c.present })
        var away = controllers.filter(function(c) { return !c.present })
        items.push({ type: "section", label: "Players" })
        // Two players, and one more for each pad connected beyond two, up to
        // six; a player with a pad chosen keeps its line while it is away.
        var pads = 0
        for (var m = 0; m < present.length; m++) {
            if (present[m].kind === "gamepad" || present[m].kind === "arcade")
                pads += present[m].count
        }
        var shown = Math.max(2, pads)
        for (var q = 0; q < players.length; q++) {
            if (players[q].key !== "")
                shown = Math.max(shown, players[q].player)
        }
        for (var n = 0; n < players.length && players[n].player <= shown; n++) {
            var p = players[n]
            items.push({ type: "player", player: p.player, label: "Player " + p.player, value: playerValue(p),
                         description: "The pad player " + p.player + " has in games. Select, or ◄ ►, to choose it. Auto takes "
                                      + (p.player <= 2 ? "the JAMMA board's P" + p.player + " stick, else " : "")
                                      + "the pads in the order they came" })
        }
        items.push({ type: "section", label: "Connected" })
        if (present.length === 0)
            items.push({ type: "note", label: "None",
                         description: "Connect a gamepad, a keyboard or a remote: it shows here at once" })
        for (var i = 0; i < present.length; i++) {
            var c = present[i]
            var playing = players.filter(function(p) { return p.connected && p.playing === c.key })
                                 .map(function(p) { return "P" + p.player })
            items.push({ type: "controller", key: c.key,
                         label: c.name + (c.count > 1 ? " x" + c.count : ""),
                         value: (playing.length > 0 ? playing.join("+") + " " : "")
                                + kindTitle(c.kind) + (c.key === active ? " ◄" : ""),
                         description: "Its own buttons" + (c.kind === "gamepad" || c.kind === "arcade" ? " and labels" : "")
                                      + ". ◄ marks the one used last: press a button on a controller to find it" })
        }
        if (away.length > 0) {
            items.push({ type: "section", label: "Not Connected" })
            for (var j = 0; j < away.length; j++)
                items.push({ type: "controller", key: away[j].key, label: away[j].name,
                             value: kindTitle(away[j].kind),
                             description: "Its settings, kept for when it is back" })
        }
        rows = items

        var index = -1
        if (current) {
            for (var k = 0; k < items.length && index < 0; k++) {
                if (current.type === "controller" ? items[k].key === current.key
                    : current.type === "player" ? items[k].type === "player" && items[k].player === current.player
                    : items[k].type === current.type && selectable(current))
                    index = k
            }
        }
        // Its line gone (a controller unplugged): the one now in its place.
        if (index < 0)
            index = selectableFrom(Math.min(Math.max(at, 0), items.length - 1), 1)
        list.currentIndex = index
    }

    function selectable(row) {
        return !!row && (row.type === "shared" || row.type === "player" || row.type === "controller")
    }

    // The first line that can be chosen from index on, going by step, round.
    function selectableFrom(index, step) {
        for (var n = 0; n < rows.length; n++) {
            var i = ((index + n * step) % rows.length + rows.length) % rows.length
            if (selectable(rows[i]))
                return i
        }
        return 0
    }

    function activate(row) {
        if (!row)
            return
        if (row.type === "shared")
            navigateTo("views/RemapControls.qml", {}, { currentIndex: list.currentIndex })
        else if (row.type === "player")
            choosePlayer(row)
        else if (row.type === "controller")
            navigateTo("views/ControllerSettings.qml", { key: row.key },
                       { currentIndex: list.currentIndex, key: row.key })
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/keyboard.svg"
        title: "Controls"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    ListView {
        id: list
        model: controllersRoot.rows
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        // One line short of the space, so the ▼ fits above the help line; a
        // line more for each bar that is off (Main.qml's helpRoom, hintRoom).
        height: root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224
        clip: true
        focus: true
        // Lines come and go as controllers do: no sliding.
        highlightMoveDuration: 0

        function move(step) {
            if (controllersRoot.rows.length === 0)
                return
            currentIndex = controllersRoot.selectableFrom(currentIndex + step, step)
            positionViewAtIndex(currentIndex, ListView.Contain)
        }

        Keys.onUpPressed: move(-1)
        Keys.onDownPressed: move(1)
        Keys.onLeftPressed: {
            var row = controllersRoot.rows[currentIndex]
            if (row && row.type === "player")
                controllersRoot.cyclePlayer(row, -1)
        }
        Keys.onRightPressed: {
            var row = controllersRoot.rows[currentIndex]
            if (row && row.type === "player")
                controllersRoot.cyclePlayer(row, 1)
        }
        Keys.onReturnPressed: controllersRoot.activate(controllersRoot.rows[currentIndex])
        Keys.onEnterPressed: controllersRoot.activate(controllersRoot.rows[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                controllersRoot.goBack()
                event.accepted = true
            }
        }

        // Lines read like a camcorder's menu: "8BITDO PRO 2······GAMEPAD", and
        // the groups' headings like Settings': "CONNECTED ─────".
        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            heading: modelData.type === "section"
            label: modelData.label || ""
            value: modelData.value || ""
            // A long name is cut short, not what it is; a player's pad, the
            // other way round.
            keepValue: modelData.type === "controller"
            selected: list.currentIndex === index && controllersRoot.selectable(modelData)
        }
    }

    // ▲ / ▼ while lines are hidden above or below.
    ScrollMarks {
        anchors.fill: list
        list: list
    }

    HelpLine {
        property var currentRow: controllersRoot.rows[list.currentIndex]
        text: (currentRow && currentRow.description) || ""
        visible: text !== ""
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        property var currentRow: controllersRoot.rows[list.currentIndex]
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + (currentRow && currentRow.type === "player" ? root.hints.change + ":CHANGE " : "")
              + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // A player's pad, from those connected.
    ChoiceOverlay {
        id: chooser
        objectName: "playerChooser"
        anchors.fill: parent
        onActivated: function(action) {
            if (inputManager && controllersRoot.choosingPlayer > 0)
                inputManager.setPlayer(controllersRoot.choosingPlayer, action === "auto" ? "" : action)
        }
        onClosed: list.forceActiveFocus()
    }
}
