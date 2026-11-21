import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property var    item:     navParams.item || ({})
    property string videoUrl: item.url || ""
    property string videoId:  item.videoId || ""

    property alias  playbackStarted:  session.started
    property int    savedPositionMs:  0
    property var    ytdlArgs:         []
    // Subtitles, from ADVANCED: -2 none, 0 the Subtitle Language's (subLangs).
    property int    subTrack:         -2
    property var    subLangs:         []
    // Where the session is (PlayerSession).
    property alias  lastStartMs:         session.startMs
    property alias  lastKnownPositionMs: session.positionMs
    property alias  lastKnownDurationMs: session.durationMs

    focus: true

    function play(startMs) {
        session.begin(startMs)
        mpvController.loadAndPlay({ url: videoUrl, startSeconds: startMs / 1000.0, subTrack: subTrack,
                                    subLangs: subLangs, extraArgs: ytdlArgs })
        // How the main menu takes it back once it plays behind the menus, and
        // that back opens this player's menu.
        mpvController.noteSession({ module: moduleRoot.moduleId, title: item.title || "",
                                    params: { item: item }, menu: true })
    }

    // The ADVANCED settings the video plays with, as the backend reads them
    // (each at its manifest default until set).
    function readSettings() {
        var advanced = youtubeBackend.playbackSettings()
        ytdlArgs = youtubeBackend.playbackArgs(advanced)
        subTrack = advanced.subtitles !== "Off" ? 0 : -2
        subLangs = advanced.subtitles !== "Off" ? [advanced.subtitleLanguage] : []
    }

    function openMenu() {
        playerMenu.actions = menuActions()
        playerMenu.open(item.title || "")
    }
    // The video as FAVORITES keeps it, as the tree's options put it there
    // (Items.qml's offerOptions): without the description and counts.
    readonly property var entry: ({ name: item.name, path: item.path, isFolder: false, kind: "video",
                                    videoId: item.videoId, url: item.url, title: item.title,
                                    channelName: item.channelName || "", isShort: !!item.isShort })

    // Its options, as the tree offers them, then the way to the tree.
    function menuActions() {
        var later = youtubeBackend.isInWatchLater(videoId)
        return EntryFavorites.choices(EntryFavorites.isFavorite(appCore, moduleRoot.moduleId, entry),
                                      EntryFavorites.isAtStartup(appCore, moduleRoot.moduleId, entry))
               .concat([{ label: later ? "Remove from Watch Later" : "Save to Watch Later", action: "later" }])
    }
    // A setting changed in the menu: the speed and the Scaling at once, the
    // rest as the menu closes.
    function applySetting(key, value) {
        if (key === "playback_speed")
            mpvController.setVideoProperty("speed", parseFloat(value) || 1.0)
        else if (key === "video_scaling")
            playerMenu.applyScaling(value)
        else
            session.reloadOnClose = true
    }
    function menuAction(action) {
        if (session.menuAction(action))
            return
        if (action === "favorite") {
            EntryFavorites.toggleFavorite(appCore, moduleRoot.moduleId, entry)
        } else if (action === "startup") {
            EntryFavorites.toggleStartup(appCore, moduleRoot.moduleId, entry)
        } else if (action === "later") {
            if (youtubeBackend.isInWatchLater(videoId))
                youtubeBackend.removeFromWatchLater(videoId)
            else
                youtubeBackend.addToWatchLater(videoId, item.title || "", item.channelName || "")
        }
        playerMenu.actions = menuActions()
        playerMenu.refresh()
    }
    function backToVideo() {
        session.backToVideo()
    }

    Keys.onPressed: function(event) {
        // Under the resume question only its Ctrl chords come here: on to
        // Main.qml (Ctrl+Q), not to mpv.
        if (resumePrompt.visible)
            return
        session.forwardKey(event)
    }
    Keys.onReleased: function(event) { session.releaseKey(event) }

    // Back during the video opens its menu (PlayerMenu): over the picture,
    // the video playing on behind it, with Transparent Background; without,
    // the video ends for it (mpv has the screen) and starts again where it
    // was as the menu closes. A change the video can't take as it plays (its
    // format, its audio, its subtitles) reloads it where it is as the menu
    // closes; STOP, TO MAIN MENU goes back to the main menu.
    PlayerSession {
        id: session
        player: playerRoot
        menu: playerMenu
        onMenuRequested: playerRoot.openMenu()
        onRestartRequested: {
            playerRoot.readSettings()
            playerRoot.playbackStarted = false
            playerRoot.play(playerRoot.lastKnownPositionMs)
        }
    }

    Connections {
        target: mpvController

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            // yt-dlp missing/outdated or an unplayable stream surfaces as mpv exit
            // code 2 before any position event — show the error instead of leaving,
            // with mpv's own line on it (yt-dlp's ERROR). mpv itself missing says so.
            if (reason === "failed" && !playbackStarted) {
                failure.show(mpvController.playerFailed ? mpvController.lastError
                                                        : "Please check that yt-dlp is installed and up to date",
                             mpvController.lastErrorDetail, true)
                return
            }
            // Failed once its picture had shown (the stream cut off): back as
            // from any end, nothing on the screen to say why, so the log does.
            if (reason === "failed")
                mpvController.logFailure("YouTube Player", "\"" + (item.title || "") + "\" (" + videoId + ")")
            var pos = lastKnownPositionMs || finalPositionMs
            var dur = lastKnownDurationMs || finalDurationMs
            // Completed videos stay in history with pos 0: they list under
            // History but never trigger the resume prompt.
            if (dur > 0 && pos >= dur * 0.95)
                youtubeBackend.savePosition(videoId, 0, item.title || "", item.channelName || "")
            else if (pos > 5000)
                youtubeBackend.savePosition(videoId, pos, item.title || "", item.channelName || "")
            session.end(reason)
        }
    }

    Component.onCompleted: {
        if (videoUrl === "") {
            // A turn later: the router isn't listening yet.
            Qt.callLater(playerRoot.goBack)
            return
        }
        readSettings()

        var resumeSetting = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "Ask"
        var saved = youtubeBackend.getSavedPosition(videoId)
        var savedPos = saved.pos || 0
        // This video, still playing behind the menus: it goes on full screen
        // where it is, without asking, the same session (takeBack(): what
        // back saved can't say where it is, as nothing in its first seconds
        // or near its end is saved); left with BACK TO <MODULE>, it starts again where
        // it was saved.
        var note = root.takeBackNote
        var behind = note.module === moduleRoot.moduleId && note.params && note.params.item
                     && note.params.item.videoId === videoId
        if (behind && root.videoBehind && mpvController.takeBack()) {
            playbackStarted = true
            return
        }

        // The favourite played at startup begins without asking, where
        // Settings' STARTUP FROM says.
        var startupFrom = navParams.startup ? (appCore.get_setting("", "startup_from") || "Resume") : ""

        if (startupFrom === "Beginning") {
            play(0)
        } else if (resumeSetting === "Always" || behind || startupFrom !== "") {
            play(savedPos)
        } else if (savedPos > 0) {
            savedPositionMs = savedPos
            resumePrompt.open()
        } else {
            play(0)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        // The video shows through its menu.
        visible: !playerMenu.visible

        // Shown while mpv launches and buffers the stream (before its window
        // takes over). Hidden once the first position update arrives, or while
        // the resume prompt is up.
        LoadingScreen {
            anchors.fill: parent
            source: moduleRoot.moduleName
            startMs: playerRoot.lastStartMs
            durationMs: playerRoot.lastKnownDurationMs
            visible: !resumePrompt.visible && !playbackStarted && !failure.visible
        }
    }

    // Where to start a video with a saved position, while Resume Playback
    // asks: back leaves.
    ChoiceOverlay {
        id: resumePrompt
        anchors.fill: parent
        promptText: "Resume playback?"
        subtitleText: item.title || ""
        choices: [{ label: "Resume from " + root.formatTime(savedPositionMs), action: "resume" },
                  { label: "Start from the beginning", action: "start" }]
        onClosed: playerRoot.forceActiveFocus()
        onActivated: function(action) { playerRoot.play(action === "resume" ? savedPositionMs : 0) }
        onCanceled: playerRoot.goBack()
    }

    PlayerMenu {
        id: playerMenu
        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        iconSource: moduleRoot.moduleIcon
        moduleName: moduleRoot.moduleName
        // The everyday ones: Video Codec and Max Frame Rate stay in its
        // settings' ADVANCED.
        keys: ["subtitles", "subtitle_language", "audio_language", "playback_speed", "video_scaling",
               "playback_resolution"]
        onSettingChanged: function(key, value) { playerRoot.applySetting(key, value) }
        onActivated: function(action) { playerRoot.menuAction(action) }
        onClosed: playerRoot.backToVideo()
    }

    PlaybackNotice {
        id: failure
        onRetried: playerRoot.play(playerRoot.lastStartMs)
        onDismissed: playerRoot.goBack()
    }
}
