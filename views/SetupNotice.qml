import QtQuick
import Components

// What became of OSD/OS Setup's file, taken as the app started from its data
// folder or from beside it (src/util/SetupFile, main.cpp): the parts set,
// and those an OSD/OS card alone sets, named. The first thing on screen
// after a display output's window, gone with select or back.
//
// Main.qml loads it while there is one (root.setupNotice), holding the
// startup module back (root.startupHolding), and gives the keys back once
// it is dismissed.
FocusScope {
    id: noticeRoot

    Keys.onPressed: function(event) {
        // Ctrl+Q and the rest go on to Main.qml.
        if (event.modifiers & Qt.ControlModifier)
            return
        event.accepted = true
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Escape
                || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back)
            appCore.dismissSetupNotice()
    }

    PromptScreen {
        kind: "notice"
        title: root.setupNotice
        message: root.setupNoticeDetail
        choices: []
        hint: root.hints.select + ":OK"
    }
}
