// Settings → Selector Effect, as the app plays it round the selected line:
// src/fx/SelectorFx.cpp step for step (its random numbers, its frame of 33
// milliseconds, its sparks, bolts, flakes and rainbow), drawing in art pixels
// into a frame of its own, premultiplied, that the preview lays over the
// window scaled up by its art pixel, as the app's item does.
//   Sparkles   sparks streaking off the box's four corners, some twinkling;
//   Welding    a welder's sparks bursting from a corner and falling;
//   Lightning  jagged bolts out of the corners, now and then along an edge;
//   Rainbow    a pixel rainbow running down from under the box;
//   Snow       flakes falling from the box's top edge, melting away.
export const FRAME = 0.033;
const CLOCK_WRAPS = 3600;
// The app keeps its clock, its draws and its sparks' ages in floats: so
// does this, so its frames fall as the app's do.
const f32 = Math.fround;
const STEP = f32(FRAME);

// The rainbow's bands, top to bottom.
const RAINBOW = [[255, 48, 48], [255, 144, 0], [255, 232, 0], [48, 224, 72], [48, 128, 255], [176, 72, 255]];
// The outward diagonal from each corner of a box: top left, top right,
// bottom left, bottom right.
const OUT = [[-1, -1], [1, -1], [-1, 1], [1, 1]];

const mixed = (a, b, t) => a.map((v, i) => Math.trunc(v + (b[i] - v) * t));
// One channel of a colour laid over what is there, premultiplied.
const over = (value, alpha, under, keep) => Math.trunc(f32(f32(value * alpha) + f32(under * keep)));

function hash1(i) {
    let h = Math.imul(i, 0x9E3779B1) >>> 0;
    h = (h ^ (h >>> 15)) >>> 0;
    h = Math.imul(h, 0x85EBCA77) >>> 0;
    h = (h ^ (h >>> 13)) >>> 0;
    return (h & 0xFFFFFF) / 0x1000000;
}
function noise1(x) {
    const i = Math.floor(x);
    const f = x - i;
    const s = f * f * (3 - 2 * f);
    return hash1(i) + (hash1(i + 1) - hash1(i)) * s;
}
const hash3 = (x, y, z) => hash1(Math.imul(x, 73856093) ^ Math.imul(y, 19349663) ^ Math.imul(z, 83492791));

export class SelectorFx {
    constructor(cols, rows) {
        this.cols = cols;
        this.rows = rows;
        this.frame = new Uint8Array(cols * rows * 4);
        this.kind = "";
        this.ink = [255, 255, 255];
        this.paper = [0, 0, 0];
        this.seed = 0x2545F491;
        this.time = 0;
        this.nextBurst = 0;
        this.snowDue = 0;
        this.sparks = [];
        this.bolts = [];
        this.blank = true;
    }

    // "Sparkles", "Welding", "Lightning", "Rainbow", "Snow"; anything else none.
    setEffect(effect) {
        const kind = ["sparkles", "welding", "lightning", "rainbow", "snow"].includes(String(effect).toLowerCase())
            ? String(effect).toLowerCase() : "";
        if (kind === this.kind)
            return;
        this.kind = kind;
        // What the last one left flies on only if this is the same kind.
        this.sparks = [];
        this.bolts = [];
    }

    // Stopped (no effect, or resting): what was there goes. True when there
    // was something, its frame now clear.
    stop() {
        this.sparks = [];
        this.bolts = [];
        if (this.blank)
            return false;
        this.frame.fill(0);
        this.blank = true;
        return true;
    }

    random() {
        let x = this.seed;
        x = (x ^ (x << 13)) >>> 0;
        x = (x ^ (x >>> 17)) >>> 0;
        x = (x ^ (x << 5)) >>> 0;
        return this.seed = x;
    }
    uniform(from, to) {
        from = f32(from);
        return f32(from + f32(f32(f32(to) - from) * ((this.random() & 0xFFFFFF) / 0x1000000)));
    }

    plot(x, y, color, alpha) {
        if (x < 0 || y < 0 || x >= this.cols || y >= this.rows || alpha <= 0)
            return;
        alpha = f32(Math.min(alpha, 1));
        const at = (y * this.cols + x) * 4;
        const f = this.frame;
        // Over what is there, premultiplied.
        const keep = f32(1 - alpha);
        f[at] = over(color[0], alpha, f[at], keep);
        f[at + 1] = over(color[1], alpha, f[at + 1], keep);
        f[at + 2] = over(color[2], alpha, f[at + 2], keep);
        f[at + 3] = over(255, alpha, f[at + 3], keep);
    }
    line(a, b, color, alpha) {
        const dx = b[0] - a[0], dy = b[1] - a[1];
        const steps = Math.max(1, Math.ceil(Math.max(Math.abs(dx), Math.abs(dy))));
        for (let i = 0; i <= steps; ++i)
            this.plot(Math.floor(a[0] + dx * i / steps), Math.floor(a[1] + dy * i / steps), color, alpha);
    }

    bolt(from, to, jag) {
        // Halved again and again, each middle pushed aside by less each time.
        let points = [from, to];
        for (let pass = 0; pass < 4; ++pass) {
            const next = [];
            for (let i = 0; i + 1 < points.length; ++i) {
                const a = points[i], b = points[i + 1];
                const d = [b[0] - a[0], b[1] - a[1]];
                const normal = [-d[1], d[0]];
                const len = Math.max(1, Math.hypot(normal[0], normal[1]));
                const push = this.uniform(-jag, jag);
                next.push(a, [(a[0] + b[0]) / 2 + normal[0] / len * push, (a[1] + b[1]) / 2 + normal[1] / len * push]);
            }
            next.push(points[points.length - 1]);
            points = next;
            jag *= 0.55;
        }
        return points;
    }

    emitSparkles(b) {
        const corners = [[b.x, b.y], [b.x + b.w, b.y], [b.x, b.y + b.h], [b.x + b.w, b.y + b.h]];
        for (let c = 0; c < 4; ++c) {
            if (this.uniform(0, 1) > 0.5)
                continue;
            const [side, up] = OUT[c];
            const x = corners[c][0] + this.uniform(-0.5, 0.5);
            const y = corners[c][1] + this.uniform(-0.5, 0.5);
            const vx = side * this.uniform(25, 75);
            const vy = up * this.uniform(2, 22) + this.uniform(-8, 8);
            const life = this.uniform(0.45, 1.1);
            const cross = this.uniform(0, 1) < 0.3;
            this.sparks.push({ x, y, vx, vy, age: 0, life, lastX: x, lastY: y, cross, sway: 0, phase: 0 });
        }
    }

    emitWelding(b) {
        if (this.time < this.nextBurst)
            return;
        this.nextBurst = f32(this.time + this.uniform(0.05, 0.2));
        const corners = [[b.x, b.y], [b.x + b.w, b.y], [b.x, b.y + b.h], [b.x + b.w, b.y + b.h]];
        const c = this.random() % 4;
        const out = Math.atan2(OUT[c][1], OUT[c][0]);
        const count = 6 + this.random() % 10;
        for (let i = 0; i < count; ++i) {
            const angle = out + this.uniform(-1, 1);
            const speed = this.uniform(45, 165);
            const [x, y] = corners[c];
            this.sparks.push({ x, y, vx: Math.cos(angle) * speed, vy: Math.sin(angle) * speed, age: 0,
                               life: this.uniform(0.3, 0.85), lastX: x, lastY: y, cross: false, sway: 0, phase: 0 });
        }
        // The weld itself, flaring white for a moment.
        const [x, y] = corners[c];
        this.sparks.push({ x, y, vx: 0, vy: 0, age: 0, life: 0.07, lastX: x, lastY: y, cross: true, sway: 0, phase: 0 });
    }

    emitLightning(b) {
        if (this.time < this.nextBurst)
            return;
        this.nextBurst = f32(this.time + this.uniform(0.08, 0.35));
        const corners = [[b.x, b.y], [b.x + b.w, b.y], [b.x, b.y + b.h], [b.x + b.w, b.y + b.h]];
        const main = { lit: 0, frames: 2 + this.random() % 3, points: [] };
        if (this.uniform(0, 1) < 0.22) {
            // Along the box's top or bottom edge, corner to corner.
            const top = this.random() % 2;
            main.points = this.bolt(top ? corners[0] : corners[2], top ? corners[1] : corners[3], Math.min(3, b.h / 2));
            this.bolts.push(main);
            return;
        }
        const c = this.random() % 4;
        const angle = Math.atan2(OUT[c][1], OUT[c][0]) + this.uniform(-0.9, 0.9);
        const length = this.uniform(16, 46);
        const end = [corners[c][0] + Math.cos(angle) * length, corners[c][1] + Math.sin(angle) * length];
        main.points = this.bolt(corners[c], end, length * 0.22);
        this.bolts.push(main);
        // A branch off it, now and then.
        if (this.uniform(0, 1) < 0.6) {
            const third = Math.floor(main.points.length / 3);
            const from = main.points[third + this.random() % third];
            const fork = angle + (this.random() % 2 ? 0.7 : -0.7);
            this.bolts.push({ lit: 0, frames: main.frames - 1,
                              points: this.bolt(from, [from[0] + Math.cos(fork) * length * 0.45,
                                                       from[1] + Math.sin(fork) * length * 0.45], length * 0.1) });
        }
    }

    emitSnow(b) {
        // A flake a second for every seven art pixels of the box's width.
        this.snowDue = f32(this.snowDue + f32(f32(f32(b.w) / 7) * STEP));
        while (this.snowDue >= 1) {
            this.snowDue = f32(this.snowDue - 1);
            if (this.sparks.length >= 400)
                continue;
            // About one in four nearer: a small cross, falling faster.
            const cross = this.uniform(0, 1) < 0.25;
            const x = this.uniform(b.x, b.x + b.w);
            const y = b.y + this.uniform(-1, 0.5);
            const vx = this.uniform(-2.5, 2.5);
            const vy = cross ? this.uniform(14, 22) : this.uniform(7, 15);
            const life = this.uniform(1.6, 2.8);
            const sway = this.uniform(0.4, 1.4);
            const phase = this.uniform(0, 6.2832);
            this.sparks.push({ x, y, vx, vy, age: 0, life, lastX: x, lastY: y, cross, sway, phase });
        }
    }

    drawRainbow(b) {
        const top = Math.ceil(b.y + b.h);
        const left = Math.floor(b.x);
        const right = Math.ceil(b.x + b.w);
        const frame = Math.trunc(f32(this.time / STEP));
        for (let x = left; x < right; ++x) {
            // Each column its own length, swelling and ebbing as it runs.
            const length = Math.trunc(6 + 8 * noise1(x * 0.23 + this.time * 0.9) + 6 * noise1(x * 0.07 - this.time * 0.45));
            for (let k = 0; k < length; ++k) {
                const along = k / length;
                // Gone in specks toward the end.
                if (hash3(x, top + k, Math.trunc(frame / 2)) < along * along * 0.9)
                    continue;
                const band = Math.floor((k - this.time * 14) / 2) % 6;
                // Never solid: the line it runs under stays legible.
                this.plot(x, top + k, RAINBOW[(band + 6) % 6], 0.7 * (1 - Math.pow(along, 1.6)));
            }
        }
    }

    // A frame on: `box` is the selected line's, in art pixels ({ x, y, w,
    // h }), or null with none to play round. False once there is nothing
    // to draw and nothing was drawn.
    tick(box) {
        this.time = f32(this.time + STEP);
        if (this.time >= CLOCK_WRAPS) {
            this.time = f32(this.time - CLOCK_WRAPS);
            this.nextBurst = f32(this.nextBurst - CLOCK_WRAPS);
        }
        const b = box && box.w > 0 && box.h > 0 ? box : null;
        if (b) {
            if (this.kind === "sparkles") this.emitSparkles(b);
            else if (this.kind === "welding") this.emitWelding(b);
            else if (this.kind === "lightning") this.emitLightning(b);
            else if (this.kind === "snow") this.emitSnow(b);
        }
        const drawing = (b && this.kind === "rainbow") || this.sparks.length > 0 || this.bolts.length > 0;
        if (!drawing && this.blank)
            return false;
        this.frame.fill(0);
        if (b && this.kind === "rainbow")
            this.drawRainbow(b);

        const light = mixed(this.ink, [255, 255, 255], 0.45);
        // Snow in the scheme's colour, lighter; over the box, the box's own
        // other colour.
        const flake = mixed(this.ink, [255, 255, 255], 0.6);
        const snowPlot = (x, y, alpha) => {
            const over = b && x + 0.5 >= b.x && x + 0.5 <= b.x + b.w && y + 0.5 >= b.y && y + 0.5 <= b.y + b.h;
            this.plot(x, y, over ? this.paper : flake, alpha);
        };
        const frame = Math.trunc(f32(this.time / STEP));
        for (let i = this.sparks.length - 1; i >= 0; --i) {
            const s = this.sparks[i];
            s.age = f32(s.age + STEP);
            if (s.age >= s.life) {
                this.sparks.splice(i, 1);
                continue;
            }
            s.lastX = s.x;
            s.lastY = s.y;
            const f = s.age / s.life;
            if (this.kind === "welding") {
                if (s.life < 0.1) {
                    // The weld's flare: a white blob.
                    for (let dy = -1; dy <= 1; ++dy)
                        for (let dx = -1; dx <= 1; ++dx)
                            this.plot(Math.trunc(s.x) + dx, Math.trunc(s.y) + dy, [255, 255, 240], dx || dy ? 0.6 : 1);
                    continue;
                }
                s.vy += 170 * FRAME;
                s.vx *= 0.985;
                s.x += s.vx * FRAME;
                s.y += s.vy * FRAME;
                // White hot, then yellow, orange and red as it cools.
                const color = f < 0.15 ? [255, 255, 235] : f < 0.4 ? [255, 226, 92] : f < 0.7 ? [255, 140, 32] : [222, 54, 24];
                this.line([s.lastX, s.lastY], [s.x, s.y], color, 1 - Math.pow(f, 1.5));
            } else if (this.kind === "snow") {
                // Straight down where it is, whatever the box does now, swaying.
                s.x += s.vx * FRAME;
                s.y += s.vy * FRAME;
                const x = Math.floor(s.x + s.sway * Math.sin(s.phase + s.age * 2.6));
                const y = Math.floor(s.y);
                // Melting away over the last third of its fall.
                const alpha = f < 0.66 ? 1 : 1 - (f - 0.66) / 0.34;
                if (s.cross) {
                    snowPlot(x, y, alpha);
                    snowPlot(x - 1, y, alpha * 0.6);
                    snowPlot(x + 1, y, alpha * 0.6);
                    snowPlot(x, y - 1, alpha * 0.6);
                    snowPlot(x, y + 1, alpha * 0.6);
                } else {
                    snowPlot(x, y, alpha * 0.85);
                }
            } else {
                s.vx *= 0.955;
                s.vy *= 0.955;
                s.x += s.vx * FRAME;
                s.y += s.vy * FRAME;
                const alpha = 1 - f * f;
                const x = Math.floor(s.x), y = Math.floor(s.y);
                // A streak behind it, as long as it is fast, then its head.
                this.line([s.x - s.vx * 0.08, s.y - s.vy * 0.08], [s.x, s.y], light, alpha * 0.4);
                this.plot(x, y, [255, 255, 255], alpha);
                // A cross that twinkles: its arms there half the time.
                if (s.cross && (frame + i) % 6 < 3) {
                    for (let d = 1; d <= 2; ++d) {
                        const arm = alpha * (d === 1 ? 0.8 : 0.45);
                        this.plot(x - d, y, light, arm);
                        this.plot(x + d, y, light, arm);
                        this.plot(x, y - d, light, arm);
                        this.plot(x, y + d, light, arm);
                    }
                }
            }
        }

        const core = mixed(this.ink, [255, 255, 255], 0.75);
        for (let i = this.bolts.length - 1; i >= 0; --i) {
            const bolt = this.bolts[i];
            if (bolt.frames <= 0) {
                this.bolts.splice(i, 1);
                continue;
            }
            // Brightest as it strikes, fainter for the frames it lingers.
            const alpha = bolt.lit === 0 ? 1 : f32(0.65);
            for (let k = 0; k + 1 < bolt.points.length; ++k) {
                const a = bolt.points[k], c = bolt.points[k + 1];
                // A faint glow either side, then the bolt.
                this.line([a[0], a[1] + 1], [c[0], c[1] + 1], this.ink, f32(alpha * f32(0.3)));
                this.line([a[0], a[1] - 1], [c[0], c[1] - 1], this.ink, f32(alpha * f32(0.3)));
                this.line(a, c, core, alpha);
            }
            ++bolt.lit;
            --bolt.frames;
        }
        this.blank = !drawing;
        return true;
    }
}
