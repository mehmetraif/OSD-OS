import QtQuick

// A long list to pick one of (a time zone, a keyboard's layout), in the
// standard window (PromptScreen): ▲ ▼ one at a time, round the ends, ◄ ► from
// group to group (the time zones' regions, the names' first letters), select
// picks it and back leaves it as it was. The host binds entries to
// [{ label, value, group }], opens it on the value in force (open(value)) and
// acts on picked(value); either way it closes first (closed()).
FocusScope {
    id: picker

    property string title: ""
    property string message: ""
    property var entries: []
    // What ◄ ► step by, as the hint bar names it.
    property string groupName: "GROUP"
    property int index: 0

    signal picked(var value)
    signal closed()

    anchors.fill: parent
    visible: false
    // Hidden, it lets go of the keys: an item only hidden would keep them.
    enabled: visible

    function open(current) {
        index = 0
        for (var i = 0; i < entries.length; i++) {
            if (entries[i].value === current)
                index = i
        }
        visible = true
        forceActiveFocus()
    }
    function close() {
        visible = false
        closed()
    }
    // To the first entry of the next group (step 1), or of the one before
    // (step -1), round.
    function jump(step) {
        var n = entries.length
        if (n === 0)
            return
        var group = entries[index].group
        var i = index
        if (step > 0) {
            while (i < n && entries[i].group === group)
                i++
            index = i < n ? i : 0
        } else {
            while (i >= 0 && entries[i].group === group)
                i--
            if (i < 0)
                i = n - 1
            while (i > 0 && entries[i - 1].group === entries[i].group)
                i--
            index = i
        }
    }

    Keys.onPressed: function(event) {
        // Ctrl+Q and the rest go on to Main.qml.
        if (event.modifiers & Qt.ControlModifier)
            return
        event.accepted = true
        var n = entries.length
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            close()
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && n > 0) {
            var value = entries[index].value
            close()
            picked(value)
        } else if (event.key === Qt.Key_Up && n > 0) {
            index = (index - 1 + n) % n
        } else if (event.key === Qt.Key_Down && n > 0) {
            index = (index + 1) % n
        } else if (event.key === Qt.Key_Left) {
            jump(-1)
        } else if (event.key === Qt.Key_Right) {
            jump(1)
        }
    }

    PromptScreen {
        title: picker.title
        message: picker.message
        choices: picker.entries.map(function(e) { return { label: e.label, action: e.value } })
        currentIndex: picker.index
        onHovered: function(index) { picker.index = index }
        hint: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.change + ":" + picker.groupName + " "
              + root.hints.select + ":SELECT"
    }
}
