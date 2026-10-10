import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property var    item:     navParams.item || ({})
    property string videoUrl: item.url || ""
    property string videoId:  item.videoId || ""

    property bool   overlayVisible:   false
    property bool   playbackStarted:  false
    property int    savedPositionMs:  0
    property int    choiceIndex:      0
    property string errorMessage:     ""
    property var    ytdlArgs:         []
    // Subtitles, from ADVANCED: -2 none, 0 the Subtitle Language's (subLangs).
    property int    subTrack:         -2
    property var    subLangs:         []
    property int    lastStartMs:      0   // what the last attempt started from, for retry

    // Track last non-null values during playback for robust save on exit
    property int    lastKnownPositionMs: 0
    property int    lastKnownDurationMs: 0

    // Back during the video opens its menu (PlayerMenu): over the picture,
    // the video playing on behind it, with Transparent Background; without,
    // the video ends for it (mpv has the screen) and starts again where it
    // was as the menu closes. A change the video can't take as it plays (its
    // format, its audio, its subtitles) reloads it where it is as the menu
    // closes; CLOSE VIDEO goes back to the main menu.
    property bool   reloadOnClose:   false
    property bool   closeToMainMenu: false

    focus: true

    function play(startMs) {
        overlayVisible = false
        lastStartMs = startMs
        mpvController.loadAndPlay(videoUrl, startMs / 1000.0, 0, subTrack, [], subLangs, false, -1, 0.0, "", false, "", false, [], 0.0, false, ytdlArgs)
        // How the main menu takes it back once it plays behind the menus, and
        // that back opens this player's menu.
        mpvController.noteSession({ module: moduleRoot.moduleId, title: item.title || "",
                                    params: { item: item }, menu: true })
    }

    // The ADVANCED settings the video plays with, each at its manifest
    // default until set.
    function setting(key, fallback) {
        var value = appCore.get_setting(moduleRoot.moduleId, key)
        return (value === undefined || value === null || value === "") ? fallback : value
    }
    function readSettings() {
        var subtitles = setting("subtitles", "Off")
        var subtitleLanguage = setting("subtitle_language", "en")
        ytdlArgs = youtubeBackend.playbackArgs({
            resolution: setting("playback_resolution", "480p"),
            codec: setting("video_codec", "H.264"),
            maxFrameRate: setting("max_frame_rate", "Any"),
            audioLanguage: setting("audio_language", "original"),
            subtitles: subtitles,
            subtitleLanguage: subtitleLanguage,
            speed: setting("playback_speed", "1x")
        })
        subTrack = subtitles !== "Off" ? 0 : -2
        subLangs = subtitles !== "Off" ? [subtitleLanguage] : []
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
        var favorite = appCore.list_contains(moduleRoot.moduleId, "favorites", item.path)
        var later = youtubeBackend.isInWatchLater(videoId)
        return [{ label: favorite ? "Remove from Favorites" : "Add to Favorites", action: "favorite" },
                { label: atStartup() ? "Don't Play at Startup" : "Play at Startup", action: "startup" },
                { label: later ? "Remove from Watch Later" : "Save to Watch Later", action: "later" },
                { label: "Browse " + moduleRoot.moduleName, action: "browse" }]
    }
    // This video is the favourite played at startup.
    function atStartup() {
        var startup = appCore.get_setting("", "startup_favorite")
        return !!startup && startup.module === moduleRoot.moduleId && startup.path === item.path
    }
    // A setting changed in the menu: the speed and the Scaling at once, the
    // rest as the menu closes.
    function applySetting(key, value) {
        if (key === "playback_speed")
            mpvController.setVideoProperty("speed", parseFloat(value) || 1.0)
        else if (key === "video_scaling")
            playerMenu.applyScaling(value)
        else
            reloadOnClose = true
    }
    function menuAction(action) {
        var favorite = appCore.list_contains(moduleRoot.moduleId, "favorites", item.path)
        if (action === "favorite") {
            if (favorite) {
                appCore.remove_from_list(moduleRoot.moduleId, "favorites", item.path)
                // The startup favourite is one of the favourites.
                if (atStartup())
                    appCore.save_setting("", "startup_favorite", "")
            } else {
                appCore.add_to_list(moduleRoot.moduleId, "favorites", entry, 100)
            }
        } else if (action === "startup") {
            if (atStartup()) {
                appCore.save_setting("", "startup_favorite", "")
            } else {
                // Played at startup from FAVORITES, so it goes there too.
                if (!favorite)
                    appCore.add_to_list(moduleRoot.moduleId, "favorites", entry, 100)
                appCore.save_setting("", "startup_favorite",
                                     { module: moduleRoot.moduleId, path: item.path, name: item.title || item.name || "" })
            }
        } else if (action === "later") {
            if (youtubeBackend.isInWatchLater(videoId))
                youtubeBackend.removeFromWatchLater(videoId)
            else
                youtubeBackend.addToWatchLater(videoId, item.title || "", item.channelName || "")
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
            playbackStarted = false
            play(lastKnownPositionMs)
        }
    }

    Keys.onPressed: function(event) {
        if (errorMessage !== "") {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                errorMessage = ""
                play(lastStartMs)
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
                play(choiceIndex === 0 ? savedPositionMs : 0)
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
                playerRoot.playbackStarted = true
                playerRoot.lastKnownPositionMs = ms
            }
        }
        function onDurationChanged(ms) {
            if (ms > 0) playerRoot.lastKnownDurationMs = ms
        }

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            // yt-dlp missing/outdated or an unplayable stream surfaces as mpv exit
            // code 2 before any position event — show the error instead of leaving.
            if (reason === "failed" && !playbackStarted) {
                playerRoot.errorMessage = "Please check that yt-dlp is installed and up to date"
                return
            }
            var pos = lastKnownPositionMs || finalPositionMs
            var dur = lastKnownDurationMs || finalDurationMs
            // Completed videos stay in history with pos 0: they list under
            // History but never trigger the resume prompt.
            if (dur > 0 && pos >= dur * 0.95)
                youtubeBackend.savePosition(videoId, 0, item.title || "", item.channelName || "")
            else if (pos > 5000)
                youtubeBackend.savePosition(videoId, pos, item.title || "", item.channelName || "")
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
        if (videoUrl === "") {
            goBack()
            return
        }
        readSettings()

        var resumeSetting = setting("resume_playback", "Ask")
        var saved = youtubeBackend.getSavedPosition(videoId)
        var savedPos = saved.pos || 0
        // This video, still playing behind the menus: it goes on full screen
        // where it is, without asking (back saved where it got to); left with
        // Browse, it starts again where it was saved.
        var note = root.takeBackNote
        var behind = note.module === moduleRoot.moduleId && note.params && note.params.item
                     && note.params.item.videoId === videoId

        // The favourite played at startup begins without asking, where
        // Settings' STARTUP FROM says.
        var startupFrom = navParams.startup ? (appCore.get_setting("", "startup_from") || "Resume") : ""

        if (startupFrom === "Beginning") {
            play(0)
        } else if (resumeSetting === "Always" || behind || startupFrom !== "") {
            play(savedPos)
        } else if (savedPos > 0) {
            savedPositionMs = savedPos
            overlayVisible = true
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
        message: item.title || ""
        choices: [
            "Resume from " + root.formatTime(savedPositionMs),
            "Start from the beginning"
        ]
        currentIndex: choiceIndex
    }

    PlayerMenu {
        id: playerMenu
        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        iconSource: moduleRoot.moduleIcon
        moduleName: moduleRoot.moduleName
        keys: ["playback_resolution", "video_codec", "max_frame_rate", "video_scaling",
               "audio_language", "subtitles", "subtitle_language", "playback_speed"]
        onSettingChanged: function(key, value) { playerRoot.applySetting(key, value) }
        onActivated: function(action) { playerRoot.menuAction(action) }
        onClosed: playerRoot.backToVideo()
    }

}
