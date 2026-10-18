import QtQuick
import Components

// After Settings → Display Output switched the picture to another output, the
// first thing on it, over the boot screen: kept with select, else the old
// output comes back after `seconds` (displayOutput.revert()), so a screen that
// shows nothing puts itself right. Or, when the last change didn't stay, a
// word on it, gone with select.
//
// Main.qml loads it while it has something to say (root.displayHolding),
// holding the startup module back, and gives the keys back once it is done.
FocusScope {
    id: keepRoot

    // Long enough for a CRT to warm up and show the picture first: the
    // count starts with the app, before a tube has.
    readonly property int seconds: 30
    // Main.qml's mirrors of displayOutput, safe as the view tears down.
    readonly property bool asking: root.displayConfirmPending
    readonly property bool holding: asking || root.displayNotice !== ""
    property int remaining: seconds
    property int choiceIndex: 0

    visible: holding
    // Hidden, it lets go of the keys.
    enabled: visible

    Timer {
        interval: 1000
        repeat: true
        running: keepRoot.asking
        onTriggered: {
            keepRoot.remaining--
            if (keepRoot.remaining <= 0) {
                stop()
                displayOutput.revert()
            }
        }
    }

    Keys.onPressed: function(event) {
        event.accepted = true
        var enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        if (!keepRoot.asking) {
            // The notice: select or back closes it.
            if (enter || event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back)
                displayOutput.dismissNotice()
            return
        }
        if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            keepRoot.choiceIndex = 1 - keepRoot.choiceIndex
        } else if (enter) {
            if (keepRoot.choiceIndex === 0)
                displayOutput.keep()
            else
                displayOutput.revert()
        }
        // Back does nothing: on a screen that shows nothing, a key pressed at
        // random must not keep it.
    }

    PromptScreen {
        kind: keepRoot.asking ? "question" : "notice"
        title: keepRoot.asking ? "Keep this display output?" : root.displayNotice
        message: keepRoot.asking
                 ? root.displayCurrentLabel + "\nBack to " + root.displayPreviousLabel
                   + " in " + keepRoot.remaining + " sec"
                 : root.displayNoticeDetail
        choices: keepRoot.asking ? ["Keep", "Switch Back"] : []
        currentIndex: keepRoot.choiceIndex
        hint: keepRoot.asking ? root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
                              : root.hints.select + ":OK"
    }
}
