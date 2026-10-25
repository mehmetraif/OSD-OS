import QtQuick
import Components

// A pad's Arcade Buttons, from its page in Settings → Controls
// (ControllerSettings.qml): which of its inputs is each of a cabinet's,
// Button 1 to 8, Coin and Start, the same in every game, MAME's and FinalBurn
// Neo's alike.
//
// LAYOUT starts it from a preset (Fighting 6: a fighter's punches over its
// kicks, as a six-button cabinet's two rows; Neo Geo 4: A B C D on the four
// face buttons), leaves it to the emulators (Default: what OSD/OS always did,
// a JAMMA stick's pins' buttons in their order), or is Custom, set here:
// SET ALL BUTTONS takes them from presses in turn, 1 to 6, Coin, Start, 7 and
// 8 (2 to 8 none if their wait runs out: a pad may have fewer); a line of its
// own takes one press, or ◄ ► goes round the inputs the pad has left. The
// D-pad and the left stick are the games' joystick, never a button. TEST
// BUTTONS shows what each press is. Beside the lines, and in the waits for a
// press, a two-player cabinet's control panel (ArcadePanel) lights the one a
// line, a step or a press is, on the side of the player the pad plays as.
// Kept in config.json, app.controllers.<key>.arcade, through inputManager,
// which gives each player's to MAME and RetroArch as a game starts.
FocusScope {
    id: pageRoot

    signal goBack()

    property var navParams: ({})

    readonly property string key: navParams && navParams.key ? navParams.key : ""
    // inputManager.arcadePage(): taken again whenever a controller comes,
    // goes or changes.
    property var page: ({})
    readonly property bool present: !!page.present
    readonly property int jamma: page.jamma || 0
    // Under Default MAME and FinalBurn Neo number another pad's buttons their
    // own ways.
    readonly property bool theirs: page.layout === "default" && !jamma
    // Its name as last seen.
    property string name: ""

    property var rows: []
    // Waiting for a press for one of them ("1" to "8", "coin", "start").
    property string captureInput: ""
    readonly property bool capturing: captureInput !== ""
    property int secondsLeft: 0
    // What the last press did, in the help line until the cursor moves.
    property string notice: ""

    // SET ALL BUTTONS: the step it is at in allOrder, and the inputs pressed
    // so far, with their names, in kArcadeInputs' order ("" for none yet).
    property bool settingAll: false
    property int step: 0
    property var setTokens: []
    property var setLabels: []
    // Why the last press was not taken.
    property string stepNotice: ""
    // 1 to 6, Coin, Start, then 7 and 8. A pad may have fewer than eight
    // buttons: the wait for Button 2 to 8 is shorter, and running out leaves
    // it none.
    readonly property var allOrder: [0, 1, 2, 3, 4, 5, 8, 9, 6, 7]
    readonly property bool optionalStep: settingAll && optionalAt(step)

    // TEST BUTTONS: the presses seen, the last first, and which of the
    // cabinet's the last one is ("1" to "8", "coin", "start"; "" for none).
    property bool testing: false
    property var presses: []
    property string pressed: ""

    // What the control panel lights: the line's input, the one a press is
    // waited for, or the one the last test press is.
    readonly property var lit: testing ? (pressed !== "" ? [pressed] : [])
                             : capturing ? [captureInput]
                             : rows[list.currentIndex] && rows[list.currentIndex].type === "input"
                               ? [rows[list.currentIndex].input] : []
    // On whose side of it (playerSide()): 2 the right, 1 the left.
    property int player: 1

    // InputManager as the page opened, for its end: the context property may
    // read null while the view Loader tears down.
    property var manager: null

    Component.onCompleted: {
        manager = inputManager
        refresh()
        list.currentIndex = selectableFrom(0, 1)
        list.forceActiveFocus()
    }
    // Leaving the page while it waits for a press: the controller acts again.
    Component.onDestruction: {
        if (manager)
            manager.captureController("")
    }

    Connections {
        target: inputManager
        function onControllersChanged() { pageRoot.refresh() }
        function onPlayersChanged() { pageRoot.player = pageRoot.playerSide() }
        function onControllerInputCaptured(key, input, label) {
            if (key !== pageRoot.key)
                return
            if (pageRoot.testing)
                pageRoot.testPress(input, label)
            else if (pageRoot.settingAll)
                pageRoot.setAllPress(input, label)
            else if (pageRoot.capturing)
                pageRoot.setPress(input, label)
        }
    }

    function refresh() {
        if (!inputManager || key === "")
            return
        page = inputManager.arcadePage(key)
        if (page.name)
            name = page.name
        player = playerSide()
        if ((capturing || settingAll) && !present)
            cancelCapture()
        if (testing && !present)
            endTest()

        var away = "Connect it to set its buttons"
        var items = [
            { type: "layout", label: "Layout", value: page.layoutTitle || "Default", description: layoutDescription() },
            { type: "setall", label: "Set All Buttons",
              description: !present ? away : "Press its buttons in turn: 1 to 6, Coin, Start, 7 and 8, or wait past one it hasn't" },
            { type: "test", label: "Test Buttons",
              description: !present ? "Connect it to test its buttons" : "Press its buttons to see which of the cabinet's each one is" },
            { type: "section", label: "Buttons" }
        ]
        var r = page.rows || []
        for (var i = 0; i < r.length; i++)
            items.push({ type: "input", input: r[i].input, label: r[i].label, token: r[i].token, value: r[i].value,
                         description: !present ? "Connect it to press its button for " + r[i].label + ", or ◄ ► to choose one"
                                    : "Select, then press the button on it for " + r[i].label + ", or ◄ ► to choose one" })

        var at = list.currentIndex
        rows = items
        list.currentIndex = at >= 0 && at < items.length && selectable(items[at]) ? at : selectableFrom(0, 1)
    }

    // 2 for a pad that plays as player 2, or is chosen for player 2 while
    // away; 1 for one that plays as another. One that plays as no one goes by
    // the JAMMA board's stick it is: its second, 2.
    function playerSide() {
        var players = inputManager ? inputManager.players || [] : []
        for (var i = 0; i < players.length; i++) {
            if (players[i].playing === key || (players[i].key === key && !players[i].playing))
                return players[i].player === 2 ? 2 : 1
        }
        return jamma === 2 ? 2 : 1
    }

    // The layout's own words, by what its buttons are on this pad.
    function layoutDescription() {
        var r = page.rows || []
        var on = function(from, to) {
            var names = []
            for (var i = from; i <= to && i < r.length; i++)
                names.push(r[i].value)
            return names.join(" ")
        }
        if (page.layout === "fighting6")
            return "A fighter's six: punches 1 2 3 on " + on(0, 2) + ", kicks 4 5 6 on " + on(3, 5)
        if (page.layout === "neogeo4")
            return "A Neo Geo's A B C D, buttons 1 to 4, on " + on(0, 3)
        if (page.layout === "custom")
            return "Set button by button below, or with Set All Buttons"
        return jamma ? "Its pins' buttons in their order, 1 to 8, COIN and START"
                     : "MAME and FinalBurn Neo lay its buttons out their own ways"
    }

    function selectable(row) { return !!row && row.type !== "section" }

    // The first line that can be chosen from index on, going by step, round.
    function selectableFrom(index, step) {
        for (var n = 0; n < rows.length; n++) {
            var i = ((index + n * step) % rows.length + rows.length) % rows.length
            if (selectable(rows[i]))
                return i
        }
        return 0
    }

    function cycleLayout(step) {
        var layouts = page.layouts || []
        var at = 0
        for (var i = 0; i < layouts.length; i++) {
            if (layouts[i].layout === page.layout)
                at = i
        }
        if (layouts.length > 0)
            inputManager.setArcadeLayout(key, layouts[(at + step + layouts.length) % layouts.length].layout)
    }

    // ◄ ►: the next of the inputs the pad has that no other line has, or
    // none, which Coin and Start never are.
    function cycleInput(row, step) {
        var used = {}
        for (var i = 0; i < rows.length; i++) {
            if (rows[i].type === "input" && rows[i].input !== row.input && rows[i].token)
                used[rows[i].token] = true
        }
        var tokens = []
        var choices = page.choices || []
        for (var c = 0; c < choices.length; c++) {
            if (!used[choices[c].token])
                tokens.push(choices[c].token)
        }
        if (row.input !== "coin" && row.input !== "start")
            tokens.push("")
        var at = tokens.indexOf(row.token)
        var next = tokens[at < 0 ? (step > 0 ? 0 : tokens.length - 1) : (at + step + tokens.length) % tokens.length]
        showOutcome(row.label, inputManager.setArcadeInput(key, row.input, next))
    }

    function inputLabel(input) {
        var r = page.rows || []
        for (var i = 0; i < r.length; i++) {
            if (r[i].input === input)
                return r[i].label
        }
        return input
    }

    function isChoice(token) {
        var choices = page.choices || []
        for (var i = 0; i < choices.length; i++) {
            if (choices[i].token === token)
                return true
        }
        return false
    }

    // What came of a press, or a choice, for one of them
    // (InputManager::setArcadeInput()): "RB is Button 1 now, and A is
    // Button 3" for one another had.
    function showOutcome(label, result) {
        if (result.outcome === "set")
            notice = isText(result.label, label) + " now"
        else if (result.outcome === "swapped")
            notice = isText(result.label, label) + " now, and " + isText(valueOf(result.other), result.other)
        else if (result.outcome === "already")
            notice = isText(result.label, label) + " already"
        else if (result.outcome === "kept")
            notice = result.label + " stays " + result.other + ": Coin and Start always have a button"
        else if (result.outcome === "joystick")
            notice = result.label + " is the joystick, not a button"
        else if (result.outcome === "menu")
            notice = result.label + " shows MAME's menu, not a button"
        else if (result.outcome === "unsaved")
            notice = "Not saved: the settings could not be written"
    }

    // "RB is Button 1", "Button 7 is None".
    function isText(value, label) {
        return value === "None" ? label + " is None" : value + " is " + label
    }

    function valueOf(label) {
        for (var i = 0; i < rows.length; i++) {
            if (rows[i].type === "input" && rows[i].label === label)
                return rows[i].value
        }
        return "None"
    }

    // A wait of so many seconds from now: a wait under way counts them from
    // a whole second.
    function waitFor(seconds) {
        secondsLeft = seconds
        if (countdown.running)
            countdown.restart()
    }

    function startCapture(input) {
        if (!present) {
            notice = "Connect it first"
            return
        }
        notice = ""
        stepNotice = ""
        waitFor(10)
        captureInput = input
        inputManager.captureController(key)
        overlay.forceActiveFocus()
    }

    function cancelCapture() {
        var hadKeys = overlay.activeFocus
        captureInput = ""
        settingAll = false
        if (inputManager)
            inputManager.captureController("")
        if (hadKeys)
            list.forceActiveFocus()
    }

    function setPress(token, label) {
        var input = captureInput
        cancelCapture()
        showOutcome(inputLabel(input), inputManager.setArcadeInput(key, input, token))
    }

    function startSetAll() {
        if (!present) {
            notice = "Connect it first"
            return
        }
        var none = []
        for (var i = 0; i < allOrder.length; i++)
            none.push("")
        setTokens = none
        setLabels = none.slice()
        settingAll = true
        step = 0
        startCapture(page.rows[allOrder[0]].input)
    }

    // Whether Set All's step s may be left none: Button 2 to 8.
    function optionalAt(s) {
        var input = allOrder[s]
        return input >= 1 && input <= 7
    }

    // The next step, or with every one done, the buttons set.
    function nextStep() {
        step++
        stepNotice = ""
        if (step < allOrder.length) {
            captureInput = page.rows[allOrder[step]].input
            waitFor(optionalAt(step) ? 5 : 10)
            inputManager.captureController(key)
            return
        }
        var outcome = inputManager.setArcadeInputs(key, setTokens)
        cancelCapture()
        notice = outcome === "set" ? "Set: its layout is Custom now"
               : outcome === "unsaved" ? "Not saved: the settings could not be written" : ""
    }

    // A press taken for the step's input, unless it is the joystick, Guide or
    // one pressed already: then the step waits for another.
    function setAllPress(token, label) {
        var already = setTokens.indexOf(token)
        if (token === "guide") {
            stepNotice = label + " shows MAME's menu: press another button"
        } else if (!isChoice(token)) {
            stepNotice = label + " is the joystick: press a button"
        } else if (already >= 0) {
            stepNotice = label + " is " + page.rows[already].label + " already"
        } else {
            var tokens = setTokens.slice()
            var labels = setLabels.slice()
            tokens[allOrder[step]] = token
            labels[allOrder[step]] = label
            setTokens = tokens
            setLabels = labels
            nextStep()
            return
        }
        waitFor(optionalAt(step) ? 5 : 10)
        inputManager.captureController(key)
    }

    // Set All's presses so far, the last first: "BUTTON 2: Y".
    function setSoFar() {
        var list = []
        for (var s = step - 1; s >= 0; s--) {
            var i = allOrder[s]
            list.push(page.rows[i].label + ": " + (setTokens[i] !== "" ? setLabels[i] : "None"))
        }
        return list
    }

    // TEST BUTTONS: its presses are shown until 10 seconds go by without one,
    // or another controller's Back.
    function startTest() {
        if (!present) {
            notice = "Connect it first"
            return
        }
        notice = ""
        presses = []
        pressed = ""
        waitFor(10)
        testing = true
        inputManager.captureController(key)
        testOverlay.forceActiveFocus()
    }

    function endTest() {
        var hadKeys = testOverlay.activeFocus
        testing = false
        if (inputManager)
            inputManager.captureController("")
        if (hadKeys)
            list.forceActiveFocus()
    }

    // "X → BUTTON 1": which of the cabinet's a press is; the joystick's are
    // the joystick, Guide MAME's menu, and under Default another pad's
    // buttons are the emulators' to number.
    function testPress(token, label) {
        var role = ""
        var r = page.rows || []
        pressed = ""
        for (var i = 0; i < r.length && role === ""; i++) {
            if (r[i].token !== token)
                continue
            role = theirs && i < 8 ? "-" : r[i].label
            pressed = role !== "-" ? r[i].input : ""
        }
        var text = token === "guide" ? label + " → MAME's Menu"
                 : !isChoice(token) ? label + " → Joystick"
                 : role === "-" || (role === "" && theirs) ? label
                 : label + " → " + (role !== "" ? role : "None")
        var seen = presses.slice()
        seen.unshift(text)
        presses = seen.slice(0, 6)
        waitFor(10)
        inputManager.captureController(key)
    }

    function activate(row) {
        if (!row || !inputManager)
            return
        if (row.type === "layout")
            cycleLayout(1)
        else if (row.type === "setall")
            startSetAll()
        else if (row.type === "test")
            startTest()
        else if (row.type === "input")
            startCapture(row.input)
    }

    // The waits end on their own: a pad can't cancel them itself, its every
    // button is one to set (InputManager's own end comes later). Set All's
    // Button 2 to 8 are left none; its others end it, nothing changed. While
    // one runs the screen saver waits: the presses it takes are no keys.
    Timer {
        id: countdown
        interval: 1000
        repeat: true
        running: pageRoot.capturing || pageRoot.testing
        onTriggered: {
            if (idleTracker)
                idleTracker.resetActivity()
            pageRoot.secondsLeft--
            if (pageRoot.secondsLeft > 0)
                return
            if (pageRoot.testing) {
                pageRoot.endTest()
            } else if (pageRoot.optionalStep) {
                pageRoot.nextStep()
            } else {
                var waited = pageRoot.settingAll
                var label = pageRoot.inputLabel(pageRoot.captureInput)
                pageRoot.cancelCapture()
                if (waited)
                    pageRoot.notice = "No press for " + label + ": nothing changed"
            }
        }
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/keyboard.svg"
        title: "Arcade Buttons"
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
        anchors.leftMargin: root.sw * 0.115625 //74
        // Up to the control panel's drawing.
        width: panel.x - x - root.sw * 0.0125 //8
        // One line short of the space, so the ▼ fits above the help line; a
        // line more for each bar that is off (Main.qml's helpRoom, hintRoom).
        height: root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224
        clip: true
        focus: !pageRoot.capturing && !pageRoot.testing
        highlightMoveDuration: 0

        function move(step) {
            if (pageRoot.rows.length === 0)
                return
            currentIndex = pageRoot.selectableFrom(currentIndex + step, step)
            positionViewAtIndex(currentIndex, ListView.Contain)
            pageRoot.notice = ""
        }

        function change(step) {
            var row = pageRoot.rows[currentIndex]
            if (row && row.type === "layout")
                pageRoot.cycleLayout(step)
            else if (row && row.type === "input")
                pageRoot.cycleInput(row, step)
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

        // "BUTTON 1······◄ X ►", "BUTTONS ─────": the layout and the inputs,
        // which ◄ ► go round, between ◄ and ►.
        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            heading: modelData.type === "section"
            label: modelData.label || ""
            value: modelData.value || ""
            changes: modelData.type === "layout" || modelData.type === "input"
            selected: list.currentIndex === index && pageRoot.selectable(modelData)
        }
    }

    // The control panel, the line's input lit, at the content's right, level
    // with the list.
    ArcadePanel {
        id: panel
        lit: pageRoot.lit
        player: pageRoot.player
        visible: !pageRoot.capturing && !pageRoot.testing
        x: root.sw * 0.884375 - width //566
        anchors.verticalCenter: list.verticalCenter
    }

    // ▲ / ▼ while lines are hidden above or below.
    ScrollMarks {
        anchors.fill: list
        list: list
    }

    HelpLine {
        property var currentRow: pageRoot.rows[list.currentIndex]
        visible: !pageRoot.capturing && !pageRoot.testing && text !== ""
        text: pageRoot.notice !== "" ? pageRoot.notice : ((currentRow && currentRow.description) || "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        property var currentRow: pageRoot.rows[list.currentIndex]
        visible: !pageRoot.capturing && !pageRoot.testing
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + (currentRow && (currentRow.type === "layout" || currentRow.type === "input") ? root.hints.change + ":CHANGE " : "")
              + root.hints.select + (currentRow && currentRow.type === "test" ? ":TEST"
                                     : currentRow && currentRow.type === "setall" ? ":START" : ":SET")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // --- WAITING FOR A PRESS ---
    FocusScope {
        id: overlay
        anchors.fill: parent
        visible: pageRoot.capturing
        // Hidden, it lets go of the keys: an item only hidden would keep them.
        enabled: visible

        // This pad's presses never come here: InputManager reports them
        // through controllerInputCaptured instead. Another's do: its Back
        // cancels, nothing changed, the rest wait. Autorepeat is ignored: the
        // press that opened this may still be held when repeat kicks in.
        // Ctrl+Q and the rest go on to Main.qml.
        Keys.onPressed: function(event) {
            if (event.modifiers & Qt.ControlModifier)
                return
            if (!event.isAutoRepeat
                    && (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back)) {
                var all = pageRoot.settingAll
                pageRoot.cancelCapture()
                if (all)
                    pageRoot.notice = "Nothing changed"
            }
            event.accepted = true
        }

        PromptScreen {
            objectName: "capturePrompt"
            kind: "notice"
            title: pageRoot.settingAll ? "Set All Buttons (" + (pageRoot.step + 1) + "/" + pageRoot.allOrder.length + ")"
                                       : pageRoot.inputLabel(pageRoot.captureInput)
            message: (pageRoot.stepNotice !== "" ? pageRoot.stepNotice + "\n" : "")
                     + "Press the button on " + pageRoot.name + " for [" + pageRoot.inputLabel(pageRoot.captureInput) + "]"
                     + (pageRoot.optionalStep ? ", or wait if it has none" : "")
            // What Set All has taken so far, the last three.
            maxChoices: 3
            choices: pageRoot.settingAll ? pageRoot.setSoFar().slice(0, maxChoices) : []
            currentIndex: -1
            hint: (pageRoot.optionalStep ? "NONE IN " : "CANCELS IN ") + pageRoot.secondsLeft

            // The one to press, lit, at half the page's size: none on a
            // 240-line picture, which has no room for it.
            ArcadePanel {
                objectName: "capturePanel"
                cell: Math.floor(root.px / 2)
                visible: cell > 0
                lit: pageRoot.lit
                player: pageRoot.player
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    // --- TESTING ITS BUTTONS ---
    FocusScope {
        id: testOverlay
        anchors.fill: parent
        visible: pageRoot.testing
        enabled: visible

        // As the wait for a press: this pad's presses come through
        // controllerInputCaptured, another's Back ends it.
        Keys.onPressed: function(event) {
            if (event.modifiers & Qt.ControlModifier)
                return
            if (!event.isAutoRepeat
                    && (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back))
                pageRoot.endTest()
            event.accepted = true
        }

        // The presses as answers, the last boxed, at the top.
        PromptScreen {
            objectName: "testPrompt"
            kind: "notice"
            title: "Test Buttons"
            message: "Press the buttons on " + pageRoot.name
            // One fewer than a prompt's own, for the panel's room.
            maxChoices: root.hintBar ? 4 : 5
            choices: pageRoot.presses.slice(0, maxChoices)
            currentIndex: 0
            hint: "ENDS IN " + pageRoot.secondsLeft

            // The one the last press is, lit, as the wait's.
            ArcadePanel {
                objectName: "testPanel"
                cell: Math.floor(root.px / 2)
                visible: cell > 0
                lit: pageRoot.lit
                player: pageRoot.player
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }
}
