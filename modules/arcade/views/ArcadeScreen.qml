import QtQuick
import Components

// The Arcade screen (Settings → Arcade → Screen: Arcade), a cabinet's
// selection screen in place of the original tree (Games.qml). The games of
// one filter (ArcadeBackend::filters) lie as Settings → Arcade → List Style
// says, one under another or on an arc a page at a time, or on a wheel
// turning once the game chosen reaches its top or bottom row, under a bar
// naming the filter and the page; on the right the game's pictures, one
// after another, with the credits bought, and under them a box of its high
// scores (the High Scores module's), what it is (ArcadeBackend::gameDetails)
// and how long it has been played, as many lines as fit; under them the
// help line with its full name and what the box has no line for, the
// status icons at its right, and the hint bar. The module's own
// Hint Bar and Help Line settings (Root.qml's, which root.hintBar and
// root.helpLine go by here) hide those, and their room goes to the list, a
// row more each, and to the box. Laid out in the design's units, a 960th of
// the width and a 720th of the height, in letters a CRT reads: none smaller
// than the hint bar's and the help line's, but for the wheel's rows going
// back; and in an area that Settings → Arcade → Screen Adjustment moves
// each side of, which then holds as many games and lines as fit.
//
// ▲ ▼ choose a game, from page to page (the wheel a row at a time) and
// round; ◄ ► go round the filters, keeping the game where the next filter
// has it. COIN (MAME's 5, which a pad's Back button, MAME's coin, is here:
// takesCoin) buys a credit; select, let go, or START (Space, a pad's Start,
// or 1) plays the game with them (navigateTo Play.qml, which Root.qml plays
// as Start Info Screen says); select held opens its options (Play, Game
// Details, its favourite, Play at Startup, and with Online Play on,
// RetroArch Lobby, the whole lobby: Lobby.qml) and back leaves the module.
// Game Details (Details.qml) says what the game is and lists its
// achievements on RetroAchievements. Coming back from it or from a game
// restores the filter, the game and its row on the wheel from the listState
// handed to navigateTo; a game played from the screen itself (Start Info
// Screen off) leaves it as it was. Opened with the startup favourite
// (navParams.startupPlay), it plays that as soon as the folder has been
// read, the screen on it in Favorites.
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
    readonly property int nameLines: 30
    readonly property int nameSize: Math.round(nameLines * v)

    // The area everything lies in: from the bar's top and the help line's
    // left to the column's right and the hint bar's foot, as the design has
    // it, each side moved out (in, below 0) as far as Screen Adjustment says
    // (Root.qml's screenAdjustment), in the design's units, within what
    // adjustSide() allows (withinLimits()).
    readonly property var adjustment: withinLimits(moduleRoot.screenAdjustment)
    readonly property int areaLeft: 120 - adjustment.left
    readonly property int areaTop: 90 - adjustment.top
    readonly property int areaRight: 840 + adjustment.right
    readonly property int areaBottom: 645 + adjustment.bottom
    readonly property int areaWidth: areaRight - areaLeft
    // Where the list and the column end: above the help line, or above what
    // is in its place.
    readonly property int listBottom: areaBottom - (root.hintBar ? (root.helpLine ? 93 : 41) : (root.helpLine ? 53 : 1))
    // Settings → Arcade → List Style (Root.qml's listStyle): the games one
    // under another (Straight) or on an arc (Curved), a page at a time; or
    // on a wheel (3D Curved), where the game chosen goes up and down its
    // rows, furthest out, the others going back and fading the further they
    // are from it, and the wheel turns a row once it would leave them.
    readonly property bool straight: moduleRoot.listStyle === "Straight"
    readonly property bool wheel: moduleRoot.listStyle === "3D Curved"
    // The rows under the bar, as many as fit minPitch lines apart (seven with
    // both bars, as the design has it), spread over the room; the wheel's, as
    // many as it holds packed as a wheel is (wheelRows()).
    readonly property int listTop: areaTop + 57
    readonly property int listRoom: listBottom - listTop
    readonly property int rowHeight: 48
    readonly property int minPitch: 54
    readonly property int rowsFit: Math.floor((listRoom - rowHeight) / minPitch) + 1
    readonly property int perPage: wheel ? wheelRows() : rowsFit
    readonly property int pitch: Math.floor((listRoom - rowHeight) / Math.max(1, perPage - 1))
    // The wheel's middle row (of an even number, the lower of the two), where
    // a game chosen afresh comes to (a filter, the screen opened), and how
    // many rows from the game chosen a row is as far back as the wheel goes:
    // a row more than its ends are from its middle, so with the game there
    // they aren't quite as far back.
    readonly property int middleRow: Math.floor(perPage / 2)
    readonly property int wheelReach: perPage - middleRow
    // On the wheel, the size of a row so many rows from the game chosen, as
    // far back as it is (depth, 0 to 1 wheelReach rows from it): 0.7 of a
    // row as far back as the wheel goes.
    function wheelSize(distance, reach) {
        return 1 - 0.3 * Math.min(1, distance / reach)
    }
    // The wheel's rows as a wheel seen from the front has them, with the
    // game chosen in the row selected: each the size it is, the gap to the
    // next as much smaller, from listTop to listBottom. Their tops, in the
    // design's lines, and how far minPitch is stretched (k above 1) or
    // squeezed between them to fill the room.
    function wheelLayout(rows, selected) {
        var sizes = []
        for (var i = 0; i < rows; ++i)
            sizes.push(wheelSize(Math.abs(i - selected), rows - Math.floor(rows / 2)))
        var between = 0   // from the first row's middle to the last's, in minPitches
        for (i = 0; i + 1 < rows; ++i)
            between += (sizes[i] + sizes[i + 1]) / 2
        var ends = rowHeight * (sizes[0] + sizes[rows - 1]) / 2
        var k = between > 0 ? (listRoom - ends) / (minPitch * between) : 1
        var tops = []
        var middle = listTop + rowHeight * sizes[0] / 2
        for (i = 0; i < rows; ++i) {
            if (i > 0)
                middle += minPitch * k * (sizes[i - 1] + sizes[i]) / 2
            tops.push(middle - rowHeight * sizes[i] / 2)
        }
        return { k: k, tops: tops }
    }
    // As many rows as the wheel holds wherever the game chosen is, its rows
    // at full size wheelGap lines apart at least.
    readonly property int wheelGap: 4
    function wheelRows() {
        if (listRoom < rowHeight)
            return 0
        var rows = 1
        for (var more = 2; more < 30; ++more) {
            for (var selected = 0; selected < more; ++selected) {
                if (wheelLayout(more, selected).k * minPitch < rowHeight + wheelGap)
                    return rows
            }
            rows = more
        }
        return rows
    }
    // The wheel's rows' tops with the game chosen where it is.
    readonly property var wheelTops: wheel ? wheelLayout(perPage, selectedRow).tops : []
    // The column right of the list, its share of the area's width, or
    // narrower where the height would leave the box under its picture no
    // line: the picture, 4:3 on the screen (the design's 345 by 259), and
    // infoGap under it a box down to the list's foot, its lines infoPadX and
    // infoPadY in from its frame. The list and the bar over it have the rest,
    // 20 short of it.
    readonly property int infoGap: 7
    readonly property int infoPadX: 10
    readonly property int infoPadY: 6
    readonly property real pictureAspect: 345 / 259
    readonly property int pictureRoom: listBottom - areaTop - infoGap - 2 * infoPadY - textLines
    readonly property int columnWidth: Math.min(Math.round(345 * areaWidth / 720), Math.floor(pictureRoom * pictureAspect))
    readonly property int columnX: areaRight - columnWidth
    readonly property int pictureHeight: Math.round(columnWidth / pictureAspect)
    readonly property int listWidth: columnX - 20 - areaLeft
    readonly property int infoTop: areaTop + pictureHeight + infoGap
    // The box's inside, across: where its lines start, and their width.
    readonly property int infoX: columnX + infoPadX
    readonly property int infoWidth: columnWidth - 2 * infoPadX
    // The box's lines, a scorePitch apart, as many as its height holds, in
    // this order: the play time, against its foot; the high scores' heading
    // with the first of them, scoreFirst under it; the next two; then the
    // game's maker, year, players and system (detailList), over the play
    // time. The help line says what the box has no line for.
    readonly property int scoresTop: infoTop + infoPadY
    readonly property int scoreFirst: 32
    readonly property int scorePitch: 29
    readonly property int playTimeTop: listBottom - infoPadY - textLines
    readonly property int infoLines: Math.max(0, Math.floor((playTimeTop - 3 - textLines - scoresTop - scoreFirst)
                                                            / scorePitch) + 1)
    readonly property int scoreRows: Math.min(3, infoLines)
    readonly property int detailRows: Math.min(4, infoLines - scoreRows)

    // The filters with games (ArcadeBackend::filters), the one shown, and the
    // one wanted: Start Filter's, or the one ◄ ► came to, shown as soon as it
    // has games (while MAME is asked, only the lists have).
    property var filters: []
    property int filterIndex: 0
    property string wanted: navListState.filter || moduleRoot.startFilter || "all"
    readonly property var filter: filters.length > 0 ? filters[Math.min(filterIndex, filters.length - 1)] : null
    readonly property string filterId: filter ? filter.id : ""
    readonly property int count: filter ? filter.count : 0
    // The game selected, by its place in the filter, and the page it is on.
    property int selected: 0
    readonly property int page: Math.floor(selected / perPage)
    readonly property int pages: Math.max(1, Math.ceil(count / perPage))
    // Counted up as what the filters hold changes, so the rows and the game
    // are read again.
    property int revision: 0
    // The place of the game in the wheel's top row, round from the list's
    // end to its start once the filter has a game for every row (else 0).
    property int wheelTop: 0
    // The games in the rows (gamesInRows()), and the row of the game
    // selected.
    readonly property var rows: (revision, gamesInRows())
    readonly property int selectedRow: wheel ? (count > 0 ? (selected - wheelTop + count) % count : 0)
                                             : selected - page * perPage
    readonly property var current: rows[selectedRow] ? rows[selectedRow].game : null
    // The wheel turned so the game selected is in that row.
    function placeWheel(row) {
        wheelTop = count >= perPage ? ((selected - row) % count + count) % count : 0
    }
    // Its rows changed (Screen Adjustment): the game keeps its row, the last
    // if there are fewer now. The style and the bars are read as the module
    // opens, before this screen is made.
    onPerPageChanged: if (wheel) placeWheel(Math.min(selectedRow, perPage - 1))
    // A { place, game } for each row, null for one with none: the page's; or
    // the wheel's, from its top row on.
    function gamesInRows() {
        var shown = []
        for (var r = 0; r < perPage; ++r)
            shown.push(null)
        if (!arcadeBackend || filterId === "" || count === 0)
            return shown
        // n games from place from on, in the rows from row down.
        var fill = function(row, from, n) {
            var games = n > 0 ? arcadeBackend.filterGames(filterId, from, n) : []
            for (var i = 0; i < games.length; ++i)
                shown[row + i] = { place: from + i, game: games[i] }
        }
        if (!wheel) {
            fill(0, page * perPage, perPage)
            return shown
        }
        // Round from the list's end to its start; fewer games than rows
        // from the top row down.
        if (count >= perPage) {
            var start = (wheelTop % count + count) % count
            var head = Math.min(perPage, count - start)
            fill(0, start, head)
            fill(head, 0, perPage - head)
        } else {
            fill(0, 0, count)
        }
        return shown
    }
    readonly property string currentSet: current ? current.set : ""
    readonly property bool isGame: !!current && !current.unknown
    // Counted up as a game's pictures come in (RetroAchievements'), so its
    // details are read again.
    property int picturesRevision: 0
    readonly property var details: (revision, picturesRevision, arcadeBackend && currentSet !== ""
                                                                ? arcadeBackend.gameDetails(currentSet) : ({}))
    // The game's pictures (ArcadeBackend::picturesOf), one after another
    // every few seconds while it has more than one, from the first as it is
    // chosen, but while a game plays; RetroAchievements' downloaded as it is
    // shown, with a Web API key (wantPictures()).
    readonly property var pictures: isGame && details.pictures ? details.pictures : []
    property int pictureIndex: 0
    onCurrentSetChanged: {
        pictureIndex = 0
        if (pictureTurn.running)
            pictureTurn.restart()
    }
    onDetailsChanged: {
        if (isGame && arcadeBackend)
            arcadeBackend.wantPictures(currentSet)
    }
    Timer {
        id: pictureTurn
        interval: 4000
        repeat: true
        running: arcadeScreen.pictures.length > 1 && !!arcadeBackend && !arcadeBackend.running
        onTriggered: arcadeScreen.pictureIndex = (arcadeScreen.pictureIndex + 1) % arcadeScreen.pictures.length
    }
    // Its achievements on RetroAchievements, counted, with a Web API key
    // (the box's first line while it has some): read again once
    // RetroAchievements has answered, and after the game has been played.
    property int achievementsRevision: 0
    readonly property var achievementCount: (revision, achievementsRevision,
                                             isGame && arcadeBackend ? arcadeBackend.achievementCount(currentSet) : ({}))
    // The game plays in FinalBurn Neo, which takes no credits bought here: as
    // its engine says, and a room of the lobby always.
    readonly property bool fbneo: details.engine === "fbneo" || (!!current && current.room !== undefined)
    // The column's ground, the scheme's background as solid as Settings →
    // Arcade → Info Background says (Root.qml's infoBackground), over Settings'
    // Background Effect. Main.qml names its colours; a color property reads
    // the name as the colour.
    readonly property color surface: root.surfaceColor
    readonly property color ground: Qt.rgba(surface.r, surface.g, surface.b, moduleRoot.infoBackground / 100)
    // The frame round the picture and the box, as wide as Settings → Arcade
    // → Border has it (Root.qml's border): a line, or none.
    readonly property int frame: moduleRoot.border ? root.px : 0
    // The game's high scores, read again once the High Scores module has
    // read MAME's files (a game just played), or for a game FinalBurn Neo
    // plays with Online High Scores on and a Web API key kept, its
    // RetroAchievements leaderboard's, once it has been read (board: its
    // state, "off" for any other game).
    property int scoresRevision: 0
    readonly property var board: (scoresRevision, arcadeBackend && isGame
                                                  ? arcadeBackend.leaderboard(currentSet) : ({ state: "off" }))
    readonly property bool onlineScores: !!board.state && board.state !== "off"
    readonly property var scores: (scoresRevision, onlineScores ? (board.entries || [])
                                                   : scoresBackend && isGame
                                                     ? scoresBackend.topEntries(currentSet, scoreRows) : [])

    // Screen Adjustment: whether ▲ ▼ ◄ ► move the sides in rather than out;
    // how far a press moves one, up to the screen's edge; and the least room
    // the list and the column need.
    readonly property bool adjusting: !!navParams.adjust
    property bool shrinking: false
    readonly property int adjustStep: 6
    readonly property int minAreaWidth: 600
    readonly property int minAreaHeight: 420

    // A saved adjustment as adjustSide() could have made it, whatever
    // config.json says: no side out past the screen's edge, and a pair of
    // sides that leaves less than the least room back where the design has
    // them.
    function withinLimits(saved) {
        var sides = { top: Math.min(90, saved.top), bottom: Math.min(720 - 645, saved.bottom),
                      left: Math.min(120, saved.left), right: Math.min(960 - 840, saved.right) }
        if (840 + sides.right - (120 - sides.left) < minAreaWidth)
            sides.left = sides.right = 0
        if (645 + sides.bottom - (90 - sides.top) < minAreaHeight)
            sides.top = sides.bottom = 0
        return sides
    }

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
    // game kept where it is in it, or landOn. A filter shown in the wanted
    // one's place while that one has no games yet (the folder read, MAME
    // asked) hands its game on only once ▲ ▼ have moved in it (browsed):
    // else the wanted one opens as the screen would have, on the game it was
    // left on or its first.
    property bool browsed: false
    function reload(landOn) {
        if (!arcadeBackend)
            return
        var standIn = filterId !== "" && filterId !== wanted && !browsed
        var fromCurrent = !landOn && currentSet !== "" && !standIn
        var keep = landOn || (fromCurrent ? currentSet : (navListState.set || ""))
        // On Online Games, the room it was on: its game may be hosted twice.
        var keepRoom = landOn ? -1
                     : fromCurrent ? (current.room !== undefined ? current.room : -1)
                     : (navListState.room !== undefined ? navListState.room : -1)
        // Where it is: its filter, and its row on the wheel.
        var keptFilter = filterId
        var keptRow = selectedRow
        filters = arcadeBackend.filters()
        var at = indexOfFilter(wanted)
        filterIndex = Math.max(0, at)
        // One with no games once MAME has answered gives way to the one
        // shown, which a favourite added later doesn't take the list from;
        // not one whose games are still to come (RetroAchievements' list).
        if (at < 0 && !scanning && filterId !== "" && !arcadeBackend.filterPending(wanted))
            wanted = filterId
        var place = keep !== "" && filterId !== "" ? arcadeBackend.filterIndex(filterId, keep, keepRoom) : -1
        selected = place >= 0 ? place : (standIn ? 0 : Math.max(0, Math.min(selected, count - 1)))
        // On the wheel, the game kept keeps its row while its filter is the
        // one shown still, and the game come back to after playing it the
        // row it was played from; any other comes to the middle row.
        var row = fromCurrent ? (filterId === keptFilter ? keptRow : middleRow)
                : !landOn && navListState.row !== undefined ? navListState.row : middleRow
        placeWheel(Math.min(row, perPage - 1))
        revision++
    }

    function move(delta) {
        if (count === 0)
            return
        browsed = true
        // The wheel turns a row as the game chosen would leave its rows,
        // round from the list's end to its start, before the game changes,
        // so it is never out of them.
        var row = selectedRow + delta
        if (wheel && count >= perPage && (row < 0 || row >= perPage))
            wheelTop = (wheelTop + delta + count) % count
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
        placeWheel(middleRow)
    }

    // Plays a game; coming back lands on it in listFilter.
    // A game played, or with online, a room of the lobby joined ({ room,
    // host }) or one hosted ({ hosting: true }), of the local network with
    // lan: true too.
    function play(set, name, listFilter, online) {
        var params = { game: set, title: name }
        var state = { filter: listFilter, set: set, row: selectedRow }
        if (online)
            params.online = online
        if (online && online.room !== undefined)
            state.room = online.room
        arcadeScreen.navigateTo("Play.qml", params, state)
    }
    function playCurrent() {
        if (current && current.room !== undefined)
            play(currentSet, current.name, filterId, { room: current.room, host: current.host, lan: !!current.lan })
        else if (isGame)
            play(currentSet, details.name || current.name, filterId)
    }

    function openOptions() {
        if (isGame && current.room === undefined)
            options.offer({ name: details.name || current.name, path: "game:" + currentSet, isFolder: false })
    }
    // Its Game Details; coming back lands on it, as from playing it.
    function openDetails() {
        navigateTo("Details.qml", { game: currentSet, title: details.name || current.name },
                   { filter: filterId, set: currentSet, row: selectedRow })
    }

    // The lobby read again while its rooms are the filter shown.
    Timer {
        interval: 20000
        repeat: true
        running: arcadeScreen.filterId === "online" && !!arcadeBackend && !arcadeBackend.running
        onTriggered: arcadeBackend.refreshRooms()
    }
    // The local network asked again while the screen is up, so that LAN
    // Games comes as soon as someone there hosts a game, and goes as they
    // stop (with LAN Play off, the backend asks nothing).
    Timer {
        interval: 5000
        repeat: true
        running: arcadeScreen.visible && !!arcadeBackend && !arcadeBackend.running
        onTriggered: arcadeBackend.refreshLanRooms()
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
    // Waits here while the folder is read (ArcadeBackend reads it as the
    // module opens).
    property var startupEntry: null
    function playAtStartup(entry) {
        if (arcadeBackend.reading) {
            startupEntry = entry
            return
        }
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
        function onReadingChanged() {
            var entry = arcadeScreen.startupEntry
            if (entry && !arcadeBackend.reading) {
                arcadeScreen.startupEntry = null
                arcadeScreen.playAtStartup(entry)
            }
        }
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
    Connections {
        target: arcadeBackend
        ignoreUnknownSignals: true
        function onLeaderboardReady(set) {
            if (set === arcadeScreen.currentSet)
                arcadeScreen.scoresRevision++
        }
        function onGameAchievementsReady(set) {
            if (set === arcadeScreen.currentSet)
                arcadeScreen.achievementsRevision++
        }
        // Named after the game or its parent: read again either way.
        function onPicturesChanged() { arcadeScreen.picturesRevision++ }
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
            // COIN, for MAME to put in: FinalBurn Neo takes the game's own.
            if (!event.isAutoRepeat && !arcadeScreen.fbneo)
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
        // The page of so many; on the wheel, the game of so many.
        rightText: arcadeScreen.adjusting ? ""
                   : arcadeScreen.wheel ? (arcadeScreen.selected + 1) + "/" + arcadeScreen.count
                   : (arcadeScreen.page + 1) + "/" + arcadeScreen.pages
    }

    Repeater {
        model: arcadeScreen.perPage
        Item {
            id: row
            required property int index
            readonly property var entry: arcadeScreen.rows[index] || null
            readonly property var game: entry ? entry.game : ({})
            readonly property int number: entry ? entry.place + 1 : 0
            readonly property bool isCurrent: !!entry && index === arcadeScreen.selectedRow
            // On the wheel, how far the row is from the game chosen: from 0
            // there to 1 wheelReach rows from it, and no further. It is as
            // much further back: smaller, and fainter, wherever the game
            // chosen is.
            readonly property real depth: arcadeScreen.wheel
                                          ? Math.min(1, Math.abs(index - arcadeScreen.selectedRow) / arcadeScreen.wheelReach) : 0
            readonly property real size: arcadeScreen.wheel
                                         ? arcadeScreen.wheelSize(Math.abs(index - arcadeScreen.selectedRow), arcadeScreen.wheelReach)
                                         : 1
            // The arc the rows' left ends lie on, those at the page's ends (the
            // wheel's) furthest left and those in its middle furthest right;
            // none for Straight. The selected row stands out of the others to
            // the left, and a straight one reaches the bar's ends.
            readonly property int arc: arcadeScreen.straight ? 0
                                       : Math.round(24 * Math.sin(Math.PI * (index + 0.5) / arcadeScreen.perPage))
            readonly property int boxLeft: arcadeScreen.areaLeft + (arcadeScreen.straight ? 16 : 12 + arc)
                                           - (isCurrent ? 16 : 0)
            readonly property int boxWidth: arcadeScreen.listWidth - (arcadeScreen.straight ? 16 : 35)
                                            + (isCurrent ? 16 : 0)
            readonly property color ink: isCurrent ? root.surfaceColor : root.primaryColor
            readonly property real gap: Math.round(10 * size * arcadeScreen.u)
            visible: entry !== null
            opacity: 1 - 0.6 * depth
            // Made smaller from its left end, which keeps to the arc; on the
            // wheel, nearer the next row as it is smaller (wheelLayout()).
            x: Math.round(boxLeft * arcadeScreen.u)
            y: Math.round((arcadeScreen.wheel ? arcadeScreen.wheelTops[index] || 0
                                              : arcadeScreen.listTop + index * arcadeScreen.pitch) * arcadeScreen.v)
            width: Math.round(boxWidth * size * arcadeScreen.u)
            height: Math.round(arcadeScreen.rowHeight * size * arcadeScreen.v)

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
                font.pixelSize: Math.round(arcadeScreen.textLines * row.size * arcadeScreen.v)
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
                    text: row.game.name || ""
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: row.ink
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: Math.round(arcadeScreen.nameLines * row.size * arcadeScreen.v)
                    onTextChanged: Qt.callLater(nameClip.updateScroll)
                }

                // Whether it scrolls, and how far: started (and started over)
                // once the row has its new width, a turn of the event loop
                // later, as MenuRow's value is; and for each name the row
                // comes to (the wheel's rows as it turns).
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
                visible: !!row.game.favorite
                name: "heart"
                color: row.ink
                x: row.width - width - row.gap - root.px
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ---
    // The column: the picture, and the box of the high scores, the game's
    // details and the play time
    // ---

    Rectangle {
        id: preview
        visible: arcadeScreen.filters.length > 0
        x: Math.round(arcadeScreen.columnX * arcadeScreen.u)
        y: Math.round(arcadeScreen.areaTop * arcadeScreen.v)
        width: Math.round(arcadeScreen.columnWidth * arcadeScreen.u)
        height: Math.round(arcadeScreen.pictureHeight * arcadeScreen.v)
        color: shown.status === Image.Ready ? "#000000" : arcadeScreen.ground
        border.width: arcadeScreen.frame
        border.color: root.primaryColor
        antialiasing: false

        // The picture to show, and the two that show it: the one on the
        // screen, and the next loading behind it, which takes its place once
        // it is in (or can't be), so the pictures cut from one to the next.
        readonly property string wanted: arcadeScreen.pictures.length > 0
                                         ? arcadeScreen.pictures[arcadeScreen.pictureIndex % arcadeScreen.pictures.length]
                                         : ""
        property Image shown: pictureA
        readonly property Image behind: shown === pictureA ? pictureB : pictureA
        onWantedChanged: showWanted()
        Component.onCompleted: showWanted()
        function showWanted() {
            if (wanted === "") {
                pictureA.source = ""
                pictureB.source = ""
            } else if (shown.source.toString() !== wanted) {
                behind.source = wanted
                takeOver(behind)
            }
        }
        function takeOver(picture) {
            if (picture === behind && picture.source.toString() === wanted
                && (picture.status === Image.Ready || picture.status === Image.Error))
                shown = picture
        }

        // A picture in the frame. A screenshot (its shape near enough the
        // game's screen's, whatever its pixels) fills the screen, 4:3 (a
        // vertical game's 3:4, upright in the middle); one of another shape (a
        // box, a flyer, a marquee) keeps its own, as big as fits. Each is
        // decoded no bigger than the screen, in its own shape (sourceSize).
        readonly property real screenHeight: height - 2 * arcadeScreen.frame
        readonly property real screenWidth: arcadeScreen.details.vertical ? Math.round(screenHeight * 3 / 4)
                                                                          : width - 2 * arcadeScreen.frame
        component Picture: Image {
            id: picture
            readonly property real shape: implicitHeight > 0 ? implicitWidth / implicitHeight : 0
            readonly property bool screenLike: shape === 0 || (arcadeScreen.details.vertical ? shape >= 0.55 && shape <= 1
                                                                                             : shape >= 1 && shape <= 2)
            visible: preview.shown === picture && status === Image.Ready
            width: screenLike ? preview.screenWidth : Math.min(preview.screenWidth, Math.round(preview.screenHeight * shape))
            height: screenLike ? preview.screenHeight : Math.min(preview.screenHeight, Math.round(preview.screenWidth / shape))
            x: Math.round((preview.width - width) / 2)
            y: Math.round((preview.height - height) / 2)
            sourceSize.width: preview.screenWidth
            sourceSize.height: preview.screenHeight
            asynchronous: true
            onStatusChanged: preview.takeOver(picture)
        }
        Picture { id: pictureA }
        Picture { id: pictureB }

        // Without one, or with one that can't be shown: which file it would be.
        Column {
            visible: (preview.wanted === "" || preview.shown.status === Image.Error) && arcadeScreen.current !== null
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
            // Why none was downloaded: RetroAchievements' pictures come with
            // the account's Web API key alone (Settings → Arcade →
            // RetroAchievements Account).
            Text {
                readonly property bool noKey: arcadeBackend ? !arcadeBackend.achievementsKey : false
                readonly property bool refused: arcadeBackend ? arcadeBackend.achievementsKeyRefused : false
                visible: arcadeScreen.isGame && (noKey || refused)
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: refused ? "Web API Key Refused" : "No Web API Key"
                elide: Text.ElideRight
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: arcadeScreen.textSize
            }
        }

        // The credits bought, on a ground of their own so they read over
        // the picture; none for a game FinalBurn Neo plays.
        Rectangle {
            visible: !arcadeScreen.fbneo
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: arcadeScreen.frame
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
        border.width: arcadeScreen.frame
        border.color: root.primaryColor
        antialiasing: false
    }

    // The high scores, in the box: a line for each it has room for, kept
    // from game to game.
    Item {
        visible: arcadeScreen.isGame && arcadeScreen.scoreRows > 0
        x: Math.round(arcadeScreen.infoX * arcadeScreen.u)
        y: Math.round(arcadeScreen.scoresTop * arcadeScreen.v)
        width: Math.round(arcadeScreen.infoWidth * arcadeScreen.u)

        Text {
            text: arcadeScreen.onlineScores ? "RA Hi-Score" : "Hi-Score"
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
                // A name: initials, or a RetroAchievements user's, longer.
                readonly property int nameWidth: arcadeScreen.onlineScores ? 100 : 50
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: scoreLine.entry && scoreLine.entry.name !== ""
                                         ? Math.round((scoreLine.nameWidth + 14) * arcadeScreen.u) : 0
                    text: scoreLine.entry ? scoreLine.entry.score : ""
                    textFormat: Text.PlainText
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: arcadeScreen.textSize
                }
                Text {
                    anchors.right: parent.right
                    width: Math.round(scoreLine.nameWidth * arcadeScreen.u)
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
            text: !arcadeScreen.onlineScores ? "No Scores Yet"
                  : arcadeScreen.board.state === "loading" ? "Reading…"
                  : arcadeScreen.board.state === "none" ? "No Leaderboard"
                  : arcadeScreen.board.state === "failed" ? "Not Reachable" : "No Scores Yet"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: arcadeScreen.textSize
        }
    }

    // What the game is, a line each in the box as it has room, over the play
    // time; the rest for the help line. In the order the box takes them:
    // value is the box's ("?" for what MAME doesn't say), told the help
    // line's. First, while RetroAchievements has some of the game (or is
    // being asked), its achievements: those earned of all (7/100), or all
    // with no account to say.
    readonly property var detailList: {
        var d = details
        var players = d.players > 0 ? d.players : 0
        var list = []
        var a = achievementCount
        if (a.state === "loading")
            list.push({ label: "Achievements", value: "…", told: "" })
        else if (a.state === "ready") {
            var count = a.progress ? a.earned + "/" + a.total : String(a.total)
            list.push({ label: "Achievements", value: count,
                        told: count + (a.total === 1 ? " Achievement" : " Achievements") })
        }
        return list.concat([{ label: "Maker", value: d.maker || "?", told: d.maker || "" },
                            { label: "Year", value: d.year || "?", told: d.year || "" },
                            { label: "Players", value: players > 0 ? String(players) : "?",
                              told: moduleRoot.players(players) },
                            { label: "System", value: d.system || "?", told: d.system || "" }])
    }

    // The game's details, and how long it has been played, against the box's
    // foot. The labels where the high scores' are: MenuRow pads its line. A
    // value too long for its line (Data East Corporation) scrolls through it,
    // as About's lines do: the box's are read rather than chosen.
    Repeater {
        model: arcadeScreen.detailRows + 1
        MenuRow {
            required property int index
            readonly property var detail: index < arcadeScreen.detailRows ? arcadeScreen.detailList[index]
                                          : { label: "Play Time", value: moduleRoot.playTime(arcadeScreen.details.playTime) }
            alwaysScroll: true
            visible: arcadeScreen.isGame
            x: Math.round(arcadeScreen.infoX * arcadeScreen.u) - pad
            y: Math.round((arcadeScreen.playTimeTop - (arcadeScreen.detailRows - index) * arcadeScreen.scorePitch)
                          * arcadeScreen.v)
            width: Math.round(arcadeScreen.infoWidth * arcadeScreen.u) + 2 * pad
            height: arcadeScreen.textSize
            fontSize: arcadeScreen.textSize
            label: detail.label
            value: detail.value
        }
    }

    // What the game is, for the help line: its full name as MAME has it,
    // then those of its details the box has no line for; a room of the
    // local network's who hosts it and its versions against ours first.
    readonly property string about: (current && current.lan && current.detail ? [current.detail] : [])
        .concat([details.name || ""]).concat(detailList.slice(detailRows).map(function(detail) {
        return detail.told
    })).join("\n")

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
    // the list comes first. It makes room at its right for the status icons.
    HelpLine {
        id: helpBox
        visible: (arcadeScreen.adjusting || arcadeScreen.filters.length > 0) && !options.visible
        width: Math.round(arcadeScreen.areaWidth * arcadeScreen.u) - (root.helpLine ? arcadeScreen.statusRoom : 0)
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
    // START with select: the bar has room for four, and with no help line,
    // for the status icons at its right end.
    HintBar {
        id: hintLine
        visible: !options.visible
        width: Math.round(arcadeScreen.areaWidth * arcadeScreen.u)
        rightPadding: root.sw * 0.0125 + (root.helpLine ? 0 : arcadeScreen.statusRoom) //8
        text: arcadeScreen.adjusting
              ? root.hints.back + ":DONE " + root.hints.arrows + (arcadeScreen.shrinking ? ":SHRINK " : ":EXPAND ")
                + root.hints.select + (arcadeScreen.shrinking ? ":EXPAND" : ":SHRINK") + " HOLD:RESET"
              : arcadeScreen.filters.length === 0 ? root.hints.back + ":BACK"
              : arcadeScreen.current && arcadeScreen.current.room !== undefined
                ? root.hints.back + ":BACK " + root.hints.select + ":JOIN"
              : root.hints.back + ":BACK " + root.hints.coin + ":COIN " + root.hints.select + ":PLAY HOLD:OPTIONS"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 + Math.round((645 - arcadeScreen.areaBottom) * arcadeScreen.v) //50
        anchors.leftMargin: Math.round(arcadeScreen.areaLeft * arcadeScreen.u)
    }

    // What is on (Settings → Status Icons), at the screen's bottom right: at
    // the right of the help line, which makes room for them; with no help
    // line, at the hint bar's right end, in its letters' colour, while the
    // hint line still fits beside them; with neither, none.
    readonly property int statusRoom: statusRow.shown ? statusRow.width + Math.round(12 * u) : 0
    // The hint line at its smallest, which beside the icons must still fit.
    TextMetrics {
        id: hintProbe
        font.family: root.globalFont
        font.pixelSize: hintLine.smallest
        text: hintLine.text
    }
    StatusIcons {
        id: statusRow
        readonly property Item beside: root.helpLine ? helpBox : hintLine
        readonly property bool shown: root.statusIcons.length > 0
                                      && (root.helpLine || (root.hintBar && hintProbe.advanceWidth <= hintLine.width
                                          - hintLine.leftPadding - root.sw * 0.0125 - width - Math.round(12 * arcadeScreen.u)))
        visible: shown && beside.visible
        color: root.helpLine ? root.primaryColor : root.surfaceColor
        x: Math.round(arcadeScreen.areaRight * arcadeScreen.u) - width - (root.helpLine ? 0 : Math.round(root.sw * 0.0125))
        y: Math.round(beside.y + (beside.height - height) / 2)
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

    // A game's options: Play, Game Details, its favourite, Play at Startup,
    // its engine where MAME and FinalBurn Neo both have it, with Online Play
    // on, hosting it and RetroArch's whole lobby, and with LAN Play on,
    // hosting it on the local network.
    EntryOptions {
        id: options
        anchors.fill: parent
        moduleId: moduleRoot.moduleId
        firstChoices: [{ label: "Play", action: "play" }, { label: "Game Details", action: "details" }]
        moreChoices: (arcadeScreen.revision,
                      (engines.offered(arcadeScreen.currentSet)
                       ? [{ label: engines.lineFor(arcadeScreen.currentSet), action: "engine" }] : [])
                      .concat(arcadeBackend && arcadeBackend.canHost(arcadeScreen.currentSet)
                              ? [{ label: "Host Online Game", action: "host" }] : [])
                      .concat(arcadeBackend && arcadeBackend.onlinePlay()
                              ? [{ label: "RetroArch Lobby", action: "lobby" }] : [])
                      .concat(arcadeBackend && arcadeBackend.canHostLan(arcadeScreen.currentSet)
                              ? [{ label: "Host LAN Game", action: "hostLan" }] : []))
        onActivated: function(action) {
            if (action === "play")
                arcadeScreen.playCurrent()
            else if (action === "details")
                arcadeScreen.openDetails()
            else if (action === "engine")
                engines.offer(arcadeScreen.currentSet, arcadeScreen.details.name)
            else if (action === "host" || action === "hostLan")
                arcadeScreen.play(arcadeScreen.currentSet, arcadeScreen.details.name || arcadeScreen.current.name,
                                  arcadeScreen.filterId, { hosting: true, lan: action === "hostLan" })
            // The whole lobby; coming back lands on the game, as from it.
            else if (action === "lobby")
                arcadeScreen.navigateTo("Lobby.qml", {}, { filter: arcadeScreen.filterId, set: arcadeScreen.currentSet,
                                                           row: arcadeScreen.selectedRow })
        }
        onFavoritesEdited: arcadeScreen.reload()
        onClosed: if (!engines.visible) arcadeScreen.forceActiveFocus()
    }
    EngineChooser {
        id: engines
        anchors.fill: parent
        onEngineChosen: arcadeScreen.revision++
        onClosed: arcadeScreen.forceActiveFocus()
    }
}
