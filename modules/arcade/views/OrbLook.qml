import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import Components
import "Rooms.js" as Rooms

// The Arcade screen's Orb look, a theme's ("arcade": "orb", ArcadeScreen's
// orbLook): the screen in 3D, in x-ray glass whose light adds up where its
// parts cross. On the right the list's panel, the filter's name on its tab
// and the page's games on bars, the selected one standing out towards us and
// the one left going back slowly; on the left a glass orb with the game's
// pictures inside it, the credits bought at its foot, and under it a panel of
// its high scores and what it is (on a room, what is said of the room).
// Brackets hold the panels from past the screen's sides, fog drifts behind,
// and the whole sways round the screen's middle as if alive. Each of ▲ ▼ ◄ ►
// nudges it on a spring (nudge()), and a game started (launch()) swings the
// panels out on their hinges while the orb comes to the middle and the camera
// goes into it, its picture opening out to fill the screen, before the
// loading screen. Laid out in the design's units in the Arcade screen's area
// (Screen Adjustment's), above the help line, its keys in the hint bar's
// place, and everything but the letters drawn in Qt Quick 3D. The
// Arcade screen keeps its keys, its list and what it shows (arcadeScreen,
// whose Loader this is), and the hint bar's pairs (hintPairs), the help line,
// Search's keys and the dialogs; this draws the rest.
Item {
    id: look

    // ---
    // Time, the sway, the keys' nudges and a game's start
    // ---

    // Seconds the look has been up, counted as its frames are drawn; still
    // while a game has the screen, or a dialog is over this one.
    property real time: 0
    readonly property bool resting: !visible || arcadeScreen.covered || (!!arcadeBackend && arcadeBackend.running)
    FrameAnimation {
        running: !look.resting
        onTriggered: {
            look.time += Math.min(frameTime, 0.1)
            look.spring(Math.min(frameTime, 0.1))
        }
    }
    function clamp01(x) { return Math.max(0, Math.min(1, x)) }
    function smoothstep(x) { x = clamp01(x); return x * x * (3 - 2 * x) }
    function wave(hz, phase) { return Math.sin(2 * Math.PI * hz * time + phase) }

    // The sway: slow waves of different lengths, so it never quite repeats a
    // move; calmed as a game starts.
    readonly property real calm: 1 - clamp01(since / 0.5)
    readonly property real yaw: calm * (3.2 * wave(0.23, 0) + 1.2 * wave(0.41, 1.3))
    readonly property real pitchAngle: calm * (2.0 * wave(0.19, 0.7) + 0.8 * wave(0.53, 2.1))
    readonly property real roll: calm * 0.6 * wave(0.31, 0.4)
    readonly property real bob: calm * 6 * wave(0.17, 1.9)

    // Each key a nudge to the whole, as if pushed on the side it points to:
    // ▼ tips its foot away and its top towards us, ▲ the other way; ► turns its
    // right side away, ◄ its left. A spring brings it back, a little past and
    // back again: a nudge comes out to 1 at its most (kick), keys in a row
    // add up, and the tilt is at most tiltMost degrees.
    readonly property real springW: 2 * Math.PI * 1.15
    readonly property real damping: 0.5
    readonly property real kick: {
        var turning = springW * Math.sqrt(1 - damping * damping)
        var most = Math.atan2(turning, damping * springW) / turning
        return turning / (Math.exp(-damping * springW * most) * Math.sin(turning * most))
    }
    readonly property real tiltMost: 9.6
    property real downNudge: 0
    property real downSpeed: 0
    property real acrossNudge: 0
    property real acrossSpeed: 0
    function nudge(across, down) {
        acrossSpeed += kick * across
        downSpeed += kick * down
    }
    // The spring a frame on, in steps of 4 ms at most.
    function spring(dt) {
        if (downNudge === 0 && downSpeed === 0 && acrossNudge === 0 && acrossSpeed === 0)
            return
        var steps = Math.max(1, Math.ceil(dt / 0.004))
        var h = dt / steps
        var d = downNudge, dv = downSpeed, a = acrossNudge, av = acrossSpeed
        for (var i = 0; i < steps; ++i) {
            dv += (-springW * springW * d - 2 * damping * springW * dv) * h
            d += dv * h
            av += (-springW * springW * a - 2 * damping * springW * av) * h
            a += av * h
        }
        // At rest once it no longer shows.
        var still = function(n, v) { return Math.abs(n) < 1e-4 && Math.abs(v) < 1e-3 }
        downNudge = still(d, dv) ? 0 : d
        downSpeed = still(d, dv) ? 0 : dv
        acrossNudge = still(a, av) ? 0 : a
        acrossSpeed = still(a, av) ? 0 : av
    }
    function tilt(n) { return calm * tiltMost * Math.tanh(n / 1.6) }

    // A game started: the panels swing out first, slow to start and then
    // flung; the orb comes to the middle meanwhile; the camera goes in a
    // moment later, slow at both ends, and the picture opens as it gets
    // there; then the look fades for the loading screen, and done() is
    // called (ArcadeScreen's play()). False while one is already starting.
    property bool launching: false
    property real launchedAt: 0
    property var launchDone: null
    readonly property real since: launching ? Math.max(0, time - launchedAt) : 0
    readonly property real swing: clamp01(since / 1.3)
    readonly property real flung: Math.pow(swing, 2.2)
    readonly property real centred: smoothstep(since / 0.9)
    readonly property real dolly: smoothstep((since - 0.25) / 1.5)
    readonly property real open: smoothstep((since - 1.15) / 0.6)
    readonly property real launchFade: clamp01((since - 2.05) / 0.4)
    function launch(done) {
        if (launching)
            return false
        launchedAt = time
        launchDone = done
        launching = true
        launchEnd.restart()
        return true
    }
    // The end of a start, whether or not its frames were drawn.
    Timer {
        id: launchEnd
        interval: 2450
        onTriggered: {
            var done = look.launchDone
            look.launchDone = null
            if (done)
                done()
        }
    }
    // Back on the list after a game played from it (Root.qml's playing,
    // the loading screen over the list meanwhile): everything in its place
    // again, fading in as the screen does when it opens.
    readonly property bool gameOn: !!moduleRoot.playing
    onGameOnChanged: {
        if (!gameOn && launching && !launchEnd.running) {
            launching = false
            fadeIn.restart()
        }
    }

    // The row under a point of the screen (Main.qml's followPointer(), by way
    // of hoverAt()): its index, -1 for none. Its bar's corners where the
    // camera sees them (View3D's pick() misses a ProceduralMesh), the nearer
    // bar where two overlap.
    function rowAt(x, y) {
        if (!area.visible)
            return -1
        var p = view.mapFromItem(arcadeScreen, x, y)
        var found = -1
        var nearest = Infinity
        for (var i = 0; i < rowNodes.count; ++i) {
            var row = rowNodes.objectAt(i)
            if (!row || !row.visible)
                continue
            var corners = [[-1, 1], [1, 1], [1, -1], [-1, -1]].map(function(c) {
                return view.mapFrom3DScene(row.mapPositionToScene(v3(c[0] * (panelWidth / 2 - 16), c[1] * rowHeight / 2, 6)))
            })
            if (inside(p, corners) && corners[0].z < nearest) {
                found = i
                nearest = corners[0].z
            }
        }
        return found
    }
    // Whether a point lies in a convex outline (its corners in order).
    function inside(p, corners) {
        var turn = 0
        for (var i = 0; i < corners.length; ++i) {
            var a = corners[i], b = corners[(i + 1) % corners.length]
            var cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)
            if (cross !== 0 && turn !== 0 && (cross > 0) !== (turn > 0))
                return false
            if (cross !== 0)
                turn = cross
        }
        return true
    }

    // ---
    // The orb's and the panels' layout, in the design's units (the screen's
    // 960 by 720, at ArcadeScreen's orbScale 1)
    // ---

    readonly property int panelLeft: 476
    readonly property int panelRight: 806
    readonly property int panelTop: 92
    readonly property int panelBottom: 592
    readonly property int tabBottom: 142
    readonly property int rowsTop: 154
    readonly property int rowsBottom: 582
    readonly property int rowHeight: 44
    readonly property real pitch: (rowsBottom - rowsTop - rowHeight) / Math.max(1, arcadeScreen.perPage - 1)
    readonly property int orbX: 296
    readonly property int orbY: 240
    readonly property int orbR: 150
    readonly property int orbZ: 30
    readonly property int infoLeft: 120
    readonly property int infoRight: 436
    readonly property int infoTop: 398
    readonly property int infoBottom: 600
    // How far forward the selected row comes, all the way out.
    readonly property real liftBy: 90
    // The design's units into the scene's: the screen's middle at the
    // origin, y up, a unit a design unit at z = 0 from where the camera rests.
    function sx(x) { return x - 480 }
    function sy(y) { return 360 - y }
    function v3(x, y, z) { return Qt.vector3d(x, y, z) }

    // ---
    // Colours: the scheme's two, a brighter one for what is lit, the orb's,
    // turned round the colour wheel, and the letters'
    // ---

    readonly property color ink: root.primaryColor
    readonly property color ground: root.surfaceColor
    function mix(a, b, f) { return Qt.rgba(a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, 1) }
    function turned(c, by) { return Qt.hsla((c.hslHue + by + 1) % 1, c.hslSaturation, c.hslLightness, 1) }
    readonly property color glow: mix(ink, Qt.rgba(1, 1, 0.63, 1), 0.38)
    readonly property color sea: turned(ink, 0.27)
    readonly property color letters: mix(ink, Qt.rgba(1, 1, 1, 1), 0.55)
    readonly property color darkLetters: mix(ground, ink, 0.1)

    // ---
    // Meshes, in a part's own units: x right, y up, round its middle
    // ---

    // A tube round a path ([x, y, z]), closed or not.
    function tube(points, radius, sides, closed) {
        var pos = [], nor = [], idx = []
        var n = points.length
        var p = points.map(function(q) { return v3(q[0], q[1], q[2] || 0) })
        for (var i = 0; i < n; ++i) {
            var a = p[closed ? (i - 1 + n) % n : Math.max(0, i - 1)]
            var b = p[closed ? (i + 1) % n : Math.min(n - 1, i + 1)]
            var along = b.minus(a).normalized()
            var side = along.crossProduct(v3(0, 0, 1))
            if (side.length() < 1e-4)
                side = along.crossProduct(v3(0, 1, 0))
            side = side.normalized()
            var up = along.crossProduct(side).normalized()
            for (var k = 0; k < sides; ++k) {
                var angle = 2 * Math.PI * k / sides
                var dir = side.times(Math.cos(angle)).plus(up.times(Math.sin(angle)))
                pos.push(p[i].plus(dir.times(radius)))
                nor.push(dir)
            }
        }
        var rings = closed ? n : n - 1
        for (i = 0; i < rings; ++i) {
            var j = (i + 1) % n
            for (k = 0; k < sides; ++k) {
                var k2 = (k + 1) % sides
                idx.push(i * sides + k, j * sides + k, j * sides + k2)
                idx.push(i * sides + k, j * sides + k2, i * sides + k2)
            }
        }
        return { positions: pos, normals: nor, indexes: idx }
    }
    // A box w by h round (cx, cy), its corners cut by c.
    function chamfered(cx, cy, w, h, c) {
        var l = cx - w / 2, r = cx + w / 2, b = cy - h / 2, t = cy + h / 2
        return [[l + c, t], [r - c, t], [r, t - c], [r, b + c], [r - c, b], [l + c, b], [l, b + c], [l, t - c]]
    }
    // A slab of a convex outline, deep through z: flat faces, so its edges
    // light and its faces stay dim.
    function slab(outline, z, deep) {
        var pos = [], nor = [], idx = []
        var n = outline.length
        var faces = [[z + deep / 2, 1], [z - deep / 2, -1]]
        for (var f = 0; f < 2; ++f) {
            var base = pos.length
            for (var i = 0; i < n; ++i) {
                pos.push(v3(outline[i][0], outline[i][1], faces[f][0]))
                nor.push(v3(0, 0, faces[f][1]))
            }
            for (i = 1; i + 1 < n; ++i)
                idx.push(base, base + i, base + i + 1)
        }
        for (i = 0; i < n; ++i) {
            var a = outline[i], b = outline[(i + 1) % n]
            var dx = b[0] - a[0], dy = b[1] - a[1], len = Math.sqrt(dx * dx + dy * dy)
            var normal = v3(dy / len, -dx / len, 0)
            base = pos.length
            pos.push(v3(a[0], a[1], z + deep / 2), v3(b[0], b[1], z + deep / 2),
                     v3(b[0], b[1], z - deep / 2), v3(a[0], a[1], z - deep / 2))
            for (var k = 0; k < 4; ++k)
                nor.push(normal)
            idx.push(base, base + 1, base + 2, base, base + 2, base + 3)
        }
        return { positions: pos, normals: nor, indexes: idx }
    }
    component Mesh: ProceduralMesh {
        property var made: ({ positions: [], normals: [], indexes: [] })
        positions: made.positions
        normals: made.normals
        indexes: made.indexes
    }
    // A row's bar round its middle, cut on a slant at its right end; the
    // selected one deeper, and reaching a little further right.
    readonly property real panelWidth: panelRight - panelLeft
    readonly property real panelHeight: panelBottom - panelTop
    function bar(on) {
        var left = -panelWidth / 2 + 16, right = panelWidth / 2 - 16 + (on ? 8 : 0)
        var top = rowHeight / 2, bottom = -rowHeight / 2
        return slab([[left, top], [right, top], [right - 16, bottom], [left, bottom]], 6, on ? 18 : 10)
    }

    // Seen as the screen opens, and after a game played from the list.
    NumberAnimation {
        id: fadeIn
        target: view
        property: "opacity"
        from: 0
        to: 1
        duration: 300
        running: true
    }

    View3D {
        id: view
        anchors.fill: parent
        opacity: 1 - look.launchFade
        // Named, for rowAt()'s mapFrom3DScene().
        camera: camera
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }
        PerspectiveCamera {
            id: camera
            // 720 units high at z = 0 at rest; in to where the orb's
            // picture, opened out, fills the view.
            fieldOfView: 20
            readonly property real rest: 360 / Math.tan(10 * Math.PI / 180)
            readonly property real near: area.s * (look.orbZ + 800)
            z: rest - (rest - near) * look.dolly
            clipNear: 20
            clipFar: 8000
        }

        // A sheet in its node's plane, w by h round its middle, showing 2D
        // content (drawn at twice its size, for sharp letters) over what is
        // behind it: the letters on a panel.
        component Sheet: Model {
            property real w
            property real h
            property alias content: picture.sourceItem
            source: "#Rectangle"
            scale: Qt.vector3d(w / 100, h / 100, 1)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                alphaMode: PrincipledMaterial.Blend
                depthDrawMode: Material.NeverDepthDraw
                cullMode: Material.NoCulling
                baseColorMap: Texture { id: picture; generateMipmaps: true; mipFilter: Texture.Linear }
            }
        }
        // 2D content in design units, drawn at twice its size for a Sheet.
        component Twice: Item {
            default property alias contents: inner.data
            property real w
            property real h
            width: w * 2
            height: h * 2
            Item {
                id: inner
                width: parent.w
                height: parent.h
                scale: 2
                transformOrigin: Item.TopLeft
            }
        }
        // The look's letters.
        component Letters: Text {
            color: look.letters
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: arcadeScreen.textLines
            textFormat: Text.PlainText
        }
        // Glass that shows its edges and adds its light to what is behind.
        component Glass: CustomMaterial {
            property color tint: look.ink
            property real core: 0.05
            property real strength: 1.0
            property real power: 2.0
            // How much of it shows: a part's own opacity, which a custom
            // material is left to apply.
            property real fade: 1.0
            shadingMode: CustomMaterial.Unshaded
            sourceBlend: CustomMaterial.One
            destinationBlend: CustomMaterial.One
            cullMode: Material.NoCulling
            depthDrawMode: Material.NeverDepthDraw
            vertexShader: "orb/glass.vert"
            fragmentShader: "orb/glass.frag"
        }

        // A key at the foot: its button in glass, its label just in front,
        // and what it does beside it, on the side outwards (-1 left, 1
        // right); laid from its end of the bar (edge, the design's units)
        // inwards, to inner.
        component FootKey: Node {
            id: key
            property var pair: null
            property int outwards: 1
            property real edge: 0
            property real fade: 1.0
            readonly property string label: pair ? String(pair.key).replace(/^\[|\]$/g, "") : ""
            readonly property real wide: Math.max(44, labelMetrics.advanceWidth + 24)
            readonly property real word: wordMetrics.advanceWidth
            readonly property real inner: pair ? edge - outwards * (word + 8 + wide) : edge
            visible: pair !== null
            x: look.sx(edge - outwards * (word + 8 + wide / 2))
            z: 20
            TextMetrics {
                id: labelMetrics
                font.family: root.globalFont
                font.pixelSize: arcadeScreen.nameLines
                text: key.label
            }
            TextMetrics {
                id: wordMetrics
                font.family: root.globalFont
                font.pixelSize: arcadeScreen.nameLines
                font.capitalization: Font.AllUppercase
                text: key.pair ? key.pair.word : ""
            }
            Model {
                source: "#Sphere"
                scale: Qt.vector3d(key.wide / 100, 0.44, 0.2)
                materials: Glass { tint: look.glow; core: 0.55; strength: 0.9; power: 1.4; fade: key.fade }
            }
            Sheet {
                z: 14
                w: key.wide
                h: 40
                content: Twice {
                    w: key.wide
                    h: 40
                    Letters {
                        anchors.centerIn: parent
                        text: key.label
                        color: look.ground
                        font.pixelSize: arcadeScreen.nameLines
                        font.capitalization: Font.MixedCase
                    }
                }
            }
            Sheet {
                x: key.outwards * (key.wide / 2 + 8 + key.word / 2)
                z: 10
                w: key.word + 8
                h: 40
                content: Twice {
                    w: key.word + 8
                    h: 40
                    Letters {
                        anchors.centerIn: parent
                        text: key.pair ? key.pair.word : ""
                        font.pixelSize: arcadeScreen.nameLines
                    }
                }
            }
        }

        Node {
            id: stage
            // The design's 4:3 across a screen of another shape, as the
            // menus are drawn on it.
            scale: Qt.vector3d(look.width > 0 && look.height > 0 ? (look.width / look.height) / (4 / 3) : 1, 1, 1)

            // ---
            // Fog: three layers at three depths, drifting, the further the
            // darker
            // ---
            Repeater3D {
                model: [{ z: -500, tint: look.ink, strength: 0.52, scale: 1.2, seed: 0.13, radius: 0.55 },
                        { z: -1100, tint: look.sea, strength: 0.42, scale: 0.92, seed: 0.77, radius: 0.45 },
                        { z: -1900, tint: look.mix(look.ink, look.ground, 0.25), strength: 0.56, scale: 0.64,
                          seed: 0.31, radius: 0.35 }]
                Model {
                    required property var modelData
                    // As big as the view at its depth, and half again.
                    readonly property real high: 2 * (camera.rest - modelData.z) * Math.tan(10 * Math.PI / 180) * 1.5
                    source: "#Rectangle"
                    z: modelData.z
                    scale: Qt.vector3d(high * 4 / 3 / 100, high / 100, 1)
                    materials: CustomMaterial {
                        property color tint: modelData.tint
                        property real strength: modelData.strength
                        property real scale: modelData.scale
                        property real seed: modelData.seed
                        property real radius: modelData.radius
                        // Once round in two minutes.
                        property real phase: look.time / 120
                        // (Not "noise": the shaders' compiler takes that name.)
                        property TextureInput fogNoise: TextureInput {
                            texture: Texture {
                                source: "orb/fog.png"
                                tilingModeHorizontal: Texture.Repeat
                                tilingModeVertical: Texture.Repeat
                                generateMipmaps: true
                                mipFilter: Texture.Linear
                            }
                        }
                        shadingMode: CustomMaterial.Unshaded
                        sourceBlend: CustomMaterial.One
                        destinationBlend: CustomMaterial.One
                        cullMode: Material.NoCulling
                        depthDrawMode: Material.NeverDepthDraw
                        vertexShader: "orb/flat.vert"
                        fragmentShader: "orb/fog.frag"
                    }
                }
            }

            // ---
            // The whole, swaying round the screen's middle and nudged by the
            // keys
            // ---
            Node {
                id: dash
                eulerRotation.y: look.yaw + look.tilt(look.acrossNudge)
                eulerRotation.x: look.pitchAngle + look.tilt(look.downNudge)
                eulerRotation.z: look.roll
                y: look.bob

                // The design's orb and panels (120..840 by 90..610) in the
                // Arcade screen's area, as large as fit above the help line
                // (ArcadeScreen's orbScale and orbLeft).
                Node {
                    id: area
                    readonly property real s: arcadeScreen.orbScale
                    visible: arcadeScreen.filters.length > 0
                    x: arcadeScreen.orbLeft - 480 + 360 * s
                    y: 360 - arcadeScreen.areaTop - 270 * s
                    scale: Qt.vector3d(s, s, s)

                    // The orb: the game's pictures inside, glass round it. As
                    // the game starts it comes to the middle, and its picture
                    // opens out as the camera gets there, the glass going.
                    // Search's keys lie over it while Search is shown.
                    Node {
                        id: orb
                        readonly property bool away: arcadeScreen.filterId === "search"
                        x: look.sx(look.orbX) + (-area.x / area.s - look.sx(look.orbX)) * look.centred
                        y: look.sy(look.orbY) + (-area.y / area.s - look.sy(look.orbY)) * look.centred
                        z: look.orbZ
                        opacity: away ? 0 : 1
                        Behavior on opacity { NumberAnimation { duration: 200 } }
                        Model {
                            // The screen, 4:3, as high as the disc, a little
                            // behind the orb's middle, so it is drawn before the
                            // glass, which then lights only its front over it.
                            source: "#Rectangle"
                            z: -2
                            scale: Qt.vector3d(look.orbR * 1.9 * 4 / 3 / 100, look.orbR * 1.9 / 100, 1)
                            materials: CustomMaterial {
                                property color tint: look.sea
                                property real open: look.open
                                property real fade: orb.opacity
                                property TextureInput picture: TextureInput {
                                    texture: Texture { sourceItem: look.screen }
                                }
                                shadingMode: CustomMaterial.Unshaded
                                sourceBlend: CustomMaterial.One
                                destinationBlend: CustomMaterial.OneMinusSrcAlpha
                                depthDrawMode: Material.AlwaysDepthDraw
                                vertexShader: "orb/flat.vert"
                                fragmentShader: "orb/screen.frag"
                            }
                        }
                        Model {
                            source: "#Sphere"
                            scale: Qt.vector3d(look.orbR / 50, look.orbR / 50, look.orbR / 50)
                            materials: Glass {
                                tint: look.sea
                                core: 0.03 * (1 - look.open)
                                strength: 1.5 * (1 - look.open)
                                power: 2.4
                                fade: orb.opacity
                            }
                        }
                        // The credits bought, where COIN buys them, on the
                        // glass at its foot.
                        Sheet {
                            visible: arcadeScreen.buysCredits
                            opacity: 1 - look.swing
                            y: -0.7 * look.orbR
                            z: look.orbR * 0.75
                            w: 200
                            h: 40
                            content: Twice {
                                w: 200
                                h: 40
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: creditText.implicitWidth + 30
                                    height: creditText.implicitHeight + 6
                                    radius: height / 2
                                    color: Qt.rgba(look.ground.r, look.ground.g, look.ground.b, 0.85)
                                    border.color: look.ink
                                    border.width: 2
                                    Letters {
                                        id: creditText
                                        anchors.centerIn: parent
                                        text: "Credit " + moduleRoot.credits
                                    }
                                }
                            }
                        }
                    }

                    // The list's panel, its right side back, held from the
                    // right by arms on a hinge past the screen's edge: as a
                    // game starts it swings out on it, its inner side towards
                    // us.
                    Node {
                        id: listHinge
                        x: 620
                        y: look.sy((look.panelTop + look.panelBottom) / 2)
                        z: -130
                        eulerRotation.y: 105 * look.flung
                        Node {
                            id: listPanel
                            x: look.sx((look.panelLeft + look.panelRight) / 2) - listHinge.x
                            z: -listHinge.z
                            eulerRotation.y: 16
                            eulerRotation.x: -3

                            Model {
                                geometry: Mesh {
                                    made: look.slab(look.chamfered(0, 0, look.panelWidth, look.panelHeight, 24), -8, 10)
                                }
                                materials: Glass { core: 0.06; strength: 0.9; power: 1.0 }
                            }
                            Model {
                                geometry: Mesh {
                                    made: look.tube(look.chamfered(0, 0, look.panelWidth, look.panelHeight, 24), 6, 10, true)
                                }
                                materials: Glass { core: 0.25; strength: 1.4; power: 1.6 }
                            }
                            // The line under the tab.
                            Model {
                                geometry: Mesh {
                                    readonly property real y0: look.panelHeight / 2 - (look.tabBottom - look.panelTop)
                                    made: look.tube([[-look.panelWidth / 2 + 12, y0, 0], [look.panelWidth / 2 - 12, y0, 0]],
                                                    3, 8, false)
                                }
                                materials: Glass { tint: look.glow; core: 0.3; strength: 1.2 }
                            }

                            // The rows: bars, the selected one lit through and
                            // standing out of the panel towards us, the one
                            // left going back slowly; each with its game's name
                            // on it, coming out with it.
                            Repeater3D {
                                id: rowNodes
                                model: arcadeScreen.perPage
                                Node {
                                    id: row
                                    required property int index
                                    readonly property var entry: arcadeScreen.rows[index] || null
                                    readonly property var game: entry ? entry.game : ({})
                                    readonly property bool on: !!entry && index === arcadeScreen.selectedRow
                                    // How far it stands out, 0 to 1: the
                                    // selected one comes out at once, a little
                                    // past and back; one the cursor has left
                                    // goes back slowly; each from where it is.
                                    property real lift: 0
                                    visible: entry !== null
                                    x: -12 * lift
                                    y: look.panelHeight / 2 - (look.rowsTop - look.panelTop) - index * look.pitch
                                       - look.rowHeight / 2
                                    z: look.liftBy * lift
                                    onOnChanged: {
                                        out.stop()
                                        back.stop()
                                        if (on) {
                                            out.from = lift
                                            out.start()
                                        } else {
                                            back.from = lift
                                            back.start()
                                        }
                                    }
                                    Component.onCompleted: lift = on ? 1 : 0
                                    NumberAnimation {
                                        id: out
                                        target: row
                                        property: "lift"
                                        to: 1
                                        duration: 180
                                        easing.type: Easing.OutBack
                                    }
                                    NumberAnimation {
                                        id: back
                                        target: row
                                        property: "lift"
                                        to: 0
                                        duration: 800
                                        easing.type: Easing.InOutQuad
                                    }

                                    Model {
                                        // Its own: a mesh no model holds any more
                                        // shows nothing once another takes it up.
                                        geometry: Mesh { made: look.bar(row.on) }
                                        materials: Glass {
                                            tint: row.on ? look.glow : look.ink
                                            core: row.on ? 0.62 + 0.08 * look.wave(1.2, 0) : 0.16 + 0.12 * row.lift
                                            strength: row.on ? 0.7 : 1.1
                                            power: 1.4
                                        }
                                    }
                                    // The game's name, scrolled through while
                                    // it is selected and too long for its bar;
                                    // a favourite's heart, Search's glass on its
                                    // own row, a room of the lobby's ▶ or lock;
                                    // ▲ ▼ where there is a page before or after.
                                    Sheet {
                                        z: 22
                                        w: look.panelWidth - 40
                                        h: look.rowHeight
                                        content: Twice {
                                            id: rowSheet
                                            w: look.panelWidth - 40
                                            h: look.rowHeight
                                            readonly property color ink: row.on ? look.darkLetters : look.letters
                                            readonly property bool before: row.index === 0 && arcadeScreen.page > 0
                                            readonly property bool after: row.index === arcadeScreen.perPage - 1
                                                                          && arcadeScreen.page < arcadeScreen.pages - 1
                                            Item {
                                                id: nameClip
                                                x: 10
                                                width: (mark.visible ? mark.x - 8 : (arrow.visible ? arrow.x - 8 : rowSheet.w - 8)) - x
                                                height: rowSheet.h
                                                clip: true
                                                Letters {
                                                    id: nameText
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    width: row.on ? implicitWidth : Math.min(implicitWidth, nameClip.width)
                                                    text: row.game.name || ""
                                                    elide: Text.ElideRight
                                                    color: rowSheet.ink
                                                    font.pixelSize: arcadeScreen.nameLines
                                                    onTextChanged: Qt.callLater(nameClip.updateScroll)
                                                }
                                                readonly property bool scrolls: row.on && nameText.implicitWidth > width
                                                readonly property real scrollBy: width - nameText.implicitWidth
                                                onScrollsChanged: Qt.callLater(updateScroll)
                                                onScrollByChanged: Qt.callLater(updateScroll)
                                                function updateScroll() {
                                                    if (scrolls)
                                                        scroll.restart()
                                                    else
                                                        scroll.stop()
                                                }
                                                SequentialAnimation {
                                                    id: scroll
                                                    loops: Animation.Infinite
                                                    onRunningChanged: if (!running) nameText.x = 0
                                                    PauseAnimation { duration: 1500 }
                                                    NumberAnimation {
                                                        target: nameText
                                                        property: "x"
                                                        to: nameClip.scrollBy
                                                        duration: Math.abs(to) * 20
                                                    }
                                                    PauseAnimation { duration: 2000 }
                                                    PropertyAction { target: nameText; property: "x"; value: 0 }
                                                }
                                            }
                                            PixelIcon {
                                                id: mark
                                                readonly property string kind: row.game.search ? "search"
                                                                               : row.game.lobby ? Rooms.mark(row.game.lobby)
                                                                               : row.game.favorite ? "heart" : ""
                                                visible: kind !== ""
                                                name: kind
                                                color: rowSheet.ink
                                                // An art pixel, in the design's units.
                                                pixel: 3
                                                x: (arrow.visible ? arrow.x - 8 : rowSheet.w - 8) - width
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Letters {
                                                id: arrow
                                                visible: rowSheet.before || rowSheet.after
                                                x: rowSheet.w - 12 - width
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: rowSheet.before ? "▲" : "▼"
                                                color: row.on ? rowSheet.ink : look.glow
                                                font.pixelSize: 24
                                            }
                                        }
                                    }
                                }
                            }

                            // The tab's letters: the filter's name, ◄ ► where
                            // there are others, and the page of so many (on
                            // Search, how many games its words find); under
                            // the tab, where a filter has nothing, why.
                            Sheet {
                                z: 20
                                y: look.panelHeight / 2 - (look.tabBottom - look.panelTop) / 2
                                w: look.panelWidth
                                h: look.tabBottom - look.panelTop
                                content: Twice {
                                    id: tab
                                    w: look.panelWidth
                                    h: look.tabBottom - look.panelTop
                                    readonly property bool arrows: arcadeScreen.filters.length > 1
                                    Letters {
                                        visible: tab.arrows
                                        x: 18
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "◄"
                                        color: look.glow
                                    }
                                    Letters {
                                        x: 50
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: pageText.x - 12 - x
                                        text: arcadeScreen.filter ? arcadeScreen.filter.name : ""
                                        elide: Text.ElideRight
                                        font.pixelSize: arcadeScreen.nameLines
                                        // Smaller, down to the hint bar's
                                        // smallest, before it is cut short.
                                        fontSizeMode: Text.HorizontalFit
                                        minimumPixelSize: 21
                                    }
                                    Letters {
                                        id: pageText
                                        x: tab.w - (tab.arrows ? 50 : 20) - width
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: arcadeScreen.barRight
                                    }
                                    Letters {
                                        visible: tab.arrows
                                        x: tab.w - 20 - width
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "►"
                                        color: look.glow
                                    }
                                }
                            }
                            Sheet {
                                visible: arcadeScreen.emptyLine !== ""
                                z: 20
                                y: look.panelHeight / 2 - (look.rowsTop - look.panelTop) - arcadeScreen.emptyRow * look.pitch
                                   - 60
                                w: look.panelWidth - 40
                                h: 120
                                content: Twice {
                                    w: look.panelWidth - 40
                                    h: 120
                                    Letters {
                                        x: 10
                                        width: parent.width - 20
                                        text: arcadeScreen.emptyLine
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: arcadeScreen.nameLines
                                    }
                                }
                            }

                            // The bracket: a plate on the panel's edge with
                            // jaws over it, and two arms out past the screen's
                            // edge.
                            Node {
                                x: look.panelWidth / 2
                                Model {
                                    geometry: Mesh { made: look.slab([[2, 150], [30, 150], [30, -110], [2, -110]], -4, 38) }
                                    materials: Glass { core: 0.1; strength: 1.2; power: 1.5 }
                                }
                                Model {
                                    geometry: Mesh { made: look.slab(look.chamfered(12, 162, 46, 24, 6), -4, 48) }
                                    materials: Glass { core: 0.1; strength: 1.2; power: 1.5 }
                                }
                                Model {
                                    geometry: Mesh { made: look.slab(look.chamfered(12, -122, 46, 24, 6), -4, 48) }
                                    materials: Glass { core: 0.1; strength: 1.2; power: 1.5 }
                                }
                                Model {
                                    geometry: Mesh { made: look.slab([[52, 44], [330, 104], [330, 52], [52, -4]], -4, 30) }
                                    materials: Glass { core: 0.08; strength: 1.1; power: 1.4 }
                                }
                                Model {
                                    geometry: Mesh { made: look.slab([[52, -4], [300, -150], [300, -196], [52, -48]], -4, 26) }
                                    materials: Glass { core: 0.08; strength: 1.1; power: 1.4 }
                                }
                            }
                        }
                    }

                    // The details' panel, its left side back, held from the
                    // left on a hinge past the screen's left edge, swinging out
                    // the other way.
                    Node {
                        id: infoHinge
                        x: -600
                        y: look.sy((look.infoTop + look.infoBottom) / 2)
                        z: -95
                        eulerRotation.y: -105 * look.flung
                        Node {
                            id: infoPanel
                            readonly property real w: look.infoRight - look.infoLeft
                            readonly property real h: look.infoBottom - look.infoTop
                            x: look.sx((look.infoLeft + look.infoRight) / 2) - infoHinge.x
                            z: 10 - infoHinge.z
                            eulerRotation.y: -12
                            eulerRotation.x: -4

                            Model {
                                geometry: Mesh { made: look.slab(look.chamfered(0, 0, infoPanel.w, infoPanel.h, 22), -8, 10) }
                                materials: Glass { core: 0.06; strength: 0.9; power: 1.0 }
                            }
                            Model {
                                geometry: Mesh { made: look.tube(look.chamfered(0, 0, infoPanel.w, infoPanel.h, 22), 6, 10, true) }
                                materials: Glass { core: 0.25; strength: 1.4; power: 1.6 }
                            }
                            Node {
                                x: -infoPanel.w / 2
                                Model {
                                    geometry: Mesh { made: look.slab([[-30, 70], [-2, 70], [-2, -70], [-30, -70]], -4, 38) }
                                    materials: Glass { core: 0.1; strength: 1.2; power: 1.5 }
                                }
                                Model {
                                    geometry: Mesh { made: look.slab(look.chamfered(-12, 82, 46, 24, 6), -4, 48) }
                                    materials: Glass { core: 0.1; strength: 1.2; power: 1.5 }
                                }
                                Model {
                                    geometry: Mesh { made: look.slab(look.chamfered(-12, -82, 46, 24, 6), -4, 48) }
                                    materials: Glass { core: 0.1; strength: 1.2; power: 1.5 }
                                }
                                Model {
                                    geometry: Mesh { made: look.slab([[-52, 24], [-300, 110], [-300, 62], [-52, -20]], -4, 28) }
                                    materials: Glass { core: 0.08; strength: 1.1; power: 1.4 }
                                }
                                Model {
                                    geometry: Mesh { made: look.slab([[-52, -20], [-280, -120], [-280, -164], [-52, -62]], -4, 24) }
                                    materials: Glass { core: 0.08; strength: 1.1; power: 1.4 }
                                }
                            }
                            // Its lines: the game's high scores (its
                            // leaderboard's, or this machine's), who made it
                            // and when, how many play it and how long it has
                            // been played; on a room, what is said of it.
                            Sheet {
                                z: 14
                                w: infoPanel.w
                                h: infoPanel.h
                                content: Twice {
                                    w: infoPanel.w
                                    h: infoPanel.h
                                    Column {
                                        x: 24
                                        y: 16
                                        width: infoPanel.w - 48
                                        Repeater {
                                            model: arcadeScreen.orbLines
                                            Item {
                                                required property var modelData
                                                width: parent.width
                                                height: 29
                                                Letters {
                                                    width: Math.min(implicitWidth, parent.width - (value.text !== "" ? value.implicitWidth + 16 : 0))
                                                    text: modelData.label
                                                    elide: Text.ElideRight
                                                    color: modelData.heading ? look.glow : look.ink
                                                }
                                                Letters {
                                                    id: value
                                                    anchors.right: parent.right
                                                    width: Math.min(implicitWidth, parent.width * 0.6)
                                                    horizontalAlignment: Text.AlignRight
                                                    text: modelData.value
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // The keys at the foot, in the hint bar's place: back's and
                // select's buttons in glass from the bar's ends inwards (the
                // status icons, if any, at its right end), what the rest do
                // between them; they drop away as the panels go.
                Node {
                    id: foot
                    visible: root.hintBar
                    y: look.sy(arcadeScreen.orbFootY) - 260 * look.flung
                    opacity: 1 - look.swing
                    readonly property var pairs: arcadeScreen.hintPairs
                    readonly property var backPair: pairs.length > 0 ? pairs[0] : null
                    readonly property var selectPair: {
                        for (var i = 1; i < pairs.length; ++i) {
                            if (pairs[i].key === root.hints.select)
                                return pairs[i]
                        }
                        return null
                    }
                    readonly property string between: pairs.filter(function(p) {
                        return p !== backPair && p !== selectPair
                    }).map(function(p) { return p.key + ":" + p.word }).join("  ")
                    FootKey {
                        id: backKey
                        pair: foot.backPair
                        outwards: -1
                        edge: arcadeScreen.areaLeft + 12
                        fade: foot.opacity
                    }
                    FootKey {
                        id: selectKey
                        pair: foot.selectPair
                        outwards: 1
                        edge: arcadeScreen.areaRight - 12 - (root.helpLine ? 0 : arcadeScreen.statusRoom / arcadeScreen.u)
                        fade: foot.opacity
                    }
                    Sheet {
                        id: betweenSheet
                        readonly property real from: backKey.inner + 16
                        readonly property real to: selectKey.inner - 16
                        x: look.sx((from + to) / 2)
                        z: 30
                        w: Math.max(40, to - from)
                        h: 40
                        content: Twice {
                            w: betweenSheet.w
                            h: 40
                            Letters {
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, parent.width)
                                text: foot.between
                                elide: Text.ElideRight
                                color: Qt.rgba(look.letters.r, look.letters.g, look.letters.b, 0.85)
                                // The hint bar's letters, smaller as far as
                                // the room between the buttons needs (HintBar's
                                // 18 to 14 lines of 480).
                                font.pixelSize: Math.max(21, Math.min(27, Math.floor(27 * parent.width
                                                                                     / Math.max(1, betweenProbe.advanceWidth))))
                                TextMetrics {
                                    id: betweenProbe
                                    font.family: root.globalFont
                                    font.pixelSize: 27
                                    font.capitalization: Font.AllUppercase
                                    text: foot.between
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // The orb's screen, 4:3, drawn at twice the design's size for the orb's
    // picture (screen.frag): the game's pictures one after another, as the
    // Arcade screen's frame shows them, each cut to the next once it is in; a
    // screenshot fills the screen (a vertical game's 3:4, upright in the
    // middle), one of another shape keeps its own. Without one, which file
    // it would be, or that a set isn't a game; on a room, its game's name.
    readonly property Item screen: Item {
        id: screen
        width: 760
        height: 570
        readonly property string wanted: arcadeScreen.pictures.length > 0
                                         ? arcadeScreen.pictures[arcadeScreen.pictureIndex % arcadeScreen.pictures.length]
                                         : ""
        property Image shown: pictureA
        readonly property Image behind: shown === pictureA ? pictureB : pictureA
        onWantedChanged: showWanted()
        Component.onCompleted: showWanted()
        function showWanted() {
            if (wanted === "") {
                pictureA.source = ""
                pictureB.source = ""
            } else if (shown.source.toString() !== wanted) {
                behind.source = wanted
                takeOver(behind)
            }
        }
        function takeOver(picture) {
            if (picture === behind && picture.source.toString() === wanted
                && (picture.status === Image.Ready || picture.status === Image.Error))
                shown = picture
        }
        Rectangle {
            anchors.fill: parent
            color: "#000000"
        }
        readonly property real screenHeight: height
        readonly property real screenWidth: arcadeScreen.details.vertical ? Math.round(screenHeight * 3 / 4) : width
        component Picture: Image {
            id: picture
            readonly property real shape: implicitHeight > 0 ? implicitWidth / implicitHeight : 0
            readonly property bool screenLike: shape === 0 || (arcadeScreen.details.vertical ? shape >= 0.55 && shape <= 1
                                                                                             : shape >= 1 && shape <= 2)
            visible: screen.shown === picture && status === Image.Ready
            width: screenLike ? screen.screenWidth : Math.min(screen.screenWidth, Math.round(screen.screenHeight * shape))
            height: screenLike ? screen.screenHeight : Math.min(screen.screenHeight, Math.round(screen.screenWidth / shape))
            x: Math.round((screen.width - width) / 2)
            y: Math.round((screen.height - height) / 2)
            sourceSize.width: screen.screenWidth
            sourceSize.height: screen.screenHeight
            asynchronous: true
            onStatusChanged: screen.takeOver(picture)
        }
        Picture { id: pictureA }
        Picture { id: pictureB }
        Column {
            visible: (screen.wanted === "" || screen.shown.status === Image.Error) && arcadeScreen.current !== null
                     && arcadeScreen.filterId !== "search"
            anchors.centerIn: parent
            width: parent.width * 0.7
            spacing: 24
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: arcadeScreen.onRoom ? (arcadeScreen.current ? arcadeScreen.current.name || "" : "")
                      : arcadeScreen.isGame ? "No Picture" : "Not Recognized"
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: look.letters
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: arcadeScreen.nameLines * 2
            }
            Text {
                visible: arcadeScreen.isGame && !arcadeScreen.onRoom
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "snap/" + arcadeScreen.currentSet + ".png"
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                color: look.ink
                font.family: root.globalFont
                font.pixelSize: arcadeScreen.textLines * 2
            }
            Text {
                visible: arcadeScreen.isGame && !arcadeScreen.onRoom && arcadeScreen.pictureNote !== ""
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: arcadeScreen.pictureNote
                elide: Text.ElideRight
                color: look.ink
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: arcadeScreen.textLines * 2
            }
        }
    }
}
