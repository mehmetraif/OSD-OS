import QtQuick
import "ArcadePanelArt.js" as Art

// An arcade stick after Sega's Dreamcast Arcade Stick, drawn in the theme's
// two colours: a ball-top stick, Sega's six buttons in two rows of three,
// and COIN and START at the back (ArcadePanelArt.js draws it). lit: the
// inputs shown lit, "1" to "6", "coin" and "start" (Arcade Buttons' names);
// Button 7 and 8 aren't on it.
//
// Each of the drawing's cells is cell screen pixels square, so it keeps its
// pixels whole: root.px, a 240-line picture's pixel, by default.
Canvas {
    id: panel

    property var lit: []
    property int cell: root.px
    property color ink: root.primaryColor

    width: Art.width * cell
    height: Art.height * cell
    antialiasing: false

    onLitChanged: requestPaint()
    onInkChanged: requestPaint()
    onCellChanged: requestPaint()

    // The ink a row at a time, each run of it in one rectangle.
    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        var art = Art.draw(lit || [])
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
