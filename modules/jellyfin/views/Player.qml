import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()
    // Emitted when autoplay advances in place, so Root can repoint the BACK
    // target to the now-playing episode's detail instead of the original one.
    signal updateBackItem(var item)

    property string streamUrl:      navParams.streamUrl      || ""
    property string itemId:         navParams.itemId         || ""
    property string mediaSourceId:  navParams.mediaSourceId  || itemId
    property string itemTitle:      navParams.title          || ""
    property int    viewOffset:     navParams.viewOffset     || 0
    // How long it is, from the server, and where the last start began: the
    // loading screen's counters.
    property int    itemDurationMs: navParams.duration       || 0
    property int    lastStartMs:    0
    property var    audioStreams:       navParams.audioStreams     || []
    property var    subtitleStreams:    navParams.subtitleStreams  || []
    property string selectedAudioId:    navParams.selectedAudioId    || ""
    property string selectedSubtitleId: navParams.selectedSubtitleId || ""

    property bool isTranscoding: streamUrl.indexOf("master.m3u8") >= 0

    // Autoplay next episode
    property bool   autoplayNext:       false
    property bool   pendingNextEpisode: false
    // Set when a direct-play attempt fails and we re-request forcing a transcode.
    property bool   pendingRetryTranscode: false
    // Set when the user changed a track mid-transcode: mpv is quitting so we can
    // re-request the stream with a different audio track / burned-in subtitle
    // (not a real stop). "audio", "sub", or "" for none.
    property string pendingTrackSwitch: ""
    property string carryAudioLang:     ""
    property string carrySubLang:       "__off__"
    // Full stream metadata used to disambiguate when several streams share a language.
    // These are runtime carry-over hints for autoplay; the persisted backend cache
    // stores the language plus its position within the language group.
    property var carryAudioPrefs: ({ language: "", title: "", displayTitle: "", codec: "", channels: "" })
    property var carrySubPrefs:   ({ language: "__off__", title: "", displayTitle: "", codec: "", forced: false })

    property int    audioIdx:    0
    property int    subtitleIdx: -1

    property bool stoppedReported: false
    property bool playbackStarted: false
    property bool overlayVisible:  false
    property int  choiceIndex:     0

    // Intro/outro skip
    property var    segments:           []
    property var    activeSegment:      null
    property bool   skipPromptShown:    false
    property bool   introAutoSkipped:   false
    property bool   outroAutoSkipped:   false
    property string introSkipSetting:   "Off"
    property string outroSkipSetting:   "Off"

    property int lastKnownPositionMs: 0
    property int lastKnownDurationMs: 0

    // Waiting on the server with no mpv running: for the next episode, or for
    // the transcode a failed direct play (or a track switch) falls back to.
    readonly property bool waiting: pendingNextEpisode || pendingRetryTranscode

    // Back to the detail screen from a wait that won't end: the server failed
    // it, or the user gave up on it with back. Its answer, should it come, is
    // dropped; a transcode retried is reported stopped as a failed one is.
    // A next episode never started is reported nothing, not even as the view
    // goes (Component.onDestruction): a stop at 0 would wipe its resume point.
    function leaveWaiting() {
        if (pendingRetryTranscode)
            reportStopped(lastKnownPositionMs, lastKnownDurationMs, true)
        stoppedReported = true
        pendingNextEpisode = false
        pendingRetryTranscode = false
        jellyfinBackend.cancel_playback_url()
        goBack()
    }

    focus: true

    Keys.onPressed: function(event) {
        if (overlayVisible) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                reportStopped(0, 0)
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
                if (choiceIndex === 0) {
                    beginPlayback(viewOffset)
                } else {
                    beginPlayback(0)
                }
                event.accepted = true
            }
        } else if (waiting) {
            // No mpv to send the keys to: back leaves.
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back)
                leaveWaiting()
            event.accepted = true
        } else {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                mpvController.sendKey("ESC")
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace) {
                mpvController.sendKey("BS")
                event.accepted = true
            } else if (event.key === Qt.Key_Space) {
                mpvController.sendKey("SPACE")
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
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                mpvController.sendKey("ENTER")
                event.accepted = true
            }
        }
    }

    function msToTicks(ms) {
        return ms * 10000
    }

    function initStreamIndices() {
        var selAudio = String(selectedAudioId || "")
        var selSub   = String(selectedSubtitleId || "")
        audioIdx = 0
        for (var i = 0; i < audioStreams.length; i++) {
            if (String(audioStreams[i].id || "") === selAudio) { audioIdx = i; break }
        }
        // Subtitle track: -1 means off; otherwise find the 0-based index in subtitleStreams.
        subtitleIdx = -1
        for (var j = 0; j < subtitleStreams.length; j++) {
            if (String(subtitleStreams[j].id || "") === selSub) { subtitleIdx = j; break }
        }
        captureCarryLanguages()
    }

    // Record the language and metadata of the current audio/subtitle selection
    // so the next episode (which has different per-file stream IDs) can be matched
    // by language, with title/codec/channels used to disambiguate duplicates.
    function captureCarryLanguages() {
        var a = audioStreams[audioIdx]
        carryAudioLang = (a && a.language) ? a.language : ""
        carryAudioPrefs = a ? {
            language: a.language || "",
            title: a.title || "",
            displayTitle: a.displayTitle || "",
            codec: a.codec || "",
            channels: a.channels !== undefined ? a.channels : ""
        } : { language: "", title: "", displayTitle: "", codec: "", channels: "" }

        var s = subtitleStreams[subtitleIdx]
        carrySubLang = (subtitleIdx === -1 || !s) ? "__off__" : (s.language || "")
        carrySubPrefs = (subtitleIdx === -1 || !s)
            ? { language: "__off__", title: "", displayTitle: "", codec: "", forced: false }
            : {
                language: s.language || "",
                title: s.title || "",
                displayTitle: s.displayTitle || "",
                codec: s.codec || "",
                forced: s.forced === true
            }
    }

    // Score a stream against a previously-selected stream. Returns -1 when the
    // language doesn't match; higher scores mean a closer match.
    function streamScore(s, prefs) {
        if (!s || !prefs) return -1
        var lang = s.language || ""
        var prefLang = prefs.language || ""
        if (lang !== prefLang) return -1
        var score = 100
        var st = (s.title || "").toString().toLowerCase()
        var pt = (prefs.title || "").toString().toLowerCase()
        if (st && pt && st === pt) score += 100
        var sd = (s.displayTitle || "").toString().toLowerCase()
        var pd = (prefs.displayTitle || "").toString().toLowerCase()
        if (sd && pd && sd === pd) score += 80
        var sc = (s.codec || "").toString().toLowerCase()
        var pc = (prefs.codec || "").toString().toLowerCase()
        if (sc && pc && sc === pc) score += 50
        if (prefs.channels !== undefined && s.channels !== undefined) {
            if (String(s.channels).toLowerCase() === String(prefs.channels).toLowerCase())
                score += 30
        }
        if (prefs.forced !== undefined && s.forced === prefs.forced)
            score += 20
        return score
    }

    function bestAudioMatch(streams, prefs) {
        var best = 0
        var bestScore = -1
        for (var i = 0; i < streams.length; i++) {
            var score = streamScore(streams[i], prefs)
            if (score > bestScore) {
                bestScore = score
                best = i
            }
        }
        return best
    }

    function bestSubtitleMatch(streams, prefs) {
        if (!prefs || prefs.language === "__off__" || prefs.language === "") return -1
        var best = -1
        var bestScore = -1
        for (var i = 0; i < streams.length; i++) {
            var score = streamScore(streams[i], prefs)
            if (score > bestScore) {
                bestScore = score
                best = i
            }
        }
        return best
    }

    // Select audioIdx/subtitleIdx on the current stream lists to match the carried
    // languages. When several streams share a language, title/codec/channels are
    // used to pick the closest match; the fallback is the first audio track / off.
    function applyCarryLanguages() {
        if (audioStreams && audioStreams.length > 0) {
            if (carryAudioLang && carryAudioPrefs.language)
                audioIdx = bestAudioMatch(audioStreams, carryAudioPrefs)
            else
                audioIdx = 0
        } else {
            audioIdx = 0
        }

        if (subtitleStreams && subtitleStreams.length > 0) {
            if (carrySubLang !== "__off__" && carrySubLang !== "")
                subtitleIdx = bestSubtitleMatch(subtitleStreams, carrySubPrefs)
            else
                subtitleIdx = -1
        } else {
            subtitleIdx = -1
        }
    }

    function reportStopped(finalPositionMs, finalDurationMs, failed) {
        if (stoppedReported) return
        stoppedReported = true
        // Fall back to viewOffset when playback never reported a position
        // (canceled resume overlay, back during LOADING): Stopped with
        // PositionTicks=0 would wipe the server-side resume point.
        var pos = lastKnownPositionMs || finalPositionMs || viewOffset
        jellyfinBackend.report_playback_stopped(itemId, mediaSourceId, msToTicks(pos), failed || false)
    }

    // Swap the player's context to the next episode in place (no navigation) and
    // begin playing it from the beginning, carrying over the track languages.
    function advanceToEpisode(detail) {
        itemId         = detail.itemId         || ""
        mediaSourceId  = detail.mediaSourceId  || detail.itemId || ""
        itemTitle      = detail.title          || ""
        itemDurationMs = detail.duration       || 0
        audioStreams   = detail.audioStreams   || []
        subtitleStreams= detail.subtitleStreams|| []

        // Fresh-start state for the new episode
        viewOffset           = 0
        stoppedReported      = false
        playbackStarted      = false
        lastKnownPositionMs  = 0
        lastKnownDurationMs  = 0

        // Reset skip state for the new episode
        segments = []
        activeSegment = null
        skipPromptShown = false
        introAutoSkipped = false
        outroAutoSkipped = false
        mpvController.clearOsdPrompt()

        // Repoint the BACK target so exiting returns to THIS episode's detail
        updateBackItem({
            itemId: detail.itemId,
            type: detail.type || "episode",
            title: detail.title || "",
            grandparentTitle: detail.grandparentTitle || "",
            parentIndex: detail.parentIndex,
            index: detail.index
        })

        // Match the carried languages onto this episode's stream lists
        applyCarryLanguages()
        selectedAudioId    = (audioStreams[audioIdx] && audioStreams[audioIdx].id) ? String(audioStreams[audioIdx].id) : ""
        selectedSubtitleId = (subtitleIdx >= 0 && subtitleStreams[subtitleIdx] && subtitleStreams[subtitleIdx].id) ? String(subtitleStreams[subtitleIdx].id) : ""
        captureCarryLanguages()
        // Compute same-language index for the cache so menu navigation can
        // restore the exact track, not just the first stream with that language.
        var aLangIdx = -1
        if (carryAudioLang && audioStreams && audioStreams.length > 0) {
            var found = -1
            for (var ai2 = 0; ai2 < audioStreams.length; ai2++) {
                if (audioStreams[ai2].language === carryAudioLang) {
                    found++
                    if (ai2 === audioIdx) { aLangIdx = found; break }
                }
            }
        }
        var sLangIdx = -1
        if (carrySubLang !== "__off__" && carrySubLang !== "" && subtitleStreams && subtitleStreams.length > 0) {
            var sfound = -1
            for (var si2 = 0; si2 < subtitleStreams.length; si2++) {
                if (subtitleStreams[si2].language === carrySubLang) {
                    sfound++
                    if (si2 === subtitleIdx) { sLangIdx = sfound; break }
                }
            }
        }
        jellyfinBackend.set_last_track_langs(carryAudioLang, carrySubLang === "__off__" ? "" : carrySubLang,
                                              aLangIdx, sLangIdx)

        // Request the new stream URL — get_playback_url() reports the playback
        // Start to the server once PlaybackInfo resolves (correct session/method).
        pendingNextEpisode = true
        var audioStreamIdx = selectedAudioId ? parseInt(selectedAudioId) : -1
        var subStreamIdx   = selectedSubtitleId ? parseInt(selectedSubtitleId) : -1
        jellyfinBackend.get_playback_url(detail.itemId, detail.mediaSourceId || detail.itemId,
                                          audioStreamIdx, subStreamIdx)
    }

    function beginPlayback(offsetMs) {
        lastStartMs = offsetMs
        doStartPlayback(offsetMs)
    }

    // Mirrors PlexBackend's Player.buildSubArgs: text subtitles are handed to mpv
    // as sidecar --sub-file URLs (so direct play never transcodes to show them),
    // while image subs (no subUrl) are selected from the embedded stream via --sid.
    // subtitleIdx is -1 for off, otherwise a 0-based index into subtitleStreams.
    // Friendly track name for a sidecar — mpv would otherwise title it from the
    // opaque sidecar URL. Passed to the OSC alongside the URL (see loadAndPlay).
    function subLabel(s) {
        return (s.displayTitle || s.title || s.language || s.codec || "")
    }

    function buildSubArgs() {
        // Only hand mpv the *selected* sidecar — never preload every text track.
        // Each sidecar URL makes the server extract that subtitle on the fly, and
        // mpv loads --sub-file synchronously at startup, so preloading them
        // stalls playback for ~20-30s *per track* even with subs off (a file
        // with many embedded subs looks like it won't play at all). Subs off
        // starts at once; turning one on costs a single extraction, not all of
        // them. Non-selected embedded subs remain reachable in mpv via --sid
        // (they ride along in the direct-play stream); non-selected external
        // subs simply aren't offered mid-playback, which the pre-launch UI
        // already covers. As Emby's.
        var pairs = []
        var selectedSub = subtitleIdx >= 0 ? subtitleStreams[subtitleIdx] : null
        var selectedSubUrl = selectedSub ? (selectedSub.subUrl || "") : ""
        if (selectedSubUrl)
            pairs.push({ url: selectedSubUrl, title: subLabel(selectedSub) })
        var subTrack
        if (subtitleIdx < 0)
            subTrack = -2                 // off → --sid=no
        else if (selectedSubUrl)
            subTrack = 0                  // selected sidecar is the only loaded sub-file
        else
            subTrack = subtitleIdx + 1    // embedded/image sub → mpv 1-based --sid
        return {
            urls:   pairs.map(function(p) { return p.url }),
            titles: pairs.map(function(p) { return p.title }),
            track:  subTrack
        }
    }

    function doStartPlayback(offsetMs) {
        lastStartMs = offsetMs
        var jfToken = jellyfinBackend.get_access_token()
        if (isTranscoding) {
            // The HLS manifest bakes in the selected audio, and the chosen subtitle
            // is burned into the video — so there's no soft track for mpv to pick
            // (subTrack -2 = --sid=no) or to cycle, for either audio or subs.
            // Instead the OSC gets:
            //   sub-cycle       — show a SUBTITLE button that asks us to switch
            //                     (handled in onSubtitleCycleRequested → new transcode)
            //   transcode-sub   — the burned-in track's name, for the info line
            //   audio-cycle     — same deal for the AUDIO button
            //                     (onAudioCycleRequested → new transcode)
            //   transcode-audio — the baked-in audio track's name, for the info line
            // All go through --script-opts-append: a plain --script-opts= would
            // replace the list MpvController already built (screensaver, hide-crop…).
            //
            // script-opts is a comma-separated key=value list, so neither value can
            // carry a comma or an "="; spaces round-trip as underscores.
            var subName = "Off"
            if (subtitleIdx >= 0 && subtitleStreams[subtitleIdx]) {
                var s = subtitleStreams[subtitleIdx]
                subName = s.displayTitle || s.title || s.language || "Unknown"
            }
            subName = subName.replace(/ /g, "_").replace(/[,=]/g, "")

            // "Off" only when the item has no audio at all — the cycle itself never
            // lands there (see onPlaybackEnded).
            var audioName = "Off"
            if (audioIdx >= 0 && audioStreams[audioIdx]) {
                var a = audioStreams[audioIdx]
                audioName = a.displayTitle || a.title || a.language || "Unknown"
            }
            audioName = audioName.replace(/ /g, "_").replace(/[,=]/g, "")

            var extra = [
                "--script-opts-append=transcode-sub=" + subName,
                "--script-opts-append=sub-cycle=" + (subtitleStreams.length > 0 ? "1" : "0"),
                "--script-opts-append=transcode-audio=" + audioName,
                "--script-opts-append=audio-cycle=" + (audioStreams.length > 0 ? "1" : "0")
            ]

            mpvController.loadAndPlay(streamUrl, offsetMs / 1000.0,
                                       -1, -2, [], [], false, -1, 0.0, "",
                                       false, "", false, [], 0.0, false, extra, jfToken)
        } else {
            // Direct play: file served whole. audioIdx is 0-based → mpv's 1-based
            // --aid; subtitles come from buildSubArgs (sidecars + --sid).
            var audioTrack = audioStreams.length > 0 ? audioIdx + 1 : 0
            var sub = buildSubArgs()
            mpvController.loadAndPlay(streamUrl, offsetMs / 1000.0,
                                       audioTrack, sub.track, sub.urls, [], false, -1, 0.0, "",
                                       false, "", false, sub.titles, 0.0, false, [], jfToken)
        }
    }

    function findActiveSegment(ms) {
        for (var i = 0; i < segments.length; i++) {
            if (ms >= segments[i].startMs && ms < segments[i].endMs)
                return segments[i]
        }
        return null
    }

    Connections {
        target: jellyfinBackend
        function onErrorOccurred(msg) {
            console.log("[Jellyfin Player] Backend error: " + msg)
            // The stream it waited for won't come: back to the detail screen,
            // not LOADING with no mpv to take a key.
            if (waiting)
                leaveWaiting()
        }

        function onSegmentsReady(itemId_, segments_) {
            if (itemId_ !== playerRoot.itemId) return
            playerRoot.segments = segments_
        }

        function onStreamUrlReady(url) {
            if (pendingNextEpisode) {
                // Stream URL for the auto-advanced next episode just arrived
                pendingNextEpisode = false
                playerRoot.streamUrl = url
                doStartPlayback(0)
                // Fetch segments for intro/outro skip after playback starts, so
                // the HTTP request doesn't contend with the PlaybackInfo POST.
                if (introSkipSetting !== "Off" || outroSkipSetting !== "Off")
                    jellyfinBackend.fetchSegments(playerRoot.itemId)
                return
            }
            if (pendingRetryTranscode) {
                // Fallback transcode after a direct-play failure. The transcode
                // covers the full timeline from 0, so seek mpv to where we left
                // off. isTranscoding follows the URL (the server's HLS
                // manifest): assigned, it would stay true for a next episode
                // that plays direct.
                pendingRetryTranscode = false
                playerRoot.streamUrl = url
                doStartPlayback(lastKnownPositionMs > 0 ? lastKnownPositionMs : viewOffset)
                return
            }
        }

        function onNextEpisodeReady(detail) {
            if (!pendingNextEpisode) return
            // Empty detail → no next episode in the season
            if (!detail || !detail.itemId) {
                pendingNextEpisode = false
                goBack()
                return
            }
            playerRoot.advanceToEpisode(detail)
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

                // --- Skip segment tracking ---
                if (playerRoot.segments.length > 0) {
                    var seg = findActiveSegment(ms)
                    if (seg && seg !== playerRoot.activeSegment) {
                        playerRoot.activeSegment = seg
                        var setting = seg.type === "Intro"
                            ? playerRoot.introSkipSetting
                            : playerRoot.outroSkipSetting

                        if (setting === "Auto") {
                            if (seg.type === "Intro" && !playerRoot.introAutoSkipped) {
                                playerRoot.introAutoSkipped = true
                                mpvController.seekTo(seg.endMs)
                            } else if (seg.type === "Outro" && !playerRoot.outroAutoSkipped) {
                                playerRoot.outroAutoSkipped = true
                                mpvController.seekTo(seg.endMs)
                            }
                        } else if (setting === "Button") {
                            if (!playerRoot.skipPromptShown) {
                                playerRoot.skipPromptShown = true
                                mpvController.showOsdSkipPrompt()
                            }
                        }
                    } else if (!seg && playerRoot.activeSegment) {
                        // Segment ended naturally
                        playerRoot.activeSegment = null
                        playerRoot.skipPromptShown = false
                        mpvController.clearOsdPrompt()
                    }
                }
                // --- End skip segment tracking ---
            }
        }
        function onDurationChanged(ms) {
            if (ms > 0) playerRoot.lastKnownDurationMs = ms
        }

        function onSkipRequested() {
            if (playerRoot.activeSegment) {
                if (playerRoot.activeSegment.type === "Intro")
                    playerRoot.introAutoSkipped = true
                else
                    playerRoot.outroAutoSkipped = true
                mpvController.seekTo(playerRoot.activeSegment.endMs)
                mpvController.clearOsdPrompt()
                // Don't null activeSegment here — the async seek moves past the
                // segment boundary, and the next onPositionChanged detects the
                // end naturally. Nulling it before the seek completes causes a
                // position update at the old location to re-detect the same
                // segment as "new" and re-trigger the prompt.
            }
        }

        // OSC SUBTITLE button during a transcode. The sub is burned into the
        // video, so switching means a new transcode: quit mpv, then do the work
        // in onPlaybackEnded once it has actually exited — starting the request
        // here would race mpv's exit, and that exit would run the normal
        // "user stopped" path and navigate out of the player.
        function onSubtitleCycleRequested() {
            if (!isTranscoding || subtitleStreams.length === 0) return
            playerRoot.lastKnownPositionMs = mpvController.position
            playerRoot.pendingTrackSwitch = "sub"
            mpvController.stop()
        }

        // OSC AUDIO button during a transcode. The audio is baked into the HLS
        // manifest, so switching means a new transcode — same deferred-until-exit
        // contract as onSubtitleCycleRequested above.
        function onAudioCycleRequested() {
            if (!isTranscoding || audioStreams.length === 0) return
            playerRoot.lastKnownPositionMs = mpvController.position
            playerRoot.pendingTrackSwitch = "audio"
            mpvController.stop()
        }

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            if (pendingTrackSwitch !== "") {
                // mpv exited because we asked it to (onAudio/onSubtitleCycleRequested),
                // not because the user stopped. Close out the old transcode session so
                // the server tears the encode down, advance to the next track, and
                // re-request — onStreamUrlReady's pendingRetryTranscode branch resumes
                // at lastKnownPositionMs. Deliberately no goBack().
                var which = pendingTrackSwitch
                pendingTrackSwitch = ""
                reportStopped(finalPositionMs, finalDurationMs)
                stoppedReported = false

                if (which === "audio") {
                    // No "off" stop in the audio cycle: silence isn't worth a
                    // transcode restart, and direct play still gets mpv's own
                    // no-audio step from `cycle audio`.
                    audioIdx = (audioIdx + 1) % audioStreams.length
                    selectedAudioId = String(audioStreams[audioIdx].id || "")
                } else {
                    subtitleIdx++
                    if (subtitleIdx >= subtitleStreams.length) subtitleIdx = -1
                    selectedSubtitleId = subtitleIdx >= 0 ? subtitleStreams[subtitleIdx].id : ""
                }
                // Keep the autoplay carry-over hints in step with the new choice so
                // the next episode inherits it.
                captureCarryLanguages()

                pendingRetryTranscode = true
                var newAIdx = selectedAudioId ? parseInt(selectedAudioId) : -1
                var newSIdx = selectedSubtitleId ? parseInt(selectedSubtitleId) : -1
                jellyfinBackend.get_playback_url(itemId, mediaSourceId, newAIdx, newSIdx, true)
                return
            }
            if (reason === "failed") {
                if (!isTranscoding) {
                    // Direct play failed (e.g. a codec mpv couldn't handle, or a
                    // network drop). Retry transparently with a transcode, resuming
                    // at the last known position. Mirrors the Plex module.
                    reportStopped(finalPositionMs, finalDurationMs, true)
                    stoppedReported = false
                    pendingRetryTranscode = true
                    var aIdx = selectedAudioId ? parseInt(selectedAudioId) : -1
                    var sIdx = selectedSubtitleId ? parseInt(selectedSubtitleId) : -1
                    jellyfinBackend.get_playback_url(itemId, mediaSourceId, aIdx, sIdx, true)
                    return
                }
                // mpv exited with an error. Report as failed so the server
                // doesn't update the resume position. reportStopped uses the
                // last known position internally, so this is safe even when
                // mpv exited before the first position update.
                reportStopped(finalPositionMs, finalDurationMs, true)
                goBack()
                return
            }

            // Both a natural end ("eof") and user quit ("stopped") save
            // the current position for resume. A natural end only attempts
            // to auto-advance when the user has autoplay enabled.
            reportStopped(finalPositionMs, finalDurationMs)
            if (reason === "eof" && autoplayNext) {
                pendingNextEpisode = true
                jellyfinBackend.load_next_episode(itemId)
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
            var pos = mpvController.position
            if (pos > 0) {
                if (pos > playerRoot.lastKnownPositionMs)
                    playerRoot.lastKnownPositionMs = pos
                jellyfinBackend.update_playback_progress(itemId, mediaSourceId,
                                                         msToTicks(pos), false)
            }
        }
    }

    Component.onCompleted: {
        initStreamIndices()
        if (streamUrl === "") return
        var resumeSetting = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"
        // Match ModuleSettings.qml's reading of a toggle: stored as a real bool
        // once the user touches it, but accept the legacy "ON" string too.
        var autoplayRaw = appCore.get_setting(moduleRoot.moduleId, "autoplay_next_episode")
        autoplayNext = (autoplayRaw === true || autoplayRaw === "ON")

        // Hoist skip settings
        introSkipSetting = appCore.get_setting(moduleRoot.moduleId, "intro_skip") || "Off"
        outroSkipSetting = appCore.get_setting(moduleRoot.moduleId, "outro_skip") || "Off"

        // Fetch segments if either skip mode is enabled
        if (introSkipSetting !== "Off" || outroSkipSetting !== "Off")
            jellyfinBackend.fetchSegments(itemId)

        // "ask": prompt resume vs. start over when there's a saved position.
        // "always" (or anything else): resume directly.
        if (resumeSetting === "ask" && viewOffset > 0) {
            overlayVisible = true
        } else {
            beginPlayback(viewOffset)
        }
    }

    // Safety net: if the Player view is destroyed (e.g. app quit, back nav
    // without stopping), report stopped so Jellyfin doesn't show this as
    // still playing. The guard in reportStopped prevents double-reporting.
    Component.onDestruction: {
        if (streamUrl !== "")
            reportStopped(lastKnownPositionMs, lastKnownDurationMs)
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
    }
}
