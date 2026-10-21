import QtQuick
import QtTest

// About, views/About.qml, against stand-ins for AppCore's licence files,
// ArcadeBackend's FinalBurn Neo licence and SystemInfo's device name: System
// Information first, the device's name its value, select opening its page and
// back from it on its line again; OSD/OS's own lines, then the arcade's under
// its heading, which the cursor steps over; select on a line with a licence
// opens it, OSD/OS's under the GPL's notice, another's under its name,
// FinalBurn Neo's as it was installed or what to do without it; back closes
// it; a line without one opens nothing.
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
    property var statusIcons: []
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
    readonly property var keyboardHints: ({ back: "[ESC]", navigate: "[▲▼]", arrows: "[▲▼◄►]", change: "[◄►]",
                                            browse: "[►]", select: "[ENTER]", play_pause: "[SPACE]" })
    property var hints: keyboardHints
    function selectorShown(item, shown) {}
    function coverShown(item, shown) {}
    function skinIcon(url) { return url }
    property string appVersion: "1.0.0"
    property string appBuild: "abc1234 2026-10-18"
    property int backs: 0
    property var navigations: []

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
    QtObject {
        id: systemInfo
        property string deviceName: "den-tv"
        property int refreshes: 0
        function refresh() { refreshes++ }
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
        function onNavigateTo(path, params, listState) {
            root.navigations = root.navigations.concat([{ path: path, params: params, listState: listState }])
        }
    }

    TestCase {
        name: "About"
        when: windowShown

        function init() {
            root.backs = 0
            root.navigations = []
            appCore.asked = []
            arcadeBackend.license = "You may freely use, modify, and distribute FB Neo."
            systemInfo.deviceName = "den-tv"
            systemInfo.refreshes = 0
            root.hints = root.keyboardHints
        }
        function cleanup() {
            loader.source = ""
        }

        // About, as Settings opens it, or with the list state the router
        // gives a view it goes back to.
        function open(listState) {
            loader.setSource(Qt.resolvedUrl("../../views/About.qml"), { navListState: listState || {} })
            verify(loader.item !== null, "loaded About.qml")
            loader.item.forceActiveFocus()
            return loader.item
        }
        function childOf(page, has) {
            for (var i = 0; i < page.children.length; i++) {
                if (page.children[i][has] !== undefined)
                    return page.children[i]
            }
            return null
        }
        // A line's value as MenuList draws it.
        function shown(page, index) {
            var list = childOf(childOf(childOf(page, "shownTop"), "topCut"), "highlightMoveDuration")
            return list.itemAtIndex(index).value
        }
        function hintOf(page) { return childOf(page, "largest").text }
        function helpOf(page) { return childOf(page, "always").text }
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

        // System Information first, the device's name its value; its page
        // opens on select, and nothing else does.
        function test_systemInfo() {
            var page = open()
            compare(systemInfo.refreshes, 1)
            compare(cursor(page), "System Information")
            compare(shown(page, 0), "den-tv")
            verify(hintOf(page).indexOf("[ENTER]:OPEN") >= 0)
            verify(helpOf(page).indexOf("tells it apart from others on the network") >= 0)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
            compare(root.navigations[0].path, "views/SystemInfo.qml")
            compare(root.navigations[0].listState.currentIndex, 0)
            compare(findText(page, "This program is free software"), null)
            keyClick(Qt.Key_Down)
            compare(cursor(page), "Build")
            compare(hintOf(page).indexOf("[ENTER]:OPEN"), -1)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 1)
        }

        // Back from System Information, on its line again, wherever the list
        // was; a new name on its line in place, the cursor where it is.
        function test_backFromSystemInfo() {
            var page = open({ currentIndex: 0 })
            compare(cursor(page), "System Information")
            cleanup()
            page = open({ currentIndex: 5 })
            compare(cursor(page), page.rows[5].label)
            systemInfo.deviceName = "attic-tv"
            compare(shown(page, 0), "attic-tv")
            compare(cursor(page), page.rows[5].label)
        }

        // A gamepad pressed after the keyboard changes the select key's hint
        // in the help line and the hint bar, not the lines: the cursor stays
        // where it was, and select does what it did (not System Information,
        // the first line).
        function test_hintsChange() {
            var page = open()
            downTo(page, "License")
            root.hints = Object.assign({}, root.keyboardHints, { select: "[A]" })
            compare(cursor(page), "License")
            verify(helpOf(page).indexOf("[A] The notice and the license's text") >= 0)
            verify(hintOf(page).indexOf("[A]:LICENSE") >= 0)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 0)
            tryVerify(function() { return findText(page, "This program is free software") !== null }, 1000)
            keyClick(Qt.Key_Escape)
        }

        // The arcade's lines under their heading, after OSD/OS's own; the
        // cursor steps over the heading.
        function test_lines() {
            var page = open()
            var labels = page.rows.map(function(r) { return (r.type === "section" ? "# " : "") + r.label })
            compare(labels.slice(0, 2), ["System Information", "Build"])
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
