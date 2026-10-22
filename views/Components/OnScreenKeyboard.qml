import QtQuick

// Typing with a remote, the way a deck's menu spells a title: a grid of
// letters and digits under a line showing what has been typed. The arrows move
// the box, select types what is in it (or SPACE, DEL, OK on the last row), and
// back cancels. A real keyboard types straight in, which moves the box to OK
// so Enter then finishes. It covers its parent below the title bar.
//     OnScreenKeyboard { anchors.fill: parent; onAccepted: (text) => … }
// For a password or a key (caseSensitive), letters in both cases (SHIFT) and
// the signs one may hold, typed as they are and given back untrimmed; masked
// shows a star for each. Inline, it lies in another view's window (the
// Arcade screen's Search, read as it is typed), which shows it and hands it
// the keys: only the line typed and the keys, across its own width, in
// letters of fontSize, the box and the cursor only while it has the keys
// (live). OK and back leave it to the view, OK taking nothing typed too
// (allowEmpty), and ◄ ► past the keys' ends go on to the view (pastEnd).
FocusScope {
    id: osk

    property string title: "Search"
    property string text: ""
    property int maxLength: 40
    property bool caseSensitive: false
    property bool masked: false
    property bool shifted: false
    property bool inline: false
    property bool allowEmpty: false

    signal accepted(string text)
    signal canceled()
    // Inline: ◄ (-1) or ► (1) pressed at the end of a row of keys.
    signal pastEnd(int direction)

    function open(initial) {
        text = initial || ""
        row = 0
        col = 0
        shifted = false
        visible = true
        forceActiveFocus()
    }
    function close() { visible = false }

    readonly property var letters: [
        ["A", "B", "C", "D", "E", "F", "G", "H", "I", "J"],
        ["K", "L", "M", "N", "O", "P", "Q", "R", "S", "T"],
        ["U", "V", "W", "X", "Y", "Z", "0", "1", "2", "3"],
        ["4", "5", "6", "7", "8", "9", "-", "'", ".", "&"]
    ]
    readonly property var rows: caseSensitive
        ? letters.map(function(keys) {
              return keys.map(function(key) { return shifted ? key : key.toLowerCase() })
          }).concat([["@", "_", "!", "?", "#", "$", "%", "*", "+", "="], ["SHIFT", "SPACE", "DEL", "OK"]])
        : letters.concat([["SPACE", "DEL", "OK"]])
    property int row: 0
    property int col: 0
    readonly property int okCol: rows[rows.length - 1].length - 1

    visible: false
    // The box on its key and the cursor after the line typed.
    readonly property bool live: !inline || activeFocus

    function press(key) {
        if (key === "OK") {
            if (text.trim() === "" && !allowEmpty) return
            if (!inline)
                close()
            accepted(caseSensitive ? text : text.trim())
        } else if (key === "DEL") {
            text = text.slice(0, -1)
        } else if (key === "SHIFT") {
            shifted = !shifted
        } else if (text.length < maxLength) {
            text += key === "SPACE" ? " " : key
        }
    }
    // Down onto the last row's keys from the grid's column, and up from one
    // of them: three keys over the grid's ten columns, or four.
    function lastKeyOf(column) {
        var count = rows[rows.length - 1].length
        return count === 3 ? Math.floor(column / 4) : Math.floor(column * count / 10)
    }
    function columnOf(key) {
        var count = rows[rows.length - 1].length
        return count === 3 ? key * 4 : Math.round(key * 10 / count)
    }
    // The box on row r, column c, within the row.
    function moveTo(r, c) {
        row = (r + rows.length) % rows.length
        col = Math.max(0, Math.min(rows[row].length - 1, c))
    }
    // A row up or down, round the ends, the column carried over: mapped onto
    // the last row's keys as the box comes down onto them, and back onto the
    // grid's columns as it leaves them.
    function step(delta) {
        var to = (row + delta + rows.length) % rows.length
        var last = rows.length - 1
        var c = col
        if (to === last && row !== last)
            c = lastKeyOf(c)
        else if (row === last && to !== last)
            c = columnOf(c)
        moveTo(to, c)
    }

    Keys.onPressed: function(event) {
        // Ctrl+Q and the rest go on to Main.qml.
        if (event.modifiers & Qt.ControlModifier)
            return
        event.accepted = true
        switch (event.key) {
        case Qt.Key_Up:    step(-1); return
        case Qt.Key_Down:  step(1); return
        // Inline, a key held stops at the row's end, and a press there goes on.
        case Qt.Key_Left:
            if (inline && col === 0) {
                if (!event.isAutoRepeat)
                    pastEnd(-1)
            } else {
                col = (col - 1 + rows[row].length) % rows[row].length
            }
            return
        case Qt.Key_Right:
            if (inline && col === rows[row].length - 1) {
                if (!event.isAutoRepeat)
                    pastEnd(1)
            } else {
                col = (col + 1) % rows[row].length
            }
            return
        case Qt.Key_Return:
        case Qt.Key_Enter: press(rows[row][col]); return
        case Qt.Key_Escape:
        case Qt.Key_Back:
            if (!inline)
                close()
            canceled()
            return
        case Qt.Key_Backspace: press("DEL"); return
        }
        typeIn(event.text)
    }
    // Typed on a real keyboard: straight in, and the box goes to OK.
    function typeIn(typed) {
        var ch = caseSensitive ? typed : typed.toUpperCase()
        if (ch.length === 1 && ch >= " " && text.length < maxLength) {
            text += ch
            row = rows.length - 1
            col = okCol
        }
    }

    property real fontSize: root.sh * 0.05 //24
    readonly property real cellW: inline ? Math.floor(width / 10) : Math.floor(root.sw * 0.75 / 10) //48
    readonly property real cellH: inline ? Math.round(fontSize * 1.4) : root.sh * 0.0666667 //32

    // Everything under the title bar, on the ground every view has (OSD
    // BACKGROUND): a window goes on under it as it was.
    OsdGround {
        visible: !osk.inline
        y: root.sh * 0.1916667 //92
        width: parent.width
        height: parent.height - y
    }

    Column {
        x: osk.inline ? 0 : root.sw * 0.125 //80
        y: osk.inline ? 0 : root.sh * 0.2083333 //100
        spacing: osk.inline ? Math.round(osk.fontSize * 0.4) : root.sh * 0.025 //12

        Text {
            visible: !osk.inline
            textFormat: Text.PlainText
            text: osk.title
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0375 //18
        }

        // What has been typed, with a block where the next letter goes.
        Rectangle {
            width: osk.cellW * 10
            height: osk.cellH + 2 * root.px
            color: "transparent"
            border.width: root.px
            border.color: root.primaryColor
            antialiasing: false
            clip: true
            Row {
                x: root.sw * 0.0125 //8
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    id: typed
                    textFormat: Text.PlainText
                    text: osk.masked ? "*".repeat(osk.text.length) : osk.text
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: osk.caseSensitive ? Font.MixedCase : Font.AllUppercase
                    font.pixelSize: osk.fontSize
                }
                Rectangle {
                    width: Math.round(osk.fontSize * 0.55)
                    height: Math.round(osk.fontSize * 0.75)
                    anchors.verticalCenter: typed.verticalCenter
                    color: root.primaryColor
                    antialiasing: false
                    visible: blink.on && osk.live
                }
            }
            Timer {
                id: blink
                property bool on: true
                interval: 500
                repeat: true
                running: osk.visible && osk.live
                onTriggered: on = !on
            }
        }

        Column {
            Repeater {
                model: osk.rows.length
                Row {
                    id: keyRow
                    required property int index
                    readonly property var keys: osk.rows[index]
                    // The last row's three keys share the grid's width.
                    readonly property real keyW: keys.length === 10 ? osk.cellW : osk.cellW * 10 / keys.length
                    Repeater {
                        model: keyRow.keys
                        Item {
                            required property int index
                            required property string modelData
                            readonly property bool current: osk.row === keyRow.index && osk.col === index
                            width: keyRow.keyW
                            height: osk.cellH
                            SelectionBox {
                                anchors.centerIn: parent
                                width: keyText.implicitWidth + 2 * root.sw * 0.009375
                                height: parent.height
                                visible: parent.current && osk.live
                                skinned: false
                            }
                            Text {
                                id: keyText
                                anchors.centerIn: parent
                                text: modelData
                                color: parent.current && osk.live ? root.surfaceColor : root.primaryColor
                                font.family: root.globalFont
                                font.pixelSize: osk.fontSize
                            }
                        }
                    }
                }
            }
        }
    }

    HintBar {
        visible: !osk.inline
        text: root.hints.back + ":CANCEL " + root.hints.navigate + ":MOVE " + root.hints.select + ":TYPE"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
