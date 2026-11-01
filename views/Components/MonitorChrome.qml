import QtQuick

// Everything round a menu's rows in a monitor layout (Settings → Look → Menu
// Layout), in the layout's own colours (root.menuPalette), after the menus of
// three generations of studio monitors:
//
//   Studio Monitor  a black window over the picture, framed in blue, the
//                   title on a blue tab above its top left corner, a blue
//                   strip of icons down its left side (the one shown on
//                   yellow), the help line in a box at its foot and the key
//                   guide on a blue strip under it;
//   Broadcast       plain blue over the whole screen, the title centred at
//                   the top, the help line and a three-line key guide
//                   ("TO SELECT  :[▲▼]") at the foot;
//   Flat Panel      black over the whole screen, the title a path at the top
//                   left (">Look"), the page the rows are on at the top right
//                   ("1/2" with ▲▼), the help line, a rule and a one-line key
//                   guide at the foot.
//
// The host lays its list where this says (listX, listY, listWidth,
// listHeight, rows of rowHeight) and draws its rows with MonitorRow. The
// status icons stay at the top right. A hidden Help Line's or Hint Bar's
// room goes to the rows, whole rows of it. Flat Panel's rows go a page at a
// time (pageTo()).
//
//     MonitorChrome { anchors.fill: parent; title: "LOOK"; guide: ["[▲▼]:MENU/ITEM"]; helpText: "…" }
Item {
    id: chrome

    property string title: ""
    // Flat Panel's line under the title.
    property string subtitle: ""
    // Studio Monitor's strip: PixelIcon names, the one shown, and whether
    // the keys are in it (its cell then in the cursor's colour, else framed).
    property var icons: []
    property int iconIndex: 0
    property bool iconsActive: false
    // The help line's text ("" for none) and the key guide's lines. A menu
    // without a help line (the main menu's) leaves its room to the rows.
    property bool hasHelp: true
    property string helpText: ""
    property var guide: []
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
    readonly property real rowHeight: root.sh * 0.0583333 //28, Main.qml's menuRowHeight
    readonly property real inset: 3 * root.px

    // Studio Monitor's window, down to the guide's strip or, without it, to
    // where the hint bar would end.
    readonly property real winX: root.sw * 0.103125 //66
    readonly property real winY: root.sh * 0.1625 //78
    readonly property real winW: root.sw * 0.79375 //508
    readonly property real winBottom: root.hintBar ? root.sh * 0.8291667 : root.sh * 0.8958333 //398 : 430
    readonly property real stripW: root.sw * 0.0625 //40

    // The help line's place, above the guide (or where it would be).
    readonly property real helpY: studio ? winBottom - inset - rowHeight - root.px
                                : broadcast ? (root.hintBar ? root.sh * 0.6958333 : root.sh * 0.8375) //334 : 402
                                : (root.hintBar ? root.sh * 0.775 : root.sh * 0.8375) //372 : 402
    readonly property real helpX: listX
    readonly property real helpW: listWidth

    readonly property real listX: studio ? winX + inset + stripW + root.sw * 0.009375 //112
                                : flat ? root.sw * 0.11875 : root.sw * 0.125 //76 : 80
    readonly property real listY: studio ? winY + root.sh * 0.025 //90
                                : broadcast ? root.sh * 0.1916667 //92
                                : subtitle !== "" ? root.sh * 0.2333333 : root.sh * 0.2 //112 : 96
    readonly property real listWidth: studio ? winX + winW - root.sw * 0.028125 - listX //444
                                    : flat ? root.sw * 0.7625 : root.sw * 0.75 //488 : 480
    // Down to the help line, or to where it would be: whole rows.
    readonly property real listBottom: hasHelp && root.helpLine ? helpY - root.sh * 0.0125
                                     : studio ? winBottom - inset - root.px
                                     : broadcast ? (root.hintBar ? root.sh * 0.7541667 : root.sh * 0.8958333) //362 : 430
                                     : (root.hintBar ? root.sh * 0.8333333 : root.sh * 0.8958333) //400 : 430
    readonly property real listHeight: Math.max(rowHeight, Math.floor((listBottom - listY) / rowHeight + 0.001) * rowHeight)

    // Flat Panel: the page the list's cursor is on, scrolled to its top.
    // Every row is rowHeight high in the monitor layouts. The list's last
    // page is filled out with empty rows (pageFill(), its footer's height),
    // so every page's top is within the list's bounds, where a ListView
    // keeps it.
    readonly property int perPage: Math.max(1, Math.floor(listHeight / rowHeight + 0.001))
    function pageFill(count) {
        return flat ? (Math.ceil(count / perPage) * perPage - count) * rowHeight : 0
    }
    function pageTo(list) {
        if (!list)
            return
        pages = Math.max(1, Math.ceil(list.count / perPage))
        page = Math.min(pages - 1, Math.floor(Math.max(0, list.currentIndex) / perPage))
        list.contentY = list.originY + page * perPage * rowHeight
    }

    // Broadcast's and Flat Panel's ground over the whole screen, as solid
    // over a video behind the menus as Transparent Background has it.
    Rectangle {
        anchors.fill: parent
        visible: !chrome.studio && (!root.videoBehind || root.backdropSolidity > 0)
        opacity: root.videoBehind ? root.backdropSolidity / 100 : 1
        color: chrome.pal.ground
    }

    // --- STUDIO MONITOR ---
    Rectangle {
        visible: chrome.studio
        x: chrome.winX
        y: chrome.winY
        width: chrome.winW
        height: chrome.winBottom - chrome.winY
        radius: 4 * root.px
        color: chrome.pal.ground
        border.color: chrome.pal.accent
        border.width: root.px
    }
    Rectangle {
        id: tab
        visible: chrome.studio
        x: chrome.winX
        y: root.sh * 0.1041667 //50
        width: Math.round((tabText.implicitWidth + root.sw * 0.040625) / root.px) * root.px //+26
        height: chrome.winY - y + 2 * root.px
        radius: 2 * root.px
        color: chrome.pal.accent

        Text {
            id: tabText
            x: root.sw * 0.0203125 //13
            y: Math.round((chrome.winY - tab.y - height) / 2)
            text: chrome.title
            color: chrome.pal.soft
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0375 //18
        }
    }
    // The strip of icons, each a cell; as many as there are fit it.
    Rectangle {
        id: strip
        visible: chrome.studio && chrome.icons.length > 0
        x: chrome.winX + chrome.inset
        y: chrome.winY + chrome.inset
        width: chrome.stripW
        height: chrome.winBottom - chrome.winY - 2 * chrome.inset
        color: chrome.pal.accent
        antialiasing: false
        readonly property real cell: Math.min(root.sh * 0.0666667,
                                              Math.floor(height / Math.max(1, chrome.icons.length) / root.px) * root.px) //32

        Repeater {
            model: chrome.icons
            Item {
                required property int index
                required property string modelData
                readonly property bool current: index === chrome.iconIndex
                y: index * strip.cell
                width: strip.width
                height: strip.cell

                Rectangle {
                    anchors.fill: parent
                    visible: parent.current
                    color: chrome.iconsActive ? chrome.pal.mark : "transparent"
                    border.color: chrome.pal.mark
                    border.width: chrome.iconsActive ? 0 : root.px
                    antialiasing: false
                }
                PixelIcon {
                    name: parent.modelData
                    color: parent.current && chrome.iconsActive ? chrome.pal.ground : chrome.pal.soft
                    x: Math.round((parent.width - width) / 2 / root.px) * root.px
                    y: Math.round((parent.height - height) / 2 / root.px) * root.px
                }
            }
        }
    }
    // The key guide's strip under the window.
    Rectangle {
        visible: chrome.studio && root.hintBar && chrome.guide.length > 0
        x: chrome.winX
        y: root.sh * 0.8416667 //404
        width: chrome.winW
        height: root.sh * 0.05 //24
        color: chrome.pal.accent
        antialiasing: false

        Text {
            anchors.fill: parent
            leftPadding: root.sw * 0.01875 //12
            rightPadding: leftPadding
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: chrome.guide.join("  ")
            color: chrome.pal.soft
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0291667 //14
        }
    }

    // --- BROADCAST ---
    Text {
        visible: chrome.broadcast
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.sh * 0.1 //48
        text: chrome.title
        color: chrome.pal.ink
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.05 //24
    }
    Column {
        visible: chrome.broadcast && root.hintBar
        x: root.sw * 0.2625 //168
        y: root.sh * 0.7708333 //370
        spacing: 0
        Repeater {
            model: chrome.broadcast ? chrome.guide : []
            Text {
                required property string modelData
                height: root.sh * 0.0416667 //20
                verticalAlignment: Text.AlignVCenter
                text: modelData
                color: chrome.pal.ink
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333 //16
            }
        }
    }

    // --- FLAT PANEL ---
    Text {
        visible: chrome.flat
        x: root.sw * 0.125 //80
        y: root.sh * 0.1083333 //52
        width: (pageMark.visible ? pageMark.x : root.sw * 0.875) - x - root.sw * 0.025
        elide: Text.ElideRight
        text: chrome.title
        color: chrome.pal.ink
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0416667 //20
    }
    Text {
        visible: chrome.flat && chrome.subtitle !== ""
        x: root.sw * 0.14375 //92
        y: root.sh * 0.1666667 //80
        text: chrome.subtitle
        color: chrome.pal.soft
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0333333 //16
    }
    // "1/2", and ▲▼ beside it, while there are pages.
    Row {
        id: pageMark
        visible: chrome.flat && chrome.pages > 1
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
        text: chrome.guide.join("   ")
        color: chrome.pal.soft
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0291667 //14
    }

    // --- EVERY LAYOUT ---
    // What is on, at the top right: on the tab's row, beside the title or
    // left of the page.
    StatusIcons {
        color: chrome.studio ? chrome.pal.soft : chrome.pal.ink
        x: (chrome.studio ? chrome.winX + chrome.winW
            : chrome.flat && pageMark.visible ? pageMark.x - root.sw * 0.0125
            : root.sw * 0.884375) - width //574 : … : 566
        y: chrome.studio ? root.sh * 0.1166667 : root.sh * 0.1166667 //56
    }
    HelpLine {
        visible: chrome.helpText !== ""
        x: chrome.helpX
        y: chrome.helpY
        width: chrome.helpW
        ink: chrome.studio ? chrome.pal.soft : chrome.flat ? chrome.pal.soft : chrome.pal.ink
        framed: chrome.studio
        text: chrome.helpText
    }
}
