import QtQuick
import QtTest
import Components
import OSDOS.Testing

// Settings → Look → Menu Layout's monitor layouts (Studio Monitor, Broadcast,
// Flat Panel) as a frame every kind of view takes its place in: the shell
// draws the layout's chrome once (Main.qml's frameChrome, a MonitorChrome),
// and a view's shared components hand it what they would have drawn. Each
// kind of view, in each of the three: a menu's page (AppBar, MenuList,
// MenuRow, HelpLine, HintBar), a module's tree (TreeBrowser), a list that
// draws its own rows (ListPager), a question asked over a view
// (ChoiceOverlay, PromptScreen) and a layer that takes the whole screen (an
// OsdGround, as the on-screen keyboard's). The root stands in for Main.qml
// with its rules for when the frame is up (monitorFramed) and when its
// colours are in force (monitorInk). In Standard and Camcorder nothing of it
// shows, and every component draws as it always has.
StandInRoot {
    id: root
    // Video 1's colours, the screen's ground whatever the layout's.
    color: schemePaper
    schemeInk: "#FFFFFF"
    schemePaper: "#0110C5"

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
            }
            MenuList {
                id: list
                model: parent.rows
                focus: true
                delegate: MenuRow {
                    width: list.width
                    height: root.menuRowHeight
                    label: modelData.label
                    value: modelData.value
                    changes: true
                    selected: list.currentIndex === index
                }
            }
            HelpLine {
                id: help
                text: parent.rows[list.currentIndex].description
            }
            HintBar {
                id: hint
                text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
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
                    height: root.menuRowHeight
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
                height: root.menuRowHeight
                label: "Set One By One"
                opens: true
            }
        }
    }

    // The bars declared bare, as a view declares them, and a hint bar a view
    // anchors elsewhere.
    Component {
        id: barePage
        FocusScope {
            property alias bar: bar
            property alias help: help
            property alias hint: hint
            property alias moved: moved
            AppBar {
                id: bar
                title: "Wi-Fi"
            }
            HelpLine {
                id: help
                text: "What the row does"
            }
            HintBar {
                id: hint
                text: root.hints.back + ":BACK"
            }
            HintBar {
                id: moved
                text: root.hints.back + ":BACK"
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 10
                anchors.rightMargin: 5
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
            root.chrome.basePath = []
            root.chrome.pageTo(null)
            wait(0)
            compare(root.chrome.parts.length, 0, "a view gone leaves nothing in the frame")
        }
        function cleanup() {
            coverLoader.active = false
            loader.sourceComponent = undefined
        }

        // A view opened, as Main.qml opens one (showView()): no page of the
        // last view's rows over it.
        function open(page, layout) {
            root.menuLayout = layout
            root.chrome.pageTo(null)
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
            verify(!root.chrome.visible)
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

        // The title bar, the help line and the hint bar each stand in their
        // place on their own, where every view had anchored them: the title
        // bar the gutter in from the top left, the hint bar above the foot,
        // the help line over it, or in its place with the hint bar off. A
        // view's anchors win over that place.
        function test_barsInTheirPlace() {
            var page = open(barePage, "Standard")
            compare(page.bar.x, root.sw * 0.125)
            compare(page.bar.y, root.sh * 0.125)
            compare(page.hint.x, root.sw * 0.125)
            compare(page.hint.y, page.height - page.hint.height - root.sh * 0.1041667)
            compare(page.help.x, root.sw * 0.125)
            compare(page.help.y, page.height - page.help.height - root.sh * 0.1583333)
            root.hintBar = false
            compare(page.help.y, page.height - page.help.height - root.sh * 0.1041667)
            // The foot followed as the view's height changes.
            page.height = 400
            compare(page.hint.y, 400 - page.hint.height - root.sh * 0.1041667)
            compare(page.help.y, 400 - page.help.height - root.sh * 0.1041667)
            compare(page.bar.y, root.sh * 0.125)
            // Anchored elsewhere, where the anchors put it.
            compare(page.moved.x, page.width - page.moved.width - 5)
            compare(page.moved.y, 10)
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
            root.chrome.basePath = ["Settings", "Network"]
            var page = open(menuPage, data.layout)
            verify(root.monitorFramed && root.monitorInk)
            verify(root.chrome.visible)
            compare(page.bar.opacity, 0)
            compare(page.hint.opacity, 0)
            compare(page.help.opacity, 0)
            compare(root.chrome.title, data.title)
            // Broadcast's colons stand in a column: the words padded to it.
            var guide = root.chrome.guide
            compare(guide.map(function(line) { return line.replace(/ +/g, " ") }), data.guide)
            if (data.layout === "Broadcast") {
                compare(guide[0].indexOf(":"), guide[1].indexOf(":"))
                compare(guide[0].indexOf(":"), guide[2].indexOf(":"))
            }
            compare(root.chrome.helpText, "What row 1 does")
            keyClick(Qt.Key_Down)
            compare(root.chrome.helpText, "What row 2 does")
            // The view's two colours are the layout's.
            verify(Qt.colorEqual(root.primaryColor, root.menuPalette.ink))
            verify(Qt.colorEqual(root.surfaceColor, root.menuPalette.ground))
            // The way to whatever the page opens is its own.
            compare(root.chrome.viewPath, ["Settings", "Network", "Wi-Fi"])
        }

        // Its rows lie where the frame has them, drawn as the layout draws a
        // row, and a hidden Help Line's or Hint Bar's room goes to them.
        function test_rowsInThePane_data() { return layouts() }
        function test_rowsInThePane(data) {
            var page = open(menuPage, data.layout)
            compare(page.list.x, root.chrome.listX)
            compare(page.list.y, root.chrome.listY)
            compare(page.list.width, root.chrome.listWidth)
            compare(page.list.height, root.chrome.listHeight)
            var row = find(page.list, function(item) { return item.label === "Row 1" && item.handed !== undefined })
            verify(row !== null && row.handed)
            var drawn = find(row, function(item) { return item.pal !== undefined && item.label === "Row 1" })
            verify(drawn !== null, "MonitorRow draws it")
            compare(drawn.value, "Off")
            verify(drawn.selected)
            var withBoth = page.list.height
            // Whole rows of it.
            fuzzyCompare(withBoth / root.chrome.rowHeight, Math.round(withBoth / root.chrome.rowHeight), 0.001)
            root.helpLine = false
            verify(page.list.height > withBoth, "the help line's room is the rows'")
            compare(root.chrome.helpText, "")
            var withoutHelp = page.list.height
            root.hintBar = false
            verify(page.list.height >= withoutHelp)
            verify(page.list.y + page.list.height <= root.chrome.tallBottom + 0.5)
        }

        // Flat Panel: the rows a page at a time, the frame saying which.
        function test_flatPanelPagesAList() {
            var page = open(menuPage, "Flat Panel")
            var view = find(page.list, function(item) { return item.contentY !== undefined && item.count === 20 })
            verify(view !== null)
            var per = Math.floor(view.height / root.chrome.rowHeight + 0.001)
            tryCompare(root.chrome, "pages", Math.ceil(20 / per))
            compare(root.chrome.page, 0)
            for (var i = 0; i < per; i++)
                keyClick(Qt.Key_Down)
            compare(page.list.currentIndex, per)
            compare(root.chrome.page, 1)
            fuzzyCompare(view.contentY, view.originY + per * root.chrome.rowHeight, 0.01)
            keyClick(Qt.Key_Up)
            compare(root.chrome.page, 0)
            compare(view.contentY, view.originY)
            // Round from the first row to the last: the last page, its top
            // within the list.
            page.list.currentIndex = 0
            keyClick(Qt.Key_Up)
            compare(page.list.currentIndex, 19)
            compare(root.chrome.page, root.chrome.pages - 1)
            verify(view.contentY <= view.originY + view.contentHeight - view.height + 0.5)
            // The other two scroll, and count no pages.
            var other = open(menuPage, "Studio Monitor")
            compare(root.chrome.pages, 1)
        }

        // A list that draws its own rows: paged by its ListPager, as tall as
        // the pane with no help line under it.
        function test_ownListInThePane_data() { return layouts() }
        function test_ownListInThePane(data) {
            var page = open(listPage, data.layout)
            compare(page.list.x, root.chrome.listX)
            compare(page.list.y, root.chrome.listY)
            compare(page.list.height, root.chrome.tallHeight)
            compare(root.chrome.helpText, "")
            var per = Math.floor(page.list.height / root.chrome.rowHeight + 0.001)
            if (data.layout === "Flat Panel") {
                verify(page.pager.paging)
                tryCompare(root.chrome, "pages", Math.ceil(25 / per))
                page.list.currentIndex = per
                compare(root.chrome.page, 1)
                fuzzyCompare(page.list.contentY, page.list.originY + per * root.chrome.rowHeight, 0.01)
                // A list that doesn't show counts no pages (Camcorder's
                // column, which is hidden in this layout; a list under a
                // keyboard).
                page.list.visible = false
                verify(!page.pager.paged && !page.pager.paging)
                page.list.visible = true
                verify(page.pager.paging)
            } else {
                verify(!page.pager.paging)
                compare(root.chrome.pages, 1)
            }
            compare(root.chrome.title, data.layout === "Studio Monitor" ? "Movies"
                                     : data.layout === "Broadcast" ? "Movies" : ">Jellyfin >Movies")
        }

        // A question is asked in the layout: its title the frame's, its
        // answers over the view's rows, the view's help line not shown; and
        // the view's own come back with it gone.
        function test_questionAskedInTheFrame_data() { return layouts() }
        function test_questionAskedInTheFrame(data) {
            root.chrome.basePath = ["Settings", "Network"]
            var page = open(menuPage, data.layout)
            var own = root.chrome.title
            page.question.open()
            wait(0)
            verify(root.monitorInk, "a question leaves the frame's colours in force")
            verify(root.chrome.frontLayer !== null)
            compare(root.chrome.title, "Forget this network?")
            compare(root.chrome.helpText, "")
            compare(root.chrome.viewPath, ["Settings", "Network", "Wi-Fi"])
            var prompt = find(page.question, function(item) { return item.choices !== undefined && item.paned !== undefined })
            verify(prompt !== null && prompt.monitor)
            var ground = find(prompt, function(item) { return item.frameRole === "layer" })
            verify(ground !== null && ground.paned, "its ground covers the pane, not the screen")
            // What it covers has the corners of what is under it: Studio
            // Monitor's inside is round, and the frame shows in its corners.
            var covered = find(ground, function(item) {
                return item.radius !== undefined && item.visible && item.width === root.chrome.cover.width
            })
            verify(covered !== null)
            compare(covered.radius, root.chrome.coverCorner)
            compare(root.chrome.coverCorner > 0, data.layout === "Studio Monitor")
            keyClick(Qt.Key_Down)
            compare(page.question.choiceIndex, 1)
            keyClick(Qt.Key_Escape)
            verify(!page.question.visible)
            compare(root.chrome.frontLayer, null)
            compare(root.chrome.title, own)
            compare(root.chrome.helpText, "What row 1 does")
        }

        // Studio Monitor's window is one blue shape: the tab goes down into
        // the frame past its corner, the icons stand in the middle of its
        // side and the key guide is written on its foot. The black inside
        // lies within it, where the rows are.
        function test_studioWindowIsOnePiece() {
            root.chrome.icons = ["play", "stop", "eject"]
            var page = open(menuPage, "Studio Monitor")
            var part = function(name) {
                return find(root.chrome, function(item) { return item.objectName === name })
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
            compare(inside.x, root.chrome.cover.x)
            compare(inside.width, root.chrome.cover.width)
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
            root.chrome.icons = []
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
            compare(page.list.y, root.chrome.listY)
            // The chrome goes with the ink: the layer's ground is the
            // stage's, and the tab and the window's corners showed round it.
            verify(!root.chrome.visible, "no chrome round the layer")
            coverLoader.active = false
            wait(0)
            verify(root.monitorInk)
            verify(root.chrome.visible)
            compare(page.bar.opacity, 0)
        }

        // A view that is not in the frame (a player) has none.
        function test_viewOutsideTheFrame_data() { return layouts() }
        function test_viewOutsideTheFrame(data) {
            root.viewInFrame = false
            var page = open(menuPage, data.layout)
            verify(!root.monitorFramed && !root.monitorInk)
            verify(!root.chrome.visible)
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
            var room = data.layout === "Flat Panel" ? tree.pad : root.chrome.gutter
            compare(tree.leftEdge, root.chrome.listX + room)
            compare(tree.rightEdge, root.chrome.listX + root.chrome.listWidth - tree.pad)
            compare(tree.areaBottom, root.chrome.tallBottom)
            compare(tree.windowed, data.layout === "Studio Monitor")
            compare(root.chrome.title, data.layout === "Flat Panel" ? ">Local Files" : "Local Files")
            keyClick(Qt.Key_Right)
            tryCompare(tree, "folderName", "Action")
            compare(root.chrome.title, data.layout === "Studio Monitor" ? "Action"
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
            compare(root.chrome.hintPairs("[ESC]:BACK [▲▼]:NAVIGATE [►]:ADD TO PLAYLIST [ENTER]:PLAY").length, 4)
            compare(root.chrome.hintPairs("[ESC]:BACK [▲▼]:NAVIGATE [►]:ADD TO PLAYLIST [ENTER]:PLAY")[2].label, "ADD TO PLAYLIST")
            compare(root.chrome.hintPairs("[►]/[ENTER]:OPEN")[0].key, "[►]/[ENTER]")
            compare(root.chrome.hintPairs("HOLD ANY BUTTON TO STOP (9)").length, 0)
            compare(root.chrome.titleCase("ADD TO PLAYLIST"), "Add To Playlist")
            compare(root.chrome.joined(["Settings", "TV / Movie"], ["Settings", "Plex"]), ["Settings", "TV / Movie", "Plex"])
            compare(root.chrome.joined(["Settings", "Look"], ["Settings", "Look", "Effects"]), ["Settings", "Look", "Effects"])
            compare(root.chrome.joined([], ["Local Files", "Action"]), ["Local Files", "Action"])
            // A hint line that is no keys is said as it is.
            var page = open(menuPage, "Studio Monitor")
            page.hint.text = "HOLD ANY BUTTON TO STOP (9)"
            compare(root.chrome.guide, ["HOLD ANY BUTTON TO STOP (9)"])
            // A view's own lines for the guide come first.
            page.hint.guide = ["[▲▼]:MENU/ITEM"]
            compare(root.chrome.guide, ["[▲▼]:MENU/ITEM"])
        }

        // The keys that are back (Hints.js, Main.qml's root.isBack()).
        function test_isBack() {
            verify(root.isBack({ key: Qt.Key_Escape }))
            verify(root.isBack({ key: Qt.Key_Backspace }))
            verify(root.isBack({ key: Qt.Key_Back }))
            verify(!root.isBack({ key: Qt.Key_Return }))
            verify(!root.isBack({ key: Qt.Key_Left }))
        }
    }
}
