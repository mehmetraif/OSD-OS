import QtQuick
import Components

// Main.qml's root for the QML tests (qml/tst_*.qml): what the views and the
// shared components they are made of read of it as root.*, each as Main.qml
// has it on a 640×480 screen in the Standard layout with Settings'
// defaults, but white on black, with nothing moving (the effects rest) and
// no selector effect. A test declares one with `id: root`, which the views
// it loads find as they find Main.qml's root, sets in it what it has
// otherwise (another layout, colours of its own) and adds its own
// stand-ins beside it. The window's changes (WindowChange) are Main.qml's
// own, and stood in for by their test alone.
Rectangle {
    id: root
    color: surfaceColor
    width: 640
    height: 480

    // The stage the menus are laid out on, and one pixel of a 240-line
    // picture (Main.qml works it out from sh: 2 on 480 lines).
    property real sw: width
    property real sh: height
    property real px: 2
    property string globalFont: "monospace"

    // --- THE COLOURS ---
    // The scheme's two, and the two every view draws in: a monitor layout's
    // own while its frame is in force (monitorInk), else the scheme's.
    property string schemeInk: "#ffffff"
    property string schemePaper: "#000000"
    property string primaryColor: monitorInk ? menuPalette.ink : schemeInk
    property string surfaceColor: monitorInk ? menuPalette.ground : schemePaper
    // Deprecated, for third-party modules: the primary colour, read-only.
    readonly property alias secondaryColor: root.primaryColor
    readonly property alias tertiaryColor: root.primaryColor
    readonly property alias accentColor: root.primaryColor
    // A selected cell's box and what is written in it.
    property string cursorColor: !monitorInk ? primaryColor
                                 : menuLayout === "Flat Panel" ? menuPalette.bar : menuPalette.mark
    property string cursorInk: !monitorInk ? surfaceColor
                               : menuLayout === "Flat Panel" ? menuPalette.ink : menuPalette.ground
    // The loading screen's, the theme's like every screen's, or OSD/OS's
    // own (Video 1's).
    readonly property var schemes: ({ "Video 1": { primary: "#FFFFFF", secondary: "#C2BFE4", tertiary: "#8480C9",
                                                   surface: "#0110C5", accent: "#AECFFF" } })
    property bool loadingThemed: true
    readonly property string loadingInk: loadingThemed ? schemeInk : schemes["Video 1"].primary
    readonly property string loadingPaper: loadingThemed ? schemePaper : schemes["Video 1"].surface
    // The letters' bleed: the scheme's second ink (the stand-in's scheme has
    // none but its own), or Video 1's.
    readonly property string loadingBleed: loadingThemed ? schemeInk : schemes["Video 1"].secondary
    // No skin: the components' own drawing, and OSD/OS's own icons.
    property var skin: ({})
    function skinIcon(url) {
        var icons = skin.icons
        if (!icons)
            return url
        var path = String(url)
        var module = path.match(/\/modules\/([^\/]+)\//)
        var name = module ? module[1] : path.replace(/^.*\//, "").replace(/\.[^.]*$/, "")
        return icons[name.toLowerCase()] || url
    }

    // --- THE BARS AND THE OSD BACKGROUND ---
    // Settings' Hint Bar, Help Line and status icons (none on).
    property bool hintBar: true
    property bool helpLine: true
    property var statusIcons: []
    // The area the views lay their content out in, and OSD BACKGROUND's
    // window round it, on art pixels.
    property rect contentBox: Qt.rect(sw * 0.115625, sh * 0.11875, sw * 0.76875, sh * 0.7770833)
    readonly property real osdMargin: sh * 0.025
    property rect osdWindow: {
        var left = snapPx(contentBox.x - osdMargin), right = snapPx(contentBox.x + contentBox.width + osdMargin)
        var top = snapPx(contentBox.y - osdMargin), bottom = snapPx(contentBox.y + contentBox.height + osdMargin)
        return Qt.rect(left, top, right - left, bottom - top)
    }
    function snapPx(v) { return Math.round(v / px) * px }
    property string osdBackground: "Full"
    property string osdFrame: "On"

    // --- THE EFFECTS ---
    // They rest: nothing moves under a test. The layers that rest them in
    // the app (a loading screen, a player's menu) say so all the same.
    property bool effectsRest: true
    property var restLayers: []
    function restFor(layer, resting) {
        var layers = restLayers.filter(function(l) { return l !== layer })
        if (resting)
            layers.push(layer)
        restLayers = layers
    }
    property real fxTime: 0
    property var backgroundEffect: ({})
    property string backgroundShader: ""
    property bool screenSaverActive: false
    // No selector effect: the boxes that show are not kept.
    property var selector: null
    function selectorShown(box, shown) {}
    // The grounds laid over a view, the latest last.
    property var covers: []
    function coverShown(ground, shown) {
        var list = covers.filter(function(g) { return g !== ground })
        if (shown)
            list.push(ground)
        covers = list
    }

    // --- THE KEYS ---
    // The hints as InputManager has them for a keyboard.
    readonly property var keyboardHints: ({ navigate: "[▲▼]", arrows: "[▲▼◄►]", change: "[◄►]", browse: "[►]",
                                            left: "[◄]", back: "[ESC]", select: "[ENTER]", play_pause: "[SPACE]",
                                            coin: "[5]" })
    property var hints: keyboardHints
    // Whether a key event is back, and a hint bar's line from its parts, as
    // Main.qml has them.
    function isBack(event) { return Hints.isBack(event) }
    function hintLine(parts) { return Hints.line(hints, parts) }

    // --- A VIDEO ---
    // None playing, none behind the menus, the screen the app's own.
    property bool videoActive: false
    property bool videoBehind: false
    property var takeBackNote: ({})
    property int backdropSolidity: 100
    property bool screenHandedOff: false
    property bool loadingEffect: true
    // A time as the players show it: h:mm:ss, or m:ss under an hour.
    function formatTime(ms) {
        var s = Math.floor(ms / 1000)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60
        return (h > 0 ? h + ":" + pad(m) : m) + ":" + pad(sec)
    }
    function pad(n) { return n < 10 ? "0" + n : "" + n }

    // --- THE LAYOUT'S FRAME (Settings → Look → Menu Layout) ---
    property string menuLayout: "Standard"
    readonly property var menuPalette: ({
        "Studio Monitor": { ground: "#050608", ink: "#FFFFFF", dim: "#6E7891", accent: "#2350C8",
                            soft: "#CFDDFF", mark: "#F4D21C", bar: "#050608" },
        "Broadcast":      { ground: "#3A55B4", ink: "#FFFFFF", dim: "#94A8EC", accent: "#FFFFFF",
                            soft: "#FFFFFF", mark: "#FFFFFF", bar: "#3A55B4" },
        "Flat Panel":     { ground: "#000000", ink: "#EDEDED", dim: "#7B7B7B", accent: "#7B7B7B",
                            soft: "#9A9A9A", mark: "#F6CF00", bar: "#4C4C4C" }
    })[menuLayout] || null
    // Camcorder's column of icons, as the view that owns it hands it over.
    property var frameIcons: []
    property int frameIndex: 0
    property bool frameActive: false
    property bool frameTall: false
    function setFrame(icons, index, active, tall) {
        frameIcons = icons || []
        frameIndex = index || 0
        frameActive = !!active
        frameTall = !!tall
    }
    readonly property var shownFrameIcons: frameIcons
    readonly property int shownFrameIndex: frameIndex
    // Whether the view takes its place in the frame (Main.qml reads its
    // inFrame): here every view does, unless a test says otherwise.
    property bool viewInFrame: true
    // Camcorder's column is up beside the view: set by a test that wants it
    // (Main.qml works it out from the layout and the view).
    property bool framed: false
    // A monitor layout's frame is up, the view's rows in its pane; a value
    // changed there in two steps (Broadcast, Flat Panel).
    readonly property bool monitorFramed: menuPalette !== null && viewInFrame
    readonly property bool twoStep: monitorFramed && (menuLayout === "Broadcast" || menuLayout === "Flat Panel")
    // A layer over the whole screen in the scheme's colours puts the frame's
    // colours away, as a layer that rests the effects does, and the screens
    // before the menus (neither here: no boot screen, no notice at startup).
    readonly property bool frameCovered: covers.some(function(g) { return g.inLayout !== true })
    property bool bootActive: false
    property bool startupHolding: false
    property bool monitorInk: monitorFramed && !frameCovered && restLayers.length === 0
                              && !bootActive && !startupHolding
    // The monitor layouts' frame, under the views, which their bars speak to.
    MonitorChrome {
        id: frameChrome
        anchors.fill: parent
        visible: root.monitorInk
        // Studio Monitor's strip: Camcorder's icons, by their names.
        icons: root.menuLayout === "Studio Monitor" ? root.frameIcons.map(function(entry) { return entry.icon }) : []
        iconIndex: root.frameIndex
        iconsActive: root.frameActive
    }
    readonly property alias chrome: frameChrome
    // Where Camcorder's column stands, for a question to draw it again.
    readonly property rect frameColumn: Qt.rect(sw * 0.115625, sh * 0.25, sw * 0.084375,
        frameTall ? tallListHeight : listHeight)

    // --- THE PANE ---
    // Where a view's rows go: beside the column, in a monitor layout's frame,
    // or where every view has its list. Down, a menu's rows from listY,
    // listHeight one row short of the help line, tallListHeight where there
    // is none, helpedListHeight over one that always shows; anything else
    // from paneTop to paneBottom.
    readonly property real paneX: monitorFramed ? frameChrome.listX : framed ? sw * 0.209375 : sw * 0.115625
    readonly property real paneWidth: monitorFramed ? frameChrome.listWidth : framed ? sw * 0.675 : sw * 0.76875
    readonly property real listY: monitorFramed ? frameChrome.listY : sh * 0.25
    readonly property real listHeight: monitorFramed ? frameChrome.listHeight : sh * 0.4666667 + helpRoom + hintRoom
    readonly property real tallListHeight: monitorFramed ? frameChrome.tallHeight : sh * 0.525 + hintRoom
    readonly property real helpedListHeight: monitorFramed ? frameChrome.helpedHeight : sh * 0.4666667 + hintRoom
    readonly property real paneTop: monitorFramed ? frameChrome.listY : sh * 0.2083333
    readonly property real paneBottom: monitorFramed ? frameChrome.tallBottom
                                     : hintBar ? sh * 0.8333333 : contentBox.y + contentBox.height
    // The room the Hint Bar and the Help Line leave when they are off, a
    // menu's row each, and where a help line goes.
    readonly property real menuRowHeight: sh * 0.0583333
    property real hintRoom: hintBar ? 0 : menuRowHeight
    property real helpRoom: helpLine ? 0 : menuRowHeight
    property real helpLineMargin: hintBar ? sh * 0.1583333 : sh * 0.1041667

    // --- THE SHELL ---
    // Main.qml's changeWindow(): here the new view at once, without its
    // transition; or, for a test that sets changesDeferred, a moment later,
    // as a transition has it (WindowChange catches the old window first),
    // but for a loader's first view and into or out of a video's view.
    property bool changesDeferred: false
    readonly property var playbackView: /(Player|Play|Takeover|SignIn|Launch|Console)\.qml$/
    function changeWindow(loader, source, properties, base) {
        var from = String(loader.source)
        if (!changesDeferred || from === "" || playbackView.test(from) || playbackView.test(String(source)))
            loader.setSource(source, properties)
        else
            Qt.callLater(function() { if (loader) loader.setSource(source, properties) })
    }
    // Where the menus can be a window on a desktop (Window Mode).
    property bool canWindow: Qt.platform.os === "windows" || Qt.platform.pluginName === "xcb"
                             || Qt.platform.pluginName === "wayland"
    property string appVersion: "1.0.0"
}
