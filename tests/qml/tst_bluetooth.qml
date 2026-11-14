import QtQuick
import QtTest
import "../../views/Components/Hover.js" as Hover

// Settings → Network → Bluetooth, views/Bluetooth.qml, against a stand-in for
// BluetoothManager: its lines (Bluetooth, Search, the paired devices and
// those found), a paired device's options, and what BlueZ says while the page
// is up changing the lines where they are, the cursor kept on its device.
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
    property bool framed: false
    readonly property real paneX: sw * 0.115625
    readonly property real paneWidth: sw * 0.76875
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

    // BluetoothManager as the page uses it, its calls kept in order.
    QtObject {
        id: bluetoothManager
        property bool supported: true
        property bool available: true
        property bool powered: true
        property bool searching: false
        property var devices: []
        property var prompt: ({})
        property string message: ""
        property bool powerFailed: false
        property string adapterName: "den-tv"
        property string details: ""
        property var calls: []
        function record(call) { calls = calls.concat([call]) }
        function setPowered(on) { record("setPowered " + on) }
        function startSearch() { record("startSearch") }
        function stopSearch() { record("stopSearch") }
        function pair(path) { record("pair " + path) }
        function connectDevice(path) { record("connect " + path) }
        function disconnectDevice(path) { record("disconnect " + path) }
        function forget(path) { record("forget " + path) }
        function cancelPairing() { record("cancelPairing") }
        function answerPrompt(accept) { record("answerPrompt " + accept) }
        function collectDetails() { record("collectDetails") }
        function clearMessage() { message = "" }
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }

    function device(name, extra) {
        return Object.assign({ path: "/org/bluez/hci0/dev_" + name.replace(/ /g, "_"), name: name, kind: "Keyboard",
                               paired: false, connected: false, busy: "" }, extra || {})
    }
    // A keyboard and a pad paired, two more found: two pads of one name among
    // them, as two of a model have.
    function someDevices(change) {
        var all = [
            root.device("Keys", { paired: true, connected: true }),
            root.device("Pad", { paired: true, kind: "Gamepad" }),
            root.device("Remote", { kind: "Remote" }),
            root.device("Pad", { path: "/org/bluez/hci0/dev_Pad_2", kind: "Gamepad" })
        ]
        return change ? all.map(change) : all
    }

    TestCase {
        name: "Bluetooth"
        when: windowShown

        function init() {
            bluetoothManager.available = true
            bluetoothManager.powered = true
            bluetoothManager.searching = false
            bluetoothManager.prompt = {}
            bluetoothManager.message = ""
            bluetoothManager.powerFailed = false
            bluetoothManager.devices = root.someDevices()
            bluetoothManager.calls = []
        }
        function cleanup() {
            loader.source = ""
        }

        function open() {
            loader.setSource(Qt.resolvedUrl("../../views/Bluetooth.qml"))
            verify(loader.item !== null, "loaded Bluetooth.qml")
            loader.item.forceActiveFocus()
            return loader.item
        }
        // The page's lines as their labels, a heading's marked "# ".
        function lines(page) {
            return page.rows.map(function(row) { return (row.type === "section" ? "# " : "") + (row.label || "") })
        }
        function listOf(page) {
            for (var i = 0; i < page.children.length; i++) {
                var item = page.children[i]
                if (item.currentIndex !== undefined && item.model !== undefined && item.highlightMoveDuration !== undefined)
                    return item
            }
            return null
        }
        // The path of the device under the cursor, or the line's label.
        function cursor(page) {
            var row = page.rows[listOf(page).currentIndex]
            return row.device ? row.device.path : row.label
        }
        function overlayOf(page) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i].promptText !== undefined)
                    return page.children[i]
            }
            return null
        }

        function test_unavailable() {
            bluetoothManager.available = false
            var page = open()
            compare(lines(page), ["No Bluetooth"])
        }

        // The lines in their groups, each device's value; off, the switch
        // and Search alone with nothing paired or found.
        function test_lines() {
            var page = open()
            compare(lines(page), ["Bluetooth", "Search", "# Paired", "Keys", "Pad", "# Found", "Remote", "Pad"])
            compare(page.rows.map(function(row) { return row.value || "" }),
                    ["On", "", "", "Connected", "Paired", "", "Remote", "Gamepad"])
            compare(cursor(page), "Bluetooth")
            keyClick(Qt.Key_Return)
            compare(bluetoothManager.calls, ["setPowered false"])
            bluetoothManager.powered = false
            bluetoothManager.devices = []
            compare(lines(page), ["Bluetooth", "Search"])
            compare(page.rows[0].value, "Off")
        }

        // Select on a device found pairs it; on a paired one it offers to
        // connect or disconnect it, or forget it. Leaving the page stops a
        // search and gives a pairing up.
        function test_devices() {
            var page = open()
            for (var i = 0; i < 4; i++)
                keyClick(Qt.Key_Down)
            compare(cursor(page), "/org/bluez/hci0/dev_Remote")
            keyClick(Qt.Key_Return)
            compare(bluetoothManager.calls, ["pair /org/bluez/hci0/dev_Remote"])
            keyClick(Qt.Key_Up)
            compare(cursor(page), "/org/bluez/hci0/dev_Pad")
            keyClick(Qt.Key_Return)
            var overlay = overlayOf(page)
            verify(overlay.visible)
            compare(overlay.subtitleText, "Pad")
            compare(overlay.choices.map(function(c) { return c.label }), ["Connect", "Forget"])
            keyClick(Qt.Key_Return)
            verify(!overlay.visible)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(overlay.choices.map(function(c) { return c.label }), ["Disconnect", "Forget"])
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(bluetoothManager.calls, ["pair /org/bluez/hci0/dev_Remote", "connect /org/bluez/hci0/dev_Pad",
                                             "forget /org/bluez/hci0/dev_Keys"])
            loader.source = ""
            compare(bluetoothManager.calls.slice(3), ["stopSearch", "cancelPairing"])
        }

        // What BlueZ says while the page is up changes the lines where they
        // are: a device connecting, one that says its name at last and a
        // search make no line again, and the cursor and the list stay put.
        // Only a line that comes or goes gives the list its lines anew, the
        // cursor still on its device, by its path where two share a name;
        // the mouse still passes over a heading.
        function test_linesChangeInPlace() {
            var page = open()
            var list = listOf(page)
            for (var i = 0; i < 3; i++)
                keyClick(Qt.Key_Down)
            var pad = "/org/bluez/hci0/dev_Pad"
            compare(cursor(page), pad)
            var at = list.currentIndex
            var line = list.itemAtIndex(at)
            var keys = list.itemAtIndex(at - 1)
            verify(line !== null && keys !== null)
            compare(line.label, "Pad")
            compare(line.value, "Paired")
            // Connecting, then connected.
            bluetoothManager.devices = root.someDevices(function(d) {
                return d.path === pad ? Object.assign({}, d, { busy: "connecting" }) : d
            })
            verify(list.itemAtIndex(at) === line, "the same line, connecting")
            compare(line.value.indexOf("Connecting."), 0)
            verify(list.itemAtIndex(at - 1) === keys, "the line of a device that didn't change")
            bluetoothManager.devices = root.someDevices(function(d) {
                return d.path === pad ? Object.assign({}, d, { connected: true }) : d
            })
            verify(list.itemAtIndex(at) === line, "the same line, connected")
            compare(line.value, "Connected")
            // The same again, as every property of a device says it.
            bluetoothManager.devices = root.someDevices(function(d) {
                return d.path === pad ? Object.assign({}, d, { connected: true }) : d
            })
            verify(list.itemAtIndex(at) === line, "the same line, nothing changed")
            // Its name, said at last, and a search.
            bluetoothManager.devices = root.someDevices(function(d) {
                return d.path === pad ? Object.assign({}, d, { name: "Arcade Pad", connected: true }) : d
            })
            bluetoothManager.searching = true
            verify(list.itemAtIndex(at) === line, "the same line, named")
            compare(line.label, "Arcade Pad")
            compare(list.itemAtIndex(1).value.indexOf("Searching."), 0)
            compare(cursor(page), pad)
            compare(list.currentIndex, at)
            // The mouse over the heading under it moves nothing; over the
            // device found under that, the cursor.
            var hover = function(item) {
                var p = item.mapToItem(root, item.width / 2, item.height / 2)
                Hover.follow(root, root.Window.activeFocusItem, p.x, p.y)
            }
            compare(page.rows[at + 1].type, "section")
            hover(list.itemAtIndex(at + 1))
            compare(cursor(page), pad)
            hover(list.itemAtIndex(at + 2))
            compare(cursor(page), "/org/bluez/hci0/dev_Remote")
            keyClick(Qt.Key_Up)
            compare(cursor(page), pad)
            // The keyboard forgotten: a line fewer, the cursor on its device
            // a line up, not on the other pad of its name.
            bluetoothManager.devices = root.someDevices().slice(1)
            compare(lines(page), ["Bluetooth", "Search", "# Paired", "Pad", "# Found", "Remote", "Pad"])
            compare(cursor(page), pad)
            compare(list.currentIndex, at - 1)
            compare(list.itemAtIndex(at - 1).label, "Pad")
        }

        // The cursor keeps to its device as the list changes: paired, it
        // moves from FOUND to PAIRED, the cursor with it.
        function test_cursorKept() {
            var page = open()
            for (var i = 0; i < 4; i++)
                keyClick(Qt.Key_Down)
            var remote = "/org/bluez/hci0/dev_Remote"
            compare(cursor(page), remote)
            bluetoothManager.devices = root.someDevices(function(d) {
                return d.path === remote ? Object.assign({}, d, { paired: true, connected: true }) : d
            })
            compare(lines(page), ["Bluetooth", "Search", "# Paired", "Keys", "Pad", "Remote", "# Found", "Pad"])
            compare(cursor(page), remote)
            compare(page.rows[listOf(page).currentIndex].value, "Connected")
            // A message over the line's help until the cursor moves.
            bluetoothManager.message = "Remote connected"
            keyClick(Qt.Key_Down)
            compare(bluetoothManager.message, "")
        }
    }
}
