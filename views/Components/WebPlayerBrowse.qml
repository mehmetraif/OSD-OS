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
    // In the layout's frame (Main.qml): its tree or list in the pane, beside
    // the column a layout keeps on the screen.
    readonly property bool inFrame: true

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
        navigateTo("Launch.qml", params, { trail: trail || catalogBrowser.tree.trailState() })
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

    CatalogBrowser {
        id: catalogBrowser
        anchors.fill: parent
        focus: true
        catalog: browse.catalog
        savedTrail: browse.navListState.trail || []
        playKind: "title"
        searchTitle: "Search " + browse.serviceName
        // What stands in the way of browsing, when anything does: no TMDB
        // key, or no network. Said with Help Line off too.
        problemAlways: true
        // No TMDB key: select opens the module's settings on its TMDB Key.
        problemAction: browse.catalog && browse.catalog.keyMissing ? "SETTINGS" : ""
        onProblemActivated: moduleRoot.openSettings("tmdb_api_key")
        covered: options.visible
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
            if (item.kind === "home")
                browse.open({ name: browse.serviceName })
        }
        onPlayRequested: function(item) { browse.open({ item: item, name: item.name }) }
        onOptionsRequested: function(item) { options.offer(item) }
        onLeaveRequested: browse.goBack()
    }

    EntryOptions {
        id: options
        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        onFavoritesEdited: catalogBrowser.tree.refresh("favorites")
        onClosed: catalogBrowser.refocus()
    }
}
