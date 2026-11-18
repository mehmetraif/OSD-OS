import QtQuick
import Components

// Reusable list view — handles all listType values via navParams (kinds:
// what loads each, what answers it, what its rows open), its rows and A–Z
// panel a LetterList.
FocusScope {
    id: itemListRoot
    // In the layout's frame (Main.qml): its list in the pane, beside the
    // column a layout keeps on the screen.
    readonly property bool inFrame: true

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string listType: navParams.listType || ""
    property string listTitle: navParams.title || ""
    property string sectionId: navParams.sectionId || ""
    property string hubKey: navParams.hubKey || ""
    property string ratingKey: navParams.ratingKey || ""
    property string categoryKey: navParams.categoryKey || ""
    property string libraryName: navParams.libraryName || ""
    // Folder browsing: the key of the folder being listed (empty at a section's
    // top level) and the trail of folder names walked to reach it. The trail is
    // appended to the library name in the header, so a deep folder says where it
    // is — the leaf on its own is ambiguous, since the "Season 1" folders of two
    // different shows would give identical headers.
    property string folderKey: navParams.folderKey || ""
    property string folderTrail: navParams.folderTrail || ""

    property var items: []
    property bool isLoading: false
    property string errorMessage: ""

    // A folder listing only earns the panel when it agrees with the rows it
    // points into — see letterNavUsable, which sets this as the listing loads.
    // library_all is always server-sorted, so it needs no such test.
    property bool folderLetterNav: false
    property bool showLetterNav: listType === "library_all"
                                 || (listType === "folders" && folderLetterNav)

    // ----------------------------------------------------------------
    // PLAY ALL / SHUFFLE rows — curated sets can be played as a queue.
    // They are prepended as virtual rows rather than mixed into `items`, so
    // `items` stays exactly what the backend sent and every index into it has to
    // go through actionRows.length. `rows` is what the ListView actually shows.
    // ----------------------------------------------------------------
    property bool canQueue: listType === "playlist_items" || listType === "collection_items"
    // Passed to the backend as-is: seasons were flattened when the list loaded,
    // so a row is either a leaf or a show, and expand_queue fans the shows out
    // into their episodes. The ordering (and any shuffle) happens after that
    // expansion, in QueuePlay.qml — shuffling here would randomize shows, not
    // episodes.
    property var queueItems: canQueue
        ? items.filter(function(i) { return i && i.ratingKey })
        : []
    // The write row rides on the same warrant as the play rows, so the three stay
    // one block: a set worth playing as a queue is a set worth putting on a card.
    property var actionRows: !queueRowsWarranted(queueItems) ? []
        : [{ __action: "play_all", title: "» PLAY ALL" },
           { __action: "shuffle",  title: "~ SHUFFLE"  }]
          .concat(cardWriter.available
                  ? [{ __action: "write_card", title: "@ WRITE NFC CARD" }] : [])
    property var rows: actionRows.concat(items)

    // One show is already a queue — all of its episodes — so it earns the rows on
    // its own, where a lone movie does not.
    function queueRowsWarranted(candidates) {
        if (candidates.length > 1) return true
        return candidates.length === 1 && candidates[0].type === "show"
    }

    // '#' sorts ahead of the letters, matching the panel's own ordering.
    function bucketRank(key) {
        return key === '#' ? 0 : key.charCodeAt(0)
    }

    // Buckets by the server's sort title when one is set (Plex sorts the list by
    // titleSort), otherwise falls back to article-stripping the display title —
    // which is what Plex itself does for items without a custom sort title
    // (LetterList's titleKey()).
    //
    // Folder listings are the exception: there a row *is* the name on disk, so it
    // buckets on the literal first character. Article-stripping is metadata logic,
    // and it makes the panel disagree with what is on screen — a library filed
    // into letter folders keeps "An Inconvenient Truth" in A/ on disk, but
    // stripping the article would bucket it under I.
    function sortKey(item) {
        if (listType === "folders") return itemList.bucket((item && item.title) || "")
        return itemList.titleKey(item)
    }

    // The panel is only useful when it agrees with the list it jumps into: the
    // rows have to already be in bucket order, and there has to be more than one
    // bucket to jump between. Plex returns a folder's subfolders ahead of its
    // loose files, each group sorted on its own, so a folder holding both
    // restarts the alphabet — A B C A B C — and a letter present in both groups
    // could only ever reach the folder. Testing the whole listing catches that
    // along with any other ordering surprise, and it also drops the panel for a
    // single-bucket folder, where every row shares one letter anyway.
    function letterNavUsable(itemArr) {
        if (itemArr.length === 0) return false
        var distinct = 1
        var prev = sortKey(itemArr[0])
        for (var i = 1; i < itemArr.length; i++) {
            var key = sortKey(itemArr[i])
            if (bucketRank(key) < bucketRank(prev)) return false
            if (key !== prev) distinct++
            prev = key
        }
        return distinct > 1
    }

    // ----------------------------------------------------------------
    // The kinds of list, by listType: what loads one (load), the backend's
    // signal that answers it (answer) and, for a list of lists, the list a
    // row opens (opens: its Items.qml's parameters). Any other row opens by
    // its own type (selectItem()).
    // ----------------------------------------------------------------
    readonly property var kinds: ({
        "library_all":       { answer: "itemsLoaded",
                               load: function() { plexBackend.load_library_all(sectionId) } },
        "hub_items":         { answer: "itemsLoaded",
                               load: function() { plexBackend.load_items_for_hub(hubKey) } },
        "collection_items":  { answer: "itemsLoaded",
                               load: function() { plexBackend.load_collection_items(ratingKey) } },
        "playlist_items":    { answer: "itemsLoaded",
                               load: function() { plexBackend.load_playlist_items(ratingKey) } },
        "category_items":    { answer: "itemsLoaded",
                               load: function() { plexBackend.load_category_items(sectionId, categoryKey) } },
        "folders":           { answer: "folderLoaded",
                               load: function() { plexBackend.load_folder(sectionId, folderKey) } },
        "continue_watching": { answer: "continueWatchingLoaded",
                               load: function() { plexBackend.load_continue_watching() } },
        "hubs": {
            answer: "hubsLoaded",
            load: function() { plexBackend.load_section_hubs(sectionId) },
            // Hub selected → load items for that hub
            opens: function(item) {
                return { listType: "hub_items", title: item.title, hubKey: item.hubKey || item.key,
                         libraryName: libraryName }
            }
        },
        "collections": {
            answer: "collectionsLoaded",
            load: function() { plexBackend.load_collections(sectionId) },
            opens: function(item) {
                return { listType: "collection_items", title: item.title, ratingKey: item.ratingKey,
                         libraryName: libraryName }
            }
        },
        "playlists": {
            answer: "playlistsLoaded",
            load: function() { plexBackend.load_playlists(sectionId) },
            opens: function(item) {
                return { listType: "playlist_items", title: item.title, ratingKey: item.ratingKey,
                         libraryName: libraryName }
            }
        },
        "categories": {
            answer: "categoriesLoaded",
            load: function() { plexBackend.load_categories(sectionId) },
            // Boolean filters (e.g. hdr) have no directory listing — apply directly
            opens: function(item) {
                return { listType: "category_items", title: item.title, sectionId: sectionId,
                         categoryKey: item.filterType === "boolean" ? item.key + "=1" : item.key,
                         libraryName: libraryName }
            }
        }
    })
    // This view's, or null for a listType there is none of.
    readonly property var kind: kinds[listType] || null
    // Whether the backend's answer, by its signal's name, is this view's list.
    function answers(signal) {
        return kind !== null && kind.answer === signal
    }

    // The list come in: shown (a folder's with the letters if they agree
    // with it), the cursor back where it was as the list was left. The saved
    // index is a *row* index, PLAY ALL and SHUFFLE among the rows
    // (LetterList's restore()).
    function show(loadedItems) {
        isLoading = false
        items = loadedItems
        if (listType === "folders")
            folderLetterNav = letterNavUsable(loadedItems)
        itemList.restore(navListState)
    }

    Connections {
        target: plexBackend

        function onItemsLoaded(loadedItems) {
            if (itemListRoot.answers("itemsLoaded")) itemListRoot.show(loadedItems)
        }
        function onContinueWatchingLoaded(loadedItems) {
            if (itemListRoot.answers("continueWatchingLoaded")) itemListRoot.show(loadedItems)
        }
        function onHubsLoaded(loadedHubs) {
            if (itemListRoot.answers("hubsLoaded")) itemListRoot.show(loadedHubs)
        }
        function onCollectionsLoaded(loadedItems) {
            if (itemListRoot.answers("collectionsLoaded")) itemListRoot.show(loadedItems)
        }
        function onPlaylistsLoaded(loadedItems) {
            if (itemListRoot.answers("playlistsLoaded")) itemListRoot.show(loadedItems)
        }
        function onFolderLoaded(loadedItems) {
            if (itemListRoot.answers("folderLoaded")) itemListRoot.show(loadedItems)
        }
        function onCategoriesLoaded(loadedItems) {
            if (itemListRoot.answers("categoriesLoaded")) itemListRoot.show(loadedItems)
        }

        function onErrorOccurred(msg) {
            if (itemListRoot.listType !== "") {
                itemListRoot.isLoading = false
                itemListRoot.errorMessage = msg
            }
            console.log("[ItemList] Error: " + msg)
        }
    }

    // ----------------------------------------------------------------
    // Select item — navigate based on type
    // ----------------------------------------------------------------

    function selectItem() {
        var item = rows[itemList.currentIndex]
        if (!item) return

        // Virtual rows — act on the whole set instead of drilling into one item.
        if (item.__action === "write_card") {
            cardWriter.open()
            return
        }
        if (item.__action) {
            itemListRoot.navigateTo("QueuePlay.qml", {
                queueItems: queueItems,
                shuffle: item.__action === "shuffle",
                title: listTitle,
                libraryName: libraryName
            }, { currentIndex: itemList.currentIndex })
            return
        }

        // A folder row opens the next level down. The server's own key is passed
        // back untouched, so the section id only matters at the top level.
        if (item.type === "folder") {
            itemListRoot.navigateTo("Items.qml", {
                listType: "folders",
                title: item.title,
                sectionId: sectionId,
                folderKey: item.folderKey,
                folderTrail: (folderTrail !== "" ? folderTrail + " / " : "") + item.title,
                libraryName: libraryName
            }, { currentIndex: itemList.currentIndex })
            return
        }

        // Intermediate lists that navigate deeper: the list the row is.
        if (kind && kind.opens) {
            itemListRoot.navigateTo("Items.qml", kind.opens(item), { currentIndex: itemList.currentIndex })
            return
        }

        // For category_items the items are sub-filter values (e.g. genre names),
        // not actual media. Navigate further if type is 'genre_item'.
        if (item.type === "genre_item") {
            // item.ratingKey is actually the filter value key from the server
            // Use it to load actual media items
            itemListRoot.navigateTo("Items.qml", {
                listType: "category_items",
                title: item.title,
                sectionId: item._sectionId || sectionId,
                categoryKey: item._filterKey + "=" + encodeURIComponent(item.ratingKey),
                libraryName: libraryName
            }, { currentIndex: itemList.currentIndex })
            return
        }

        // TV Show → go to Show detail view
        if (item.type === "show") {
            itemListRoot.navigateTo("ItemShow.qml", {
                item: item,
                libraryName: libraryName
            }, { currentIndex: itemList.currentIndex })
            return
        }

        // Actual media item (movie, episode, other) → go to detail
        itemListRoot.navigateTo("Item.qml", {
            item: item,
            libraryName: libraryName
        }, { currentIndex: itemList.currentIndex })
    }

    // ----------------------------------------------------------------
    // Data loading on appear
    // ----------------------------------------------------------------

    Component.onCompleted: {
        isLoading = true
        errorMessage = ""
        if (kind)
            kind.load()
    }

    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
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
        subtitle: folderTrail !== "" ? libraryName + " / " + folderTrail : libraryName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Loading / empty / error states
    Text {
        visible: isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        // In the middle of the pane.
        x: root.paneX
        anchors.verticalCenter: parent.verticalCenter
        width: root.paneWidth
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !isLoading && errorMessage !== ""
        text: errorMessage
        color: root.tertiaryColor
        font.family: root.globalFont
        // In the middle of the pane, and as wide.
        x: root.paneX
        anchors.verticalCenter: parent.verticalCenter
        width: root.paneWidth
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }
    Text {
        visible: !isLoading && errorMessage === "" && items.length === 0
        text: "NO ITEMS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        // In the middle of the pane.
        x: root.paneX
        anchors.verticalCenter: parent.verticalCenter
        width: root.paneWidth
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: root.sh * 0.05 //24
    }

    // Body: the rows and, for a library's or a folder's, the A–Z panel.
    LetterList {
        id: itemList
        rows: itemListRoot.rows
        letters: itemListRoot.showLetterNav
        sortKey: function(item) { return itemListRoot.sortKey(item) }
        labelOf: function(item) {
            if (item.type === "folder") return (item.title || "") + "/"
            if (item.type === "episode" && item.grandparentTitle) {
                var sNum = (item.parentIndex != null) ? item.parentIndex : "?"
                var eNum = (item.index != null) ? item.index : "?"
                return item.grandparentTitle + " S" + sNum + "E" + eNum
                       + ": " + (item.title || "")
            }
            var base = item.title || ""
            return item.editionTitle ? base + " (" + item.editionTitle + ")" : base
        }
        focus: true
        onChosen: itemListRoot.selectItem()
    }

    // Footer
    HintBar {
        id: footer
        text: showLetterNav
              ? root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.browse + ":BROWSE " + root.hints.select + ":SELECT"
              : root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SELECT"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Writes a card for the whole set. The ref carries the collection's or
    // playlist's ratingKey — these are server-local, user-created objects with no
    // metadata-agent guid to be portable with — and the rows behind it are
    // resolved when the card is tapped, so the card follows the set.
    //
    // The kind suffix keeps a card file readable on disk, the way the year does
    // for a movie: "80s Action (Collection).txt" reads apart from the movie of
    // the same name.
    NfcCardWriter {
        id: cardWriter
        anchors.fill: parent
        offerShuffle: true
        // A set of movies has no episodes to sequence, so the show/season wording
        // would read wrong here.
        orderedLabel: "In Order"
        shuffleLabel: "Shuffled"
        cardRef: (itemListRoot.canQueue && itemListRoot.ratingKey !== "")
                 ? "plex://" + (itemListRoot.listType === "collection_items" ? "collection" : "playlist")
                   + "/" + itemListRoot.ratingKey
                 : ""
        cardTitle: itemListRoot.listTitle
                   + (itemListRoot.listType === "collection_items" ? " (Collection)" : " (Playlist)")
        onClosed: itemList.forceActiveFocus()
    }
}
