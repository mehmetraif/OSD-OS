// OSD/OS Setup's settings against the app's own: schema.json as
// scripts/build-site.py makes it, each row one the app has, offering the
// values the app takes, the website's files all there (the landing page's
// pictures and themes, and Setup's live preview, the app's shaders and the
// skins' pictures among them), the looks the preview draws as Main.qml has
// them, and the time zones the card has.
//   node --test tests/site/*.test.mjs
import { test, after } from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../..", import.meta.url));
const out = mkdtempSync(join(tmpdir(), "osdos-site-"));
after(() => rmSync(out, { recursive: true, force: true }));
execFileSync("python3", ["-I", join(root, "scripts/build-site.py"), out], { stdio: "ignore" });
const setup = join(out, "setup");
const schema = JSON.parse(readFileSync(join(setup, "schema.json"), "utf8"));
const read = path => readFileSync(join(root, path), "utf8");
const settingsQml = read("views/Settings.qml");
const mainQml = read("Main.qml");
const appRows = schema.app.flatMap(group => group.settings);
const valuesOf = row => row.options.map(([, value]) => String(value));

// The names a QML list (`name: [...]` or `var name = [...]`) or object of
// presets (`name: ({ "A": ..., })`) holds.
function qmlNames(source, name) {
    const list = source.match(new RegExp(`${name}\\s*[:=]\\s*\\[([^\\]]*)\\]`));
    if (list)
        return [...list[1].matchAll(/"([^"]+)"/g)].map(m => m[1]);
    const object = source.match(new RegExp(`${name}\\s*:\\s*\\(\\{([\\s\\S]*?)\\}\\)`));
    assert.ok(object, `${name} in the QML`);
    // The keys at the block's own depth: a scheme's colours lie deeper.
    const keys = [...object[1].matchAll(/^( *)"([^"]+)"\s*:/gm)].map(m => [m[1].length, m[2]]);
    const depth = Math.min(...keys.map(([d]) => d));
    return keys.filter(([d]) => d === depth).map(([, key]) => key);
}

// Every file a page's HTML and CSS name, there beside it (a recording the
// script swaps in too); a link to another page or a fragment isn't a file,
// and a query is the page's own (setup/?device=arcade).
function filesNamed(folder, html, css) {
    const named = [];
    for (const [, path] of readFileSync(join(folder, html), "utf8").matchAll(/(?:href|src|data-recording)="([^":#?]+)(?:\?[^":#]*)?"/g))
        named.push([html, path]);
    for (const [, path] of readFileSync(join(folder, css), "utf8").matchAll(/url\("([^"]+)"\)/g))
        named.push([css, path]);
    return named;
}

test("the website's files are all there", () => {
    // The landing page, at the root.
    for (const file of ["index.html", "style.css", "js/home.js", "themes.json", ".nojekyll",
                        "fonts/VCR_OSD_MONO_1.001.ttf", "fonts/LICENSE-vcr-osd-mono.txt", "img/icon-16.png", "img/icon-32.png"])
        assert.ok(existsSync(join(out, file)), file);
    for (const [file, path] of filesNamed(out, "index.html", "style.css"))
        assert.ok(existsSync(join(out, path)), `${file}'s ${path}`);
    const landing = readFileSync(join(out, "index.html"), "utf8");
    assert.ok(landing.includes('href="setup/"'), "the landing page leads to Setup");
    // Nothing of the page from elsewhere: no script, style, font or picture
    // off the site.
    const loads = [...landing.matchAll(/<(?:script|img)\b[^>]*\bsrc="([^"]+)"|<link\b[^>]*\brel="(?:stylesheet|icon|preload)"[^>]*\bhref="([^"]+)"/g)]
        .map(m => m[1] || m[2]);
    assert.ok(loads.length >= 4 && loads.every(u => !/^(?:https?:)?\/\//.test(u)), `nothing loaded from elsewhere: ${loads.join(" ")}`);
    assert.ok(!/url\(["']?(?:https?:)?\/\//.test(readFileSync(join(out, "style.css"), "utf8")), "no style from elsewhere");
    execFileSync("node", ["--check", join(out, "js/home.js")], { stdio: "ignore" });
    // OSD/OS Setup, under setup/.
    for (const file of ["index.html", "style.css", "js/core.mjs", "js/page.mjs", "schema.json",
                        "fonts/VCR_OSD_MONO_1.001.ttf", "fonts/LICENSE-vcr-osd-mono.txt", "img/logo.svg", "img/icon-16.png", "img/icon-32.png"])
        assert.ok(existsSync(join(setup, file)), `setup/${file}`);
    for (const file of readdirSync(join(root, "site/setup/js/preview")))
        assert.ok(existsSync(join(setup, "js/preview", file)), `setup/js/preview/${file}`);
    for (const [file, path] of filesNamed(setup, "index.html", "style.css"))
        assert.ok(existsSync(join(setup, path)), `setup/${file}'s ${path}`);
    assert.ok(readFileSync(join(setup, "index.html"), "utf8").includes('href="../"'), "Setup leads back to the landing page");
    // Every module the page's scripts import, where they import it from.
    for (const dir of ["js", "js/preview"]) {
        for (const file of readdirSync(join(setup, dir)).filter(f => f.endsWith(".mjs"))) {
            const source = readFileSync(join(setup, dir, file), "utf8");
            for (const [, path] of source.matchAll(/^import [^;]* from "(\.[^"]+)";/gm))
                assert.ok(existsSync(join(setup, dir, path)), `setup/${dir}/${file}'s ${path}`);
        }
    }
});

test("the landing page's themes are the app's, each with its colours and pictures", () => {
    const themes = JSON.parse(readFileSync(join(out, "themes.json"), "utf8"));
    const offered = valuesOf(appRows.find(r => r.key === "osd_theme")).filter(v => v !== "" && v !== "Off").sort();
    assert.deepEqual(themes.map(t => t.id).sort(), offered);
    for (const theme of themes) {
        assert.equal(theme.name, schema.looks.themes[theme.id].name);
        assert.ok(theme.about.length > 0, `${theme.id}'s line`);
        assert.match(theme.primary, /^#[0-9A-Fa-f]{6}$/, theme.id);
        assert.match(theme.surface, /^#[0-9A-Fa-f]{6}$/, theme.id);
        for (const file of [theme.still, theme.recording])
            assert.ok(existsSync(join(out, file)), `${theme.id}'s ${file}`);
    }
});

test("the live preview draws with the app's own shaders, as they are", () => {
    const frags = readdirSync(join(root, "shaders")).filter(f => f.endsWith(".frag"));
    assert.deepEqual(schema.looks.shaders, frags.map(f => f.slice(0, -5)).sort());
    for (const file of frags)
        assert.equal(readFileSync(join(setup, "shaders", file), "utf8"), read(`shaders/${file}`), file);
    // Each background's and window change's shader, by the names Main.qml
    // gives them.
    for (const name of [...Object.values(schema.looks.background), ...Object.values(schema.looks.transitionShaders)])
        assert.ok(schema.looks.shaders.includes(name), name);
    assert.ok(schema.looks.shaders.includes("effects"));
});

test("the looks the preview draws are Main.qml's, each theme's and each skin's", () => {
    const looks = schema.looks;
    // Every colour scheme Settings offers, with its two colours.
    for (const name of valuesOf(appRows.find(r => r.key === "color_scheme")).filter(v => v !== "" && v !== "Off")) {
        assert.match(looks.schemes[name].primary, /^#[0-9A-Fa-f]{6}$/, name);
        assert.match(looks.schemes[name].surface, /^#[0-9A-Fa-f]{6}$/, name);
    }
    assert.deepEqual(Object.keys(looks.screen).sort(), qmlNames(mainQml, "screenPresets").sort());
    assert.deepEqual(Object.keys(looks.text).sort(), qmlNames(mainQml, "textPresets").sort());
    assert.deepEqual(Object.keys(looks.background).sort(), qmlNames(mainQml, "backgroundPresets").sort());
    assert.deepEqual(looks.selector.slice().sort(), qmlNames(mainQml, "selectorPresets").sort());
    assert.deepEqual(looks.transition.slice().sort(), qmlNames(mainQml, "transitionPresets").sort());
    for (const knobs of [...Object.values(looks.screen), ...Object.values(looks.text)])
        assert.ok(Object.values(knobs).length && Object.values(knobs).every(v => typeof v === "number"));
    // Each theme Settings offers, and the pictures of every skin.
    const themes = valuesOf(appRows.find(r => r.key === "osd_theme")).filter(v => v !== "" && v !== "Off");
    assert.deepEqual(Object.keys(looks.themes).sort(), themes.sort());
    const skins = [...Object.values(looks.skins), ...Object.values(looks.themes).map(t => t.skin).filter(s => typeof s === "object")];
    assert.ok(skins.length > 0);
    for (const skin of skins) {
        for (const part of Object.values(skin)) {
            assert.ok(existsSync(join(setup, part.source)), part.source);
            assert.equal(part.border.length, 4);
            assert.ok(!("file" in part), "no path of the build's own");
        }
    }
    for (const theme of Object.values(looks.themes)) {
        if (typeof theme.skin === "string")
            assert.ok(theme.skin in looks.skins, theme.skin);
        if (typeof theme.colors === "string")
            assert.ok(theme.colors in looks.schemes, theme.colors);
    }
});

test("a skin's pictures only from its own folder, as the app takes them", () => {
    const dir = mkdtempSync(join(tmpdir(), "osdos-skin-"));
    try {
        const check = `
import importlib.util, json, os, pathlib, sys
spec = importlib.util.spec_from_file_location("site", sys.argv[1])
site = importlib.util.module_from_spec(spec)
spec.loader.exec_module(site)
root = pathlib.Path(sys.argv[2])
look = root / "look"
(look / "sub").mkdir(parents=True)
for name in ("look/a.png", "look/sub/b.png", "look/a.txt", "outside.png"):
    (root / name).write_bytes(b"x")
os.symlink(root / "outside.png", look / "link.png")
parts = {}
for image in ("a.png", "sub/b.png", "sub/../a.png", "../outside.png", str(root / "outside.png"), "link.png", "a.txt", ""):
    part = site.skin_part({"image": image, "border": [1, 2, 3, 4.5]}, look, "skins/look")
    parts[image] = part and {"source": part["source"], "border": part["border"]}
print(json.dumps(parts))`;
        const parts = JSON.parse(execFileSync("python3", ["-I", "-B", "-c", check, join(root, "scripts/build-site.py"), dir], { encoding: "utf8" }));
        // A side of the border a whole number, or none (QJsonValue::toInt()).
        const kept = { source: "skins/look/a.png", border: [1, 2, 3, 0] };
        assert.deepEqual(parts["a.png"], kept);
        assert.deepEqual(parts["sub/b.png"], { source: "skins/look/sub/b.png", border: [1, 2, 3, 0] });
        assert.deepEqual(parts["sub/../a.png"], kept);
        for (const image of Object.keys(parts).filter(i => !["a.png", "sub/b.png", "sub/../a.png"].includes(i)))
            assert.equal(parts[image], null, image);
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});

test("the time zones the card has, by their names there", () => {
    for (const zone of ["Europe/Istanbul", "Asia/Kolkata", "Europe/Kyiv", "America/New_York", "UTC"])
        assert.ok(schema.zones.includes(zone), zone);
    // tzdata-legacy's older names, which a browser may offer, aren't on the card.
    for (const zone of ["Asia/Calcutta", "Europe/Kiev", "US/Eastern"])
        assert.ok(!schema.zones.includes(zone), zone);
});

test("a theme's parts and the sliders' ends, as Settings has them", () => {
    assert.deepEqual(appRows.filter(r => r.theme_part).map(r => r.key).sort(), qmlNames(settingsQml, "themeParts").sort());
    for (const row of appRows.filter(r => r.type.startsWith("slider"))) {
        assert.ok(settingsQml.includes(`startText: "${row.start_text}"`) && settingsQml.includes(`endText: "${row.end_text}"`),
                  `${row.key}: ${row.start_text} … ${row.end_text}`);
    }
});

test("a folder it didn't build isn't replaced", () => {
    const mine = mkdtempSync(join(tmpdir(), "osdos-not-site-"));
    try {
        writeFileSync(join(mine, "keep.txt"), "mine");
        assert.throws(() => execFileSync("python3", ["-I", join(root, "scripts/build-site.py"), mine], { stdio: "pipe" }));
        assert.equal(readFileSync(join(mine, "keep.txt"), "utf8"), "mine");
    } finally {
        rmSync(mine, { recursive: true, force: true });
    }
});

test("every General and TV / Movie row is one of Settings'", () => {
    assert.ok(appRows.length > 20);
    for (const row of appRows)
        assert.ok(settingsQml.includes(`"${row.key}"`), `app.${row.key} in views/Settings.qml`);
});

test("the looks offered are the app's", () => {
    const offered = key => valuesOf(appRows.find(r => r.key === key)).filter(v => v !== "" && v !== "Off").sort();
    assert.deepEqual(offered("text_effect"), qmlNames(mainQml, "textPresets").sort());
    assert.deepEqual(offered("background_effect"), qmlNames(mainQml, "backgroundPresets").sort());
    assert.deepEqual(offered("screen_effect"), qmlNames(mainQml, "screenPresets").sort());
    assert.deepEqual(offered("selector_effect"), qmlNames(mainQml, "selectorPresets").sort());
    assert.deepEqual(offered("transition"), qmlNames(mainQml, "transitionPresets").sort());
    // Settings offers Main.qml's schemes (root.allSchemes), as the page does.
    assert.deepEqual(offered("color_scheme"), qmlNames(mainQml, "schemes").sort());
    assert.match(settingsQml, /partRow\("color_scheme",[^)]*root\.allSchemes/);
    const folders = dir => readdirSync(join(root, "assets", dir), { withFileTypes: true })
        .filter(e => e.isDirectory() && existsSync(join(root, "assets", dir, e.name, `${dir.slice(0, -1)}.json`)))
        .map(e => e.name).sort();
    // Every theme but one whose theme.json says "setup": false, which is the
    // app's alone (Orb's).
    const appsAlone = folders("themes").filter(name =>
        JSON.parse(readFileSync(join(root, "assets/themes", name, "theme.json"), "utf8")).setup === false);
    assert.ok(appsAlone.includes("orb"));
    assert.deepEqual(offered("osd_theme"), folders("themes").filter(name => !appsAlone.includes(name)));
    assert.ok(!appsAlone.some(name => name in schema.looks.themes));
    assert.deepEqual(offered("skin"), folders("skins"));
});

test("each module's rows are its manifest's, with values it takes", () => {
    const manifests = new Map();
    for (const dir of readdirSync(join(root, "modules"))) {
        const path = join(root, "modules", dir, "manifest.json");
        if (existsSync(path)) {
            const manifest = JSON.parse(readFileSync(path, "utf8"));
            manifests.set(manifest.id, manifest);
        }
    }
    assert.deepEqual(schema.modules.map(m => m.id).sort(), [...manifests.keys()].sort());
    for (const module of schema.modules) {
        const keys = new Set();
        const walk = settings => settings.forEach(s => { keys.add(s.key); walk(s.settings || []); });
        walk(manifests.get(module.id).settings || []);
        for (const row of module.settings.filter(r => r.type !== "heading"))
            assert.ok(keys.has(row.key) || ["server_url"].includes(row.key), `${module.id}: ${row.key} in its manifest`);
    }
    const startOn = valuesOf(appRows.find(r => r.key === "startup_module")).filter(v => v !== "None");
    for (const id of startOn)
        assert.ok(manifests.has(id), `Start on Module's ${id}`);
});

test("defaults are among the values, and rows show with rows that are there", () => {
    const scopes = [["app", appRows], ...schema.modules.map(m => [m.id, m.settings])];
    for (const [scope, rows] of scopes) {
        const keys = new Set(rows.map(r => r.key));
        for (const row of rows) {
            if (row.type === "choice")
                assert.ok(valuesOf(row).includes(String(row.default)), `${scope}.${row.key}: ${row.default}`);
            if (row.type === "toggle")
                assert.ok([row.on, row.off].includes(row.default), `${scope}.${row.key}: ${row.default}`);
            for (const c of [row.shown_with, row.shown_unless].flat().filter(Boolean))
                assert.ok(keys.has(c.key), `${scope}.${row.key} shows with ${c.key}`);
        }
    }
});

test("every display output is a preset the zip can carry, on the boards Settings offers it", () => {
    assert.ok(schema.presets.length > 10);
    for (const preset of schema.presets) {
        assert.ok(existsSync(join(setup, "presets", `osdos-display-${preset.id}.txt`)), preset.id);
        assert.ok(preset.models.length > 0 && preset.models.every(m => ["pi3", "pi4", "pi5"].includes(m)), preset.id);
    }
    assert.deepEqual(schema.presets.find(p => p.id === "hdmi").models, ["pi3", "pi4", "pi5"]);
    assert.ok(schema.countries.some(([, code]) => code === "TR"));
});
