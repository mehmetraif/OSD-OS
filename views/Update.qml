import QtQuick
import Components

// Software Update page — checks GitHub Releases via the UpdateManager backend
// (context property "updateManager"), downloads the platform asset, and applies
// it. All state lives in C++; this view renders updateManager.state:
//   idle | checking | upToDate | updateAvailable | downloading | readyToApply | error
FocusScope {
    id: updateRoot

    signal goBack()

    property var navParams: ({})

    property bool autostartSession: false

    // Null guards: context properties resolve to null while the view Loader
    // tears down; guarded bindings stay teardown-safe (see Main.qml mirrors).
    readonly property string updState: updateManager ? updateManager.state : "idle"
    readonly property string latestVersion: updateManager ? updateManager.latestVersion : ""
    readonly property string releaseNotes: updateManager ? updateManager.releaseNotes : ""
    readonly property string errorMessage: updateManager ? updateManager.errorMessage : ""
    readonly property string applyHint: updateManager ? updateManager.applyHint : ""
    readonly property bool canApply: updateManager ? updateManager.canApply : false

    // Dev builds can check (to read the notes) but not download; on the Pi a
    // download is pointless unless the launcher can actually apply it. macOS
    // always allows download — the no-canApply fallback opens the DMG manually.
    readonly property bool canDownload: !root.devBuild && (Qt.platform.os === "osx" || canApply)

    readonly property string actionLabel: {
        switch (updState) {
        case "idle": case "upToDate": return "CHECK"
        case "error":                 return "RETRY"
        case "updateAvailable":       return canDownload ? "DOWNLOAD" : ""
        case "readyToApply":          return "INSTALL"
        default:                      return ""
        }
    }

    readonly property string statusText: {
        switch (updState) {
        case "checking":        return "Checking for updates..."
        case "upToDate":        return "You're up to date"
        case "updateAvailable": return "Update available"
        case "downloading":     return "Downloading... " + Math.round((updateManager ? updateManager.downloadProgress : 0) * 100) + "%"
        case "readyToApply":    return latestVersion + " ready to install"
        case "error":           return errorMessage
        default:                return "Press " + root.hints.select + " to check for latest"
        }
    }

    // Contextual hint under the status line, when there is something to explain
    readonly property string hintText: {
        if (root.devBuild && (updState === "updateAvailable" || updState === "readyToApply"))
            return "Dev build — self-update is disabled."
        if (!canApply && applyHint && (updState === "updateAvailable" || updState === "readyToApply"))
            return applyHint
        return ""
    }

    readonly property string applyActionLabel: {
        if (Qt.platform.os === "osx")
            return canApply ? "Apply & Relaunch" : "Quit & Open Disk Image"
        return updateRoot.autostartSession ? "Apply & Restart" : "Quit & Apply on Next Launch"
    }
    // No "Cancel": Back/Escape dismisses the overlay instead. A downloaded
    // update stays inert until Apply is chosen (that's when the launcher-facing
    // marker is written), so backing out truly means "not now".
    readonly property var confirmOptions: [
        { label: updateRoot.applyActionLabel, action: "apply" },
        { label: "Discard Update",            action: "discard" }
    ]

    function primaryAction() {
        if (!updateManager) return
        switch (updState) {
        case "idle": case "upToDate": case "error":
            updateManager.checkForUpdates()
            break
        case "updateAvailable":
            if (canDownload) updateManager.download()
            break
        case "readyToApply":
            confirmPrompt.open()
            break
        }
    }

    Component.onCompleted: autostartSession = appCore.isAutostartSession()

    // Header
    AppBar {
        iconSource: "../../assets/images/update.svg"
        title: "Update"
        //subtitle: root.appVersion
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    Item {
        id: content
        focus: true
        anchors.fill: parent

        Keys.onReturnPressed: updateRoot.primaryAction()
        Keys.onUpPressed:   notesFlick.scrollBy(-1)
        Keys.onDownPressed: notesFlick.scrollBy(1)
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                if (updateRoot.updState === "downloading" && updateManager)
                    updateManager.cancelDownload()
                updateRoot.goBack()
                event.accepted = true
            }
        }

        Column {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: root.sh * 0.25 //120
            anchors.leftMargin: root.sw * 0.125 //80
            width: root.sw * 0.76875 //492
            spacing: root.sh * 0.0291667 //14

            // Version summary
            Row {
                spacing: root.sw * 0.025 //16
                Text {
                    text: "INSTALLED: " + root.appVersion
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0375 //18
                }
                Text {
                    visible: updateRoot.latestVersion !== ""
                    text: "LATEST: " + updateRoot.latestVersion
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.0375 //18
                }
            }

            // Status line
            Text {
                width: parent.width
                text: updateRoot.statusText
                color: root.accentColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                wrapMode: Text.WordWrap
                font.pixelSize: root.sh * 0.05 //24
            }

            // Contextual hint (dev build / old launcher / non-standard install)
            Text {
                visible: updateRoot.hintText !== ""
                width: parent.width
                text: updateRoot.hintText
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                font.pixelSize: root.sh * 0.0291667 //14
            }

            // Download progress bar
            Rectangle {
                visible: updateRoot.updState === "downloading"
                width: parent.width
                height: root.sh * 0.0291667 //14
                color: "transparent"
                border.color: root.tertiaryColor
                border.width: root.px
                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.margins: 2 * root.px
                    width: (parent.width - 4 * root.px) * (updateManager ? updateManager.downloadProgress : 0)
                    color: root.accentColor
                }
            }

            // Release notes
            Rectangle {
                visible: updateRoot.releaseNotes !== ""
                width: parent.width
                height: root.sh * 0.3291667 + root.hintRoom //158
                color: "transparent"
                border.width: root.px
                border.color: root.primaryColor

                Flickable {
                    id: notesFlick
                    anchors.fill: parent
                    anchors.margins: root.sw * 0.009375 //6
                    contentHeight: notesText.height
                    clip: true

                    function scrollBy(direction) {
                        var step = height * 0.8
                        var maxY = Math.max(0, contentHeight - height)
                        contentY = Math.max(0, Math.min(maxY, contentY + direction * step))
                    }
                    Text {
                        id: notesText
                        width: notesFlick.width
                        text: updateRoot.releaseNotes
                        textFormat: Text.PlainText
                        color: root.primaryColor
                        font.family: root.globalFont
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        font.pixelSize: root.sh * 0.0291667 //14
                    }
                }
            }
        }

        // Footer
        HintBar {
            text: root.hints.back + ":BACK "
                  + (updateRoot.releaseNotes !== "" ? root.hints.navigate + ":SCROLL " : "")
                  + (updateRoot.actionLabel !== "" ? root.hints.select + ":" + updateRoot.actionLabel : "")
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.bottomMargin: root.sh * 0.1041667 //50
            anchors.leftMargin: root.sw * 0.125 //80
        }
    }

    // --- INSTALL CONFIRMATION OVERLAY --- (as the Settings quit overlay)
    ChoiceOverlay {
        id: confirmPrompt
        anchors.fill: parent
        promptText: "Install update?"
        subtitleText: updateRoot.latestVersion
        choices: confirmOptions
        onActivated: function(act) {
            if (act === "apply" && updateManager) {
                // Linux exits with code 11 under autostart (see osdos-stop in
                // scripts/install.sh) or quits for apply-on-next-launch; macOS
                // spawns the swap helper and quits.
                updateManager.applyAndRestart()
            } else if (act === "discard" && updateManager) {
                updateManager.discardStagedUpdate()
            }
        }
        onClosed: content.forceActiveFocus()
    }
}
