import QtQuick
import Components

// The Orb look's letters (OrbLook.qml): a selected game's name in its bar,
// and the list's name on its tab, scrolled through as every line too long
// for its room is (views/Components/ScrolledText.qml), each name the bar or
// the tab comes to from its beginning. A file of its own, apart from the
// look, so that it is tested where there is no Qt Quick 3D to draw the look.
//
//     ScrolledLetters {
//         width: room; height: bar.height
//         on: row.selected
//         resting: look.resting
//         text: game.name
//     }
ScrolledText {
    id: scrolled
    property alias pixelSize: scrolled.font.pixelSize
    onTextChanged: Qt.callLater(updateScroll)
}
