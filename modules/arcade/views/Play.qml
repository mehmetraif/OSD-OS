import QtQuick
import Components

// A game in MAME (ArcadeBackend::play). This view says which game and how to
// come back, then MAME has the screen until the game ends, and the view goes
// back to the list. On a headless Pi it isn't drawn while MAME runs: what it
// shows just before the hand-off stays on the screen while MAME starts. A game
// that couldn't start, or that MAME ended with an error, says why here.
//
// The way back is MAME's own, Coin and Start together on player 1's
// controller or Esc on a keyboard (the controller file ArcadeBackend writes),
// so that MAME saves the game's high scores and settings as it closes. While
// it runs every key reaches this view too (both read the controllers) and is
// taken here, so none acts on the menus underneath.
FocusScope {
    id: playRoot

    property var navParams: ({})
    readonly property string game: navParams.game || ""
    readonly property string title: navParams.title || game

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    // "opening", "running", "error" (never started), "failed" (MAME ended
    // with an error) or "canceled".
    property string phase: "opening"
    property string message: ""
    property string output: ""
    // Player 1's Coin and Start as their controller's labels name them
    // ("COIN + START" on a JAMMA stick, "BACK + START" on an Xbox pad); ""
    // with no controller.
    property string exitKeys: ""
    readonly property bool running: arcadeBackend ? arcadeBackend.running : false

    function isBack(key) {
        return key === Qt.Key_Escape || key === Qt.Key_Backspace || key === Qt.Key_Back
    }

    function launch() {
        if (arcadeBackend.play(playRoot.game)) {
            playRoot.phase = "running"
        } else {
            playRoot.phase = "error"
            playRoot.message = arcadeBackend.lastError()
        }
    }

    Component.onCompleted: {
        var players = inputManager ? inputManager.players : []
        var key = players.length > 0 ? players[0].playing : ""
        if (key)
            exitKeys = inputManager.buttonLabel(key, "back") + " + " + inputManager.buttonLabel(key, "start")
    }

    // This frame on the screen first, as a headless Pi hands it over; the
    // first game this run long enough to read how to come back, as the
    // screen stays as it is while MAME starts.
    Timer {
        id: launchTimer
        interval: arcadeBackend && arcadeBackend.played ? 150 : 1500
        running: true
        onTriggered: playRoot.launch()
    }

    // A game played counts as activity: no screen saver comes up behind it.
    Timer {
        interval: 10000
        repeat: true
        triggeredOnStart: true
        running: playRoot.running
        onTriggered: if (idleTracker) idleTracker.resetActivity()
    }

    Connections {
        target: arcadeBackend
        function onFinished(exitCode, reason, notice) {
            if (playRoot.phase !== "running")
                return
            // Back to the list, as after a video.
            if (reason === "ok") {
                playRoot.goBack()
                return
            }
            playRoot.phase = "failed"
            playRoot.message = notice
            playRoot.output = arcadeBackend.output()
        }
    }

    Keys.onPressed: function(event) {
        event.accepted = true
        if (!isBack(event.key) || event.isAutoRepeat)
            return
        if (playRoot.phase === "opening") {
            launchTimer.stop()
            playRoot.phase = "canceled"
            playRoot.goBack()
        } else if (playRoot.phase === "error" || playRoot.phase === "failed") {
            playRoot.goBack()
        }
    }
    Keys.onReleased: function(event) { event.accepted = true }

    // ---
    // UI
    // ---

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    Column {
        x: root.sw * 0.125 //80
        y: root.sh * 0.25 //120
        width: root.sw * 0.75 //480
        spacing: root.sh * 0.025 //12

        // The deck's own display, as for a video.
        Row {
            visible: playRoot.phase === "opening" || playRoot.phase === "running"
            spacing: root.sw * 0.0125 //8
            Text {
                id: playLabel
                text: "PLAY"
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.05 //24
            }
            PixelIcon {
                name: "play"
                color: root.primaryColor
                anchors.verticalCenter: playLabel.verticalCenter
            }
        }

        Text {
            width: parent.width
            text: playRoot.phase === "error" || playRoot.phase === "failed"
                  ? "Could not play " + playRoot.title : playRoot.title
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.05 //24
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        // How to come back, in a box, the way a deck prints a notice.
        Rectangle {
            visible: playRoot.phase === "opening" || playRoot.phase === "running"
            width: parent.width
            height: howTo.height + 2 * root.sh * 0.025
            color: "transparent"
            border.width: root.px
            border.color: root.primaryColor
            antialiasing: false
            Column {
                id: howTo
                x: root.sw * 0.0125 //8
                y: root.sh * 0.025 //12
                width: parent.width - 2 * x
                spacing: root.sh * 0.0125 //6
                Repeater {
                    model: (playRoot.exitKeys !== "" ? ["Press " + playRoot.exitKeys + " together",
                                                         "to come back to OSD/OS",
                                                         "or Esc on a keyboard"]
                                                      : ["Press Esc on a keyboard",
                                                         "to come back to OSD/OS"])
                    Text {
                        required property string modelData
                        width: parent.width
                        text: modelData
                        color: root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.0375 //18
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // Why it didn't play, and what MAME said.
        Text {
            visible: playRoot.phase === "error" || playRoot.phase === "failed"
            width: parent.width
            text: playRoot.message
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0375 //18
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        Text {
            visible: playRoot.phase === "error" && playRoot.message === "MAME is not installed"
            width: parent.width
            text: "On Raspberry Pi OS: sudo apt install mame"
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0291667 //14
            wrapMode: Text.WordWrap
        }
        Rectangle {
            visible: playRoot.phase === "failed" && playRoot.output !== ""
            width: parent.width
            height: root.sh * 0.2 + root.hintRoom //96
            color: "transparent"
            border.width: root.px
            border.color: root.primaryColor
            antialiasing: false
            clip: true
            Text {
                anchors.fill: parent
                anchors.margins: root.sw * 0.0125 //8
                text: playRoot.output
                textFormat: Text.PlainText
                // The end of the output is where the error is.
                verticalAlignment: Text.AlignBottom
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0291667 //14
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
        }
    }

    HintBar {
        visible: playRoot.phase !== "running"
        text: root.hints.back + (playRoot.phase === "opening" ? ":CANCEL" : ":BACK")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
