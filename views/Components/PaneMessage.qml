import QtQuick

// What a list says in its place while it has no rows: that it is loading,
// why it can't be shown (a server's error), or that there is nothing in it.
// In the middle of the pane (Main.qml's paneX and paneWidth: beside a
// layout's column, in a monitor layout's frame, or where every view has its
// list), down the middle of the view, and as wide as the pane: a long
// message wraps there rather than running off the screen.
//
//     PaneMessage { visible: isLoading; text: "LOADING..." }
Text {
    x: root.paneX
    anchors.verticalCenter: parent.verticalCenter
    width: root.paneWidth
    color: root.primaryColor
    font.family: root.globalFont
    font.pixelSize: root.sh * 0.05 //24
    wrapMode: Text.WordWrap
    horizontalAlignment: Text.AlignHCenter
}
