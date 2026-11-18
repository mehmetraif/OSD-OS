import QtQuick

// A line of text scrolled through while it is `on` and too long for its
// room, as a deck's menu shows a long name: a pause, across at a steady pace
// (20 ms a pixel), a pause at its end, and back to its start. Not `on` (a
// row that isn't selected), it stands at its start, cut short with "…", or
// with `elides` false at its room's edge; `resting`, it stands at its start.
// Every line of the menus that can be too long for its room has it:
// MenuRow's and MonitorRow's values, AppBar's title, TreeBrowser's cursor
// row, the main menu's, LetterList's and the modules' lists, the Arcade's
// bar and names (ScrolledLetters, its Orb look's).
//
//     ScrolledText {
//         width: Math.min(implicitWidth, list.width); height: row.height
//         on: row.selected
//         text: item.name
//         color: row.ink
//         font.pixelSize: root.sh * 0.05 //24
//     }
//
// Its implicit size is its text's, so a host can size its room by it.
// `textItem` is the line itself: for its padding, and for what moves with
// it, laid under it with `z: -1` (a selected row's box, `anchors.fill:
// scrolled.textItem`). A host whose text changes as its cursor moves on (a
// name for each row the cursor comes to) starts each from its beginning
// with `onTextChanged: Qt.callLater(updateScroll)`.
Item {
    id: scrolled

    property alias text: letters.text
    property alias color: letters.color
    // The app's font, in capitals, but for what the host sets: its size,
    // Flat Panel's mixed case.
    property alias font: letters.font
    property bool on: false
    // Nothing moves then: the Orb look hidden, covered, or a game playing; a
    // title the monitor layout's frame writes in its own place.
    property bool resting: false
    // Cut short with "…" while it isn't `on`; false, it is cut at its room's
    // edge instead (a list's rows).
    property bool elides: true
    readonly property alias textItem: letters
    // Not while it is hidden (a list under a keyboard or a page): a scroll
    // going on there would redraw the window every frame.
    readonly property bool scrolls: on && !resting && visible && letters.implicitWidth > width
    // How far the text goes: to its end at the room's.
    readonly property real scrollBy: width - letters.implicitWidth
    // Where it is now, 0 at its start, and whether it is cut short.
    readonly property alias offset: letters.x
    readonly property alias cut: letters.truncated

    implicitWidth: letters.implicitWidth
    implicitHeight: letters.implicitHeight
    clip: true
    // The animation keeps the distance it started with, so it is started
    // (and started over) once the layout has settled, a turn of the event
    // loop later: one started in the middle of it, as a line that scrolls
    // from the start is, would keep a stale distance for good.
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
        id: letters
        anchors.verticalCenter: parent.verticalCenter
        width: scrolled.on || !scrolled.elides ? implicitWidth : Math.min(implicitWidth, scrolled.width)
        elide: scrolled.elides ? Text.ElideRight : Text.ElideNone
        // As it is: a name such as "<B>" (a file's, a server's) isn't markup.
        textFormat: Text.PlainText
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
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
