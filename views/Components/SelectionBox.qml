import QtQuick

// The selected line's box: the skin's picture of a selected line (Settings →
// Look → Skin, or the theme's), or a solid bar of the scheme's colour. Every selected
// line is one: a MenuRow's, the main menu's, the tree's cursor, a
// PromptScreen's answer, the on-screen keyboard's key (`skinned` false: the
// plain bar, a key not being a line).
//
// While it shows, it is a selector Settings → Look → Effects → Selector Effect may play round:
// it tells Main.qml (root.selectorShown), whose root.selector is the one in
// front, and the effect follows it there.
//
//     SelectionBox { anchors.fill: label; visible: current }
Item {
    id: box

    property bool skinned: true

    SkinImage {
        id: skinnedBar
        anchors.fill: parent
        part: box.skinned ? root.skin.selection : undefined
    }
    Rectangle {
        anchors.fill: parent
        visible: !skinnedBar.shown
        color: root.primaryColor
        antialiasing: false
    }

    readonly property bool showing: visible && width > 0 && height > 0
    onShowingChanged: root.selectorShown(box, showing)
    Component.onCompleted: if (showing) root.selectorShown(box, true)
    Component.onDestruction: root.selectorShown(box, false)
}
