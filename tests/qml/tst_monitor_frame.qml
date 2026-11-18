import QtQuick
import QtTest
import Components

// Settings → Look → Menu Layout's monitor layouts (Studio Monitor, Broadcast,
// Flat Panel) as a frame every kind of view takes its place in: the shell
// draws the layout's chrome once (Main.qml's frameChrome, a MonitorChrome),
// and a view's shared components hand it what they would have drawn. Each
// kind of view, in each of the three: a menu's page (AppBar, MenuList,
// MenuRow, HelpLine, HintBar), a module's tree (TreeBrowser), a list that
// draws its own rows (ListPager), a question asked over a view
// (ChoiceOverlay, PromptScreen) and a layer that takes the whole screen (an
// OsdGround, as the on-screen keyboard's). The root stands in for Main.qml
// with its own rules for when the frame is up (monitorFramed) and when its
// colours are in force (monitorInk). In Standard and Camcorder nothing of it
// shows, and every component draws as it always has.
Rectangle {
    id: root
    color: schemePaper
    width: 640
    height: 480
    // Main.qml's, as the views and their components read them.
    property real sw: width
    property real sh: height
    property real px: 2
    property string globalFont: "monospace"
    property color schemeInk: "#FFFFFF"
    property color schemePaper: "#0110C5"
    property color primaryColor: monitorInk ? menuPalette.ink : schemeInk
    property color surfaceColor: monitorInk ? menuPalette.ground : schemePaper
    readonly property color secondaryColor: primaryColor
    readonly property color tertiaryColor: primaryColor
    readonly property color accentColor: primaryColor
    property var skin: ({})
    property bool hintBar: true
    property bool helpLine: true
    property var statusIcons: []
    readonly property real menuRowHeight: sh * 0.0583333
    readonly property real hintRoom: hintBar ? 0 : menuRowHeight
    readonly property real helpRoom: helpLine ? 0 : menuRowHeight
    readonly property real helpLineMargin: hintBar ? sh * 0.1583333 : sh * 0.1041667
    property rect contentBox: Qt.rect(sw * 0.115625, sh * 0.11875, sw * 0.76875, sh * 0.7770833)
    property rect osdWindow: contentBox
    property string osdBackground: "Full"
    property string osdFrame: "On"
    property var backgroundEffect: ({})
    property string backgroundShader: ""
    property bool effectsRest: true
    property bool screenSaverActive: false
    property real fxTime: 0
    property bool videoBehind: false
    property int backdropSolidity: 100
    property var hints: ({ back: "[ESC]", navigate: "[▲▼]", arrows: "[▲▼◄►]", change: "[◄►]",
                           browse: "[►]", left: "[◄]", select: "[ENTER]", play_pause: "[SPACE]" })
    function skinIcon(url) { return url }
    function selectorShown(item, shown) {}

    // Menu Layout, its colours, and the frame, as Main.qml has them.
    property string menuLayout: "Standard"
    readonly property var menuPalette: ({
        "Studio Monitor": { ground: "#050608", ink: "#FFFFFF", dim: "#6E7891", accent: "#2350C8",
                            soft: "#CFDDFF", mark: "#F4D21C", bar: "#050608" },
        "Broadcast":      { ground: "#3A55B4", ink: "#FFFFFF", dim: "#94A8EC", accent: "#FFFFFF",
                            soft: "#FFFFFF", mark: "#FFFFFF", bar: "#3A55B4" },
        "Flat Panel":     { ground: "#000000", ink: "#EDEDED", dim: "#7B7B7B", accent: "#7B7B7B",
                            soft: "#9A9A9A", mark: "#F6CF00", bar: "#4C4C4C" }
    })[menuLayout] || null
    // The grounds laid over a view, as Main.qml's selectors keep them: one
    // not laid in the layout takes the view out of the frame's colours.
    property var covers: []
    function coverShown(item, shown) {
        var list = covers.filter(function(c) { return c !== item })
        if (shown)
            list.push(item)
        covers = list
    }
    property bool viewInFrame: true
    property bool framed: false
    readonly property bool monitorFramed: menuPalette !== null && viewInFrame
    readonly property bool frameCovered: covers.some(function(c) { return c.inLayout !== true })
    readonly property bool monitorInk: monitorFramed && !frameCovered
    readonly property alias chrome: frameChrome
    readonly property color cursorColor: !monitorInk ? primaryColor
                                       : menuLayout === "Flat Panel" ? menuPalette.bar : menuPalette.mark
    readonly property color cursorInk: !monitorInk ? surfaceColor
                                     : menuLayout === "Flat Panel" ? menuPalette.ink : menuPalette.ground
    readonly property real paneX: monitorFramed ? frameChrome.listX : framed ? sw * 0.209375 : sw * 0.115625
    readonly property real paneWidth: monitorFramed ? frameChrome.listWidth : framed ? sw * 0.675 : sw * 0.76875
    readonly property real listY: monitorFramed ? frameChrome.listY : sh * 0.25
    readonly property real listHeight: monitorFramed ? frameChrome.listHeight : sh * 0.4666667 + helpRoom + hintRoom
    readonly property real tallListHeight: monitorFramed ? frameChrome.tallHeight : sh * 0.525 + hintRoom
    readonly property real helpedListHeight: monitorFramed ? frameChrome.helpedHeight : sh * 0.4666667 + hintRoom
    readonly property real paneTop: monitorFramed ? frameChrome.listY : sh * 0.2083333
    readonly property real paneBottom: monitorFramed ? frameChrome.tallBottom
                                     : hintBar ? sh * 0.8333333 : contentBox.y + contentBox.height
    MonitorChrome {
        id: frameChrome
        anchors.fill: parent
        visible: root.monitorInk
    }

    // A menu's page, as the pages opened from Settings are made: Wi-Fi's
    // title with its address beside it, twenty rows with a value each, a
    // help line, the hint bar, and a question it asks.
    Component {
        id: menuPage
        FocusScope {
            property alias bar: bar
            property alias list: list
            property alias help: help
            property alias hint: hint
            property alias question: question
            readonly property var rows: {
                var made = []
                for (var i = 0; i < 20; i++)
                    made.push({ label: "Row " + (i + 1), value: i % 2 ? "On" : "Off", description: "What row " + (i + 1) + " does" })
                return made
            }
            AppBar {
                id: bar
                title: "Wi-Fi"
                subtitle: "192.0.2.2"
                path: ["Wi-Fi"]
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.topMargin: root.sh * 0.125
                anchors.leftMargin: root.sw * 0.125
            }
            MenuList {
                id: list
                model: parent.rows
                focus: true
                delegate: MenuRow {
                    width: list.width
                    height: root.sh * 0.0583333
                    label: modelData.label
                    value: modelData.value
                    changes: true
                    selected: list.currentIndex === index
                }
            }
            HelpLine {
                id: help
                text: parent.rows[list.currentIndex].description
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.bottomMargin: root.helpLineMargin
                anchors.leftMargin: root.sw * 0.125
            }
            HintBar {
                id: hint
                text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.bottomMargin: root.sh * 0.1041667
                anchors.leftMargin: root.sw * 0.125
            }
            ChoiceOverlay {
                id: question
                anchors.fill: parent
                promptText: "Forget this network?"
                subtitleText: "Home"
                choices: [{ label: "Forget", action: "forget" }, { label: "Cancel", action: "cancel" }]
                onClosed: list.forceActiveFocus()
            }
        }
    }

    // A module's tree, as Local Files' is: the title bar naming the folder.
    Component {
        id: treePage
        FocusScope {
            property alias tree: tree
            property alias hint: hint
            AppBar {
                title: "Local Files"
                subtitle: tree.folderName
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.topMargin: root.sh * 0.125
                anchors.leftMargin: root.sw * 0.125
            }
            TreeBrowser {
                id: tree
                anchors.fill: parent
                focus: true
                rootPath: "top"
                fetch: function(path, preview) {
                    if (path === "top")
                        return [{ name: "Action", path: "top/action", isFolder: true },
                                { name: "Comedy", path: "top/comedy", isFolder: true }]
                    return [{ name: "A Film (1988)", path: path + "/a", isFolder: false },
                            { name: "Another Film (1990)", path: path + "/b", isFolder: false }]
                }
            }
            HintBar {
                id: hint
                text: root.hints.back + ":BACK " + root.hints.arrows + ":NAVIGATE " + root.hints.select + ":OPEN"
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.bottomMargin: root.sh * 0.1041667
                anchors.leftMargin: root.sw * 0.125
            }
        }
    }

    // A list that draws its own rows, as a media server's libraries do: laid
    // out by the pane, with no help line under it, and paged by a ListPager.
    Component {
        id: listPage
        FocusScope {
            property alias list: list
            property alias pager: pager
            AppBar {
                title: "Jellyfin"
                subtitle: "Movies"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.topMargin: root.sh * 0.125
                anchors.leftMargin: root.sw * 0.125
            }
            ListView {
                id: list
                model: 25
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.topMargin: root.listY
                anchors.leftMargin: root.paneX
                width: root.paneWidth
                height: root.tallListHeight
                clip: true
                focus: true
                delegate: Item {
                    width: list.width
                    height: root.sh * 0.0583333
                    Loader {
                        anchors.fill: parent
                        active: root.monitorInk === true
                        sourceComponent: MonitorRow {
                            label: "Film " + (index + 1)
                            selected: list.currentIndex === index
                        }
                    }
                }
            }
            ListPager {
                id: pager
                list: list
            }
        }
    }

    // A layer that takes the whole screen over a view, as the on-screen
    // keyboard's ground does.
    Component {
        id: coverLayer
        Item {
            anchors.fill: parent
            OsdGround {
                anchors.fill: parent
            }
        }
    }

    // A line too narrow for its label, as Game Buttons' are beside the
    // control panel in Camcorder's pane.
    Component {
        id: narrowRow
        Item {
            property alias row: row
            MenuRow {
                id: row
                y: root.sh * 0.25
                x: root.sw * 0.209375
                width: root.sw * 0.3
                height: root.sh * 0.0583333
                label: "Set One By One"
                opens: true
            }
        }
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
        onLoaded: item.forceActiveFocus()
    }
    Loader {
        id: coverLoader
        anchors.fill: parent
        active: false
        sourceComponent: coverLayer
    }

    TestCase {
        name: "MonitorFrame"
        when: windowShown

        readonly property var monitorLayouts: ["Studio Monitor", "Broadcast", "Flat Panel"]

        function init() {
            coverLoader.active = false
            loader.sourceComponent = undefined
            root.menuLayout = "Standard"
            root.framed = false
            root.viewInFrame = true
            root.hintBar = true
            root.helpLine = true
            frameChrome.basePath = []
            frameChrome.pageTo(null)
            wait(0)
            compare(frameChrome.parts.length, 0, "a view gone leaves nothing in the frame")
        }
        function cleanup() {
            coverLoader.active = false
            loader.sourceComponent = undefined
        }

        // A view opened, as Main.qml opens one (showView()): no page of the
        // last view's rows over it.
        function open(page, layout) {
            root.menuLayout = layout
            frameChrome.pageTo(null)
            loader.sourceComponent = page
            verify(loader.item !== null)
            wait(0)
            return loader.item
        }
        // The first item under one, all the way down, that passes.
        function find(item, passes) {
            for (var i = 0; i < item.children.length; i++) {
                var child = item.children[i]
                if (passes(child))
                    return child
                var deeper = find(child, passes)
                if (deeper)
                    return deeper
            }
            return null
        }

        function layouts() {
            return monitorLayouts.map(function(layout) { return { tag: layout, layout: layout } })
        }

        // Standard and Camcorder: no frame, and every bar draws itself.
        function test_noFrameInTheTwoColourLayouts_data() {
            return [{ tag: "Standard", layout: "Standard" }, { tag: "Camcorder", layout: "Camcorder" }]
        }
        function test_noFrameInTheTwoColourLayouts(data) {
            var page = open(menuPage, data.layout)
            verify(!root.monitorFramed)
            verify(!frameChrome.visible)
            compare(page.bar.opacity, 1)
            compare(page.hint.opacity, 1)
            compare(page.help.opacity, 1)
            compare(page.list.y, root.sh * 0.25)
            compare(page.list.height, root.sh * 0.4666667)
            compare(page.list.x, root.sw * 0.115625)
            compare(Qt.colorEqual(root.primaryColor, root.schemeInk), true)
            var row = find(page.list, function(item) { return item.label === "Row 1" && item.handed !== undefined })
            verify(row !== null)
            verify(!row.handed, "a row draws itself, the camcorder's way")
        }

        // A page's bars hand what they hold to the frame and draw nothing;
        // the frame writes it the layout's way, the title as the way from
        // the top level.
        function test_barsHandedToTheFrame_data() {
            return [{ tag: "Studio Monitor", layout: "Studio Monitor", title: "Wi-Fi",
                      guide: ["[ESC]:BACK", "[▲▼]:NAVIGATE", "[ENTER]:SELECT"] },
                    // The keys that move first and back last, as a monitor's
                    // guide lists them, in a monitor's words.
                    { tag: "Broadcast", layout: "Broadcast", title: "Network / Wi-Fi",
                      guide: ["TO SELECT :[▲▼]", "TO ENTER :[ENTER]", "TO GO BACK :[ESC]"] },
                    { tag: "Flat Panel", layout: "Flat Panel", title: ">Settings >Network >Wi-Fi",
                      guide: ["[ESC] Back", "[▲▼] Navigate", "[ENTER] Select"] }]
        }
        function test_barsHandedToTheFrame(data) {
            frameChrome.basePath = ["Settings", "Network"]
            var page = open(menuPage, data.layout)
            verify(root.monitorFramed && root.monitorInk)
            verify(frameChrome.visible)
            compare(page.bar.opacity, 0)
            compare(page.hint.opacity, 0)
            compare(page.help.opacity, 0)
            compare(frameChrome.title, data.title)
            // Broadcast's colons stand in a column: the words padded to it.
            var guide = frameChrome.guide
            compare(guide.map(function(line) { return line.replace(/ +/g, " ") }), data.guide)
            if (data.layout === "Broadcast") {
                compare(guide[0].indexOf(":"), guide[1].indexOf(":"))
                compare(guide[0].indexOf(":"), guide[2].indexOf(":"))
            }
            compare(frameChrome.helpText, "What row 1 does")
            keyClick(Qt.Key_Down)
            compare(frameChrome.helpText, "What row 2 does")
            // The view's two colours are the layout's.
            verify(Qt.colorEqual(root.primaryColor, root.menuPalette.ink))
            verify(Qt.colorEqual(root.surfaceColor, root.menuPalette.ground))
            // The way to whatever the page opens is its own.
            compare(frameChrome.viewPath, ["Settings", "Network", "Wi-Fi"])
        }

        // Its rows lie where the frame has them, drawn as the layout draws a
        // row, and a hidden Help Line's or Hint Bar's room goes to them.
        function test_rowsInThePane_data() { return layouts() }
        function test_rowsInThePane(data) {
            var page = open(menuPage, data.layout)
            compare(page.list.x, frameChrome.listX)
            compare(page.list.y, frameChrome.listY)
            compare(page.list.width, frameChrome.listWidth)
            compare(page.list.height, frameChrome.listHeight)
            var row = find(page.list, function(item) { return item.label === "Row 1" && item.handed !== undefined })
            verify(row !== null && row.handed)
            var drawn = find(row, function(item) { return item.pal !== undefined && item.label === "Row 1" })
            verify(drawn !== null, "MonitorRow draws it")
            compare(drawn.value, "Off")
            verify(drawn.selected)
            var withBoth = page.list.height
            // Whole rows of it.
            fuzzyCompare(withBoth / frameChrome.rowHeight, Math.round(withBoth / frameChrome.rowHeight), 0.001)
            root.helpLine = false
            verify(page.list.height > withBoth, "the help line's room is the rows'")
            compare(frameChrome.helpText, "")
            var withoutHelp = page.list.height
            root.hintBar = false
            verify(page.list.height >= withoutHelp)
            verify(page.list.y + page.list.height <= frameChrome.tallBottom + 0.5)
        }

        // Flat Panel: the rows a page at a time, the frame saying which.
        function test_flatPanelPagesAList() {
            var page = open(menuPage, "Flat Panel")
            var view = find(page.list, function(item) { return item.contentY !== undefined && item.count === 20 })
            verify(view !== null)
            var per = Math.floor(view.height / frameChrome.rowHeight + 0.001)
            tryCompare(frameChrome, "pages", Math.ceil(20 / per))
            compare(frameChrome.page, 0)
            for (var i = 0; i < per; i++)
                keyClick(Qt.Key_Down)
            compare(page.list.currentIndex, per)
            compare(frameChrome.page, 1)
            fuzzyCompare(view.contentY, view.originY + per * frameChrome.rowHeight, 0.01)
            keyClick(Qt.Key_Up)
            compare(frameChrome.page, 0)
            compare(view.contentY, view.originY)
            // Round from the first row to the last: the last page, its top
            // within the list.
            page.list.currentIndex = 0
            keyClick(Qt.Key_Up)
            compare(page.list.currentIndex, 19)
            compare(frameChrome.page, frameChrome.pages - 1)
            verify(view.contentY <= view.originY + view.contentHeight - view.height + 0.5)
            // The other two scroll, and count no pages.
            var other = open(menuPage, "Studio Monitor")
            compare(frameChrome.pages, 1)
        }

        // A list that draws its own rows: paged by its ListPager, as tall as
        // the pane with no help line under it.
        function test_ownListInThePane_data() { return layouts() }
        function test_ownListInThePane(data) {
            var page = open(listPage, data.layout)
            compare(page.list.x, frameChrome.listX)
            compare(page.list.y, frameChrome.listY)
            compare(page.list.height, frameChrome.tallHeight)
            compare(frameChrome.helpText, "")
            var per = Math.floor(page.list.height / frameChrome.rowHeight + 0.001)
            if (data.layout === "Flat Panel") {
                verify(page.pager.paging)
                tryCompare(frameChrome, "pages", Math.ceil(25 / per))
                page.list.currentIndex = per
                compare(frameChrome.page, 1)
                fuzzyCompare(page.list.contentY, page.list.originY + per * frameChrome.rowHeight, 0.01)
                // A list that doesn't show counts no pages (Camcorder's
                // column, which is hidden in this layout; a list under a
                // keyboard).
                page.list.visible = false
                verify(!page.pager.paged && !page.pager.paging)
                page.list.visible = true
                verify(page.pager.paging)
            } else {
                verify(!page.pager.paging)
                compare(frameChrome.pages, 1)
            }
            compare(frameChrome.title, data.layout === "Studio Monitor" ? "Movies"
                                     : data.layout === "Broadcast" ? "Movies" : ">Jellyfin >Movies")
        }

        // A question is asked in the layout: its title the frame's, its
        // answers over the view's rows, the view's help line not shown; and
        // the view's own come back with it gone.
        function test_questionAskedInTheFrame_data() { return layouts() }
        function test_questionAskedInTheFrame(data) {
            frameChrome.basePath = ["Settings", "Network"]
            var page = open(menuPage, data.layout)
            var own = frameChrome.title
            page.question.open()
            wait(0)
            verify(root.monitorInk, "a question leaves the frame's colours in force")
            verify(frameChrome.frontLayer !== null)
            compare(frameChrome.title, "Forget this network?")
            compare(frameChrome.helpText, "")
            compare(frameChrome.viewPath, ["Settings", "Network", "Wi-Fi"])
            var prompt = find(page.question, function(item) { return item.choices !== undefined && item.paned !== undefined })
            verify(prompt !== null && prompt.monitor)
            var ground = find(prompt, function(item) { return item.frameRole === "layer" })
            verify(ground !== null && ground.paned, "its ground covers the pane, not the screen")
            // What it covers has the corners of what is under it: Studio
            // Monitor's inside is round, and the frame shows in its corners.
            var covered = find(ground, function(item) {
                return item.radius !== undefined && item.visible && item.width === frameChrome.cover.width
            })
            verify(covered !== null)
            compare(covered.radius, frameChrome.coverCorner)
            compare(frameChrome.coverCorner > 0, data.layout === "Studio Monitor")
            keyClick(Qt.Key_Down)
            compare(page.question.choiceIndex, 1)
            keyClick(Qt.Key_Escape)
            verify(!page.question.visible)
            compare(frameChrome.frontLayer, null)
            compare(frameChrome.title, own)
            compare(frameChrome.helpText, "What row 1 does")
        }

        // Studio Monitor's window is one blue shape: the tab goes down into
        // the frame past its corner, the icons stand in the middle of its
        // side and the key guide is written on its foot. The black inside
        // lies within it, where the rows are.
        function test_studioWindowIsOnePiece() {
            frameChrome.icons = ["play", "stop", "eject"]
            var page = open(menuPage, "Studio Monitor")
            var part = function(name) {
                return find(frameChrome, function(item) { return item.objectName === name })
            }
            var body = part("frameBody"), tab = part("frameTab"), inside = part("frameInside")
            var strip = part("frameStrip"), guide = part("frameGuide")
            verify(body && tab && inside && strip && guide)
            verify(body.visible && tab.visible && inside.visible)
            // The tab: the frame's colour, from its left side, down past its
            // top's corner.
            verify(Qt.colorEqual(tab.color, body.color))
            compare(tab.x, body.x)
            verify(tab.y < body.y)
            verify(tab.y + tab.height >= body.y + body.radius, "the tab ends inside the frame")
            // The inside: within the frame on every side, and where the
            // rows and a question are laid.
            var edge = inside.y - body.y
            verify(edge > 0)
            fuzzyCompare(body.x + body.width - (inside.x + inside.width), edge, 0.01)
            compare(inside.x, frameChrome.cover.x)
            compare(inside.width, frameChrome.cover.width)
            verify(page.list.x >= inside.x && page.list.x + page.list.width <= inside.x + inside.width)
            verify(page.list.y >= inside.y && page.list.y + page.list.height <= inside.y + inside.height)
            // The icons: as much of the frame before them as after.
            compare(strip.children.length > 0, true)
            fuzzyCompare(strip.x - body.x, inside.x - (strip.x + strip.width), 0.01)
            // The guide: on the foot, from the inside's end to the frame's.
            verify(guide.visible)
            verify(guide.text.indexOf("BACK") >= 0)
            compare(guide.y, inside.y + inside.height)
            fuzzyCompare(guide.y + guide.height, body.y + body.height, 0.01)
            verify(guide.height > edge)
            // Without the Hint Bar the foot is an edge as the others are.
            root.hintBar = false
            wait(0)
            verify(!guide.visible)
            fuzzyCompare(body.y + body.height - (inside.y + inside.height), edge, 0.01)
            frameChrome.icons = []
        }

        // A layer over the whole screen takes the view out of the frame
        // while it is up: the scheme's colours, and bars that draw.
        function test_fullScreenLayerLeavesTheFrame_data() { return layouts() }
        function test_fullScreenLayerLeavesTheFrame(data) {
            var page = open(menuPage, data.layout)
            verify(root.monitorInk)
            coverLoader.active = true
            wait(0)
            verify(root.monitorFramed, "the view keeps its place")
            verify(!root.monitorInk)
            verify(Qt.colorEqual(root.primaryColor, root.schemeInk))
            compare(page.bar.opacity, 1)
            compare(page.list.y, frameChrome.listY)
            // The chrome goes with the ink: the layer's ground is the
            // stage's, and the tab and the window's corners showed round it.
            verify(!frameChrome.visible, "no chrome round the layer")
            coverLoader.active = false
            wait(0)
            verify(root.monitorInk)
            verify(frameChrome.visible)
            compare(page.bar.opacity, 0)
        }

        // A view that is not in the frame (a player) has none.
        function test_viewOutsideTheFrame_data() { return layouts() }
        function test_viewOutsideTheFrame(data) {
            root.viewInFrame = false
            var page = open(menuPage, data.layout)
            verify(!root.monitorFramed && !root.monitorInk)
            verify(!frameChrome.visible)
            compare(page.bar.opacity, 1)
            compare(page.list.y, root.sh * 0.25)
        }

        // A module's tree is embedded as it is, the pane a window onto it:
        // its sides the pane's, with room for the layout's cursor, its foot
        // the pane's.
        function test_treeInThePane_data() { return layouts() }
        function test_treeInThePane(data) {
            var page = open(treePage, data.layout)
            var tree = page.tree
            verify(tree.monitor)
            var room = data.layout === "Flat Panel" ? tree.pad : frameChrome.gutter
            compare(tree.leftEdge, frameChrome.listX + room)
            compare(tree.rightEdge, frameChrome.listX + frameChrome.listWidth - tree.pad)
            compare(tree.areaBottom, frameChrome.tallBottom)
            compare(tree.windowed, data.layout === "Studio Monitor")
            compare(frameChrome.title, data.layout === "Flat Panel" ? ">Local Files" : "Local Files")
            keyClick(Qt.Key_Right)
            tryCompare(tree, "folderName", "Action")
            compare(frameChrome.title, data.layout === "Studio Monitor" ? "Action"
                                     : data.layout === "Broadcast" ? "Action" : ">Local Files >Action")
            // The cursor is the layout's: no bar of the scheme's, and what
            // is written on it in the layout's text colour.
            var cursor = find(tree, function(item) { return item.marked !== undefined && item.visible })
            verify(cursor !== null)
            compare(cursor.marked, data.layout !== "Flat Panel")
            verify(Qt.colorEqual(cursor.ink, root.menuPalette.ink))
        }

        // A label too long for its line is cut short with "…" a cell before
        // the ► at its end, never drawn over it; one that fits is whole.
        function test_longLabelStopsBeforeTheEnd_data() {
            return [{ tag: "Standard", layout: "Standard" }, { tag: "Camcorder", layout: "Camcorder" }]
        }
        function test_longLabelStopsBeforeTheEnd(data) {
            var row = open(narrowRow, data.layout).row
            var label = find(row, function(item) { return item.text === "Set One By One" && item.elide !== undefined })
            verify(label !== null)
            verify(label.truncated, "the label is cut short")
            verify(label.x + label.width <= row.pad + (row.endCell - 1) * row.cell + 0.5, "clear of the ►")
            row.label = "Layout"
            wait(0)
            verify(!label.truncated)
            compare(label.width, label.implicitWidth)
        }

        // The key guide made from a hint line, and the way joined.
        function test_guideAndPath() {
            compare(frameChrome.hintPairs("[ESC]:BACK [▲▼]:NAVIGATE [►]:ADD TO PLAYLIST [ENTER]:PLAY").length, 4)
            compare(frameChrome.hintPairs("[ESC]:BACK [▲▼]:NAVIGATE [►]:ADD TO PLAYLIST [ENTER]:PLAY")[2].label, "ADD TO PLAYLIST")
            compare(frameChrome.hintPairs("[►]/[ENTER]:OPEN")[0].key, "[►]/[ENTER]")
            compare(frameChrome.hintPairs("HOLD ANY BUTTON TO STOP (9)").length, 0)
            compare(frameChrome.titleCase("ADD TO PLAYLIST"), "Add To Playlist")
            compare(frameChrome.joined(["Settings", "TV / Movie"], ["Settings", "Plex"]), ["Settings", "TV / Movie", "Plex"])
            compare(frameChrome.joined(["Settings", "Look"], ["Settings", "Look", "Effects"]), ["Settings", "Look", "Effects"])
            compare(frameChrome.joined([], ["Local Files", "Action"]), ["Local Files", "Action"])
            // A hint line that is no keys is said as it is.
            var page = open(menuPage, "Studio Monitor")
            page.hint.text = "HOLD ANY BUTTON TO STOP (9)"
            compare(frameChrome.guide, ["HOLD ANY BUTTON TO STOP (9)"])
            // A view's own lines for the guide come first.
            page.hint.guide = ["[▲▼]:MENU/ITEM"]
            compare(frameChrome.guide, ["[▲▼]:MENU/ITEM"])
        }
    }
}
