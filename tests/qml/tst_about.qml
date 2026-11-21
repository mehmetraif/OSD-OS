import QtQuick
import QtTest
import OSDOS.Testing

// About, views/About.qml, against stand-ins for AppCore's license files and
// ArcadeBackend's FinalBurn Neo license: Build first (System Information is
// System's own row now); OSD/OS's own lines, then the arcade's under
// its heading, which the cursor steps over; select on a line with a licence
// opens it, OSD/OS's under the GPL's notice, another's under its name,
// FinalBurn Neo's as it was installed or what to do without it; back closes
// it; a line without one opens nothing.
StandInRoot {
    id: root
    property string appBuild: "abc1234 2026-10-18"
    property int backs: 0
    property var navigations: []

    // The licence files as AppCore reads them, by path ("" for LICENSE).
    QtObject {
        id: appCore
        property var asked: []
        // OSD/OS's own, LICENSE's.
        property string gpl: "GNU GENERAL PUBLIC LICENSE Version 3"
        function licenseText(file) {
            asked = asked.concat([file])
            return file === "" ? gpl
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
            appCore.gpl = "GNU GENERAL PUBLIC LICENSE Version 3"
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

        // Build first: System Information is a row of System's own, and no
        // line here leads to its page.
        function test_noSystemInfo() {
            var page = open()
            compare(cursor(page), "Build")
            verify(page.rows.every(function(r) { return r.page !== "views/SystemInfo.qml" }))
            compare(hintOf(page).indexOf("[ENTER]:OPEN"), -1)
            keyClick(Qt.Key_Return)
            compare(root.navigations.length, 0)
        }

        // A line that leads to a page, or holds a licence, ends in a ►, and ►
        // opens it as select does; a line read and nothing more has none.
        function test_rightOpens() {
            var page = open()
            var list = childOf(childOf(childOf(page, "shownTop"), "topCut"), "highlightMoveDuration")
            verify(!list.itemAtIndex(0).opens)      // Build
            verify(!list.itemAtIndex(0).changes)
            keyClick(Qt.Key_Right)
            compare(root.navigations.length, 0)
            downTo(page, "License")
            verify(list.itemAtIndex(list.currentIndex).opens)
            keyClick(Qt.Key_Right)
            tryVerify(function() { return findText(page, "This program is free software") !== null }, 1000)
            keyClick(Qt.Key_Escape)
            compare(findText(page, "This program is free software"), null)
        }

        // Back to the page, on the line it was left on.
        function test_backToItsLine() {
            var page = open({ currentIndex: 4 })
            compare(cursor(page), page.rows[4].label)
        }

        // A gamepad pressed after the keyboard changes the select key's hint
        // in the help line and the hint bar, not the lines: the cursor stays
        // where it was, and select does what it did (not the first line's).
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
        // In a layout's frame the lines are in the pane, beside the column
        // the shell keeps on the screen, and the wordmark in its middle.
        function test_inTheFrame() {
            var page = open()
            verify(page.inFrame)
            var list = childOf(page, "shownTop")
            compare(list.x, root.sw * 0.115625)
            root.framed = true
            compare(list.x, root.sw * 0.209375)
            compare(list.width, root.sw * 0.675)
            root.framed = false
        }

        function test_lines() {
            var page = open()
            var labels = page.rows.map(function(r) { return (r.type === "section" ? "# " : "") + r.label })
            compare(labels.slice(0, 2), ["Build", "Developer"])
            var at = labels.indexOf("# Arcade")
            verify(at > labels.indexOf("Data"))
            compare(labels.slice(at + 1), ["MAME", "RetroArch", "FinalBurn Neo", "openhi2txt", "hi2txt", "RapidXML",
                                           "minizip, zlib", "LZMA SDK", "RetroAchievements", "Online Play",
                                           "progetto-SNAPS", "ArcadeDB"])
            downTo(page, "Data")
            keyClick(Qt.Key_Down)
            compare(cursor(page), "MAME")
            keyClick(Qt.Key_Up)
            compare(cursor(page), "Data")
        }

        // A licence's page longer than its box: its ▲ ▼ inside the box, over
        // the strips the text leaves them, clear of the hint bar under it,
        // in every Menu Layout.
        function test_licenseMarksInTheBox_data() {
            return root.menuLayouts.map(function(layout) { return { tag: layout, layout: layout } })
        }
        function test_licenseMarksInTheBox(data) {
            root.menuLayout = data.layout
            appCore.gpl = "GNU GENERAL PUBLIC LICENSE Version 3" + new Array(60).join("\nA line of the license's text")
            var page = open()
            downTo(page, "License")
            keyClick(Qt.Key_Return)
            tryVerify(function() { return findText(page, "This program is free software") !== null }, 1000)
            var find = function(item, test) {
                if (test(item))
                    return item
                for (var i = 0; i < item.children.length; i++) {
                    var found = find(item.children[i], test)
                    if (found)
                        return found
                }
                return null
            }
            // The page's marks: those of the text in its bordered box.
            var marks = find(page, function(i) { return i.moreBelow !== undefined && !!i.list && !!i.list.parent
                                                       && i.list.parent.border !== undefined })
            verify(marks !== null)
            var flick = marks.list
            var box = flick.parent
            var rectOf = function(item) {
                var p = item.mapToItem(root, 0, 0)
                return { top: p.y, bottom: p.y + item.height }
            }
            var check = function(name) {
                var mark = find(marks, function(i) { return i.name === name })
                verify(mark !== null && mark.visible, name)
                var m = rectOf(mark)
                var b = rectOf(box)
                verify(m.top >= b.top + root.px && m.bottom <= b.bottom - root.px,
                       name + " in the box: " + JSON.stringify(m) + " " + JSON.stringify(b))
                var t = rectOf(flick)
                verify(m.bottom <= t.top || m.top >= t.bottom, name + " clear of the text")
            }
            tryVerify(function() { return marks.moreBelow }, 1000, "longer than its box")
            check("down")
            flick.contentY = flick.contentHeight - flick.height
            verify(marks.moreAbove)
            check("up")
            root.menuLayout = "Standard"
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
