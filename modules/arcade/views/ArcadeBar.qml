import QtQuick
import Components

// The bar an Arcade screen starts with, where the other screens have their
// title bar: the skin's title bar (or a solid bar of the scheme's colour),
// its text in the background colour, at its left and at its right. The
// list's names the filter (◄ ► go round them) and the page; a game's, PLAY ▶.
Item {
    id: bar

    property string text: ""
    // ◄ and ► either side of the text, while ◄ ► change it.
    property bool arrows: false
    // A deck's symbol after the text (PixelIcon's): "play", "stop".
    property string icon: ""
    property string rightText: ""

    // The design's units: a 960th of the width, a 720th of the height.
    readonly property real u: root.sw / 960
    readonly property real v: root.sh / 720
    readonly property real pad: Math.round(12 * u)
    readonly property real gap: Math.round(8 * u)

    SkinImage {
        id: skinned
        anchors.fill: parent
        part: root.skin.titleBar
    }
    Rectangle {
        anchors.fill: parent
        visible: !skinned.shown
        color: root.primaryColor
        antialiasing: false
    }

    Row {
        x: bar.pad
        anchors.verticalCenter: parent.verticalCenter
        spacing: bar.gap

        Text {
            id: before
            visible: bar.arrows
            text: "◄"
            color: root.surfaceColor
            font.family: root.globalFont
            font.pixelSize: Math.round(22 * bar.v)
        }
        Text {
            id: label
            // What is left of the bar, so a long name is cut short rather
            // than the page's number.
            width: Math.max(0, Math.min(implicitWidth,
                bar.width - 2 * bar.pad - (bar.rightText !== "" ? right.implicitWidth + 2 * bar.gap : 0)
                - (bar.arrows ? 2 * (before.implicitWidth + bar.gap) : 0)
                - (bar.icon !== "" ? symbol.width + bar.gap : 0)))
            text: bar.text
            elide: Text.ElideRight
            color: root.surfaceColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: Math.round(22 * bar.v)
        }
        Text {
            visible: bar.arrows
            text: "►"
            color: root.surfaceColor
            font.family: root.globalFont
            font.pixelSize: Math.round(22 * bar.v)
        }
        PixelIcon {
            id: symbol
            visible: bar.icon !== ""
            name: bar.icon !== "" ? bar.icon : "play"
            color: root.surfaceColor
            anchors.verticalCenter: label.verticalCenter
        }
    }

    Text {
        id: right
        visible: bar.rightText !== ""
        anchors.right: parent.right
        anchors.rightMargin: bar.pad
        anchors.verticalCenter: parent.verticalCenter
        text: bar.rightText
        color: root.surfaceColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: Math.round(20 * bar.v)
    }
}
