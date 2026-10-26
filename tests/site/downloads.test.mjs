// The landing page's download buttons as scripts/build-site.py makes them:
// each led to its file in the latest release, given GitHub's record of the
// release, with the file's name and size under it, and to the releases page
// without the record, or for a file the release hasn't. Every button names a
// file the release workflow publishes.
//   node --test tests/site/*.test.mjs
import { test, after } from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../..", import.meta.url));
const work = mkdtempSync(join(tmpdir(), "osdos-downloads-"));
after(() => rmSync(work, { recursive: true, force: true }));

const LATEST = "https://github.com/mehmetraif/OSD-OS/releases/latest";

// The landing page as the script builds it, with the release's record or
// without one.
function landing(name, release) {
    const out = join(work, name);
    const args = ["-I", join(root, "scripts/build-site.py"), out];
    if (release) {
        writeFileSync(join(work, `${name}.json`), JSON.stringify(release));
        args.push(join(work, `${name}.json`));
    }
    execFileSync("python3", args, { stdio: "ignore" });
    return readFileSync(join(out, "index.html"), "utf8");
}

const links = html => [...html.matchAll(/<a\b[^>]*?\bhref="([^"]*)"[^>]*?\bdata-asset="([^"]+)"/g)]
    .map(([, href, suffix]) => ({ href, suffix }));
const lines = html => [...html.matchAll(/<span class="download-meta" data-asset="([^"]+)">([^<]*)<\/span>/g)]
    .map(([, suffix, text]) => ({ suffix, text }));

const source = readFileSync(join(root, "site/index.html"), "utf8");
const suffixes = [...new Set(links(source).map(l => l.suffix))];

test("each download button is a file the release workflow publishes", () => {
    // The files of the release notes' Downloads table, for a made-up tag.
    const workflow = readFileSync(join(root, ".github/workflows/release.yml"), "utf8");
    const published = [...workflow.matchAll(/^ +echo "\| \*\*[^|]*\| \\`([^`\\]+)\\` \|/gm)]
        .map(([, file]) => file.replaceAll("${TAG}", "v1.2.3").replaceAll("${OPENBSD_PACKAGE}", "OSD-OS-1.2.3-openbsd-amd64.tgz"));
    assert.ok(published.length >= 7, `the Downloads table's files: ${published.join(" ")}`);
    assert.ok(suffixes.length >= 6, `the buttons' files: ${suffixes.join(" ")}`);
    for (const suffix of suffixes)
        assert.equal(published.filter(f => f.endsWith(suffix)).length, 1, `${suffix} among ${published.join(" ")}`);
    // Each line under a button is about a button's file.
    for (const { suffix } of lines(source))
        assert.ok(suffixes.includes(suffix), suffix);
});

test("with the release's record, each button leads to its file, named with its size", () => {
    const sizes = { "-raspberry-pi.img.xz": 1192510000, "-macOS-arm64.dmg": 61078313, "-linux-x86_64.AppImage": 131258872,
                    "-windows-x64.zip": 66787209, "-freebsd-amd64.pkg": 19151243 };
    const shown = { "-raspberry-pi.img.xz": "1.11 GB", "-macOS-arm64.dmg": "58.2 MB", "-linux-x86_64.AppImage": "125 MB",
                    "-windows-x64.zip": "63.7 MB", "-freebsd-amd64.pkg": "18.3 MB" };
    // OpenBSD's package left out: a file the release hasn't.
    const assets = Object.entries(sizes).map(([suffix, size]) => {
        const name = suffix === "-linux-x86_64.AppImage" ? "OSD-OS-linux-x86_64.AppImage" : `OSD-OS-v9.8.7${suffix}`;
        return { name, size, browser_download_url: `https://github.com/mehmetraif/OSD-OS/releases/download/v9.8.7/${name}` };
    });
    assets.push({ name: "install.sh", size: 39704, browser_download_url: "https://example.invalid/install.sh" });
    const html = landing("with", { tag_name: "v9.8.7", assets });
    for (const { href, suffix } of links(html)) {
        const asset = assets.find(a => a.name.endsWith(suffix));
        assert.equal(href, asset ? asset.browser_download_url : LATEST, suffix);
    }
    const original = Object.fromEntries(lines(source).map(l => [l.suffix, l.text]));
    for (const { suffix, text } of lines(html)) {
        const asset = assets.find(a => a.name.endsWith(suffix));
        assert.equal(text, asset ? `${asset.name} · ${shown[suffix]}` : original[suffix], suffix);
    }
    assert.ok(links(html).some(l => l.suffix === "-openbsd-amd64.tgz" && l.href === LATEST), "OpenBSD's left as it was");
});

test("without the release's record, every button leads to the releases page", () => {
    const html = landing("without", null);
    assert.ok(links(html).length >= suffixes.length);
    for (const { href, suffix } of links(html))
        assert.equal(href, LATEST, suffix);
    assert.deepEqual(lines(html), lines(source));
});
