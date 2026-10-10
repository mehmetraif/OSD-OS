import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string filePath:    navParams.filePath || ""
    property string itemTitle:   navParams.title    || ""

    property bool   overlayVisible:      false
    property int    choiceIndex:         0
    // Overlay choices, each { label, startMs, plPos, shuffle } — executed via play()
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

    // The video has shown its first position: the loading screen goes.
    property bool   playbackStarted:      false
    // Where the last start began, for the loading screen's counter.
    property int    lastStartMs:          0

    // Track last non-null values during playback for robust save on exit
    property int    lastKnownPositionMs:  0
    property int    lastKnownDurationMs:  0
    property int    lastKnownPlaylistPos: -1

    // Back during the video opens its menu (PlayerMenu): over the picture,
    // the video playing on behind it, with Transparent Background; without,
    // the video ends for it (mpv has the screen) and starts again where it
    // was as the menu closes. A change the video can't take as it plays (its
    // subtitles) reloads it where it is as the menu closes; CLOSE VIDEO goes
    // back to the main menu. How it was started, for that reload.
    property bool   reloadOnClose:   false
    property bool   closeToMainMenu: false
    property bool   startedShuffled: false

    focus: true

    // The module's settings the file plays with.
    function readSettings() {
        loopOn        = !!appCore.get_setting(moduleRoot.moduleId, "loop_playback")
        // Some fancy logic to honor the old boolean settings until they get updated to the new format
        var shufRaw   = appCore.get_setting(moduleRoot.moduleId, "shuffle_playback")
        shuffleSetting = (typeof shufRaw === "boolean") ? (shufRaw ? "yes" : "no") : (shufRaw || "ask")
        var autoSubs  = appCore.get_setting(moduleRoot.moduleId, "auto_subtitles")
        subtitleMode  = (typeof autoSubs === "boolean") ? ((autoSubs === true) ? "on" : "forced") : (autoSubs || "forced")
        var resRaw    = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"
        resumeSetting = (resRaw === "yes" || resRaw === "no") ? resRaw : "ask"
        var imgDur = parseFloat(appCore.get_setting(moduleRoot.moduleId, "image_duration"))
        imageDurationSec = isNaN(imgDur) ? 5 : imgDur

        // Leaving this as an array since MPV - like most players - expects a *list* of languages
        // to progressively fall back to until a sub track is found. If we ever switch back to
        // selecting a list in Settings, the change to support them all will be considerably simpler.
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
        var favorite = appCore.list_contains(moduleRoot.moduleId, "favorites", filePath)
        var startup = appCore.get_setting("", "startup_favorite")
        var atStartup = !!startup && startup.module === moduleRoot.moduleId && startup.path === filePath
        return [{ label: favorite ? "Remove from Favorites" : "Add to Favorites", action: "favorite" },
                { label: atStartup ? "Don't Play at Startup" : "Play at Startup", action: "startup" },
                { label: "Browse " + moduleRoot.moduleName, action: "browse" }]
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
            reloadOnClose = true
        }
    }
    function menuAction(action) {
        var favorite = appCore.list_contains(moduleRoot.moduleId, "favorites", filePath)
        var startup = appCore.get_setting("", "startup_favorite")
        var atStartup = !!startup && startup.module === moduleRoot.moduleId && startup.path === filePath
        if (action === "favorite") {
            if (favorite) {
                appCore.remove_from_list(moduleRoot.moduleId, "favorites", filePath)
                // The startup favourite is one of the favourites.
                if (atStartup)
                    appCore.save_setting("", "startup_favorite", "")
            } else {
                appCore.add_to_list(moduleRoot.moduleId, "favorites", entry, 100)
            }
        } else if (action === "startup") {
            if (atStartup) {
                appCore.save_setting("", "startup_favorite", "")
            } else {
                // Played at startup from FAVORITES, so it goes there too.
                if (!favorite)
                    appCore.add_to_list(moduleRoot.moduleId, "favorites", entry, 100)
                appCore.save_setting("", "startup_favorite",
                                     { module: moduleRoot.moduleId, path: filePath, name: itemTitle })
            }
        } else if (action === "browse") {
            // As back always did: saved where it is, then the module's tree,
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
            return
        } else if (action === "close") {
            closeToMainMenu = true
            playerMenu.close()
            if (mpvController.videoActive)
                mpvController.stop()
            else
                moduleRoot.goBack()
            return
        }
        playerMenu.actions = menuActions()
        playerMenu.refresh()
    }
    // Back in the menu: the video full screen again, reloaded where it is if
    // a setting asks for it, or started again there if it ended for the menu.
    function backToVideo() {
        playerMenu.close()
        playerRoot.forceActiveFocus()
        var ended = !mpvController.videoActive
        mpvController.closePlayerMenu()
        if (reloadOnClose || ended) {
            reloadOnClose = false
            readSettings()
            play(lastKnownPositionMs, lastKnownPlaylistPos, startedShuffled)
        }
    }

    Keys.onPressed: function(event) {
        if (overlayVisible) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                if (choiceIndex > 0) choiceIndex--
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                if (choiceIndex < choices.length - 1) choiceIndex++
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                var choice = choices[choiceIndex]
                overlayVisible = false
                play(choice.startMs, choice.plPos, choice.shuffle)
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

        // mpv exited for any reason ("eof"/"stopped"/"failed"). Local Files has no
        // autoplay-next or transcode-retry, so every exit is handled the same way:
        // save/clear the resume position and return to the menu. Handling the single
        // playbackEnded signal here is what keeps the app from freezing on a natural
        // end-of-file (the original bug was a missing per-reason handler).
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            var pos   = lastKnownPositionMs  || finalPositionMs
            var dur   = lastKnownDurationMs  || finalDurationMs
            var plPos = lastKnownPlaylistPos

            if (isPlaylist(filePath)) {
                // The per-item duration can't tell "finished the list" from
                // "finished one video of it", but the exit reason can: mpv only
                // ends with "eof" when the final item played to its end (a quit
                // mid-list leaves a trailing quit/stop end-file event). A
                // completed playlist clears its resume point like a completed
                // single video; anything else saves item + timecode.
                if (reason === "eof")
                    localFilesBackend.clearPosition(filePath)
                else if (pos > 0 || plPos >= 0)
                    localFilesBackend.savePosition(filePath, pos, plPos)
            } else if (!isImage(filePath)) {
                // Single file: clear if near completion, save otherwise.
                // Images carry no resume position, so they never write history.
                if (dur > 0 && pos >= dur * 0.95)
                    localFilesBackend.clearPosition(filePath)
                else if (pos > 5000)
                    localFilesBackend.savePosition(filePath, pos, -1)
            }
            // Back during an mpv process, which has the screen while it
            // plays: the video ended for this menu, and starts again where
            // it was as the menu closes (backToVideo).
            if (reason === "menu") {
                openMenu()
                return
            }
            playerMenu.close()
            // CLOSE VIDEO leaves the module for the main menu.
            if (closeToMainMenu)
                moduleRoot.goBack()
            else
                goBack()
        }
    }

    Component.onCompleted: {
        if (filePath === "") return
        readSettings()

        imageContent = isImage(filePath) ||
                       (isPlaylist(filePath) && localFilesBackend.playlistContainsImages(filePath))

        // This file, still playing behind the menus: it goes on full screen
        // where it is, without asking, played as it was started so that it is
        // the same session (back saved where it got to). Left with Browse, it
        // starts again where it was saved, at the playlist's video then; a
        // shuffled playlist's places are gone with its order, so it starts
        // shuffled afresh.
        var note = root.takeBackNote
        if (note.module === moduleRoot.moduleId && note.params && note.params.filePath === filePath) {
            var held = localFilesBackend.getSavedPosition(filePath)
            if (root.videoBehind)
                play(held.pos || 0, note.plPos, note.shuffle)
            else if (note.shuffle)
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
            play(heldPos, heldPos > 0 && held.plPos !== undefined ? held.plPos : -1, false)
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

        if (resumeSetting === "yes" && !askShuffle) {
            play(savedPos > 0 ? savedPos : 0, savedPos > 0 ? savedPl : -1, false)
            return
        }

        var opts = []
        if (savedPos > 0) {
            opts.push({ label: savedPl >= 0
                            ? "Resume video " + (savedPl + 1) + " at " + root.formatTime(savedPos)
                            : "Resume from " + root.formatTime(savedPos),
                        startMs: savedPos, plPos: savedPl, shuffle: false })
            if (resumeSetting === "ask")
                opts.push({ label: "Start from the beginning", startMs: 0, plPos: -1, shuffle: false })
        } else if (askShuffle) {
            opts.push({ label: "Play in order", startMs: 0, plPos: -1, shuffle: false })
        }
        if (askShuffle)
            opts.push({ label: "Shuffle", startMs: 0, plPos: -1, shuffle: true })

        if (opts.length > 1) {
            choices        = opts
            choiceIndex    = 0
            overlayVisible = true
        } else {
            play(0, -1, false)
        }
    }

    function play(startMs, plPos, shuffle) {
        playbackStarted = false
        lastStartMs = startMs
        mpvController.loadAndPlay(filePath, startMs > 0 ? startMs / 1000.0 : 0.0, 0, subFlag, [], subtitleLangs, loopOn, plPos, 0.0, "", false, "", shuffle, [], imageDurationSec, imageContent)
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
            visible: !overlayVisible && !playbackStarted
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

    PromptScreen {
        visible: overlayVisible
        // Whenever a Shuffle choice is offered, the more general question.
        title: playerRoot.choices.some(function(c) { return c.shuffle }) ? "Start playback?" : "Resume playback?"
        message: itemTitle.replace(/\.[^.\/]+$/, "")
        choices: playerRoot.choices
        currentIndex: choiceIndex
    }

    function isPlaylist(path) {
        return localFilesBackend.isPlaylist(path)
    }

    function isImage(path) {
        return localFilesBackend.isImage(path)
    }

}
