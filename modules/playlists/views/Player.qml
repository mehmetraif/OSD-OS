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

    // The resume question's choices: { label, action, startMs, plPos }.
    property var    choices:         []

    // Where the session is (PlayerSession).
    property alias  playbackStarted:      session.started
    property alias  lastStartMs:          session.startMs
    property alias  lastKnownPositionMs:  session.positionMs
    property alias  lastKnownDurationMs:  session.durationMs
    property alias  lastKnownPlaylistPos: session.playlistPos
    // The place in the list the session started at (its playlist-start).
    property int    startedAt:       -1

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

    // What stands in the way, in the standard window (PlaybackNotice), with
    // what there is to add as it is written (mpv's own line).
    function showError(title, message, retry, detail) {
        failure.title = title
        failure.show(message, detail || "", retry)
    }

    Component.onCompleted: {
        if (playlistId === "" || !playlistsBackend) {
            // A turn later: the router isn't listening yet.
            Qt.callLater(playerRoot.goBack)
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
            if (mpvController.takeBack()) {
                prepared = note.params.prepared
                startedAt = note.params.plPos
                lastStartMs = playlistsBackend.savedPositionMs(playlistId)
                playbackStarted = true
                return
            }
        }

        prepared = playlistsBackend.prepare(playlistId, fromItemId)
        if (prepared.error) {
            showError("Could not play", prepared.error, false)
            return
        }
        // PLAY FROM HERE on a video that can't play yet (prepare() leaves
        // it out): why, rather than the list from its first video.
        if (fromItemId !== "" && prepared.startIndex < 0) {
            showError("Could not play", whyNotPlayable(fromItemId), false)
            return
        }
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
                         action: "resume", startMs: prepared.resumeMs, plPos: prepared.resumeIndex },
                       { label: "Start from the beginning", action: "start", startMs: 0, plPos: -1 }]
            resumePrompt.open()
        } else {
            play(0, -1)
        }
    }

    // Why one of the list's videos can't play, from its state on the list's
    // page.
    function whyNotPlayable(itemId) {
        var item = (playlist.items || []).filter(function(i) { return i.id === itemId })[0] || {}
        if (item.state === "missing")
            return item.reason === "signed out" ? "Its server is signed out" : "Its file is not there"
        if (item.state === "queued" || item.state === "downloading")
            return "It is not on the device yet"
        if (item.state === "failed")
            return "Its download failed"
        return "It can't be reached"
    }

    function play(startMs, plPos) {
        playbackStarted = false
        session.begin(startMs, plPos)
        startedAt = plPos
        var spec = specFor(prepared)
        mpvController.loadAndPlay({ url: spec.file, startSeconds: startMs > 0 ? startMs / 1000.0 : 0.0,
                                    subTrack: spec.subTrack, subLangs: spec.subLangs, loop: spec.loop,
                                    playlistStart: plPos, imageDurationSec: spec.imageSec,
                                    imageContent: spec.images, extraArgs: spec.extraArgs })
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
    // A setting changed in the menu: loop and the Scaling at once; one the
    // video can't take as it plays (its subtitles) starts it again where it
    // is as the menu closes.
    function applySetting(key, value) {
        if (key === "loop_playback")
            mpvController.setVideoProperty("loop-playlist", value === true || value === "ON" ? "inf" : "no")
        else if (key === "video_scaling")
            playerMenu.applyScaling(value)
        else
            session.reloadOnClose = true
    }
    function menuAction(action) {
        session.menuAction(action)
    }
    function backToVideo() {
        session.backToVideo()
    }

    Keys.onPressed: function(event) {
        // Under the resume question only its Ctrl chords come here: on to
        // Main.qml (Ctrl+Q), not to mpv.
        if (resumePrompt.visible)
            return
        // With a TV remote's channel keys, or its page keys: the list's next
        // or previous video (mpv-media-keys.lua, round the list's end).
        session.forwardKey(event, true)
    }
    Keys.onReleased: function(event) { session.releaseKey(event) }

    // Started again after the menu with the menu's settings, from the same
    // m3u, so in the same order.
    PlayerSession {
        id: session
        player: playerRoot
        menu: playerMenu
        followsPlaylist: true
        onMenuRequested: playerRoot.openMenu()
        onRestartRequested: playerRoot.play(playerRoot.lastKnownPositionMs,
                                            playerRoot.lastKnownPlaylistPos >= 0 ? playerRoot.lastKnownPlaylistPos
                                                                                 : playerRoot.startedAt)
    }

    Connections {
        target: mpvController

        // mpv ends "eof" only when the last video played to its end: the list
        // has played out, and starts from its first next time. Anything else
        // keeps where it got to.
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            if (reason === "failed" && !playerRoot.playbackStarted) {
                playerMenu.close()
                playerRoot.showError("Playback failed",
                                     mpvController.playerFailed ? mpvController.lastError
                                     : prepared.youtube
                                     ? "Its YouTube videos need yt-dlp, up to date, and the network"
                                     : "None of its videos would play", true, mpvController.lastErrorDetail)
                return
            }
            // Failed once a picture had shown (its last video would not
            // play, a stream cut off): back as from any end, nothing on the
            // screen to say why, so the log does: the list, and which of
            // its videos.
            if (reason === "failed")
                mpvController.logFailure("Playlists Player", "\"" + (playlist.name || "") + "\""
                                         + (playerRoot.lastKnownPlaylistPos >= 0
                                            ? ", video " + (playerRoot.lastKnownPlaylistPos + 1) : ""))
            if (reason === "eof") {
                playlistsBackend.clearPosition(playlistId)
            } else {
                var plPos = playerRoot.lastKnownPlaylistPos >= 0 ? playerRoot.lastKnownPlaylistPos
                                                                 : Math.max(0, playerRoot.startedAt)
                playlistsBackend.savePosition(playlistId, plPos, playerRoot.lastKnownPositionMs || finalPositionMs)
            }
            session.end(reason)
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
            visible: !resumePrompt.visible && !playbackStarted && !failure.visible
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

    // Where to start a list that stopped part-way: back leaves.
    ChoiceOverlay {
        id: resumePrompt
        anchors.fill: parent
        promptText: "Resume playback?"
        subtitleText: playerRoot.playlist.name || ""
        choices: playerRoot.choices
        onClosed: playerRoot.forceActiveFocus()
        onActivated: function(action) {
            var choice = playerRoot.choices.filter(function(c) { return c.action === action })[0]
            playerRoot.play(choice.startMs, choice.plPos)
        }
        onCanceled: playerRoot.goBack()
    }

    PlaybackNotice {
        id: failure
        onRetried: playerRoot.play(playerRoot.lastStartMs, playerRoot.startedAt)
        onDismissed: playerRoot.goBack()
    }
}
