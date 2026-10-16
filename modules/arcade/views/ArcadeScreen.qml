import QtQuick
import Components

// The Arcade screen (Settings → Arcade → Screen: Arcade), a cabinet's
// selection screen in place of the original tree (Games.qml). The games of
// one filter (ArcadeBackend::filters) lie on an arc, a page at a time, under
// a bar naming the filter and the page; on the right the game's picture with
// the credits bought, its high scores (the High Scores module's) and how
// long it has been played; under them the help line with its full name and
// what it is (ArcadeBackend::gameDetails), and the hint bar. The module's
// own Hint Bar and Help Line settings (Root.qml's, which root.hintBar and
// root.helpLine go by here) hide those, and their room goes to the list, a
// row more each, and to the high scores. Laid out in the design's units, a
// 960th of the width and a 720th of the height, in letters a CRT reads: none
// smaller than the hint bar's and the help line's.
//
// ▲ ▼ choose a game, from page to page and round; ◄ ► go round the filters,
// keeping the game where the next filter has it. COIN (MAME's 5, which a
// pad's Back button, MAME's coin, is here: takesCoin) buys a credit; select,
// let go, or START (Space, a pad's Start, or 1) plays the game with them
// (navigateTo Play.qml, which Root.qml plays as Start Info Screen says);
// select held opens its options (Play, its favourite, Play at Startup) and
// back leaves the module. Coming back from a game restores the filter and
// the game from the listState handed to navigateTo; a game played from the
// screen itself (Start Info Screen off) leaves it as it was. Opened with the
// startup favourite (navParams.startupPlay), it plays that at once.
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
    // A pad's Back button is COIN here, not while the options are up
    // (InputManager).
    readonly property bool takesCoin: !options.visible

    // The design's units.
    readonly property real u: root.sw / 960
    readonly property real v: root.sh / 720

    // The letters, in the design's lines: the hint bar's and the help
    // line's, 18 of 480 lines, and the games' names larger.
    readonly property int textLines: 27
    readonly property int textSize: Math.round(textLines * v)
    readonly property int nameSize: Math.round(30 * v)

    // Where the list and the column end, in the design's 720 lines: above
    // the help line, or above what is in its place.
    readonly property int listBottom: root.hintBar ? (root.helpLine ? 552 : 604) : (root.helpLine ? 592 : 644)
    // The rows under the bar, as many as fit minPitch lines apart (seven with
    // both bars), spread over the room.
    readonly property int listTop: 147
    readonly property int rowHeight: 48
    readonly property int minPitch: 54
    readonly property int perPage: Math.floor((listBottom - listTop - rowHeight) / minPitch) + 1
    readonly property int pitch: Math.floor((listBottom - listTop - rowHeight) / (perPage - 1))
    // The column right of the list: the picture, and under it the high
    // scores, as many as fit (four with both bars, five at most), a line a
    // scorePitch apart from scoreFirst under their heading, and the play
    // time against its foot.
    readonly property int columnX: 495
    readonly property int columnWidth: 345
    readonly property int scoresTop: 362
    readonly property int scoreFirst: 34
    readonly property int scorePitch: 31
    readonly property int playTimeTop: listBottom - textLines
    readonly property int scoreRows: Math.min(5, Math.floor((playTimeTop - 3 - textLines - scoresTop - scoreFirst)
                                                            / scorePitch) + 1)

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
    // game kept where it is in it, or landOn.
    function reload(landOn) {
        if (!arcadeBackend)
            return
        var keep = landOn || (currentSet !== "" ? currentSet : (navListState.set || ""))
        filters = arcadeBackend.filters()
        var at = indexOfFilter(wanted)
        filterIndex = Math.max(0, at)
        // One with no games once MAME has answered gives way to the one
        // shown, which a favourite added later doesn't take the list from.
        if (at < 0 && !scanning && filterId !== "")
            wanted = filterId
        var place = keep !== "" && filterId !== "" ? arcadeBackend.filterIndex(filterId, keep) : -1
        selected = place >= 0 ? place : Math.max(0, Math.min(selected, count - 1))
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

    // Plays a game; coming back lands on it in listFilter.
    function play(set, name, listFilter) {
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

    // Select plays the game as it is let go, and opens its options once held
    // this long. Only a press this screen saw counts, while the timer runs:
    // one let go after the options or a game took it doesn't.
    Timer {
        id: holdTimer
        interval: 800
        onTriggered: arcadeScreen.openOptions()
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
        if (!game.set || game.unknown)
            return
        // There as it plays, for a game played from the screen itself.
        wanted = "favorites"
        reload(set)
        play(set, game.name, "favorites")
    }

    Component.onCompleted: {
        // The high scores kept, read before the first game's are asked for.
        if (scoresBackend)
            scoresBackend.refresh()
        reload()
        if (!navParams.navListState && navParams.startupPlay)
            Qt.callLater(playAtStartup, navParams.startupPlay)
    }

    Connections {
        target: arcadeBackend
        function onGamesChanged() { arcadeScreen.reload() }
        // A game played from the screen itself (Start Info Screen off): its
        // play time, Recently Played, a snapshot taken in it and its high
        // scores, as the screen opening again would read them.
        function onFinished() {
            if (arcadeScreen.scoresBackend)
                arcadeScreen.scoresBackend.refresh()
            arcadeScreen.reload()
        }
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
        // A held select is select alone.
        if (!event.isAutoRepeat && event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter)
            holdTimer.stop()
        if (event.key === Qt.Key_5) {
            // COIN.
            if (!event.isAutoRepeat)
                moduleRoot.addCredit()
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
                holdTimer.restart()
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            if (!event.isAutoRepeat)
                arcadeScreen.goBack()
        } else {
            return
        }
        event.accepted = true
    }

    Keys.onReleased: function(event) {
        if ((event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter) || event.isAutoRepeat)
            return
        event.accepted = true
        if (!holdTimer.running)
            return
        holdTimer.stop()
        playCurrent()
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
        text: arcadeScreen.filter ? arcadeScreen.filter.name : ""
        arrows: arcadeScreen.filters.length > 1
        rightText: (arcadeScreen.page + 1) + "/" + arcadeScreen.pages
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
            readonly property int arc: Math.round(24 * Math.sin(Math.PI * (index + 0.5) / arcadeScreen.perPage))
            readonly property color ink: isCurrent ? root.surfaceColor : root.primaryColor
            readonly property real gap: Math.round(10 * arcadeScreen.u)
            x: Math.round((132 + arc - (isCurrent ? 16 : 0)) * arcadeScreen.u)
            y: Math.round((arcadeScreen.listTop + index * arcadeScreen.pitch) * arcadeScreen.v)
            width: Math.round((isCurrent ? 336 : 320) * arcadeScreen.u)
            height: Math.round(arcadeScreen.rowHeight * arcadeScreen.v)

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
                x: row.gap + root.px
                anchors.verticalCenter: parent.verticalCenter
                text: ("00" + row.number).slice(-Math.max(3, String(row.number).length))
                color: row.ink
                font.family: root.globalFont
                font.pixelSize: arcadeScreen.textSize
            }

            // The name: cut short with "…" when it is too long for the row,
            // scrolled through instead while the game is selected.
            Item {
                id: nameClip
                x: numberText.x + numberText.width + row.gap
                width: (heart.visible ? heart.x - row.gap : row.width - row.gap - root.px) - x
                height: parent.height
                clip: true

                Text {
                    id: nameText
                    anchors.verticalCenter: parent.verticalCenter
                    width: row.isCurrent ? implicitWidth : Math.min(implicitWidth, nameClip.width)
                    text: row.modelData.name
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: row.ink
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: arcadeScreen.nameSize
                }

                // Whether it scrolls, and how far: started (and started over)
                // once the row has its new width, a turn of the event loop
                // later, as MenuRow's value is.
                readonly property bool scrolls: row.isCurrent && nameText.implicitWidth > width
                readonly property real scrollBy: width - nameText.implicitWidth
                onScrollsChanged: Qt.callLater(updateScroll)
                onScrollByChanged: Qt.callLater(updateScroll)
                function updateScroll() {
                    if (scrolls)
                        scroll.restart()
                    else
                        scroll.stop()
                }

                SequentialAnimation {
                    id: scroll
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) nameText.x = 0

                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        target: nameText
                        property: "x"
                        to: nameClip.scrollBy
                        duration: Math.abs(to) * 20
                    }
                    PauseAnimation { duration: 2000 }
                    PropertyAction { target: nameText; property: "x"; value: 0 }
                }
            }
            PixelIcon {
                id: heart
                visible: !!row.modelData.favorite
                name: "heart"
                color: row.ink
                x: row.width - width - row.gap - root.px
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ---
    // The column: the picture, the high scores, the play time
    // ---

    Rectangle {
        id: preview
        visible: arcadeScreen.filters.length > 0
        x: Math.round(arcadeScreen.columnX * arcadeScreen.u)
        y: Math.round(90 * arcadeScreen.v)
        width: Math.round(arcadeScreen.columnWidth * arcadeScreen.u)
        height: Math.round(259 * arcadeScreen.v)
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
                font.pixelSize: arcadeScreen.nameSize
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
                font.pixelSize: arcadeScreen.textSize
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
                font.pixelSize: arcadeScreen.textSize
            }
        }
    }

    // The high scores, under the picture: a line for each the column has
    // room for, kept from game to game.
    Item {
        visible: arcadeScreen.isGame
        x: Math.round(arcadeScreen.columnX * arcadeScreen.u)
        y: Math.round(arcadeScreen.scoresTop * arcadeScreen.v)
        width: Math.round(arcadeScreen.columnWidth * arcadeScreen.u)

        Text {
            text: "Hi-Score"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: arcadeScreen.textSize
        }
        Repeater {
            model: arcadeScreen.scoreRows
            Item {
                id: scoreLine
                required property int index
                readonly property var entry: index < arcadeScreen.scores.length ? arcadeScreen.scores[index] : null
                visible: entry !== null
                y: Math.round((arcadeScreen.scoreFirst + index * arcadeScreen.scorePitch) * arcadeScreen.v)
                width: parent.width
                height: arcadeScreen.textSize
                Text {
                    text: scoreLine.entry ? scoreLine.entry.rank : ""
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: arcadeScreen.textSize
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: scoreLine.entry && scoreLine.entry.name !== "" ? Math.round(64 * arcadeScreen.u) : 0
                    text: scoreLine.entry ? scoreLine.entry.score : ""
                    textFormat: Text.PlainText
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: arcadeScreen.textSize
                }
                Text {
                    anchors.right: parent.right
                    width: Math.round(50 * arcadeScreen.u)
                    horizontalAlignment: Text.AlignRight
                    text: scoreLine.entry ? scoreLine.entry.name : ""
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: arcadeScreen.textSize
                }
            }
        }
        Text {
            visible: arcadeScreen.scores.length === 0
            y: Math.round(arcadeScreen.scoreFirst * arcadeScreen.v)
            text: "No Scores Yet"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: arcadeScreen.textSize
        }
    }

    // How long the game has been played, against the column's foot. The
    // label where the high scores' are: MenuRow pads its line.
    MenuRow {
        visible: arcadeScreen.isGame
        x: Math.round(arcadeScreen.columnX * arcadeScreen.u) - pad
        y: Math.round(arcadeScreen.playTimeTop * arcadeScreen.v)
        width: Math.round(arcadeScreen.columnWidth * arcadeScreen.u) + 2 * pad
        height: arcadeScreen.textSize
        fontSize: arcadeScreen.textSize
        label: "Play Time"
        value: arcadeScreen.playTime(arcadeScreen.details.playTime)
    }

    // What the game is, for the help line: its full name as MAME has it,
    // then its system, maker, year and players, those MAME says.
    readonly property string about: {
        var d = details
        return [d.name || "", d.system || "", d.maker || "", d.year || "",
                d.players > 0 ? d.players + (d.players === 1 ? " Player" : " Players") : ""].join("\n")
    }

    // ---
    // Nothing to list
    // ---

    // No games: what stands in the way, and where the games go; while MAME is
    // asked, that it is.
    NoGames {
        visible: arcadeScreen.filters.length === 0
        reading: arcadeScreen.scanning
        problem: arcadeScreen.problem
        folderReadable: arcadeScreen.folderReadable
        gamesFolder: arcadeScreen.gamesFolder
    }

    // ---
    // The help line and the hint bar
    // ---

    // What the game is; why a set isn't a game. What stands in the way of
    // the list comes first.
    HelpLine {
        visible: arcadeScreen.filters.length > 0 && !options.visible
        text: arcadeScreen.problem !== "" ? arcadeScreen.problem
              : arcadeScreen.scanning ? "Reading the games…"
              : arcadeScreen.details.unknown ? arcadeScreen.details.detail
              : arcadeScreen.about
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // ▲ ▼ and ◄ ► go without saying, the filter's bar showing ◄ ►, and
    // START with select: the bar has room for four.
    HintBar {
        visible: !options.visible
        text: arcadeScreen.filters.length === 0 ? root.hints.back + ":BACK"
              : root.hints.back + ":BACK " + root.hints.coin + ":COIN " + root.hints.select + ":PLAY HOLD:OPTIONS"
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
