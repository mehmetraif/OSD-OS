import QtQuick
import Components

FocusScope {
    id: pinAuthRoot
    // In the layout's frame (Main.qml): what it says in the middle of the
    // pane, beside the column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})

    signal replaceCurrent(string path, var params)
    signal goBack()

    property string pinCode: ""
    property bool waiting: false
    property string errorMsg: ""
    // Remembered only so a PIN prompt can name the profile it is asking about.
    property var autoUser: null

    Connections {
        target: plexBackend

        function onPinReady(code, pinId) {
            pinAuthRoot.pinCode = code
            pinAuthRoot.waiting = true
            pinAuthRoot.errorMsg = ""
        }

        function onAuthSuccess() {
            // After PIN claimed: users + servers fetched, move to user selection
        }

        function onUsersLoaded(users) {
            if (users.length === 1) {
                // Only one user — auto-select and go straight to server select
                pinAuthRoot.autoUser = users[0]
                plexBackend.select_user(users[0].id)
            } else {
                pinAuthRoot.replaceCurrent("UserSelect.qml", { users: users })
            }
        }

        // The auto-selected profile is PIN-protected. Hand off rather than sitting
        // on "Waiting..." forever.
        function onUserPinRequired(userId, wrongPin) {
            pinAuthRoot.waiting = false
            pinAuthRoot.replaceCurrent("ProfilePin.qml", {
                userId: userId,
                title: pinAuthRoot.autoUser ? pinAuthRoot.autoUser.title : "",
                reauth: false,
                wrongPin: wrongPin
            })
        }

        function onServersLoaded(servers) {
            pinAuthRoot.replaceCurrent("ServerSelect.qml", { servers: servers })
        }

        function onErrorOccurred(msg) {
            pinAuthRoot.waiting = false
            pinAuthRoot.errorMsg = msg
        }
    }

    // This screen's own request, the one it gives up as it goes.
    property int pinRequest: 0

    Component.onCompleted: {
        pinRequest = plexBackend.start_pin_auth()
    }
    // Backed out: nothing to poll plex.tv for any more.
    Component.onDestruction: plexBackend.cancel_pin_auth(pinRequest)

    focus: true
    Keys.onPressed: function(event) {
        if (root.isBack(event)) {
            goBack()
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Sign in"
    }

    // Body
    Column {
        anchors.centerIn: parent
        // In the middle of the pane, in a layout's frame (Main.qml's).
        anchors.horizontalCenterOffset: root.framed || root.monitorFramed === true
                                        ? root.paneX + root.paneWidth / 2 - root.sw / 2 : 0
        anchors.verticalCenterOffset: root.monitorFramed === true
                                      ? (root.paneTop + root.paneBottom) / 2 - root.sh / 2 : 0
        spacing: root.sh * 0.05 //24

        Text {
            visible: pinCode !== ""
            text: pinCode
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.1166667 //56
        }

        Text {
            visible: pinCode !== ""
            text: "Visit plex.tv/link and enter the code above"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
        }

        // Waiting indicator with animated dots
        Row {
            visible: waiting && pinCode !== ""
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                text: "Waiting"
                color: root.tertiaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0333333 //16
            }

            Text {
                id: dots
                text: "..."
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333 //16

                SequentialAnimation on opacity {
                    running: waiting
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 600 }
                    NumberAnimation { to: 1.0; duration: 600 }
                }
            }
        }

        // Loading Indicator
        Text {
            visible: pinCode === "" && errorMsg === ""
            text: "Requesting Pin..."
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0416667 //20
        }

        // Error message, wrapped inside the content's width.
        Text {
            visible: errorMsg !== ""
            text: errorMsg
            color: root.accentColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            width: root.paneWidth //492
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0375 //20
        }
    }

    // Footer
    HintBar {
        id: footer
        text: root.hints.back + ":BACK"
    }
}
