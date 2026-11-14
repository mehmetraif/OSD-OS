import QtQuick
import QtTest

// What comes over a view, each on its own: the on-screen keyboard
// (views/Components/OnScreenKeyboard.qml), whose keys stay the keys they are
// as SHIFT and SHOW change what they say.
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

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }

    TestCase {
        name: "OnScreenKeyboard"
        when: windowShown

        function cleanup() {
            loader.source = ""
        }

        // The keyboard on its own, as a view carries it.
        function keyboardWith(properties) {
            loader.setSource(Qt.resolvedUrl("../../views/Components/OnScreenKeyboard.qml"), properties)
            verify(loader.item !== null, "loaded OnScreenKeyboard.qml")
            return loader.item
        }
        // Every key drawn, row by row.
        function keysOf(keyboard) {
            var keys = []
            for (var r = 0; r < keyboard.rows.length; r++) {
                keys.push([])
                for (var c = 0; c < keyboard.rows[r].length; c++)
                    keys[r].push(keyboard.keyItem(r, c))
            }
            return keys
        }
        // What the key at a row and column says.
        function saidBy(keyboard, r, c) {
            var key = keyboard.keyItem(r, c)
            for (var i = 0; key && i < key.children.length; i++) {
                if (key.children[i].text !== undefined)
                    return key.children[i].text
            }
            return null
        }
        function sameKeys(keyboard, keys) {
            for (var r = 0; r < keys.length; r++) {
                for (var c = 0; c < keys[r].length; c++) {
                    if (keys[r][c] === null || keyboard.keyItem(r, c) !== keys[r][c])
                        return false
                }
            }
            return true
        }

        // SHIFT changes what the keys say, the letters and the second row of
        // signs, and SHOW its own key: every key stays the item it was, none
        // made again, and the box where it was.
        function test_shiftKeepsTheKeys() {
            var keyboard = keyboardWith({ caseSensitive: true, masked: true })
            keyboard.open("")
            var last = keyboard.rows.length - 1
            var keys = keysOf(keyboard)
            compare(saidBy(keyboard, 0, 0), "a")
            compare(saidBy(keyboard, 5, 2), ",")
            keyboard.moveTo(last, 0)
            compare(saidBy(keyboard, last, 0), "SHIFT")
            keyClick(Qt.Key_Return)
            verify(keyboard.shifted)
            compare(saidBy(keyboard, 0, 0), "A")
            compare(saidBy(keyboard, 5, 2), "^")
            verify(sameKeys(keyboard, keys), "the same keys after SHIFT")
            compare(keyboard.row, last)
            compare(keyboard.col, 0)
            keyClick(Qt.Key_Return)
            verify(!keyboard.shifted)
            compare(saidBy(keyboard, 0, 0), "a")
            verify(sameKeys(keyboard, keys), "the same keys after SHIFT again")
            // SHOW becomes HIDE on its own key.
            keyboard.moveTo(last, 3)
            compare(saidBy(keyboard, last, 3), "SHOW")
            keyClick(Qt.Key_Return)
            verify(keyboard.revealed)
            compare(saidBy(keyboard, last, 3), "HIDE")
            verify(sameKeys(keyboard, keys), "the same keys after SHOW")
            compare(keyboard.row, last)
            compare(keyboard.col, 3)
            // A key still types what it says.
            keyboard.moveTo(1, 0)
            keyClick(Qt.Key_Return)
            compare(keyboard.text, "k")
        }
    }
}
