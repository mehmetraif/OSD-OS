import QtQuick
import QtTest

// A module's settings page, views/ModuleSettings.qml, against a stand-in for
// AppCore: rows offered only while another row has a value (manifest
// "shown_with", as the Arcade module's rows for its Arcade screen are), an
// unset one by its default, coming and going as that row changes with the
// cursor kept on it, and their values kept while they are hidden; a dynamic
// row's choices asked for as it comes; a toggle's value as ON or OFF, and a
// row whose own row is hidden hidden too; a slider and its tape bar; a value
// that couldn't be saved left as it was. A multiselect_submenu row's page,
// views/MultiSelectSettings.qml, with its headings. A group's page, with
// another module's rows under its name, saved in that module.
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
    property var selector: null
    property var hints: ({ back: "[ESC]", navigate: "[▲▼]", arrows: "[▲▼◄►]", change: "[◄►]",
                           browse: "[►]", select: "[ENTER]", play_pause: "[SPACE]" })
    function selectorShown(item, shown) {}
    function skinIcon(url) { return url }

    // AppCore as the page uses it: the module's schema and saved values, and
    // another module's, of the same group.
    QtObject {
        id: appCore
        property var values: ({})
        property var actions: []
        property var schema: []
        property var otherValues: ({})
        property var otherSchema: []
        // config.json can't be written (a full card): nothing is saved.
        property bool saveFails: false
        signal dynamicOptionsReady(string mid, string key, var items)
        signal moduleAuthStateChanged(string mid)
        function get_settings() { return { modules: { "com.osdos.test": values, "com.osdos.other": otherValues } } }
        function get_installed_modules() { return [{ id: "com.osdos.test", name: "Test" }, { id: "com.osdos.other", name: "Other" }] }
        function get_module_auth_state(id) { return "" }
        function invoke_module_action(id, slot) { actions = actions.concat([slot]) }
        function save_setting(module, key, value) {
            if (saveFails)
                return false
            var v = Object.assign({}, module === "com.osdos.other" ? otherValues : values)
            v[key] = value
            if (module === "com.osdos.other")
                otherValues = v
            else
                values = v
            return true
        }
        function get_module_settings_schema(id) { return id === "com.osdos.other" ? otherSchema : schema }
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }

    TestCase {
        name: "ModuleSettings"
        when: windowShown

        // The Arcade module's rows, in short.
        readonly property var arcadeRows: [
            { key: "enabled", label: "Enabled", type: "toggle", default: "OFF" },
            { key: "screen", label: "Screen", type: "list_single", options: ["Original", "Arcade"],
              default: "Original" },
            { key: "start_filter", label: "Start Filter", type: "list_single", options_source: "dynamic",
              options_slot: "startFilterOptions", shown_with: { key: "screen", value: "Arcade" } },
            { key: "look", label: "Look", type: "submenu", shown_with: { key: "screen", value: "Arcade" },
              settings: [{ key: "look_theme", label: "Theme", type: "list_single", options: ["Default"] }] },
            { key: "video", label: "Video", type: "list_single", options: ["Auto", "OpenGL"], default: "Auto" }
        ]

        function init() {
            appCore.values = {}
            appCore.actions = []
            appCore.schema = arcadeRows
            appCore.otherValues = {}
            appCore.otherSchema = []
            appCore.saveFails = false
        }
        function cleanup() {
            loader.source = ""
        }

        // With `also`, a group's page, those modules' rows after the test's.
        function open(also) {
            loader.setSource(Qt.resolvedUrl("../../views/ModuleSettings.qml"),
                             { navParams: { moduleId: "com.osdos.test", alsoModules: also || [] } })
            verify(loader.item !== null, "loaded ModuleSettings.qml")
            loader.item.forceActiveFocus()
            return loader.item
        }
        function keys(page) {
            return page.schemaItems.map(function(item) { return item.key })
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
        // The row the cursor is on, by its key, and where.
        function cursor(page) {
            var at = cursorIndex(page)
            return at >= 0 ? page.schemaItems[at].key : ""
        }
        function cursorIndex(page) {
            for (var i = 0; i < page.children.length; i++) {
                var list = page.children[i]
                if (list.currentIndex !== undefined && list.model !== undefined)
                    return list.currentIndex
            }
            return -1
        }

        // A group's page: the other module's rows after the test's, under its
        // name, which the cursor steps over; their values their own, shown
        // and saved in that module though a key is the same, and its own
        // rows following them.
        function test_alsoModules() {
            appCore.otherSchema = [
                { key: "enabled", label: "Enabled", type: "toggle", default: "OFF" },
                { key: "mode", label: "Mode", type: "list_single", options: ["A", "B"], default: "A",
                  shown_with: { key: "enabled", value: "ON" } }
            ]
            appCore.values = { enabled: true }
            var page = open(["com.osdos.other"])
            compare(keys(page), ["enabled", "screen", "video", undefined, "enabled"])
            compare(page.schemaItems[3].type, "section")
            compare(page.schemaItems[3].label, "Other")
            verify(findText(page, "Other") !== null)
            compare(page.currentDisplayValue(page.schemaItems[0]), "ON")
            compare(page.currentDisplayValue(page.schemaItems[4]), "OFF")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(cursorIndex(page), 4)
            keyClick(Qt.Key_Right)
            compare(appCore.otherValues.enabled, true)
            compare(appCore.values.enabled, true)
            compare(page.currentDisplayValue(page.schemaItems[4]), "ON")
            compare(keys(page), ["enabled", "screen", "video", undefined, "enabled", "mode"])
            compare(cursorIndex(page), 4)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(appCore.otherValues.mode, "B")
            compare(appCore.values.mode, undefined)
            // Up from the other's first row steps over the heading too.
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(cursor(page), "video")
            // A page of one module has no other rows.
            loader.source = ""
            page = open()
            compare(keys(page), ["enabled", "screen", "video"])
        }

        // Unset, Screen is its default, Original: the Arcade screen's rows
        // aren't offered, nor their choices asked for. Arcade offers them,
        // the cursor staying on Screen, and asks for Start Filter's; Original
        // takes them away, what was set in them kept.
        function test_shownWith() {
            var page = open()
            compare(keys(page), ["enabled", "screen", "video"])
            compare(appCore.actions, [])
            keyClick(Qt.Key_Down)
            compare(cursor(page), "screen")
            keyClick(Qt.Key_Right)
            compare(appCore.values.screen, "Arcade")
            compare(keys(page), ["enabled", "screen", "start_filter", "look", "video"])
            compare(cursor(page), "screen")
            compare(appCore.actions, ["startFilterOptions"])
            appCore.dynamicOptionsReady("com.osdos.test", "start_filter",
                                        [{ id: "all", label: "All Games" }, { id: "favorites", label: "Favorites" }])
            compare(page.currentDisplayValue(page.schemaItems[2]), "All Games")
            keyClick(Qt.Key_Down)
            compare(cursor(page), "start_filter")
            keyClick(Qt.Key_Right)
            compare(appCore.values.start_filter, "favorites")
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Left)
            compare(appCore.values.screen, "Original")
            compare(keys(page), ["enabled", "screen", "video"])
            compare(cursor(page), "screen")
            compare(appCore.values.start_filter, "favorites")
            // Saved as Arcade before the page opened: offered from the start,
            // its choices asked for with the rest.
            loader.source = ""
            appCore.values = { screen: "Arcade" }
            appCore.actions = []
            page = open()
            compare(keys(page), ["enabled", "screen", "start_filter", "look", "video"])
            compare(appCore.actions, ["startFilterOptions"])
        }

        // A value that couldn't be saved (a full card): the row stays on what
        // is saved, its rows with it; saved, it changes.
        function test_unsaved() {
            var page = open()
            keyClick(Qt.Key_Down)
            appCore.saveFails = true
            keyClick(Qt.Key_Right)
            compare(appCore.values.screen, undefined)
            compare(page.currentDisplayValue(page.schemaItems[1]), "Original")
            compare(keys(page), ["enabled", "screen", "video"])
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Right)
            compare(page.currentDisplayValue(page.schemaItems[0]), "OFF")
            appCore.saveFails = false
            keyClick(Qt.Key_Right)
            compare(page.currentDisplayValue(page.schemaItems[0]), "ON")
            compare(appCore.values.enabled, true)
        }

        // A toggle's rows follow it as ON or OFF, unset as its default and
        // saved as true or false; a row whose own row is hidden is hidden
        // too, whatever that row holds.
        function test_shownWithToggle() {
            appCore.schema = [
                { key: "music", label: "Music", type: "toggle", default: "ON" },
                { key: "volume", label: "Volume", type: "list_single", options: ["Low", "High"], default: "High",
                  shown_with: { key: "music", value: "ON" } },
                { key: "loud", label: "Loud", type: "toggle", default: "OFF",
                  shown_with: { key: "volume", value: "High" } }
            ]
            var page = open()
            compare(keys(page), ["music", "volume", "loud"])
            keyClick(Qt.Key_Right)
            compare(appCore.values.music, false)
            compare(keys(page), ["music"])
            keyClick(Qt.Key_Right)
            compare(appCore.values.music, true)
            compare(keys(page), ["music", "volume", "loud"])
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(appCore.values.volume, "Low")
            compare(keys(page), ["music", "volume"])
            compare(cursor(page), "volume")
        }

        // A slider: its label, and the tape bar between its two ends under
        // it, at its value, unset its default; ◄ ► a step toward either end,
        // saved, and no further than an end. ▼ goes past its bar's lines.
        function test_slider() {
            appCore.schema = [
                { key: "enabled", label: "Enabled", type: "toggle", default: "OFF" },
                { key: "ground", label: "Info Background", type: "slider", default: 100, step: 10,
                  start_text: "TRANSPARENT", end_text: "SOLID" },
                { key: "video", label: "Video", type: "list_single", options: ["Auto", "OpenGL"], default: "Auto" }
            ]
            var page = open()
            compare(page.valueOf(page.schemaItems[1]), 100)
            verify(findText(page, "TRANSPARENT") !== null)
            verify(findText(page, "SOLID") !== null)
            keyClick(Qt.Key_Down)
            compare(cursor(page), "ground")
            keyClick(Qt.Key_Right)
            compare(appCore.values.ground, undefined)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            compare(appCore.values.ground, 80)
            compare(page.valueOf(page.schemaItems[1]), 80)
            for (var i = 0; i < 10; ++i)
                keyClick(Qt.Key_Left)
            compare(appCore.values.ground, 0)
            keyClick(Qt.Key_Down)
            compare(cursor(page), "video")
        }

        // A slider the list's foot would cut as the page opens is left out
        // of it until ▼ brings it in whole.
        function test_sliderAtEdge() {
            var rows = []
            for (var i = 0; i < 6; ++i)
                rows.push({ key: "row" + i, label: "Row " + i, type: "toggle", default: "ON" })
            rows.push({ key: "ground", label: "Info Background", type: "slider", default: 100, step: 10,
                        start_text: "TRANSPARENT", end_text: "SOLID" })
            appCore.schema = rows
            var page = open()
            var list = null
            for (i = 0; i < page.children.length; i++) {
                if (page.children[i].shownHeight !== undefined)
                    list = page.children[i]
            }
            verify(list !== null)
            var line = root.sh * 0.0583333
            // Eight lines' room: the six rows, not the slider's three lines
            // (in whole pixels).
            tryVerify(function() { return Math.abs(list.shownHeight - 6 * line) < 1 })
            for (i = 0; i < 6; ++i)
                keyClick(Qt.Key_Down)
            compare(cursor(page), "ground")
            tryCompare(list, "shownHeight", list.height)
            compare(list.shownTop, 0)
        }

        // A slider the list's top would cut at its end, where no scroll can
        // carry on past it, is left out of it too.
        function test_sliderAtEnd() {
            var rows = []
            for (var i = 0; i < 12; ++i)
                rows.push({ key: "row" + i, label: "Row " + i, type: "toggle", default: "ON" })
            rows.splice(6, 0, { key: "ground", label: "Info Background", type: "slider", default: 100, step: 10,
                                start_text: "TRANSPARENT", end_text: "SOLID" })
            appCore.schema = rows
            var page = open()
            var list = null
            for (i = 0; i < page.children.length; i++) {
                if (page.children[i].shownTop !== undefined)
                    list = page.children[i]
            }
            verify(list !== null)
            var line = root.sh * 0.0583333
            // ▲ round to the last row: the list at its end, lines 8 to 15 of
            // 15 in view, the slider's last two of its three lines among them.
            keyClick(Qt.Key_Up)
            compare(cursor(page), "row11")
            tryVerify(function() { return Math.abs(list.shownTop - 2 * line) < 1 })
            verify(Math.abs(list.shownHeight - (list.height - 2 * line)) < 1)
            // Up to it: wholly in view.
            for (i = 0; i < 6; ++i)
                keyClick(Qt.Key_Up)
            compare(cursor(page), "ground")
            tryCompare(list, "shownTop", 0)
            compare(list.shownHeight, list.height)
        }

        // A multiselect_submenu row's page: its choices asked for as it
        // opens, each ON unless saved OFF or its default says OFF, under
        // their headings. ◄ ► or select flips a choice; on a heading, its
        // whole group: all on, unless they all are. The row is saved whole
        // each time, whatever its ids hold. The help line says so on a
        // heading.
        function test_multiSelect() {
            appCore.schema = [{ key: "menu_filters", label: "Menu Filters", type: "multiselect_submenu",
                                options_source: "dynamic", options_slot: "menuFilterOptions" }]
            appCore.values = { menu_filters: { "genres/Shmup": false } }
            loader.setSource(Qt.resolvedUrl("../../views/MultiSelectSettings.qml"),
                             { navParams: { moduleId: "com.osdos.test", settingKey: "menu_filters",
                                            settingLabel: "Menu Filters" } })
            var page = loader.item
            verify(page !== null, "loaded MultiSelectSettings.qml")
            page.forceActiveFocus()
            compare(appCore.actions, ["menuFilterOptions"])
            appCore.dynamicOptionsReady("com.osdos.test", "menu_filters", [
                { label: "Genres", heading: true },
                { id: "genres/Fighting", label: "Fighting", default: true },
                { id: "genres/Shmup", label: "Shmup", default: true },
                { label: "Publishers", heading: true },
                { id: "makers/Capcom", label: "Capcom", default: false },
                { id: "makers/U.S. Games", label: "U.S. Games", default: false }
            ])
            compare(page.enabledMap, { "genres/Fighting": true, "genres/Shmup": false,
                                       "makers/Capcom": false, "makers/U.S. Games": false })
            verify(findText(page, "Turns all of the choices") !== null)

            // Genres: one of two on, so both on; then both off.
            keyClick(Qt.Key_Return)
            compare(appCore.values.menu_filters, { "genres/Fighting": true, "genres/Shmup": true })
            keyClick(Qt.Key_Right)
            compare(appCore.values.menu_filters, { "genres/Fighting": false, "genres/Shmup": false })
            compare(page.enabledMap["genres/Shmup"], false)

            // A choice, then one with dots in its id.
            keyClick(Qt.Key_Down)
            verify(findText(page, "Turns all of the choices") === null)
            keyClick(Qt.Key_Left)
            compare(appCore.values.menu_filters["genres/Fighting"], true)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(appCore.values.menu_filters["makers/U.S. Games"], true)
            compare(page.enabledMap["makers/U.S. Games"], true)
            compare(page.enabledMap["makers/Capcom"], false)

            // Publishers: one on, so both on.
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(page.enabledMap, { "genres/Fighting": true, "genres/Shmup": false,
                                       "makers/Capcom": true, "makers/U.S. Games": true })
            compare(appCore.values.menu_filters, { "genres/Fighting": true, "genres/Shmup": false,
                                                   "makers/Capcom": true, "makers/U.S. Games": true })

            var back = signalSpy.createObject(page, { target: page, signalName: "goBack" })
            keyClick(Qt.Key_Escape)
            compare(back.count, 1)
        }

        // A choice whose name is too long for its line is cut short, its ON
        // or OFF kept whole at the line's end.
        function test_multiSelectLongName() {
            appCore.schema = [{ key: "menu_filters", label: "Menu Filters", type: "multiselect_submenu",
                                options_source: "dynamic", options_slot: "menuFilterOptions" }]
            loader.setSource(Qt.resolvedUrl("../../views/MultiSelectSettings.qml"),
                             { navParams: { moduleId: "com.osdos.test", settingKey: "menu_filters",
                                            settingLabel: "Menu Filters" } })
            var page = loader.item
            verify(page !== null, "loaded MultiSelectSettings.qml")
            var name = "Konami Industry Company Limited of Japan and America"
            appCore.dynamicOptionsReady("com.osdos.test", "menu_filters", [
                { id: "makers/Konami", label: name, default: false }
            ])
            var label = findText(page, name)
            verify(label !== null)
            verify(label.width < label.implicitWidth)
            var value = findText(label.parent, "OFF")
            verify(value !== null)
            compare(value.width, value.implicitWidth)
            verify(value.mapToItem(label.parent, value.width, 0).x <= label.parent.width)
            verify(value.parent.width >= value.implicitWidth)
        }

        Component {
            id: signalSpy
            SignalSpy {}
        }
    }
}
