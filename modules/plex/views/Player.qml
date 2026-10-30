import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()
    // Emitted when autoplay advances in place, so Root can repoint the BACK
    // target to the now-playing episode's detail instead of the original one.
    signal updateBackItem(var item)

    property string streamUrl:    navParams.streamUrl    || ""
    property string plexToken:    navParams.plexToken    || ""
    property string ratingKey:    navParams.ratingKey    || ""
    property string partKey:      navParams.partKey      || ""
    property string partId:       navParams.partId       || ""
    property string sessionId:    navParams.sessionId    || ""
    property int    viewOffset:   navParams.viewOffset   || 0
    // How long it is, from the server, and where the last start began: the
    // loading screen's counters.
    property int    itemDurationMs: navParams.duration   || 0
    property int    lastStartMs:  0
    property string itemTitle:    navParams.title        || ""
    property var    audioStreams:     navParams.audioStreams     || []
    property var    subtitleStreams:  navParams.subtitleStreams  || []
    property int    audioIdx:    0
    property int    subtitleIdx: 0
    property bool   isTranscoding:    navParams.isTranscoding    || false
    property string selectedAudioId:    navParams.selectedAudioId    || ""
    property string selectedSubtitleId: navParams.selectedSubtitleId || "0"
    // Extras pass false: "next episode" has no meaning for a trailer/clip, so a
    // natural end must return to the Extras list instead of probing for one.
    property bool   allowAutoplay:      navParams.allowAutoplay !== false
    // False when this playback session must not write anything back to Plex.
    // Shuffle-mode NFC cards play as a jukebox: no timeline is reported, so the
    // show's watched state, Continue Watching and on-deck are all left alone.
    // Progress reporting is entirely client-side (the two update_timeline calls
    // below are the only ones), so suppressing them is sufficient — nothing
    // server-side marks an item watched. It also gates the per-part audio and
    // subtitle selections persisted on autoplay (see advanceToEpisode).
    property bool   trackProgress:      navParams.trackProgress !== false
    // Show/season ratingKey a shuffle card draws its episodes from; empty for
    // every other kind of playback, which advances sequentially.
    property string shuffleScope:       navParams.shuffleScope || ""
    // PLAY ALL / SHUFFLE over a playlist or collection: the ordered ratingKeys of
    // everything still to play, and where in them the current item sits. Empty for
    // every other kind of playback. A queue advances unconditionally — the user
    // asked for the whole set — so it is checked ahead of autoplayNext, and
    // QueuePlay.qml passes allowAutoplay: false to keep the two from racing.
    property var    queue:              navParams.queue || []
    property int    queueIndex:         navParams.queueIndex || 0

    property bool stoppedReported:    false
    property bool playbackStarted:    false
    property bool overlayVisible:     false
    property int  choiceIndex:        0
    property string resumeSetting:    "ask"
    property bool pendingRetryTranscode: false

    // Autoplay-next-episode. When enabled, a natural end-of-file advances to the
    // next episode in the same season, carrying over the audio/subtitle language.
    property bool   autoplayNext:       false
    property bool   pendingNextEpisode: false
    property string carryAudioLang:     ""        // language code of the chosen audio track
    property string carrySubLang:       "__off__" // language code, or "__off__" when subtitles are off

    // Waiting on the server with no mpv running: for the next episode, or for
    // the transcode a failed direct play falls back to.
    readonly property bool waiting: pendingNextEpisode || pendingRetryTranscode

    // Back to the detail screen from a wait that won't end: the server failed
    // it, or the user gave up on it with back. Its answer, should it come, is
    // dropped.
    function leaveWaiting() {
        pendingNextEpisode = false
        pendingRetryTranscode = false
        plexBackend.cancel_stream()
        goBack()
    }

    property int lastKnownPositionMs: 0
    property int lastKnownDurationMs: 0

    focus: true

    Keys.onPressed: function(event) {
        if (overlayVisible) {
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
                doStartPlayback(choiceIndex === 0 ? viewOffset : 0)
                event.accepted = true
            }
        } else if (waiting) {
            // No mpv to send the keys to: back leaves.
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back)
                leaveWaiting()
            event.accepted = true
        } else {
            // A key's repeats go as repeats: mpv holds the key down for
            // them, and lets it go as the key is (Keys.onReleased).
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                mpvController.sendKey("ESC", event.isAutoRepeat)
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace) {
                mpvController.sendKey("BS", event.isAutoRepeat)
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                mpvController.sendKey("UP", event.isAutoRepeat)
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                mpvController.sendKey("DOWN", event.isAutoRepeat)
                event.accepted = true
            } else if (event.key === Qt.Key_Left) {
                mpvController.sendKey("LEFT", event.isAutoRepeat)
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                mpvController.sendKey("RIGHT", event.isAutoRepeat)
                event.accepted = true
            } else if (event.key === Qt.Key_Space) {
                mpvController.sendKey("SPACE", event.isAutoRepeat)
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                mpvController.sendKey("ENTER", event.isAutoRepeat)
                event.accepted = true
            }
        }
    }
    // A key let go: the one mpv holds down for its repeats, if any, with it.
    Keys.onReleased: function(event) {
        if (!event.isAutoRepeat)
            mpvController.releaseKeys()
    }

    function newSessionId() {
        var chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        var id = ""
        for (var i = 0; i < 12; i++) id += chars[Math.floor(Math.random() * chars.length)]
        return id
    }

    // Report the final "stopped" timeline for the current episode exactly once.
    // The fallback position/duration are used only when no live value is known.
    function reportStopped(finalPositionMs, finalDurationMs) {
        if (stoppedReported) return
        stoppedReported = true
        if (!trackProgress) return
        var pos = lastKnownPositionMs || finalPositionMs
        var dur = lastKnownDurationMs || finalDurationMs
        plexBackend.update_timeline(ratingKey, partKey, "stopped", pos, dur)
    }

    function initStreamIndices() {
        var selAudio = navParams.selectedAudioId    || ""
        var selSub   = navParams.selectedSubtitleId || "0"
        for (var i = 0; i < audioStreams.length; i++) {
            if (audioStreams[i].id === selAudio) { audioIdx = i; break }
        }
        for (var j = 0; j < subtitleStreams.length; j++) {
            if (subtitleStreams[j].id === selSub) { subtitleIdx = j; break }
        }
        captureCarryLanguages()
    }

    // Record the language of the current audio/subtitle selection so the next
    // episode (which has different per-file stream IDs) can be matched by language.
    function captureCarryLanguages() {
        var a = audioStreams[audioIdx]
        carryAudioLang = (a && a.language) ? a.language : ""
        // subtitleIdx 0 is the synthetic "OFF" entry — preserve "off" deliberately.
        var s = subtitleStreams[subtitleIdx]
        carrySubLang = (subtitleIdx === 0 || !s) ? "__off__" : (s.language || "")
    }

    // Select audioIdx/subtitleIdx on the current stream lists to match the carried
    // languages. Falls back to the first audio track / subtitles-off when no match.
    function applyCarryLanguages() {
        audioIdx = 0
        for (var i = 0; i < audioStreams.length; i++) {
            if (carryAudioLang && audioStreams[i].language === carryAudioLang) { audioIdx = i; break }
        }
        subtitleIdx = 0
        if (carrySubLang !== "__off__" && carrySubLang !== "") {
            for (var j = 1; j < subtitleStreams.length; j++) {
                if (subtitleStreams[j].language === carrySubLang) { subtitleIdx = j; break }
            }
        }
    }

    function buildSubArgs() {
        var allSubUrls = []
        for (var i = 1; i < subtitleStreams.length; i++) {
            if (subtitleStreams[i] && subtitleStreams[i].subUrl)
                allSubUrls.push(subtitleStreams[i].subUrl)
        }
        var selectedSub = subtitleIdx > 0 ? subtitleStreams[subtitleIdx] : null
        var selectedSubUrl = selectedSub ? (selectedSub.subUrl || "") : ""
        if (selectedSubUrl && allSubUrls.length > 1) {
            allSubUrls = allSubUrls.filter(function(u) { return u !== selectedSubUrl })
            allSubUrls.unshift(selectedSubUrl)
        }
        var subTrack
        if (subtitleIdx === 0)
            subTrack = -1
        else if (selectedSubUrl)
            subTrack = 0
        else
            subTrack = subtitleIdx
        return { urls: allSubUrls, track: subTrack }
    }

    // The stream into mpv from offsetMs: the loading screen shows while mpv
    // spins up (lastStartMs is its counter).
    function doStartPlayback(offsetMs) {
        lastStartMs = offsetMs
        if (isTranscoding) {
            // Transcode covers the full timeline (requested at offset 0), so seek mpv
            // to the resume point. This keeps everything before offsetMs seekable, so
            // the user can rewind past the resume point.
            mpvController.loadAndPlay(streamUrl, offsetMs / 1000.0, 0, -1, [], [], false, -1, 0.0, plexToken)
        } else {
            var sub = buildSubArgs()
            mpvController.loadAndPlay(streamUrl, offsetMs / 1000.0,
                                       audioIdx + 1, sub.track, sub.urls, [], false, -1, 0.0, plexToken)
        }
    }

    Connections {
        target: plexBackend
        function onErrorOccurred(msg) {
            console.log("[Player] Backend error: " + msg)
            // The stream it waited for won't come: back to the detail screen,
            // not a black screen with no mpv to take a key.
            if (waiting)
                leaveWaiting()
        }
        function onStreamUrlReady(url, plexToken) {
            if (pendingNextEpisode) {
                // Stream URL for the auto-advanced next episode just arrived.
                pendingNextEpisode = false
                playerRoot.streamUrl = url
                playerRoot.plexToken = plexToken
                doStartPlayback(0)
                return
            }
            if (pendingRetryTranscode) {
                pendingRetryTranscode = false
                // The transcode's one audio track and burnt-in subtitle, from
                // 0 (the full timeline): started as a transcode is, mpv seeking
                // to where the direct play got to, else where it was started.
                playerRoot.streamUrl = url
                playerRoot.plexToken = plexToken
                isTranscoding = true
                doStartPlayback(lastKnownPositionMs > 0 ? lastKnownPositionMs : lastStartMs)
                return
            }
        }

        function onNextEpisodeReady(detail) {
            if (!pendingNextEpisode) return
            // Empty detail → no next episode in the season (or a lookup failure).
            // Fall back to the standard behavior: return to the detail view.
            if (!detail || !detail.ratingKey) {
                // Inside a queue an unplayable entry is skipped instead: one bad
                // item must not end a playlist the user asked to play in full.
                if (playerRoot.requestNextQueueItem()) return
                pendingNextEpisode = false
                goBack()
                return
            }
            playerRoot.advanceToEpisode(detail)
        }
    }

    // Ask the backend for the next item of the queue, if there is one. Reports
    // through nextEpisodeReady, so the advance is the same code path as autoplay.
    // Returns false when there is no queue or it is exhausted.
    function requestNextQueueItem() {
        if (queue.length === 0 || queueIndex + 1 >= queue.length) return false
        queueIndex++
        pendingNextEpisode = true
        plexBackend.load_queue_item(queue[queueIndex])
        return true
    }

    // Swap the player's context to the next episode in place (no navigation) and
    // begin playing it from the beginning, carrying over the track languages.
    function advanceToEpisode(detail) {
        ratingKey   = detail.ratingKey
        partKey     = detail.partKey      || ""
        partId      = detail.partId       || ""
        itemTitle   = detail.title        || ""
        itemDurationMs  = detail.duration        || 0
        audioStreams    = detail.audioStreams    || []
        subtitleStreams = detail.subtitleStreams || []
        isTranscoding   = detail.forceTranscode  || false

        // Fresh-start state for the new episode: the loading screen again
        // until its first frame.
        viewOffset           = 0
        stoppedReported      = false
        playbackStarted      = false
        lastKnownPositionMs  = 0
        lastKnownDurationMs  = 0
        sessionId            = newSessionId()

        // Repoint the BACK target so exiting returns to THIS episode's detail
        // screen, not the one we auto-advanced from. Item.qml reloads from
        // item.ratingKey, so a minimal item carrying the new keys suffices.
        // A queue was launched with replaceWith, so the stack top is the list the
        // queue came from — leave it pointing at the list. Same for a shuffle,
        // whose stack top is the show or season being shuffled: repointing it at
        // an episode would leave a show detail screen rendering an episode.
        if (queue.length === 0 && shuffleScope === "") updateBackItem({
            ratingKey: detail.ratingKey,
            type: detail.type || "episode",
            title: detail.title || "",
            grandparentTitle: detail.grandparentTitle || "",
            parentIndex: detail.parentIndex,
            index: detail.index
        })

        // Match the carried languages onto this episode's stream lists, then
        // remember the resulting selection for the episode after this one.
        applyCarryLanguages()
        var audioId = (audioStreams[audioIdx] && audioStreams[audioIdx].id) ? audioStreams[audioIdx].id : ""
        var subId   = (subtitleStreams[subtitleIdx] && subtitleStreams[subtitleIdx].id) ? subtitleStreams[subtitleIdx].id : "0"
        selectedAudioId    = audioId
        selectedSubtitleId = subId
        captureCarryLanguages()

        // Persist the chosen tracks to Plex, so the next play of this episode
        // anywhere remembers them (mirrors Item.qml's behavior before playback).
        // Skipped when this session doesn't write back to Plex: CardPlay.qml
        // skips the same call for a card's first episode, and the languages
        // carried onto a shuffled episode are a match, not a user choice — the
        // transcode gets its stream IDs from request_transcode's query either way.
        if (trackProgress && partId) {
            if (audioId) plexBackend.set_audio_stream(audioId, partId)
            plexBackend.set_subtitle_stream(subId, partId)
        }

        // Both paths resolve through onStreamUrlReady, which checks this flag.
        // build_stream_url emits synchronously, so the flag must be set first.
        pendingNextEpisode = true
        if (isTranscoding) {
            plexBackend.request_transcode(ratingKey, partKey, sessionId, audioId, subId, 0)
        } else {
            plexBackend.build_stream_url(ratingKey, partKey, sessionId)
        }
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0) {
                playerRoot.lastKnownPositionMs = ms
                // First position update means mpv is up and playing — drop the
                // loading indicator (mpv's own window now covers the screen).
                playerRoot.playbackStarted = true
            }
        }
        function onDurationChanged(ms) {
            if (ms > 0) playerRoot.lastKnownDurationMs = ms
        }

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            if (reason === "failed") {
                if (!isTranscoding) {
                    // Direct play failed (e.g. HTTP 500 from PMS on WAN). Retry
                    // transparently with transcoding at the same resume offset,
                    // the loading screen up while the server is waited for.
                    playbackStarted = false
                    pendingRetryTranscode = true
                    plexBackend.request_transcode(ratingKey, partKey, sessionId,
                                                  selectedAudioId, selectedSubtitleId,
                                                  0)
                } else {
                    goBack()
                }
                return
            }

            // Both a natural end ("eof") and a user quit ("stopped") mark the item
            // stopped in Plex. A natural end only *attempts* to auto-advance, and
            // only when the user has autoplay enabled — and even then it's just a
            // request: load_next_episode returns an empty detail when there is no
            // next episode (a movie, or the last episode of a season), in which case
            // onNextEpisodeReady falls back to goBack(). Everything else returns to
            // the detail view here.
            reportStopped(finalPositionMs, finalDurationMs)
            if (reason === "eof" && queue.length > 0) {
                // A queue advances regardless of the autoplay setting: playing the
                // whole set is what the user selected. Exhausted → back to the list.
                if (requestNextQueueItem()) return
                goBack()
                return
            }
            if (reason === "eof" && autoplayNext) {
                pendingNextEpisode = true
                // Same advance machinery either way — only the source of the next
                // episode differs, and both report through nextEpisodeReady.
                if (shuffleScope !== "")
                    plexBackend.load_random_episode(shuffleScope)
                else
                    plexBackend.load_next_episode(ratingKey)
                return
            }
            goBack()
        }
    }

    // Where this item has got to, every ten seconds: only while it plays.
    // Another video left playing behind the menus (Transparent Background)
    // moves mpvController.position too, and that is not this item's.
    Timer {
        interval: 10000
        repeat:   true
        running:  playbackStarted && !waiting
        onTriggered: {
            if (trackProgress && mpvController.position > 0)
                plexBackend.update_timeline(ratingKey, partKey, "playing",
                                            mpvController.position, mpvController.duration)
        }
    }

    Component.onCompleted: {
        initStreamIndices()
        // Nothing to play: back out, a tick later (the router isn't
        // listening for goBack yet), rather than stand on a black screen
        // with no video to end it.
        if (streamUrl === "") {
            Qt.callLater(playerRoot.goBack)
            return
        }
        resumeSetting = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"
        // Match ModuleSettings.qml's reading of a toggle: stored as a real bool
        // once the user touches it, but accept the legacy "ON" string too.
        var autoplayRaw = appCore.get_setting(moduleRoot.moduleId, "autoplay_next_episode")
        autoplayNext  = allowAutoplay && (autoplayRaw === true || autoplayRaw === "ON")

        if (resumeSetting === "ask" && viewOffset > 0) {
            overlayVisible = true
        } else {
            doStartPlayback(viewOffset)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"

        // Shown while mpv launches and buffers the stream (before its window
        // takes over). Hidden once the first position update arrives, or while
        // the resume prompt is up.
        LoadingScreen {
            anchors.fill: parent
            source: moduleRoot.moduleName
            startMs: playerRoot.lastStartMs
            durationMs: playerRoot.itemDurationMs || playerRoot.lastKnownDurationMs
            visible: streamUrl !== "" && !overlayVisible && !playbackStarted
        }
    }

    PromptScreen {
        visible: overlayVisible
        title: "Resume playback?"
        message: itemTitle
        choices: [
            "Resume from " + root.formatTime(viewOffset),
            "Start from the beginning"
        ]
        currentIndex: choiceIndex
        onHovered: function(index) { choiceIndex = index }
    }
}
