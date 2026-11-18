import QtQuick
import Components
import "Rooms.js" as Rooms

// The Arcade screen's own look (ArcadeScreen's classicLook), drawn while a
// theme's Orb look (OrbLook.qml) isn't: the bar naming the filter and the
// page, and under it the games in their rows, a page at a time or on the
// wheel; on the right the game's picture in its frame, with the credits
// bought, and the box under it of its high scores, what it is and how long
// it has been played (on a room, what is said of the room); and in the
// list's place, where a filter has nothing, why. Laid out in the design's
// units in the Arcade screen's area (Screen Adjustment's), above the help
// line. The Arcade screen keeps its keys, its list and what it shows
// (arcadeScreen, whose Loader this is), and the help line, the hint bar, the
// status icons, Search's keys (in the picture's frame) and the dialogs; this
// draws the rest.
Item {
    objectName: "classicLook"

    // The row under a point of the screen (Main.qml's followPointer(), by way
    // of ArcadeScreen's hoverAt(); x, y in the screen's coordinates, which
    // this look covers): its index, -1 for none. On the wheel only the middle
    // half of a row counts, where the pointer stays in the row it chose.
    function rowAt(x, y) {
        for (var i = 0; i < gameRows.count; ++i) {
            var row = gameRows.itemAt(i)
            var inset = arcadeScreen.wheel ? row.height / 4 : 0
            if (!row || !row.entry || x < row.x || x >= row.x + row.width
                    || y < row.y + inset || y >= row.y + row.height - inset)
                continue
            return i
        }
        return -1
    }

    // ---
    // The list
    // ---

    ArcadeBar {
        visible: arcadeScreen.adjusting || arcadeScreen.filters.length > 0
        x: Math.round(arcadeScreen.areaLeft * arcadeScreen.u)
        y: Math.round(arcadeScreen.barTop * arcadeScreen.v)
        width: Math.round(arcadeScreen.listWidth * arcadeScreen.u)
        text: arcadeScreen.adjusting ? "Screen Adjustment" : arcadeScreen.filter ? arcadeScreen.filter.name : ""
        arrows: !arcadeScreen.adjusting && arcadeScreen.filters.length > 1
        rightText: arcadeScreen.barRight
    }

    Repeater {
        id: gameRows
        model: arcadeScreen.perPage
        Item {
            id: row
            required property int index
            readonly property var entry: arcadeScreen.rows[index] || null
            readonly property var game: entry ? entry.game : ({})
            // Search's games counted under its own row, which has none.
            readonly property int number: entry ? entry.place + (arcadeScreen.filterId === "search" ? 0 : 1) : 0
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
                text: row.game.search ? "" : ("00" + row.number).slice(-Math.max(3, String(row.number).length))
                color: row.ink
                font.family: root.globalFont
                font.pixelSize: Math.round(arcadeScreen.textLines * row.size * arcadeScreen.v)
            }

            // The name: cut short with "…" when it is too long for the row,
            // scrolled through instead while the game is selected.
            ScrolledText {
                id: nameClip
                x: numberText.x + numberText.width + row.gap
                width: (markIcon.visible ? markIcon.x - row.gap : row.width - row.gap - root.px) - x
                height: parent.height
                on: row.isCurrent
                // Not while the list is hidden under the screen saver (or a
                // game's screen, which hides it): a long name redrew it every
                // frame.
                resting: root.screenSaverActive
                // Search's own row: what is typed, as it is typed.
                text: row.game.name || ""
                color: row.ink
                font.pixelSize: Math.round(arcadeScreen.nameLines * row.size * arcadeScreen.v)
                // Each name the row comes to (the wheel's rows as it turns)
                // from its beginning.
                onTextChanged: Qt.callLater(updateScroll)
            }
            // A favourite's heart, but in Favorites, where every game is
            // one; Search's glass on its own row; and on a room of the
            // lobby, ▶ for one to join, a lock for one with a password.
            PixelIcon {
                id: markIcon
                readonly property string mark: row.game.search ? "search"
                                               : row.game.lobby ? Rooms.mark(row.game.lobby)
                                               : row.game.favorite && arcadeScreen.filterId !== "favorites" ? "heart" : ""
                visible: mark !== ""
                name: mark
                color: row.ink
                x: row.width - width - row.gap - root.px
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ---
    // The column: the picture, and the box of the high scores, the game's
    // details and the play time; on a room, the box alone, of what is said
    // of the room
    // ---

    // The picture's frame; not on a room, where the box takes its place.
    Rectangle {
        id: preview
        objectName: "preview"
        visible: arcadeScreen.filters.length > 0 && !arcadeScreen.onRoom
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
        // The game's screen in the frame: as big as fits, in its shape.
        readonly property real gameShape: arcadeScreen.details.vertical ? 3 / 4 : 4 / 3
        readonly property real roomWidth: width - 2 * arcadeScreen.frame
        readonly property real roomHeight: height - 2 * arcadeScreen.frame
        readonly property bool heightBound: roomHeight > 0 && gameShape < roomWidth / roomHeight
        readonly property real screenHeight: heightBound ? roomHeight : Math.round(roomWidth / gameShape)
        readonly property real screenWidth: heightBound ? Math.round(roomHeight * gameShape) : roomWidth
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

        // Without one, or with one that can't be shown: which file it would be;
        // for a set that isn't a game, that it isn't recognized. None on
        // Search, whose keys are in the frame.
        Column {
            visible: (preview.wanted === "" || preview.shown.status === Image.Error) && arcadeScreen.current !== null
                     && arcadeScreen.filterId !== "search"
            anchors.centerIn: parent
            width: parent.width - 4 * root.px
            spacing: Math.round(12 * arcadeScreen.v)
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: arcadeScreen.isGame ? "No Picture" : "Not Recognized"
                textFormat: Text.PlainText
                elide: Text.ElideRight
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
            Text {
                visible: arcadeScreen.isGame && arcadeScreen.pictureNote !== ""
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: arcadeScreen.pictureNote
                elide: Text.ElideRight
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: arcadeScreen.textSize
            }
        }

        // The credits bought, or Free Play, on a ground of their own so they
        // read over the picture.
        Rectangle {
            visible: arcadeScreen.showsCredits
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
                text: arcadeScreen.creditLine
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: arcadeScreen.textSize
            }
        }
    }

    // The box under the picture, framed as the picture is; on a room, the
    // whole column, from its top.
    Rectangle {
        objectName: "infoBox"
        visible: arcadeScreen.filters.length > 0
                 && !(arcadeScreen.portrait && arcadeScreen.filterId === "search")
        x: Math.round(arcadeScreen.boxX * arcadeScreen.u)
        y: Math.round(arcadeScreen.boxTop * arcadeScreen.v)
        width: Math.round(arcadeScreen.boxWidth * arcadeScreen.u)
        height: Math.round(arcadeScreen.boxBottom * arcadeScreen.v) - y
        color: arcadeScreen.ground
        border.width: arcadeScreen.frame
        border.color: root.primaryColor
        antialiasing: false
    }

    // The high scores, in the box: a line for each it has room for, kept
    // from game to game; for a game, not a room (arcadeScreen.gameBox),
    // whose box says what is said of the room.
    Item {
        visible: arcadeScreen.gameBox && arcadeScreen.scoreRows > 0
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
        // Without any: that the leaderboard is still being read, or couldn't
        // be; else that there are none yet, RetroAchievements' or this
        // machine's.
        Text {
            visible: arcadeScreen.scores.length === 0
            y: Math.round(arcadeScreen.scoreFirst * arcadeScreen.v)
            text: arcadeScreen.board.state === "loading" ? "Reading…"
                  : arcadeScreen.board.state === "failed" ? "Not Reachable" : "No Scores Yet"
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: arcadeScreen.textSize
        }
    }

    // A line of the box, across its inside in its letters, the label where
    // the high scores' are: MenuRow pads its line. A value too long for its
    // line (Data East Corporation) scrolls through it, as About's lines do:
    // the box's are read rather than chosen.
    component BoxLine: MenuRow {
        alwaysScroll: true
        x: Math.round(arcadeScreen.infoX * arcadeScreen.u) - pad
        width: Math.round(arcadeScreen.infoWidth * arcadeScreen.u) + 2 * pad
        height: arcadeScreen.textSize
        fontSize: arcadeScreen.textSize
    }

    // The game's details, and how long it has been played, against the box's
    // foot.
    Repeater {
        model: arcadeScreen.detailRows + 1
        BoxLine {
            required property int index
            readonly property var detail: index < arcadeScreen.detailRows ? arcadeScreen.detailList[index]
                                          : { label: "Play Time", value: moduleRoot.playTime(arcadeScreen.details.playTime) }
            visible: arcadeScreen.gameBox
            y: Math.round((arcadeScreen.playTimeTop - (arcadeScreen.detailRows - index) * arcadeScreen.scorePitch)
                          * arcadeScreen.v)
            label: detail.label
            value: detail.value
        }
    }

    // A room in the box, from its top: what the lobby, or its host on the
    // local network, says of it (arcadeScreen.roomLines), a line each, as
    // many as the box has room for (roomRows); the help line has the rest.
    Repeater {
        model: Math.min(arcadeScreen.roomRows, arcadeScreen.roomLines.length)
        BoxLine {
            required property int index
            objectName: "roomLine"
            readonly property var line: arcadeScreen.roomLines[index] || ({})
            y: Math.round((arcadeScreen.roomTop + index * arcadeScreen.scorePitch) * arcadeScreen.v)
            label: line.label || ""
            value: line.value || ""
            icons: line.icons || []
        }
    }

    // ---
    // Nothing to list
    // ---

    // In the list's place, why a filter lists nothing (arcadeScreen's
    // emptyLine), in its row: under Search's own, else the first.
    Text {
        readonly property int rowTop: !arcadeScreen.noneFound ? arcadeScreen.listTop
                                      : arcadeScreen.wheel ? arcadeScreen.wheelTops[1] || arcadeScreen.listTop
                                      : arcadeScreen.listTop + arcadeScreen.pitch
        visible: arcadeScreen.emptyLine !== ""
        x: Math.round((arcadeScreen.areaLeft + 16) * arcadeScreen.u)
        y: Math.round((rowTop + (arcadeScreen.rowHeight - arcadeScreen.nameLines) / 2) * arcadeScreen.v)
        width: Math.round((arcadeScreen.listWidth - 16) * arcadeScreen.u)
        text: arcadeScreen.emptyLine
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: arcadeScreen.nameSize
    }
}
