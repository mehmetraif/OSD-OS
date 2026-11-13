import QtQuick
import QtTest

// Settings → Screen → Test Patterns, views/TestPatterns.qml: the 240p Test Suite's
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

        // On a screen of 288 or 576 lines (or any other a whole number of the
        // Grid's 16-line squares, but 240 and 480), the Grid is drawn on them
        // one for one, as the suite draws it: squares of 16 with a border a
        // line wide and a 2 × 2 dot, red round the edge, white inside, over
        // the screen's height and 4:3 across. Elsewhere, the suite's picture.
        function test_drawnGrid() {
            var sizes = [[288, 24, 18], [576, 48, 36]]
            for (var i = 0; i < sizes.length; i++) {
                loader.source = ""
                root.sh = sizes[i][0]
                var page = open()
                compare(page.kind.draw, "grid")
                compare(page.kind.w, sizes[i][1] * 16)
                compare(page.kind.h, sizes[i][2] * 16)
                var art = child(page, "patternArt")
                compare(art.scale, 1)
                var canvas = child(page, "patternDrawn")
                verify(canvas.visible)
                verify(!child(page, "patternImage").visible)
                // The canvas's own pixels, as drawn: the corner square red,
                // its dot red; the next square white, its middle black and
                // its dot white; the edge's last square red at the foot.
                var w = sizes[i][1] * 16, h = sizes[i][2] * 16
                tryVerify(function() { return canvas.available }, 3000)
                var ctx = canvas.getContext("2d")
                function at(x, y) {
                    var d = ctx.getImageData(x, y, 1, 1).data
                    return [d[0], d[1], d[2]]
                }
                var red = [255, 0, 0], white = [255, 255, 255], black = [0, 0, 0]
                tryVerify(function() { return at(0, 0)[0] === 255 }, 3000, "the grid is drawn")
                compare(at(0, 0), red)
                compare(at(7, 7), red)
                compare(at(16, 16), white)
                compare(at(19, 19), black)
                compare(at(23, 24), white)
                compare(at(31, 20), white)
                compare(at(w - 1, h - 1), red)
                compare(at(w - 9, h - 9), red)
            }
            // A screen of 1080 lines isn't a whole number of squares: the
            // suite's 640 × 480 picture, scaled.
            loader.source = ""
            root.sh = 1080
            page = open()
            compare(page.kind.image, "grid-480.png")
            verify(!child(page, "patternDrawn").visible)
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
