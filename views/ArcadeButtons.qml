import QtQuick
import Components

// A pad's Game Buttons, from its page in Settings → Controls
// (ControllerSettings.qml): which of its inputs is each of a cabinet's,
// Button 1 to 8, Coin and Start, the same in every game, MAME's and FinalBurn
// Neo's alike.
//
// LAYOUT is chosen from a list (back leaves it as it was): a preset (Fighting
// 6: a fighter's punches over its kicks, as a six-button cabinet's two rows;
// Neo Geo 4: A B C D on the four face buttons; NES, Mega Drive, SNES, Saturn
// and N64: a console pad's own), the emulators' own (Default: what OSD/OS
// always did, a JAMMA stick's pins' buttons in their order), or Custom, the
// inputs set by hand, which InputManager keeps while another layout is
// chosen, so Custom brings them back.
//
// SET ONE BY ONE asks for them in turn, 1 to 6, Coin, Start, 7 and 8, a press
// each, with the joystick left to go a step back and right to give this one
// no button (never Coin or Start); it waits as long as it takes, twenty
// seconds a step, and ends on what it has taken, all ten, to save, do again
// or drop. A line of its own asks in a list: a press, one of the inputs no
// line has, or none. Nothing changes by ◄ ► on a line.
//
// Every wait is PressPrompt's: a tap answers, holding any button stops it
// with nothing changed. Beside the lines a two-player cabinet's control panel
// (ArcadePanel) blinks the line's input, on the side of the player the pad
// plays as; the waits show the cabinet's buttons as a grid (ButtonGrid), the
// one asked for lit, and what the others are so far. Kept in config.json,
// app.controllers.<key>.arcade, through inputManager, which gives each
// player's to MAME and RetroArch as a game starts.
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
    // Its name as last seen.
    property string name: ""

    property var rows: []
    // Waiting for a press for one of them ("1" to "8", "coin", "start").
    property string captureInput: ""
    readonly property bool capturing: captureInput !== ""
    // What the last press or choice did, in the help line until the cursor moves.
    property string notice: ""

    // SET ONE BY ONE: the step it is at in allOrder, and the inputs taken so
    // far, with their names, in kArcadeInputs' order ("" for none yet).
    property bool settingAll: false
    property int step: 0
    property var setTokens: []
    property var setLabels: []
    // Why the last press was not taken.
    property string stepNotice: ""
    // Its end: all ten shown, to save, do again or drop.
    property bool summing: false
    property int sumIndex: 0
    // 1 to 6, Coin, Start, then 7 and 8.
    readonly property var allOrder: [0, 1, 2, 3, 4, 5, 8, 9, 6, 7]
    readonly property bool optionalStep: settingAll && optionalAt(step)
    readonly property int stepSeconds: 20

    // What the control panel lights: the line's input, blinking, so that it
    // shows on a tube.
    readonly property var lit: rows[list.currentIndex] && rows[list.currentIndex].type === "input" && blink
                               ? [rows[list.currentIndex].input] : []
    property bool blink: true
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
        // A tap: the answer.
        function onControllerInputCaptured(key, input, label) {
            if (key !== pageRoot.key || !pageRoot.capturing)
                return
            if (pageRoot.settingAll)
                pageRoot.stepPress(input, label)
            else
                pageRoot.setPress(input, label)
        }
        // Held: the way out.
        function onCaptureHeld(key) {
            if (key === pageRoot.key && pageRoot.capturing)
                pageRoot.endCapture("Nothing changed")
        }
    }

    Timer {
        interval: 400
        repeat: true
        running: list.activeFocus
        onRunningChanged: pageRoot.blink = true
        onTriggered: pageRoot.blink = !pageRoot.blink
    }

    function refresh() {
        if (!inputManager || key === "")
            return
        page = inputManager.arcadePage(key)
        if (page.name)
            name = page.name
        player = playerSide()
        if (capturing && !present)
            endCapture("")

        var items = [
            { type: "layout", label: "Layout", description: layoutDescription() },
            { type: "setall", label: "Set One by One",
              description: !present ? "Connect it to set its buttons" : "All ten in turn, a press each" },
            // The layout's name heads its buttons: beside the drawing a
            // line has no room for it after "LAYOUT".
            { type: "section", label: page.layoutTitle || "Buttons" }
        ]
        var r = page.rows || []
        for (var i = 0; i < r.length; i++)
            items.push({ type: "input", input: r[i].input, label: r[i].label, token: r[i].token, value: r[i].value,
                         description: "A press, another input, or none" })

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

    // The layout's own words, short enough to be read at once.
    function layoutDescription() {
        var words = {
            fighting6: "Punches 1 2 3 over kicks 4 5 6",
            neogeo4: "A B C D on the four face buttons",
            nes: "B is button 1, A button 2",
            megadrive: "A B C are 1 2 3, X Y Z 4 5 6",
            snes: "Y X L are 1 2 3, B A R 4 5 6",
            saturn: "X Y Z are 1 2 3, A B C 4 5 6",
            n64: "B C◄ C▲ are 1 2 3, A C▼ C► 4 5 6",
            custom: "Set by hand, line by line or in turn"
        }
        return words[page.layout] || (jamma ? "Its pins' buttons in their order"
                                            : "MAME and FinalBurn Neo number them")
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

    // LAYOUT: the list, the cursor on the one it has.
    function chooseLayout() {
        var layouts = page.layouts || []
        layoutChooser.choices = layouts.map(function(l) { return { label: l.title, action: l.layout } })
        layoutChooser.open()
        for (var i = 0; i < layouts.length; i++) {
            if (layouts[i].layout === page.layout)
                layoutChooser.choiceIndex = i
        }
    }

    // A line of its own: a press, one of the inputs the pad has that no
    // other line has, or none, which Coin and Start never are.
    function chooseInput(row) {
        var used = {}
        for (var i = 0; i < rows.length; i++) {
            if (rows[i].type === "input" && rows[i].input !== row.input && rows[i].token)
                used[rows[i].token] = true
        }
        var choices = []
        if (present)
            choices.push({ label: "Press a button", action: "press" })
        var all = page.choices || []
        for (var c = 0; c < all.length; c++) {
            if (!used[all[c].token] && all[c].token !== row.token)
                choices.push({ label: all[c].label, action: "token:" + all[c].token })
        }
        if (row.input !== "coin" && row.input !== "start" && row.token !== "")
            choices.push({ label: "None", action: "token:" })
        choices.push({ label: "Cancel", action: "cancel" })
        inputChooser.input = row.input
        inputChooser.promptText = row.label
        inputChooser.subtitleText = "Now: " + row.value
        inputChooser.choices = choices
        inputChooser.open()
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
            notice = result.label + " stays " + result.other + ": it needs a button"
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

    function startCapture(input) {
        if (!present) {
            notice = "Connect it first"
            return
        }
        notice = ""
        stepNotice = ""
        captureInput = input
        inputManager.captureController(key)
        prompt.visible = true
        prompt.start(settingAll ? stepSeconds : 10)
        prompt.forceActiveFocus()
    }

    // The wait over, with what to say of it.
    function endCapture(said) {
        var hadKeys = prompt.activeFocus
        captureInput = ""
        settingAll = false
        prompt.visible = false
        if (inputManager)
            inputManager.captureController("")
        notice = said
        if (hadKeys)
            list.forceActiveFocus()
    }

    function setPress(token, label) {
        var input = captureInput
        endCapture("")
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

    // Whether Set One by One's step s may be left none: Button 2 to 8.
    function optionalAt(s) {
        var input = allOrder[s]
        return input >= 1 && input <= 7
    }

    // The step it is at, asked for (again).
    function askStep() {
        captureInput = page.rows[allOrder[step]].input
        prompt.start(stepSeconds)
        inputManager.captureController(key)
    }

    function take(token, label) {
        var tokens = setTokens.slice()
        var labels = setLabels.slice()
        tokens[allOrder[step]] = token
        labels[allOrder[step]] = label
        setTokens = tokens
        setLabels = labels
    }

    // The next step, or with every one done, all ten to look at.
    function nextStep() {
        step++
        stepNotice = ""
        if (step < allOrder.length) {
            askStep()
            return
        }
        var hadKeys = prompt.activeFocus
        captureInput = ""
        settingAll = false
        prompt.visible = false
        inputManager.captureController("")
        summing = true
        sumIndex = 0
        if (hadKeys)
            summary.forceActiveFocus()
    }

    // A tap in Set One by One. The joystick is never a button, so it steers:
    // left a step back, right no button for this one. Guide, the joystick's
    // up and down and a button taken already ask again.
    function stepPress(token, label) {
        var left = token === "dpleft" || token === "leftx-"
        var right = token === "dpright" || token === "leftx+"
        var already = setTokens.indexOf(token)
        if (left) {
            stepNotice = ""
            if (step > 0) {
                step--
                take("", "")
            }
        } else if (right) {
            if (optionalStep) {
                take("", "")
                nextStep()
                return
            }
            stepNotice = inputLabel(captureInput) + " needs a button"
        } else if (token === "guide") {
            stepNotice = label + " shows MAME's menu: press another"
        } else if (!isChoice(token)) {
            stepNotice = label + " is the joystick: press a button"
        } else if (already >= 0) {
            stepNotice = label + " is " + page.rows[already].label + " already"
        } else {
            take(token, label)
            nextStep()
            return
        }
        askStep()
    }

    // The grid's names: Set One by One's so far, else the page's.
    function gridLabels() {
        var labels = {}
        var r = page.rows || []
        for (var i = 0; i < r.length; i++) {
            var value = settingAll || summing ? (setLabels[i] || "") : r[i].value
            labels[r[i].input] = value === "None" || value === "Engine's" ? "" : value
        }
        return labels
    }

    // The summary's answer.
    function finish(action) {
        summing = false
        list.forceActiveFocus()
        if (action === "redo") {
            startSetAll()
            return
        }
        if (action !== "save") {
            notice = "Nothing changed"
            return
        }
        var outcome = inputManager.setArcadeInputs(key, setTokens)
        notice = outcome === "set" ? "Saved: its layout is Custom now"
               : outcome === "unsaved" ? "Not saved: the settings could not be written" : "Nothing changed"
    }

    function activate(row) {
        if (!row || !inputManager)
            return
        if (row.type === "layout")
            chooseLayout()
        else if (row.type === "setall")
            startSetAll()
        else if (row.type === "input")
            chooseInput(row)
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/keyboard.svg"
        title: pageRoot.present ? "Game Buttons" : "Not Connected"
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
        focus: !pageRoot.capturing && !pageRoot.summing
        highlightMoveDuration: 0

        function move(step) {
            if (pageRoot.rows.length === 0)
                return
            currentIndex = pageRoot.selectableFrom(currentIndex + step, step)
            positionViewAtIndex(currentIndex, ListView.Contain)
            pageRoot.notice = ""
        }

        Keys.onUpPressed: move(-1)
        Keys.onDownPressed: move(1)
        Keys.onRightPressed: pageRoot.activate(pageRoot.rows[currentIndex])
        Keys.onReturnPressed: pageRoot.activate(pageRoot.rows[currentIndex])
        Keys.onEnterPressed: pageRoot.activate(pageRoot.rows[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                pageRoot.goBack()
                event.accepted = true
            }
        }

        // "BUTTON 1········X ►", "FIGHTING 6 ─────": every line opens a list
        // or a wait.
        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            heading: modelData.type === "section"
            label: modelData.label || ""
            value: modelData.value || ""
            opens: pageRoot.selectable(modelData)
            selected: list.currentIndex === index && pageRoot.selectable(modelData)
        }
    }

    // The control panel, the line's input blinking, at the content's right,
    // level with the list.
    ArcadePanel {
        id: panel
        lit: pageRoot.lit
        player: pageRoot.player
        visible: !pageRoot.capturing && !pageRoot.summing
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
        visible: !pageRoot.capturing && !pageRoot.summing && text !== ""
        text: pageRoot.notice !== "" ? pageRoot.notice : ((currentRow && currentRow.description) || "")
        // What became of a press is said with Help Line off too.
        always: pageRoot.notice !== ""
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        property var currentRow: pageRoot.rows[list.currentIndex]
        visible: !pageRoot.capturing && !pageRoot.summing
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + root.hints.select + (currentRow && currentRow.type === "setall" ? ":START" : ":CHANGE")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // The layout, from a list: back leaves it as it was.
    ChoiceOverlay {
        id: layoutChooser
        objectName: "layoutChooser"
        anchors.fill: parent
        promptText: "Layout"
        subtitleText: "Custom is what was set by hand: it is kept while another is chosen"
        onActivated: function(layout) {
            if (inputManager)
                inputManager.setArcadeLayout(pageRoot.key, layout)
        }
        onClosed: list.forceActiveFocus()
    }

    // One of the ten: a press, an input no line has, or none.
    ChoiceOverlay {
        id: inputChooser
        objectName: "inputChooser"
        property string input: ""
        anchors.fill: parent
        onActivated: function(action) {
            if (action === "press")
                pageRoot.startCapture(input)
            else if (action.indexOf("token:") === 0)
                pageRoot.showOutcome(pageRoot.inputLabel(input),
                                     inputManager.setArcadeInput(pageRoot.key, input, action.substring(6)))
        }
        onClosed: list.forceActiveFocus()
    }

    // --- WAITING FOR A PRESS ---
    PressPrompt {
        id: prompt
        objectName: "capturePrompt"
        title: pageRoot.settingAll ? "Set One by One (" + (pageRoot.step + 1) + "/" + pageRoot.allOrder.length + ")"
                                   : pageRoot.inputLabel(pageRoot.captureInput)
        message: (pageRoot.stepNotice !== "" ? pageRoot.stepNotice + "\n" : "")
                 + "Press the button on " + pageRoot.name + " for [" + pageRoot.inputLabel(pageRoot.captureInput) + "]"
        extra: !pageRoot.settingAll ? ""
               : "Joystick ◄ back" + (pageRoot.optionalStep ? ", ► no button" : "")
        onBackPressed: pageRoot.endCapture("Nothing changed")
        onTimedOut: pageRoot.endCapture("No press: nothing changed")

        // The cabinet's buttons, the one asked for lit, and what the others
        // are: Set One by One's so far, the page's for a line's own.
        ButtonGrid {
            objectName: "captureGrid"
            lit: pageRoot.capturing ? [pageRoot.captureInput] : []
            labels: pageRoot.capturing ? pageRoot.gridLabels() : ({})
        }
    }

    // --- SET ONE BY ONE'S END: ALL TEN, TO SAVE ---
    FocusScope {
        id: summary
        objectName: "summary"
        anchors.fill: parent
        visible: pageRoot.summing
        // Hidden, it lets go of the keys.
        enabled: visible

        readonly property var answers: [{ label: "Save", action: "save" }, { label: "Do It Again", action: "redo" },
                                        { label: "Cancel", action: "cancel" }]

        Keys.onPressed: function(event) {
            if (event.modifiers & Qt.ControlModifier)
                return
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back)
                pageRoot.finish("cancel")
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                pageRoot.finish(answers[pageRoot.sumIndex].action)
            else if (event.key === Qt.Key_Up)
                pageRoot.sumIndex = (pageRoot.sumIndex + answers.length - 1) % answers.length
            else if (event.key === Qt.Key_Down)
                pageRoot.sumIndex = (pageRoot.sumIndex + 1) % answers.length
            event.accepted = true
        }

        PromptScreen {
            objectName: "summaryPrompt"
            title: "Save these buttons?"
            message: "They become " + pageRoot.name + "'s Custom layout"
            choices: summary.answers
            currentIndex: pageRoot.sumIndex
            onHovered: function(index) { pageRoot.sumIndex = index }

            ButtonGrid {
                objectName: "summaryGrid"
                labels: pageRoot.summing ? pageRoot.gridLabels() : ({})
            }
        }
    }
}
