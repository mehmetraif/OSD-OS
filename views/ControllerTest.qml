import QtQuick
import Components

// A controller's test, the one there is for it: TEST BUTTONS on a gamepad's
// page (ControllerSettings.qml), TEST KEYS on a keyboard's or a remote's
// (MenuButtons.qml). It opens waiting, shows each press as it comes and acts
// on none, the last at the top, each a line of what it is called, what it
// does in the menus and, on a pad, what it is in the games:
//
//     A → SELECT / OK → BUTTON 4
//     D-PAD ▲ → UP → JOYSTICK
//     X → NO ACTION → BUTTON 1
//
// with the cabinet's buttons as a grid under the text, the last press's lit
// (ButtonGrid). It ends, back on the page it came from, when any button is
// held (InputManager's captureHeld()), ten seconds after the last press, at
// another controller's Back, or at a keyboard's or a remote's own Back.
//
// On one of the JAMMA board's sticks it takes both players' sticks at once
// and the board's pins with them, from osdos-jamma
// (inputManager.watchJammaPins()), the pins the sticks don't show among them
// (NONE): the way to see how a board is wired. The edge connector's control
// pins are a grid there (JammaPinGrid), each lit as its switch reaches the
// board, named by the layout chosen on the stick's page (navParams.pinLayout),
// and it waits thirty seconds, as the grid is read too.
FocusScope {
    id: testRoot
    // In the layout's frame (Main.qml), as the page it was opened from: its
    // text and grid in the pane, beside the column a layout keeps on the
    // screen (PromptScreen draws both).
    readonly property bool inFrame: true

    signal goBack()

    property var navParams: ({})

    readonly property string key: navParams && navParams.key ? navParams.key : ""
    readonly property bool pad: key.indexOf("pad-") === 0
    property var page: ({})
    readonly property int jamma: page.jamma || 0
    property string name: ""

    // The presses seen, the last first, { player, label, token, pin,
    // pinInput, at }: label what InputManager calls it ("" for a pin the
    // sticks don't show) and token its input; player, pin and pinInput (what
    // osdos-jamma's mapping makes of the pin) the JAMMA board's, 0 and ""
    // elsewhere.
    property var presses: []
    // Which of the cabinet's the last press is ("1" to "8", "coin", "start").
    property string pressed: ""
    // The board's sticks a JAMMA stick's test takes, by key ({ key: player });
    // why the board's pins can't be read, once that is known; and the pins
    // seen, for the grid (JammaPinGrid.pins).
    property var sticks: ({})
    property string pinsNotice: ""
    property var jammaPins: ({})
    // How long it waits for the next press.
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
    // A Game Button's input by its label ("Button 4": "4").
    readonly property var gameInputs: ({ "Button 1": "1", "Button 2": "2", "Button 3": "3", "Button 4": "4",
                                         "Button 5": "5", "Button 6": "6", "Button 7": "7", "Button 8": "8",
                                         "Coin": "coin", "Start": "start" })

    // InputManager as the page opened, for its end: the context property may
    // read null while the view Loader tears down.
    property var manager: null
    property bool ended: false

    Component.onCompleted: {
        manager = inputManager
        // Nothing to test: back, once the view is in its place.
        if (!inputManager || key === "") {
            Qt.callLater(end)
            return
        }
        page = inputManager.controllerPage(key)
        name = page.name || ""
        if (!page.present) {
            Qt.callLater(end)
            return
        }
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
        capture()
        if (jamma)
            inputManager.watchJammaPins(true)
        prompt.visible = true
        prompt.start(testSeconds)
        prompt.forceActiveFocus()
    }
    Component.onDestruction: {
        if (manager) {
            manager.captureController("")
            manager.watchJammaPins(false)
        }
    }

    Connections {
        target: inputManager
        function onControllersChanged() {
            // Unplugged: nothing left to test.
            if (!testRoot.ended && !inputManager.controllerPage(testRoot.key).present)
                testRoot.end()
        }
        function onControllerInputDown(key, input, label) {
            if (!testRoot.ended && testRoot.mine(key))
                testRoot.testPress(key, input, label)
        }
        function onCaptureHeld(key) {
            if (testRoot.mine(key))
                testRoot.end()
        }
        function onJammaPinChanged(player, address, pin, pressed, input) {
            if (testRoot.ended || !testRoot.jamma)
                return
            var pins = Object.assign({}, testRoot.jammaPins)
            pins[address + " " + pin] = { player: player, address: address, pin: pin, input: input, held: pressed }
            testRoot.jammaPins = pins
            if (pressed && player > 0)
                testRoot.notePress(player, "", "", pin, input)
            else
                testRoot.keepTesting()
        }
        function onJammaPinsFailed(reason) {
            if (testRoot.jamma)
                testRoot.pinsNotice = reason
        }
    }

    function mine(key) {
        return key === testRoot.key || sticks[key] !== undefined
    }

    function capture() {
        inputManager.captureControllers(jamma ? Object.keys(sticks) : [key])
    }

    // Over: the controller acts again, and the page it came from is back.
    function end() {
        if (ended)
            return
        ended = true
        prompt.stop()
        if (inputManager) {
            inputManager.captureController("")
            inputManager.watchJammaPins(false)
        }
        goBack()
    }

    function testPress(from, input, label) {
        // A keyboard's or a remote's own Back ends it.
        if (!pad && inputManager.keyCapture(key, Number(input), "none").outcome === "cancel") {
            end()
            return
        }
        notePress(sticks[from] || 0, label, input, "", "", from)
    }

    // A press seen, by InputManager (label, token) or on the JAMMA board
    // (pin, pinInput): the other half of one of the same player's seen a
    // moment before, by what the pin's input is to SDL (pinTokens), the
    // oldest of those, or a press of its own. Either keeps the test going,
    // InputManager's wait with it.
    function notePress(player, label, token, pin, pinInput, from) {
        var now = Date.now()
        var seen = presses.slice()
        var roles = label !== "" ? inputManager.inputRoles(from || key, token) : { action: "", game: "" }
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
            seen[i] = pin !== "" ? { player: player, label: p.label, token: p.token, roles: p.roles, pin: pin,
                                     pinInput: pinInput, at: p.at }
                                 : { player: player, label: label, token: token, roles: roles, pin: p.pin,
                                     pinInput: p.pinInput, at: p.at }
            paired = true
        }
        if (!paired)
            seen.unshift({ player: player, label: label, token: token, roles: roles, pin: pin, pinInput: pinInput, at: now })
        presses = seen.slice(0, 6)
        if (label !== "")
            pressed = gameInputs[roles.game] || ""
        keepTesting()
    }

    // Something of the test's came: it goes on.
    function keepTesting() {
        if (ended)
            return
        prompt.start(testSeconds)
        capture()
    }

    // "A → SELECT / OK → BUTTON 4"; on the JAMMA board "P1 B1 → 4 → BUTTON 4",
    // the player, the pin and what OSD/OS sees ("?" for a pin not come yet,
    // NONE for one the sticks don't show).
    function pressText(p) {
        var parts = []
        if (jamma) {
            var pin = p.pin !== "" ? p.pin : pinsNotice === "" ? "?" : ""
            parts.push("P" + p.player + (pin !== "" ? " " + pin : ""))
        }
        parts.push(p.label !== "" ? p.label : "None")
        if (p.label !== "") {
            // A stick's page has room for one more: what it is in the games.
            if (!jamma)
                parts.push(p.roles.action !== "" ? p.roles.action : "No action")
            if (pad && p.roles.game !== "")
                parts.push(p.roles.game)
        }
        return parts.join(" → ")
    }

    PressPrompt {
        id: prompt
        objectName: "testPrompt"
        keys: !testRoot.pad
        title: testRoot.pad ? "Test Buttons" : "Test Keys"
        message: testRoot.jamma && testRoot.pinsNotice !== ""
                 ? "The board's pins can't be read: " + testRoot.pinsNotice
                 : testRoot.jamma ? "Press the controls. Pin names: " + pinGrid.layoutName
                 : page.kind === "keys" ? "Press the keys of any keyboard or remote"
                 : (testRoot.pad ? "Press the buttons on " : "Press the keys on ") + testRoot.name
        // Under a grid: the last two on the JAMMA board's, three on a pad's.
        maxChoices: testRoot.jamma ? 2 : testRoot.pad ? 3 : (root.hintBar ? 4 : 5)
        choices: testRoot.presses.slice(0, maxChoices).map(testRoot.pressText)
        currentIndex: 0
        onBackPressed: testRoot.end()
        onTimedOut: testRoot.end()

        ButtonGrid {
            objectName: "testGrid"
            // Shown, taking room: on a pad's test only.
            readonly property bool shown: testRoot.pad && !testRoot.jamma
            visible: shown
            height: shown ? cellHeight * 2 : 0
            lit: testRoot.pressed !== "" ? [testRoot.pressed] : []
        }
        JammaPinGrid {
            id: pinGrid
            objectName: "pinGrid"
            shown: testRoot.jamma > 0 && testRoot.pinsNotice === ""
            pins: testRoot.jammaPins
            layout: testRoot.navParams && testRoot.navParams.pinLayout ? testRoot.navParams.pinLayout : 0
        }
    }
}
