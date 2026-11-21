import QtQuick

// One line of a menu in a monitor layout (Settings → Look → Menu Layout:
// Studio Monitor, Broadcast, Flat Panel), in the layout's own colours
// (root.menuPalette), the way a studio monitor's menu draws one. Settings
// and the main menu draw theirs with it, and so does every MenuRow in a
// monitor layout's frame:
//
//   Studio Monitor  ■ DISPLAY OUTPUT           HDMI ►    a yellow square
//                                                        before the line
//   Broadcast       ■ LOOK...                             a filled square;
//                     THEME              [TRINITRON]      "..." opens a
//                                                        level, the value
//                                                        boxed while it is
//                                                        being changed
//   Flat Panel      Theme:           ▶  None              a grey bar; mixed
//                                                        case; the value at
//                                                        a column of its
//                                                        own, yellow while
//                                                        it is being changed
//
// A line that can't be used is in the layout's dim colour (a deck's dither
// is the two-colour layouts'), a group's heading in its soft one (Broadcast's,
// whose soft colour is its text's, in the dim one). The label is kept whole
// and the value has the rest of the line: one too long for it is cut short
// with "…", and scrolled through instead while the line is selected (or
// always, with alwaysScroll), as a MenuRow's is.
Item {
    id: line

    property string label: ""
    property string value: ""
    property bool selected: false
    property bool heading: false
    // A line select leads on from, to another screen.
    property bool opens: false
    property bool dimmed: false
    // Its value being changed: Broadcast's and Flat Panel's second step.
    property bool editing: false
    // Pixel symbols at the line's end (PixelIcon's names): a Wi-Fi network's
    // lock and signal. The value stops short of them.
    property var icons: []
    // The label is cut short instead of the value (MenuRow's keepValue).
    property bool keepValue: false
    // A value too long for the line scrolls through all the time, not only
    // while the line is selected: lines that are read rather than chosen.
    property bool alwaysScroll: false

    readonly property bool flat: root.flatLayout
    readonly property bool broadcast: root.broadcastLayout
    readonly property var pal: root.menuPalette || ({ ink: root.primaryColor, dim: root.primaryColor,
                                                     soft: root.primaryColor, mark: root.primaryColor,
                                                     bar: root.primaryColor, ground: root.surfaceColor })
    readonly property real fontSize: flat ? root.sh * 0.0458333 : root.sh * 0.05 //22 : 24
    // The cursor's room at the line's start, its mark in the middle of it.
    readonly property real gutter: flat ? root.sw * 0.025 : root.sw * 0.03125 //16 : 20
    readonly property real pad: root.sw * 0.009375 //6
    readonly property color ink: dimmed ? pal.dim : heading ? (broadcast ? pal.dim : pal.soft) : pal.ink
    // Flat Panel's columns: the ▶ of a line that opens, then the value.
    readonly property real arrowX: Math.round(width * 0.496)
    readonly property real valueX: Math.round(width * 0.545)
    // Where the value ends: before the icons and a Studio Monitor line's ►.
    // As a list, whatever a host being taken down hands on meanwhile.
    readonly property var iconList: icons || []
    readonly property real iconsWidth: iconList.length > 0 ? iconRow.width + pad : 0
    readonly property real valueEnd: width - pad - iconsWidth - (opens && !flat && !broadcast ? arrow.width + pad : 0)
    // The line from the label's start to the value's end, the least of it a
    // value is left (a third, if it wants it), and the label's share.
    readonly property real room: Math.max(0, valueEnd - gutter)
    readonly property real valueFloor: value === "" ? 0
                                     : keepValue ? Math.min(valueClip.implicitWidth, room * 0.75)
                                     : Math.min(valueClip.implicitWidth, room * 0.34)
    readonly property real labelRoom: flat ? (opens ? arrowX : value !== "" ? valueX : width - iconsWidth) - gutter - pad
                                           : room - (value !== "" ? valueFloor + 2 * pad : 0)
    readonly property real valueWidth: flat ? Math.max(0, width - pad - iconsWidth - valueX)
                                            : Math.min(valueClip.implicitWidth,
                                                       Math.max(0, room - labelText.width - 2 * pad))

    // Flat Panel's bar under the selected line.
    Rectangle {
        anchors.fill: parent
        visible: line.flat && line.selected
        color: line.pal.bar
        antialiasing: false
    }

    // The cursor: Studio Monitor's yellow square, Broadcast's filled one.
    Rectangle {
        visible: line.selected && !line.flat
        width: Math.round(line.fontSize * 0.4583 / root.px) * root.px //11
        height: width
        x: Math.round((line.gutter - width) / 2 / root.px) * root.px
        anchors.verticalCenter: parent.verticalCenter
        color: line.pal.mark
        antialiasing: false
    }

    Text {
        id: labelText
        x: line.gutter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(1, Math.min(implicitWidth, line.labelRoom))
        elide: Text.ElideRight
        textFormat: Text.PlainText
        text: line.label + (line.broadcast && line.opens ? "..." : "")
              + (line.flat && line.value !== "" && !line.opens ? ":" : "")
        color: line.ink
        font.family: root.globalFont
        font.capitalization: line.flat ? Font.MixedCase : Font.AllUppercase
        font.pixelSize: line.fontSize
    }

    // Broadcast's value being changed: boxed, in the ground's colour.
    Rectangle {
        visible: line.broadcast && line.editing && line.value !== ""
        x: valueClip.x - line.pad
        width: valueClip.width + 2 * line.pad
        height: line.height - 2 * root.px
        anchors.verticalCenter: parent.verticalCenter
        color: line.pal.ink
        antialiasing: false
    }

    // The value: against the line's end (Flat Panel's at its own column),
    // cut short when it is too long, scrolled through while it may.
    ScrolledText {
        id: valueClip
        visible: line.value !== ""
        x: line.flat ? line.valueX : line.valueEnd - width
        width: line.valueWidth
        height: parent.height
        on: line.selected || line.alwaysScroll
        // One no more than a pixel too long for its room stands still.
        resting: implicitWidth <= width + 1
        text: line.value
        color: line.editing ? (line.broadcast ? line.pal.ground : line.pal.mark) : line.ink
        font.capitalization: line.flat ? Font.MixedCase : Font.AllUppercase
        font.pixelSize: line.fontSize
    }

    // A line that opens: Studio Monitor's ► at its end, Flat Panel's before
    // the value's column. Broadcast's says so with "...".
    PixelIcon {
        id: arrow
        visible: line.opens && !line.broadcast
        name: "play"
        color: line.ink
        x: line.flat ? line.arrowX : line.width - line.pad - line.iconsWidth - width
        y: Math.round((line.height - height) / 2 / root.px) * root.px
    }

    // The icons, against the line's end.
    Row {
        id: iconRow
        visible: line.iconList.length > 0
        x: line.width - line.pad - width
        y: Math.round((line.height - height) / 2 / root.px) * root.px
        spacing: line.pad

        Repeater {
            model: line.iconList
            PixelIcon {
                required property string modelData
                name: modelData
                color: line.ink
            }
        }
    }
}
