import QtQuick

// A view's menu: the app's rows in the place every view's list has (under the
// title bar, one row short of the help line, so the ▼ fits above it), ▲ / ▼
// while rows are hidden above or below (ScrollMarks), and a cursor that steps
// over section headings (rows whose type is "section"), round the ends. The
// host gives it its rows (model, a list) and their delegate, and keys it (the
// cursor's up and down are its own).
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

    anchors.top: parent.top
    anchors.left: parent.left
    anchors.topMargin: root.sh * 0.25 //120
    anchors.leftMargin: root.sw * 0.115625 //74
    width: root.sw * 0.76875 //492
    // A row more for each bar that is off (Main.qml's helpRoom, hintRoom).
    height: root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224

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
    // edge: it carries on past it, keeping the row.
    function show(i) {
        view.positionViewAtIndex(i, ListView.Contain)
        var top = view.itemAt(0, view.contentY + 1)
        if (top && top !== view.currentItem && top.y < view.contentY - 1)
            view.contentY = Math.min(top.y + top.height, view.originY + view.contentHeight - view.height)
        var bottom = view.itemAt(0, view.contentY + view.height - 1)
        if (bottom && bottom !== view.currentItem && bottom.y + bottom.height > view.contentY + view.height + 1)
            view.contentY = Math.max(bottom.y - view.height, view.originY)
    }

    // The rows laid out again at once, after one has changed its height.
    function forceLayout() {
        view.forceLayout()
    }

    Keys.onUpPressed: step(-1)
    Keys.onDownPressed: step(1)

    ListView {
        id: view
        anchors.fill: parent
        clip: true
    }

    ScrollMarks {
        anchors.fill: parent
        list: view
    }
}
