import QtQuick
import Components

// The High Scores module's tables as a horizontal tree (see TreeBrowser):
// Recently Played and All Games, a game's table as a line for each entry (or
// its tables, a folder each), and Not Readable for the files hi2txt can't
// read (HighScoresBackend). The tables are those MAME kept of the games
// played in the Arcade module. The line under the tree says what the entry
// under the cursor is: a game's best entry and its file, an entry's cells by
// column, why a file can't be read.
FocusScope {
    id: scoresRoot

    property var navParams: ({})
    // Context properties read null while the module's Loader tears this view
    // down, hence the guards.
    readonly property string problem: highScoresBackend ? highScoresBackend.problem : ""
    readonly property bool reading: highScoresBackend ? highScoresBackend.reading : false

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: tree.folderName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // No tables: what keeps them from being read, or how they come.
    Column {
        anchors.centerIn: parent
        width: root.sw * 0.75 //480
        spacing: root.sh * 0.0333333 //16
        visible: tree.rootEmpty
        Text {
            width: parent.width
            text: "No high scores yet"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: root.sh * 0.05 //24
        }
        Text {
            width: parent.width
            text: scoresRoot.problem !== "" ? scoresRoot.problem
                  : "Play a game in Arcade: its table shows here once MAME has kept it"
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            font.pixelSize: root.sh * 0.0333333 //16
        }
    }

    TreeBrowser {
        id: tree
        anchors.fill: parent
        focus: true
        visible: !rootEmpty
        rootPath: "top"
        // The info line's room, while it shows (Help Line on).
        reservedBottom: infoLine.visible && infoLine.shown ? areaBottom - infoLine.y : 0
        fetch: function(path, preview) {
            if (!highScoresBackend)
                return []
            var entries = highScoresBackend.requestEntries(path)
            return entries === undefined ? null : entries
        }
        onLeaveRequested: scoresRoot.goBack()
    }

    Connections {
        target: highScoresBackend
        function onEntriesReady(path) { tree.refresh(path, true) }
    }

    // What the entry under the cursor is; on a folder of games, where the
    // tables come from. With a problem nothing is read, so the empty screen
    // above says it.
    HelpLine {
        id: infoLine
        visible: !tree.rootEmpty
        readonly property var entry: tree.currentEntry
        text: entry && entry.detail ? entry.detail
              : scoresRoot.reading ? "Reading the high scores…"
              : "High score tables of the Arcade games"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.helpLineMargin
        anchors.leftMargin: root.sw * 0.125 //80
    }

    // Footer: select opens a folder; an entry's line is only read.
    HintBar {
        readonly property var entry: tree.currentEntry
        text: root.hints.back + ":BACK " + root.hints.arrows + ":NAVIGATE "
              + (entry && entry.isFolder ? root.hints.select + ":OPEN" : "")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
    }
}
