import QtQuick
import Components

// One controller's page, from Settings → Controls (Controllers.qml): which of
// its buttons does what, set by pressing them on it, and for a pad what its
// buttons are called (BUTTON LABELS, here and in the hints).
//
// A pad's button set here does that action instead of the one the shared map
// (the defaults, input.cfg) gives it, on this pad (this model) only; CLEAR A
// BUTTON makes one do nothing. A keyboard's or a remote's keys keep what they
// do: a key set here is an extra one for the action, on this one only, and
// REMOVE A KEY takes it away. RESET TO DEFAULTS forgets them all. Kept in
// config.json, app.controllers.<key>, through inputManager.
FocusScope {
    id: pageRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: ({})

    readonly property string key: navParams && navParams.key ? navParams.key : ""
    readonly property bool pad: key.indexOf("pad-") === 0
    // inputManager.controllerPage(): taken again whenever a controller comes,
    // goes or changes.
    property var page: ({})
    readonly property bool present: !!page.present
    // Its name as last seen: kept once a reset leaves nothing of one away.
    property string name: ""

    readonly property var styles: ["auto", "xbox", "playstation", "nintendo", "arcade"]
    readonly property var styleTitles: ({ auto: "Auto", xbox: "Xbox", playstation: "PlayStation",
                                          nintendo: "Nintendo", arcade: "Arcade" })

    property var rows: []
    // Waiting for a press on it: the action it's for, or "none" to clear one.
    property string captureAction: ""
    readonly property bool capturing: captureAction !== ""
    property int secondsLeft: 0
    // What the last press did, in the help line until the cursor moves.
    property string notice: ""

    // InputManager as the page opened, for its end: the context property may
    // read null while the view Loader tears down.
    property var manager: null

    Component.onCompleted: {
        manager = inputManager
        refresh()
        list.currentIndex = selectableFrom(0, 1)
        list.forceActiveFocus()
    }
    // Leaving the page while it waits for a press: the controller acts again
    // (and would anyway after a while: InputManager ends a wait by itself).
    Component.onDestruction: {
        if (manager)
            manager.captureController("")
    }

    Connections {
        target: inputManager
        function onControllersChanged() { pageRoot.refresh() }
        // What came of the press (InputManager::bindControllerInput()): a
        // keyboard's or a remote's Back ends the wait ("cancel"); its Select /
        // OK and the keys the menus use keep what they do ("already"), and so
        // do a pad's only Select / OK and Back ("only").
        function onControllerInputCaptured(key, input, label) {
            if (!pageRoot.capturing || key !== pageRoot.key)
                return
            var action = pageRoot.captureAction
            pageRoot.cancelCapture()
            var result = inputManager.bindControllerInput(pageRoot.key, input, action)
            if (result.outcome === "set")
                pageRoot.notice = action !== "none" ? label + " is " + pageRoot.actionLabel(action) + " now"
                                : pageRoot.pad ? label + " does nothing now" : label + " is no extra key now"
            else if (result.outcome === "already")
                pageRoot.notice = result.action === "none" ? label + " does nothing already"
                                : action === "none" ? label + " stays " + result.label
                                : label + " is " + result.label + " already"
            else if (result.outcome === "only")
                pageRoot.notice = label + " is its only " + result.label + " button"
            else if (result.outcome === "other")
                pageRoot.notice = label + " is not one of its extra keys"
        }
    }

    function actionLabel(action) {
        var r = page.rows || []
        for (var i = 0; i < r.length; i++) {
            if (r[i].action === action)
                return r[i].label
        }
        return action
    }

    function labelsValue() {
        if (page.labels === "auto" || !page.labels)
            return page.labelsInUse ? "Auto (" + page.labelsInUse + ")" : "Auto"
        return styleTitles[page.labels] || page.labels
    }

    function refresh() {
        if (!inputManager || key === "")
            return
        page = inputManager.controllerPage(key)
        if (page.name)
            name = page.name
        if (capturing && !present)
            cancelCapture()

        var items = []
        if (pad)
            items.push({ type: "labels", label: "Button Labels", value: labelsValue(),
                         description: "What its buttons are called, here and in the hints. Auto: as the pad says it is" })
        items.push({ type: "section", label: pad ? "Buttons" : "Keys" })
        var r = page.rows || []
        for (var i = 0; i < r.length; i++)
            items.push({ type: "action", action: r[i].action, label: r[i].label, value: r[i].value,
                         description: !present ? "Connect it to set its " + (pad ? "buttons" : "keys")
                                    : pad ? "Select, then press a button on it for " + r[i].label
                                    : "Select, then press a key on it: an extra one for " + r[i].label + ", on this one only" })
        items.push({ type: "clear", label: pad ? "Clear a Button" : "Remove a Key",
                     description: !present ? "Connect it to set its " + (pad ? "buttons" : "keys")
                                : pad ? "Select, then press a button on it that should do nothing"
                                : "Select, then press an extra key on it to take it away" })
        items.push({ type: "reset", label: "Reset to Defaults",
                     description: pad ? "Its buttons and labels as on every pad" : "No extra keys of its own" })

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

    function cycleLabels(step) {
        var i = styles.indexOf(page.labels || "auto")
        var next = styles[((i < 0 ? 0 : i) + step + styles.length) % styles.length]
        inputManager.setControllerLabels(key, next)
    }

    function startCapture(action) {
        if (!present) {
            notice = "Connect it first"
            return
        }
        notice = ""
        secondsLeft = 10
        captureAction = action
        inputManager.captureController(key)
        overlay.forceActiveFocus()
    }

    function cancelCapture() {
        captureAction = ""
        if (inputManager)
            inputManager.captureController("")
        list.forceActiveFocus()
    }

    function activate(row) {
        if (!row || !inputManager)
            return
        if (row.type === "labels") {
            cycleLabels(1)
        } else if (row.type === "action") {
            startCapture(row.action)
        } else if (row.type === "clear") {
            startCapture("none")
        } else if (row.type === "reset") {
            inputManager.resetController(key)
            // Away, it had nothing else: its line goes from the list too.
            if (!present)
                goBack()
            else
                notice = "Back to the defaults"
        }
    }

    // The wait ends on its own: a pad can't cancel it itself, its every button
    // is one to set (InputManager's own end comes 2 seconds later).
    Timer {
        interval: 1000
        repeat: true
        running: pageRoot.capturing
        onTriggered: {
            pageRoot.secondsLeft--
            if (pageRoot.secondsLeft <= 0)
                pageRoot.cancelCapture()
        }
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/keyboard.svg"
        title: "Controls"
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
        width: root.sw * 0.76875 //492
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
        Keys.onLeftPressed: {
            var row = pageRoot.rows[currentIndex]
            if (row && row.type === "labels")
                pageRoot.cycleLabels(-1)
        }
        Keys.onRightPressed: {
            var row = pageRoot.rows[currentIndex]
            if (row && row.type === "labels")
                pageRoot.cycleLabels(1)
        }
        Keys.onReturnPressed: pageRoot.activate(pageRoot.rows[currentIndex])
        Keys.onEnterPressed: pageRoot.activate(pageRoot.rows[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                pageRoot.goBack()
                event.accepted = true
            }
        }

        // "SELECT / OK······A, START", "BUTTONS ─────".
        delegate: MenuRow {
            width: list.width
            height: root.sh * 0.0583333 //28
            heading: modelData.type === "section"
            label: modelData.label || ""
            value: modelData.value || ""
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
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        property var currentRow: pageRoot.rows[list.currentIndex]
        visible: !pageRoot.capturing
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + (currentRow && currentRow.type === "labels" ? root.hints.change + ":CHANGE " : "")
              + root.hints.select + ":SET"
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

        // This controller's presses never come here: InputManager reports
        // them through controllerInputCaptured instead. Another's do: its
        // Back cancels, the rest wait. Autorepeat is ignored: the press that
        // opened this may still be held when repeat kicks in.
        Keys.onPressed: function(event) {
            if (!event.isAutoRepeat
                    && (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back))
                pageRoot.cancelCapture()
            event.accepted = true
        }

        PromptScreen {
            objectName: "capturePrompt"
            kind: "notice"
            title: pageRoot.captureAction === "none" ? (pageRoot.pad ? "Clear a button" : "Remove a key")
                                                     : (pageRoot.pad ? "New button" : "New key")
            message: {
                var name = pageRoot.name
                if (pageRoot.captureAction === "none")
                    return pageRoot.pad ? "Press the button on " + name + " that should do nothing"
                                        : "Press the extra key on " + name + " to take away"
                return (pageRoot.pad ? "Press a button on " : "Press a key on ") + name
                       + " for [" + pageRoot.actionLabel(pageRoot.captureAction) + "]"
            }
            // A pad's Back is one of the buttons to set: another controller's
            // cancels, or the wait running out.
            hint: pageRoot.pad ? "CANCELS IN " + pageRoot.secondsLeft
                               : root.hints.back + ":CANCEL (" + pageRoot.secondsLeft + ")"
        }
    }
}
