import QtQuick
import QtQuick.Window
import OSDOS.Video
import Components

Window {
    id: root
    flags: Qt.FramelessWindowHint | Qt.Window
    title: "OSD/OS"
    x:      Qt.platform.os === "osx" ? macScreenX      : Screen.virtualX
    y:      Qt.platform.os === "osx" ? macScreenY      : Screen.virtualY
    width:  Qt.platform.os === "osx" ? macScreenWidth  : Screen.width
    height: Qt.platform.os === "osx" ? macScreenHeight : Screen.height
    // macOS uses manual geometry (the target display chosen by the app-level
    // "display_index" setting, resolved in main.cpp) + a native fullscreen
    // call to keep the mpv-over-window layering intact. Everywhere else,
    // request true fullscreen so a desktop compositor's panel/dock (KDE on the
    // Steam Deck, labwc on the Pi) is covered rather than left stacked on top;
    // the Screen.* bindings track the window's screen, so they follow the
    // display_index move main.cpp performs after load. Headless EGLFS is
    // already fullscreen, so this is a no-op there.
    visibility: Qt.platform.os === "osx" ? Window.AutomaticVisibility
                                         : Window.FullScreen
    visible: true
    // Under everything: what OSD BACKGROUND puts around its window (black
    // but for FULL). OsdGround draws the ground itself, over it.
    color: root.osdBackground === "Full" ? root.surfaceColor : "#000000"

    // --- Color Schemes ---
    readonly property var schemes: ({
        "Video 1": {
            "primary": "#FFFFFF",
            "secondary": "#C2BFE4",
            "tertiary": "#8480C9",
            "surface": "#0110C5", // the blue of a VCR's on-screen menu
            "accent": "#AECFFF"
        },
        "Late Night": {
            "primary": "#FFFFFF",
            "secondary": "#A1A1A1",
            "tertiary": "#444444",
            "surface": "#000000",
            "accent": "#FFD900"
        },
        "Synthwave": {
            "primary": "#FFFFFF",
            "secondary": "#D48BFF",
            "tertiary": "#7836B5",
            "surface": "#12012B",
            "accent": "#00E5FF"
        },
        "Terminal": {
            "primary": "#4AF626",
            "secondary": "#32A81B",
            "tertiary": "#1A590E",
            "surface": "#000000",
            "accent": "#4AF626"
        },
        "T-120": {
            "primary": "#000000",
            "secondary": "#818181",
            "tertiary": "#df9c27",
            "surface": "#FAF5E8",
            "accent": "#EE442F"
        },
        "Amber": {
            "primary": "#FFB000",
            "secondary": "#B37B00",
            "tertiary": "#B37B00",
            "surface": "#000000",
            "accent": "#FFEE11"
        },
        "Kinescope": {
            "primary": "#FFFFFF",
            "secondary": "#9E9E9E",
            "tertiary": "#424242",
            "surface": "#121212",
            "accent": "#FFFFFF"
        },
        "SMPTE ECR 1-1978": {  // 75% max 0xFF == 0xBF, 40% max 0xFF == 0x66, 7.5% max 0xFF == 0x13; 75/7.5 targets per https://en.wikipedia.org/wiki/SMPTE_color_bars#Analog_NTSC - mixed with 40% in "off channels" to both wash out and improve contrast
            "primary": "#BFBFBF",
            "secondary": "#66BF66",
            "tertiary": "#6666BF",
            "surface": "#131313",
            "accent": "#BF6666"
        }
    })
    property var allSchemes: schemes  // may gain a "Custom" entry on startup
    // --- THE LOOK: Settings → Theme, and each of its parts ---
    // Settings' THEME (app.osd_theme, a theme's folder name; none when unset):
    // AppCore::theme()'s reading of it, {} for none. A theme brings the colour
    // scheme, the skin, the effects (text, background, selector, screen and
    // the transition between windows) and the menu music together, any of
    // them. Each part has its own row in Settings, as saved here: "" (Theme,
    // the default) the theme's, "Off" none, or another of its own choosing.
    // Choosing a theme sets them all back to Theme.
    property var theme: ({})
    // Settings' COLOR SCHEME (app.color_scheme): a scheme's name.
    property string currentScheme: ""
    // Settings' SKIN (app.skin, a skin's folder name; before skins had their
    // name, app.theme), and AppCore::skin()'s reading of it.
    property string skinSetting: ""
    property var skinChosen: ({})
    // Settings' effects (app.text_effect, background_effect, selector_effect,
    // screen_effect, transition): a preset's name.
    property string textEffectSetting: ""
    property string backgroundEffectSetting: ""
    property string selectorEffectSetting: ""
    property string screenEffectSetting: ""
    property string transitionSetting: ""
    // Settings' MENU MUSIC (app.menu_music): "", "Off" or "File", the file
    // being app.menu_music_file; MUSIC VOLUME (app.menu_music_volume).
    property string musicSetting: ""
    property string musicFile: ""
    property int musicVolume: 60

    // The colours in force: Settings' scheme, else the theme's, a scheme's
    // name or its own; Video 1 for none, or a name not there.
    readonly property var scheme: {
        var colors = currentScheme !== "" ? currentScheme : theme.colors
        if (colors === undefined || colors === "Off")
            return allSchemes["Video 1"]
        if (typeof colors === "string")
            return allSchemes[colors] || allSchemes["Video 1"]
        return colors.primary ? colors : allSchemes["Video 1"]
    }
    // The skin in force, Settings' or the theme's: pictures of the window's
    // parts (its frame, the title and hint bars, the selected line) that the
    // shared components draw in the scheme's two colours (SkinImage), each in
    // place of its own drawing, and icons in place of OSD/OS's (skinIcon()).
    readonly property var skin: skinSetting === "Off" ? ({})
                              : skinSetting !== "" ? skinChosen
                              : (theme.skin || ({}))
    // An icon's URL, or the skin's icon of its name: a module's by its
    // folder's name ("youtube"), any other by its file's ("settings").
    function skinIcon(url) {
        var icons = skin.icons
        if (!icons)
            return url
        var path = String(url)
        var module = path.match(/\/modules\/([^\/]+)\//)
        var name = module ? module[1] : path.replace(/^.*\//, "").replace(/\.[^.]*$/, "")
        return icons[name.toLowerCase()] || url
    }

    // The effects there are, by name, as Settings offers them. The screen's
    // and the text's are numbers the screen's shader takes
    // (shaders/effects.frag), each 0 (none) to 1; a background is a shader of
    // its own (shaders/bg-*.frag), drawn in the window or along its foot; the
    // selector's are SelectorFx's; the transitions, changeWindow()'s.
    readonly property var screenPresets: ({
        "Scanlines": { "scanlines": 0.5 },
        "CRT":       { "scanlines": 0.35, "curvature": 0.6, "glow": 0.35, "vignette": 0.5 },
        "VHS":       { "bleed": 0.6, "noise": 0.5, "glow": 0.25, "scanlines": 0.15 }
    })
    readonly property var textPresets: ({
        "Rainbow": { "rainbow": 1.0 },
        "Shimmer": { "shimmer": 1.0 },
        "Glow":    { "glow": 0.8 },
        "Flicker": { "flicker": 0.8 }
    })
    readonly property var backgroundPresets: ({
        "Matrix": { "shader": builtInShader("bg-matrix") },
        "Fire":   { "shader": builtInShader("bg-fire") },
        "Stars":  { "shader": builtInShader("bg-stars") },
        "Snow":   { "shader": builtInShader("bg-snow") }
    })
    readonly property var selectorPresets: ["Sparkles", "Welding", "Lightning", "Rainbow", "Snow"]
    readonly property var transitionPresets: ["Fade", "Cube", "Ripple", "Wave", "Drop"]
    // Cube Left, Right, Up and Down fixed the way the cube turned; it turns a
    // way of its own each time now, and those names, saved or a theme's, are
    // the cube.
    function cubeOf(name) { return /^Cube (Left|Right|Up|Down)$/.test(name) ? "Cube" : name }
    function builtInShader(name) { return appCore ? appCore.effectShader(name) : "" }

    // One effect in force: Settings' row unless it is Theme (""), else the
    // theme's; a preset's name read from `presets`, {} for none.
    function effectOf(setting, fromTheme, presets) {
        var e = setting !== "" ? setting : fromTheme
        if (e === undefined || e === "Off")
            return ({})
        return typeof e === "string" ? (presets[e] || ({})) : e
    }
    function nameOf(setting, fromTheme, names) {
        var e = setting !== "" ? setting : fromTheme
        return typeof e === "string" && names.indexOf(e) >= 0 ? e : ""
    }
    readonly property var themeEffects: theme.effects || ({})
    readonly property var screenEffect: effectOf(screenEffectSetting, themeEffects.screen, screenPresets)
    readonly property var textEffect: effectOf(textEffectSetting, themeEffects.text, textPresets)
    readonly property var backgroundEffect: effectOf(backgroundEffectSetting, themeEffects.background, backgroundPresets)
    readonly property string selectorEffect: nameOf(selectorEffectSetting, themeEffects.selector, selectorPresets)
    readonly property string transition: nameOf(transitionSetting, cubeOf(themeEffects.transition), transitionPresets)
    // The menu music in force: Settings' file (a path as it is: "file://"
    // before it would make a # or ? in its name a URL's), the theme's (a
    // URL), or none.
    readonly property string music: musicSetting === "Off" ? ""
                                  : musicSetting === "File" ? musicFile
                                  : (theme.music || "")

    // The shaders: the effects' as a URL each, "" where the build has none
    // (see CMakeLists.txt); and whether there is a GPU to run them on, the
    // software renderer drawing no shaders. Settings offers only what can be.
    readonly property string effectShader: builtInShader("effects")
    readonly property bool gpu: screen.gpu
    readonly property bool effectsUsable: effectShader !== "" && gpu
    readonly property string backgroundShader: gpu && backgroundEffect.shader ? backgroundEffect.shader : ""

    // --- WHEN THE EFFECTS REST ---
    // A video is never touched by an effect, nor its loading, nor its own
    // menu, nor anything of a player's: while one plays (in this window,
    // behind the menus or in an mpv process with the screen), loads
    // (LoadingScreen) or has its menu open (PlayerMenu), and while a video's
    // view is shown (a player's, a launch's: playbackView), the effects and
    // the menu music rest, and a window changes without its transition. The
    // layers say so through restFor(), the views through changeWindow().
    property var restLayers: []
    function restFor(layer, resting) {
        var layers = restLayers.filter(function(l) { return l !== layer })
        if (resting)
            layers.push(layer)
        restLayers = layers
    }
    readonly property var playbackView: /(Player|Play|Takeover|SignIn|Launch|Console)\.qml$/
    // The loaders in a module that show a video's view now. They die with
    // the module, so a new view in moduleLoader clears them.
    property var playbackLoaders: []
    // `loader` showing `source` (changeWindow()'s every change).
    function showView(loader, source, properties) {
        var loaders = loader === moduleLoader ? []
                      : playbackLoaders.filter(function(l) { return l !== loader })
        if (loader !== moduleLoader && playbackView.test(String(source)))
            loaders.push(loader)
        playbackLoaders = loaders
        loader.setSource(source, properties)
    }
    readonly property bool effectsRest: videoActive || screenHandedOff || restLayers.length > 0
                                        || playbackLoaders.length > 0
    // Seconds, for the effects that move; it counts only while one of them is
    // on and not resting, about thirty times a second.
    property real fxTime: 0
    readonly property bool fxMoving: !effectsRest && (backgroundShader !== ""
        || (screen.shaded && (screenEffect.noise > 0 || screenEffect.animate === true
                              || textEffect.rainbow > 0 || textEffect.shimmer > 0 || textEffect.flicker > 0)))
    Timer {
        interval: 33
        repeat: true
        running: root.fxMoving
        onTriggered: root.fxTime = (root.fxTime + interval / 1000) % 3600
    }

    // --- THE SELECTOR (Settings → Selector Effect) ---
    // Every selected line's box (SelectionBox) and every ground laid over a
    // view (OsdGround) say when they show, the latest last. The selector is
    // the latest box in front: inside the latest ground's layer, if any is
    // laid over the view, so a dialog's answer, not the line under it.
    property var selectors: []
    function selectorShown(box, shown) { listShown(box, shown, false) }
    function coverShown(ground, shown) { listShown(ground, shown, true) }
    function listShown(item, shown, cover) {
        var list = selectors.filter(function(e) { return e.item !== item })
        if (shown)
            list.push({ item: item, cover: cover })
        selectors = list
    }
    function isWithin(item, layer) {
        for (var p = item; p; p = p.parent) {
            if (p === layer)
                return true
        }
        return false
    }
    readonly property var selector: {
        var list = selectors
        var layer = null
        for (var i = list.length - 1; i >= 0 && !layer; --i) {
            if (list[i].cover)
                layer = list[i].item.parent
        }
        for (var j = list.length - 1; j >= 0; --j) {
            if (!list[j].cover && (!layer || isWithin(list[j].item, layer)))
                return list[j].item
        }
        return null
    }

    // Whether the selector is in a dialog's layer, over the view: its ground
    // would hide what plays behind the lines (the rainbow plays in front).
    readonly property bool selectorInLayer: {
        var list = selectors
        for (var i = list.length - 1; i >= 0; --i) {
            if (list[i].cover)
                return selector !== null && isWithin(selector, list[i].item.parent)
        }
        return false
    }

    // --- WINDOW CHANGES (Settings → Transition, or the theme's) ---
    // A window changing, the view a loader shows (Main.qml's moduleLoader, a
    // module's own) giving way to the next: the old one is caught as a
    // picture (lastFace) a moment before, and the two play out the change.
    // Fade: the old fades away over the new. Cube: the window turns over like
    // a cube's face, the new on the next face, a way at random each time
    // (left, right, up or down).
    // Ripple: the old ripples out as the new ripples in. Wave: a wave runs
    // out from a corner and dies away, the new window behind it. Drop: a drop
    // falls in a corner, the new window inside its spreading ring. Which
    // corner, at random. The last three are shaders, "" where there are none.
    readonly property var transitionShaders: ({
        "Ripple": builtInShader("ripple"),
        "Wave":   builtInShader("wave"),
        "Drop":   builtInShader("drop")
    })
    function transitionUsable(name) {
        return !(name in transitionShaders) || (transitionShaders[name] !== "" && gpu)
    }
    QtObject {
        id: windowChange
        // The change playing ("Fade", "Cube", "Ripple", "Wave", "Drop"), ""
        // none, and how far it is, 0 the old window to 1 the new.
        property string kind: ""
        property real progress: 0
        readonly property bool running: kind !== ""
        // The old window being caught, and the change waiting for it.
        property bool capturing: false
        property var pending: null
        // A change being made: a view made by it changing its own (a module's
        // first) changes at once.
        property bool changing: false
        // The cube's way: 0 the new face from the right (it turns left), 1
        // from the left, 2 from below (it turns up), 3 from above. A wave's or
        // a drop's corner: 0 top left, 1 top right, 2 bottom left, 3 bottom
        // right.
        property int direction: 0
        // The shader of the change playing, kept once it ends (a shader
        // effect without one would draw the default).
        property string shader: ""
        readonly property var faceMatrix: cubeMatrix(true)
        readonly property var lastMatrix: cubeMatrix(false)
        // Each face shown only while it is turned towards the eye: one
        // turned away is seen from inside the cube, where the other hides it.
        readonly property bool faceShown: kind !== "Cube" || facing(true)
        readonly property bool lastShown: kind !== "Cube" || facing(false)
        // How far in front of the screen the cube is seen from, in its edges.
        readonly property real distance: 2.5

        // One face of the cube as it turns, as a 4×4 matrix: the old window's
        // turning away, the new one's turning in. The cube's edge is the
        // screen's width (its height, turning up or down), its middle that far
        // behind the screen; its points are seen from two and a half widths in
        // front, and flattened onto the screen as Qt Quick draws in two
        // dimensions (as Rotation's projected turn is).
        function cubeMatrix(isNew) {
            if (kind !== "Cube")
                return Qt.matrix4x4()
            var across = direction < 2
            var w = across ? root.sw : root.sh
            var h = w / 2
            var turn = progress * Math.PI / 2
            var c = Math.cos(turn), s = Math.sin(turn)
            // Along the turn a point at t goes to a*t + b, at depth z*t + e.
            var a, b, z, e
            if (!isNew) {
                a = c; b = h - h * c - h * s; z = -s; e = h + h * s - h * c
            } else {
                a = s; b = h + h * c - h * s; z = c; e = h - h * s - h * c
            }
            // The other way round: mirrored.
            if (direction % 2 === 1) {
                b = w - a * w - b
                e = z * w + e
                z = -z
            }
            var d = distance * w
            var cx = root.sw / 2, cy = root.sh / 2
            if (across)
                return Qt.matrix4x4(a + cx * z / d, 0, 0, b + cx * e / d,
                                    cy * z / d,     1, 0, cy * e / d,
                                    0,              0, 0, 0,
                                    z / d,          0, 0, 1 + e / d)
            return Qt.matrix4x4(1, cx * z / d,     0, cx * e / d,
                                0, a + cy * z / d, 0, b + cy * e / d,
                                0, 0,              0, 0,
                                0, z / d,          0, 1 + e / d)
        }
        // Whether a face is turned towards the eye, in the middle of the
        // screen: the new one once the turn's sine, the old one while its
        // cosine, is past half an edge over the eye's distance from the
        // cube's middle.
        function facing(isNew) {
            var turn = progress * Math.PI / 2
            return (isNew ? Math.sin(turn) : Math.cos(turn)) * (2 * distance + 1) > 1
        }
    }
    NumberAnimation {
        id: transitionAnimation
        target: windowChange
        property: "progress"
        from: 0
        to: 1
        duration: windowChange.kind === "Cube" ? 650 : windowChange.kind === "Wave" ? 900
                  : windowChange.kind === "Drop" ? 1000 : 480
        // A wave runs at one speed; a drop's ring slows as it spreads.
        easing.type: windowChange.kind === "Wave" ? Easing.Linear
                     : windowChange.kind === "Drop" ? Easing.OutQuad : Easing.InOutQuad
        onFinished: windowChange.kind = ""
    }
    // A window that is never caught (nothing being drawn) changes all the same.
    Timer {
        id: catchTimeout
        interval: 250
        onTriggered: root.windowCaught(true)
    }

    // The view `loader` shows becomes `source` (with `properties`, as
    // Loader.setSource has them), as Settings → Transition has it. Never into
    // or out of a video's views (a player's, a launch's), nor while the
    // effects rest, nor at the very start.
    function changeWindow(loader, source, properties) {
        var from = String(loader.source), to = String(source)
        if (windowChange.capturing)
            windowCaught(true)
        if (transition === "" || windowChange.changing || effectsRest || bootActive || displayHolding
                || from === "" || playbackView.test(from) || playbackView.test(to)) {
            showView(loader, source, properties)
            return
        }
        if (windowChange.running)
            transitionAnimation.complete()
        windowChange.pending = function() { showView(loader, source, properties) }
        windowChange.capturing = true
        lastFace.scheduleUpdate()
        catchTimeout.restart()
    }
    // The old window caught (or given up on, `plain`): the change is made,
    // and plays out unless plain.
    function windowCaught(plain) {
        if (!windowChange.capturing)
            return
        catchTimeout.stop()
        windowChange.capturing = false
        var change = windowChange.pending
        windowChange.pending = null
        windowChange.changing = true
        change()
        windowChange.changing = false
        if (plain === true)
            return
        var kind = transition
        if (!transitionUsable(kind))
            kind = "Fade"
        windowChange.direction = Math.floor(Math.random() * 4)
        windowChange.shader = transitionShaders[kind] || ""
        windowChange.kind = kind
        windowChange.progress = 0
        transitionAnimation.restart()
    }

    // --- MENU MUSIC (Settings → Menu Music, or the theme's) ---
    // In the menus only: not while the effects rest, a script or a module's
    // own music plays (they hold it off themselves, MenuMusic), the boot
    // screen or the screen saver shows.
    Binding { target: menuMusic; property: "source"; value: root.music; when: !!menuMusic }
    Binding { target: menuMusic; property: "volume"; value: root.musicVolume; when: !!menuMusic }
    Binding {
        target: menuMusic
        property: "wanted"
        when: !!menuMusic
        value: root.music !== "" && !root.effectsRest && !root.bootActive && !root.screenSaverActive
               && !root.displayHolding
    }
    // Settings' OSD BACKGROUND (app.osd_background), what the menus are drawn
    // on (Components/OsdGround): "Full", the default, the scheme's background
    // over the whole screen; "Window", a framed window of it behind what a
    // view shows (osdWindow), black around it; "Off", none: black, the menus
    // in whichever of the scheme's two colours is the lighter, as a deck's OSD
    // with nothing playing. A scheme's dark text (T-120's) would vanish on
    // black, so Off takes its background colour for the text instead.
    property string osdBackground: "Full"
    readonly property bool osdOff: osdBackground === "Off"
    // Settings' WINDOW FRAME (app.osd_frame), offered with Window: "On", the
    // default, a line in the scheme's colour (or the skin's window image);
    // "Off", none; "Shadow", the line and a DOS window's shadow.
    property string osdFrame: "On"
    property string primaryColor:   osdOff ? lighterOf(scheme.primary, scheme.surface) : scheme.primary
    property string surfaceColor:   osdOff ? "#000000" : scheme.surface
    // Two colours only, like a deck's on-screen display: everything is drawn in
    // the scheme's primary colour on its surface colour. A selection is a solid
    // box with its text in the surface colour, and anything dimmed is dithered
    // (Components/Dither) rather than faded. The schemes' other three colours
    // stay in their definitions, unused.
    property string secondaryColor: primaryColor
    property string tertiaryColor:  primaryColor
    property string accentColor:    primaryColor

    readonly property real sw: width
    readonly property real sh: height
    // One pixel of a 240-line picture, in screen pixels: the unit the
    // pixel-drawn OSD elements (Components/Osd*, PixelIcon) are built on.
    readonly property int px: Math.max(1, Math.floor(sh / 240))

    // The area the views lay their content out in: the title bar's logo to
    // the hint bar (74 to 566 across and 57 to 430 down, of 640×480).
    readonly property rect contentBox: Qt.rect(sw * 0.115625, sh * 0.11875, sw * 0.76875, sh * 0.7770833)
    // OSD BACKGROUND's window: the content box with a margin on every side,
    // on art pixels.
    readonly property real osdMargin: sh * 0.025 //12
    readonly property rect osdWindow: {
        var left = snapPx(contentBox.x - osdMargin), right = snapPx(contentBox.x + contentBox.width + osdMargin)
        var top = snapPx(contentBox.y - osdMargin), bottom = snapPx(contentBox.y + contentBox.height + osdMargin)
        return Qt.rect(left, top, right - left, bottom - top)
    }
    function snapPx(v) { return Math.round(v / px) * px }
    // The room Settings' Hint Bar and Help Line leave when they are off, which
    // the content takes; the window stays as it is. A menu's list grows a row
    // (menuRowHeight) for the hint bar (hintRoom) and, over a help line, a row
    // for it (helpRoom): ten rows at most, its ▼ inside the content box. A help
    // line moves down into the hint bar's place (helpLineMargin), a tree
    // reaches the content box's foot (TreeBrowser's areaBottom), a dialog
    // centres in the taller space (PromptScreen).
    readonly property real menuRowHeight: sh * 0.0583333 //28
    readonly property real hintRoom: hintBar ? 0 : menuRowHeight
    readonly property real helpRoom: helpLine ? 0 : menuRowHeight
    readonly property real helpLineMargin: hintBar ? sh * 0.1583333 : sh * 0.1041667 //76, or the hint bar's 50

    // A time as the players show it: h:mm:ss, or m:ss under an hour.
    function formatTime(ms) {
        var s = Math.floor(ms / 1000)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60
        return (h > 0 ? h + ":" + pad(m) : m) + ":" + pad(sec)
    }
    function pad(n) { return n < 10 ? "0" + n : "" + n }

    // The lighter of two colours, by how bright the eye finds them.
    function lighterOf(a, b) {
        function luma(c) { var k = Qt.color(c); return 0.2126 * k.r + 0.7152 * k.g + 0.0722 * k.b }
        return luma(a) >= luma(b) ? a : b
    }

    // A look setting saved, or read at start: kept in its property. False for
    // a key that isn't one.
    function applyLook(key, value) {
        var text = value === undefined || value === null ? "" : String(value)
        switch (key) {
        case "osd_theme":
            theme = appCore.theme(text)
            break
        case "color_scheme":
            currentScheme = text
            break
        case "skin":
            skinSetting = text
            skinChosen = text !== "" && text !== "Off" ? appCore.skin(text) : ({})
            break
        case "text_effect":       textEffectSetting = text; break
        case "background_effect": backgroundEffectSetting = text; break
        case "selector_effect":   selectorEffectSetting = text; break
        case "screen_effect":     screenEffectSetting = text; break
        case "transition":        transitionSetting = text; break
        case "menu_music":        musicSetting = text; break
        case "menu_music_file":   musicFile = text; break
        case "menu_music_volume":
            var volume = parseInt(text)
            musicVolume = isNaN(volume) ? 60 : Math.max(0, Math.min(100, volume))
            break
        default:
            return false
        }
        return true
    }
    readonly property var lookKeys: ["osd_theme", "color_scheme", "skin", "text_effect", "background_effect",
                                     "selector_effect", "screen_effect", "transition", "menu_music",
                                     "menu_music_file", "menu_music_volume"]

    Connections {
        target: appCore
        function onAppSettingChanged(key, value) {
            if (root.applyLook(key, value)) {
                return
            } else if (key === "osd_frame") {
                root.osdFrame = root.osdFrameOf(value)
            } else if (key === "transparent_background") {
                root.backdropSolidity = root.solidityOf(value)
            } else if (key === "loading_effect") {
                root.loadingEffect = value !== "Off"
            } else if (key === "boot_colors") {
                root.bootThemed = value !== "Default"
            } else if (key === "loading_colors") {
                root.loadingThemed = value !== "Default"
            } else if (key === "hint_bar") {
                root.hintBar = value !== "Off"
            } else if (key === "help_line") {
                root.helpLine = value !== "Off"
            } else if (key === "osd_background") {
                root.osdBackground = root.osdBackgroundOf(value)
            } else if (key === "mouse_pointer") {
                root.pointerSetting = String(value)
                if (root.pointerShown)
                    pointerTimer.restart()
            } else if (key === "screensaver_timeout") {
                var sec = parseInt(value)
                if (sec > 0) {
                    idleTracker.threshold = sec
                    idleTracker.enabled = true
                } else {  // "OFF"
                    idleTracker.enabled = false
                    if (screenSaverActive) screenSaverActive = false
                }
            }
        }
    }

    Component.onCompleted: {
        var cfg = appCore.get_settings()

        var cSchemes = appCore.getCustomColorSchemes()
        if (Object.keys(cSchemes).length > 0) {
            var s = Object.assign({}, schemes, root.allSchemes)
            for (var cScheme in cSchemes) {
                if (Object.keys(cSchemes[cScheme]).length === 5) {
                    s[cScheme] = cSchemes[cScheme]
                }
            }
            root.allSchemes = s
        }

        var custom = appCore.getCustomColorScheme()
        if (Object.keys(custom).length === 5) {
            var s = Object.assign({}, schemes, root.allSchemes)
            s["Custom"] = custom
            root.allSchemes = s
        }

        var app = cfg.app || {}
        if (app.color_scheme === "Custom" && !root.allSchemes["Custom"]) {
            appCore.save_setting("", "color_scheme", "")
            app.color_scheme = ""
        }
        // A theme or a skin gone from its folder reads as none. A skin chosen
        // before skins had their name was saved as app.theme.
        if (app.skin === undefined && app.theme)
            app.skin = app.theme
        var cube = root.cubeOf(app.transition)
        if (cube !== app.transition) {
            appCore.save_setting("", "transition", cube)
            app.transition = cube
        }
        for (var k = 0; k < root.lookKeys.length; ++k)
            root.applyLook(root.lookKeys[k], app[root.lookKeys[k]])
        root.osdFrame = root.osdFrameOf(cfg.app && cfg.app.osd_frame)
        root.backdropSolidity = root.solidityOf(cfg.app && cfg.app.transparent_background)
        root.pointerSetting = String((cfg.app && cfg.app.mouse_pointer) || "5")
        root.loadingEffect = !(cfg.app && cfg.app.loading_effect === "Off")
        root.bootThemed = !(cfg.app && cfg.app.boot_colors === "Default")
        root.loadingThemed = !(cfg.app && cfg.app.loading_colors === "Default")
        root.hintBar = !(cfg.app && cfg.app.hint_bar === "Off")
        root.helpLine = !(cfg.app && cfg.app.help_line === "Off")
        root.osdBackground = root.osdBackgroundOf(cfg.app && cfg.app.osd_background)

        // Screensaver: the tracker starts disabled; this is the single place the
        // saved setting is applied (live changes land in onAppSettingChanged above,
        // mirroring color_scheme). parseInt("OFF") is NaN, so OFF stays disabled.
        var ssSec = parseInt(cfg.app && cfg.app.screensaver_timeout)
        if (ssSec > 0) {
            idleTracker.threshold = ssSec
            idleTracker.enabled = true
        }

        // Break declarative bindings on macOS so the C++ NSWindow override
        // in forceWindowFullScreenOnScreen() isn't immediately re-fought by QML.
        if (Qt.platform.os === "osx") {
            root.x = macScreenX
            root.y = macScreenY
            root.width = macScreenWidth
            root.height = macScreenHeight
        }
    }
    
    FontLoader {
        id: font; source: "assets/fonts/VCR_OSD_MONO_1.001.ttf"
    }
    // Unifont fills in glyphs VCR OSD Mono doesn't have (CJK, Hangul, etc.),
    // it's a bitmap-style font so it actually matches the retro CRT look
    // instead of a mismatched serif/sans fallback. This Qt build's font value
    // type has no font.families (checked plugins.qmltypes: family only), so
    // loading it here just registers it with Qt's fontconfig-backed font
    // database; Qt's own missing-glyph fallback then picks it up for every
    // existing font.family: root.globalFont binding with no further changes.
    FontLoader {
        id: unifontLoader; source: "assets/fonts/unifont.otf"
    }
    property string globalFont: font.name;

    // --- INPUT / APP INFO MIRRORS ---
    // Views must bind these via `root.*`, never the appCore/inputManager
    // context properties directly: when the module Loader swaps views, the
    // dying view's context properties resolve to null and any binding on them
    // throws a TypeError during teardown. id-resolved `root.*` stays valid
    // (root lives as long as the app), so these mirrors are teardown-safe.
    // The null guards absorb the same nulling here at app shutdown, when the
    // engine invalidates the root context itself.
    readonly property var hints: inputManager ? inputManager.hints : ({})
    readonly property string appVersion: appCore ? appCore.appVersion : ""
    readonly property string appBuild: appCore ? appCore.appBuild : ""

    // --- BOOT SCREEN (OSD/OS image only, see os/README.md) ---
    // bootProgress mirrors, for the same teardown-safety reason as above.
    readonly property bool   bootActive: bootProgress ? bootProgress.active : false
    readonly property real   bootValue:  bootProgress ? bootProgress.progress : 0
    readonly property var    bootSteps:  bootProgress ? bootProgress.steps : []
    readonly property string bootLabel:  bootProgress ? bootProgress.currentLabel : ""

    // The startup module waits for the boot screen: most modules need the
    // network the boot screen is waiting on.
    onBootActiveChanged: {
        if (bootActive || displayHolding) return
        if (moduleLoader.item) moduleLoader.item.forceActiveFocus()
        openStartupModule()
    }

    // --- DISPLAY OUTPUT (Settings → Display Output) ---
    // displayOutput mirrored, as bootProgress is, for the same teardown-safety.
    readonly property bool   displayConfirmPending: displayOutput ? displayOutput.confirmPending : false
    readonly property string displayNotice:         displayOutput ? displayOutput.notice : ""
    readonly property string displayNoticeDetail:   displayOutput ? displayOutput.noticeDetail : ""
    readonly property string displayCurrentLabel:   displayOutput ? displayOutput.currentLabel : ""
    readonly property string displayPreviousLabel:  displayOutput ? displayOutput.previousLabel : ""
    // On a new output, the window asking to keep it (views/DisplayKeep.qml)
    // comes first, over the boot screen; the startup module waits for it, a
    // favourite played at startup with it.
    readonly property bool   displayHolding: displayConfirmPending || displayNotice !== ""
    onDisplayHoldingChanged: {
        if (displayHolding) return
        if (bootActive) {
            if (bootScreenLoader.item) bootScreenLoader.item.forceActiveFocus()
            return
        }
        if (moduleLoader.item) moduleLoader.item.forceActiveFocus()
        openStartupModule()
    }

    // --- SCREEN SAVER STATE ---
    property bool screenSaverActive: false

    // Playback counts as user activity even when no key started it (an NFC
    // card tap launches mpv directly). If the saver was showing at launch,
    // nothing key-driven ever reaches its dismiss handler — focus has moved
    // into the module's player view — so clear it on playback windowChange.
    function dismissScreenSaver() {
        if (!screenSaverActive) return
        screenSaverActive = false
        moduleLoader.forceActiveFocus()
    }

    // --- APP-LEVEL NAV STACK ---
    property var appNavStack: []
    property var appCurrentParams: ({})
    property bool _startupNavigated: false

    // Opens the configured startup module, once per run. A favourite chosen to
    // PLAY AT STARTUP (EntryOptions) comes first: its module opens and plays
    // it straight away (navParams.startupPlay), as long as it is still one of
    // that module's favourites.
    function openStartupModule() {
        if (root._startupNavigated || root.displayHolding) return
        root._startupNavigated = true
        var params = { fromAppStartup: true }
        var entryPoint = ""
        var startup = appCore.get_setting("", "startup_favorite")
        if (startup && startup.module && startup.path) {
            var favorites = appCore.get_list(startup.module, "favorites")
            for (var i = 0; i < favorites.length; ++i) {
                if (favorites[i].path === startup.path) {
                    entryPoint = appCore.moduleEntryPoint(startup.module)
                    params.startupPlay = favorites[i]
                    break
                }
            }
        }
        if (!entryPoint) {
            delete params.startupPlay
            entryPoint = appCore.startupModuleEntryPoint()
        }
        if (entryPoint) {
            root.appNavStack.push({
                source: moduleLoader.source,
                params: root.appCurrentParams,
                listState: {}
            })
            moduleLoader.setSource(entryPoint, { "navParams": params })
        }
    }

    // --- MPV PLAYBACK TRACKING ---
    // Block the screen saver while mpv is playing so it never flashes during or
    // immediately after playback. The core guard is in IdleTracker (mpvActive
    // property), which also resets the idle timer on windowChange.
    Connections {
        target: mpvController
        function onPositionChanged(ms) {
            if (ms > 0 && !idleTracker.mpvActive) {
                idleTracker.mpvActive = true
                idleTracker.resetActivity()
                root.dismissScreenSaver()
            }
        }
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            // A video left playing behind the menus is still playing.
            idleTracker.mpvActive = mpvController.videoActive
            idleTracker.resetActivity()
            root.dismissScreenSaver()
        }
        function onVideoActiveChanged() {
            idleTracker.mpvActive = mpvController.videoActive
            idleTracker.resetActivity()
        }
    }

    // --- VIDEO PLAYED INSIDE THIS WINDOW (Transparent Background) ---
    // MpvController plays it here rather than in an mpv window of its own
    // while the setting is on: its picture lies over everything while it
    // plays full screen, and under the menus once back has returned to them,
    // where it goes on playing (videoBehind). The menus draw no background of
    // their own, so the picture is theirs then; full-screen dialogs keep
    // theirs. The setting's slider says how solid their ground is over it.
    readonly property bool videoActive: mpvController ? mpvController.videoActive : false
    readonly property bool videoBehind: mpvController ? mpvController.background : false
    // What its player noted of the video behind the menus ({ module, title,
    // params }, see MpvController::noteSession): the main menu offers it
    // back as its first row. Empty for a player that notes nothing.
    readonly property var behindNote: mpvController ? mpvController.backgroundNote : ({})
    // A video its menu's Browse left while an mpv process had the screen
    // (Transparent Background off): stopped where its player saved it.
    readonly property var leftNote: mpvController ? mpvController.leftNote : ({})
    // The video the main menu's first row takes back: the one behind the
    // menus, or one left with Browse, which its player starts again where it
    // was, without asking.
    readonly property var takeBackNote: videoBehind ? behindNote : leftNote
    property int backdropSolidity: 100
    // Another process has the screen: mpv, or a script, on the Pi's console
    // (see DisplayHandoff). Nothing drawn here reaches it until it is back,
    // so what animates rests meanwhile (LoadingScreen).
    readonly property bool screenHandedOff: displayHandoff ? displayHandoff.held : false
    // "loading_effect": the tape's noise and bands on the screen a video
    // loads behind (LoadingScreen), "On" (the default, when unset) or "Off".
    property bool loadingEffect: true
    // "hint_bar": the key hints on the bar at the foot of every screen
    // (HintBar), "On" (the default, when unset) or "Off".
    property bool hintBar: true
    // "help_line": the line about the selected row in the box under a menu
    // (HelpLine), "On" (the default, when unset) or "Off".
    property bool helpLine: true
    // "boot_colors", "loading_colors": the boot screen's and the loading
    // screen's colours, the ones in force like every screen's ("Theme", the
    // default, when unset) or OSD/OS's own, Video 1's white on a VCR's blue
    // ("Default"), whatever the theme or colour scheme.
    property bool bootThemed: true
    property bool loadingThemed: true
    readonly property string bootInk: bootThemed ? primaryColor : schemes["Video 1"].primary
    readonly property string bootPaper: bootThemed ? surfaceColor : schemes["Video 1"].surface
    readonly property string loadingInk: loadingThemed ? primaryColor : schemes["Video 1"].primary
    readonly property string loadingPaper: loadingThemed ? surfaceColor : schemes["Video 1"].surface

    // "transparent_background": how solid the menus' ground is over a video
    // behind them, 0 (TRANSPARENT) to 100 (SOLID: none of it shows, but it
    // plays on, sound and all), or "Off", the default when unset (back stops
    // the video, as it always has). Its first values were words: On (0) and
    // Dim (60). Read as MpvController::transparentBackground() reads it.
    function backgroundOn(raw) {
        var s = String(raw === undefined || raw === null ? "" : raw).trim().toLowerCase()
        return s === "on" || s === "dim" || !isNaN(parseInt(s))
    }
    function osdBackgroundOf(raw) {
        return raw === "Window" || raw === "Off" ? raw : "Full"
    }
    function osdFrameOf(raw) {
        return raw === "Off" || raw === "Shadow" ? raw : "On"
    }
    function solidityOf(raw) {
        var s = String(raw === undefined || raw === null ? "" : raw).toLowerCase()
        if (s === "on") return 0
        if (s === "dim") return 60
        var n = parseInt(s)
        return isNaN(n) ? 100 : Math.max(0, Math.min(100, n))
    }

    // --- A VIDEO PLAYED INSIDE THIS WINDOW (Transparent Background) ---
    // Its picture is never touched by an effect: it lies outside the screen's
    // layer below, over the menus while it plays full screen, under them while
    // it plays on behind them.
    VideoSurface {
        anchors.fill: parent
        controller: mpvController
        visible: root.videoActive
        z: root.videoBehind ? -1 : 1
    }

    // --- THE SCREEN, AND ITS EFFECTS ---
    // All the screen shows but a video is drawn in here: the window (the menus
    // on their ground, the selector's effect), the boot screen, the screen
    // saver, the pointer. With a screen or text effect in force (Settings →
    // Screen Effect and Text Effect, or the theme's), it is drawn into one
    // texture first, and that through one shader on the GPU on its way to the
    // screen (shaders/effects.frag, or a theme's own): scanlines, a picture
    // tube's curve, glow, colour bleed, noise, a vignette, and the ink in the
    // rainbow, glinting, flickering or glowing. With none, or while the
    // effects rest (root.effectsRest), neither the texture nor the shader is
    // there.
    Item {
        id: screen
        anchors.fill: parent

        // The software renderer draws no shaders.
        readonly property bool gpu: GraphicsInfo.api !== GraphicsInfo.Software
        // The shader: the theme's own, unless it won't compile, else the
        // built-in one; none once that won't either. A theme's own that
        // wouldn't is tried again once it is chosen again.
        readonly property string ownShader: root.screenEffect.shader || ""
        property bool ownShaderFailed: false
        onOwnShaderChanged: ownShaderFailed = false
        property bool shaderFailed: false
        readonly property string shader: ownShader !== "" && !ownShaderFailed ? ownShader : root.effectShader
        readonly property bool shaded: gpu && shader !== "" && !shaderFailed && !root.effectsRest
            && (ownShader !== ""
                || ["scanlines", "curvature", "glow", "bleed", "noise", "vignette"]
                       .some(function(knob) { return root.screenEffect[knob] > 0 })
                || ["rainbow", "shimmer", "flicker", "glow"]
                       .some(function(knob) { return root.textEffect[knob] > 0 }))

        layer.enabled: shaded
        layer.smooth: true
        layer.effect: ShaderEffect {
            fragmentShader: screen.shader
            property size resolution: Qt.size(root.sw, root.sh)
            property real px: root.px
            property real time: root.fxTime
            property real scanlines: root.screenEffect.scanlines || 0
            property real curvature: root.screenEffect.curvature || 0
            property real glow: root.screenEffect.glow || 0
            property real bleed: root.screenEffect.bleed || 0
            property real noise: root.screenEffect.noise || 0
            property real vignette: root.screenEffect.vignette || 0
            property real rainbow: root.textEffect.rainbow || 0
            property real shimmer: root.textEffect.shimmer || 0
            property real flicker: root.textEffect.flicker || 0
            property real inkGlow: root.textEffect.glow || 0
            property color ink: root.primaryColor
            property color paper: root.surfaceColor
            onStatusChanged: {
                if (status !== ShaderEffect.Error)
                    return
                var own = screen.shader !== root.effectShader
                console.warn("[Effect] " + fragmentShader + " can't be used" + (log ? ": " + log : "")
                             + (own ? ", OSD/OS's own in its place" : ", none"))
                // Once the shader is set: it is being set now.
                Qt.callLater(function() {
                    if (own)
                        screen.ownShaderFailed = true
                    else
                        screen.shaderFailed = true
                })
            }
        }

        // The window's colour, under everything, in the texture with the rest.
        Rectangle {
            anchors.fill: parent
            z: -3
            visible: screen.shaded
            color: root.color
        }

        // Behind a window turning over (the cube) or rippling, the dark.
        Rectangle {
            anchors.fill: parent
            z: -2
            visible: windowChange.kind === "Cube" || windowChange.kind === "Ripple"
            color: "#000000"
        }

        // --- THE WINDOW ---
        // What a view shows, on its ground (OSD BACKGROUND), with the
        // selector's effect round the selected line: the face a window change
        // turns away, lastFace keeping a picture of it as it was.
        Item {
            id: face
            anchors.fill: parent
            transform: Matrix4x4 { matrix: windowChange.faceMatrix }
            // Not hidden (visible: false): a view keeps its focus.
            opacity: windowChange.faceShown ? 1 : 0

            // What the menus are drawn on (OSD BACKGROUND), under every view.
            // Over a video behind them, as solid as Transparent Background
            // says, and in WINDOW only the window, the picture showing whole
            // around it.
            OsdGround {
                anchors.fill: parent
                z: -1
                cover: false
                visible: !root.videoBehind || root.backdropSolidity > 0
                opacity: root.videoBehind ? root.backdropSolidity / 100 : 1
                surround: !root.videoBehind
            }

            // The selector's effect (Settings → Selector Effect, or the
            // theme's): the rainbow runs down from under the selected line,
            // behind the lines below it (in front, in a dialog, whose ground
            // would hide it); the rest play in front of everything.
            SelectorFx {
                anchors.fill: parent
                z: -0.5
                target: root.selector
                effect: root.selectorEffect === "Rainbow" && !root.selectorInLayer ? "Rainbow" : ""
                ink: root.primaryColor
                pixel: root.px
                running: !root.effectsRest
            }

            // A running user script suppresses the screen saver too — a takeover script
            // owns the display, and even a console one is legitimately silent for as long
            // as it takes. Its own flag rather than reusing mpvActive, so ending one
            // session can't unblock the saver while the other is still going.
            Connections {
                target: scriptsBackend
                function onScriptRunningChanged() {
                    idleTracker.scriptActive = scriptsBackend.scriptBusy
                    idleTracker.resetActivity()
                    if (!scriptsBackend.scriptBusy)
                        root.dismissScreenSaver()
                }
            }

            // --- MODULE LOADER ---
            Loader {
                id: moduleLoader;
                anchors.fill: parent;
                focus: true;
                source: "views/ModuleList.qml";

                // Playback follows the open module's own settings where it has them
                // (its Scaling).
                onSourceChanged: mpvController.setActiveModule(appCore.moduleIdForSource(source.toString()))

                Keys.onPressed: (event) => {
                    if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Q) {
                        Qt.quit()
                    }
                }

                onLoaded: {
                    // While the boot screen is up it keeps the focus; QML gives no
                    // order between this and its own onLoaded, so don't race it. Nor
                    // the window asking to keep a new display output.
                    if (root.bootActive || root.displayHolding)
                        return
                    item.forceActiveFocus()
                    root.openStartupModule()
                }

                Connections {
                    target: moduleLoader.item
                    ignoreUnknownSignals: true

                    function onNavigateTo(path, params, listState) {
                        root.appNavStack.push({ source: moduleLoader.source, params: root.appCurrentParams, listState: listState || {} })
                        root.appCurrentParams = params || {}
                        root.changeWindow(moduleLoader, path, { "navParams": params || {} })
                    }

                    function onGoBack() {
                        if (root.appNavStack.length === 0) return
                        var prev = root.appNavStack.pop()
                        root.appCurrentParams = prev.params
                        root.changeWindow(moduleLoader, prev.source,
                                          { "navParams": prev.params, "navListState": prev.listState || {} })
                    }

                }
            }

            SelectorFx {
                anchors.fill: parent
                z: 1
                target: root.selector
                effect: root.selectorEffect !== "Rainbow" || root.selectorInLayer ? root.selectorEffect : ""
                ink: root.primaryColor
                paper: root.surfaceColor
                pixel: root.px
                running: !root.effectsRest
            }
        }

        // --- WINDOW CHANGES (Settings → Transition, or the theme's) ---
        // The window as it was, caught a moment before the next comes in
        // (changeWindow()): under the window while it is caught, then turned
        // away with it (the cube), faded out over it (fade), or rippled into
        // the next (ripple).
        ShaderEffectSource {
            id: lastFace
            anchors.fill: parent
            sourceItem: face
            live: false
            visible: windowChange.capturing || windowChange.running
            z: windowChange.kind === "Fade" ? 0.5 : -1.5
            opacity: windowChange.kind === "Fade" ? 1 - windowChange.progress : windowChange.lastShown ? 1 : 0
            transform: Matrix4x4 { matrix: windowChange.lastMatrix }
            onScheduledUpdateCompleted: root.windowCaught()
        }
        // The next window, as a picture, while a shader brings it in.
        ShaderEffectSource {
            id: nextFace
            anchors.fill: parent
            sourceItem: windowChange.kind in root.transitionShaders ? face : null
            hideSource: windowChange.kind in root.transitionShaders
            visible: false
        }
        Loader {
            anchors.fill: parent
            z: 0.6
            active: windowChange.kind in root.transitionShaders
            sourceComponent: ShaderEffect {
                fragmentShader: windowChange.shader
                property variant from: lastFace
                property variant to: nextFace
                property real progress: windowChange.progress
                property size resolution: Qt.size(root.sw, root.sh)
                property real px: root.px
                property point corner: Qt.point(windowChange.direction % 2, Math.floor(windowChange.direction / 2))
                property color ink: root.primaryColor
            }
        }

        // --- SCREEN SAVER (Idle Tracker integration) ---
        Connections {
            target: idleTracker
            function onActiveChanged() {
                // Only show on active → true; never hide here — the overlay's
                // key handler owns dismissal, preventing the C++ event filter's
                // synchronous reset from stealing the key from QML. Never over the
                // boot screen, which would lose focus to it.
                if (idleTracker.active && idleTracker.enabled && !root.bootActive) {
                    if (!screenSaverActive) {
                        var usableW = screenSaverOverlay.width - bounceLogo.width
                        var usableH = screenSaverOverlay.height - bounceLogo.height
                        bounceLogo.x = Math.random() * (usableW > 0 ? usableW : 1)
                        bounceLogo.y = Math.random() * (usableH > 0 ? usableH : 1)
                        bounceLogo.vx = (Math.random() > 0.5 ? 1 : -1) * (1 + Math.random() * 1.5)
                        bounceLogo.vy = (Math.random() > 0.5 ? 1 : -1) * (1 + Math.random() * 1.5)
                        screenSaverActive = true
                        screenSaverOverlay.forceActiveFocus()
                    }
                }
            }
        }

        // Above the module views, below the screen saver. Declared after
        // moduleLoader so its focus grab wins over the first view's.
        Loader {
            id: bootScreenLoader
            anchors.fill: parent
            z: 9000
            active: root.bootActive
            source: "views/BootScreen.qml"
            onLoaded: if (!root.displayHolding) item.forceActiveFocus()
        }

        // Above the boot screen: on a new display output, keep it or go back.
        Loader {
            id: displayKeepLoader
            anchors.fill: parent
            z: 9500
            active: root.displayHolding
            source: "views/DisplayKeep.qml"
            onLoaded: item.forceActiveFocus()
        }

        Item {
            id: screenSaverOverlay
            anchors.fill: parent
            visible: screenSaverActive
            z: 9999
            focus: visible

            // Solid black background — no transparency so it serves as a true
            // CRT burn-in prevention black frame between the logo bounces.
            Rectangle {
                anchors.fill: parent
                color: "#000000"
            }

            // Bouncing logo — classic DVD player screen saver
            Image {
                id: bounceLogo
                source: "assets/images/logo.svg"
                sourceSize.width: root.sw * 0.05
                sourceSize.height: root.sw * 0.05
                fillMode: Image.PreserveAspectFit
                antialiasing: true

                property real vx: 0
                property real vy: 0

                // Physics tick at ~60 fps while the overlay is visible
                Timer {
                    interval: 16
                    repeat: true
                    running: screenSaverActive
                    onTriggered: {
                        bounceLogo.x += bounceLogo.vx
                        bounceLogo.y += bounceLogo.vy

                        if (bounceLogo.x + bounceLogo.width > screenSaverOverlay.width) {
                            bounceLogo.x = screenSaverOverlay.width - bounceLogo.width
                            bounceLogo.vx = -Math.abs(bounceLogo.vx)
                        } else if (bounceLogo.x < 0) {
                            bounceLogo.x = 0
                            bounceLogo.vx = Math.abs(bounceLogo.vx)
                        }

                        if (bounceLogo.y + bounceLogo.height > screenSaverOverlay.height) {
                            bounceLogo.y = screenSaverOverlay.height - bounceLogo.height
                            bounceLogo.vy = -Math.abs(bounceLogo.vy)
                        } else if (bounceLogo.y < 0) {
                            bounceLogo.y = 0
                            bounceLogo.vy = Math.abs(bounceLogo.vy)
                        }
                    }
                }
            }

            // Capture any keypress to dismiss — consumes the event so the
            // underlying view never sees it, preventing accidental navigation.
            // Ctrl+Q still quits (moduleLoader's handler is a sibling, so it
            // can't see keys focused here — handle the chord directly).
            Keys.onPressed: (event) => {
                event.accepted = true
                if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Q) {
                    Qt.quit()
                    return
                }
                screenSaverActive = false
                moduleLoader.forceActiveFocus()
            }
        }

        // The mouse pointer (MOUSE POINTER, below), over everything.
        MousePointer {
            id: pointer
            z: 20001
            visible: root.pointerShown && root.pointerSetting !== "off"
        }
    }

    // --- MOUSE POINTER ---
    // Qt's own pointer stays hidden (main.cpp: on a headless screen it is a
    // hardware cursor that wouldn't stay hidden). This one is drawn in the
    // OSD's pixels, over everything: it shows as the mouse moves and goes
    // again after Settings' MOUSE POINTER seconds without moving
    // (app.mouse_pointer: "off", "always" or seconds; 5 when unset). Moving
    // the mouse counts as being there, for the screen saver too. The menus
    // themselves go by keys.
    property string pointerSetting: "5"
    property bool pointerShown: false

    MouseArea {
        anchors.fill: parent
        z: 20000
        enabled: root.pointerSetting !== "off"
        hoverEnabled: true
        // Only follows the mouse: clicks and the wheel go on to what is under it.
        acceptedButtons: Qt.NoButton
        onPositionChanged: function(mouse) {
            pointer.x = mouse.x
            pointer.y = mouse.y
            root.pointerShown = true
            pointerTimer.restart()
            idleTracker.resetActivity()
            root.dismissScreenSaver()
        }
    }

    Timer {
        id: pointerTimer
        interval: (parseInt(root.pointerSetting) || 5) * 1000
        onTriggered: {
            if (root.pointerSetting !== "always")
                root.pointerShown = false
        }
    }
}
