import QtQuick

// A camcorder's menu column (Settings → Look → Menu Layout's Camcorder):
// Settings' pages, or the main menu's groups, as the deck's pixel symbols
// one under another, a row each, level with the rows beside them. The one
// the cursor is on (currentIndex) is boxed: in the selected line's box while
// the keys move through the column (`active`), framed while they move
// through the rows beside it. The views move the cursor; the column only
// shows it, with ▲ ▼ while symbols are hidden above or below, and a rule
// down its right side between it and the rows.
//
//     IconColumn { model: [{ icon: "tv" }, { icon: "gear" }]; currentIndex: 0; active: true }
//
// An entry with a `source` instead is a module's logo: inside a module the
// column is the modules of its group (Main.qml's frame).
Item {
    id: column

    property alias model: icons.model
    // The column's own, not the list's: a list puts its cursor back on its
    // first row whenever its rows are given anew, and the shell's column
    // (Main.qml's frame) is given them by each view that owns it in turn.
    property int currentIndex: 0
    property bool active: true
    onCurrentIndexChanged: icons.positionViewAtIndex(currentIndex, ListView.Contain)
    readonly property alias count: icons.count

    ListView {
        id: icons
        // The rule's room at the right.
        width: parent.width - 2 * root.px
        height: parent.height
        interactive: false
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        onCountChanged: positionViewAtIndex(column.currentIndex, ListView.Contain)

        delegate: Item {
            id: cell
            required property int index
            required property var modelData
            readonly property bool current: index === column.currentIndex
            width: icons.width
            height: root.menuRowHeight

            SelectionBox {
                anchors.fill: parent
                visible: cell.current && column.active
            }
            Rectangle {
                anchors.fill: parent
                visible: cell.current && !column.active
                color: "transparent"
                border.color: root.primaryColor
                border.width: root.px
                antialiasing: false
            }
            IconCell {
                anchors.fill: parent
                symbol: cell.modelData.icon || ""
                logo: String(cell.modelData.source || "")
                color: cell.current && column.active ? root.surfaceColor : root.primaryColor
            }
        }
    }

    ScrollMarks {
        anchors.fill: icons
        list: icons
        // A column of icons, not a menu's rows: never paged.
        pages: false
    }

    Rectangle {
        anchors.right: parent.right
        width: root.px
        height: parent.height
        color: root.primaryColor
        antialiasing: false
    }
}
