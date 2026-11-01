import QtQuick
import QtTest

// Settings → Test Patterns, views/TestPatterns.qml: the 240p Test Suite's
// pictures, every one of them there and loading; ◄ ► through the patterns
// and round, ▲ ▼ through a pattern's kinds; the name and keys over it as it
// opens and changes, gone after a few seconds or with select; the effects
// resting and the screen saver waiting while it is up; back leaving.
Rectangle {
    id: root
    color: "black"
    width: 640
    height: 480
    // Main.qml's, as the view reads them.
    property real sw: width
    property real sh: height
    property real px: 2
    property string globalFont: "monospace"
    readonly property var keyboardHints: ({ back: "[ESC]", navigate: "[▲▼]", change: "[◄►]", select: "[ENTER]" })
    property var hints: keyboardHints
    property var restLayers: []
    function restFor(layer, resting) {
        var layers = restLayers.filter(function(l) { return l !== layer })
        if (resting)
            layers.push(layer)
        restLayers = layers
    }
    property int backs: 0

    QtObject {
        id: idleTracker
        property bool viewActive: false
        property int resets: 0
        function resetActivity() { resets++ }
    }

    Loader {
        id: loader
        width: root.sw
        height: root.sh
        focus: true
    }
    Connections {
        target: loader.item
        ignoreUnknownSignals: true
        function onGoBack() { root.backs++ }
    }

    TestCase {
        name: "TestPatterns"
        when: windowShown

        function init() {
            root.backs = 0
            root.sh = Qt.binding(function() { return root.height })
            idleTracker.viewActive = false
            idleTracker.resets = 0
        }
        function cleanup() {
            loader.source = ""
        }
        function open() {
            loader.source = Qt.resolvedUrl("../../views/TestPatterns.qml")
            verify(loader.item !== null, "loaded TestPatterns.qml")
            loader.item.forceActiveFocus()
            return loader.item
        }
        function child(item, name) {
            if (item.objectName === name)
                return item
            for (var i = 0; i < item.children.length; i++) {
                var found = child(item.children[i], name)
                if (found)
                    return found
            }
            return null
        }
        function findText(item, text) {
            if (item.text !== undefined && String(item.text).indexOf(text) >= 0 && item.visible)
                return item
            for (var i = 0; i < item.children.length; i++) {
                var found = findText(item.children[i], text)
                if (found)
                    return found
            }
            return null
        }

        // Up, the effects rest and the screen saver waits; gone, both are
        // as they were.
        function test_restsWhileUp() {
            var page = open()
            compare(root.restLayers.length, 1)
            verify(idleTracker.viewActive)
            loader.source = ""
            compare(root.restLayers.length, 0)
            verify(!idleTracker.viewActive)
            compare(idleTracker.resets, 1)
        }

        // The Grid first, the suite's 640 × 480 one on a screen of 480 lines,
        // its 320 × 240 one on 240, scaled whole to the screen's height in
        // the middle.
        function test_gridBySize() {
            var page = open()
            compare(page.pattern.name, "Grid")
            compare(page.kind.image, "grid-480.png")
            var art = child(page, "patternArt")
            compare(art.height * art.scale, root.sh)
            compare(art.width * art.scale, root.sh * 4 / 3)
            loader.source = ""
            root.sh = 240
            page = open()
            compare(page.kind.image, "grid-240.png")
            art = child(page, "patternArt")
            compare(art.height * art.scale, 240)
        }

        // Every pattern's every kind is there and loads: a picture, a tile,
        // or a colour over the whole screen.
        function test_everyPatternLoads() {
            var page = open()
            for (var p = 0; p < page.patterns.length; p++) {
                for (var k = 0; k < page.patterns[p].kinds.length; k++) {
                    var kind = page.kind
                    if (kind.color !== undefined) {
                        verify(!child(page, "patternArt").visible)
                    } else {
                        var image = child(page, kind.image !== undefined ? "patternImage" : "patternTile")
                        verify(image.visible)
                        tryCompare(image, "status", Image.Ready, 2000,
                                   page.pattern.name + " " + (kind.image || kind.tile) + " loads")
                    }
                    keyClick(Qt.Key_Down)
                }
                keyClick(Qt.Key_Right)
            }
            compare(page.pattern.name, "Grid")
        }

        // ◄ ► through the patterns and round; ▲ ▼ through a pattern's
        // kinds and round, from its first on each pattern; nothing on a
        // pattern of one kind.
        function test_keys() {
            var page = open()
            keyClick(Qt.Key_Left)
            compare(page.pattern.name, "Solid Color")
            compare(page.kind.name, "White")
            keyClick(Qt.Key_Up)
            compare(page.kind.name, "Blue")
            keyClick(Qt.Key_Right)
            compare(page.pattern.name, "Grid")
            keyClick(Qt.Key_Down)
            compare(page.kindIndex, 0)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Right)
            compare(page.pattern.name, "Convergence")
            keyClick(Qt.Key_Down)
            compare(page.title, "Convergence: Crosses (2/5)")
            verify(findText(page, "Convergence: Crosses (2/5)") !== null)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // The name, what it is for and the keys over the pattern as it
        // opens, gone after a few seconds; back with a change or select,
        // and select hides them again.
        function test_info() {
            var page = open()
            var info = child(page, "patternInfo")
            verify(info.visible)
            verify(findText(page, "[◄►]:PATTERN [ENTER]:HIDE [ESC]:BACK") !== null)
            verify(findText(page, "red squares just show") !== null)
            tryCompare(info, "visible", false, 6000)
            keyClick(Qt.Key_Right)
            verify(info.visible)
            keyClick(Qt.Key_Return)
            verify(!info.visible)
            keyClick(Qt.Key_Return)
            verify(info.visible)
            keyClick(Qt.Key_Right)
            verify(findText(page, "[◄►]:PATTERN [▲▼]:KIND [ENTER]:HIDE [ESC]:BACK") !== null)
        }
    }
}
