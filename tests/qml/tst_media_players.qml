import QtQuick
import QtTest

// The media server modules' players, detail screens and routers (Plex,
// Jellyfin, Emby: modules/*/views/Player.qml, Item.qml, Root.qml) against
// stand-ins for their backends and for MpvController: a stream that won't
// come (a failed fallback transcode, autoplay's next episode) and back while
// waiting for one lead back to the detail screen, not a black screen with no
// mpv to take a key; a detail screen plays only the stream it asked for; a
// launch given up drops its stream; signing out leaves no view signed out of
// for back to return to. And Local Files' player: a remote's channel or page
// keys step through the folder's films.
Rectangle {
    id: root
    color: surfaceColor
    width: 640
    height: 480
    // Main.qml's, as the views and their components read them.
    property real sw: width
    property real sh: height
    property real px: 2
    property string globalFont: "monospace"
    property color primaryColor: "white"
    property color secondaryColor: "white"
    property color tertiaryColor: "white"
    property color accentColor: "white"
    property color surfaceColor: "black"
    property color loadingInk: "white"
    property color loadingPaper: "black"
    property bool loadingEffect: false
    property bool loadingThemed: true
    property bool screenHandedOff: false
    property bool effectsRest: true
    property var schemes: ({})
    property var skin: ({})
    property bool hintBar: true
    property bool helpLine: true
    property var statusIcons: []
    property var takeBackNote: ({})
    property bool videoBehind: false
    readonly property real hintRoom: 0
    readonly property real helpRoom: 0
    readonly property real helpLineMargin: sh * 0.1583333
    property rect contentBox: Qt.rect(sw * 0.115625, sh * 0.11875, sw * 0.76875, sh * 0.7770833)
    property rect osdWindow: contentBox
    property string osdBackground: "Full"
    property string osdFrame: "On"
    property var backgroundEffect: ({})
    property string backgroundShader: ""
    property real fxTime: 0
    property var selector: null
    property var hints: ({ back: "[ESC]", navigate: "[▲▼]", arrows: "[▲▼◄►]", change: "[◄►]",
                           browse: "[►]", select: "[ENTER]", play_pause: "[SPACE]", hold: "HOLD" })
    function restFor(layer, resting) {}
    function dismissScreenSaver() {}
    function selectorShown(item, shown) {}
    function coverShown(item, shown) {}
    function skinIcon(url) { return url }
    function pad(n) { return n < 10 ? "0" + n : "" + n }
    function formatTime(ms) {
        var s = Math.floor(ms / 1000)
        return Math.floor(s / 60) + ":" + pad(s % 60)
    }
    // The routers' view changes, without the window transitions.
    function changeWindow(loader, source, properties) { loader.setSource(source, properties) }

    // What the views asked of their router.
    property var navigations: []
    property int backs: 0

    // The module's router, as a view reads it.
    QtObject {
        id: moduleRoot
        property string moduleId: "com.osdos.plex"
        property string moduleName: "Plex"
        property string moduleIcon: ""
    }

    // AppCore: the module's settings.
    QtObject {
        id: appCore
        property var settings: ({})
        function get_setting(moduleId, key) { return settings[key] }
        function get_settings() {
            var modules = {}
            modules[moduleRoot.moduleId] = settings
            return { modules: modules }
        }
        function get_module_info(id) { return { name: moduleRoot.moduleName, icon: "" } }
        function is_module_enabled(id) { return false }
        // The video's menu (PlayerMenu): the module's rows, none here.
        function get_module_settings_schema(id) { return [] }
        function invoke_module_action(id, slot) {}
        function save_setting(id, key, value) {}
        signal dynamicOptionsReady(string moduleId, string key, var items)
        // The lists a module keeps (RECENTLY WATCHED).
        property var lists: ({})
        function add_to_list(moduleId, list, entry, max) {
            var l = lists
            l[list] = (l[list] || []).concat([entry])
            lists = l
        }
        function list_contains(moduleId, list, path) { return false }
    }

    // LocalFilesBackend as its player uses it: a folder's entries, which
    // files are images or playlists, and the positions saved.
    QtObject {
        id: localFilesBackend
        property var folder: []          // /films' entries
        property var positions: ({})     // path → { pos, plPos }
        signal dynamicOptionsReady(string key, var options)
        function entries(path) { return path === "/films" ? folder : [] }
        function isImage(path) { return /\.(jpg|png)$/.test(path) }
        function isPlaylist(path) { return /\.m3u$/.test(path) }
        function playlistContainsImages(path) { return false }
        function getSavedPosition(path) { return positions[path] || {} }
        function savePosition(path, pos, plPos) {
            var p = positions
            p[path] = { pos: pos, plPos: plPos }
            positions = p
        }
        function clearPosition(path) {
            var p = positions
            delete p[path]
            positions = p
        }
    }

    // MpvController: what was started (each file and where from, in
    // seconds) and the keys sent to mpv.
    QtObject {
        id: mpvController
        property int position: 0
        property int duration: 0
        property bool videoActive: false
        property int loads: 0
        property var urls: []
        property var starts: []
        property var keys: []
        signal playbackEnded(int finalPositionMs, int finalDurationMs, string reason)
        signal playlistPosChanged(int pos)
        signal playerMenuRequested()
        signal skipRequested()
        signal subtitleCycleRequested()
        signal audioCycleRequested()
        function loadAndPlay(url, startSeconds) {
            loads++
            urls = urls.concat([url])
            starts = starts.concat([startSeconds || 0])
        }
        function noteSession(note) {}
        function closePlayerMenu() {}
        function setVideoProperty(name, value) {}
        function sendKey(key) { keys = keys.concat([key]) }
        function releaseKeys() {}
        function stop() {}
        function seekTo(ms) {}
        function clearOsdPrompt() {}
        function showOsdSkipPrompt() {}
    }

    // PlexBackend as the views use it: build_stream_url answers at once, as
    // the backend does; request_transcode, later, with streamUrlReady.
    QtObject {
        id: plexBackend
        property int cancels: 0
        property int transcodes: 0
        property int nextAsks: 0
        property int cardAsks: 0
        property int queueAsks: 0
        property string authState: "authed"
        signal streamUrlReady(string url, string plexToken)
        signal errorOccurred(string message)
        signal nextEpisodeReady(var detail)
        signal itemLoaded(var detail)
        signal extrasLoaded(var items)
        signal showYearReady(string showRatingKey, int year)
        signal authRevoked()
        signal logoutComplete()
        signal librariesLoaded(var libraries)
        signal userPinRequired(string userId, bool wrongPin)
        signal pinReady(string code, string pinId)
        signal authSuccess()
        signal usersLoaded(var users)
        signal serversLoaded(var servers)
        signal cardItemReady(var detail)
        signal cardQueueReady(var items)
        signal cardError(string message)
        signal queueReady(var ratingKeys)
        function cancel_stream() { cancels++ }
        function resolve_card(ref, mode) { cardAsks++ }
        function resolve_card_queue(ref) { cardAsks++ }
        function expand_queue(items) { queueAsks++ }
        function request_transcode() { transcodes++ }
        function build_stream_url(ratingKey, partKey, sessionId) { streamUrlReady("http://plex/" + ratingKey, "token") }
        function update_timeline() {}
        function load_next_episode(ratingKey) { nextAsks++ }
        function load_queue_item(ratingKey) {}
        function load_random_episode(scope) {}
        function set_audio_stream() {}
        function set_subtitle_stream() {}
        function load_item_detail(ratingKey) {}
        function load_extras(ratingKey) {}
        function load_show_year(ratingKey) {}
        function reset_device_check() {}
        function get_auth_state() { return authState }
        function pending_pin_user() { return "" }
        function load_users_from_cache() {}
        function start_pin_auth() { return 1 }
        function cancel_pin_auth(request) {}
        function get_active_server_name() { return "Server" }
        function get_active_user_name() { return "User" }
        function get_switchable_servers() { return [] }
        function load_libraries() {}
    }

    // JellyfinBackend and EmbyBackend (the same) as the views use them:
    // get_playback_url answers later, with streamUrlReady.
    component ServerBackend: QtObject {
        property int cancels: 0
        property int asks: 0
        property int nextAsks: 0
        property int stops: 0
        signal streamUrlReady(string url)
        signal errorOccurred(string message)
        signal nextEpisodeReady(var detail)
        signal segmentsReady(string itemId, var segments)
        signal itemLoaded(var detail)
        function cancel_playback_url() { cancels++ }
        function get_playback_url() { asks++ }
        function get_access_token() { return "" }
        function load_next_episode(itemId) { nextAsks++ }
        function report_playback_stopped() { stops++ }
        function update_playback_progress() {}
        function set_last_track_langs() {}
        function fetchSegments(itemId) {}
    }
    ServerBackend { id: jellyfinBackend }
    ServerBackend { id: embyBackend }

    // PlaylistsBackend as its player uses it: a list, the m3u written for
    // it, and where it stopped.
    QtObject {
        id: playlistsBackend
        function playlist(id) { return { id: id, name: "Night", kind: "online", items: [{ id: "v1" }, { id: "v2" }] } }
        function prepare(id, fromItemId) {
            return { file: "/data/" + id + ".m3u", count: 2, youtube: false, images: false,
                     startIndex: -1, resumeIndex: 0, resumeMs: 0 }
        }
        function savedPositionMs(id) { return 0 }
        function savePosition(id, pos, plPos) {}
        function clearPosition(id) {}
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }
    Connections {
        target: loader.item
        ignoreUnknownSignals: true
        function onGoBack() { root.backs++ }
        function onNavigateTo(path, params, listState) {
            root.navigations = root.navigations.concat([{ path: path, params: params }])
        }
    }

    TestCase {
        name: "MediaPlayers"
        when: windowShown

        function init() {
            root.navigations = []
            root.backs = 0
            appCore.settings = {}
            appCore.lists = {}
            mpvController.loads = 0
            mpvController.urls = []
            mpvController.starts = []
            mpvController.keys = []
            localFilesBackend.folder = []
            localFilesBackend.positions = {}
            plexBackend.cancels = 0
            plexBackend.transcodes = 0
            plexBackend.nextAsks = 0
            plexBackend.cardAsks = 0
            plexBackend.queueAsks = 0
            plexBackend.authState = "authed"
            for (var backend of [jellyfinBackend, embyBackend]) {
                backend.cancels = 0
                backend.asks = 0
                backend.nextAsks = 0
                backend.stops = 0
            }
        }

        function cleanup() {
            loader.source = ""
        }

        function open(module, view, params) {
            moduleRoot.moduleId = "com.osdos." + module
            loader.setSource(Qt.resolvedUrl("../../modules/" + module + "/views/" + view), { navParams: params })
            verify(loader.item !== null, "loaded " + module + "/" + view)
            loader.item.forceActiveFocus()
            return loader.item
        }

        function openPlexPlayer() {
            return open("plex", "Player.qml", { streamUrl: "http://plex/10", plexToken: "token",
                                                ratingKey: "10", partKey: "/library/parts/10",
                                                title: "Episode 1" })
        }

        // Direct play fails, and the transcode it falls back to fails too:
        // back to the detail screen, the stream dropped.
        function test_plexFallbackFailureGoesBack() {
            var player = openPlexPlayer()
            compare(mpvController.loads, 1)
            mpvController.playbackEnded(0, 0, "failed")
            compare(plexBackend.transcodes, 1)
            verify(player.waiting)
            plexBackend.errorOccurred("TRANSCODE FAILED: Host unreachable")
            compare(root.backs, 1)
            compare(plexBackend.cancels, 1)
            compare(mpvController.loads, 1)
        }

        // Back while the fallback is on its way: the player leaves, nothing
        // is sent to an mpv that isn't there, and the stream is dropped.
        function test_plexBackWhileWaiting() {
            openPlexPlayer()
            mpvController.playbackEnded(0, 0, "failed")
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
            compare(mpvController.keys.length, 0)
            compare(plexBackend.cancels, 1)
        }

        // Autoplay's next episode can't be transcoded: back, not a black
        // screen.
        function test_plexNextEpisodeFailureGoesBack() {
            appCore.settings = { autoplay_next_episode: true }
            openPlexPlayer()
            mpvController.playbackEnded(60000, 60000, "eof")
            compare(plexBackend.nextAsks, 1)
            plexBackend.nextEpisodeReady({ ratingKey: "11", partKey: "/library/parts/11", title: "Episode 2",
                                           forceTranscode: true, audioStreams: [], subtitleStreams: [] })
            compare(plexBackend.transcodes, 1)
            plexBackend.errorOccurred("TRANSCODE FAILED: Internal Server Error")
            compare(root.backs, 1)
            compare(mpvController.loads, 1)
        }

        // While a video plays, nothing is waited for: an error elsewhere
        // leaves the player as it is, and back goes to mpv.
        function test_plexPlayingKeepsKeysForMpv() {
            openPlexPlayer()
            plexBackend.errorOccurred("LOAD HUBS FAILED: timeout")
            compare(root.backs, 0)
            keyClick(Qt.Key_Escape)
            compare(mpvController.keys, ["ESC"])
            compare(root.backs, 0)
        }

        function openPlexDetail() {
            var detail = open("plex", "Item.qml", { item: { ratingKey: "10", type: "movie", title: "FILM" } })
            plexBackend.itemLoaded({ ratingKey: "10", partKey: "/library/parts/10", title: "FILM",
                                     forceTranscode: true, audioStreams: [], subtitleStreams: [] })
            verify(detail.detail !== null)
            return detail
        }

        // A stream the detail screen didn't ask for (an extra's, given up,
        // answering late) doesn't start a player with the film's details.
        function test_plexDetailPlaysOnlyItsStream() {
            openPlexDetail()
            plexBackend.streamUrlReady("http://plex/trailer", "token")
            compare(root.navigations.length, 0)
            keyClick(Qt.Key_Return)
            compare(plexBackend.transcodes, 1)
            plexBackend.streamUrlReady("http://plex/film", "token")
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "Player.qml")
            compare(root.navigations[0].params.streamUrl, "http://plex/film")
        }

        // Back while the film's transcode is on its way gives it up.
        function test_plexBackWhileLaunchingCancels() {
            var detail = openPlexDetail()
            keyClick(Qt.Key_Return)
            verify(detail.isLaunching)
            keyClick(Qt.Key_Escape)
            compare(plexBackend.cancels, 1)
            compare(root.backs, 1)
        }

        // Back while a card's item is looked for, or a PLAY ALL's queue is
        // made: the launch is given up, its stream and the item it looked for
        // dropped as they come, and back to where it was started from.
        function test_plexBackWhileCardOrQueueLaunches() {
            var card = open("plex", "CardPlay.qml", { cardRef: "plex://movie/abc", authState: "authed" })
            compare(plexBackend.cardAsks, 1)
            verify(card.launching)
            keyClick(Qt.Key_Escape)
            compare(plexBackend.cancels, 1)
            compare(root.backs, 1)
            verify(!card.launching)
            loader.source = ""
            var queue = open("plex", "QueuePlay.qml", { queueItems: [{ ratingKey: "1", type: "movie" }] })
            compare(plexBackend.queueAsks, 1)
            verify(queue.launching)
            keyClick(Qt.Key_Escape)
            compare(plexBackend.cancels, 2)
            compare(root.backs, 2)
            verify(!queue.launching)
        }

        // Signed out from under a view (its token revoked): PinAuth, and back
        // from it leaves the module rather than returning to the view.
        function test_plexRevokedLeavesNothingToReturnTo() {
            appCore.settings = { auto_sign_in: true }
            var router = open("plex", "Root.qml", {})
            tryVerify(function() { return String(router.children[0].source).indexOf("Libraries.qml") >= 0 })
            plexBackend.authRevoked()
            tryVerify(function() { return String(router.children[0].source).indexOf("PinAuth.qml") >= 0 })
            compare(router.navStack.length, 0)
            router.children[0].item.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
            verify(String(router.children[0].source).indexOf("PinAuth.qml") >= 0)
        }

        function openJellyfinPlayer(module) {
            return open(module, "Player.qml", { streamUrl: "http://server/stream", itemId: "e1",
                                                mediaSourceId: "e1", title: "Episode 1" })
        }

        // Autoplay's next episode: its PlaybackInfo fails, or back while it
        // is on its way. Back to the detail screen, its stream dropped, and
        // nothing reported of an episode that never started, not even as the
        // view goes: a stop at 0 would wipe its resume point (Jellyfin, then
        // Emby).
        function test_serverNextEpisodeFailureGoesBack_data() {
            return [{ tag: "jellyfin", module: "jellyfin", back: false },
                    { tag: "emby", module: "emby", back: false },
                    { tag: "jellyfin, back", module: "jellyfin", back: true },
                    { tag: "emby, back", module: "emby", back: true }]
        }
        function test_serverNextEpisodeFailureGoesBack(data) {
            var backend = data.module === "emby" ? embyBackend : jellyfinBackend
            appCore.settings = { autoplay_next_episode: true }
            openJellyfinPlayer(data.module)
            compare(mpvController.loads, 1)
            mpvController.playbackEnded(60000, 60000, "eof")
            compare(backend.nextAsks, 1)
            var stops = backend.stops
            backend.nextEpisodeReady({ itemId: "e2", mediaSourceId: "e2", title: "Episode 2",
                                       audioStreams: [], subtitleStreams: [] })
            compare(backend.asks, 1)
            if (data.back)
                keyClick(Qt.Key_Escape)
            else
                backend.errorOccurred("PLAYBACK INFO FAILED: Host unreachable")
            compare(root.backs, 1)
            compare(backend.cancels, 1)
            compare(mpvController.loads, 1)
            loader.source = ""
            compare(backend.stops, stops)
        }

        // Back while a direct play's fallback transcode is on its way: the
        // player leaves, reporting the playback stopped as a failed one.
        function test_serverBackWhileWaiting_data() {
            return [{ tag: "jellyfin", module: "jellyfin" }, { tag: "emby", module: "emby" }]
        }
        function test_serverBackWhileWaiting(data) {
            var backend = data.module === "emby" ? embyBackend : jellyfinBackend
            openJellyfinPlayer(data.module)
            mpvController.playbackEnded(0, 0, "failed")
            compare(backend.asks, 1)
            var stops = backend.stops
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
            compare(mpvController.keys.length, 0)
            compare(backend.cancels, 1)
            compare(backend.stops, stops + 1)
        }

        // Local Files: Page Up or Channel + plays the folder's next film,
        // Page Down or Channel − the previous one, round the folder's end,
        // its images, playlists, folders and the tree's own entries passed
        // over; the film left is saved where it got to, the next one goes on
        // RECENTLY WATCHED and starts where it was left without asking,
        // unless Resume Playback is Never. Under the video's menu the keys do
        // nothing. A playlist steps through its own entries, in mpv.
        function test_localFilesStepsThroughTheFolder() {
            localFilesBackend.folder = [{ name: "A.mkv", path: "/films/A.mkv", isFolder: false },
                                        { name: "B.mkv", path: "/films/B.mkv", isFolder: false },
                                        { name: "C.mkv", path: "/films/C.mkv", isFolder: false },
                                        { name: "cover.jpg", path: "/films/cover.jpg", isFolder: false },
                                        { name: "list.m3u", path: "/films/list.m3u", isFolder: false },
                                        { name: "More", path: "/films/More", isFolder: true },
                                        { name: "Search", path: "search", isFolder: false }]
            localFilesBackend.positions = { "/films/C.mkv": { pos: 90000, plPos: -1 } }
            appCore.settings = { resume_playback: "ask", shuffle_playback: "no" }
            var player = open("local_files", "Player.qml", { filePath: "/films/B.mkv", title: "B.mkv" })
            compare(mpvController.urls, ["/films/B.mkv"])
            player.lastKnownPositionMs = 120000
            player.lastKnownDurationMs = 600000

            // (The channel keys take the same branch; QTest can't send them.)
            keyClick(Qt.Key_PageUp)
            compare(mpvController.urls, ["/films/B.mkv", "/films/C.mkv"])
            compare(mpvController.starts[1], 90)
            compare(localFilesBackend.positions["/films/B.mkv"].pos, 120000)
            compare(appCore.lists.recent[0].path, "/films/C.mkv")
            compare(player.filePath, "/films/C.mkv")
            compare(player.itemTitle, "C.mkv")
            compare(player.lastKnownPositionMs, 0)
            verify(!player.overlayVisible)
            keyClick(Qt.Key_PageUp)
            compare(mpvController.urls[2], "/films/A.mkv")
            keyClick(Qt.Key_PageDown)
            compare(mpvController.urls[3], "/films/C.mkv")
            keyClick(Qt.Key_PageDown)
            compare(mpvController.urls[4], "/films/B.mkv")
            compare(mpvController.starts[4], 120)
            // Under the video's menu: nothing.
            player.openMenu()
            keyClick(Qt.Key_PageUp)
            compare(mpvController.urls.length, 5)
            keyClick(Qt.Key_Escape)

            // Never resuming, the next film starts from its beginning.
            appCore.settings = { resume_playback: "no", shuffle_playback: "no" }
            open("local_files", "Player.qml", { filePath: "/films/B.mkv", title: "B.mkv" })
            var n = mpvController.urls.length
            keyClick(Qt.Key_PageUp)
            compare(mpvController.urls[n], "/films/C.mkv")
            compare(mpvController.starts[n], 0)

            // A playlist steps through its own entries, in mpv.
            open("local_files", "Player.qml", { filePath: "/films/list.m3u", title: "list.m3u" })
            n = mpvController.urls.length
            keyClick(Qt.Key_PageUp)
            keyClick(Qt.Key_PageDown)
            compare(mpvController.urls.length, n)
            compare(mpvController.keys, ["CHANNEL_UP", "CHANNEL_DOWN"])
        }

        // The Playlists module's player: the same keys step through the list,
        // in mpv.
        function test_playlistsPlayerStepsThroughTheList() {
            open("playlists", "Player.qml", { playlistId: "p1", name: "Night", startMs: 0, plPos: 0 })
            keyClick(Qt.Key_PageUp)
            keyClick(Qt.Key_PageDown)
            compare(mpvController.keys, ["CHANNEL_UP", "CHANNEL_DOWN"])
        }
    }
}
