import QtQuick
import Components

// Adding videos to a playlist from inside the module: the sources as one
// tree, each browsed the way its own module browses it. LOCAL FILES (its
// RECENTLY WATCHED, FAVORITES and folders), YOUTUBE (its RECENTLY WATCHED,
// FAVORITES, SEARCH and the rest of its home), JELLYFIN and EMBY (CONTINUE
// WATCHING, NEXT UP and the libraries, down to their shows' seasons), each
// where its module is on (a server, signed in to). Select on a video adds it
// and stays, so several go on in a row; the line above the hints says what
// became of the last one.
FocusScope {
    id: addRoot
    // In the layout's frame (Main.qml): its tree or list in the pane, beside
    // the column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})
    readonly property string playlistId: navParams.playlistId || ""
    readonly property string playlistName: navParams.playlistName || ""
    readonly property bool offline: navParams.kind === "offline"

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    // What became of the last video selected.
    property string outcome: ""

    readonly property string kLocal: "com.osdos.local_files"
    readonly property string kYouTube: "com.osdos.youtube"
    readonly property string kJellyfin: "com.osdos.jellyfin"
    readonly property string kEmby: "com.osdos.emby"
    // A server's paths: its module, by the prefix they start with.
    readonly property var servers: ({ "jf": "com.osdos.jellyfin", "em": "com.osdos.emby" })
    // Where the on-screen keyboard searches: a module's prefix ("local:",
    // "yt:").
    property string searchIn: ""

    function sources() {
        var out = []
        if (appCore.is_module_enabled(kLocal) && typeof localFilesBackend !== "undefined" && localFilesBackend)
            out.push({ name: "Local Files", path: "local:" + localFilesBackend.mediaRoot(), isFolder: true })
        if (appCore.is_module_enabled(kYouTube) && typeof youtubeBackend !== "undefined" && youtubeBackend)
            out.push({ name: "YouTube", path: "yt:home", isFolder: true })
        if (appCore.is_module_enabled(kJellyfin) && typeof jellyfinBackend !== "undefined" && jellyfinBackend
                && jellyfinBackend.has_auth())
            out.push({ name: "Jellyfin", path: "jf", isFolder: true })
        if (appCore.is_module_enabled(kEmby) && typeof embyBackend !== "undefined" && embyBackend
                && embyBackend.has_auth())
            out.push({ name: "Emby", path: "em", isFolder: true })
        return out
    }

    // A module's tree's entries, as its own browser has them (its backend's
    // entries()), as this tree has them: folders under the module's prefix
    // and its own paths, videos with their module and the entry as the
    // module gave it, and what else the module's tree offers (SEARCH, MORE),
    // acted on as it does.
    function moduleEntries(prefix, moduleId, entries) {
        return entries.map(function(e) {
            if (e.isFolder)
                return { name: e.name, path: prefix + e.path, isFolder: true }
            if (!e.kind || e.kind === "video")
                return { name: e.name, path: prefix + e.path, isFolder: false,
                         kind: "video", module: moduleId, entry: e }
            return { name: e.name, path: prefix + e.path, isFolder: false, kind: e.kind, entry: e }
        })
    }
    function serverEntries(prefix, items) {
        var moduleId = servers[prefix]
        return items.map(function(e) {
            if (e.isFolder)
                return { name: e.name, path: prefix + ":" + e.itemId, isFolder: true }
            // An episode out of its season (CONTINUE WATCHING, NEXT UP): with its show.
            var shown = e.type === "episode" && e.grandparentTitle && e.name === e.title
                        ? e.grandparentTitle + " - " + e.name : e.name
            return { name: shown, path: prefix + ":item:" + e.itemId, isFolder: false,
                     kind: "video", module: moduleId, entry: e }
        })
    }

    function fetch(path, preview) {
        if (!appCore || !playlistsBackend)
            return []
        if (path === "sources")
            return sources()
        if (path.indexOf("local:") === 0) {
            // Read off the app's thread, as Local Files' own tree reads them
            // (entriesReady brings the folder in): on a USB drive or a disk
            // that sleeps, each folder near the cursor was a wait.
            var files = localFilesBackend.requestEntries(path.substring(6))
            return files === undefined ? null : moduleEntries("local:", kLocal, files)
        }
        if (path.indexOf("yt:") === 0) {
            var videos = youtubeBackend.entries(path.substring(3), preview)
            return videos === undefined ? null : moduleEntries("yt:", kYouTube, videos)
        }
        // Jellyfin and Emby
        var prefix = path.substring(0, 2)
        if (servers[prefix] !== undefined) {
            var parentId = path.length > 3 ? path.substring(3) : ""
            var items = playlistsBackend.serverListing(servers[prefix], parentId, preview)
            if (items === undefined)
                return null
            var entries = serverEntries(prefix, items)
            if (parentId === "")
                entries = [{ name: "Continue Watching", path: prefix + ":resume", isFolder: true },
                           { name: "Next Up", path: prefix + ":nextup", isFolder: true }].concat(entries)
            return entries
        }
        return []
    }

    function add(item) {
        var result = playlistsBackend.addEntry(playlistId, item.module, item.entry)
        outcome = result.ok ? (result.downloading ? "Added, to download: " : "Added: ") + result.title
                : result.reason === "duplicate" ? "Already on " + playlistName + ": " + result.title
                : "This one can't go on a playlist"
    }

    // The servers' folders as they are now, not as they were last time.
    Component.onCompleted: {
        if (playlistsBackend)
            playlistsBackend.forgetListings()
        // Local Files' folders read afresh too, so what was copied in since
        // shows.
        if (typeof localFilesBackend !== "undefined" && localFilesBackend)
            localFilesBackend.clearDirectoryCache()
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: "Add to " + addRoot.playlistName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    TreeBrowser {
        id: tree
        anchors.fill: parent
        focus: !osk.visible
        rootPath: "sources"
        // The help line's room, while it shows (Help Line on).
        reservedBottom: helpLine.shown ? areaBottom - helpLine.y : 0
        fetch: function(path, preview) { return addRoot.fetch(path, preview) }
        onActivated: function(item) {
            if (item.kind === "video") {
                addRoot.add(item)
            } else if (item.kind === "search") {
                addRoot.searchIn = item.path.substring(0, item.path.indexOf(":") + 1)
                osk.open("")
            } else if (item.kind === "more") {
                youtubeBackend.loadMore(item.entry.path.replace("#more", ""))
            }
        }
        onCurrentEntryChanged: addRoot.outcome = ""
        onLeaveRequested: addRoot.goBack()
    }

    Connections {
        target: typeof youtubeBackend !== "undefined" ? youtubeBackend : null
        ignoreUnknownSignals: true
        function onListingReady(path) { tree.refresh("yt:" + path) }
    }
    Connections {
        target: typeof localFilesBackend !== "undefined" ? localFilesBackend : null
        ignoreUnknownSignals: true
        function onSearchReady(path) { tree.refresh("local:" + path) }
        function onEntriesReady(path) { tree.refresh("local:" + path) }
        // A USB drive plugged in or taken out, as in Local Files' own tree.
        function onDrivesChanged(gone) {
            for (var i = 0; i < gone.length; ++i)
                tree.leave("local:" + gone[i])
            tree.refresh("local:" + localFilesBackend.mediaRoot(), true)
        }
    }
    Connections {
        target: playlistsBackend
        function onServerListingReady(moduleId, parentId) {
            var prefix = moduleId === addRoot.kJellyfin ? "jf" : "em"
            tree.refresh(parentId === "" ? prefix : prefix + ":" + parentId)
        }
    }

    HelpLine {
        id: helpLine
        visible: !osk.visible
        text: addRoot.outcome !== "" ? addRoot.outcome
              : tree.currentEntry && tree.currentEntry.kind === "video"
                ? (addRoot.offline && tree.currentEntry.module !== addRoot.kLocal
                   ? "Select adds it to " + addRoot.playlistName + ", to download once"
                   : "Select adds it to " + addRoot.playlistName)
              : "Choose the videos to add"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        visible: !osk.visible
        readonly property var entry: tree.currentEntry
        text: root.hints.back + ":BACK " + root.hints.arrows + ":NAVIGATE " + root.hints.select
              + (!entry || entry.isFolder ? ":OPEN"
                 : entry.kind === "video" ? ":ADD"
                 : entry.kind === "search" ? ":SEARCH" : ":MORE")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    OnScreenKeyboard {
        id: osk
        anchors.fill: parent
        title: addRoot.searchIn === "local:" ? "Search Local Files" : "Search YouTube"
        onAccepted: function(text) {
            tree.forceActiveFocus()
            var path = "search/" + text
            // Afresh: the folder may have changed since the same words last ran.
            if (addRoot.searchIn === "local:") {
                localFilesBackend.search(path, text, true)
                tree.refresh("local:" + path)
            }
            tree.openItem({ name: "Search: " + text, path: addRoot.searchIn + path })
        }
        onCanceled: tree.forceActiveFocus()
    }
}
