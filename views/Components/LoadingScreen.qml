import QtQuick
import OSDOS.Video

// What a player shows while its video starts: a VCR's screen as a tape loads.
// The theme's ground in a tape's noise (VhsNoise), and a dubbing deck's
// on-screen display in its corners, in the theme's colours or Video 1's
// (Settings → TV / Movie → Loading Screen Colors: root.loadingInk,
// root.loadingPaper): TAPE A PLAY at the point the video is (where it starts
// from), TV between them, TAPE B LOADING with the video's length once that is
// known and the seconds it has taken until then, and the source under SLP ▶;
// what loads, when the player knows, across the middle.
// The tracking band jitters across the top and now and then rolls down over
// the lot, eating into the letters; Settings' Loading Effect turns all of
// that off, leaving the display on the plain ground. It runs while it is
// visible, and rests, still on its last frame, while another process has the
// screen.
//
//     LoadingScreen {
//         anchors.fill: parent
//         source: moduleRoot.moduleName
//         startMs: lastStartMs
//         durationMs: lastKnownDurationMs
//         visible: !playbackStarted
//     }
Item {
    id: tape

    // A video starting: the theme's effects and menu music rest meanwhile,
    // the playing as plain as it always is (Main.qml's root.effectsRest).
    readonly property bool resting: visible
    onRestingChanged: root.restFor(tape, resting)
    Component.onCompleted: if (resting) root.restFor(tape, true)
    Component.onDestruction: root.restFor(tape, false)

    // Under SLP ▶: what plays, a module's name say.
    property string source: "SOURCE"
    // TAPE A's counter: where the video is, in milliseconds; while it loads,
    // where it starts from.
    property int startMs: 0
    // TAPE B's counter once known: how long the video is, in milliseconds (0
    // while that isn't known, from mpv or the module's server).
    property int durationMs: 0
    // What loads, if known before it plays (a card's title): across the middle.
    property string title: ""

    // TAPE B's counter until the length is known: the seconds it has been up.
    property int elapsed: 0
    property bool blink: true
    // The display's jump sideways now and then, in art pixels.
    property int jitter: 0
    // Running. Not while mpv has the Pi's screen (the hand-off before its
    // picture shows), nor while a video played inside the window has its
    // picture over this (Transparent Background, until its first position
    // takes the screen down): nothing drawn here would reach the screen.
    readonly property bool live: visible && !root.screenHandedOff && !root.videoActive
    // The tape's effects: its noise and bands, the display's jumps and its
    // colour bleeding (Settings → TV / Movie → Loading Effect).
    property bool effect: root.loadingEffect

    readonly property real fontSize: root.sh * 0.05 //24

    function counter(seconds) {
        var s = Math.max(0, Math.floor(seconds))
        return root.pad(Math.floor(s / 3600)) + ":" + root.pad(Math.floor(s % 3600 / 60)) + ":" + root.pad(s % 60)
    }

    onVisibleChanged: {
        if (visible) {
            elapsed = 0
            blink = true
        }
    }

    // A line of the display in the VCR's font, its colour bleeding to the
    // right a little, as on a tape.
    component OsdLine: Item {
        property alias text: line.text
        width: line.implicitWidth
        height: line.implicitHeight

        // The bleed in the colours' second ink, Video 1's with Default.
        Text {
            x: root.px
            visible: tape.effect
            text: line.text
            color: root.loadingThemed ? root.secondaryColor : root.schemes["Video 1"].secondary
            opacity: 0.45
            font: line.font
        }
        Text {
            id: line
            color: root.loadingInk
            font.family: root.globalFont
            font.pixelSize: tape.fontSize
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.loadingPaper
    }

    VhsNoise {
        anchors.fill: parent
        visible: tape.effect
        color: root.loadingInk
        pixel: root.px
        running: tape.live
    }

    Item {
        id: osd
        anchors.fill: parent
        transform: Translate { x: tape.effect ? tape.jitter * root.px : 0 }

        Column {
            x: root.sw * 0.1 //64
            y: root.sh * 0.1125 //54
            spacing: root.sh * 0.0083333 //4
            OsdLine { text: "TAPE A" }
            OsdLine { text: "PLAY" }
            OsdLine { text: tape.counter(tape.startMs / 1000) }
        }

        // The deck's output, the way its display shows it: TV.
        OsdLine {
            anchors.horizontalCenter: parent.horizontalCenter
            y: root.sh * 0.1125 //54
            text: "TV"
        }

        Column {
            anchors.right: parent.right
            anchors.rightMargin: root.sw * 0.1 //64
            y: root.sh * 0.1125 //54
            spacing: root.sh * 0.0083333 //4
            OsdLine { anchors.right: parent.right; text: "TAPE B" }
            OsdLine { anchors.right: parent.right; text: "LOADING"; opacity: tape.blink ? 1 : 0 }
            OsdLine {
                anchors.right: parent.right
                text: tape.counter(tape.durationMs > 0 ? tape.durationMs / 1000 : tape.elapsed)
            }
        }

        Text {
            anchors.centerIn: parent
            width: root.sw * 0.76875 //492
            visible: tape.title !== ""
            text: tape.title
            color: root.loadingInk
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0333333 //16
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        Column {
            x: root.sw * 0.1 //64
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.sh * 0.1666667 //80
            spacing: root.sh * 0.0083333 //4
            OsdLine { text: "SLP▶" }
            OsdLine { text: tape.source.toUpperCase() }
        }

        Column {
            anchors.right: parent.right
            anchors.rightMargin: root.sw * 0.1 //64
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.sh * 0.1666667 //80
            spacing: root.sh * 0.0083333 //4
            OsdLine { anchors.right: parent.right; text: "SLP◀" }
            OsdLine { anchors.right: parent.right; text: "DEST" }
        }
    }

    // The bands over the display: where they pass, its letters break up.
    VhsNoise {
        anchors.fill: parent
        visible: tape.effect
        color: root.loadingInk
        shade: root.loadingPaper
        pixel: root.px
        streaks: 0
        bands: true
        running: tape.live
    }

    Timer {
        interval: 1000
        repeat: true
        running: tape.live
        onTriggered: tape.elapsed++
    }
    Timer {
        interval: 500
        repeat: true
        running: tape.live
        onTriggered: tape.blink = !tape.blink
    }
    Timer {
        interval: 120
        repeat: true
        running: tape.live && tape.effect
        onTriggered: tape.jitter = Math.random() < 0.06 ? (Math.random() < 0.5 ? -1 : 1) : 0
    }
}
