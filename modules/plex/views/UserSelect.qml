import QtQuick
import Components

FocusScope {
    id: userSelectRoot
    // In the layout's frame (Main.qml): its list in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})

    signal navigateTo(string path, var params)
    signal goBack()

    property var users: navParams.users || []
    property string errorMsg: ""
    // The users asked for and not come yet: the backend has them kept, or
    // asks plex.tv for the account a sign-in was left without.
    property bool loading: false

    function titleFor(userId) {
        for (var i = 0; i < users.length; i++)
            if (users[i].id === userId) return users[i].title || ""
        return ""
    }

    Connections {
        target: plexBackend

        function onUsersLoaded(loadedUsers) {
            userSelectRoot.loading = false
            userSelectRoot.users = loadedUsers
        }

        // plex.tv wants the profile's PIN before it will switch.
        function onUserPinRequired(userId, wrongPin) {
            userSelectRoot.navigateTo("ProfilePin.qml", {
                userId: userId,
                title: userSelectRoot.titleFor(userId),
                reauth: userSelectRoot.navParams.reauth === true,
                wrongPin: wrongPin
            })
        }

        function onServersLoaded(servers) {
            if (!navParams.reauth) {
                userSelectRoot.navigateTo("ServerSelect.qml", { servers: servers })
            }
        }

        function onAuthSuccess() {
            if (navParams.reauth) {
                userSelectRoot.navigateTo("Libraries.qml", {})
            }
        }

        function onErrorOccurred(msg) {
            console.log("[UserSelect] Error: " + msg)
            userSelectRoot.loading = false
            userSelectRoot.errorMsg = msg
        }
    }

    Component.onCompleted: {
        // If users weren't passed via navParams, load from cache: answered
        // at once with the users kept, later when plex.tv has to be asked.
        if (!navParams.users || navParams.users.length === 0) {
            loading = true
            plexBackend.load_users_from_cache()
        }
        if (users.length > 0) userList.currentIndex = 0
    }

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
        subtitle: "Select User"
    }

    // Loading Indicator
    PaneMessage {
        visible: userSelectRoot.loading && users.length === 0
        text: "LOADING..."
    }

    // Body
    ListView {
        id: userList
        model: users
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.listY //120
        anchors.leftMargin: root.paneX //74
        width: root.paneWidth //492
        height: root.tallListHeight //252
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
            userList.positionViewAtIndex(userList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            userList.positionViewAtIndex(userList.currentIndex, ListView.Contain)
        }

        Keys.onReturnPressed: {
            var user = users[currentIndex]
            if (user) {
                if (navParams.reauth) {
                    plexBackend.reauth_select_user(user.id)
                } else {
                    plexBackend.select_user(user.id)
                }
            }
        }

        Keys.onPressed: function(event) {
            if (root.isBack(event)) {
                userSelectRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: userList.width
            height: root.menuRowHeight

            // The row as a monitor layout draws one, in its frame (Main.qml's):
            // the layout's cursor, its case, its colours.
            Loader {
                anchors.fill: parent
                active: root.monitorInk === true
                sourceComponent: MonitorRow {
                    label: rowText.text
                    selected: userList.currentIndex === index
                }
            }

            Item {
                id: textClip
                visible: root.monitorInk !== true
                width: Math.min(rowText.implicitWidth, userList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.cursorColor
                    anchors.fill: rowText
                    visible: userList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData.title || ""
                    color: userList.currentIndex === index ? root.cursorInk : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667 //2
                    leftPadding: root.sw * 0.009375 //6
                    rightPadding: root.sw * 0.009375 //6
                    bottomPadding: root.sh * 0.00625 //3
                    font.pixelSize: root.sh * 0.05 //24
                }
            }
        }
    }

    // Error message — without this a failed switch is invisible in the very view
    // the selection was made from.
    Text {
        visible: userSelectRoot.errorMsg !== ""
        text: userSelectRoot.errorMsg
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        width: root.sw * 0.6
        anchors.horizontalCenter: parent.horizontalCenter
        // Over the hint bar, or in its place with it off, clear of the list
        // that grows into its room.
        anchors.bottom: root.monitorFramed === true ? parent.bottom : root.hintBar ? footer.top : footer.bottom
        anchors.bottomMargin: root.monitorFramed === true ? parent.height - root.paneBottom
                            : root.hintBar ? root.sh * 0.05 : 0
        font.pixelSize: root.sh * 0.0333333 //16
    }

    // Flat Panel's pages for the list (Settings → Look → Menu Layout): the
    // frame says which.
    ListPager { list: userList }

    // Footer
    HintBar {
        id: footer
        text: root.hintLine(["back:BACK", "navigate:NAVIGATE", "select:SELECT"])
    }
}
