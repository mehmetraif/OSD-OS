import QtQuick
import QtTest
import OSDOS.Testing
import "../../views/Components" as Components

StandInRoot {
    id: root
    width: 1280
    height: 960
    function folder(name, path) { return { name: name, path: path, isFolder: true } }
    function file(name, path) { return { name: name, path: path, isFolder: false } }
    property var entries: ({})
    // A host's own keys, for the select the tree leaves (nothing under the
    // cursor).
    property int selectsLeft: 0
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Return) {
            root.selectsLeft++
            event.accepted = true
        }
    }
    Components.TreeBrowser {
        id: browser
        anchors.fill: parent
        rootPath: "/"
        expandedFolderPreviews: true
        fetch: function(path, preview) { return root.entries[path] === undefined ? [] : root.entries[path] }
    }
    SignalSpy { id: activatedSpy; target: browser; signalName: "activated" }
    TestCase {
        name: "ExpandedFolderTree"
        when: windowShown
        function init() {
            while (browser.closeFolder()) {}
            root.entries = {
                "/": [root.folder("projects", "/projects"), root.folder("films", "/films"), root.folder("empty", "/empty")],
                "/projects": [root.folder("source", "/projects/source"), root.folder("tests", "/projects/tests"),
                    root.file("README", "/projects/README"), root.file("build", "/projects/build"),
                    root.folder("assets", "/projects/assets"), root.folder("docs", "/projects/docs"),
                    root.file("LICENSE", "/projects/LICENSE")],
                "/films": [root.file("one", "/films/one"), root.file("two", "/films/two"),
                    root.file("three", "/films/three"), root.file("four", "/films/four")],
                "/projects/source": [root.file("main.cpp", "/projects/source/main.cpp")],
                "/projects/tests": [root.file("test.cpp", "/projects/tests/test.cpp")],
                "/projects/assets": [root.file("logo.svg", "/projects/assets/logo.svg")],
                "/projects/docs": [root.file("guide.md", "/projects/docs/guide.md")],
                "/empty": []
            }
            root.hintBar = true
            browser.listings = ({})
            browser.remembered = ({})
            browser.expandedFolderPreviews = true
            browser.refresh("/")
            var trail = browser.trailState()
            if (trail[0].sel !== 0) browser.move(-trail[0].sel)
            browser.relayout()
        }
        function block(path) {
            for (var i = 0; i < browser.blocks.length; i++)
                if (browser.blocks[i].path === path) return browser.blocks[i]
            return null
        }
        // The tree's sides are the pane's: beside a layout's column where the
        // view is in its frame, the content's with none. Only the window onto
        // the tree moves; its folders stay as they are laid out.
        function test_sidesAreThePanes() {
            compare(browser.leftEdge, root.sw * 0.125)
            compare(browser.rightEdge, root.sw * 0.875)
            var widths = JSON.stringify(browser.columnW)
            root.framed = true
            compare(browser.leftEdge, root.sw * 0.209375 + browser.pad)
            compare(browser.rightEdge, root.sw * 0.884375 - browser.pad)
            root.framed = false
            compare(JSON.stringify(browser.columnW), widths)
        }

        function test_completeSiblingContents() {
            compare(block("/projects").rows.length, 7)
            compare(block("/films").rows.length, 4)
            // An empty folder off the spine has no block: "(empty)" beside a
            // folder read as if it held nothing.
            compare(block("/empty"), null)
            verify(block("/projects/source") !== null)
            verify(block("/projects/tests") !== null)
            verify(block("/projects/assets") !== null)
            verify(block("/projects/docs") !== null)
        }
        function test_onlyTheCursorsFolderInFull() {
            // The folder under the cursor (projects) in full, every other
            // branch faint, its own second level included.
            verify(!block("/projects").faint)
            verify(block("/films").faint)
            verify(block("/projects/source").faint)
            var spine = 0
            for (var i = 0; i < browser.wires.length; i++) {
                var w = browser.wires[i]
                if (!w.faint) {
                    spine++
                    compare(w.y0, 0)
                    compare(w.y1, 0)
                }
            }
            compare(spine, 1)
        }
        function test_noOverlappingBlocks() {
            for (var i = 0; i < browser.blocks.length; i++) {
                var a = browser.blocks[i]
                for (var j = i + 1; j < browser.blocks.length; j++) {
                    var b = browser.blocks[j]
                    if (a.x !== b.x) continue
                    verify(a.top + a.rows.length * browser.rowHeight <= b.top ||
                           b.top + b.rows.length * browser.rowHeight <= a.top)
                }
            }
        }
        function test_navigationAndRememberedSelection() {
            verify(browser.openFolder())
            compare(browser.trailState()[1].path, "/projects")
            browser.move(1)
            compare(browser.selectedItem().path, "/projects/tests")
            verify(browser.closeFolder())
            verify(browser.openFolder())
            compare(browser.selectedItem().path, "/projects/tests")
        }
        function test_virtualRowsAndHiddenActions() {
            var list = [{ name: "USE THIS FOLDER", path: "/action", branchHidden: true }]
            for (var i = 0; i < 1000; i++) list.push(root.file("item" + i, "/projects/" + i))
            root.entries["/projects"] = list
            browser.refresh("/projects")
            var b = block("/projects")
            compare(b.rows.length, 1000)
            verify(browser.visibleRows(b).rows.length <= Math.ceil((browser.bandBottom - browser.bandTop) / browser.rowHeight))
            compare(b.rows[0].label, "item0")
        }
        function test_closedFolderShrinksBeforeReopening() {
            verify(browser.openFolder())
            browser.move(6)
            compare(browser.selectedItem().path, "/projects/LICENSE")
            verify(browser.closeFolder())
            // Another app removed the later entries while the folder was closed.
            root.entries["/projects"] = root.entries["/projects"].slice(0, 2)
            browser.refresh("/projects")
            compare(block("/projects").offset, 1)
            verify(browser.openFolder())
            compare(browser.selectedItem().path, "/projects/tests")
        }
        // A folder whose read its source dropped (Local Files' as a drive
        // comes or goes) is asked for again by refreshPending(), and is in
        // once it answers; folders already in are left as they are.
        function test_foldersOnTheirWayAskedAgain() {
            var films = root.entries["/films"]
            root.entries["/films"] = null
            browser.refresh("/films")
            browser.move(1)
            verify(browser.openFolder())
            compare(browser.trailState()[1].path, "/films")
            verify(browser.listings["/films"].pending)
            var projects = browser.listings["/projects"]
            verify(projects && !projects.pending)
            // The source has the folder now, but never says so.
            root.entries["/films"] = films
            browser.refreshPending()
            verify(!browser.listings["/films"].pending)
            compare(browser.listings["/films"].items.length, 4)
            compare(browser.selectedItem().path, "/films/one")
            verify(browser.listings["/projects"] === projects)
        }
        function test_hintBarRoom() {
            // With the hint bar off, the area reaches the content box's foot:
            // more of a long folder shows under the spine, which stays put.
            var list = []
            for (var i = 0; i < 40; i++) list.push(root.file("item" + i, "/projects/" + i))
            root.entries["/projects"] = list
            browser.refresh("/projects")
            var spine = browser.spine
            var bottom = browser.bandBottom
            var shown = browser.visibleRows(block("/projects")).rows.length
            root.hintBar = false
            compare(browser.spine, spine)
            compare(browser.bandBottom, bottom + root.contentBox.y + root.contentBox.height - browser.treeBottom)
            verify(browser.visibleRows(block("/projects")).rows.length > shown)
        }
        function test_compactModePreserved() {
            browser.expandedFolderPreviews = false
            browser.relayout()
            verify(browser.blockFor("/projects", 0).rows.length <= 5)
            verify(browser.blockFor("/films", -1).rows.length <= 3)
            // Every tree, compact or not, shows only the cursor's folder in full.
            verify(browser.blocks.length > 1)
            for (var i = 0; i < browser.blocks.length; i++)
                compare(!!browser.blocks[i].faint, browser.blocks[i].path !== "/projects")
        }
        // Select on a folder opens it and on a file is the tree's activated;
        // with nothing under the cursor (an empty folder, an empty tree) it
        // is left to the host's own keys.
        function test_selectOnNothingIsTheHosts() {
            browser.forceActiveFocus()
            root.selectsLeft = 0
            activatedSpy.clear()
            keyClick(Qt.Key_Return)
            compare(browser.trailState()[1].path, "/projects")
            compare(root.selectsLeft, 0)
            browser.move(2)
            compare(browser.selectedItem().path, "/projects/README")
            keyClick(Qt.Key_Return)
            compare(activatedSpy.count, 1)
            compare(root.selectsLeft, 0)
            verify(browser.closeFolder())
            browser.move(2)
            compare(browser.selectedItem().path, "/empty")
            verify(browser.openFolder())
            compare(browser.selectedItem(), null)
            keyClick(Qt.Key_Return)
            compare(activatedSpy.count, 1)
            compare(root.selectsLeft, 1)
            while (browser.closeFolder()) {}
            root.entries["/"] = []
            browser.refresh("/")
            tryVerify(function() { return browser.rootEmpty })
            keyClick(Qt.Key_Return)
            compare(activatedSpy.count, 1)
            compare(root.selectsLeft, 2)
        }
        // A host puts an open folder's cursor on one of its entries by its
        // path (select()): the folder with the keys, or one further back,
        // whose spine then runs through that entry. Not a folder that isn't
        // open, nor an entry the folder hasn't.
        function test_selectByPath() {
            verify(browser.select("/", "/films"))
            compare(browser.selectedItem().path, "/films")
            verify(!browser.select("/", "/nowhere"))
            compare(browser.selectedItem().path, "/films")
            verify(!browser.select("/films", "/films/two"), "not open")
            verify(browser.openFolder())
            verify(browser.select("/films", "/films/three"))
            compare(browser.selectedItem().path, "/films/three")
            verify(browser.select("/", "/projects"))
            compare(browser.trailState()[0].sel, 0)
            compare(browser.selectedItem().path, "/films/three")
            while (browser.closeFolder()) {}
            compare(browser.selectedItem().path, "/projects")
        }
        // The cursor's row, and what in it slides as its name scrolls.
        function cursorRow(item) {
            if (item.entryIndex !== undefined && item.cursor === true && item.visible)
                return item
            for (var i = 0; i < item.children.length; i++) {
                var found = cursorRow(item.children[i])
                if (found)
                    return found
            }
            return null
        }
        function textIn(item, text) {
            if (item.text === text)
                return item
            for (var i = 0; i < item.children.length; i++) {
                var found = textIn(item.children[i], text)
                if (found)
                    return found
            }
            return null
        }
        // The cursor's name scrolls as far as it is too long for its column,
        // and rests there: as the column is laid out once its rows are made
        // (a folder just opened, its first name long), and again as the
        // cursor goes to a name of another length.
        function scrollsAsFarAsItOverflows(name) {
            var row = cursorRow(browser)
            verify(row !== null)
            compare(row.label, name)
            var label = textIn(row, name)
            verify(label !== null)
            // What moves: its line, in its room (a ScrolledText's).
            var slider = label.textItem
            // How far it overflows the column as it is laid out by then.
            var distance = function() { return row.width - label.implicitWidth }
            // From the start (started over a turn later), to the far end.
            tryVerify(function() { return slider.x === 0 })
            tryVerify(function() { return slider.x <= distance() + 0.5 }, 6000)
            wait(200)
            fuzzyCompare(slider.x, distance(), 0.5)
            verify(distance() < -10, "the name is too long for its column: " + distance())
        }
        function test_longNameScrollsAsFarAsItOverflows() {
            var first = "a film whose name runs past its end"
            var second = "a film whose name runs on past its end"
            var third = "a film whose name runs on past its fin"
            root.entries["/films"] = [root.file(first, "/films/1"), root.file(second, "/films/2"),
                                      root.file(third, "/films/3")]
            browser.refresh("/films")
            browser.move(1)
            verify(browser.openFolder())
            compare(browser.selectedItem().path, "/films/1")
            scrollsAsFarAsItOverflows(first)
            browser.move(1)
            scrollsAsFarAsItOverflows(second)
            // A name as long as the one before starts from its own beginning
            // at once, not where that one's scroll had got to.
            browser.move(1)
            var row = cursorRow(browser)
            compare(row.label, third)
            tryCompare(textIn(row, third).textItem, "x", 0, 200)
            scrollsAsFarAsItOverflows(third)
        }
        function test_referencePreview() {
            wait(250)
            var image = grabImage(root)
            compare(image.width, root.width)
            compare(image.height, root.height)
            image.save("tree-browser-preview.png")
        }
    }
}
