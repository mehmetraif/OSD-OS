import QtQuick
import Components

FocusScope {
    focus: true
    id: authRoot

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    // The address typed before a Quick Connect that failed (back from it
    // returns here with it), else the one saved.
    property string serverUrl: (navParams.navListState && navParams.navListState.serverUrl)
                               || appCore.get_setting(moduleRoot.moduleId, "server_url") || ""
    property bool waiting: false
    property string errorMsg: ""

    // Focus index: 0=server URL, 1=Quick Connect
    property int focusIndex: 0

    Connections {
        target: jellyfinBackend

        function onAuthStateChanged() {
            // Root.qml owns post-auth routing and server_url persistence (the
            // single funnel for both the password and Quick Connect sign-ins);
            // here we just drop the connecting spinner.
            if (jellyfinBackend.get_auth_state() === "authed")
                authRoot.waiting = false
        }

        function onQuickConnectCodeReady(code, secret) {
            authRoot.waiting = false
            authRoot.navigateTo("QuickConnect.qml", {
                serverUrl: authRoot.serverUrl,
                code: code,
                secret: secret
            }, { serverUrl: authRoot.serverUrl })
        }

        function onQuickConnectFailed(msg) {
            authRoot.waiting = false
            authRoot.errorMsg = msg
        }

        function onErrorOccurred(msg) {
            authRoot.waiting = false
            authRoot.errorMsg = msg
        }
    }

    Component.onCompleted: {
        focusIndex = 0
    }

    // Navigation keys — don't intercept modifier keys so Shift/CapsLock work
    Keys.onPressed: function(event) {
        // Never intercept bare modifier keys
        if (event.key === Qt.Key_Shift || event.key === Qt.Key_Control ||
            event.key === Qt.Key_Alt   || event.key === Qt.Key_Meta ||
            event.key === Qt.Key_AltGr) {
            return
        }
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
            return
        }
        if (waiting) {
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Up) {
            if (focusIndex > 0) focusIndex--
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            if (focusIndex < 1) focusIndex++
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            // On the address, the on-screen keyboard, for a remote or a pad
            // (a keyboard types into the field as well).
            if (focusIndex === 0)
                authRoot.typeServerUrl()
            else
                authRoot.submit()
            event.accepted = true
        }
    }

    // The address on the on-screen keyboard: what is there, else http:// to
    // go on from. OK goes on to Quick Connect.
    function typeServerUrl() {
        keyboard.open(serverUrl !== "" ? serverUrl : "http://")
    }
    // The keys back to the field or the button, as the cursor is.
    function refocus() {
        if (focusIndex === 0)
            serverInput.forceActiveFocus()
        else
            authRoot.forceActiveFocus()
    }

    function submit() {
        if (waiting) return
        if (serverUrl === "") {
            errorMsg = "Please enter a server URL"
            return
        }
        waiting = true
        errorMsg = ""
        jellyfinBackend.quick_connect_initiate(authRoot.serverUrl)
    }

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Connect to Server"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Body
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333 //16

        // Server URL field
        Column {
            spacing: root.sh * 0.0166667 //8
            width: root.sw * 0.5 //320
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                text: "Server URL"
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0291667 //14
            }

            Rectangle {
                width: parent.width
                height: root.sh * 0.075 //36
                color: root.surfaceColor
                border.color: focusIndex === 0 ? root.accentColor : root.tertiaryColor
                border.width: root.sh * 0.003125 //2

                TextInput {
                    id: serverInput
                    anchors.fill: parent
                    anchors.margins: root.sh * 0.0166667 //8
                    text: authRoot.serverUrl
                    color: root.primaryColor
                    font.family: root.globalFont
                    // As typed: an address's path keeps its case.
                    font.pixelSize: root.sh * 0.0375 //18
                    clip: true
                    focus: authRoot.focusIndex === 0

                    onTextChanged: { authRoot.serverUrl = text }

                    Keys.onPressed: function(event) {
                        event.accepted = false
                    }
                }
            }
        }

        // Quick Connect button
        Rectangle {
            width: root.sw * 0.234375 //150
            height: root.sh * 0.0583333 //28
            color: focusIndex === 1 ? root.accentColor : root.surfaceColor
            border.color: focusIndex === 1 ? root.accentColor : root.tertiaryColor
            border.width: root.sh * 0.003125 //2
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                anchors.centerIn: parent
                text: "Quick Connect"
                color: focusIndex === 1 ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0375 //18
            }
        }

        // Loading indicator
        Text {
            visible: waiting
            text: "CONNECTING..."
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
        }

        // Error message: within its width whatever is in it (a server's
        // address is one long name of dots, with no place for a line to
        // break, so it breaks where the line ends), and cut after six lines.
        Text {
            visible: errorMsg !== ""
            text: errorMsg
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.sw * 0.5
            wrapMode: Text.Wrap
            maximumLineCount: 6
            elide: Text.ElideRight
            font.pixelSize: root.sh * 0.0333333 //16
        }
    }

    // Footer
    HintBar {
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select
              + (focusIndex === 0 ? ":TYPE" : ":CONNECT")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    OnScreenKeyboard {
        id: keyboard
        anchors.fill: parent
        title: "Server URL"
        caseSensitive: true
        maxLength: 200
        onAccepted: function(text) {
            authRoot.serverUrl = text.trim()
            authRoot.errorMsg = ""
            authRoot.focusIndex = 1
            authRoot.refocus()
        }
        onCanceled: authRoot.refocus()
    }
}
