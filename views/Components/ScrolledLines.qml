import QtQuick

// Lines of text too tall for their room, scrolled up through as they are
// read, as a page with a poster shows a film's summary: a pause at their top
// (3 s), up at a steady pace (120 ms a pixel), a pause at their foot (4 s),
// and back to the top. ScrolledText's counterpart down the page, started as
// it is: once the layout has settled, and again, from the top, whenever how
// far they go changes (another text, another width); stopped at the top
// while hidden (the page under a player or a keyboard), and started over,
// after the pause, as they are shown again.
//
//     ScrolledLines {
//         anchors.left: parent.left; anchors.right: parent.right
//         height: root.sh * 0.1375 //66
//         text: detail ? detail.summary : ""
//         font.pixelSize: root.sh * 0.0291667 //14
//     }
//
// The lines wrap at its width, in the app's font, a third again as far
// apart as the font is high, in the host's colour. They are shown as they
// are written, a server's words never read as markup.
Item {
    id: scrolled

    property alias text: lines.text
    property alias color: lines.color
    property alias font: lines.font
    readonly property alias textItem: lines
    // Not while it is hidden: a scroll going on there would redraw the
    // window every frame.
    readonly property bool scrolls: visible && lines.implicitHeight > height
    // How far the lines go: up until their foot is at the room's.
    readonly property real scrollBy: height - lines.implicitHeight
    // Where they are now, 0 at their top.
    readonly property alias offset: lines.y

    clip: true
    // The animation keeps the distance it started with, so it is started
    // (and started over) once the layout has settled, a turn of the event
    // loop later: one started in the middle of it would keep a stale
    // distance for good.
    onScrollsChanged: Qt.callLater(updateScroll)
    onScrollByChanged: Qt.callLater(updateScroll)
    Component.onCompleted: Qt.callLater(updateScroll)
    function updateScroll() {
        if (scrolls)
            scroll.restart()
        else
            scroll.stop()
    }

    Text {
        id: lines
        width: scrolled.width
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        lineHeight: 1.3
        font.family: root.globalFont
    }
    SequentialAnimation {
        id: scroll
        loops: Animation.Infinite
        onRunningChanged: if (!running) lines.y = 0
        PauseAnimation { duration: 3000 }
        NumberAnimation {
            target: lines
            property: "y"
            to: scrolled.scrollBy
            duration: Math.abs(to) * 120
        }
        PauseAnimation { duration: 4000 }
        PropertyAction { target: lines; property: "y"; value: 0 }
    }
}
