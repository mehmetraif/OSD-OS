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
// In a monitor layout's frame (Settings → Look → Menu Layout) it is the
// layout's cursor instead, as MonitorRow draws a row's: Studio Monitor's
// yellow square and Broadcast's filled one before the line, Flat Panel's
// grey bar under it; no skin, and nothing for the selector effect to play
// round. What is written on it is in `ink`: the background colour on the
// bar, the layout's text colour beside a mark or on Flat Panel's bar.
//
//     SelectionBox { id: cursor; anchors.fill: label; visible: current }
//     Text { id: label; color: current ? cursor.ink : root.primaryColor }
Item {
    id: box

    property bool skinned: true
    readonly property bool monitor: root.monitorInk === true
    readonly property var pal: root.menuPalette || ({})
    // A mark before the line rather than a bar under it.
    readonly property bool marked: monitor && root.menuLayout !== "Flat Panel"
    readonly property color ink: monitor ? pal.ink : root.surfaceColor

    SkinImage {
        id: skinnedBar
        anchors.fill: parent
        part: box.skinned && !box.monitor ? root.skin.selection : undefined
    }
    Rectangle {
        anchors.fill: parent
        visible: !skinnedBar.shown && !box.marked
        color: box.monitor ? box.pal.bar : root.primaryColor
        antialiasing: false
    }
    // The mark, an art pixel short of the line's start.
    Rectangle {
        visible: box.marked
        width: Math.round(root.sh * 0.05 * 0.4583 / root.px) * root.px //11, MonitorRow's
        height: width
        x: -width - root.px
        y: Math.round((box.height - height) / 2 / root.px) * root.px
        color: box.marked ? box.pal.mark : "transparent"
        antialiasing: false
    }

    readonly property bool showing: visible && width > 0 && height > 0 && !monitor
    onShowingChanged: root.selectorShown(box, showing)
    Component.onCompleted: if (showing) root.selectorShown(box, true)
    Component.onDestruction: root.selectorShown(box, false)
}
