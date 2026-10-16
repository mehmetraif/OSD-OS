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
// row more each. Laid out in the design's units, a 960th of the width and a
// 720th of the height, in letters a CRT reads: none smaller than the hint
// bar's and the help line's; and in an area that Settings → Arcade → Screen
// Adjustment moves each side of, which then holds as many games as fit.
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
// startup favourite (navParams.startupPlay), it plays that at once, the
// screen on it in Favorites.
//
// Opened to adjust it (navParams.adjust, Screen Adjustment's), a frame lies
// round the area: ▲ ▼ ◄ ► move that side of it out, or in once select has
// switched to shrinking, the list and the column following at once; select
// held puts it back where the design has it, and back keeps it and leaves.
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
    // A pad's Back button is COIN here, not while the options are up, nor
    // while the screen is adjusted (InputManager).
    readonly property bool takesCoin: !options.visible && !adjusting

    // The design's units.
    readonly property real u: root.sw / 960
    readonly property real v: root.sh / 720

    // The letters, in the design's lines: the hint bar's and the help
    // line's, 18 of 480 lines, and the games' names larger.
    readonly property int textLines: 27
    readonly property int textSize: Math.round(textLines * v)
    readonly property int nameSize: Math.round(30 * v)

    // The area everything lies in: from the bar's top and the help line's
    // left to the column's right and the hint bar's foot, as the design has
    // it, each side moved out (in, below 0) as far as Screen Adjustment says
    // (Root.qml's screenAdjustment), in the design's units.
    readonly property var adjustment: moduleRoot.screenAdjustment
    readonly property int areaLeft: 120 - adjustment.left
    readonly property int areaTop: 90 - adjustment.top
    readonly property int areaRight: 840 + adjustment.right
    readonly property int areaBottom: 645 + adjustment.bottom
    readonly property int areaWidth: areaRight - areaLeft
    // Where the list and the column end: above the help line, or above what
    // is in its place.
    readonly property int listBottom: areaBottom - (root.hintBar ? (root.helpLine ? 93 : 41) : (root.helpLine ? 53 : 1))
    // The rows under the bar, as many as fit minPitch lines apart (seven with
    // both bars, as the design has it), spread over the room.
    readonly property int listTop: areaTop + 57
    readonly property int rowHeight: 48
    readonly property int minPitch: 54
    readonly property int perPage: Math.floor((listBottom - listTop - rowHeight) / minPitch) + 1
    readonly property int pitch: Math.floor((listBottom - listTop - rowHeight) / (perPage - 1))
    // The column right of the list, its share of the area's width: the
    // picture, 4:3, and under it a box of the high scores, three or as many
    // as fit, a line a scorePitch apart from scoreFirst under their heading,
    // and the play time against its foot, infoPadX and infoPadY in from its
    // frame. The list and the bar over it have the rest, 20 short of it.
    readonly property int columnWidth: Math.round(345 * areaWidth / 720)
    readonly property int columnX: areaRight - columnWidth
    readonly property int pictureHeight: Math.round(columnWidth * 259 / 345)
    readonly property int listWidth: columnX - 20 - areaLeft
    readonly property int infoTop: areaTop + pictureHeight + 7
    readonly property int infoPadX: 10
    readonly property int infoPadY: 6
    // The box's inside, across: where its lines start, and their width.
    readonly property int infoX: columnX + infoPadX
    readonly property int infoWidth: columnWidth - 2 * infoPadX
    readonly property int scoresTop: infoTop + infoPadY
    readonly property int scoreFirst: 32
    readonly property int scorePitch: 29
    readonly property int playTimeTop: listBottom - infoPadY - textLines
    readonly property int scoreRows: Math.max(0, Math.min(3, Math.floor((playTimeTop - 3 - textLines - scoresTop
                                                                         - scoreFirst) / scorePitch) + 1))

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
    // The column's ground, the scheme's background as solid as Settings →
    // Arcade → Info Background says (Root.qml's infoBackground), over Settings'
    // Background Effect. Main.qml names its colours; a color property reads
    // the name as the colour.
    readonly property color surface: root.surfaceColor
    readonly property color ground: Qt.rgba(surface.r, surface.g, surface.b, moduleRoot.infoBackground / 100)
    // The game's high scores, read again once the High Scores module has
    // read MAME's files (a game just played).
    property int scoresRevision: 0
    readonly property var scores: (scoresRevision, scoresBackend && isGame
                                                   ? scoresBackend.topEntries(currentSet, scoreRows) : [])

    // Screen Adjustment: whether ▲ ▼ ◄ ► move the sides in rather than out;
    // how far a press moves one, up to the screen's edge; and the least room
    // the list and the column need.
    readonly property bool adjusting: !!navParams.adjust
    property bool shrinking: false
    readonly property int adjustStep: 6
    readonly property int minAreaWidth: 600
    readonly property int minAreaHeight: 420

    // One side of the area ("top", "bottom", "left" or "right") a step out,
    // or in while shrinking, as far as the screen's edge and the least room
    // allow.
    function adjustSide(side) {
        var across = side === "left" || side === "right"
        var room = side === "left" ? areaLeft : side === "top" ? areaTop
                 : side === "right" ? 960 - areaRight : 720 - areaBottom
        var spare = across ? areaWidth - minAreaWidth : areaBottom - areaTop - minAreaHeight
        var by = shrinking ? -Math.min(adjustStep, Math.max(0, spare)) : Math.min(adjustStep, Math.max(0, room))
        if (by === 0)
            return
        var moved = Object.assign({}, adjustment)
        moved[side] += by
        moduleRoot.screenAdjustment = moved
        saveAdjustment.restart()
    }
    function resetAdjustment() {
        moduleRoot.screenAdjustment = { top: 0, bottom: 0, left: 0, right: 0 }
        saveAdjustment.restart()
    }
    // Saved a moment after the last move rather than at every step of a held
    // key, and as the screen leaves.
    function keepAdjustment() {
        saveAdjustment.stop()
        appCore.save_setting(moduleRoot.moduleId, "screen_adjustment", moduleRoot.screenAdjustment)
    }
    Timer {
        id: saveAdjustment
        interval: 500
        onTriggered: arcadeScreen.keepAdjustment()
    }
    Component.onDestruction: {
        if (saveAdjustment.running && appCore)
            keepAdjustment()
    }

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
    // this long (while the screen is adjusted, switches between moving the
    // sides out and in, and held puts them back). Only a press this screen
    // saw counts, while the timer runs: one let go after the options or a
    // game took it doesn't.
    Timer {
        id: holdTimer
        interval: 800
        onTriggered: arcadeScreen.adjusting ? arcadeScreen.resetAdjustment() : arcadeScreen.openOptions()
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
        if (adjusting) {
            adjustKey(event)
            return
        }
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
        if (adjusting)
            shrinking = !shrinking
        else
            playCurrent()
    }

    // Screen Adjustment's keys: ▲ ▼ ◄ ► move their side, select switches
    // between out and in (held, back as the design has it), and back keeps
    // the area and leaves.
    function adjustKey(event) {
        if (event.key === Qt.Key_Up) {
            adjustSide("top")
        } else if (event.key === Qt.Key_Down) {
            adjustSide("bottom")
        } else if (event.key === Qt.Key_Left) {
            adjustSide("left")
        } else if (event.key === Qt.Key_Right) {
            adjustSide("right")
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (!event.isAutoRepeat)
                holdTimer.restart()
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            if (!event.isAutoRepeat) {
                keepAdjustment()
                arcadeScreen.goBack()
            }
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
        visible: arcadeScreen.adjusting || arcadeScreen.filters.length > 0
        x: Math.round(arcadeScreen.areaLeft * arcadeScreen.u)
        y: Math.round(arcadeScreen.areaTop * arcadeScreen.v)
        width: Math.round(arcadeScreen.listWidth * arcadeScreen.u)
        text: arcadeScreen.adjusting ? "Screen Adjustment" : arcadeScreen.filter ? arcadeScreen.filter.name : ""
        arrows: !arcadeScreen.adjusting && arcadeScreen.filters.length > 1
        rightText: arcadeScreen.adjusting ? "" : (arcadeScreen.page + 1) + "/" + arcadeScreen.pages
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
            x: Math.round((arcadeScreen.areaLeft + 12 + arc - (isCurrent ? 16 : 0)) * arcadeScreen.u)
            y: Math.round((arcadeScreen.listTop + index * arcadeScreen.pitch) * arcadeScreen.v)
            width: Math.round((arcadeScreen.listWidth - (isCurrent ? 19 : 35)) * arcadeScreen.u)
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
        y: Math.round(arcadeScreen.areaTop * arcadeScreen.v)
        width: Math.round(arcadeScreen.columnWidth * arcadeScreen.u)
        height: Math.round(arcadeScreen.pictureHeight * arcadeScreen.v)
        color: picture.status === Image.Ready ? "#000000" : arcadeScreen.ground
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

    // The box under the picture, framed as the picture is.
    Rectangle {
        visible: arcadeScreen.filters.length > 0
        x: Math.round(arcadeScreen.columnX * arcadeScreen.u)
        y: Math.round(arcadeScreen.infoTop * arcadeScreen.v)
        width: Math.round(arcadeScreen.columnWidth * arcadeScreen.u)
        height: Math.round(arcadeScreen.listBottom * arcadeScreen.v) - y
        color: arcadeScreen.ground
        border.width: root.px
        border.color: root.primaryColor
        antialiasing: false
    }

    // The high scores, in the box: a line for each it has room for, kept
    // from game to game.
    Item {
        visible: arcadeScreen.isGame
        x: Math.round(arcadeScreen.infoX * arcadeScreen.u)
        y: Math.round(arcadeScreen.scoresTop * arcadeScreen.v)
        width: Math.round(arcadeScreen.infoWidth * arcadeScreen.u)

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

    // How long the game has been played, against the box's foot. The label
    // where the high scores' are: MenuRow pads its line.
    MenuRow {
        visible: arcadeScreen.isGame
        x: Math.round(arcadeScreen.infoX * arcadeScreen.u) - pad
        y: Math.round(arcadeScreen.playTimeTop * arcadeScreen.v)
        width: Math.round(arcadeScreen.infoWidth * arcadeScreen.u) + 2 * pad
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
        visible: (arcadeScreen.adjusting || arcadeScreen.filters.length > 0) && !options.visible
        width: Math.round(arcadeScreen.areaWidth * arcadeScreen.u)
        text: arcadeScreen.adjusting ? "The frame's sides go to the screen's edges: ▲ ▼ ◄ ► move each, and the list holds as many games as fit"
              : arcadeScreen.problem !== "" ? arcadeScreen.problem
              : arcadeScreen.scanning ? "Reading the games…"
              : arcadeScreen.details.unknown ? arcadeScreen.details.detail
              : arcadeScreen.about
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin + Math.round((645 - arcadeScreen.areaBottom) * arcadeScreen.v)
        anchors.leftMargin: Math.round(arcadeScreen.areaLeft * arcadeScreen.u)
    }

    // ▲ ▼ and ◄ ► go without saying, the filter's bar showing ◄ ►, and
    // START with select: the bar has room for four.
    HintBar {
        visible: !options.visible
        width: Math.round(arcadeScreen.areaWidth * arcadeScreen.u)
        text: arcadeScreen.adjusting
              ? root.hints.back + ":DONE " + root.hints.arrows + (arcadeScreen.shrinking ? ":SHRINK " : ":EXPAND ")
                + root.hints.select + (arcadeScreen.shrinking ? ":EXPAND" : ":SHRINK") + " HOLD:RESET"
              : arcadeScreen.filters.length === 0 ? root.hints.back + ":BACK"
              : root.hints.back + ":BACK " + root.hints.coin + ":COIN " + root.hints.select + ":PLAY HOLD:OPTIONS"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 + Math.round((645 - arcadeScreen.areaBottom) * arcadeScreen.v) //50
        anchors.leftMargin: Math.round(arcadeScreen.areaLeft * arcadeScreen.u)
    }

    // Screen Adjustment's frame, a little outside the area and inside the
    // screen, with an arrow beyond each side the way ▲ ▼ ◄ ► move it; none
    // beyond a side at the screen's edge, where there is no room.
    Item {
        id: adjustFrame
        readonly property int gap: 8
        visible: arcadeScreen.adjusting
        x: Math.round(Math.max(0, arcadeScreen.areaLeft - gap) * arcadeScreen.u)
        y: Math.round(Math.max(0, arcadeScreen.areaTop - gap) * arcadeScreen.v)
        width: Math.round(Math.min(960, arcadeScreen.areaRight + gap) * arcadeScreen.u) - x
        height: Math.round(Math.min(720, arcadeScreen.areaBottom + gap) * arcadeScreen.v) - y

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: root.px
            border.color: root.primaryColor
            antialiasing: false
        }
        Repeater {
            model: [{ across: 0.5, down: 0, out: "▲", back: "▼" }, { across: 0.5, down: 1, out: "▼", back: "▲" },
                    { across: 0, down: 0.5, out: "◄", back: "►" }, { across: 1, down: 0.5, out: "►", back: "◄" }]
            Rectangle {
                required property var modelData
                width: mark.implicitWidth + 4 * root.px
                height: mark.implicitHeight
                visible: modelData.across === 0 ? adjustFrame.x >= width
                         : modelData.across === 1 ? adjustFrame.x + adjustFrame.width + width <= arcadeScreen.width
                         : modelData.down === 0 ? adjustFrame.y >= height
                         : adjustFrame.y + adjustFrame.height + height <= arcadeScreen.height
                x: modelData.across === 0.5 ? Math.round((adjustFrame.width - width) / 2)
                   : modelData.across === 0 ? -width : adjustFrame.width
                y: modelData.down === 0.5 ? Math.round((adjustFrame.height - height) / 2)
                   : modelData.down === 0 ? -height : adjustFrame.height
                color: root.surfaceColor
                antialiasing: false
                Text {
                    id: mark
                    anchors.centerIn: parent
                    text: arcadeScreen.shrinking ? parent.modelData.back : parent.modelData.out
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: arcadeScreen.textSize
                }
            }
        }
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
