import QtQuick
import QtTest

// A module's settings page, views/ModuleSettings.qml, against a stand-in for
// AppCore: rows offered only while another row has a value (manifest
// "shown_with", as the Arcade module's rows for its Arcade screen are), an
// unset one by its default, coming and going as that row changes with the
// cursor kept on it, and their values kept while they are hidden.
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
        function get_module_settings_schema(id) {
            return [
                { key: "enabled", label: "Enabled", type: "toggle", default: "OFF" },
                { key: "screen", label: "Screen", type: "list_single", options: ["Original", "Arcade"],
                  default: "Original" },
                { key: "start_filter", label: "Start Filter", type: "list_single", options: ["All", "Favorites"],
                  default: "All", shown_with: { key: "screen", value: "Arcade" } },
                { key: "look", label: "Look", type: "submenu", shown_with: { key: "screen", value: "Arcade" },
                  settings: [{ key: "look_theme", label: "Theme", type: "list_single", options: ["Default"] }] },
                { key: "video", label: "Video", type: "list_single", options: ["Auto", "OpenGL"], default: "Auto" }
            ]
        }
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
    }

    TestCase {
        name: "ModuleSettings"
        when: windowShown

        function init() {
            appCore.values = {}
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
        // aren't offered. Arcade offers them, the cursor staying on Screen;
        // Original takes them away, what was set in them kept.
        function test_shownWith() {
            var page = open()
            compare(keys(page), ["enabled", "screen", "video"])
            keyClick(Qt.Key_Down)
            compare(cursor(page), "screen")
            keyClick(Qt.Key_Right)
            compare(appCore.values.screen, "Arcade")
            compare(keys(page), ["enabled", "screen", "start_filter", "look", "video"])
            compare(cursor(page), "screen")
            keyClick(Qt.Key_Down)
            compare(cursor(page), "start_filter")
            keyClick(Qt.Key_Right)
            compare(appCore.values.start_filter, "Favorites")
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Left)
            compare(appCore.values.screen, "Original")
            compare(keys(page), ["enabled", "screen", "video"])
            compare(cursor(page), "screen")
            compare(appCore.values.start_filter, "Favorites")
            // Saved as Arcade before the page opened: offered from the start.
            loader.source = ""
            appCore.values = { screen: "Arcade" }
            page = open()
            compare(keys(page), ["enabled", "screen", "start_filter", "look", "video"])
        }
    }
}
