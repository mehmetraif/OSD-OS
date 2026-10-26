// OSD/OS Setup's page: the form made from schema.json (scripts/build-site.py
// writes it from the app), what it holds, and the file it gives
// (core.mjs makes it). Only what is changed goes into the file, marked • on
// its line; ↺ takes a line out again.
import * as core from "./core.mjs";
import { el } from "./dom.mjs";
import { Preview } from "./preview/preview.mjs";

let schema = null;
let state = fresh();
// The live preview, made once the schema is in and kept as the form is drawn
// again.
let preview = null;

function fresh() {
    return {
        // autoStartup: what Start on Module was before Arcade Cabinet set it
        // ({ was }), which TV puts back; null once it is the user's own.
        kind: "tv", showTv: false, autoStartup: null,
        model: "pi4",
        displayPreset: "",
        deviceName: "",
        wifiCountry: "", wifiSsid: "", wifiPassword: "", wifiHidden: false,
        passwordPlain: "", passwordHash: "",
        sshEnabled: null, sshPasswordLogin: true, sshKeys: "",
        timezone: "", keyboard: "",
        raUser: "", raPassword: "", raKey: "",
        config: {},
    };
}

// The rows a theme gives (Settings' themeParts, marked theme_part in the
// schema): THEME while there is a theme, set back to it as one is chosen.
let THEME_PARTS = [];
const TV_GROUP = "TV / Movie";

// --- Small builders.
// A line: the label, dots, then its control against the line's end, and its
// help under it. `changed` marks it as in the file, with ↺ to take it out (a
// line without one keeps its room, so that every control ends alike).
function line(label, control, { help = "", changed = false, onReset = null, id = control.id } = {}) {
    const row = el("div", { class: "row" + (changed ? " changed" : "") },
        el("label", { for: id, text: label }),
        el("span", { class: "dots", "aria-hidden": "true" }),
        control,
        onReset ? el("button", { type: "button", class: "reset secondary", title: "Leave it as the card has it",
                                 "aria-label": `${label}: leave it as the card has it`, onclick: onReset }, "↺")
                : el("span", { class: "reset", "aria-hidden": "true" }));
    const wrap = el("div", { class: "line" }, row);
    if (help)
        wrap.append(el("p", { class: "help", text: help }));
    return wrap;
}

// The same id each time the form is drawn, so that focus finds its line again.
function idFor(...path) {
    return "o-" + path.join("-").replace(/[^A-Za-z0-9_-]/g, "_");
}

function select(options, value, onchange, { id, leave = null } = {}) {
    const node = el("select", { id, onchange: e => onchange(e.target.value) },
        leave !== null ? el("option", { value: "", text: leave }) : null,
        options.map(([label, v]) => el("option", { value: v, text: label })));
    node.value = value;
    return node;
}

function checkbox(checked, onchange, { id, label = "" } = {}) {
    return el("input", { type: "checkbox", id, checked, "aria-label": label || null,
                         onchange: e => onchange(e.target.checked) });
}

// A checkbox with ON or OFF after it, as the app's toggles read; nothing
// after it while it is left as the card has it (`unset`).
function toggle(on, onchange, { id, unset = false } = {}) {
    const state = el("span", { class: "state", text: unset ? "" : on ? "ON" : "OFF" });
    return el("span", { class: "toggle" }, checkbox(on, value => {
        state.textContent = value ? "ON" : "OFF";
        onchange(value);
    }, { id }), state);
}

// A password with its SHOW / HIDE button; one shown stays shown as the form
// is drawn again.
const shownSecrets = new Set();
function secret(value, oninput, { id, placeholder = "", autocomplete = "off" } = {}) {
    const shown = shownSecrets.has(id);
    const input = el("input", { type: shown ? "text" : "password", id, value, placeholder, autocomplete,
                                spellcheck: "false", oninput: e => oninput(e.target.value) });
    const button = el("button", { type: "button", class: "secondary", "aria-pressed": String(shown),
                                  "aria-controls": id, text: shown ? "Hide" : "Show" });
    button.addEventListener("click", () => {
        const show = input.type === "password";
        input.type = show ? "text" : "password";
        button.textContent = show ? "Hide" : "Show";
        button.setAttribute("aria-pressed", String(show));
        if (show)
            shownSecrets.add(id);
        else
            shownSecrets.delete(id);
    });
    const wrap = el("span", { class: "secret" }, input, button);
    wrap.id = id + "-wrap";
    return wrap;
}

function text(value, oninput, { id, placeholder = "", maxlength = null, onchange = null } = {}) {
    return el("input", { type: "text", id, value, placeholder, spellcheck: "false", autocomplete: "off", maxlength,
                         oninput: e => oninput(e.target.value), onchange });
}

function section(title, help, ...children) {
    return el("section", {}, el("h2", { text: title }), help ? el("p", { class: "section-help", text: help }) : null,
              ...children);
}

function say(message, ok = false) {
    const status = document.getElementById("status");
    status.textContent = message;
    status.className = "status" + (ok ? " ok" : message ? " error" : "");
}

// --- The parts of the form.
// What the card goes into, first: a cabinet starts on the Arcade screen, and
// the TV's settings are put away.
function device() {
    const kinds = [
        ["tv", "TV", "Films, series and music videos on a TV: OSD/OS starts on its main menu."],
        ["arcade", "Arcade Cabinet", "Arcade games in a cabinet: OSD/OS starts on the Arcade screen, and the TV / Movie settings are put away."],
    ];
    return section("Device", "What the card goes into. The settings below follow it, and anything can still be changed.",
        el("div", { class: "kinds", role: "radiogroup", "aria-label": "Device" },
            kinds.map(([value, label, about]) => el("label", { class: "kind" + (state.kind === value ? " chosen" : "") },
                el("input", { type: "radio", name: "kind", value, checked: state.kind === value,
                              onchange: () => setKind(value) }),
                el("span", { class: "kind-name", text: label }),
                el("span", { class: "kind-about", text: about })))));
}

function setKind(kind) {
    if (kind === state.kind)
        return;
    state.kind = kind;
    const path = ["app", "startup_module"];
    const startup = core.getPath(state.config, path);
    if (kind === "arcade") {
        // On the Arcade screen from the start, its settings open below.
        if (startup === undefined || startup === "None") {
            core.setPath(state.config, path, core.ARCADE_MODULE);
            state.autoStartup = { was: startup };
        }
        openModules.add(core.ARCADE_MODULE);
    } else if (state.autoStartup && startup === core.ARCADE_MODULE) {
        if (state.autoStartup.was === undefined)
            core.deletePath(state.config, path);
        else
            core.setPath(state.config, path, state.autoStartup.was);
        state.autoStartup = null;
    }
    render();
}

function raspberryPi() {
    const models = [["Raspberry Pi 5", "pi5"], ["Raspberry Pi 4", "pi4"], ["Raspberry Pi 3", "pi3"]];
    return section("Raspberry Pi", "Which Pi the card is for: the display outputs below are the ones it has.",
        line("Model", select(models, state.model, value => {
            state.model = value;
            const preset = schema.presets.find(p => p.id === state.displayPreset);
            if (preset && !preset.models.includes(value))
                state.displayPreset = "";
            render();
        }, { id: "model" })));
}

function displayOutput() {
    let offered = schema.presets.filter(p => p.models.includes(state.model));
    // A cabinet's JAMMA board first.
    if (state.kind === "arcade")
        offered = [...offered.filter(p => p.id.startsWith("jamma-")), ...offered.filter(p => !p.id.startsWith("jamma-"))];
    const chosen = schema.presets.find(p => p.id === state.displayPreset);
    // Its warning once, as the paragraph under the line.
    const help = chosen ? chosen.about : "Leave as it is: a new card starts on HDMI.";
    return section("Display Output", "The picture's way out of the Pi, read as the Pi powers on: for a CRT or a JAMMA cabinet, the one it needs.",
        line("Output", select(offered.map(p => [p.label, p.id]), state.displayPreset, value => {
            state.displayPreset = value;
            render();
        }, { id: "display", leave: "Leave as it is" }), {
            help, changed: !!state.displayPreset,
            onReset: () => { state.displayPreset = ""; render(); },
        }),
        chosen && chosen.warning ? el("p", { class: "help warn", text: chosen.warning }) : null);
}

function network() {
    const name = state.deviceName ? core.hostnameFor(state.deviceName) : null;
    return section("Network", "The device's name tells it apart from others on the network (Settings → About → System Information shows it). Wi-Fi joins a network at start; Ethernet needs nothing.",
        line("Device Name", text(state.deviceName, value => { state.deviceName = value; refreshDeviceNameHelp(); markChanged("device-name", !!value); },
                                 { id: "device-name", placeholder: "osdos" }),
             { changed: !!state.deviceName, onReset: () => { state.deviceName = ""; render(); } }),
        el("p", { class: "help" + (name && name.why ? " error" : ""), id: "device-name-help", text: deviceNameHelp() }),
        line("Wi-Fi Country", select(schema.countries, state.wifiCountry, value => { state.wifiCountry = value; render(); },
                                     { id: "wifi-country", leave: "Leave as it is" }),
             { help: "The country whose rules Wi-Fi keeps to. Raspberry Pi OS keeps Wi-Fi off until one is set.",
               changed: !!state.wifiCountry, onReset: () => { state.wifiCountry = ""; render(); } }),
        line("Wi-Fi Network", text(state.wifiSsid, value => { state.wifiSsid = value; refreshWifiHelp(); markChanged("wifi-ssid", !!value); },
                                   { id: "wifi-ssid", placeholder: "The network's name" }),
             { changed: !!state.wifiSsid, onReset: () => { state.wifiSsid = ""; state.wifiPassword = ""; state.wifiHidden = false; render(); } }),
        line("Wi-Fi Password", secret(state.wifiPassword, value => { state.wifiPassword = value; refreshWifiHelp(); },
                                      { id: "wifi-password", placeholder: "Empty for an open network" }),
             { id: "wifi-password" }),
        line("Hidden Network", toggle(state.wifiHidden, value => { state.wifiHidden = value; }, { id: "wifi-hidden" }),
             { id: "wifi-hidden", help: "On for a network that doesn't say its name." }),
        el("p", { class: "help", id: "wifi-help" }));
}

function deviceNameHelp() {
    if (!state.deviceName)
        return "Empty: the name it has (osdos on a new card).";
    const name = core.hostnameFor(state.deviceName);
    return name.why || `On the network as ${name.name} (${name.name}.local).`;
}

function refreshDeviceNameHelp() {
    const node = document.getElementById("device-name-help");
    if (!node)
        return;
    node.textContent = deviceNameHelp();
    node.classList.toggle("error", !!state.deviceName && !!core.hostnameFor(state.deviceName).why);
}

function wifiProblem() {
    if (!state.wifiSsid)
        return "";
    return core.checkWifi(state.wifiSsid, state.wifiPassword)
        || (state.wifiCountry ? "" : "Choose the Wi-Fi Country too: Wi-Fi stays off without one");
}

function refreshWifiHelp() {
    const node = document.getElementById("wifi-help");
    const problem = wifiProblem();
    node.textContent = problem;
    node.classList.toggle("error", !!problem);
}

function markChanged(id, changed) {
    const node = document.getElementById(id);
    const row = node && node.closest(".row");
    if (row)
        row.classList.toggle("changed", changed);
}

// Time zones as the card has them: the zone list's names (build-site.py
// reads them from the time zone database), not a browser's older ones.
function system() {
    const imported = state.passwordHash && !state.passwordPlain;
    const zones = schema.zones.map(z => [z.replace(/_/g, " "), z]);
    return section("System", "The user OSD/OS runs as, pi, and the system around it, as Raspberry Pi Imager would set them.",
        line("pi's Password", secret(state.passwordPlain, value => { state.passwordPlain = value; markChanged("pi-password", !!value || !!state.passwordHash); },
                                     { id: "pi-password", placeholder: imported ? "Kept from the imported file" : "Empty: as it is",
                                       autocomplete: "new-password" }),
             { id: "pi-password", changed: !!(state.passwordPlain || state.passwordHash),
               help: "For logging in, over SSH too, and for sudo. Only its hash goes into the file.",
               onReset: () => { state.passwordPlain = ""; state.passwordHash = ""; render(); } }),
        line("SSH", toggle(state.sshEnabled === true, value => { state.sshEnabled = value; render(); },
                           { id: "ssh", unset: state.sshEnabled === null }),
             { id: "ssh", changed: state.sshEnabled !== null,
               help: "A login from another computer: ssh pi@<device name>.local.",
               onReset: () => { state.sshEnabled = null; state.sshPasswordLogin = true; state.sshKeys = ""; render(); } }),
        state.sshEnabled ? line("Password Login", toggle(state.sshPasswordLogin, value => { state.sshPasswordLogin = value; },
                                                         { id: "ssh-password-login" }),
                                { id: "ssh-password-login",
                                  help: "On: with pi's password, set above. Off: only the keys below log in." }) : null,
        state.sshEnabled ? el("div", { class: "line" },
            el("label", { for: "ssh-keys", class: "label", text: "SSH Keys" }),
            el("textarea", { id: "ssh-keys", placeholder: "ssh-ed25519 AAAA… one public key a line", spellcheck: "false",
                             oninput: e => { state.sshKeys = e.target.value; } }, state.sshKeys)) : null,
        line("Time Zone", select(zones, state.timezone, value => { state.timezone = value; render(); },
                                 { id: "timezone", leave: "Leave as it is" }),
             { changed: !!state.timezone, onReset: () => { state.timezone = ""; render(); } }),
        line("Keyboard", select(schema.keyboards, state.keyboard, value => { state.keyboard = value; render(); },
                                { id: "keyboard", leave: "Leave as it is" }),
             { help: "A keyboard's layout, at Exit to Terminal.", changed: !!state.keyboard,
               onReset: () => { state.keyboard = ""; render(); } }));
}

// --- OSD/OS's own settings: app.<key>, and modules.<id>.<key>.
const theme = () => core.getPath(state.config, ["app", "osd_theme"]) || "";

// What a row holds, as the app reads it: its default when unset; a theme's
// part left at THEME without a theme as what it is then (its first choice);
// an old word for a number as that number.
function valueAt(scope, row) {
    const set = core.getPath(state.config, [...scope, row.key]);
    let value = set === undefined ? row.default : set;
    if (row.theme_part && !theme() && value === "")
        value = themeless(row)[0][1];
    if (row.legacy && typeof value === "string" && value.toLowerCase() in row.legacy)
        value = row.legacy[value.toLowerCase()];
    return value;
}

// A theme's part's choices without a theme: THEME is no choice then.
function themeless(row) {
    return row.options.filter(([, v]) => v !== "");
}

function setValue(scope, key, value) {
    core.setPath(state.config, [...scope, key], value);
    if (scope.length === 1 && key === "startup_module")
        state.autoStartup = null;
    // As Settings does: a theme chosen sets its parts back to THEME.
    if (scope.length === 1 && key === "osd_theme") {
        for (const part of THEME_PARTS)
            core.setPath(state.config, ["app", part], "");
    }
    render();
}

function unset(scope, key) {
    core.deletePath(state.config, [...scope, key]);
    if (scope.length === 1 && key === "startup_module")
        state.autoStartup = null;
    render();
}

function settingLine(scope, row) {
    if (row.type === "heading")
        return el("h3", { class: "subheading", text: row.label });
    const value = valueAt(scope, row);
    const changed = core.getPath(state.config, [...scope, row.key]) !== undefined;
    const id = idFor(...scope, row.key);
    let control;
    switch (row.type) {
    case "toggle": {
        control = toggle(core.toggleOn(row, value), checked => setValue(scope, row.key, checked ? row.on : row.off), { id });
        break;
    }
    case "choice": {
        let options = row.theme_part && !theme() ? themeless(row) : row.options;
        if (!options.some(([, v]) => String(v) === String(value)))
            options = [...options, [String(value), String(value)]];
        control = select(options, String(value), v => setValue(scope, row.key, v), { id });
        break;
    }
    case "slider":
        control = slider(row, id, value, true, v => setValue(scope, row.key, v));
        break;
    case "slider_off": {
        const on = value !== row.off;
        control = el("span", { class: "slider" },
            checkbox(on, checked => setValue(scope, row.key, checked ? row.on_default : row.off),
                     { id: id + "-on", label: `${row.label} on` }),
            slider(row, id, on ? value : row.on_default, on, v => setValue(scope, row.key, v)));
        break;
    }
    case "multi": {
        const current = core.isPlainObject(value) ? value : {};
        // Choices under headings ({ heading }, Arcade's regions under their
        // parts of the world) go on lines of their own under the label.
        const grouped = row.options.some(option => !Array.isArray(option));
        control = el("span", { class: grouped ? "multi grouped" : "multi", id },
            row.options.map(option => !Array.isArray(option) ? el("span", { class: "group", text: option.heading })
                : el("label", { class: "pick" },
                    checkbox(current[option[1]] !== false, checked => setValue(scope, row.key, { ...current, [option[1]]: checked })),
                    " " + option[0])));
        break;
    }
    default:
        control = text(String(value ?? ""), v => { core.setPath(state.config, [...scope, row.key], v); markChanged(id, true); },
                       { id, placeholder: row.placeholder || "", maxlength: row.max_length || null,
                         // As the app keeps it: without spaces at either end.
                         onchange: row.trim ? e => {
                             e.target.value = e.target.value.trim();
                             core.setPath(state.config, [...scope, row.key], e.target.value);
                         } : null });
    }
    return line(row.label, control, { id, help: row.help, changed, onReset: () => unset(scope, row.key) });
}

// A slider with its ends' words, as the app's (TRANSPARENT … SOLID), and the
// number it is at.
function slider(row, id, value, enabled, onchange) {
    return el("span", { class: "slider" },
        row.start_text ? el("span", { class: "end", text: row.start_text }) : null,
        el("input", { type: "range", id, min: row.min, max: row.max, step: row.step, value, disabled: !enabled,
                      "aria-valuetext": `${value}${row.start_text ? ` (${row.start_text} ${row.min} to ${row.end_text} ${row.max})` : ""}`,
                      onchange: e => onchange(Number(e.target.value)) }),
        row.end_text ? el("span", { class: "end", text: row.end_text }) : null,
        el("span", { class: "state", text: enabled ? String(value) : "OFF" }));
}

function settingLines(scope, rows) {
    const valueOf = key => {
        const row = rows.find(r => r.key === key);
        return row ? valueAt(scope, row) : core.getPath(state.config, [...scope, key]);
    };
    return rows.filter(row => core.shown(row, valueOf)).map(row => settingLine(scope, row));
}

function retroAchievements() {
    return el("div", { class: "account" },
        el("h3", { class: "subheading", text: "RetroAchievements Account" }),
        el("p", { class: "help", text: "Signed in as OSD/OS starts: RetroArch swaps the password for a token, and the password is kept nowhere. The Web API key, from retroachievements.org's Settings page, reads the leaderboards and achievements." }),
        line("User Name", text(state.raUser, v => { state.raUser = v; markChanged("ra-user", !!v); }, { id: "ra-user" }),
             { changed: !!state.raUser, onReset: () => { state.raUser = ""; render(); } }),
        line("Password", secret(state.raPassword, v => { state.raPassword = v; markChanged("ra-password", !!v); },
                                { id: "ra-password", autocomplete: "new-password" }),
             { id: "ra-password", changed: !!state.raPassword, onReset: () => { state.raPassword = ""; render(); } }),
        line("Web API Key", secret(state.raKey, v => { state.raKey = v; markChanged("ra-key", !!v); }, { id: "ra-key" }),
             { id: "ra-key", changed: !!state.raKey, onReset: () => { state.raKey = ""; render(); } }));
}

const startupModule = () => core.getPath(state.config, ["app", "startup_module"]);

// Whether a module is on as the file would leave it: as set, else as its
// manifest has it, and on with the module OSD/OS starts on.
function moduleEnabled(module) {
    const set = core.getPath(state.config, ["modules", module.id, "enabled"]);
    return set === undefined ? module.enabled || startupModule() === module.id : set;
}

function moduleBlock(module) {
    const scope = ["modules", module.id];
    const enabledSet = core.getPath(state.config, [...scope, "enabled"]);
    const startsOn = startupModule() === module.id;
    const enabled = moduleEnabled(module);
    const open = openModules.has(module.id);
    const details = el("details", { open: open || null },
        el("summary", { text: `${module.name} ${enabled ? "· ON" : "· OFF"}` }),
        line("Enabled", toggle(enabled, on => setValue(scope, "enabled", on), { id: idFor(...scope, "enabled") }),
             { id: idFor(...scope, "enabled"), changed: enabledSet !== undefined, onReset: () => unset(scope, "enabled"),
               help: startsOn && enabledSet === undefined ? "On: OSD/OS starts on it (Start on Module)." : "On the main menu, or not." }),
        settingLines(scope, module.settings),
        module.retroachievements ? retroAchievements() : null);
    details.addEventListener("toggle", () => {
        if (details.open) openModules.add(module.id);
        else openModules.delete(module.id);
    });
    return details;
}
const openModules = new Set();

// What the live preview draws with: every row as the card would have it
// (each unset one at its default; a theme's part "" for THEME, as the app
// reads it), each module on or off as the file would leave it.
function previewSettings() {
    const values = (scope, rows) => Object.fromEntries(rows.filter(row => row.key).map(row => {
        const set = core.getPath(state.config, [...scope, row.key]);
        return [row.key, set === undefined ? row.default : set];
    }));
    return {
        app: values(["app"], schema.app.flatMap(group => group.settings)),
        modules: schema.modules.map(module => ({
            id: module.id, name: module.name, folder: module.folder,
            enabled: moduleEnabled(module),
            values: values(["modules", module.id], module.settings),
        })),
        wifi: !!state.wifiSsid,
        account: !!(state.raUser && state.raPassword),
        key: !!state.raKey,
    };
}

function osdos() {
    const arcade = state.kind === "arcade";
    // A cabinet's TV / Movie settings out of the way, unless asked for.
    const tvShown = !arcade || state.showTv;
    const parts = [preview.element];
    if (arcade) {
        parts.push(line("TV / Movie Settings", toggle(state.showTv, value => { state.showTv = value; render(); },
                                                      { id: "show-tv" }),
                        { id: "show-tv", help: "Off: the TV / Movie settings and modules stay as they are, out of sight." }));
    }
    for (const group of schema.app) {
        if (group.title === TV_GROUP && !tvShown)
            continue;
        parts.push(el("details", { open: true },
            el("summary", { text: group.title }),
            settingLines(["app"], group.settings)));
    }
    let groups = [...new Set(schema.modules.map(m => m.group))].filter(g => g !== TV_GROUP || tvShown);
    // A cabinet's own modules first.
    if (arcade)
        groups = [...groups.filter(g => g === "Arcade"), ...groups.filter(g => g !== "Arcade")];
    for (const group of groups) {
        parts.push(el("h3", { class: "group", text: group === "Modules" ? "Other Modules" : `${group} Modules` }));
        for (const module of schema.modules.filter(m => m.group === group))
            parts.push(moduleBlock(module));
    }
    return section("OSD/OS", "OSD/OS's own settings, as Settings has them. Each module's section has its settings; a module that is off stays off the main menu.", parts);
}

function render() {
    const form = document.getElementById("form");
    const scroll = window.scrollY;
    const focused = document.activeElement && document.activeElement.id;
    form.replaceChildren(device(), raspberryPi(), displayOutput(), network(), system(), osdos());
    preview.update(previewSettings());
    refreshWifiHelp();
    window.scrollTo(0, scroll);
    if (focused) {
        const again = document.getElementById(focused);
        if (again)
            again.focus({ preventScroll: true });
    }
}

// --- The file.
function problems() {
    const out = [];
    if (state.deviceName) {
        const name = core.hostnameFor(state.deviceName);
        if (name.why)
            out.push(`Device Name: ${name.why}`);
    }
    const wifi = wifiProblem();
    if (wifi)
        out.push(`Wi-Fi: ${wifi}`);
    const password = core.checkPassword(state.passwordPlain);
    if (password)
        out.push(`pi's Password: ${password}`);
    if (state.sshEnabled === true) {
        const keys = core.keyLines(state.sshKeys);
        // The card won't let a password in that this file doesn't set: the
        // one it had may be every card's.
        if (state.sshPasswordLogin && !state.passwordPlain && !state.passwordHash)
            out.push("SSH: password login needs pi's password, set here; or turn it off and give a key");
        if (!state.sshPasswordLogin && !keys.length)
            out.push("SSH: with password login off, give a key");
        for (const key of keys) {
            if (!core.checkKey(key))
                out.push(`SSH Keys: not a whole public key: ${key.slice(0, 32)}…`);
        }
    }
    out.push(...core.checkRetroAchievements(state.raUser, state.raPassword, state.raKey));
    const startup = startupModule();
    if (startup && startup !== "None" && core.getPath(state.config, ["modules", startup, "enabled"]) === false) {
        const module = schema.modules.find(m => m.id === startup);
        out.push(`Start on Module: ${module ? module.name : startup} is turned off`);
    }
    return out;
}

async function setupFile() {
    const found = problems();
    if (found.length) {
        say(found.join(" · "));
        return null;
    }
    const s = { ...state };
    s.deviceName = state.deviceName ? core.hostnameFor(state.deviceName).name : "";
    if (state.passwordPlain) {
        say("Hashing pi's password…", true);
        // A moment for the line to show: the hash takes a while.
        await new Promise(resolve => setTimeout(resolve, 30));
        s.passwordHash = await core.sha512Crypt(state.passwordPlain);
    }
    return core.buildSetup(s);
}

function save(name, data, type) {
    const url = URL.createObjectURL(new Blob([data], { type }));
    const a = el("a", { href: url, download: name });
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function counted(setup) {
    const parts = Object.keys(setup).filter(k => k !== "osdos_setup");
    return parts.length ? parts.join(", ") : "nothing yet";
}

async function exportFile() {
    const setup = await setupFile();
    if (!setup)
        return;
    save(core.SETUP_FILE, JSON.stringify(setup, null, 2) + "\n", "application/json");
    say(`Saved osdos-setup.json: ${counted(setup)}.`, true);
}

async function downloadZip() {
    const setup = await setupFile();
    if (!setup)
        return;
    const files = [{ name: core.SETUP_FILE, data: JSON.stringify(setup, null, 2) + "\n" }];
    if (state.displayPreset) {
        // One of the page's own, by its id from the schema alone.
        const preset = schema.presets.find(p => p.id === state.displayPreset);
        const reply = preset && await fetch(`presets/osdos-display-${preset.id}.txt`);
        if (!reply || !reply.ok) {
            say("The display preset couldn't be read from the page: try again.");
            return;
        }
        files.push({ name: "osdos-display.txt", data: new Uint8Array(await reply.arrayBuffer()) });
    }
    save("osdos-setup.zip", core.zip(files), "application/zip");
    say(`Saved osdos-setup.zip: ${files.map(f => f.name).join(" and ")}. Unpack it onto bootfs.`, true);
}

// The same file on the clipboard, for OSD/OS on a computer (Settings → Paste
// OSD/OS Setup). The clipboard is handed its text as a promise in the click
// itself, which Safari asks for, while pi's password may still be hashing.
async function copySetup() {
    const made = setupFile();
    const blob = made.then(setup => setup ? new Blob([JSON.stringify(setup, null, 2) + "\n"], { type: "text/plain" })
                                          : Promise.reject(new Error("problems() said why")));
    blob.catch(() => {});
    try {
        if (typeof ClipboardItem === "function" && navigator.clipboard.write)
            await navigator.clipboard.write([new ClipboardItem({ "text/plain": blob })]);
        else
            await navigator.clipboard.writeText(await (await blob).text());
    } catch {
        if (await made)
            say("The browser didn't let the page copy: export osdos-setup.json and put it in OSD/OS's data folder instead.");
        return;
    }
    say(`Copied: ${counted(await made)}. In OSD/OS on a computer, Settings → Paste OSD/OS Setup sets it.`, true);
}

// What a file read in brings that gets a card in: said, so that one from
// someone else isn't taken on trust.
function carried(read) {
    const out = [];
    if (read.passwordHash)
        out.push("pi's password");
    if (read.sshEnabled === true)
        out.push(read.sshPasswordLogin ? "SSH on, with password login" : "SSH on");
    if (core.keyLines(read.sshKeys).length)
        out.push("SSH keys");
    if (read.wifiPassword)
        out.push("a Wi-Fi password");
    if (read.raPassword)
        out.push("a RetroAchievements password");
    return out;
}

async function importFile(file) {
    try {
        const read = core.readSetup(await file.text());
        const { state: values, dropped } = core.fitImport(read, schema);
        const notes = [];
        if (read.from === "setup") {
            // One of the page's own: the whole form from it.
            const { from, ...rest } = values;
            state = { ...fresh(), ...rest, passwordPlain: "" };
            const what = carried(state);
            notes.push(`Read ${file.name}: change what you need, then download it again.`);
            if (what.length)
                notes.push(`It sets ${what.join(", ")}: keep those only from a file of your own.`);
        } else {
            // Its Start on Module the file's own, whatever the device.
            state.config = values.config;
            state.autoStartup = null;
            notes.push(`Read OSD/OS's settings from ${file.name}.`);
        }
        if (dropped.length)
            notes.push(`Left out, not on this page: ${dropped.slice(0, 6).join(", ")}${dropped.length > 6 ? ` and ${dropped.length - 6} more` : ""}.`);
        openModules.clear();
        shownSecrets.clear();
        say(notes.join(" "), true);
        render();
    } catch (e) {
        say(`${file.name}: ${e.message}`);
    }
}

async function start() {
    try {
        const reply = await fetch("schema.json");
        schema = await reply.json();
    } catch (e) {
        document.getElementById("loading").textContent = "The settings couldn't be loaded: " + e.message;
        return;
    }
    THEME_PARTS = schema.app.flatMap(group => group.settings).filter(row => row.theme_part).map(row => row.key);
    preview = new Preview(schema.looks);
    const picker = document.getElementById("import-file");
    document.getElementById("import").addEventListener("click", () => picker.click());
    picker.addEventListener("change", () => {
        if (picker.files.length)
            importFile(picker.files[0]);
        picker.value = "";
    });
    document.getElementById("export").addEventListener("click", exportFile);
    document.getElementById("download-zip").addEventListener("click", downloadZip);
    document.getElementById("copy").addEventListener("click", copySetup);
    document.getElementById("reset-all").addEventListener("click", () => {
        state = fresh();
        openModules.clear();
        shownSecrets.clear();
        say("");
        render();
    });
    // The landing page's cabinet tab opens the page for a cabinet
    // (?device=arcade); setKind() draws the form.
    if (new URLSearchParams(location.search).get("device") === "arcade")
        setKind("arcade");
    else
        render();
}

start();
