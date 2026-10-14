import QtQuick
import QtTest

// The High Scores module's view, modules/highscores/views/Scores.qml, against
// a stand-in for HighScoresBackend: a game's table opened from the tree, the
// line under it saying what the entry under the cursor holds, the tree listed
// once the files are read, and the screen with no tables yet, and why.
Rectangle {
    id: root
    color: surfaceColor
    width: 640
    height: 480
    // Main.qml's, as the views and their components read them.
    property real sw: width
    property real sh: height
    property real px: 2
    property string globalFont: "monospace"
    property color primaryColor: "white"
    property color secondaryColor: "white"
    property color tertiaryColor: "white"
    property color accentColor: "white"
    property color surfaceColor: "black"
    property var skin: ({})
    property bool hintBar: true
    property bool helpLine: true
    readonly property real hintRoom: 0
    readonly property real helpRoom: 0
    readonly property real helpLineMargin: sh * 0.1583333
    property rect contentBox: Qt.rect(sw * 0.115625, sh * 0.11875, sw * 0.76875, sh * 0.7770833)
    property rect osdWindow: contentBox
    property string osdBackground: "Full"
    property string osdFrame: "On"
    property var backgroundEffect: ({})
    property string backgroundShader: ""
    property bool effectsRest: true
    property real fxTime: 0
    property var selector: null
    property var hints: ({ back: "[ESC]", navigate: "[▲▼]", arrows: "[▲▼◄►]", change: "[◄►]",
                           browse: "[►]", select: "[ENTER]", play_pause: "[SPACE]" })
    function selectorShown(item, shown) {}
    function coverShown(item, shown) {}
    function skinIcon(url) { return url }

    // The module's router, as the view reads it.
    QtObject {
        id: moduleRoot
        property string moduleId: "com.osdos.highscores"
        property string moduleName: "High Scores"
        property string moduleIcon: ""
    }

    // HighScoresBackend as the view uses it: nothing for a folder not yet
    // read while reading, then entriesReady.
    QtObject {
        id: highScoresBackend
        property bool reading: false
        property string problem: ""
        property var entries: ({})
        signal entriesReady(string path)
        function refresh() {}
        function requestEntries(path) {
            if (reading && entries[path] === undefined)
                return undefined
            return entries[path] || []
        }
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }
    property int backs: 0
    Connections {
        target: loader.item
        ignoreUnknownSignals: true
        function onGoBack() { root.backs++ }
    }

    TestCase {
        name: "HighScoresViews"
        when: windowShown

        readonly property var tables: ({
            "top": [{ name: "Recently Played", path: "recent", isFolder: true },
                    { name: "All Games", path: "all", isFolder: true },
                    { name: "Not Readable", path: "unreadable", isFolder: true }],
            "recent": [],
            "all": [{ name: "Final Fight (World, set 1)", path: "game:ffight", isFolder: true, game: "ffight",
                      detail: "1 116,370 ACD\nffight.hi" }],
            "game:ffight": [{ name: "1  116,370  ACD", path: "row:ffight:0:0", isFolder: false, kind: "row",
                              detail: "RANK 1\nSCORE 116,370\nNAME ACD" },
                            { name: "2   89,760   .Z", path: "row:ffight:0:1", isFolder: false, kind: "row",
                              detail: "RANK 2\nSCORE 89,760\nNAME .Z" },
                            { name: "3   20,000  <B>", path: "row:ffight:0:2", isFolder: false, kind: "row",
                              detail: "RANK 3\nSCORE 20,000\nNAME <B>" }],
            "unreadable": [{ name: "zzzunknown", path: "unreadable:zzzunknown", isFolder: false, kind: "unreadable",
                             detail: "hi2txt has no description of this game's high scores yet\nzzzunknown.hi" }]
        })

        function init() {
            highScoresBackend.reading = false
            highScoresBackend.problem = ""
            highScoresBackend.entries = tables
            root.backs = 0
        }

        function cleanup() {
            loader.source = ""
        }

        function open() {
            loader.setSource(Qt.resolvedUrl("../../modules/highscores/views/Scores.qml"), { navParams: {} })
            verify(loader.item !== null, "loaded Scores.qml")
            loader.item.forceActiveFocus()
            return loader.item
        }

        // Every visible Text item that has this in it.
        function findTexts(item, text, found) {
            found = found || []
            if (item.textFormat !== undefined && String(item.text).indexOf(text) >= 0 && item.visible)
                found.push(item)
            for (var i = 0; i < item.children.length; i++)
                findTexts(item.children[i], text, found)
            return found
        }

        function findText(item, text) {
            if (item.text !== undefined && String(item.text).indexOf(text) >= 0 && item.visible)
                return item
            for (var i = 0; i < item.children.length; i++) {
                var found = findText(item.children[i], text)
                if (found)
                    return found
            }
            return null
        }

        // A game's table from All Games: the game's best entry and file, then
        // each entry's cells by column; back at the top leaves the module.
        function test_table() {
            var view = open()
            verify(findText(view, "[ENTER]:OPEN") !== null)
            keyClick(Qt.Key_Down)   // All Games
            keyClick(Qt.Key_Right)  // into it, on Final Fight
            tryVerify(function() { return findText(view, "ffight.hi") !== null })
            keyClick(Qt.Key_Right)  // its table
            tryVerify(function() { return findText(view, "RANK 1") !== null })
            verify(findText(view, "SCORE 116,370") !== null)
            // An entry is only read: nothing to open.
            compare(findText(view, ":OPEN"), null)
            keyClick(Qt.Key_Down)
            tryVerify(function() { return findText(view, "NAME .Z") !== null })
            // A name such as <B> is shown as it is, in the tree and under it.
            keyClick(Qt.Key_Down)
            tryVerify(function() { return findText(view, "NAME <B>") !== null })
            var named = findTexts(view, "<B>")
            verify(named.length >= 2, "the line and the line under the tree")
            for (var i = 0; i < named.length; i++)
                compare(named[i].textFormat, Text.PlainText)
            keyClick(Qt.Key_Escape)   // out of the table
            keyClick(Qt.Key_Escape)   // out of All Games
            compare(root.backs, 0)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }

        // A file that can't be read says why.
        function test_unreadable() {
            var view = open()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)   // Not Readable
            keyClick(Qt.Key_Right)
            tryVerify(function() { return findText(view, "hi2txt has no description") !== null })
        }

        // While the files are first read: then the tree, as entriesReady says.
        function test_reading() {
            highScoresBackend.reading = true
            highScoresBackend.entries = {}
            var view = open()
            verify(findText(view, "Reading the high scores") !== null)
            compare(findText(view, "No high scores yet"), null)
            compare(findText(view, "All Games"), null)
            highScoresBackend.entries = tables
            highScoresBackend.reading = false
            highScoresBackend.entriesReady("top")
            tryVerify(function() { return findText(view, "All Games") !== null })
        }

        // No tables: how they come, or what keeps them from being read.
        function test_empty() {
            highScoresBackend.entries = { "top": [] }
            var view = open()
            tryVerify(function() { return findText(view, "No high scores yet") !== null })
            verify(findText(view, "Play a game in Arcade") !== null)
            highScoresBackend.problem = "This build of OSD/OS reads no high score tables: it was built without minizip"
            verify(findText(view, "built without minizip") !== null)
            compare(findText(view, "Play a game in Arcade"), null)
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }
    }
}
