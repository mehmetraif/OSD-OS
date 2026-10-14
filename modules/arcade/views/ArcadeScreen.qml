import QtQuick
import Components

// The Arcade screen (Settings → Arcade → Screen: Arcade), a cabinet's
// selection screen in place of the original tree (Games.qml). The games of
// one filter (ArcadeBackend::filters) lie on an arc, a page at a time, under
// a bar naming the filter and the page; on the right the game's picture with
// the credits bought, its high scores (the High Scores module's) and what it
// is (ArcadeBackend::gameDetails); under them the help line with its full
// name, and the hint bar. The module's own Hint Bar and Help Line settings
// hide those, and their room goes to the list, a row more each, and to the
// high scores. Laid out in the design's units: a 960th of the width, a 720th
// of the height.
//
// ▲ ▼ choose a game, from page to page and round; ◄ ► go round the filters,
// keeping the game where the next filter has it. COIN (a pad's Back button,
// MAME's coin, or 5) buys a credit, START (a pad's Start, Space, or 1) plays
// the game with them (Play.qml), select opens its options (Play, its
// favourite, Play at Startup) and back leaves the module. Coming back from a
// game restores the filter and the game from the listState handed to
// navigateTo. Opened with the startup favourite (navParams.startupPlay), it
// plays that at once.
FocusScope {
    id: arcadeScreen

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    // Context properties read null while the module's Loader tears this view
    // down, hence the guards.
    readonly property string problem: arcadeBackend ? arcadeBackend.problem : ""
    readonly property bool folderReadable: arcadeBackend ? arcadeBackend.folderReadable : true
    readonly property bool scanning: arcadeBackend ? arcadeBackend.scanning : false
    readonly property string gamesFolder: arcadeBackend ? arcadeBackend.gamesFolder() : ""
    readonly property var scoresBackend: typeof highScoresBackend !== "undefined" ? highScoresBackend : null

    // The design's units.
    readonly property real u: root.sw / 960
    readonly property real v: root.sh / 720

    readonly property bool hintShown: moduleRoot.hintBar
    readonly property bool helpShown: moduleRoot.helpLine
    // Where the list and the column end, in the design's 720 lines: above
    // the help line, or above what is in its place.
    readonly property int listBottom: hintShown ? (helpShown ? 552 : 604) : (helpShown ? 592 : 644)
    // As many rows as fit 38 lines apart, spread over the room.
    readonly property int perPage: Math.floor((listBottom - 179) / 38) + 1
    readonly property int pitch: Math.floor((listBottom - 179) / (perPage - 1))
    // The column's lines against its foot; the high scores above them, as
    // many as fit, five at most.
    readonly property int infoTop: listBottom - 130
    readonly property int scoreRows: Math.min(5, Math.floor((infoTop - 333) / 25))

    // The filters with games (ArcadeBackend::filters), the one shown, and the
    // one wanted: Start Filter's, or the one ◄ ► came to, shown as soon as it
    // has games (while MAME is asked, only the lists have).
    property var filters: []
    property int filterIndex: 0
    property string wanted: navListState.filter || moduleRoot.startFilter || "all"
    readonly property var filter: filters.length > 0 ? filters[Math.min(filterIndex, filters.length - 1)] : null
    readonly property string filterId: filter ? filter.id : ""
    readonly property int count: filter ? filter.count : 0
    // The game selected, by its place in the filter, and its page's games.
    property int selected: 0
    readonly property int page: Math.floor(selected / perPage)
    readonly property int pages: Math.max(1, Math.ceil(count / perPage))
    // Counted up as what the filters hold changes, so the page and the game
    // are read again.
    property int revision: 0
    readonly property var rows: (revision, arcadeBackend && filterId !== ""
                                            ? arcadeBackend.filterGames(filterId, page * perPage, perPage) : [])
    readonly property var current: {
        var at = selected - page * perPage
        return at >= 0 && at < rows.length ? rows[at] : null
    }
    readonly property string currentSet: current ? current.set : ""
    readonly property bool isGame: !!current && !current.unknown
    readonly property var details: (revision, arcadeBackend && currentSet !== ""
                                               ? arcadeBackend.gameDetails(currentSet) : ({}))
    // The game's high scores, read again once the High Scores module has
    // read MAME's files (a game just played).
    property int scoresRevision: 0
    readonly property var scores: (scoresRevision, scoresBackend && isGame
                                                   ? scoresBackend.topEntries(currentSet, scoreRows) : [])

    function indexOfFilter(id) {
        for (var i = 0; i < filters.length; ++i) {
            if (filters[i].id === id)
                return i
        }
        return -1
    }

    // The filters read again, the wanted one shown if it has games, and the
    // game kept where it is in it.
    function reload() {
        if (!arcadeBackend)
            return
        var keep = currentSet !== "" ? currentSet : (navListState.set || "")
        filters = arcadeBackend.filters()
        filterIndex = Math.max(0, indexOfFilter(wanted))
        var at = keep !== "" && filterId !== "" ? arcadeBackend.filterIndex(filterId, keep) : -1
        selected = at >= 0 ? at : Math.max(0, Math.min(selected, count - 1))
        revision++
    }

    function move(delta) {
        if (count > 0)
            selected = (selected + delta + count) % count
    }

    function cycle(delta) {
        if (filters.length < 2)
            return
        var keep = currentSet
        filterIndex = (filterIndex + delta + filters.length) % filters.length
        wanted = filterId
        var at = keep !== "" ? arcadeBackend.filterIndex(filterId, keep) : -1
        selected = Math.max(0, at)
    }

    function addCredit() {
        moduleRoot.credits = Math.min(9, moduleRoot.credits + 1)
    }

    // Plays a game, putting it on Recently Played; coming back lands on it in
    // listFilter.
    function play(set, name, listFilter) {
        appCore.add_to_list(moduleRoot.moduleId, "recent", { name: name, path: "game:" + set, isFolder: false }, 30)
        arcadeScreen.navigateTo("Play.qml", { game: set, title: name }, { filter: listFilter, set: set })
    }
    function playCurrent() {
        if (isGame)
            play(currentSet, details.name || current.name, filterId)
    }

    function openOptions() {
        if (isGame)
            options.offer({ name: details.name || current.name, path: "game:" + currentSet, isFolder: false })
    }

    // The startup favourite, played as if chosen in Favorites. Only as the
    // view first opens: coming back from the game brings navListState
    // instead.
    function playAtStartup(entry) {
        var path = String(entry.path || "")
        if (path.indexOf("game:") !== 0)
            return
        var set = path.substring(5)
        var game = arcadeBackend.gameDetails(set)
        if (game.set && !game.unknown)
            play(set, game.name, "favorites")
    }

    Component.onCompleted: {
        reload()
        if (scoresBackend)
            scoresBackend.refresh()
        if (!navParams.navListState && navParams.startupPlay)
            Qt.callLater(playAtStartup, navParams.startupPlay)
    }

    Connections {
        target: arcadeBackend
        function onGamesChanged() { arcadeScreen.reload() }
    }
    Connections {
        target: arcadeScreen.scoresBackend
        ignoreUnknownSignals: true
        function onReadingChanged() {
            if (!arcadeScreen.scoresBackend.reading)
                arcadeScreen.scoresRevision++
        }
    }

    Keys.onPressed: function(event) {
        // Keys the options leave go no further.
        if (options.visible)
            return
        var button = typeof inputManager !== "undefined" && inputManager
                     ? inputManager.padButton(event.nativeScanCode) : ""
        if (button === "back" || event.key === Qt.Key_5) {
            // COIN, though a pad's Back button does Back everywhere else.
            if (!event.isAutoRepeat)
                addCredit()
        } else if (event.key === Qt.Key_Space || event.key === Qt.Key_1) {
            if (!event.isAutoRepeat)
                playCurrent()
        } else if (event.key === Qt.Key_Up) {
            move(-1)
        } else if (event.key === Qt.Key_Down) {
            move(1)
        } else if (event.key === Qt.Key_Left) {
            cycle(-1)
        } else if (event.key === Qt.Key_Right) {
            cycle(1)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (!event.isAutoRepeat)
                openOptions()
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            if (!event.isAutoRepeat)
                arcadeScreen.goBack()
        } else {
            return
        }
        event.accepted = true
    }

    // A play time as the column shows it: 1H 23M, 45M; NEVER.
    function playTime(seconds) {
        if (!seconds)
            return "Never"
        var minutes = Math.ceil(seconds / 60)
        return minutes >= 60 ? Math.floor(minutes / 60) + "H " + (minutes % 60) + "M" : minutes + "M"
    }

    // ---
    // The list
    // ---

    ArcadeBar {
        visible: arcadeScreen.filters.length > 0
        x: Math.round(120 * arcadeScreen.u)
        y: Math.round(90 * arcadeScreen.v)
        width: Math.round(420 * arcadeScreen.u)
        height: Math.round(42 * arcadeScreen.v)
        text: arcadeScreen.filter ? arcadeScreen.filter.name : ""
        arrows: arcadeScreen.filters.length > 1
        rightText: "Page " + (arcadeScreen.page + 1) + "/" + arcadeScreen.pages
    }

    Repeater {
        model: arcadeScreen.rows
        Item {
            id: row
            required property var modelData
            required property int index
            readonly property int number: arcadeScreen.page * arcadeScreen.perPage + index + 1
            readonly property bool isCurrent: number - 1 === arcadeScreen.selected
            // The rows lie on an arc, the middle of the page furthest right;
            // the selected one stands out of it to the left.
            readonly property int arc: Math.round(36 * Math.sin(Math.PI * (index + 0.5) / arcadeScreen.perPage))
            readonly property color ink: isCurrent ? root.surfaceColor : root.primaryColor
            x: Math.round((150 + arc - (isCurrent ? 18 : 0)) * arcadeScreen.u)
            y: Math.round((146 + index * arcadeScreen.pitch) * arcadeScreen.v)
            width: Math.round((isCurrent ? 348 : 330) * arcadeScreen.u)
            height: Math.round(33 * arcadeScreen.v)

            SelectionBox {
                anchors.fill: parent
                visible: row.isCurrent
            }
            Rectangle {
                anchors.fill: parent
                visible: !row.isCurrent
                color: "transparent"
                border.width: root.px
                border.color: root.primaryColor
                antialiasing: false
            }

            Text {
                id: numberText
                x: Math.round(10 * arcadeScreen.u) + root.px
                anchors.verticalCenter: parent.verticalCenter
                text: ("00" + row.number).slice(-Math.max(3, String(row.number).length))
                color: row.ink
                font.family: root.globalFont
                font.pixelSize: Math.round(16 * arcadeScreen.v)
            }
            Text {
                x: numberText.x + numberText.width + Math.round(10 * arcadeScreen.u)
                width: (heart.visible ? heart.x - Math.round(10 * arcadeScreen.u)
                                      : row.width - Math.round(10 * arcadeScreen.u) - root.px) - x
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.name
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: row.ink
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: Math.round(22 * arcadeScreen.v)
            }
            PixelIcon {
                id: heart
                visible: !!row.modelData.favorite
                name: "heart"
                color: row.ink
                x: row.width - width - Math.round(10 * arcadeScreen.u) - root.px
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ---
    // The column: the picture, the high scores, what the game is
    // ---

    Rectangle {
        id: preview
        visible: arcadeScreen.filters.length > 0
        x: Math.round(560 * arcadeScreen.u)
        y: Math.round(90 * arcadeScreen.v)
        width: Math.round(280 * arcadeScreen.u)
        height: Math.round(210 * arcadeScreen.v)
        color: picture.status === Image.Ready ? "#000000" : "transparent"
        border.width: root.px
        border.color: root.primaryColor
        antialiasing: false

        // The game's picture, 4:3; a vertical game's upright in the middle.
        Image {
            id: picture
            readonly property real inner: preview.height - 2 * root.px
            visible: status === Image.Ready
            y: root.px
            height: inner
            width: arcadeScreen.details.vertical ? Math.round(inner * 3 / 4) : preview.width - 2 * root.px
            x: Math.round((preview.width - width) / 2)
            source: arcadeScreen.isGame && arcadeScreen.details.picture ? arcadeScreen.details.picture : ""
            sourceSize.width: width
            sourceSize.height: height
            fillMode: Image.Stretch
            asynchronous: true
        }

        // Without one: which file it would be.
        Column {
            visible: picture.status !== Image.Ready && picture.status !== Image.Loading && arcadeScreen.current !== null
            anchors.centerIn: parent
            width: parent.width - 4 * root.px
            spacing: Math.round(12 * arcadeScreen.v)
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: arcadeScreen.isGame ? "No Picture" : "Not Recognized"
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: Math.round(20 * arcadeScreen.v)
            }
            Text {
                visible: arcadeScreen.isGame
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "snap/" + arcadeScreen.currentSet + ".png"
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: Math.round(15 * arcadeScreen.v)
            }
        }

        // The credits bought, on a ground of their own so they read over
        // the picture.
        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: root.px
            width: creditText.implicitWidth + 2 * Math.round(6 * arcadeScreen.u)
            height: creditText.implicitHeight + 2 * root.px
            color: root.surfaceColor
            antialiasing: false
            Text {
                id: creditText
                anchors.centerIn: parent
                text: "Credit " + moduleRoot.credits
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: Math.round(15 * arcadeScreen.v)
            }
        }
    }

    Column {
        visible: arcadeScreen.isGame
        x: Math.round(560 * arcadeScreen.u)
        y: Math.round(312 * arcadeScreen.v)
        width: Math.round(280 * arcadeScreen.u)
        spacing: Math.round(5 * arcadeScreen.v)

        Text {
            text: "Hi-Score"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: Math.round(18 * arcadeScreen.v)
        }
        Repeater {
            model: arcadeScreen.scores
            Item {
                required property var modelData
                width: parent.width
                height: Math.round(20 * arcadeScreen.v)
                Text {
                    text: modelData.rank
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: Math.round(20 * arcadeScreen.v)
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: modelData.name !== "" ? Math.round(60 * arcadeScreen.u) : 0
                    text: modelData.score
                    textFormat: Text.PlainText
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: Math.round(20 * arcadeScreen.v)
                }
                Text {
                    anchors.right: parent.right
                    width: Math.round(48 * arcadeScreen.u)
                    horizontalAlignment: Text.AlignRight
                    text: modelData.name
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: Math.round(20 * arcadeScreen.v)
                }
            }
        }
        Text {
            visible: arcadeScreen.scores.length === 0
            text: "No Scores Yet"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: Math.round(18 * arcadeScreen.v)
        }
    }

    // What the game is, against the column's foot. The online line keeps its
    // place while it is hidden, so the column stays still from game to game.
    Repeater {
        model: arcadeScreen.isGame ? [
            { label: "System", value: arcadeScreen.details.system || "?", shown: true },
            { label: "Maker", value: arcadeScreen.details.maker || "?", shown: true },
            { label: "Year", value: arcadeScreen.details.year || "?", shown: true },
            { label: "Players", value: arcadeScreen.details.players > 0 ? String(arcadeScreen.details.players) : "?", shown: true },
            { label: "Play Time", value: arcadeScreen.playTime(arcadeScreen.details.playTime), shown: true },
            { label: "Online", value: "Available", shown: !!arcadeScreen.details.online }
        ] : []
        MenuRow {
            required property var modelData
            required property int index
            visible: modelData.shown
            // The label where the high scores' are: MenuRow pads its line.
            x: Math.round(560 * arcadeScreen.u) - pad
            y: Math.round((arcadeScreen.infoTop + index * 23) * arcadeScreen.v)
            width: Math.round(280 * arcadeScreen.u) + 2 * pad
            height: Math.round(18 * arcadeScreen.v)
            fontSize: Math.round(18 * arcadeScreen.v)
            label: modelData.label
            value: modelData.value
        }
    }

    // ---
    // Nothing to list
    // ---

    // No games: what stands in the way, and where the games go (unless that
    // is the folder that can't be read); while MAME is asked, that it is.
    Column {
        anchors.centerIn: parent
        width: root.sw * 0.75 //480
        spacing: root.sh * 0.0333333 //16
        visible: arcadeScreen.filters.length === 0
        Text {
            width: parent.width
            text: arcadeScreen.scanning ? "Reading the games…" : "No games found"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: root.sh * 0.05 //24
        }
        Text {
            visible: !arcadeScreen.scanning && arcadeScreen.problem !== ""
            width: parent.width
            text: arcadeScreen.problem
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            font.pixelSize: root.sh * 0.0333333 //16
        }
        Text {
            visible: !arcadeScreen.scanning && arcadeScreen.folderReadable
            width: parent.width
            text: "Put MAME's ROM sets, a zip file for each game, in " + arcadeScreen.gamesFolder
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            font.pixelSize: root.sh * 0.0333333 //16
        }
    }

    // ---
    // The help line and the hint bar
    // ---

    // The game's full name, as MAME has it; why a set isn't a game. What
    // stands in the way of the list comes first.
    HelpLine {
        visible: arcadeScreen.filters.length > 0 && !options.visible
        shown: arcadeScreen.helpShown
        text: arcadeScreen.problem !== "" ? arcadeScreen.problem
              : arcadeScreen.scanning ? "Reading the games…"
              : arcadeScreen.details.unknown ? arcadeScreen.details.detail
              : arcadeScreen.details.name || ""
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: arcadeScreen.hintShown ? root.sh * 0.1583333 : root.sh * 0.1041667 //76, or the hint bar's 50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // ▲ ▼ and ◄ ► go without saying, the filter's bar showing ◄ ►: the
    // bar has room for four.
    HintBar {
        visible: !options.visible
        shown: arcadeScreen.hintShown
        text: arcadeScreen.filters.length === 0 ? root.hints.back + ":BACK"
              : root.hints.back + ":BACK " + root.hints.coin + ":CREDIT " + root.hints.play_pause + ":PLAY "
                + root.hints.select + ":OPTIONS"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // A game's options: Play, its favourite, Play at Startup.
    EntryOptions {
        id: options
        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        firstChoices: [{ label: "Play", action: "play" }]
        onActivated: function(action) {
            if (action === "play")
                arcadeScreen.playCurrent()
        }
        onFavoritesEdited: arcadeScreen.reload()
        onClosed: arcadeScreen.forceActiveFocus()
    }
}
