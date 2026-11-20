import QtQuick

// Reusable list browser — handles movies, series, and episodes via navParams.
// Its rows and A–Z panel are a LetterList, as Plex's Items.qml's are.
// Jellyfin's and Emby's: a module's Items.qml is this with its backend (see
// MediaServerLibraries).
FocusScope {
    id: itemListRoot
    // In the layout's frame (Main.qml): its list in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    // The module's backend (an EmbyApiBackend: jellyfinBackend, embyBackend),
    // which its view file names.
    property var backend: null

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string parentId: navParams.parentId || ""
    property string libraryName: navParams.libraryName || ""
    property string includeTypes: navParams.includeTypes || "Movie"
    property string mode: navParams.mode || "browse"   // "browse", "resume", or "up_next"

    property var items: []
    property bool isLoading: false
    property string errorMessage: ""

    // A–Z letter-jump panel — only for the alphabetized lists ("browse", a
    // folder's, a box set's); resume/up_next are not alpha-sorted.
    property bool showLetterNav: mode === "browse" || mode === "folder" || mode === "boxset"

    // A list come in: shown, the cursor back where it was as the list was
    // left (LetterList's restore()). Continue Watching and Next Up reorder as
    // an item is played, so the item is looked for first there; the other
    // lists keep their order, and the cursor its row.
    function show(loadedItems) {
        isLoading = false
        errorMessage = ""
        items = loadedItems
        itemList.restore(mode === "resume" || mode === "up_next" ? navListState
                         : { currentIndex: navListState.currentIndex })
    }

    Connections {
        target: backend

        function onItemsLoaded(loadedItems) {
            if (itemListRoot.mode === "browse") itemListRoot.show(loadedItems)
        }
        function onFolderChildrenLoaded(loadedItems) {
            if (itemListRoot.mode === "folder") itemListRoot.show(loadedItems)
        }
        function onBoxsetChildrenLoaded(loadedItems) {
            if (itemListRoot.mode === "boxset") itemListRoot.show(loadedItems)
        }
        function onContinueWatchingLoaded(loadedItems) {
            if (itemListRoot.mode === "resume") itemListRoot.show(loadedItems)
        }
        function onUpNextLoaded(loadedItems) {
            if (itemListRoot.mode === "up_next") itemListRoot.show(loadedItems)
        }

        // The backend's errors are every request's: this list's own only
        // while it waits for its rows. One that comes once they are shown
        // (a request of a page left, failing late) is not drawn over them;
        // one taken while waiting goes as the rows come after all (show()).
        function onErrorOccurred(msg) {
            console.log("[" + moduleRoot.moduleName + " Items] Error: " + msg)
            if (!itemListRoot.isLoading)
                return
            itemListRoot.isLoading = false
            itemListRoot.errorMessage = msg
        }
    }

    Component.onCompleted: {
        errorMessage = ""
        if (mode === "static") {
            // Pre-filtered list handed over by Boxset.qml — no fetch needed.
            show(navParams.items || [])
            return
        }
        isLoading = true
        if (mode === "folder")
            backend.load_folder_children(parentId)
        else if (mode === "boxset")
            backend.load_boxset_children(parentId)
        else if (mode === "resume")
            backend.load_continue_watching()
        else if (mode === "up_next")
            backend.load_up_next()
        else
            backend.load_items(parentId, includeTypes, "SortName")
    }

    focus: true
    Keys.onPressed: function(event) {
        if (root.isBack(event)) {
            goBack()
            event.accepted = true
        }
    }

    // ---
    // UI
    // ---

    // Header
    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        // libraryName is the persistent section label (= title in every mode
        // except "static", where title is the category name but libraryName is
        // the box-set name) — keeps the box-set name consistent down the chain.
        subtitle: libraryName
    }

    // Loading / empty / error states
    PaneMessage {
        visible: isLoading
        text: "LOADING..."
    }
    PaneMessage {
        visible: !isLoading && errorMessage !== ""
        text: errorMessage
    }
    PaneMessage {
        visible: !isLoading && errorMessage === "" && items.length === 0
        text: "NO ITEMS FOUND"
    }

    // Body — Plex-style text list, and the A–Z panel beside it.
    LetterList {
        id: itemList
        rows: itemListRoot.items
        letters: itemListRoot.showLetterNav
        // An episode with its show, season and number (S1E2), as Plex's
        // lists show it.
        labelOf: function(item) {
            if (item.type === "episode" && item.grandparentTitle) {
                var sNum = (item.parentIndex != null) ? item.parentIndex : "?"
                var eNum = (item.index || item.index === 0) ? item.index : "?"
                return item.grandparentTitle + " S" + sNum + "E" + eNum + ": " + (item.title || "")
            }
            var base = item.title || ""
            return item.year ? base + " (" + String(item.year) + ")" : base
        }
        focus: true
        onChosen: itemListRoot.selectItem()
    }

    function selectItem() {
        var item = items[itemList.currentIndex]
        if (!item) return
        // Where the cursor was, and on which item, for back to put it again.
        var here = { currentIndex: itemList.currentIndex, itemId: item.itemId }

        if (item.type === "series") {
            itemListRoot.navigateTo("ItemShow.qml", { item: item, libraryName: libraryName }, here)
        } else if (item.type === "boxset") {
            itemListRoot.navigateTo("Boxset.qml", { item: item, libraryName: libraryName }, here)
        } else if (item.isFolder) {
            // homevideos folder — drill into its direct children, keeping the
            // persistent library name in the header.
            itemListRoot.navigateTo("Items.qml", {
                parentId: item.itemId,
                libraryName: libraryName,
                mode: "folder"
            }, here)
        } else {
            itemListRoot.navigateTo("Item.qml", { item: item, libraryName: libraryName }, here)
        }
    }

    // Footer
    HintBar {
        id: footer
        text: root.hintLine(["back:BACK", "navigate:NAVIGATE", showLetterNav ? "browse:BROWSE" : "", "select:SELECT"])
    }
}
