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
//
// TEST BUTTONS (TEST KEYS) shows what each press on it is called, and acts on
// none. On the JAMMA board's sticks it tests both players' controls at once,
// each press with its pin on the board, from osdos-jamma
// (inputManager.watchJammaPins()), the pins the sticks don't show among them
// (NONE): the way to see how a board is wired. The edge connector's control
// pins are a grid there (JammaPinGrid), each lit as its switch reaches the
// board, and a keyboard's N goes round the layouts that name the board's
// pins.
FocusScope {
    id: pageRoot

    signal goBack()

    property var navParams: ({})

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

    // Testing its buttons: the presses seen, the last first, { player, label,
    // token, pin, pinInput, at }: label what InputManager calls it ("" for a
    // pin the sticks don't show) and token its input; player, pin and
    // pinInput (what osdos-jamma's mapping makes of the pin) the JAMMA
    // board's, 0 and "" elsewhere.
    property bool testing: false
    property var presses: []
    // The JAMMA board's player a stick of its is (1, 2), else 0; the board's
    // sticks a test of one takes, by key ({ key: player }); and why the
    // board's pins can't be read, once that is known.
    readonly property int jamma: page.jamma || 0
    property var sticks: ({})
    property string pinsNotice: ""
    // The board's pins seen this test, for the grid (JammaPinGrid.pins), and
    // the layout it names them by.
    property var jammaPins: ({})
    property int jammaLayout: 0
    // How long a test waits for the next press: on the JAMMA board's, time
    // to read the grid too.
    readonly property int testSeconds: jamma ? 30 : 10
    // A press and its pin come apart, the one a moment after the other.
    readonly property int pairMs: 300
    // What SDL makes of a pin's input on the sticks (its Linux mapping), for
    // a pin and its press to be told for each other: x and y either way round,
    // as SDL's versions map them. A pin of an input not here (z, c, none) is
    // one the sticks don't show.
    readonly property var pinTokens: ({
        a: ["a"], b: ["b"], x: ["x", "y"], y: ["x", "y"],
        tl: ["leftshoulder"], tr: ["rightshoulder"],
        tl2: ["lefttrigger+", "lefttrigger"], tr2: ["righttrigger+", "righttrigger"],
        start: ["start"], coin: ["back"], mode: ["guide"],
        up: ["lefty-", "dpup"], down: ["lefty+", "dpdown"], left: ["leftx-", "dpleft"], right: ["leftx+", "dpright"]
    })

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
        if (manager) {
            manager.captureController("")
            manager.watchJammaPins(false)
        }
    }

    Connections {
        target: inputManager
        function onControllersChanged() { pageRoot.refresh() }
        // What came of the press (InputManager::bindControllerInput()): a
        // keyboard's or a remote's Back ends the wait ("cancel"); its Select /
        // OK and the keys the menus use keep what they do ("already"), and so
        // do a pad's only Select / OK and Back ("only"). One config.json
        // couldn't take is as it was ("unsaved").
        function onControllerInputCaptured(key, input, label) {
            if (pageRoot.testing) {
                if (key === pageRoot.key || pageRoot.sticks[key] !== undefined)
                    pageRoot.testPress(key, input, label)
                return
            }
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
            else if (result.outcome === "unsaved")
                pageRoot.notice = "Not saved: the settings could not be written"
        }
        function onJammaPinChanged(player, address, pin, pressed, input) {
            if (!pageRoot.testing || !pageRoot.jamma)
                return
            var pins = Object.assign({}, pageRoot.jammaPins)
            pins[address + " " + pin] = { player: player, address: address, pin: pin, input: input, held: pressed }
            pageRoot.jammaPins = pins
            if (pressed && player > 0)
                pageRoot.notePress(player, "", "", pin, input)
            else
                pageRoot.keepTesting()
        }
        function onJammaPinsFailed(reason) {
            if (pageRoot.testing && pageRoot.jamma)
                pageRoot.pinsNotice = reason
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
        if (testing && !present)
            endTest()

        var items = []
        if (pad)
            items.push({ type: "labels", label: "Button Labels", value: labelsValue(),
                         description: "What its buttons are called, here and in the hints. Auto: as the pad says it is" })
        items.push({ type: "test", label: pad ? "Test Buttons" : "Test Keys",
                     description: !present ? "Connect it to test its " + (pad ? "buttons" : "keys")
                                : jamma ? "Press the board's controls, both players', to see each one's pin on the board and what OSD/OS sees"
                                : pad ? "Press its buttons to see what each one is called"
                                : "Press its keys to see what each one is called" })
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

    // TEST BUTTONS: its presses are shown until 10 seconds go by without one
    // (30 on the JAMMA board's), or another controller's Back (a keyboard's
    // or a remote's own Back). A JAMMA stick's takes the board's other stick
    // too, and its pins.
    function startTest() {
        if (!present) {
            notice = "Connect it first"
            return
        }
        notice = ""
        presses = []
        pinsNotice = ""
        jammaPins = {}
        secondsLeft = testSeconds
        var found = {}
        if (jamma) {
            var all = inputManager.controllers || []
            for (var i = 0; i < all.length; i++) {
                if (all[i].jamma > 0 && all[i].present)
                    found[all[i].key] = all[i].jamma
            }
            found[key] = jamma
        }
        sticks = found
        testing = true
        captureTested()
        if (jamma)
            inputManager.watchJammaPins(true)
        testOverlay.forceActiveFocus()
    }

    function captureTested() {
        inputManager.captureControllers(jamma ? Object.keys(sticks) : [key])
    }

    function endTest() {
        // The list takes the keys back from the test only: not from what
        // has taken them since.
        var hadKeys = testOverlay.activeFocus
        testing = false
        sticks = {}
        if (inputManager) {
            inputManager.captureController("")
            inputManager.watchJammaPins(false)
        }
        if (hadKeys)
            list.forceActiveFocus()
    }

    function testPress(from, input, label) {
        if (!pad && inputManager.keyCapture(key, Number(input), "none").outcome === "cancel") {
            endTest()
            return
        }
        notePress(sticks[from] || 0, label, input, "", "")
    }

    // A press seen, by InputManager (label, token) or on the JAMMA board
    // (pin, pinInput): the other half of one of the same player's seen a
    // moment before, by what the pin's input is to SDL (pinTokens), the
    // oldest of those, or a press of its own. Either keeps the test going,
    // InputManager's wait with it, and is activity to the screen saver, which
    // takes no key from it.
    function notePress(player, label, token, pin, pinInput) {
        var now = Date.now()
        var seen = presses.slice()
        var recent = 0
        while (recent < seen.length && now - seen[recent].at < pairMs)
            recent++
        var paired = false
        for (var i = recent - 1; i >= 0 && !paired; i--) {
            var p = seen[i]
            if (p.player !== player)
                continue
            var tokens = pinTokens[pin !== "" ? pinInput : p.pinInput]
            if (!tokens || (pin !== "" ? p.pin !== "" || tokens.indexOf(p.token) < 0
                                       : p.label !== "" || p.pin === "" || tokens.indexOf(token) < 0))
                continue
            seen[i] = pin !== "" ? { player: player, label: p.label, token: p.token, pin: pin, pinInput: pinInput, at: p.at }
                                 : { player: player, label: label, token: token, pin: p.pin, pinInput: p.pinInput, at: p.at }
            paired = true
        }
        if (!paired)
            seen.unshift({ player: player, label: label, token: token, pin: pin, pinInput: pinInput, at: now })
        presses = seen.slice(0, 6)
        keepTesting()
    }

    // Something of the test's came: it goes on.
    function keepTesting() {
        secondsLeft = testSeconds
        captureTested()
        if (idleTracker)
            idleTracker.resetActivity()
    }

    // "P1 B1 → 7" on the JAMMA board, the player, the pin and what OSD/OS sees
    // ("?" for a pin not come yet, NONE for one the sticks don't show); just
    // "7" elsewhere.
    function pressText(p) {
        var label = p.label !== "" ? p.label : "NONE"
        if (!jamma)
            return label
        var pin = p.pin !== "" ? p.pin : pinsNotice === "" ? "?" : ""
        return "P" + p.player + " " + (pin !== "" ? pin + " → " : "") + label
    }

    function activate(row) {
        if (!row || !inputManager)
            return
        if (row.type === "labels") {
            cycleLabels(1)
        } else if (row.type === "test") {
            startTest()
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
    // is one to set (InputManager's own end comes 2 seconds later). So does a
    // test, 10 seconds after its last press.
    Timer {
        interval: 1000
        repeat: true
        running: pageRoot.capturing || pageRoot.testing
        onTriggered: {
            pageRoot.secondsLeft--
            if (pageRoot.secondsLeft > 0)
                return
            if (pageRoot.testing)
                pageRoot.endTest()
            else
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
        focus: !pageRoot.capturing && !pageRoot.testing
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
              + (currentRow && currentRow.type === "labels" ? root.hints.change + ":CHANGE " : "")
              + root.hints.select + (currentRow && currentRow.type === "test" ? ":TEST" : ":SET")
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
        // opened this may still be held when repeat kicks in. Ctrl+Q and
        // the rest go on to Main.qml.
        Keys.onPressed: function(event) {
            if (event.modifiers & Qt.ControlModifier)
                return
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

    // --- TESTING ITS BUTTONS ---
    FocusScope {
        id: testOverlay
        anchors.fill: parent
        visible: pageRoot.testing
        enabled: visible

        // As the wait for a press: this controller's presses come through
        // controllerInputCaptured, another's Back ends it. Ctrl+Q and the
        // rest go on to Main.qml.
        Keys.onPressed: function(event) {
            if (event.modifiers & Qt.ControlModifier)
                return
            if (event.isAutoRepeat) {
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                pageRoot.endTest()
            } else if (event.key === Qt.Key_N && pageRoot.jamma) {
                // A keyboard's N: the next layout of the board's pins.
                pageRoot.jammaLayout = (pageRoot.jammaLayout + 1) % pinGrid.layouts.length
                pageRoot.keepTesting()
            }
            event.accepted = true
        }

        // The presses as answers, the last boxed, at the top.
        PromptScreen {
            id: testPrompt
            objectName: "testPrompt"
            kind: "notice"
            title: pageRoot.pad ? "Test Buttons" : "Test Keys"
            message: pageRoot.jamma && pageRoot.pinsNotice !== ""
                     ? "The board's pins can't be read: " + pageRoot.pinsNotice
                     : pageRoot.jamma ? "Layout: " + pinGrid.layoutName
                     : (pageRoot.pad ? "Press the buttons on " : "Press the keys on ") + pageRoot.name
            // On the JAMMA board's, under its grid: the last two, each
            // press's pin on the board → what OSD/OS sees.
            maxChoices: pageRoot.jamma ? 2 : (root.hintBar ? 5 : 6)
            choices: pageRoot.presses.slice(0, maxChoices).map(pageRoot.pressText)
            currentIndex: 0
            hint: pageRoot.jamma ? "ENDS IN " + pageRoot.secondsLeft + " N:LAYOUT"
                  : pageRoot.pad ? "ENDS IN " + pageRoot.secondsLeft
                  : root.hints.back + ":END (" + pageRoot.secondsLeft + ")"

            JammaPinGrid {
                id: pinGrid
                objectName: "pinGrid"
                shown: pageRoot.jamma > 0 && pageRoot.pinsNotice === ""
                pins: pageRoot.jammaPins
                layout: pageRoot.jammaLayout
            }
        }
    }
}
