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
Item {
    id: column

    property alias model: icons.model
    property alias currentIndex: icons.currentIndex
    property bool active: true
    readonly property alias count: icons.count

    ListView {
        id: icons
        // The rule's room at the right.
        width: parent.width - 2 * root.px
        height: parent.height
        interactive: false
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        onCountChanged: positionViewAtIndex(currentIndex, ListView.Contain)

        delegate: Item {
            id: cell
            required property int index
            required property var modelData
            readonly property bool current: index === icons.currentIndex
            width: icons.width
            height: root.sh * 0.0583333 //28, a menu row's

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
            PixelIcon {
                name: cell.modelData.icon || ""
                color: cell.current && column.active ? root.surfaceColor : root.primaryColor
                // On whole art pixels, so the symbol stays as crisp as the text.
                x: Math.round((cell.width - width) / 2 / root.px) * root.px
                y: Math.round((cell.height - height) / 2 / root.px) * root.px
            }
        }
    }

    ScrollMarks {
        anchors.fill: icons
        list: icons
    }

    Rectangle {
        anchors.right: parent.right
        width: root.px
        height: parent.height
        color: root.primaryColor
        antialiasing: false
    }
}
