import QtQuick
import Components

// Remote/keyboard button remapping, lets an extra physical key (e.g. a
// remote's "OK" button sending a code the app doesn't otherwise recognize)
// also fire one of the six navigation actions, on top of that action's
// default key, on every keyboard and remote (Settings → Controls → All
// Keyboards & Remotes; one keyboard's own are on its page,
// ControllerSettings.qml). Backed by InputManager's keyRemap table (config.json
// app.remote_keymap.<action>), which is additive: it never removes an
// action's default key, so a bad remap can't lock the menus out from a
// plain keyboard. See src/input/InputManager.cpp for the runtime side.
FocusScope {
    id: remapRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: ({})

    readonly property var actions: [
        { id: "up",     label: "Up" },
        { id: "down",   label: "Down" },
        { id: "left",   label: "Left" },
        { id: "right",  label: "Right" },
        { id: "select", label: "Select / OK" },
        { id: "back",   label: "Back" }
    ]

    property var rows: []
    property bool capturing: false
    property int captureIndex: -1

    // While capturing, InputManager suspends remap delivery and reports every
    // real input (keyboard key, Consumer Control button, mouse button) through
    // auxButtonPressed instead — without this, a key that's already bound
    // would be consumed by the remap filter and captured as its action's
    // default key rather than as itself, and capturing a bound mouse/remote
    // button would fire the action on the same press that bound it.
    onCapturingChanged: inputManager.setRemapCapture(capturing)
    Component.onDestruction: inputManager.setRemapCapture(false)

    function buildRows() {
        var list = []
        for (var i = 0; i < actions.length; i++) {
            var a = actions[i]
            var stored = appCore.get_setting("", "remote_keymap." + a.id)
            // A never-written key arrives as undefined or null depending on
            // the Qt version's invalid-QVariant conversion — check both.
            var hasCustom = stored !== undefined && stored !== null && stored !== "" && stored !== 0
            list.push({
                id: a.id,
                label: a.label,
                value: hasCustom ? ("default + " + inputManager.keyDisplayName(stored)) : "default"
            })
        }
        list.push({ id: "reset", label: "Reset to Defaults", isReset: true })
        rows = list
        if (rowList.currentIndex < 0 || rowList.currentIndex >= rows.length)
            rowList.currentIndex = 0
    }

    function startCapture(idx) {
        captureIndex = idx
        capturing = true
    }

    function finishCapture(qtKey) {
        if (captureIndex >= 0 && captureIndex < actions.length) {
            // One physical button, one action: InputManager's remap table is
            // keyed by the button, so a key left bound to two actions would
            // silently fire only one of them while both rows claim it. Clear
            // it from any other action before saving.
            for (var i = 0; i < actions.length; i++) {
                if (i !== captureIndex
                        && appCore.get_setting("", "remote_keymap." + actions[i].id) === qtKey)
                    appCore.save_setting("", "remote_keymap." + actions[i].id, 0)
            }
            appCore.save_setting("", "remote_keymap." + actions[captureIndex].id, qtKey)
        }
        capturing = false
        captureIndex = -1
        buildRows()
        rowList.forceActiveFocus()
    }

    function cancelCapture() {
        capturing = false
        captureIndex = -1
        rowList.forceActiveFocus()
    }

    function resetAll() {
        for (var i = 0; i < actions.length; i++)
            appCore.save_setting("", "remote_keymap." + actions[i].id, 0)
        buildRows()
    }

    Component.onCompleted: {
        buildRows()
        rowList.forceActiveFocus()
    }

    // The single capture feed: while capturing, InputManager reports every
    // real input — keyboard key, Consumer Control button, mouse button —
    // through this one signal (see setRemapCapture). Back-family keys cancel
    // instead of binding, so there is always a guaranteed way out.
    Connections {
        target: inputManager
        function onAuxButtonPressed(extendedKeyId) {
            if (!remapRoot.capturing)
                return
            if (extendedKeyId === Qt.Key_Escape || extendedKeyId === Qt.Key_Backspace
                    || extendedKeyId === Qt.Key_Back)
                remapRoot.cancelCapture()
            else
                remapRoot.finishCapture(extendedKeyId)
        }
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/keyboard.svg"
        title: "Controls"
        subtitle: "All Keyboards & Remotes"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    ListView {
        id: rowList
        model: remapRoot.rows
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        // One row short of the space, so the ▼ fits above the help line; a
        // row more for each bar that is off (Main.qml's helpRoom, hintRoom).
        height: root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224
        clip: true
        focus: !remapRoot.capturing

        Keys.onUpPressed: {
            if (currentIndex > 0) currentIndex-- 
            else {
                currentIndex = rows.length-1
            }
            rowList.positionViewAtIndex(currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            rowList.positionViewAtIndex(currentIndex, ListView.Contain)
        }
        Keys.onReturnPressed: {
            var row = remapRoot.rows[currentIndex]
            if (!row) return
            if (row.isReset) remapRoot.resetAll()
            else remapRoot.startCapture(currentIndex)
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                remapRoot.goBack()
                event.accepted = true
            }
        }

        // Lines read like the settings menus: "UP······DEFAULT".
        delegate: MenuRow {
            width: rowList.width
            height: root.sh * 0.0583333 //28
            label: modelData.label || ""
            value: modelData.isReset ? "" : (modelData.value || "")
            selected: rowList.currentIndex === index
        }
    }

    // ▲ / ▼ while lines are hidden above or below.
    ScrollMarks {
        anchors.fill: rowList
        list: rowList
    }

    // --- HELP TEXT --- on one line that scrolls when it is too long.
    HelpLine {
        visible: !remapRoot.capturing
        text: "Map one additional button for each action\n(the default button will continue to function)"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // --- FOOTER ---
    HintBar {
        visible: !remapRoot.capturing
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SET"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // --- CAPTURE OVERLAY ---
    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: remapRoot.capturing
        focus: remapRoot.capturing

        // Real keys never reach here while capturing — InputManager swallows
        // them and reports through auxButtonPressed (see the Connections
        // above). What still arrives is the synthesized default-key events a
        // gamepad produces: let its Back button cancel, and swallow the rest
        // so a pad press can't bind its synthesized key or leak into the list
        // underneath. Autorepeat is ignored: the press that opened this
        // overlay may still be held when repeat kicks in.
        Keys.onPressed: function(event) {
            if (!event.isAutoRepeat
                    && (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back))
                remapRoot.cancelCapture()
            event.accepted = true
        }

        PromptScreen {
            kind: "notice"
            title: "New button"
            message: remapRoot.captureIndex >= 0
                     ? "Press a new button for [" + remapRoot.actions[remapRoot.captureIndex].label + "]"
                     : ""
            hint: root.hints.back + ":CANCEL"
        }
    }
}
