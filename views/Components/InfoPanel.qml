import QtQuick

// A film's info, the way a deck's INFO key puts up what is on the tape: the
// PLAY box, its name, a line of facts and the story, then its details as
// menu lines (GENRE······DRAMA). It is the tree's last layer: a host opens it
// from a film (show(item)) and fills details as they come in. It covers its
// parent below the title bar.
//
// Select plays the film, up/down close it and move on through the list (the
// host moves its tree), and left or back close it. Right asks for the film's
// options (optionsRequested): its favourite, YouTube's Watch Later.
//
//     InfoPanel { anchors.fill: parent; onPlayRequested: (item) => … }
FocusScope {
    id: info

    // The entry shown; null while closed.
    property var item: null
    // { title, facts, summary, rows: [{ label, value }] }; until it is in,
    // the entry's own name and a "loading…" story.
    property var details: ({})
    property bool loading: false
    // The hint for right; "" when the host has no options.
    property string optionsHint: root.hints.browse + ":OPTIONS"

    signal playRequested(var item)
    signal optionsRequested(var item)
    signal moveRequested(int delta)
    signal closed()

    function show(entry) {
        item = entry
        details = ({})
        visible = true
        forceActiveFocus()
    }
    function close() {
        if (!visible) return
        visible = false
        item = null
        closed()
    }

    visible: false

    Keys.onPressed: function(event) {
        // Ctrl+Q and the rest go on to Main.qml.
        if (event.modifiers & Qt.ControlModifier)
            return
        event.accepted = true
        switch (event.key) {
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (!event.isAutoRepeat) info.playRequested(info.item)
            return
        case Qt.Key_Up:
            info.close()
            info.moveRequested(-1)
            return
        case Qt.Key_Down:
            info.close()
            info.moveRequested(1)
            return
        case Qt.Key_Right:
            if (info.optionsHint !== "" && !event.isAutoRepeat) info.optionsRequested(info.item)
            return
        case Qt.Key_Left:
        case Qt.Key_Escape:
        case Qt.Key_Backspace:
        case Qt.Key_Back:
        case Qt.Key_Space:
        case Qt.Key_Info:
        case Qt.Key_I:
            if (!event.isAutoRepeat) info.close()
            return
        }
    }

    readonly property string title: details.title || (item ? (item.title || item.name || "") : "")
    readonly property string facts: details.facts || ""
    readonly property string summary: details.summary !== undefined && details.summary !== ""
                                       ? details.summary : (loading ? "loading…" : "")
    readonly property var rows: details.rows || []

    // Everything under the title bar, on the ground every view has (OSD
    // BACKGROUND): a window goes on under it as it was.
    OsdGround {
        y: root.sh * 0.1916667 //92
        width: parent.width
        height: parent.height - y
    }

    Row {
        id: top
        x: root.sw * 0.125 //80
        y: root.sh * 0.2083333 //100
        spacing: root.sw * 0.0375 //24

        // PLAY, the way the deck's button reads when it is the one selected.
        Rectangle {
            width: root.sw * 0.1875 //120
            height: root.sh * 0.1166667 //56
            color: root.primaryColor
            antialiasing: false
            Text {
                anchors.centerIn: parent
                text: "PLAY ►"
                color: root.surfaceColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.05 //24
            }
        }

        Column {
            width: root.sw * 0.75 - root.sw * 0.1875 - top.spacing //336
            spacing: root.sh * 0.0166667 //8

            Text {
                width: parent.width
                text: info.title
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.05 //24
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: text !== ""
                text: info.facts
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0333333 //16
                elide: Text.ElideRight
            }

            // The story, scrolling through when it is longer than its room.
            Item {
                id: summaryBox
                width: parent.width
                height: root.sh * 0.1916667 //92
                clip: true

                Text {
                    id: summaryText
                    width: parent.width
                    text: info.summary
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0291667 //14
                    lineHeight: 1.3
                    wrapMode: Text.WordWrap
                }

                SequentialAnimation {
                    running: info.visible && summaryText.implicitHeight > summaryBox.height
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) summaryText.y = 0
                    PauseAnimation { duration: 3000 }
                    NumberAnimation {
                        target: summaryText
                        property: "y"
                        to: summaryBox.height - summaryText.implicitHeight
                        duration: Math.abs(to) * 120
                    }
                    PauseAnimation { duration: 4000 }
                    PropertyAction { target: summaryText; property: "y"; value: 0 }
                }
            }
        }
    }

    // The details, as menu lines.
    Column {
        x: root.sw * 0.125 //80
        y: top.y + top.height + root.sh * 0.0166667 //8
        width: root.sw * 0.75 //480
        Repeater {
            model: info.rows
            MenuRow {
                required property var modelData
                width: parent.width
                height: root.sh * 0.05 //24
                fontSize: root.sh * 0.0375 //18
                label: modelData.label
                value: modelData.value
            }
        }
    }

    HintBar {
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + (info.optionsHint !== "" ? info.optionsHint + " " : "")
              + root.hints.select + ":PLAY"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
