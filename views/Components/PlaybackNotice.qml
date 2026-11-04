import QtQuick

// A video that couldn't be played, in the standard window (PromptScreen, a
// notice): "Playback failed", why in a few words, and under it what there is
// to add as it is written (how to install mpv, mpv's own line on the video,
// a server's message), with back to leave and, where the player can try
// again, select to retry. A player shows a failure so, never going back
// without a word (CONTRIBUTING).
//
// It takes the keys while it is up and gives them back to its parent, the
// player, as it goes: back is dismissed() (the player leaves), select
// retried() (the player starts the video again), or dismissed() too where
// there is nothing to retry.
//
//     PlaybackNotice {
//         id: failure
//         onRetried: playerRoot.retry()
//         onDismissed: playerRoot.goBack()
//     }
//     failure.showMpv(true)              // the session mpv couldn't play
//     failure.show(message, "", false)   // a server's message
FocusScope {
    id: notice

    property string title: "Playback failed"
    property string reason: ""
    property string detail: ""
    property bool canRetry: true

    signal retried()
    signal dismissed()

    anchors.fill: parent
    visible: false
    enabled: visible

    function show(why, more, retry) {
        reason = why || "The video could not be played"
        detail = more || ""
        canRetry = retry === undefined ? true : !!retry
        visible = true
        forceActiveFocus()
    }
    // Why the last session failed, as mpvController says (lastError,
    // lastErrorDetail).
    function showMpv(retry) {
        show(mpvController.lastError, mpvController.lastErrorDetail, retry)
    }
    function close() {
        visible = false
        // The player's own keys again, not a hidden notice's.
        focus = false
        if (parent)
            parent.forceActiveFocus()
    }

    Keys.onPressed: function(event) {
        // Ctrl+Q and the rest go on to Main.qml.
        if (event.modifiers & Qt.ControlModifier)
            return
        event.accepted = true
        var enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back
                || (enter && !canRetry)) {
            close()
            dismissed()
        } else if (enter) {
            close()
            retried()
        }
    }

    PromptScreen {
        kind: "notice"
        title: notice.title
        message: notice.reason
        hint: root.hints.back + ":BACK" + (notice.canRetry ? " " + root.hints.select + ":RETRY" : "")

        // As it is written: a command, a path or mpv's words keep their case.
        Text {
            visible: notice.detail !== ""
            width: parent.width
            textFormat: Text.PlainText
            text: notice.detail
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333 //16
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 4
            elide: Text.ElideRight
        }
    }
}
