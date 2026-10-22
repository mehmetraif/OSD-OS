// The look a card's settings give, worked out as Main.qml works it out: the
// colour scheme's two colours (Settings' own, else the theme's), the skin,
// each effect (Settings' row unless it is THEME, "", else the theme's), the
// ground and its frame, the bars, the status icons, the main menu's rows and
// the Arcade screen's settings. `looks` is schema.json's (scripts/build-site.py
// reads it from Main.qml, assets/themes and assets/skins); `settings` is what
// the page holds, each row's default where it is unset.
//   settings = { app: { key: value }, modules: [{ id, name, folder, enabled,
//                values: { key: value } }], wifi, account, key }
import { isOn as on } from "../core.mjs";

export const ARCADE = "com.osdos.arcade";

export const SCREEN_KNOBS = ["scanlines", "curvature", "glow", "bleed", "noise", "vignette"];
export const TEXT_KNOBS = ["rainbow", "shimmer", "flicker", "glow"];

// One effect in force, as effectOf() and nameOf() have it: Settings' row
// unless it is THEME (""), else the theme's; a preset's name read from the
// presets, {} or "" for none.
function effectOf(setting, fromTheme, presets) {
    const e = setting !== "" && setting !== undefined ? setting : fromTheme;
    if (e === undefined || e === null || e === "Off")
        return {};
    return typeof e === "string" ? presets[e] || {} : e;
}
function nameOf(setting, fromTheme, names) {
    const e = setting !== "" && setting !== undefined ? setting : fromTheme;
    return typeof e === "string" && names.includes(e) ? e : "";
}
// Cube Left, Right, Up and Down, as a theme or an older config may still name
// them: the cube.
const cubeOf = name => /^Cube (Left|Right|Up|Down)$/.test(name) ? "Cube" : name;

// A colour's channels, 0 to 255, and its parts, 0 to 1.
export const bytesOf = hex => { const n = parseInt(String(hex).replace("#", ""), 16); return [n >> 16 & 255, n >> 8 & 255, n & 255]; };
export const rgbOf = hex => bytesOf(hex).map(c => c / 255);
// The lighter of two colours, by how bright the eye finds them.
function lighterOf(a, b) {
    const luma = c => { const [r, g, bl] = rgbOf(c); return 0.2126 * r + 0.7152 * g + 0.0722 * bl; };
    return luma(a) >= luma(b) ? a : b;
}

export function lookOf(looks, settings) {
    const app = settings.app;
    const theme = looks.themes[app.osd_theme] || {};
    const themeEffects = theme.effects || {};

    const colors = app.color_scheme !== "" && app.color_scheme !== undefined ? app.color_scheme : theme.colors;
    const video1 = looks.schemes["Video 1"];
    const scheme = colors === undefined || colors === "Off" ? video1
                 : typeof colors === "string" ? looks.schemes[colors] || video1
                 : colors && /^#[0-9A-Fa-f]{6}$/.test(colors.primary || "") && /^#[0-9A-Fa-f]{6}$/.test(colors.surface || "")
                   ? colors : video1;
    const skinOf = value => typeof value === "string" ? looks.skins[value] || {} : value || {};
    const skin = app.skin === "Off" ? {} : app.skin ? skinOf(app.skin) : skinOf(theme.skin);

    const screenEffect = effectOf(app.screen_effect, themeEffects.screen, looks.screen);
    const textEffect = effectOf(app.text_effect, themeEffects.text, looks.text);
    const backgrounds = Object.fromEntries(Object.entries(looks.background).map(([n, shader]) => [n, { shader }]));
    const background = effectOf(app.background_effect, themeEffects.background, backgrounds);

    const osdBackground = app.osd_background === "Window" || app.osd_background === "Off" ? app.osd_background : "Full";
    const osdFrame = app.osd_frame === "Off" || app.osd_frame === "Shadow" ? app.osd_frame : "On";
    const osdOff = osdBackground === "Off";
    const primary = osdOff ? lighterOf(scheme.primary, scheme.surface) : scheme.primary;
    const surface = osdOff ? "#000000" : scheme.surface;

    const knobs = (effect, names) => Object.fromEntries(names.map(k => [k, Number(effect[k]) || 0]));

    // The main menu's rows: the modules on, in their folders' order.
    const modules = settings.modules.filter(m => m.enabled).sort((a, b) => a.folder < b.folder ? -1 : 1);
    const arcadeModule = settings.modules.find(m => m.id === ARCADE);
    const arcadeValues = arcadeModule ? arcadeModule.values : {};

    // What is on (Settings → Status Icons): the Arcade's, while it is on, then
    // Wi-Fi's bars once the card has a network.
    const statusIcons = [];
    if (on(app.status_icons)) {
        if (arcadeModule && arcadeModule.enabled) {
            if (on(arcadeValues.high_scores))
                statusIcons.push("hiscore");
            if (arcadeValues.online_high_scores === "RetroAchievements" && settings.account)
                statusIcons.push("trophy");
            if (on(arcadeValues.online_play))
                statusIcons.push("globe");
            if (on(arcadeValues.lan_play))
                statusIcons.push("network");
        }
        if (settings.wifi)
            statusIcons.push("signal4");
    }

    return {
        primary, surface,
        // The window's colour, under everything: black but for Full.
        windowColor: osdBackground === "Full" ? surface : "#000000",
        skin,
        screen: knobs(screenEffect, SCREEN_KNOBS),
        text: knobs(textEffect, TEXT_KNOBS),
        background: typeof background.shader === "string" ? background.shader : "",
        selector: nameOf(app.selector_effect, themeEffects.selector, looks.selector),
        transition: nameOf(cubeOf(app.transition), cubeOf(themeEffects.transition), looks.transition),
        osdBackground, osdFrame,
        hintBar: on(app.hint_bar),
        helpLine: on(app.help_line),
        statusIcons,
        menu: modules.map(m => ({ id: m.id, name: m.name })),
        arcade: {
            screen: arcadeValues.screen || "Arcade",
            listStyle: arcadeValues.list_style || "Curved",
            border: on(arcadeValues.border ?? true),
            infoBackground: Number(arcadeValues.info_background ?? 100),
            hintBar: on(arcadeValues.hint_bar ?? true),
            helpLine: on(arcadeValues.help_line ?? true),
            startFilter: arcadeValues.start_filter || "all",
            webApiKey: !!settings.key,
        },
    };
}
