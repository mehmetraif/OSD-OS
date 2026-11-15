import QtQuick

// Letters scrolled through while they are `on` and too long for their room:
// a pause, across at a steady pace, a pause at their end, and back to their
// start. Not `on`, they are cut short with "…"; `resting`, they stand at
// their start. The Orb look's (OrbLook.qml): a selected game's name in its
// bar, and the list's name on its tab. A file of its own, apart from the
// look, so that it is tested where there is no Qt Quick 3D to draw the look.
//
//     ScrolledLetters {
//         width: room; height: bar.height
//         on: row.selected
//         resting: look.resting
//         text: game.name
//     }
Item {
    id: scrolled

    property alias text: letters.text
    property alias color: letters.color
    property alias pixelSize: letters.font.pixelSize
    property bool on: false
    // Nothing moves then: the look hidden, covered, or a game playing.
    property bool resting: false
    readonly property bool scrolls: on && !resting && letters.implicitWidth > width
    // How far the letters go: to their end at the room's.
    readonly property real scrollBy: width - letters.implicitWidth
    // Where they are now, 0 at their start, and whether they are cut short.
    readonly property alias offset: letters.x
    readonly property alias cut: letters.truncated

    clip: true
    onScrollsChanged: Qt.callLater(updateScroll)
    onScrollByChanged: Qt.callLater(updateScroll)
    function updateScroll() {
        if (scrolls)
            scroll.restart()
        else
            scroll.stop()
    }

    Text {
        id: letters
        anchors.verticalCenter: parent.verticalCenter
        width: scrolled.on ? implicitWidth : Math.min(implicitWidth, scrolled.width)
        elide: Text.ElideRight
        textFormat: Text.PlainText
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        onTextChanged: Qt.callLater(scrolled.updateScroll)
    }
    SequentialAnimation {
        id: scroll
        loops: Animation.Infinite
        onRunningChanged: if (!running) letters.x = 0
        PauseAnimation { duration: 1500 }
        NumberAnimation {
            target: letters
            property: "x"
            to: scrolled.scrollBy
            duration: Math.abs(to) * 20
        }
        PauseAnimation { duration: 2000 }
        PropertyAction { target: letters; property: "x"; value: 0 }
    }
}
