import QtQuick
import Components

// Which buttons work the menus: a gamepad's Menu Buttons (from its page,
// ControllerSettings.qml), and the same rows for a keyboard's or a remote's
// extra keys, or every keyboard's and remote's at once (All Keyboards &
// Remotes, inputManager.allKeys), from Settings → Controls → Keyboards &
// Remotes (Controllers.qml). Seven actions, each with what does it now:
//
//     SELECT / OK ························ A ►
//     BACK ························ B, BACK ►
//
// Select on one asks what to do, in a list, so nothing is changed by a stray
// press and back leaves it as it was:
//
//     REPLACE     a press: that button is the action's only one from now on.
//                 Where it was the pad's only Select / OK, Back, Up or Down,
//                 the button it takes the place of does that instead: A and
//                 B swap in one step, from either row.
//     ADD ANOTHER a press: that button does it too.
//     REMOVE ONE  from a list of the ones that do it: no press.
//     CANCEL
//
// A keyboard's own keys (the arrows, Enter, Esc, Space) always keep what they
// do: what is replaced, added and removed are extra keys, and the page has
// two more lines, TEST KEYS and REMOVE ALL EXTRA KEYS (asked first). A pad's
// test and reset are on its own page.
//
// The wait for a press is PressPrompt's: a tap answers, holding any button
// stops it, and so do ten seconds or another controller's Back. What came of
// it is said in the help line (InputManager's outcome), and so is a wait that
// changed nothing.
FocusScope {
    id: pageRoot
    // In the layout's frame (Main.qml): its rows in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})

    readonly property string key: navParams && navParams.key ? navParams.key : ""
    readonly property bool pad: key.indexOf("pad-") === 0
    readonly property bool all: page.kind === "keys"
    readonly property string noun: pad ? "button" : "key"
    // inputManager.controllerPage(): taken again whenever a controller comes,
    // goes or changes.
    property var page: ({})
    readonly property bool present: !!page.present
    property string name: ""

    property var rows: []
    // Waiting for a press: for this action, to replace what does it or to
    // add to it.
    property string captureAction: ""
    property bool replacing: false
    readonly property bool capturing: captureAction !== ""
    // What the last choice or press did, in the help line until the cursor moves.
    property string notice: ""

    // InputManager as the page opened, for its end: the context property may
    // read null while the view Loader tears down.
    property var manager: null

    Component.onCompleted: {
        manager = inputManager
        refresh()
        // Back from its test: on that line.
        var state = navParams.navListState || {}
        list.currentIndex = selectable(rows[state.currentIndex]) ? state.currentIndex : selectableFrom(0, 1)
        list.positionViewAtIndex(list.currentIndex, ListView.Contain)
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
        // A tap: the answer.
        function onControllerInputCaptured(key, input, label) {
            if (pageRoot.capturing && key === pageRoot.key)
                pageRoot.answer(input, label)
        }
        // Held: the way out.
        function onCaptureHeld(key) {
            if (pageRoot.capturing && key === pageRoot.key)
                pageRoot.endCapture("Nothing changed")
        }
    }

    function rowOf(action) {
        var r = page.rows || []
        for (var i = 0; i < r.length; i++) {
            if (r[i].action === action)
                return r[i]
        }
        return { action: action, label: action, value: "", inputs: [] }
    }

    function refresh() {
        if (!inputManager || key === "")
            return
        page = inputManager.controllerPage(key)
        if (page.name)
            name = page.name
        if (capturing && !present)
            endCapture("")

        var items = []
        if (!pad)
            items.push({ type: "test", label: "Test Keys",
                         description: present ? "What each key is and does" : "Connect it to test its keys" })
        items.push({ type: "section", label: pad ? "Buttons" : "Keys" })
        var r = page.rows || []
        for (var i = 0; i < r.length; i++)
            items.push({ type: "action", action: r[i].action, label: r[i].label, value: r[i].value,
                         description: pad ? "Replace, add or remove its buttons" : "Add, replace or remove an extra key" })
        if (!pad)
            items.push({ type: "reset", label: "Remove All Extra Keys",
                         description: all ? "Every keyboard's and remote's" : "This one's own" })

        var at = list.currentIndex
        rows = items
        list.currentIndex = at >= 0 && at < items.length && selectable(items[at]) ? at : selectableFrom(0, 1)
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

    // "B", "B and BACK", "LB, D-PAD ◄ and L STICK ◄".
    function listed(names) {
        if (names.length < 2)
            return names.join("")
        return names.slice(0, -1).join(", ") + " and " + names[names.length - 1]
    }

    // Select on an action: what to do with it.
    function ask(row) {
        var now = rowOf(row.action)
        var extras = now.inputs || []
        var choices = []
        if (pad || extras.length > 0)
            choices.push({ label: pad ? "Replace with a new button" : "Replace with a new key", action: "replace" })
        choices.push({ label: "Add another " + noun, action: "add" })
        if (extras.length > 0)
            choices.push({ label: "Remove one", action: "remove" })
        choices.push({ label: "Cancel", action: "cancel" })
        // A key's own comes first, so adding is its first choice.
        if (!pad && extras.length > 0)
            choices.unshift(choices.splice(1, 1)[0])
        chooser.action = row.action
        chooser.promptText = now.label
        chooser.subtitleText = "Now: " + now.value
        chooser.choices = choices
        chooser.open()
    }

    function startCapture(action, replace) {
        if (!present) {
            notice = "Connect it first"
            return
        }
        notice = ""
        captureAction = action
        replacing = replace
        inputManager.captureController(key)
        prompt.visible = true
        prompt.start(10)
        prompt.forceActiveFocus()
    }

    // The wait over, with what to say of it.
    function endCapture(said) {
        var hadKeys = prompt.activeFocus
        captureAction = ""
        prompt.visible = false
        if (inputManager)
            inputManager.captureController("")
        notice = said
        if (hadKeys)
            list.forceActiveFocus()
    }

    // A tap for the action waited for: what came of it
    // (InputManager::bindControllerInput(), replaceControllerInput()).
    function answer(input, label) {
        var action = captureAction
        var replace = replacing
        endCapture("")
        var result = replace ? inputManager.replaceControllerInput(key, input, action)
                             : inputManager.bindControllerInput(key, input, action)
        var does = rowOf(action).label
        var gone = result.cleared || []
        // "A is Back, B Select / OK, BACK nothing": short enough to be read
        // at once on the help line.
        var rest = gone.length === 0 ? "" : ", " + listed(gone) + (pad ? " nothing" : " no extra key")
        if (result.outcome === "set")
            notice = label + " is " + does + (replace ? "" : " too") + rest
        else if (result.outcome === "swapped")
            notice = label + " is " + does + ", " + result.other + " " + result.otherAction + rest
        else if (result.outcome === "already")
            notice = label + " is " + result.label + " already"
        else if (result.outcome === "only")
            notice = pad ? label + " is its only " + result.label + " button: it stays"
                         : label + " is the " + result.label + " in use: it stays"
        else if (result.outcome === "cancel")
            notice = "Nothing changed"
        else if (result.outcome === "unsaved")
            notice = "Not saved: the settings could not be written"
    }

    // Remove one: which, from a list.
    function askRemove(action) {
        var now = rowOf(action)
        var choices = (now.inputs || []).map(function(i) { return { label: i.label, action: i.input } })
        choices.push({ label: "Cancel", action: "" })
        remover.action = action
        remover.promptText = "Remove from " + now.label
        remover.choices = choices
        remover.open()
    }

    function remove(action, input) {
        var label = ""
        var inputs = rowOf(action).inputs || []
        for (var i = 0; i < inputs.length; i++) {
            if (inputs[i].input === input)
                label = inputs[i].label
        }
        var result = inputManager.removeControllerInput(key, input)
        if (result.outcome === "set")
            notice = pad ? label + " does nothing now" : label + " is no extra key now"
        else if (result.outcome === "only")
            notice = pad ? label + " is its only " + result.label + " button: it stays"
                         : label + " is the " + result.label + " in use: it stays"
        else if (result.outcome === "unsaved")
            notice = "Not saved: the settings could not be written"
    }

    function activate(row) {
        if (!row || !inputManager)
            return
        if (row.type === "test") {
            if (!present)
                notice = "Connect it first"
            else
                navigateTo("views/ControllerTest.qml", { key: key }, { currentIndex: list.currentIndex })
        } else if (row.type === "action") {
            if (!present)
                notice = "Connect it first"
            else
                ask(row)
        } else if (row.type === "reset") {
            resetConfirm.open()
        }
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/keyboard.svg"
        title: !pageRoot.present ? "Not Connected" : pageRoot.pad ? "Menu Buttons" : "Controls"
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
        // One line short of the space, so the ▼ fits above the help line; a
        // line more for each bar that is off (Main.qml's helpRoom, hintRoom).
        height: root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224
        clip: true
        focus: !pageRoot.capturing
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
        Keys.onRightPressed: {
            var row = pageRoot.rows[currentIndex]
            if (row && row.type !== "reset")
                pageRoot.activate(row)
        }
        Keys.onReturnPressed: pageRoot.activate(pageRoot.rows[currentIndex])
        Keys.onEnterPressed: pageRoot.activate(pageRoot.rows[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                pageRoot.goBack()
                event.accepted = true
            }
        }

        // "SELECT / OK······A ►", "BUTTONS ─────": every line opens something.
        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            heading: modelData.type === "section"
            label: modelData.label || ""
            value: modelData.value || ""
            opens: pageRoot.selectable(modelData)
            keepValue: modelData.type === "action"
            selected: list.currentIndex === index && pageRoot.selectable(modelData)
        }
    }

    // ▲ / ▼ while lines are hidden above or below.
    ScrollMarks {
        anchors.fill: list
        list: list
    }

    HelpLine {
        property var currentRow: pageRoot.rows[list.currentIndex]
        visible: !pageRoot.capturing && text !== ""
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
        readonly property string type: currentRow ? currentRow.type : ""
        visible: !pageRoot.capturing
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + root.hints.select + (type === "test" ? ":TEST" : type === "reset" ? ":REMOVE" : ":CHANGE")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // What to do with an action.
    ChoiceOverlay {
        id: chooser
        objectName: "actionChooser"
        property string action: ""
        anchors.fill: parent
        onActivated: function(choice) {
            if (choice === "replace" || choice === "add")
                pageRoot.startCapture(action, choice === "replace")
            else if (choice === "remove")
                pageRoot.askRemove(action)
        }
        onClosed: list.forceActiveFocus()
    }

    // Which of the ones that do it to take away.
    ChoiceOverlay {
        id: remover
        objectName: "removeChooser"
        property string action: ""
        anchors.fill: parent
        onActivated: function(input) {
            if (input !== "")
                pageRoot.remove(action, input)
        }
        onClosed: list.forceActiveFocus()
    }

    // A keyboard's extra keys, all of them: asked first, the cursor on Cancel.
    ChoiceOverlay {
        id: resetConfirm
        objectName: "resetConfirm"
        anchors.fill: parent
        promptText: "Remove all extra keys?"
        subtitleText: pageRoot.all ? "Every keyboard and remote keeps only the menus' own keys"
                                   : pageRoot.name + " keeps the menus' own keys, and every keyboard's extra ones"
        choices: [{ label: "Cancel", action: "cancel" }, { label: "Remove", action: "reset" }]
        onActivated: function(action) {
            if (action !== "reset" || !inputManager)
                return
            inputManager.resetController(pageRoot.key)
            // Away, it had nothing else: its line goes from the list too.
            if (!pageRoot.present)
                pageRoot.goBack()
            else
                pageRoot.notice = "No extra keys now"
        }
        onClosed: list.forceActiveFocus()
    }

    // --- WAITING FOR A PRESS ---
    PressPrompt {
        id: prompt
        objectName: "capturePrompt"
        keys: !pageRoot.pad
        title: (pageRoot.replacing ? "New " : "Another ") + pageRoot.noun
        message: {
            var now = pageRoot.rowOf(pageRoot.captureAction)
            var where = pageRoot.all ? "" : " on " + pageRoot.name
            return "Press the " + (pageRoot.replacing ? "new " : "") + pageRoot.noun + where + " for [" + now.label + "]"
        }
        onBackPressed: pageRoot.endCapture("Nothing changed")
        onTimedOut: pageRoot.endCapture("No press: nothing changed")
    }
}
