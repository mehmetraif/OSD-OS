#!/usr/bin/env python3
"""Builds OSD/OS's website, published on GitHub Pages (site/), into a folder:
the landing page at its root (site/index.html, with the tour and the theme
recordings, the photo and the logo from docs/images, the Arcade screen from
docs/screenshots and the stills from site/assets, and themes.json, the themes
with their colours and pictures; its download buttons lead to the latest
release's files, given GitHub's record of the release), the terminal
downloads beside it (site/get.sh and site/get.ps1), and OSD/OS Setup,
the settings page, under setup/ (site/setup/): its own files, the app's font
and logo, the display presets, what its live preview draws with (the app's
own shaders, shaders/, and the skins' pictures), and schema.json, the
settings the page offers, read from the app as it is: Settings' own rows
(site/setup/schema-extra.json), the modules' manifests, the presets
(os/stage-osdos/03-boot/files, with the names Settings → Display Output gives
them in src/display/DisplayOutput.cpp), the themes and skins (assets/), the
looks Main.qml has (its colour schemes and effects), the subtitle languages,
and the time zone database's countries and zones.

  scripts/build-site.py <out-dir> [release.json]

release.json is the latest release as GitHub's API gives it
(repos/<owner>/<repo>/releases/latest); without it, or for a file the release
hasn't, a download button leads to the releases page. A folder that is there
already is replaced only if it is one this made (its .osdos-site) or empty.
"""
import json
import pathlib
import re
import shutil
import sys
import unicodedata

ROOT = pathlib.Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
SETUP = SITE / "setup"
IMAGES = ROOT / "docs/images"
SCREENSHOTS = ROOT / "docs/screenshots"
PRESETS = ROOT / "os/stage-osdos/03-boot/files"
SHADERS = ROOT / "shaders"
ISO3166 = pathlib.Path("/usr/share/zoneinfo/iso3166.tab")
ZONES = pathlib.Path("/usr/share/zoneinfo/zone.tab")
MARK = ".osdos-site"
# A theme's recording on the landing page: a GIF, or an MP4 where a GIF would
# blur the look and weigh far more (Orb's Arcade screen, in 3D).
RECORDINGS = (".gif", ".mp4")

# The landing page's line on each theme, after the wiki's Themes page: what
# its recording shows, read out for the picture.
THEME_ABOUT = {
    "arcade": "Synthwave's colours, a starfield, text running through the rainbow and a pixel rainbow "
              "dripping from under the selected line",
    "demoscene": "gold on purple, a starfield, glowing text and sparkles, windows turning like a demo's "
                 "cube, and a tracker's music",
    "green-screen": "a phosphor green of its own, DOS windows, a glowing tube and glowing text; windows "
                    "come in on a wave from a corner",
    "inferno": "amber on brown, pixel flames burning up from the window's foot and a welder's sparks "
               "bursting from the selected line",
    "late-show": "Late Night's white on black in DOS windows, with a tape's colour bleed and noise and "
                 "text flickering like a neon sign",
    "matrix": "green on black, Matrix rain behind the menus, glowing text and lightning crackling out "
              "of the selected line; windows ripple",
    "orb": "the Arcade screen in 3D x-ray glass: the games on a panel, the game's picture in an orb and "
           "fog drifting behind; a game started takes the camera into the orb",
    "trinitron": "Video 1's blue in rounded windows on a tube's curved face, with scanlines and glow; "
                 "windows turn over like a cube",
    "winter": "ice on night blue, snow falling, a glint sweeping across the text and sparkles flying "
              "off the selected line; windows spread from a drop",
}
# What a theme's recording shows where it isn't the menus being browsed: a
# look of the Arcade screen alone is recorded there.
THEME_RECORDED = {
    "orb": "on the Arcade screen as a game is chosen and started",
}


def option(o):
    """An option as [label, value]: a string is both."""
    return [o, o] if isinstance(o, str) else [o[0], o[1]]


def help_text(description):
    """A manifest's description in one paragraph: its lines, which the app's
    help line shows one at a time, as sentences."""
    lines = [" ".join(line.split()) for line in str(description or "").splitlines()]
    return " ".join(line if line[-1] in ".!?:…" else line + "." for line in lines if line)


def languages():
    """Local Files' and the NFC reader's subtitle languages, as their backends
    list them: Any first, then each language once."""
    out, seen = [["Any", "-"]], set()
    for lang in json.loads((ROOT / "modules/local_files/iso639-1.json").read_text()):
        if lang["label"] and lang["label"] not in seen:
            seen.add(lang["label"])
            out.append([lang["label"], lang["id"]])
    return out


def builtin(folder, file_name):
    """The built-in themes or skins, as (path, JSON)."""
    for path in sorted((ROOT / "assets" / folder).glob("*/" + file_name)):
        yield path, json.loads(path.read_text())


def offered(folder, file_name):
    """The built-in themes or skins the page offers: every one but a theme
    whose theme.json says "setup": false, which is the app's alone (chosen in
    its Settings)."""
    return ((path, obj) for path, obj in builtin(folder, file_name) if obj.get("setup", True) is not False)


def looks(folder, file_name):
    """The built-in themes or skins: [name, folder], by name."""
    out = [[obj["name"], path.parent.name] for path, obj in offered(folder, file_name)]
    return sorted(out, key=lambda o: o[0].lower())


def module_settings(manifest, extra):
    """A manifest's rows as the page offers them: those whose values are
    known without the device (submenus' rows flat under the module, as the
    app keeps them), and the extra ones. Views and actions have none."""
    options = extra.get("options", {})
    defaults = extra.get("defaults", {})
    out = []

    def walk(settings):
        for s in settings:
            key, kind = s.get("key"), s.get("type")
            row = {"key": key, "label": s.get("label", key), "help": help_text(s.get("description"))}
            if "shown_with" in s:
                row["shown_with"] = s["shown_with"]
            if kind == "submenu":
                out.append({"type": "heading", "label": s.get("label", "")})
                walk(s.get("settings", []))
                continue
            if key == "enabled":
                continue
            if kind == "toggle":
                row.update(type="toggle", on=True, off=False, default=str(s.get("default", "OFF")).upper() == "ON")
            elif kind == "list_single":
                known = s.get("options") or options.get(key)
                if not known:
                    continue
                known = languages() if known == "languages" else [option(o) for o in known]
                default = s.get("default", defaults.get(key, known[0][1]))
                values = {v.lower(): v for _, v in known}
                if len(known) == 2 and set(values) == {"on", "off"}:
                    # Two ways, On and Off: a checkbox, as the app's toggles.
                    row.update(type="toggle", on=values["on"], off=values["off"], default=default)
                else:
                    row.update(type="choice", options=known, default=default)
            elif kind == "multiselect_submenu":
                if key not in options:
                    continue
                row.update(type="multi", options=[option(o) for o in options[key]], default=True)
            elif kind == "slider":
                row.update(type="slider", min=0, max=100, step=s.get("step", 10), default=s.get("default", 0),
                           start_text=s.get("start_text", ""), end_text=s.get("end_text", ""))
            elif kind == "directory_browser":
                row.update(type="text", default=s.get("default", ""), folder=True,
                           placeholder="Empty: the module's own folder")
            else:
                continue
            out.append(row)

    walk(manifest.get("settings", []))
    # The rows a module keeps without one of its own, after the row they
    # belong with, else last.
    for row in extra.get("extra", []):
        row = dict(row)
        after = row.pop("after", None)
        at = next((i + 1 for i, r in enumerate(out) if after and r.get("key") == after), len(out))
        out.insert(at, row)
    return out


def modules(extra):
    groups = extra["groups"]
    out = []
    for path in sorted((ROOT / "modules").glob("*/manifest.json")):
        manifest = json.loads(path.read_text())
        mid = manifest["id"]
        enabled = next((s for s in manifest.get("settings", []) if s.get("key") == "enabled"), {})
        mod_extra = extra["modules"].get(mid, {})
        group = manifest.get("settings_group", "")
        out.append({
            "id": mid,
            "name": manifest["name"],
            # The main menu lists the modules in their folders' order.
            "folder": path.parent.name,
            "group": groups.get(group, groups[""]),
            "enabled": str(enabled.get("default", "ON")).upper() != "OFF",
            "settings": module_settings(manifest, mod_extra),
            "retroachievements": bool(mod_extra.get("retroachievements")),
        })
    order = list(dict.fromkeys(groups.values()))
    return sorted(out, key=lambda m: (order.index(m["group"]), m["name"].lower()))


def preset_models(pid):
    """The boards a preset is for, as Settings → Display Output offers it
    (DisplayOutput.cpp's boardHas()): keep the two in step."""
    if pid == "hdmi":
        return ["pi3", "pi4", "pi5"]
    if pid.startswith("crt-gpio-"):
        return ["pi5"]
    if pid.startswith("crt-"):
        return ["pi3", "pi4", "pi5"]
    if pid in ("scart-rgb-ntsc", "scart-rgb-pal"):
        return ["pi4", "pi5"]
    if pid.startswith("scart-rgb-"):
        return ["pi4"]
    if pid in ("jamma-480i", "jamma-576i"):
        return ["pi4"]
    if pid.startswith("jamma-"):
        return ["pi4", "pi5"]
    return []


def preset_about(text):
    """A preset's opening comment, up to where it says how it is chosen."""
    lines = []
    for line in text.splitlines():
        if not line.startswith("#"):
            break
        body = line.lstrip("#").strip()
        if not body or body.startswith("osdos-"):
            break
        lines.append(body)
    about = " ".join(lines)
    return about.split(" Chosen in Settings")[0].strip()


def presets():
    source = (ROOT / "src/display/DisplayOutput.cpp").read_text()
    # The warnings said as a preset is chosen: a constant of one or more
    # string literals.
    warnings = {name: "".join(re.findall(r'"([^"]*)"', literals))
                for name, literals in re.findall(r'constexpr const char \*(k\w+) =\s*((?:"[^"]*"\s*)+);', source)}
    out = []
    for pid, label, warning in re.findall(
            r'\{\s*"([a-z0-9-]+)",\s*"([^"]+)",\s*\d+,\s*\d+(?:,\s*(k\w+))?\s*\}', source):
        path = PRESETS / f"osdos-display-{pid}.txt"
        if not path.is_file():
            sys.exit(f"build-site: {path} is missing for the preset {pid}")
        text = path.read_text()
        out.append({"id": pid, "label": label, "models": preset_models(pid),
                    "about": preset_about(text), "warning": warnings.get(warning, "")})
    files = {p.name for p in PRESETS.glob("osdos-display-*.txt")}
    named = {f"osdos-display-{p['id']}.txt" for p in out}
    if files != named:
        sys.exit(f"build-site: presets without a name in DisplayOutput.cpp: {sorted(files - named)}")
    return out


def countries():
    if not ISO3166.is_file():
        sys.exit(f"build-site: {ISO3166} is missing (the tzdata package)")
    out = []
    for line in ISO3166.read_text().splitlines():
        if line and not line.startswith("#"):
            code, name = line.split("\t", 1)
            out.append([name, code])
    # By the name as it reads, its accents aside: Åland Islands among the A's,
    # not after Zimbabwe.
    return sorted(out, key=lambda o: unicodedata.normalize("NFKD", o[0]).encode("ascii", "ignore").lower())


def zones():
    """The time zones by the names the card has (Debian's tzdata, without
    tzdata-legacy's older ones a browser may still offer): each country's
    from zone.tab, and UTC."""
    if not ZONES.is_file():
        sys.exit(f"build-site: {ZONES} is missing (the tzdata package)")
    out = {line.split("\t")[2].strip() for line in ZONES.read_text().splitlines()
           if line and not line.startswith("#")}
    return sorted(out) + ["UTC"]


def qml_block(source, name):
    """What a QML property of Main.qml holds between its brackets: an object,
    `name: ({ … })`, or a list, `name: [ … ]`, its comments left out. A
    bracket in a string or a comment isn't one."""
    found = re.search(r"\b" + re.escape(name) + r"\s*:\s*\(?([{\[])", source)
    if not found:
        sys.exit(f"build-site: Main.qml has no {name}")
    opener = found.group(1)
    closer = "}" if opener == "{" else "]"
    depth = 0
    # The block's tokens: a comment, a string, or any one character.
    token = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|.', re.S)
    kept = []
    for match in token.finditer(source, found.start(1)):
        text = match.group(0)
        if text.startswith(("//", "/*")):
            continue
        if text == opener:
            depth += 1
        elif text == closer:
            depth -= 1
            if depth == 0:
                return "".join(kept[1:])
        kept.append(text)
    sys.exit(f"build-site: Main.qml's {name} has no end")


def skin_part(value, folder, served):
    """One of a skin's pictures as AppCore's skinImage() reads it: { source,
    border: [left, top, right, bottom], tile }, its picture copied where the
    page has it (served: its folder there). As lookFile() takes one: a PNG,
    GIF or BMP in the look's folder, none out of it, through a link either.
    None for none."""
    part = value if isinstance(value, dict) else {"image": value}
    image = part.get("image")
    if not isinstance(image, str) or not image:
        return None
    home = folder.resolve()
    path = (folder / image).resolve()
    if home not in path.parents or not path.is_file() or path.suffix.lower() not in (".png", ".gif", ".bmp"):
        return None
    # A whole number of the picture's pixels, as QJsonValue::toInt() takes one.
    number = lambda v: isinstance(v, (int, float)) and not isinstance(v, bool)
    side = lambda v: max(0, min(512, int(v))) if float(v).is_integer() else 0
    border = part.get("border", 0)
    if number(border):
        border = [side(border)] * 4
    elif isinstance(border, list) and len(border) == 4 and all(number(b) for b in border):
        border = [side(b) for b in border]
    else:
        border = [0, 0, 0, 0]
    tile = part.get("tile") if part.get("tile") in ("stretch", "repeat", "round") else "stretch"
    return {"source": f"{served}/{path.relative_to(home).as_posix()}", "border": border, "tile": tile,
            "file": str(path)}


def skin_parts(obj, folder, served):
    """What a skin dresses: a picture of any of the window's parts."""
    parts = {}
    for key in ("window", "titleBar", "hintBar", "selection"):
        if key in obj:
            part = skin_part(obj[key], folder, served)
            if part:
                parts[key] = part
    return parts


def looks_data():
    """What the live preview draws a look with, as the app has it: Main.qml's
    colour schemes (the two colours everything is drawn in) and effects (the
    screen's and the text's knobs, each background's shader, the selectors'
    and transitions' names and the transitions' shaders), the parts of each
    theme the page offers (offered()) and each skin's pictures, and the
    shaders' names (shaders/, copied as they are)."""
    main = (ROOT / "Main.qml").read_text()
    entries = lambda block: re.findall(r'"([^"]+)"\s*:\s*\{([^{}]*)\}', block)
    schemes = {}
    for name, body in entries(qml_block(main, "schemes")):
        colors = dict(re.findall(r'"(\w+)"\s*:\s*"(#[0-9A-Fa-f]{6})"', body))
        schemes[name] = {"primary": colors["primary"], "surface": colors["surface"]}
    knobs = lambda name: {n: {k: float(v) for k, v in re.findall(r'"(\w+)"\s*:\s*([0-9.]+)', body)}
                          for n, body in entries(qml_block(main, name))}
    shader = r'"([^"]+)"\s*:\s*(?:\{\s*"shader"\s*:\s*)?builtInShader\("([\w-]+)"\)'
    skins = {}
    for path in sorted((ROOT / "assets/skins").glob("*/skin.json")):
        skins[path.parent.name] = skin_parts(json.loads(path.read_text()), path.parent, f"skins/{path.parent.name}")
    themes = {}
    for path, obj in offered("themes", "theme.json"):
        theme = {"name": obj.get("name", path.parent.name), "effects": obj.get("effects", {})}
        if "colors" in obj:
            theme["colors"] = obj["colors"]
        if isinstance(obj.get("skin"), str):
            theme["skin"] = obj["skin"]
        elif isinstance(obj.get("skin"), dict):
            theme["skin"] = skin_parts(obj["skin"], path.parent, f"themes/{path.parent.name}")
        themes[path.parent.name] = theme
    return {
        "schemes": schemes,
        "screen": knobs("screenPresets"),
        "text": knobs("textPresets"),
        "background": dict(re.findall(shader, qml_block(main, "backgroundPresets"))),
        "selector": re.findall(r'"([^"]+)"', qml_block(main, "selectorPresets")),
        "transition": re.findall(r'"([^"]+)"', qml_block(main, "transitionPresets")),
        "transitionShaders": dict(re.findall(shader, qml_block(main, "transitionShaders"))),
        "themes": themes,
        "skins": skins,
        "shaders": sorted(p.stem for p in SHADERS.glob("*.frag")),
    }


def landing_themes(looks):
    """themes.json, the landing page's theme previews: each built-in theme,
    Setup's and the app's alone (Orb's) alike, by its folder, with its name, its
    line, its two colours (its own, or its colour scheme's; the default
    scheme's without either), its pictures, a still from site/assets and the
    recording docs/images holds of it (RECORDINGS), and what the recording
    shows where it isn't the menus being browsed. A theme without both
    pictures isn't shown."""
    out = []
    for path, theme in builtin("themes", "theme.json"):
        folder = path.parent.name
        still = SITE / "assets" / f"theme-{folder}.png"
        recording = next((IMAGES / f"theme-{folder}{ext}" for ext in RECORDINGS
                          if (IMAGES / f"theme-{folder}{ext}").is_file()), None)
        if not still.is_file() or recording is None:
            continue
        colors = theme.get("colors")
        if isinstance(colors, str):
            colors = looks["schemes"].get(colors)
        if not isinstance(colors, dict):
            colors = looks["schemes"].get("Video 1") or next(iter(looks["schemes"].values()))
        entry = {"id": folder, "name": theme.get("name", folder),
                 "about": THEME_ABOUT.get(folder, "its own colours, effects and music"),
                 "primary": colors["primary"], "surface": colors["surface"],
                 "still": f"assets/{still.name}", "recording": f"assets/{recording.name}"}
        if folder in THEME_RECORDED:
            entry["recorded"] = THEME_RECORDED[folder]
        out.append(entry)
    return out


def copy_fonts(folder):
    """The deck's letters, with their licence, where a page's style.css
    reads them."""
    (folder / "fonts").mkdir()
    shutil.copy2(ROOT / "assets/fonts/VCR_OSD_MONO_1.001.ttf", folder / "fonts")
    shutil.copy2(ROOT / "assets/fonts/LICENSE-vcr-osd-mono.txt", folder / "fonts")


def write_icons(folder, logo=True):
    """The tab's icon, OSD/OS's own, in the sizes a browser picks from (the
    app's window's, assets/images/app-icon), and the app's logo where the
    page draws it (Setup's title bar)."""
    (folder / "img").mkdir()
    if logo:
        (folder / "img/logo.svg").write_text((ROOT / "assets/images/logo.svg").read_text())
    for size in (16, 32, 48, 256):
        shutil.copy2(ROOT / f"assets/images/app-icon/{size}.png", folder / f"img/icon-{size}.png")


def file_size(size):
    """A file's size as the release page gives it, in MB or GB of 1024."""
    mb = size / 1024 ** 2
    if mb >= 1024:
        return f"{mb / 1024:.2f} GB"
    return f"{mb:.1f} MB" if mb < 100 else f"{mb:.0f} MB"


def link_downloads(html, release):
    """The landing page's download buttons led to their files in the release
    (GitHub's record of it, or None): each link and each line under one is
    marked with the end of its file's name (data-asset), as the release
    workflow names the files and the app's updater finds them. A link gets
    the file's address, the line its name and size; a file the release
    hasn't keeps the HTML's own, the releases page."""
    assets = (release or {}).get("assets", [])

    def asset(suffix):
        return next((a for a in assets if a["name"].endswith(suffix)), None)

    def link(m):
        found = asset(m.group(4))
        return m.group(1) + (found["browser_download_url"] if found else m.group(2)) + m.group(3)

    def line(m):
        found = asset(m.group(2))
        text = f'{found["name"]} · {file_size(found["size"])}' if found else m.group(3)
        return m.group(1) + text + m.group(4)

    html = re.sub(r'(<a\b[^>]*?\bhref=")([^"]*)("[^>]*?\bdata-asset="([^"]+)")', link, html)
    return re.sub(r'(<span class="download-meta" data-asset="([^"]+)">)([^<]*)(</span>)', line, html)


def schema():
    extra = json.loads((SETUP / "schema-extra.json").read_text())
    mods = modules(extra)
    app = json.loads(json.dumps(extra["app"]))
    for group in app:
        for row in group["settings"]:
            source = row.pop("options_from", None)
            row["options"] = [option(o) for o in row.get("options", [])]
            if source == "themes":
                row["options"] += looks("themes", "theme.json")
            elif source == "skins":
                row["options"] += looks("skins", "skin.json")
            elif source == "modules":
                row["options"] += [[m["name"], m["id"]] for m in mods]
    return {
        "app": app,
        "modules": mods,
        "presets": presets(),
        "looks": looks_data(),
        "countries": countries(),
        "zones": zones(),
        "keyboards": extra["keyboards"],
    }


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    out = pathlib.Path(sys.argv[1])
    release = json.loads(pathlib.Path(sys.argv[2]).read_text()) if len(sys.argv) == 3 else None
    if out.exists():
        if not (out / MARK).is_file() and any(out.iterdir()):
            sys.exit(f"build-site: {out} is there and isn't a page this built: not replaced")
        shutil.rmtree(out)
    out.mkdir(parents=True)
    (out / MARK).write_text("Built by scripts/build-site.py, which replaces this folder.\n")
    data = schema()

    # The landing page, at the root: its own files, its downloads led to the
    # release's, the app's font and icon, its pictures and its themes.
    landing = link_downloads((SITE / "index.html").read_text(encoding="utf-8"), release)
    (out / "index.html").write_text(landing, encoding="utf-8")
    shutil.copy2(SITE / "style.css", out / "style.css")
    # The terminal downloads the landing page gives, served from the root:
    # curl -fsSL https://osdos.tv/get.sh | sh, irm https://osdos.tv/get.ps1 | iex.
    for name in ("get.sh", "get.ps1"):
        shutil.copy2(SITE / name, out / name)
    (out / "js").mkdir()
    shutil.copy2(SITE / "js/home.js", out / "js")
    copy_fonts(out)
    write_icons(out, logo=False)
    (out / "assets").mkdir()
    recordings = sorted(p for ext in RECORDINGS for p in IMAGES.glob(f"theme-*{ext}"))
    for path in sorted((SITE / "assets").glob("*.png")) + recordings:
        shutil.copy2(path, out / "assets" / path.name)
    for name in ("tour.mp4", "osdos-on-a-crt.jpg", "osdos-logo.svg"):
        shutil.copy2(IMAGES / name, out / "assets" / name)
    shutil.copy2(SCREENSHOTS / "arcade-list-3d-curved.png", out / "assets/arcade-screen.png")
    (out / "themes.json").write_text(json.dumps(landing_themes(data["looks"]), indent=1, ensure_ascii=False) + "\n")

    # OSD/OS Setup, under setup/: its own files, the app's font and logo, the
    # display presets, what its live preview draws with, and schema.json.
    setup = out / "setup"
    (setup / "js").mkdir(parents=True)
    for name in ("index.html", "style.css"):
        shutil.copy2(SETUP / name, setup / name)
    for path in (SETUP / "js").rglob("*.mjs"):
        target = setup / "js" / path.relative_to(SETUP / "js")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    copy_fonts(setup)
    write_icons(setup)
    (setup / "presets").mkdir()
    for path in PRESETS.glob("osdos-display-*.txt"):
        shutil.copy2(path, setup / "presets" / path.name)
    (setup / "shaders").mkdir()
    for path in SHADERS.glob("*.frag"):
        shutil.copy2(path, setup / "shaders" / path.name)
    # The skins' pictures, where the page reads them: the file's own path
    # isn't the page's business.
    parts = [p for skin in data["looks"]["skins"].values() for p in skin.values()]
    parts += [p for theme in data["looks"]["themes"].values() if isinstance(theme.get("skin"), dict)
              for p in theme["skin"].values()]
    for part in parts:
        target = setup / part["source"]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(part.pop("file"), target)
    (setup / "schema.json").write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n")
    # Served as it is, not through Jekyll.
    (out / ".nojekyll").write_text("")
    print(f"build-site: {out}")


if __name__ == "__main__":
    main()
