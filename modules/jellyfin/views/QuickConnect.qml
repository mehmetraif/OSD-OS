import QtQuick
import Components

FocusScope {
    id: qcRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string serverUrl: navParams.serverUrl || ""
    property string code: navParams.code || ""
    property string secret: navParams.secret || ""
    property bool approved: false
    property bool failed: false
    property string errorMsg: ""
    property int pollCount: 0

    // When approved, authenticate: Root.qml routes to the libraries as the
    // auth state changes.
    Connections {
        target: jellyfinBackend

        function onQuickConnectCodeReady(c, s) {
            qcRoot.code = c
            qcRoot.secret = s
            pollTimer.restart()
        }

        function onQuickConnectApproved() {
            pollTimer.stop()
            qcRoot.approved = true
            // Exchange the quick connect secret for an access token
            jellyfinBackend.quick_connect_authenticate(qcRoot.secret)
        }

        function onQuickConnectFailed(msg) {
            pollTimer.stop()
            qcRoot.failed = true
            qcRoot.errorMsg = msg
        }
    }

    Component.onCompleted: {
        if (serverUrl === "") {
            errorMsg = "NO SERVER URL"
            failed = true
            return
        }
        // If code/secret already provided from Auth.qml, start polling
        if (qcRoot.code !== "" && qcRoot.secret !== "") {
            pollTimer.restart()
            return
        }
        // Otherwise initiate a new quick connect
        jellyfinBackend.quick_connect_initiate(serverUrl)
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace ||
            event.key === Qt.Key_Back) {
            jellyfinBackend.quick_connect_cancel()
            goBack()
            event.accepted = true
        }
    }

    // Poll every 2 seconds
    Timer {
        id: pollTimer
        interval: 2000
        repeat: true
        onTriggered: {
            if (qcRoot.secret !== "" && !approved && !failed) {
                pollCount++
                jellyfinBackend.quick_connect_poll(qcRoot.secret)
                // Give up after 60 polls (2 minutes)
                if (pollCount >= 60) {
                    stop()
                    failed = true
                    errorMsg = "CODE EXPIRED"
                }
            }
        }
    }

    // ---
    // UI
    // ---

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Quick Connect"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    // Body
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.05 //24

        Text {
            visible: !failed && code !== ""
            text: qcRoot.code
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.1166667 //56
            font.letterSpacing: root.sw * 0.025 // 16
        }

        Text {
            visible: !failed && code !== ""
            text: "Enter the above code at:\n" + serverUrl + "/web/#quickconnect"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
            lineHeight: 1.3
        }

        // Status
        Row {
            visible: !failed
            anchors.horizontalCenter: parent.horizontalCenter
            Text {
                text: approved ? "Approved"
                            : (code !== "" ? "Waiting"
                                            : "Connecting")
                color: approved ? root.accentColor : root.tertiaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0333333  // 16
            }
            Text {
                id: dots
                text: "..."
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333 //16
                SequentialAnimation on opacity {
                    running: !failed && !approved
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 600 }
                    NumberAnimation { to: 1.0; duration: 600 }
                }
            }
        }

        // Error state: a line of the column, across its middle.
        Text {
            visible: failed
            anchors.horizontalCenter: parent.horizontalCenter
            text: qcRoot.errorMsg
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.05 // 24
        }

    }

    // Footer
    HintBar {
        text: failed ? root.hints.back + ":RETRY" : root.hints.back + ":CANCEL"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667  // 50
        anchors.leftMargin: root.sw * 0.125  // 80
    }
}
