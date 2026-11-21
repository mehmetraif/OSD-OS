import QtQuick
import QtTest
import Components

// The window and its changes (views/Components/WindowChange.qml), laid as
// Main.qml lays it, against a stand-in for Main.qml's root: what it is given
// lies in the face it turns; a view changes at once where no transition
// plays (none chosen, the very first view, into or out of a video's view,
// the effects resting, the boot screen up, the startup held), else once the
// old window is caught, or given up on; a change asked for while one is
// being caught makes that one first, and one made by a change is made at
// once; a shader that can't be used fades instead. And the cube's turn: the
// old window whole at its start, the new at its end, the two faces meeting
// along an edge all the way round, each shown only while it faces the eye.
Rectangle {
    id: root
    color: "black"
    width: 640
    height: 480
    // Main.qml's, as WindowChange reads them.
    property real sw: width
    property real sh: height
    property real canvasW: width
    property real canvasH: height
    property real px: 2
    property color primaryColor: "white"
    property string transition: "Fade"
    property bool effectsRest: false
    property bool bootActive: false
    property bool startupHolding: false
    readonly property var playbackView: /(Player|Play|Takeover|SignIn|Launch|Console)\.qml$/
    // No shader for any of the three: the software renderer draws none.
    readonly property var transitionShaders: ({ "Ripple": "", "Wave": "", "Drop": "" })
    function transitionUsable(name) { return !(name in transitionShaders) || transitionShaders[name] !== "" }
    // Main.qml's showView(): the view set, and noted.
    property var shown: []
    property var onShow: null
    function showView(loader, source, properties, base) {
        if (loader.setSource)
            loader.setSource(source, properties)
        else
            loader.source = source
        shown = shown.concat([String(source)])
        if (onShow)
            onShow()
    }
    function changeWindow(loader, source, properties, base) {
        windowChange.change(loader, source, properties, base)
    }
    // The old window being caught, as Main.qml says it to the routers.
    readonly property bool windowCatching: windowChange.capturing
    // AppCore, for a module's name (ModuleRouter).
    QtObject {
        id: appCore
        function get_module_info(id) { return {} }
    }

    WindowChange {
        id: windowChange
        anchors.fill: parent
        Rectangle {
            id: ground
            anchors.fill: parent
            color: "#0110C5"
        }
        Text {
            id: label
            text: view.source
            color: "white"
        }
    }
    // A module's router as a module's Root is one (module_router/Root.qml),
    // its views changing through the window.
    Loader {
        id: module
        anchors.fill: parent
    }
    // Two loaders' stand-ins (moduleLoader's, a module's own): only their
    // source is read and set.
    QtObject {
        id: view
        property string source: ""
    }
    QtObject {
        id: other
        property string source: ""
    }

    // A point of a face as the cube's matrix puts it on the screen.
    function project(matrix, x, y) {
        var p = matrix.times(Qt.vector4d(x, y, 0, 1))
        return Qt.point(p.x / p.w, p.y / p.w)
    }

    TestCase {
        name: "WindowChange"
        when: windowShown

        function init() {
            // Whatever played before is over.
            tryCompare(windowChange, "capturing", false, 1000)
            tryCompare(windowChange, "running", false, 3000)
            windowChange.visible = true
            root.transition = "Fade"
            root.effectsRest = false
            root.bootActive = false
            root.startupHolding = false
            root.onShow = null
            view.source = "List.qml"
            other.source = "Other.qml"
            root.shown = []
        }

        function test_contentInTheFace() {
            // In the face, not beside it: the face turns, the window's
            // pictures of it lie round it.
            verify(ground.parent !== windowChange)
            compare(ground.parent.parent, windowChange)
            compare(label.parent, ground.parent)
        }

        function test_atOnce_data() {
            return [
                { tag: "no transition", from: "List.qml", to: "Next.qml", transition: "" },
                { tag: "the first view", from: "", to: "Next.qml" },
                { tag: "into a player", from: "List.qml", to: "VideoPlayer.qml" },
                { tag: "out of a launch", from: "Launch.qml", to: "List.qml" },
                { tag: "the effects resting", from: "List.qml", to: "Next.qml", rest: true },
                { tag: "the boot screen up", from: "List.qml", to: "Next.qml", boot: true },
                { tag: "the startup held", from: "List.qml", to: "Next.qml", holding: true }
            ]
        }
        function test_atOnce(data) {
            if (data.transition !== undefined)
                root.transition = data.transition
            view.source = data.from
            root.effectsRest = !!data.rest
            root.bootActive = !!data.boot
            root.startupHolding = !!data.holding
            root.changeWindow(view, data.to, {})
            compare(view.source, data.to)
            verify(!windowChange.capturing)
            wait(50)
            verify(!windowChange.running)
            compare(windowChange.kind, "")
        }

        function test_fade() {
            root.changeWindow(view, "Next.qml", {})
            // The old window is caught first, the view as it was meanwhile.
            verify(windowChange.capturing)
            compare(view.source, "List.qml")
            tryCompare(windowChange, "capturing", false, 1000)
            compare(view.source, "Next.qml")
            compare(windowChange.kind, "Fade")
            verify(windowChange.progress < 1)
            tryCompare(windowChange, "running", false, 2000)
            compare(windowChange.progress, 1)
            compare(root.shown, ["Next.qml"])
        }

        function test_neverCaught() {
            // Nothing drawn, so nothing caught: the view changes all the
            // same, a moment later, without its transition.
            windowChange.visible = false
            root.changeWindow(view, "Next.qml", {})
            verify(windowChange.capturing)
            wait(100)
            compare(view.source, "List.qml")
            tryCompare(view, "source", "Next.qml", 1000)
            verify(!windowChange.capturing)
            verify(!windowChange.running)
        }

        function test_changeWhileCaught() {
            root.changeWindow(view, "Next.qml", {})
            verify(windowChange.capturing)
            // The change waiting is made, plainly, and the new one waits.
            root.changeWindow(other, "Again.qml", {})
            compare(view.source, "Next.qml")
            verify(windowChange.capturing)
            compare(other.source, "Other.qml")
            tryCompare(other, "source", "Again.qml", 1000)
            compare(root.shown, ["Next.qml", "Again.qml"])
            compare(windowChange.kind, "Fade")
        }

        // A view asked for again while the old window is caught (a second
        // select) is not: the view asking is the one leaving, and it would
        // be on the way back twice, with the next view's params. Back from
        // the view opened returns to it, and back meanwhile is not taken
        // either.
        function test_askedAgainWhileCaught() {
            module.setSource(Qt.resolvedUrl("module_router/Root.qml"))
            var router = module.item
            tryCompare(router.currentView, "name", "first", 1000)
            tryCompare(windowChange, "running", false, 3000)
            var first = router.currentView
            // The first view's way back is out of the module.
            var depth = router.navStack.length
            first.navigateTo("Page.qml", { name: "second" }, {})
            verify(windowChange.capturing)
            first.navigateTo("Page.qml", { name: "third" }, {})
            first.goBack()
            tryCompare(windowChange, "capturing", false, 1000)
            compare(router.currentView.name, "second")
            compare(router.navStack.length, depth + 1)
            tryCompare(windowChange, "running", false, 3000)
            router.currentView.goBack()
            tryCompare(windowChange, "capturing", false, 1000)
            compare(router.currentView.name, "first")
            compare(router.navStack.length, depth)
            module.source = ""
        }

        function test_changeMadeByAChange() {
            // A view made by a change that changes a view of its own (a
            // module's first) does so at once.
            var inner = ""
            root.onShow = function() {
                root.onShow = null
                root.changeWindow(other, "Inner.qml", {})
                inner = other.source
            }
            root.changeWindow(view, "Next.qml", {})
            tryCompare(view, "source", "Next.qml", 1000)
            compare(inner, "Inner.qml")
            compare(root.shown, ["Next.qml", "Inner.qml"])
            compare(windowChange.kind, "Fade")
        }

        function test_unusableShaderFades() {
            root.transition = "Ripple"
            root.changeWindow(view, "Next.qml", {})
            tryCompare(windowChange, "capturing", false, 1000)
            compare(windowChange.kind, "Fade")
            compare(windowChange.shader, "")
        }

        function test_noCubeNoTurn() {
            var still = Qt.matrix4x4()
            verify(windowChange.faceMatrix.fuzzyEquals(still, 1e-9))
            verify(windowChange.lastMatrix.fuzzyEquals(still, 1e-9))
            verify(windowChange.faceShown)
            verify(windowChange.lastShown)
        }

        function test_cube_data() {
            // Where the new face's edge meets the old one's: the new face
            // comes from the right (it turns left), the left, below (it
            // turns up) or above.
            return [
                { tag: "left", direction: 0, newEdge: [0, NaN], lastEdge: [1, NaN] },
                { tag: "right", direction: 1, newEdge: [1, NaN], lastEdge: [0, NaN] },
                { tag: "up", direction: 2, newEdge: [NaN, 0], lastEdge: [NaN, 1] },
                { tag: "down", direction: 3, newEdge: [NaN, 1], lastEdge: [NaN, 0] }
            ]
        }
        function test_cube(data) {
            windowChange.kind = "Cube"
            windowChange.direction = data.direction
            // A point along an edge, `along` of the way: x, y.
            function edge(ends, along) {
                return [isNaN(ends[0]) ? along * root.sw : ends[0] * root.sw,
                        isNaN(ends[1]) ? along * root.sh : ends[1] * root.sh]
            }
            var corners = [[0, 0], [root.sw, 0], [0, root.sh], [root.sw, root.sh]]
            try {
                // At the start the old window is as it was, the new one
                // turned away; at the end the other way round.
                windowChange.progress = 0
                for (var i = 0; i < corners.length; ++i) {
                    var p = root.project(windowChange.lastMatrix, corners[i][0], corners[i][1])
                    fuzzyCompare(p.x, corners[i][0], 0.01)
                    fuzzyCompare(p.y, corners[i][1], 0.01)
                }
                verify(windowChange.lastShown)
                verify(!windowChange.faceShown)
                windowChange.progress = 1
                for (var j = 0; j < corners.length; ++j) {
                    var q = root.project(windowChange.faceMatrix, corners[j][0], corners[j][1])
                    fuzzyCompare(q.x, corners[j][0], 0.01)
                    fuzzyCompare(q.y, corners[j][1], 0.01)
                }
                verify(windowChange.faceShown)
                verify(!windowChange.lastShown)
                // On the way, the faces meet along the edge they share, and
                // halfway both face the eye.
                var steps = [0.25, 0.5, 0.75]
                for (var s = 0; s < steps.length; ++s) {
                    windowChange.progress = steps[s]
                    for (var along = 0; along <= 1; along += 0.5) {
                        var n = edge(data.newEdge, along), l = edge(data.lastEdge, along)
                        var a = root.project(windowChange.faceMatrix, n[0], n[1])
                        var b = root.project(windowChange.lastMatrix, l[0], l[1])
                        fuzzyCompare(a.x, b.x, 0.01)
                        fuzzyCompare(a.y, b.y, 0.01)
                    }
                }
                windowChange.progress = 0.5
                verify(windowChange.faceShown)
                verify(windowChange.lastShown)
            } finally {
                windowChange.kind = ""
                windowChange.progress = 0
            }
        }
    }
}
