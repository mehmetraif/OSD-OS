import QtQuick

// A full-screen keyboard-driven chooser: a prompt, the thing being acted on, and
// a short list of options. Factored out of NfcCardWriter.qml's chooser so every
// prompt in the app reads the same way, in the standard window (PromptScreen):
// the prompt in the title bar, the thing under it.
//
// The host binds `choices` to a list of { label, action } maps and acts on the
// action in onActivated — the action is what drives behavior, never the label
// text, which is free to change with state. Select closes it before acting,
// unless closeOnSelect is off: then the host closes it (close()) once it is
// done, as one that shows what became of the choice does. A host that acts on
// a choice before anything closes overrides choose(action).
FocusScope {
    id: overlayRoot

    property string promptText:   ""
    property string subtitleText: ""
    property var    choices:      []
    // PromptScreen's kind: "question" or "notice".
    property string promptKind:   "question"
    // The hint line: PromptScreen's own unless set.
    property string hintText:     ""
    property bool   closeOnSelect: true

    property int choiceIndex: 0

    signal activated(string action)
    signal closed()

    visible: false
    // Hidden, it lets go of the keys.
    enabled: visible
    focus: visible

    function open() {
        choiceIndex = 0
        visible = true
        forceActiveFocus()
    }

    function close() {
        visible = false
        closed()
    }

    // Select on a choice.
    function choose(action) {
        // Close first, so the host's onClosed focus restore lands before any
        // navigation the action triggers.
        if (closeOnSelect)
            close()
        activated(action)
    }

    Keys.onPressed: function(event) {
        var back = event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back
        var enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        if (back) {
            close()
        } else if (enter) {
            // With nothing to choose (a notice), select closes it too.
            var choice = choices[choiceIndex]
            if (choice)
                choose(choice.action)
            else
                close()
        } else if (event.key === Qt.Key_Up && choices.length > 0) {
            choiceIndex = (choiceIndex - 1 + choices.length) % choices.length
        } else if (event.key === Qt.Key_Down && choices.length > 0) {
            choiceIndex = (choiceIndex + 1) % choices.length
        }
        // A window over the host's view: its keys stop here (the host may
        // have uses for ◄ ►), but for a chord like Ctrl+Q.
        if (!(event.modifiers & Qt.ControlModifier))
            event.accepted = true
    }

    PromptScreen {
        kind: overlayRoot.promptKind
        title: overlayRoot.promptText
        message: overlayRoot.subtitleText
        choices: overlayRoot.choices
        currentIndex: overlayRoot.choiceIndex
        hint: overlayRoot.hintText
        // The answer under the mouse becomes the cursor's.
        onHovered: function(index) { overlayRoot.choiceIndex = index }
    }
}
