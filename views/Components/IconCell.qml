import QtQuick

// One cell of a column of icons, Camcorder's (IconColumn) and Studio
// Monitor's strip (MonitorChrome): a deck's pixel symbol (`symbol`), or a
// module's logo in its place (`logo`, a URL) drawn by OsdIconProvider in the
// cell's colour, as the title bar's is, or the skin's icon of its name. Both
// on whole art pixels, so they stay as crisp as the text.
//
//     IconCell { anchors.fill: parent; symbol: "gear"; color: root.primaryColor }
Item {
    id: cell

    property string symbol: ""
    property string logo: ""
    property color color: root.primaryColor

    PixelIcon {
        visible: cell.logo === ""
        name: cell.symbol
        color: cell.color
        x: Math.round((cell.width - width) / 2 / root.px) * root.px
        y: Math.round((cell.height - height) / 2 / root.px) * root.px
    }
    Image {
        visible: cell.logo !== ""
        readonly property real box: Math.round(root.sh * 0.0375 / root.px) * root.px //18
        height: box
        width: Math.min(implicitWidth, cell.width - 4 * root.px)
        fillMode: Image.PreserveAspectFit
        sourceSize.height: box
        x: Math.round((cell.width - width) / 2 / root.px) * root.px
        y: Math.round((cell.height - height) / 2 / root.px) * root.px
        source: cell.logo !== "" ? root.iconUrl(cell.color, root.skinIcon(cell.logo)) : ""
    }
}
