import QtQuick
import "ArcadePanelArt.js" as Art

// A two-player arcade cabinet's control panel, with a little of the cabinet
// above and below it, drawn in the theme's two colours: each player's
// ball-top stick and six buttons in two rows of three, and 1P START, COIN
// and 2P START on its front (ArcadePanelArt.js draws it). lit: the inputs
// shown lit, "1" to "6", "coin" and "start" (Arcade Buttons' names), on
// player's side: 2 the right, else the left. Button 7 and 8 aren't on it.
//
// Each of the drawing's cells is cell screen pixels square, so it keeps its
// pixels whole: root.px, a 240-line picture's pixel, by default.
Canvas {
    id: panel

    property var lit: []
    property int player: 1
    property int cell: root.px
    property color ink: root.primaryColor

    width: Art.width * cell
    height: Art.height * cell
    antialiasing: false

    onLitChanged: requestPaint()
    onPlayerChanged: requestPaint()
    onInkChanged: requestPaint()
    onCellChanged: requestPaint()
    // A canvas paints whether or not it is shown: one in a hidden prompt
    // (the capture's, the test's) is drawn once that opens, not at every
    // press meanwhile.
    onVisibleChanged: if (visible) requestPaint()

    // The ink a row at a time, each run of it in one rectangle.
    onPaint: {
        if (!visible)
            return
        var ctx = getContext("2d")
        ctx.reset()
        var art = Art.draw(lit || [], player)
        ctx.fillStyle = panel.ink
        for (var y = 0; y < art.height; y++) {
            var x = 0
            while (x < art.width) {
                if (art.cells[y * art.width + x] !== 1) {
                    ++x
                    continue
                }
                var end = x
                while (end < art.width && art.cells[y * art.width + end] === 1)
                    ++end
                ctx.fillRect(x * cell, y * cell, (end - x) * cell, cell)
                x = end
            }
        }
    }
}
