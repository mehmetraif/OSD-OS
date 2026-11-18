import QtQuick
import Components

FocusScope {
    id: itemsRoot

    // In a layout's frame (Settings → Look → Menu Layout), as the other
    // modules' pages are: the page in the pane (Main.qml's), and in a monitor
    // layout its lines the layout's own (MonitorRow).
    readonly property bool inFrame: true
    readonly property bool monitorRows: root.monitorInk === true

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    // Focus rows: 0=video track, 1=audio track, 2=start button
    property int focusRow: 0

    property var videoFiles: []
    property var audioFiles: []
    property int videoIndex: 0
    property int audioIndex: 0   // 0 = "Video Audio"; 1+ = audioFiles[audioIndex - 1]

    property bool autoLaunch: false

    // Each row ends in a virtual SHUFFLE slot, offered only when there is actually
    // more than one thing to shuffle.
    property bool videoShuffleAvailable: videoFiles.length > 1
    property bool audioShuffleAvailable: audioFiles.length > 1

    // Video row:  0..N-1 = files,        N   = SHUFFLE
    // Audio row:  0 = VIDEO AUDIO, 1..N = files, N+1 = SHUFFLE
    property int videoSlotCount: videoFiles.length + (videoShuffleAvailable ? 1 : 0)
    property int audioSlotCount: audioFiles.length + 1 + (audioShuffleAvailable ? 1 : 0)
    property bool videoShuffleSelected: videoShuffleAvailable && videoIndex === videoFiles.length
    property bool audioShuffleSelected: audioShuffleAvailable && audioIndex === audioFiles.length + 1

    focus: true

    function launchPlayer(videoShuffle, audioShuffle) {
        // The picker as it was, for the way back from the player.
        navigateTo("Player.qml", {
            videoPath:  videoShuffle ? "" : videoFiles[videoIndex].path,
            videoPaths: videoShuffle ? videoFiles.map(function(f) { return f.path }) : [],
            audioPath:  (!audioShuffle && audioIndex > 0) ? audioFiles[audioIndex - 1].path : "",
            audioPaths: audioShuffle ? audioFiles.map(function(f) { return f.path }) : []
        }, { returnedFromPlayer: true, focusRow: focusRow, videoIndex: videoIndex, audioIndex: audioIndex })
    }

    Component.onCompleted: {
        videoFiles = ambientModeBackend.getVideoFiles()
        audioFiles = ambientModeBackend.getAudioFiles()
        autoLaunch = !!appCore.get_setting(moduleRoot.moduleId, "auto_launch")
        // Back from the player: the picker where it was left, within the
        // files there are now.
        if (navListState.videoIndex !== undefined)
            videoIndex = Math.max(0, Math.min(navListState.videoIndex, videoSlotCount - 1))
        if (navListState.audioIndex !== undefined)
            audioIndex = Math.max(0, Math.min(navListState.audioIndex, audioSlotCount - 1))
        if (navListState.focusRow !== undefined)
            focusRow = Math.max(0, Math.min(navListState.focusRow, 2))

        // Auto-launch only fires when the app boots straight into this module
        // (it's the configured startup module), not when someone navigates here
        // from the main menu, that screen still has to be reachable to change
        // video/audio/shuffle. Also skipped when we're only back here because
        // the player exited, or ESC would relaunch forever with no way out.
        if (autoLaunch && videoFiles.length > 0 && navParams.fromAppStartup && !navListState.returnedFromPlayer) {
            // Auto-launch always shuffles
            Qt.callLater(function() { launchPlayer(videoShuffleAvailable, audioShuffleAvailable) })
        }
    }

    Keys.onPressed: function(event) {
        if (root.isBack(event)) {
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            focusRow = Math.max(0, focusRow - 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            focusRow = Math.min(2, focusRow + 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            // Both rows wrap (app-wide list convention), which is what puts SHUFFLE
            // one Left press away from each row's default position.
            if (focusRow === 0 && videoSlotCount > 1)
                videoIndex = (videoIndex - 1 + videoSlotCount) % videoSlotCount
            else if (focusRow === 1 && audioSlotCount > 1)
                audioIndex = (audioIndex - 1 + audioSlotCount) % audioSlotCount
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            if (focusRow === 0 && videoSlotCount > 1)
                videoIndex = (videoIndex + 1) % videoSlotCount
            else if (focusRow === 1 && audioSlotCount > 1)
                audioIndex = (audioIndex + 1) % audioSlotCount
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (focusRow === 2 && videoFiles.length > 0)
                launchPlayer(videoShuffleSelected, audioShuffleSelected)
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
    }

    // Empty state
    Column {
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: root.paneX + root.paneWidth / 2 - root.sw / 2
        anchors.verticalCenterOffset: root.monitorFramed === true
                                      ? (root.paneTop + root.paneBottom) / 2 - root.sh / 2 : 0
        spacing: root.sh * 0.0333333 //16
        visible: videoFiles.length === 0
        Text {
            text: "No items found"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.05 //24
        }
        Text {
            text: "Please add items in the ambient:mode media directory"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
        }
    }

    // Body
    Item {
        visible: videoFiles.length > 0 && !itemsRoot.monitorRows
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: root.paneX + root.paneWidth / 2 - root.sw / 2
        width: root.paneWidth //492
        height: root.sh * 0.4083333 //196
        clip: true

        // Playback Settings
        Text {
            id: playbackSettingsLabel
            text: "Playback Settings:"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.top: parent.top
            anchors.topMargin: root.sh * 0.0145833 //7
            leftPadding: root.sw * 0.009375 //6
            rightPadding: root.sw * 0.009375 //6
            font.pixelSize: root.sh * 0.0291667 //14
        }

        // Video Track
        Item {
            id: videoTrackRow
            anchors.top: playbackSettingsLabel.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: root.sh * 0.0145833 //7
            height: root.menuRowHeight

            Rectangle {
                anchors.fill: parent
                color: focusRow === 0 ? root.primaryColor : "transparent"
            }

            Text {
                text: "Video"
                color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.009375 //6
                font.pixelSize: root.sh * 0.0416667 //20
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.009375 //6
                spacing: root.sw * 0.00625 //4

                Text {
                    text: "◄"
                    color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                    visible: videoSlotCount > 1
                }
                // Capped and clipped so a long filename can't run under the row
                // label, and scrolled through while the row is selected.
                ScrolledText {
                    id: videoValueClip
                    width: Math.min(implicitWidth, root.sw * 0.35)
                    height: parent.height
                    on: focusRow === 0
                    elides: false
                    // The body is hidden with no videos, but still bound.
                    text: videoShuffleSelected ? "SHUFFLE" : (videoFiles.length > 0 ? videoFiles[videoIndex].name : "")
                    color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                    font.pixelSize: root.sh * 0.0416667 //20
                }
                Text {
                    text: "►"
                    color: focusRow === 0 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                    visible: videoSlotCount > 1
                }
            }
        }

        // Audio Track
        Item {
            id: audioTrackRow
            anchors.top: videoTrackRow.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: root.menuRowHeight

            Rectangle {
                anchors.fill: parent
                color: focusRow === 1 ? root.primaryColor : "transparent"
            }

            Text {
                text: "Audio"
                color: focusRow === 1 ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: root.sw * 0.009375 //6
                font.pixelSize: root.sh * 0.0416667 //20
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: root.sw * 0.009375 //6
                spacing: root.sw * 0.00625 //4

                Text {
                    text: "◄"
                    color: focusRow === 1 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                    visible: audioSlotCount > 1
                }
                ScrolledText {
                    id: audioValueClip
                    width: Math.min(implicitWidth, root.sw * 0.35)
                    height: parent.height
                    on: focusRow === 1
                    elides: false
                    text: audioShuffleSelected ? "SHUFFLE"
                        : audioIndex === 0 ? "VIDEO AUDIO"
                        : audioFiles[audioIndex - 1].name
                    color: focusRow === 1 ? root.surfaceColor : root.primaryColor
                    font.pixelSize: root.sh * 0.0416667 //20
                }
                Text {
                    text: "►"
                    color: focusRow === 1 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: root.sh * 0.0375 //18
                    visible: audioSlotCount > 1
                }
            }
        }

        Row {
            id: startPlayback
            anchors.top: audioTrackRow.bottom
            anchors.topMargin: root.sh * 0.0583333 //28

            // PLAY button
            Rectangle {
                id: playButton
                color: focusRow === 2 ? root.primaryColor : root.surfaceColor
                border.color: root.primaryColor
                width: root.paneWidth //492
                height: root.sh * 0.1166667 //56
                border.width: root.sh * 0.003125 //2

                Text {
                    anchors.centerIn: parent
                    text: "START ►"
                    color: focusRow === 2 ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.05 //24
                }
            }
        }
    }

    // In a monitor layout: the same lines as the layout draws them, in its
    // pane, the values changed with ◄ ► as above.
    Column {
        visible: videoFiles.length > 0 && itemsRoot.monitorRows
        x: root.paneX
        y: root.listY
        width: root.paneWidth
        MonitorRow {
            width: parent.width
            height: root.menuRowHeight
            heading: true
            label: "Playback Settings"
        }
        MonitorRow {
            width: parent.width
            height: root.menuRowHeight
            label: "Video"
            value: videoValueClip.text
            selected: itemsRoot.focusRow === 0
        }
        MonitorRow {
            width: parent.width
            height: root.menuRowHeight
            label: "Audio"
            value: audioValueClip.text
            selected: itemsRoot.focusRow === 1
        }
        MonitorRow {
            width: parent.width
            height: root.menuRowHeight
            label: "Start"
            opens: true
            selected: itemsRoot.focusRow === 2
        }
    }

    HintBar {
        id: footer
        text: root.hintLine(["back:BACK", "navigate:NAVIGATE", "change:CHANGE", "select:SELECT"])
    }
}
