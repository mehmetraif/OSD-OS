import QtQuick

// The VHS cassette on the boot screen, the owner's drawing (made with ChatGPT),
// its reels turning and its tape winding off the left (supply) reel onto the
// right (take-up) one as `progress` goes from 0 to 1.
//
// The drawing's shell is assets/images/cassette-shell.png, the drawing
// (cassette.png: white where it is ink, with its own edges) with its two
// windows cut out, drawn in the theme's text colour. What shows through the
// windows is painted under it on a grid of cells three of the drawing's pixels
// wide, so it keeps the drawing's stepped edges: the shell behind the tape, the
// tape as a disc as large as the tape the reel holds, the hub with its ten
// holes, square and round in turn, and the spindle. Both reels turn the way a
// deck's do in play, the one with less tape on it the faster. The slash on the
// label is painted over it all in its three colours.
//
// The host sizes it (width and height), and the drawing is stretched to that,
// so on a screen whose pixels are not square (720×480 on a 4:3 tube) it
// keeps its shape on the glass.
Item {
    id: cassette

    // 0 = all of the tape on the left reel, 1 = all of it on the right.
    property real progress: 0
    // Whether the reels turn.
    property bool running: true
    // The colours drawn: the host sets them to the theme's text colour and its
    // background, which is the colour of the tape and of the holes.
    property color ink: "#ffffff"
    property color ground: "#0110c5"

    // The drawing's frame, in its pixels, and the grid the windows are painted
    // on: `cell` of its pixels to a cell.
    readonly property real artWidth: 612
    readonly property real artHeight: 284
    readonly property int cell: 3
    readonly property int columns: artWidth / cell
    readonly property int rows: Math.ceil(artHeight / cell)

    // Each window's cells, from the drawing: from `firstRow` down, a
    // [first, last] column for each row. They are painted a cell further out
    // all round, under the frame around the window (which is at least two
    // cells wide), so that the frame's smoothed edge lies over what the window
    // shows. A reel's hub is on the window's inner edge, so a window shows the
    // half of its reel on its side (`side`: -1 left of the hub, 1 right of it).
    readonly property int firstRow: 32
    readonly property var leftSpans: [
        [19, 49], [18, 49], [17, 48], [16, 48], [15, 48], [15, 49], [14, 49], [13, 49],
        [13, 49], [12, 49], [11, 49], [11, 49], [11, 49], [11, 49], [10, 49], [10, 49],
        [10, 49], [10, 49], [10, 49], [10, 49], [10, 49], [10, 49], [10, 49], [10, 49],
        [10, 49], [10, 49], [10, 49], [10, 49], [10, 49], [10, 49], [10, 49], [10, 49],
        [10, 49], [11, 49], [11, 49], [11, 49], [11, 49], [12, 49], [13, 49], [13, 49],
        [14, 49], [15, 49], [16, 49], [17, 49], [18, 49], [18, 49], [19, 48]]
    readonly property var rightSpans: [
        [155, 184], [155, 186], [155, 186], [155, 187], [155, 188], [155, 189], [155, 190], [155, 190],
        [155, 191], [155, 191], [155, 192], [155, 192], [155, 192], [155, 192], [155, 193], [155, 193],
        [155, 193], [155, 193], [155, 193], [155, 193], [155, 193], [155, 193], [155, 193], [155, 193],
        [155, 193], [155, 193], [155, 193], [155, 193], [155, 193], [155, 193], [155, 193], [155, 193],
        [155, 193], [155, 192], [155, 192], [155, 192], [155, 192], [155, 191], [155, 190], [155, 190],
        [155, 189], [155, 188], [155, 187], [155, 187], [155, 185], [155, 184], [155, 184]]
    readonly property real leftReelX: 149
    readonly property real rightReelX: 463
    readonly property real reelY: 167.5
    // The reel's parts, as radii: the spindle's hole, the hub, the ring its
    // holes are on, and the tape, full as drawn and down to a turn or two on
    // an empty reel.
    readonly property real spindleRadius: 18.5
    readonly property real hubRadius: 50
    readonly property real holeRadius: 40.5
    readonly property real fullRadius: 121
    readonly property real emptyRadius: 57
    // How fast the tape runs, in the drawing's pixels a second: a reel turns
    // at this over its radius, a turn every four seconds when it is empty.
    readonly property real tapeSpeed: emptyRadius * Math.PI / 2

    // The slash on the label: three stripes `stripe` wide, from `slashX` on
    // its top row, a pixel to the left for every two rows down.
    readonly property real slashTop: 148
    readonly property real slashBottom: 188
    readonly property real slashX: 327
    readonly property real stripe: 5
    readonly property var slashColours: ["#ff3d3d", "#2f6bff", "#2fe063"]

    implicitWidth: artWidth
    implicitHeight: artHeight

    // Tape radius on a reel holding `share` of the tape. The tape's area is
    // conserved, so the radius follows the square root, not a straight line.
    function reelRadius(share) {
        var e = emptyRadius * emptyRadius
        var f = fullRadius * fullRadius
        return Math.sqrt(e + Math.max(0, Math.min(1, share)) * (f - e))
    }
    readonly property real leftRadius: reelRadius(1 - progress)
    readonly property real rightRadius: reelRadius(progress)

    // How far each reel has turned, in radians. Seen from above, a deck turns
    // both reels anticlockwise in play.
    property real leftTurn: 0
    property real rightTurn: 0

    Timer {
        interval: 80
        repeat: true
        running: cassette.running && cassette.visible
        onTriggered: {
            var seconds = interval / 1000
            cassette.leftTurn = (cassette.leftTurn - cassette.tapeSpeed / cassette.leftRadius * seconds) % (2 * Math.PI)
            cassette.rightTurn = (cassette.rightTurn - cassette.tapeSpeed / cassette.rightRadius * seconds) % (2 * Math.PI)
            reels.requestPaint()
        }
    }

    // What shows through the windows, a cell to a pixel, scaled up to the
    // drawing's size without smoothing, so each cell stays square.
    Canvas {
        id: reels
        width: cassette.columns
        height: cassette.rows
        smooth: false
        antialiasing: false
        transform: Scale {
            xScale: cassette.width / cassette.columns
            yScale: cassette.height * cassette.cell / cassette.artHeight
        }

        // The cells of row `row` from `first` to `last`, kept to [lo, hi].
        function run(ctx, row, first, last, lo, hi, colour) {
            var a = Math.max(first, lo)
            var b = Math.min(last, hi)
            if (b < a)
                return
            ctx.fillStyle = colour
            ctx.fillRect(a, row, b - a + 1, 1)
        }

        // The cells of a row whose centres are within `r` of the hub: on the
        // left reel, the first of them (from the window's outer end); on the
        // right one, the last.
        function reach(cx, dy, r, side) {
            var c = cassette.cell
            if (r <= Math.abs(dy))
                return side < 0 ? Infinity : -Infinity
            var w = Math.sqrt(r * r - dy * dy)
            return side < 0 ? Math.ceil((cx - w) / c - 0.5) : Math.floor((cx + w) / c - 0.5)
        }

        function paintReel(ctx, spans, cx, side, tape, turn) {
            var c = cassette.cell
            var ink = cassette.ink
            var ground = cassette.ground
            for (var i = -1; i <= spans.length; ++i) {
                var row = cassette.firstRow + i
                var span = spans[Math.max(0, Math.min(spans.length - 1, i))]
                var lo = span[0] - 1, hi = span[1] + 1
                var dy = (row + 0.5) * c - cassette.reelY
                // From the window's outer end in to the hub: the shell, the
                // tape, the hub, the spindle's hole.
                var t = reach(cx, dy, tape, side)
                var h = reach(cx, dy, cassette.hubRadius, side)
                var s = reach(cx, dy, cassette.spindleRadius, side)
                if (side < 0) {
                    run(ctx, row, lo, Math.min(t, h, s) - 1, lo, hi, ink)
                    run(ctx, row, t, Math.min(h, s) - 1, lo, hi, ground)
                    run(ctx, row, h, s - 1, lo, hi, ink)
                    run(ctx, row, s, hi, lo, hi, ground)
                } else {
                    run(ctx, row, lo, s, lo, hi, ground)
                    run(ctx, row, Math.max(s + 1, lo), h, lo, hi, ink)
                    run(ctx, row, Math.max(h + 1, lo), t, lo, hi, ground)
                    run(ctx, row, Math.max(t + 1, h + 1, lo), hi, lo, hi, ink)
                }
            }
            // The hub's ten holes, square and round in turn, four cells
            // across, turned with it; a round one is a square without its
            // corners. Each is kept to the window.
            ctx.fillStyle = ground
            for (var k = 0; k < 10; ++k) {
                var a = (side < 0 ? Math.PI : 0) + k * Math.PI / 5 + turn
                var hx = Math.round((cx + cassette.holeRadius * Math.cos(a)) / c - 2)
                var hy = Math.round((cassette.reelY + cassette.holeRadius * Math.sin(a)) / c - 2)
                for (var y = 0; y < 4; ++y) {
                    var r = hy + y - cassette.firstRow
                    if (r < 0 || r >= spans.length)
                        continue
                    for (var x = 0; x < 4; ++x) {
                        if (k % 2 === 1 && (x === 0 || x === 3) && (y === 0 || y === 3))
                            continue
                        var col = hx + x
                        if (col >= spans[r][0] && col <= spans[r][1])
                            ctx.fillRect(col, hy + y, 1, 1)
                    }
                }
            }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            paintReel(ctx, cassette.leftSpans, cassette.leftReelX, -1, cassette.leftRadius, cassette.leftTurn)
            paintReel(ctx, cassette.rightSpans, cassette.rightReelX, 1, cassette.rightRadius, cassette.rightTurn)
        }
    }

    Image {
        anchors.fill: parent
        sourceSize.width: width
        sourceSize.height: height
        source: width > 0 && height > 0
                ? root.iconUrl(cassette.ink, Qt.resolvedUrl("../../assets/images/cassette-shell.png"))
                : ""
    }

    // The slash, over the drawing, in its own pixels.
    Canvas {
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.scale(width / cassette.artWidth, height / cassette.artHeight)
            var drop = (cassette.slashBottom - cassette.slashTop) / 2
            for (var i = 0; i < 3; ++i) {
                var x = cassette.slashX + i * cassette.stripe
                ctx.fillStyle = cassette.slashColours[i]
                ctx.beginPath()
                ctx.moveTo(x, cassette.slashTop)
                ctx.lineTo(x + cassette.stripe, cassette.slashTop)
                ctx.lineTo(x + cassette.stripe - drop, cassette.slashBottom)
                ctx.lineTo(x - drop, cassette.slashBottom)
                ctx.closePath()
                ctx.fill()
            }
        }
    }

    onLeftRadiusChanged: reels.requestPaint()
    onRightRadiusChanged: reels.requestPaint()
    onInkChanged: reels.requestPaint()
    onGroundChanged: reels.requestPaint()
}
