import QtQuick
import QtTest
import "../../views/Components/ArcadePanelArt.js" as Art

// Settings → Controls' pages against a stand-in for InputManager:
// views/Controllers.qml (the gamepads first, the one used last on top, then
// Keyboards & Remotes and Not Connected, each a list of its own),
// ControllerSettings.qml (a pad's six lines: its player from a list, its
// button names, Reset asked first), MenuButtons.qml (an action's buttons
// replaced, added and removed from a list, on a pad, a keyboard and every
// keyboard at once; the wait for a press, which a tap answers and a hold, a
// timeout or another controller's Back stops, each saying what changed),
// ControllerTest.qml (a controller's one test: what each press is called,
// does and is in the games; the JAMMA board's two sticks together with their
// pins) and ArcadeButtons.qml (a pad's Game Buttons: the layout from a list,
// a line from a list or a press, Set One by One with the joystick a step
// back and no button, its summary saved, done again or dropped, the control
// panel drawn beside them), and CabinetLock.qml's wait for the unlock
// buttons.
Rectangle {
    id: root
    color: surfaceColor
    width: 640
    height: 480
    // Main.qml's, as the views and their components read them.
    property real sw: width
    // The layout's frame and its pane (Main.qml): the column's icons as a
    // view hands them over, and where a view's rows go.
    property var frameIcons: []
    property int frameIndex: 0
    property bool frameActive: false
    property bool framed: false
    readonly property real paneX: framed ? sw * 0.209375 : sw * 0.115625
    readonly property real paneWidth: framed ? sw * 0.675 : sw * 0.76875
    function setFrame(icons, index, active, tall) { frameIcons = icons; frameIndex = index; frameActive = active }
    property real sh: height
    property real px: 2
    property string globalFont: "monospace"
    property color primaryColor: "white"
    property color surfaceColor: "black"
    property var skin: ({})
    property bool hintBar: true
    property bool helpLine: true
    property var statusIcons: []
    readonly property real hintRoom: 0
    readonly property real helpRoom: 0
    readonly property real helpLineMargin: sh * 0.1583333
    property rect contentBox: Qt.rect(sw * 0.115625, sh * 0.11875, sw * 0.76875, sh * 0.7770833)
    property rect osdWindow: contentBox
    property string osdBackground: "Full"
    property string osdFrame: "On"
    property var backgroundEffect: ({})
    property string backgroundShader: ""
    property bool effectsRest: true
    property real fxTime: 0
    property var selector: null
    property var hints: ({ back: "[ESC]", navigate: "[▲▼]", arrows: "[▲▼◄►]", change: "[◄►]",
                           browse: "[►]", select: "[ENTER]", play_pause: "[SPACE]" })
    function selectorShown(item, shown) {}
    function coverShown(item, shown) {}
    function skinIcon(url) { return url }

    readonly property string padKey: "pad-0300aaaa"
    readonly property string jammaKey: "pad-0600bbbb"
    readonly property string jamma2Key: "pad-0700dddd"
    readonly property string keyboardKey: "kbd-0003-046d-c31c"
    readonly property string remoteKey: "kbd-0003-1d57-ad02"
    readonly property string awayKey: "pad-0500cccc"
    readonly property string allKey: "kbd-all"

    // InputManager as the pages use it: its controllers, their pages, and a
    // note of every call.
    QtObject {
        id: inputManager
        property var controllers: []
        property string activeController: ""
        readonly property string allKeys: root.allKey
        property var calls: []
        property string capturing: ""
        property bool watchingPins: false
        property var pages: ({})
        property var players: []
        // What a press, a choice or a removal makes of an input
        // ("<controller> <input>"), as InputManager decides it; any other is
        // set.
        property var outcomes: ({})
        // What an input is ("<controller> <input>": { action, game }).
        property var roles: ({})
        signal controllerInputDown(string key, string input, string label)
        signal controllerInputCaptured(string key, string input, string label)
        signal captureHeld(string key)
        signal jammaPinChanged(int player, string address, string pin, bool pressed, string input)
        signal jammaPinsFailed(string reason)
        // A page as InputManager's QVariantMaps reach QML: a copy on each
        // call. A var property given the same object again emits no change
        // signal from Qt 6.10 on, and a page's bindings would keep what its
        // contents were.
        function copyOf(map) { return JSON.parse(JSON.stringify(map || {})) }
        function note(call) { calls = calls.concat([call]) }
        function last() { return calls[calls.length - 1] }
        function controllerPage(key) { return copyOf(pages[key]) }
        function captureController(key) { capturing = key }
        function captureControllers(keys) { capturing = keys.join(",") }
        function watchJammaPins(on) { watchingPins = on }
        function bindControllerInput(key, input, action) {
            note(["bind", key, input, action])
            return outcomes[key + " " + input] || { outcome: "set", action: action, label: "" }
        }
        function replaceControllerInput(key, input, action) {
            note(["replace", key, input, action])
            return outcomes[key + " " + input] || { outcome: "set", action: action, label: "", cleared: [] }
        }
        function removeControllerInput(key, input) {
            note(["remove", key, input])
            return outcomes[key + " " + input] || { outcome: "set", action: "none", label: "" }
        }
        function keyCapture(key, id, action) {
            return outcomes[key + " " + id] || { outcome: "set", action: "none", label: "none" }
        }
        function inputRoles(key, input) { return roles[key + " " + input] || { action: "", game: "" } }
        function setControllerLabels(key, style) {
            note(["labels", key, style])
            var p = pages
            p[key].labels = style
            pages = p
            controllersChanged()
        }
        function resetController(key) { note(["reset", key]) }
        function setPadPlayer(key, player) {
            note(["padPlayer", key, player])
            return outcomes["player " + player] || { outcome: "set", other: "", otherPlayer: 0 }
        }
        function resetPlayers() {
            note(["resetPlayers"])
            players = root.withAuto([])
        }
        // A pad's Game Buttons: its page, a layout chosen (taken at once),
        // and what a press or a choice makes of one of them, as InputManager
        // decides it ("<input> <token>"); any other is set.
        property var arcadePages: ({})
        property var arcadeOutcomes: ({})
        function arcadePage(key) { return copyOf(arcadePages[key]) }
        function setArcadeLayout(key, layout) {
            note(["arcadeLayout", key, layout])
            var p = arcadePages
            p[key].layout = layout
            p[key].layoutTitle = p[key].layouts.filter(function(l) { return l.layout === layout })[0].title
            arcadePages = p
            controllersChanged()
        }
        function setArcadeInput(key, input, token) {
            note(["arcadeInput", key, input, token])
            controllersChanged()
            var labels = { x: "X", a: "A", leftstick: "LS", rightstick: "RS", "": "None" }
            return arcadeOutcomes[input + " " + token]
                   || { outcome: "set", label: labels[token] !== undefined ? labels[token] : token, other: "" }
        }
        function setArcadeInputs(key, tokens) {
            note(["arcadeInputs", key, tokens])
            return "set"
        }
        // Cabinet Lock's unlock buttons, and the wait for new ones.
        property string unlockButtons: ""
        signal unlockButtonsHeld(string names, bool tooMany)
        signal unlockCaptureEnded(bool saved)
        function captureUnlockButtons(on) {
            note(["captureUnlock", on])
            if (!on)
                unlockCaptureEnded(false)
        }
    }

    // The screen saver's idle tracker: the activity a wait notes.
    QtObject {
        id: idleTracker
        property int resets: 0
        function resetActivity() { resets++ }
    }

    // AppCore's settings, for Cabinet Lock and a JAMMA stick's Pin Names.
    QtObject {
        id: appCore
        property var settings: ({})
        property var modules: [{ id: "com.osdos.arcade", name: "Arcade", enabled: true }]
        function get_installed_modules() { return modules }
        function get_setting(module, key) { return settings[key] }
        function save_setting(module, key, value) {
            var s = settings
            s[key] = value
            settings = s
            return true
        }
    }

    // InputManager's six players: these, then Auto with no pad.
    function withAuto(list) {
        var all = list.slice()
        for (var n = list.length + 1; n <= 6; n++)
            all.push({ player: n, key: "", name: "", playing: "", connected: false })
        return all
    }

    // A pad's Game Buttons page: its layout, the ten lines' inputs and what
    // they show, the inputs it has.
    function arcadePage(layout, tokens, values, present) {
        var inputs = ["1", "2", "3", "4", "5", "6", "7", "8", "coin", "start"]
        var labels = ["Button 1", "Button 2", "Button 3", "Button 4", "Button 5", "Button 6", "Button 7", "Button 8",
                      "Coin", "Start"]
        var rows = []
        for (var i = 0; i < inputs.length; i++)
            rows.push({ input: inputs[i], label: labels[i], token: tokens[i], value: values[i] })
        var choices = [["a", "A"], ["b", "B"], ["x", "X"], ["y", "Y"], ["leftshoulder", "LB"], ["rightshoulder", "RB"],
                       ["lefttrigger+", "LT"], ["righttrigger+", "RT"], ["back", "BACK"], ["start", "START"],
                       ["leftstick", "LS"], ["rightstick", "RS"]]
        var titles = { "default": "Engine's", fighting6: "Fighting 6", neogeo4: "Neo Geo 4", custom: "Custom" }
        return { key: root.padKey, name: "8BitDo Pro 2", present: present !== false, jamma: 0, layout: layout,
                 layoutTitle: titles[layout],
                 layouts: [{ layout: "default", title: "Engine's" }, { layout: "fighting6", title: "Fighting 6" },
                           { layout: "neogeo4", title: "Neo Geo 4" }, { layout: "custom", title: "Custom" }],
                 rows: rows, choices: choices.map(function(c) { return { token: c[0], label: c[1] } }) }
    }
    readonly property var padInputs: ["a", "b", "x", "y", "leftshoulder", "rightshoulder", "lefttrigger+", "righttrigger+",
                                      "back", "start"]

    // A page's seven rows: each action's value, and its inputs by name
    // ({ action: [[input, label], …] }), the first of which is its first.
    function rows(values, inputs) {
        var actions = ["up", "down", "left", "right", "select", "back", "play_pause"]
        var labels = ["Up", "Down", "Left", "Right", "Select / OK", "Back", "Play / Pause"]
        var list = []
        for (var i = 0; i < actions.length; i++) {
            var own = ((inputs || {})[actions[i]] || []).map(function(p) { return { input: p[0], label: p[1] } })
            list.push({ action: actions[i], label: labels[i], value: values[i],
                        first: values[i].split(/, | \+ /)[0], inputs: own })
        }
        return list
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }
    property var navigations: []
    property int backs: 0
    Connections {
        target: loader.item
        ignoreUnknownSignals: true
        function onNavigateTo(path, params, listState) {
            root.navigations = root.navigations.concat([{ path: path, params: params, listState: listState }])
        }
        function onGoBack() { root.backs++ }
    }

    // The MenuRow drawn for a page's line at index: its list's delegate.
    function menuRow(item, index) {
        if (item.itemAtIndex !== undefined) {
            var d = item.itemAtIndex(index)
            return d && d.opens !== undefined ? d : null
        }
        for (var i = 0; i < item.children.length; i++) {
            var found = menuRow(item.children[i], index)
            if (found)
                return found
        }
        return null
    }

    TestCase {
        name: "ControlsPages"
        when: windowShown

        function init() {
            inputManager.controllers = [
                { key: root.padKey, name: "8BitDo Pro 2", kind: "gamepad", count: 2, present: true, players: [2, 3] },
                { key: root.jammaKey, name: "OSD/OS JAMMA P1", kind: "arcade", count: 1, present: true, jamma: 1, players: [1] },
                { key: root.keyboardKey, name: "Logitech K400", kind: "keyboard", count: 1, present: true, players: [] },
                { key: root.remoteKey, name: "G20S PRO", kind: "remote", count: 1, present: true, players: [] },
                { key: root.awayKey, name: "DualSense", kind: "gamepad", count: 0, present: false, players: [] }
            ]
            var padRows = root.rows(["D-PAD ▲, L STICK ▲", "D-PAD ▼, L STICK ▼", "LB, D-PAD ◄, L STICK ◄",
                                     "RB, D-PAD ►, L STICK ►", "A", "B, BACK", "START"],
                                    { select: [["a", "A"]], back: [["b", "B"], ["back", "BACK"]],
                                      play_pause: [["start", "START"]] })
            var keys = ["Arrow key", "Arrow key", "Arrow key", "Arrow key"]
            inputManager.pages = {
                "pad-0300aaaa": { key: root.padKey, name: "8BitDo Pro 2", kind: "gamepad", present: true, count: 2,
                                  labels: "auto", labelsInUse: "A below", arcade: "Fighting 6", players: [2, 3],
                                  player: 2, rows: padRows },
                "kbd-0003-046d-c31c": { key: root.keyboardKey, name: "Logitech K400", kind: "keyboard", present: true,
                                        labels: "auto", labelsInUse: "", players: [], player: 0,
                                        rows: root.rows(keys.concat(["Enter + O", "Esc", "Space"]),
                                                        { select: [["79", "O"]] }) },
                "kbd-all": { key: root.allKey, name: "All Keyboards & Remotes", kind: "keys", present: true,
                             labels: "auto", labelsInUse: "", players: [], player: 0,
                             rows: root.rows(keys.concat(["Enter", "Esc", "Space"]), {}) },
                "pad-0500cccc": { key: root.awayKey, name: "DualSense", kind: "gamepad", present: false, count: 0,
                                  labels: "shapes", labelsInUse: "Shapes", arcade: "Engine's", players: [], player: 0,
                                  rows: root.rows(["D-PAD ▲", "D-PAD ▼", "D-PAD ◄", "D-PAD ►", "X", "O", "START"],
                                                  { select: [["a", "X"]], back: [["b", "O"]] }) },
                "pad-0600bbbb": { key: root.jammaKey, name: "OSD/OS JAMMA P1", kind: "arcade", present: true, count: 1,
                                  labels: "auto", labelsInUse: "Numbers", arcade: "Engine's", jamma: 1, players: [1],
                                  player: 0,
                                  rows: root.rows(["JOYSTICK ▲", "JOYSTICK ▼", "JOYSTICK ◄", "JOYSTICK ►", "1", "2, COIN",
                                                   "START"], { select: [["a", "1"]] }) }
            }
            inputManager.players = root.withAuto([
                { player: 1, key: "", name: "OSD/OS JAMMA P1", playing: root.jammaKey, connected: true },
                { player: 2, key: root.padKey, name: "8BitDo Pro 2", playing: root.padKey, connected: true },
                { player: 3, key: "", name: "8BitDo Pro 2", playing: root.padKey, connected: true }
            ])
            inputManager.activeController = ""
            inputManager.calls = []
            inputManager.capturing = ""
            inputManager.watchingPins = false
            idleTracker.resets = 0
            inputManager.outcomes = {}
            inputManager.roles = {}
            inputManager.arcadePages = {
                "pad-0300aaaa": root.arcadePage("fighting6",
                    ["x", "y", "rightshoulder", "a", "b", "righttrigger+", "leftshoulder", "lefttrigger+", "back", "start"],
                    ["X", "Y", "RB", "A", "B", "RT", "LB", "LT", "BACK", "START"])
            }
            inputManager.arcadeOutcomes = {}
            appCore.settings = {}
            root.navigations = []
            root.backs = 0
        }

        function cleanup() {
            loader.source = ""
        }

        function open(view, params) {
            loader.setSource(Qt.resolvedUrl("../../views/" + view), { navParams: params || {} })
            verify(loader.item !== null, "loaded " + view)
            // As Main.qml gives it: the view loaded takes the keys.
            loader.item.forceActiveFocus()
            return loader.item
        }

        // The text item shown with this in it (as written: the screen has it
        // in capitals).
        function findText(item, text) {
            if (item.text !== undefined && item.visible && String(item.text).indexOf(text) >= 0)
                return item
            for (var i = 0; i < item.children.length; i++) {
                var found = findText(item.children[i], text)
                if (found)
                    return found
            }
            return null
        }
        function types(page) { return page.rows.map(function(r) { return r.type }) }
        function labels(choices) { return choices.map(function(c) { return c.label }) }
        function nav() { return root.navigations[root.navigations.length - 1] }
        function keys(key, times) {
            for (var i = 0; i < times; i++)
                keyClick(key)
        }

        // Controls: the gamepads first, the one used last on top with the
        // cursor and the ● on it, each with its player; then every player
        // back to Auto while one was chosen by hand, Keyboards & Remotes and
        // Not Connected. Every line but that one opens a page, with select
        // or ►, and nothing changes by ◄ ►.
        function test_list() {
            inputManager.activeController = root.jammaKey
            var page = open("Controllers.qml")
            compare(types(page), ["controller", "controller", "players", "list", "list"])
            compare(page.rows.map(function(r) { return r.label }),
                    ["OSD/OS JAMMA P1", "8BitDo Pro 2 x2", "All Players Automatic", "Keyboards & Remotes", "Not Connected"])
            compare(page.rows[0].value, "Player 1")
            compare(page.rows[1].value, "Players 2+3")
            compare(page.rows[4].value, "1")
            compare(menuRow(page, 0).icons, ["rec"])
            compare(menuRow(page, 1).icons, [])
            verify(menuRow(page, 0).opens && menuRow(page, 3).opens && !menuRow(page, 2).opens)
            verify(findText(page, ":OPEN") !== null)
            // Another pad pressed: the ● moves, the lines stay where they are.
            inputManager.activeController = root.padKey
            compare(page.rows[0].key, root.jammaKey)
            verify(page.rows[1].last && !page.rows[0].last)

            keyClick(Qt.Key_Left)
            compare(inputManager.calls.length, 0)
            keyClick(Qt.Key_Return)
            compare(nav().path, "views/ControllerSettings.qml")
            compare(nav().params.key, root.jammaKey)
            compare(nav().listState.at, root.jammaKey)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(nav().params.key, root.padKey)
            // Every player back to Auto: done at once, said, and its line goes.
            keyClick(Qt.Key_Down)
            verify(findText(page, ":SET") !== null)
            keyClick(Qt.Key_Right)
            compare(inputManager.calls.length, 0)
            keyClick(Qt.Key_Return)
            compare(inputManager.last(), ["resetPlayers"])
            compare(types(page), ["controller", "controller", "list", "list"])
            verify(findText(page, "Every player is automatic now") !== null)
            // The two lists.
            compare(page.rows[2].label, "Keyboards & Remotes")
            keyClick(Qt.Key_Return)
            compare(nav().path, "views/Controllers.qml")
            compare(nav().params.show, "keyboards")
            compare(nav().listState.at, "list:keyboards")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(nav().params.show, "away")
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)

            // Back from a line's page: on that line.
            loader.source = ""
            page = open("Controllers.qml", { navListState: { currentIndex: 0, at: "list:away" } })
            keyClick(Qt.Key_Return)
            compare(nav().params.show, "away")
        }

        // No gamepad: two lines say so and what to do, where they can be
        // read, and the cursor passes over them. No keyboard listed apart
        // (a Mac, Windows): Keyboards & Remotes opens every keyboard's keys.
        function test_noGamepad() {
            inputManager.controllers = []
            inputManager.players = root.withAuto([])
            var page = open("Controllers.qml")
            compare(types(page), ["note", "note", "list"])
            verify(findText(page, "No gamepad connected") !== null)
            verify(findText(page, "Plug one in or pair it in Network") !== null)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(nav().path, "views/MenuButtons.qml")
            compare(nav().params.key, root.allKey)
        }

        // Keyboards & Remotes: all of them at once, then each one connected;
        // Not Connected: the controllers away. A pad opens its page, a
        // keyboard or a remote its keys.
        function test_lists() {
            var page = open("Controllers.qml", { show: "keyboards" })
            compare(page.rows.map(function(r) { return r.label }),
                    ["All Keyboards & Remotes", "Logitech K400", "G20S PRO"])
            compare(page.rows[2].value, "Remote")
            verify(findText(page, "Keyboards & Remotes") !== null)
            keyClick(Qt.Key_Return)
            compare(nav().path, "views/MenuButtons.qml")
            compare(nav().params.key, root.allKey)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(nav().params.key, root.keyboardKey)

            loader.source = ""
            page = open("Controllers.qml", { show: "away" })
            compare(page.rows.map(function(r) { return r.label }), ["DualSense"])
            keyClick(Qt.Key_Return)
            compare(nav().path, "views/ControllerSettings.qml")
            compare(nav().params.key, root.awayKey)
            // The last of them gone (reset from its page): said, and back works.
            inputManager.controllers = inputManager.controllers.filter(function(c) { return c.present })
            compare(types(page), ["note"])
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // A pad's page: six lines, each opening its page, its list or its
        // question; the player from a list that says who has each number;
        // the button names by ◄ ►; Reset asked first, the cursor on Cancel.
        function test_padPage() {
            var page = open("ControllerSettings.qml", { key: root.padKey })
            compare(types(page), ["test", "menu", "game", "player", "labels", "reset"])
            compare(page.rows.map(function(r) { return r.value || "" }),
                    ["", "A Select, B Back", "Fighting 6", "2", "Auto (A below)", ""])
            verify(findText(page, ":TEST") !== null)
            keyClick(Qt.Key_Return)
            compare(nav().path, "views/ControllerTest.qml")
            compare(nav().params.key, root.padKey)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(nav().path, "views/MenuButtons.qml")
            compare(nav().listState.currentIndex, 1)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(nav().path, "views/ArcadeButtons.qml")

            // PLAYER: Auto and the six, each with who has it; back changes nothing.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Left)
            compare(inputManager.calls.length, 0)
            keyClick(Qt.Key_Return)
            var chooser = findChild(page, "playerChooser")
            verify(chooser.visible)
            compare(labels(chooser.choices), ["Auto", "Player 1: OSD/OS JAMMA P1", "Player 2: this pad",
                                              "Player 3: this pad", "Player 4", "Player 5", "Player 6"])
            compare(chooser.choiceIndex, 2)
            keyClick(Qt.Key_Escape)
            verify(!chooser.visible)
            compare(inputManager.calls.length, 0)
            // Player 1, which the JAMMA stick has: said where that one went.
            inputManager.outcomes = { "player 1": { outcome: "set", other: "OSD/OS JAMMA P1", otherPlayer: 3 } }
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(inputManager.last(), ["padPlayer", root.padKey, 1])
            verify(findText(page, "It is player 1 now, OSD/OS JAMMA P1 player 3") !== null)
            keyClick(Qt.Key_Return)
            keys(Qt.Key_Up, 2)
            keyClick(Qt.Key_Return)
            compare(inputManager.last(), ["padPlayer", root.padKey, 0])
            verify(findText(page, "Its player is automatic now") !== null)

            // BUTTON NAMES.
            keyClick(Qt.Key_Down)
            verify(findText(page, ":CHANGE") !== null)
            keyClick(Qt.Key_Right)
            compare(inputManager.last(), ["labels", root.padKey, "a_below"])
            compare(page.rows[4].value, "A below")
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            compare(inputManager.last(), ["labels", root.padKey, "numbers"])

            // RESET: asked, the cursor on Cancel, with what goes.
            keyClick(Qt.Key_Down)
            verify(findText(page, ":RESET") !== null)
            var calls = inputManager.calls.length
            keyClick(Qt.Key_Right)
            var confirm = findChild(page, "resetConfirm")
            verify(!confirm.visible)
            keyClick(Qt.Key_Return)
            verify(confirm.visible)
            verify(findText(page, "Reset 8BitDo Pro 2?") !== null)
            verify(findText(page, "its Game Buttons") !== null)
            compare(labels(confirm.choices), ["Cancel", "Reset"])
            keyClick(Qt.Key_Return)
            verify(!confirm.visible)
            compare(inputManager.calls.length, calls)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(inputManager.last(), ["reset", root.padKey])
            verify(findText(page, "Back to the defaults") !== null)
            compare(root.backs, 0)
        }

        // A pad away has Not Connected for its title, and says so for its test; reset,
        // it has nothing left and its page goes. One of the JAMMA board's
        // sticks has a line for the names its test gives the board's pins.
        function test_padAwayAndJamma() {
            var page = open("ControllerSettings.qml", { key: root.awayKey })
            verify(findText(page, "Not Connected") !== null && findText(page, "DualSense") !== null)
            compare(page.rows[3].value, "Auto")
            compare(page.rows[4].value, "Shapes")
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 0)
            verify(findText(page, "Connect it first") !== null)
            keys(Qt.Key_Up, 1)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(inputManager.last(), ["reset", root.awayKey])
            compare(root.backs, 1)

            loader.source = ""
            appCore.settings = { jamma_pin_layout: "Crown, CHAMMA" }
            page = open("ControllerSettings.qml", { key: root.jammaKey })
            compare(types(page), ["test", "menu", "game", "player", "labels", "pins", "reset"])
            compare(page.rows[3].value, "Auto (1)")
            compare(page.rows[5].value, "Crown, CHAMMA")
            keys(Qt.Key_Down, 5)
            keyClick(Qt.Key_Right)
            compare(appCore.settings.jamma_pin_layout, "JammaPi")
            keys(Qt.Key_Up, 5)
            keyClick(Qt.Key_Return)
            compare(nav().path, "views/ControllerTest.qml")
            compare(nav().params.pinLayout, 2)
        }

        // A pad's Menu Buttons: an action asks what to do in a list; Replace
        // and Add another wait for a press, which a tap answers (what came
        // of it said in the help line) and a hold, the seconds running out
        // or another controller's Back stop, saying that nothing changed;
        // Remove one is a list of the action's buttons.
        function test_menuButtons() {
            var page = open("MenuButtons.qml", { key: root.padKey })
            compare(types(page), ["section", "action", "action", "action", "action", "action", "action", "action"])
            verify(findText(page, "Menu Buttons") !== null)
            verify(menuRow(page, 1).opens)
            var chooser = findChild(page, "actionChooser")
            var prompt = findChild(page, "capturePrompt")
            keys(Qt.Key_Down, 5)   // Back
            keyClick(Qt.Key_Left)
            verify(!chooser.visible)
            keyClick(Qt.Key_Return)
            verify(chooser.visible)
            compare(labels(chooser.choices), ["Replace with a new button", "Add another button", "Remove one", "Cancel"])
            verify(findText(page, "Now: B, BACK") !== null)
            // Back from the list: nothing asked, nothing changed.
            keyClick(Qt.Key_Escape)
            verify(!chooser.visible && !page.capturing)

            // Replace: the rule and the seconds are in the body.
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            verify(page.capturing && prompt.visible)
            compare(inputManager.capturing, root.padKey)
            verify(findText(page, "Press the new button on 8BitDo Pro 2 for [Back]") !== null)
            verify(findText(page, "Hold any button to stop\nStops by itself in 10") !== null)
            // A press only shown, another pad's tap: still waiting. Its own
            // tap: the answer, here a swap.
            inputManager.outcomes = { "pad-0300aaaa a": { outcome: "swapped", action: "back", label: "Back", other: "B",
                                                         otherAction: "Select / OK", cleared: ["BACK"] } }
            inputManager.controllerInputDown(root.padKey, "a", "A")
            inputManager.controllerInputCaptured(root.jammaKey, "a", "1")
            verify(page.capturing)
            inputManager.controllerInputCaptured(root.padKey, "a", "A")
            verify(!page.capturing && !prompt.visible)
            compare(inputManager.capturing, "")
            compare(inputManager.last(), ["replace", root.padKey, "a", "back"])
            verify(findText(page, "A is Back, B Select / OK, BACK nothing") !== null)
            // The cursor moved: the line's own help again.
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Down)
            verify(findText(page, "A is Back") === null)

            // Add another.
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(findText(page, "Press the button on 8BitDo Pro 2 for [Back]") !== null)
            inputManager.controllerInputCaptured(root.padKey, "x", "X")
            compare(inputManager.last(), ["bind", root.padKey, "x", "back"])
            verify(findText(page, "X is Back too") !== null)
            // Its only Select / OK stays.
            inputManager.outcomes = { "pad-0300aaaa a": { outcome: "only", action: "select", label: "Select / OK" } }
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.padKey, "a", "A")
            verify(findText(page, "A is its only Select / OK button: it stays") !== null)

            // The ways out: held, the seconds out, another controller's Back.
            var calls = inputManager.calls.length
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            inputManager.captureHeld(root.jammaKey)
            verify(page.capturing)
            inputManager.captureHeld(root.padKey)
            verify(!page.capturing)
            compare(inputManager.capturing, "")
            verify(findText(page, "Nothing changed") !== null)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            prompt.timedOut()
            verify(!page.capturing)
            verify(findText(page, "No press: nothing changed") !== null)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)   // another controller's select: nothing
            verify(page.capturing)
            keyClick(Qt.Key_Escape)
            verify(!page.capturing)
            compare(root.backs, 0)
            compare(inputManager.calls.length, calls)
            // It counts down by itself.
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            prompt.secondsLeft = 1
            tryVerify(function() { return !page.capturing }, 2500)
            verify(idleTracker.resets > 0)

            // Remove one: a list, no press.
            var remover = findChild(page, "removeChooser")
            keyClick(Qt.Key_Return)
            keys(Qt.Key_Down, 2)
            keyClick(Qt.Key_Return)
            verify(remover.visible && !page.capturing)
            compare(labels(remover.choices), ["B", "BACK", "Cancel"])
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(inputManager.last(), ["remove", root.padKey, "back"])
            verify(findText(page, "BACK does nothing now") !== null)
            inputManager.outcomes = { "pad-0300aaaa b": { outcome: "only", action: "back", label: "Back" } }
            keyClick(Qt.Key_Return)
            keys(Qt.Key_Down, 2)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            verify(findText(page, "B is its only Back button: it stays") !== null)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)

            // A pad away: connect it first.
            loader.source = ""
            page = open("MenuButtons.qml", { key: root.awayKey })
            verify(findText(page, "Not Connected") !== null && findText(page, "Menu Buttons") === null)
            keyClick(Qt.Key_Return)
            verify(!findChild(page, "actionChooser").visible)
            verify(findText(page, "Connect it first") !== null)
        }

        // A keyboard's keys, and every keyboard's: the same rows and the
        // same list, for extra keys; Test Keys and Remove All Extra Keys
        // (asked first) on the page.
        function test_keys() {
            var page = open("MenuButtons.qml", { key: root.keyboardKey })
            compare(types(page), ["test", "section", "action", "action", "action", "action", "action", "action", "action",
                                  "reset"])
            compare(page.rows[6].value, "Enter + O")
            keyClick(Qt.Key_Return)
            compare(nav().path, "views/ControllerTest.qml")
            compare(nav().params.key, root.keyboardKey)
            var chooser = findChild(page, "actionChooser")
            var prompt = findChild(page, "capturePrompt")
            // No extra key yet: one to add.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(labels(chooser.choices), ["Add another key", "Cancel"])
            keyClick(Qt.Key_Escape)
            // One there: add, replace or remove.
            keys(Qt.Key_Down, 4)
            keyClick(Qt.Key_Return)
            compare(labels(chooser.choices), ["Add another key", "Replace with a new key", "Remove one", "Cancel"])
            verify(findText(page, "Now: Enter + O") !== null)
            keyClick(Qt.Key_Return)
            verify(page.capturing && prompt.visible)
            verify(findText(page, "Press the key on Logitech K400 for [Select / OK]") !== null)
            verify(findText(page, "Hold any key to stop") !== null)
            inputManager.controllerInputCaptured(root.keyboardKey, "80", "P")
            compare(inputManager.last(), ["bind", root.keyboardKey, "80", "select"])
            verify(findText(page, "P is Select / OK too") !== null)
            // Replace: the one it had goes.
            inputManager.outcomes = { "kbd-0003-046d-c31c 80": { outcome: "set", action: "select", label: "Select / OK",
                                                               cleared: ["O"] } }
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.keyboardKey, "80", "P")
            compare(inputManager.last(), ["replace", root.keyboardKey, "80", "select"])
            verify(findText(page, "P is Select / OK, O no extra key") !== null)
            // The menus' own key, the Select / OK in use, its Back.
            var said = function(outcome, text) {
                inputManager.outcomes = { "kbd-0003-046d-c31c 80": outcome }
                keyClick(Qt.Key_Return)
                keyClick(Qt.Key_Return)
                inputManager.controllerInputCaptured(root.keyboardKey, "80", "P")
                verify(findText(page, text) !== null, text)
            }
            said({ outcome: "already", action: "up", label: "Up" }, "P is Up already")
            said({ outcome: "only", action: "up", label: "Up" }, "P is the Up in use: it stays")
            said({ outcome: "cancel", action: "back", label: "Back" }, "Nothing changed")
            // Remove one.
            inputManager.outcomes = {}
            keyClick(Qt.Key_Return)
            keys(Qt.Key_Down, 2)
            keyClick(Qt.Key_Return)
            compare(labels(findChild(page, "removeChooser").choices), ["O", "Cancel"])
            keyClick(Qt.Key_Return)
            compare(inputManager.last(), ["remove", root.keyboardKey, "79"])
            verify(findText(page, "O is no extra key now") !== null)

            // Remove All Extra Keys: asked, the cursor on Cancel.
            keys(Qt.Key_Down, 3)
            verify(findText(page, ":REMOVE") !== null)
            var calls = inputManager.calls.length
            keyClick(Qt.Key_Return)
            var confirm = findChild(page, "resetConfirm")
            verify(confirm.visible)
            keyClick(Qt.Key_Return)
            compare(inputManager.calls.length, calls)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(inputManager.last(), ["reset", root.keyboardKey])
            verify(findText(page, "No extra keys now") !== null)

            // Every keyboard and remote at once: the same page, under its key.
            loader.source = ""
            page = open("MenuButtons.qml", { key: root.allKey })
            verify(findText(page, "All Keyboards & Remotes") !== null)
            compare(types(page).length, 10)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            compare(inputManager.capturing, root.allKey)
            verify(findText(page, "Press the key for [Up]") !== null)
            // A pad's Back comes as a key: it stops the wait.
            keyClick(Qt.Key_Escape)
            verify(!page.capturing)
            compare(root.backs, 0)
        }

        // A pad's test: it opens waiting; each press at once, a line of
        // what it is called, does in the menus and is in the games, the
        // cabinet's button lit in the grid; held, the seconds out, another
        // controller's Back or the pad unplugged, it is over and the page
        // it came from is back.
        function test_padTest() {
            inputManager.roles = { "pad-0300aaaa a": { action: "Select / OK", game: "Button 4" },
                                   "pad-0300aaaa x": { action: "", game: "Button 1" },
                                   "pad-0300aaaa dpup": { action: "Up", game: "Joystick" } }
            var page = open("ControllerTest.qml", { key: root.padKey })
            var prompt = findChild(page, "testPrompt")
            verify(prompt.visible)
            compare(inputManager.capturing, root.padKey)
            compare(prompt.secondsLeft, 10)
            verify(findText(page, "Press the buttons on 8BitDo Pro 2\nHold any button to stop\nStops by itself in 10") !== null)
            inputManager.controllerInputDown(root.padKey, "a", "A")
            verify(findText(page, "A → Select / OK → Button 4") !== null)
            verify(findChild(page, "gridCell-4").on)
            inputManager.controllerInputDown(root.jammaKey, "b", "2")
            compare(page.presses.length, 1)
            // Each press starts the seconds again; a tap's release is nothing more.
            prompt.secondsLeft = 3
            inputManager.controllerInputDown(root.padKey, "x", "X")
            inputManager.controllerInputCaptured(root.padKey, "x", "X")
            compare(prompt.secondsLeft, 10)
            compare(page.presses.length, 2)
            verify(findText(page, "X → No action → Button 1") !== null)
            verify(findChild(page, "gridCell-1").on && !findChild(page, "gridCell-4").on)
            inputManager.controllerInputDown(root.padKey, "dpup", "D-PAD ▲")
            verify(findText(page, "D-PAD ▲ → Up → Joystick") !== null)
            verify(!findChild(page, "gridCell-1").on)
            compare(root.backs, 0)
            inputManager.captureHeld(root.padKey)
            compare(root.backs, 1)
            compare(inputManager.capturing, "")
            // Over: nothing more of it.
            inputManager.controllerInputDown(root.padKey, "a", "A")
            compare(page.presses.length, 3)

            var ends = [function(p) { findChild(p, "testPrompt").timedOut() },
                        function(p) { keyClick(Qt.Key_Escape) },
                        function(p) {
                            var pages = inputManager.pages
                            pages[root.padKey].present = false
                            inputManager.pages = pages
                            inputManager.controllersChanged()
                        }]
            for (var i = 0; i < ends.length; i++) {
                loader.source = ""
                root.backs = 0
                page = open("ControllerTest.qml", { key: root.padKey })
                compare(inputManager.capturing, root.padKey)
                ends[i](page)
                compare(root.backs, 1)
                compare(inputManager.capturing, "")
            }
        }

        // A JAMMA stick's test takes the board's two sticks and its pins:
        // each press with its pin, the pins the sticks don't show too, the
        // grid named by the layout chosen on the stick's page; thirty
        // seconds; either stick held ends it.
        function test_jammaTest() {
            inputManager.controllers = inputManager.controllers.concat([
                { key: root.jamma2Key, name: "OSD/OS JAMMA P2", kind: "arcade", count: 1, present: true, jamma: 2, players: [] }])
            inputManager.roles = { "pad-0600bbbb y": { action: "", game: "Button 4" } }
            var page = open("ControllerTest.qml", { key: root.jammaKey, pinLayout: 1 })
            var prompt = findChild(page, "testPrompt")
            compare(inputManager.capturing, root.jammaKey + "," + root.jamma2Key)
            verify(inputManager.watchingPins)
            compare(prompt.secondsLeft, 30)
            verify(findText(page, "Pin names: Crown, CHAMMA") !== null)
            verify(findChild(page, "pinGrid").visible && !findChild(page, "testGrid").visible)
            // A pin and its press, the one a moment after the other: one line.
            inputManager.jammaPinChanged(1, "0x20", "B1", true, "y")
            inputManager.controllerInputDown(root.jammaKey, "y", "4")
            compare(page.presses.length, 1)
            verify(findText(page, "P1 B1 → 4 → Button 4") !== null)
            inputManager.jammaPinChanged(1, "0x20", "B1", false, "y")
            // The other stick's, its pin not come yet; a pin the sticks don't show.
            inputManager.controllerInputDown(root.jamma2Key, "a", "1")
            verify(findText(page, "P2 ? → 1") !== null)
            inputManager.jammaPinChanged(1, "0x20", "B6", true, "z")
            verify(findText(page, "P1 B6 → None") !== null)
            compare(Object.keys(page.jammaPins).length, 2)
            // The pins can't be read: said, the sticks' presses still shown.
            inputManager.jammaPinsFailed("no MCP23017 at 0x20")
            verify(findText(page, "The board's pins can't be read: no MCP23017 at 0x20") !== null)
            verify(!findChild(page, "pinGrid").visible)
            inputManager.captureHeld(root.jamma2Key)
            compare(root.backs, 1)
            verify(!inputManager.watchingPins)
        }

        // A keyboard's test: each key with what it does; its own Back ends it.
        function test_keysTest() {
            inputManager.roles = { "kbd-0003-046d-c31c 80": { action: "Select / OK", game: "" } }
            inputManager.outcomes = { "kbd-0003-046d-c31c 16777216": { outcome: "cancel", action: "back", label: "Back" } }
            var page = open("ControllerTest.qml", { key: root.keyboardKey })
            verify(findText(page, "Test Keys") !== null)
            verify(findText(page, "Press the keys on Logitech K400\nHold any key to stop") !== null)
            verify(!findChild(page, "testGrid").visible)
            inputManager.controllerInputDown(root.keyboardKey, "80", "P")
            verify(findText(page, "P → Select / OK") !== null)
            inputManager.controllerInputDown(root.keyboardKey, "81", "Q")
            verify(findText(page, "Q → No action") !== null)
            inputManager.controllerInputDown(root.keyboardKey, "16777216", "Esc")
            compare(root.backs, 1)
            compare(page.presses.length, 2)
        }

        // A pad's Game Buttons: the layout from a list, back leaving it as
        // it was; a line from a list too, a press, an input no line has, or
        // none (never for Coin or Start); nothing by ◄.
        function test_gameButtons() {
            var page = open("ArcadeButtons.qml", { key: root.padKey })
            compare(types(page).slice(0, 4), ["layout", "setall", "section", "input"])
            compare(page.rows.length, 13)
            verify(findText(page, "Game Buttons") !== null)
            // The layout's name heads its buttons.
            compare(page.rows[2].label, "Fighting 6")
            verify(menuRow(page, 0).opens && menuRow(page, 3).opens)
            var layouts = findChild(page, "layoutChooser")
            keyClick(Qt.Key_Left)
            verify(!layouts.visible)
            compare(inputManager.calls.length, 0)
            keyClick(Qt.Key_Return)
            verify(layouts.visible)
            compare(labels(layouts.choices), ["Engine's", "Fighting 6", "Neo Geo 4", "Custom"])
            compare(layouts.choiceIndex, 1)
            keyClick(Qt.Key_Escape)
            compare(inputManager.calls.length, 0)
            keyClick(Qt.Key_Return)
            keys(Qt.Key_Down, 2)
            keyClick(Qt.Key_Return)
            compare(inputManager.last(), ["arcadeLayout", root.padKey, "custom"])
            compare(page.rows[2].label, "Custom")

            // Button 1: the list.
            var inputs = findChild(page, "inputChooser")
            var prompt = findChild(page, "capturePrompt")
            keys(Qt.Key_Down, 2)
            compare(page.rows[list(page).currentIndex].label, "Button 1")
            var calls = inputManager.calls.length
            keyClick(Qt.Key_Left)
            compare(inputManager.calls.length, calls)
            keyClick(Qt.Key_Return)
            verify(inputs.visible)
            compare(labels(inputs.choices), ["Press a button", "LS", "RS", "None", "Cancel"])
            verify(findText(page, "Now: X") !== null)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(inputManager.last(), ["arcadeInput", root.padKey, "1", "leftstick"])
            verify(findText(page, "LS is Button 1 now") !== null)
            keyClick(Qt.Key_Return)
            keys(Qt.Key_Down, 3)
            keyClick(Qt.Key_Return)
            compare(inputManager.last(), ["arcadeInput", root.padKey, "1", ""])
            verify(findText(page, "Button 1 is None now") !== null)
            // A press: the grid shows which one, lit, and what the others are.
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            verify(page.capturing && prompt.visible)
            compare(inputManager.capturing, root.padKey)
            compare(prompt.secondsLeft, 10)
            verify(findText(page, "Press the button on 8BitDo Pro 2 for [Button 1]\nHold any button to stop") !== null)
            verify(findChild(prompt, "gridCell-1").on && !findChild(prompt, "gridCell-2").on)
            compare(findChild(prompt, "gridCell-2").name, "Y")
            inputManager.arcadeOutcomes = { "1 a": { outcome: "swapped", label: "A", other: "Button 4" } }
            inputManager.controllerInputCaptured(root.padKey, "a", "A")
            verify(!page.capturing)
            compare(inputManager.last(), ["arcadeInput", root.padKey, "1", "a"])
            verify(findText(page, "A is Button 1 now, and") !== null)
            // Held: nothing changed.
            calls = inputManager.calls.length
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Return)
            inputManager.captureHeld(root.padKey)
            verify(!page.capturing)
            verify(findText(page, "Nothing changed") !== null)
            compare(inputManager.calls.length, calls)
            // Coin is never none.
            keys(Qt.Key_Up, 4)
            compare(page.rows[list(page).currentIndex].label, "Coin")
            keyClick(Qt.Key_Return)
            verify(labels(inputs.choices).indexOf("None") < 0)
            keyClick(Qt.Key_Escape)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)

            // A pad away: no press to ask for; the rest as before.
            loader.source = ""
            inputManager.arcadePages = { "pad-0300aaaa": root.arcadePage("custom", root.padInputs,
                ["A", "B", "X", "Y", "LB", "RB", "LT", "RT", "BACK", "START"], false) }
            page = open("ArcadeButtons.qml", { key: root.padKey })
            verify(findText(page, "Not Connected") !== null && findText(page, "Game Buttons") === null)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(!page.settingAll)
            verify(findText(page, "Connect it first") !== null)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(labels(findChild(page, "inputChooser").choices), ["LS", "RS", "None", "Cancel"])
        }
        function list(page) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i].currentIndex !== undefined && page.children[i].model !== undefined)
                    return page.children[i]
            }
            return null
        }

        // Set One by One: a press a step, twenty seconds each; the joystick
        // a step back and no button for this one, but for Coin and Start;
        // what can't be one asked again; then all ten, to save, do again or
        // drop. Held or left alone, nothing changed.
        function test_setOneByOne() {
            var page = open("ArcadeButtons.qml", { key: root.padKey })
            var prompt = findChild(page, "capturePrompt")
            var tap = function(token, label) { inputManager.controllerInputCaptured(root.padKey, token, label || token) }
            keyClick(Qt.Key_Down)
            verify(findText(page, ":START") !== null)
            keyClick(Qt.Key_Return)
            verify(page.settingAll && page.capturing && prompt.visible)
            compare(prompt.secondsLeft, 20)
            verify(findText(page, "Set One by One (1/10)") !== null)
            verify(findText(page, "[Button 1]\nJoystick ◄ back\nHold any button to stop") !== null)
            // Button 1 needs one.
            tap("dpright", "D-PAD ►")
            compare(page.step, 0)
            verify(findText(page, "Button 1 needs a button") !== null)
            tap("x", "X")
            compare(page.step, 1)
            verify(findText(page, "Joystick ◄ back, ► no button") !== null)
            verify(findChild(prompt, "gridCell-2").on)
            compare(findChild(prompt, "gridCell-1").name, "X")
            // Taken already, the joystick's up, Guide: asked again.
            tap("x", "X")
            verify(findText(page, "X is Button 1 already") !== null)
            tap("dpup", "D-PAD ▲")
            verify(findText(page, "D-PAD ▲ is the joystick: press a button") !== null)
            tap("guide", "GUIDE")
            verify(findText(page, "GUIDE shows MAME's menu: press another") !== null)
            compare(page.step, 1)
            prompt.secondsLeft = 2
            tap("y", "Y")
            compare(page.step, 2)
            compare(prompt.secondsLeft, 20)
            // A step back: Button 2 again, what it had gone.
            tap("leftx-", "L STICK ◄")
            compare(page.step, 1)
            compare(page.setTokens[1], "")
            tap("y", "Y")
            // No button for 3 to 6.
            for (var i = 0; i < 4; i++)
                tap("dpright", "D-PAD ►")
            compare(page.step, 6)
            verify(findText(page, "[Coin]") !== null)
            tap("leftx+", "L STICK ►")
            compare(page.step, 6)
            verify(findText(page, "Coin needs a button") !== null)
            tap("back", "BACK")
            tap("start", "START")
            tap("dpright", "D-PAD ►")
            compare(inputManager.calls.length, 0)
            tap("dpright", "D-PAD ►")
            // All ten, to look at.
            verify(page.summing && !page.capturing && !prompt.visible)
            compare(inputManager.capturing, "")
            verify(findText(page, "Save these buttons?") !== null)
            var grid = findChild(page, "summaryGrid")
            compare(findChild(grid, "gridCell-1").name, "X")
            compare(findChild(grid, "gridCell-coin").name, "BACK")
            compare(findChild(grid, "gridCell-3").name, "")
            compare(inputManager.calls.length, 0)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            verify(!page.summing)
            compare(inputManager.last(), ["arcadeInputs", root.padKey, ["x", "y", "", "", "", "", "", "", "back", "start"]])
            verify(findText(page, "Saved: its layout is Custom now") !== null)

            // Again from the summary, then dropped there.
            var through = function() {
                keyClick(Qt.Key_Return)
                var presses = ["a", "b", "x", "y", "leftshoulder", "rightshoulder", "back", "start", "lefttrigger+",
                               "righttrigger+"]
                for (var p = 0; p < presses.length; p++)
                    tap(presses[p])
                verify(page.summing)
            }
            var calls = inputManager.calls.length
            through()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(!page.summing && page.settingAll)
            compare(page.step, 0)
            compare(page.setTokens[0], "")
            inputManager.captureHeld(root.padKey)
            verify(!page.settingAll && !page.capturing)
            verify(findText(page, "Nothing changed") !== null)
            through()
            keyClick(Qt.Key_Escape)
            verify(!page.summing)
            verify(findText(page, "Nothing changed") !== null)
            compare(root.backs, 0)
            // Left alone, or another controller's Back.
            keyClick(Qt.Key_Return)
            tap("a")
            prompt.timedOut()
            verify(!page.settingAll)
            verify(findText(page, "No press: nothing changed") !== null)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Escape)
            verify(!page.settingAll)
            compare(inputManager.calls.length, calls)
        }

        // The control panel's drawing: each of the cabinet's inputs on it
        // lights some of its cells, in a place of its own on each player's
        // side, but COIN, the one both have; Button 7 and 8, which a
        // six-button panel hasn't, none.
        function test_arcadePanelArt() {
            var plain = Art.draw([], 1)
            compare(plain.cells.length, Art.width * Art.height)
            var changed = function(lit, player) {
                var art = Art.draw(lit, player), cells = []
                for (var i = 0; i < art.cells.length; i++) {
                    if (art.cells[i] !== plain.cells[i])
                        cells.push(i)
                }
                return cells
            }
            // Nothing lit, both sides alike.
            compare(changed([], 2).length, 0)
            var seen = {}
            var inputs = ["1", "2", "3", "4", "5", "6", "coin", "start"]
            for (var player = 1; player <= 2; player++) {
                for (var n = 0; n < inputs.length; n++) {
                    var cells = changed([inputs[n]], player)
                    verify(cells.length > 20, inputs[n] + " " + player)
                    var place = cells[0] + ":" + cells[cells.length - 1]
                    if (player === 2 && inputs[n] === "coin")
                        verify(seen[place], "coin")
                    else
                        verify(!seen[place], inputs[n] + " " + player)
                    seen[place] = true
                }
            }
            compare(changed(["7"], 1).length, 0)
            compare(changed(["8", ""], 2).length, 0)
            // Ink only where it was drawn: 0 none, 1 ink, 2 ground.
            for (var c = 0; c < plain.cells.length; c++)
                verify(plain.cells[c] <= 2)
        }

        // Settings → Startup → Cabinet Lock (views/CabinetLock.qml): turned on without
        // unlock buttons, they are waited for first, what is held shown as it
        // changes, too many said so; once they are kept, it asks before it
        // locks, and Lock saves it. Without a module to stay on, it says so
        // and stays off. Off is saved at once.
        function test_cabinetLock() {
            appCore.settings = { startup_module: "com.osdos.arcade" }
            var page = open("CabinetLock.qml")
            verify(findText(page, "Cabinet Lock") !== null)
            verify(findText(page, "None") !== null)
            keyClick(Qt.Key_Return)
            compare(inputManager.calls[inputManager.calls.length - 1], ["captureUnlock", true])
            verify(page.capturing)
            verify(findText(page, "Nothing held") !== null)
            // How to cancel, and the seconds left, in the body: they show
            // with Settings' Hint Bar off too.
            verify(findText(page, "for two seconds\nLet go of everything to cancel\nStops by itself in 10") !== null)
            inputManager.unlockButtonsHeld("P1 START", false)
            verify(findText(page, "P1 START") !== null)
            verify(findText(page, "Let go of everything to cancel\nHold them") !== null)
            inputManager.unlockButtonsHeld("P1 START + P2 START + P1 1 + P2 1 + P1 COIN", true)
            verify(findText(page, "At most four") !== null)
            inputManager.unlockButtonsHeld("P1 START + P2 START", false)
            inputManager.unlockButtons = "P1 START + P2 START"
            inputManager.unlockCaptureEnded(true)
            verify(!page.capturing)
            tryVerify(function() { return findText(page, "Lock the cabinet?") !== null })
            verify(findText(page, "Hold P1 START + P2 START together for three seconds") !== null)
            keyClick(Qt.Key_Return)   // Lock
            compare(appCore.settings.cabinet_lock, "On")
            // Off again, at once.
            page.refresh()
            verify(page.lockOn)
            keyClick(Qt.Key_Left)
            compare(appCore.settings.cabinet_lock, "Off")
            verify(!page.lockOn)
            // A wait ended from the page keeps the old ones.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(page.capturing)
            keyClick(Qt.Key_Escape)
            compare(inputManager.calls[inputManager.calls.length - 1], ["captureUnlock", false])
            verify(!page.capturing)
            verify(findText(page, "P1 START + P2 START") !== null)
            // On with no module to stay on any more (Start on Module set to
            // None since): it locks nothing, and its line says so.
            appCore.settings = { cabinet_lock: "On" }
            page.refresh()
            verify(findText(page, "On (No Module)") !== null)
            // No module to stay on: said, and off.
            appCore.settings = {}
            page.refresh()
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            tryVerify(function() { return findText(page, "No Start on Module") !== null })
            compare(appCore.settings.cabinet_lock, undefined)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
            inputManager.unlockButtons = ""
        }
    }
}
