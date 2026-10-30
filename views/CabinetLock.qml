import QtQuick
import Components

// Settings → Cabinet Lock: OSD/OS kept on its Start on Module, a cabinet's
// Arcade screen, while the lock is on (Main.qml). Back there goes nowhere,
// so the main menu, Settings and Quit are out of reach, but for the unlock
// buttons: one to four inputs of any pads and keyboards, a direction among
// them if wanted, held together for three seconds (InputManager). They open
// the main menu until OSD/OS starts again, or until they are held again.
// Here the lock is turned on, once there are unlock buttons and a module to
// stay on, and the buttons are chosen by holding them (captureUnlockButtons()).
FocusScope {
    id: lockRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})

    // Null guards: context properties resolve to null while the view Loader
    // tears down (see Bluetooth.qml).
    readonly property string buttons: inputManager ? inputManager.unlockButtons : ""
    property bool lockOn: false
    // The Start on Module's name, "" for none (or one turned off).
    property string homeName: ""

    // Waiting for new unlock buttons: what is held now, by name, more than
    // four of them, and the seconds left with nothing held.
    property bool capturing: false
    property string heldNames: ""
    property bool tooMany: false
    property int secondsLeft: 0
    // Turning the lock on waited for the buttons to be chosen first.
    property bool lockAfter: false

    readonly property var rows: [
        { key: "lock", label: "Cabinet Lock", value: lockOn ? "On" : "Off",
          description: "Keeps OSD/OS on its Start on Module" + (homeName !== "" ? ", " + homeName : "")
                       + ": back there goes nowhere, and the main menu, Settings and Quit open only with the unlock"
                       + " buttons held together for three seconds, until OSD/OS starts again or they are held again" },
        { key: "buttons", label: "Unlock Buttons", value: buttons !== "" ? buttons : "None",
          description: "One to four buttons or keys held together, a direction among them if you like:"
                       + " select, then hold them for two seconds. Each but a direction waits a moment for the"
                       + " rest before it acts, so a START among them never starts a game" }
    ]

    function refresh() {
        lockOn = appCore.get_setting("", "cabinet_lock") === "On"
        var id = appCore.get_setting("", "startup_module") || ""
        var name = ""
        var modules = appCore.get_installed_modules()
        for (var i = 0; i < modules.length; i++) {
            if (modules[i].id === id && modules[i].enabled)
                name = modules[i].name
        }
        homeName = name
    }
    Component.onCompleted: refresh()

    // The lock turned on: asked first, with what unlocks it; without a module
    // to stay on, or without buttons, those first.
    function wantLock(on) {
        if (!on) {
            appCore.save_setting("", "cabinet_lock", "Off")
            refresh()
            return
        }
        if (homeName === "") {
            notice.promptText = "No Start on Module"
            notice.subtitleText = "Choose the module the cabinet stays on in Settings → Start on Module first"
            notice.open()
            return
        }
        if (buttons === "") {
            lockAfter = true
            startCapture()
            return
        }
        confirm.subtitleText = "OSD/OS stays on " + homeName + ". Hold " + buttons
                               + " together for three seconds to open the menus"
        confirm.open()
    }

    function startCapture() {
        if (!inputManager)
            return
        heldNames = ""
        tooMany = false
        secondsLeft = 10
        capturing = true
        overlay.forceActiveFocus()
        inputManager.captureUnlockButtons(true)
    }

    function activate(row) {
        if (!row)
            return
        if (row.key === "lock")
            wantLock(!lockOn)
        else if (row.key === "buttons")
            startCapture()
    }

    Connections {
        target: inputManager
        ignoreUnknownSignals: true
        function onUnlockButtonsHeld(names, many) {
            lockRoot.heldNames = names
            lockRoot.tooMany = many
            lockRoot.secondsLeft = 10
        }
        function onUnlockCaptureEnded(saved) {
            if (!lockRoot.capturing)
                return
            lockRoot.capturing = false
            list.forceActiveFocus()
            var lock = lockRoot.lockAfter && saved
            lockRoot.lockAfter = false
            if (lock)
                lockRoot.wantLock(true)
        }
    }

    // The seconds left of the wait with nothing held: InputManager ends it
    // at ten, this only counts them down.
    Timer {
        interval: 1000
        repeat: true
        running: lockRoot.capturing && lockRoot.heldNames === "" && lockRoot.secondsLeft > 0
        onTriggered: lockRoot.secondsLeft--
    }

    // Header
    AppBar {
        iconSource: "../../assets/images/lock.svg"
        title: "Cabinet Lock"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    ListView {
        id: list
        objectName: "lockList"
        // A model that never changes, so the cursor stays where it is as
        // the rows' values do; each row reads its own from rows.
        model: lockRoot.rows.length
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.4666667 + root.helpRoom + root.hintRoom //224
        clip: true
        focus: true
        highlightMoveDuration: 0

        Keys.onUpPressed: currentIndex = (currentIndex + count - 1) % count
        Keys.onDownPressed: currentIndex = (currentIndex + 1) % count
        Keys.onLeftPressed: {
            if (lockRoot.rows[currentIndex].key === "lock")
                lockRoot.wantLock(!lockRoot.lockOn)
        }
        Keys.onRightPressed: lockRoot.activate(lockRoot.rows[currentIndex])
        Keys.onReturnPressed: lockRoot.activate(lockRoot.rows[currentIndex])
        Keys.onEnterPressed: lockRoot.activate(lockRoot.rows[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                lockRoot.goBack()
                event.accepted = true
            }
        }

        // "CABINET LOCK·········◄ OFF ►", "UNLOCK BUTTONS·····P1 START + P2 START ►".
        delegate: MenuRow {
            required property int index
            readonly property var row: lockRoot.rows[index]
            width: list.width
            height: root.sh * 0.0583333 //28
            label: row.label
            value: row.value
            changes: row.key === "lock"
            opens: row.key === "buttons"
            keepValue: row.key === "buttons"
            selected: list.currentIndex === index
        }
    }

    HelpLine {
        text: lockRoot.rows[list.currentIndex].description
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE "
              + (lockRoot.rows[list.currentIndex].key === "lock" ? root.hints.change + ":CHANGE " : "")
              + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // --- WAITING FOR THE BUTTONS ---
    FocusScope {
        id: overlay
        objectName: "unlockCapture"
        anchors.fill: parent
        visible: lockRoot.capturing
        // Hidden, it lets go of the keys.
        enabled: visible

        // Every pad's press and every real key go to InputManager while it
        // waits; what comes here is a remote's or right shift's Back, which
        // ends the wait. Ctrl+Q goes on to Main.qml.
        Keys.onPressed: function(event) {
            if (event.modifiers & Qt.ControlModifier)
                return
            if (!event.isAutoRepeat
                    && (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back)
                    && inputManager)
                inputManager.captureUnlockButtons(false)
            event.accepted = true
        }

        PromptScreen {
            objectName: "capturePrompt"
            kind: "notice"
            title: "Unlock Buttons"
            message: "Hold the buttons that unlock the cabinet together, one to four of them, for two seconds"
            choices: [lockRoot.tooMany ? "At most four: let one go" : lockRoot.heldNames !== "" ? lockRoot.heldNames
                                                                                                  : "Nothing held"]
            currentIndex: lockRoot.heldNames !== "" && !lockRoot.tooMany ? 0 : -1
            hint: lockRoot.heldNames === "" ? "KEEPS THE OLD ONES IN " + lockRoot.secondsLeft : "HOLD THEM"
        }
    }

    // Asked before it locks: what it stays on and what unlocks it.
    ChoiceOverlay {
        id: confirm
        objectName: "lockConfirm"
        anchors.fill: parent
        promptText: "Lock the cabinet?"
        choices: [{ label: "Lock", action: "lock" }, { label: "Cancel", action: "cancel" }]
        onActivated: function(action) {
            // Main.qml opens the Start on Module at once.
            if (action === "lock")
                appCore.save_setting("", "cabinet_lock", "On")
        }
        onClosed: list.forceActiveFocus()
    }

    ChoiceOverlay {
        id: notice
        objectName: "lockNotice"
        anchors.fill: parent
        promptKind: "notice"
        onClosed: list.forceActiveFocus()
    }
}
