import QtQuick
import "Hover.js" as Hover

// A view's menu: the app's rows in the pane (Main.qml's), the place every
// view's list has (under the title bar, one row short of the help line, so the ▼ fits above it), ▲ / ▼
// while rows are hidden above or below (ScrollMarks), and a cursor that steps
// over the rows it doesn't stop on (selectable(): headings, notes, lines read
// alone, a gap), round the ends. The host gives it its rows (model, a list,
// or a number of rows whose delegate reads each its own) and their delegate,
// and keys it (the cursor's up and down are its own). In a monitor layout's
// frame (Settings → Look → Menu Layout) it lies where the frame has its rows
// (Main.qml's listY, listHeight), and in Flat Panel's its rows come a page at
// a time, the frame saying which page (ScrollMarks): rows of one line's
// height there, as MonitorRow draws them.
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
    readonly property alias originY: view.originY
    readonly property alias contentHeight: view.contentHeight
    readonly property alias count: view.count
    // What of the list shows: the rows wholly in view, from shownTop down.
    readonly property alias shownTop: wholeRows.y
    readonly property alias shownHeight: wholeRows.height

    // In a monitor layout's frame, and its rows a page at a time there.
    readonly property bool monitor: root.monitorFramed === true
    readonly property bool paged: monitor && root.flatLayout

    anchors.top: parent.top
    anchors.left: parent.left
    anchors.topMargin: root.listY
    // The pane's: beside a layout's column where the view is in its frame
    // (Main.qml's paneX, paneWidth), else where every view has its list.
    anchors.leftMargin: root.paneX
    width: root.paneWidth
    // A row more for each bar that is off (Main.qml's listHeight).
    height: root.listHeight

    // The cursor held on its row, ▲ ▼ the host's meanwhile (heldStep): a
    // value being changed there in two steps, as Broadcast and Flat Panel
    // change one, or the keys in a layout's icons beside the rows.
    property bool held: false
    signal heldStep(int delta)
    // The cursor moved by ▲ ▼ (step(), delta 1 down, -1 up), not by the host
    // or the mouse: what a host said of the row it left (a notice) goes.
    signal moved(int delta)
    // ▲ ▼ beside the rows while some are hidden (ScrollMarks): off for a
    // list drawn as a module's lists are, without them (LetterList's).
    // Flat Panel's pages either way.
    property bool marks: true
    // The rows glide after the cursor as ▲ ▼ step it past an edge, as the
    // ListView follows its current row by itself (its highlightMoveVelocity,
    // 400 pixels a second), rather than jump to it: a media server's A–Z
    // lists (LetterList), as Plex's list moved before it was one. Round the
    // ends, and in Flat Panel's pages, it jumps as any list does.
    property bool glides: false

    // The rows the cursor stops on: every row but a heading, a note, a line
    // read alone and a gap (Hover.js's selectable(), which the mouse keeps
    // to as well), unless the host's rows go by a rule of their own (rule, a
    // function of the row).
    property var rule: null
    function selectable(row) {
        return rule ? rule(row) : Hover.selectable(row)
    }
    // Whether the cursor stops on the row at index i: in a list of a number
    // of rows, which have nothing to say of themselves, on every one.
    function stopsAt(i) {
        if (i < 0 || i >= count)
            return false
        var rows = view.model
        return !rows || rows.length === undefined || selectable(rows[i])
    }
    // The first row the cursor stops on from index on, going by delta (1
    // down, -1 up), round; the first row where it stops on none.
    function selectableFrom(index, delta) {
        for (var n = 0; n < count; n++) {
            var i = ((index + n * delta) % count + count) % count
            if (stopsAt(i))
                return i
        }
        return 0
    }

    // The cursor delta rows on, over the rows it doesn't stop on, round the
    // ends.
    function step(delta) {
        if (count === 0)
            return
        var i = selectableFrom(currentIndex + delta, delta)
        if (stopsAt(i)) {
            var round = delta > 0 ? i < currentIndex : i > currentIndex
            currentIndex = i
            if (!glides || round || paged)
                show(i)
        }
        moved(delta)
    }

    // New rows (model) for a list whose lines are read again and again (a
    // search's networks, the system's facts): given it only as lines come,
    // go or change places, idOf(row) being which line a row is, whatever it
    // says now. The same lines in the same places leave the list and its
    // cursor as they are, the host's delegate changing a line's value or
    // help where it is; otherwise the cursor goes back to its line, else to
    // the row now in its place that it stops on.
    function take(rows, idOf) {
        var shown = view.model && view.model.length !== undefined ? view.model : []
        var same = rows.length === shown.length
        for (var i = 0; i < rows.length && same; i++)
            same = idOf(rows[i]) === idOf(shown[i])
        if (same)
            return
        var at = currentIndex
        var current = shown[at]
        var id = current && selectable(current) ? idOf(current) : undefined
        view.model = rows
        if (rows.length === 0)
            return
        var index = -1
        for (i = 0; i < rows.length && index < 0 && id !== undefined; i++) {
            if (idOf(rows[i]) === id)
                index = i
        }
        if (index < 0)
            index = selectableFrom(Math.min(Math.max(at, 0), rows.length - 1), 1)
        currentIndex = index
        show(index)
    }

    // Scrolls so that the row is in view. Rows are whole lines, a slider's
    // several (a module's settings), so a scroll can stop with one cut by an
    // edge: it carries on past it, keeping the row, as far as the list goes.
    function show(i) {
        // Flat Panel's page is the marks' to turn to (ScrollMarks), and
        // turned to here too for a list given new rows the cursor is on
        // as it was. Not for a list that doesn't show (under a keyboard).
        if (paged) {
            if (view.visible)
                root.chrome.pageTo(view)
            return
        }
        view.positionViewAtIndex(i, ListView.Contain)
        // The rows at an end of the list the cursor doesn't stop on (a
        // heading over the first row, a note after the last) come into view
        // with the first or the last it stops on: nothing else would bring
        // them.
        if (i === selectableFrom(0, 1))
            view.positionViewAtBeginning()
        else if (i === selectableFrom(count - 1, -1))
            view.positionViewAtEnd()
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
    // The row drawn at index, or null (ListView's).
    function itemAtIndex(i) {
        return view.itemAtIndex(i)
    }
    // The row at index scrolled to as ListView's mode has it (a jump to a
    // row: LetterList's letters), not as the cursor's moves are (show()).
    function positionViewAtIndex(i, mode) {
        view.positionViewAtIndex(i, mode)
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
            // The mouse stops on the rows the cursor does (Hover.js).
            function selectable(row) {
                return menu.selectable(row)
            }
        }
    }

    ScrollMarks {
        anchors.fill: parent
        list: view
        visible: menu.marks
    }
}
