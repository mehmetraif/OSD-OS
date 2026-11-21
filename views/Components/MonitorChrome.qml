import QtQuick

// A monitor layout's frame (Settings → Look → Menu Layout): everything round
// a view's rows, in the layout's own colours (root.menuPalette), after the
// menus of three generations of studio monitors:
//
//   Studio Monitor  a blue window over the picture, all of a piece: the
//                   page's name on a tab that rises from its top left corner,
//                   the top level's icons down its left side (the one shown
//                   on yellow), the key guide on its foot, and inside it,
//                   black with rounded corners, the rows and the help line
//                   in a box under them;
//   Broadcast       plain blue over the whole screen, where one is centred
//                   at the top ("NETWORK / WI-FI"), the help line and a
//                   three-line key guide ("TO SELECT  :[▲▼]") at the foot;
//   Flat Panel      black over the whole screen, the path at the top left
//                   (">Settings >Network >Wi-Fi"), the page the rows are on
//                   at the top right ("1/2" with ▲▼), the help line, a rule
//                   and a one-line key guide at the foot.
//
// The shell draws one (Main.qml, root.chrome), under the views, and keeps it
// up from one view to the next. A view draws no chrome of its own in it: its
// title bar, hint bar and help line (AppBar, HintBar, HelpLine) tell the
// frame what they would have shown (shown()), and the frame writes it the
// layout's way, the title as a path from the top level (basePath, then the
// bar's own), the hint line as the layout's key guide. The parts of whatever
// is in front speak: a question's (an OsdGround laid `inLayout`, a layer)
// before the view's under it.
//
// A view lays its rows where the frame says (listX, listY, listWidth,
// listHeight, or tallHeight with no help line under them; Main.qml hands
// them on as the pane) and draws them with MonitorRow, as MenuRow does by
// itself. The status icons stay at the top right. A hidden Help Line's or
// Hint Bar's room goes to the rows, whole rows of it. Flat Panel's rows go a
// page at a time (pageTo()).
Item {
    id: chrome

    // --- WHAT THE SHELL GIVES IT ---
    // Studio Monitor's strip: the top level's icons, each a PixelIcon's name
    // or { icon }, or a module's logo, { source }; the one shown, and whether
    // the keys are in it (its cell then in the cursor's colour, else framed).
    property var icons: []
    property int iconIndex: 0
    property bool iconsActive: false
    // The way to the view that is open, from the top level down to the view
    // it was opened from (Main.qml's nav stack): [] for one opened from the
    // main menu.
    property var basePath: []
    // The whole screen in the frame's coordinates: the frame itself, or on a
    // screen taller than wide, where the stage is a band, the screen's.
    property rect screen: Qt.rect(0, 0, width, height)

    // --- WHAT THE VIEWS HAND IT ---
    // The bars that show, the latest last: a view's title bar ("title"),
    // hint bar ("hints") and help line ("help"), by their frameRole, and the
    // ground of a layer laid over the view in the layout ("layer").
    property var parts: []
    function shown(part, on) {
        var list = parts.filter(function(p) { return p !== part })
        if (on)
            list.push(part)
        parts = list
    }
    function within(item, layer) {
        for (var p = item; p; p = p.parent) {
            if (p === layer)
                return true
        }
        return false
    }
    // The layer in front, or null: what a question is asked in.
    readonly property var frontLayer: {
        for (var i = parts.length - 1; i >= 0; --i) {
            if (parts[i].frameRole === "layer")
                return parts[i].parent
        }
        return null
    }
    // The part of a role that speaks: the latest in the layer in front, or
    // with no layer the latest. A layer with no help line has none (a
    // question's: the view's under it is not shown); one with no title or
    // keys of its own keeps the view's (a licence's text under About's
    // title).
    function front(role) {
        for (var i = parts.length - 1; i >= 0; --i) {
            if (parts[i].frameRole === role && (!frontLayer || within(parts[i], frontLayer)))
                return parts[i]
        }
        if (role === "help")
            return null
        for (var j = parts.length - 1; j >= 0; --j) {
            if (parts[j].frameRole === role)
                return parts[j]
        }
        return null
    }
    // The view's own title bar: the first that showed.
    function first(role) {
        for (var i = 0; i < parts.length; ++i) {
            if (parts[i].frameRole === role)
                return parts[i]
        }
        return null
    }
    readonly property var titlePart: front("title")
    readonly property var hintPart: front("hints")
    readonly property var helpPart: front("help")

    // A title bar's own steps of the way: its path, or its title and then
    // its subtitle's parts ("Look / Effects").
    function pathOf(bar) {
        if (!bar)
            return []
        if (bar.path !== null && bar.path !== undefined)
            return bar.path
        var own = [String(bar.title || "")]
        if (bar.subtitle)
            own = own.concat(String(bar.subtitle).split(" / "))
        return own.filter(function(step) { return step.trim() !== "" })
    }
    // A way and the steps on from it: a step it has already is not said
    // twice (Settings' pages each start at Settings).
    function joined(base, own) {
        var path = (base || []).slice()
        var had = path.map(function(step) { return String(step).toLowerCase() })
        for (var i = 0; i < own.length; i++) {
            if (had.indexOf(String(own[i]).toLowerCase()) < 0)
                path.push(own[i])
        }
        return path
    }
    // Where one is, from the top level; and the way to whatever the view
    // opens, which is the view's own whatever is asked over it.
    readonly property var path: joined(basePath, pathOf(titlePart))
    readonly property var viewPath: joined(basePath, pathOf(first("title")))
    // The title: said as it is where the bar says so (a question, the main
    // menu), else Studio Monitor's tab names the page, Broadcast writes the
    // way under the top level and Flat Panel the whole path.
    readonly property string title: {
        var bar = titlePart
        if (bar && bar.frameTitle)
            return bar.frameTitle
        if (path.length === 0)
            return ""
        if (studio)
            return path[path.length - 1]
        if (broadcast)
            return (path.length > 1 ? path.slice(1) : path).join(" / ")
        return ">" + path.join(" >")
    }

    // A hint line's keys and what each does: "[ESC]:BACK [▲▼]:NAVIGATE" as
    // [{ key: "[ESC]", label: "BACK" }, …].
    function hintPairs(text) {
        var pairs = []
        var each = /((?:\[[^\]]*\]\/?)+):([^\[]*)/g
        var found
        while ((found = each.exec(String(text || ""))) !== null) {
            var label = found[2].trim()
            if (label !== "")
                pairs.push({ key: found[1], label: label })
        }
        return pairs
    }
    // Broadcast's words for what every menu's keys do, as a monitor's guide
    // has them; any other key says what the hint line says.
    readonly property var broadcastWords: ({ "NAVIGATE": "SELECT", "SELECT": "ENTER", "BACK": "GO BACK" })
    function titleCase(text) {
        return String(text).toLowerCase().replace(/(^|[\s\/-])([a-z])/g, function(all, lead, letter) {
            return lead + letter.toUpperCase()
        })
    }
    // The key guide's lines: the hint bar's own for this layout where it has
    // them (`guide`), else made from its hint line.
    readonly property var guide: {
        var bar = hintPart
        if (!bar)
            return []
        if (bar.guide && bar.guide.length > 0)
            return bar.guide
        var pairs = hintPairs(bar.text)
        if (pairs.length === 0)
            return bar.text ? [String(bar.text)] : []
        if (studio)
            return pairs.map(function(p) { return p.key + ":" + p.label })
        if (flat)
            return pairs.map(function(p) { return p.key + " " + titleCase(p.label) })
        // Broadcast: the keys that move first and back last, as a monitor's
        // guide lists them, "TO SELECT  :[▲▼]", the colons in a column.
        var h = root.hints || ({})
        var isBack = function(p) { return p.key === h.back }
        var isMove = function(p) { return p.key === h.navigate || p.key === h.arrows }
        var back = pairs.filter(isBack)
        var move = pairs.filter(isMove)
        var rest = pairs.filter(function(p) { return !isBack(p) && !isMove(p) })
        var words = move.concat(rest).concat(back).map(function(p) {
            return { key: p.key, word: "TO " + (broadcastWords[p.label] || p.label) }
        })
        var wide = 11
        for (var i = 0; i < words.length; i++)
            wide = Math.max(wide, words[i].word.length)
        return words.map(function(w) {
            var word = w.word
            while (word.length < wide)
                word += " "
            return word + ":" + w.key
        })
    }
    // The help line's text, "" for none: a help line shows while Settings'
    // Help Line is on, or always where its lines are the page itself.
    readonly property string helpText: {
        var help = helpPart
        return help && (help.always === true || root.helpLine) ? String(help.text || "") : ""
    }

    // Flat Panel's page and pages, as pageTo() leaves them.
    property int page: 0
    property int pages: 1

    readonly property string layout: root.menuLayout
    readonly property bool studio: layout === "Studio Monitor"
    readonly property bool broadcast: layout === "Broadcast"
    readonly property bool flat: layout === "Flat Panel"
    readonly property var pal: root.menuPalette || ({ ground: root.surfaceColor, ink: root.primaryColor,
                                                     dim: root.primaryColor, accent: root.primaryColor,
                                                     soft: root.primaryColor, mark: root.primaryColor,
                                                     bar: root.primaryColor })
    readonly property real rowHeight: root.menuRowHeight
    readonly property real inset: 3 * root.px
    // The cursor's room at a row's start (MonitorRow's): Studio Monitor's
    // and Broadcast's mark stands in it.
    readonly property real gutter: flat ? root.sw * 0.025 : root.sw * 0.03125 //16 : 20

    // Studio Monitor's window: its black inside and the icons before it,
    // `inset` in, down to the guide's foot or, without it, to where the hint
    // bar would end.
    readonly property real winX: root.sw * 0.103125 //66
    readonly property real winY: root.sh * 0.1625 //78
    readonly property real winW: root.sw * 0.79375 //508
    readonly property real winBottom: root.hintBar ? root.sh * 0.8291667 : root.sh * 0.8958333 //398 : 430
    readonly property real stripW: root.sw * 0.0625 //40
    // The blue round the inside, `edge` thick: the icons' side is that and
    // the icons' own width, the foot goes down to where the guide ends.
    readonly property real edge: 4 * root.px
    readonly property real corner: 5 * root.px
    readonly property real coverCorner: studio ? 4 * root.px : 0
    readonly property real frameX: winX + inset - edge //64
    readonly property real frameY: winY + inset - edge //76
    readonly property real frameW: winW - 2 * (inset - edge) //512
    readonly property real frameBottom: root.hintBar ? root.sh * 0.8916667 : winBottom - inset + edge //428 : 432
    readonly property real sideW: edge + stripW //48

    // The help line's place, above the guide (or where it would be).
    readonly property real helpY: studio ? winBottom - inset - rowHeight - root.px
                                : broadcast ? (root.hintBar ? root.sh * 0.6958333 : root.sh * 0.8375) //334 : 402
                                : (root.hintBar ? root.sh * 0.775 : root.sh * 0.8375) //372 : 402
    readonly property real helpX: listX
    readonly property real helpW: listWidth

    readonly property real listX: studio ? winX + inset + stripW + root.sw * 0.009375 //112
                                : flat ? root.sw * 0.11875 : root.sw * 0.125 //76 : 80
    readonly property real listY: studio ? winY + root.sh * 0.025 //90
                                : broadcast ? root.sh * 0.1916667 : root.sh * 0.2 //92 : 96
    readonly property real listWidth: studio ? winX + winW - root.sw * 0.028125 - listX //444
                                    : flat ? root.sw * 0.7625 : root.sw * 0.75 //488 : 480
    // Down to the help line, or with none (tall: the main menu's, a
    // module's list) to where it would end: whole rows.
    readonly property real tallBottom: studio ? winBottom - inset - root.px
                                     : broadcast ? (root.hintBar ? root.sh * 0.7541667 : root.sh * 0.8958333) //362 : 430
                                     : (root.hintBar ? root.sh * 0.8333333 : root.sh * 0.8958333) //400 : 430
    readonly property real listBottom: root.helpLine ? helpY - root.sh * 0.0125 : tallBottom
    function wholeRows(bottom) {
        return Math.max(rowHeight, Math.floor((bottom - listY) / rowHeight + 0.001) * rowHeight)
    }
    readonly property real listHeight: wholeRows(listBottom)
    readonly property real tallHeight: wholeRows(tallBottom)
    // Over a help line that shows whatever Settings' Help Line is (one
    // whose lines are the page itself: About's).
    readonly property real helpedHeight: wholeRows(helpY - root.sh * 0.0125)
    // What a layer laid over the view covers, to ask in the layout: the
    // window's inside (its corners coverCorner round), or the ground between
    // the title and the guide.
    readonly property rect cover: studio
        ? Qt.rect(winX + inset + stripW, winY + inset, winW - 2 * inset - stripW, winBottom - winY - 2 * inset)
        : Qt.rect(screen.x, root.sh * 0.1666667, screen.width, //80
                  (root.hintBar ? (broadcast ? root.sh * 0.7625 : root.sh * 0.8395833) : screen.y + screen.height)
                  - root.sh * 0.1666667) //366 : 403

    // Flat Panel: the page the list's cursor is on, scrolled to its top.
    // A list's rows are each as high as the next (rowHeight in the monitor
    // layouts, MonitorRow's; a list that draws its own, as high as the
    // cursor's), and as many to the page as its own height holds. The
    // list's last page is filled out with empty rows (pageFill(), its
    // footer's height), so every page's top is within the list's bounds,
    // where a ListView keeps it. pageTo(null) is no list paged: a view
    // without pages.
    function rowsOf(list) {
        var high = list.currentItem && list.currentItem.height > 0 ? list.currentItem.height : rowHeight
        return { height: high, per: Math.max(1, Math.floor(list.height / high + 0.001)) }
    }
    function pageFill(list) {
        if (!flat || !list)
            return 0
        var rows = rowsOf(list)
        return (Math.ceil(list.count / rows.per) * rows.per - list.count) * rows.height
    }
    function pageTo(list) {
        if (!list) {
            pages = 1
            page = 0
            return
        }
        var rows = rowsOf(list)
        pages = Math.max(1, Math.ceil(list.count / rows.per))
        page = Math.min(pages - 1, Math.floor(Math.max(0, list.currentIndex) / rows.per))
        list.contentY = list.originY + page * rows.per * rows.height
    }

    // Broadcast's and Flat Panel's ground over the whole screen, as solid
    // over a video behind the menus as Transparent Background has it.
    Rectangle {
        x: chrome.screen.x
        y: chrome.screen.y
        width: chrome.screen.width
        height: chrome.screen.height
        visible: !chrome.studio && (!root.videoBehind || root.backdropSolidity > 0)
        opacity: root.videoBehind ? root.backdropSolidity / 100 : 1
        color: chrome.pal.ground
    }

    // --- STUDIO MONITOR ---
    // One blue shape: the frame, the tab, which goes down into it past its
    // corner, the icons' side and the guide's foot. The black inside is laid
    // over it.
    Rectangle {
        objectName: "frameBody"
        visible: chrome.studio
        x: chrome.frameX
        y: chrome.frameY
        width: chrome.frameW
        height: chrome.frameBottom - chrome.frameY
        radius: chrome.corner
        color: chrome.pal.accent
    }
    Rectangle {
        id: tab
        objectName: "frameTab"
        visible: chrome.studio && chrome.title !== ""
        x: chrome.frameX
        y: root.sh * 0.1041667 //50
        // As wide as its title, up to the status icons.
        width: Math.min(Math.round((tabText.implicitWidth + root.sw * 0.040625) / root.px) * root.px, //+26
                        (status.visible ? status.x - root.sw * 0.0125 : chrome.frameX + chrome.frameW) - x)
        height: chrome.frameY - y + 2 * chrome.corner
        radius: chrome.corner
        color: chrome.pal.accent

        Text {
            id: tabText
            x: root.sw * 0.0203125 //13
            y: Math.round((chrome.winY - tab.y - height) / 2) + root.px
            width: Math.min(implicitWidth, tab.width - x - 4 * root.px)
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: chrome.title
            color: chrome.pal.soft
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0375 //18
        }
    }
    Rectangle {
        objectName: "frameInside"
        visible: chrome.studio
        x: chrome.cover.x
        y: chrome.cover.y
        width: chrome.cover.width
        height: chrome.cover.height
        radius: chrome.coverCorner
        color: chrome.pal.ground
    }
    // The icons, each a cell, in the middle of the frame's side; as many as
    // there are fit it.
    Item {
        id: strip
        objectName: "frameStrip"
        visible: chrome.studio
        x: chrome.frameX + (chrome.sideW - chrome.stripW) / 2
        y: chrome.cover.y
        width: chrome.stripW
        height: chrome.cover.height
        readonly property real cell: Math.min(root.sh * 0.0666667,
                                              Math.floor(height / Math.max(1, chrome.icons.length) / root.px) * root.px) //32

        Repeater {
            model: chrome.studio ? chrome.icons : []
            Item {
                id: cell
                required property int index
                required property var modelData
                readonly property bool current: index === chrome.iconIndex
                readonly property bool lit: current && chrome.iconsActive
                readonly property string symbol: typeof modelData === "string" ? modelData : (modelData.icon || "")
                readonly property string logo: typeof modelData === "string" ? "" : String(modelData.source || "")
                y: index * strip.cell
                width: strip.width
                height: strip.cell

                Rectangle {
                    anchors.fill: parent
                    visible: cell.current
                    color: cell.lit ? chrome.pal.mark : "transparent"
                    border.color: chrome.pal.mark
                    border.width: cell.lit ? 0 : root.px
                    antialiasing: false
                }
                // A module's logo in a symbol's place: inside a module the
                // strip is the modules of its group (Main.qml's frame).
                IconCell {
                    anchors.fill: parent
                    symbol: cell.symbol
                    logo: cell.logo
                    color: cell.lit ? chrome.pal.ground : chrome.pal.soft
                }
            }
        }
    }
    // The key guide, on the frame's foot.
    Text {
        objectName: "frameGuide"
        visible: chrome.studio && root.hintBar
        x: chrome.frameX
        y: chrome.cover.y + chrome.cover.height
        width: chrome.frameW
        height: chrome.frameBottom - y
        leftPadding: root.sw * 0.01875 //12
        rightPadding: leftPadding
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        textFormat: Text.PlainText
        text: chrome.guide.join("  ")
        color: chrome.pal.soft
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0291667 //14
    }

    // --- BROADCAST ---
    Text {
        visible: chrome.broadcast
        y: root.sh * 0.1 //48
        // Between the screen's safe edges, short of the status icons; a way
        // too long for it keeps its end, where one is.
        width: root.sw * 0.75 - (status.visible ? 2 * (status.width + root.sw * 0.0125) : 0)
        anchors.horizontalCenter: parent.horizontalCenter
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideLeft
        textFormat: Text.PlainText
        text: chrome.title
        color: chrome.pal.ink
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.05 //24
    }
    // The guide's three lines; more keys than three stand in a second
    // column beside them.
    Row {
        id: guideLines
        visible: chrome.broadcast && root.hintBar
        readonly property var lines: chrome.broadcast ? chrome.guide.slice(0, 6) : []
        readonly property bool wide: lines.length > 3
        x: wide ? root.sw * 0.125 : root.sw * 0.2625 //80 : 168
        y: root.sh * 0.7708333 //370
        spacing: root.sw * 0.025 //16
        Repeater {
            model: guideLines.wide ? 2 : 1
            Column {
                id: guideColumn
                required property int index
                spacing: 0
                Repeater {
                    model: guideLines.lines.slice(guideColumn.index * 3, guideColumn.index * 3 + 3)
                    Text {
                        required property string modelData
                        width: guideLines.wide ? root.sw * 0.3625 : implicitWidth //232
                        height: root.sh * 0.0416667 //20
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        text: modelData
                        color: chrome.pal.ink
                        font.family: root.globalFont
                        font.pixelSize: root.sh * 0.0333333 //16
                    }
                }
            }
        }
    }

    // --- FLAT PANEL ---
    Text {
        visible: chrome.flat
        x: root.sw * 0.125 //80
        y: root.sh * 0.1083333 //52
        // Up to the status icons, which stand before the page, or the page.
        width: (status.visible ? status.x : pageMark.visible ? pageMark.x : root.sw * 0.875) - x - root.sw * 0.025
        // A path too long for the line keeps its end, where one is.
        elide: Text.ElideLeft
        textFormat: Text.PlainText
        text: chrome.title
        color: chrome.pal.ink
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0416667 //20
    }
    // "1/2", and ▲▼ beside it, while there are pages; not over a question,
    // whose answers are not the rows it counts.
    Row {
        id: pageMark
        visible: chrome.flat && chrome.pages > 1 && !chrome.frontLayer
        x: root.sw * 0.875 - width //560
        y: root.sh * 0.1125 //54
        spacing: root.sw * 0.009375 //6
        Text {
            text: (chrome.page + 1) + "/" + chrome.pages
            color: chrome.pal.ink
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0333333 //16
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: root.px
            PixelIcon { name: "up"; color: chrome.pal.ink }
            PixelIcon { name: "down"; color: chrome.pal.ink }
        }
    }
    Rectangle {
        visible: chrome.flat && root.hintBar
        x: chrome.listX
        y: root.sh * 0.8416667 //404
        width: chrome.listWidth
        height: root.px
        color: chrome.pal.accent
        antialiasing: false
    }
    Text {
        visible: chrome.flat && root.hintBar
        x: chrome.listX + root.sw * 0.025 //16
        y: root.sh * 0.8541667 //410
        width: chrome.listWidth - root.sw * 0.025
        elide: Text.ElideRight
        textFormat: Text.PlainText
        text: chrome.guide.join("   ")
        color: chrome.pal.soft
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0291667 //14
    }

    // --- EVERY LAYOUT ---
    // What is on, at the top right: on the tab's row, beside the title or
    // left of the page.
    StatusIcons {
        id: status
        color: chrome.studio ? chrome.pal.soft : chrome.pal.ink
        x: (chrome.studio ? chrome.frameX + chrome.frameW
            : chrome.flat && pageMark.visible ? pageMark.x - root.sw * 0.0125
            : root.sw * 0.884375) - width //576 : … : 566
        y: root.sh * 0.1166667 //56
    }
    HelpLine {
        ofFrame: true
        always: true
        visible: chrome.helpText !== ""
        x: chrome.helpX
        y: chrome.helpY
        width: chrome.helpW
        ink: chrome.studio ? chrome.pal.soft : chrome.flat ? chrome.pal.soft : chrome.pal.ink
        framed: chrome.studio
        text: chrome.helpText
    }
}
