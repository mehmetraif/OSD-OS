import QtQuick
import QtTest

// A module's settings page, views/ModuleSettings.qml, against a stand-in for
// AppCore: rows offered only while another row has a value (manifest
// "shown_with", as the Arcade module's rows for its Arcade screen are), an
// unset one by its default, coming and going as that row changes with the
// cursor kept on it, and their values kept while they are hidden; a dynamic
// row's choices asked for as it comes; a toggle's value as ON or OFF, and a
// row whose own row is hidden hidden too; a slider and its tape bar.
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
    property var selector: null
    property var hints: ({ back: "[ESC]", navigate: "[▲▼]", arrows: "[▲▼◄►]", change: "[◄►]",
                           browse: "[►]", select: "[ENTER]", play_pause: "[SPACE]" })
    function selectorShown(item, shown) {}
    function skinIcon(url) { return url }

    // AppCore as the page uses it: the module's schema and saved values.
    QtObject {
        id: appCore
        property var values: ({})
        property var actions: []
        property var schema: []
        signal dynamicOptionsReady(string mid, string key, var items)
        signal moduleAuthStateChanged(string mid)
        function get_settings() { return { modules: { "com.osdos.test": values } } }
        function get_installed_modules() { return [{ id: "com.osdos.test", name: "Test" }] }
        function get_module_auth_state(id) { return "" }
        function invoke_module_action(id, slot) { actions = actions.concat([slot]) }
        function save_setting(module, key, value) {
            var v = Object.assign({}, values)
            v[key] = value
            values = v
        }
        function get_module_settings_schema(id) { return schema }
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
        }
        function cleanup() {
            loader.source = ""
        }

        function open() {
            loader.setSource(Qt.resolvedUrl("../../views/ModuleSettings.qml"),
                             { navParams: { moduleId: "com.osdos.test" } })
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
        // The row the cursor is on, by its key.
        function cursor(page) {
            for (var i = 0; i < page.children.length; i++) {
                var list = page.children[i]
                if (list.currentIndex !== undefined && list.model !== undefined)
                    return page.schemaItems[list.currentIndex].key
            }
            return ""
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
    }
}
