import QtQuick
import Components

// A game's details, from its options' Game Details: what it is, in a line
// under the title bar, and its achievements on RetroAchievements
// (ArcadeBackend::gameAchievements), read with the Web API key of Settings →
// Arcade → RetroAchievements Account and, for which of them were earned, its
// user: those earned, then those not, each with RetroAchievements' badge of
// it (achievementBadge(), downloaded as its row is shown), in colour once
// earned and grey till then, and its points. The one selected has its badge
// large at the right under the title bar, and the help line says what it
// asks for and when it was earned. Select plays the game (as from the list),
// and back goes back to where it was chosen.
FocusScope {
    id: detailsRoot

    property var navParams: ({})
    readonly property string game: navParams.game || ""
    readonly property string title: navParams.title || game

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    // Context properties read null while the module's Loader tears this view
    // down, hence the guards. Read again once a game played from here has
    // ended (with Start Info Screen off, the page stays while it plays): its
    // play time, and its achievements as it left them.
    property int revision: 0
    readonly property var details: (revision, arcadeBackend && game !== "" ? arcadeBackend.gameDetails(game) : ({}))
    // RetroAchievements' pictures of it downloaded, as on the Arcade screen.
    Component.onCompleted: {
        if (arcadeBackend && game !== "")
            arcadeBackend.wantPictures(game)
        firstRow()
    }
    // Its achievements, read again once RetroAchievements has answered, and
    // the badges' files, looked for again as each is downloaded.
    property int achievementsRevision: 0
    property int badgesRevision: 0
    readonly property var progress: (revision, achievementsRevision, arcadeBackend && game !== ""
                                                                     ? arcadeBackend.gameAchievements(game) : ({ state: "none" }))
    readonly property var achievements: progress.state === "ready" ? progress.achievements || [] : []
    // Whether they say which the account has earned: else none reads as
    // earned, and every badge is in colour.
    readonly property bool knowsEarned: !!progress.progress

    // The list's rows: a heading over those earned, then one over the rest.
    readonly property var rows: {
        var earned = []
        var rest = []
        for (var i = 0; i < achievements.length; ++i)
            (achievements[i].earned !== "" ? earned : rest).push(achievements[i])
        var made = []
        if (earned.length > 0)
            made = made.concat([{ type: "section", label: "Earned" }], earned)
        if (rest.length > 0)
            made = made.concat([{ type: "section", label: knowsEarned ? "Not Earned" : "Achievements" }], rest)
        return made
    }
    // The first row that isn't a heading, once there are rows: after the
    // list has taken them. Rows made again with the cursor on one (the
    // game played from here, its achievements read again) keep it there.
    // Not once the Loader has let the view go (out of its parent, deleted
    // when the event loop gets to it): a call deferred then would run on a
    // list already destroyed.
    onRowsChanged: {
        if (detailsRoot.parent)
            Qt.callLater(firstRow)
    }
    function firstRow() {
        if (!detailsRoot.parent)
            return
        var at = rows[list.currentIndex]
        if (at && at.type !== "section")
            return
        list.currentIndex = rows.length > 1 ? 1 : 0
    }

    // What the game is: its maker, year, players, system and play time.
    readonly property string facts: {
        var d = details
        var parts = []
        if (d.maker)
            parts.push(d.maker)
        if (d.year)
            parts.push(d.year)
        if (d.players > 0)
            parts.push(moduleRoot.players(d.players))
        if (d.system)
            parts.push(d.system)
        if (d.playTime > 0)
            parts.push("Played " + moduleRoot.playTime(d.playTime))
        return parts.join(" • ")
    }

    // How many were earned, and their points; or why there are none.
    readonly property string summary: {
        switch (progress.state) {
        case "ready":
            var earned = 0
            var points = 0
            var earnedPoints = 0
            for (var i = 0; i < achievements.length; ++i) {
                points += achievements[i].points
                if (achievements[i].earned !== "") {
                    ++earned
                    earnedPoints += achievements[i].points
                }
            }
            var pointsWord = points === 1 ? " Point" : " Points"
            return knowsEarned ? earned + " of " + achievements.length + " Earned • " + earnedPoints + " of " + points + pointsWord
                               : achievements.length + (achievements.length === 1 ? " Achievement • " : " Achievements • ")
                                 + points + pointsWord
        case "loading": return "Reading RetroAchievements…"
        case "nokey": return "No Web API key: Settings, Arcade, RetroAchievements Account"
        case "refused": return "RetroAchievements refused the Web API key"
        case "failed": return "RetroAchievements could not be reached"
        }
        return "RetroAchievements has no achievements for this game"
    }

    // An achievement's badge: in colour once earned (or with no user to say),
    // grey till then; looked for again as each is downloaded.
    function badgeOf(row) {
        return !row || row.type === "section" || !arcadeBackend ? ""
               : (badgesRevision, arcadeBackend.achievementBadge(game, row.id, row.badge,
                                                                  row.earned !== "" || !knowsEarned))
    }

    // What the one selected asks for, its points and when it was earned.
    function about(row) {
        if (!row || row.type === "section")
            return title
        var parts = [row.title]
        if (row.description)
            parts.push(row.description)
        parts.push(row.points + (row.points === 1 ? " Point" : " Points"))
        if (knowsEarned)
            parts.push(row.earned !== "" ? "Earned " + row.earned + (row.hardcore ? " in Hardcore" : "") : "Not Earned Yet")
        return parts.join("\n")
    }

    function play() {
        detailsRoot.navigateTo("Play.qml", { game: game, title: title }, {})
    }

    Connections {
        target: arcadeBackend
        ignoreUnknownSignals: true
        function onGameAchievementsReady(set) {
            if (set === detailsRoot.game)
                detailsRoot.achievementsRevision++
        }
        function onBadgesReady() { detailsRoot.badgesRevision++ }
        function onFinished() { detailsRoot.revision++ }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: detailsRoot.title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // The selected achievement's badge, large, at the right under the title
    // bar; framed while it is downloaded.
    Rectangle {
        id: selectedBadge
        readonly property var row: detailsRoot.rows[list.currentIndex]
        visible: !!row && row.type !== "section"
        x: root.sw * 0.875 - width //560
        y: root.sh * 0.2 //96
        width: Math.round(root.sh * 0.1083333) //52
        height: width
        color: "transparent"
        border.width: bigBadge.status === Image.Ready ? 0 : root.px
        border.color: root.primaryColor
        antialiasing: false
        Image {
            id: bigBadge
            anchors.fill: parent
            source: detailsRoot.badgeOf(selectedBadge.row)
            sourceSize.width: width
            sourceSize.height: height
            asynchronous: true
        }
    }

    // Beside the badge while there are achievements.
    Column {
        x: root.sw * 0.125 //80
        y: root.sh * 0.2041667 //98
        width: root.sw * 0.75 - (detailsRoot.rows.length > 0 ? selectedBadge.width + root.sw * 0.0125 : 0) //480
        spacing: root.sh * 0.0041667 //2

        Text {
            width: parent.width
            visible: text !== ""
            text: detailsRoot.facts
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0333333 //16
        }
        Text {
            width: parent.width
            text: detailsRoot.summary
            elide: Text.ElideRight
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0375 //18
        }
    }

    MenuList {
        id: list
        focus: true
        model: detailsRoot.rows
        // Under the two lines, with room for its ▲ between, down to where
        // every view's list ends.
        anchors.topMargin: root.sh * 0.3166667 //152
        height: root.sh * 0.4 + root.helpRoom + root.hintRoom //192

        delegate: Item {
            id: line
            required property var modelData
            required property int index
            readonly property bool heading: modelData.type === "section"
            readonly property bool earned: !heading && modelData.earned !== ""
            width: list.width
            height: root.sh * 0.0583333 //28

            MenuRow {
                visible: line.heading
                anchors.fill: parent
                heading: true
                label: line.modelData.label || ""
            }

            // Its badge, in colour once earned (or with no user to say),
            // grey till then; framed while it is downloaded.
            Rectangle {
                id: badgeFrame
                visible: !line.heading
                x: root.sw * 0.009375 //6
                anchors.verticalCenter: parent.verticalCenter
                width: Math.round(root.sh * 0.05) //24
                height: width
                color: "transparent"
                border.width: badge.status === Image.Ready ? 0 : root.px
                border.color: root.primaryColor
                antialiasing: false
                Image {
                    id: badge
                    anchors.fill: parent
                    source: detailsRoot.badgeOf(line.modelData)
                    sourceSize.width: width
                    sourceSize.height: height
                    asynchronous: true
                }
            }
            MenuRow {
                visible: !line.heading
                x: badgeFrame.x + badgeFrame.width + root.sw * 0.0125 //8
                width: parent.width - x
                height: parent.height
                label: line.heading ? "" : line.modelData.title
                value: line.heading ? "" : String(line.modelData.points)
                selected: list.currentIndex === line.index
                keepValue: true
            }
        }

        Keys.onReturnPressed: function(event) {
            if (!event.isAutoRepeat)
                detailsRoot.play()
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                if (!event.isAutoRepeat)
                    detailsRoot.goBack()
                event.accepted = true
            }
        }
    }

    HelpLine {
        text: detailsRoot.about(detailsRoot.rows[list.currentIndex])
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    HintBar {
        text: root.hints.back + ":BACK" + (detailsRoot.rows.length > 0 ? " " + root.hints.navigate + ":NAVIGATE" : "")
              + " " + root.hints.select + ":PLAY"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
