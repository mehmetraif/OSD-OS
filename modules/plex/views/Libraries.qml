import QtQuick
import Components

// Main Plex home screen: Continue Watching + library list
FocusScope {
    id: browseRoot
    // In the layout's frame (Main.qml): its list in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    // Used for the PIN handoff below: replacing rather than pushing keeps this
    // view from ending up on the stack twice when ProfilePin returns here.
    signal replaceCurrent(string path, var params)
    signal goBack()

    property var libraries: []
    property string serverName: ""
    property string userName: ""
    // Servers the active user can switch to from the main menu. More than one
    // means the quick-switch action (◄/►) is offered.
    property var switchableServers: []
    property bool canSwitchServer: switchableServers.length > 1
    property string errorMsg: ""
    // The server has answered: none listed is none, not LOADING.
    property bool loaded: false

    Connections {
        target: plexBackend

        function onLibrariesLoaded(items) {
            browseRoot.errorMsg = ""
            browseRoot.loaded = true
            browseRoot.libraries = items
            if (items.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                libraryList.currentIndex = Math.min(restore, items.length - 1)
                libraryList.positionViewAtIndex(libraryList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            console.log("[Library] Error: " + msg)
            browseRoot.errorMsg = msg
        }

        // The 401-recovery retry re-runs the user switch, which can turn out to
        // need the profile's PIN.
        function onUserPinRequired(userId, wrongPin) {
            browseRoot.replaceCurrent("ProfilePin.qml", {
                userId: userId,
                title: browseRoot.userName,
                reauth: true,
                wrongPin: wrongPin
            })
        }
    }

    Component.onCompleted: {
        browseRoot.serverName = plexBackend.get_active_server_name()
        browseRoot.userName = plexBackend.get_active_user_name()
        browseRoot.switchableServers = plexBackend.get_switchable_servers()
        plexBackend.load_libraries()
    }

    function openServerSwitch() {
        if (!canSwitchServer) return
        browseRoot.navigateTo("ServerSelect.qml",
                              { servers: switchableServers, switching: true },
                              { currentIndex: libraryList.currentIndex })
    }

    focus: true

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: browseRoot.serverName + (browseRoot.userName ? " (" + browseRoot.userName + ")" : "")
    }

    // Loading Indicator
    PaneMessage {
        visible: libraries.length === 0 && browseRoot.errorMsg === ""
        text: browseRoot.loaded ? "NO LIBRARIES" : "LOADING..."
    }

    // Error message
    Text {
        visible: browseRoot.errorMsg !== ""
        text: browseRoot.errorMsg
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        // In the middle of the pane.
        x: root.paneX
        anchors.verticalCenter: parent.verticalCenter
        width: root.paneWidth
        font.pixelSize: root.sh * 0.0375 //18
    }

    // Body
    ListView {
        id: libraryList
        model: libraries
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.listY //120
        anchors.leftMargin: root.paneX
        width: root.paneWidth
        height: root.tallListHeight //252
        clip: true
        focus: true

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) currentIndex--
            else currentIndex = count - 1
            libraryList.positionViewAtIndex(libraryList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            libraryList.positionViewAtIndex(libraryList.currentIndex, ListView.Contain)
        }
        Keys.onLeftPressed: browseRoot.openServerSwitch()
        Keys.onRightPressed: browseRoot.openServerSwitch()

        Keys.onReturnPressed: {
            var lib = libraries[currentIndex]
            if (!lib) return

            if (lib.key === "continue_watching") {
                browseRoot.navigateTo("Items.qml", {
                    listType: "continue_watching",
                    title: "CONTINUE WATCHING",
                    libraryName: lib.title
                }, { currentIndex: libraryList.currentIndex })
            } else if (lib.key === "live_tv") {
                browseRoot.navigateTo("LiveChannels.qml", {
                    libraryName: lib.title
                }, { currentIndex: libraryList.currentIndex })
            } else {
                browseRoot.navigateTo("Library.qml", {
                    libraryName: lib.title,
                    sectionId: lib.sectionId,
                    sectionType: lib.sectionType
                }, { currentIndex: libraryList.currentIndex })
            }
        }

        Keys.onPressed: function(event) {
            if (root.isBack(event)) {
                browseRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: libraryList.width
            height: root.menuRowHeight

            // The row as a monitor layout draws one, in its frame (Main.qml's):
            // the layout's cursor, its case, its colours.
            Loader {
                anchors.fill: parent
                active: root.monitorInk === true
                sourceComponent: MonitorRow {
                    label: textClip.text
                    selected: libraryList.currentIndex === index
                }
            }

            ScrolledText {
                id: textClip
                visible: root.monitorInk !== true
                width: Math.min(implicitWidth, libraryList.width)
                height: parent.height
                on: libraryList.currentIndex === index
                elides: false
                text: modelData.title || ""
                color: libraryList.currentIndex === index ? root.cursorInk : root.primaryColor
                font.pixelSize: root.sh * 0.05 //24
                textItem.topPadding: root.sh * 0.0041667 //2
                textItem.leftPadding: root.sw * 0.009375 //6
                textItem.rightPadding: root.sw * 0.009375 //6
                textItem.bottomPadding: root.sh * 0.00625 //3

                Rectangle {
                    z: -1
                    color: root.cursorColor
                    anchors.fill: textClip.textItem
                    visible: libraryList.currentIndex === index
                }
            }
        }
    }

    // Flat Panel's pages for the list (Settings → Look → Menu Layout): the
    // frame says which.
    ListPager { list: libraryList }

    // Footer
    HintBar {
        id: footer
        text: root.hintLine(["back:BACK", "navigate:NAVIGATE", "select:SELECT",
                             browseRoot.canSwitchServer ? "change:SERVER" : ""])
    }
}
