// The website's terminal downloads, site/get.sh and site/get.ps1, run against
// a stand-in for GitHub: a local server answering with a release's record and
// its files. get.sh runs as each system it knows (uname stood in for), under
// each POSIX shell here, with each awk, and with each tool it may download
// and check with, FreeBSD's and OpenBSD's stood in for; get.ps1 runs where
// PowerShell is installed. Neither reaches anything but 127.0.0.1.
//   node --test tests/site/*.test.mjs
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { createHash, randomBytes } from "node:crypto";
import { createServer } from "node:http";
import { accessSync, constants, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, realpathSync, rmSync,
         statSync, symlinkSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { delimiter, join } from "node:path";
import { fileURLToPath } from "node:url";
import { zip } from "../../site/setup/js/core.mjs";

const root = fileURLToPath(new URL("../..", import.meta.url));
const GET_SH = join(root, "site/get.sh");
const GET_PS1 = join(root, "site/get.ps1");
const work = mkdtempSync(join(tmpdir(), "osdos-get-"));
after(() => rmSync(work, { recursive: true, force: true }));
// Home for the programs run, apart from the folders they download into.
const home = join(work, "home");
mkdirSync(home);

// A program on PATH, or null.
function which(name) {
    for (const dir of (process.env.PATH || "").split(delimiter)) {
        const file = join(dir, name);
        try {
            accessSync(file, constants.X_OK);
            if (statSync(file).isFile())
                return file;
        } catch {
            // Not in this folder.
        }
    }
    return null;
}

function run(file, args, options) {
    return new Promise(resolve => {
        execFile(file, args, { timeout: 120000, ...options }, (error, stdout, stderr) =>
            resolve({ code: error ? (typeof error.code === "number" ? error.code : -1) : 0, stdout, stderr }));
    });
}

// The release: each file some bytes of its own, the zip the one folder the
// Windows build unpacks to.
const TAG = "v9.8.7";
const sha256 = data => createHash("sha256").update(data).digest("hex");
const files = {
    "OSD-OS-v9.8.7-raspberry-pi.img.xz": randomBytes(3000),
    "OSD-OS-v9.8.7-macOS-arm64.dmg": randomBytes(2000),
    "OSD-OS-linux-x86_64.AppImage": randomBytes(2000),
    "install.sh": Buffer.from("#!/usr/bin/env bash\necho OSD/OS installer\n"),
    "OSD-OS-v9.8.7-linux-arm64.tar.gz": randomBytes(1000),
    "OSD-OS-v9.8.7-freebsd-amd64.pkg": randomBytes(1000),
    "OSD-OS-9.8.7-openbsd-amd64.tgz": randomBytes(1000),
    "OSD-OS-v9.8.7-windows-x64.zip": Buffer.from(zip([{ name: "osdos/osdos.exe", data: "MZ, a stand-in" },
                                                      { name: "osdos/modules/README.txt", data: "OSD/OS" }])),
};

// GitHub's record of it, the way its API lays one out, in a variant. Its
// notes, ahead of the files here, and each file's uploader, after its name
// as GitHub has it, mention names, digests and links of their own, which
// must never count.
function record(base, variant) {
    const drop = { "no-bsd": ["OSD-OS-v9.8.7-freebsd-amd64.pkg", "OSD-OS-9.8.7-openbsd-amd64.tgz"],
                   "no-windows": ["OSD-OS-v9.8.7-windows-x64.zip"] }[variant] ?? [];
    return {
        url: `${base}/repos/mehmetraif/OSD-OS/releases/1`,
        html_url: `${base}/releases/tag/${TAG}`,
        author: { login: "github-actions[bot]", id: 41898282, type: "Bot" },
        tag_name: TAG,
        name: `OSD/OS ${TAG} "quoted" {braced}`,
        draft: false,
        body: `Downloads: "name": "OSD-OS-v0-macOS-arm64.dmg", "digest": "sha256:${"0".repeat(64)}", `
              + `"browser_download_url": "${base}/elsewhere" {[,]} \\ "assets": [] and é.`,
        assets: Object.entries(files).filter(([name]) => !drop.includes(name)).map(([name, data], i) => ({
            url: `${base}/repos/mehmetraif/OSD-OS/releases/assets/${i}`,
            id: i,
            name,
            label: "",
            uploader: { login: "github-actions[bot]", id: 41898282, name: "decoy-macOS-arm64.dmg",
                        url: `${base}/elsewhere`, browser_download_url: `${base}/elsewhere` },
            content_type: "application/octet-stream",
            state: "uploaded",
            size: data.length,
            digest: variant === "no-digest" ? null
                  : variant === "bad-digest" ? `sha256:${"1".repeat(64)}`
                  : `sha256:${sha256(data)}`,
            download_count: 0,
            browser_download_url: `${base}/download/${TAG}/${name}`,
        })),
    };
}

let base;
const hits = {};
const server = createServer((request, response) => {
    const path = decodeURIComponent(new URL(request.url, "http://x").pathname);
    let m;
    if ((m = path.match(/^\/release\/(pretty|compact|broken|bad-digest|no-digest|no-bsd|no-windows)$/))) {
        const r = record(base, m[1]);
        response.setHeader("Content-Type", "application/json; charset=utf-8");
        return response.end(m[1] === "compact" ? JSON.stringify(r) : m[1] === "broken" ? JSON.stringify(r).slice(0, 900)
                            : JSON.stringify(r, null, 2));
    }
    if ((m = path.match(/^\/download\/v9\.8\.7\/(.+)$/)) && files[m[1]]) {
        hits[m[1]] = (hits[m[1]] || 0) + 1;
        response.setHeader("Content-Type", "application/octet-stream");
        return response.end(files[m[1]]);
    }
    if (path === "/get.sh") {
        response.setHeader("Content-Type", "application/x-sh");
        return response.end(readFileSync(GET_SH));
    }
    // As GitHub Pages serves it: a type it has no name for.
    if (path === "/get.ps1") {
        response.setHeader("Content-Type", "application/octet-stream");
        return response.end(readFileSync(GET_PS1));
    }
    response.statusCode = 404;
    response.end("Not Found");
});
before(() => new Promise(resolve => server.listen(0, "127.0.0.1", () => {
    base = `http://127.0.0.1:${server.address().port}`;
    resolve();
})));
after(() => server.close());

// --- get.sh

// The shells here that take a POSIX script: the system's sh is one of them.
const shells = [["dash"], ["bash"], ["busybox", "sh"], ["mksh"], ["lksh"], ["ksh93"], ["posh"], ["yash"]]
    .filter(([name]) => which(name)).map(([name, ...rest]) => [which(name), ...rest]);
// Each awk here once, by what it is rather than by the name it goes by.
const awks = ["mawk", "gawk", "original-awk", "nawk", "awk"].map(which).filter(Boolean)
    .filter((awk, i, all) => all.findIndex(a => realpathSync(a) === realpathSync(awk)) === i);
if (which("busybox"))
    awks.push(`#!/bin/sh\nexec ${which("busybox")} awk "$@"\n`);

const REAL = Object.fromEntries(["curl", "wget", "sha256sum", "shasum", "openssl", "mv", "rm", "chmod", "printf",
                                 "test", "["].map(name => [name, which(name)]));

// A folder for PATH with these programs alone: the system's own, or a
// stand-in script.
let bins = 0;
function bin(programs) {
    const dir = join(work, `bin-${bins++}`);
    mkdirSync(dir);
    for (const [name, target] of Object.entries(programs)) {
        if (target.startsWith("#!"))
            writeFileSync(join(dir, name), target, { mode: 0o755 });
        else
            symlinkSync(target, join(dir, name));
    }
    return dir;
}
const STAND_INS = {
    uname: '#!/bin/sh\ncase $1 in -s) echo "$UNAME_S" ;; -m) echo "$UNAME_M" ;; *) exit 64 ;; esac\n',
    sysctl: '#!/bin/sh\n[ "$1 $2" = "-n hw.optional.arm64" ] && echo "${SYSCTL_ARM64:-0}"\n',
    // FreeBSD's fetch and OpenBSD's ftp in the forms get.sh calls them, and
    // the BSDs' sha256 -q: anything else fails.
    fetch: `#!/bin/sh\ncase "$1 $#" in "-qo 3"|"-o 3") ;; *) exit 64 ;; esac\n`
           + `if [ "$2" = - ]; then exec ${REAL.curl} -fsS "$3"; fi\nexec ${REAL.curl} -fsS -o "$2" "$3"\n`,
    ftp: `#!/bin/sh\ncase "$1 $#" in "-MVo 3"|"-o 3") ;; *) exit 64 ;; esac\n`
         + `if [ "$2" = - ]; then exec ${REAL.curl} -fsS "$3"; fi\nexec ${REAL.curl} -fsS -o "$2" "$3"\n`,
    sha256: `#!/bin/sh\n[ "$1 $#" = "-q 2" ] || exit 64\nset -- $(${REAL.sha256sum} "$2")\necho "$1"\n`,
};

// get.sh as this system, in a folder of its own: its exit code, what it
// said, and the folder.
async function getSh({ os = "Linux", arch = "x86_64", arm64, args = [], shell = shells[0], awk = awks[0],
                       tools = ["curl", "sha256sum"], variant = "pretty", dir, command }) {
    dir ??= mkdtempSync(join(work, "run-"));
    const programs = { awk, mv: REAL.mv, rm: REAL.rm, chmod: REAL.chmod, printf: REAL.printf, test: REAL.test,
                       "[": REAL["["], uname: STAND_INS.uname, sysctl: STAND_INS.sysctl, sh: shell[0] };
    for (const tool of tools)
        programs[tool] = STAND_INS[tool] ?? REAL[tool];
    const env = { PATH: bin(programs), HOME: home, OSDOS_RELEASE_URL: `${base}/release/${variant}`,
                  UNAME_S: os, UNAME_M: arch, SYSCTL_ARM64: arm64 ?? "", LANG: "C.UTF-8" };
    if (process.env.LD_LIBRARY_PATH)
        env.LD_LIBRARY_PATH = process.env.LD_LIBRARY_PATH;
    const [program, ...before] = shell;
    const result = await run(program, [...before, ...(command ? ["-c", command] : [GET_SH, ...args])], { cwd: dir, env });
    return { ...result, dir, listing: readdirSync(dir).sort() };
}

const systems = [
    { os: "Darwin", arch: "arm64", file: "OSD-OS-v9.8.7-macOS-arm64.dmg",
      next: "open OSD-OS-v9.8.7-macOS-arm64.dmg" },
    { os: "Linux", arch: "x86_64", file: "OSD-OS-linux-x86_64.AppImage",
      next: "./OSD-OS-linux-x86_64.AppImage", executable: true },
    { os: "Linux", arch: "aarch64", file: "install.sh", next: "bash install.sh" },
    { os: "FreeBSD", arch: "amd64", file: "OSD-OS-v9.8.7-freebsd-amd64.pkg", tools: ["fetch", "sha256"],
      next: "pkg add OSD-OS-v9.8.7-freebsd-amd64.pkg" },
    { os: "OpenBSD", arch: "amd64", file: "OSD-OS-9.8.7-openbsd-amd64.tgz", tools: ["ftp", "sha256"],
      next: "pkg_add -D unsigned OSD-OS-9.8.7-openbsd-amd64.tgz" },
    { os: "MINGW64_NT-10.0-26100", arch: "x86_64", file: "OSD-OS-v9.8.7-windows-x64.zip", next: "osdos.exe" },
    { os: "Linux", arch: "x86_64", args: ["image"], file: "OSD-OS-v9.8.7-raspberry-pi.img.xz",
      next: "Raspberry Pi Imager" },
];

function downloaded(result, file, next) {
    assert.equal(result.code, 0, result.stderr);
    assert.deepEqual(result.listing, [file], "the file alone, no .part left");
    assert.ok(readFileSync(join(result.dir, file)).equals(files[file]), `${file}'s bytes`);
    assert.match(result.stdout, /Checked: it matches the SHA-256 GitHub keeps for it\./);
    assert.ok(result.stdout.includes(next), result.stdout);
}

test("get.sh finds a POSIX shell and an awk to run with", () => {
    assert.ok(shells.length && awks.length, "dash or bash, and awk");
});

// A shell that runs its own uname before PATH's (busybox's, with its
// applets first) can be this system alone.
async function standsInUname([program, ...before]) {
    const dir = bin({ uname: STAND_INS.uname });
    const said = await run(program, [...before, "-c", "uname -s"], { env: { PATH: dir, UNAME_S: "Stand-in" } });
    return said.stdout.trim() === "Stand-in";
}

for (const shell of shells) {
    test(`get.sh under ${shell.join(" ").split("/").pop()}: each system's file, checked, and what comes next`, async () => {
        const stands = await standsInUname(shell);
        const here = { os: (await run("uname", ["-s"])).stdout.trim(), arch: (await run("uname", ["-m"])).stdout.trim() };
        for (const system of systems.filter(s => stands || (s.os === here.os && s.arch === here.arch))) {
            const result = await getSh({ ...system, shell });
            downloaded(result, system.file, system.next);
            if (system.executable)
                assert.ok(statSync(join(result.dir, system.file)).mode & 0o100, "the AppImage can be run");
        }
    });
}

test("get.sh on Apple Silicon under Rosetta takes the Mac's own file", async () => {
    const result = await getSh({ os: "Darwin", arch: "x86_64", arm64: "1" });
    downloaded(result, "OSD-OS-v9.8.7-macOS-arm64.dmg", "open OSD-OS-v9.8.7-macOS-arm64.dmg");
});

test("get.sh with each awk, downloader and SHA-256 tool, and a record on one line", async () => {
    for (const awk of awks) {
        const result = await getSh({ awk, variant: "compact" });
        assert.equal(result.code, 0, `${awk}: ${result.stderr}`);
        downloaded(result, "OSD-OS-linux-x86_64.AppImage", "./OSD-OS-linux-x86_64.AppImage");
    }
    for (const downloader of ["curl", "wget"].filter(t => REAL[t]))
        for (const hasher of ["sha256sum", "shasum", "openssl", "sha256"].filter(t => STAND_INS[t] || REAL[t])) {
            const result = await getSh({ tools: [downloader, hasher] });
            downloaded(result, "OSD-OS-linux-x86_64.AppImage", "./OSD-OS-linux-x86_64.AppImage");
        }
});

test("get.sh refuses a system OSD/OS has no app for, downloading nothing", async () => {
    const before = { ...hits };
    for (const [os, arch, says] of [["Darwin", "x86_64", /built for Apple Silicon/], ["Linux", "armv7l", /no app for Linux on armv7l/],
                                    ["FreeBSD", "arm64", /no app for FreeBSD on arm64/], ["SunOS", "i86pc", /no app for SunOS/]]) {
        const result = await getSh({ os, arch });
        assert.equal(result.code, 1, `${os} ${arch}`);
        assert.match(result.stderr, says);
        assert.match(result.stderr, /sh -s -- image/, "the image downloads anywhere");
        assert.deepEqual(result.listing, []);
    }
    assert.deepEqual(hits, before);
    const usage = await getSh({ args: ["everything"] });
    assert.equal(usage.code, 2);
    assert.match(usage.stderr, /sh -s -- image/);
});

test("get.sh deletes a file that doesn't match its digest, and keeps one GitHub has none for", async () => {
    const bad = await getSh({ variant: "bad-digest" });
    assert.equal(bad.code, 1);
    assert.match(bad.stderr, /doesn't match the SHA-256 GitHub keeps for it, so it is deleted again/);
    assert.deepEqual(bad.listing, [], "neither the file nor its .part");

    const none = await getSh({ variant: "no-digest" });
    assert.equal(none.code, 0, none.stderr);
    assert.match(none.stdout, /GitHub keeps no SHA-256 for OSD-OS-linux-x86_64\.AppImage, so it isn't checked\./);
    assert.deepEqual(none.listing, ["OSD-OS-linux-x86_64.AppImage"]);
});

test("get.sh says so when the release has no file for the system, or GitHub no release", async () => {
    const missing = await getSh({ os: "FreeBSD", arch: "amd64", tools: ["fetch", "sha256"], variant: "no-bsd" });
    assert.equal(missing.code, 1);
    assert.match(missing.stderr, /OSD\/OS v9\.8\.7 has no FreeBSD package\./);
    for (const [variant, says] of [["broken", /isn't a release's record/], ["gone", /didn't answer/]]) {
        const result = await getSh({ variant });
        assert.equal(result.code, 1, variant);
        assert.match(result.stderr, says);
        assert.deepEqual(result.listing, []);
    }
});

test("get.sh keeps a file already downloaded and checked, and replaces one that isn't right", async () => {
    const file = "OSD-OS-v9.8.7-raspberry-pi.img.xz";
    const first = await getSh({ args: ["image"] });
    downloaded(first, file, "Raspberry Pi Imager");
    const count = hits[file];
    const again = await getSh({ args: ["image"], dir: first.dir });
    assert.equal(again.code, 0, again.stderr);
    assert.match(again.stdout, /is here already, and matches the SHA-256 GitHub keeps for it\./);
    assert.equal(hits[file], count, "not downloaded again");

    writeFileSync(join(first.dir, file), "a download gone wrong");
    writeFileSync(join(first.dir, `${file}.part`), "one cut short");
    const fixed = await getSh({ args: ["image"], dir: first.dir });
    downloaded(fixed, file, "Raspberry Pi Imager");
    assert.equal(hits[file], count + 1);
});

test("get.sh passes shellcheck and every shell's parser, in plain ASCII", async t => {
    const script = readFileSync(GET_SH);
    assert.ok(![...script].some(b => b > 0x7e || (b < 0x20 && b !== 0x0a)), "ASCII alone");
    for (const [program, ...before] of shells) {
        const parsed = await run(program, [...before, "-n", GET_SH]);
        assert.equal(parsed.code, 0, `${program}: ${parsed.stderr}`);
    }
    const shellcheck = which("shellcheck");
    if (!shellcheck)
        return t.diagnostic("no shellcheck here");
    const checked = await run(shellcheck, ["-s", "sh", GET_SH]);
    assert.equal(checked.code, 0, checked.stdout);
});

test("get.sh's next steps are INSTALL.md's", () => {
    const script = readFileSync(GET_SH, "utf8");
    const install = readFileSync(join(root, "INSTALL.md"), "utf8");
    const freebsd = install.match(/^ +(pkg install -y .+)$/m)[1];
    assert.ok(script.includes(`say "    ${freebsd}"`), freebsd);
    assert.match(install, /pkg_add -D unsigned OSD-OS-<number>-openbsd-amd64\.tgz/);
    assert.ok(script.includes('say "    pkg_add -D unsigned $name"'));
    // Each link to a part of a page, to one of its headings, as GitHub
    // names them.
    const anchors = page => readFileSync(join(root, page), "utf8").split("\n").filter(l => /^#+ /.test(l))
        .map(l => l.replace(/^#+ /, "").toLowerCase().replace(/[^a-z0-9 _-]/g, "").replaceAll(" ", "-"));
    for (const [, page, anchor] of script.replaceAll("$INSTALL", "INSTALL.md")
                                        .matchAll(/(INSTALL\.md|os\/README\.md)#([a-z0-9_-]+)/g))
        assert.ok(anchors(page).includes(anchor), `${page}#${anchor}`);
});

// --- get.ps1

const pwsh = which("pwsh");

async function getPs1(command, { variant = "pretty", dir } = {}) {
    dir ??= mkdtempSync(join(work, "ps-"));
    const env = { PATH: process.env.PATH, HOME: home, OSDOS_RELEASE_URL: `${base}/release/${variant}`,
                  POWERSHELL_TELEMETRY_OPTOUT: "1", DOTNET_CLI_TELEMETRY_OPTOUT: "1", POWERSHELL_UPDATECHECK: "Off" };
    const result = await run(pwsh, ["-NoProfile", "-NonInteractive", "-Command", command.replaceAll("BASE", base)],
                             { cwd: dir, env });
    return { ...result, dir, listing: readdirSync(dir).sort() };
}

test("get.ps1 by irm | iex: the zip, checked and unzipped into osdos, then kept as it is", { skip: !pwsh && "no PowerShell here" }, async () => {
    const zipName = "OSD-OS-v9.8.7-windows-x64.zip";
    // In a session in strict mode, which the script's own block leaves aside.
    const first = await getPs1("Set-StrictMode -Version Latest; irm BASE/get.ps1 | iex");
    assert.equal(first.code, 0, first.stderr);
    assert.doesNotMatch(first.stdout, /Error:/, first.stdout);
    assert.match(first.stdout, /Checked: it matches the SHA-256 GitHub keeps for it\./);
    assert.match(first.stdout, /\.\\osdos\\osdos\.exe/);
    assert.deepEqual(first.listing, [zipName, "osdos"]);
    assert.equal(readFileSync(join(first.dir, "osdos/osdos.exe"), "utf8"), "MZ, a stand-in");
    assert.ok(readFileSync(join(first.dir, zipName)).equals(files[zipName]));

    const count = hits[zipName];
    writeFileSync(join(first.dir, "osdos/mpv.exe"), "the user's own");
    const again = await getPs1("irm BASE/get.ps1 | iex", { dir: first.dir });
    assert.match(again.stdout, /is here already, and matches the SHA-256 GitHub keeps for it\./);
    assert.match(again.stdout, /is here already, so the zip stays as it is/);
    assert.equal(hits[zipName], count, "not downloaded again");
    assert.equal(readFileSync(join(first.dir, "osdos/mpv.exe"), "utf8"), "the user's own");
});

test("get.ps1 with image downloads the Raspberry Pi image", { skip: !pwsh && "no PowerShell here" }, async () => {
    const file = "OSD-OS-v9.8.7-raspberry-pi.img.xz";
    const result = await getPs1("& ([scriptblock]::Create((irm BASE/get.ps1))) image");
    assert.doesNotMatch(result.stdout, /Error:/, result.stdout);
    assert.match(result.stdout, /Raspberry Pi Imager/);
    assert.deepEqual(result.listing, [file]);
    assert.ok(readFileSync(join(result.dir, file)).equals(files[file]));
});

test("get.ps1 deletes a file that doesn't match its digest, and says what went wrong", { skip: !pwsh && "no PowerShell here" }, async () => {
    const bad = await getPs1("irm BASE/get.ps1 | iex", { variant: "bad-digest" });
    assert.match(bad.stdout, /Error: OSD-OS-v9\.8\.7-windows-x64\.zip doesn't match the SHA-256 GitHub keeps for it/);
    assert.deepEqual(bad.listing, [], "neither the zip, its .part nor osdos");
    for (const [command, variant, says] of [["irm BASE/get.ps1 | iex", "gone", /Error: GitHub didn't answer/],
                                            ["irm BASE/get.ps1 | iex", "no-windows", /Error: OSD\/OS v9\.8\.7 has no app for Windows\./],
                                            ["& ([scriptblock]::Create((irm BASE/get.ps1))) everything", "pretty", /Error: get\.ps1 downloads/]]) {
        const result = await getPs1(command, { variant });
        assert.match(result.stdout, says, result.stdout);
    }
});

test("get.ps1 in plain ASCII", () => {
    assert.ok(![...readFileSync(GET_PS1)].some(b => b > 0x7e || (b < 0x20 && b !== 0x0a)), "ASCII alone");
});

// --- On the website

// Every command the landing page gives, at its top and in the hardware
// tabs, as it reads there, the site's address the stand-in's: each
// downloads the file it says, on the system it is for.
test("the landing page's terminal commands download what they say", async t => {
    const page = readFileSync(join(root, "site/index.html"), "utf8");
    const commands = [...new Set([...page.matchAll(
        /<div class="command">(?:<span class="command-on">[^<]*<\/span>)?<code[^>]*>([^<]+)<\/code>/g)]
        .map(([, code]) => code.replaceAll("&amp;", "&")))];
    assert.deepEqual(commands.map(c => c.split(" ")[0]).sort(), ["curl", "curl", "fetch", "ftp", "iex", "irm"],
                     commands.join("\n"));
    const systems = {
        curl: { os: "Linux", arch: "x86_64", file: "OSD-OS-linux-x86_64.AppImage" },
        fetch: { os: "FreeBSD", arch: "amd64", tools: ["fetch", "sha256"], file: "OSD-OS-v9.8.7-freebsd-amd64.pkg" },
        ftp: { os: "OpenBSD", arch: "amd64", tools: ["ftp", "sha256"], file: "OSD-OS-9.8.7-openbsd-amd64.tgz" },
    };
    for (const code of commands) {
        const command = code.replaceAll("https://osdos.tv", base);
        const image = /\bimage"?$/.test(code);
        const program = code.split(" ")[0];
        if (program === "irm" || program === "iex") {
            if (!pwsh) {
                t.diagnostic(`no PowerShell here for: ${code}`);
                continue;
            }
            const result = await getPs1(command);
            assert.doesNotMatch(result.stdout, /Error:/, `${code}: ${result.stdout}`);
            assert.deepEqual(result.listing, image ? ["OSD-OS-v9.8.7-raspberry-pi.img.xz"]
                                                   : ["OSD-OS-v9.8.7-windows-x64.zip", "osdos"], code);
            continue;
        }
        const { file, ...system } = systems[program];
        const result = await getSh({ ...system, command });
        downloaded(result, image ? "OSD-OS-v9.8.7-raspberry-pi.img.xz" : file, "Next:");
    }
});

test("the website serves get.sh and get.ps1 as they are, and its commands name them", async () => {
    const out = join(work, "site");
    const built = await run("python3", ["-I", join(root, "scripts/build-site.py"), out]);
    assert.equal(built.code, 0, built.stderr);
    for (const name of ["get.sh", "get.ps1"])
        assert.ok(readFileSync(join(out, name)).equals(readFileSync(join(root, "site", name))), name);
    const page = readFileSync(join(out, "index.html"), "utf8");
    const named = [...page.matchAll(/https:\/\/osdos\.tv\/([a-z0-9.-]+)/g)].map(m => m[1]).filter(n => /^get\./.test(n));
    assert.ok(named.includes("get.sh") && named.includes("get.ps1"), named.join(" "));
    for (const name of named)
        assert.ok(existsSync(join(out, name)), name);
});
