import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string filePath:    navParams.filePath || ""
    property string itemTitle:   navParams.title    || ""

    // The start question's choices, each { label, action, startMs, plPos,
    // shuffle } — executed via play()
    property var    choices:             []
    property bool   loopOn:              false
    property string shuffleSetting:      "ask"
    property string resumeSetting:       "ask"
    property string subtitleMode:        "forced"
    property var    subtitleLangs:       []
    property int    imageDurationSec:    5

    // True when playback is images (a standalone image, or a playlist that contains
    // at least one image). Gates the slideshow-redraw mpv script — see MpvController.
    property bool   imageContent:        false

    // mpv subtitle-track flag derived from subtitleMode: 0 = on, -1 = forced only, -2 = off.
    property int    subFlag:             (subtitleMode == "on") ? 0 : ((subtitleMode == "forced") ? -1 : -2)

    // Where the session is (PlayerSession).
    property alias  playbackStarted:      session.started
    property alias  lastStartMs:          session.startMs
    property alias  lastKnownPositionMs:  session.positionMs
    property alias  lastKnownDurationMs:  session.durationMs
    property alias  lastKnownPlaylistPos: session.playlistPos

    // Back during the video opens its menu (PlayerMenu): over the picture,
    // the video playing on behind it, with Transparent Background; without,
    // the video ends for it (mpv has the screen) and starts again where it
    // was as the menu closes. A change the video can't take as it plays (its
    // subtitles) reloads it where it is as the menu closes; CLOSE VIDEO goes
    // back to the main menu. How it was started, for that reload.
    property bool   startedShuffled: false

    focus: true

    // The module's settings the file plays with.
    function readSettings() {
        loopOn        = root.settingOn(moduleRoot.moduleId, "loop_playback", false)
        // As the backend reads them: one saved as On or Off before it had
        // three choices (true or false) is the choice that form is now.
        shuffleSetting = localFilesBackend.shufflePlaybackChoice(appCore.get_setting(moduleRoot.moduleId, "shuffle_playback"))
        subtitleMode  = localFilesBackend.autoSubtitlesChoice(appCore.get_setting(moduleRoot.moduleId, "auto_subtitles"))
        var resRaw    = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"
        resumeSetting = (resRaw === "yes" || resRaw === "no") ? resRaw : "ask"
        var imgDur = parseFloat(appCore.get_setting(moduleRoot.moduleId, "image_duration"))
        imageDurationSec = isNaN(imgDur) ? 5 : imgDur

        // Leaving this as an array since MPV - like most players - expects a *list* of languages
        // to progressively fall back to until a sub track is found.
        // "-" is the value we store for "Any" (i.e. no preference) thats also the manifest default and
        // "Any" option's id. If the user never opened this setting, then get_setting returns nothing,
        // so it will fall back to "-" too. With this, "haven't picked one" will behave the same as "Any":
        // the check below adds nothing to the list and MPV is launched without a --slang preference.
        var subLangString = appCore.get_setting(moduleRoot.moduleId, "sub_lang") || "-"
        subtitleLangs = []
        if (subLangString !== "-") {
            subtitleLangs.push(subLangString)
        }
    }

    // The file as FAVORITES keeps it, as the tree's options put it there.
    readonly property var entry: ({ name: itemTitle, path: filePath, isFolder: false })

    function openMenu() {
        playerMenu.actions = menuActions()
        playerMenu.open(itemTitle.replace(/\.[^.\/]+$/, ""))
    }
    function menuActions() {
        return EntryFavorites.choices(EntryFavorites.isFavorite(appCore, moduleRoot.moduleId, entry),
                                      EntryFavorites.isAtStartup(appCore, moduleRoot.moduleId, entry))
               .concat([{ label: "Browse " + moduleRoot.moduleName, action: "browse" }])
    }
    // A setting changed in the menu: loop and the Scaling at once, the
    // subtitles as the menu closes.
    function applySetting(key, value) {
        if (key === "loop_playback") {
            loopOn = value === true || value === "ON"
            mpvController.setVideoProperty("loop-playlist", loopOn ? "inf" : "no")
        } else if (key === "video_scaling") {
            playerMenu.applyScaling(value)
        } else {
            session.reloadOnClose = true
        }
    }
    function menuAction(action) {
        if (session.menuAction(action))
            return
        if (action === "favorite")
            EntryFavorites.toggleFavorite(appCore, moduleRoot.moduleId, entry)
        else if (action === "startup")
            EntryFavorites.toggleStartup(appCore, moduleRoot.moduleId, entry)
        playerMenu.actions = menuActions()
        playerMenu.refresh()
    }
    function backToVideo() {
        session.backToVideo()
    }
    // The session started again where it is. A shuffled playlist's places
    // are gone with its order (mpv's shuffle, made again at each start), so
    // it starts shuffled afresh, as it does taken back from Browse.
    function playAgain() {
        if (startedShuffled)
            play(0, -1, true)
        else
            play(lastKnownPositionMs || lastStartMs, lastKnownPlaylistPos, false)
    }

    Keys.onPressed: function(event) {
        // Under the start question only its Ctrl chords come here: on to
        // Main.qml (Ctrl+Q), not to mpv.
        if (startPrompt.visible)
            return
        if (session.forwardKey(event)) {
            return
        } else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_ChannelUp) {
            // A TV remote's channel keys, or its page keys: the folder's
            // next or previous film, once per press however long it is
            // held, and not under the video's menu (which passes on the
            // keys it doesn't take).
            if (!event.isAutoRepeat && !playerMenu.visible)
                stepFilm(1)
            event.accepted = true
        } else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_ChannelDown) {
            if (!event.isAutoRepeat && !playerMenu.visible)
                stepFilm(-1)
            event.accepted = true
        }
    }
    Keys.onReleased: function(event) { session.releaseKey(event) }

    // The folder's next (1) or previous (-1) film, as a TV's programme keys
    // change the channel: round the folder's end, this one saved where it got
    // to, the next one where it was left, without asking (Resume Playback's
    // Never apart), and on RECENTLY WATCHED as the tree puts it. A playlist
    // steps through its own entries, in mpv (mpv-media-keys.lua, round the
    // list's end); an image on its own isn't stepped from.
    function stepFilm(delta) {
        if (isPlaylist(filePath)) {
            mpvController.sendKey(delta > 0 ? "CHANNEL_UP" : "CHANNEL_DOWN")
            return
        }
        if (isImage(filePath))
            return
        var folder = filePath.substring(0, filePath.lastIndexOf("/"))
        // The folder as it was read when the film started (onCompleted), off
        // the app's thread; read now only if a key came before that was in.
        var listed = localFilesBackend.requestEntries(folder)
        if (listed === undefined || listed.length === 0)
            listed = localFilesBackend.entries(folder)
        var films = listed.filter(function(e) {
            return !e.isFolder && e.path.substring(0, folder.length + 1) === folder + "/"
                   && !isImage(e.path) && !isPlaylist(e.path)
        })
        var at = -1
        for (var i = 0; i < films.length; ++i) {
            if (films[i].path === filePath)
                at = i
        }
        if (at < 0 || films.length < 2)
            return
        var next = films[(at + delta + films.length) % films.length]
        rememberPosition(lastKnownPositionMs, lastKnownDurationMs, lastKnownPlaylistPos, "stopped")
        appCore.add_to_list(moduleRoot.moduleId, "recent",
                            { name: next.name, path: next.path, isFolder: false }, 30)
        filePath = next.path
        itemTitle = next.name
        lastKnownPositionMs  = 0
        lastKnownDurationMs  = 0
        lastKnownPlaylistPos = -1
        imageContent = false
        var held = resumeSetting === "no" ? ({}) : localFilesBackend.getSavedPosition(filePath)
        play(held.pos || 0, -1, false)
    }

    // Where the file got to, kept for Resume, or forgotten once it is as good
    // as watched. A playlist's per-item duration can't tell "finished the
    // list" from "finished one video of it", but the exit reason can: mpv
    // only ends with "eof" when the final item played to its end (a quit
    // mid-list leaves a trailing quit/stop end-file event). A completed
    // playlist clears its resume point like a completed single video;
    // anything else saves item + timecode. A single file: cleared near its
    // end, saved otherwise. Images carry no resume position, so they never
    // write history. A shuffled playlist's places are in an order only that
    // session had: it leaves the one kept for the file as it was.
    function rememberPosition(pos, dur, plPos, reason) {
        if (isPlaylist(filePath)) {
            if (startedShuffled)
                return
            if (reason === "eof")
                localFilesBackend.clearPosition(filePath)
            else if (pos > 0 || plPos >= 0)
                localFilesBackend.savePosition(filePath, pos, plPos)
        } else if (!isImage(filePath)) {
            if (dur > 0 && pos >= dur * 0.95)
                localFilesBackend.clearPosition(filePath)
            else if (pos > 5000)
                localFilesBackend.savePosition(filePath, pos, -1)
        }
    }

    PlayerSession {
        id: session
        player: playerRoot
        menu: playerMenu
        followsPlaylist: true
        onMenuRequested: playerRoot.openMenu()
        onRestartRequested: {
            playerRoot.readSettings()
            playerRoot.playAgain()
        }
    }

    Connections {
        target: mpvController

        // mpv exited for any reason ("eof"/"stopped"/"failed"/"menu"). Local Files
        // has no autoplay-next or transcode-retry, so every exit is handled the same
        // way: save/clear the resume position, a failure saying why (RETRY plays it
        // again where it got to), any other end the session's (PlayerSession.end():
        // its menu, or back to the menus).
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            rememberPosition(lastKnownPositionMs || finalPositionMs,
                             lastKnownDurationMs || finalDurationMs,
                             lastKnownPlaylistPos, reason)
            if (reason === "failed") {
                playerMenu.close()
                failure.showMpv(true)
                return
            }
            session.end(reason)
        }
    }

    Component.onCompleted: {
        // Nothing to play: back out, a tick later (the router isn't
        // listening for goBack yet), rather than stand on a loading screen
        // with no video to end it.
        if (filePath === "") {
            Qt.callLater(playerRoot.goBack)
            return
        }
        // The film's folder, for CH+ and CH−, read in the background now
        // rather than on the app's thread at each of those keys.
        if (!isPlaylist(filePath) && !isImage(filePath))
            localFilesBackend.requestEntries(filePath.substring(0, filePath.lastIndexOf("/")))
        readSettings()

        imageContent = isImage(filePath) ||
                       (isPlaylist(filePath) && localFilesBackend.playlistContainsImages(filePath))

        // This file, still playing behind the menus: it goes on full screen
        // where it is, without asking, the same session (takeBack(): what
        // back saved can't say where it is, as nothing in its first seconds
        // or near its end is saved). Left with Browse, it starts again where
        // it was saved, at the playlist's video then; a shuffled playlist's
        // places are gone with its order, so it starts shuffled afresh.
        var note = root.takeBackNote
        if (note.module === moduleRoot.moduleId && note.params && note.params.filePath === filePath) {
            if (root.videoBehind && mpvController.takeBack()) {
                startedShuffled = !!note.shuffle
                playbackStarted = true
                return
            }
            var held = localFilesBackend.getSavedPosition(filePath)
            if (note.shuffle)
                play(0, -1, true)
            else
                play(held.pos || 0, held.plPos !== undefined && held.plPos !== null ? held.plPos : -1, false)
            return
        }

        // Shuffle only applies to playlists; "Always" wins over resume: a shuffled
        // playlist starts fresh & random; resume position (a sequential item index)
        // is meaningless once order is randomized.
        var canShuffle = isPlaylist(filePath)
        if (canShuffle && shuffleSetting === "yes") {
            play(0, -1, true)
            return
        }

        // A standalone image has no meaningful playback position, so it bypasses
        // resume entirely (no saved-position lookup, no "RESUME PLAYBACK?" overlay).
        // Images inside a playlist still resume via the playlist's item index below.
        if (!canShuffle && isImage(filePath)) {
            play(0, -1, false)
            return
        }

        // The favourite played at startup begins without asking, where
        // Settings' STARTUP FROM says (a playlist in order, unless shuffle is
        // always on, above).
        if (navParams.startup) {
            var held = appCore.get_setting("", "startup_from") === "Beginning"
                       ? ({}) : localFilesBackend.getSavedPosition(filePath)
            var heldPos = held.pos || 0
            var heldPl = held.plPos !== undefined && held.plPos !== null ? held.plPos : -1
            play(heldPos, heldPos > 0 || heldPl > 0 ? heldPl : -1, false)
            return
        }

        var askShuffle = canShuffle && shuffleSetting === "ask"

        var savedPos = 0
        var savedPl  = -1
        if (resumeSetting !== "no") {
            var saved = localFilesBackend.getSavedPosition(filePath)
            savedPos  = saved.pos || 0
            savedPl   = (saved.plPos !== undefined && saved.plPos !== null) ? saved.plPos : -1
        }

        // A place to resume: a time, or a playlist's video past its first,
        // which a list stopped as a video began (a slideshow's, whose
        // pictures have no time) saves with a time of 0.
        var resumable = savedPos > 0 || savedPl > 0
        if (resumeSetting === "yes" && !askShuffle) {
            play(resumable ? savedPos : 0, resumable ? savedPl : -1, false)
            return
        }

        var opts = []
        if (resumable) {
            opts.push({ label: savedPl >= 0
                            ? "Resume video " + (savedPl + 1) + " at " + root.formatTime(savedPos)
                            : "Resume from " + root.formatTime(savedPos),
                        action: "resume", startMs: savedPos, plPos: savedPl, shuffle: false })
            if (resumeSetting === "ask")
                opts.push({ label: "Start from the beginning", action: "start", startMs: 0, plPos: -1, shuffle: false })
        } else if (askShuffle) {
            opts.push({ label: "Play in order", action: "start", startMs: 0, plPos: -1, shuffle: false })
        }
        if (askShuffle)
            opts.push({ label: "Shuffle", action: "shuffle", startMs: 0, plPos: -1, shuffle: true })

        if (opts.length > 1) {
            choices = opts
            startPrompt.open()
        } else {
            play(0, -1, false)
        }
    }

    function play(startMs, plPos, shuffle) {
        playbackStarted = false
        session.begin(startMs, plPos)
        mpvController.loadAndPlay({ url: filePath, startSeconds: startMs > 0 ? startMs / 1000.0 : 0.0,
                                    subTrack: subFlag, subLangs: subtitleLangs, loop: loopOn,
                                    playlistStart: plPos, shuffle: shuffle,
                                    imageDurationSec: imageDurationSec, imageContent: imageContent })
        // How the main menu takes it back once it plays behind the menus, and
        // how it was started, which taking it back repeats.
        mpvController.noteSession({ module: moduleRoot.moduleId,
                                    title: itemTitle.replace(/\.[^.\/]+$/, ""),
                                    params: { filePath: filePath, title: itemTitle },
                                    plPos: plPos, shuffle: shuffle, menu: true })
        startedShuffled = shuffle
        // A still image never moves mpv's clock (its position stays 0), so no
        // first position comes to end the loading screen: it is up with mpv.
        if (imageContent)
            playbackStarted = true
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        // The video shows through its menu.
        visible: !playerMenu.visible

        // While mpv starts (before its picture takes over), and while it
        // starts again after its menu.
        LoadingScreen {
            anchors.fill: parent
            source: moduleRoot.moduleName
            startMs: playerRoot.lastStartMs
            durationMs: playerRoot.lastKnownDurationMs
            visible: !startPrompt.visible && !playbackStarted && !failure.visible
        }
    }

    PlayerMenu {
        id: playerMenu
        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        iconSource: moduleRoot.moduleIcon
        moduleName: moduleRoot.moduleName
        keys: ["auto_subtitles", "sub_lang", "loop_playback", "video_scaling"]
        onSettingChanged: function(key, value) { playerRoot.applySetting(key, value) }
        onActivated: function(action) { playerRoot.menuAction(action) }
        onClosed: playerRoot.backToVideo()
    }

    // Where to start, or in what order, while Resume Playback or Shuffle
    // Playback asks: back leaves.
    ChoiceOverlay {
        id: startPrompt
        anchors.fill: parent
        // Whenever a Shuffle choice is offered, the more general question.
        promptText: playerRoot.choices.some(function(c) { return c.shuffle }) ? "Start playback?" : "Resume playback?"
        subtitleText: itemTitle.replace(/\.[^.\/]+$/, "")
        choices: playerRoot.choices
        onClosed: playerRoot.forceActiveFocus()
        onActivated: function(action) {
            var choice = playerRoot.choices.filter(function(c) { return c.action === action })[0]
            playerRoot.play(choice.startMs, choice.plPos, choice.shuffle)
        }
        onCanceled: playerRoot.goBack()
    }

    PlaybackNotice {
        id: failure
        onRetried: playerRoot.playAgain()
        // CLOSE VIDEO leaves the module for the main menu.
        onDismissed: session.leave()
    }

    function isPlaylist(path) {
        return localFilesBackend.isPlaylist(path)
    }

    function isImage(path) {
        return localFilesBackend.isImage(path)
    }

}
