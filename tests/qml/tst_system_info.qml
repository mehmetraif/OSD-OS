import QtQuick
import QtTest

// Settings → About → System Information, views/SystemInfo.qml, against a
// stand-in for SystemInfo: read again as it opens, its rows under the
// device's name, the cursor stepping over the Network heading and kept on its
// line, by its key, as the rows are read again; select on the name opens the
// keyboard on it where the device can be renamed, the name typed goes to
// rename(), the line says Renaming meanwhile and the help line how it went;
// where it can't be, neither the keyboard nor the hint; back leaves.
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
    property int backs: 0

    // A Pi 5 named den-tv, on Ethernet and Wi-Fi.
    readonly property var deviceRows: [
        { key: "name", label: "Device Name", value: "den-tv",
          description: "This device's name on the network: what a router lists it under" },
        { key: "model", label: "Model", value: "Raspberry Pi 5 Model B",
          description: "The board or computer OSD/OS runs on: Raspberry Pi 5 Model B Rev 1.0" },
        { key: "system", label: "OS", value: "Debian GNU/Linux 13 (trixie)", description: "OSD/OS 0.0.10" },
        { key: "uptime", label: "Up For", value: "3D 4H 12M", description: "Since the system last started" },
        { type: "section", label: "Network" },
        { key: "ip:eth0", label: "Ethernet IP", value: "192.168.1.20", description: "eth0's addresses: 192.168.1.20" },
        { key: "mac:eth0", label: "Ethernet MAC", value: "D8:3A:DD:00:00:01", description: "eth0's hardware address" },
        { key: "ip:wlan0", label: "Wi-Fi IP", value: "192.168.1.21", description: "wlan0's addresses: 192.168.1.21" },
        { key: "mac:wlan0", label: "Wi-Fi MAC", value: "D8:3A:DD:00:00:02", description: "wlan0's hardware address" }
    ]

    // SystemInfo as the page uses it, its calls kept in order.
    QtObject {
        id: systemInfo
        property string deviceName: "den-tv"
        property var rows: root.deviceRows
        property bool renameAvailable: true
        property bool renaming: false
        property string message: ""
        property var calls: []
        function record(call) { calls = calls.concat([call]) }
        function refresh() { record("refresh") }
        function rename(typed) { record("rename " + typed); return true }
        function clearMessage() { record("clearMessage"); message = "" }
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }
    Connections {
        target: loader.item
        ignoreUnknownSignals: true
        function onGoBack() { root.backs++ }
    }

    TestCase {
        name: "SystemInfo"
        when: windowShown

        function init() {
            systemInfo.deviceName = "den-tv"
            systemInfo.rows = root.deviceRows
            systemInfo.renameAvailable = true
            systemInfo.renaming = false
            systemInfo.message = ""
            systemInfo.calls = []
            root.backs = 0
        }
        function cleanup() {
            loader.source = ""
        }

        function open() {
            loader.setSource(Qt.resolvedUrl("../../views/SystemInfo.qml"))
            verify(loader.item !== null, "loaded SystemInfo.qml")
            loader.item.forceActiveFocus()
            return loader.item
        }
        function childOf(page, has) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i][has] !== undefined)
                    return page.children[i]
            }
            return null
        }
        function listOf(page) { return childOf(page, "shownTop") }
        // A row as MenuList draws it, its value as shown.
        function shown(page, index) {
            return childOf(childOf(listOf(page), "topCut"), "highlightMoveDuration").itemAtIndex(index).value
        }
        function keyboardOf(page) { return childOf(page, "masked") }
        function helpOf(page) { return childOf(page, "always").text }
        function hintOf(page) { return childOf(page, "largest").text }
        function cursor(page) {
            var row = page.rows[listOf(page).currentIndex]
            return row.type === "section" ? "# " + row.label : row.key
        }
        function type(text) {
            for (var i = 0; i < text.length; i++)
                keyClick(text.charAt(i))
        }

        // Read again as it opens, under the device's name; the cursor on the
        // name, which can be renamed.
        function test_rows() {
            var page = open()
            compare(systemInfo.calls, ["clearMessage", "refresh"])
            compare(childOf(page, "subtitle").subtitle, "den-tv")
            compare(page.rows.length, 9)
            compare(cursor(page), "name")
            compare(helpOf(page), "This device's name on the network: what a router lists it under • [ENTER] Rename it")
            compare(hintOf(page), "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:RENAME")
            // The page reads the system again every few seconds.
            tryCompare(systemInfo, "calls", ["clearMessage", "refresh", "refresh"], 4000)
        }

        // Down past the heading, and up again; away from the name, its line's
        // own help and no RENAME.
        function test_cursor() {
            var page = open()
            for (var i = 0; i < 3; i++)
                keyClick(Qt.Key_Down)
            compare(cursor(page), "uptime")
            keyClick(Qt.Key_Down)
            compare(cursor(page), "ip:eth0")
            compare(helpOf(page), "eth0's addresses: 192.168.1.20")
            compare(hintOf(page), "[ESC]:BACK [▲▼]:NAVIGATE")
            keyClick(Qt.Key_Up)
            compare(cursor(page), "uptime")
            // Select on a line that isn't the name opens nothing.
            keyClick(Qt.Key_Return)
            verify(!keyboardOf(page).visible)
        }

        // The rows read again: the cursor on its line by its key, wherever
        // it is now; its line gone, on the one where it was, never the
        // heading.
        function test_cursorKept() {
            var page = open()
            for (var i = 0; i < 6; i++)
                keyClick(Qt.Key_Down)
            compare(cursor(page), "ip:wlan0")
            // Ethernet unplugged: its lines go.
            systemInfo.rows = root.deviceRows.filter(function(row) { return row.key !== "ip:eth0" && row.key !== "mac:eth0" })
            compare(cursor(page), "ip:wlan0")
            // Wi-Fi gone too: the cursor where it was, on the last line.
            systemInfo.rows = root.deviceRows.slice(0, 5).concat([{ key: "none", label: "No Network", value: "",
                                                                      description: "No Ethernet or Wi-Fi" }])
            compare(cursor(page), "none")
            // Only the heading where it was: the line after it.
            keyClick(Qt.Key_Up)
            compare(cursor(page), "uptime")
            systemInfo.rows = root.deviceRows.slice(0, 3).concat(root.deviceRows.slice(4))
            compare(cursor(page), "ip:eth0")
        }

        // Read again with the same lines: each value changes in place, its
        // line not made anew (a value scrolling through goes on scrolling),
        // and the cursor stays where it is.
        function test_valuesInPlace() {
            var page = open()
            for (var i = 0; i < 3; i++)
                keyClick(Qt.Key_Down)
            compare(cursor(page), "uptime")
            var view = childOf(childOf(listOf(page), "topCut"), "highlightMoveDuration")
            var line = view.itemAtIndex(3)
            systemInfo.rows = root.deviceRows.map(function(row) {
                return row.key === "uptime" ? Object.assign({}, row, { value: "3D 4H 13M", description: "Up" }) : row
            })
            verify(view.itemAtIndex(3) === line, "the same line")
            compare(line.value, "3D 4H 13M")
            compare(cursor(page), "uptime")
            compare(helpOf(page), "Up")
        }

        // Select on the name: the keyboard on it; the name typed sent, then
        // Renaming on its line until the answer, which the help line says.
        function test_rename() {
            var page = open()
            keyClick(Qt.Key_Return)
            var keyboard = keyboardOf(page)
            verify(keyboard.visible)
            compare(keyboard.title, "Device Name")
            compare(keyboard.text, "den-tv")
            compare(keyboard.maxLength, 63)
            for (var i = 0; i < 6; i++)
                keyClick(Qt.Key_Backspace)
            type("ATTIC TV")
            keyClick(Qt.Key_Return)
            verify(!keyboard.visible)
            compare(systemInfo.calls, ["clearMessage", "refresh", "rename ATTIC TV"])
            systemInfo.renaming = true
            compare(shown(page, 0), "Renaming")
            // None other meanwhile.
            keyClick(Qt.Key_Return)
            verify(!keyboard.visible)
            systemInfo.renaming = false
            systemInfo.deviceName = "attic-tv"
            systemInfo.rows = [Object.assign({}, root.deviceRows[0], { value: "attic-tv" })].concat(root.deviceRows.slice(1))
            systemInfo.message = "Now called attic-tv"
            compare(shown(page, 0), "attic-tv")
            compare(childOf(page, "subtitle").subtitle, "attic-tv")
            compare(helpOf(page), "Now called attic-tv")
            // Away from the name, its own help; back on it, the message still.
            keyClick(Qt.Key_Down)
            compare(helpOf(page), "The board or computer OSD/OS runs on: Raspberry Pi 5 Model B Rev 1.0")
            keyClick(Qt.Key_Up)
            compare(helpOf(page), "Now called attic-tv")
            // Back from the keyboard: nothing sent.
            keyClick(Qt.Key_Return)
            verify(keyboard.visible)
            keyClick(Qt.Key_Escape)
            verify(!keyboard.visible)
            compare(systemInfo.calls.length, 3)
            compare(root.backs, 0)
        }

        // Where it can't be renamed: no keyboard, nor a word of renaming.
        function test_noRename() {
            systemInfo.renameAvailable = false
            var page = open()
            compare(helpOf(page), "This device's name on the network: what a router lists it under")
            compare(hintOf(page), "[ESC]:BACK [▲▼]:NAVIGATE")
            keyClick(Qt.Key_Return)
            verify(!keyboardOf(page).visible)
            compare(systemInfo.calls, ["clearMessage", "refresh"])
        }

        function test_back() {
            open()
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
            keyClick(Qt.Key_Backspace)
            compare(root.backs, 2)
        }
    }
}
