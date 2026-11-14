import QtQuick

// The screen saver: black over everything, with OSD/OS's logo bouncing round
// it as on a DVD player, to spare a tube left on a menu. Main.qml shows it
// (shown) once no key has been pressed for Settings' time (idleTracker), and
// puts it away at the next key, which is spent on that alone (woken), as the
// mouse moves, or when something else takes the screen.
//
// While it is up the keys are its own, whatever comes up under it. A
// question asked meanwhile (the update's, as its download ends) takes the
// keys as it opens; the saver takes them straight back, or the next key
// would answer a question nobody can see, and the saver would stay up with
// no key to put it away. As it goes it hands the keys to whoever has them
// under it: the first of keysTo there is (a notice over the views before
// the views), and through it to what is open there, that question too.
//
//     ScreenSaver {
//         anchors.fill: parent
//         shown: root.screenSaverActive
//         keysTo: [noticeLoader.item, moduleLoader]
//         onWoken: root.screenSaverActive = false
//     }
Item {
    id: saver

    property bool shown: false
    // Who has the keys under it, the top-most first; null for one that is
    // not there now.
    property var keysTo: []

    // A key pressed while it is up, which it took: its host puts it away.
    signal woken()
    // Ctrl+Q, which no view can see while the keys are here.
    signal quitAsked()

    visible: shown
    focus: visible

    onShownChanged: {
        if (shown) {
            var usableW = width - bounceLogo.width
            var usableH = height - bounceLogo.height
            bounceLogo.x = Math.random() * (usableW > 0 ? usableW : 1)
            bounceLogo.y = Math.random() * (usableH > 0 ? usableH : 1)
            bounceLogo.vx = (Math.random() > 0.5 ? 1 : -1) * (1 + Math.random() * 1.5)
            bounceLogo.vy = (Math.random() > 0.5 ? 1 : -1) * (1 + Math.random() * 1.5)
            forceActiveFocus()
        } else {
            giveKeysBack()
        }
    }
    // Something under it took the keys as it opened: back they come, and
    // go to it again once the saver has gone (giveKeysBack()).
    onFocusChanged: if (shown && !focus) forceActiveFocus()

    function giveKeysBack() {
        for (var i = 0; i < keysTo.length; i++) {
            if (keysTo[i]) {
                keysTo[i].forceActiveFocus()
                return
            }
        }
    }

    // Solid black background — no transparency so it serves as a true
    // CRT burn-in prevention black frame between the logo bounces.
    Rectangle {
        anchors.fill: parent
        color: "#000000"
    }

    // Bouncing logo — classic DVD player screen saver
    Image {
        id: bounceLogo
        source: "../../assets/images/logo.svg"
        sourceSize.width: root.sw * 0.05
        sourceSize.height: root.sw * 0.05
        fillMode: Image.PreserveAspectFit
        antialiasing: true

        property real vx: 0
        property real vy: 0

        // Physics tick at ~60 fps while the overlay is visible
        Timer {
            interval: 16
            repeat: true
            running: saver.shown
            onTriggered: {
                bounceLogo.x += bounceLogo.vx
                bounceLogo.y += bounceLogo.vy

                if (bounceLogo.x + bounceLogo.width > saver.width) {
                    bounceLogo.x = saver.width - bounceLogo.width
                    bounceLogo.vx = -Math.abs(bounceLogo.vx)
                } else if (bounceLogo.x < 0) {
                    bounceLogo.x = 0
                    bounceLogo.vx = Math.abs(bounceLogo.vx)
                }

                if (bounceLogo.y + bounceLogo.height > saver.height) {
                    bounceLogo.y = saver.height - bounceLogo.height
                    bounceLogo.vy = -Math.abs(bounceLogo.vy)
                } else if (bounceLogo.y < 0) {
                    bounceLogo.y = 0
                    bounceLogo.vy = Math.abs(bounceLogo.vy)
                }
            }
        }
    }

    // Capture any keypress to dismiss — consumes the event so the
    // underlying view never sees it, preventing accidental navigation.
    // Ctrl+Q still quits (the views' loader is not above it in the keys'
    // chain, so it can't see keys focused here: the host is asked directly).
    Keys.onPressed: (event) => {
        event.accepted = true
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Q) {
            saver.quitAsked()
            return
        }
        saver.woken()
    }
}
