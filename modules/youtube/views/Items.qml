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

    // The app's INFO SCREEN setting: "off", "key" (right only) or seconds.
    readonly property string infoSetting: (appCore ? appCore.get_setting("", "info_screen") : "") || "3"

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

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: tree.folderName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    TreeBrowser {
        id: tree
        anchors.fill: parent
        focus: true
        rootPath: "home"
        // The problem line's room, while it shows (Help Line on).
        reservedBottom: problemLine.visible && problemLine.shown ? areaBottom - problemLine.y : 0
        preview: itemsRoot.infoSetting !== "off"
        previewDelay: (parseInt(itemsRoot.infoSetting) || 0) * 1000
        savedTrail: itemsRoot.navListState.trail || []
        fetch: function(path, preview) {
            if (!youtubeBackend)
                return []
            var entries = youtubeBackend.entries(path, preview)
            return entries === undefined ? null : entries
        }
        onActivated: function(item) {
            switch (item.kind) {
            case "search":
                osk.open("")
                break
            case "more":
                youtubeBackend.loadMore(item.path.replace("#more", ""))
                break
            case "video":
                itemsRoot.navigateTo("Player.qml", { item: item }, { trail: tree.trailState() })
                break
            }
        }
        onOptionsRequested: function(item) {
            if (item.kind === "video")
                itemsRoot.offerOptions(item)
        }
        onPreviewRequested: function(item) {
            if (item.kind !== "video")
                return
            info.show(item)
            info.loading = true
            youtubeBackend.loadDetails(item)
        }
        onLeaveRequested: itemsRoot.goBack()
    }

    Connections {
        target: youtubeBackend
        function onListingReady(path) { tree.refresh(path) }
        function onDetailsReady(path, details) {
            if (!info.item || info.item.path !== path)
                return
            info.details = details
            info.loading = !details.complete
        }
    }

    // What stands in the way of browsing, when anything does: no network, or
    // no yt-dlp.
    HelpLine {
        id: problemLine
        visible: !osk.visible && !options.visible && !info.visible && text !== ""
        text: youtubeBackend ? youtubeBackend.problem : ""
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        visible: !osk.visible && !options.visible && !info.visible
        // What select does with the entry under the cursor: a video plays,
        // and right opens its info (or its options, with the info screen off)
        // rather than moving.
        readonly property var entry: tree.currentEntry
        readonly property bool onVideo: !!entry && entry.kind === "video"
        text: root.hints.back + ":BACK "
              + (onVideo ? String(root.hints.navigate).replace("]", "\u25C4]") + ":NAVIGATE "
                           + root.hints.browse + (tree.preview ? ":INFO " : ":OPTIONS ")
                         : root.hints.arrows + ":NAVIGATE ")
              + root.hints.select
              + (onVideo ? ":PLAY"
                 : !entry || entry.isFolder ? ":OPEN"
                 : entry.kind === "search" ? ":SEARCH"
                 : entry.kind === "more" ? ":MORE" : ":OPEN")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    OnScreenKeyboard {
        id: osk
        anchors.fill: parent
        title: "Search YouTube"
        onAccepted: function(text) {
            tree.forceActiveFocus()
            tree.openItem({ name: "Search: " + text, path: "search/" + text })
        }
        onCanceled: tree.forceActiveFocus()
    }

    // A video's info: the tree's last layer. Right there offers its options.
    InfoPanel {
        id: info
        anchors.fill: parent
        onPlayRequested: function(item) {
            itemsRoot.navigateTo("Player.qml", { item: item }, { trail: tree.trailState() })
        }
        onOptionsRequested: function(item) { itemsRoot.offerOptions(item) }
        onMoveRequested: function(delta) { tree.move(delta) }
        onClosed: tree.forceActiveFocus()
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
        onFavoritesEdited: tree.refresh("favorites")
        // Back to the info screen when it was opened from there.
        onClosed: info.visible ? info.forceActiveFocus() : tree.forceActiveFocus()
        onActivated: function(action) {
            if (action !== "watchlater" || !entry)
                return
            if (watchLater)
                youtubeBackend.removeFromWatchLater(entry.videoId)
            else
                youtubeBackend.addToWatchLater(entry.videoId, entry.title || "", entry.channelName || "")
            // WATCH LATER comes and goes with what is on it.
            tree.refresh("watchlater")
            tree.refresh("home")
        }
    }
}
