import QtQuick

// A catalogue browsed in a TreeBrowser, as YouTube's Items.qml and a web
// player module's WebPlayerBrowse have it: SEARCH on the on-screen keyboard,
// MORE at the end of a folder that has more, and right on what plays (an
// entry of playKind) its info screen (InfoPanel), the tree's last layer, as
// the cursor resting on it does after the app's INFO SCREEN setting's
// seconds; or its options with the info screen off. The view keeps what is
// its own: what each folder lists (fetch), what plays an entry
// (playRequested) and its options (optionsRequested: an EntryOptions
// declared over this, which `covered` says is up, and which gives the keys
// back with refocus()).
FocusScope {
    id: browser

    // What it browses: problem, the line that says what stands in the way;
    // loadMore(path) and loadDetails(item); and the signals listingReady(path)
    // and detailsReady(path, details), whose complete is false while more of
    // them is on its way.
    property var catalog: null
    // The tree's fetch(path, preview).
    property var fetch: function(path, preview) { return [] }
    property var savedTrail: []
    // The kind of the entries that play ("video", "title").
    property string playKind: ""
    property string searchTitle: ""
    // The problem line shows with Help Line off too.
    property bool problemAlways: false
    // The view's options are up, over the problem line and the hints.
    property bool covered: false
    // What select does about the problem in a folder with nothing in it
    // (a web player's SETTINGS, with no TMDB key): its word on the hint bar,
    // and problemActivated() on select. "" for none.
    property string problemAction: ""
    signal problemActivated()
    readonly property alias tree: tree

    signal playRequested(var item)
    signal optionsRequested(var item)
    // Select on an entry that is neither a folder, SEARCH, MORE nor one that
    // plays.
    signal activated(var item)
    signal leaveRequested()
    // Words typed on the keyboard, their folder's path, just before it opens.
    signal searchAccepted(string path)

    // The keys back once the options close: to the info screen when they
    // were opened from there.
    function refocus() {
        if (info.visible)
            info.forceActiveFocus()
        else
            tree.forceActiveFocus()
    }

    // The app's INFO SCREEN setting: "off", "key" (right only) or seconds.
    readonly property string infoSetting: (appCore ? appCore.get_setting("", "info_screen") : "") || "3"

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: tree.folderName
    }

    // Select with nothing under the cursor, which the tree leaves to its
    // host: what the problem line offers.
    readonly property bool offersProblemAction: problemAction !== "" && !tree.currentEntry
                                                && !!catalog && catalog.problem !== ""
    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && offersProblemAction) {
            problemActivated()
            event.accepted = true
        }
    }

    TreeBrowser {
        id: tree
        anchors.fill: parent
        focus: true
        rootPath: "home"
        // The problem line's room, while it shows (Help Line on).
        reservedBottom: problemLine.visible && problemLine.shown ? areaBottom - problemLine.y : 0
        preview: browser.infoSetting !== "off"
        previewDelay: (parseInt(browser.infoSetting) || 0) * 1000
        savedTrail: browser.savedTrail
        fetch: browser.fetch
        onActivated: function(item) {
            if (item.kind === "search")
                osk.open("")
            else if (item.kind === "more")
                browser.catalog.loadMore(item.path.replace("#more", ""))
            else if (item.kind === browser.playKind)
                browser.playRequested(item)
            else
                browser.activated(item)
        }
        onPreviewRequested: function(item) {
            if (item.kind !== browser.playKind || !browser.catalog)
                return
            info.show(item)
            info.loading = true
            browser.catalog.loadDetails(item)
        }
        onOptionsRequested: function(item) {
            if (item.kind === browser.playKind)
                browser.optionsRequested(item)
        }
        onLeaveRequested: browser.leaveRequested()
    }

    Connections {
        target: browser.catalog
        function onListingReady(path) { tree.refresh(path) }
        function onDetailsReady(path, details) {
            if (!info.item || info.item.path !== path)
                return
            info.details = details
            info.loading = details.complete === false
        }
    }

    HelpLine {
        id: problemLine
        visible: !osk.visible && !info.visible && !browser.covered && text !== ""
        text: browser.catalog ? browser.catalog.problem : ""
        always: browser.problemAlways && text !== ""
    }

    HintBar {
        visible: !osk.visible && !info.visible && !browser.covered
        // What select does with the entry under the cursor: what plays plays,
        // and right on it opens its info (or its options, with the info
        // screen off) rather than moving.
        readonly property var entry: tree.currentEntry
        readonly property bool onPlayable: !!entry && entry.kind === browser.playKind
        text: browser.offersProblemAction ? root.hintLine(["back:BACK", "select:" + browser.problemAction])
              : root.hints.back + ":BACK "
              + (onPlayable ? String(root.hints.navigate).replace("]", "◄]") + ":NAVIGATE "
                              + root.hints.browse + (tree.preview ? ":INFO " : ":OPTIONS ")
                            : root.hints.arrows + ":NAVIGATE ")
              + root.hints.select
              + (onPlayable ? ":PLAY"
                 : !entry || entry.isFolder ? ":OPEN"
                 : entry.kind === "search" ? ":SEARCH"
                 : entry.kind === "more" ? ":MORE" : ":OPEN")
    }

    OnScreenKeyboard {
        id: osk
        anchors.fill: parent
        title: browser.searchTitle
        onAccepted: function(text) {
            tree.forceActiveFocus()
            browser.searchAccepted("search/" + text)
            tree.openItem({ name: "Search: " + text, path: "search/" + text })
        }
        onCanceled: tree.forceActiveFocus()
    }

    // An entry's info: the tree's last layer.
    InfoPanel {
        id: info
        anchors.fill: parent
        onPlayRequested: function(item) { browser.playRequested(item) }
        onOptionsRequested: function(item) { browser.optionsRequested(item) }
        onMoveRequested: function(delta) { tree.move(delta) }
        onClosed: tree.forceActiveFocus()
    }
}
