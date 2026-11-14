import QtQuick

// Flat Panel's pages for a list (Settings → Look → Menu Layout): in its
// frame a menu's rows come a page at a time, as a flat panel's do, and the
// frame writes which page at its top right ("1/2", root.chrome's pageTo()).
// This sees to it for one list, a ListView of rows one under another, each
// as high as the next: the cursor's page turned to as the cursor, the rows
// or the list's height change; its last page filled out to a whole one, so
// that every page's top is within the list's bounds; and the list left
// where its page puts it, not moved to follow the cursor. In any other
// layout it does nothing. A list that pages itself (one with a footer of
// its own: Settings', the main menu's) is left to.
//
// ScrollMarks holds one for the list it marks; a list without marks has
// its own:
//     ListPager { list: theList }
Item {
    id: pager

    property Flickable list: null
    // A list that shows: one hidden (Camcorder's column, a list under a
    // keyboard) counts no pages.
    readonly property bool paged: list !== null && root.monitorFramed === true
                                  && root.menuLayout === "Flat Panel" && list.orientation === Qt.Vertical
                                  && list.visible
    // The list is this one's to page: it has no footer but the pager's.
    property bool paging: false

    visible: false

    function repage() {
        if (paging && paged)
            root.chrome.pageTo(list)
    }
    function adopt() {
        if (!list)
            return
        if (paged && !paging && (!list.footer || list.footer === pageFill)) {
            paging = true
            list.footer = pageFill
            list.highlightFollowsCurrentItem = false
        } else if (!paged && paging) {
            paging = false
            list.highlightFollowsCurrentItem = true
            if (list.currentIndex >= 0)
                list.positionViewAtIndex(list.currentIndex, ListView.Contain)
        }
        repage()
    }
    onPagedChanged: adopt()
    Component.onCompleted: adopt()

    Component {
        id: pageFill
        Item {
            width: 1
            height: pager.paging && pager.paged ? root.chrome.pageFill(pager.list) : 0
        }
    }
    Connections {
        target: pager.paging ? pager.list : null
        function onCurrentIndexChanged() { pager.repage() }
        function onCountChanged() { Qt.callLater(pager.repage) }
        function onHeightChanged() { Qt.callLater(pager.repage) }
    }
}
