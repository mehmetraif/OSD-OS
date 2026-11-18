.pragma library

// The keys as every view takes them, for Main.qml's root.isBack(), which
// the views call.
//
//     Keys.onPressed: function(event) {
//         if (root.isBack(event)) { goBack(); event.accepted = true }
//     }

// Whether a key event is back: Esc (a pad's back comes as Esc, by
// InputManager), Backspace, or a remote's Back.
function isBack(event) {
    return event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back
}
