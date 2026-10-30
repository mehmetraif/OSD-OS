// The screens the preview shows, drawn as OSD/OS lays them out at 640×480
// (the QML's own sizes, root.sw and root.sh, and its art pixel, root.px): the
// ground (Components/OsdGround), the main menu (views/ModuleList.qml with
// AppBar, its rows and HintBar) and the Arcade screen
// (modules/arcade/views/ArcadeScreen.qml with ArcadeBar, its list, the
// column, MenuRow, HelpLine and the status icons; in the Orb look, the parts
// it keeps in 2D over the scene orb.mjs draws). Each part is drawn where the
// QML puts it, in the scheme's two colours and the deck's letters; the skins'
// pictures in those colours, nine-sliced on art pixels. The effects are the
// app's own shaders, run over these by the preview (renderer.mjs).
import { bytesOf } from "./look.mjs";

export const SW = 640, SH = 480;
export const PX = Math.max(1, Math.floor(SH / 240));

const FONT = '"VCR OSD Mono"';
// VCR OSD Mono's ascent and descent, of its size, as Qt takes them: its OS/2
// table's (its hhea table has no descent), each in whole pixels. A line's
// baseline is its ascent down it; a line is the two together.
export const ascentOf = size => Math.round(size * 1800 / 2048);
const descentOf = size => Math.round(size * 200 / 2048);
// How far above the baseline the middle of its capitals is (MenuRow's).
const CAP_MIDDLE = 800 / 2048;

// A rectangle of Qt Quick's without antialiasing covers the pixels whose
// middles are inside it.
const snap = v => Math.ceil(v - 0.5);

export function fillRect(ctx, x, y, w, h, color) {
    const x0 = snap(x), y0 = snap(y), x1 = snap(x + w), y1 = snap(y + h);
    if (x1 > x0 && y1 > y0) {
        ctx.fillStyle = color;
        ctx.fillRect(x0, y0, x1 - x0, y1 - y0);
    }
}
// A Rectangle's border, inside it.
function frameRect(ctx, x, y, w, h, color, width) {
    if (width <= 0)
        return;
    fillRect(ctx, x, y, w, width, color);
    fillRect(ctx, x, y + h - width, w, width, color);
    fillRect(ctx, x, y + width, width, h - 2 * width, color);
    fillRect(ctx, x + w - width, y + width, width, h - 2 * width, color);
}
const rgba = (hex, alpha) => `rgba(${bytesOf(hex).join(",")},${alpha})`;

// --- Letters: a Text's line, its box `top` after its top padding.
export const lineHeight = size => ascentOf(size) + descentOf(size);
export function measure(ctx, size, s) {
    ctx.font = `${size}px ${FONT}`;
    return ctx.measureText(s).width;
}
export function drawText(ctx, s, x, top, size, color) {
    ctx.font = `${size}px ${FONT}`;
    ctx.fillStyle = color;
    ctx.textBaseline = "alphabetic";
    ctx.fillText(s, x, top + ascentOf(size));
}
// Cut short with "…" as Text.ElideRight cuts it.
export function elided(ctx, size, s, width) {
    if (measure(ctx, size, s) <= width + 0.01)
        return s;
    let n = s.length;
    while (n > 0 && measure(ctx, size, s.slice(0, n) + "…") > width + 0.01)
        --n;
    return s.slice(0, n) + "…";
}
export function clipped(ctx, x, y, w, h, draw) {
    ctx.save();
    ctx.beginPath();
    ctx.rect(x, y, w, h);
    ctx.clip();
    draw();
    ctx.restore();
}

// Whether anything on the screen drawn last moves by itself (a name or a
// line scrolling through): it is drawn again as it does.
let moving = false;
export const screenMoves = () => moving;

// A value scrolled through as MenuRow's and the selected names are: still a
// second and a half, along at 20 ms a pixel, still two seconds, back.
export function scrollOffset(elapsed, by) {
    const move = Math.abs(by) * 20;
    const t = elapsed % (1500 + move + 2000);
    return t < 1500 ? 0 : t < 1500 + move ? by * (t - 1500) / move : by;
}
function scrolled(elapsed, by) {
    moving = true;
    return scrollOffset(elapsed, by);
}

// --- The deck's symbols (Components/PixelIcon): "#" a set cell.
const SHAPES = {
    play: ["#...", "##..", "###.", "####", "###.", "##..", "#..."],
    up: ["...#...", "..###..", ".#####.", "#######"],
    down: ["#######", ".#####.", "..###..", "...#..."],
    heart: [".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."],
    search: [".###...", "#...#..", "#...#..", "#...#..", ".###...", "....##.", ".....##"],
    signal4: [".........##", ".........##", "......##.##", "......##.##", "...##.##.##", "##.##.##.##", "##.##.##.##"],
    hiscore: ["#..#.###", "#..#..#.", "#..#..#.", "####..#.", "#..#..#.", "#..#..#.", "#..#.###"],
    trophy: ["#######", "#.###.#", ".#####.", "..###..", "...#...", "..###..", ".#####."],
    globe: ["..###..", ".#.#.#.", "#..#..#", "#######", "#..#..#", ".#.#.#.", "..###.."],
    network: ["..###..", "..###..", "...#...", ".#####.", ".#...#.", "###.###", "###.###"],
};
// A symbol's size and the symbol itself, `pixel` a cell's size: an art pixel
// on the screen, or the Orb look's 3 design units on its sheets.
export const iconWidth = (name, pixel = PX) => (SHAPES[name] ? SHAPES[name][0].length : 0) * pixel;
export const iconHeight = (name, pixel = PX) => (SHAPES[name] || []).length * pixel;
export function drawIcon(ctx, name, x, y, color, pixel = PX) {
    const rows = SHAPES[name] || [];
    if (pixel === PX) {
        x = snap(x);
        y = snap(y);
    }
    ctx.fillStyle = color;
    rows.forEach((row, r) => {
        for (let c = 0; c < row.length;) {
            if (row[c] !== "#") { ++c; continue; }
            const start = c;
            while (c < row.length && row[c] === "#") ++c;
            ctx.fillRect(x + start * pixel, y + r * pixel, (c - start) * pixel, pixel);
        }
    });
}
// Settings → Status Icons' row (Components/StatusIcons).
const statusWidth = icons => icons.reduce((w, name, i) => w + iconWidth(name) + (i ? 3 * PX : 0), 0);
function drawStatus(ctx, icons, x, y, color) {
    for (const name of icons) {
        drawIcon(ctx, name, x, y, color);
        x += iconWidth(name) + 3 * PX;
    }
}

// One of the skin's pictures over a part of the window (Components/SkinImage):
// in the scheme's two colours, nine slices as its border has them, on art
// pixels. False while there is none to draw.
function drawSkin(ctx, look, assets, part, x, y, w, h) {
    const image = part && assets.skin(part.source, look.primary, look.surface);
    if (!image)
        return false;
    const [l, t, r, b] = part.border;
    const iw = image.width, ih = image.height;
    const aw = w / PX, ah = h / PX;
    const cols = [0, l, iw - r, iw], rows = [0, t, ih - b, ih];
    const to = [0, l, aw - r, aw].map(v => snap(x + v * PX));
    const down = [0, t, ah - b, ah].map(v => snap(y + v * PX));
    ctx.imageSmoothingEnabled = false;
    for (let i = 0; i < 3; ++i) {
        for (let j = 0; j < 3; ++j) {
            const sw = cols[i + 1] - cols[i], sh = rows[j + 1] - rows[j];
            const dw = to[i + 1] - to[i], dh = down[j + 1] - down[j];
            if (sw > 0 && sh > 0 && dw > 0 && dh > 0)
                ctx.drawImage(image, cols[i], rows[j], sw, sh, to[i], down[j], dw, dh);
        }
    }
    return true;
}

// The selected line's box (Components/SelectionBox): the skin's picture of
// one, or a solid bar of the scheme's colour.
function drawSelection(ctx, look, assets, x, y, w, h) {
    if (!drawSkin(ctx, look, assets, look.skin.selection, x, y, w, h))
        fillRect(ctx, x, y, w, h, look.primary);
}

// --- The ground (Components/OsdGround), as Settings' OSD BACKGROUND has it.
// OSD BACKGROUND's window: the content box with a margin, on art pixels.
function osdWindow() {
    const snapPx = v => Math.round(v / PX) * PX;
    const box = { x: SW * 0.115625, y: SH * 0.11875, w: SW * 0.76875, h: SH * 0.7770833 };
    const margin = SH * 0.025;
    const left = snapPx(box.x - margin), right = snapPx(box.x + box.w + margin);
    const top = snapPx(box.y - margin), bottom = snapPx(box.y + box.h + margin);
    return { x: left, y: top, w: right - left, h: bottom - top };
}

// Draws the ground, and says where the background effect goes on it.
export function drawGround(ctx, look, assets) {
    const windowed = look.osdBackground === "Window";
    const framed = look.osdFrame !== "Off";
    fillRect(ctx, 0, 0, SW, SH, windowed ? "#000000" : look.surface);
    const win = windowed ? osdWindow() : { x: 0, y: 0, w: SW, h: SH };
    let skinned = false;
    if (windowed) {
        if (look.osdFrame === "Shadow") {
            // A DOS window's shadow, dithered, down its right and along its foot.
            const depth = 6 * PX;
            for (const arm of [{ x: win.x + win.w, y: win.y + depth, w: depth, h: win.h },
                               { x: win.x + depth, y: win.y + win.h, w: win.w - depth, h: depth }]) {
                const phase = Math.round(arm.x / PX + arm.y / PX) % 2;
                ctx.fillStyle = look.surface;
                for (let r = 0; r < Math.ceil(arm.h / PX); ++r)
                    for (let c = (r + phase) % 2; c < Math.ceil(arm.w / PX); c += 2)
                        ctx.fillRect(arm.x + c * PX, arm.y + r * PX, PX, PX);
            }
        }
        // The skin's frame draws the window whole, its middle too.
        skinned = framed && !!look.skin.window && !!assets.skin(look.skin.window.source, look.primary, look.surface);
        if (!skinned) {
            fillRect(ctx, win.x, win.y, win.w, win.h, look.surface);
            if (framed)
                frameRect(ctx, win.x, win.y, win.w, win.h, look.primary, PX);
        } else {
            drawSkin(ctx, look, assets, look.skin.window, win.x, win.y, win.w, win.h);
        }
    }
    // The background effect: in the window, inside its frame, or over the
    // whole screen without one.
    const border = windowed && framed ? (skinned ? look.skin.window.border : [1, 1, 1, 1]) : [0, 0, 0, 0];
    return { x: win.x + border[0] * PX, y: win.y + border[1] * PX,
             w: win.w - (border[0] + border[2]) * PX, h: win.h - (border[1] + border[3]) * PX };
}

// --- The bars every view has.
// The title bar (Components/AppBar): the logo in the bar's colour on its own
// ground, then the bar with the title, the status icons at its end.
function drawAppBar(ctx, look, assets, x, y, title) {
    const width = SW * 0.75;
    const contentHeight = Math.max(lineHeight(SH * 0.05), SH * 0.05);
    const height = contentHeight + 2 * PX;
    const logoHeight = Math.round(height * 1.2 / PX) * PX;
    const logo = assets.logo(look.primary, logoHeight);
    let barX = 0;
    if (logo) {
        const lx = x + PX, ly = y + Math.round((height - logo.height) / 2);
        fillRect(ctx, lx, ly, logo.width, logo.height, look.surface);
        ctx.drawImage(logo, snap(lx), snap(ly));
        barX = PX + logo.width + PX;
    }
    const bx = x + barX, bw = width - barX;
    if (!drawSkin(ctx, look, assets, look.skin.titleBar, bx, y, bw, height))
        fillRect(ctx, bx, y, bw, height, look.primary);
    const contentX = SW * 0.0125;
    const rowY = y + (height - contentHeight) / 2;
    const size = SH * 0.05;
    const status = look.statusIcons;
    const statusW = status.length ? statusWidth(status) : 0;
    const room = bw - 2 * contentX - (statusW ? statusW + SW * 0.025 : 0);
    clipped(ctx, bx + contentX, y, Math.max(0, room), height, () =>
        drawText(ctx, title.toUpperCase(), bx + contentX, rowY + (contentHeight - lineHeight(size)) / 2, size, look.surface));
    if (statusW)
        drawStatus(ctx, status, bx + bw - contentX - statusW, y + (height - 7 * PX) / 2, look.surface);
}

// The hint bar (Components/HintBar), from its bottom margin, at its own size
// or as much smaller as the line needs: where it is, and its letters' size.
function hintBar(ctx, text, { x = SW * 0.125, bottom = SH * 0.1041667, width = SW * 0.75, rightPadding = SW * 0.0125 } = {}) {
    const largest = SH * 0.0375, smallest = SH * 0.0291667;
    const leftPadding = SW * 0.0125;
    const probe = measure(ctx, largest, text);
    const size = Math.trunc(Math.max(smallest, Math.min(largest,
        Math.floor(largest * (width - leftPadding - rightPadding) / Math.max(1, probe)))));
    const height = lineHeight(size) + 2 * PX;
    return { x, y: SH - bottom - height, w: width, h: height, size, leftPadding, rightPadding };
}
function drawHintBar(ctx, look, assets, text, bar = hintBar(ctx, text)) {
    if (!drawSkin(ctx, look, assets, look.skin.hintBar, bar.x, bar.y, bar.w, bar.h))
        fillRect(ctx, bar.x, bar.y, bar.w, bar.h, look.primary);
    drawText(ctx, elided(ctx, bar.size, text, bar.w - bar.leftPadding - bar.rightPadding), bar.x + bar.leftPadding,
             bar.y + PX, bar.size, look.surface);
    return bar;
}

// The help line (Components/HelpLine): the line in an outlined box, centred,
// or scrolled through as a ticker when it is too long.
function drawHelpLine(ctx, look, text, x, y, width, elapsed) {
    const height = SH * 0.0583333;
    const pad = SW * 0.0125, spacing = SW * 0.0625, speed = SW * 0.09375;
    const size = SH * 0.0375;
    frameRect(ctx, x, y, width, height, look.primary, PX);
    const line = text.split("\n").map(p => p.trim()).filter(Boolean).join("  •  ");
    const wx = x + PX + pad, ww = width - 2 * (PX + pad);
    const first = measure(ctx, size, line);
    const top = y + (height - lineHeight(size)) / 2;
    clipped(ctx, wx, y, ww, height, () => {
        if (first <= ww) {
            drawText(ctx, line, wx + Math.round((ww - first) / 2), top, size, look.primary);
            return;
        }
        // Still a second and a half, then along at its speed, round again.
        moving = true;
        const span = first + spacing;
        const run = 1000 * span / speed;
        const t = elapsed % (1500 + run);
        const travel = t < 1500 ? 0 : span * (t - 1500) / run;
        drawText(ctx, line, wx - Math.round(travel), top, size, look.primary);
        drawText(ctx, line, wx - Math.round(travel) + span, top, size, look.primary);
    });
    return { x, y, w: width, h: height };
}

// One line of a menu (Components/MenuRow): label, a dot in each cell, the
// value against its end, all on the letters' cells.
function drawMenuRow(ctx, look, x, y, width, height, size, label, value, elapsed) {
    const pad = SW * 0.009375;
    const cell = Math.max(1, measure(ctx, size, "M"));
    const labelText = label.toUpperCase(), valueText = value.toUpperCase();
    const labelW = measure(ctx, size, labelText);
    const valueW = measure(ctx, size, valueText);
    const labelCells = Math.ceil(labelW / cell - 0.01);
    const lineCells = Math.floor((width - 2 * pad) / cell);
    const valueCells = Math.ceil(valueW / cell - 0.01);
    const valueCell = Math.max(labelCells + 2, lineCells - valueCells);
    const labelY = (height - lineHeight(size)) / 2;
    const top = y + labelY;
    drawText(ctx, labelText, x + pad, top, size, look.primary);
    // Level with the middle of the capitals, on whole pixels of the line.
    const dotY = y + Math.round(labelY + ascentOf(size) - size * CAP_MIDDLE - PX / 2);
    ctx.fillStyle = look.primary;
    for (let k = 0; k < valueCell - labelCells; ++k)
        ctx.fillRect(x + Math.round(pad + (labelCells + k + 0.5) * cell - PX / 2), dotY, PX, PX);
    const vx = x + pad + valueCell * cell, vw = Math.max(0, lineCells - valueCell) * cell;
    // A value too long for the line scrolls through it (alwaysScroll).
    clipped(ctx, vx, y, vw, height, () =>
        drawText(ctx, valueText, vx + (valueW > vw ? scrolled(elapsed, vw - valueW) : 0), top, size, look.primary));
}

// --- The main menu (views/ModuleList.qml): the title bar, the modules on, the
// hint bar. ui: { index, top } (the cursor's row, the first row shown).
export function drawMainMenu(ctx, look, assets, ui, now) {
    moving = false;
    drawAppBar(ctx, look, assets, SW * 0.125, SH * 0.125, "OSD/OS");
    const rows = look.menu;
    const listX = SW * 0.115625, listY = SH * 0.25, listW = SW * 0.76875;
    const rowH = SH * 0.0583333;
    const listH = SH * 0.525 + (look.hintBar ? 0 : rowH);
    let box = null;
    if (rows.length === 0) {
        const lines = [["No modules enabled", SH * 0.05], ["Please enable one in settings", SH * 0.0333333]];
        const height = lineHeight(lines[0][1]) + SH * 0.0333333 + lineHeight(lines[1][1]);
        let y = (SH - height) / 2;
        for (const [text, size] of lines) {
            const s = text.toUpperCase();
            drawText(ctx, s, (SW - measure(ctx, size, s)) / 2, y, size, look.primary);
            y += lineHeight(size) + SH * 0.0333333;
        }
    }
    clipped(ctx, listX, listY, listW, listH, () => {
        rows.forEach((row, i) => {
            const rowY = listY + i * rowH - ui.top;
            if (rowY + rowH < listY || rowY > listY + listH)
                return;
            const label = row.name.toUpperCase();
            const size = SH * 0.05;
            const padL = SW * 0.009375, padT = SH * 0.0041667, padB = SH * 0.00625;
            const textW = measure(ctx, size, label) + 2 * padL;
            const textH = lineHeight(size) + padT + padB;
            const container = Math.min(textW, listW);
            const textY = rowY + (rowH - textH) / 2;
            const current = i === ui.index;
            clipped(ctx, listX, rowY, container, rowH, () => {
                const by = current && textW > container ? scrolled(now - ui.since, container - textW) : 0;
                if (current) {
                    drawSelection(ctx, look, assets, listX + by, textY, textW, textH);
                    box = { x: listX, y: textY, w: container, h: textH };
                }
                drawText(ctx, label, listX + by + padL, textY + padT, size, current ? look.surface : look.primary);
            });
        });
    });
    // ▲ / ▼ while rows are hidden above or below.
    const gap = PX * 3;
    if (ui.top > 0)
        drawIcon(ctx, "up", listX + (listW - iconWidth("up")) / 2, listY - gap - iconHeight("up"), look.primary);
    if (ui.top + listH < rows.length * rowH - 1)
        drawIcon(ctx, "down", listX + (listW - iconWidth("down")) / 2, listY + listH + gap, look.primary);
    if (look.hintBar)
        drawHintBar(ctx, look, assets, "[ESC]:SETTINGS [▲▼]:NAVIGATE [ENTER]:SELECT");
    return box;
}
// The main menu's rows shown with the cursor on `index`, from `top` as it was
// (ListView.positionViewAtIndex's Contain).
export function mainMenuTop(look, index, top) {
    const rowH = SH * 0.0583333;
    const listH = SH * 0.525 + (look.hintBar ? 0 : rowH);
    if (index * rowH < top)
        return index * rowH;
    if ((index + 1) * rowH > top + listH)
        return (index + 1) * rowH - listH;
    return top;
}

// --- The Arcade screen. Its games: what a stand-in for MAME says of ten sets
// (scripts/../tests' kind of data: the sets' names, descriptions, years,
// makers and players as MAME's list has them), no pictures or high scores
// yet, as on a card freshly made.
const GAMES = [
    ["1942", "1942 (Revision B)", "1984", "Capcom", 2],
    ["dkong", "Donkey Kong (US set 1)", "1981", "Nintendo", 2],
    ["frogger", "Frogger", "1981", "Konami", 2],
    ["galaga", "Galaga (Namco rev. B)", "1981", "Namco", 2],
    ["joust", "Joust (Green label)", "1982", "Williams", 2],
    ["outrun", "Out Run (sitdown/upright, Rev B)", "1986", "Sega", 1],
    ["pacman", "Pac-Man (Midway)", "1980", "Namco", 2],
    ["qbert", "Q*bert (US set 1)", "1982", "Gottlieb", 2],
    ["sf2", "Street Fighter II: The World Warrior (World 910522)", "1991", "Capcom", 2],
    ["tmnt", "Teenage Mutant Ninja Turtles (World 4 Players)", "1989", "Konami", 4],
].map(([set, description, year, maker, players]) => ({
    set, description, year, maker, players,
    // MAME's description without its version (mame::titleOf()).
    name: description.split(" (")[0].split(" / ")[0].trim(),
}));

// Search's own row, over the games its words find.
const SEARCH_ROW = { search: true, set: "", name: "Search" };

// The filters with games, as ArcadeBackend::filters() lists them: All Games,
// then by players and by decade (genres need catver.ini, publishers are off
// until Menu Filters turns them on), and Search last.
export const ARCADE_FILTERS = (() => {
    const byName = [...GAMES].sort((a, b) => a.name.toLowerCase() < b.name.toLowerCase() ? -1 : 1);
    const filters = [{ id: "all", name: "All Games", games: byName }];
    for (const n of [...new Set(GAMES.map(g => g.players))].sort((a, b) => a - b))
        filters.push({ id: `players/${n}`, name: n === 1 ? "1 Player" : `${n} Players`, games: byName.filter(g => g.players === n) });
    for (const d of [...new Set(GAMES.map(g => g.year.slice(0, 3) + "0s"))].sort())
        filters.push({ id: `years/${d}`, name: d, games: byName.filter(g => g.year.slice(0, 3) + "0s" === d) });
    filters.push({ id: "search", name: "Search", games: [SEARCH_ROW, ...byName] });
    return filters;
})();

// A name, or what is typed, as Search reads it (ArcadeBackend's
// searchable()): in lower case, without accents, of letters and digits
// alone, its words a space apart: "Pac-Man" is "pacman".
const searchable = s => s.normalize("NFKD").toLowerCase().replace(/[^\p{L}\p{N}\s]+/gu, "")
    .split(/\s+/).filter(Boolean).join(" ");
// Search's rows (ArcadeBackend::searchSets()): its own, then the games whose
// name or set has every word typed, those whose name starts with the words
// (or with them run together: "pac man" is Pac-Man's "pacman") or that are
// the set first, then those with a word starting with the first, then the
// rest, each by name.
function searchGames(words) {
    const wanted = searchable(words).split(" ").filter(Boolean);
    const byName = ARCADE_FILTERS[0].games;
    if (!wanted.length)
        return [SEARCH_ROW, ...byName];
    const whole = wanted.join(" ");
    const joined = wanted.join("");
    const best = [], next = [], rest = [];
    for (const game of byName) {
        const text = searchable(game.description) + " " + searchable(game.set);
        if (!wanted.every(word => text.includes(word)))
            continue;
        (text.startsWith(whole) || text.startsWith(joined) || text.endsWith(" " + whole) ? best
            : text.startsWith(wanted[0]) || text.includes(" " + wanted[0]) ? next : rest).push(game);
    }
    return [SEARCH_ROW, ...best, ...next, ...rest];
}
// The filter shown, Search's games as its words find them.
export function arcadeFilter(ui) {
    const filter = ARCADE_FILTERS[ui.filter % ARCADE_FILTERS.length];
    return filter.id === "search" ? { ...filter, games: searchGames(ui.words || "") } : filter;
}

// Search's keys (Components/OnScreenKeyboard, inline): the line typed over
// four rows of letters, digits and signs, and SPACE, DEL and OK; down onto
// the last row from a column of the grid and up from one of its keys.
export const KEY_ROWS = [
    ["A", "B", "C", "D", "E", "F", "G", "H", "I", "J"],
    ["K", "L", "M", "N", "O", "P", "Q", "R", "S", "T"],
    ["U", "V", "W", "X", "Y", "Z", "0", "1", "2", "3"],
    ["4", "5", "6", "7", "8", "9", "-", "'", ".", "&"],
    ["SPACE", "DEL", "OK"],
];
export const lastKeyOf = column => Math.floor(column / 4);
export const columnOf = key => key * 4;

// The Arcade screen's layout, in the design's units (a 960th of the width,
// a 720th of the height), as ArcadeScreen.qml works it out. In the Orb look,
// eight rows on its panel, and its design (120..840 by 90..610) as large as
// fits above the help line, in the middle across (orbScale, orbLeft).
export const ORB_ROWS = 8;
// The details the Orb look's panel has lines for; the help line says the rest.
export const ORB_SHOWN = ["Maker", "Year", "Players"];
export function arcadeLayout(look) {
    const a = look.arcade;
    const u = SW / 960, v = SH / 720;
    const areaLeft = 120, areaTop = 90, areaRight = 840, areaBottom = 645, areaWidth = 720;
    const listBottom = areaBottom - (a.hintBar ? (a.helpLine ? 93 : 41) : (a.helpLine ? 53 : 1));
    const orb = look.orb;
    const straight = a.listStyle === "Straight", wheel = !orb && a.listStyle === "3D Curved";
    const orbScale = Math.min(areaWidth / 720, (listBottom - areaTop) / 520);
    const orbLeft = areaLeft + (areaWidth - 720 * orbScale) / 2;
    const listTop = areaTop + 57, listRoom = listBottom - listTop, rowHeight = 48, minPitch = 54;
    const rowsFit = Math.floor((listRoom - rowHeight) / minPitch) + 1;
    const wheelSize = (distance, reach) => 1 - 0.3 * Math.min(1, distance / reach);
    const wheelLayout = (rows, selected) => {
        const sizes = [];
        for (let i = 0; i < rows; ++i)
            sizes.push(wheelSize(Math.abs(i - selected), rows - Math.floor(rows / 2)));
        let between = 0;
        for (let i = 0; i + 1 < rows; ++i)
            between += (sizes[i] + sizes[i + 1]) / 2;
        const ends = rowHeight * (sizes[0] + sizes[rows - 1]) / 2;
        const k = between > 0 ? (listRoom - ends) / (minPitch * between) : 1;
        const tops = [];
        let middle = listTop + rowHeight * sizes[0] / 2;
        for (let i = 0; i < rows; ++i) {
            if (i > 0)
                middle += minPitch * k * (sizes[i - 1] + sizes[i]) / 2;
            tops.push(middle - rowHeight * sizes[i] / 2);
        }
        return { k, tops };
    };
    const wheelRows = () => {
        if (listRoom < rowHeight)
            return 0;
        let rows = 1;
        for (let more = 2; more < 30; ++more) {
            for (let selected = 0; selected < more; ++selected) {
                if (wheelLayout(more, selected).k * minPitch < rowHeight + 4)
                    return rows;
            }
            rows = more;
        }
        return rows;
    };
    const perPage = orb ? ORB_ROWS : wheel ? wheelRows() : rowsFit;
    const pitch = Math.floor((listRoom - rowHeight) / Math.max(1, perPage - 1));
    const middleRow = Math.floor(perPage / 2);
    const textLines = 27;
    const pictureRoom = listBottom - areaTop - 7 - 2 * 6 - textLines;
    const columnWidth = Math.min(Math.round(345 * areaWidth / 720), Math.floor(pictureRoom * 345 / 259));
    const columnX = areaRight - columnWidth;
    const pictureHeight = Math.round(columnWidth / (345 / 259));
    const listWidth = columnX - 20 - areaLeft;
    const infoTop = areaTop + pictureHeight + 7;
    const scoresTop = infoTop + 6;
    const playTimeTop = listBottom - 6 - textLines;
    const infoLines = Math.max(0, Math.floor((playTimeTop - 3 - textLines - scoresTop - 32) / 29) + 1);
    const scoreRows = Math.min(3, infoLines);
    return {
        u, v, areaLeft, areaTop, areaRight, areaBottom, areaWidth, listBottom, straight, wheel, listTop,
        rowHeight, perPage, pitch, middleRow, wheelReach: perPage - middleRow, wheelSize, wheelLayout,
        textSize: Math.round(textLines * v), nameSize: Math.round(30 * v), columnWidth, columnX, pictureHeight,
        listWidth, infoTop, infoX: columnX + 10, infoWidth: columnWidth - 20, scoresTop, playTimeTop,
        scoreRows, detailRows: Math.min(4, infoLines - scoreRows),
        orb, orbScale, orbLeft, orbX: x => orbLeft + (x - 120) * orbScale, orbY: y => areaTop + (y - 90) * orbScale,
    };
}

// What the hint bar says on the Arcade screen, as { key, word } pairs
// (ArcadeScreen.qml's hintPairs), with a keyboard's keys: ▲ ▼ and ◄ ► go
// without saying, and COIN only where it buys a credit, not on Search.
export function arcadeHintPairs(ui, filter, current) {
    const back = word => ({ key: "[ESC]", word }), select = word => ({ key: "[ENTER]", word });
    if (ui.typing)
        return [back("DONE"), { key: "[▲▼◄►]", word: "MOVE" }, select("TYPE")];
    if (!current)
        return [back("BACK")];
    if (current.search)
        return [back("BACK"), select("SEARCH")];
    return [back("BACK"), ...(filter.id !== "search" ? [{ key: "[5]", word: "COIN" }] : []),
            select("PLAY"), { key: "HOLD", word: "OPTIONS" }];
}

// The Arcade screen's foot: the hint bar's line and where the bar is, and
// the status icons (Settings → Status Icons), at the help line's right, or
// with no help line at the hint bar's end while the hint line still fits
// beside them at its smallest (in the Orb look, at the right of its keys,
// which make room); with neither, none. statusRoom is the room they take.
export function arcadeFoot(ctx, look, L, pairs) {
    const a = look.arcade;
    const text = pairs.map(pair => `${pair.key}:${pair.word}`).join(" ");
    const width = Math.round(L.areaWidth * L.u);
    const icons = look.statusIcons;
    const iconsW = icons.length ? statusWidth(icons) : 0;
    const iconsShown = iconsW > 0 && (a.helpLine || (a.hintBar && (look.orb
        || measure(ctx, SH * 0.0291667, text) <= width - SW * 0.0125 - SW * 0.0125 - iconsW - Math.round(12 * L.u))));
    const statusRoom = iconsShown ? iconsW + Math.round(12 * L.u) : 0;
    const bar = hintBar(ctx, text, { x: Math.round(L.areaLeft * L.u), width,
                                     rightPadding: SW * 0.0125 + (a.helpLine ? 0 : statusRoom) });
    return { text, bar, icons, iconsW, iconsShown, statusRoom };
}

// The game in each row: the page's, or the wheel's from its top row on.
export function arcadeRows(L, filter, ui) {
    const count = filter.games.length;
    const shown = new Array(L.perPage).fill(null);
    const fill = (row, from, n) => {
        for (let i = 0; i < n && from + i < count; ++i)
            shown[row + i] = { place: from + i, game: filter.games[from + i] };
    };
    if (!L.wheel) {
        fill(0, Math.floor(ui.selected / L.perPage) * L.perPage, L.perPage);
    } else if (count >= L.perPage) {
        const start = ((ui.wheelTop % count) + count) % count;
        const head = Math.min(L.perPage, count - start);
        fill(0, start, head);
        fill(head, 0, L.perPage - head);
    } else {
        fill(0, 0, count);
    }
    return shown;
}
export function arcadeSelectedRow(L, filter, ui) {
    const count = filter.games.length;
    return L.wheel ? (count > 0 ? (ui.selected - ui.wheelTop + count) % count : 0)
                   : ui.selected - Math.floor(ui.selected / L.perPage) * L.perPage;
}

// Search's keys in the picture's frame, inside its padding (in the Orb look,
// over the orb, which gives way to them: ArcadeScreen.qml's keysX, keysTop,
// keysWidth and keysHeight), in the box's letters or smaller to fit it; the
// box on a key and the cursor after the line typed while they have the keys
// (ui.typing).
function keysBox(L) {
    const r = Math.round;
    return L.orb ? { x: r(L.orbX(140)), y: r(L.orbY(110)), w: r(L.orbX(452) - L.orbX(140)), h: r(L.orbY(380) - L.orbY(110)) }
                 : { x: L.infoX, y: L.areaTop + 6, w: L.infoWidth, h: L.pictureHeight - 12 };
}
function drawKeys(ctx, look, L, ui, now) {
    const { u, v } = L;
    const keys = keysBox(L);
    const size = Math.min(L.textSize, Math.floor((keys.h * v - 2 * PX - 4) / 8.8));
    const x = Math.round(keys.x * u), y = Math.round(keys.y * v);
    const cellW = Math.floor(Math.round(keys.w * u) / 10), cellH = Math.round(size * 1.4);
    const lineH = cellH + 2 * PX;
    frameRect(ctx, x, y, cellW * 10, lineH, look.primary, PX);
    clipped(ctx, x, y, cellW * 10, lineH, () => {
        const top = y + (lineH - lineHeight(size)) / 2;
        const words = (ui.words || "").toUpperCase();
        const textX = x + SW * 0.0125;
        drawText(ctx, words, textX, top, size, look.primary);
        if (ui.typing) {
            moving = true;
            const blockW = Math.round(size * 0.55), blockH = Math.round(size * 0.75);
            if (Math.floor(now / 500) % 2 === 0)
                fillRect(ctx, textX + measure(ctx, size, words), top + (lineHeight(size) - blockH) / 2, blockW, blockH,
                         look.primary);
        }
    });
    let rowY = y + lineH + Math.round(size * 0.4);
    KEY_ROWS.forEach((keys, row) => {
        const keyW = keys.length === 10 ? cellW : cellW * 10 / keys.length;
        keys.forEach((key, col) => {
            const current = ui.typing && ui.keysRow === row && ui.keysCol === col;
            const keyX = x + col * keyW, textW = measure(ctx, size, key);
            if (current) {
                const boxW = textW + 2 * SW * 0.009375;
                fillRect(ctx, keyX + (keyW - boxW) / 2, rowY, boxW, cellH, look.primary);
            }
            drawText(ctx, key, keyX + (keyW - textW) / 2, rowY + (cellH - lineHeight(size)) / 2, size,
                     current ? look.surface : look.primary);
        });
        rowY += cellH;
    });
}

// ui: { filter, selected, wheelTop, credits, since, words (Search's),
// typing (Search's keys have the keys), keysRow, keysCol }; bar, the bar's
// name and since when it scrolls, is kept there as it is drawn. In the Orb
// look only what it keeps in 2D: Search's keys over the orb, the help line
// and the status icons, which go as its panels swing out for a game (going,
// 0 to 1); orb.mjs draws the rest.
export function drawArcade(ctx, look, assets, ui, now, going = 0) {
    moving = false;
    const L = arcadeLayout(look);
    const a = look.arcade;
    const { u, v } = L;
    const filters = ARCADE_FILTERS;
    const filter = arcadeFilter(ui);
    const onSearch = filter.id === "search";
    const count = filter.games.length;
    const rows = arcadeRows(L, filter, ui);
    const selectedRow = arcadeSelectedRow(L, filter, ui);
    const current = rows[selectedRow] ? rows[selectedRow].game : null;
    const elapsed = now - ui.since;
    let box = null;
    const game = current && !current.search ? current : null;
    // What the game is, for the box and the help line.
    const details = game ? [
        { label: "Maker", value: game.maker || "?", told: game.maker || "" },
        { label: "Year", value: game.year || "?", told: game.year || "" },
        { label: "Players", value: game.players > 0 ? String(game.players) : "?",
          told: game.players > 0 ? game.players + (game.players === 1 ? " Player" : " Players") : "" },
        { label: "System", value: "?", told: "" },
    ] : [];

    if (!look.orb) {
        // The bar over the list: the filter, ◄ ►, the page of so many.
        {
            const x = Math.round(L.areaLeft * u), y = Math.round(L.areaTop * v);
            const w = Math.round(L.listWidth * u), h = Math.round(45 * v);
            const pad = Math.round(12 * u), gap = Math.round(8 * u), size = SH * 0.0375;
            if (!drawSkin(ctx, look, assets, look.skin.titleBar, x, y, w, h))
                fillRect(ctx, x, y, w, h, look.primary);
            const page = Math.floor(ui.selected / L.perPage), pages = Math.max(1, Math.ceil(count / L.perPage));
            const right = onSearch ? `${count - 1} FOUND` : L.wheel ? `${ui.selected + 1}/${count}` : `${page + 1}/${pages}`;
            const rightW = measure(ctx, size, right);
            const arrows = filters.length > 1;
            const arrowW = measure(ctx, size, "◄");
            // The filter's name in what is left of the bar, cut between
            // two letters: too long for it, it scrolls through, started over
            // as the name or the room changes (ArcadeBar's labelClip).
            const label = filter.name.toUpperCase();
            const labelFull = measure(ctx, size, label);
            const cell = Math.max(1, measure(ctx, size, "M"));
            const room = w - 2 * pad - (rightW + 2 * gap) - (arrows ? 2 * (arrowW + gap) : 0);
            const labelW = Math.max(0, Math.min(labelFull, Math.floor(room / cell) * cell));
            if (!ui.bar || ui.bar.label !== label || ui.bar.width !== labelW)
                ui.bar = { label, width: labelW, since: now };
            const top = y + (h - lineHeight(size)) / 2;
            let at = x + pad;
            if (arrows) {
                drawText(ctx, "◄", at, top, size, look.surface);
                at += arrowW + gap;
            }
            const labelX = at;
            if (labelFull > labelW)
                clipped(ctx, labelX, y, labelW, h, () =>
                    drawText(ctx, label, labelX + scrolled(now - ui.bar.since, labelW - labelFull), top, size, look.surface));
            else
                drawText(ctx, label, labelX, top, size, look.surface);
            at += labelW + gap;
            if (arrows)
                drawText(ctx, "►", at, top, size, look.surface);
            drawText(ctx, right, x + w - pad - rightW, top, size, look.surface);
        }

        // The rows: straight, on an arc, or on the wheel, the game chosen
        // standing out to the left.
        const wheelTops = L.wheel ? L.wheelLayout(L.perPage, selectedRow).tops : [];
        rows.forEach((entry, index) => {
            if (!entry)
                return;
            const isCurrent = index === selectedRow;
            const depth = L.wheel ? Math.min(1, Math.abs(index - selectedRow) / L.wheelReach) : 0;
            const size = L.wheel ? L.wheelSize(Math.abs(index - selectedRow), L.wheelReach) : 1;
            const arc = L.straight ? 0 : Math.round(24 * Math.sin(Math.PI * (index + 0.5) / L.perPage));
            const boxLeft = L.areaLeft + (L.straight ? 16 : 12 + arc) - (isCurrent ? 16 : 0);
            const boxWidth = L.listWidth - (L.straight ? 16 : 35) + (isCurrent ? 16 : 0);
            const ink = isCurrent ? look.surface : look.primary;
            const gap = Math.round(10 * size * u);
            const x = Math.round(boxLeft * u);
            const y = Math.round((L.wheel ? wheelTops[index] || 0 : L.listTop + index * L.pitch) * v);
            const w = Math.round(boxWidth * size * u), h = Math.round(L.rowHeight * size * v);
            ctx.save();
            ctx.globalAlpha = 1 - 0.6 * depth;
            if (isCurrent) {
                drawSelection(ctx, look, assets, x, y, w, h);
                box = { x, y, w, h };
            } else {
                frameRect(ctx, x, y, w, h, look.primary, PX);
            }
            // Search's games counted under its own row, which has none, and
            // its glass at the right end.
            const numberSize = Math.round(27 * size * v);
            const number = entry.place + (onSearch ? 0 : 1);
            const numberText = entry.game.search ? "" : ("00" + number).slice(-Math.max(3, String(number).length));
            const numberX = x + gap + PX;
            const numberW = numberText ? measure(ctx, numberSize, numberText) : 0;
            drawText(ctx, numberText, numberX, y + (h - lineHeight(numberSize)) / 2, numberSize, ink);
            const markX = x + w - iconWidth("search") - gap - PX;
            if (entry.game.search)
                drawIcon(ctx, "search", markX, y + (h - iconHeight("search")) / 2, ink);
            const nameSize = Math.round(30 * size * v);
            const nameX = numberX + numberW + gap;
            const nameW = (entry.game.search ? markX - gap : x + w - gap - PX) - nameX;
            const name = (entry.game.search ? ui.words || "Search" : entry.game.name).toUpperCase();
            const fullW = measure(ctx, nameSize, name);
            const nameTop = y + (h - lineHeight(nameSize)) / 2;
            clipped(ctx, nameX, y, nameW, h, () => {
                if (isCurrent && fullW > nameW)
                    drawText(ctx, name, nameX + scrolled(elapsed, nameW - fullW), nameTop, nameSize, ink);
                else
                    drawText(ctx, elided(ctx, nameSize, name, nameW), nameX, nameTop, nameSize, ink);
            });
            ctx.restore();
        });

        // The column: the picture's frame (none yet: which file it would
        // be), the credits bought, and the box of high scores, details and
        // play time.
        const ground = rgba(look.surface, a.infoBackground / 100);
        const frame = a.border ? PX : 0;
        {
            const x = Math.round(L.columnX * u), y = Math.round(L.areaTop * v);
            const w = Math.round(L.columnWidth * u), h = Math.round(L.pictureHeight * v);
            fillRect(ctx, x, y, w, h, ground);
            frameRect(ctx, x, y, w, h, look.primary, frame);
            if (onSearch)
                drawKeys(ctx, look, L, ui, now);
            else if (current) {
                const lines = [["No Picture".toUpperCase(), L.nameSize], [`snap/${current.set}.png`, L.textSize]];
                if (!a.webApiKey)
                    lines.push(["No Web API Key".toUpperCase(), L.textSize]);
                const spacing = Math.round(12 * v);
                const height = lines.reduce((sum, [, size], i) => sum + lineHeight(size) + (i ? spacing : 0), 0);
                const colW = w - 4 * PX, colX = x + (w - colW) / 2;
                let top = y + (h - height) / 2;
                for (const [text, size] of lines) {
                    const s = elided(ctx, size, text, colW);
                    drawText(ctx, s, colX + (colW - measure(ctx, size, s)) / 2, top, size, look.primary);
                    top += lineHeight(size) + spacing;
                }
            }
            if (!onSearch) {
                const credit = `CREDIT ${ui.credits}`;
                const creditW = measure(ctx, L.textSize, credit) + 2 * Math.round(6 * u);
                const creditH = lineHeight(L.textSize) + 2 * PX;
                const cx = x + w - frame - creditW, cy = y + h - frame - creditH;
                fillRect(ctx, cx, cy, creditW, creditH, look.surface);
                drawText(ctx, credit, cx + Math.round(6 * u), cy + PX, L.textSize, look.primary);
            }
        }
        // Under Search's own row, that its words find no game.
        if (onSearch && count === 1) {
            const rowTop = L.wheel ? L.wheelLayout(L.perPage, selectedRow).tops[1] || L.listTop : L.listTop + L.pitch;
            drawText(ctx, "NO GAME FOUND", Math.round((L.areaLeft + 16) * u),
                     Math.round((rowTop + (L.rowHeight - 30) / 2) * v), L.nameSize, look.primary);
        }
        {
            const x = Math.round(L.columnX * u), y = Math.round(L.infoTop * v);
            const w = Math.round(L.columnWidth * u), h = Math.round(L.listBottom * v) - y;
            fillRect(ctx, x, y, w, h, ground);
            frameRect(ctx, x, y, w, h, look.primary, frame);
        }
        if (game && L.scoreRows > 0) {
            const x = Math.round(L.infoX * u), y = Math.round(L.scoresTop * v);
            drawText(ctx, "HI-SCORE", x, y, L.textSize, look.primary);
            drawText(ctx, "NO SCORES YET", x, y + Math.round(32 * v), L.textSize, look.primary);
        }
        // The details over the play time, as many as the box has lines
        // for; the help line says the rest.
        if (game) {
            const pad = SW * 0.009375;
            for (let i = 0; i <= L.detailRows; ++i) {
                const detail = i < L.detailRows ? details[i] : { label: "Play Time", value: "Never" };
                const x = Math.round(L.infoX * u) - pad;
                const y = Math.round((L.playTimeTop - (L.detailRows - i) * 29) * v);
                drawMenuRow(ctx, look, x, y, Math.round(L.infoWidth * u) + 2 * pad, L.textSize, L.textSize,
                            detail.label, detail.value, elapsed);
            }
        }
    } else if (onSearch) {
        drawKeys(ctx, look, L, ui, now);
    }

    // The help line and the hint bar, the status icons at the help line's
    // right; with none, at the hint bar's end (arcadeFoot()). The Orb look
    // has its keys in the hint bar's place.
    const foot = arcadeFoot(ctx, look, L, arcadeHintPairs(ui, filter, current));
    ctx.save();
    ctx.globalAlpha = 1 - going;
    if (going > 0 && going < 1)
        moving = true;
    let help = null;
    if (a.helpLine && current) {
        // What the box has no line for: in the Orb look, all but the maker,
        // the year and the players (ArcadeScreen.qml's orbShown).
        const untold = look.orb ? details.filter(d => !ORB_SHOWN.includes(d.label)) : details.slice(L.detailRows);
        const about = !current.search ? [current.description].concat(untold.map(d => d.told)).join("\n")
            : ui.typing ? "The games found follow each letter typed: OK goes down to them, back to the list, "
                          + "and ◄ ► past the keys' ends go round the filters"
            : "► or select to type what to look for: the games found follow each letter";
        const helpMargin = a.hintBar ? SH * 0.1583333 : SH * 0.1041667;
        const helpH = SH * 0.0583333;
        help = drawHelpLine(ctx, look, about, Math.round(L.areaLeft * u), SH - helpMargin - helpH,
                            foot.bar.w - foot.statusRoom, elapsed);
    }
    if (a.hintBar && !look.orb)
        drawHintBar(ctx, look, assets, foot.text, foot.bar);
    if (foot.iconsShown && (help || a.hintBar)) {
        const beside = help || foot.bar;
        drawStatus(ctx, foot.icons, Math.round(L.areaRight * u) - foot.iconsW - (a.helpLine ? 0 : Math.round(SW * 0.0125)),
                   Math.round(beside.y + (beside.h - 7 * PX) / 2), a.helpLine || look.orb ? look.primary : look.surface);
    }
    ctx.restore();
    return box;
}
