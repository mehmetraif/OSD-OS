import QtQuick
import Components

// A playlist playing: its videos one after another, from the m3u the backend
// writes for it (prepare(): shuffled there, for a list that is), from the
// video chosen on its page (PLAY FROM HERE), else from where it stopped (it
// asks, as Local Files does), else from its first. mpv's display has ◄ ► for
// the previous and next video. Back opens its menu (PlayerMenu: SUBTITLES,
// LOOP PLAYBACK, Scaling, BROWSE PLAYLISTS, CLOSE VIDEO). Where it stopped is
// kept for next time, and forgotten once the list has played out.
//
// YouTube videos on an online list play as the YouTube module plays them
// (its ADVANCED settings, through yt-dlp); Jellyfin and Emby ones stream from
// their servers; everything on an offline list is a file on the device.
FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    readonly property string playlistId: navParams.playlistId || ""
    readonly property string fromItemId: navParams.fromItemId || ""
    property var playlist: ({})

    // What the backend wrote for the list to play from (prepare()): { file,
    // count, youtube, images, startIndex, resumeIndex, resumeMs }. The same
    // m3u for every start within a session, so the same order.
    property var prepared: ({})

    property bool   overlayVisible:  false
    property int    choiceIndex:     0
    // The resume question's choices: { label, startMs, plPos }.
    property var    choices:         []
    property string errorTitle:      ""
    property string errorMessage:    ""
    property bool   canRetry:        false

    // The video has shown its first position: the loading screen goes.
    property bool   playbackStarted: false
    property int    lastStartMs:     0
    property int    lastKnownPositionMs:  0
    property int    lastKnownDurationMs:  0
    property int    lastKnownPlaylistPos: -1
    // The place in the list the session started at (its playlist-start).
    property int    startedAt:       -1

    property bool   closeToMainMenu: false
    // A setting changed in the menu that the video can't take as it plays
    // (its subtitles): started again where it is as the menu closes.
    property bool   reloadOnClose:   false

    focus: true

    function setting(moduleId, key, fallback) {
        var value = appCore.get_setting(moduleId, key)
        return (value === undefined || value === null || value === "") ? fallback : value
    }

    // The launch for what prepare() wrote, with the settings as they are
    // now: the module's SUBTITLES and LOOP PLAYBACK, the YouTube module's
    // ways for its videos, Local Files' IMAGE DURATION for a still image.
    function specFor(prepared) {
        var loop = setting(moduleRoot.moduleId, "loop_playback", false)
        var subtitles = setting(moduleRoot.moduleId, "subtitles", "Forced Only")
        // mpv's: 0 any, -1 forced only, -2 none.
        var spec = { file: prepared.file, loop: loop === true || loop === "ON",
                     extraArgs: [], subTrack: subtitles === "On" ? 0 : subtitles === "Off" ? -2 : -1,
                     subLangs: [], imageSec: 0.0, images: !!prepared.images }
        if (prepared.youtube && typeof youtubeBackend !== "undefined" && youtubeBackend) {
            // The YouTube module's ADVANCED settings, but for its speed: the
            // list's other videos play as they are, so its do too. Its
            // subtitles are fetched only to be shown.
            var yt = youtubeBackend.playbackSettings()
            yt.subtitles = subtitles === "On" ? "On" : "Off"
            yt.speed = "1x"
            spec.extraArgs = youtubeBackend.playbackArgs(yt)
        }
        if (spec.images) {
            var seconds = parseFloat(setting("com.osdos.local_files", "image_duration", "5"))
            spec.imageSec = isNaN(seconds) ? 5.0 : seconds
        }
        return spec
    }

    function showError(title, message, retry) {
        errorTitle = title
        errorMessage = message
        canRetry = retry
    }

    Component.onCompleted: {
        if (playlistId === "" || !playlistsBackend) {
            goBack()
            return
        }
        playlist = playlistsBackend.playlist(playlistId)

        // This list, still playing behind the menus: full screen where it
        // is, without asking, the same session going on. Left with Browse,
        // it starts again where it was saved, without asking either.
        var note = root.takeBackNote
        var takenBack = fromItemId === "" && note.module === moduleRoot.moduleId && note.params
                        && note.params.playlistId === playlistId
        if (takenBack && root.videoBehind && note.params.prepared) {
            var resumeMs = playlistsBackend.savedPositionMs(playlistId)
            if (mpvController.takeBack(resumeMs / 1000.0)) {
                prepared = note.params.prepared
                startedAt = note.params.plPos
                lastStartMs = resumeMs
                playbackStarted = true
                return
            }
        }

        prepared = playlistsBackend.prepare(playlistId, fromItemId)
        if (!prepared.file || prepared.count === 0) {
            var items = playlist.items || []
            showError("Nothing to play",
                      items.length === 0 ? "Add videos to it first"
                      : playlist.kind === "offline" ? "None of its videos is on the device yet"
                      : "None of its videos can be reached", false)
            return
        }

        if (prepared.startIndex >= 0) {
            play(0, prepared.startIndex)
        } else if (takenBack && (prepared.resumeIndex > 0 || prepared.resumeMs > 0)) {
            play(prepared.resumeMs, prepared.resumeIndex)
        } else if (prepared.resumeIndex > 0 || prepared.resumeMs > 0) {
            choices = [{ label: "Resume video " + (prepared.resumeIndex + 1) + " at " + root.formatTime(prepared.resumeMs),
                         startMs: prepared.resumeMs, plPos: prepared.resumeIndex },
                       { label: "Start from the beginning", startMs: 0, plPos: -1 }]
            choiceIndex = 0
            overlayVisible = true
        } else {
            play(0, -1)
        }
    }

    function play(startMs, plPos) {
        playbackStarted = false
        lastStartMs = startMs
        startedAt = plPos
        var spec = specFor(prepared)
        mpvController.loadAndPlay(spec.file, startMs > 0 ? startMs / 1000.0 : 0.0, 0, spec.subTrack, [],
                                  spec.subLangs, spec.loop, plPos, 0.0, "", false, "", false, [],
                                  spec.imageSec, spec.images, spec.extraArgs)
        // How the main menu takes it back once it plays behind the menus.
        mpvController.noteSession({ module: moduleRoot.moduleId, title: playlist.name || "",
                                    params: { playlistId: playlistId, prepared: prepared, plPos: plPos },
                                    menu: true })
        // A still image never moves mpv's clock, so no first position would
        // end the loading screen.
        if (spec.images)
            playbackStarted = true
    }

    // --- Its menu ---
    function openMenu() {
        playerMenu.actions = [{ label: "Browse " + moduleRoot.moduleName, action: "browse" }]
        playerMenu.open(playlist.name || "")
    }
    function applySetting(key, value) {
        if (key === "loop_playback")
            mpvController.setVideoProperty("loop-playlist", value === true || value === "ON" ? "inf" : "no")
        else if (key === "video_scaling")
            playerMenu.applyScaling(value)
        else
            reloadOnClose = true
    }
    function menuAction(action) {
        if (action === "browse") {
            // As back always did: where it is saved, then the list's page,
            // the video playing on behind it. Ended for the menu (an mpv
            // process has the screen), it is saved already.
            playerMenu.close()
            if (mpvController.videoActive) {
                mpvController.leavePlayerMenu()
            } else {
                // The main menu's first row takes it back from there.
                mpvController.leaveSession()
                goBack()
            }
        } else if (action === "close") {
            closeToMainMenu = true
            playerMenu.close()
            if (mpvController.videoActive)
                mpvController.stop()
            else
                moduleRoot.goBack()
        }
    }
    // Back in the menu: the video full screen again, or started again where
    // it is if it ended for the menu (an mpv process has the screen) or a
    // setting asks for it, with the menu's settings. The same m3u, so the
    // same order.
    function backToVideo() {
        playerMenu.close()
        playerRoot.forceActiveFocus()
        var ended = !mpvController.videoActive
        mpvController.closePlayerMenu()
        if (ended || reloadOnClose) {
            reloadOnClose = false
            play(lastKnownPositionMs, lastKnownPlaylistPos >= 0 ? lastKnownPlaylistPos : startedAt)
        }
    }

    Keys.onPressed: function(event) {
        var back = event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back
        var enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        if (errorMessage !== "") {
            if (back || (enter && !canRetry)) {
                goBack()
            } else if (enter) {
                errorMessage = ""
                play(lastStartMs, startedAt)
            }
            event.accepted = back || enter
        } else if (overlayVisible) {
            if (back) {
                goBack()
            } else if (event.key === Qt.Key_Up) {
                if (choiceIndex > 0) choiceIndex--
            } else if (event.key === Qt.Key_Down) {
                if (choiceIndex < choices.length - 1) choiceIndex++
            } else if (enter) {
                var choice = choices[choiceIndex]
                overlayVisible = false
                play(choice.startMs, choice.plPos)
            }
            event.accepted = true
        } else {
            var keys = {}
            keys[Qt.Key_Escape] = "ESC"; keys[Qt.Key_Back] = "ESC"; keys[Qt.Key_Backspace] = "BS"
            keys[Qt.Key_Up] = "UP"; keys[Qt.Key_Down] = "DOWN"; keys[Qt.Key_Left] = "LEFT"
            keys[Qt.Key_Right] = "RIGHT"; keys[Qt.Key_Space] = "SPACE"
            keys[Qt.Key_Return] = "ENTER"; keys[Qt.Key_Enter] = "ENTER"
            if (keys[event.key] !== undefined) {
                mpvController.sendKey(keys[event.key])
                event.accepted = true
            }
        }
    }

    Connections {
        target: mpvController

        function onPlayerMenuRequested() {
            playerRoot.openMenu()
        }
        function onPositionChanged(ms) {
            if (ms > 0) {
                playerRoot.lastKnownPositionMs = ms
                playerRoot.playbackStarted = true
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

        // mpv ends "eof" only when the last video played to its end: the list
        // has played out, and starts from its first next time. Anything else
        // keeps where it got to.
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            if (reason === "failed" && !playerRoot.playbackStarted) {
                playerMenu.close()
                playerRoot.showError("Playback failed",
                                     prepared.youtube
                                     ? "Its YouTube videos need yt-dlp, up to date, and the network"
                                     : "None of its videos would play", true)
                return
            }
            if (reason === "eof") {
                playlistsBackend.clearPosition(playlistId)
            } else {
                var plPos = playerRoot.lastKnownPlaylistPos >= 0 ? playerRoot.lastKnownPlaylistPos
                                                                 : Math.max(0, playerRoot.startedAt)
                playlistsBackend.savePosition(playlistId, plPos, playerRoot.lastKnownPositionMs || finalPositionMs)
            }
            // Back during an mpv process, which has the screen while it
            // plays: the video ended for this menu, and starts again where
            // it was as the menu closes (backToVideo).
            if (reason === "menu") {
                playerRoot.openMenu()
                return
            }
            playerMenu.close()
            // CLOSE VIDEO leaves the module for the main menu.
            if (playerRoot.closeToMainMenu)
                moduleRoot.goBack()
            else
                playerRoot.goBack()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        // The video shows through its menu.
        visible: !playerMenu.visible

        LoadingScreen {
            anchors.fill: parent
            source: moduleRoot.moduleName
            startMs: playerRoot.lastStartMs
            durationMs: playerRoot.lastKnownDurationMs
            visible: !overlayVisible && !playbackStarted && errorMessage === ""
        }

        PromptScreen {
            visible: errorMessage !== ""
            kind: "notice"
            title: playerRoot.errorTitle
            message: playerRoot.errorMessage
            hint: root.hints.back + ":BACK" + (playerRoot.canRetry ? " " + root.hints.select + ":RETRY" : "")
        }
    }

    PlayerMenu {
        id: playerMenu
        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        iconSource: moduleRoot.moduleIcon
        moduleName: moduleRoot.moduleName
        keys: ["subtitles", "loop_playback", "video_scaling"]
        onSettingChanged: function(key, value) { playerRoot.applySetting(key, value) }
        onActivated: function(action) { playerRoot.menuAction(action) }
        onClosed: playerRoot.backToVideo()
    }

    PromptScreen {
        visible: overlayVisible
        title: "Resume playback?"
        message: playerRoot.playlist.name || ""
        choices: playerRoot.choices
        currentIndex: choiceIndex
    }
}
