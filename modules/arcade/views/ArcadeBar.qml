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
    // The hint bar's letters.
    readonly property real textSize: root.sh * 0.0375 //18

    // Where the design has it, over the list, beside the column's picture.
    x: Math.round(120 * u)
    y: Math.round(90 * v)
    width: Math.round(355 * u)
    height: Math.round(45 * v)

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
            font.pixelSize: bar.textSize
        }
        // The text, in what is left of the bar, so the page's number keeps
        // its place: a name too long for it (a genre's, a publisher's)
        // scrolls through, as AppBar's title does, cut on the font's
        // character grid.
        Item {
            id: labelClip
            readonly property real cell: Math.max(1, labelCell.advanceWidth)
            readonly property real room: bar.width - 2 * bar.pad
                - (bar.rightText !== "" ? right.implicitWidth + 2 * bar.gap : 0)
                - (bar.arrows ? 2 * (before.implicitWidth + bar.gap) : 0)
                - (bar.icon !== "" ? symbol.width + bar.gap : 0)
            width: Math.max(0, Math.min(label.implicitWidth, Math.floor(room / cell) * cell))
            height: label.implicitHeight
            anchors.verticalCenter: parent.verticalCenter
            clip: label.implicitWidth > width

            Text {
                id: label
                // A filter's name may be catver.ini's: never markup.
                textFormat: Text.PlainText
                text: bar.text
                color: root.surfaceColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: bar.textSize
                // Each name from its beginning.
                onTextChanged: Qt.callLater(labelClip.updateScroll)
            }
            TextMetrics {
                id: labelCell
                font: label.font
                text: "M"
            }

            // Started (and started over) a turn of the event loop after the
            // layout changes, as AppBar's is: the animation keeps the
            // distance it started with.
            readonly property bool scrolls: bar.visible && label.implicitWidth > width
            readonly property real scrollBy: width - label.implicitWidth
            onScrollsChanged: Qt.callLater(updateScroll)
            onScrollByChanged: Qt.callLater(updateScroll)
            Component.onCompleted: Qt.callLater(updateScroll)
            function updateScroll() {
                if (scrolls)
                    scroll.restart()
                else
                    scroll.stop()
            }

            SequentialAnimation {
                id: scroll
                loops: Animation.Infinite
                onRunningChanged: if (!running) label.x = 0

                PauseAnimation { duration: 1500 }
                NumberAnimation {
                    target: label
                    property: "x"
                    to: labelClip.scrollBy
                    duration: Math.abs(to) * 20
                }
                PauseAnimation { duration: 2000 }
                PropertyAction { target: label; property: "x"; value: 0 }
            }
        }
        Text {
            visible: bar.arrows
            text: "►"
            color: root.surfaceColor
            font.family: root.globalFont
            font.pixelSize: bar.textSize
        }
        PixelIcon {
            id: symbol
            visible: bar.icon !== ""
            name: bar.icon !== "" ? bar.icon : "play"
            color: root.surfaceColor
            anchors.verticalCenter: labelClip.verticalCenter
        }
    }

    Text {
        id: right
        visible: bar.rightText !== ""
        anchors.right: parent.right
        anchors.rightMargin: bar.pad
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: bar.rightText
        color: root.surfaceColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: bar.textSize
    }
}
