import QtQuick
import QtTest
import "../../views/Components" as Components

Rectangle {
    id: root
    color: surfaceColor
    width: 1280
    height: 960
    property real sw: width
    property real sh: height
    property real px: 2
    property string globalFont: "monospace"
    property color primaryColor: "white"
    property color surfaceColor: "black"
    property var skin: ({})
    // Main.qml's: Settings' Hint Bar, and the area the views lay out in.
    property bool hintBar: true
    property rect contentBox: Qt.rect(sw * 0.115625, sh * 0.11875, sw * 0.76875, sh * 0.7770833)
    function selectorShown(item, showing) {}
    function folder(name, path) { return { name: name, path: path, isFolder: true } }
    function file(name, path) { return { name: name, path: path, isFolder: false } }
    property var entries: ({})
    Components.TreeBrowser {
        id: browser
        anchors.fill: parent
        rootPath: "/"
        expandedFolderPreviews: true
        fetch: function(path, preview) { return root.entries[path] === undefined ? [] : root.entries[path] }
    }
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
            compare(browser.currentItem().path, "/projects/tests")
            verify(browser.closeFolder())
            verify(browser.openFolder())
            compare(browser.currentItem().path, "/projects/tests")
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
            compare(browser.currentItem().path, "/projects/LICENSE")
            verify(browser.closeFolder())
            // Another app removed the later entries while the folder was closed.
            root.entries["/projects"] = root.entries["/projects"].slice(0, 2)
            browser.refresh("/projects")
            compare(block("/projects").offset, 1)
            verify(browser.openFolder())
            compare(browser.currentItem().path, "/projects/tests")
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
        function test_referencePreview() {
            wait(250)
            var image = grabImage(root)
            compare(image.width, root.width)
            compare(image.height, root.height)
            image.save("tree-browser-preview.png")
        }
    }
}
