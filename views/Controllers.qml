import QtQuick
import Components

// Settings → Controls: the gamepads connected first, the one used last on
// top with the cursor on it and a ● on its line, each with the player it is
// ("8BITDO PRO 2 ······ PLAYER 1 ►") and a page of its own
// (ControllerSettings.qml); then Keyboards & Remotes, and Not Connected while
// there are controllers away with settings of their own. With no gamepad
// there, two lines say so and what to do, where the cursor passes over them.
// While a player's pad was chosen by hand, a line puts every player back to
// Auto.
//
// Keyboards & Remotes and Not Connected are this view again (navParams.show
// "keyboards", "away"): All Keyboards & Remotes and each keyboard and remote
// connected, which open their keys (MenuButtons.qml); the controllers away.
// Where no keyboard is listed apart (a Mac, Windows, a Linux without the
// input group), Keyboards & Remotes opens All Keyboards & Remotes' keys at
// once.
FocusScope {
    id: controllersRoot
    // In the layout's frame (Main.qml): its rows in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})
    // "" (the first list), "keyboards" or "away".
    readonly property string show: navParams && navParams.show ? navParams.show : ""

    // Null guards: context properties resolve to null while the view Loader
    // tears down; guarded bindings stay teardown-safe (see Bluetooth.qml).
    readonly property var controllers: inputManager ? inputManager.controllers : []
    readonly property string active: inputManager ? inputManager.activeController : ""
    readonly property var players: inputManager ? inputManager.players : []
    readonly property string allKeys: inputManager ? inputManager.allKeys : ""

    // The lines: { type: "controller" | "keys" | "list" | "players" | "note" | "section", … }.
    property var rows: []
    // The pad on top: the one used last as the page opened. It stays there
    // while the page is up, so the lines don't jump as pads are pressed.
    property string topKey: ""
    // What the last choice did, in the help line until the cursor moves.
    property string notice: ""

    onControllersChanged: buildRows()
    onActiveChanged: buildRows()
    onPlayersChanged: buildRows()
    Component.onCompleted: {
        topKey = active
        buildRows()
        // Back from a line's page: on that line. Else on the first line.
        var index = -1
        for (var i = 0; i < rows.length && navListState.at; i++) {
            if (at(rows[i]) === navListState.at)
                index = i
        }
        if (index < 0 && navListState.currentIndex !== undefined)
            index = selectableFrom(Math.min(navListState.currentIndex, rows.length - 1), 1)
        list.currentIndex = index >= 0 ? index : selectableFrom(0, 1)
        list.positionViewAtIndex(list.currentIndex, ListView.Contain)
    }

    function isPad(c) { return c.kind === "gamepad" || c.kind === "arcade" }

    function kindTitle(kind) {
        if (kind === "arcade")   return "Arcade"
        if (kind === "keyboard") return "Keyboard"
        if (kind === "remote")   return "Remote"
        return "Gamepad"
    }

    // "PLAYER 1", "PLAYERS 3+4" (two of a model), or "NO PLAYER".
    function playerText(c) {
        var numbers = c.players || []
        if (numbers.length === 0)
            return "No player"
        return (numbers.length > 1 ? "Players " : "Player ") + numbers.join("+")
    }

    // What names a line, to find it again: a controller's key, the list it
    // opens, else its type.
    function at(row) {
        return row ? (row.key || (row.show ? "list:" + row.show : row.type)) : ""
    }

    function buildRows() {
        var current = rows[list.currentIndex]
        var before = list.currentIndex
        var present = controllers.filter(function(c) { return c.present })
        var away = controllers.filter(function(c) { return !c.present })
        var items = []
        if (show === "keyboards") {
            items.push({ type: "keys", key: allKeys, label: "All Keyboards & Remotes",
                         description: "Extra keys on every keyboard and remote" })
            present.filter(function(c) { return !isPad(c) }).forEach(function(c) {
                items.push({ type: "keys", key: c.key, label: c.name, value: kindTitle(c.kind), last: c.key === active,
                             description: "Extra keys on this one only" })
            })
        } else if (show === "away") {
            away.forEach(function(c) {
                items.push({ type: isPad(c) ? "controller" : "keys", key: c.key, label: c.name, value: kindTitle(c.kind),
                             description: "Its settings, kept for when it is back" })
            })
            if (items.length === 0)
                items.push({ type: "note", label: "None" })
        } else {
            var pads = present.filter(isPad)
            // The one used last on top, the rest as InputManager has them.
            pads.sort(function(a, b) { return (b.key === topKey ? 1 : 0) - (a.key === topKey ? 1 : 0) })
            pads.forEach(function(c) {
                items.push({ type: "controller", key: c.key, label: c.name + (c.count > 1 ? " x" + c.count : ""),
                             value: playerText(c), last: c.key === active,
                             description: "Its buttons, its player and its test" })
            })
            if (pads.length === 0) {
                items.push({ type: "note", label: "No gamepad connected" })
                items.push({ type: "note", label: "Plug one in or pair it in Network" })
            }
            if (players.some(function(p) { return p.key !== "" }))
                items.push({ type: "players", label: "All Players Automatic",
                             description: "Each pad back to the number it gets" })
            items.push({ type: "list", show: "keyboards", label: "Keyboards & Remotes",
                         description: "Extra keys for the menus' actions" })
            if (away.length > 0)
                items.push({ type: "list", show: "away", label: "Not Connected", value: String(away.length),
                             description: "Controllers away that have settings" })
        }
        rows = items

        // The cursor stays on its line as controllers come and go; its line
        // gone (a controller unplugged), on the one now in its place.
        var index = -1
        for (var k = 0; k < items.length && current && index < 0; k++) {
            if (at(items[k]) === at(current) && selectable(items[k]))
                index = k
        }
        if (index < 0)
            index = selectableFrom(Math.min(Math.max(before, 0), items.length - 1), 1)
        list.currentIndex = index
    }

    function selectable(row) {
        return !!row && row.type !== "note" && row.type !== "section"
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

    // A line that leads to a page: every one but the players'.
    function opens(row) {
        return selectable(row) && row.type !== "players"
    }

    function listState(row) {
        return { currentIndex: list.currentIndex, at: at(row) }
    }

    function activate(row) {
        if (!selectable(row) || !inputManager)
            return
        if (row.type === "controller") {
            navigateTo("views/ControllerSettings.qml", { key: row.key }, listState(row))
        } else if (row.type === "keys") {
            navigateTo("views/MenuButtons.qml", { key: row.key }, listState(row))
        } else if (row.type === "players") {
            inputManager.resetPlayers()
            notice = "Every player is automatic now"
        } else if (row.show === "keyboards"
                   && !controllers.some(function(c) { return c.present && !isPad(c) })) {
            // No keyboard listed apart: straight to every keyboard's keys.
            navigateTo("views/MenuButtons.qml", { key: allKeys }, listState(row))
        } else {
            navigateTo("views/Controllers.qml", { show: row.show }, listState(row))
        }
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/keyboard.svg"
        title: "Controls"
        subtitle: controllersRoot.show === "keyboards" ? "Keyboards & Remotes"
                : controllersRoot.show === "away" ? "Not Connected" : ""
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
        anchors.leftMargin: root.paneX
        width: root.paneWidth
        // One line short of the space, so the ▼ fits above the help line; a
        // line more for each bar that is off (Main.qml's helpRoom, hintRoom).
        height: root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224
        clip: true
        focus: true
        // Lines come and go as controllers do: no sliding.
        highlightMoveDuration: 0

        // The cursor's line in view, and with it the lines the cursor passes
        // over before or after it (a note at an end of the list).
        function showCurrent() {
            positionViewAtIndex(currentIndex, ListView.Contain)
            if (controllersRoot.selectableFrom(0, 1) === currentIndex)
                positionViewAtBeginning()
            else if (controllersRoot.selectableFrom(controllersRoot.rows.length - 1, -1) === currentIndex)
                positionViewAtEnd()
        }

        function move(step) {
            if (controllersRoot.rows.length === 0)
                return
            currentIndex = controllersRoot.selectableFrom(currentIndex + step, step)
            showCurrent()
            controllersRoot.notice = ""
        }

        Keys.onUpPressed: move(-1)
        Keys.onDownPressed: move(1)
        Keys.onRightPressed: {
            var row = controllersRoot.rows[currentIndex]
            if (controllersRoot.opens(row))
                controllersRoot.activate(row)
        }
        Keys.onReturnPressed: controllersRoot.activate(controllersRoot.rows[currentIndex])
        Keys.onEnterPressed: controllersRoot.activate(controllersRoot.rows[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                controllersRoot.goBack()
                event.accepted = true
            }
        }

        // Lines read like a camcorder's menu: "8BITDO PRO 2······PLAYER 1 ►",
        // the one used last with a ● before the ►.
        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            label: modelData.label || ""
            value: modelData.value || ""
            icons: modelData.last ? ["rec"] : []
            opens: controllersRoot.opens(modelData)
            // A long name is cut short, not what it is.
            keepValue: true
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
        text: controllersRoot.notice !== "" ? controllersRoot.notice : ((currentRow && currentRow.description) || "")
        // What a choice did is said with Help Line off too.
        always: controllersRoot.notice !== ""
        visible: text !== ""
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        property var currentRow: controllersRoot.rows[list.currentIndex]
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + root.hints.select + (currentRow && currentRow.type === "players" ? ":SET" : ":OPEN")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
