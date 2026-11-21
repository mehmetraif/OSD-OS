import QtQuick

// A media server's list with an A–Z panel beside it (Plex's, Jellyfin's and
// Emby's lists): the rows in the pane, a MenuList's (its cursor round the
// ends, Flat Panel's pages; no ▲ ▼, as a module's lists have none), each
// the host's label for it (labelOf) on one line in the selected line's box,
// one too long for the line scrolled through while the cursor is on it, and
// in a monitor layout's frame the layout's row (MonitorRow). With letters on,
// for a list in the order of its letters, the letters its rows are under go
// down a column beside them (sortKey: A–Z, or "#", first): ► takes the keys
// there, on the letter of the row the cursor is on, ▲ ▼ jump the rows to the
// first under each letter, round the ends, as the mouse over a letter does,
// and select, ◄ or back go back to the rows. The side without the keys is
// dimmed the two-colour way (Dither).
// Select on a row is the host's (chosen), and back on the rows.
//
//     LetterList {
//         id: itemList
//         rows: items
//         letters: true
//         labelOf: function(row) { return row.title }
//         onChosen: itemListRoot.selectItem()
//     }
//
// As the rows come in, restore(navListState) puts the cursor back where it
// was as the list was left.
FocusScope {
    id: letterListRoot

    anchors.fill: parent

    property var rows: []
    // The A–Z panel, for a list in the order of its letters.
    property bool letters: false
    // The letter a row is under (bucket()). By default the first of the
    // server's sort title (titleKey()).
    property var sortKey: function(row) { return letterListRoot.titleKey(row) }
    // What a row says.
    property var labelOf: function(row) { return (row && row.title) || "" }

    property alias currentIndex: itemList.currentIndex
    readonly property alias count: itemList.count
    // The keys in the letters, the rows dimmed meanwhile.
    property bool letterNavActive: false
    // The letters there are, '#' first: [{ letter, firstIndex }], each with
    // the first row under it.
    readonly property var letterIndex: letters ? buildLetterIndex(rows) : []

    signal chosen(int index)

    // First character as a bucket label; everything non-alphabetic shares '#'.
    function bucket(text) {
        var ch = text.charAt(0).toUpperCase()
        return (ch >= 'A' && ch <= 'Z') ? ch : '#'
    }

    // Sort key for an item: take the first A–Z char of the server's sort name
    // (which the browse queries also sort by, and which already has articles
    // stripped per the server's config), else group under "#". Falls back to
    // stripping a leading article from the display title when no sort name came
    // back — otherwise the letters would disagree with the list order.
    function titleKey(item) {
        var sortTitle = (item && item.titleSort) || ""
        var t = (sortTitle || (item && item.title) || "").toLowerCase()
        if (!sortTitle) {
            var articles = ["the ", "a ", "an "]
            for (var i = 0; i < articles.length; i++) {
                if (t.indexOf(articles[i]) === 0) { t = t.substring(articles[i].length); break }
            }
        }
        return bucket(t)
    }

    // Highlights the letter matching the currently selected item.
    function syncLetterToItem() {
        if (letterIndex.length === 0) return
        var curLetter = sortKey(rows[itemList.currentIndex])
        for (var i = 0; i < letterIndex.length; i++) {
            if (letterIndex[i].letter === curLetter) { letterList.currentIndex = i; break }
        }
        letterList.positionViewAtIndex(letterList.currentIndex, ListView.Contain)
    }

    // Build [{letter, firstIndex}] over the (already alpha-sorted) item list.
    function buildLetterIndex(itemArr) {
        var seen = {}
        var result = []
        for (var i = 0; i < itemArr.length; i++) {
            var letter = sortKey(itemArr[i])
            if (!seen[letter]) {
                seen[letter] = true
                result.push({ letter: letter, firstIndex: i })
            }
        }
        result.sort(function(a, b) {
            if (a.letter === '#') return -1
            if (b.letter === '#') return 1
            return a.letter < b.letter ? -1 : 1
        })
        return result
    }

    // The letters after ▲ ▼ moved the cursor (delta 1 down, -1 up): the
    // row's own, or, round an end of the list, the letters round theirs.
    function follow(delta) {
        if (delta > 0 && itemList.currentIndex === 0) {
            letterList.currentIndex = 0
            letterList.positionViewAtIndex(letterList.currentIndex, ListView.Contain)
        } else if (delta < 0 && itemList.currentIndex === itemList.count - 1) {
            letterList.currentIndex = letterIndex.length - 1
            letterList.positionViewAtIndex(letterList.currentIndex, ListView.Contain)
        } else {
            syncLetterToItem()
        }
    }

    // The rows jumped to the first under the letter the cursor is on, that
    // row at the top; in Flat Panel's frame, whose rows come a page at a
    // time, the page it is on (MenuList's show()).
    function jumpToLetter() {
        itemList.currentIndex = letterIndex[letterList.currentIndex].firstIndex
        if (itemList.paged)
            itemList.show(itemList.currentIndex)
        else
            itemList.positionViewAtIndex(itemList.currentIndex, ListView.Beginning)
    }

    // The keys back in the rows.
    function leaveLetters() {
        letterNavActive = false
        itemList.forceActiveFocus()
    }

    // The row the cursor was on as the list was left (a view's navListState),
    // once the rows have come in: the item it was on (state.itemId, a row's
    // itemId), wherever it is now, else the same place within the list, the
    // last row of one grown shorter.
    function restore(state) {
        if (rows.length === 0)
            return
        var at = -1
        for (var i = 0; state.itemId !== undefined && i < rows.length && at < 0; i++) {
            if (rows[i].itemId === state.itemId)
                at = i
        }
        if (at < 0)
            at = Math.min(state.currentIndex !== undefined ? state.currentIndex : 0, rows.length - 1)
        itemList.currentIndex = at
        itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
    }

    // The rows: in the pane, less the letters' room beside it, and down to
    // where the help line would be, a module's list having none.
    MenuList {
        id: itemList
        model: letterListRoot.rows
        width: root.paneWidth - (letterListRoot.letters ? root.sw * 0.096875 : 0) //430 or 492
        height: root.tallListHeight //252
        marks: false
        focus: true

        onMoved: function(delta) { letterListRoot.follow(delta) }
        Keys.onReturnPressed: letterListRoot.chosen(itemList.currentIndex)
        Keys.onPressed: function(event) {
            // Right hands focus to the letter panel, synced to the current letter.
            if (event.key === Qt.Key_Right && letterListRoot.letters && letterListRoot.letterIndex.length > 0) {
                letterListRoot.syncLetterToItem()
                letterListRoot.letterNavActive = true
                letterList.forceActiveFocus()
                event.accepted = true
            }
        }

        delegate: Item {
            width: itemList.width
            height: root.menuRowHeight

            // The row as a monitor layout draws one, in its frame (Main.qml's):
            // the layout's cursor, its case, its colours.
            Loader {
                anchors.fill: parent
                active: root.monitorInk === true
                sourceComponent: MonitorRow {
                    label: textClip.text
                    selected: itemList.currentIndex === index && !letterListRoot.letterNavActive
                }
            }

            ScrolledText {
                id: textClip
                visible: root.monitorInk !== true
                width: Math.min(implicitWidth, itemList.width)
                height: parent.height
                on: itemList.currentIndex === index
                elides: false
                text: letterListRoot.labelOf(modelData)
                color: (itemList.currentIndex === index && !letterListRoot.letterNavActive)
                   ? root.cursorInk : root.primaryColor
                font.pixelSize: root.sh * 0.05 //24
                textItem.topPadding: root.sh * 0.0041667 //2
                textItem.leftPadding: root.sw * 0.009375 //6
                textItem.rightPadding: root.sw * 0.009375 //6
                textItem.bottomPadding: root.sh * 0.00625 //3

                Rectangle {
                    z: -1
                    color: root.cursorColor
                    anchors.fill: textClip.textItem
                    visible: itemList.currentIndex === index && !letterListRoot.letterNavActive
                }
            }
        }
    }

    // Dimmed the two-colour way while the other panel has the focus.
    Dither {
        anchors.fill: itemList
        visible: itemList.visible && letterListRoot.letterNavActive
    }

    // A–Z letter navigation panel
    ListView {
        id: letterList
        model: letterListRoot.letterIndex
        visible: letterListRoot.letters && letterListRoot.letterIndex.length > 0
        anchors.left: itemList.right
        anchors.leftMargin: root.sw * 0.0375 //24
        anchors.top: itemList.top
        width: root.sw * 0.0328125 //21
        height: itemList.height
        clip: true
        focus: false

        Keys.onUpPressed: {
            if (count === 0) return
            if (currentIndex > 0) {
                currentIndex--
            }
            else {
                currentIndex = count - 1
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Beginning)
            }
            letterListRoot.jumpToLetter()
        }
        Keys.onDownPressed: {
            if (count === 0) return
            if (currentIndex < count - 1) {
                currentIndex++
            }
            else {
                currentIndex = 0
                letterList.positionViewAtIndex(letterList.currentIndex, ListView.Beginning)
            }
            letterListRoot.jumpToLetter()
        }
        // The mouse over a letter (Hover.js), which follows the letters only
        // while the keys are in them: the rows jump to it, as ▲ ▼ take them.
        function hoverRow(index, list) {
            if (index === currentIndex)
                return
            currentIndex = index
            letterListRoot.jumpToLetter()
        }

        Keys.onReturnPressed: letterListRoot.leaveLetters()
        Keys.onLeftPressed: letterListRoot.leaveLetters()
        Keys.onPressed: function(event) {
            if (root.isBack(event)) {
                letterListRoot.leaveLetters()
                event.accepted = true
            }
        }

        delegate: Item {
            width: letterList.width
            height: root.sh * 0.04375 //21

            Rectangle {
                color: root.cursorColor
                anchors.fill: parent
                visible: letterList.currentIndex === index && letterListRoot.letterNavActive
            }

            Text {
                text: modelData.letter
                color: (letterList.currentIndex === index && letterListRoot.letterNavActive)
                       ? root.cursorInk : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0354167 //17
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                rightPadding: root.sw * 0.009375 //6
                topPadding: root.sh * 0.0041667 //2
                bottomPadding: root.sh * 0.00625 //3
            }
        }
    }

    // Dimmed the two-colour way while the other panel has the focus.
    Dither {
        anchors.fill: letterList
        visible: letterList.visible && !letterListRoot.letterNavActive
    }
}
