import QtQuick

// A wait for a press, full screen (PromptScreen): what to press, and under it,
// in its body, the one rule every such screen has and how long it has left,
// so that both show with Settings' Hint Bar off too:
//
//     PRESS THE NEW BUTTON FOR [BACK] ON 8BITDO PRO 2
//     HOLD ANY BUTTON TO STOP
//     STOPS BY ITSELF IN 9
//
// A tap answers and a hold stops: InputManager tells them apart
// (controllerInputCaptured(), captureHeld()), the host listens. The presses
// of the controller waited for never come here as keys; another controller's
// do, and its Back stops the wait too (backPressed()). It counts the seconds
// down itself (start(), stop()) and says when they are out (timedOut()); a
// host that goes on waiting (a test, at each press) starts it again.
//
// Items declared inside it go under the text (PromptScreen's content), and
// choices under those: the presses seen so far.
FocusScope {
    id: press

    property string title: ""
    // What to press, and for what.
    property string message: ""
    // A line of its own between that and the rule (the joystick's part in
    // Set One by One), "" for none.
    property string extra: ""
    // A keyboard's or a remote's keys are waited for, not a pad's buttons.
    property bool keys: false
    property int secondsLeft: 0
    property alias choices: screen.choices
    property alias currentIndex: screen.currentIndex
    property alias maxChoices: screen.maxChoices
    default property alias content: screen.content

    readonly property string rule: "Hold any " + (keys ? "key" : "button") + " to stop"
    readonly property string countdown: "Stops by itself in " + secondsLeft

    signal backPressed()
    signal timedOut()

    function start(seconds) {
        secondsLeft = seconds
        timer.restart()
    }
    function stop() {
        timer.stop()
    }

    anchors.fill: parent
    visible: false
    // Hidden, it lets go of the keys: an item only hidden would keep them.
    enabled: visible
    onVisibleChanged: if (!visible) timer.stop()

    // Autorepeat is ignored: the press that opened this may still be held
    // when repeat kicks in. Ctrl+Q and the rest go on to Main.qml.
    Keys.onPressed: function(event) {
        if (event.modifiers & Qt.ControlModifier)
            return
        if (!event.isAutoRepeat
                && (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back))
            press.backPressed()
        event.accepted = true
    }

    // While it waits the screen saver does too: the presses it takes are no
    // keys to it.
    Timer {
        id: timer
        interval: 1000
        repeat: true
        onTriggered: {
            if (idleTracker)
                idleTracker.resetActivity()
            press.secondsLeft--
            if (press.secondsLeft > 0)
                return
            stop()
            press.timedOut()
        }
    }

    PromptScreen {
        id: screen
        objectName: "pressScreen"
        kind: "notice"
        title: press.title
        message: press.message + (press.extra !== "" ? "\n" + press.extra : "") + "\n" + press.rule + "\n" + press.countdown
        currentIndex: -1
        hint: (press.rule + " (" + press.secondsLeft + ")").toUpperCase()
    }
}
