import QtQuick

// The marks a deck's menu shows when it holds more lines than fit: a ▲ just
// above a list while lines are hidden above it, and a ▼ just below while lines
// are hidden below. Lay it over the list it marks:
//     ScrollMarks { anchors.fill: theList; list: theList }
Item {
    id: marks

    property Flickable list: null
    property color color: root.primaryColor

    readonly property real gap: root.px * 3
    readonly property bool moreAbove: list !== null && list.contentY > list.originY + 1
    readonly property bool moreBelow: list !== null
        && list.contentY + list.height < list.originY + list.contentHeight - 1

    PixelIcon {
        name: "up"
        color: marks.color
        visible: marks.moreAbove
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.top
        anchors.bottomMargin: marks.gap
    }
    PixelIcon {
        name: "down"
        color: marks.color
        visible: marks.moreBelow
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.bottom
        anchors.topMargin: marks.gap
    }
}
