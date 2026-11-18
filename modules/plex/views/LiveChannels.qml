import QtQuick
import Components

// Live TV channel list. Lists the tunable channels of the server's DVR; selecting
// one hands off to LivePlayer.qml, which tunes it and can switch channels in place.
FocusScope {
    id: channelsRoot
    // In the layout's frame (Main.qml): its list in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string libraryName: navParams.libraryName || "LIVE TV"
    property var channels: []
    property bool loaded: false
    property string errorMessage: ""

    Connections {
        target: plexBackend

        function onLiveChannelsLoaded(items) {
            channelsRoot.channels = items
            channelsRoot.loaded = true
            if (items.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                channelList.currentIndex = Math.min(restore, items.length - 1)
                channelList.positionViewAtIndex(channelList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            console.log("[LiveChannels] Error: " + msg)
            channelsRoot.loaded = true
            channelsRoot.errorMessage = msg
        }
    }

    Component.onCompleted: plexBackend.load_live_channels()

    focus: true

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: channelsRoot.libraryName
    }

    // Loading / empty / error indicator
    Text {
        visible: channels.length === 0
        text: channelsRoot.errorMessage !== "" ? channelsRoot.errorMessage
              : (channelsRoot.loaded ? "NO CHANNELS" : "LOADING...")
        color: root.tertiaryColor
        font.family: root.globalFont
        // In the middle of the pane.
        x: root.paneX
        anchors.verticalCenter: parent.verticalCenter
        width: root.paneWidth
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body
    ListView {
        id: channelList
        model: channels
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
            channelList.positionViewAtIndex(channelList.currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) currentIndex++
            else currentIndex = 0
            channelList.positionViewAtIndex(channelList.currentIndex, ListView.Contain)
        }

        Keys.onReturnPressed: {
            if (channels.length === 0) return
            channelsRoot.navigateTo("LivePlayer.qml", {
                channel: channels[currentIndex]
            }, { currentIndex: channelList.currentIndex })
        }

        Keys.onPressed: function(event) {
            if (root.isBack(event)) {
                channelsRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: channelList.width
            height: root.menuRowHeight

            // The row as a monitor layout draws one, in its frame (Main.qml's):
            // the layout's cursor, its case, its colours.
            Loader {
                anchors.fill: parent
                active: root.monitorInk === true
                sourceComponent: MonitorRow {
                    label: textClip.text
                    selected: channelList.currentIndex === index
                }
            }

            ScrolledText {
                id: textClip
                visible: root.monitorInk !== true
                width: Math.min(implicitWidth, channelList.width)
                height: parent.height
                on: channelList.currentIndex === index
                elides: false
                text: (modelData.number ? modelData.number + "  " : "") + (modelData.title || "")
                color: channelList.currentIndex === index ? root.surfaceColor : root.primaryColor
                font.pixelSize: root.sh * 0.05 //24
                textItem.topPadding: root.sh * 0.0041667 //2
                textItem.leftPadding: root.sw * 0.009375 //6
                textItem.rightPadding: root.sw * 0.009375 //6
                textItem.bottomPadding: root.sh * 0.00625 //3

                Rectangle {
                    z: -1
                    color: root.accentColor
                    anchors.fill: textClip.textItem
                    visible: channelList.currentIndex === index
                }
            }
        }
    }

    // Flat Panel's pages for the list (Settings → Look → Menu Layout): the
    // frame says which.
    ListPager { list: channelList }

    // Footer
    HintBar {
        id: footer
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":WATCH"
    }
}
