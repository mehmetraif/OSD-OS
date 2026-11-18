import QtQuick
import Components

FocusScope {
    id: serverSelectRoot
    // In the layout's frame (Main.qml): its list in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})

    signal navigateTo(string path, var params)
    signal goBack()

    property var servers: navParams.servers || []
    // When opened from the main menu (Libraries) as a quick switch rather than as
    // part of the initial auth flow, return to the menu on success instead of
    // pushing a fresh Libraries onto the stack.
    property bool switching: navParams.switching === true

    Connections {
        target: plexBackend

        function onServersLoaded(loadedServers) {
            serverSelectRoot.servers = loadedServers
        }

        function onAuthSuccess() {
            if (serverSelectRoot.switching) {
                serverSelectRoot.goBack()
            } else {
                serverSelectRoot.navigateTo("Libraries.qml", {})
            }
        }

        function onErrorOccurred(msg) {
            console.log("[ServerSelect] Error: " + msg)
        }
    }

    Component.onCompleted: {
        if (servers.length === 0) return
        // Pre-highlight the currently active server when switching.
        var activeName = plexBackend.get_active_server_name()
        serverList.currentIndex = 0
        for (var i = 0; i < servers.length; i++) {
            if (servers[i].name === activeName) { serverList.currentIndex = i; break }
        }
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
        subtitle: "Select Server"
    }

    // Body
    ListView {
        id: serverList
        model: servers
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
            serverList.positionViewAtIndex(serverList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            serverList.positionViewAtIndex(serverList.currentIndex, ListView.Contain)
        }

        Keys.onReturnPressed: {
            var server = servers[currentIndex]
            if (server) plexBackend.select_server(server.machineId)
        }

        Keys.onPressed: function(event) {
            if (root.isBack(event)) {
                serverSelectRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: serverList.width
            height: root.menuRowHeight

            // The row as a monitor layout draws one, in its frame (Main.qml's):
            // the layout's cursor, its case, its colours.
            Loader {
                anchors.fill: parent
                active: root.monitorInk === true
                sourceComponent: MonitorRow {
                    label: rowText.text
                    selected: serverList.currentIndex === index
                }
            }

            Item {
                id: textClip
                visible: root.monitorInk !== true
                width: Math.min(rowText.implicitWidth, serverList.width)
                height: parent.height
                clip: true

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: serverList.currentIndex === index
                }

                Text {
                    id: rowText
                    text: modelData.name || ""
                    color: serverList.currentIndex === index ? root.surfaceColor : root.primaryColor
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

    // Flat Panel's pages for the list (Settings → Look → Menu Layout): the
    // frame says which.
    ListPager { list: serverList }

    // Footer
    HintBar {
        id: footer
        text: root.hintLine(["back:BACK", "navigate:NAVIGATE", "select:SELECT"])
    }
}
