import QtQuick

// A wait for a press, full screen (PromptScreen): what to press, and under it,
// in its body, the one rule every such screen has and how long it has left,
// so that both show with Settings' Hint Bar off too:
//
//     PRESS THE NEW BUTTON FOR [BACK] ON 8BITDO PRO 2
//     HOLD ANY BUTTON TO STOP
//     STOPS BY ITSELF IN 9
//
// It makes the wait itself: begin() tells InputManager which controllers it
// waits for (controllerKeys) and for how long (seconds, which it counts down
// on screen too), again() starts both anew (a test at each press, the next
// step of Set One by One), and end() lets the controllers act again. A tap
// answers and a hold stops: InputManager tells them apart, and the prompt
// passes them on, for the controllers waited for alone (tapped(), held();
// inputDown() at once, for a screen that shows presses). Their presses never
// come here as keys; another controller's do, and its Back stops the wait
// too (backPressed()). The seconds out, it says so (timedOut()). Whether it
// shows is the host's (visible), as the host's state says what is waited for.
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
    // The keys of the controllers waited for (a pad's, a keyboard's, allKeys,
    // the JAMMA board's two sticks), and how long a wait lasts.
    property var controllerKeys: []
    property int seconds: 10
    property int secondsLeft: 0
    readonly property bool waiting: d.waiting
    property alias choices: screen.choices
    property alias currentIndex: screen.currentIndex
    property alias maxChoices: screen.maxChoices
    default property alias content: screen.content

    readonly property string rule: "Hold any " + (keys ? "key" : "button") + " to stop"
    readonly property string countdown: "Stops by itself in " + secondsLeft

    signal tapped(string key, string input, string label)
    signal held(string key)
    signal inputDown(string key, string input, string label)
    signal backPressed()
    signal timedOut()

    // The wait begun: the keys come here. The host shows the prompt first.
    function begin() {
        again()
        forceActiveFocus()
    }
    // The count from the top, and InputManager told again: it listens for as
    // long.
    function again() {
        d.waiting = true
        secondsLeft = seconds
        timer.restart()
        if (inputManager)
            inputManager.captureControllers(controllerKeys, seconds)
    }
    // The wait over, the controllers acting again. Returns whether the prompt
    // had the keys, for the host to give them back where they belong.
    function end() {
        var hadKeys = activeFocus
        d.waiting = false
        timer.stop()
        if (d.manager)
            d.manager.captureController("")
        return hadKeys
    }

    QtObject {
        id: d
        property bool waiting: false
        // InputManager as the prompt was made, for its end: the context
        // property may read null while the view Loader tears down.
        property var manager: null
    }
    Component.onCompleted: d.manager = inputManager
    // Its page left while it waits: the controllers act again.
    Component.onDestruction: {
        if (d.manager)
            d.manager.captureController("")
    }

    Connections {
        target: inputManager
        enabled: d.waiting
        function onControllerInputDown(key, input, label) {
            if (press.controllerKeys.indexOf(key) >= 0)
                press.inputDown(key, input, label)
        }
        function onControllerInputCaptured(key, input, label) {
            if (press.controllerKeys.indexOf(key) >= 0)
                press.tapped(key, input, label)
        }
        function onCaptureHeld(key) {
            if (press.controllerKeys.indexOf(key) >= 0)
                press.held(key)
        }
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
                && root.isBack(event))
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
