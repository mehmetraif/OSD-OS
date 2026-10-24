import QtQuick

// The deck's tape position bar: a ▼ over where the tape is, a ruled bar filled
// from the start of the tape up to there, and BEGIN and END under its ends.
// Any setting between two ends reads the same way with its own names under
// them (Settings' TRANSPARENT … SOLID), with a ◄ and a ► beyond the bar's
// ends for the keys that move the ▼ (changes).
Item {
    id: tape

    // 0 to 1: how far into the tape.
    property real value: 0
    property string startText: "BEGIN"
    property string endText: "END"
    property color color: root.primaryColor
    property int pixel: root.px
    property real fontSize: root.sh * 0.0416667 //20
    property int rules: 8
    // A setting's bar, which ◄ ► move a step toward either end: the two
    // arrows stand beyond the bar's ends.
    property bool changes: false

    readonly property int barHeight: 7 * pixel
    // What each arrow takes off the bar's end: the arrow and a gap.
    readonly property int inset: changes ? 6 * pixel : 0
    readonly property real at: Math.max(0, Math.min(1, value))
    readonly property int fillWidth: Math.round((width - 2 * inset - 2 * pixel) * at / pixel) * pixel

    implicitHeight: marker.height + pixel + barHeight + endLabel.implicitHeight

    PixelIcon {
        id: marker
        name: "down"
        color: tape.color
        pixel: tape.pixel
        x: Math.round((tape.inset + tape.pixel + tape.fillWidth - width / 2) / tape.pixel) * tape.pixel
    }

    // The arrows, level with the bar, as tall as it is.
    PixelIcon {
        visible: tape.changes
        name: "left"
        color: tape.color
        pixel: tape.pixel
        y: bar.y
    }
    PixelIcon {
        visible: tape.changes
        name: "play"
        color: tape.color
        pixel: tape.pixel
        x: tape.width - width
        y: bar.y
    }

    Item {
        id: bar
        x: tape.inset
        y: marker.height + tape.pixel
        width: tape.width - 2 * tape.inset
        height: tape.barHeight

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: tape.pixel
            border.color: tape.color
            antialiasing: false
        }
        Rectangle {
            x: tape.pixel
            y: tape.pixel
            width: tape.fillWidth
            height: bar.height - 2 * tape.pixel
            color: tape.color
            antialiasing: false
        }
        // Rules hang from the top edge at even steps along the tape.
        Repeater {
            model: tape.rules - 1
            Rectangle {
                x: Math.round(bar.width * (index + 1) / tape.rules / tape.pixel) * tape.pixel
                y: tape.pixel
                width: tape.pixel
                height: 2 * tape.pixel
                color: tape.color
                antialiasing: false
            }
        }
    }

    Text {
        anchors.top: bar.bottom
        anchors.left: bar.left
        text: tape.startText
        color: tape.color
        font.family: root.globalFont
        font.pixelSize: tape.fontSize
    }
    Text {
        id: endLabel
        anchors.top: bar.bottom
        anchors.right: bar.right
        text: tape.endText
        color: tape.color
        font.family: root.globalFont
        font.pixelSize: tape.fontSize
    }
}
