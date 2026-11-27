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
                                           "minizip, zlib", "LZMA SDK", "Switchres", "RetroAchievements", "Online Play",
                                           "progetto-SNAPS", "ArcadeDB"])
            downTo(page, "Data")
            keyClick(Qt.Key_Down)
            compare(cursor(page), "MAME")
            keyClick(Qt.Key_Up)
            compare(cursor(page), "Data")
        }

        // A licence's page longer than its box, in every Menu Layout: at
        // every scroll position only whole lines show, none cut by the box,
        // a strip, or a mark; the ▲ and the ▼ are inside the box, clear of
        // the text and of the hint bar under it, and their strips are there
        // only while they are: at the top the text starts at the box's top.
        // Long wrapped lines among them, so the lines aren't the box's.
        function test_licenseShowsWholeLines_data() {
            return root.menuLayouts.map(function(layout) { return { tag: layout, layout: layout } })
        }
        function test_licenseShowsWholeLines(data) {
            root.menuLayout = data.layout
            appCore.gpl = "GNU GENERAL PUBLIC LICENSE Version 3"
                          + new Array(40).join("\nA line of the license's text, long enough that it wraps once or twice in the box")
            var page = open()
            downTo(page, "License")
            keyClick(Qt.Key_Return)
            tryVerify(function() { return findText(page, "This program is free software") !== null }, 1000)
            var box = findChild(page, "licenseBox")
            var window = findChild(page, "licenseWindow")
            var flick = findChild(page, "licenseFlick")
            var marks = findChild(page, "licenseMarks")
            verify(box && window && flick && marks)
            var room = window.parent
            // The lines where the page says they are: each text as tall as
            // its lines at their pitch.
            var texts = []
            var findTexts = function(item) {
                if (item.pitch !== undefined && item.lineCount !== undefined)
                    texts.push(item)
                for (var i = 0; i < item.children.length; i++)
                    findTexts(item.children[i])
            }
            findTexts(flick.contentItem)
            compare(texts.length, 2)
            for (var t = 0; t < texts.length; t++)
                fuzzyCompare(texts[t].height, texts[t].lineCount * texts[t].pitch, 1)
            var markOf = function(name) {
                for (var i = 0; i < marks.children.length; i++) {
                    if (marks.children[i].name === name)
                        return marks.children[i]
                }
                return null
            }
            var yOf = function(item) { return item.mapToItem(root, 0, 0).y }
            var check = function(where) {
                var top = yOf(window)
                var bottom = top + window.height
                var lines = flick.lines()
                for (var i = 0; i < lines.length; i++) {
                    var lt = yOf(flick.contentItem) + lines[i].top
                    var lb = lt + (lines[i].bottom - lines[i].top)
                    var inside = lt >= top - 0.5 && lb <= bottom + 0.5
                    var outside = lb <= top + 0.5 || lt >= bottom - 0.5
                    verify(inside || outside, where + ": line " + i + " cut")
                }
                verify(window.height > 0, where)
                var b = { top: yOf(box) + root.px, bottom: yOf(box) + box.height - root.px }
                for (var name of ["up", "down"]) {
                    var mark = markOf(name)
                    if (!mark.visible)
                        continue
                    var m = { top: yOf(mark), bottom: yOf(mark) + mark.height }
                    verify(m.top >= b.top && m.bottom <= b.bottom, where + ": " + name + " in the box")
                    verify(m.bottom <= top + 0.5 || m.top >= bottom - 0.5, where + ": " + name + " clear of the text")
                }
                // No strip at an end with no mark.
                if (!markOf("up").visible)
                    fuzzyCompare(top, yOf(room), 0.5)
            }
            tryVerify(function() { return marks.moreBelow }, 1000, "longer than its box")
            check("the top")
            compare(markOf("up").visible, false)
            var steps = 0
            while (marks.moreBelow && steps < 40) {
                keyClick(Qt.Key_Down)
                check("down " + ++steps)
            }
            verify(!marks.moreBelow && marks.moreAbove, "to its end")
            while (marks.moreAbove && steps < 80) {
                keyClick(Qt.Key_Up)
                check("up " + ++steps)
            }
            compare(flick.contentY, 0)
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
