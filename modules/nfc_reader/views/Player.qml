import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string videoPath:  navParams.videoPath || ""
    property string videoTitle: navParams.title || ""

    property bool   playbackStarted: false
    property int    lastStartMs:     0   // what the last attempt started from, for retry
    property int    lastPlPos:       -1

    property int    savedPositionMs: 0
    property int    savedPlaylistPos: -1
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
        // Under the resume question only its Ctrl chords come here: on to
        // Main.qml (Ctrl+Q), not to mpv.
        if (resumePrompt.visible)
            return
        // The keys mpv takes (MpvKeys.js). A key's repeats go as repeats:
        // mpv holds the key down for them, and lets it go as the key is
        // (Keys.onReleased).
        var key = MpvKeys.name(event.key)
        if (key !== "") {
            mpvController.sendKey(key, event.isAutoRepeat)
            event.accepted = true
        }
    }
    // A key let go: the one mpv holds down for its repeats, if any, with it.
    Keys.onReleased: function(event) {
        if (!event.isAutoRepeat)
            mpvController.releaseKeys()
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
            // before any position event — show the error instead of leaving,
            // with mpv's own line on it. mpv itself missing says so.
            if (reason === "failed" && !playbackStarted) {
                failure.show(mpvController.playerFailed ? mpvController.lastError
                                                        : "Check the mapped path or URL\n(YouTube links require yt-dlp)",
                             mpvController.lastErrorDetail, true)
                return
            }
            // Failed once its picture had shown (a stream cut off, a file
            // damaged further in): back to the tap screen as from any end,
            // nothing on the screen to say why, so the log does: the card's
            // title, else what it plays (of an address, logFailure keeps
            // the host alone).
            if (reason === "failed")
                mpvController.logFailure("NfcReader Player", videoTitle !== "" ? "\"" + videoTitle + "\"" : videoPath)
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
            // A tick later: the router isn't listening for goBack yet.
            Qt.callLater(playerRoot.goBack)
            return
        }
        // A card tap is user activity but arrives with no key event — if the
        // screen saver is up, the resume dialog / LOADING frame would render
        // invisibly behind it.
        root.dismissScreenSaver()

        var resolution = appCore.get_setting(moduleRoot.moduleId, "playback_resolution") || "480p"
        ytdlArgs = ["--ytdl=yes", "--ytdl-format=" + nfcReaderBackend.ytdlFormatForResolution(resolution)]

        // As the backend reads it: one saved as On or Off before it had
        // three choices (true or false) is On or Forced Only.
        subtitleMode = nfcReaderBackend.autoSubtitlesChoice(appCore.get_setting(moduleRoot.moduleId, "auto_subtitles"))
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
            resumePrompt.open()
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
            visible: !resumePrompt.visible && !playbackStarted && !failure.visible
        }
    }

    // Where to start a card's video, or its list, with a saved position,
    // while Resume Playback asks: back leaves.
    ChoiceOverlay {
        id: resumePrompt
        anchors.fill: parent
        promptText: "Resume playback?"
        subtitleText: videoTitle
        choices: [{ label: savedPlaylistPos >= 0
                        ? "Resume video " + (savedPlaylistPos + 1) + " at " + root.formatTime(savedPositionMs)
                        : "Resume from " + root.formatTime(savedPositionMs),
                    action: "resume" },
                  { label: "Start from the beginning", action: "start" }]
        onClosed: playerRoot.forceActiveFocus()
        onActivated: function(action) {
            if (action === "resume")
                playerRoot.play(savedPositionMs, savedPlaylistPos)
            else
                playerRoot.play(0, -1)
        }
        onCanceled: playerRoot.goBack()
    }

    PlaybackNotice {
        id: failure
        onRetried: playerRoot.play(playerRoot.lastStartMs, playerRoot.lastPlPos)
        onDismissed: playerRoot.goBack()
    }
}
