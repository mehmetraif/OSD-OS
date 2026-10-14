import QtQuick
import Components

// A game in MAME (ArcadeBackend::play). This view says which game and how to
// come back (Settings → Arcade → Start Info Screen), then MAME has the screen
// until the game ends, and the view goes back to the list. On a headless Pi
// it isn't drawn while MAME runs: what it shows just before the hand-off
// stays on the screen while MAME starts. A game that couldn't start, or that
// MAME ended with an error, says why here.
//
// From the Arcade screen (ArcadeScreen.qml) it has that screen's bar, says
// how many credits go in, and takes COIN (another credit) and START or
// select (the game at once) before MAME starts; the credits go in with the
// game (ArcadeBackend::play), and are spent.
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

    // The design's units, for the Arcade screen's bar.
    readonly property real u: root.sw / 960
    readonly property real v: root.sh / 720

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
    // Played from the Arcade screen rather than the original tree.
    readonly property bool arcade: moduleRoot.arcadeScreen
    readonly property bool before: phase === "opening" || phase === "running"

    function isBack(key) {
        return key === Qt.Key_Escape || key === Qt.Key_Backspace || key === Qt.Key_Back
    }

    function launch() {
        launchTimer.stop()
        if (playRoot.phase !== "opening")
            return
        if (arcadeBackend.play(playRoot.game, moduleRoot.credits)) {
            playRoot.phase = "running"
            // In the game now.
            moduleRoot.credits = 0
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

    // This frame on the screen first, as a headless Pi hands it over. With
    // Start Info Screen on, long enough to read how to come back: from the
    // Arcade screen every time, from the tree the first game this run, as
    // the screen stays as it is while MAME starts.
    Timer {
        id: launchTimer
        interval: !moduleRoot.startInfo ? 150
                  : playRoot.arcade ? 3000
                  : arcadeBackend && arcadeBackend.played ? 150 : 1500
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
        if (event.isAutoRepeat)
            return
        if (playRoot.arcade && playRoot.phase === "opening") {
            // As on the Arcade screen: COIN (a pad's Back button, or 5) buys
            // another credit, START or select plays at once.
            var button = typeof inputManager !== "undefined" && inputManager
                         ? inputManager.padButton(event.nativeScanCode) : ""
            if (button === "back" || event.key === Qt.Key_5) {
                moduleRoot.credits = Math.min(9, moduleRoot.credits + 1)
                return
            }
            if (event.key === Qt.Key_Space || event.key === Qt.Key_1
                || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                playRoot.launch()
                return
            }
        }
        if (!isBack(event.key))
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
        visible: !playRoot.arcade
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // The Arcade screen's bar: PLAY ▶, or STOP ■ for a game that didn't.
    ArcadeBar {
        visible: playRoot.arcade
        x: Math.round(120 * u)
        y: Math.round(90 * v)
        width: Math.round(420 * u)
        height: Math.round(42 * v)
        text: playRoot.before ? "Play" : "Stop"
        icon: playRoot.before ? "play" : "stop"
    }

    Column {
        x: root.sw * 0.125 //80
        y: root.sh * 0.25 //120
        width: root.sw * 0.75 //480
        spacing: root.sh * 0.025 //12

        // The deck's own display, as for a video (the Arcade screen's bar
        // says it).
        Row {
            visible: !playRoot.arcade && playRoot.before
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
            visible: moduleRoot.startInfo && playRoot.before
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

        // The credits going in with the game, from the Arcade screen.
        Text {
            visible: playRoot.arcade && moduleRoot.startInfo && playRoot.before
            width: parent.width
            text: moduleRoot.credits === 0 ? "No credit: insert a coin in the game"
                  : moduleRoot.credits + (moduleRoot.credits === 1 ? " credit goes" : " credits go")
                    + " in as the game starts"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0375 //18
            wrapMode: Text.WordWrap
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
        shown: playRoot.arcade ? moduleRoot.hintBar : root.hintBar
        text: playRoot.phase !== "opening" ? root.hints.back + ":BACK"
              : playRoot.arcade ? root.hints.back + ":CANCEL " + root.hints.coin + ":CREDIT "
                                  + root.hints.play_pause + ":PLAY"
              : root.hints.back + ":CANCEL"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
