import QtQuick
import Components

// Local Files browser: the media folder as a horizontal tree (see
// TreeBrowser), led by RECENTLY WATCHED, FAVORITES and SEARCH (names under the
// whole folder and the drives, typed on the on-screen keyboard) and the USB
// drives plugged in, which come and go as they are. Select on a file plays it,
// right on it offers its options (EntryOptions: its favourite, PLAY AT
// STARTUP), and the whole tree is this one view: playing a file and coming
// back restores it from the listState handed to navigateTo. Opened with the
// startup favourite (navParams.startupPlay), it plays that at once.
FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})
    // Context properties read null while the module's Loader tears this view
    // down, hence the guards.
    readonly property string rootPath: navParams.folderPath || (localFilesBackend ? localFilesBackend.mediaRoot() : "")
    readonly property bool hideExtensions: {
        var v = appCore ? appCore.get_setting(moduleRoot.moduleId, "hide_extensions") : false
        return v === true || v === "ON"
    }

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    // Plays a file, putting it on RECENTLY WATCHED; trail is where coming back
    // lands, and startup says it is the favourite played at startup.
    function play(item, trail, startup) {
        appCore.add_to_list(moduleRoot.moduleId, "recent",
                            { name: item.name, path: item.path, isFolder: false }, 30)
        itemsRoot.navigateTo("Player.qml", { filePath: item.path, title: item.name, startup: !!startup },
                             { trail: trail })
    }

    // The startup favourite, played as if chosen in FAVORITES, so coming back
    // from it lands there, or the file behind the menus (the main menu's row
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
        itemsRoot.navigateTo("Player.qml", params,
                             { trail: [{ path: itemsRoot.rootPath, sel: 0, name: "", pushed: false },
                                       { path: "recent", sel: 0, name: "Recently Watched", pushed: false }] })
    }
    function playAtStartup(entry) {
        var favorites = localFilesBackend.entries("favorites")
        for (var i = 0; i < favorites.length; ++i) {
            if (favorites[i].path === entry.path) {
                play(favorites[i], [{ path: itemsRoot.rootPath, sel: 1, name: "", pushed: false },
                                    { path: "favorites", sel: i, name: "Favorites", pushed: false }], true)
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

    // Empty state
    Column {
        anchors.centerIn: parent
        spacing: root.sh * 0.0333333 //16
        visible: tree.rootEmpty
        Text {
            text: "No items found"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.05 //24
        }
        Text {
            text: "Please add items in the local files media directory"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: root.sh * 0.0333333 //16
        }
    }

    TreeBrowser {
        expandedFolderPreviews: true
        id: tree
        anchors.fill: parent
        focus: true
        visible: !rootEmpty
        rootPath: { if (localFilesBackend) localFilesBackend.clearDirectoryCache(); return itemsRoot.rootPath }
        savedTrail: itemsRoot.navListState.trail || []
        fetch: function(path, preview) {
            if (!localFilesBackend)
                return []
            var entries = localFilesBackend.requestEntries(path)
            return entries === undefined ? null : entries
        }
        labelOf: function(item) {
            if (item.isFolder || item.kind || !itemsRoot.hideExtensions) return item.name
            var dot = item.name.lastIndexOf(".")
            return dot > 0 ? item.name.substring(0, dot) : item.name
        }
        onActivated: function(item) {
            if (item.kind === "search") {
                osk.open("")
                return
            }
            itemsRoot.play(item, tree.trailState())
        }
        onOptionsRequested: function(item) {
            if (!item.kind)
                options.offer({ name: item.name, path: item.path, isFolder: false })
        }
        onLeaveRequested: itemsRoot.goBack()
    }

    Connections {
        target: localFilesBackend
        function onSearchReady(path) { tree.refresh(path) }
        function onEntriesReady(path) { tree.refresh(path) }
        // A drive plugged in or taken out: the top of the tree lists it or
        // not, folders open on one taken out close, and the two lists show
        // its files again or leave them out.
        function onDrivesChanged(gone) {
            for (var i = 0; i < gone.length; ++i)
                tree.leave(gone[i])
            tree.refresh(itemsRoot.rootPath, true)
            tree.refresh("recent", true)
            tree.refresh("favorites", true)
        }
    }

    // Footer
    HintBar {
        id: footer
        visible: !osk.visible && !options.visible
        // Select opens a folder and plays a file, and right on a file offers
        // its options rather than moving.
        readonly property var entry: tree.currentEntry
        readonly property bool onFile: !!entry && !entry.isFolder && !entry.kind
        text: root.hints.back + ":BACK "
              + (onFile ? String(root.hints.navigate).replace("]", "◄]") + ":NAVIGATE "
                          + root.hints.browse + ":OPTIONS "
                        : root.hints.arrows + ":NAVIGATE ")
              + root.hints.select
              + (onFile ? ":PLAY" : entry && entry.kind === "search" ? ":SEARCH" : ":OPEN")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    OnScreenKeyboard {
        id: osk
        anchors.fill: parent
        title: "Search " + moduleRoot.moduleName
        onAccepted: function(text) {
            tree.forceActiveFocus()
            // Afresh: the folder may have changed since the same words last ran.
            var path = "search/" + text
            localFilesBackend.search(path, text, true)
            tree.refresh(path)
            tree.openItem({ name: "Search: " + text, path: path })
        }
        onCanceled: tree.forceActiveFocus()
    }

    EntryOptions {
        id: options
        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        onFavoritesEdited: tree.refresh("favorites")
        onClosed: tree.forceActiveFocus()
    }
}
