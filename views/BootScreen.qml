import QtQuick
import Components

// Boot screen of the OSD/OS image (os/README.md). The image puts the app on
// screen first and starts the services it held back afterwards, in the order
// it lists them; this shows them coming up while the cassette plays: its reels
// turn, and the tape winds from one onto the other in step with the progress
// bar. It stays until they have all settled, or a minute has passed, whatever
// keys are pressed.
//
// Binds only to root.* (Main.qml mirrors bootProgress there), which stays
// valid while this Loader-hosted view is torn down. Its colours are
// root.bootInk and root.bootPaper: the theme's, or Video 1's (Settings → Look → Boot
// Screen Colors).
FocusScope {
    id: bootRoot

    focus: true

    // Smoothed copy of the boot progress that drives both the bar and the tape.
    property real shownProgress: root.bootValue
    Behavior on shownProgress { NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }

    // The column the bar and the text share: about 3/8 of the screen's width,
    // in whole bar steps of whole art pixels (20 steps of 12 px, 240 px, at
    // 640×480), so the bar spans it exactly.
    readonly property int barSteps: 20
    readonly property real columnWidth: barSteps * root.px
        * Math.max(2, Math.round(root.sw * 0.375 / barSteps / root.px))
    readonly property real columnX: Math.round((root.sw - columnWidth) / 2)

    // The longest service line runs the column's width, which sets the text's
    // size, kept within what a CRT reads comfortably. TextMetrics rather than
    // FontMetrics.advanceWidth(), so the size follows the font once it is set.
    TextMetrics {
        id: probe
        font.family: root.globalFont
        font.pixelSize: 100
        text: bootRoot.longestLine
    }
    readonly property string longestLine: {
        var line = "[ OK ] READY"
        for (var i = 0; i < root.bootSteps.length; ++i) {
            var candidate = "[ OK ] " + root.bootSteps[i].label
            if (candidate.length > line.length)
                line = candidate
        }
        return line.toUpperCase()
    }
    readonly property real textSize: Math.max(root.sh * 0.0375, Math.min(root.sh * 0.0583333,
        Math.floor(100 * columnWidth / Math.max(1, probe.advanceWidth))))

    // The cassette and everything under it sit as one block, spaced in art
    // pixels, in the middle of what is left under the deck's display: between
    // PLAY and the foot of the safe area, where every view's hint bar ends.
    readonly property real barGap: root.px * 8
    readonly property real statusGap: root.px * 5
    readonly property real linesGap: root.px * 3
    readonly property real blockHeight: cassette.height + barGap + bar.height
        + statusGap + status.height + linesGap + lines.height
    readonly property real blockTop: playRow.y + playRow.height
    readonly property real blockBottom: root.sh - root.sh * 0.1041667 //430
    readonly property real blockY: Math.round(blockTop + (blockBottom - blockTop - blockHeight) / 2)

    // Cycles 0..3 for the "..." after whatever is starting.
    property int dots: 0
    Timer {
        interval: 400
        repeat: true
        running: bootRoot.visible
        onTriggered: bootRoot.dots = (bootRoot.dots + 1) % 4
    }

    function marker(state) {
        switch (state) {
        case "done":    return "[ OK ]"
        case "failed":  return "[FAIL]"
        case "skipped": return "[ -- ]"
        case "running": return "[" + [" .  ", " .. ", " ...", "    "][bootRoot.dots] + "]"
        default:        return "[    ]"
        }
    }

    // Keys go nowhere while it is up, so none reaches the view underneath.
    Keys.onPressed: function(event) {
        event.accepted = true
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Q)
            Qt.quit()
    }

    Rectangle {
        anchors.fill: parent
        color: root.bootPaper
    }

    // The deck's own on-screen display while a tape plays, inside the same
    // overscan-safe gutter as every other view.
    Row {
        id: playRow
        x: root.sw * 0.125 //80
        y: root.sh * 0.125 //60
        spacing: root.sw * 0.0125 //8
        Text {
            id: playLabel
            text: "PLAY"
            color: root.bootInk
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0416667 //20
        }
        PixelIcon {
            name: "play"
            color: root.bootInk
            anchors.verticalCenter: playLabel.verticalCenter
        }
    }

    VhsCassette {
        id: cassette
        // As large as on the owner's drawing of this screen, where it is
        // 612 × 284 of 1448 × 1086: 270 × 126 at 640×480, 304 × 126 at 720×480.
        width: Math.round(root.sw * 612 / 1448)
        height: Math.round(root.sh * 284 / 1086)
        ink: root.bootInk
        ground: root.bootPaper
        x: Math.round((root.sw - width) / 2)
        y: bootRoot.blockY
        progress: bootRoot.shownProgress
    }

    // The deck's segment bar, across the column.
    OsdTicks {
        id: bar
        x: bootRoot.columnX
        anchors.top: cassette.bottom
        anchors.topMargin: bootRoot.barGap
        width: bootRoot.columnWidth
        segments: bootRoot.barSteps
        value: bootRoot.shownProgress
        color: root.bootInk
    }

    Text {
        id: status
        anchors.left: bar.left
        anchors.top: bar.bottom
        anchors.topMargin: bootRoot.statusGap
        text: root.bootLabel !== "" ? "LOADING" + "...".substr(0, bootRoot.dots) : "READY"
        color: root.bootInk
        font.family: root.globalFont
        font.pixelSize: bootRoot.textSize
    }

    Text {
        anchors.right: bar.right
        anchors.baseline: status.baseline
        text: Math.round(bootRoot.shownProgress * 100) + "%"
        color: root.bootInk
        font.family: root.globalFont
        font.pixelSize: bootRoot.textSize
    }

    // One line per service, in the order the image starts them; the one
    // starting now is inverted, like a deck's menu marks what is selected.
    Column {
        id: lines
        anchors.left: bar.left
        anchors.top: status.bottom
        anchors.topMargin: bootRoot.linesGap

        Repeater {
            model: root.bootSteps
            Item {
                readonly property bool starting: modelData.state === "running"
                width: line.implicitWidth
                height: line.implicitHeight
                Rectangle {
                    anchors.fill: parent
                    visible: parent.starting
                    color: root.bootInk
                    antialiasing: false
                }
                Text {
                    id: line
                    text: bootRoot.marker(modelData.state) + " " + modelData.label
                    color: parent.starting ? root.bootPaper : root.bootInk
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: bootRoot.textSize
                }
            }
        }
    }
}
