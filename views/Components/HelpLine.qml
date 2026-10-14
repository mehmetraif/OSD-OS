import QtQuick

// The help line under a settings menu: the focused line's description in an
// outlined box, on one line. A description too long for the box scrolls
// through it like a ticker, pausing whenever its start comes round again; one
// written as several lines reads as one, its lines joined with "•".
// Settings' HELP LINE (root.helpLine) hides every one at once, by its opacity,
// so a host's own visible binding holds and its menu keeps its shape; one
// whose lines are the page itself (ABOUT's) stays, with always. A module with
// a Help Line setting of its own (Arcade's) sets shown instead.
Rectangle {
    id: help

    property string text: ""
    property bool always: false
    property bool shown: always || root.helpLine

    readonly property string line: text.split("\n")
        .map(function(part) { return part.trim() })
        .filter(function(part) { return part !== "" })
        .join("  •  ")
    readonly property real pad: root.sw * 0.0125 //8
    // The gap between the end of the line and its start coming round again.
    readonly property real spacing: root.sw * 0.0625 //40
    readonly property real speed: root.sw * 0.09375 //60 px a second
    readonly property bool scrolls: first.implicitWidth > window.width
    // How far the line has moved left, in whole pixels so it stays crisp.
    property real travel: 0

    width: root.sw * 0.75 //480
    height: root.sh * 0.0583333 //28
    // Outlined rather than tinted: the OSD keeps to two colours.
    color: "transparent"
    border.width: root.px
    border.color: root.primaryColor
    antialiasing: false
    opacity: shown ? 1 : 0

    function restart() {
        ticker.stop()
        travel = 0
        if (scrolls && visible && shown)
            ticker.start()
    }
    // Deferred, so the text has its new width before it is measured.
    onLineChanged: Qt.callLater(restart)
    onVisibleChanged: Qt.callLater(restart)
    onShownChanged: Qt.callLater(restart)
    onScrollsChanged: Qt.callLater(restart)

    Item {
        id: window
        anchors.fill: parent
        anchors.leftMargin: help.border.width + help.pad
        anchors.rightMargin: help.border.width + help.pad
        clip: true

        Row {
            x: help.scrolls ? -Math.round(help.travel) : Math.round((window.width - first.implicitWidth) / 2)
            anchors.verticalCenter: parent.verticalCenter
            spacing: help.spacing

            Text {
                id: first
                text: help.line
                // As it is: what a line quotes (a name, a file) isn't markup.
                textFormat: Text.PlainText
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0375 //18
            }
            // The start coming round again behind the end.
            Text {
                visible: help.scrolls
                text: help.line
                textFormat: Text.PlainText
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0375 //18
            }
        }
    }

    SequentialAnimation {
        id: ticker
        loops: Animation.Infinite
        PauseAnimation { duration: 1500 }
        NumberAnimation {
            target: help
            property: "travel"
            from: 0
            to: first.implicitWidth + help.spacing
            duration: 1000 * (first.implicitWidth + help.spacing) / help.speed
        }
    }
}
