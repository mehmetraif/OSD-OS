import QtQuick
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
    }

    Item {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.2604167 //125
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252

        Item {
            anchors.centerIn: parent
            height: statusIndicator.height

            Item {
                id: statusIndicator
                anchors.horizontalCenter: parent.horizontalCenter
                width: statusIndicatorImage.width
                height: root.sh * 0.3791667 //182
                // In the scheme's colour through the app's icon provider, as
                // the title bar's icon is (AppBar).
                Image {
                    id: statusIndicatorImage
                    height: parent.height
                    sourceSize.height: height
                    source: "image://osdicon/" + root.primaryColor.toString().replace("#", "") + "/"
                            + Qt.resolvedUrl("../assets/images/vhs.svg")
                }
                // No reader, or a card it doesn't know: the icon is dithered,
                // the two-colour stand-in for the faded look it used to have.
                Dither {
                    anchors.fill: statusIndicatorImage
                    visible: !nfcReaderBackend.readerConnected || nfcReaderBackend.cardState === "unmatched"
                }
            }

            Rectangle {
                id: statusLabel
                anchors.centerIn: parent
                color: root.surfaceColor
                width: statusIndicatorImage.width * 0.365
                height: statusIndicator.height * 0.375
                clip: true
                Text {
                    id: statusText
                    text: !nfcReaderBackend.readerConnected ? "Reader not connected"
                        : nfcReaderBackend.cardState === "matched" ? "Playing \u25BA"
                        : nfcReaderBackend.cardState === "unmatched" ? "Card not matched"
                        : "Tap a card to play"
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    width: parent.width * 0.9
                    height: parent.height * 0.9
                    anchors.centerIn: parent
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: root.sh * 0.0333333 //16
                    wrapMode: Text.WordWrap
                    lineHeight: 1.3
                }
            }

            Text {
                id: additionalText
                // Naming the reader matters because there is no reader picker —
                // detection is automatic, so this line is how you tell which
                // driver won, and what to plug in when nothing was found.
                text: !nfcReaderBackend.readerConnected
                        ? (nfcReaderBackend.pcscAvailable ? "Connect a PN532 USB or PC/SC reader"
                                                          : "Connect a PN532 USB reader")
                    : nfcReaderBackend.cardState === "matched" ? nfcReaderBackend.videoTitle
                    : nfcReaderBackend.cardState === "unmatched" ? nfcReaderBackend.cardUid
                    : nfcReaderBackend.readerName
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: root.sh * 0.05 //24
                font.pixelSize: root.sh * 0.0291667 //14
            }
        }
    }

    // Dwell on the matched-cassette state (PLAYING ► + title) briefly before
    // handing off to the player, so a tap gets on-screen confirmation of what
    // matched instead of a hard cut to the loading screen. Extra taps during
    // the dwell are already ignored by the backend (m_playbackActive), and
    // backing out mid-dwell is safe: leaving the module disarms the backend.
    Timer {
        id: matchedDwell
        interval: 1200
        repeat: false
        property string pendingPath: ""
        property string pendingTitle: ""
        // Set for a handoff card; empty for an ordinary file/stream card.
        property string pendingModule: ""
        property string pendingRef: ""
        property string pendingMode: ""
        onTriggered: {
            if (pendingModule !== "") {
                // Leaves this module entirely: the shell pushes the NFC module as
                // the back target, so ending playback returns to the tap screen.
                var entry = appCore.module_entry_point(pendingModule)
                if (entry === "") return
                moduleRoot.navigateTo(entry, {
                    cardRef:   pendingRef,
                    cardMode:  pendingMode,
                    cardTitle: pendingTitle
                }, {})
                return
            }
            navigateTo("Player.qml", { videoPath: pendingPath, title: pendingTitle }, {})
        }
    }

    Connections {
        target: nfcReaderBackend
        // A matched card tap hands off to Player.qml, which owns the whole mpv
        // session (launch, key forwarding over IPC, exit handling).
        function onPlaybackRequested(videoPath) {
            matchedDwell.pendingModule = ""
            matchedDwell.pendingPath = videoPath
            matchedDwell.pendingTitle = nfcReaderBackend.videoTitle
            matchedDwell.restart()
        }
        // A card pointing at another module's content (e.g. a Plex guid). Same
        // dwell-then-hand-off shape; that module owns resolution and playback.
        function onCardHandoffRequested(moduleId, ref, mode) {
            matchedDwell.pendingModule = moduleId
            matchedDwell.pendingRef   = ref
            matchedDwell.pendingMode  = mode
            matchedDwell.pendingPath  = ""
            matchedDwell.pendingTitle = nfcReaderBackend.videoTitle
            matchedDwell.restart()
        }
    }

    HintBar {
        id: footer
        text: root.hints.back + ":BACK"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
    }
}