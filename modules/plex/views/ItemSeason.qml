import QtQuick
import Components

FocusScope {
    id: seasonRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var item: navParams.item || {}
    property string showTitle: navParams.showTitle || item.grandparentTitle || ""
    property string libraryName: navParams.libraryName || ""

    property var episodes: []
    property bool isLoading: false

    // Extras attached to this season (trailers, behind the scenes, …)
    property var extras: []
    readonly property bool hasExtras: extras.length > 0

    // Drives both the play button's PLAY/RSUM label and the wording of the
    // matching choice in the play prompt — one expression, two consumers.
    readonly property bool resumeAvailable: {
        for (var i = 0; i < episodes.length; i++) {
            if (episodes[i].viewOffset > 0) return true
        }
        return false
    }

    // Focus rows: 0 = play button, 1 = extras (when hasExtras),
    // 4 = write NFC card (when a reader is present), 2 = episode list.
    // The NFC row is 4 rather than 3 so the existing saved-focus restores, which
    // compare against literal row numbers, keep working untouched.
    property int focusRow: 0

    property color baseColor: root.primaryColor

    Connections {
        target: plexBackend

        function onChildrenLoaded(loadedItems) {
            seasonRoot.isLoading = false
            seasonRoot.episodes = loadedItems
            if (loadedItems.length > 0) {
                var restore = (navListState.currentIndex !== undefined) ? navListState.currentIndex : 0
                episodeList.currentIndex = Math.min(restore, loadedItems.length - 1)
                if (navListState.focusRow === 2) focusRow = 2
                episodeList.positionViewAtIndex(episodeList.currentIndex, ListView.Contain)
            }
        }

        function onExtrasLoaded(items) {
            seasonRoot.extras = items
            if (navListState.focusRow === 1 && items.length > 0) focusRow = 1
        }

        function onErrorOccurred(msg) {
            seasonRoot.isLoading = false
            console.log("[SeasonItem] Error: " + msg)
        }
    }

    Component.onCompleted: {
        isLoading = true
        focusRow = 0
        if (item.ratingKey) {
            plexBackend.load_children(item.ratingKey)
            plexBackend.load_extras(item.ratingKey)
        }
    }

    focus: true

    // Season label as displayed in the header line ("SPECIALS" / "SEASON n")
    function seasonLabel() {
        if (item.index === 0) return "Specials"
        if (item.index) return "Season " + item.index
        return ""
    }

    // The episode under the mouse becomes the cursor's (Main.qml's
    // followPointer()).
    function hoverRow(index) {
        episodeList.currentIndex = index
        focusRow = 2
    }

    Keys.onUpPressed: {
        if (focusRow === 2) {
            if (episodeList.currentIndex > 0) {
                episodeList.currentIndex--
            } else {
                focusRow = cardWriter.available ? 4 : (hasExtras ? 1 : 0)
            }
        } else if (focusRow === 4) {
            focusRow = hasExtras ? 1 : 0
        } else if (focusRow === 1) {
            focusRow = 0
        } else {
            if (episodes.length > 0) {
                episodeList.currentIndex = episodes.length - 1
                focusRow = 2
            } else if (cardWriter.available) {
                focusRow = 4
            } else if (hasExtras) {
                focusRow = 1
            }
        }
        episodeList.positionViewAtIndex(episodeList.currentIndex, ListView.Contain)
    }
    Keys.onDownPressed: {
        if (focusRow === 0) {
            if (hasExtras) focusRow = 1
            else if (cardWriter.available) focusRow = 4
            else if (episodes.length > 0) {
                episodeList.currentIndex = 0
                focusRow = 2
            }
        } else if (focusRow === 1) {
            if (cardWriter.available) focusRow = 4
            else if (episodes.length > 0) {
                episodeList.currentIndex = 0
                focusRow = 2
            } else {
                focusRow = 0
            }
        } else if (focusRow === 4) {
            if (episodes.length > 0) {
                episodeList.currentIndex = 0
                focusRow = 2
            } else {
                focusRow = 0
            }
        } else {
            if (episodeList.currentIndex < episodes.length - 1) {
                episodeList.currentIndex++
            } else {
                episodeList.currentIndex = 0
                focusRow = 0
            }
        }
        episodeList.positionViewAtIndex(episodeList.currentIndex, ListView.Contain)
    }
    Keys.onReturnPressed: {
        if (focusRow === 0) {
            if (episodes.length === 0) return
            playChoice.open()
        } else if (focusRow === 4) {
            cardWriter.open()
        } else if (focusRow === 1) {
            var label = seasonLabel()
            seasonRoot.navigateTo("Extras.qml", {
                extras: extras,
                ratingKey: item.ratingKey,
                itemTitle: showTitle + (label ? " - " + label : ""),
                libraryName: libraryName
            }, { focusRow: 1 })
        } else {
            var ep = episodes[episodeList.currentIndex]
            if (!ep) return
            seasonRoot.navigateTo("Item.qml", {
                item: ep,
                libraryName: libraryName
            }, { currentIndex: episodeList.currentIndex, focusRow: 2 })
        }
    }
    Keys.onPressed: function(event) {
        if (root.isBack(event)) {
            goBack()
            event.accepted = true
        }
    }

    function playBestEpisode() {
        if (episodes.length === 0) return
        var target = null
        // 1. In-progress episode (viewOffset > 0)
        for (var i = 0; i < episodes.length; i++) {
            if (episodes[i].viewOffset > 0) { target = episodes[i]; break }
        }
        // 2. First unwatched episode (viewCount === 0)
        if (!target) {
            for (var j = 0; j < episodes.length; j++) {
                if (!episodes[j].viewCount || episodes[j].viewCount === 0) { target = episodes[j]; break }
            }
        }
        // 3. All watched — start from beginning
        if (!target) {
            target = episodes[0]
        }
        var ep = Object.assign({}, target)
        // For cases 2 and 3 ensure we start from beginning
        if (!ep.viewOffset) ep.viewOffset = 0
        seasonRoot.navigateTo("Item.qml", {
            item: ep,
            libraryName: libraryName
        }, {})
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

            Column {
                width: root.sw * 0.1875 //120

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
                        text: seasonRoot.resumeAvailable ? "RSUM \u25BA" : "PLAY \u25BA"
                        color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.pixelSize: root.sh * 0.05 //24
                    }
                }

                // Extras Button
                Rectangle {
                    id: extrasButton
                    visible: seasonRoot.hasExtras
                    color: focusRow === 1 ? root.accentColor : "transparent"
                    border.width: focusRow === 1 ? 0 : root.px
                    border.color: root.primaryColor
                    width: parent.width
                    height: extrasLabel.implicitHeight + root.sh * 0.025 //12
                    anchors.horizontalCenter: parent.horizontalCenter

                    Text {
                        id: extrasLabel
                        text: "VIEW EXTRAS"
                        color: focusRow === 1 ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        anchors.centerIn: parent
                        font.pixelSize: root.sh * 0.025 //12
                    }
                }

                // Write NFC Card. Directly under Extras so it collapses upward
                // when there are no extras, leaving screen height unchanged.
                Rectangle {
                    id: writeCardButton
                    visible: cardWriter.available
                    color: focusRow === 4 ? root.accentColor : "transparent"
                    border.width: focusRow === 4 ? 0 : root.px
                    border.color: root.primaryColor
                    width: parent.width
                    height: writeCardLabel.implicitHeight + root.sh * 0.025 //12
                    anchors.horizontalCenter: parent.horizontalCenter

                    Text {
                        id: writeCardLabel
                        text: "WRITE NFC CARD"
                        color: focusRow === 4 ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        anchors.centerIn: parent
                        font.pixelSize: root.sh * 0.025 //12
                    }
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
                        var yr = item.originallyAvailableAt
                                 ? item.originallyAvailableAt.substring(0, 4)
                                 : (item.year ? String(item.year) : "")
                        if (yr) parts.push(yr)
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

        ListView {
            id: episodeList
            model: episodes
            anchors.top: seasonDetails.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: root.sh * 0.05625 //27
            height: root.sh * 0.2916667 + root.hintRoom //140
            clip: true

            delegate: Item {
                width: episodeList.width
                height: root.menuRowHeight

                ScrolledText {
                    id: textClip
                    width: Math.min(implicitWidth, episodeList.width)
                    height: parent.height
                    on: episodeList.currentIndex === index && focusRow === 2
                    elides: false
                    text: {
                        var s = (modelData.parentIndex != null) ? ("S" + modelData.parentIndex) : ""
                        var e = modelData.index ? ("E" + modelData.index) : ""
                        var prefix = (s || e) ? (s + e + ": ") : ""
                        return prefix + (modelData.title || "")
                    }
                    color: (episodeList.currentIndex === index && focusRow === 2)
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
                        visible: episodeList.currentIndex === index && focusRow === 2
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
    // PLAY prompt. Shuffle plays as a jukebox — endless random episodes drawn
    // from this season, reporting no timeline — so it must be a deliberate
    // choice, not something the play button silently does.
    ChoiceOverlay {
        id: playChoice
        anchors.fill: parent
        promptText:   "What would you like to play?"
        subtitleText: seasonRoot.showTitle
                      + (seasonRoot.seasonLabel() ? " - " + seasonRoot.seasonLabel() : "")
        choices: [
            { label: seasonRoot.resumeAvailable ? "Resume Next Episode" : "Play Next Episode",
              action: "next" },
            { label: "Shuffle Episodes", action: "shuffle" }
        ]
        onClosed: seasonRoot.forceActiveFocus()
        onActivated: function(action) {
            if (action === "shuffle") {
                var label = seasonRoot.seasonLabel()
                seasonRoot.navigateTo("QueuePlay.qml", {
                    shuffleScope: item.ratingKey,
                    title:        showTitle + (label ? " - " + label : ""),
                    libraryName:  libraryName
                }, { focusRow: 0 })
            } else {
                seasonRoot.playBestEpisode()
            }
        }
    }

    NfcCardWriter {
        id: cardWriter
        anchors.fill: parent
        offerShuffle: true
        cardRef:   item.guid || ""
        // "Show (Year) - S1" rather than "Show - Season 1", so season cards for one
        // show stay distinct, sort next to that show's episode cards, and don't
        // collide with a same-named show from another year. parentYear is the
        // show's year — a season carries no year of its own.
        cardTitle: seasonRoot.showTitle
                   + (item.parentYear ? " (" + item.parentYear + ")" : "")
                   + (item.index !== undefined ? " - S" + item.index : "")
        onClosed: seasonRoot.forceActiveFocus()
    }

}
