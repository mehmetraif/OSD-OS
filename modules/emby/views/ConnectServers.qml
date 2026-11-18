import QtQuick
import Components

// Emby Connect server picker: shown when an account is linked to more than one
// server. Selecting a server exchanges its access key for a local token
// (embyBackend.connect_select_server); Root.qml routes to Libraries on success.
FocusScope {
    id: pickerRoot
    // In the layout's frame (Main.qml): its list in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true
    focus: true

    property var navParams: ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var servers: navParams.servers || []
    property bool waiting: false
    property string errorMsg: ""

    Connections {
        target: embyBackend

        // A failed exchange returns here so the user can pick another server.
        function onConnectFailed(msg) {
            pickerRoot.waiting = false
            pickerRoot.errorMsg = msg
        }
    }

    function selectCurrent() {
        if (waiting) return
        var s = servers[serverList.currentIndex]
        if (!s) return
        waiting = true
        errorMsg = ""
        embyBackend.connect_select_server(s.address, s.accessKey)
    }

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Choose a Server"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Connecting indicator
    Text {
        visible: pickerRoot.waiting
        text: "CONNECTING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body
    ListView {
        id: serverList
        model: servers
        visible: !pickerRoot.waiting
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.listY //120
        anchors.leftMargin: root.paneX //74
        width: root.paneWidth //492
        height: root.tallListHeight //252
        clip: true
        focus: true

        Keys.onUpPressed: if (currentIndex > 0) currentIndex--
        Keys.onDownPressed: if (currentIndex < count - 1) currentIndex++
        Keys.onReturnPressed: pickerRoot.selectCurrent()
        Keys.onEnterPressed: pickerRoot.selectCurrent()

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                pickerRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: serverList.width
            height: root.sh * 0.0583333 //28

            // The row as a monitor layout draws one, in its frame (Main.qml's):
            // the layout's cursor, its case, its colours.
            Loader {
                anchors.fill: parent
                active: root.monitorInk === true
                sourceComponent: MonitorRow {
                    label: textClip.text
                    selected: serverList.currentIndex === index
                }
            }

            ScrolledText {
                id: textClip
                visible: root.monitorInk !== true
                width: Math.min(implicitWidth, serverList.width)
                height: parent.height
                on: serverList.currentIndex === index
                elides: false
                text: modelData.name || modelData.address || ""
                color: serverList.currentIndex === index ? root.surfaceColor : root.primaryColor
                font.pixelSize: root.sh * 0.05 //24
                textItem.topPadding: root.sh * 0.0041667 //2
                textItem.leftPadding: root.sw * 0.009375 //6
                textItem.rightPadding: root.sw * 0.009375 //6
                textItem.bottomPadding: root.sh * 0.00625 //3

                Rectangle {
                    z: -1
                    color: root.accentColor
                    anchors.fill: textClip.textItem
                    visible: serverList.currentIndex === index
                }
            }
        }
    }

    // Error message
    Text {
        visible: errorMsg !== "" && !pickerRoot.waiting
        text: errorMsg
        color: root.accentColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        horizontalAlignment: Text.AlignHCenter
        anchors.horizontalCenter: parent.horizontalCenter
        // Over the hint bar, or in its place with it off, clear of the list
        // that grows into its room.
        anchors.bottom: root.monitorFramed === true ? parent.bottom : root.hintBar ? footer.top : footer.bottom
        anchors.bottomMargin: root.monitorFramed === true ? parent.height - root.paneBottom
                            : root.hintBar ? root.sh * 0.0333333 : 0 //16
        width: root.sw * 0.5
        wrapMode: Text.WordWrap
        font.pixelSize: root.sh * 0.0333333 //16
    }

    // Flat Panel's pages for the list (Settings → Look → Menu Layout): the
    // frame says which.
    ListPager { list: serverList }

    // Footer
    HintBar {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
