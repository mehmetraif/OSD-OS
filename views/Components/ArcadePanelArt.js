.pragma library

// The Arcade Buttons page's drawing (ArcadePanel.qml): an arcade stick in
// two-colour isometric pixel art, after Sega's Dreamcast Arcade Stick. A box
// with a plate set in its top: on the plate a Japanese ball-top stick (a
// Sanwa JLF's) on the left, six snap-in buttons (Sanwa OBSF-30s) in Sega's
// layout on the right, two rows of three that follow the fingers (the first
// column nearer the player, the second the farthest), and a memory card's
// window behind them; COIN and START, two small buttons, at the back right.
// Any of them can be lit: a button, COIN or START.
//
// draw() lays it out on a grid of cells, one art pixel each: 0 for none
// (the ground the host draws on shows), 1 for ink, 2 for the ground drawn
// over something behind. The host paints the ink in its text colour, so the
// drawing takes the theme's two colours. Ink is light: what faces the light
// (from the top left) has more of it.

// The grid.
var width = 104
var height = 72

// Isometric 2:1: along the stick (u, the player's left to right) a cell
// down for every two right, into it (v, away from the player) a cell up for
// every two right, z straight up, all in cells. Origin: the top's front left
// corner.
var originX = 1
var originY = 29

function project(u, v, z) {
    return { x: originX + 2 * u + 2 * v, y: originY + u - v - (z || 0) }
}

// The box: its length, depth and height, its corners rounded off; the plate
// set in its top, the rim round it (left and right, front, back).
var boxLength = 30
var boxDepth = 21
var boxHeight = 8
var corner = 1.2
var plate = { left: 1.6, right: 1.6, front: 3.4, back: 1.6 }

// Where things stand on the top (u, v). The buttons' columns follow the
// fingers: the first a little nearer the player, the second the farthest,
// the third a little nearer again.
var stick = { u: 7, v: 11.5 }
var columns = [14.8, 20.5, 26.2]
var bend = [-1.5, 0, -0.6]
var rows = [13.8, 7.8]        // the top row (1 2 3), the bottom (4 5 6)
var coin = { u: 23.4, v: 18.3 }
var start = { u: 26.8, v: 18.3 }
var memoryCard = { u: 10, v: 18.1, length: 5, depth: 2.6 }

// The sprites: "#" ink, "o" ground, "." nothing; each with the cell it
// stands on (its anchor). A button's plunger, a short cylinder with the
// shine on its top, lit (ink, its shine cut out) or not; COIN's and START's,
// smaller.
var button = {
    anchor: { x: 5, y: 4 },
    off: ["...#####...",
          ".##ooooo##.",
          "#oo##ooooo#",
          "##ooooooo##",
          "#o#######o#",
          ".##ooooo##.",
          "...#####..."],
    on:  ["...#####...",
          ".#########.",
          "##oo#######",
          "#o#######o#",
          "##ooooooo##",
          ".#########.",
          "...#####..."]
}
var smallButton = {
    anchor: { x: 3, y: 3 },
    off: ["..###..",
          "##ooo##",
          "#o###o#",
          "#ooooo#",
          ".#####."],
    on:  ["..###..",
          "##o####",
          "##ooo##",
          "#######",
          ".#####."]
}

// The ball: lit from the top left, its shine in a dither that thins away
// from it.
function ballSprite(d) {
    var r = d / 2, lines = []
    var bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
    var inside = function(x, y) {
        var dx = x + 0.5 - r, dy = y + 0.5 - r
        return dx * dx + dy * dy <= r * r
    }
    for (var y = 0; y < d; y++) {
        var line = ""
        for (var x = 0; x < d; x++) {
            if (!inside(x, y)) {
                line += "."
            } else if (!inside(x - 1, y) || !inside(x + 1, y) || !inside(x, y - 1) || !inside(x, y + 1)) {
                line += "#"
            } else {
                var nx = (x + 0.5 - r) / r, ny = (y + 0.5 - r) / r
                var nz = Math.sqrt(Math.max(0, 1 - nx * nx - ny * ny))
                var lum = (nx * -0.55 + ny * -0.6 + nz * 0.58) / 0.9993
                var t = Math.max(0, (lum - 0.35) / 0.65)
                line += bayer[y % 4][x % 4] < t * 16 ? "#" : "o"
            }
        }
        lines.push(line)
    }
    return lines
}
var ballDiameter = 15
var ball = ballSprite(ballDiameter)
var shaftHeight = 10

// Where each of the cabinet's buttons stands, "1" to "6" from 0.
function buttonAt(index) {
    var column = index % 3
    return { u: columns[column], v: rows[index < 3 ? 0 : 1] + bend[column] }
}

function Grid(w, h) {
    this.w = w
    this.h = h
    this.cells = new Uint8Array(w * h)
}
Grid.prototype.set = function(x, y, value) {
    if (x >= 0 && y >= 0 && x < this.w && y < this.h)
        this.cells[y * this.w + x] = value
}
Grid.prototype.get = function(x, y) {
    return x >= 0 && y >= 0 && x < this.w && y < this.h ? this.cells[y * this.w + x] : 0
}
// Every cell of a shape (a test of a cell's centre) in its box [x0, y0, x1,
// y1]: fill(x, y) says what each becomes.
Grid.prototype.paint = function(shape, fill) {
    var box = shape.box
    for (var y = Math.max(0, Math.floor(box[1])); y <= Math.min(this.h - 1, Math.ceil(box[3])); y++) {
        for (var x = Math.max(0, Math.floor(box[0])); x <= Math.min(this.w - 1, Math.ceil(box[2])); x++) {
            if (shape.inside(x + 0.5, y + 0.5))
                this.set(x, y, fill(x, y))
        }
    }
}
// Its outline: the cells of it with a neighbour (above, below, either
// side) that isn't.
Grid.prototype.edge = function(shape) {
    var inside = shape.inside
    this.paint(shape, function(x, y) {
        var cx = x + 0.5, cy = y + 0.5
        return !inside(cx - 1, cy) || !inside(cx + 1, cy) || !inside(cx, cy - 1) || !inside(cx, cy + 1) ? 1 : this.get(x, y)
    }.bind(this))
}
Grid.prototype.line = function(a, b, value) {
    var x0 = Math.round(a.x), y0 = Math.round(a.y), x1 = Math.round(b.x), y1 = Math.round(b.y)
    var dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0)
    var sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1
    var err = dx + dy
    for (;;) {
        this.set(x0, y0, value)
        if (x0 === x1 && y0 === y1)
            return
        var e2 = 2 * err
        if (e2 >= dy) { err += dy; x0 += sx }
        if (e2 <= dx) { err += dx; y0 += sy }
    }
}
// Corners round a box (left, top, right, bottom), as a camera's focus
// frame: what is lit is marked so beside the rest.
Grid.prototype.frame = function(l, t, r, b) {
    var arm = 3
    for (var i = 0; i < arm; i++) {
        this.set(l + i, t, 1); this.set(l, t + i, 1)
        this.set(r - i, t, 1); this.set(r, t + i, 1)
        this.set(l + i, b, 1); this.set(l, b - i, 1)
        this.set(r - i, b, 1); this.set(r, b - i, 1)
    }
}
// A sprite with its anchor on the cell at (x, y).
Grid.prototype.sprite = function(lines, anchor, x, y) {
    var left = Math.round(x) - anchor.x, top = Math.round(y) - anchor.y
    for (var r = 0; r < lines.length; r++) {
        for (var c = 0; c < lines[r].length; c++) {
            var ch = lines[r][c]
            if (ch !== ".")
                this.set(left + c, top + r, ch === "#" ? 1 : 2)
        }
    }
}

// A polygon of points ({ x, y }), as a test of a point.
function polygon(points) {
    var box = [Infinity, Infinity, -Infinity, -Infinity]
    for (var i = 0; i < points.length; i++) {
        box[0] = Math.min(box[0], points[i].x); box[1] = Math.min(box[1], points[i].y)
        box[2] = Math.max(box[2], points[i].x); box[3] = Math.max(box[3], points[i].y)
    }
    return {
        box: box,
        inside: function(x, y) {
            var odd = false
            for (var i = 0, j = points.length - 1; i < points.length; j = i++) {
                var a = points[i], b = points[j]
                if ((a.y > y) !== (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
                    odd = !odd
            }
            return odd
        }
    }
}
function ellipse(cx, cy, rx, ry) {
    return {
        box: [cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1],
        inside: function(x, y) {
            var dx = (x - cx) / rx, dy = (y - cy) / ry
            return dx * dx + dy * dy <= 1
        }
    }
}
// A rectangle on the top, (u0, v0) to (u1, v1), its corners cut by c: the
// points round it from the front left, at height z.
function outline(u0, v0, u1, v1, c, z) {
    return [project(u0 + c, v0, z), project(u1 - c, v0, z), project(u1, v0 + c, z), project(u1, v1 - c, z),
            project(u1 - c, v1, z), project(u0 + c, v1, z), project(u0, v1 - c, z), project(u0, v0 + c, z)]
}

// Two-colour shading: ink on a share of the cells, in an even pattern.
function ground() { return 2 }
function half(x, y) { return (x + y) % 2 === 0 ? 1 : 2 }
function quarter(x, y) { return x % 2 === 0 && y % 2 === 0 ? 1 : 2 }
function sixteenth(x, y) { return x % 4 === 0 && y % 4 === 0 ? 1 : 2 }

// The stick (Sanwa JLF with its ball top): the dust washer round the shaft,
// the shaft, the ball.
function drawStick(grid) {
    var c = project(stick.u, stick.v, 0)
    var washer = ellipse(c.x, c.y, 6.5, 3.2)
    grid.paint(washer, half)
    grid.edge(washer)
    var hole = ellipse(c.x, c.y, 2.5, 1.2)
    grid.paint(hole, ground)
    for (var z = 0; z < shaftHeight; z++) {
        grid.set(Math.round(c.x) - 1, Math.round(c.y) - z, 1)
        grid.set(Math.round(c.x), Math.round(c.y) - z, 1)
    }
    grid.sprite(ball, { x: Math.floor(ballDiameter / 2), y: ballDiameter - 1 }, c.x, c.y - shaftHeight + 2)
}

// The memory card's window behind the buttons: its frame, and the card's
// slot along its front.
function drawMemoryCard(grid) {
    var m = memoryCard
    var u0 = m.u - m.length / 2, u1 = m.u + m.length / 2, v0 = m.v - m.depth / 2, v1 = m.v + m.depth / 2
    var corners = [project(u0, v0, 0), project(u1, v0, 0), project(u1, v1, 0), project(u0, v1, 0)]
    grid.paint(polygon(corners), ground)
    for (var i = 0; i < 4; i++)
        grid.line(corners[i], corners[(i + 1) % 4], 1)
    grid.line(project(u0 + 1, v0 + 1, 0), project(u1 - 1, v0 + 1, 0), 1)
}

// lit: the inputs to light, by their names ("1" to "6", "coin", "start").
function draw(lit) {
    var on = {}
    for (var i = 0; lit && i < lit.length; i++)
        on[lit[i]] = true
    var grid = new Grid(width, height)
    var L = boxLength, D = boxDepth, H = boxHeight

    // The box's sides that face the viewer: the front and the rounded
    // corner in the light, the right end in less.
    var top = outline(0, 0, L, D, corner, 0)
    var bottom = outline(0, 0, L, D, corner, -H)
    var sides = [[0, 1, half], [1, 2, half], [2, 3, quarter]]
    for (var s = 0; s < sides.length; s++) {
        var a = sides[s][0], b = sides[s][1]
        var side = polygon([top[a], top[b], bottom[b], bottom[a]])
        grid.paint(side, sides[s][2])
        grid.edge(side)
    }
    // The front's lip, and the notch in its foot.
    grid.line(project(corner, 0, -2.5), project(L - corner, 0, -2.5), 1)
    var notch = [project(L / 2 - 2.5, 0, -H), project(L / 2, 0, -H + 3.5), project(L / 2 + 2.5, 0, -H)]
    grid.paint(polygon(notch), ground)
    grid.line(notch[0], notch[1], 1)
    grid.line(notch[1], notch[2], 1)

    // The top: the rim, the plate set in it with its screws, the memory
    // card's window.
    var deck = polygon(top)
    grid.paint(deck, ground)
    grid.edge(deck)
    var face = polygon(outline(plate.left, plate.front, L - plate.right, D - plate.back, corner * 0.6, 0))
    grid.paint(face, sixteenth)
    grid.edge(face)
    var screws = [[plate.left + 1.1, plate.front + 1.1], [L - plate.right - 1.1, plate.front + 1.1],
                  [plate.left + 1.1, D - plate.back - 1.1]]
    for (var w = 0; w < screws.length; w++) {
        var sc = project(screws[w][0], screws[w][1], 0)
        grid.set(Math.round(sc.x), Math.round(sc.y), 1)
        grid.set(Math.round(sc.x) + 1, Math.round(sc.y), 1)
    }
    drawMemoryCard(grid)

    // What stands on it, from the back: whatever is higher up the screen is
    // farther away.
    var small = function(name, at) {
        return { at: at, draw: function() {
            var c = project(at.u, at.v, 0)
            grid.sprite(on[name] ? smallButton.on : smallButton.off, smallButton.anchor, c.x, c.y)
        } }
    }
    var things = [{ at: stick, draw: function() { drawStick(grid) } }, small("coin", coin), small("start", start)]
    for (var b = 0; b < 6; b++) {
        (function(index) {
            var at = buttonAt(index)
            things.push({ at: at, draw: function() {
                var c = project(at.u, at.v, 0)
                grid.sprite(on[String(index + 1)] ? button.on : button.off, button.anchor, c.x, c.y)
            } })
        })(b)
    }
    things.sort(function(a, b) { return (a.at.u - a.at.v) - (b.at.u - b.at.v) })
    for (var t = 0; t < things.length; t++)
        things[t].draw()

    // The focus frame round each lit one.
    var gap = 2
    var framed = function(sprite, at) {
        var c = project(at.u, at.v, 0)
        var left = Math.round(c.x) - sprite.anchor.x, top = Math.round(c.y) - sprite.anchor.y
        grid.frame(left - gap, top - gap, left + sprite.off[0].length - 1 + gap, top + sprite.off.length - 1 + gap)
    }
    for (var n = 0; n < 6; n++) {
        if (on[String(n + 1)])
            framed(button, buttonAt(n))
    }
    if (on["coin"])
        framed(smallButton, coin)
    if (on["start"])
        framed(smallButton, start)
    return { width: width, height: height, cells: grid.cells }
}
