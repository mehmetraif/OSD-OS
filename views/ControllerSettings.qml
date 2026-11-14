import QtQuick
import Components

// A gamepad's page, from Settings → Controls (Controllers.qml): six lines,
// each one job.
//
//     TEST BUTTONS ►                     what each button is and does (ControllerTest.qml)
//     MENU BUTTONS ····· A SELECT, B BACK ►   which buttons work the menus (MenuButtons.qml)
//     GAME BUTTONS ·········· FIGHTING 6 ►    which is each of a cabinet's (ArcadeButtons.qml)
//     PLAYER ·················· AUTO (1) ►    the player it is, from a list that says who has each number
//     BUTTON NAMES ········ ◄ AUTO (A BELOW) ►   what its buttons are called, here and in the hints
//     RESET ►                            all of it back to the defaults, asked first
//
// One of the JAMMA board's sticks has a seventh, PIN NAMES: the layout its
// test names the board's pins by (JammaPinGrid.layouts), kept in config.json
// as app.jamma_pin_layout. A pad away has NOT CONNECTED for its title, and
// what needs it connected says so when chosen.
FocusScope {
    id: pageRoot
    // In the layout's frame (Main.qml): its rows in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})

    readonly property string key: navParams && navParams.key ? navParams.key : ""
    // inputManager.controllerPage(): taken again whenever a controller comes,
    // goes or changes, or a player's pad does.
    property var page: ({})
    readonly property bool present: !!page.present
    readonly property int jamma: page.jamma || 0
    // Its name as last seen: kept once a reset leaves nothing of one away.
    property string name: ""

    readonly property var styles: ["auto", "a_below", "a_right", "shapes", "numbers"]
    readonly property var styleTitles: ({ auto: "Auto", a_below: "A below", a_right: "A right",
                                          shapes: "Shapes", numbers: "Numbers" })
    // The layouts a JAMMA stick's test names the board's pins by.
    readonly property var pinLayouts: ["OSD/OS", "Crown, CHAMMA", "JammaPi"]
    property int pinLayout: 0

    property var rows: []
    // What the last choice did, in the help line until the cursor moves.
    property string notice: ""

    Component.onCompleted: {
        var kept = appCore ? pinLayouts.indexOf(appCore.get_setting("", "jamma_pin_layout")) : -1
        pinLayout = Math.max(0, kept)
        refresh()
        // Back from one of its pages: on that line.
        var state = navParams.navListState || {}
        list.currentIndex = selectable(rows[state.currentIndex]) ? state.currentIndex : 0
        list.forceActiveFocus()
    }

    Connections {
        target: inputManager
        function onControllersChanged() { pageRoot.refresh() }
        function onPlayersChanged() { pageRoot.refresh() }
    }

    function labelsValue() {
        if (page.labels === "auto" || !page.labels)
            return page.labelsInUse ? "Auto (" + page.labelsInUse + ")" : "Auto"
        return styleTitles[page.labels] || page.labels
    }

    // "AUTO (1)", "AUTO" for one that plays as no one, "2" chosen by hand.
    function playerValue() {
        var numbers = (page.players || []).join("+")
        if (page.player > 0)
            return String(page.player)
        return numbers !== "" ? "Auto (" + numbers + ")" : "Auto"
    }

    function first(action) {
        var r = page.rows || []
        for (var i = 0; i < r.length; i++) {
            if (r[i].action === action)
                return r[i].first
        }
        return "None"
    }

    function refresh() {
        if (!inputManager || key === "")
            return
        page = inputManager.controllerPage(key)
        if (page.name)
            name = page.name
        var items = [
            { type: "test", label: "Test Buttons",
              description: present ? "What each button is and does" : "Connect it to test its buttons" },
            { type: "menu", label: "Menu Buttons", value: first("select") + " Select, " + first("back") + " Back",
              description: "Which of its buttons work the menus" },
            { type: "game", label: "Game Buttons", value: page.arcade || "",
              description: "Which is each of a cabinet's buttons" },
            { type: "player", label: "Player", value: playerValue(),
              description: "The player it is in the games" },
            { type: "labels", label: "Button Names", value: labelsValue(),
              description: "What its buttons are called on screen" }
        ]
        if (jamma)
            items.push({ type: "pins", label: "Pin Names", value: pinLayouts[pinLayout],
                         description: "How its test names the board's pins" })
        items.push({ type: "reset", label: "Reset",
                     description: "Its buttons and names as on every pad" })
        // The cursor stays on its line: a new model would put it back on
        // the first.
        var at = list.currentIndex
        rows = items
        list.currentIndex = at >= 0 && at < items.length ? at : 0
    }

    function selectable(row) { return !!row }

    function cycleLabels(step) {
        var i = styles.indexOf(page.labels || "auto")
        inputManager.setControllerLabels(key, styles[((i < 0 ? 0 : i) + step + styles.length) % styles.length])
    }

    function cyclePins(step) {
        pinLayout = (pinLayout + step + pinLayouts.length) % pinLayouts.length
        if (appCore)
            appCore.save_setting("", "jamma_pin_layout", pinLayouts[pinLayout])
        refresh()
    }

    // PLAYER: Auto and the six numbers, each with the pad that has it now.
    function choosePlayer() {
        var players = inputManager.players || []
        var choices = [{ label: "Auto", action: "0" }]
        for (var i = 0; i < players.length; i++) {
            var p = players[i]
            var holder = !p.connected ? "" : p.playing === key ? "this pad" : p.name
            choices.push({ label: "Player " + p.player + (holder !== "" ? ": " + holder : ""), action: String(p.player) })
        }
        playerChooser.choices = choices
        playerChooser.subtitleText = "Which player " + name + " is. A number another pad has swaps the two"
        playerChooser.open()
        playerChooser.choiceIndex = page.player > 0 ? page.player : 0
    }

    function setPlayer(number) {
        var result = inputManager.setPadPlayer(key, number)
        if (result.outcome === "away")
            notice = "Connect it first"
        else if (result.outcome === "already")
            notice = number === 0 ? "Its player is automatic already" : "It is player " + number + " already"
        else if (number === 0)
            notice = "Its player is automatic now"
        else
            notice = "It is player " + number + " now"
                     + (result.other ? ", " + result.other + (result.otherPlayer > 0 ? " player " + result.otherPlayer
                                                                                    : " no player") : "")
    }

    function activate(row) {
        if (!row || !inputManager)
            return
        var state = { currentIndex: list.currentIndex }
        if (row.type === "test") {
            if (!present)
                notice = "Connect it first"
            else
                navigateTo("views/ControllerTest.qml", { key: key, pinLayout: pinLayout }, state)
        } else if (row.type === "menu") {
            navigateTo("views/MenuButtons.qml", { key: key }, state)
        } else if (row.type === "game") {
            navigateTo("views/ArcadeButtons.qml", { key: key }, state)
        } else if (row.type === "player") {
            choosePlayer()
        } else if (row.type === "labels") {
            cycleLabels(1)
        } else if (row.type === "pins") {
            cyclePins(1)
        } else if (row.type === "reset") {
            resetConfirm.promptText = "Reset " + name + "?"
            resetConfirm.open()
        }
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/keyboard.svg"
        // A pad away says so where a long name can't push it out of sight.
        title: pageRoot.present ? "Controls" : "Not Connected"
        subtitle: pageRoot.name
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    ListView {
        id: list
        model: pageRoot.rows
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.paneX
        width: root.paneWidth
        height: root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224
        clip: true
        focus: true
        highlightMoveDuration: 0

        function move(step) {
            if (count === 0)
                return
            currentIndex = (currentIndex + step + count) % count
            positionViewAtIndex(currentIndex, ListView.Contain)
            pageRoot.notice = ""
        }
        function change(step) {
            var row = pageRoot.rows[currentIndex]
            if (row && row.type === "labels")
                pageRoot.cycleLabels(step)
            else if (row && row.type === "pins")
                pageRoot.cyclePins(step)
            else if (row && step > 0 && row.type !== "reset")
                pageRoot.activate(row)
        }

        Keys.onUpPressed: move(-1)
        Keys.onDownPressed: move(1)
        Keys.onLeftPressed: change(-1)
        Keys.onRightPressed: change(1)
        Keys.onReturnPressed: pageRoot.activate(pageRoot.rows[currentIndex])
        Keys.onEnterPressed: pageRoot.activate(pageRoot.rows[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                pageRoot.goBack()
                event.accepted = true
            }
        }

        // Every line that opens something ends in ►; the names, which ◄ ► go
        // round, are between ◄ and ►.
        delegate: MenuRow {
            readonly property bool cycles: modelData.type === "labels" || modelData.type === "pins"
            width: list.width
            height: root.sh * 0.0583333 //28
            label: modelData.label || ""
            value: modelData.value || ""
            opens: !cycles
            changes: cycles
            keepValue: modelData.type === "menu"
            selected: list.currentIndex === index
        }
    }

    HelpLine {
        property var currentRow: pageRoot.rows[list.currentIndex]
        visible: text !== ""
        text: pageRoot.notice !== "" ? pageRoot.notice : ((currentRow && currentRow.description) || "")
        // What a choice did is said with Help Line off too.
        always: pageRoot.notice !== ""
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        property var currentRow: pageRoot.rows[list.currentIndex]
        readonly property string type: currentRow ? currentRow.type : ""
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + (type === "labels" || type === "pins" ? root.hints.change + ":CHANGE "
                 : root.hints.select + (type === "test" ? ":TEST" : type === "reset" ? ":RESET"
                                        : type === "player" ? ":CHANGE" : ":OPEN"))
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // The player it is, from a list: back leaves it as it was.
    ChoiceOverlay {
        id: playerChooser
        objectName: "playerChooser"
        anchors.fill: parent
        promptText: "Player"
        onActivated: function(action) { pageRoot.setPlayer(Number(action)) }
        onClosed: list.forceActiveFocus()
    }

    // Asked before it resets, the cursor on Cancel, with what goes.
    ChoiceOverlay {
        id: resetConfirm
        objectName: "resetConfirm"
        anchors.fill: parent
        subtitleText: "Its menu buttons, its Game Buttons and its button names go back to the defaults"
        choices: [{ label: "Cancel", action: "cancel" }, { label: "Reset", action: "reset" }]
        onActivated: function(action) {
            if (action !== "reset" || !inputManager)
                return
            inputManager.resetController(pageRoot.key)
            // Away, it had nothing else: its line goes from the list too.
            if (!pageRoot.present)
                pageRoot.goBack()
            else
                pageRoot.notice = "Back to the defaults"
        }
        onClosed: list.forceActiveFocus()
    }
}
