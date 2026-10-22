// OSD/OS Setup's live preview: the main menu and the Arcade screen as the app
// draws them at 640×480, in the look the form holds, moving as they move on
// the TV: the app's own shaders for the background, the screen and the text
// (shaders/*.frag, on WebGL 2), its selector effects (SelectorFx), and its
// window changes as the preview goes from one screen to the other. Clicked,
// it takes the remote's keys: ▲ ▼ to move, ◄ ► the Arcade screen's filters,
// select on ARCADE to open it, back to leave it, 5 for a coin. One element,
// kept as the page draws its form again; update() gives it the settings.
import { lookOf, rgbOf, ARCADE } from "./look.mjs";
import { Assets } from "./assets.mjs";
import { SelectorFx, FRAME } from "./selector.mjs";
import { SW, SH, PX, drawGround, drawMainMenu, mainMenuTop, drawArcade, ARCADE_FILTERS, arcadeFilter, arcadeLayout,
         arcadeSelectedRow, screenMoves, KEY_ROWS, lastKeyOf, columnOf } from "./screens.mjs";
import { Renderer } from "./renderer.mjs";

const TICK = FRAME * 1000;
// Main.qml's window changes: how long each takes, and how it eases.
const DURATIONS = { Cube: 650, Wave: 900, Drop: 1000 };
const EASINGS = {
    Wave: t => t,
    Drop: t => 1 - (1 - t) * (1 - t),
    other: t => t < 0.5 ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t),
};
const SHADED_KNOBS = ["scanlines", "curvature", "glow", "bleed", "noise", "vignette"];

// One face of the cube as it turns (Main.qml's cubeMatrix()), column after
// column as WebGL takes a matrix.
function cubeMatrix(isNew, progress, direction) {
    const across = direction < 2;
    const w = across ? SW : SH, h = w / 2;
    const turn = progress * Math.PI / 2, c = Math.cos(turn), s = Math.sin(turn);
    let a, b, z, e;
    if (!isNew) {
        a = c; b = h - h * c - h * s; z = -s; e = h + h * s - h * c;
    } else {
        a = s; b = h + h * c - h * s; z = c; e = h - h * s - h * c;
    }
    if (direction % 2 === 1) {
        b = w - a * w - b;
        e = z * w + e;
        z = -z;
    }
    const d = 2.5 * w, cx = SW / 2, cy = SH / 2;
    const rows = across
        ? [[a + cx * z / d, 0, 0, b + cx * e / d], [cy * z / d, 1, 0, cy * e / d], [0, 0, 0, 0], [z / d, 0, 0, 1 + e / d]]
        : [[1, cx * z / d, 0, cx * e / d], [0, a + cy * z / d, 0, b + cy * e / d], [0, 0, 0, 0], [0, z / d, 0, 1 + e / d]];
    const out = new Float32Array(16);
    for (let row = 0; row < 4; ++row)
        for (let col = 0; col < 4; ++col)
            out[col * 4 + row] = rows[row][col];
    return out;
}
// A colour's channels, 0 to 255, as SelectorFx takes its ink and paper.
const bytesOf = hex => { const n = parseInt(hex.slice(1), 16); return [n >> 16 & 255, n >> 8 & 255, n & 255]; };
// Whether a face is turned towards the eye (Main.qml's facing()).
const facing = (isNew, progress) => (isNew ? Math.sin : Math.cos)(progress * Math.PI / 2) * (2 * 2.5 + 1) > 1;

function el(tag, attrs = {}, ...children) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(attrs)) {
        if (key === "class") node.className = value;
        else if (key === "text") node.textContent = value;
        else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
        else node.setAttribute(key, value);
    }
    node.append(...children);
    return node;
}

export class Preview {
    // looks: schema.json's; base: where the page's files are.
    constructor(looks, base = "") {
        this.looks = looks;
        this.look = null;
        this.home = "main";
        this.screen = "main";
        this.main = { index: 0, top: 0, since: 0 };
        this.arcade = { filter: 0, selected: 0, wheelTop: 0, credits: 0, since: 0,
                        words: "", typing: false, keysRow: 0, keysCol: 0 };
        this.fxTime = 0;
        this.change = null;
        // Drawn again from scratch at the next frame: the ground and the view.
        this.dirty = true;
        this.selectors = { back: new SelectorFx(SW / PX, SH / PX), front: new SelectorFx(SW / PX, SH / PX) };

        this.canvas = el("canvas", { width: SW, height: SH, tabindex: "0", class: "screen",
                                     "aria-label": "OSD/OS as it will look, live: click it, then use the arrow keys, Enter and Escape" });
        this.tabs = {};
        const tabRow = el("div", { class: "tabs", role: "tablist", "aria-label": "Preview" });
        for (const [screen, label] of [["main", "Main Menu"], ["arcade", "Arcade"]]) {
            this.tabs[screen] = el("button", { type: "button", role: "tab", class: "tab", id: `preview-${screen}`,
                                               onclick: () => this.go(screen) }, label);
            tabRow.append(this.tabs[screen]);
        }
        this.caption = el("figcaption", {});
        this.element = el("figure", { class: "preview" }, tabRow, this.canvas, this.caption);

        this.groundCanvas = el("canvas", { width: SW, height: SH });
        this.contentCanvas = el("canvas", { width: SW, height: SH });
        this.assets = new Assets(base, () => this.redraw());
        // Everything with the deck's letters once they are in.
        if (document.fonts)
            document.fonts.load(`24px "VCR OSD Mono"`).then(() => this.redraw(), () => {});

        this.renderer = null;
        this.flat = null;
        try {
            this.renderer = new Renderer(this.canvas, SW, SH, {});
        } catch {
            this.flat = this.canvas.getContext("2d");
        }
        if (this.renderer) {
            Promise.all(looks.shaders.map(name => fetch(`${base}shaders/${name}.frag`)
                .then(r => r.ok ? r.text() : "").then(text => [name, text], () => [name, ""])))
                .then(list => {
                    this.renderer.sources = Object.fromEntries(list.filter(([, text]) => text));
                    this.redraw();
                });
            // The GPU reset: nothing drawn until it is back, then all of it
            // made again.
            this.lost = false;
            this.canvas.addEventListener("webglcontextlost", e => {
                e.preventDefault();
                this.lost = true;
                this.change = null;
            });
            this.canvas.addEventListener("webglcontextrestored", () => {
                this.renderer = new Renderer(this.canvas, SW, SH, this.renderer.sources);
                this.lost = false;
                this.redraw();
            });
        }

        this.canvas.addEventListener("keydown", e => this.key(e));
        this.visible = true;
        if ("IntersectionObserver" in window) {
            new IntersectionObserver(entries => {
                this.visible = entries[entries.length - 1].isIntersecting;
                this.wake();
            }).observe(this.element);
        }
        document.addEventListener("visibilitychange", () => this.wake());
        // The next frame asked for: one at a time.
        this.frameId = null;
        this.wake();
    }

    // The settings the form holds (look.mjs's), and the screen OSD/OS opens
    // on with them: the Arcade screen when it starts on the Arcade module.
    update(settings) {
        // The Arcade screen's rows before: a game keeps its row on the wheel
        // as their number changes (ArcadeScreen.qml's onPerPageChanged).
        const before = this.look && arcadeLayout(this.look);
        const row = before ? arcadeSelectedRow(before, arcadeFilter(this.arcade), this.arcade) : 0;
        this.look = lookOf(this.looks, settings);
        const after = arcadeLayout(this.look);
        if (before && after.wheel && (after.perPage !== before.perPage || !before.wheel))
            this.placeWheel(Math.min(row, after.perPage - 1));
        const arcadeScreen = this.look.arcade.screen === "Arcade";
        const startsOnArcade = settings.app.startup_module === ARCADE && arcadeScreen;
        const home = startsOnArcade ? "arcade" : "main";
        if (home !== this.home) {
            this.home = home;
            this.show(home);
        }
        if (!arcadeScreen && this.screen === "arcade")
            this.show("main");
        this.tabs.arcade.hidden = !arcadeScreen;
        this.main.index = Math.min(this.main.index, Math.max(0, this.look.menu.length - 1));
        this.main.top = mainMenuTop(this.look, this.main.index, this.main.top);
        // As Main.qml has its two: the rainbow behind the view in the scheme's
        // colour, the rest in front, over the box in its background's.
        const { back, front } = this.selectors;
        back.setEffect(this.look.selector === "Rainbow" ? "Rainbow" : "");
        front.setEffect(this.look.selector !== "Rainbow" ? this.look.selector : "");
        back.ink = front.ink = bytesOf(this.look.primary);
        front.paper = bytesOf(this.look.surface);
        const theme = this.looks.themes[settings.app.osd_theme];
        this.caption.textContent = `${theme ? theme.name : "No theme, OSD/OS's own look"}, live at 640×480`
            + (this.flat ? " without its effects: this browser has no WebGL 2" : " with OSD/OS's own effects")
            + ". Click it, then ▲ ▼ ◄ ►, Enter and Esc, as on the remote; 5 is a coin.";
        this.markTabs();
        this.redraw();
    }

    redraw() {
        this.dirty = true;
        this.wake();
    }

    markTabs() {
        for (const [screen, tab] of Object.entries(this.tabs)) {
            tab.classList.toggle("chosen", screen === this.screen);
            tab.setAttribute("aria-selected", String(screen === this.screen));
        }
    }

    // A screen at once, as it is when OSD/OS opens on it.
    show(screen) {
        this.screen = screen;
        this.change = null;
        if (screen === "arcade")
            this.openArcade();
        this.markTabs();
        this.redraw();
    }

    // The Arcade module opened: its first filter with games (Start Filter's
    // if it has any), on its first game, in the wheel's middle row; no
    // credits, and Search with no words.
    openArcade() {
        const start = ARCADE_FILTERS.findIndex(f => f.id === this.look.arcade.startFilter);
        this.arcade = { filter: Math.max(0, start), selected: 0, wheelTop: 0, credits: 0, since: performance.now(),
                        words: "", typing: false, keysRow: 0, keysCol: 0, passing: null };
        this.placeWheel(Math.floor(arcadeLayout(this.look).perPage / 2));
    }
    placeWheel(row) {
        const L = arcadeLayout(this.look);
        const count = arcadeFilter(this.arcade).games.length;
        this.arcade.wheelTop = count >= L.perPage ? ((this.arcade.selected - row) % count + count) % count : 0;
    }
    // ◄ ►: the next filter round, on the game where it has it; Search on its
    // own row, at the wheel's top, and on past it with the game ◄ ► came to
    // it with while still on that row (ArcadeScreen.qml's cycle()).
    cycle(delta, L) {
        const a = this.arcade;
        const current = arcadeFilter(a).games[a.selected];
        const keep = current && current.search ? a.passing : current;
        a.filter = (a.filter + delta + ARCADE_FILTERS.length) % ARCADE_FILTERS.length;
        const filter = arcadeFilter(a);
        const toSearch = filter.id === "search";
        a.passing = toSearch ? keep : null;
        a.selected = toSearch ? 0 : Math.max(0, filter.games.indexOf(keep));
        this.placeWheel(toSearch ? 0 : Math.floor(L.perPage / 2));
        a.since = performance.now();
    }
    // Search: its keys handed the keys, at the left end of their row, or
    // with a letter typed on a keyboard (the box then on OK); the list for
    // what is typed, on Search's own row; OK down to the first game found.
    enterKeys(typed) {
        const a = this.arcade;
        a.typing = true;
        a.keysCol = 0;
        if (typed)
            this.typeIn(typed);
    }
    typeIn(typed) {
        const a = this.arcade;
        if (a.words.length < 40)
            this.searchFor(a.words + typed.toUpperCase());
        a.keysRow = KEY_ROWS.length - 1;
        a.keysCol = KEY_ROWS[a.keysRow].length - 1;
    }
    searchFor(words) {
        const a = this.arcade;
        a.words = words;
        a.selected = 0;
        this.placeWheel(0);
        a.since = performance.now();
    }
    pressKey(key, L) {
        const a = this.arcade;
        if (key === "OK") {
            a.typing = false;
            if (arcadeFilter(a).games.length > 1) {
                a.selected = 1;
                this.placeWheel(Math.min(1, L.perPage - 1));
            }
        } else if (key === "DEL") {
            this.searchFor(a.words.slice(0, -1));
        } else if (a.words.length < 40) {
            this.searchFor(a.words + (key === "SPACE" ? " " : key));
        }
    }
    // Search's keys while they have the keys (OnScreenKeyboard): the arrows
    // from key to key, ◄ ► pressed at a row's end round the filters (not
    // while held), select types, back returns to the list.
    keysKey(e, L) {
        const a = this.arcade;
        const last = KEY_ROWS.length - 1;
        if (e.key === "ArrowUp" || e.key === "ArrowDown") {
            const down = e.key === "ArrowDown";
            const col = down && a.keysRow === last - 1 ? lastKeyOf(a.keysCol)
                      : !down && a.keysRow === last ? columnOf(a.keysCol) : a.keysCol;
            a.keysRow = (a.keysRow + (down ? 1 : -1) + KEY_ROWS.length) % KEY_ROWS.length;
            a.keysCol = Math.max(0, Math.min(KEY_ROWS[a.keysRow].length - 1, col));
        } else if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
            const step = e.key === "ArrowLeft" ? -1 : 1;
            if (a.keysCol === (step < 0 ? 0 : KEY_ROWS[a.keysRow].length - 1)) {
                if (!e.repeat) {
                    a.typing = false;
                    this.cycle(step, L);
                }
            } else {
                a.keysCol += step;
            }
        } else if (e.key === "Enter") {
            this.pressKey(KEY_ROWS[a.keysRow][a.keysCol], L);
        } else if (e.key === "Escape") {
            a.typing = false;
        } else if (e.key === "Backspace") {
            this.pressKey("DEL", L);
        } else if (e.key.length === 1 && e.key > " ") {
            this.typeIn(e.key);
        } else {
            return false;
        }
        return true;
    }

    // To the other screen as the app changes windows: the face as it was
    // caught, then the transition in force (none: at once).
    go(screen) {
        if (screen === this.screen || !this.look)
            return;
        if (screen === "arcade" && this.look.arcade.screen !== "Arcade")
            return;
        const kind = this.look.transition;
        if (kind && this.renderer && !this.lost) {
            const usable = !["Ripple", "Wave", "Drop"].includes(kind)
                || this.renderer.shader(this.looks.transitionShaders[kind]);
            const r = this.renderer;
            r.bindTarget(r.targets.last);
            r.clear([0, 0, 0, 0]);
            r.draw(r.blit, r.targets.last, { blend: false, textures: { source: r.targets.face.texture } });
            this.change = { kind: usable ? kind : "Fade", start: performance.now(), direction: Math.floor(Math.random() * 4) };
        }
        this.screen = screen;
        if (screen === "arcade")
            this.openArcade();
        else
            this.main.since = performance.now();
        this.markTabs();
        this.redraw();
    }

    key(e) {
        if (!this.look)
            return;
        const now = performance.now();
        if (this.screen === "main") {
            const rows = this.look.menu;
            if (e.key === "ArrowUp" || e.key === "ArrowDown") {
                if (rows.length) {
                    this.main.index = (this.main.index + (e.key === "ArrowUp" ? rows.length - 1 : 1)) % rows.length;
                    this.main.top = mainMenuTop(this.look, this.main.index, this.main.top);
                    this.main.since = now;
                }
            } else if (e.key === "Enter") {
                if (rows[this.main.index] && rows[this.main.index].id === ARCADE)
                    this.go("arcade");
            } else {
                return;
            }
        } else {
            const a = this.arcade;
            const L = arcadeLayout(this.look);
            const filter = arcadeFilter(a);
            const count = filter.games.length;
            const onSearch = filter.id === "search";
            if (a.typing) {
                if (!this.keysKey(e, L))
                    return;
            } else if (e.key === "ArrowUp" || e.key === "ArrowDown") {
                const delta = e.key === "ArrowUp" ? -1 : 1;
                const row = arcadeSelectedRow(L, filter, a) + delta;
                if (L.wheel && count >= L.perPage && (row < 0 || row >= L.perPage))
                    a.wheelTop = (a.wheelTop + delta + count) % count;
                a.selected = (a.selected + delta + count) % count;
                a.since = now;
            } else if (e.key === "ArrowRight" && onSearch) {
                // On Search, its keys lie to the right.
                this.enterKeys("");
            } else if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
                this.cycle(e.key === "ArrowLeft" ? -1 : 1, L);
            } else if (e.key === "Enter" && onSearch && filter.games[a.selected].search) {
                this.enterKeys("");
            } else if (e.key === "5") {
                // A coin, where the credits are shown: not over Search's keys.
                if (!onSearch)
                    a.credits = Math.min(9, a.credits + 1);
            } else if (e.key === "Escape" || e.key === "Backspace") {
                this.go("main");
            } else if (onSearch && e.key.length === 1 && e.key > " ") {
                this.enterKeys(e.key);
            } else {
                return;
            }
        }
        e.preventDefault();
        this.redraw();
    }

    // Running while it can be seen.
    wake() {
        const run = this.visible && document.visibilityState !== "hidden" && !!this.look;
        if (run && this.frameId === null) {
            this.lastTick = performance.now();
            this.frameId = requestAnimationFrame(t => this.frame(t));
        } else if (!run && this.frameId !== null) {
            cancelAnimationFrame(this.frameId);
            this.frameId = null;
        }
    }

    frame(now) {
        this.frameId = requestAnimationFrame(t => this.frame(t));
        // The app's clock: a tick every 33 ms, the effects' time with it
        // while one of them moves, and the selectors a frame on each.
        if (now - this.lastTick > 1000)
            this.lastTick = now - TICK;
        let ticked = false, selected = false;
        while (now - this.lastTick >= TICK) {
            this.lastTick += TICK;
            ticked = true;
            if (this.moving())
                this.fxTime = (this.fxTime + FRAME) % 3600;
            // A selector's new frame, the last one it clears too.
            for (const fx of Object.values(this.selectors))
                selected = (fx.kind ? fx.tick(this.box) : fx.stop()) || selected;
        }
        // Drawn again while anything on it moves, or once anything changed.
        if (!this.lost && (this.dirty || this.change || (ticked && (this.moving() || selected || this.screenMoving))))
            this.draw(now);
    }

    // Whether the effects' time runs (Main.qml's fxMoving).
    moving() {
        const look = this.look;
        return !!look.background || (this.shaded()
            && (look.screen.noise > 0 || look.text.rainbow > 0 || look.text.shimmer > 0 || look.text.flicker > 0));
    }
    shaded() {
        const look = this.look;
        return SHADED_KNOBS.some(k => look.screen[k] > 0) || ["rainbow", "shimmer", "flicker", "glow"].some(k => look.text[k] > 0);
    }

    draw(now) {
        const look = this.look;
        const fresh = this.dirty;
        if (fresh) {
            const ctx = this.groundCanvas.getContext("2d");
            ctx.clearRect(0, 0, SW, SH);
            this.area = drawGround(ctx, look, this.assets);
            this.dirty = false;
        }
        // The view, drawn again only when it changed or moves.
        const redrawn = fresh || this.screenMoving;
        if (redrawn) {
            const ctx = this.contentCanvas.getContext("2d");
            ctx.clearRect(0, 0, SW, SH);
            const box = this.screen === "main" ? drawMainMenu(ctx, look, this.assets, this.main, now)
                                               : drawArcade(ctx, look, this.assets, this.arcade, now);
            this.screenMoving = screenMoves();
            this.box = box ? { x: box.x / PX, y: box.y / PX, w: box.w / PX, h: box.h / PX } : null;
        }

        if (this.flat) {
            this.flat.clearRect(0, 0, SW, SH);
            this.flat.drawImage(this.groundCanvas, 0, 0);
            this.flat.drawImage(this.contentCanvas, 0, 0);
            return;
        }
        const r = this.renderer;
        const T = r.targets;
        const ink = [...rgbOf(look.primary), 1], paper = [...rgbOf(look.surface), 1];
        if (fresh)
            r.upload("ground", this.groundCanvas, SW, SH, r.gl.NEAREST);
        const content = redrawn ? r.upload("content", this.contentCanvas, SW, SH, r.gl.NEAREST) : r.textures.content;

        // The window's face: its ground, the background effect over it in art
        // pixels, the rainbow behind the view, the view, the other selectors.
        r.bindTarget(T.face);
        r.clear([0, 0, 0, 0]);
        r.copy(r.textures.ground, T.face);
        const bg = look.background && r.shader(look.background);
        if (bg) {
            const area = this.area;
            const cols = Math.ceil(area.w / PX), rows = Math.ceil(area.h / PX);
            if (!T.bg || T.bg.width !== cols || T.bg.height !== rows)
                T.bg = r.target(cols, rows, r.gl.NEAREST);
            r.bindTarget(T.bg);
            r.clear([0, 0, 0, 0]);
            r.draw(bg, T.bg, { blend: false, uniforms: { size: [cols, rows], origin: [Math.round(area.x / PX), Math.round(area.y / PX)],
                                                          time: this.fxTime, ink, paper } });
            r.copy(T.bg.texture, T.face, [area.x, area.y, cols * PX, rows * PX]);
        }
        const selectorFrame = (name, fx) => fx.kind && !fx.blank ? r.upload(name, fx.frame, SW / PX, SH / PX, r.gl.NEAREST) : null;
        const back = selectorFrame("selectorBack", this.selectors.back);
        if (back)
            r.copy(back, T.face);
        r.copy(content, T.face);
        const front = selectorFrame("selectorFront", this.selectors.front);
        if (front)
            r.copy(front, T.face);

        // The screen: the window, or a window change playing out.
        r.bindTarget(T.screen);
        r.clear([...rgbOf(look.windowColor), 1]);
        const change = this.change;
        let progress = 1;
        if (change) {
            const t = Math.min(1, (now - change.start) / (DURATIONS[change.kind] || 480));
            progress = (EASINGS[change.kind] || EASINGS.other)(t);
            if (t >= 1)
                this.change = null;
        }
        if (!this.change) {
            r.copy(T.face.texture, T.screen);
        } else if (change.kind === "Fade") {
            r.copy(T.face.texture, T.screen);
            r.copy(T.last.texture, T.screen, null, { opacity: 1 - progress });
        } else if (change.kind === "Cube") {
            r.clear([0, 0, 0, 1]);
            if (facing(false, progress))
                r.copy(T.last.texture, T.screen, null, { transform: cubeMatrix(false, progress, change.direction) });
            if (facing(true, progress))
                r.copy(T.face.texture, T.screen, null, { transform: cubeMatrix(true, progress, change.direction) });
        } else {
            if (change.kind === "Ripple")
                r.clear([0, 0, 0, 1]);
            r.draw(r.shader(this.looks.transitionShaders[change.kind]), T.screen, {
                uniforms: { progress, resolution: [SW, SH], px: PX, ink,
                            corner: [change.direction % 2, Math.floor(change.direction / 2)] },
                textures: { from: T.last.texture, to: T.face.texture } });
        }

        // On to the page, through the screen's and the text's effects.
        const effects = this.shaded() && r.shader("effects");
        if (effects) {
            r.draw(effects, null, { blend: false, textures: { source: T.screen.texture }, uniforms: {
                resolution: [SW, SH], px: PX, time: this.fxTime, ...look.screen,
                rainbow: look.text.rainbow, shimmer: look.text.shimmer, flicker: look.text.flicker,
                inkGlow: look.text.glow, ink, paper } });
        } else {
            r.draw(r.blit, null, { blend: false, textures: { source: T.screen.texture } });
        }
    }
}
