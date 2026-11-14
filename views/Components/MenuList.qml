import QtQuick

// A view's menu: the app's rows in the pane (Main.qml's), the place every
// view's list has (under the title bar, one row short of the help line, so the ▼ fits above it), ▲ / ▼
// while rows are hidden above or below (ScrollMarks), and a cursor that steps
// over section headings (rows whose type is "section"), round the ends. The
// host gives it its rows (model, a list) and their delegate, and keys it (the
// cursor's up and down are its own). In a monitor layout's frame (Settings →
// Look → Menu Layout) it lies where the frame has its rows (Main.qml's
// listY, listHeight), and in Flat Panel's its rows come a page at a time, the
// frame saying which page (ScrollMarks): rows of one line's height there, as
// MonitorRow draws them.
//
//     MenuList {
//         id: list
//         model: rows
//         delegate: MenuRow { width: list.width; …; selected: list.currentIndex === index }
//         Keys.onReturnPressed: …
//     }
Item {
    id: menu

    property alias model: view.model
    property alias delegate: view.delegate
    property alias currentIndex: view.currentIndex
    property alias contentY: view.contentY
    readonly property alias count: view.count
    // What of the list shows: the rows wholly in view, from shownTop down.
    readonly property alias shownTop: wholeRows.y
    readonly property alias shownHeight: wholeRows.height

    // In a monitor layout's frame, and its rows a page at a time there.
    readonly property bool monitor: root.monitorFramed === true
    readonly property bool paged: monitor && root.menuLayout === "Flat Panel"

    anchors.top: parent.top
    anchors.left: parent.left
    anchors.topMargin: monitor ? root.listY : root.sh * 0.25 //120
    // The pane's: beside a layout's column where the view is in its frame
    // (Main.qml's paneX, paneWidth), else where every view has its list.
    anchors.leftMargin: root.paneX
    width: root.paneWidth
    // A row more for each bar that is off (Main.qml's helpRoom, hintRoom).
    height: monitor ? root.listHeight : root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224

    // The cursor held on its row, ▲ ▼ the host's meanwhile (heldStep): a
    // value being changed there in two steps, as Broadcast and Flat Panel
    // change one.
    property bool held: false
    signal heldStep(int delta)

    function isSection(i) {
        var row = view.model[i]
        return !!row && row.type === "section"
    }

    // The cursor delta rows on, over the headings, round the ends.
    function step(delta) {
        if (count === 0)
            return
        var i = currentIndex
        for (var n = 0; n < count; n++) {
            i = (i + delta + count) % count
            if (!isSection(i))
                break
        }
        currentIndex = i
        show(i)
    }

    // Scrolls so that the row is in view. Rows are whole lines, a slider's
    // several (a module's settings), so a scroll can stop with one cut by an
    // edge: it carries on past it, keeping the row, as far as the list goes.
    function show(i) {
        // Flat Panel's page is the marks' to turn to (ScrollMarks).
        if (paged)
            return
        view.positionViewAtIndex(i, ListView.Contain)
        var top = cutAtTop()
        if (top && top !== view.currentItem)
            view.contentY = Math.min(top.y + top.height, view.originY + view.contentHeight - view.height)
        var bottom = cutAtFoot()
        if (bottom && bottom !== view.currentItem)
            view.contentY = Math.max(bottom.y - view.height, view.originY)
    }

    // The row the list's top or foot cuts (a slider's several lines), or
    // null.
    function cutAtTop() {
        var row = view.itemAt(0, view.contentY + 1)
        return row && row.y < view.contentY - 1 ? row : null
    }
    function cutAtFoot() {
        var edge = view.contentY + view.height
        var row = view.itemAt(0, edge - 1)
        return row && row.y + row.height > edge + 1 ? row : null
    }

    // The rows laid out again at once, after one has changed its height.
    function forceLayout() {
        view.forceLayout()
    }

    Keys.onUpPressed: held ? heldStep(-1) : step(-1)
    Keys.onDownPressed: held ? heldStep(1) : step(1)

    // The rows wholly in view: one an edge would cut where no scroll can
    // carry on past it (a slider's several lines at the foot as the page
    // opens, or at the top at the list's end) is left out, until a scroll
    // brings it in whole (show()). The list stays where it is, under them.
    Item {
        id: wholeRows
        // Read again as the rows are laid out, as well as scrolled.
        readonly property var topCut: (view.contentHeight, menu.cutAtTop())
        readonly property var footCut: (view.contentHeight, menu.cutAtFoot())
        y: topCut ? Math.round(topCut.y + topCut.height - view.contentY) : 0
        width: parent.width
        height: Math.max(0, (footCut ? Math.round(footCut.y - view.contentY) : view.height) - y)
        clip: true

        ListView {
            id: view
            y: -wholeRows.y
            width: menu.width
            height: menu.height
        }
    }

    ScrollMarks {
        anchors.fill: parent
        list: view
    }
}
