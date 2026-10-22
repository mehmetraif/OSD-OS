// OSD/OS Setup's live preview against the app: each of the app's shaders made
// WebGL's (site/setup/js/preview/glsl.mjs) taking only what the preview sets it, a
// look worked out from the settings as Main.qml works it out (look.mjs), and
// the selector effects (selector.mjs) drawn as src/fx/SelectorFx.cpp draws
// them.
//   node --test tests/site/*.test.mjs
import { test, after } from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, readdirSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { fromQt, uniformsOf } from "../../site/setup/js/preview/glsl.mjs";
import { lookOf, ARCADE } from "../../site/setup/js/preview/look.mjs";
import { SelectorFx } from "../../site/setup/js/preview/selector.mjs";

const root = fileURLToPath(new URL("../..", import.meta.url));
const out = mkdtempSync(join(tmpdir(), "osdos-preview-"));
after(() => rmSync(out, { recursive: true, force: true }));
execFileSync("python3", ["-I", join(root, "scripts/build-site.py"), out], { stdio: "ignore" });
const looks = JSON.parse(readFileSync(join(out, "setup/schema.json"), "utf8")).looks;

// What preview.mjs sets each kind of the app's shaders, by the names Main.qml
// sets them by: a shader taking anything else would go without it.
const SET = {
    background: ["size", "origin", "time", "ink", "paper"],
    transition: ["progress", "resolution", "px", "corner", "ink", "from", "to"],
    effects: ["resolution", "px", "time", "scanlines", "curvature", "glow", "bleed", "noise", "vignette",
              "rainbow", "shimmer", "flicker", "inkGlow", "ink", "paper", "source"],
};

test("each of the app's shaders, made WebGL's, takes what the preview sets it", () => {
    const transitions = Object.values(looks.transitionShaders);
    const frags = readdirSync(join(root, "shaders")).filter(f => f.endsWith(".frag"));
    assert.ok(frags.length >= 10);
    for (const file of frags) {
        const name = file.slice(0, -5);
        const source = readFileSync(join(root, "shaders", file), "utf8");
        const made = fromQt(source);
        assert.match(made, /^#version 300 es\nprecision highp float;\n/, name);
        assert.doesNotMatch(made, /#version 440|std140|binding\s*=|location\s*=\s*\d+\s*\)\s*in\b/, `${name}: Qt's own left`);
        // The rest of it the app's code as it is.
        const body = source.replace(/^#version[^\n]*\n/, "");
        const tail = body.slice(body.indexOf("void main"));
        assert.ok(made.endsWith(tail), `${name}: its main() untouched`);
        const kind = name === "effects" ? "effects" : transitions.includes(name) ? "transition" : "background";
        const taken = uniformsOf(source).filter(u => !u.startsWith("qt_"));
        assert.ok(taken.length > 0, name);
        for (const uniform of taken)
            assert.ok(SET[kind].includes(uniform), `${name}'s ${uniform}: the preview doesn't set it`);
    }
});

test("a uniform block's members, each a uniform, its comments gone", () => {
    const made = fromQt(`#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;      // in art pixels; the window's
    float time;
};
layout(binding = 1) uniform sampler2D source;
void main() { fragColor = texture(source, qt_TexCoord0) * time; }
`);
    assert.equal(made, `#version 300 es
precision highp float;
precision highp int;
in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
uniform mat4 qt_Matrix;
uniform float qt_Opacity;
uniform vec2 size;
uniform float time;
uniform sampler2D source;
void main() { fragColor = texture(source, qt_TexCoord0) * time; }
`);
    // A brace, or the block's own end, in a comment isn't its end.
    assert.deepEqual(uniformsOf(`#version 440
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;   // }; not the end
    float qt_Opacity; /* nor } this */
    /* a block's
       comment }; */ vec2 size;
};
void main() {}
`), ["qt_Matrix", "qt_Opacity", "size"]);
});

// What the page holds: each row's value, the modules on.
const settings = (app, modules = [], more = {}) => ({ app, modules, wifi: false, account: false, key: false, ...more });

test("a look's colours: Settings' scheme, else the theme's, else Video 1", () => {
    const video1 = looks.schemes["Video 1"];
    let look = lookOf(looks, settings({ osd_theme: "", color_scheme: "" }));
    assert.deepEqual([look.primary, look.surface, look.windowColor], [video1.primary, video1.surface, video1.surface]);
    // The theme's, by a scheme's name or its own two.
    look = lookOf(looks, settings({ osd_theme: "late-show", color_scheme: "" }));
    assert.equal(look.surface, looks.schemes[looks.themes["late-show"].colors].surface);
    look = lookOf(looks, settings({ osd_theme: "inferno", color_scheme: "" }));
    assert.deepEqual([look.primary, look.surface], [looks.themes.inferno.colors.primary, looks.themes.inferno.colors.surface]);
    // Settings' own over the theme's.
    look = lookOf(looks, settings({ osd_theme: "inferno", color_scheme: "Terminal" }));
    assert.equal(look.primary, looks.schemes.Terminal.primary);
    // OSD Background Off: on black, in the lighter of the two; Window: black
    // round the window.
    const t120 = looks.schemes["T-120"];
    look = lookOf(looks, settings({ color_scheme: "T-120", osd_background: "Off" }));
    assert.deepEqual([look.primary, look.surface, look.windowColor], [t120.surface, "#000000", "#000000"]);
    look = lookOf(looks, settings({ color_scheme: "T-120", osd_background: "Window" }));
    assert.deepEqual([look.primary, look.surface, look.windowColor], [t120.primary, t120.surface, "#000000"]);
});

test("a look's effects and skin: Settings' row unless it is THEME, else the theme's", () => {
    const theme = { osd_theme: "inferno", skin: "", background_effect: "", selector_effect: "", transition: "",
                    screen_effect: "", text_effect: "" };
    let look = lookOf(looks, settings(theme));
    assert.equal(look.background, looks.background[looks.themes.inferno.effects.background]);
    assert.equal(look.selector, looks.themes.inferno.effects.selector);
    assert.equal(look.transition, "Cube");
    assert.deepEqual(look.skin, looks.skins[looks.themes.inferno.skin]);
    look = lookOf(looks, settings({ ...theme, skin: "Off", background_effect: "Off", selector_effect: "Snow", transition: "Off" }));
    assert.deepEqual([look.background, look.selector, look.transition, look.skin], ["", "Snow", "", {}]);
    // A preset's knobs as Main.qml has them, a theme's own as it gives them.
    look = lookOf(looks, settings({ ...theme, osd_theme: "late-show" }));
    for (const [knob, value] of Object.entries(looks.screen[looks.themes["late-show"].effects.screen]))
        assert.equal(look.screen[knob], value, knob);
    assert.ok(look.text.flicker > 0);
    look = lookOf(looks, settings({ ...theme, osd_theme: "green-screen" }));
    assert.equal(look.screen.scanlines, looks.themes["green-screen"].effects.screen.scanlines);
    assert.equal(look.screen.noise, 0);
    // A cube's old names, from a theme or a config of before: the cube.
    look = lookOf(looks, settings({ ...theme, osd_theme: "", transition: "Cube Left" }));
    assert.equal(look.transition, "Cube");
    // Without a theme, nothing moves.
    look = lookOf(looks, settings({ osd_theme: "" }));
    assert.deepEqual([look.background, look.selector, look.transition], ["", "", ""]);
    assert.ok(Object.values(look.screen).every(v => v === 0) && Object.values(look.text).every(v => v === 0));
});

test("the main menu's rows and the status icons, from the modules on", () => {
    const modules = [
        { id: "com.osdos.youtube", name: "YouTube", folder: "youtube", enabled: true, values: {} },
        { id: ARCADE, name: "Arcade", folder: "arcade", enabled: true,
          values: { high_scores: "On", online_high_scores: "RetroAchievements", online_play: "Off", lan_play: "On",
                    list_style: "3D Curved", hint_bar: "Off" } },
        { id: "com.osdos.plex", name: "Plex", folder: "plex", enabled: false, values: {} },
    ];
    let look = lookOf(looks, settings({ status_icons: "On" }, modules, { wifi: true, account: true }));
    assert.deepEqual(look.menu.map(m => m.name), ["Arcade", "YouTube"]);
    assert.deepEqual(look.statusIcons, ["hiscore", "trophy", "network", "signal4"]);
    assert.deepEqual([look.arcade.listStyle, look.arcade.hintBar, look.arcade.helpLine], ["3D Curved", false, true]);
    // The trophy with an account to send the scores to; nothing with Status
    // Icons off, nor the Arcade's with it off.
    look = lookOf(looks, settings({ status_icons: "On" }, modules));
    assert.deepEqual(look.statusIcons, ["hiscore", "network"]);
    look = lookOf(looks, settings({ status_icons: "Off" }, modules, { wifi: true, account: true }));
    assert.deepEqual(look.statusIcons, []);
    modules[1].enabled = false;
    look = lookOf(looks, settings({ status_icons: "On" }, modules, { wifi: true, account: true }));
    assert.deepEqual([look.menu.map(m => m.name), look.statusIcons], [["YouTube"], ["signal4"]]);
});

test("each selector effect, drawn the same from the same start, round the box", () => {
    const box = { x: 40, y: 50, w: 120, h: 12 };
    for (const effect of ["Sparkles", "Welding", "Lightning", "Rainbow", "Snow"]) {
        const a = new SelectorFx(320, 240), b = new SelectorFx(320, 240);
        a.setEffect(effect);
        b.setEffect(effect);
        let drawn = 0;
        for (let i = 0; i < 60; ++i) {
            a.tick(box);
            b.tick(box);
            drawn += !a.blank;
        }
        assert.ok(drawn > 0, `${effect} draws`);
        assert.deepEqual(a.frame, b.frame, effect);
        // Premultiplied: no colour brighter than its pixel is solid.
        for (let p = 0; p < a.frame.length; p += 4)
            assert.ok(Math.max(a.frame[p], a.frame[p + 1], a.frame[p + 2]) <= a.frame[p + 3], `${effect} at ${p / 4}`);
    }
    // The rainbow under the box, across it.
    const rainbow = new SelectorFx(320, 240);
    rainbow.setEffect("Rainbow");
    rainbow.tick(box);
    for (let p = 0; p < rainbow.frame.length; p += 4) {
        if (!rainbow.frame[p + 3])
            continue;
        const x = (p / 4) % 320, y = Math.floor(p / 4 / 320);
        assert.ok(y >= box.y + box.h && x >= box.x && x < box.x + box.w, `rainbow at ${x},${y}`);
    }
});

test("a selector draws nothing without a box or an effect, and stops clear", () => {
    const fx = new SelectorFx(320, 240);
    fx.setEffect("Sparkles");
    for (let i = 0; i < 10; ++i)
        assert.equal(fx.tick(null), false);
    assert.ok(fx.blank && fx.frame.every(v => v === 0));
    for (let i = 0; i < 10; ++i)
        fx.tick({ x: 40, y: 50, w: 120, h: 12 });
    assert.ok(!fx.blank);
    // Its last frame a clear one, to be drawn too; then nothing.
    assert.equal(fx.stop(), true);
    assert.ok(fx.blank && fx.frame.every(v => v === 0));
    assert.equal(fx.stop(), false);
    fx.setEffect("Rainbow");
    fx.tick({ x: 40, y: 50, w: 120, h: 12 });
    assert.equal(fx.tick(null), true, "the rainbow cleared once its box goes");
    assert.ok(fx.blank && fx.frame.every(v => v === 0));
    assert.equal(fx.tick(null), false);
    fx.setEffect("Nothing");
    assert.equal(fx.kind, "");
    assert.equal(fx.tick({ x: 40, y: 50, w: 120, h: 12 }), false);
});
