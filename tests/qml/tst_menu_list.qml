import QtQuick
import QtTest
import Components
import "../../views/Components/Hover.js" as Hover

// A view's menu (views/Components/MenuList.qml) on its own: the rows its
// cursor stops on (selectable(): every row but a heading, a note, a line read
// alone and a gap, unless the host's rows go by a rule of their own), round
// the ends; a list of a number of rows, which say nothing of themselves; the
// cursor held while ▲ ▼ are the host's; the rows at an end the cursor passes
// over shown with the one beside them; and the mouse (Hover.js) stopping on
// the rows the keys do.
Rectangle {
    id: root
    color: surfaceColor
    width: 640
    height: 480
    // Main.qml's, as the list and its rows read them.
    property real sw: width
    property real sh: height
    property real px: 2
    property string globalFont: "monospace"
    property color primaryColor: "white"
    property color surfaceColor: "black"
    readonly property color cursorColor: primaryColor
    readonly property color cursorInk: surfaceColor
    property var skin: ({})
    property bool hintBar: true
    property bool helpLine: true
    property string menuLayout: "Standard"
    readonly property var menuPalette: null
    readonly property bool monitorFramed: false
    readonly property bool monitorInk: false
    readonly property real paneX: sw * 0.115625
    readonly property real paneWidth: sw * 0.76875
    readonly property real listY: sh * 0.25
    readonly property real listHeight: sh * 0.4666667 + helpRoom + hintRoom
    readonly property real hintRoom: 0
    readonly property real helpRoom: 0
    property var selector: null
    function selectorShown(item, shown) {}
    function skinIcon(url) { return url }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }

    // A page with a menu of the rows given, by the rule given (none: the
    // list's own), counting the cursor's moves and the steps held for it.
    Component {
        id: menuPage
        FocusScope {
            property alias list: list
            property var rows: []
            property var rule: null
            property bool held: false
            property int moves: 0
            property var heldSteps: []
            MenuList {
                id: list
                model: parent.rows
                focus: true
                rule: parent.rule
                held: parent.held
                onMoved: parent.moves++
                onHeldStep: function(delta) { parent.heldSteps = parent.heldSteps.concat([delta]) }
                delegate: MenuRow {
                    width: list.width
                    height: root.sh * 0.0583333 //28
                    label: typeof modelData === "object" && modelData ? (modelData.label || "") : "Row " + index
                    heading: typeof modelData === "object" && !!modelData && modelData.type === "section"
                    selected: list.currentIndex === index
                }
            }
        }
    }

    TestCase {
        name: "MenuList"
        when: windowShown

        function cleanup() {
            loader.sourceComponent = undefined
        }

        function open(properties) {
            loader.sourceComponent = menuPage
            verify(loader.item !== null)
            for (var key in properties)
                loader.item[key] = properties[key]
            loader.item.forceActiveFocus()
            return loader.item
        }
        // The mouse over the middle of the list's row at index.
        function hover(page, index) {
            var row = page.list.itemAtIndex(index)
            verify(row !== null)
            var p = row.mapToItem(root, row.width / 2, row.height / 2)
            Hover.follow(root, root.Window.activeFocusItem, p.x, p.y)
        }

        // A heading, a note, a line read alone, a gap and a row marked a
        // heading are passed over, down and up, round the ends; every move
        // is the cursor's own (moved()).
        function test_stepsOverWhatItDoesntStopOn() {
            var page = open({ rows: [{ type: "section", label: "Group" }, { label: "One" },
                                     { type: "note", label: "Searching" }, { label: "Two" },
                                     { type: "info", label: "Version", value: "1.0" }, { gap: true, label: "" },
                                     { heading: true, label: "Marked" }, { label: "Three" }] })
            var list = page.list
            compare(list.selectableFrom(0, 1), 1)
            compare(list.selectableFrom(7, 1), 7)
            compare(list.selectableFrom(6, -1), 3)
            verify(!list.stopsAt(0) && list.stopsAt(1) && !list.stopsAt(2) && !list.stopsAt(4)
                   && !list.stopsAt(5) && !list.stopsAt(6) && !list.stopsAt(8) && !list.stopsAt(-1))
            list.currentIndex = 1
            var at = []
            for (var i = 0; i < 4; i++) {
                keyClick(Qt.Key_Down)
                at.push(list.currentIndex)
            }
            compare(at, [3, 7, 1, 3])
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(list.currentIndex, 7)
            compare(page.moves, 6)
        }

        // A host whose rows go by a rule of their own (a multi-select page's
        // headings, which turn their group on or off): the cursor, and the
        // mouse, stop where it says.
        function test_hostsOwnRule() {
            var page = open({ rows: [{ label: "Genres", heading: true }, { label: "Drama" },
                                     { label: "Loading", note: true }],
                              rule: function(row) { return !!row && !row.note } })
            var list = page.list
            list.currentIndex = 1
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 0)
            hover(page, 2)
            compare(list.currentIndex, 0)
            hover(page, 1)
            compare(list.currentIndex, 1)
            hover(page, 0)
            compare(list.currentIndex, 0)
        }

        // The mouse passes over the rows the cursor does.
        function test_mouseKeepsToTheRule() {
            var page = open({ rows: [{ label: "One" }, { type: "section", label: "Group" },
                                     { type: "note", label: "Searching" }, { label: "Two" }] })
            var list = page.list
            compare(list.currentIndex, 0)
            hover(page, 1)
            compare(list.currentIndex, 0)
            hover(page, 2)
            compare(list.currentIndex, 0)
            hover(page, 3)
            compare(list.currentIndex, 3)
        }

        // A list of a number of rows, each drawn from the host's own: the
        // cursor stops on every one, and only on those there are.
        function test_numberOfRows() {
            var page = open({ rows: 3 })
            var list = page.list
            compare(list.count, 3)
            verify(list.stopsAt(0) && list.stopsAt(2) && !list.stopsAt(3))
            keyClick(Qt.Key_Up)
            compare(list.currentIndex, 2)
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 0)
            hover(page, 1)
            compare(list.currentIndex, 1)
        }

        // Held, ▲ ▼ are the host's (a value changed in two steps, a
        // layout's icons): the cursor stays.
        function test_heldForTheHost() {
            var page = open({ rows: [{ label: "One" }, { label: "Two" }], held: true })
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(page.list.currentIndex, 0)
            compare(page.heldSteps, [1, -1, -1])
            compare(page.moves, 0)
        }

        // A heading over the first row the cursor stops on, and a note after
        // the last, come into view with it, round the ends: nothing else
        // would show them.
        function test_endsShown() {
            var rows = [{ type: "section", label: "Paired" }]
            for (var i = 0; i < 18; i++)
                rows.push({ label: "Row " + i })
            rows.push({ type: "note", label: "Searching" })
            var page = open({ rows: rows })
            var list = page.list
            list.currentIndex = 1
            list.show(1)
            keyClick(Qt.Key_Up)
            compare(list.currentIndex, 18)
            fuzzyCompare(list.contentY, list.originY + list.contentHeight - list.height, 0.5)
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 1)
            fuzzyCompare(list.contentY, list.originY, 0.5)
            // A row in the middle is scrolled to alone.
            for (i = 0; i < 9; i++)
                keyClick(Qt.Key_Down)
            compare(list.currentIndex, 10)
            fuzzyCompare(list.contentY, list.originY + 11 * root.sh * 0.0583333 - list.height, 0.5)
        }
    }
}
