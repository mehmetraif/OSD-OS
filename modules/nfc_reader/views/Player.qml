import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string videoPath:  navParams.videoPath || ""
    property string videoTitle: navParams.title || ""

    property bool   playbackStarted: false
    property string errorMessage:    ""
    property int    lastStartMs:     0   // what the last attempt started from, for retry
    property int    lastPlPos:       -1

    property bool   overlayVisible:  false
    property int    savedPositionMs: 0
    property int    savedPlaylistPos: -1
    property int    choiceIndex:     0
    property var    ytdlArgs:        []
    property string subtitleMode:    "forced"
    property var    subtitleLangs:   []

    // mpv subtitle-track flag derived from subtitleMode: 0 = on, -1 = forced only, -2 = off.
    property int    subFlag:         (subtitleMode == "on") ? 0 : ((subtitleMode == "forced") ? -1 : -2)

    // Track last non-null values during playback; groundwork for a future
    // resume-playback setting (mirrors the other module players).
    property int    lastKnownPositionMs: 0
    property int    lastKnownDurationMs: 0
    property int    lastKnownPlaylistPos: -1

    focus: true

    // Playlists get their item index saved alongside the timecode so a card
    // resumes at "video N" rather than replaying the whole list: .m3u/.m3u8
    // by extension (same set as local_files), YouTube playlist URLs by the
    // list= query param.
    function isPlaylist(path) {
        var lower = path.toLowerCase()
        if (/\.m3u8?$/.test(lower)) return true
        return (lower.indexOf("youtube.") !== -1 || lower.indexOf("youtu.be") !== -1)
               && lower.indexOf("list=") !== -1
    }

    function play(startMs, plPos) {
        lastStartMs = startMs
        lastPlPos   = plPos
        // extraArgs opts into yt-dlp so YouTube-page URLs in the mapping
        // resolve; safe for local files and direct media URLs, which the
        // native demuxer handles before the ytdl hook ever runs.
        mpvController.loadAndPlay(videoPath, startMs / 1000.0, -1, subFlag, [], subtitleLangs, false, plPos, 0.0, "", false, "", false, [], 0.0, false, ytdlArgs)
    }

    Keys.onPressed: function(event) {
        if (errorMessage !== "") {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                errorMessage = ""
                play(lastStartMs, lastPlPos)
                event.accepted = true
            }
        } else if (overlayVisible) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                choiceIndex = 0
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                choiceIndex = 1
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                overlayVisible = false
                play(choiceIndex === 0 ? savedPositionMs : 0,
                     choiceIndex === 0 ? savedPlaylistPos : -1)
                event.accepted = true
            }
        } else {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                mpvController.sendKey("ESC")
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace) {
                mpvController.sendKey("BS")
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                mpvController.sendKey("UP")
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                mpvController.sendKey("DOWN")
                event.accepted = true
            } else if (event.key === Qt.Key_Left) {
                mpvController.sendKey("LEFT")
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                mpvController.sendKey("RIGHT")
                event.accepted = true
            } else if (event.key === Qt.Key_Space) {
                mpvController.sendKey("SPACE")
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                mpvController.sendKey("ENTER")
                event.accepted = true
            }
        }
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0) {
                playerRoot.playbackStarted = true
                playerRoot.lastKnownPositionMs = ms
            }
        }
        function onDurationChanged(ms) {
            if (ms > 0) playerRoot.lastKnownDurationMs = ms
        }
        function onPlaylistPosChanged(pos) {
            if (pos >= 0) {
                playerRoot.lastKnownPlaylistPos = pos
                playerRoot.lastKnownPositionMs  = 0
            }
        }

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            // A bad mapping path or missing yt-dlp surfaces as an mpv failure
            // before any position event — show the error instead of leaving.
            if (reason === "failed" && !playbackStarted) {
                playerRoot.errorMessage = "Check the mapped path or URL\n(YouTube links require yt-dlp)"
                return
            }
            var pos   = lastKnownPositionMs || finalPositionMs
            var dur   = lastKnownDurationMs || finalDurationMs
            var plPos = lastKnownPlaylistPos
            if (isPlaylist(videoPath)) {
                // The per-item duration can't tell "finished the list" from
                // "finished one video of it", but the exit reason can: mpv only
                // ends with "eof" when the final item played to its end (a quit
                // mid-list leaves a trailing quit/stop end-file event). A
                // completed playlist clears its resume point like a completed
                // single video; anything else saves item + timecode.
                if (reason === "eof")
                    nfcReaderBackend.clearPosition(videoPath)
                else if (pos > 0 || plPos >= 0)
                    nfcReaderBackend.savePosition(videoPath, pos, plPos)
            } else {
                // Same completion rule as local_files: near the end clears the
                // resume point, meaningful progress saves it.
                if (dur > 0 && pos >= dur * 0.95)
                    nfcReaderBackend.clearPosition(videoPath)
                else if (pos > 5000)
                    nfcReaderBackend.savePosition(videoPath, pos, -1)
            }
            goBack()
        }
    }

    Component.onCompleted: {
        if (videoPath === "") {
            goBack()
            return
        }
        // A card tap is user activity but arrives with no key event — if the
        // screen saver is up, the resume dialog / LOADING frame would render
        // invisibly behind it.
        root.dismissScreenSaver()

        var resolution = appCore.get_setting(moduleRoot.moduleId, "playback_resolution") || "480p"
        ytdlArgs = ["--ytdl=yes", "--ytdl-format=" + nfcReaderBackend.ytdlFormatForResolution(resolution)]

        subtitleMode = appCore.get_setting(moduleRoot.moduleId, "auto_subtitles") || "forced"
        // mpv takes a *list* of languages to fall back through; "-" is the
        // stored id for "Any" (no preference), which adds nothing and launches
        // mpv without an --slang preference — same behavior as local_files.
        var subLangString = appCore.get_setting(moduleRoot.moduleId, "sub_lang") || "-"
        subtitleLangs = (subLangString !== "-") ? [subLangString] : []

        var resumeSetting = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"
        if (resumeSetting === "no") {
            play(0, -1)
            return
        }

        var saved    = nfcReaderBackend.getSavedPosition(videoPath)
        var savedPos = saved.pos || 0
        var savedPl  = (saved.plPos !== undefined && saved.plPos !== null) ? saved.plPos : -1

        if (resumeSetting === "yes") {
            play(savedPos, savedPl)
        } else if (savedPos > 0 || savedPl >= 0) {
            // plPos alone is enough to offer resume: stopping early in video N
            // of a playlist saves the item index with a near-zero timecode.
            savedPositionMs  = savedPos
            savedPlaylistPos = savedPl
            overlayVisible = true
        } else {
            play(0, -1)
        }
    }

    // Every exit path (natural end, user quit, backing out of the error
    // screen) must re-arm the backend or it keeps ignoring card taps.
    Component.onDestruction: nfcReaderBackend.resetAfterPlayback()

    Rectangle {
        anchors.fill: parent
        color: "black"

        // Shown while mpv launches and (for YouTube URLs) yt-dlp resolves the
        // stream. Hidden once the first position update arrives. The title
        // carries the tap confirmation from Items.qml through the whole
        // pre-playback wait.
        LoadingScreen {
            anchors.fill: parent
            source: moduleRoot.moduleName
            title: videoTitle
            startMs: playerRoot.lastStartMs
            durationMs: playerRoot.lastKnownDurationMs
            visible: !overlayVisible && !playbackStarted && errorMessage === ""
        }

        PromptScreen {
            visible: errorMessage !== ""
            kind: "notice"
            title: "Playback failed"
            message: errorMessage
            hint: root.hints.back + ":BACK " + root.hints.select + ":RETRY"
        }
    }

    PromptScreen {
        visible: overlayVisible
        title: "Resume playback?"
        message: videoTitle
        choices: [
            savedPlaylistPos >= 0
                ? "Resume video " + (savedPlaylistPos + 1) + " at " + root.formatTime(savedPositionMs)
                : "Resume from " + root.formatTime(savedPositionMs),
            "Start from the beginning"
        ]
        currentIndex: choiceIndex
        onHovered: function(index) { choiceIndex = index }
    }

}
