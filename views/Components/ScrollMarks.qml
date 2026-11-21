import QtQuick

// The marks a deck's menu shows when it holds more lines than fit: a ▲ just
// above a list while lines are hidden above it, and a ▼ just below while lines
// are hidden below. Lay it over the list it marks:
//     ScrollMarks { anchors.fill: theList; list: theList }
//
// In Flat Panel's frame (Settings → Look → Menu Layout) a menu says so the
// way a flat panel's does: its rows come a page at a time, and the frame
// writes which page at its top right ("1/2"). The marks see to that for the
// list they mark (ListPager), with no ▲ ▼ of their own then; `pages` off
// leaves a list scrolling under its marks (a question's answers).
Item {
    id: marks

    property Flickable list: null
    property color color: root.primaryColor
    property bool pages: true

    ListPager {
        id: pager
        list: marks.pages ? marks.list : null
    }

    readonly property real gap: root.px * 3
    readonly property bool moreAbove: list !== null && list.contentY > list.originY + 1
    readonly property bool moreBelow: list !== null
        && list.contentY + list.height < list.originY + list.contentHeight - 1

    PixelIcon {
        name: "up"
        color: marks.color
        visible: marks.moreAbove && !pager.paged
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.top
        anchors.bottomMargin: marks.gap
    }
    PixelIcon {
        name: "down"
        color: marks.color
        visible: marks.moreBelow && !pager.paged
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.bottom
        anchors.topMargin: marks.gap
    }
}
