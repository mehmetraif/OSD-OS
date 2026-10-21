// OSD/OS Setup's workings (site/js/core.mjs), under Node's own test runner:
// pi's password hashed as openssl passwd -6 hashes it, the device's name, the
// Wi-Fi, SSH keys and the password checked as the card checks them, the
// setup file made and read back, a file of someone else's taken only as far
// as the page has it, OSD/OS's config.json read for its settings, and a zip
// that Python's zipfile opens. Every name, key and password is an invented
// one.
//   node --test tests/site
import { test } from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import * as core from "../../site/js/core.mjs";

test("pi's password as openssl passwd -6 hashes it", async () => {
    const openssl = (salt, password) => execFileSync("openssl", ["passwd", "-6", "-salt", salt, password]).toString().trim();
    // Every length of a block and either side of one, SHA-512's own included.
    for (const password of ["secret", "şifre-ğüç", "x".repeat(70), "a", "y".repeat(111), "z".repeat(112), "w".repeat(250)]) {
        const salt = "saltsaltsaltsalt";
        assert.equal(await core.sha512Crypt(password, salt, 5000), openssl(salt, password), password);
    }
    assert.equal(await core.sha512Crypt("pi", "ab", 5000), openssl("ab", "pi"));
    // The page's own: many more rounds, said in the hash.
    assert.equal(await core.sha512Crypt("correct horse", "saltsaltsaltsalt"),
                 openssl(`rounds=${core.PASSWORD_ROUNDS}$saltsaltsaltsalt`, "correct horse"));
    assert.ok(core.PASSWORD_ROUNDS >= 100000);
    const salt = core.randomSalt();
    assert.match(salt, /^[./0-9A-Za-z]{16}$/);
    assert.match(await core.sha512Crypt("secret", core.randomSalt(), 1000), /^\$6\$rounds=1000\$[./0-9A-Za-z]{16}\$[./0-9A-Za-z]{86}$/);
    // What the system takes: 511 bytes at most.
    assert.equal(core.checkPassword("x".repeat(511)), "");
    assert.equal(core.checkPassword("x".repeat(512)), "A password has 511 bytes at most");
    assert.equal(core.checkPassword("ş".repeat(256)), "A password has 511 bytes at most");
});

test("an SSH key whole, as sshd takes it", () => {
    // An invented ed25519 key: its type, then 32 made-up bytes.
    const key = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4f den@laptop";
    assert.equal(core.checkKey(key), true);
    assert.equal(core.checkKey(key.split(" ").slice(0, 2).join(" ")), true, "without a comment");
    assert.equal(core.checkKey("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAABAgMEBQYHCAkKCwwNDg8Q den@laptop"), false, "cut short");
    assert.equal(core.checkKey(key.replace("ssh-ed25519 ", "ssh-rsa ")), false, "another type's blob");
    assert.equal(core.checkKey(key + "\u000bcommand=\"/bin/sh\" ssh-ed25519 AAAA"), false, "a line in its comment");
    assert.equal(core.checkKey("ssh-ed25519 AAAA den@laptop"), false);
    assert.equal(core.checkKey("ssh-ed25519 !!!! den"), false);
    assert.equal(core.checkKey("not a key"), false);
    assert.deepEqual(core.keyLines("ssh-ed25519 A\r\n\n# a comment\n  ssh-rsa B  "), ["ssh-ed25519 A", "ssh-rsa B"]);
});

test("the device's name, as System Information makes one", () => {
    assert.deepEqual(core.hostnameFor("  Den TV "), { name: "den-tv" });
    assert.deepEqual(core.hostnameFor("Attic_TV 2"), { name: "attic-tv-2" });
    assert.equal(core.hostnameFor("").why, "No name typed");
    assert.equal(core.hostnameFor("x".repeat(64)).why, "A name has 63 letters at most");
    assert.equal(core.hostnameFor("-den").why, "Letters, digits and hyphens only, and no hyphen first or last");
    assert.equal(core.hostnameFor("çay").why, "Letters, digits and hyphens only, and no hyphen first or last");
    assert.equal(core.hostnameFor("den.local").name, "");
});

test("Wi-Fi as NetworkManager takes it", () => {
    assert.equal(core.checkWifi("Home", "correct battery"), "");
    assert.equal(core.checkWifi("Home", ""), "");
    assert.equal(core.checkWifi("Home", "a".repeat(64).replace(/a/g, "f")), "");
    assert.equal(core.checkWifi("", "correct battery"), "No network name");
    assert.equal(core.checkWifi("ü".repeat(17), ""), "A network name has 32 bytes at most");
    assert.equal(core.checkWifi("Home", "short"), "A Wi-Fi password has 8 to 63 characters");
    assert.equal(core.checkWifi("Home", "şifreşifre"), "A Wi-Fi password is plain ASCII letters, digits and signs");
});

test("what a file leaves out stays as it is", () => {
    const into = { app: { theme: "Trinitron", hint_bar: "On" }, modules: { "com.osdos.arcade": { lan_play: "Off" } } };
    const merged = core.deepMerge(into, { app: { hint_bar: "Off" }, modules: { "com.osdos.arcade": { online_name: "den" } } });
    assert.deepEqual(merged, { app: { theme: "Trinitron", hint_bar: "Off" },
                               modules: { "com.osdos.arcade": { lan_play: "Off", online_name: "den" } } });
    assert.deepEqual(into.app.hint_bar, "On", "the original isn't changed");
    assert.deepEqual(core.deepMerge({ a: [1, 2] }, { a: [3] }), { a: [3] });
    // No key that is an object's own machinery.
    const polluted = {};
    core.setPath(polluted, ["modules", "__proto__", "enabled"], true);
    assert.equal({}.enabled, undefined);
    assert.deepEqual(polluted, {});
    assert.equal(core.getPath({}, ["constructor"]), undefined);
    const config = {};
    core.setPath(config, ["modules", "com.osdos.arcade", "lan_play"], "On");
    assert.equal(core.getPath(config, ["modules", "com.osdos.arcade", "lan_play"]), "On");
    assert.equal(core.getPath(config, ["modules", "com.osdos.plex", "server"]), undefined);
    core.setPath(config, ["app", "hint_bar"], "Off");
    core.deletePath(config, ["modules", "com.osdos.arcade", "lan_play"]);
    assert.deepEqual(config, { app: { hint_bar: "Off" } }, "the emptied objects go with it");
});

test("a row shows as the app shows it", () => {
    const values = { screen: "Arcade", online_play: "Off", lan_play: "On", menu_music: "Off" };
    const valueOf = key => values[key];
    assert.equal(core.shown({}, valueOf), true);
    assert.equal(core.shown({ shown_with: { key: "screen", value: "Arcade" } }, valueOf), true);
    assert.equal(core.shown({ shown_with: { key: "screen", value: "Original" } }, valueOf), false);
    assert.equal(core.shown({ shown_with: [{ key: "online_play", value: "On" }, { key: "lan_play", value: "On" }] }, valueOf), true);
    assert.equal(core.shown({ shown_with: [{ key: "online_play", value: "On" }] }, valueOf), false);
    assert.equal(core.shown({ shown_unless: { key: "menu_music", value: "Off" } }, valueOf), false);
    assert.equal(core.shown({ shown_unless: { key: "menu_music", value: "File" } }, valueOf), true);
});

test("the setup file made and read back", () => {
    const state = {
        deviceName: "den-tv", model: "pi5", displayPreset: "scart-rgb-pal",
        wifiCountry: "TR", wifiSsid: "Home \"5G\"", wifiPassword: "correct battery", wifiHidden: true,
        passwordHash: "$6$ab$x", sshEnabled: true, sshPasswordLogin: false,
        sshKeys: "ssh-ed25519 AAAA den@laptop\n\n# a comment\nssh-rsa BBBB attic",
        timezone: "Europe/Istanbul", keyboard: "tr",
        raUser: "denplayer", raPassword: "hunter22", raKey: "0123456789abcdef",
        config: { app: { theme: "Trinitron" }, modules: { "com.osdos.arcade": { lan_play: "On" } } },
    };
    const setup = core.buildSetup(state);
    assert.deepEqual(setup, {
        osdos_setup: 1,
        device: { name: "den-tv", model: "pi5" },
        display: { preset: "scart-rgb-pal" },
        wifi: { country: "TR", ssid: "Home \"5G\"", password: "correct battery", hidden: true },
        system: { password_hash: "$6$ab$x",
                  ssh: { enabled: true, password_login: false,
                         authorized_keys: ["ssh-ed25519 AAAA den@laptop", "ssh-rsa BBBB attic"] },
                  timezone: "Europe/Istanbul", keyboard: "tr" },
        retroachievements: { user: "denplayer", password: "hunter22", key: "0123456789abcdef" },
        config: { app: { theme: "Trinitron" }, modules: { "com.osdos.arcade": { lan_play: "On" } } },
    });
    const back = core.readSetup(JSON.stringify(setup));
    assert.equal(back.from, "setup");
    assert.equal(back.deviceName, "den-tv");
    assert.equal(back.displayPreset, "scart-rgb-pal");
    assert.equal(back.wifiSsid, "Home \"5G\"");
    assert.equal(back.wifiPassword, "correct battery");
    assert.equal(back.wifiHidden, true);
    assert.equal(back.passwordHash, "$6$ab$x");
    assert.equal(back.sshEnabled, true);
    assert.equal(back.sshPasswordLogin, false);
    assert.equal(back.sshKeys, "ssh-ed25519 AAAA den@laptop\nssh-rsa BBBB attic");
    assert.equal(back.keyboard, "tr");
    assert.equal(back.raUser, "denplayer");
    assert.equal(back.raPassword, "hunter22");
    assert.equal(back.raKey, "0123456789abcdef");
    assert.deepEqual(core.buildSetup(back), setup, "made again, the same file");
    // Nothing chosen: a file that changes nothing.
    assert.deepEqual(core.buildSetup({}), { osdos_setup: 1 });
    assert.deepEqual(core.buildSetup({ model: "pi4" }), { osdos_setup: 1 }, "the board only with an output");
});

test("a cabinet's file: its kind, and the Arcade it starts on turned on", () => {
    const setup = core.buildSetup({ kind: "arcade", config: { app: { startup_module: core.ARCADE_MODULE } } });
    assert.deepEqual(setup, { osdos_setup: 1, device: { kind: "arcade" },
                              config: { app: { startup_module: core.ARCADE_MODULE },
                                        modules: { [core.ARCADE_MODULE]: { enabled: true } } } });
    assert.equal(core.readSetup(JSON.stringify(setup)).kind, "arcade");
    assert.equal(core.readSetup(JSON.stringify({ osdos_setup: 1 })).kind, undefined);
    // A module turned on or off on purpose stays so.
    const off = { app: { startup_module: "com.osdos.plex" }, modules: { "com.osdos.plex": { enabled: false } } };
    assert.deepEqual(core.withStartupModule(off), off);
    assert.deepEqual(core.withStartupModule({ app: { startup_module: "None" } }), { app: { startup_module: "None" } });
    // SSH's keys with SSH on alone.
    assert.deepEqual(core.buildSetup({ sshEnabled: false, sshKeys: "ssh-ed25519 AAAA" }).system,
                     { ssh: { enabled: false, password_login: true } });
});

test("a file read back as the card would read it", () => {
    const read = core.readSetup(JSON.stringify({
        osdos_setup: 1,
        wifi: { ssid: "Home", hidden: "true" },
        system: { ssh: { enabled: "false", authorized_keys: "ssh-ed25519 AAAA" } },
    }));
    assert.equal(read.wifiHidden, false, "hidden as a true alone");
    assert.equal(read.sshEnabled, undefined, "SSH as a true or a false alone");
    const keys = core.readSetup(JSON.stringify({ osdos_setup: 1, system: { ssh: { enabled: true, authorized_keys: "x" } } }));
    assert.equal(keys.sshKeys, "", "keys as a list alone");
    for (const version of ["1", 0, 2])
        assert.throws(() => core.readSetup(JSON.stringify({ osdos_setup: version })), /a version this page doesn't know/);
});

test("a file of someone else's, as far as this page has it", () => {
    const schema = {
        presets: [{ id: "hdmi", models: ["pi3", "pi4", "pi5"] }, { id: "jamma-480i", models: ["pi4"] }],
        app: [{ title: "General", settings: [{ key: "osd_theme" }, { key: "hint_bar" }] }],
        modules: [{ id: "com.osdos.arcade", settings: [{ key: "lan_play" }, { type: "heading", label: "Look" }] }],
    };
    const { state, dropped } = core.fitImport({
        model: "pi5", displayPreset: "jamma-480i",
        config: { app: { osd_theme: "matrix", window: { x: 1 } },
                  modules: { "com.osdos.arcade": { enabled: true, lan_play: "On", secret: 1 }, "com.example.x": {} } },
    }, schema);
    assert.equal(state.displayPreset, undefined, "an output that Pi hasn't");
    assert.deepEqual(state.config, { app: { osd_theme: "matrix" },
                                     modules: { "com.osdos.arcade": { enabled: true, lan_play: "On" } } });
    assert.deepEqual(dropped, ["display.preset", "app.window", "modules.com.osdos.arcade.secret", "modules.com.example.x"]);
    assert.equal(core.fitImport({ model: "pi4", displayPreset: "jamma-480i", config: {} }, schema).state.displayPreset,
                 "jamma-480i");
    assert.deepEqual(core.fitImport({ displayPreset: "../../x/y?", config: {} }, schema).dropped, ["display.preset"]);
});

test("OSD/OS's config.json read for its settings, anything else refused", () => {
    const config = { app: { theme: "Arcade" }, modules: { "com.osdos.plex": { enabled: true } }, window: { x: 1 } };
    assert.deepEqual(core.readSetup(JSON.stringify(config)),
                     { from: "config", config: { app: { theme: "Arcade" }, modules: { "com.osdos.plex": { enabled: true } } } });
    assert.throws(() => core.readSetup("{ not json"), /Not a JSON file/);
    assert.throws(() => core.readSetup("[1, 2]"), /Not a settings file/);
    assert.throws(() => core.readSetup("{\"a\": 1}"), /Neither an OSD\/OS Setup file nor/);
    assert.throws(() => core.readSetup("{\"osdos_setup\": 2}"), /a version this page doesn't know/);
});

test("a zip Python's zipfile opens", () => {
    const files = [{ name: "osdos-setup.json", data: "{\"osdos_setup\": 1}\n" },
                   { name: "osdos-display.txt", data: new TextEncoder().encode("display_auto_detect=1\n") },
                   { name: "çay.txt", data: "" }];
    const bytes = core.zip(files, new Date(2026, 9, 21, 12, 30, 10));
    const dir = mkdtempSync(join(tmpdir(), "osdos-site-"));
    try {
        const path = join(dir, "setup.zip");
        writeFileSync(path, bytes);
        const listing = execFileSync("python3", ["-I", "-c", `
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    assert z.testzip() is None
    for info in z.infolist():
        print(info.filename, info.file_size, info.date_time, z.read(info).decode())
`, path]).toString();
        assert.equal(listing,
                     "osdos-setup.json 19 (2026, 10, 21, 12, 30, 10) {\"osdos_setup\": 1}\n\n" +
                     "osdos-display.txt 22 (2026, 10, 21, 12, 30, 10) display_auto_detect=1\n\n" +
                     "çay.txt 0 (2026, 10, 21, 12, 30, 10) \n");
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
    assert.equal(core.crc32(new TextEncoder().encode("123456789")), 0xcbf43926);
});
