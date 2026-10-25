import QtQuick
import Components

// What pairing a Bluetooth device needs, over Settings → Bluetooth while it
// does (bluetoothManager.prompt):
// - "passkey": a code to type on the device (a keyboard), then its Enter. The
//   digits typed so far stand out, for a device that says.
// - "pin": the same for an older device, which doesn't say.
// - "confirm": does the device show the same code? YES / NO (answered).
// Back gives the pairing up (canceled).
FocusScope {
    id: promptRoot

    property var prompt: ({})

    signal answered(bool accept)
    signal canceled()

    readonly property string kind: (prompt && prompt.kind) || ""
    readonly property string code: (prompt && prompt.code) || ""
    readonly property int entered: (prompt && prompt.entered) || 0
    property int choiceIndex: 0

    visible: kind !== ""
    // Hidden, it lets go of the keys: an item only hidden would keep them.
    enabled: visible
    onVisibleChanged: {
        if (visible) {
            choiceIndex = 0
            forceActiveFocus()
        }
    }

    Keys.onPressed: function(event) {
        // Ctrl+Q and the rest go on to Main.qml.
        if (event.modifiers & Qt.ControlModifier)
            return
        event.accepted = true
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            canceled()
        } else if (kind === "confirm") {
            if (event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                choiceIndex = 1 - choiceIndex
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                answered(choiceIndex === 0)
        }
    }

    PromptScreen {
        // A question to answer, or a code to type: a notice.
        kind: promptRoot.kind === "confirm" ? "question" : "notice"
        title: "Pairing"
        message: (promptRoot.prompt && promptRoot.prompt.name) || ""
        choices: promptRoot.kind === "confirm" ? ["Yes", "No"] : []
        currentIndex: promptRoot.choiceIndex
        onHovered: function(index) { promptRoot.choiceIndex = index }
        hint: promptRoot.kind === "confirm"
              ? root.hints.back + ":CANCEL " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
              : root.hints.back + ":CANCEL"

        Column {
            width: parent.width
            spacing: root.sh * 0.0333333 //16

            Text {
                width: parent.width
                text: promptRoot.kind === "confirm"
                      ? "Does it show this code?"
                      : "Type this code on it, then press its Enter key"
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0375 //18
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            // The code, a box per digit: those typed so far solid.
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: root.sw * 0.0125 //8
                Repeater {
                    model: promptRoot.code.length
                    delegate: Rectangle {
                        readonly property bool typed: promptRoot.kind === "passkey" && index < promptRoot.entered
                        width: digit.implicitWidth + root.sw * 0.01875 //12
                        height: digit.implicitHeight + root.sh * 0.0125 //6
                        color: typed ? root.primaryColor : "transparent"
                        antialiasing: false
                        Text {
                            id: digit
                            anchors.centerIn: parent
                            text: promptRoot.code.charAt(index)
                            color: parent.typed ? root.surfaceColor : root.primaryColor
                            font.family: root.globalFont
                            font.pixelSize: root.sh * 0.1 //48
                        }
                    }
                }
            }
        }
    }
}
