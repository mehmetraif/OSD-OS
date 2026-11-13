import QtQuick

// A web player module's catalogue (Netflix, Prime Video), browsed in a
// TreeBrowser before the service's own player opens: RECENTLY WATCHED and
// FAVORITES (the module's lists in AppCore), then SEARCH, MOVIES and SERIES by
// genre, and the service's home page. The catalogue comes from the backend's
// TmdbCatalog. Choosing a title, or the home page, opens it in Launch.qml, and
// coming back reopens the same folders. A title's info screen (InfoPanel) is
// the tree's last layer: right on it, or the cursor resting on it as long as
// the app's INFO SCREEN setting says. Right there offers the title's options
// (EntryOptions: its favourite, PLAY AT STARTUP), as right on the title does
// with the info screen off. Opened with the startup favourite
// (navParams.startupPlay), it plays that at once.
//
// A module's Browse.qml is just this, with its backend and name:
//     WebPlayerBrowse { backend: netflixBackend; serviceName: "Netflix" }
FocusScope {
    id: browse

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})
    // The module's WebPlayerBackend.
    property var backend: null
    property string serviceName: ""
    readonly property var catalog: backend ? backend.catalog : null

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    // Opens a title (params.item) or the service's home page; trail is where
    // coming back lands, the tree as it is unless given.
    function open(params, trail) {
        if (params.item && appCore)
            appCore.add_to_list(moduleRoot.moduleId, "recent", params.item, 30)
        navigateTo("Launch.qml", params, { trail: trail || tree.trailState() })
    }

    // The startup favourite, opened as if chosen in FAVORITES, so coming back
    // from it lands there. Only as the view first opens: coming back from the
    // player brings navListState instead.
    Component.onCompleted: {
        if (navParams.startupPlay && !navParams.navListState)
            Qt.callLater(playAtStartup, navParams.startupPlay)
    }
    function playAtStartup(entry) {
        var favorites = appCore.get_list(moduleRoot.moduleId, "favorites")
        for (var i = 0; i < favorites.length; ++i) {
            if (favorites[i].path === entry.path) {
                open({ item: favorites[i], name: favorites[i].name },
                     [{ path: "home", sel: 1, name: "", pushed: false },
                      { path: "favorites", sel: i, name: "Favorites", pushed: false }])
                return
            }
        }
    }

    // The app's INFO SCREEN setting: "off", "key" (right only) or seconds.
    readonly property string infoSetting: (appCore ? appCore.get_setting("", "info_screen") : "") || "3"

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
        preview: browse.infoSetting !== "off"
        previewDelay: (parseInt(browse.infoSetting) || 0) * 1000
        savedTrail: browse.navListState.trail || []
        fetch: function(path, preview) {
            if (!browse.catalog || !appCore)
                return []
            if (path === "recent" || path === "favorites")
                return appCore.get_list(moduleRoot.moduleId, path)
            var entries = browse.catalog.listing(path, preview)
            if (path !== "home" || !entries)
                return entries
            return [{ name: "Recently Watched", path: "recent", isFolder: true },
                    { name: "Favorites", path: "favorites", isFolder: true }].concat(entries)
        }
        onActivated: function(item) {
            switch (item.kind) {
            case "search":
                osk.open("")
                break
            case "more":
                browse.catalog.loadMore(item.path.replace("#more", ""))
                break
            case "home":
                browse.open({ name: browse.serviceName })
                break
            case "title":
                browse.open({ item: item, name: item.name })
                break
            }
        }
        onPreviewRequested: function(item) {
            if (item.kind !== "title" || !browse.catalog)
                return
            info.show(item)
            info.loading = true
            browse.catalog.loadDetails(item)
        }
        onOptionsRequested: function(item) {
            if (item.kind === "title")
                options.offer(item)
        }
        onLeaveRequested: browse.goBack()
    }

    Connections {
        target: browse.catalog
        function onListingReady(path) { tree.refresh(path) }
        function onDetailsReady(path, details) {
            if (!info.item || info.item.path !== path)
                return
            info.details = details
            info.loading = false
        }
    }

    // What stands in the way of browsing, when anything does: no TMDB key, or
    // no network.
    HelpLine {
        id: problemLine
        visible: !osk.visible && !info.visible && !options.visible && text !== ""
        text: browse.catalog ? browse.catalog.problem : ""
        // A problem is said with Help Line off too.
        always: text !== ""
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        visible: !osk.visible && !info.visible && !options.visible
        // What select does with the entry under the cursor: a title plays,
        // and right on it opens its info (or its options, with the info
        // screen off) rather than moving.
        readonly property var entry: tree.currentEntry
        readonly property bool onTitle: !!entry && entry.kind === "title"
        text: root.hints.back + ":BACK "
              + (onTitle ? String(root.hints.navigate).replace("]", "\u25C4]") + ":NAVIGATE "
                           + root.hints.browse + (tree.preview ? ":INFO " : ":OPTIONS ")
                         : root.hints.arrows + ":NAVIGATE ")
              + root.hints.select
              + (!entry || entry.isFolder ? ":OPEN"
                 : entry.kind === "title" ? ":PLAY"
                 : entry.kind === "search" ? ":SEARCH"
                 : entry.kind === "more" ? ":MORE" : ":OPEN")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // A title's info: the tree's last layer.
    InfoPanel {
        id: info
        anchors.fill: parent
        onPlayRequested: function(item) { browse.open({ item: item, name: item.name }) }
        onOptionsRequested: function(item) { options.offer(item) }
        onMoveRequested: function(delta) { tree.move(delta) }
        onClosed: tree.forceActiveFocus()
    }

    OnScreenKeyboard {
        id: osk
        anchors.fill: parent
        title: "Search " + browse.serviceName
        onAccepted: function(text) {
            tree.forceActiveFocus()
            tree.openItem({ name: "Search: " + text, path: "search/" + text })
        }
        onCanceled: tree.forceActiveFocus()
    }

    EntryOptions {
        id: options
        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        onFavoritesEdited: tree.refresh("favorites")
        // Back to the info screen when it was opened from there.
        onClosed: info.visible ? info.forceActiveFocus() : tree.forceActiveFocus()
    }
}
