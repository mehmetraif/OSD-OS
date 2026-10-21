// OSD/OS Setup's workings, apart from the page: the file the card takes
// (osdos-setup.json) made from what the form holds, and a file read back into
// it (one of its own, or OSD/OS's config.json); the device's name, the Wi-Fi,
// SSH keys and pi's password as the card checks them; pi's password as
// /etc/shadow keeps it (SHA-512 crypt); and a zip of the files. The page
// (page.mjs) and the tests (tests/site) import it. Nothing here leaves the
// browser.

export const SETUP_VERSION = 1;
export const SETUP_FILE = "osdos-setup.json";
export const ARCADE_MODULE = "com.osdos.arcade";

// --- The device's name, as Settings → About → System Information makes one
// (SystemInfo::hostnameFor): small letters, spaces and underscores as
// hyphens, then RFC 1123's letters, digits and hyphens, 63 at most, no hyphen
// at either end.
export function hostnameFor(typed) {
    const name = String(typed).trim().toLowerCase().replace(/[\s_]+/g, "-");
    if (/^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$/.test(name))
        return { name };
    return {
        name: "",
        why: name === "" ? "No name typed"
           : name.length > 63 ? "A name has 63 letters at most"
           : "Letters, digits and hyphens only, and no hyphen first or last",
    };
}

// --- Wi-Fi as NetworkManager takes it: a name of 1 to 32 bytes; a WPA
// password of 8 to 63 characters (or 64 hexadecimal digits, a key already
// worked out), or none for an open network.
export function checkWifi(ssid, password) {
    const bytes = new TextEncoder().encode(ssid).length;
    if (bytes === 0)
        return "No network name";
    if (bytes > 32)
        return "A network name has 32 bytes at most";
    if (password === "")
        return "";
    if (/^[0-9a-fA-F]{64}$/.test(password))
        return "";
    if (password.length < 8 || password.length > 63)
        return "A Wi-Fi password has 8 to 63 characters";
    if (/[^\x20-\x7e]/.test(password))
        return "A Wi-Fi password is plain ASCII letters, digits and signs";
    return "";
}

// --- pi's password: what the system takes (libxcrypt refuses one of 512
// bytes or more, and the account could never be logged into).
export function checkPassword(password) {
    return new TextEncoder().encode(password).length > 511 ? "A password has 511 bytes at most" : "";
}

// --- An SSH public key as sshd takes it, and as the card checks it
// (osdos-setup's public_key()): its type, a blob of that type whole, and a
// comment of printable ASCII alone, so that nothing in it is a line of its
// own to sshd.
const KEY_PARTS = { "ssh-rsa": 3, "ssh-dss": 5, "ssh-ed25519": 2,
                    "sk-ssh-ed25519@openssh.com": 3, "sk-ecdsa-sha2-nistp256@openssh.com": 4 };

export function checkKey(key) {
    const match = /^(ssh-(?:ed25519|rsa|dss)|ecdsa-sha2-nistp\d+|sk-(?:ssh-ed25519|ecdsa-sha2-nistp256)@openssh\.com) ([A-Za-z0-9+/=]+)( [\x20-\x7e]*)?$/
        .exec(key);
    if (!match || key.length > 16384 || match[2].length % 4 !== 0)
        return false;
    let data;
    try {
        data = Uint8Array.from(atob(match[2]), c => c.charCodeAt(0));
    } catch {
        return false;
    }
    const parts = [];
    for (let at = 0; at < data.length;) {
        const size = at + 4 <= data.length
            ? ((data[at] << 24) | (data[at + 1] << 16) | (data[at + 2] << 8) | data[at + 3]) >>> 0 : Infinity;
        if (at + 4 + size > data.length)
            return false;
        parts.push(data.subarray(at + 4, at + 4 + size));
        at += 4 + size;
    }
    const kind = match[1];
    const wanted = KEY_PARTS[kind] ?? (kind.startsWith("ecdsa-sha2-") ? 3 : 0);
    return parts.length === wanted && new TextDecoder().decode(parts[0]) === kind
        && (kind !== "ssh-ed25519" || parts[1].length === 32);
}

// The keys typed, a line each, comments and empty lines left out.
export function keyLines(text) {
    return String(text || "").split(/\r?\n/).map(k => k.trim()).filter(k => k && !k.startsWith("#"));
}

// --- pi's password: SHA-512 crypt ($6$), as chpasswd -e and /etc/shadow
// take it (Ulrich Drepper's "Unix crypt using SHA-256 and SHA-512"), with
// many more rounds than the scheme's 5000, so that a hash that gets out is
// that much slower to guess at.
export const PASSWORD_ROUNDS = 100000;
const ITOA64 = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

// SHA-512 itself (FIPS 180-4), on 32-bit halves: the crypt asks for a great
// many small digests, each far quicker here than crypto.subtle's, which
// answers every one asynchronously. The constants: the first 64 bits of the
// fractional parts of the first 80 primes' cube roots, and of the first
// eight's square roots.
const K = new Uint32Array([
    0x428a2f98, 0xd728ae22, 0x71374491, 0x23ef65cd, 0xb5c0fbcf, 0xec4d3b2f, 0xe9b5dba5, 0x8189dbbc,
    0x3956c25b, 0xf348b538, 0x59f111f1, 0xb605d019, 0x923f82a4, 0xaf194f9b, 0xab1c5ed5, 0xda6d8118,
    0xd807aa98, 0xa3030242, 0x12835b01, 0x45706fbe, 0x243185be, 0x4ee4b28c, 0x550c7dc3, 0xd5ffb4e2,
    0x72be5d74, 0xf27b896f, 0x80deb1fe, 0x3b1696b1, 0x9bdc06a7, 0x25c71235, 0xc19bf174, 0xcf692694,
    0xe49b69c1, 0x9ef14ad2, 0xefbe4786, 0x384f25e3, 0x0fc19dc6, 0x8b8cd5b5, 0x240ca1cc, 0x77ac9c65,
    0x2de92c6f, 0x592b0275, 0x4a7484aa, 0x6ea6e483, 0x5cb0a9dc, 0xbd41fbd4, 0x76f988da, 0x831153b5,
    0x983e5152, 0xee66dfab, 0xa831c66d, 0x2db43210, 0xb00327c8, 0x98fb213f, 0xbf597fc7, 0xbeef0ee4,
    0xc6e00bf3, 0x3da88fc2, 0xd5a79147, 0x930aa725, 0x06ca6351, 0xe003826f, 0x14292967, 0x0a0e6e70,
    0x27b70a85, 0x46d22ffc, 0x2e1b2138, 0x5c26c926, 0x4d2c6dfc, 0x5ac42aed, 0x53380d13, 0x9d95b3df,
    0x650a7354, 0x8baf63de, 0x766a0abb, 0x3c77b2a8, 0x81c2c92e, 0x47edaee6, 0x92722c85, 0x1482353b,
    0xa2bfe8a1, 0x4cf10364, 0xa81a664b, 0xbc423001, 0xc24b8b70, 0xd0f89791, 0xc76c51a3, 0x0654be30,
    0xd192e819, 0xd6ef5218, 0xd6990624, 0x5565a910, 0xf40e3585, 0x5771202a, 0x106aa070, 0x32bbd1b8,
    0x19a4c116, 0xb8d2d0c8, 0x1e376c08, 0x5141ab53, 0x2748774c, 0xdf8eeb99, 0x34b0bcb5, 0xe19b48a8,
    0x391c0cb3, 0xc5c95a63, 0x4ed8aa4a, 0xe3418acb, 0x5b9cca4f, 0x7763e373, 0x682e6ff3, 0xd6b2b8a3,
    0x748f82ee, 0x5defb2fc, 0x78a5636f, 0x43172f60, 0x84c87814, 0xa1f0ab72, 0x8cc70208, 0x1a6439ec,
    0x90befffa, 0x23631e28, 0xa4506ceb, 0xde82bde9, 0xbef9a3f7, 0xb2c67915, 0xc67178f2, 0xe372532b,
    0xca273ece, 0xea26619c, 0xd186b8c7, 0x21c0c207, 0xeada7dd6, 0xcde0eb1e, 0xf57d4f7f, 0xee6ed178,
    0x06f067aa, 0x72176fba, 0x0a637dc5, 0xa2c898a6, 0x113f9804, 0xbef90dae, 0x1b710b35, 0x131c471b,
    0x28db77f5, 0x23047d84, 0x32caab7b, 0x40c72493, 0x3c9ebe0a, 0x15c9bebc, 0x431d67c4, 0x9c100d4c,
    0x4cc5d4be, 0xcb3e42b6, 0x597f299c, 0xfc657e2a, 0x5fcb6fab, 0x3ad6faec, 0x6c44198c, 0x4a475817,
]);
const H0 = new Uint32Array([
    0x6a09e667, 0xf3bcc908, 0xbb67ae85, 0x84caa73b, 0x3c6ef372, 0xfe94f82b, 0xa54ff53a, 0x5f1d36f1,
    0x510e527f, 0xade682d1, 0x9b05688c, 0x2b3e6c1f, 0x1f83d9ab, 0xfb41bd6b, 0x5be0cd19, 0x137e2179,
]);
const W = new Uint32Array(160);

function sha512(...parts) {
    let size = 0;
    for (const part of parts)
        size += part.length;
    // The message, a 1 bit, zeros, and its length in bits: whole blocks.
    const blocks = Math.ceil((size + 17) / 128);
    const m = new Uint8Array(blocks * 128);
    let at = 0;
    for (const part of parts) {
        m.set(part, at);
        at += part.length;
    }
    m[size] = 0x80;
    const view = new DataView(m.buffer);
    view.setUint32(m.length - 8, Math.floor(size / 0x20000000));
    view.setUint32(m.length - 4, (size * 8) >>> 0);
    const h = H0.slice();
    for (let block = 0; block < blocks; block++) {
        for (let i = 0; i < 32; i++)
            W[i] = view.getUint32(block * 128 + i * 4);
        for (let i = 32; i < 160; i += 2) {
            // σ0 of the word 15 back (ROTR 1, ROTR 8, SHR 7); σ1 of the
            // one 2 back (ROTR 19, ROTR 61, SHR 6).
            let xh = W[i - 30], xl = W[i - 29];
            const s0h = ((xh >>> 1) | (xl << 31)) ^ ((xh >>> 8) | (xl << 24)) ^ (xh >>> 7);
            const s0l = ((xl >>> 1) | (xh << 31)) ^ ((xl >>> 8) | (xh << 24)) ^ ((xl >>> 7) | (xh << 25));
            xh = W[i - 4];
            xl = W[i - 3];
            const s1h = ((xh >>> 19) | (xl << 13)) ^ ((xl >>> 29) | (xh << 3)) ^ (xh >>> 6);
            const s1l = ((xl >>> 19) | (xh << 13)) ^ ((xh >>> 29) | (xl << 3)) ^ ((xl >>> 6) | (xh << 26));
            const lo = (s0l >>> 0) + (s1l >>> 0) + W[i - 13] + W[i - 31];
            W[i + 1] = lo;
            W[i] = (s0h >>> 0) + (s1h >>> 0) + W[i - 14] + W[i - 32] + Math.floor(lo / 0x100000000);
        }
        let ah = h[0], al = h[1], bh = h[2], bl = h[3], ch = h[4], cl = h[5], dh = h[6], dl = h[7];
        let eh = h[8], el = h[9], fh = h[10], fl = h[11], gh = h[12], gl = h[13], hh = h[14], hl = h[15];
        for (let i = 0; i < 160; i += 2) {
            // Σ1 of e (ROTR 14, 18, 41), Ch(e, f, g), Σ0 of a (ROTR 28, 34,
            // 39), Maj(a, b, c).
            const S1h = ((eh >>> 14) | (el << 18)) ^ ((eh >>> 18) | (el << 14)) ^ ((el >>> 9) | (eh << 23));
            const S1l = ((el >>> 14) | (eh << 18)) ^ ((el >>> 18) | (eh << 14)) ^ ((eh >>> 9) | (el << 23));
            const chh = (eh & fh) ^ (~eh & gh);
            const chl = (el & fl) ^ (~el & gl);
            const t1l = hl + (S1l >>> 0) + (chl >>> 0) + K[i + 1] + W[i + 1];
            const t1h = hh + (S1h >>> 0) + (chh >>> 0) + K[i] + W[i] + Math.floor(t1l / 0x100000000);
            const S0h = ((ah >>> 28) | (al << 4)) ^ ((al >>> 2) | (ah << 30)) ^ ((al >>> 7) | (ah << 25));
            const S0l = ((al >>> 28) | (ah << 4)) ^ ((ah >>> 2) | (al << 30)) ^ ((ah >>> 7) | (al << 25));
            const mjh = (ah & bh) ^ (ah & ch) ^ (bh & ch);
            const mjl = (al & bl) ^ (al & cl) ^ (bl & cl);
            const t2l = (S0l >>> 0) + (mjl >>> 0);
            const t2h = (S0h >>> 0) + (mjh >>> 0) + Math.floor(t2l / 0x100000000);
            hh = gh; hl = gl;
            gh = fh; gl = fl;
            fh = eh; fl = el;
            const el2 = dl + (t1l >>> 0);
            eh = (dh + t1h + Math.floor(el2 / 0x100000000)) >>> 0;
            el = el2 >>> 0;
            dh = ch; dl = cl;
            ch = bh; cl = bl;
            bh = ah; bl = al;
            const al2 = (t1l >>> 0) + (t2l >>> 0);
            ah = (t1h + t2h + Math.floor(al2 / 0x100000000)) >>> 0;
            al = al2 >>> 0;
        }
        const add = [ah, al, bh, bl, ch, cl, dh, dl, eh, el, fh, fl, gh, gl, hh, hl];
        for (let i = 0; i < 16; i += 2) {
            const lo = h[i + 1] + add[i + 1];
            h[i + 1] = lo;
            h[i] = h[i] + add[i] + Math.floor(lo / 0x100000000);
        }
    }
    const out = new Uint8Array(64);
    const outView = new DataView(out.buffer);
    for (let i = 0; i < 16; i++)
        outView.setUint32(i * 4, h[i]);
    return out;
}

// The bytes of digest, repeated to length.
function stretch(digest, length) {
    const out = new Uint8Array(length);
    for (let i = 0; i < length; i++)
        out[i] = digest[i % digest.length];
    return out;
}

export function randomSalt(length = 16) {
    const bytes = crypto.getRandomValues(new Uint8Array(length));
    return Array.from(bytes, b => ITOA64[b % 64]).join("");
}

export async function sha512Crypt(password, salt = randomSalt(), rounds = PASSWORD_ROUNDS) {
    const p = new TextEncoder().encode(password);
    const s = new TextEncoder().encode(salt.slice(0, 16));
    const b = sha512(p, s, p);
    const parts = [p, s, stretch(b, p.length)];
    for (let i = p.length; i > 0; i >>= 1)
        parts.push(i & 1 ? b : p);
    let c = sha512(...parts);
    const dpParts = [];
    for (let i = 0; i < p.length; i++)
        dpParts.push(p);
    const pSeq = stretch(sha512(...dpParts), p.length);
    const dsParts = [];
    for (let i = 0; i < 16 + c[0]; i++)
        dsParts.push(s);
    const sSeq = stretch(sha512(...dsParts), s.length);
    for (let i = 0; i < rounds; i++) {
        const round = [];
        round.push(i & 1 ? pSeq : c);
        if (i % 3)
            round.push(sSeq);
        if (i % 7)
            round.push(pSeq);
        round.push(i & 1 ? c : pSeq);
        c = sha512(...round);
    }
    const order = [[0, 21, 42], [22, 43, 1], [44, 2, 23], [3, 24, 45], [25, 46, 4], [47, 5, 26], [6, 27, 48],
                   [28, 49, 7], [50, 8, 29], [9, 30, 51], [31, 52, 10], [53, 11, 32], [12, 33, 54], [34, 55, 13],
                   [56, 14, 35], [15, 36, 57], [37, 58, 16], [59, 17, 38], [18, 39, 60], [40, 61, 19], [62, 20, 41]];
    let out = "";
    const put = (w, n) => {
        for (let i = 0; i < n; i++, w >>= 6)
            out += ITOA64[w & 63];
    };
    for (const [x, y, z] of order)
        put((c[x] << 16) | (c[y] << 8) | c[z], 4);
    put(c[63], 2);
    const roundsPart = rounds === 5000 ? "" : `rounds=${rounds}$`;
    return `$6$${roundsPart}${salt.slice(0, 16)}$${out}`;
}

// --- Merging: objects key by key, anything else replaced. What the file
// leaves out stays as it is.
export function isPlainObject(value) {
    return value !== null && typeof value === "object" && !Array.isArray(value);
}

export function deepMerge(into, from) {
    const out = isPlainObject(into) ? { ...into } : {};
    for (const [key, value] of Object.entries(from || {})) {
        out[key] = isPlainObject(value) && isPlainObject(out[key]) ? deepMerge(out[key], value)
                 : isPlainObject(value) ? deepMerge({}, value)
                 : value;
    }
    return out;
}

// A value at a path of keys (a module's id has dots in it, so a path is a
// list, not a dotted string). No key that is an object's own machinery.
const UNSAFE = new Set(["__proto__", "constructor", "prototype"]);

export function getPath(object, path) {
    let at = object;
    for (const key of path) {
        if (!isPlainObject(at) || UNSAFE.has(key) || !Object.hasOwn(at, key))
            return undefined;
        at = at[key];
    }
    return at;
}

export function setPath(object, path, value) {
    if (path.some(key => UNSAFE.has(key)))
        return object;
    let at = object;
    for (const key of path.slice(0, -1)) {
        if (!isPlainObject(at[key]))
            at[key] = {};
        at = at[key];
    }
    at[path[path.length - 1]] = value;
    return object;
}

// The value at path taken out, and the objects it leaves empty with it.
export function deletePath(object, path) {
    if (!path.length || !isPlainObject(object) || UNSAFE.has(path[0]))
        return object;
    const [key, ...rest] = path;
    if (rest.length === 0) {
        delete object[key];
    } else if (isPlainObject(object[key])) {
        deletePath(object[key], rest);
        if (Object.keys(object[key]).length === 0)
            delete object[key];
    }
    return object;
}

// Whether a row shows, as the app has it: shown_with (one condition or a
// list, any of which is enough) holding, and shown_unless not, each against
// valueOf(key), the value the row's neighbour has now.
export function shown(row, valueOf) {
    const holds = c => String(valueOf(c.key)) === String(c.value);
    if (row.shown_with) {
        const any = Array.isArray(row.shown_with) ? row.shown_with : [row.shown_with];
        if (!any.some(holds))
            return false;
    }
    return !(row.shown_unless && holds(row.shown_unless));
}

// The module OSD/OS starts on, turned on with it unless turned off on purpose
// (which the page says it can't be).
export function withStartupModule(config) {
    const id = getPath(config, ["app", "startup_module"]);
    if (typeof id !== "string" || id === "" || id === "None" || getPath(config, ["modules", id, "enabled"]) !== undefined)
        return config;
    return setPath(deepMerge({}, config), ["modules", id, "enabled"], true);
}

// --- The file the card takes. Every part is optional: what isn't there is
// left as the card has it.
//   { osdos_setup: 1,
//     device:  { name, model, kind },   kind "arcade" for a cabinet, the
//                                       page's own; model with a display output
//     display: { preset },
//     wifi:    { country, ssid, password, hidden },
//     system:  { password_hash, ssh: { enabled, password_login, authorized_keys: [] },
//                timezone, keyboard },
//     retroachievements: { user, password, key },   the Arcade's account
//     config:  { app: {...}, modules: { "<id>": {...} } } }   merged into config.json
export function buildSetup(state) {
    const setup = { osdos_setup: SETUP_VERSION };
    const device = {};
    if (state.deviceName)
        device.name = state.deviceName;
    // The board only with an output, which the page offers by board.
    if (state.displayPreset && state.model)
        device.model = state.model;
    if (state.kind === "arcade")
        device.kind = "arcade";
    if (Object.keys(device).length)
        setup.device = device;
    if (state.displayPreset)
        setup.display = { preset: state.displayPreset };
    const wifi = {};
    if (state.wifiCountry)
        wifi.country = state.wifiCountry;
    if (state.wifiSsid) {
        wifi.ssid = state.wifiSsid;
        wifi.password = state.wifiPassword || "";
        wifi.hidden = !!state.wifiHidden;
    }
    if (Object.keys(wifi).length)
        setup.wifi = wifi;
    const system = {};
    if (state.passwordHash)
        system.password_hash = state.passwordHash;
    if (state.sshEnabled === true || state.sshEnabled === false) {
        system.ssh = { enabled: state.sshEnabled, password_login: state.sshPasswordLogin !== false };
        // The keys with SSH on alone.
        const keys = state.sshEnabled ? keyLines(state.sshKeys) : [];
        if (keys.length)
            system.ssh.authorized_keys = keys;
    }
    for (const key of ["timezone", "keyboard"]) {
        if (state[key])
            system[key] = state[key];
    }
    if (Object.keys(system).length)
        setup.system = system;
    const ra = {};
    for (const [key, field] of [["user", "raUser"], ["password", "raPassword"], ["key", "raKey"]]) {
        if (state[field])
            ra[key] = state[field];
    }
    if (Object.keys(ra).length)
        setup.retroachievements = ra;
    const config = withStartupModule(state.config || {});
    if (Object.keys(config).length)
        setup.config = config;
    return setup;
}

// A file read back: one of the page's own (from: "setup"), or OSD/OS's
// config.json, its settings alone (from: "config"). Throws with why the file
// can't be read.
export function readSetup(text) {
    let json;
    try {
        json = JSON.parse(text);
    } catch (e) {
        throw new Error("Not a JSON file: " + e.message);
    }
    if (!isPlainObject(json))
        throw new Error("Not a settings file");
    if ("osdos_setup" in json) {
        if (json.osdos_setup !== SETUP_VERSION)
            throw new Error("A settings file of a version this page doesn't know");
        const state = {};
        const device = isPlainObject(json.device) ? json.device : {};
        if (typeof device.name === "string") state.deviceName = device.name;
        if (typeof device.model === "string") state.model = device.model;
        if (device.kind === "arcade") state.kind = "arcade";
        if (isPlainObject(json.display) && typeof json.display.preset === "string")
            state.displayPreset = json.display.preset;
        const wifi = isPlainObject(json.wifi) ? json.wifi : {};
        if (typeof wifi.country === "string") state.wifiCountry = wifi.country;
        if (typeof wifi.ssid === "string") {
            state.wifiSsid = wifi.ssid;
            state.wifiPassword = typeof wifi.password === "string" ? wifi.password : "";
            state.wifiHidden = wifi.hidden === true;
        }
        const system = isPlainObject(json.system) ? json.system : {};
        if (typeof system.password_hash === "string") state.passwordHash = system.password_hash;
        // As the card takes it: on or off as a true or a false, nothing else.
        if (isPlainObject(system.ssh) && typeof system.ssh.enabled === "boolean") {
            state.sshEnabled = system.ssh.enabled;
            state.sshPasswordLogin = system.ssh.password_login !== false;
            const keys = Array.isArray(system.ssh.authorized_keys) ? system.ssh.authorized_keys : [];
            state.sshKeys = keys.filter(k => typeof k === "string").join("\n");
        }
        for (const key of ["timezone", "keyboard"]) {
            if (typeof system[key] === "string") state[key] = system[key];
        }
        const ra = isPlainObject(json.retroachievements) ? json.retroachievements : {};
        for (const [key, field] of [["user", "raUser"], ["password", "raPassword"], ["key", "raKey"]]) {
            if (typeof ra[key] === "string") state[field] = ra[key];
        }
        state.config = isPlainObject(json.config) ? json.config : {};
        state.from = "setup";
        return state;
    }
    if ("app" in json || "modules" in json) {
        const config = {};
        if (isPlainObject(json.app)) config.app = json.app;
        if (isPlainObject(json.modules)) config.modules = json.modules;
        return { from: "config", config };
    }
    throw new Error("Neither an OSD/OS Setup file nor OSD/OS's config.json");
}

// What of a file read back this page has: a board it knows, an output that
// board has, and the settings on its form. The rest left out, named.
export function fitImport(read, schema) {
    const state = { ...read };
    const dropped = [];
    if ("model" in state && !["pi3", "pi4", "pi5"].includes(state.model)) {
        dropped.push("device.model");
        delete state.model;
    }
    if (state.displayPreset) {
        const preset = schema.presets.find(p => p.id === state.displayPreset);
        if (!preset || !preset.models.includes(state.model || "pi4")) {
            dropped.push("display.preset");
            delete state.displayPreset;
        }
    }
    const config = {};
    const source = isPlainObject(state.config) ? state.config : {};
    const appKeys = new Set(schema.app.flatMap(group => group.settings.map(row => row.key)).filter(Boolean));
    for (const [key, value] of Object.entries(isPlainObject(source.app) ? source.app : {})) {
        if (appKeys.has(key))
            setPath(config, ["app", key], value);
        else
            dropped.push(`app.${key}`);
    }
    for (const [id, settings] of Object.entries(isPlainObject(source.modules) ? source.modules : {})) {
        const module = schema.modules.find(m => m.id === id);
        if (!module || !isPlainObject(settings)) {
            dropped.push(`modules.${id}`);
            continue;
        }
        const keys = new Set(["enabled", ...module.settings.map(row => row.key).filter(Boolean)]);
        for (const [key, value] of Object.entries(settings)) {
            if (keys.has(key))
                setPath(config, ["modules", id, key], value);
            else
                dropped.push(`modules.${id}.${key}`);
        }
    }
    state.config = config;
    return { state, dropped };
}

// --- A zip of files, stored (not compressed): [{ name, data }] with data a
// string or bytes.
const CRC_TABLE = (() => {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        table[n] = c >>> 0;
    }
    return table;
})();

export function crc32(bytes) {
    let c = 0xffffffff;
    for (const b of bytes)
        c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

export function zip(files, when = new Date()) {
    const encoder = new TextEncoder();
    const dosTime = (when.getHours() << 11) | (when.getMinutes() << 5) | (when.getSeconds() >> 1);
    const dosDate = ((when.getFullYear() - 1980) << 9) | ((when.getMonth() + 1) << 5) | when.getDate();
    const locals = [];
    const centrals = [];
    let offset = 0;
    for (const file of files) {
        const name = encoder.encode(file.name);
        const data = typeof file.data === "string" ? encoder.encode(file.data) : file.data;
        const crc = crc32(data);
        const local = new DataView(new ArrayBuffer(30));
        local.setUint32(0, 0x04034b50, true);
        local.setUint16(4, 20, true);
        local.setUint16(6, 0x0800, true);            // names in UTF-8
        local.setUint16(8, 0, true);                 // stored
        local.setUint16(10, dosTime, true);
        local.setUint16(12, dosDate, true);
        local.setUint32(14, crc, true);
        local.setUint32(18, data.length, true);
        local.setUint32(22, data.length, true);
        local.setUint16(26, name.length, true);
        local.setUint16(28, 0, true);
        locals.push(new Uint8Array(local.buffer), name, data);
        const central = new DataView(new ArrayBuffer(46));
        central.setUint32(0, 0x02014b50, true);
        central.setUint16(4, 20, true);
        central.setUint16(6, 20, true);
        central.setUint16(8, 0x0800, true);
        central.setUint16(10, 0, true);
        central.setUint16(12, dosTime, true);
        central.setUint16(14, dosDate, true);
        central.setUint32(16, crc, true);
        central.setUint32(20, data.length, true);
        central.setUint32(24, data.length, true);
        central.setUint16(28, name.length, true);
        central.setUint32(42, offset, true);
        centrals.push(new Uint8Array(central.buffer), name);
        offset += 30 + name.length + data.length;
    }
    const centralSize = centrals.reduce((n, part) => n + part.length, 0);
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true);
    end.setUint16(8, files.length, true);
    end.setUint16(10, files.length, true);
    end.setUint32(12, centralSize, true);
    end.setUint32(16, offset, true);
    const parts = [...locals, ...centrals, new Uint8Array(end.buffer)];
    const out = new Uint8Array(parts.reduce((n, part) => n + part.length, 0));
    let at = 0;
    for (const part of parts) {
        out.set(part, at);
        at += part.length;
    }
    return out;
}
