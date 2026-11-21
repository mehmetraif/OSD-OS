import QtQuick
import "MpvKeys.js" as MpvKeys

// What every player's view does the same way while its video plays, without
// drawing anything: where the session is, as mpv says; the keys it sends on
// to mpv; and, for a player with a menu of its own (PlayerMenu), the way into
// that menu, out of it and on from it. Each player keeps its own settings,
// its own rule for saving where it got to, and its own lines in the menu.
//
//     PlayerSession {
//         id: session
//         player: playerRoot          // goBack() and the keys' focus
//         menu: playerMenu            // a player with a menu
//         followsPlaylist: true       // a player that plays a playlist
//         onMenuRequested: playerRoot.openMenu()
//         onRestartRequested: playerRoot.play(session.positionMs)
//     }
//     Keys.onPressed: function(event) { session.forwardKey(event) }
//     Keys.onReleased: function(event) { session.releaseKey(event) }
QtObject {
    id: session

    // The player's view: its goBack(), and where the keys go back to as the
    // menu closes.
    property Item player: null
    // Its PlayerMenu, for a player that has one.
    property Item menu: null
    // The player plays playlists: the video mpv is on is followed too.
    property bool followsPlaylist: false

    // The video has shown its first position: the loading screen goes.
    property bool started: false
    // Where the last start began, for the loading screen's counter and a
    // start again.
    property int startMs: 0
    // Where the session is, for its save at the end and its start again
    // after the menu: where begin() asked it to start (the video of a
    // playlist, -1 its first, and the point in it) until mpv says otherwise,
    // so that back before its first picture keeps the resume point; the
    // point is zeroed only as mpv goes on to another video.
    property int positionMs: 0
    property int durationMs: 0
    property int playlistPos: -1

    // A setting changed in the menu that the video can't take as it plays:
    // the session starts again where it is as the menu closes.
    property bool reloadOnClose: false
    // CLOSE VIDEO: the session's end leaves the module for the main menu.
    property bool closeToMainMenu: false

    // Back during the video, or its end for the menu: the player opens its
    // menu, with its own lines.
    signal menuRequested()
    // The menu closed on a video that ended for it, or with a setting that
    // asks for it: the player starts it again where it is (positionMs,
    // playlistPos), with its settings as they are now.
    signal restartRequested()

    // A start asked for at startMs, at the playlist's video plPos (-1 its
    // first): where the session is until mpv says otherwise.
    function begin(fromMs, plPos) {
        startMs = fromMs
        positionMs = fromMs
        playlistPos = plPos === undefined ? -1 : plPos
    }

    // A key for mpv (MpvKeys.js), with the channel keys for a player that
    // asks for them (sent once per press). A key's repeats go as repeats:
    // mpv holds the key down for them, and lets it go as the key is
    // (releaseKey()). True when the key was mpv's.
    function forwardKey(event, channels) {
        var key = MpvKeys.name(event.key, channels)
        if (key === "")
            return false
        if (!(event.isAutoRepeat && MpvKeys.isChannel(key)))
            mpvController.sendKey(key, event.isAutoRepeat)
        event.accepted = true
        return true
    }
    // A key let go: the one mpv holds down for its repeats, if any, with it.
    function releaseKey(event) {
        if (!event.isAutoRepeat)
            mpvController.releaseKeys()
    }

    // Back to the module's menus, or, after CLOSE VIDEO, to the main menu.
    function leave() {
        if (closeToMainMenu)
            moduleRoot.leaveModule()
        else
            player.goBack()
    }

    // The menu's own lines: BROWSE (the module's menus, the video playing on
    // behind them) and CLOSE VIDEO. True when it was one of them. Ended for
    // the menu (an mpv process has the screen), the video is saved already,
    // and nothing plays to stop: the player goes itself.
    function menuAction(action) {
        if (action === "browse") {
            menu.close()
            if (mpvController.videoActive) {
                mpvController.leavePlayerMenu()
            } else {
                // The main menu's first row takes it back from there.
                mpvController.leaveSession()
                player.goBack()
            }
            return true
        }
        if (action === "close") {
            closeToMainMenu = true
            menu.close()
            if (mpvController.videoActive)
                mpvController.stop()
            else
                moduleRoot.leaveModule()
            return true
        }
        return false
    }

    // Back in the menu: the video full screen again, started again where it
    // is if it ended for the menu or a setting asks for it.
    function backToVideo() {
        menu.close()
        player.forceActiveFocus()
        var ended = !mpvController.videoActive
        mpvController.closePlayerMenu()
        if (reloadOnClose || ended) {
            reloadOnClose = false
            restartRequested()
        }
    }

    // The session's end, once the player has saved where it got to and
    // dealt with a failure: back during an mpv process, which has the screen
    // while it plays, ended it for the menu ("menu"), which opens, and the
    // video starts again as it closes (backToVideo()); any other end leaves.
    function end(reason) {
        if (reason === "menu") {
            menuRequested()
            return
        }
        if (menu)
            menu.close()
        leave()
    }

    property Connections mpv: Connections {
        target: mpvController

        function onPlayerMenuRequested() {
            if (session.menu)
                session.menuRequested()
        }
        function onPositionChanged(ms) {
            if (ms > 0) {
                session.positionMs = ms
                session.started = true
            }
        }
        function onDurationChanged(ms) {
            if (ms > 0)
                session.durationMs = ms
        }
        function onPlaylistPosChanged(pos) {
            if (session.followsPlaylist && pos >= 0) {
                if (pos !== Math.max(0, session.playlistPos))
                    session.positionMs = 0
                session.playlistPos = pos
            }
        }
    }
}
