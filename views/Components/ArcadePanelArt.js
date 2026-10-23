.pragma library

// The Arcade Buttons page's drawing (ArcadePanel.qml): a two-player arcade
// cabinet's control panel, with a little of the cabinet above and below it,
// in two-colour isometric pixel art. The panel's top rises towards the back:
// on it each player has a ball-top stick and six buttons in two rows of
// three that follow the fingers (the first column nearer the player, the
// second the farthest), player 1 on the left and player 2 on the right. Its
// front leans back as it goes down: on it, below a chrome strip, three round
// buttons, 1P START, COIN and 2P START. Behind the panel the monitor leans
// back, its bezel round the foot of the screen; under it the cabinet's front,
// leaning back too, goes down past the top of the coin door. The cabinet
// fades out above and below. One player's side can be lit: a button, COIN or
// that player's START.
//
// draw() lays it out on a grid of cells, one art pixel each: 0 for none
// (the ground the host draws on shows), 1 for ink, 2 for the ground drawn
// over something behind. The host paints the ink in its text colour, so the
// drawing takes the theme's two colours. Ink is light: what faces the light
// (from above) has more of it.

// The grid.
var width = 103
var height = 105

// Isometric 2:1: along the panel (u, the players' left to right) a cell
// down for every two right, into it (v, away from the players) a cell up for
// every two right, z straight up, all in cells. Origin: the panel top's
// front left corner.
var originX = 0
var originY = 38

function project(u, v, z) {
    return { x: originX + 2 * u + 2 * v, y: originY + u - v - (z || 0) }
}

// The panel: its length and depth; its top rises towards the back (slope
// cells up for every unit back); its front goes down frontHeight cells and
// leans back as it goes, its foot frontLean units further back than its
// top; the chrome strip along its top, how far down.
var panelLength = 38
var panelDepth = 11
var slope = 0.55
var frontHeight = 15
var frontLean = 2.5
var strip = 2
// The cabinet round it, as much as shows: the monitor's bezel, leaning back
// (lean units at its top) up from the top's back edge, how high; and the
// front under the panel, leaning back too (belowLean units at its foot) down
// from the panel front's foot, how low.
var above = 20
var lean = 3.5
var below = 20
var belowLean = 2.5

// Where things stand on the top (u, v), from the left end of each player's
// half of it. The buttons' columns follow the fingers: the first a little
// nearer the player, the second the farthest, the third a little nearer
// again.
var playerLength = panelLength / 2
var stick = { u: 3.5, v: 5.0 }
var columns = [8.1, 12.5, 16.9]
var bend = [-0.9, 0, -0.4]
var rows = [7.6, 3.5]        // the top row (1 2 3), the bottom (4 5 6)
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
var shaftHeight = 5

// Where player 1's or 2's button stands, "1" to "6" from 0.
function buttonAt(player, index) {
    var column = index % 3
    return { u: (player - 1) * playerLength + columns[column], v: rows[index < 3 ? 0 : 1] + bend[column] }
}

// The cells, and for each of the cabinet's the height it stands at (NaN for
// the rest), which its fade goes by.
function Grid(w, h) {
    this.w = w
    this.h = h
    this.cells = new Uint8Array(w * h)
    this.height = new Float32Array(w * h).fill(NaN)
}
// A cell drawn over is no longer the cabinet's.
Grid.prototype.set = function(x, y, value) {
    if (x >= 0 && y >= 0 && x < this.w && y < this.h) {
        this.cells[y * this.w + x] = value
        this.height[y * this.w + x] = NaN
    }
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
// The cells of a shape as the cabinet's, at the heights heightAt(x, y)
// gives, whatever is drawn in them.
Grid.prototype.tag = function(shape, heightAt) {
    var box = shape.box
    for (var y = Math.max(0, Math.floor(box[1])); y <= Math.min(this.h - 1, Math.ceil(box[3])); y++) {
        for (var x = Math.max(0, Math.floor(box[0])); x <= Math.min(this.w - 1, Math.ceil(box[2])); x++) {
            if (shape.inside(x + 0.5, y + 0.5))
                this.height[y * this.w + x] = heightAt(x + 0.5, y + 0.5)
        }
    }
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
// A flat face of the cabinet or the panel across it: from its left end's
// point p0 (u 0, v, z) along the cabinet (u) and up or down it by dir, t from
// 0 to 1. at(u, t): where a point of it is on the grid; quad(): a part of
// it as a shape; heightAt(x, y): the height (z) of the point of it a cell
// shows.
function Plane(p0, dir) {
    var s0 = project(0, p0.v, p0.z)
    var sx = 2 * dir.v, sy = -dir.v - dir.z
    var det = 2 * sy - sx
    this.at = function(u, t) { return project(u, p0.v + t * dir.v, p0.z + t * dir.z) }
    this.t = function(x, y) { return (2 * (y - s0.y) - (x - s0.x)) / det }
    this.heightAt = function(x, y) { return p0.z + this.t(x, y) * dir.z }.bind(this)
    this.quad = function(u0, u1, t0, t1) {
        return polygon([this.at(u0, t0), this.at(u1, t0), this.at(u1, t1), this.at(u0, t1)])
    }
    // A unit up or down it, in the cabinet's units: a cell up is shorter
    // than one along (tall of them to a unit).
    this.length = Math.sqrt(dir.v * dir.v + dir.z * dir.z / (tall * tall))
    this.det = det
    this.sx = sx
}
var tall = 2.45

// Two-colour shading: ink on a share of the cells, in an even pattern.
function ground() { return 2 }
function ink() { return 1 }
function half(x, y) { return (x + y) % 2 === 0 ? 1 : 2 }
function sixteenth(x, y) { return x % 4 === 0 && y % 4 === 0 ? 1 : 2 }
// A lit screen's lines: every other row, every other cell of it.
function scanlines(x, y) { return y % 2 === 0 && x % 2 === 0 ? 1 : 2 }

// A stick (a Sanwa JLF with its ball top): the dust washer round the
// shaft, the shaft, the ball.
function drawStick(grid, at) {
    var c = onTop(at)
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

// The panel's faces and the cabinet's: the top, rising towards the back;
// the front, leaning back as it goes down; the monitor's bezel, leaning back
// up from the top's back edge; the cabinet's front, leaning back down from
// the panel front's foot.
var deck = new Plane({ v: 0, z: 0 }, { v: panelDepth, z: slope * panelDepth })
var face = new Plane({ v: 0, z: 0 }, { v: frontLean, z: -frontHeight })
var bezel = new Plane({ v: panelDepth, z: slope * panelDepth }, { v: lean, z: above })
var low = new Plane({ v: frontLean, z: -frontHeight }, { v: belowLean, z: -below })
// Where a thing on the top stands, raised with it.
function onTop(at) {
    return project(at.u, at.v, slope * at.v)
}
// The front's buttons, halfway down it from the strip.
var frontT = (strip / frontHeight + 1) / 2
function frontAt(name) {
    return face.at(front[name] * panelLength, frontT)
}

// A disc on one of the planes, r units across its middle, as a sprite:
// its outline in ink, ground in it but for a spot at the top left (its
// shine), or ink in it but for the spot when it is lit.
function discSprite(plane, r, lit) {
    var cells = {}, left = Infinity, right = -Infinity, topRow = Infinity, bottom = -Infinity
    var place = function(dx, dy) {
        var dt = (2 * dy - dx) / plane.det
        return { u: (dx - plane.sx * dt) / 2, t: dt * plane.length }
    }
    var inside = function(dx, dy) {
        var p = place(dx, dy)
        return p.u * p.u + p.t * p.t <= r * r
    }
    for (var dy = -12; dy <= 12; dy++) {
        for (var dx = -16; dx <= 16; dx++) {
            if (!inside(dx, dy))
                continue
            var p = place(dx, dy), ch
            var su = p.u + 0.4 * r, st = p.t + 0.35 * r * (plane.det > 0 ? 1 : -1)
            var spot = su * su + st * st <= 0.12 * r * r
            if (!inside(dx - 1, dy) || !inside(dx + 1, dy) || !inside(dx, dy - 1) || !inside(dx, dy + 1))
                ch = "#"
            else
                ch = lit !== spot ? "#" : "o"
            cells[dx + "," + dy] = ch
            left = Math.min(left, dx); right = Math.max(right, dx)
            topRow = Math.min(topRow, dy); bottom = Math.max(bottom, dy)
        }
    }
    var lines = []
    for (var y = topRow; y <= bottom; y++) {
        var line = ""
        for (var x = left; x <= right; x++)
            line += cells[x + "," + y] || "."
        lines.push(line)
    }
    return { lines: lines, anchor: { x: -left, y: -topRow } }
}
var frontRadius = 2.0
var frontOff = discSprite(face, frontRadius, false)
var frontOn = discSprite(face, frontRadius, true)

// What shows of the cabinet behind and under the panel, each face filled
// and outlined: the monitor's bezel round the foot of the screen, its lines
// and a glint on its glass; and the cabinet's front under the panel, down
// past the top of the coin door with its two coin entries (a bright plate
// each with the slot in it, the coin return under it).
function drawCabinet(grid) {
    var L = panelLength
    var draw = function(shape, fill) {
        grid.paint(shape, fill)
        grid.edge(shape)
    }
    // t up the bezel and down the front for so many cells up or down.
    var up = function(cells) { return cells / above }
    var down = function(cells) { return cells / below }

    var whole = bezel.quad(0, L, 0, 1)
    draw(whole, ground)
    draw(bezel.quad(3, L - 3, up(6), 1), ground)
    draw(polygon([bezel.at(5, up(7)), bezel.at(L - 5, up(7)), bezel.at(L - 4, up(9.5)), bezel.at(L - 4, 1),
                  bezel.at(4, 1), bezel.at(4, up(9.5))]), scanlines)
    grid.line(bezel.at(6, up(9)), bezel.at(9, up(13)), 1)
    grid.line(bezel.at(6, up(12)), bezel.at(7, up(14)), 1)
    grid.tag(whole, bezel.heightAt)

    var front = low.quad(0, L, 0, 1)
    draw(front, sixteenth)
    draw(low.quad(L / 2 - 7, L / 2 + 7, down(4.5), 1), ground)
    var entries = [L / 2 - 3.5, L / 2 + 1.5]
    for (var j = 0; j < entries.length; j++) {
        var e = entries[j]
        draw(low.quad(e, e + 2, down(6), down(11)), ink)
        grid.line(low.at(e + 1, down(7)), low.at(e + 1, down(9.5)), 2)
        var r = low.at(e + 1, down(13))
        var rx = Math.round(r.x), ry = Math.round(r.y)
        grid.set(rx, ry, 1); grid.set(rx + 1, ry, 1); grid.set(rx, ry + 1, 1); grid.set(rx + 1, ry + 1, 1)
    }
    grid.tag(front, low.heightAt)
}

// The cabinet fades out up the bezel and down its front: the higher (or
// lower) one of its cells, the likelier it is gone, in an even pattern.
function fade(grid) {
    var bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
    var base = slope * panelDepth
    var from = base + 10, top = base + above, to = -frontHeight - 10, bottom = -frontHeight - below
    for (var y = 0; y < grid.h; y++) {
        for (var x = 0; x < grid.w; x++) {
            var z = grid.height[y * grid.w + x]
            if (isNaN(z))
                continue
            var keep = z > from ? (top - z) / (top - from) : z < to ? (z - bottom) / (to - bottom) : 1
            if (bayer[y % 4][x % 4] >= keep * 16)
                grid.cells[y * grid.w + x] = 0
        }
    }
}

// lit: the inputs to light, by their names ("1" to "6", "coin", "start"),
// on player's side: 2 the right, else the left.
function draw(lit, player) {
    var on = {}
    for (var i = 0; lit && i < lit.length; i++)
        on[lit[i]] = true
    var side = player === 2 ? 2 : 1
    var grid = new Grid(width, height)
    var L = panelLength, D = panelDepth
    drawCabinet(grid)

    // The panel's front, in shade, with the strip along its top; its right
    // end, the top's back edge down to the front's foot; the top over them.
    var front = face.quad(0, L, 0, 1)
    grid.paint(front, ground)
    grid.edge(front)
    grid.line(face.at(0, strip / frontHeight), face.at(L, strip / frontHeight), 1)
    var end = polygon([face.at(L, 0), deck.at(L, 1), project(L, D, -frontHeight), face.at(L, 1)])
    grid.paint(end, ground)
    grid.edge(end)
    var topFace = deck.quad(0, L, 0, 1)
    grid.paint(topFace, sixteenth)
    grid.edge(topFace)

    // The front's buttons: 1P START, COIN, 2P START.
    var start = side === 2 ? "start2" : "start1"
    var fronts = ["start1", "coin", "start2"]
    for (var f = 0; f < fronts.length; f++) {
        var name = fronts[f]
        var c = frontAt(name)
        var sprite = (name === "coin" ? on["coin"] : name === start && on["start"]) ? frontOn : frontOff
        grid.sprite(sprite.lines, sprite.anchor, c.x, c.y)
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
                        var c = onTop(at)
                        grid.sprite(lightIt ? button.on : button.off, button.anchor, c.x, c.y)
                    } })
                })(b)
            }
        })(p)
    }
    things.sort(function(a, b) { return (a.at.u - a.at.v) - (b.at.u - b.at.v) })
    for (var t = 0; t < things.length; t++)
        things[t].draw()
    fade(grid)

    // The focus frame round each lit one.
    var gap = 1
    var framed = function(lines, anchor, c) {
        var left = Math.round(c.x) - anchor.x, top = Math.round(c.y) - anchor.y
        grid.frame(left - gap, top - gap, left + lines[0].length - 1 + gap, top + lines.length - 1 + gap)
    }
    for (var n = 0; n < 6; n++) {
        if (on[String(n + 1)])
            framed(button.off, button.anchor, onTop(buttonAt(side, n)))
    }
    if (on["coin"])
        framed(frontOff.lines, frontOff.anchor, frontAt("coin"))
    if (on["start"])
        framed(frontOff.lines, frontOff.anchor, frontAt(start))
    return { width: width, height: height, cells: grid.cells }
}
