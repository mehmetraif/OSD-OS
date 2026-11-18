import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    // A single picked file, or a non-empty *Paths list when the picker's SHUFFLE
    // slot was chosen for that row — never both.
    property string videoPath:      navParams.videoPath || ""
    property string audioPath:      navParams.audioPath || ""
    property var    videoPaths:     navParams.videoPaths || []
    property var    audioPaths:     navParams.audioPaths || []
    property bool   shuffleVideo:   videoPaths.length > 0
    property bool   shuffleAudio:   audioPaths.length > 0
    property bool   hasCustomAudio: audioPath !== "" || audioPaths.length > 0

    focus: true

    // The keys mpv takes (MpvKeys.js). A key's repeats go as repeats: mpv
    // holds the key down for them, and lets it go as the key is
    // (Keys.onReleased).
    Keys.onPressed: function(event) {
        var key = MpvKeys.name(event.key)
        if (key !== "") {
            mpvController.sendKey(key, event.isAutoRepeat)
            event.accepted = true
        }
    }
    Keys.onReleased: function(event) {
        if (!event.isAutoRepeat)
            mpvController.releaseKeys()
    }

    Connections {
        target: mpvController
        // Ambient clips loop, so in practice mpv only exits on a user quit; handle
        // the single playbackEnded signal regardless of reason — stop the companion
        // audio and return to the menu. Back with Transparent Background leaves
        // the clip playing behind the menus: its audio plays on with it, and
        // stops as the clip ends (main.cpp). A clip mpv could not play says
        // why first (PlaybackNotice), its audio stopped with it: RETRY starts
        // both again, back leaves.
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            if (!mpvController.background)
                ambientModeBackend.stopAudio()
            if (reason === "failed") {
                failure.showMpv(true)
                return
            }
            goBack()
        }
    }

    Component.onCompleted: {
        if (videoPath === "" && videoPaths.length === 0) {
            // A tick later: the router isn't listening for goBack yet.
            Qt.callLater(playerRoot.goBack)
            return
        }
        play()
    }

    // The clip, or the shuffled clips, and the companion audio beside them.
    function play() {
        // Shuffling hands mpv the whole library as one playlist and lets --shuffle
        // plus --loop-playlist=inf (from loop: true) cycle it, rather than the app
        // detecting each clip's end and loading a new one. First entry goes in as
        // the url, the rest as extra playlist entries.
        var first = shuffleVideo ? videoPaths[0] : videoPath
        var rest  = shuffleVideo ? videoPaths.slice(1) : []
        mpvController.loadAndPlay(first, 0.0, 0, -1, [], [], true, -1, 0.0, "",
                                  hasCustomAudio, "ambient", shuffleVideo, [], 0.0,
                                  false, [], "", rest)

        // Companion audio is a second, independent mpv process — it does its own
        // shuffling and looping (see AmbientModeBackend::startAudio).
        if (audioPaths.length > 0)
            ambientModeBackend.startAudio(audioPaths, shuffleAudio)
        else if (audioPath !== "")
            ambientModeBackend.startAudio([audioPath], false)
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    PlaybackNotice {
        id: failure
        onRetried: playerRoot.play()
        onDismissed: playerRoot.goBack()
    }
}
