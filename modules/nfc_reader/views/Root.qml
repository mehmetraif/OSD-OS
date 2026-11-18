import QtQuick
import Components

// The NFC Reader's router. A card for another module's content routes out
// of this module entirely, to that module's entry point: Items.qml emits the
// router's own navigateTo, which the app shell handles (Main.qml), not the
// internal Loader; the module's own views are navigateToView()'s.
ModuleRouter {
    id: moduleRoot
    moduleId: "com.osdos.nfc_reader"
    viewsUrl: Qt.resolvedUrl(".")

    // With no reader driver at all no child view ever loads, so the back keys
    // for the unavailable screen must be handled here.
    Keys.onPressed: function(event) {
        if (!nfcReaderBackend.available &&
            (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back)) {
            moduleRoot.goBack()
            event.accepted = true
        }
    }

    // Shown only on a platform with no reader driver at all. The PN532 USB
    // driver links nothing, so on Linux and macOS this never appears — a
    // build without PC/SC still supports readers, and says so via
    // pcscAvailable in Items.qml.
    Column {
        visible: !nfcReaderBackend.available
        anchors.centerIn: parent
        spacing: root.sh * 0.02

        Text {
            text: "NFC Reader"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.05
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: "Not supported on this platform"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.033
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: "NFC reader support requires Linux or macOS.\nSee the wiki for details."
            color: root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.025
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            lineHeight: 1.4
        }
    }

    HintBar {
        visible: !nfcReaderBackend.available
        text: root.hints.back + ":BACK"
    }

    Component.onCompleted: {
        if (nfcReaderBackend.available) {
            nfcReaderBackend.setModuleActive(true)
            nfcReaderBackend.reloadMapping()
            navigateToView("Items.qml", {})
        }
    }

    // Card taps must do nothing once the user leaves the module.
    Component.onDestruction: nfcReaderBackend.setModuleActive(false)
}
