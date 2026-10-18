import QtQuick
import QtTest

// About, views/About.qml, against stand-ins for AppCore's licence files and
// ArcadeBackend's FinalBurn Neo licence: OSD/OS's own lines, then the
// arcade's under its heading, which the cursor steps over; select on a line
// with a licence opens it, OSD/OS's under the GPL's notice, another's under
// its name, FinalBurn Neo's as it was installed or what to do without it;
// back closes it; a line without one opens nothing.
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
    property string appVersion: "1.0.0"
    property string appBuild: "abc1234 2026-10-18"
    property int backs: 0

    // The licence files as AppCore reads them, by path ("" for LICENSE).
    QtObject {
        id: appCore
        property var asked: []
        function licenseText(file) {
            asked = asked.concat([file])
            return file === "" ? "GNU GENERAL PUBLIC LICENSE Version 3"
                 : file === "assets/licenses/GPL-2.0.txt" ? "GNU GENERAL PUBLIC LICENSE Version 2"
                 : file === "assets/licenses/zlib.txt" ? "zlib text" : ""
        }
    }
    QtObject {
        id: arcadeBackend
        property string license: "You may freely use, modify, and distribute FB Neo."
        function fbneoLicenseText() { return license }
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }
    Connections {
        target: loader.item
        ignoreUnknownSignals: true
        function onGoBack() { root.backs++ }
    }

    TestCase {
        name: "About"
        when: windowShown

        function init() {
            root.backs = 0
            appCore.asked = []
            arcadeBackend.license = "You may freely use, modify, and distribute FB Neo."
        }
        function cleanup() {
            loader.source = ""
        }

        function open() {
            loader.setSource(Qt.resolvedUrl("../../views/About.qml"))
            verify(loader.item !== null, "loaded About.qml")
            loader.item.forceActiveFocus()
            return loader.item
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
        // The line the cursor is on, by its label.
        function cursor(page) {
            for (var i = 0; i < page.children.length; i++) {
                var list = page.children[i]
                if (list.currentIndex !== undefined && list.model !== undefined)
                    return page.rows[list.currentIndex].label
            }
            return ""
        }
        // The cursor moved down to the line with this label.
        function downTo(page, label) {
            for (var n = 0; n < page.rows.length && cursor(page) !== label; n++)
                keyClick(Qt.Key_Down)
            compare(cursor(page), label)
        }

        // The arcade's lines under their heading, after OSD/OS's own; the
        // cursor steps over the heading.
        function test_lines() {
            var page = open()
            var labels = page.rows.map(function(r) { return (r.type === "section" ? "# " : "") + r.label })
            var at = labels.indexOf("# Arcade")
            verify(at > labels.indexOf("Data"))
            compare(labels.slice(at + 1), ["MAME", "RetroArch", "FinalBurn Neo", "openhi2txt", "hi2txt", "RapidXML",
                                           "minizip, zlib", "RetroAchievements", "Online Play", "Genres, Pictures"])
            downTo(page, "Data")
            keyClick(Qt.Key_Down)
            compare(cursor(page), "MAME")
            keyClick(Qt.Key_Up)
            compare(cursor(page), "Data")
        }

        // OSD/OS's licence under the GPL's notice; MAME's, the GPL v2's text
        // under its name; FinalBurn Neo's from the core, or how to get it;
        // a line without a licence opens nothing, and back leaves.
        function test_licenses() {
            var page = open()
            downTo(page, "License")
            keyClick(Qt.Key_Return)
            tryVerify(function() { return findText(page, "This program is free software") !== null }, 1000)
            verify(findText(page, "Version 3") !== null)
            keyClick(Qt.Key_Escape)
            compare(findText(page, "This program is free software"), null)
            compare(root.backs, 0)

            downTo(page, "MAME")
            keyClick(Qt.Key_Return)
            tryVerify(function() { return findText(page, "Version 2") !== null }, 1000)
            verify(findText(page, "GNU General Public License, version 2 or later") !== null)
            verify(findText(page, "MAME License") !== null)
            compare(appCore.asked[appCore.asked.length - 1], "assets/licenses/GPL-2.0.txt")
            keyClick(Qt.Key_Escape)

            downTo(page, "FinalBurn Neo")
            keyClick(Qt.Key_Return)
            tryVerify(function() { return findText(page, "freely use, modify") !== null }, 1000)
            keyClick(Qt.Key_Escape)
            arcadeBackend.license = ""
            keyClick(Qt.Key_Return)
            tryVerify(function() { return findText(page, "FinalBurn Neo isn't installed") !== null }, 1000)
            keyClick(Qt.Key_Escape)

            downTo(page, "RapidXML")
            keyClick(Qt.Key_Return)
            tryVerify(function() { return findText(page, "missing from this build") !== null }, 1000)
            keyClick(Qt.Key_Escape)

            downTo(page, "RetroAchievements")
            keyClick(Qt.Key_Return)
            compare(findText(page, "RetroAchievements License"), null)
            compare(cursor(page), "RetroAchievements")
            keyClick(Qt.Key_Escape)
            compare(root.backs, 1)
        }
    }
}
