import QtQuick

// Full-screen takeover that writes an NFC card for the item a detail view is
// showing. Shared so every module that can be reached from a card writes them
// the same way; the host view supplies only what goes on the card.
//
// Flow: armed ("TAP A CARD") → captured → choose mode (or confirm a replace) →
// written. Capture is armed only while this is open, so a card resting near the
// reader while browsing can never trigger a write.
//
// The host is responsible for showing this (open()) and for whether the entry
// point is offered at all — see available.
FocusScope {
    id: writerRoot

    // What gets written.
    property string cardRef:   ""     // line 2, e.g. "plex://movie/…"
    property string cardTitle: ""     // filename + display title
    // Shows the shuffle option. Only meaningful for a set — a show, season,
    // collection or playlist: shuffling a movie has no meaning, and the card
    // format ignores mode for one anyway.
    property bool   offerShuffle: false
    // What the two ordered/shuffled choices are called. The defaults suit a show
    // or season; a collection or playlist overrides them.
    property string orderedLabel: "Sequential Episodes"
    property string shuffleLabel: "Shuffle Episodes"

    // True when a card could actually be written right now. Hosts bind their
    // entry-point row's visibility to this.
    readonly property bool available:
        typeof nfcReaderBackend !== "undefined"
        && nfcReaderBackend.available
        && nfcReaderBackend.readerConnected
        && appCore.is_module_enabled("com.osdos.nfc_reader")

    // "armed" | "choose" | "replace" | "done" | "failed"
    property string phase: "armed"
    property string capturedUid: ""
    property string existingTitle: ""
    property int    choiceIndex: 0
    property bool   pendingShuffle: false

    signal closed()

    visible: false
    focus: visible

    function open() {
        phase = "armed"
        capturedUid = ""
        existingTitle = ""
        choiceIndex = 0
        pendingShuffle = false
        visible = true
        nfcReaderBackend.setCardCapture(true)
        forceActiveFocus()
    }

    function close() {
        nfcReaderBackend.setCardCapture(false)
        visible = false
        closed()
    }

    // Each choice carries a stable action alongside its label. The action is what
    // drives behaviour — never the label text, which is free to be reworded.
    function choices() {
        // Changing what a card plays is asked as every question is: Cancel
        // first, under the cursor as it opens, then the verb.
        if (phase === "replace")
            return [{ label: "Cancel",      action: "cancel" },
                    { label: "Change Card", action: "write" }]
        if (offerShuffle)
            return [{ label: "Write card (" + orderedLabel + ")", action: "write" },
                    { label: "Write card (" + shuffleLabel + ")", action: "shuffle" },
                    { label: "Cancel",                            action: "cancel" }]
        return [{ label: "Write card", action: "write" },
                { label: "Cancel",     action: "cancel" }]
    }

    function commit() {
        var ok = nfcReaderBackend.writeCardFile(capturedUid, cardTitle, cardRef,
                                                pendingShuffle ? "shuffle" : "")
        phase = ok ? "done" : "failed"
    }

    function activate() {
        var choice = choices()[choiceIndex]
        if (!choice || choice.action === "cancel") { close(); return }

        // The replace step re-confirms an already-chosen mode; don't re-read it
        // from the (two-option) replace list, which has no shuffle entry.
        if (phase === "replace") { commit(); return }

        pendingShuffle = (choice.action === "shuffle")
        // Confirm before overwriting a card that already plays something —
        // a card is a physical object the user may not remember assigning.
        if (existingTitle !== "") {
            phase = "replace"
            choiceIndex = 0
            return
        }
        commit()
    }

    Connections {
        target: writerRoot.visible && typeof nfcReaderBackend !== "undefined" ? nfcReaderBackend : null
        function onCardCaptured(uid, existing) {
            if (writerRoot.phase !== "armed") return
            // One card per arm: disarm so a card left on the reader can't walk
            // the dialog forward underneath the user.
            nfcReaderBackend.setCardCapture(false)
            writerRoot.capturedUid   = uid
            writerRoot.existingTitle = existing
            writerRoot.choiceIndex   = 0
            writerRoot.phase         = "choose"
        }
    }

    // Torn down while open (the host's view left): a card tapped is for
    // the reader's own screen again, not a writer that is gone.
    Component.onDestruction: {
        if (visible && typeof nfcReaderBackend !== "undefined")
            nfcReaderBackend.setCardCapture(false)
    }

    // A window over the host's view: every key stops here (the host's
    // list must not move under it), but for a chord like Ctrl+Q.
    Keys.onPressed: function(event) {
        if (event.modifiers & Qt.ControlModifier)
            return
        event.accepted = true
        var enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        if (root.isBack(event)) {
            close()
            return
        }
        if (phase === "done" || phase === "failed") {
            if (enter)
                close()
            return
        }
        if (phase === "armed") return   // nothing to choose until a card arrives

        var list = choices()
        if (event.key === Qt.Key_Up)
            choiceIndex = (choiceIndex - 1 + list.length) % list.length
        else if (event.key === Qt.Key_Down)
            choiceIndex = (choiceIndex + 1) % list.length
        else if (enter)
            activate()
    }

    // In the window every question has (PromptScreen): what to do or what
    // happened in its title bar, the title the card is for under it, and
    // the answers while there are some to choose.
    PromptScreen {
        readonly property bool asking: writerRoot.phase === "choose" || writerRoot.phase === "replace"
        kind: asking ? "question" : "notice"
        title: writerRoot.phase === "armed"    ? "Tap a card for..."
             : writerRoot.phase === "replace"  ? "This card plays \"" + writerRoot.existingTitle + "\". Change to..."
             : writerRoot.phase === "done"     ? "Card written for..."
             : writerRoot.phase === "failed"   ? "Could not write card"
             :                                   "Write card for..."
        message: writerRoot.cardTitle
        choices: asking ? writerRoot.choices() : []
        currentIndex: writerRoot.choiceIndex
        onHovered: function(index) { writerRoot.choiceIndex = index }
        hint: writerRoot.phase === "armed" ? root.hintLine(["back:CANCEL"])
            : asking ? root.hintLine(["back:CANCEL", "navigate:NAVIGATE", "select:SELECT"])
            : root.hintLine(["select:DONE"])
    }
}
