import QtQuick

// Season landing — season context + episode list. Reached from ItemShow;
// selecting an episode opens Item.qml. Jellyfin's and Emby's: a module's
// ItemSeason.qml is this with its backend (see MediaServerLibraries).
FocusScope {
    id: seasonRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    // The module's backend (an EmbyApiBackend: jellyfinBackend, embyBackend),
    // which its view file names.
    property var backend: null

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var item: navParams.item || {}                 // the season
    property string seriesId: navParams.seriesId || item.seriesId || ""
    property string showTitle: navParams.showTitle || item.grandparentTitle || ""
    property string libraryName: navParams.libraryName || ""

    property var episodes: []
    property bool isLoading: false

    // Focus rows: 0 = play button, 1 = episode list
    property int focusRow: 0

    Connections {
        target: backend

        function onEpisodesLoaded(loadedItems) {
            seasonRoot.isLoading = false
            seasonRoot.episodes = loadedItems
            if (loadedItems.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                episodeList.currentIndex = Math.min(restore, loadedItems.length - 1)
                if (navListState.focusRow === 1) focusRow = 1
                episodeList.positionViewAtIndex(episodeList.currentIndex, ListView.Contain)
            }
        }

        function onErrorOccurred(msg) {
            seasonRoot.isLoading = false
            console.log("[" + moduleRoot.moduleName + " Season] Error: " + msg)
        }
    }

    Component.onCompleted: {
        isLoading = true
        focusRow = 0
        if (seriesId && item.itemId) backend.load_episodes(seriesId, item.itemId)
    }

    focus: true

    // Resume in-progress → first unwatched → first.
    function playBestEpisode() {
        if (episodes.length === 0) return
        var target = null
        for (var i = 0; i < episodes.length; i++) {
            if (episodes[i].viewOffset > 0) { target = episodes[i]; break }
        }
        if (!target) {
            for (var j = 0; j < episodes.length; j++) {
                if (!episodes[j].played) { target = episodes[j]; break }
            }
        }
        if (!target) target = episodes[0]
        seasonRoot.navigateTo("Item.qml", { item: target, libraryName: libraryName }, {})
    }

    // The episode under the mouse becomes the cursor's (Main.qml's
    // followPointer()).
    function hoverRow(index) {
        episodeList.currentIndex = index
        focusRow = 1
    }

    Keys.onUpPressed: {
        if (focusRow === 0) {
            if (episodes.length === 0) return   // nothing to move focus into
            episodeList.currentIndex = episodes.length - 1
            focusRow = 1
        } else {
            if (episodes.length === 0) return
            if (focusRow === 1) {
                if (episodeList.currentIndex > 0) episodeList.currentIndex--
                else focusRow = 0
            }
        }
        episodeList.positionViewAtIndex(episodeList.currentIndex, ListView.Contain)
    }
    Keys.onDownPressed: {
        if (focusRow === 0) {
            episodeList.currentIndex = 0
            if (episodes.length > 0) focusRow = 1
        } else {
            if (episodes.length === 0) return
            if (episodeList.currentIndex < episodes.length - 1) episodeList.currentIndex++
            else episodeList.currentIndex = focusRow = 0
        }
        episodeList.positionViewAtIndex(episodeList.currentIndex, ListView.Contain)
    }
    Keys.onReturnPressed: {
        if (focusRow === 0) {
            playBestEpisode()
        } else {
            var ep = episodes[episodeList.currentIndex]
            if (!ep) return
            seasonRoot.navigateTo("Item.qml", {
                item: ep,
                libraryName: libraryName
            }, { currentIndex: episodeList.currentIndex, focusRow: 1 })
        }
    }
    Keys.onPressed: function(event) {
        if (root.isBack(event)) {
            goBack()
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: libraryName
    }

    Text {
        visible: isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05 //24
    }

    Item {
        visible: !isLoading
        anchors.top: parent.top
        anchors.left: parent.left
        // The pane, where every view has its list (the page is in no frame).
        anchors.topMargin: root.listY
        anchors.leftMargin: root.paneX
        width: root.paneWidth
        height: root.tallListHeight
        clip: true

        Row {
            id: seasonDetails
            height: root.sh * 0.175 //84
            spacing: root.sw * 0.0375 //24

            // PLAY / RSUM button
            Rectangle {
                id: playButton
                color: focusRow === 0 ? root.accentColor : root.surfaceColor
                border.color: focusRow === 0 ? root.accentColor : root.tertiaryColor
                width: root.sw * 0.1875 //120
                height: root.sh * 0.1166667 //56
                border.width: root.sh * 0.003125 //2

                Text {
                    anchors.centerIn: parent
                    text: {
                        for (var i = 0; i < seasonRoot.episodes.length; i++) {
                            if (seasonRoot.episodes[i].viewOffset > 0) return "RSUM ►"
                        }
                        return "PLAY ►"
                    }
                    color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.05 //24
                }
            }

            Column {
                topPadding: root.sh * 0.0083333 //4
                width: root.sw * 0.54375 //348
                spacing: root.sh * 0.0166667 //8

                Text {
                    text: showTitle
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    width: parent.width
                    font.pixelSize: root.sh * 0.05 //24
                }

                Text {
                    text: {
                        var parts = []
                        if (item.index === 0) parts.push("Specials")
                        else if (item.index) parts.push("SEASON " + item.index)
                        if (item.year) parts.push(String(item.year))
                        return parts.join(" - ")
                    }
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    width: parent.width
                    font.pixelSize: root.sh * 0.0333333 //16
                }
            }
        }

        // Episodes
        Text {
            id: episodeListLabel
            anchors.top: seasonDetails.bottom
            text: "Episodes:"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.topMargin: root.sh * 0.0145833 //7
            leftPadding: root.sw * 0.009375 //6
            rightPadding: root.sw * 0.009375 //6
            font.pixelSize: root.sh * 0.0291667 //14
        }

        ListView {
            id: episodeList
            model: episodes
            anchors.top: episodeListLabel.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: root.sh * 0.0145833 //7
            height: root.sh * 0.2916667 + root.hintRoom //140
            clip: true

            delegate: Item {
                width: episodeList.width
                height: root.menuRowHeight

                ScrolledText {
                    id: textClip
                    width: Math.min(implicitWidth, episodeList.width)
                    height: parent.height
                    on: episodeList.currentIndex === index && focusRow === 1
                    elides: false
                    text: {
                        var s = (modelData.parentIndex != null) ? ("S" + modelData.parentIndex) : ""
                        var e = modelData.index ? ("E" + modelData.index) : ""
                        var prefix = (s || e) ? (s + e + ": ") : ""
                        return prefix + (modelData.title || "")
                    }
                    color: (episodeList.currentIndex === index && focusRow === 1)
                           ? root.surfaceColor : root.primaryColor
                    font.pixelSize: root.sh * 0.05 //24
                    textItem.topPadding: root.sh * 0.0041667 //2
                    textItem.leftPadding: root.sw * 0.009375 //6
                    textItem.rightPadding: root.sw * 0.009375 //6
                    textItem.bottomPadding: root.sh * 0.00625 //3

                    Rectangle {
                        z: -1
                        color: root.accentColor
                        anchors.fill: textClip.textItem
                        visible: episodeList.currentIndex === index && focusRow === 1
                    }
                }
            }
        }
    }

    // Footer
    HintBar {
        id: footer
        text: root.hintLine(["back:BACK", "navigate:NAVIGATE", "select:SELECT"])
    }
}
