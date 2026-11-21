import QtQuick
import Components

// YouTube in the tree, browsed like Local Files: RECENTLY WATCHED (the
// backend's history) and FAVORITES (the module's list in AppCore), then SEARCH
// (on the on-screen keyboard), the subscriptions feed, each channel and
// playlist, and Watch Later, from youtubeBackend.listing(). A video plays in
// Player.qml, and coming back reopens the same folders. Right on a video opens
// its info screen (InfoPanel), the tree's last layer, as the cursor resting on
// it does after the app's INFO SCREEN setting's seconds; right there offers
// its options (EntryOptions: its favourite, PLAY AT STARTUP, Watch Later), as
// right on the video does with the info screen off. Opened with the startup
// favourite (navParams.startupPlay), it plays that at once.
FocusScope {
    id: itemsRoot
    // In the layout's frame (Main.qml): its tree or list in the pane, beside
    // the column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    // The startup favourite, played as if chosen in FAVORITES, so coming back
    // from it lands there, or the video behind the menus (the main menu's row
    // for it), opened again as if chosen in RECENTLY WATCHED. Only as the view
    // first opens: coming back from the player brings navListState instead.
    Component.onCompleted: {
        if (navParams.navListState)
            return
        if (navParams.resumePlayer)
            Qt.callLater(resumeBehind, navParams.resumePlayer)
        else if (navParams.startupPlay)
            Qt.callLater(playAtStartup, navParams.startupPlay)
    }
    function resumeBehind(params) {
        navigateTo("Player.qml", params,
                   { trail: [{ path: "home", sel: 0, name: "", pushed: false },
                             { path: "history", sel: 0, name: "Recently Watched", pushed: false }] })
    }
    function playAtStartup(entry) {
        var favorites = youtubeBackend.entries("favorites")
        for (var i = 0; i < favorites.length; ++i) {
            if (favorites[i].path === entry.path) {
                navigateTo("Player.qml", { item: favorites[i], startup: true },
                           { trail: [{ path: "home", sel: 1, name: "", pushed: false },
                                     { path: "favorites", sel: i, name: "Favorites", pushed: false }] })
                return
            }
        }
    }

    CatalogBrowser {
        id: catalogBrowser
        anchors.fill: parent
        focus: true
        // What stands in the way of browsing, when anything does: no
        // network, or no yt-dlp.
        catalog: youtubeBackend
        savedTrail: itemsRoot.navListState.trail || []
        playKind: "video"
        searchTitle: "Search YouTube"
        covered: options.visible
        fetch: function(path, preview) {
            if (!youtubeBackend)
                return []
            var entries = youtubeBackend.entries(path, preview)
            return entries === undefined ? null : entries
        }
        onPlayRequested: function(item) {
            itemsRoot.navigateTo("Player.qml", { item: item }, { trail: catalogBrowser.tree.trailState() })
        }
        onOptionsRequested: function(item) { itemsRoot.offerOptions(item) }
        onLeaveRequested: itemsRoot.goBack()
        // Afresh: YouTube may have other matches for words typed before.
        onSearchAccepted: function(path) {
            youtubeBackend.forgetSearches()
            catalogBrowser.tree.refresh(path)
        }
    }

    // A video as FAVORITES keeps it: what Player.qml plays it with, without
    // the description and counts a list would carry along.
    function offerOptions(item) {
        options.watchLater = youtubeBackend.isInWatchLater(item.videoId)
        options.offer({ name: item.name, path: item.path, isFolder: false, kind: "video",
                        videoId: item.videoId, url: item.url, title: item.title,
                        channelName: item.channelName || "", isShort: !!item.isShort })
    }

    // Its favourite, and saving it to Watch Later or taking it off.
    EntryOptions {
        id: options
        property bool watchLater: false

        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        moreChoices: [{ label: watchLater ? "Remove from Watch Later" : "Save to Watch Later",
                        action: "watchlater" }]
        onFavoritesEdited: catalogBrowser.tree.refresh("favorites")
        onClosed: catalogBrowser.refocus()
        onActivated: function(action) {
            if (action !== "watchlater" || !entry)
                return
            if (watchLater)
                youtubeBackend.removeFromWatchLater(entry.videoId)
            else
                youtubeBackend.addToWatchLater(entry.videoId, entry.title || "", entry.channelName || "")
            // WATCH LATER comes and goes with what is on it.
            catalogBrowser.tree.refresh("watchlater")
            catalogBrowser.tree.refresh("home")
        }
    }
}
