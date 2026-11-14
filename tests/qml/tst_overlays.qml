import QtQuick
import QtTest

// What comes over a view, each on its own: the on-screen keyboard
// (views/Components/OnScreenKeyboard.qml), whose keys stay the keys they are
// as SHIFT and SHOW change what they say, whose line keeps its end in view
// however long it is, and which Backspace leaves once nothing is typed.
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

    SignalSpy {
        id: canceledSpy
        signalName: "canceled"
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

        // The line typed as it is drawn (the stars of a hidden one): its
        // Text, in a row with the block after it, in the box that clips it.
        function lineOf(keyboard, shown) {
            var found = null
            var walk = function(item) {
                if (item.text === shown && item.elide !== undefined)
                    found = item
                for (var i = 0; i < item.children.length && !found; i++)
                    walk(item.children[i])
            }
            walk(keyboard)
            verify(found !== null, "the line typed, " + shown)
            var box = found.parent.parent
            verify(box.clip)
            return { text: found, block: found.parent.children[1], box: box }
        }
        function type(text) {
            for (var i = 0; i < text.length; i++)
                keyClick(text.charAt(i))
        }

        // A short line starts at the box's left edge. One longer than the
        // box (a password, 64 characters; an address) keeps its end in view,
        // at the box's right edge with room for the block after it, its
        // start running out at the left; deleted back to a short one, it
        // starts at the left edge again. A hidden line's stars the same.
        function test_longLineKeepsItsEnd_data() {
            return [{ tag: "as typed", masked: false }, { tag: "hidden", masked: true }]
        }
        function test_longLineKeepsItsEnd(data) {
            var keyboard = keyboardWith({ caseSensitive: true, masked: data.masked, maxLength: 64 })
            keyboard.open("")
            var shown = function() { return data.masked ? "*".repeat(keyboard.text.length) : keyboard.text }
            var margin = root.sw * 0.0125
            type("ab")
            var line = lineOf(keyboard, shown())
            compare(line.text.mapToItem(line.box, 0, 0).x, margin)
            type("cdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz01")
            compare(keyboard.text.length, 64)
            line = lineOf(keyboard, shown())
            verify(line.text.width > line.box.width, "a line longer than its box")
            // Its end a block's width short of the box's right edge, for the
            // block that comes and goes after it.
            verify(line.block.width > 0)
            fuzzyCompare(line.text.mapToItem(line.box, line.text.width, 0).x,
                         line.box.width - margin - line.block.width, 0.01)
            verify(line.text.mapToItem(line.box, 0, 0).x < 0)
            for (var i = 0; i < 60; i++)
                keyClick(Qt.Key_Backspace)
            compare(keyboard.text, "abcd")
            line = lineOf(keyboard, shown())
            compare(line.text.mapToItem(line.box, 0, 0).x, margin)
        }

        // Backspace deletes a letter while there is one, and leaves the
        // keyboard up; with none typed it is back: the keyboard closes,
        // canceled. Held, it deletes and no more, so a line deleted by
        // holding it stays up. Delete only ever deletes.
        function test_backspaceIsBackOnceNothingIsTyped() {
            var keyboard = keyboardWith({})
            canceledSpy.target = keyboard
            canceledSpy.clear()
            keyboard.open("AB")
            keyClick(Qt.Key_Backspace)
            compare(keyboard.text, "A")
            keyClick(Qt.Key_Backspace)
            compare(keyboard.text, "")
            verify(keyboard.visible)
            keyClick(Qt.Key_Delete)
            verify(keyboard.visible)
            // A held key's repeat, as Keys.onPressed hands it on.
            keyboard.backspace(true)
            verify(keyboard.visible)
            compare(canceledSpy.count, 0)
            keyClick(Qt.Key_Backspace)
            verify(!keyboard.visible)
            verify(!keyboard.activeFocus)
            compare(canceledSpy.count, 1)
            // Esc, as the hint bar says, with something typed too.
            keyboard.open("AB")
            keyClick(Qt.Key_Escape)
            verify(!keyboard.visible)
            compare(canceledSpy.count, 2)
        }

        // In another view's window (the Arcade screen's Search), back is the
        // view's to act on: Backspace with nothing typed says so and leaves
        // the keys up, as Esc does.
        function test_backspaceInAnotherViewsWindow() {
            var keyboard = keyboardWith({ inline: true, allowEmpty: true, visible: true })
            canceledSpy.target = keyboard
            canceledSpy.clear()
            keyboard.forceActiveFocus()
            keyClick(Qt.Key_A)
            keyClick(Qt.Key_Backspace)
            compare(keyboard.text, "")
            compare(canceledSpy.count, 0)
            keyClick(Qt.Key_Backspace)
            compare(canceledSpy.count, 1)
            verify(keyboard.visible)
        }
    }
}
