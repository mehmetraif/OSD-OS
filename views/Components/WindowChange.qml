import QtQuick

// --- WINDOW CHANGES (Settings → Look → Effects → Transition, or the theme's) ---
// The window (what a view shows, on its ground, with the selector's effect
// round the selected line: what is laid in it) and its changes. A window
// changing, the view a loader shows (Main.qml's moduleLoader, a module's
// own) giving way to the next: the old one is caught as a picture (lastFace)
// a moment before, and the two play out the change, as root.transition
// names it (Main.qml's transitionPresets).
// Fade: the old fades away over the new. Cube: the window turns over like
// a cube's face, the new on the next face, a way at random each time
// (left, right, up or down).
// Ripple: the old ripples out as the new ripples in. Wave: a wave runs
// out from a corner and dies away, the new window behind it. Drop: a drop
// falls in a corner, the new window inside its spreading ring. Which
// corner, at random. The last three are shaders (root.transitionShaders),
// Fade where they can't be used (root.transitionUsable()).
//
// Main.qml lays the window in its screen, and every view changes through
// it (root.changeWindow(), which hands the change on to change()); the view
// is set by root.showView().
//
//     WindowChange {
//         id: windowChange
//         anchors.fill: parent
//         textureSize: screen.texturePixels
//         // what the window shows: its ground, the views' loader…
//     }
Item {
    id: windowChange

    // What the window shows, laid in its face.
    default property alias content: face.data
    // The pictures' size in pixels (ShaderEffectSource's textureSize): the
    // window's, or (0, 0) for the screen's own.
    property size textureSize: Qt.size(0, 0)

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
        onTriggered: windowChange.caught(true)
    }

    // The view `loader` shows becomes `source` (with `properties`, as
    // Loader.setSource has them), as Settings → Look → Effects → Transition has it. Never into
    // or out of a video's views (a player's, a launch's), nor while the
    // effects rest, nor at the very start.
    function change(loader, source, properties, base) {
        var from = String(loader.source), to = String(source)
        if (windowChange.capturing)
            caught(true)
        if (root.transition === "" || windowChange.changing || root.effectsRest || root.bootActive
                || root.startupHolding || from === "" || root.playbackView.test(from) || root.playbackView.test(to)) {
            root.showView(loader, source, properties, base)
            return
        }
        if (windowChange.running)
            transitionAnimation.complete()
        windowChange.pending = function() { root.showView(loader, source, properties, base) }
        windowChange.capturing = true
        lastFace.scheduleUpdate()
        catchTimeout.restart()
    }
    // The old window caught (or given up on, `plain`): the change is made,
    // and plays out unless plain.
    function caught(plain) {
        if (!windowChange.capturing)
            return
        catchTimeout.stop()
        windowChange.capturing = false
        var pending = windowChange.pending
        windowChange.pending = null
        windowChange.changing = true
        pending()
        windowChange.changing = false
        if (plain === true)
            return
        var kind = root.transition
        if (!root.transitionUsable(kind))
            kind = "Fade"
        windowChange.direction = Math.floor(Math.random() * 4)
        windowChange.shader = root.transitionShaders[kind] || ""
        windowChange.kind = kind
        windowChange.progress = 0
        transitionAnimation.restart()
    }

    // Behind a window turning over (the cube) or rippling, the dark.
    Rectangle {
        anchors.fill: parent
        z: -2
        visible: windowChange.kind === "Cube" || windowChange.kind === "Ripple"
        color: "#000000"
    }

    // The window: the face a window change turns away, lastFace keeping a
    // picture of it as it was.
    Item {
        id: face
        anchors.fill: parent
        transform: Matrix4x4 { matrix: windowChange.faceMatrix }
        // Not hidden (visible: false): a view keeps its focus.
        opacity: windowChange.faceShown ? 1 : 0
    }

    // The window as it was, caught a moment before the next comes in
    // (change()): under the window while it is caught, then turned
    // away with it (the cube), faded out over it (fade), or rippled into
    // the next (ripple).
    ShaderEffectSource {
        id: lastFace
        anchors.fill: parent
        sourceItem: face
        textureSize: windowChange.textureSize
        live: false
        visible: windowChange.capturing || windowChange.running
        z: windowChange.kind === "Fade" ? 0.5 : -1.5
        opacity: windowChange.kind === "Fade" ? 1 - windowChange.progress : windowChange.lastShown ? 1 : 0
        transform: Matrix4x4 { matrix: windowChange.lastMatrix }
        onScheduledUpdateCompleted: windowChange.caught()
    }
    // The next window, as a picture, while a shader brings it in.
    ShaderEffectSource {
        id: nextFace
        anchors.fill: parent
        sourceItem: windowChange.kind in root.transitionShaders ? face : null
        textureSize: windowChange.textureSize
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
            property size resolution: Qt.size(root.canvasW, root.canvasH)
            property real px: root.px
            property point corner: Qt.point(windowChange.direction % 2, Math.floor(windowChange.direction / 2))
            property color ink: root.primaryColor
        }
    }
}
