import QtQuick

// The JAMMA board's controls on a JAMMA stick's Test Buttons page
// (ControllerSettings.qml): the edge connector's control pins, player 1's
// on the parts side (16 to 27) and player 2's on the solder side (T to e), a
// column each for Coin, Start, the joystick and buttons 1 to 6. A pin is lit
// while its switch is held and stays marked once it has been, so a control
// whose signal never reaches the board stays dim however it is pressed.
//
// Which of the board's expander pins is which control is a layout: OSD/OS's
// own (what osdos-jamma's mapping makes of each pin, its mapping file
// included), the Crown board's (mk_arcade_joystick_rpi's order, button 6 on
// pin 27, a Chinese harness's), and JammaPi's. The page's N goes round them.
// A pin the layout places on no control (a system switch, another
// expander's) is listed under the grid.
Item {
    id: grid

    // The pins seen this test, by "<address> <pin>": { player, address, pin,
    // input, held }, as osdos-jamma reports them.
    property var pins: ({})
    // Which of layouts names the pins.
    property int layout: 0
    // Shown, taking room (not on a pad's or a keyboard's test).
    property bool shown: true

    // By the expander pin, the same on both players' expanders, or by the
    // input the mapping makes of it (byInput).
    readonly property var layouts: [
        { name: "OSD/OS", byInput: true },
        { name: "Crown, CHAMMA",
          pins: { A0: "up", A1: "down", A2: "left", A3: "right", A4: "start", A5: "coin",
                  A6: "b1", A7: "b2", B0: "b3", B1: "b4", B2: "b5", B3: "b6" } },
        { name: "JammaPi",
          pins: { A0: "up", A1: "down", A2: "left", A3: "right", A5: "start", A6: "coin",
                  B7: "b1", B6: "b2", B5: "b3", B4: "b4", B3: "b5", B2: "b6" } }
    ]
    readonly property string layoutName: layouts[layout % layouts.length].name
    // osdos-jamma's inputs as controls: its buttons 1 to 6 are a to tr.
    readonly property var inputControls: ({ coin: "coin", start: "start", up: "up", down: "down",
                                            left: "left", right: "right", a: "b1", b: "b2", x: "b3",
                                            y: "b4", tl: "b5", tr: "b6" })
    // The columns: each control with its pin on the edge, player 1's and
    // player 2's. 27 and e are ground on a JAMMA harness, button 6 on a
    // Chinese one (CHAMMA).
    readonly property var controls: [
        { key: "coin", label: "COIN", p1: "16", p2: "T" },
        { key: "start", label: "START", p1: "17", p2: "U" },
        { key: "up", label: "UP", p1: "18", p2: "V" },
        { key: "down", label: "DOWN", p1: "19", p2: "W" },
        { key: "left", label: "LEFT", p1: "20", p2: "X" },
        { key: "right", label: "RIGHT", p1: "21", p2: "Y" },
        { key: "b1", label: "B1", p1: "22", p2: "Z" },
        { key: "b2", label: "B2", p1: "23", p2: "a" },
        { key: "b3", label: "B3", p1: "24", p2: "b" },
        { key: "b4", label: "B4", p1: "25", p2: "c" },
        { key: "b5", label: "B5", p1: "26", p2: "d" },
        { key: "b6", label: "B6", p1: "27", p2: "e" }
    ]

    // The control a pin seen is under the layout, "" for none.
    function controlOf(p) {
        if (p.player < 1)
            return ""
        var l = layouts[layout % layouts.length]
        return (l.byInput ? inputControls[p.input] : l.pins[p.pin]) || ""
    }
    // A player's control: held, seen, and the expander pins it is: the
    // layout's, or for OSD/OS's those seen as it.
    function cellOf(player, key) {
        var c = { held: false, seen: false, pins: [] }
        var keys = Object.keys(pins)
        for (var i = 0; i < keys.length; i++) {
            var p = pins[keys[i]]
            if (p.player !== player || controlOf(p) !== key)
                continue
            c.seen = true
            c.held = c.held || p.held
            if (c.pins.indexOf(p.pin) < 0)
                c.pins.push(p.pin)
        }
        var l = layouts[layout % layouts.length]
        if (!l.byInput) {
            var named = Object.keys(l.pins)
            for (var j = 0; j < named.length; j++) {
                if (l.pins[named[j]] === key && c.pins.indexOf(named[j]) < 0)
                    c.pins.push(named[j])
            }
        }
        return c
    }
    // The pins seen that the layout places on no control.
    readonly property string others: {
        var list = []
        var keys = Object.keys(pins).sort()
        for (var i = 0; i < keys.length; i++) {
            var p = pins[keys[i]]
            if (controlOf(p) !== "")
                continue
            list.push((p.player > 0 ? "P" + p.player : p.address) + " " + p.pin
                      + (p.input !== "none" && p.player > 0 ? " (" + p.input + ")" : "")
                      + (p.held ? " HELD" : ""))
        }
        return list.join(", ")
    }

    readonly property real labelWidth: root.sw * 0.0625 //40
    readonly property real cellWidth: (width - labelWidth) / controls.length
    readonly property real cellHeight: root.sh * 0.0791667 //38

    width: root.sw * 0.875 //560
    height: shown ? column.height : 0
    visible: shown
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined

    Column {
        id: column
        width: parent.width
        spacing: root.sh * 0.0083333 //4

        // The controls' names.
        Row {
            x: grid.labelWidth
            Repeater {
                model: grid.controls
                Text {
                    width: grid.cellWidth
                    height: root.sh * 0.0375 //18
                    textFormat: Text.PlainText
                    text: modelData.label
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0291667 //14
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: root.sh * 0.0166667 //8
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // Player 1's pins, then player 2's: the edge's pin, and under it the
        // expander's.
        Repeater {
            model: [1, 2]
            Row {
                id: playerRow
                readonly property int player: modelData
                Text {
                    width: grid.labelWidth
                    height: grid.cellHeight
                    textFormat: Text.PlainText
                    text: "P" + playerRow.player
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0375 //18
                    verticalAlignment: Text.AlignVCenter
                }
                Repeater {
                    model: grid.controls
                    Item {
                        id: cellItem
                        readonly property var cell: grid.cellOf(playerRow.player, modelData.key)
                        width: grid.cellWidth
                        height: grid.cellHeight
                        // Not seen yet: dim, until its switch reaches the board.
                        opacity: cell.seen ? 1 : 0.4

                        // Held: filled; seen: framed.
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: root.sh * 0.0041667 //2
                            color: cellItem.cell.held ? root.primaryColor : "transparent"
                            border.color: root.primaryColor
                            border.width: cellItem.cell.seen ? Math.max(1, root.sh * 0.0041667) : 0
                            antialiasing: false
                        }
                        Column {
                            anchors.centerIn: parent
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                textFormat: Text.PlainText
                                text: playerRow.player === 1 ? modelData.p1 : modelData.p2
                                color: cellItem.cell.held ? root.surfaceColor : root.primaryColor
                                font.family: root.globalFont
                                font.pixelSize: root.sh * 0.0375 //18
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                textFormat: Text.PlainText
                                text: cellItem.cell.pins.join(" ")
                                color: cellItem.cell.held ? root.surfaceColor : root.primaryColor
                                font.family: root.globalFont
                                font.pixelSize: root.sh * 0.025 //12
                            }
                        }
                    }
                }
            }
        }

        // The pins seen that no control of the layout's is.
        Text {
            width: parent.width
            visible: grid.others !== ""
            textFormat: Text.PlainText
            text: "Other pins: " + grid.others
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0291667 //14
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
