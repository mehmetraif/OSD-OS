import QtQuick

// Settings → Screen → Test Patterns: pictures to set a TV or monitor up by, its own
// knobs or service menu, from Artemio Urbina's 240p Test Suite
// (assets/testpatterns, GPL v2 or later; its README says which file came
// from where).
//
// ◄ ► go through the patterns and ▲ ▼ through a pattern's kinds
// (Convergence's lines, crosses, dots and colours; Solid Color's colours).
// The name, what the pattern is for and the keys show over it for a few
// seconds as it opens and changes, and select shows or hides them; back
// leaves. A pattern is the suite's picture of 320 × 240 (640 × 480 on a
// screen of 400 lines or more, where the suite has one), drawn pixel for
// pixel, scaled whole to the screen's height in the 4:3 middle of it: on a
// tube that is the whole picture, which the Grid's red squares should just
// reach. On a screen of other lines that are a whole number of the Grid's
// 16-line squares (288p, 576i, 384, 720), the Grid is drawn here instead,
// square for square as the suite draws it, one line of it to each of the
// screen's, so no line of it is doubled where others aren't. Nothing moves
// or is drawn over it: the screen effects rest and the screen saver waits
// while it is up, so it can be left on while the set is adjusted.
FocusScope {
    id: patternsRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})

    // The suite's 640 × 480 pictures, where it has them, on a screen with the
    // lines for them.
    readonly property bool tall: root.sh >= 400
    // The screen's lines, and whether the Grid is drawn on them one for one:
    // where they are a whole number of its 16-line squares, but for 240 and
    // 480, which have the suite's own pictures.
    readonly property int lines: Math.round(root.sh)
    readonly property bool drawnGrid: lines > 0 && lines % 16 === 0 && lines !== 240 && lines !== 480
    readonly property int gridRows: Math.round(lines / 16)
    readonly property int gridColumns: Math.round(gridRows * 4 / 3)
    readonly property string folder: "../assets/testpatterns/"

    // Each pattern: its name, what to set with it (the suite's help, put
    // shortly), and its kinds: a picture (`image`, `w` × `h` of it, the rest
    // of a texture left out), a tile repeated over a 320 × 240 picture
    // (`tile`), the Grid drawn here (`draw`, `w` × `h`), or a colour over the
    // whole screen (`color`).
    readonly property var patterns: [
        { name: "Grid",
          help: "The whole 4:3 picture. Set the screen's size and position until the red squares just show at every"
                + " edge, every square whole and square, with no bend or slant anywhere",
          kinds: [ drawnGrid ? { draw: "grid", w: gridColumns * 16, h: lines }
                   : tall ? { image: "grid-480.png", w: 640, h: 480 } : { image: "grid-240.png", w: 320, h: 240 } ] },
        { name: "Monoscope",
          help: "A tube's setup: the centre cross for static convergence, the sides for the edges', the corners for"
                + " corner convergence, purity and geometry. The red square is square on a screen set right:"
                + " measure it, and set the height and width until its sides match",
          kinds: [ tall ? { image: "monoscope-480.png", w: 640, h: 480 } : { image: "monoscope-240.png", w: 320, h: 240 } ] },
        { name: "Convergence",
          help: "Red, green and blue land on one another: no coloured fringe on the white lines, crosses or dots,"
                + " none where the colours meet. Set it at a lowered brightness",
          kinds: [ { name: "Lines", tile: "convergence-grid.png" },
                   { name: "Crosses", tile: "convergence-cross.png" },
                   { name: "Dots", tile: "convergence-dots.png" },
                   { name: "Colors", tile: "convergence-colors.png" },
                   { name: "Colors on Black", tile: "convergence-colors-black.png" } ] },
        { name: "PLUGE",
          help: "Black level: lower Brightness until the bars at the sides are gone, raise it until they just show,"
                + " then back one step, where they blend with the black",
          kinds: [ { image: "pluge.png", w: 320, h: 240 } ] },
        { name: "Color Bars",
          help: "White level, then each colour's: raise Contrast until the brightest steps at the right of a bar run"
                + " together, then lower it until they can be told apart. The darkest steps at the left just show"
                + " once the black level is set",
          kinds: [ { image: "colorbars.png", w: 320, h: 240 } ] },
        { name: "SMPTE Color Bars",
          help: "Colour and tint for video: with a blue filter or the set's blue-only mode, the bars and the blocks"
                + " under them should match",
          kinds: [ { image: "smpte.png", w: 320, h: 240 } ] },
        { name: "Gray Ramp",
          help: "Colour balance: every bar grey, with no red or blue in it. White level with Contrast: the two"
                + " whitest bars just told apart",
          kinds: [ { image: "grayramp.png", w: 320, h: 240 } ] },
        { name: "Sharpness",
          help: "Set Sharpness (aperture on a monitor) so black and grey meet cleanly, without a white ghost between"
                + " them: on most sets at zero or the middle",
          kinds: [ { image: "sharpness.png", w: 320, h: 240 } ] },
        { name: "Solid Color",
          help: "Purity and white balance: one colour, even from corner to corner, with no patch of another",
          kinds: [ { name: "White", color: "#ffffff" }, { name: "Black", color: "#000000" },
                   { name: "Red", color: "#ff0000" }, { name: "Green", color: "#00ff00" },
                   { name: "Blue", color: "#0000ff" } ] }
    ]

    property int patternIndex: 0
    property int kindIndex: 0
    readonly property var pattern: patterns[patternIndex]
    // Within the pattern's kinds while the two indexes change one after
    // the other.
    readonly property var kind: pattern.kinds[Math.min(kindIndex, pattern.kinds.length - 1)]
    readonly property string title: pattern.name + (pattern.kinds.length > 1
        ? ": " + kind.name + " (" + (pattern.kinds.indexOf(kind) + 1) + "/" + pattern.kinds.length + ")" : "")
    property bool infoShown: true

    function showPattern(index) {
        kindIndex = 0
        patternIndex = (index + patterns.length) % patterns.length
        showInfo()
    }
    function showKind(index) {
        var count = pattern.kinds.length
        if (count < 2)
            return
        kindIndex = (index + count) % count
        showInfo()
    }
    // The name and the keys for a few seconds, then the pattern alone.
    function showInfo() {
        infoShown = true
        infoTimer.restart()
    }

    Timer {
        id: infoTimer
        interval: 4000
        onTriggered: patternsRoot.infoShown = false
    }

    // Nothing over the pattern or moving it while it is up: the screen
    // effects rest, and the screen saver waits.
    Component.onCompleted: {
        root.restFor(patternsRoot, true)
        if (typeof idleTracker !== "undefined" && idleTracker)
            idleTracker.viewActive = true
        forceActiveFocus()
        infoTimer.start()
    }
    Component.onDestruction: {
        root.restFor(patternsRoot, false)
        if (typeof idleTracker !== "undefined" && idleTracker) {
            idleTracker.viewActive = false
            idleTracker.resetActivity()
        }
    }

    Keys.onPressed: function(event) {
        if (event.modifiers & Qt.ControlModifier)
            return
        switch (event.key) {
        case Qt.Key_Left: patternsRoot.showPattern(patternsRoot.patternIndex - 1); break
        case Qt.Key_Right: patternsRoot.showPattern(patternsRoot.patternIndex + 1); break
        case Qt.Key_Up: patternsRoot.showKind(patternsRoot.kindIndex - 1); break
        case Qt.Key_Down: patternsRoot.showKind(patternsRoot.kindIndex + 1); break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (patternsRoot.infoShown) {
                infoTimer.stop()
                patternsRoot.infoShown = false
            } else {
                patternsRoot.showInfo()
            }
            break
        case Qt.Key_Escape:
        case Qt.Key_Backspace:
        case Qt.Key_Back:
            patternsRoot.goBack()
            break
        default:
            return
        }
        event.accepted = true
    }

    // Black round the 4:3 picture on a wider screen; a solid colour over all
    // of it.
    Rectangle {
        anchors.fill: parent
        color: patternsRoot.kind.color !== undefined ? patternsRoot.kind.color : "#000000"
    }

    // The picture, at its own size, scaled whole to the screen's height.
    Item {
        id: art
        objectName: "patternArt"
        visible: patternsRoot.kind.color === undefined
        readonly property int artW: patternsRoot.kind.w || 320
        readonly property int artH: patternsRoot.kind.h || 240
        width: artW
        height: artH
        anchors.centerIn: parent
        scale: parent.height / artH
        transformOrigin: Item.Center

        Image {
            objectName: "patternImage"
            anchors.fill: parent
            visible: patternsRoot.kind.image !== undefined
            source: visible ? Qt.resolvedUrl(patternsRoot.folder + patternsRoot.kind.image) : ""
            // Only the picture: the rest of a texture's width and height left
            // out.
            sourceClipRect: Qt.rect(0, 0, art.artW, art.artH)
            smooth: false
            mipmap: false
            cache: false
        }
        // The Grid on the screen's own lines, as the suite draws it: squares
        // of 16 × 16, each with its own border a line wide and a dot of 2 × 2
        // in its middle, white, and red round the edge.
        Canvas {
            id: gridCanvas
            objectName: "patternDrawn"
            anchors.fill: parent
            visible: patternsRoot.kind.draw === "grid"
            smooth: false
            antialiasing: false
            onVisibleChanged: if (visible) requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.fillStyle = "#000000"
                ctx.fillRect(0, 0, width, height)
                var columns = Math.round(width / 16)
                var rows = Math.round(height / 16)
                for (var r = 0; r < rows; r++) {
                    for (var c = 0; c < columns; c++) {
                        var x = c * 16
                        var y = r * 16
                        var edge = r === 0 || c === 0 || r === rows - 1 || c === columns - 1
                        ctx.fillStyle = edge ? "#ff0000" : "#ffffff"
                        ctx.fillRect(x, y, 16, 1)
                        ctx.fillRect(x, y + 15, 16, 1)
                        ctx.fillRect(x, y + 1, 1, 14)
                        ctx.fillRect(x + 15, y + 1, 1, 14)
                        ctx.fillRect(x + 7, y + 7, 2, 2)
                    }
                }
            }
        }
        Image {
            objectName: "patternTile"
            anchors.fill: parent
            visible: patternsRoot.kind.tile !== undefined
            source: visible ? Qt.resolvedUrl(patternsRoot.folder + patternsRoot.kind.tile) : ""
            fillMode: Image.Tile
            horizontalAlignment: Image.AlignLeft
            verticalAlignment: Image.AlignTop
            smooth: false
        }
    }

    // What the pattern is and the keys, over its foot, for a few seconds.
    Rectangle {
        id: info
        objectName: "patternInfo"
        visible: patternsRoot.infoShown
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: parent.height * 0.06
        width: parent.width * 0.86
        height: infoColumn.implicitHeight + parent.height * 0.04
        color: "#d8000000"
        border.color: "#ffffff"
        border.width: Math.max(1, root.px)

        Column {
            id: infoColumn
            anchors.centerIn: parent
            width: parent.width - root.sw * 0.04
            spacing: root.sh * 0.01
            Text {
                width: parent.width
                text: patternsRoot.title
                color: "#ffffff"
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.045
                font.capitalization: Font.AllUppercase
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: patternsRoot.pattern.help
                color: "#ffffff"
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.03
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                width: parent.width
                text: root.hints.change + ":PATTERN "
                      + (patternsRoot.pattern.kinds.length > 1 ? root.hints.navigate + ":KIND " : "")
                      + root.hints.select + ":HIDE " + root.hints.back + ":BACK"
                color: "#ffffff"
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.03
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }
}
