import QtQuick

// A cabinet's buttons as a flat grid of numbered boxes, for the screens that
// ask for one of them or show which one a press is (Game Buttons' waits, a
// pad's test): 1 2 3 over 4 5 6 as a six-button panel has them, 7 and 8
// beside them, then COIN and START. The ones in lit are inverted; labels
// puts a pad's own name for each under its number ({ "1": "X", coin: "BACK" }).
// Readable on a 480-line picture, where a drawing of the panel isn't.
Item {
    id: grid

    // The inputs lit: "1" to "8", "coin", "start".
    property var lit: []
    // What each is on the pad, by input; none shown for one missing.
    property var labels: ({})

    readonly property var cells: [
        { input: "1", label: "1", column: 0, row: 0 }, { input: "2", label: "2", column: 1, row: 0 },
        { input: "3", label: "3", column: 2, row: 0 }, { input: "7", label: "7", column: 3, row: 0 },
        { input: "4", label: "4", column: 0, row: 1 }, { input: "5", label: "5", column: 1, row: 1 },
        { input: "6", label: "6", column: 2, row: 1 }, { input: "8", label: "8", column: 3, row: 1 },
        { input: "coin", label: "COIN", column: 4.25, row: 0 }, { input: "start", label: "START", column: 4.25, row: 1 }
    ]
    readonly property real cellWidth: root.sw * 0.0875 //56
    readonly property real cellHeight: root.sh * 0.0833333 //40

    width: cellWidth * 5.25
    height: cellHeight * 2
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined

    Repeater {
        model: grid.cells
        Item {
            id: cell
            readonly property bool on: (grid.lit || []).indexOf(modelData.input) >= 0
            readonly property string name: (grid.labels && grid.labels[modelData.input]) || ""
            objectName: "gridCell-" + modelData.input
            x: modelData.column * grid.cellWidth
            y: modelData.row * grid.cellHeight
            width: grid.cellWidth
            height: grid.cellHeight

            Rectangle {
                anchors.fill: parent
                anchors.margins: root.sh * 0.0041667 //2
                color: cell.on ? root.primaryColor : "transparent"
                border.color: root.primaryColor
                border.width: Math.max(1, root.sh * 0.0041667)
                antialiasing: false
            }
            Column {
                anchors.centerIn: parent
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    textFormat: Text.PlainText
                    text: modelData.label
                    color: cell.on ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: modelData.label.length > 1 ? root.sh * 0.0291667 : root.sh * 0.0375 //14, 18
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: cell.name !== ""
                    textFormat: Text.PlainText
                    text: cell.name
                    color: cell.on ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.025 //12
                    width: Math.min(implicitWidth, grid.cellWidth - root.sw * 0.0125)
                    elide: Text.ElideRight
                }
            }
        }
    }
}
