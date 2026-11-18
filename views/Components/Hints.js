.pragma library

// The keys as every view takes them and names them, for Main.qml's
// root.isBack() and root.hintLine(), which the views call.
//
//     Keys.onPressed: function(event) {
//         if (root.isBack(event)) { goBack(); event.accepted = true }
//     }
//     HintBar { text: root.hintLine(["back:BACK", "navigate:NAVIGATE", "select:SELECT"]) }

// Whether a key event is back: Esc (a pad's back comes as Esc, by
// InputManager), Backspace, or a remote's Back.
function isBack(event) {
    return event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back
}

// A hint bar's line from its parts, each "key:WHAT": the key's hint as
// `hints` has it (InputManager's, for the keyboard or a pad), then what it
// does, the parts a space apart ("[ESC]:BACK [ENTER]:SELECT"). A part that
// is "" is left out, so a part that comes and goes is written in place:
// cond ? "change:CHANGE" : "".
function line(hints, parts) {
    return parts.filter(function(part) { return !!part }).map(function(part) {
        var colon = part.indexOf(":")
        return hints[part.slice(0, colon)] + part.slice(colon)
    }).join(" ")
}
