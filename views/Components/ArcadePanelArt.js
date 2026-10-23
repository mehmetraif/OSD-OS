.pragma library

// The Arcade Buttons page's drawing (ArcadePanel.qml): a two-player arcade
// cabinet's control panel, after a Stella International cabinet's, in
// two-colour isometric pixel art. On its top each player has a ball-top
// stick and six buttons in two rows of three that follow the fingers (the
// first column nearer the player, the second the farthest), player 1 on the
// left and player 2 on the right; on its front, below a chrome strip, three
// round buttons: 1P START, COIN and 2P START. One player's side can be lit:
// a button, COIN or that player's START.
//
// draw() lays it out on a grid of cells, one art pixel each: 0 for none
// (the ground the host draws on shows), 1 for ink, 2 for the ground drawn
// over something behind. The host paints the ink in its text colour, so the
// drawing takes the theme's two colours. Ink is light: what faces the light
// (from above) has more of it.

// The grid.
var width = 103
var height = 65

// Isometric 2:1: along the panel (u, the players' left to right) a cell
// down for every two right, into it (v, away from the players) a cell up for
// every two right, z straight up, all in cells. Origin: the top's front left
// corner.
var originX = 0
var originY = 14

function project(u, v, z) {
    return { x: originX + 2 * u + 2 * v, y: originY + u - v - (z || 0) }
}

// The panel: its length and depth, the height of its front, and how far
// down the front the chrome strip along its top is.
var panelLength = 39
var panelDepth = 12
var frontHeight = 12
var strip = 2

// Where things stand on the top (u, v), from the left end of each player's
// half of it. The buttons' columns follow the fingers: the first a little
// nearer the player, the second the farthest, the third a little nearer
// again.
var playerLength = panelLength / 2
var stick = { u: 3.6, v: 6.1 }
var columns = [8.3, 12.8, 17.3]
var bend = [-0.9, 0, -0.4]
var rows = [8.2, 3.9]        // the top row (1 2 3), the bottom (4 5 6)
// The front's buttons, along it, halfway down from the strip.
var front = { start1: 0.16, coin: 0.5, start2: 0.84 }

// The sprites: "#" ink, "o" ground, "." nothing; each with the cell it
// stands on (its anchor). A button's plunger, a short cylinder with the
// shine on its top, lit (ink, its shine cut out) or not.
var button = {
    anchor: { x: 4, y: 3 },
    off: ["..#####..",
          ".#o##oo#.",
          "#ooooooo#",
          "#o#####o#",
          ".#ooooo#.",
          "..#####.."],
    on:  ["..#####..",
          ".##oo###.",
          "#########",
          "##ooooo##",
          ".#######.",
          "..#####.."]
}
// A round button on the front, which faces the players: a disc in its
// plane, leaning with it, its shine at the top left, lit (ink, the shine
// cut out) or not.
var frontButton = {
    anchor: { x: 3, y: 4 },
    off: [".###...",
          "#ooo#..",
          "#o#oo#.",
          "#o#oo#.",
          "#ooooo#",
          ".#oooo#",
          ".#oooo#",
          "..#ooo#",
          "...###."],
    on:  [".###...",
          "#####..",
          "##o###.",
          "##o###.",
          "#######",
          ".######",
          ".######",
          "..#####",
          "...###."]
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
var ballDiameter = 9
var ball = ballSprite(ballDiameter)
var shaftHeight = 6

// Where player 1's or 2's button stands, "1" to "6" from 0.
function buttonAt(player, index) {
    var column = index % 3
    return { u: (player - 1) * playerLength + columns[column], v: rows[index < 3 ? 0 : 1] + bend[column] }
}
// Where one of the front's buttons is on the grid: "start1", "coin", "start2".
function frontAt(name) {
    return project(front[name] * panelLength, 0, -(strip + frontHeight) / 2)
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
    var arm = 2
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
// The panel's corners at height z, from the front left round to the back
// left.
function corners(z) {
    return [project(0, 0, z), project(panelLength, 0, z), project(panelLength, panelDepth, z), project(0, panelDepth, z)]
}

// Two-colour shading: ink on a share of the cells, in an even pattern.
function ground() { return 2 }
function half(x, y) { return (x + y) % 2 === 0 ? 1 : 2 }
function sixteenth(x, y) { return x % 4 === 0 && y % 4 === 0 ? 1 : 2 }

// A stick (a Sanwa JLF with its ball top): the dust washer round the
// shaft, the shaft, the ball.
function drawStick(grid, at) {
    var c = project(at.u, at.v, 0)
    var washer = ellipse(c.x, c.y, 4.2, 2.1)
    grid.paint(washer, half)
    grid.edge(washer)
    var hole = ellipse(c.x, c.y, 1.6, 0.8)
    grid.paint(hole, ground)
    for (var z = 0; z < shaftHeight; z++) {
        grid.set(Math.round(c.x) - 1, Math.round(c.y) - z, 1)
        grid.set(Math.round(c.x), Math.round(c.y) - z, 1)
    }
    grid.sprite(ball, { x: Math.floor(ballDiameter / 2), y: ballDiameter - 1 }, c.x, c.y - shaftHeight + 2)
}

// lit: the inputs to light, by their names ("1" to "6", "coin", "start"),
// on player's side: 2 the right, else the left.
function draw(lit, player) {
    var on = {}
    for (var i = 0; lit && i < lit.length; i++)
        on[lit[i]] = true
    var side = player === 2 ? 2 : 1
    var grid = new Grid(width, height)

    // The front and the right end, in shade, the strip along the front's
    // top and round the end; the top over them.
    var top = corners(0), foot = corners(-frontHeight), band = corners(-strip)
    for (var s = 0; s < 2; s++) {
        var face = polygon([top[s], top[s + 1], foot[s + 1], foot[s]])
        grid.paint(face, ground)
        grid.edge(face)
        grid.line(band[s], band[s + 1], 1)
    }
    var deck = polygon(top)
    grid.paint(deck, sixteenth)
    grid.edge(deck)

    // The front's buttons: 1P START, COIN, 2P START.
    var start = side === 2 ? "start2" : "start1"
    var fronts = ["start1", "coin", "start2"]
    for (var f = 0; f < fronts.length; f++) {
        var name = fronts[f]
        var c = frontAt(name)
        var lightIt = name === "coin" ? on["coin"] : name === start && on["start"]
        grid.sprite(lightIt ? frontButton.on : frontButton.off, frontButton.anchor, c.x, c.y)
    }

    // What stands on the top, from the back: whatever is higher up the
    // screen is farther away.
    var things = []
    for (var p = 1; p <= 2; p++) {
        (function(player) {
            var at = { u: (player - 1) * playerLength + stick.u, v: stick.v }
            things.push({ at: at, draw: function() { drawStick(grid, at) } })
            for (var b = 0; b < 6; b++) {
                (function(index) {
                    var at = buttonAt(player, index)
                    var lightIt = player === side && on[String(index + 1)]
                    things.push({ at: at, draw: function() {
                        var c = project(at.u, at.v, 0)
                        grid.sprite(lightIt ? button.on : button.off, button.anchor, c.x, c.y)
                    } })
                })(b)
            }
        })(p)
    }
    things.sort(function(a, b) { return (a.at.u - a.at.v) - (b.at.u - b.at.v) })
    for (var t = 0; t < things.length; t++)
        things[t].draw()

    // The focus frame round each lit one.
    var gap = 1
    var framed = function(sprite, c) {
        var left = Math.round(c.x) - sprite.anchor.x, top = Math.round(c.y) - sprite.anchor.y
        grid.frame(left - gap, top - gap, left + sprite.off[0].length - 1 + gap, top + sprite.off.length - 1 + gap)
    }
    for (var n = 0; n < 6; n++) {
        if (on[String(n + 1)]) {
            var at = buttonAt(side, n)
            framed(button, project(at.u, at.v, 0))
        }
    }
    if (on["coin"])
        framed(frontButton, frontAt("coin"))
    if (on["start"])
        framed(frontButton, frontAt(start))
    return { width: width, height: height, cells: grid.cells }
}
