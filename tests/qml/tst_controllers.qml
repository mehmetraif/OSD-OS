import QtQuick
import QtTest

// Settings → Controls' pages, views/Controllers.qml, ControllerSettings.qml
// and RemapControls.qml, against a stand-in for InputManager: the list of
// controllers (connected, away, the one used last, the players' pads chosen
// from those connected), a controller's page (its labels, a press waited for
// and set or kept, cancelled by another controller's Back or by the wait
// running out, a button cleared, the defaults), and All Keyboards & Remotes'
// wait for a press.
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
        property var pages: ({})
        property var players: []
        // What a press makes of a key ("<controller> <input>"), as
        // InputManager decides it; any other is set.
        property var outcomes: ({})
        property bool remapCapture: false
        signal controllerInputCaptured(string key, string input, string label)
        signal auxButtonPressed(int extendedKeyId)
        function controllerPage(key) { return pages[key] || ({}) }
        function captureController(key) { capturing = key }
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

    TestCase {
        name: "ControlsPages"
        when: windowShown

        function init() {
            inputManager.controllers = [
                { key: root.padKey, name: "8BitDo Pro 2", kind: "gamepad", count: 2, present: true },
                { key: root.jammaKey, name: "OSD/OS JAMMA P1", kind: "arcade", count: 1, present: true },
                { key: root.keyboardKey, name: "Logitech K400", kind: "keyboard", count: 1, present: true },
                { key: root.remoteKey, name: "G20S PRO", kind: "remote", count: 1, present: true },
                { key: root.awayKey, name: "DualSense", kind: "gamepad", count: 0, present: false }
            ]
            inputManager.pages = {
                "pad-0300aaaa": { key: root.padKey, name: "8BitDo Pro 2", kind: "gamepad", present: true,
                                  labels: "auto", labelsInUse: "Xbox",
                                  rows: root.rows(["D-PAD ▲, L STICK ▲", "D-PAD ▼, L STICK ▼", "LB, D-PAD ◄, L STICK ◄",
                                                   "RB, D-PAD ►, L STICK ►", "A", "B, BACK", "START"]) },
                "kbd-0003-046d-c31c": { key: root.keyboardKey, name: "Logitech K400", kind: "keyboard", present: true,
                                        labels: "auto", labelsInUse: "",
                                        rows: root.rows(["Default", "Default", "Default", "Default", "Default + O",
                                                         "Default", "Default"]) },
                "pad-0500cccc": { key: root.awayKey, name: "DualSense", kind: "gamepad", present: false,
                                  labels: "playstation", labelsInUse: "PlayStation",
                                  rows: root.rows(["D-PAD ▲", "D-PAD ▼", "D-PAD ◄", "D-PAD ►", "X", "O", "START"]) }
            }
            inputManager.players = root.withAuto([
                { player: 1, key: "", name: "OSD/OS JAMMA P1", playing: root.jammaKey, connected: true },
                { player: 2, key: root.padKey, name: "8BitDo Pro 2", playing: root.padKey, connected: true },
                { player: 3, key: "", name: "8BitDo Pro 2", playing: root.padKey, connected: true }
            ])
            inputManager.activeController = ""
            inputManager.calls = []
            inputManager.capturing = ""
            inputManager.outcomes = {}
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
            // A player for each of the three pads.
            compare(labels, ["All Keyboards & Remotes", "Players", "Player 1", "Player 2", "Player 3", "Connected",
                             "8BitDo Pro 2 x2", "OSD/OS JAMMA P1", "Logitech K400", "G20S PRO",
                             "Not Connected", "DualSense"])
            // The pads that play say so.
            compare(page.rows[6].value, "P2+P3 Gamepad")
            compare(page.rows[7].value, "P1 Arcade")
            compare(page.rows[8].value, "Keyboard")
            compare(page.rows[9].value, "Remote")
            compare(page.rows[11].value, "Gamepad")

            // The one used last is marked.
            inputManager.activeController = root.keyboardKey
            compare(page.rows[8].value, "Keyboard ◄")
            compare(page.rows[6].value, "P2+P3 Gamepad")

            // The cursor skips the headings; select opens the page.
            keyClick(Qt.Key_Down)   // Player 1
            keyClick(Qt.Key_Down)   // Player 2
            keyClick(Qt.Key_Down)   // Player 3
            keyClick(Qt.Key_Down)   // the 8BitDo
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "views/ControllerSettings.qml")
            compare(root.navigations[0].params.key, root.padKey)

            // A controller going: the cursor stays on its own line, and the
            // player there was a pad for goes.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)   // the keyboard
            inputManager.controllers = inputManager.controllers.filter(function(c) { return c.key !== root.jammaKey })
            compare(page.rows.filter(function(r) { return r.type === "player" }).length, 2)
            keyClick(Qt.Key_Return)
            compare(root.navigations[1].params.key, root.keyboardKey)

            // The shared keys' page, and back.
            for (var i = 0; i < 4; i++)
                keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(root.navigations[2].path, "views/RemapControls.qml")
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
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
                    ["shared", "section", "player", "player", "section", "note"])
            compare(page.rows[2].value, "Auto")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)   // the note can't be chosen: back round to the first line
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
            compare(page.rows[7].value, "P2 Arcade")

            // Players 3 to 6 have no JAMMA stick of their number; one chosen
            // beyond the pads there keeps its line.
            verify(page.rows[4].description.indexOf("JAMMA") < 0)
            var players = root.withAuto([])
            players[4] = { player: 5, key: root.awayKey, name: "DualSense", playing: "", connected: false }
            inputManager.players = players
            compare(page.rows.filter(function(r) { return r.type === "player" }).length, 5)
            compare(page.rows[6].value, "DualSense (Away)")
        }

        function test_padPage() {
            var page = open("ControllerSettings.qml", { key: root.padKey })
            compare(page.rows.map(function(r) { return r.type }),
                    ["labels", "section", "action", "action", "action", "action", "action", "action", "action",
                     "clear", "reset"])
            compare(page.rows[0].value, "Auto (Xbox)")
            compare(page.rows[6].value, "A")

            // The labels: ◄ ► and select go round them.
            keyClick(Qt.Key_Right)
            compare(inputManager.calls[0], ["labels", root.padKey, "xbox"])
            compare(page.rows[0].value, "Xbox")
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            compare(inputManager.calls[2], ["labels", root.padKey, "arcade"])

            // UP: a press on the pad is waited for, then set.
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
                    ["section", "action", "action", "action", "action", "action", "action", "action",
                     "clear", "reset"])
            compare(page.rows[5].value, "Default + O")
            compare(page.rows[8].label, "Remove a Key")

            // What does Back on it cancels, as on the shared page; Select /
            // OK's keys and the menus' stay; another key is set.
            var esc = root.keyboardKey + " " + String(Qt.Key_Escape)
            var enter = root.keyboardKey + " " + String(Qt.Key_Return)
            inputManager.outcomes[esc] = { outcome: "cancel", action: "back", label: "Back" }
            inputManager.outcomes[enter] = { outcome: "already", action: "select", label: "Select / OK" }
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
            compare(page.rows[8].type, "clear")
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

        function test_awayPage() {
            var page = open("ControllerSettings.qml", { key: root.awayKey })
            compare(page.rows[0].value, "PlayStation")
            // Its buttons wait for it to be back; its labels and the defaults don't.
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(!page.capturing)
            compare(inputManager.capturing, "")
            compare(page.notice, "Connect it first")
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
    }
}
