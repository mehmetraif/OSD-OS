import QtQuick
import QtTest
import "../../views/Components/ArcadePanelArt.js" as Art

// Settings → Controls' pages, views/Controllers.qml, ControllerSettings.qml,
// ArcadeButtons.qml and RemapControls.qml, against a stand-in for
// InputManager: the list of controllers (connected, away, the one used last,
// the players' pads chosen from those connected), a controller's page (its
// labels, a press waited for and set or kept, cancelled by another
// controller's Back or by the wait running out, a button cleared, the
// defaults, its buttons tested, the JAMMA board's two sticks together with
// their pins), a pad's Arcade Buttons (the layout, a line set by a press or
// with ◄ ►, all of them in turn, their test, a pad away, the control panel
// drawn beside them), and All Keyboards & Remotes' wait for a press.
Rectangle {
    id: root
    color: surfaceColor
    width: 640
    height: 480
    // Main.qml's, as the views and their components read them.
    property real sw: width
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

    // InputManager as the pages use it: its controllers, their pages, and a
    // note of every call.
    QtObject {
        id: inputManager
        property var controllers: []
        property string activeController: ""
        property var calls: []
        property string capturing: ""
        property bool watchingPins: false
        property var pages: ({})
        property var players: []
        // What a press makes of a key ("<controller> <input>"), as
        // InputManager decides it; any other is set.
        property var outcomes: ({})
        property bool remapCapture: false
        signal controllerInputCaptured(string key, string input, string label)
        signal auxButtonPressed(int extendedKeyId)
        signal jammaPinChanged(int player, string address, string pin, bool pressed, string input)
        signal jammaPinsFailed(string reason)
        // A page as InputManager's QVariantMaps reach QML: a copy on each
        // call. A var property given the same object again emits no change
        // signal from Qt 6.10 on, and a page's bindings would keep what its
        // contents were.
        function copyOf(map) { return JSON.parse(JSON.stringify(map || {})) }
        function controllerPage(key) { return copyOf(pages[key]) }
        function captureController(key) { capturing = key }
        function captureControllers(keys) { capturing = keys.join(",") }
        function watchJammaPins(on) { watchingPins = on }
        function bindControllerInput(key, input, action) {
            calls = calls.concat([["bind", key, input, action]])
            return outcomes[key + " " + input] || { outcome: "set", action: action, label: "" }
        }
        function keyCapture(key, id, action) {
            calls = calls.concat([["keyCapture", key, id, action]])
            return outcomes[key + " " + id] || { outcome: "set", action: "none", label: "none" }
        }
        function setRemapCapture(active) { remapCapture = active }
        function keyDisplayName(id) { return id === Qt.Key_Return ? "Return" : id === Qt.Key_F5 ? "F5" : String(id) }
        function setControllerLabels(key, style) {
            calls = calls.concat([["labels", key, style]])
            var p = pages
            p[key].labels = style
            pages = p
            controllersChanged()
        }
        function resetController(key) { calls = calls.concat([["reset", key]]) }
        // A pad's Arcade Buttons: its page, a layout chosen (taken at once),
        // and what a press or a choice makes of one of them, as InputManager
        // decides it ("<input> <token>"); any other is set.
        property var arcadePages: ({})
        property var arcadeOutcomes: ({})
        function arcadePage(key) { return copyOf(arcadePages[key]) }
        function setArcadeLayout(key, layout) {
            calls = calls.concat([["arcadeLayout", key, layout]])
            var p = arcadePages
            p[key].layout = layout
            p[key].layoutTitle = p[key].layouts.filter(function(l) { return l.layout === layout })[0].title
            arcadePages = p
            controllersChanged()
        }
        function setArcadeInput(key, input, token) {
            calls = calls.concat([["arcadeInput", key, input, token]])
            controllersChanged()
            var labels = { x: "X", a: "A", leftstick: "LS", rightstick: "RS", "": "None" }
            return arcadeOutcomes[input + " " + token]
                   || { outcome: "set", label: labels[token] !== undefined ? labels[token] : token, other: "" }
        }
        function setArcadeInputs(key, tokens) {
            calls = calls.concat([["arcadeInputs", key, tokens]])
            return "set"
        }
        function setPlayer(player, key) {
            calls = calls.concat([["player", player, key]])
            var list = players.slice()
            for (var i = 0; i < list.length; i++) {
                if (list[i].player !== player)
                    continue
                var c = controllers.filter(function(x) { return x.key === key })[0]
                list[i] = { player: player, key: key, name: c ? c.name : "OSD/OS JAMMA P1",
                            playing: c ? key : root.jammaKey, connected: true }
            }
            players = list
        }
    }

    // The screen saver's idle tracker: the activity a button test notes.
    QtObject {
        id: idleTracker
        property int resets: 0
        function resetActivity() { resets++ }
    }

    // AppCore's settings, for All Keyboards & Remotes.
    QtObject {
        id: appCore
        property var settings: ({})
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

    // A pad's Arcade Buttons page: its layout, the ten lines' inputs and what
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
        return { key: root.padKey, name: "8BitDo Pro 2", present: present !== false, jamma: 0, layout: layout,
                 layoutTitle: { "default": "Default", fighting6: "Fighting 6", neogeo4: "Neo Geo 4", custom: "Custom" }[layout],
                 layouts: [{ layout: "default", title: "Default" }, { layout: "fighting6", title: "Fighting 6" },
                           { layout: "neogeo4", title: "Neo Geo 4" }, { layout: "custom", title: "Custom" }],
                 rows: rows, choices: choices.map(function(c) { return { token: c[0], label: c[1] } }) }
    }
    readonly property var padInputs: ["a", "b", "x", "y", "leftshoulder", "rightshoulder", "lefttrigger+", "righttrigger+",
                                      "back", "start"]

    function rows(values) {
        var actions = ["up", "down", "left", "right", "select", "back", "play_pause"]
        var labels = ["Up", "Down", "Left", "Right", "Select / OK", "Back", "Play / Pause"]
        var list = []
        for (var i = 0; i < actions.length; i++)
            list.push({ action: actions[i], label: labels[i], value: values[i] })
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
                { key: root.padKey, name: "8BitDo Pro 2", kind: "gamepad", count: 2, present: true },
                { key: root.jammaKey, name: "OSD/OS JAMMA P1", kind: "arcade", count: 1, present: true, jamma: 1 },
                { key: root.keyboardKey, name: "Logitech K400", kind: "keyboard", count: 1, present: true },
                { key: root.remoteKey, name: "G20S PRO", kind: "remote", count: 1, present: true },
                { key: root.awayKey, name: "DualSense", kind: "gamepad", count: 0, present: false }
            ]
            inputManager.pages = {
                "pad-0300aaaa": { key: root.padKey, name: "8BitDo Pro 2", kind: "gamepad", present: true,
                                  labels: "auto", labelsInUse: "Xbox", arcade: "Fighting 6",
                                  rows: root.rows(["D-PAD ▲, L STICK ▲", "D-PAD ▼, L STICK ▼", "LB, D-PAD ◄, L STICK ◄",
                                                   "RB, D-PAD ►, L STICK ►", "A", "B, BACK", "START"]) },
                "kbd-0003-046d-c31c": { key: root.keyboardKey, name: "Logitech K400", kind: "keyboard", present: true,
                                        labels: "auto", labelsInUse: "",
                                        rows: root.rows(["Default", "Default", "Default", "Default", "Default + O",
                                                         "Default", "Default"]) },
                "pad-0500cccc": { key: root.awayKey, name: "DualSense", kind: "gamepad", present: false,
                                  labels: "playstation", labelsInUse: "PlayStation",
                                  rows: root.rows(["D-PAD ▲", "D-PAD ▼", "D-PAD ◄", "D-PAD ►", "X", "O", "START"]) },
                "pad-0600bbbb": { key: root.jammaKey, name: "OSD/OS JAMMA P1", kind: "arcade", present: true,
                                  labels: "auto", labelsInUse: "Arcade", jamma: 1,
                                  rows: root.rows(["D-PAD ▲, JOYSTICK ▲", "D-PAD ▼, JOYSTICK ▼", "5, D-PAD ◄, JOYSTICK ◄",
                                                   "6, D-PAD ►, JOYSTICK ►", "1", "2, COIN", "START"]) }
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
            inputManager.arcadePages = {
                "pad-0300aaaa": root.arcadePage("default", root.padInputs,
                                                ["Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "BACK", "START"])
            }
            inputManager.arcadeOutcomes = {}
            inputManager.remapCapture = false
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

        function test_list() {
            var page = open("Controllers.qml")
            var labels = page.rows.map(function(r) { return r.label })
            // All six players, whatever is connected: a pad can be chosen
            // for one before it is plugged in.
            compare(labels, ["All Keyboards & Remotes", "Players", "Player 1", "Player 2", "Player 3", "Player 4",
                             "Player 5", "Player 6", "Connected",
                             "8BitDo Pro 2 x2", "OSD/OS JAMMA P1", "Logitech K400", "G20S PRO",
                             "Not Connected", "DualSense"])
            compare(page.rows[5].value, "Auto")
            // The pads that play say so.
            compare(page.rows[9].value, "P2+P3 Gamepad")
            compare(page.rows[10].value, "P1 Arcade")
            compare(page.rows[11].value, "Keyboard")
            compare(page.rows[12].value, "Remote")
            compare(page.rows[14].value, "Gamepad")

            // The one used last is marked (a ● on its line), its value as
            // it is.
            inputManager.activeController = root.keyboardKey
            compare(page.rows[11].value, "Keyboard")
            verify(page.rows[11].last)
            verify(!page.rows[9].last)
            compare(page.rows[9].value, "P2+P3 Gamepad")

            // The cursor skips the headings; select opens the page.
            for (var p = 0; p < 6; p++)
                keyClick(Qt.Key_Down)   // Players 1 to 6
            keyClick(Qt.Key_Down)   // the 8BitDo
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "views/ControllerSettings.qml")
            compare(root.navigations[0].params.key, root.padKey)

            // A controller going: the cursor stays on its own line, and the
            // players' lines stay.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)   // the keyboard
            inputManager.controllers = inputManager.controllers.filter(function(c) { return c.key !== root.jammaKey })
            compare(page.rows.filter(function(r) { return r.type === "player" }).length, 6)
            keyClick(Qt.Key_Return)
            compare(root.navigations[1].params.key, root.keyboardKey)

            // The shared keys' page, and back.
            for (var i = 0; i < 8; i++)
                keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(root.navigations[2].path, "views/RemapControls.qml")
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // The lines that lead to a page end in a ► (the shared keys', a
        // controller's), a player's pad stands between ◄ and ►, and ► opens
        // the pages select does.
        function test_rightOpens() {
            var page = open("Controllers.qml")
            verify(page.opens(page.rows[0]))
            verify(!page.opens(page.rows[2]))
            verify(page.opens(page.rows[9]))
            var shared = root.menuRow(page, 0)
            verify(shared.opens && !shared.changes)
            var player = root.menuRow(page, 2)
            verify(player.changes && !player.opens)
            var heading = root.menuRow(page, 1)
            verify(!heading.opens && !heading.changes)
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "views/RemapControls.qml")
            for (var p = 0; p < 7; p++)
                keyClick(Qt.Key_Down)   // Players 1 to 6, then the 8BitDo
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 2)
            compare(root.navigations[1].path, "views/ControllerSettings.qml")
            compare(root.navigations[1].params.key, root.padKey)
        }

        function test_listBackOnItsLine() {
            loader.setSource(Qt.resolvedUrl("../../views/Controllers.qml"),
                             { navParams: {}, navListState: { currentIndex: 2, key: root.remoteKey } })
            loader.item.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(root.navigations[root.navigations.length - 1].params.key, root.remoteKey)
        }

        function test_nothingConnected() {
            inputManager.controllers = []
            inputManager.players = root.withAuto([])
            var page = open("Controllers.qml")
            compare(page.rows.map(function(r) { return r.type }),
                    ["shared", "section", "player", "player", "player", "player", "player", "player", "section", "note"])
            compare(page.rows[2].value, "Auto")
            for (var i = 0; i < 7; i++)
                keyClick(Qt.Key_Down)   // the six players; the note can't be chosen: back round to the first line
            keyClick(Qt.Key_Return)
            compare(root.navigations[0].path, "views/RemapControls.qml")
        }

        function test_players() {
            var page = open("Controllers.qml")
            compare(page.rows[2].value, "Auto (OSD/OS JAMMA P1)")
            compare(page.rows[3].value, "8BitDo Pro 2")

            // Select: the pads connected to choose from, Auto first, the
            // player's own chosen. No keyboard, remote or pad away.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            var chooser = findChild(page, "playerChooser")
            verify(chooser && chooser.visible)
            compare(chooser.choices.map(function(c) { return c.label }),
                    ["Auto", "8BitDo Pro 2 x2", "OSD/OS JAMMA P1"])
            compare(chooser.choiceIndex, 0)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(!chooser.visible)
            compare(inputManager.calls[inputManager.calls.length - 1], ["player", 1, root.padKey])
            compare(page.rows[2].value, "8BitDo Pro 2")

            // ◄ ► go round the same, Auto among them.
            keyClick(Qt.Key_Down)   // Player 2: the 8BitDo
            keyClick(Qt.Key_Right)
            compare(inputManager.calls[inputManager.calls.length - 1], ["player", 2, root.jammaKey])
            keyClick(Qt.Key_Right)
            compare(inputManager.calls[inputManager.calls.length - 1], ["player", 2, ""])
            keyClick(Qt.Key_Left)
            compare(inputManager.calls[inputManager.calls.length - 1], ["player", 2, root.jammaKey])

            // Back closes the chooser without a change.
            var before = inputManager.calls.length
            keyClick(Qt.Key_Return)
            verify(chooser.visible)
            compare(chooser.choiceIndex, 2)
            keyClick(Qt.Key_Escape)
            verify(!chooser.visible)
            compare(inputManager.calls.length, before)
            compare(root.backs, 0)

            // A pad chosen and away.
            inputManager.players = root.withAuto([
                { player: 1, key: root.awayKey, name: "DualSense", playing: "", connected: false },
                { player: 2, key: "", name: "OSD/OS JAMMA P1", playing: root.jammaKey, connected: true }
            ])
            compare(page.rows[2].value, "DualSense (Away)")
            compare(page.rows[10].value, "P2 Arcade")

            // Players 3 to 6 have no JAMMA stick of their number; one chosen
            // with its pad away says so.
            verify(page.rows[4].description.indexOf("JAMMA") < 0)
            var players = root.withAuto([])
            players[4] = { player: 5, key: root.awayKey, name: "DualSense", playing: "", connected: false }
            inputManager.players = players
            compare(page.rows.filter(function(r) { return r.type === "player" }).length, 6)
            compare(page.rows[6].value, "DualSense (Away)")
        }

        function test_padPage() {
            var page = open("ControllerSettings.qml", { key: root.padKey })
            compare(page.rows.map(function(r) { return r.type }),
                    ["labels", "test", "arcade", "section", "action", "action", "action", "action", "action", "action",
                     "action", "clear", "reset"])
            compare(page.rows[0].value, "Auto (Xbox)")
            compare(page.rows[2].value, "Fighting 6")
            compare(page.rows[8].value, "A")

            // The labels: ◄ ► and select go round them.
            keyClick(Qt.Key_Right)
            compare(inputManager.calls[0], ["labels", root.padKey, "xbox"])
            compare(page.rows[0].value, "Xbox")
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            compare(inputManager.calls[2], ["labels", root.padKey, "arcade"])

            // UP: a press on the pad is waited for, then set.
            keyClick(Qt.Key_Down)   // TEST BUTTONS
            keyClick(Qt.Key_Down)   // ARCADE BUTTONS
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(inputManager.capturing, root.padKey)
            verify(page.capturing)
            // Another controller's input name, or a press of another: not this.
            inputManager.controllerInputCaptured(root.keyboardKey, "79", "O")
            verify(page.capturing)
            inputManager.controllerInputCaptured(root.padKey, "x", "X")
            verify(!page.capturing)
            compare(inputManager.capturing, "")
            compare(inputManager.calls[inputManager.calls.length - 1], ["bind", root.padKey, "x", "up"])
            compare(page.notice, "X is Up now")

            // Another controller's Back cancels; this pad's every button is
            // one to set, so its own B would be set like any other.
            keyClick(Qt.Key_Return)
            verify(page.capturing)
            var before = inputManager.calls.length
            keyClick(Qt.Key_Escape)
            verify(!page.capturing)
            compare(inputManager.capturing, "")
            compare(inputManager.calls.length, before)
            compare(root.backs, 0)
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.padKey, "b", "B")
            compare(inputManager.calls[inputManager.calls.length - 1], ["bind", root.padKey, "b", "up"])

            // Its only Select / OK stays, and so does what does UP already.
            inputManager.outcomes[root.padKey + " a"] = { outcome: "only", action: "select", label: "Select / OK" }
            inputManager.outcomes[root.padKey + " dpup"] = { outcome: "already", action: "up", label: "Up" }
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.padKey, "a", "A")
            verify(!page.capturing)
            compare(page.notice, "A is its only Select / OK button")
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.padKey, "dpup", "D-PAD ▲")
            compare(page.notice, "D-PAD ▲ is Up already")

            // The wait runs out.
            keyClick(Qt.Key_Return)
            verify(page.capturing)
            page.secondsLeft = 1
            tryVerify(function() { return !page.capturing }, 2500)
            compare(inputManager.capturing, "")

            // CLEAR A BUTTON, RESET TO DEFAULTS.
            for (var i = 0; i < 7; i++)
                keyClick(Qt.Key_Down)
            compare(page.rows[page.rows.length - 2].type, "clear")
            keyClick(Qt.Key_Return)
            verify(page.capturing)
            inputManager.controllerInputCaptured(root.padKey, "y", "Y")
            compare(inputManager.calls[inputManager.calls.length - 1], ["bind", root.padKey, "y", "none"])
            compare(page.notice, "Y does nothing now")
            inputManager.outcomes[root.padKey + " y"] = { outcome: "already", action: "none", label: "none" }
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.padKey, "y", "Y")
            compare(page.notice, "Y does nothing already")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(inputManager.calls[inputManager.calls.length - 1], ["reset", root.padKey])

            // Leaving the page while it waits lets the pad act again.
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(inputManager.capturing, root.padKey)
            loader.source = ""
            compare(inputManager.capturing, "")
        }

        function test_keyboardPage() {
            var page = open("ControllerSettings.qml", { key: root.keyboardKey })
            compare(page.rows.map(function(r) { return r.type }),
                    ["test", "section", "action", "action", "action", "action", "action", "action", "action",
                     "clear", "reset"])
            compare(page.rows[6].value, "Default + O")
            compare(page.rows[9].label, "Remove a Key")

            // What does Back on it cancels, as on the shared page; Select /
            // OK's keys and the menus' stay; another key is set.
            var esc = root.keyboardKey + " " + String(Qt.Key_Escape)
            var enter = root.keyboardKey + " " + String(Qt.Key_Return)
            inputManager.outcomes[esc] = { outcome: "cancel", action: "back", label: "Back" }
            inputManager.outcomes[enter] = { outcome: "already", action: "select", label: "Select / OK" }
            keyClick(Qt.Key_Down)   // past TEST KEYS to UP
            keyClick(Qt.Key_Return)
            compare(inputManager.capturing, root.keyboardKey)
            inputManager.controllerInputCaptured(root.keyboardKey, String(Qt.Key_Escape), "Esc")
            verify(!page.capturing)
            compare(inputManager.capturing, "")
            compare(page.notice, "")
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.keyboardKey, String(Qt.Key_Return), "Return")
            verify(!page.capturing)
            compare(page.notice, "Return is Select / OK already")
            for (var i = 0; i < 4; i++)
                keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.keyboardKey, String(Qt.Key_P), "P")
            compare(inputManager.calls[inputManager.calls.length - 1],
                    ["bind", root.keyboardKey, String(Qt.Key_P), "select"])
            compare(page.notice, "P is Select / OK now")

            // REMOVE A KEY: an extra key of its own goes; Select / OK stays;
            // a key that isn't one is said so.
            for (i = 0; i < 3; i++)
                keyClick(Qt.Key_Down)
            compare(page.rows[9].type, "clear")
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.keyboardKey, String(Qt.Key_O), "O")
            compare(page.notice, "O is no extra key now")
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.keyboardKey, String(Qt.Key_Return), "Return")
            compare(page.notice, "Return stays Select / OK")
            inputManager.outcomes[root.keyboardKey + " " + String(Qt.Key_F6)] = { outcome: "other", action: "none", label: "none" }
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.keyboardKey, String(Qt.Key_F6), "F6")
            compare(page.notice, "F6 is not one of its extra keys")

            // A keyboard's wait runs out too, and says so.
            keyClick(Qt.Key_Return)
            verify(page.capturing)
            compare(findChild(page, "capturePrompt").hint, "[ESC]:CANCEL (10)")
            page.secondsLeft = 1
            tryVerify(function() { return !page.capturing }, 2500)
        }

        // All Keyboards & Remotes: a button pressed is the action's, unless
        // it is Back, which cancels, or keeps what it does.
        function test_sharedPage() {
            var page = open("RemapControls.qml")
            inputManager.outcomes[" " + Qt.Key_Escape] = { outcome: "cancel", action: "back", label: "Back" }
            inputManager.outcomes[" " + Qt.Key_Return] = { outcome: "already", action: "select", label: "Select / OK" }
            keyClick(Qt.Key_Return)   // UP
            verify(page.capturing)
            verify(inputManager.remapCapture)
            inputManager.auxButtonPressed(Qt.Key_Return)
            verify(!page.capturing)
            compare(appCore.settings["remote_keymap.up"], undefined)
            compare(page.notice, "Return is Select / OK already")
            keyClick(Qt.Key_Down)
            compare(page.notice, "")
            keyClick(Qt.Key_Return)   // DOWN
            inputManager.auxButtonPressed(Qt.Key_Escape)
            verify(!page.capturing)
            compare(appCore.settings["remote_keymap.down"], undefined)
            keyClick(Qt.Key_Return)
            inputManager.auxButtonPressed(Qt.Key_F5)
            verify(!page.capturing)
            verify(!inputManager.remapCapture)
            compare(appCore.settings["remote_keymap.down"], Qt.Key_F5)
            compare(inputManager.calls[inputManager.calls.length - 1], ["keyCapture", "", Qt.Key_F5, "down"])
        }

        // TEST BUTTONS: each press on the pad shown, the last first, acting on
        // nothing, until 10 seconds go by without one or another
        // controller's Back.
        function test_padTest() {
            var page = open("ControllerSettings.qml", { key: root.padKey })
            compare(page.rows[1].label, "Test Buttons")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(page.testing)
            compare(inputManager.capturing, root.padKey)
            verify(!inputManager.watchingPins)
            var prompt = findChild(page, "testPrompt")
            verify(prompt.visible)
            compare(prompt.title, "Test Buttons")
            compare(prompt.message, "Press the buttons on 8BitDo Pro 2")
            compare(prompt.choices, [])
            inputManager.controllerInputCaptured(root.padKey, "a", "A")
            inputManager.controllerInputCaptured(root.keyboardKey, "79", "O")
            inputManager.controllerInputCaptured(root.padKey, "dpup", "D-PAD ▲")
            compare(prompt.choices, ["D-PAD ▲", "A"])
            compare(prompt.currentIndex, 0)
            compare(inputManager.calls.filter(function(c) { return c[0] === "bind" }), [])
            // Each press keeps it going; the last six are kept, five shown.
            page.secondsLeft = 3
            inputManager.controllerInputCaptured(root.padKey, "b", "B")
            compare(page.secondsLeft, 10)
            compare(prompt.hint, "ENDS IN 10")
            for (var i = 1; i <= 6; i++)
                inputManager.controllerInputCaptured(root.padKey, "x", "X" + i)
            compare(page.presses.length, 6)
            compare(prompt.choices, ["X6", "X5", "X4", "X3", "X2"])

            // Another controller's Back ends it, and so does the wait.
            keyClick(Qt.Key_Escape)
            verify(!page.testing)
            compare(inputManager.capturing, "")
            compare(root.backs, 0)
            keyClick(Qt.Key_Return)
            verify(page.testing)
            compare(page.presses, [])
            page.secondsLeft = 1
            tryVerify(function() { return !page.testing }, 2500)
            compare(inputManager.capturing, "")

            // Leaving the page while it tests lets the pad act again.
            keyClick(Qt.Key_Return)
            compare(inputManager.capturing, root.padKey)
            loader.source = ""
            compare(inputManager.capturing, "")
        }

        // A JAMMA stick's test takes the board's two sticks and their pins:
        // each press with its player and pin, the two halves paired as they
        // come, either first, a pin the sticks don't show as NONE.
        function test_jammaTest() {
            inputManager.controllers = inputManager.controllers.concat([
                { key: root.jamma2Key, name: "OSD/OS JAMMA P2", kind: "arcade", count: 1, present: true, jamma: 2 }])
            var page = open("ControllerSettings.qml", { key: root.jammaKey })
            compare(page.rows[1].type, "test")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(page.testing)
            compare(page.secondsLeft, 30)
            compare(inputManager.capturing, root.jammaKey + "," + root.jamma2Key)
            verify(inputManager.watchingPins)
            var prompt = findChild(page, "testPrompt")
            compare(prompt.message, "Layout: OSD/OS")
            compare(prompt.hint, "ENDS IN 30 N:LAYOUT")
            // A pin pressed on a player's expander, and let go.
            var pin = function(player, name, input) {
                var address = player === 2 ? "0x21" : "0x20"
                inputManager.jammaPinChanged(player, address, name, true, input)
                inputManager.jammaPinChanged(player, address, name, false, input)
            }
            var shown = function() { return page.presses.map(page.pressText) }

            // A pin, then what OSD/OS calls it.
            pin(1, "B1", "tl2")
            compare(prompt.choices, ["P1 B1 → NONE"])
            inputManager.controllerInputCaptured(root.jammaKey, "lefttrigger+", "7")
            compare(prompt.choices, ["P1 B1 → 7"])
            // What it is, then its pin; each player's apart. Two are shown
            // under the grid.
            inputManager.controllerInputCaptured(root.jamma2Key, "guide", "MODE")
            compare(prompt.choices, ["P2 ? → MODE", "P1 B1 → 7"])
            pin(1, "A4", "mode")
            pin(2, "A4", "mode")
            compare(shown(), ["P1 A4 → NONE", "P2 A4 → MODE", "P1 B1 → 7"])
            compare(prompt.choices, ["P1 A4 → NONE", "P2 A4 → MODE"])
            // A moment later, a press is one of its own.
            wait(page.pairMs + 50)
            inputManager.controllerInputCaptured(root.jammaKey, "a", "1")
            compare(prompt.choices[0], "P1 ? → 1")
            pin(1, "B7", "a")
            compare(shown().slice(0, 2), ["P1 B7 → 1", "P1 A4 → NONE"])
            // A controller that isn't one of the board's: not this.
            inputManager.controllerInputCaptured(root.padKey, "a", "A")
            compare(prompt.choices[0], "P1 B7 → 1")
            // A pin the sticks don't show takes no press; two pressed at once
            // are each told by what they are, whatever order they come in.
            wait(page.pairMs + 50)
            pin(1, "A7", "z")
            pin(1, "B1", "tl2")
            inputManager.controllerInputCaptured(root.jammaKey, "lefttrigger+", "7")
            compare(shown().slice(0, 2), ["P1 B1 → 7", "P1 A7 → NONE"])
            wait(page.pairMs + 50)
            pin(1, "B6", "b")
            pin(1, "B7", "a")
            inputManager.controllerInputCaptured(root.jammaKey, "a", "1")
            inputManager.controllerInputCaptured(root.jammaKey, "b", "2")
            compare(shown().slice(0, 2), ["P1 B7 → 1", "P1 B6 → 2"])
            // Each press is activity to the screen saver.
            verify(idleTracker.resets > 0)

            // The grid: OSD/OS's own layout names the pins by their inputs,
            // B7 (a) as button 1, at the edge's 22; A4 (mode), B1 (tl2) and
            // A7 (z) as no control, and a third expander's pin as its own.
            var grid = findChild(page, "pinGrid")
            verify(grid.visible)
            verify(grid.height > 0)
            compare(grid.cellOf(1, "b1").seen, true)
            compare(grid.cellOf(1, "b1").pins, ["B7"])
            compare(grid.cellOf(1, "b4").seen, false)
            compare(grid.cellOf(2, "b1").seen, false)
            inputManager.jammaPinChanged(0, "0x24", "B3", true, "none")
            compare(grid.others, "P1 A4 (mode), P1 A7 (z), P1 B1 (tl2), P2 A4 (mode), 0x24 B3 HELD")
            // Held, then let go: still seen.
            inputManager.jammaPinChanged(1, "0x20", "B7", true, "a")
            verify(grid.cellOf(1, "b1").held)
            inputManager.jammaPinChanged(1, "0x20", "B7", false, "a")
            verify(!grid.cellOf(1, "b1").held)
            verify(grid.cellOf(1, "b1").seen)
            // N: the Crown board's layout, by the pins: B1 is button 4 there,
            // B7 none; the layout's own pins on the controls not seen yet.
            keyClick(Qt.Key_N)
            compare(prompt.message, "Layout: Crown, CHAMMA")
            verify(grid.cellOf(1, "b4").seen)
            compare(grid.cellOf(1, "b1").pins, ["A6"])
            verify(!grid.cellOf(1, "b1").seen)
            compare(grid.cellOf(1, "start").pins, ["A4"])
            verify(grid.cellOf(1, "start").seen)
            compare(grid.cellOf(2, "b6").pins, ["B3"])
            // JammaPi's: B7 is button 1 again, A4 nothing; then round to
            // OSD/OS's.
            keyClick(Qt.Key_N)
            compare(prompt.message, "Layout: JammaPi")
            verify(grid.cellOf(1, "b1").seen)
            verify(!grid.cellOf(1, "start").seen)
            keyClick(Qt.Key_N)
            compare(prompt.message, "Layout: OSD/OS")
            verify(page.testing)

            // The pins can't be read: it says why, and the presses come alone.
            inputManager.jammaPinsFailed("no MCP23017 at 0x20 or 0x21 on /dev/i2c-0: nothing to read")
            compare(prompt.message, "The board's pins can't be read: no MCP23017 at 0x20 or 0x21 on /dev/i2c-0: nothing to read")
            verify(!grid.visible)
            inputManager.controllerInputCaptured(root.jammaKey, "b", "2")
            compare(prompt.choices[0], "P1 2")

            page.secondsLeft = 1
            tryVerify(function() { return !page.testing }, 2500)
            verify(!inputManager.watchingPins)
            compare(inputManager.capturing, "")
        }

        // TEST KEYS: a keyboard's key shown; what does Back on it ends it.
        function test_keyboardTest() {
            var page = open("ControllerSettings.qml", { key: root.keyboardKey })
            compare(page.rows[0].label, "Test Keys")
            inputManager.outcomes[root.keyboardKey + " " + String(Qt.Key_Escape)] = { outcome: "cancel", action: "back", label: "Back" }
            keyClick(Qt.Key_Return)
            verify(page.testing)
            compare(inputManager.capturing, root.keyboardKey)
            var prompt = findChild(page, "testPrompt")
            compare(prompt.title, "Test Keys")
            compare(prompt.hint, "[ESC]:END (10)")
            inputManager.controllerInputCaptured(root.keyboardKey, String(Qt.Key_P), "P")
            compare(prompt.choices, ["P"])
            inputManager.controllerInputCaptured(root.keyboardKey, String(Qt.Key_Escape), "Esc")
            verify(!page.testing)
            compare(inputManager.capturing, "")
            compare(root.backs, 0)
        }

        function test_awayPage() {
            var page = open("ControllerSettings.qml", { key: root.awayKey })
            compare(page.rows[0].value, "PlayStation")
            // Its buttons and their test wait for it to be back; its labels
            // and the defaults don't.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(!page.testing)
            compare(page.notice, "Connect it first")
            keyClick(Qt.Key_Down)   // ARCADE BUTTONS
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(!page.capturing)
            compare(inputManager.capturing, "")
            compare(page.notice, "Connect it first")
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Right)
            compare(inputManager.calls[0], ["labels", root.awayKey, "nintendo"])
            // Its settings forgotten, it has nothing left: back to the list.
            keyClick(Qt.Key_Up)   // round to RESET TO DEFAULTS
            keyClick(Qt.Key_Return)
            compare(inputManager.calls[1], ["reset", root.awayKey])
            compare(root.backs, 1)
            compare(page.name, "DualSense")
        }

        // ARCADE BUTTONS opens the pad's own page, and back from it the
        // cursor is on its line.
        function test_arcadeRow() {
            var page = open("ControllerSettings.qml", { key: root.padKey })
            compare(page.rows[2].label, "Arcade Buttons")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "views/ArcadeButtons.qml")
            compare(root.navigations[0].params.key, root.padKey)
            compare(root.navigations[0].listState.currentIndex, 2)
            loader.source = ""
            page = open("ControllerSettings.qml", { key: root.padKey, navListState: { currentIndex: 2 } })
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 2)
            compare(root.navigations[1].path, "views/ArcadeButtons.qml")
            // Its line ends in a ►, which opens it too; the labels' line, which
            // ◄ ► go round, has its value between ◄ and ►.
            verify(root.menuRow(page, 2).opens)
            verify(root.menuRow(page, 0).changes)
            verify(!root.menuRow(page, 0).opens)
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 3)
            compare(root.navigations[2].path, "views/ArcadeButtons.qml")
        }

        // A pad's Arcade Buttons: its layout, a line set by a press (and
        // what InputManager made of it), or with ◄ ► among the inputs no
        // other line has (Coin and Start never none).
        function test_arcadePage() {
            var page = open("ArcadeButtons.qml", { key: root.padKey })
            compare(page.rows.map(function(r) { return r.type }),
                    ["layout", "setall", "test", "section", "input", "input", "input", "input", "input", "input",
                     "input", "input", "input", "input"])
            compare(page.rows[0].value, "Default")
            compare(page.rows[4].value, "Auto")
            compare(page.rows[12].label, "Coin")
            compare(page.rows[12].value, "BACK")
            // Under Default another pad's buttons are the emulators'.
            verify(page.rows[0].description.indexOf("their own ways") >= 0)

            // The layout: ◄ ► and select go round them.
            keyClick(Qt.Key_Right)
            compare(inputManager.calls[0], ["arcadeLayout", root.padKey, "fighting6"])
            compare(page.rows[0].value, "Fighting 6")
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            compare(inputManager.calls[2], ["arcadeLayout", root.padKey, "custom"])
            keyClick(Qt.Key_Return)
            compare(inputManager.calls[3], ["arcadeLayout", root.padKey, "default"])

            // The control panel beside the lines lights the line's input, on
            // the right while the pad plays as player 2 (or is chosen for
            // player 2 while away), on the left while it plays as another or
            // as no one.
            compare(page.lit, [])
            keyClick(Qt.Key_Down)   // SET ALL BUTTONS
            keyClick(Qt.Key_Down)   // TEST BUTTONS
            keyClick(Qt.Key_Down)   // BUTTON 1, past the heading
            compare(page.lit, ["1"])
            compare(page.player, 2)
            inputManager.players = root.withAuto([
                { player: 1, key: root.padKey, name: "8BitDo Pro 2", playing: root.padKey, connected: true }])
            compare(page.player, 1)
            inputManager.players = root.withAuto([])
            compare(page.player, 1)
            inputManager.players = root.withAuto([
                { player: 1, key: "", name: "OSD/OS JAMMA P1", playing: root.jammaKey, connected: true },
                { player: 2, key: root.padKey, name: "8BitDo Pro 2", playing: "", connected: false }])
            compare(page.player, 2)

            // BUTTON 1: a press on the pad is waited for, then set; the
            // wait's panel lights it.
            compare(page.rows[4].label, "Button 1")
            keyClick(Qt.Key_Return)
            verify(page.capturing)
            compare(inputManager.capturing, root.padKey)
            compare(findChild(page, "capturePanel").lit, ["1"])
            compare(findChild(page, "capturePanel").player, 2)
            var prompt = findChild(page, "capturePrompt")
            compare(prompt.title, "Button 1")
            compare(prompt.message, "Press the button on 8BitDo Pro 2 for [Button 1]")
            compare(prompt.hint, "CANCELS IN 10")
            // Another controller's press is not this; this pad's is.
            inputManager.controllerInputCaptured(root.keyboardKey, "79", "O")
            verify(page.capturing)
            inputManager.controllerInputCaptured(root.padKey, "x", "X")
            verify(!page.capturing)
            compare(inputManager.capturing, "")
            compare(inputManager.calls[inputManager.calls.length - 1], ["arcadeInput", root.padKey, "1", "x"])
            compare(page.notice, "X is Button 1 now")
            // Swapped with the line that had it, kept for Coin, the joystick,
            // Guide, what it is already.
            inputManager.arcadePages[root.padKey].rows[3].value = "X"
            inputManager.arcadeOutcomes["1 a"] = { outcome: "swapped", label: "A", other: "Button 4" }
            inputManager.arcadeOutcomes["1 back"] = { outcome: "kept", label: "BACK", other: "Coin" }
            inputManager.arcadeOutcomes["1 dpup"] = { outcome: "joystick", label: "D-PAD ▲", other: "" }
            inputManager.arcadeOutcomes["1 guide"] = { outcome: "menu", label: "GUIDE", other: "" }
            inputManager.arcadeOutcomes["1 b"] = { outcome: "already", label: "B", other: "" }
            var tries = [["a", "A", "A is Button 1 now, and X is Button 4"],
                         ["back", "BACK", "BACK stays Coin: Coin and Start always have a button"],
                         ["dpup", "D-PAD ▲", "D-PAD ▲ is the joystick, not a button"],
                         ["guide", "GUIDE", "GUIDE shows MAME's menu, not a button"],
                         ["b", "B", "B is Button 1 already"]]
            for (var i = 0; i < tries.length; i++) {
                keyClick(Qt.Key_Return)
                inputManager.controllerInputCaptured(root.padKey, tries[i][0], tries[i][1])
                compare(page.notice, tries[i][2])
            }
            // Another controller's Back cancels; so does the wait.
            keyClick(Qt.Key_Return)
            var before = inputManager.calls.length
            keyClick(Qt.Key_Escape)
            verify(!page.capturing)
            compare(inputManager.calls.length, before)
            compare(root.backs, 0)
            keyClick(Qt.Key_Return)
            page.secondsLeft = 1
            tryVerify(function() { return !page.capturing }, 2500)
            compare(inputManager.capturing, "")

            // ◄ ►: the inputs no other line has, then none.
            keyClick(Qt.Key_Right)
            compare(inputManager.calls[inputManager.calls.length - 1], ["arcadeInput", root.padKey, "1", "leftstick"])
            compare(page.notice, "LS is Button 1 now")
            keyClick(Qt.Key_Left)
            compare(inputManager.calls[inputManager.calls.length - 1], ["arcadeInput", root.padKey, "1", ""])
            compare(page.notice, "Button 1 is None now")
            // Coin's: never none, round to the last.
            for (i = 0; i < 8; i++)
                keyClick(Qt.Key_Down)
            compare(page.rows[12].label, "Coin")
            compare(page.lit, ["coin"])
            keyClick(Qt.Key_Left)
            compare(inputManager.calls[inputManager.calls.length - 1], ["arcadeInput", root.padKey, "coin", "rightstick"])

            // Leaving the page while it waits lets the pad act again.
            keyClick(Qt.Key_Return)
            compare(inputManager.capturing, root.padKey)
            loader.source = ""
            compare(inputManager.capturing, "")
        }

        // SET ALL BUTTONS: 1 to 6, Coin, Start, 7 and 8, Button 2 to 8 left
        // none when their shorter wait runs out (a pad may have fewer); the
        // joystick, Guide and a button pressed already are told so and
        // waited past; all ten set at the end, or none. Each step's wait is
        // counted from a whole second. The screen saver waits meanwhile: the
        // presses come as no keys.
        function test_arcadeSetAll() {
            var page = open("ArcadeButtons.qml", { key: root.padKey })
            keyClick(Qt.Key_Down)
            compare(page.rows[1].type, "setall")
            keyClick(Qt.Key_Return)
            verify(page.settingAll)
            var prompt = findChild(page, "capturePrompt")
            compare(prompt.title, "Set All Buttons (1/10)")
            compare(prompt.message, "Press the button on 8BitDo Pro 2 for [Button 1]")
            compare(prompt.hint, "CANCELS IN 10")
            var panel = findChild(page, "capturePanel")
            compare(panel.lit, ["1"])
            wait(800)
            inputManager.controllerInputCaptured(root.padKey, "x", "X")
            compare(panel.lit, ["2"])
            compare(prompt.message, "Press the button on 8BitDo Pro 2 for [Button 2], or wait if it has none")
            compare(prompt.hint, "NONE IN 5")
            wait(500)
            compare(prompt.hint, "NONE IN 5")
            var presses = [["y", "Y"], ["rightshoulder", "RB"], ["a", "A"], ["b", "B"],
                           ["righttrigger+", "RT"], ["back", "BACK"]]
            for (var i = 0; i < presses.length; i++)
                inputManager.controllerInputCaptured(root.padKey, presses[i][0], presses[i][1])
            compare(prompt.title, "Set All Buttons (8/10)")
            compare(prompt.message, "Press the button on 8BitDo Pro 2 for [Start]")
            compare(panel.lit, ["start"])
            compare(prompt.hint, "CANCELS IN 10")
            inputManager.controllerInputCaptured(root.padKey, "start", "START")
            compare(prompt.title, "Set All Buttons (9/10)")
            compare(prompt.message, "Press the button on 8BitDo Pro 2 for [Button 7], or wait if it has none")
            compare(prompt.choices, ["Start: START", "Coin: BACK", "Button 6: RT"])
            compare(prompt.hint, "NONE IN 5")
            inputManager.controllerInputCaptured(root.padKey, "dpup", "D-PAD ▲")
            compare(prompt.message, "D-PAD ▲ is the joystick: press a button\n"
                                    + "Press the button on 8BitDo Pro 2 for [Button 7], or wait if it has none")
            inputManager.controllerInputCaptured(root.padKey, "guide", "GUIDE")
            compare(prompt.message, "GUIDE shows MAME's menu: press another button\n"
                                    + "Press the button on 8BitDo Pro 2 for [Button 7], or wait if it has none")
            compare(prompt.title, "Set All Buttons (9/10)")
            inputManager.controllerInputCaptured(root.padKey, "x", "X")
            verify(prompt.message.indexOf("X is Button 1 already") === 0)
            compare(prompt.title, "Set All Buttons (9/10)")
            inputManager.controllerInputCaptured(root.padKey, "leftshoulder", "LB")
            compare(prompt.title, "Set All Buttons (10/10)")
            compare(inputManager.calls.filter(function(c) { return c[0] === "arcadeInputs" }), [])
            // Button 8's wait runs out: none.
            page.secondsLeft = 1
            tryVerify(function() { return !page.settingAll }, 2500)
            verify(idleTracker.resets > 0)
            compare(inputManager.calls[inputManager.calls.length - 1],
                    ["arcadeInputs", root.padKey, ["x", "y", "rightshoulder", "a", "b", "righttrigger+", "leftshoulder", "",
                                                   "back", "start"]])
            compare(page.notice, "Set: its layout is Custom now")
            compare(inputManager.capturing, "")

            // A pad of two buttons: 3 to 6 run out, none; Coin, which must
            // have one, ends it, nothing set. So does another controller's
            // Back.
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.padKey, "a", "A")
            inputManager.controllerInputCaptured(root.padKey, "b", "B")
            for (i = 3; i <= 6; i++) {
                compare(prompt.title, "Set All Buttons (" + i + "/10)")
                page.secondsLeft = 1
                tryCompare(prompt, "title", "Set All Buttons (" + (i + 1) + "/10)", 2500)
            }
            compare(prompt.message, "Press the button on 8BitDo Pro 2 for [Coin]")
            compare(prompt.choices, ["Button 6: None", "Button 5: None", "Button 4: None"])
            compare(prompt.hint, "CANCELS IN 10")
            page.secondsLeft = 1
            tryVerify(function() { return !page.settingAll }, 2500)
            compare(page.notice, "No press for Coin: nothing changed")
            keyClick(Qt.Key_Return)
            verify(page.settingAll)
            keyClick(Qt.Key_Escape)
            verify(!page.settingAll)
            compare(page.notice, "Nothing changed")
            compare(inputManager.calls.filter(function(c) { return c[0] === "arcadeInputs" }).length, 1)
            compare(root.backs, 0)
            // Button 1 must have one too.
            keyClick(Qt.Key_Return)
            page.secondsLeft = 1
            tryVerify(function() { return !page.settingAll }, 2500)
            compare(page.notice, "No press for Button 1: nothing changed")
        }

        // TEST BUTTONS: which of the cabinet's each press is; under Default
        // a pad's buttons are the emulators', its Back and Start Coin and
        // Start.
        function test_arcadeTest() {
            var page = open("ArcadeButtons.qml", { key: root.padKey })
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(page.testing)
            compare(inputManager.capturing, root.padKey)
            var prompt = findChild(page, "testPrompt")
            var panel = findChild(page, "testPanel")
            compare(panel.lit, [])
            inputManager.controllerInputCaptured(root.padKey, "a", "A")
            compare(panel.lit, [])
            inputManager.controllerInputCaptured(root.padKey, "back", "BACK")
            compare(panel.lit, ["coin"])
            inputManager.controllerInputCaptured(root.padKey, "dpup", "D-PAD ▲")
            compare(panel.lit, [])
            compare(prompt.choices, ["D-PAD ▲ → Joystick", "BACK → Coin", "A"])
            inputManager.controllerInputCaptured(root.padKey, "guide", "GUIDE")
            compare(panel.lit, [])
            compare(prompt.choices[0], "GUIDE → MAME's Menu")
            keyClick(Qt.Key_Escape)
            verify(!page.testing)
            compare(inputManager.capturing, "")

            // A layout of its own: each its line, or none.
            inputManager.setArcadeLayout(root.padKey, "fighting6")
            var p = inputManager.arcadePages
            p[root.padKey].rows[0].token = "x"
            p[root.padKey].rows[3].token = "a"
            inputManager.arcadePages = p
            page.refresh()
            keyClick(Qt.Key_Return)
            inputManager.controllerInputCaptured(root.padKey, "x", "X")
            compare(panel.lit, ["1"])
            inputManager.controllerInputCaptured(root.padKey, "leftstick", "LS")
            compare(panel.lit, [])
            compare(prompt.choices, ["LS → None", "X → Button 1"])
            page.secondsLeft = 1
            tryVerify(function() { return !page.testing }, 2500)
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

        // A pad away: its buttons and their test wait for it to be back; its
        // layout and ◄ ► don't. One that goes while a press is waited for, or
        // while it is tested, ends that, and the list has the keys again.
        function test_arcadeAway() {
            inputManager.arcadePages[root.padKey].present = false
            var page = open("ArcadeButtons.qml", { key: root.padKey })
            compare(page.rows[1].description, "Connect it to set its buttons")
            compare(page.rows[2].description, "Connect it to test its buttons")
            keyClick(Qt.Key_Right)
            compare(inputManager.calls[inputManager.calls.length - 1], ["arcadeLayout", root.padKey, "fighting6"])
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(!page.settingAll)
            compare(page.notice, "Connect it first")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(!page.testing)
            compare(page.notice, "Connect it first")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(!page.capturing)
            compare(inputManager.capturing, "")
            compare(page.notice, "Connect it first")
            keyClick(Qt.Key_Right)
            compare(inputManager.calls[inputManager.calls.length - 1], ["arcadeInput", root.padKey, "1", "leftstick"])

            // Back, then gone again while a press is waited for, and while
            // its buttons are tested.
            var setPresent = function(present) {
                var p = inputManager.arcadePages
                p[root.padKey].present = present
                inputManager.arcadePages = p
                inputManager.controllersChanged()
            }
            setPresent(true)
            keyClick(Qt.Key_Return)
            verify(page.capturing)
            compare(inputManager.capturing, root.padKey)
            setPresent(false)
            verify(!page.capturing)
            compare(inputManager.capturing, "")
            keyClick(Qt.Key_Up)     // TEST BUTTONS, past the heading
            setPresent(true)
            keyClick(Qt.Key_Return)
            verify(page.testing)
            setPresent(false)
            verify(!page.testing)
            compare(inputManager.capturing, "")
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }
    }
}
