// The Arcade screen's Orb look (modules/arcade/views/OrbLook.qml, a theme's
// "arcade": "orb") on WebGL 2, as OrbLook.qml draws it: the screen in 3D, in
// x-ray glass whose light adds up where its parts cross. On the right the
// list's panel, the filter's name on its tab and the page's games on bars,
// the selected one standing out towards us and the one left going back
// slowly; on the left a glass orb with the game's picture inside it (on a
// card freshly made, which file it would be), the credits bought at its foot,
// and under it a panel of its high scores and what it is. Brackets hold the
// panels from past the screen's sides, fog drifts behind, and the whole sways
// round the screen's middle; each of ▲ ▼ ◄ ► nudges it on a spring, and a
// game started (launch()) swings the panels out on their hinges while the
// orb comes to the middle and the camera goes into it, its picture opening
// out to fill the screen, before the look fades out and back in, as it does
// back from a game. The keys at the foot lie in the hint bar's place;
// screens.mjs draws what the look keeps in 2D (the help line, the status
// icons, Search's keys).
//
// It is drawn with the look's own shaders (modules/arcade/views/orb/, which
// scripts/build-site.py copies into the page's orb/ beside its fog picture),
// made WebGL's by glsl.mjs's fromQt3D(), with its meshes made as OrbLook.qml
// makes them and Qt Quick 3D's own #Rectangle and #Sphere. Its letters are
// drawn on canvases, as the look's are Items drawn on textures (Sheet): two
// texels to the design's unit, mipmapped. As Qt Quick 3D does, a material's
// colour goes to its shader in linear light (linear()) and an unshaded
// material's output is left as it is, while a sheet's picture comes out as it
// is drawn.
import { fromQt3D } from "./glsl.mjs";
import { rgbOf } from "./look.mjs";
import { SW, SH, lineHeight, measure, drawText, elided, clipped, drawIcon, iconWidth, iconHeight, scrollOffset,
         ARCADE_FILTERS, arcadeLayout, arcadeFilter, arcadeRows, arcadeSelectedRow, arcadeHintPairs,
         arcadeFoot } from "./screens.mjs";

// --- The look's layout, in the design's units (OrbLook.qml's): the screen's
// 960 by 720, its middle at the scene's origin, y up.
const PANEL = { left: 476, right: 806, top: 92, bottom: 592, tabBottom: 142, rowsTop: 154, rowsBottom: 582 };
const PANEL_W = PANEL.right - PANEL.left, PANEL_H = PANEL.bottom - PANEL.top;
const ROW_H = 44;
const ORB = { x: 296, y: 240, r: 150, z: 30 };
const INFO = { left: 120, right: 436, top: 398, bottom: 600 };
const INFO_W = INFO.right - INFO.left, INFO_H = INFO.bottom - INFO.top;
const LIFT_BY = 90;
const sx = x => x - 480, sy = y => 360 - y;
// The letters, in the design's lines (ArcadeScreen.qml's textLines and
// nameLines), and a sheet's texels to the unit (sheetScale, off a tube).
const TEXT = 27, NAME = 30, SHEET_SCALE = 2;
// The camera: 720 units high at z = 0 at rest.
const FOV = 20;
const REST = 360 / Math.tan(FOV / 2 * Math.PI / 180);

// The look's shaders, by its materials' names, with the properties each
// material sets them (OrbLook.qml's CustomMaterials: a colour a vec4, a
// texture a sampler2D), and a Sheet's PrincipledMaterial, unlit: its
// picture's colours, which Qt takes for straight ones, times its alpha and the
// sheet's opacity.
export const SHADER_FILES = ["flat.vert", "glass.vert", "glass.frag", "fog.frag", "fogLive.frag", "screen.frag"];
const FOG = { tint: "vec4", strength: "float", scale: "float", seed: "float", radius: "float", phase: "float",
              fogNoise: "sampler2D" };
export const MATERIALS = {
    glass: { vertex: "glass.vert", fragment: "glass.frag",
             properties: { tint: "vec4", core: "float", strength: "float", power: "float", fade: "float" } },
    fog: { vertex: "flat.vert", fragment: "fog.frag", properties: FOG },
    fogLive: { vertex: "flat.vert", fragment: "fogLive.frag", properties: FOG },
    screen: { vertex: "flat.vert", fragment: "screen.frag",
              properties: { tint: "vec4", open: "float", fade: "float", picture: "sampler2D" } },
    sheet: { vertex: "flat.vert", properties: { opacity: "float", sheet: "sampler2D" }, source: `VARYING vec2 vUV;
void MAIN()
{
    vec4 c = texture(sheet, vec2(vUV.x, 1.0 - vUV.y), -0.5);
    float a = c.a * opacity;
    FRAGCOLOR = vec4(c.rgb * a, a);
}` },
};

// --- Colours: the scheme's two, a brighter one for what is lit, the orb's,
// turned round the colour wheel, and the letters' (OrbLook.qml's), as
// [r, g, b] from 0 to 1.
const mix = (a, b, f) => a.map((c, i) => c + (b[i] - c) * f);
function turned([r, g, b], by) {
    const max = Math.max(r, g, b), min = Math.min(r, g, b), l = (max + min) / 2, d = max - min;
    if (d === 0)
        return [r, g, b];
    const s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    let h = max === r ? (g - b) / d + (g < b ? 6 : 0) : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
    h = ((h / 6 + by) % 1 + 1) % 1;
    const q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    const channel = t => {
        t = (t % 1 + 1) % 1;
        return t < 1 / 6 ? p + (q - p) * 6 * t : t < 1 / 2 ? q : t < 2 / 3 ? p + (q - p) * (2 / 3 - t) * 6 : p;
    };
    return [channel(h + 1 / 3), channel(h), channel(h - 1 / 3)];
}
// A colour as Qt Quick 3D hands a material one: in linear light, by Qt's own
// curve (QSSGUtils::color::sRgbToLinear()).
export const linear = c => [...c.map(v => v * (v * (v * 0.305306011 + 0.682171111) + 0.012522878)), 1];
const css = (c, alpha = 1) => `rgba(${c.map(v => Math.round(v * 255)).join(",")},${alpha})`;

// --- Meshes, in a part's own units: x right, y up, round its middle
// (OrbLook.qml's tube(), chamfered() and slab()). Each { positions, normals,
// indexes }, the first two lists of [x, y, z].
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const length = a => Math.hypot(a[0], a[1], a[2]);
const normalized = a => { const n = length(a) || 1; return [a[0] / n, a[1] / n, a[2] / n]; };

// A tube round a path ([x, y, z]), closed or not.
export function tube(points, radius, sides, closed) {
    const positions = [], normals = [], indexes = [];
    const n = points.length;
    const p = points.map(q => [q[0], q[1], q[2] || 0]);
    for (let i = 0; i < n; ++i) {
        const a = p[closed ? (i - 1 + n) % n : Math.max(0, i - 1)];
        const b = p[closed ? (i + 1) % n : Math.min(n - 1, i + 1)];
        const along = normalized(sub(b, a));
        let side = cross(along, [0, 0, 1]);
        if (length(side) < 1e-4)
            side = cross(along, [0, 1, 0]);
        side = normalized(side);
        const up = normalized(cross(along, side));
        for (let k = 0; k < sides; ++k) {
            const angle = 2 * Math.PI * k / sides;
            const dir = side.map((v, j) => v * Math.cos(angle) + up[j] * Math.sin(angle));
            positions.push(p[i].map((v, j) => v + dir[j] * radius));
            normals.push(dir);
        }
    }
    const rings = closed ? n : n - 1;
    for (let i = 0; i < rings; ++i) {
        const j = (i + 1) % n;
        for (let k = 0; k < sides; ++k) {
            const k2 = (k + 1) % sides;
            indexes.push(i * sides + k, j * sides + k, j * sides + k2);
            indexes.push(i * sides + k, j * sides + k2, i * sides + k2);
        }
    }
    return { positions, normals, indexes };
}
// A box w by h round (cx, cy), its corners cut by c.
export function chamfered(cx, cy, w, h, c) {
    const l = cx - w / 2, r = cx + w / 2, b = cy - h / 2, t = cy + h / 2;
    return [[l + c, t], [r - c, t], [r, t - c], [r, b + c], [r - c, b], [l + c, b], [l, b + c], [l, t - c]];
}
// A slab of a convex outline, deep through z: flat faces, so its edges light
// and its faces stay dim.
export function slab(outline, z, deep) {
    const positions = [], normals = [], indexes = [];
    const n = outline.length;
    for (const [face, normal] of [[z + deep / 2, 1], [z - deep / 2, -1]]) {
        const base = positions.length;
        for (const [x, y] of outline) {
            positions.push([x, y, face]);
            normals.push([0, 0, normal]);
        }
        for (let i = 1; i + 1 < n; ++i)
            indexes.push(base, base + i, base + i + 1);
    }
    for (let i = 0; i < n; ++i) {
        const a = outline[i], b = outline[(i + 1) % n];
        const dx = b[0] - a[0], dy = b[1] - a[1], len = Math.sqrt(dx * dx + dy * dy);
        const normal = [dy / len, -dx / len, 0];
        const base = positions.length;
        positions.push([a[0], a[1], z + deep / 2], [b[0], b[1], z + deep / 2], [b[0], b[1], z - deep / 2], [a[0], a[1], z - deep / 2]);
        normals.push(normal, normal, normal, normal);
        indexes.push(base, base + 1, base + 2, base, base + 2, base + 3);
    }
    return { positions, normals, indexes };
}
// A row's bar round its middle, cut on a slant at its right end; the
// selected one deeper, and reaching a little further right.
export function bar(on) {
    const left = -PANEL_W / 2 + 16, right = PANEL_W / 2 - 16 + (on ? 8 : 0);
    const top = ROW_H / 2, bottom = -ROW_H / 2;
    return slab([[left, top], [right, top], [right - 16, bottom], [left, bottom]], 6, on ? 18 : 10);
}
// Qt Quick 3D's #Rectangle, 100 by 100 facing +z, UV0's v up; its #Sphere, 50
// round.
const RECTANGLE = { positions: [[-50, -50, 0], [50, -50, 0], [50, 50, 0], [-50, 50, 0]],
                    normals: [[0, 0, 1], [0, 0, 1], [0, 0, 1], [0, 0, 1]], uvs: [[0, 0], [1, 0], [1, 1], [0, 1]],
                    indexes: [0, 1, 2, 0, 2, 3] };
function sphere(segments, rings) {
    const positions = [], normals = [], indexes = [];
    for (let j = 0; j <= rings; ++j) {
        const phi = Math.PI * j / rings;
        for (let i = 0; i <= segments; ++i) {
            const theta = 2 * Math.PI * i / segments;
            const n = [Math.sin(phi) * Math.cos(theta), Math.cos(phi), -Math.sin(phi) * Math.sin(theta)];
            normals.push(n);
            positions.push(n.map(v => v * 50));
        }
    }
    for (let j = 0; j < rings; ++j) {
        for (let i = 0; i < segments; ++i) {
            const a = j * (segments + 1) + i, b = a + segments + 1;
            indexes.push(a, b, a + 1, a + 1, b, b + 1);
        }
    }
    return { positions, normals, indexes };
}

// --- Matrices, column after column as WebGL takes them.
const multiply = (a, b) => {
    const out = new Float32Array(16);
    for (let c = 0; c < 4; ++c)
        for (let r = 0; r < 4; ++r)
            out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
    return out;
};
const translation = (x, y, z) => new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1]);
const scaling = (x, y, z) => new Float32Array([x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1]);
function rotation(axis, degrees) {
    const t = degrees * Math.PI / 180, c = Math.cos(t), s = Math.sin(t);
    if (axis === "x")
        return new Float32Array([1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1]);
    if (axis === "y")
        return new Float32Array([c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1]);
    return new Float32Array([c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
}
// A node's matrix in its parent's, as Qt Quick 3D has it: its position, then
// its Euler rotation (round z, then x, then y), then its scale.
export function node(parent, { x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0, s = [1, 1, 1] } = {}) {
    let m = multiply(parent, translation(x, y, z));
    for (const [axis, degrees] of [["y", ry], ["x", rx], ["z", rz]])
        if (degrees)
            m = multiply(m, rotation(axis, degrees));
    return multiply(m, scaling(s[0], s[1], s[2]));
}
export const IDENTITY = translation(0, 0, 0);
export const point = (m, [x, y, z]) => [0, 1, 2].map(r => m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r]);
// The inverse of a matrix's 3×3, transposed: what a normal is turned by.
function normalMatrix(m) {
    const [a, b, c, d, e, f, g, h, i] = [m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]];
    const A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
    const det = a * A + b * B + c * C || 1;
    return new Float32Array([A, B, C, c * h - b * i, a * i - c * g, b * g - a * h, b * f - c * e, c * d - a * f, a * e - b * d]
        .map(v => v / det));
}
// The camera's projection, OrbLook.qml's (20° high, clipNear 20, clipFar
// 8000), upside down: a target's rows top first, as renderer.mjs keeps them.
function projection(aspect, near = 20, far = 8000) {
    const f = 1 / Math.tan(FOV / 2 * Math.PI / 180);
    return new Float32Array([f / aspect, 0, 0, 0, 0, -f, 0, 0, 0, 0, (far + near) / (near - far), -1,
                             0, 0, 2 * far * near / (near - far), 0]);
}

// --- Time.
const clamp01 = x => Math.max(0, Math.min(1, x));
const smoothstep = x => { x = clamp01(x); return x * x * (3 - 2 * x); };
// Qt's easing curves of the rows' lifts: OutBack, a little past and back, and
// InOutQuad.
const outBack = t => { const s = 1.70158; t -= 1; return t * t * ((s + 1) * t + s) + 1; };
const inOutQuad = t => t < 0.5 ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
// The keys' spring: a nudge comes out to 1 at its most (KICK), a little past
// and back again; the tilt is at most TILT_MOST degrees.
const SPRING_W = 2 * Math.PI * 1.15, DAMPING = 0.5, TILT_MOST = 9.6;
const KICK = (() => {
    const turning = SPRING_W * Math.sqrt(1 - DAMPING * DAMPING);
    const most = Math.atan2(turning, DAMPING * SPRING_W) / turning;
    return turning / (Math.exp(-DAMPING * SPRING_W * most) * Math.sin(turning * most));
})();
// A game started: how long the camera takes into the orb and the look to fade.
const LAUNCH_MS = 2450;

export class OrbLook {
    // changed(failed): once its shaders and its fog are in (or can't be
    // used: the classic look is drawn then, as the app draws it without Qt
    // Quick 3D).
    constructor(renderer, base, changed) {
        this.r = renderer;
        this.changed = changed;
        this.ready = false;
        this.failed = false;
        this.time = 0;
        this.lastNow = null;
        this.down = { nudge: 0, speed: 0 };
        this.across = { nudge: 0, speed: 0 };
        this.launchedAt = null;
        this.launchStart = 0;
        this.fadeStart = -Infinity;
        this.lifts = [];
        this.rowOn = [];
        this.away = { from: 1, to: 1, start: 0 };
        this.sheets = new Map();
        this.measurer = document.createElement("canvas").getContext("2d");
        // The letters' font once it is in: what was drawn without it is drawn
        // again.
        this.fontReady = false;
        if (document.fonts)
            document.fonts.load(`${NAME}px "VCR OSD Mono"`).then(() => { this.fontReady = true; this.changed(false); }, () => {});
        const gl = renderer.gl;
        const files = Promise.all(SHADER_FILES.map(name => fetch(`${base}orb/${name}`).then(r => r.ok ? r.text() : Promise.reject())));
        const fog = new Promise((resolve, reject) => {
            const image = new Image();
            image.onload = () => resolve(image);
            image.onerror = reject;
            image.src = `${base}orb/fog.png`;
        });
        Promise.all([files, fog]).then(([sources, image]) => {
            const source = Object.fromEntries(SHADER_FILES.map((name, i) => [name, sources[i]]));
            this.programs = {};
            for (const [name, m] of Object.entries(MATERIALS)) {
                this.programs[name] = renderer.link3D(`orb/${name}`, fromQt3D(source[m.vertex], "vertex"),
                                                      fromQt3D(m.source || source[m.fragment], "fragment", m.properties));
                if (!this.programs[name])
                    throw new Error(`orb/${name}`);
            }
            this.fog = renderer.texture(gl.LINEAR);
            gl.bindTexture(gl.TEXTURE_2D, this.fog);
            gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
            gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, image);
            gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.BROWSER_DEFAULT_WEBGL);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
            gl.generateMipmap(gl.TEXTURE_2D);
            this.meshes = this.makeMeshes();
            this.target = renderer.msaaTarget(SW, SH);
            this.screen = { canvas: document.createElement("canvas"), texture: renderer.texture(gl.LINEAR), key: null };
            this.screen.canvas.width = 760;
            this.screen.canvas.height = 570;
            this.ready = true;
            this.changed(false);
        }).catch(() => {
            this.failed = true;
            this.changed(true);
        });
    }

    makeMeshes() {
        const panel = chamfered(0, 0, PANEL_W, PANEL_H, 24), info = chamfered(0, 0, INFO_W, INFO_H, 22);
        const tabY = PANEL_H / 2 - (PANEL.tabBottom - PANEL.top);
        const made = {
            rectangle: RECTANGLE, sphere: sphere(64, 32), barOn: bar(true), barOff: bar(false),
            panelSlab: slab(panel, -8, 10), panelTube: tube(panel, 6, 10, true),
            tabLine: tube([[-PANEL_W / 2 + 12, tabY, 0], [PANEL_W / 2 - 12, tabY, 0]], 3, 8, false),
            infoSlab: slab(info, -8, 10), infoTube: tube(info, 6, 10, true),
        };
        // The brackets: a plate on the panel's edge with jaws over it, and
        // two arms out past the screen's edge; each part with its glass.
        const plate = { core: 0.1, strength: 1.2, power: 1.5 }, arm = { core: 0.08, strength: 1.1, power: 1.4 };
        this.brackets = {
            list: [[slab([[2, 150], [30, 150], [30, -110], [2, -110]], -4, 38), plate],
                   [slab(chamfered(12, 162, 46, 24, 6), -4, 48), plate],
                   [slab(chamfered(12, -122, 46, 24, 6), -4, 48), plate],
                   [slab([[52, 44], [330, 104], [330, 52], [52, -4]], -4, 30), arm],
                   [slab([[52, -4], [300, -150], [300, -196], [52, -48]], -4, 26), arm]],
            info: [[slab([[-30, 70], [-2, 70], [-2, -70], [-30, -70]], -4, 38), plate],
                   [slab(chamfered(-12, 82, 46, 24, 6), -4, 48), plate],
                   [slab(chamfered(-12, -82, 46, 24, 6), -4, 48), plate],
                   [slab([[-52, 24], [-300, 110], [-300, 62], [-52, -20]], -4, 28), arm],
                   [slab([[-52, -20], [-280, -120], [-280, -164], [-52, -62]], -4, 24), arm]],
        };
        for (const side of Object.values(this.brackets))
            for (const part of side)
                part[0] = this.upload(part[0]);
        return Object.fromEntries(Object.entries(made).map(([name, mesh]) => [name, this.upload(mesh)]));
    }

    // A mesh on the GPU, with the middle of its bounds (what the parts are
    // drawn back to front by).
    upload({ positions, normals, uvs, indexes }) {
        const gl = this.r.gl;
        const vao = gl.createVertexArray();
        gl.bindVertexArray(vao);
        [[positions, 3], [normals, 3], [uvs, 2]].forEach(([data, size], attribute) => {
            if (!data)
                return;
            gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
            gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(data.flat()), gl.STATIC_DRAW);
            gl.enableVertexAttribArray(attribute);
            gl.vertexAttribPointer(attribute, size, gl.FLOAT, false, 0, 0);
        });
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, gl.createBuffer());
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(indexes), gl.STATIC_DRAW);
        gl.bindVertexArray(null);
        const lo = [0, 1, 2].map(i => Math.min(...positions.map(p => p[i])));
        const hi = [0, 1, 2].map(i => Math.max(...positions.map(p => p[i])));
        return { vao, count: indexes.length, center: lo.map((v, i) => (v + hi[i]) / 2) };
    }

    // --- The look's clock and what moves with it.
    // The screen opened, and back from a game: everything in its place, the
    // look fading in.
    open(now) {
        this.launchedAt = null;
        this.fadeStart = now;
        this.lifts = [];
        this.rowOn = [];
        this.away = { from: this.away.to, to: this.away.to, start: now };
        this.down = { nudge: 0, speed: 0 };
        this.across = { nudge: 0, speed: 0 };
    }
    get launching() {
        return this.launchedAt !== null;
    }
    // How far the panels have swung out for a game, 0 to 1 (the help line and
    // the status icons go with them).
    get swing() {
        return clamp01(this.since / 1.3);
    }
    get since() {
        return this.launching ? Math.max(0, this.time - this.launchedAt) : 0;
    }
    // How much of the look shows: fading in as it opens, out as a game starts.
    opacity(now) {
        const fading = now - this.fadeStart;
        return fading < 300 ? clamp01(fading / 300) : 1 - clamp01((this.since - 2.05) / 0.4);
    }
    // Each key a nudge to the whole, as if pushed on the side it points to: ▼
    // tips its foot away and its top towards us, ▲ the other way; ► turns its
    // right side away, ◄ its left.
    nudge(across, down) {
        this.across.speed += KICK * across;
        this.down.speed += KICK * down;
    }
    // A game started: false while one is starting already.
    launch(now) {
        if (this.launching)
            return false;
        this.launchedAt = this.time;
        this.launchStart = now;
        return true;
    }
    // A frame on: the seconds the look has been up, the spring in steps of 4
    // ms at most, and a start that has played out back on the list.
    step(now) {
        const dt = this.lastNow === null ? 0 : Math.min(0.1, Math.max(0, now - this.lastNow) / 1000);
        this.lastNow = now;
        this.time += dt;
        const steps = Math.max(1, Math.ceil(dt / 0.004)), h = dt / steps;
        for (const axis of [this.down, this.across]) {
            if (axis.nudge === 0 && axis.speed === 0)
                continue;
            for (let i = 0; i < steps; ++i) {
                axis.speed += (-SPRING_W * SPRING_W * axis.nudge - 2 * DAMPING * SPRING_W * axis.speed) * h;
                axis.nudge += axis.speed * h;
            }
            if (Math.abs(axis.nudge) < 1e-4 && Math.abs(axis.speed) < 1e-3)
                axis.nudge = axis.speed = 0;
        }
        if (this.launching && now - this.launchStart >= LAUNCH_MS)
            this.open(now);
    }
    wave(hz, phase) {
        return Math.sin(2 * Math.PI * hz * this.time + phase);
    }
    // How far a row stands out, 0 to 1: the selected one comes out at once, a
    // little past and back; one the cursor has left goes back slowly; each
    // from where it is.
    lift(i, now) {
        const l = this.lifts[i];
        if (!l)
            return 0;
        const t = clamp01((now - l.start) / l.duration);
        return l.from + (l.to - l.from) * l.ease(t);
    }
    follow(rows, selectedRow, now) {
        const fresh = this.rowOn.length === 0;
        rows.forEach((entry, i) => {
            const on = !!entry && i === selectedRow;
            if (fresh)
                this.lifts[i] = { from: on ? 1 : 0, to: on ? 1 : 0, start: now, duration: 1, ease: t => t };
            else if (on !== this.rowOn[i])
                this.lifts[i] = on ? { from: this.lift(i, now), to: 1, start: now, duration: 180, ease: outBack }
                                   : { from: this.lift(i, now), to: 0, start: now, duration: 800, ease: inOutQuad };
            this.rowOn[i] = on;
        });
    }
    // The orb gives way to Search's keys, in 200 ms.
    orbOpacity(away, now) {
        const shown = () => this.away.from + (this.away.to - this.away.from) * clamp01((now - this.away.start) / 200);
        if ((away ? 0 : 1) !== this.away.to)
            this.away = { from: shown(), to: away ? 0 : 1, start: now };
        return shown();
    }

    // --- A sheet's picture: drawn again, and mipmapped, as what it shows
    // changes (key).
    sheet(id, w, h, key, paint) {
        const gl = this.r.gl;
        const width = Math.max(1, Math.round(w * SHEET_SCALE)), height = Math.max(1, Math.round(h * SHEET_SCALE));
        let s = this.sheets.get(id);
        if (!s || s.canvas.width !== width || s.canvas.height !== height) {
            if (s)
                gl.deleteTexture(s.texture);
            s = { canvas: document.createElement("canvas"), texture: this.r.texture(gl.LINEAR), key: null };
            s.canvas.width = width;
            s.canvas.height = height;
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
            this.sheets.set(id, s);
        }
        key = `${this.fontReady}|${key}`;
        if (s.key !== key) {
            const ctx = s.canvas.getContext("2d");
            ctx.setTransform(1, 0, 0, 1, 0, 0);
            ctx.clearRect(0, 0, width, height);
            ctx.setTransform(width / w, 0, 0, height / h, 0, 0);
            paint(ctx);
            gl.bindTexture(gl.TEXTURE_2D, s.texture);
            gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, s.canvas);
            gl.generateMipmap(gl.TEXTURE_2D);
            s.key = key;
        }
        return s.texture;
    }

    // The orb's screen, 4:3, at twice the design's size (OrbLook.qml's
    // screen): no picture on a card freshly made, so which file it would be,
    // and that RetroAchievements' pictures need the Web API key.
    screenPicture(current, onSearch, look, colors) {
        const shown = current && !current.search && !onSearch;
        const key = `${this.fontReady}|${shown ? current.set : ""}|${look.arcade.webApiKey}|${colors.letters}|${colors.ink}`;
        const s = this.screen, gl = this.r.gl;
        if (s.key !== key) {
            const ctx = s.canvas.getContext("2d");
            ctx.fillStyle = "#000000";
            ctx.fillRect(0, 0, 760, 570);
            if (shown) {
                const width = 760 * 0.7, x = (760 - width) / 2;
                const lines = [["NO PICTURE", NAME * 2, colors.letters], [`snap/${current.set}.png`, TEXT * 2, colors.ink, "middle"]];
                if (!look.arcade.webApiKey)
                    lines.push(["NO WEB API KEY", TEXT * 2, colors.ink]);
                const height = lines.reduce((sum, [, size], i) => sum + lineHeight(size) + (i ? 24 : 0), 0);
                let top = (570 - height) / 2;
                for (const [text, size, color, elide] of lines) {
                    const shownText = elide ? elidedMiddle(ctx, size, text, width) : elided(ctx, size, text, width);
                    drawText(ctx, shownText, x + (width - measure(ctx, size, shownText)) / 2, top, size, css(color));
                    top += lineHeight(size) + 24;
                }
            }
            gl.bindTexture(gl.TEXTURE_2D, s.texture);
            gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, s.canvas);
            s.key = key;
        }
        return s.texture;
    }

    // --- The scene, drawn into its target: a texture of the look, its glass's
    // light with no alpha, to go over the window's face with what is under it
    // (premultiplied), or null while it isn't in yet.
    render(look, ui, now) {
        if (!this.ready)
            return null;
        this.step(now);
        const r = this.r, gl = r.gl;
        const L = arcadeLayout(look);
        const a = look.arcade;
        const filter = arcadeFilter(ui);
        const onSearch = filter.id === "search";
        const count = filter.games.length;
        const rows = arcadeRows(L, filter, ui);
        const selectedRow = arcadeSelectedRow(L, filter, ui);
        const current = rows[selectedRow] ? rows[selectedRow].game : null;
        const pairs = arcadeHintPairs(ui, filter, current);
        const foot = arcadeFoot(this.measurer, look, L, pairs);
        this.follow(rows, selectedRow, now);

        const ink = rgbOf(look.primary), ground = rgbOf(look.surface);
        const glow = mix(ink, [1, 1, 0.63], 0.38), sea = turned(ink, 0.27);
        const letters = mix(ink, [1, 1, 1], 0.55), darkLetters = mix(ground, ink, 0.1);
        const colors = { ink, letters };

        const since = this.since, swing = this.swing, flung = Math.pow(swing, 2.2);
        const centred = smoothstep(since / 0.9), dolly = smoothstep((since - 0.25) / 1.5);
        const open = smoothstep((since - 1.15) / 0.6);
        // The sway: slow waves of different lengths, calmed as a game starts.
        const calm = 1 - clamp01(since / 0.5);
        const tilt = n => calm * TILT_MOST * Math.tanh(n / 1.6);
        const yaw = calm * (3.2 * this.wave(0.23, 0) + 1.2 * this.wave(0.41, 1.3));
        const pitchAngle = calm * (2.0 * this.wave(0.19, 0.7) + 0.8 * this.wave(0.53, 2.1));
        const roll = calm * 0.6 * this.wave(0.31, 0.4);
        const bob = calm * 6 * this.wave(0.17, 1.9);

        const parts = [];
        const M = this.meshes;
        const glass = (mesh, model, { tint = ink, core = 0.05, strength = 1.0, power = 2.0, fade = 1 } = {}) =>
            parts.push({ program: "glass", mesh, model, uniforms: { tint: linear(tint), core, strength, power, fade } });
        const sheet = (id, model, w, h, opacity, key, paint) => {
            if (opacity > 0)
                parts.push({ program: "sheet", mesh: M.rectangle, model: node(model, { s: [w / 100, h / 100, 1] }),
                             uniforms: { opacity }, textures: { sheet: this.sheet(id, w, h, key, paint) } });
        };

        // Fog: three layers at three depths, drifting, the further the darker
        // (Settings → Orb Fog: the fog picture's, or worked out as it drifts).
        const live = look.orbFogLive;
        const fogs = live
            ? [[-500, ink, 0.7, 3.0, 1.3, 0.55], [-1100, sea, 0.55, 2.3, 7.7, 0.45], [-1900, mix(ink, ground, 0.25), 0.75, 1.6, 3.1, 0.35]]
            : [[-500, ink, 0.52, 1.2, 0.13, 0.55], [-1100, sea, 0.42, 0.92, 0.77, 0.45], [-1900, mix(ink, ground, 0.25), 0.56, 0.64, 0.31, 0.35]];
        for (const [z, tint, strength, scale, seed, radius] of fogs) {
            // As big as the view at its depth, and half again.
            const high = 2 * (REST - z) * Math.tan(FOV / 2 * Math.PI / 180) * 1.5;
            parts.push({ program: live ? "fogLive" : "fog", mesh: M.rectangle,
                         model: node(IDENTITY, { z, s: [high * 4 / 3 / 100, high / 100, 1] }),
                         uniforms: { tint: linear(tint), strength, scale, seed, radius, phase: this.time / (live ? 12 : 120) },
                         textures: { fogNoise: this.fog } });
        }

        // The whole, swaying round the screen's middle and nudged by the keys;
        // the design's orb and panels in the Arcade screen's area, as large as
        // fit above the help line.
        const dash = node(IDENTITY, { y: bob, rx: pitchAngle + tilt(this.down.nudge), ry: yaw + tilt(this.across.nudge), rz: roll });
        const s = L.orbScale;
        const areaX = L.orbLeft - 480 + 360 * s, areaY = 360 - L.areaTop - 270 * s;
        const area = node(dash, { x: areaX, y: areaY, s: [s, s, s] });

        // The orb: the game's picture inside, glass round it; it comes to the
        // middle as a game starts, and gives way to Search's keys.
        const orbFade = this.orbOpacity(onSearch, now);
        const orb = node(area, { x: sx(ORB.x) + (-areaX / s - sx(ORB.x)) * centred,
                                 y: sy(ORB.y) + (-areaY / s - sy(ORB.y)) * centred, z: ORB.z });
        parts.push({ program: "screen", mesh: M.rectangle, depth: true,
                     model: node(orb, { z: -2, s: [ORB.r * 1.9 * 4 / 3 / 100, ORB.r * 1.9 / 100, 1] }),
                     uniforms: { tint: linear(sea), open, fade: orbFade },
                     textures: { picture: this.screenPicture(current, onSearch, look, colors) } });
        glass(M.sphere, node(orb, { s: [ORB.r / 50, ORB.r / 50, ORB.r / 50] }),
              { tint: sea, core: 0.03 * (1 - open), strength: 1.5 * (1 - open), power: 2.4, fade: orbFade });
        // The credits bought, where COIN buys them, on the glass at its foot.
        const isGame = !!current && !current.search;
        if (isGame && !onSearch) {
            const text = `CREDIT ${ui.credits}`;
            sheet("credit", node(orb, { y: -0.7 * ORB.r, z: ORB.r * 0.75 }), 200, 40, (1 - swing) * orbFade,
                  `${text}|${letters}|${ink}|${ground}`, ctx => {
                      const w = measure(ctx, TEXT, text) + 30, h = lineHeight(TEXT) + 6;
                      const x = (200 - w) / 2, y = (40 - h) / 2;
                      ctx.beginPath();
                      ctx.roundRect(x + 1, y + 1, w - 2, h - 2, h / 2 - 1);
                      ctx.fillStyle = css(ground, 0.85);
                      ctx.fill();
                      ctx.lineWidth = 2;
                      ctx.strokeStyle = css(ink);
                      ctx.stroke();
                      drawText(ctx, text, x + 15, y + 3, TEXT, css(letters));
                  });
        }

        // The list's panel, its right side back, held from the right by arms
        // on a hinge past the screen's edge, which it swings out on.
        const listHinge = node(area, { x: 620, y: sy((PANEL.top + PANEL.bottom) / 2), z: -130, ry: 105 * flung });
        const listPanel = node(listHinge, { x: sx((PANEL.left + PANEL.right) / 2) - 620, z: 130, rx: -3, ry: 16 });
        glass(M.panelSlab, listPanel, { core: 0.06, strength: 0.9, power: 1.0 });
        glass(M.panelTube, listPanel, { core: 0.25, strength: 1.4, power: 1.6 });
        glass(M.tabLine, listPanel, { tint: glow, core: 0.3, strength: 1.2 });
        const page = Math.floor(ui.selected / L.perPage), pages = Math.max(1, Math.ceil(count / L.perPage));
        const pitch = (PANEL.rowsBottom - PANEL.rowsTop - ROW_H) / Math.max(1, L.perPage - 1);
        rows.forEach((entry, i) => {
            if (!entry)
                return;
            const on = i === selectedRow;
            const lift = this.lift(i, now);
            const row = node(listPanel, { x: -12 * lift, y: PANEL_H / 2 - (PANEL.rowsTop - PANEL.top) - i * pitch - ROW_H / 2,
                                          z: LIFT_BY * lift });
            glass(on ? M.barOn : M.barOff, row, { tint: on ? glow : ink, power: 1.4, strength: on ? 0.7 : 1.1,
                                                  core: on ? 0.62 + 0.08 * this.wave(1.2, 0) : 0.16 + 0.12 * lift });
            // The game's name, scrolled through while it is selected and too
            // long for its bar; Search's glass on its own row; ▲ ▼ where
            // there is a page before or after.
            const name = (entry.game.search ? ui.words || "Search" : entry.game.name).toUpperCase();
            const mark = entry.game.search ? "search" : "";
            const arrow = i === 0 && page > 0 ? "▲" : i === L.perPage - 1 && page < pages - 1 ? "▼" : "";
            const rowInk = on ? darkLetters : letters;
            const w = PANEL_W - 40;
            const arrowX = arrow ? w - 12 - measure(this.measurer, 24, arrow) : w;
            const markX = (arrow ? arrowX - 8 : w - 8) - (mark ? iconWidth(mark, 3) : 0);
            const clipW = (mark ? markX - 8 : arrow ? arrowX - 8 : w - 8) - 10;
            const full = measure(this.measurer, NAME, name);
            const by = on && full > clipW ? Math.round(scrollOffset(now - ui.since, clipW - full) * SHEET_SCALE) / SHEET_SCALE : 0;
            sheet(`row${i}`, node(row, { z: 22 }), w, ROW_H, 1, `${name}|${on}|${mark}|${arrow}|${by}|${rowInk}|${glow}`, ctx => {
                const top = (ROW_H - lineHeight(NAME)) / 2;
                clipped(ctx, 10, 0, clipW, ROW_H, () =>
                    drawText(ctx, on ? name : elided(ctx, NAME, name, clipW), 10 + by, top, NAME, css(rowInk)));
                if (mark)
                    drawIcon(ctx, mark, markX, (ROW_H - iconHeight(mark, 3)) / 2, css(rowInk), 3);
                if (arrow)
                    drawText(ctx, arrow, arrowX, (ROW_H - lineHeight(24)) / 2, 24, css(on ? rowInk : glow));
            });
        });
        // The tab's letters: the filter's name, ◄ ► where there are others,
        // and the page of so many (on Search, how many games its words find).
        {
            const w = PANEL_W, h = PANEL.tabBottom - PANEL.top;
            const arrows = ARCADE_FILTERS.length > 1;
            const right = (onSearch ? `${count - 1} Found` : count === 0 ? "" : `${page + 1}/${pages}`).toUpperCase();
            const label = filter.name.toUpperCase();
            sheet("tab", node(listPanel, { y: PANEL_H / 2 - h / 2, z: 20 }), w, h, 1, `${label}|${right}|${letters}|${glow}`, ctx => {
                const middle = size => (h - lineHeight(size)) / 2;
                const rightX = w - (arrows ? 50 : 20) - measure(ctx, TEXT, right);
                if (arrows) {
                    drawText(ctx, "◄", 18, middle(TEXT), TEXT, css(glow));
                    drawText(ctx, "►", w - 20 - measure(ctx, TEXT, "►"), middle(TEXT), TEXT, css(glow));
                }
                drawText(ctx, right, rightX, middle(TEXT), TEXT, css(letters));
                // Smaller, down to the hint bar's smallest, before it is cut
                // short.
                const room = rightX - 12 - 50;
                let size = NAME;
                while (size > 21 && measure(ctx, size, label) > room)
                    --size;
                drawText(ctx, elided(ctx, size, label, room), 50, middle(size), size, css(letters));
            });
        }
        // Under Search's own row, that its words find no game.
        if (onSearch && count === 1) {
            sheet("empty", node(listPanel, { y: PANEL_H / 2 - (PANEL.rowsTop - PANEL.top) - pitch - 60, z: 20 }),
                  PANEL_W - 40, 120, 1, `${letters}`, ctx => drawText(ctx, "NO GAME FOUND", 10, 0, NAME, css(letters)));
        }
        const listBracket = node(listPanel, { x: PANEL_W / 2 });
        for (const [mesh, how] of this.brackets.list)
            glass(mesh, listBracket, how);

        // The details' panel, its left side back, held from the left on a
        // hinge past the screen's left edge, swinging out the other way: the
        // game's high scores, who made it and when, how many play it and how
        // long it has been played.
        const infoHinge = node(area, { x: -600, y: sy((INFO.top + INFO.bottom) / 2), z: -95, ry: -105 * flung });
        const infoPanel = node(infoHinge, { x: sx((INFO.left + INFO.right) / 2) + 600, z: 105, rx: -4, ry: -12 });
        glass(M.infoSlab, infoPanel, { core: 0.06, strength: 0.9, power: 1.0 });
        glass(M.infoTube, infoPanel, { core: 0.25, strength: 1.4, power: 1.6 });
        const infoBracket = node(infoPanel, { x: -INFO_W / 2 });
        for (const [mesh, how] of this.brackets.info)
            glass(mesh, infoBracket, how);
        const game = isGame ? current : null;
        const lines = !game ? [] : [
            { label: "Hi-Score", value: "", heading: true }, { label: "No Scores Yet", value: "" },
            { label: "", value: "" }, { label: "", value: "" },
            { label: game.maker || "?", value: game.year || "?", heading: true },
            { label: game.players > 0 ? `${game.players} ${game.players === 1 ? "Player" : "Players"}` : "? Players",
              value: "Never", heading: true },
        ];
        sheet("info", node(infoPanel, { z: 14 }), INFO_W, INFO_H, 1, `${JSON.stringify(lines)}|${ink}|${glow}|${letters}`, ctx => {
            const width = INFO_W - 48;
            lines.forEach((line, i) => {
                const y = 16 + i * 29;
                const value = line.value.toUpperCase();
                const valueFull = measure(ctx, TEXT, value);
                const label = line.label.toUpperCase();
                drawText(ctx, elided(ctx, TEXT, label, width - (value ? valueFull + 16 : 0)), 24, y, TEXT,
                         css(line.heading ? glow : ink));
                const shownValue = elided(ctx, TEXT, value, Math.min(valueFull, width * 0.6));
                drawText(ctx, shownValue, 24 + width - measure(ctx, TEXT, shownValue), y, TEXT, css(letters));
            });
        });

        // The keys at the foot, in the hint bar's place: back's and select's
        // buttons in glass from the bar's ends inwards (the status icons, if
        // any, at its right end), what the rest do between them; they drop
        // away as the panels go.
        if (a.hintBar) {
            const fade = 1 - swing;
            const footY = sy(L.areaBottom - foot.bar.h / (2 * L.v)) - 260 * flung;
            const footNode = node(dash, { y: footY });
            const back = pairs[0];
            const select = pairs.slice(1).find(p => p.key === "[ENTER]") || null;
            const key = (pair, outwards, edge) => {
                if (!pair)
                    return edge;
                const label = pair.key.replace(/^\[|\]$/g, "");
                const word = pair.word.toUpperCase();
                const wide = Math.max(44, measure(this.measurer, NAME, label) + 24);
                const wordW = measure(this.measurer, NAME, word);
                const keyNode = node(footNode, { x: sx(edge - outwards * (wordW + 8 + wide / 2)), z: 20 });
                glass(M.sphere, node(keyNode, { s: [wide / 100, 0.44, 0.2] }), { tint: glow, core: 0.55, strength: 0.9, power: 1.4, fade });
                sheet(`key${outwards}`, node(keyNode, { z: 14 }), wide, 40, fade, `${label}|${ground}`, ctx =>
                    drawText(ctx, label, (wide - measure(ctx, NAME, label)) / 2, (40 - lineHeight(NAME)) / 2, NAME, css(ground)));
                sheet(`word${outwards}`, node(keyNode, { x: outwards * (wide / 2 + 8 + wordW / 2), z: 10 }), wordW + 8, 40, fade,
                      `${word}|${letters}`, ctx => drawText(ctx, word, 4, (40 - lineHeight(NAME)) / 2, NAME, css(letters)));
                return edge - outwards * (wordW + 8 + wide);
            };
            const from = key(back, -1, L.areaLeft + 12) + 16;
            const to = key(select, 1, L.areaRight - 12 - (a.helpLine ? 0 : foot.statusRoom / L.u)) - 16;
            const between = pairs.filter(p => p !== back && p !== select).map(p => `${p.key}:${p.word}`).join("  ").toUpperCase();
            const w = Math.max(40, to - from);
            sheet("between", node(footNode, { x: sx((from + to) / 2), z: 30 }), w, 40, fade, `${between}|${letters}`, ctx => {
                // The hint bar's letters, smaller as far as the room between
                // the buttons needs.
                const size = Math.max(21, Math.min(27, Math.floor(27 * w / Math.max(1, measure(ctx, 27, between)))));
                const text = elided(ctx, size, between, w);
                drawText(ctx, text, (w - measure(ctx, size, text)) / 2, (40 - lineHeight(size)) / 2, size, css(letters, 0.85));
            });
        }

        // Drawn back to front, as Qt Quick 3D draws what is see-through: the
        // glass and the fog adding their light, the orb's screen over what is
        // behind it and hiding the orb's far side, the letters over the rest.
        const camera = [0, 0, REST - (REST - s * (ORB.z + 800)) * dolly];
        const view = translation(-camera[0], -camera[1], -camera[2]);
        const viewProjection = multiply(projection(SW / SH), view);
        for (const part of parts) {
            const c = point(part.model, part.mesh.center);
            part.distance = (c[0] - camera[0]) ** 2 + (c[1] - camera[1]) ** 2 + (c[2] - camera[2]) ** 2;
        }
        parts.sort((p, q) => q.distance - p.distance);

        gl.bindFramebuffer(gl.FRAMEBUFFER, this.target.framebuffer);
        gl.viewport(0, 0, SW, SH);
        gl.clearColor(0, 0, 0, 0);
        gl.clearDepth(1);
        gl.depthMask(true);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LEQUAL);
        gl.enable(gl.BLEND);
        for (const part of parts) {
            const program = this.programs[part.program];
            gl.useProgram(program.program);
            const added = part.program === "glass" || part.program.startsWith("fog");
            gl.blendFunc(gl.ONE, added ? gl.ONE : gl.ONE_MINUS_SRC_ALPHA);
            gl.depthMask(!!part.depth);
            r.setUniforms(program, { MODEL_MATRIX: part.model, NORMAL_MATRIX: normalMatrix(part.model),
                                     MODELVIEWPROJECTION_MATRIX: multiply(viewProjection, part.model),
                                     CAMERA_POSITION: camera, ...part.uniforms }, part.textures || {});
            gl.bindVertexArray(part.mesh.vao);
            gl.drawElements(gl.TRIANGLES, part.mesh.count, gl.UNSIGNED_SHORT, 0);
        }
        gl.bindVertexArray(null);
        gl.depthMask(true);
        gl.disable(gl.DEPTH_TEST);
        return r.resolve(this.target);
    }
}

// Cut short in the middle with "…", as Text.ElideMiddle cuts it.
function elidedMiddle(ctx, size, s, width) {
    if (measure(ctx, size, s) <= width + 0.01)
        return s;
    for (let n = s.length - 1; n > 0; --n) {
        const t = s.slice(0, Math.ceil(n / 2)) + "…" + s.slice(s.length - Math.floor(n / 2));
        if (measure(ctx, size, t) <= width + 0.01)
            return t;
    }
    return "…";
}
