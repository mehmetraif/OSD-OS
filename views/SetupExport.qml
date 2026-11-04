import QtQuick
import Components

// EXPORT TO OSD/OS SETUP, Settings → System: this device's settings as QR
// codes (setupExport, src/util/SetupExporter.h), which a phone opens OSD/OS
// Setup's page (osdos.tv/setup) from, its form filled in, to change what it
// needs and make a card's file of. They carry passwords: made as the page
// opens, shown only once asked (the question says what they carry and what
// they can't), and forgotten as the page closes.
//
// Shown, the code is in the very middle of the screen, where a tube draws
// truest (its edges bend and blur), as large as the screen's safe area
// holds, a module at least three lines on a tube; nothing else but which code
// it is, at the top right ("1/2"). One code, or the parts of one where the
// settings need more, ◄ ► going through them; the page takes the parts in
// any order.
FocusScope {
    id: exportRoot

    signal goBack()

    property var navParams: ({})

    // The question up, before any code.
    property bool asking: true
    property int choiceIndex: 0
    // The code shown, of links.
    property int part: 0

    // Context properties read null while the Loader tears this view down.
    readonly property var links: setupExport ? setupExport.links : []
    readonly property bool busy: !!setupExport && setupExport.busy
    readonly property var missing: setupExport ? setupExport.missing : []
    readonly property var carried: setupExport ? setupExport.carried : []

    // The safe area's margin (5% of the screen each way, which a tube's
    // overscan leaves), the code's number at its top right, and the code's
    // room: square, centred, its sides clear of the number.
    readonly property int margin: Math.round(root.sh * 0.05)
    readonly property int marginX: Math.round(root.sw * 0.05)
    readonly property int numberSize: Math.round(root.sh * 0.0416667) //20
    readonly property int gap: Math.round(root.sh * 0.025) //12
    readonly property real room: Math.min(root.sh - 2 * margin, root.sw - 2 * (marginX + 3 * numberSize + gap))
    // A module's least size, in the screen's pixels: three lines on a tube,
    // whose lines blur into each other; two elsewhere.
    readonly property int leastModule: root.tube ? 3 : 2
    // The largest code that fits the room so: 17 + 4 × version modules, and
    // a quiet zone of two round them (SetupExport::codeImage()).
    readonly property int maxVersion: Math.max(1, Math.min(40, Math.floor((Math.floor(room / leastModule) - 4 - 17) / 4)))

    Component.onCompleted: {
        forceActiveFocus()
        if (setupExport)
            setupExport.make(maxVersion)
    }
    Component.onDestruction: {
        if (setupExport)
            setupExport.clear()
    }

    function show() {
        asking = false
        part = 0
    }

    Keys.onPressed: function(event) {
        // Ctrl+Q and the rest go on to Main.qml.
        if (event.modifiers & Qt.ControlModifier)
            return
        event.accepted = true
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            exportRoot.goBack()
        } else if (asking) {
            if (event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                choiceIndex = 1 - choiceIndex
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (busy)
                    return
                if (choiceIndex === 0)
                    show()
                else
                    exportRoot.goBack()
            }
        } else if (links.length > 1) {
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Up)
                part = (part + links.length - 1) % links.length
            else if (event.key === Qt.Key_Right || event.key === Qt.Key_Down)
                part = (part + 1) % links.length
        }
    }

    PromptScreen {
        visible: exportRoot.asking
        title: "Show the codes?"
        // What they carry that gets a device in, and what they can't.
        message: exportRoot.busy ? "Reading the settings…"
                 : "A phone opens OSD/OS Setup's page from them, filled in with this device's settings"
                   + (exportRoot.carried.length > 0
                      ? ". They carry " + exportRoot.carried.join(", ") + ": anyone who sees the screen can read them" : "")
                   + (exportRoot.missing.length > 0 ? ". Not in them: " + exportRoot.missing.join("; ") : "")
        choices: ["Show the Codes", "Cancel"]
        currentIndex: exportRoot.choiceIndex
        onHovered: function(index) { exportRoot.choiceIndex = index }
    }

    // The code: one pixel a module, drawn a whole number of the screen's
    // pixels each, sharp, its middle the screen's.
    Image {
        id: code
        visible: !exportRoot.asking && exportRoot.links.length > 0
        source: visible ? "image://qr/" + encodeURIComponent(exportRoot.links[Math.min(exportRoot.part, exportRoot.links.length - 1)]) : ""
        cache: false
        smooth: false
        readonly property int unit: implicitWidth > 0 ? Math.max(1, Math.floor(exportRoot.room / implicitWidth)) : 1
        width: implicitWidth * unit
        height: implicitHeight * unit
        anchors.centerIn: parent
    }

    // Where the code would be while there is none: the settings being read,
    // or why there is no code.
    Text {
        visible: !exportRoot.asking && exportRoot.links.length === 0
        anchors.centerIn: parent
        width: root.sw - 2 * exportRoot.marginX
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: exportRoot.busy ? "Reading the settings…" : exportRoot.missing.join(" • ")
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0375 //18
    }

    // Which code of how many, at the top right of the safe area.
    Text {
        visible: !exportRoot.asking && exportRoot.links.length > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: exportRoot.marginX
        anchors.topMargin: exportRoot.margin
        text: (exportRoot.part + 1) + "/" + exportRoot.links.length
        color: root.primaryColor
        font.family: root.globalFont
        font.pixelSize: exportRoot.numberSize
    }
}
